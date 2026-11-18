#include "obd_link.h"

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "util.h"

namespace core {
namespace {

const ObdLink* g_current = nullptr;

constexpr uint8_t kSlotPid[] = {0x0C, 0x0D, 0x05, 0x04, 0x11, 0x0F, 0x2F, 0x42, 0x01};
constexpr SignalId kSlowSignals[] = {SignalId::CoolantTemp, SignalId::EngineLoad, SignalId::ThrottlePos,
                                     SignalId::IntakeTemp,  SignalId::FuelLevel,  SignalId::BatteryVoltage};

constexpr uint32_t kAliveWindowMs = 3000;

}  // namespace

ObdLink::ObdLink(ByteTransport& transport, const ObdConfig& config) : transport_(transport), cfg_(config) {}

const ObdLink* ObdLink::current() { return g_current; }

// ---- DataLink ------------------------------------------------------------------------

void ObdLink::begin(VehicleState& state, uint32_t now_ms) {
  active_ = true;
  g_current = this;
  state.link_state = LinkState::Connecting;
  state.simulated = false;
  state.clock_valid = false;
  state.invalidate_all();
  apply_max_ages(state, true);

  q_head_ = q_count_ = 0;
  inflight_ = draining_ = false;
  rx_len_ = 0;
  phase_ = Phase::Reconnect;
  waiting_transport_ = true;
  no_ecu_ = false;
  init_failures_ = adapter_failures_ = 0;
  stats_ = Stats{};
  window_start_ = now_ms;
  rpm_count_ = speed_count_ = 0;
  protocol_ = locked_protocol_ = 0;
}

void ObdLink::end(VehicleState& state) {
  active_ = false;
  if (g_current == this) g_current = nullptr;
  abort_dtc_operation(state);
  apply_max_ages(state, false);
  phase_ = Phase::Idle;
  state.link_state = LinkState::Disconnected;
}

void ObdLink::apply_max_ages(VehicleState& state, bool enable) const {
  for (SignalId id : kSlowSignals) state.set_max_age(id, enable ? cfg_.slow_max_age_ms : 0);
}

ObdLink::Status ObdLink::status() const {
  if (!active_ || phase_ == Phase::Idle) return Status::Off;
  if (waiting_transport_) return transport_.idle() ? Status::Idle : Status::Searching;
  switch (phase_) {
    case Phase::Init: return Status::Initializing;
    case Phase::Reconnect: return no_ecu_ ? Status::NoEcu : Status::Searching;
    default: return Status::Connected;
  }
}

size_t ObdLink::format_stats(char* out, size_t len) const {
  const int n = snprintf(out, len,
                         "obd %s rpm %.1f Hz spd %.1f Hz lat %u/%u/%u ms (last/avg/max) req %lu ok %lu to %lu nd %lu "
                         "err %lu reinit %lu proto %c combo %d",
                         status() == Status::Connected ? "up" : "down", static_cast<double>(stats_.rpm_hz),
                         static_cast<double>(stats_.speed_hz), stats_.last_latency_ms, stats_.avg_latency_ms,
                         stats_.max_latency_ms, static_cast<unsigned long>(stats_.requests),
                         static_cast<unsigned long>(stats_.replies_ok), static_cast<unsigned long>(stats_.timeouts),
                         static_cast<unsigned long>(stats_.no_data), static_cast<unsigned long>(stats_.errors),
                         static_cast<unsigned long>(stats_.reinits), stats_.protocol != 0 ? stats_.protocol : '-',
                         stats_.combo ? 1 : 0);
  return n < 0 ? 0 : static_cast<size_t>(n);
}

// ---- Queue ----------------------------------------------------------------------------

bool ObdLink::push(const char* text, Kind kind, uint16_t timeout_ms, uint8_t arg) {
  if (q_count_ >= kQueueSize) return false;
  Cmd& c = queue_[(q_head_ + q_count_) % kQueueSize];
  snprintf(c.text, sizeof(c.text), "%s", text);
  c.kind = kind;
  c.timeout_ms = timeout_ms;
  c.arg = arg;
  ++q_count_;
  return true;
}

bool ObdLink::pop(Cmd& out) {
  if (q_count_ == 0) return false;
  out = queue_[q_head_];
  q_head_ = static_cast<uint8_t>((q_head_ + 1) % kQueueSize);
  --q_count_;
  return true;
}

void ObdLink::send(const Cmd& cmd, uint32_t now_ms) {
  char line[20];
  const int n = snprintf(line, sizeof(line), "%s\r", cmd.text);
  cur_ = cmd;
  rx_len_ = 0;
  inflight_ = true;
  sent_ms_ = now_ms;
  ++stats_.requests;
  transport_.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(n));
}

// ---- Scheduler ----------------------------------------------------------------------------

bool ObdLink::pid_supported(uint8_t pid) const {
  if (pid == 0) return true;
  const uint8_t range = static_cast<uint8_t>((pid - 1) / 32);
  if (range > 2) return false;
  switch (range_[range]) {
    case RangeState::Known: return elm::mask_has_pid(range_mask_[range], static_cast<uint8_t>(range * 32), pid);
    case RangeState::Empty: return false;
    default: return true;  // not asked / no answer: try it, NO DATA will switch it off
  }
}

bool ObdLink::slot_enabled(Slot slot) const { return slot_on_[slot]; }

void ObdLink::make_pid_command(Cmd& cmd, Slot slot) const {
  const char* digit = (cfg_.use_response_count && use_count_digit_) ? "1" : "";
  if (slot == kVolt && !voltage_via_pid_) {
    snprintf(cmd.text, sizeof(cmd.text), "ATRV");
    cmd.kind = Kind::Voltage;
  } else {
    snprintf(cmd.text, sizeof(cmd.text), "01%02X%s", kSlotPid[slot], digit);
    cmd.kind = slot == kMil ? Kind::Mil : Kind::Poll;
  }
  cmd.timeout_ms = cfg_.poll_timeout_ms;
  cmd.arg = slot;
}

ObdLink::Slot ObdLink::next_secondary() {
  for (uint8_t i = 0; i < kSlotCount; ++i) {
    secondary_index_ = static_cast<uint8_t>(secondary_index_ + 1);
    if (secondary_index_ >= kSlotCount || secondary_index_ < kCoolant) secondary_index_ = kCoolant;
    if (slot_enabled(static_cast<Slot>(secondary_index_))) return static_cast<Slot>(secondary_index_);
  }
  return kSlotCount;
}

bool ObdLink::next_poll(Cmd& cmd) {
  if (secondary_due_) {
    secondary_due_ = false;
    const Slot s = next_secondary();
    if (s != kSlotCount) {
      make_pid_command(cmd, s);
      return true;
    }
  }

  const bool rpm = slot_enabled(kRpm);
  const bool speed = slot_enabled(kSpeed);
  const bool combo = cfg_.try_combo && can_ && rpm && speed && combo_ != ComboState::No;
  bool end_of_cycle = false;

  if (combo) {
    const char* digit = (cfg_.use_response_count && use_count_digit_) ? "1" : "";
    snprintf(cmd.text, sizeof(cmd.text), "010C0D%s", digit);
    cmd.kind = Kind::Combo;
    cmd.timeout_ms = cfg_.poll_timeout_ms;
    cmd.arg = 0;
    end_of_cycle = true;
  } else if (rpm && speed) {
    make_pid_command(cmd, fast_step_ == 0 ? kRpm : kSpeed);
    end_of_cycle = fast_step_ == 1;
    fast_step_ ^= 1;
  } else if (rpm || speed) {
    make_pid_command(cmd, rpm ? kRpm : kSpeed);
    end_of_cycle = true;
  } else {
    const Slot s = next_secondary();
    if (s == kSlotCount) return false;
    make_pid_command(cmd, s);
    return true;
  }

  if (end_of_cycle && ++cycle_ % (cfg_.secondary_every == 0 ? 1 : cfg_.secondary_every) == 0) secondary_due_ = true;
  return true;
}

// ---- Poll -----------------------------------------------------------------------------------

void ObdLink::poll(VehicleState& state, uint32_t now_ms) {
  if (!active_) return;
  transport_.service(now_ms);

  if (!transport_.connected()) {
    if (!waiting_transport_) go_searching(state);
    return;
  }
  if (waiting_transport_) {
    waiting_transport_ = false;
    begin_init(now_ms);
  }

  pump_rx(state, now_ms);
  update_rates(now_ms);

  if (phase_ == Phase::Reconnect) {
    if (elapsed(now_ms, retry_since_) >= retry_delay_) begin_init(now_ms);
    return;
  }
  if (draining_ && elapsed(now_ms, drain_since_) >= cfg_.drain_ms) draining_ = false;
  if (inflight_) {
    if (elapsed(now_ms, sent_ms_) >= cur_.timeout_ms) on_timeout(state, now_ms);
    return;
  }
  if (draining_) return;

  Cmd cmd;
  if (pop(cmd)) {
    send(cmd, now_ms);
  } else if (phase_ == Phase::Init) {
    finish_init(state, now_ms);
  } else if (phase_ == Phase::Poll && next_poll(cmd)) {
    send(cmd, now_ms);
  }
}

void ObdLink::pump_rx(VehicleState& state, uint32_t now_ms) {
  for (int budget = 512; budget > 0; --budget) {
    const int c = transport_.read();
    if (c < 0) return;
    if (phase_ == Phase::Reconnect) continue;  // nothing is expected: discard
    if (c == '>') {
      if (inflight_) {
        on_reply(state, now_ms);
        if (phase_ == Phase::Reconnect) return;
      } else {
        draining_ = false;  // late reply of a command that timed out
        rx_len_ = 0;
      }
      continue;
    }
    if (c != 0 && rx_len_ < kRxSize) rx_[rx_len_++] = static_cast<char>(c);
  }
}

void ObdLink::update_rates(uint32_t now_ms) {
  const uint32_t window = elapsed(now_ms, window_start_);
  if (window < 1000) return;
  stats_.rpm_hz = static_cast<float>(rpm_count_) * 1000.0f / static_cast<float>(window);
  stats_.speed_hz = static_cast<float>(speed_count_) * 1000.0f / static_cast<float>(window);
  rpm_count_ = speed_count_ = 0;
  window_start_ = now_ms;
}

// ---- Replies --------------------------------------------------------------------------------

void ObdLink::on_reply(VehicleState& state, uint32_t now_ms) {
  inflight_ = false;
  const uint32_t latency = elapsed(now_ms, sent_ms_);
  stats_.last_latency_ms = static_cast<uint16_t>(latency > 0xFFFF ? 0xFFFF : latency);
  stats_.avg_latency_ms = stats_.avg_latency_ms == 0
                              ? stats_.last_latency_ms
                              : static_cast<uint16_t>((stats_.avg_latency_ms * 7 + stats_.last_latency_ms) / 8);
  if (stats_.last_latency_ms > stats_.max_latency_ms) stats_.max_latency_ms = stats_.last_latency_ms;
  consec_timeouts_ = 0;

  elm::Response r;
  elm::parse(rx_, rx_len_, cur_.text, r);

  // An adapter that rejects the "expected responses" digit gets plain requests from now on.
  const bool digit_command = cur_.kind == Kind::Poll || cur_.kind == Kind::Combo || cur_.kind == Kind::Mil;
  if (digit_command && r.question && use_count_digit_) {
    use_count_digit_ = false;
    LOG_W("OBD", "adapter rejects response-count digit, disabled");
    rx_len_ = 0;
    return;
  }

  switch (cur_.kind) {
    case Kind::Reset:
    case Kind::At:
      break;  // any answer means the adapter is alive
    case Kind::Probe:
      if (!handle_probe(r)) fail_init(state, now_ms, true);
      break;
    case Kind::Support: handle_support(r, cur_.arg); break;
    case Kind::Protocol: {
      char proto = 0;
      bool automatic = false;
      if (elm::parse_protocol(rx_, rx_len_, proto, automatic)) {
        protocol_ = proto;
        can_ = elm::is_can_protocol(proto);
        stats_.protocol = proto;
        if (locked_protocol_ != proto) {
          char cmd[8];
          snprintf(cmd, sizeof(cmd), "ATSP%c", proto);  // lock: no more protocol search
          push(cmd, Kind::At, cfg_.at_timeout_ms);
          locked_protocol_ = proto;
        }
      }
      break;
    }
    case Kind::Poll: handle_poll(state, now_ms, r, static_cast<Slot>(cur_.arg)); break;
    case Kind::Combo: handle_combo(state, now_ms, r); break;
    case Kind::Voltage: {
      float volts = 0.0f;
      if (elm::parse_voltage(rx_, rx_len_, volts)) {
        state.set(SignalId::BatteryVoltage, volts, now_ms);
        note_good(now_ms);
      }
      break;
    }
    case Kind::Mil: handle_mil(state, r); break;
    case Kind::Dtc: handle_dtc(state, now_ms, r); break;
    case Kind::Clear: handle_clear(state, now_ms, r); break;
  }
  rx_len_ = 0;
}

bool ObdLink::handle_probe(const elm::Response& r) {
  uint32_t mask = 0;
  if (r.status != elm::Status::Data || !elm::parse_support_mask(r, 0x00, mask)) return false;
  range_[0] = RangeState::Known;
  range_mask_[0] = mask;
  if ((mask & 1u) != 0) {
    push("0120", Kind::Support, cfg_.poll_timeout_ms * 2, 0x20);
  } else {
    range_[1] = range_[2] = RangeState::Empty;
  }
  if (locked_protocol_ != 0) {
    protocol_ = locked_protocol_;
    can_ = elm::is_can_protocol(protocol_);
    stats_.protocol = protocol_;
  } else {
    push("ATDPN", Kind::Protocol, cfg_.at_timeout_ms);
  }
  return true;
}

void ObdLink::handle_support(const elm::Response& r, uint8_t base) {
  uint32_t mask = 0;
  if (!elm::parse_support_mask(r, base, mask)) return;  // unknown range: pids are tried
  const uint8_t range = static_cast<uint8_t>(base / 32);
  range_[range] = RangeState::Known;
  range_mask_[range] = mask;
  if ((mask & 1u) != 0 && range == 1) {
    push("0140", Kind::Support, cfg_.poll_timeout_ms * 2, 0x40);
  } else if (range == 1) {
    range_[2] = RangeState::Empty;
  }
}

bool ObdLink::store_value(VehicleState& state, uint32_t now_ms, const elm::Response& r, uint8_t pid) {
  const uint8_t* data = nullptr;
  const elm::PidInfo* info = elm::pid_info(pid);
  if (info == nullptr || info->signal == SignalId::Count || !elm::find_pid(r, pid, data)) return false;
  state.set(info->signal, elm::decode_pid(*info, data), now_ms);
  if (pid == 0x0C) ++rpm_count_;
  if (pid == 0x0D) ++speed_count_;
  return true;
}

void ObdLink::note_good(uint32_t now_ms) {
  consec_bad_ = 0;
  last_good_ms_ = now_ms;
  ++stats_.replies_ok;
}

void ObdLink::note_bad(VehicleState& state, uint32_t now_ms) {
  if (++consec_bad_ < cfg_.lost_after) return;
  LOG_W("OBD", "no usable replies, re-initialising");
  fail_init(state, now_ms, consec_timeouts_ == 0);
}

void ObdLink::handle_poll(VehicleState& state, uint32_t now_ms, const elm::Response& r, Slot slot) {
  const uint8_t pid = kSlotPid[slot];
  if (r.status == elm::Status::Data && store_value(state, now_ms, r, pid)) {
    nodata_run_[slot] = 0;
    note_good(now_ms);
    return;
  }
  if (r.status == elm::Status::NoData) {
    ++stats_.no_data;
    const bool alive = last_good_ms_ != 0 && elapsed(now_ms, last_good_ms_) < kAliveWindowMs;
    if (++nodata_run_[slot] >= 3 && alive) {
      slot_on_[slot] = false;  // the ECU does not support it
      LOG_I("OBD", "PID %02X not supported by the ECU", pid);
    }
  } else {
    ++stats_.errors;
  }
  note_bad(state, now_ms);
}

void ObdLink::handle_combo(VehicleState& state, uint32_t now_ms, const elm::Response& r) {
  const bool rpm = r.status == elm::Status::Data && store_value(state, now_ms, r, 0x0C);
  const bool speed = r.status == elm::Status::Data && store_value(state, now_ms, r, 0x0D);
  if (rpm && speed) {
    if (combo_ != ComboState::Yes) LOG_I("OBD", "combined 010C0D requests active");
    combo_ = ComboState::Yes;
    stats_.combo = true;
    note_good(now_ms);
    return;
  }
  if (combo_ == ComboState::Yes) {  // it worked before: treat as an ordinary bad reply
    ++stats_.errors;
    note_bad(state, now_ms);
    return;
  }
  // Still probing: two misses -> separate requests for the rest of the session.
  if (++combo_fail_ >= 2) {
    combo_ = ComboState::No;
    stats_.combo = false;
    LOG_I("OBD", "adapter/ECU ignores combined requests, using separate 010C / 010D");
  }
  if (rpm || speed) note_good(now_ms);
}

void ObdLink::handle_mil(VehicleState& state, const elm::Response& r) {
  bool on = false;
  uint8_t count = 0;
  if (r.status != elm::Status::Data || !elm::parse_mil(r, on, count)) {
    return;  // a missing MIL PID alone must not trigger a re-init
  }
  if (!state.dtc.mil_known || state.dtc.mil_on != on) {
    state.dtc.mil_known = true;
    state.dtc.mil_on = on;
    state.dtc.revision++;
  }
  ++stats_.replies_ok;
}

// ---- Timeouts ---------------------------------------------------------------------------------

void ObdLink::on_timeout(VehicleState& state, uint32_t now_ms) {
  inflight_ = false;
  ++stats_.timeouts;
  ++consec_timeouts_;
  rx_len_ = 0;

  switch (phase_) {
    case Phase::Init:
      if (cur_.kind == Kind::Support) return;  // optional step, keep going
      fail_init(state, now_ms, cur_.kind == Kind::Probe);
      return;
    case Phase::DtcRead:
      if (cur_.kind == Kind::Dtc) {
        // Give up on the remaining modes: keep what was read, do not stall polling.
        q_head_ = q_count_ = 0;
        dtc_cmds_left_ = 0;
        finish_dtc_read(state, now_ms, dtc_mode3_ok_);
      }
      break;
    case Phase::DtcClear:
      state.dtc.operation = DtcOperation::Idle;
      state.dtc.last_result = DtcResult::Failed;
      state.dtc.revision++;
      phase_ = Phase::Poll;
      break;
    default:
      break;
  }
  draining_ = true;  // a late reply must not be taken for the next command's answer
  drain_since_ = now_ms;
  note_bad(state, now_ms);
}

// ---- Init / reconnect ------------------------------------------------------------------------------

void ObdLink::begin_init(uint32_t now_ms) {
  (void)now_ms;
  q_head_ = q_count_ = 0;
  inflight_ = draining_ = false;
  rx_len_ = 0;
  phase_ = Phase::Init;
  no_ecu_ = false;
  combo_ = ComboState::Unknown;
  combo_fail_ = 0;
  stats_.combo = false;
  use_count_digit_ = true;
  for (auto& r : range_) r = RangeState::Unknown;
  for (uint8_t i = 0; i < kSlotCount; ++i) {
    slot_on_[i] = false;
    nodata_run_[i] = 0;
  }
  consec_bad_ = 0;
  consec_timeouts_ = 0;
  fast_step_ = 0;
  cycle_ = 0;
  secondary_due_ = false;
  secondary_index_ = kRpm;

  push("ATZ", Kind::Reset, cfg_.reset_timeout_ms);
  push("ATE0", Kind::At, cfg_.at_timeout_ms);
  push("ATL0", Kind::At, cfg_.at_timeout_ms);
  push("ATS0", Kind::At, cfg_.at_timeout_ms);
  push("ATH0", Kind::At, cfg_.at_timeout_ms);
  char cmd[12];
  if (cfg_.st_hex != 0) {
    snprintf(cmd, sizeof(cmd), "ATST%02X", cfg_.st_hex);
    push(cmd, Kind::At, cfg_.at_timeout_ms);
  }
  if (cfg_.adaptive_timing != 0) {
    snprintf(cmd, sizeof(cmd), "ATAT%u", cfg_.adaptive_timing);
    push(cmd, Kind::At, cfg_.at_timeout_ms);
  }
  if (locked_protocol_ != 0) {
    snprintf(cmd, sizeof(cmd), "ATSP%c", locked_protocol_);
  } else {
    snprintf(cmd, sizeof(cmd), "ATSP0");
  }
  push(cmd, Kind::At, cfg_.at_timeout_ms);
  push("0100", Kind::Probe, cfg_.probe_timeout_ms);
}

void ObdLink::finish_init(VehicleState& state, uint32_t now_ms) {
  for (uint8_t s = 0; s < kSlotCount; ++s) slot_on_[s] = pid_supported(kSlotPid[s]);
  slot_on_[kMil] = true;  // PID 01 is mandatory
  slot_on_[kVolt] = true;  // PID 42 or ATRV
  voltage_via_pid_ = range_[2] == RangeState::Known && pid_supported(0x42);

  phase_ = Phase::Poll;
  no_ecu_ = false;
  init_failures_ = adapter_failures_ = 0;
  consec_bad_ = 0;
  last_good_ms_ = now_ms;
  window_start_ = now_ms;
  rpm_count_ = speed_count_ = 0;
  state.link_state = LinkState::Connected;
  LOG_I("OBD", "ready, protocol %c, %s", protocol_ != 0 ? protocol_ : '?', can_ ? "CAN" : "K-line");
}

void ObdLink::fail_init(VehicleState& state, uint32_t now_ms, bool ecu_level) {
  abort_dtc_operation(state);
  q_head_ = q_count_ = 0;
  inflight_ = draining_ = false;
  rx_len_ = 0;
  state.invalidate_all();
  state.link_state = ecu_level ? LinkState::Error : LinkState::Connecting;

  ++stats_.reinits;
  uint32_t delay = cfg_.backoff_min_ms << (init_failures_ < 3 ? init_failures_ : 3);
  if (delay > cfg_.backoff_max_ms) delay = cfg_.backoff_max_ms;
  if (init_failures_ < 250) ++init_failures_;
  retry_since_ = now_ms;
  retry_delay_ = delay;
  phase_ = Phase::Reconnect;
  no_ecu_ = ecu_level;
  stats_.combo = false;

  if (ecu_level) {
    adapter_failures_ = 0;
    locked_protocol_ = 0;  // the saved protocol may be wrong for this vehicle: search again
  } else if (++adapter_failures_ >= 3) {
    adapter_failures_ = 0;
    LOG_W("OBD", "adapter silent, asking the transport to reconnect");
    transport_.reconnect();
  }
  LOG_W("OBD", "init failed (%s), retry in %lu ms", ecu_level ? "no ECU answer" : "adapter silent",
        static_cast<unsigned long>(delay));
}

void ObdLink::go_searching(VehicleState& state) {
  abort_dtc_operation(state);
  q_head_ = q_count_ = 0;
  inflight_ = draining_ = false;
  rx_len_ = 0;
  phase_ = Phase::Reconnect;
  waiting_transport_ = true;
  no_ecu_ = false;
  state.invalidate_all();
  state.link_state = LinkState::Connecting;
  stats_.combo = false;
  LOG_I("OBD", "transport down, waiting for the adapter");
}

// ---- DTC ----------------------------------------------------------------------------------------------

bool ObdLink::request_dtc_read(VehicleState& state, uint32_t now_ms) {
  (void)now_ms;
  if (phase_ == Phase::DtcRead || phase_ == Phase::DtcClear) return false;
  if (!active_ || phase_ != Phase::Poll) {
    state.dtc.last_result = DtcResult::NotConnected;
    state.dtc.revision++;
    return false;
  }
  phase_ = Phase::DtcRead;
  state.dtc.operation = DtcOperation::Reading;
  state.dtc.revision++;
  dtc_tmp_count_ = 0;
  dtc_mode3_ok_ = false;
  dtc_cmds_left_ = 3;

  Cmd mil;
  make_pid_command(mil, kMil);
  push(mil.text, Kind::Mil, cfg_.poll_timeout_ms, kMil);
  push("03", Kind::Dtc, cfg_.dtc_timeout_ms, 0x03);
  push("07", Kind::Dtc, cfg_.dtc_timeout_ms, 0x07);
  push("0A", Kind::Dtc, cfg_.dtc_timeout_ms, 0x0A);
  return true;
}

bool ObdLink::request_dtc_clear(VehicleState& state, uint32_t now_ms) {
  if (phase_ == Phase::DtcRead || phase_ == Phase::DtcClear) return false;
  if (!active_ || phase_ != Phase::Poll) {
    state.dtc.last_result = DtcResult::NotConnected;
    state.dtc.revision++;
    return false;
  }
  // Never clear while driving.
  if (state.fresh(SignalId::Speed, now_ms) && state.get(SignalId::Speed).value > cfg_.clear_max_speed_kmh) {
    state.dtc.last_result = DtcResult::Failed;
    state.dtc.revision++;
    return false;
  }
  phase_ = Phase::DtcClear;
  state.dtc.operation = DtcOperation::Clearing;
  state.dtc.revision++;
  push("04", Kind::Clear, cfg_.dtc_timeout_ms);
  return true;
}

void ObdLink::handle_dtc(VehicleState& state, uint32_t now_ms, const elm::Response& r) {
  const uint8_t mode = cur_.arg;
  const bool replied = r.status == elm::Status::NoData ||
                       (r.status == elm::Status::Data && [&] {
                         for (uint8_t i = 0; i < r.length; ++i) {
                           if (r.bytes[i] == (mode | 0x40)) return true;
                         }
                         return false;
                       }());
  if (r.status == elm::Status::Data) {
    uint16_t codes[DtcState::kMaxCodes];
    const uint8_t n = elm::decode_dtcs(r, mode, can_, codes, DtcState::kMaxCodes);
    const DtcStatus status = mode == 0x03 ? DtcStatus::Stored : (mode == 0x07 ? DtcStatus::Pending : DtcStatus::Permanent);
    for (uint8_t i = 0; i < n && dtc_tmp_count_ < DtcState::kMaxCodes; ++i) dtc_tmp_[dtc_tmp_count_++] = Dtc{codes[i], status};
  }
  if (mode == 0x03 && replied) dtc_mode3_ok_ = true;
  if (replied) {
    note_good(now_ms);
  } else {
    ++stats_.errors;
    note_bad(state, now_ms);
    if (phase_ != Phase::DtcRead) return;  // re-init aborted the operation
  }
  if (dtc_cmds_left_ > 0 && --dtc_cmds_left_ == 0) finish_dtc_read(state, now_ms, dtc_mode3_ok_);
}

void ObdLink::finish_dtc_read(VehicleState& state, uint32_t now_ms, bool ok) {
  DtcState& d = state.dtc;
  if (ok) {
    d.count = dtc_tmp_count_;
    for (uint8_t i = 0; i < dtc_tmp_count_; ++i) d.codes[i] = dtc_tmp_[i];
    d.scanned = true;
    d.last_scan_ms = now_ms;
    d.last_result = DtcResult::ReadOk;
  } else {
    d.last_result = DtcResult::Failed;
  }
  d.operation = DtcOperation::Idle;
  d.revision++;
  phase_ = Phase::Poll;
}

void ObdLink::handle_clear(VehicleState& state, uint32_t now_ms, const elm::Response& r) {
  DtcState& d = state.dtc;
  const bool ok = r.status == elm::Status::Data && elm::is_clear_ack(r);
  if (ok) {
    d.count = 0;
    d.mil_known = true;
    d.mil_on = false;
    d.last_result = DtcResult::ClearOk;
    note_good(now_ms);
    Cmd mil;
    make_pid_command(mil, kMil);
    push(mil.text, Kind::Mil, cfg_.poll_timeout_ms, kMil);  // confirm the MIL state
  } else {
    d.last_result = DtcResult::Failed;
  }
  d.operation = DtcOperation::Idle;
  d.revision++;
  phase_ = Phase::Poll;
}

void ObdLink::abort_dtc_operation(VehicleState& state) {
  if (state.dtc.operation == DtcOperation::Idle) return;
  state.dtc.operation = DtcOperation::Idle;
  state.dtc.last_result = DtcResult::Failed;
  state.dtc.revision++;
}

}  // namespace core
