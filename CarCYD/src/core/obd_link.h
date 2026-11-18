/**
 * @file obd_link.h
 * Real vehicle data source: ELM327 adapter driven by an asynchronous,
 * non-blocking state machine (Init -> Poll -> DtcRead/DtcClear -> Reconnect).
 *
 * poll() never waits: it drains the transport, advances the machine, sends at
 * most one command and returns. All timeouts are measured with the `now_ms`
 * passed in by the caller (platform millis()).
 *
 * Scheduling: RPM and speed are requested continuously (one combined "010C0D"
 * request when the adapter/ECU supports it, otherwise two requests); the other
 * signals take turns, one request every `secondary_every` fast cycles.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "byte_transport.h"
#include "data_link.h"
#include "elm327_parser.h"

namespace core {

struct ObdConfig {
  uint8_t st_hex = 0x19;             ///< ATST value (x4 ms adapter timeout); 0 = leave default
  uint8_t adaptive_timing = 1;       ///< ATAT1 / ATAT2 (2 = aggressive); 0 = leave default
  bool use_response_count = true;    ///< append the "expected responses" digit ("010C1")
  bool try_combo = true;             ///< try "010C0D" on CAN, fall back to separate requests
  uint8_t secondary_every = 3;       ///< one secondary request per this many fast cycles
  uint16_t reset_timeout_ms = 3500;  ///< ATZ
  uint16_t at_timeout_ms = 1500;     ///< other AT commands
  uint16_t probe_timeout_ms = 12000; ///< first 0100 (protocol search can take seconds)
  uint16_t poll_timeout_ms = 500;    ///< a PID request
  uint16_t dtc_timeout_ms = 3000;    ///< DTC read / clear
  uint16_t drain_ms = 150;           ///< quiet time after a timeout to swallow a late reply
  uint8_t lost_after = 8;            ///< consecutive bad replies before re-initialising
  uint16_t slow_max_age_ms = 8000;   ///< stale limit of the round-robin signals
  uint32_t backoff_min_ms = 1000;
  uint32_t backoff_max_ms = 8000;
  uint8_t clear_max_speed_kmh = 3;   ///< DTC clear is refused above this speed
};

class ObdLink final : public DataLink {
 public:
  enum class Phase : uint8_t { Idle, Init, Poll, DtcRead, DtcClear, Reconnect };

  /** Coarse state for the UI. */
  enum class Status : uint8_t { Off, Searching, Initializing, NoEcu, Connected, Idle };  // Idle: transport gave up, waits for the user

  struct Stats {
    uint32_t requests = 0;
    uint32_t replies_ok = 0;
    uint32_t timeouts = 0;
    uint32_t no_data = 0;
    uint32_t errors = 0;
    uint32_t reinits = 0;
    uint16_t last_latency_ms = 0;
    uint16_t avg_latency_ms = 0;
    uint16_t max_latency_ms = 0;
    float rpm_hz = 0.0f;    ///< measured refresh rate of the RPM signal
    float speed_hz = 0.0f;  ///< measured refresh rate of the speed signal
    char protocol = 0;      ///< ELM protocol number ('6' = ISO 15765-4 CAN 11/500)
    bool combo = false;     ///< combined 010C0D requests active
  };

  explicit ObdLink(ByteTransport& transport, const ObdConfig& config = ObdConfig());

  const char* name() const override { return "obd"; }
  void begin(VehicleState& state, uint32_t now_ms) override;
  void end(VehicleState& state) override;
  void poll(VehicleState& state, uint32_t now_ms) override;
  bool request_dtc_read(VehicleState& state, uint32_t now_ms) override;
  bool request_dtc_clear(VehicleState& state, uint32_t now_ms) override;

  Phase phase() const { return phase_; }
  Status status() const;
  const Stats& stats() const { return stats_; }

  /** One line for the serial log: rates, latency, counters. */
  size_t format_stats(char* out, size_t len) const;

  /** The link currently active (for the UI), or null. */
  static const ObdLink* current();

 private:
  enum class Kind : uint8_t { Reset, At, Probe, Support, Protocol, Poll, Combo, Voltage, Mil, Dtc, Clear };

  struct Cmd {
    char text[16];
    uint16_t timeout_ms;
    Kind kind;
    uint8_t arg;  ///< slot / PID base / DTC mode
  };

  // Polled signals. Order: the two fast ones first, the rest round-robin.
  enum Slot : uint8_t { kRpm, kSpeed, kCoolant, kLoad, kThrottle, kIntake, kFuel, kVolt, kMil, kSlotCount };

  enum class RangeState : uint8_t { Unknown, Known, Empty };

  static constexpr uint8_t kQueueSize = 16;
  static constexpr size_t kRxSize = 384;

  // Queue / transmit
  bool push(const char* text, Kind kind, uint16_t timeout_ms, uint8_t arg = 0);
  bool pop(Cmd& out);
  void send(const Cmd& cmd, uint32_t now_ms);
  void make_pid_command(Cmd& cmd, Slot slot) const;
  bool next_poll(Cmd& cmd);
  Slot next_secondary();
  bool slot_enabled(Slot slot) const;
  bool pid_supported(uint8_t pid) const;

  // Receive / state machine
  void pump_rx(VehicleState& state, uint32_t now_ms);
  void on_reply(VehicleState& state, uint32_t now_ms);
  void on_timeout(VehicleState& state, uint32_t now_ms);
  void handle_poll(VehicleState& state, uint32_t now_ms, const elm::Response& r, Slot slot);
  void handle_combo(VehicleState& state, uint32_t now_ms, const elm::Response& r);
  bool handle_probe(const elm::Response& r);
  void handle_support(const elm::Response& r, uint8_t base);
  void handle_mil(VehicleState& state, const elm::Response& r);
  void handle_dtc(VehicleState& state, uint32_t now_ms, const elm::Response& r);
  void handle_clear(VehicleState& state, uint32_t now_ms, const elm::Response& r);
  void note_bad(VehicleState& state, uint32_t now_ms);
  void note_good(uint32_t now_ms);
  bool store_value(VehicleState& state, uint32_t now_ms, const elm::Response& r, uint8_t pid);

  void go_searching(VehicleState& state);
  void begin_init(uint32_t now_ms);
  void finish_init(VehicleState& state, uint32_t now_ms);
  void fail_init(VehicleState& state, uint32_t now_ms, bool ecu_level);
  void finish_dtc_read(VehicleState& state, uint32_t now_ms, bool ok);
  void abort_dtc_operation(VehicleState& state);
  void update_rates(uint32_t now_ms);
  void apply_max_ages(VehicleState& state, bool enable) const;

  ByteTransport& transport_;
  ObdConfig cfg_;

  Phase phase_ = Phase::Idle;
  bool active_ = false;
  bool waiting_transport_ = true;
  bool no_ecu_ = false;

  // Command queue and the command in flight
  Cmd queue_[kQueueSize] = {};
  uint8_t q_head_ = 0;
  uint8_t q_count_ = 0;
  bool inflight_ = false;
  Cmd cur_ = {};
  uint32_t sent_ms_ = 0;
  bool draining_ = false;
  uint32_t drain_since_ = 0;

  char rx_[kRxSize] = {};
  size_t rx_len_ = 0;

  // Re-init back-off
  uint32_t retry_since_ = 0;
  uint32_t retry_delay_ = 0;
  uint8_t init_failures_ = 0;
  uint8_t adapter_failures_ = 0;

  // Session knowledge
  char protocol_ = 0;
  char locked_protocol_ = 0;  ///< kept across re-inits to skip the protocol search
  bool can_ = true;
  bool use_count_digit_ = true;
  enum class ComboState : uint8_t { Unknown, Yes, No } combo_ = ComboState::Unknown;
  uint8_t combo_fail_ = 0;
  RangeState range_[3] = {};
  uint32_t range_mask_[3] = {};
  bool slot_on_[kSlotCount] = {};
  uint8_t nodata_run_[kSlotCount] = {};
  bool voltage_via_pid_ = false;

  // Scheduler
  uint8_t fast_step_ = 0;
  uint32_t cycle_ = 0;
  bool secondary_due_ = false;
  uint8_t secondary_index_ = 0;

  // Health
  uint8_t consec_bad_ = 0;
  uint8_t consec_timeouts_ = 0;
  uint32_t last_good_ms_ = 0;

  // DTC operation
  Dtc dtc_tmp_[DtcState::kMaxCodes] = {};
  uint8_t dtc_tmp_count_ = 0;
  bool dtc_mode3_ok_ = false;
  uint8_t dtc_cmds_left_ = 0;

  // Statistics
  Stats stats_;
  uint32_t window_start_ = 0;
  uint16_t rpm_count_ = 0;
  uint16_t speed_count_ = 0;
};

}  // namespace core
