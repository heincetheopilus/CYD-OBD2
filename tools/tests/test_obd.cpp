// Unit tests for the ELM327 parser and the asynchronous ObdLink state machine.
// Pure host test (no Arduino): see tools/tests/run_obd_tests.sh.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../../CarCYD/src/core/elm327_parser.h"
#include "../../CarCYD/src/core/loopback_transport.h"
#include "../../CarCYD/src/core/obd_link.h"
#include "../../CarCYD/src/core/ring_buffer.h"
#include "../../CarCYD/src/core/vehicle_state.h"

using namespace core;

static int g_checks = 0;
static int g_failed = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failed;                                                   \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (tol))

static void section(const char* name) { std::printf("[test] %s\n", name); }

// ---- Parser ------------------------------------------------------------------------------

static elm::Response parse(const char* text, const char* echo = nullptr) {
  elm::Response r;
  elm::parse(text, std::strlen(text), echo, r);
  return r;
}

static bool rpm_of(const elm::Response& r, float& rpm) {
  const uint8_t* d = nullptr;
  if (!elm::find_pid(r, 0x0C, d)) return false;
  rpm = elm::decode_pid(*elm::pid_info(0x0C), d);
  return true;
}

static void test_parser_basic() {
  section("parser: formats of a single PID reply");
  const char* variants[] = {"41 0C 1A F8 \r\r",
                            "410C1AF8\r\r",
                            "41 0C 1A F8",
                            "\r41 0C 1A F8\r\r",
                            "7E8 04 41 0C 1A F8\r",
                            "7E804410C1AF8\r",
                            "SEARCHING...\r41 0C 1A F8\r",
                            "BUS INIT: OK\r41 0C 1A F8\r",
                            "41 0c 1a f8\r"};
  for (const char* v : variants) {
    float rpm = 0;
    const elm::Response r = parse(v);
    CHECK(r.status == elm::Status::Data);
    CHECK(rpm_of(r, rpm));
    CHECK_NEAR(rpm, 1726.0f, 0.01);
  }
  float rpm = 0;
  elm::Response r = parse("010C\r41 0C 1A F8\r", "010C");
  CHECK(rpm_of(r, rpm));
  r = parse("010C1\r410C1AF8\r", "010C1");
  CHECK(rpm_of(r, rpm));
  r = parse("SEARCHING...\r41 0C 1A F8\r");
  CHECK(r.searching);
}

static void test_parser_errors() {
  section("parser: status lines");
  CHECK(parse("NO DATA\r").status == elm::Status::NoData);
  CHECK(parse("SEARCHING...\rUNABLE TO CONNECT\r").status == elm::Status::Unable);
  CHECK(parse("SEARCHING...\r").status == elm::Status::Empty);
  CHECK(parse("STOPPED\r").status == elm::Status::Stopped);
  CHECK(parse("BUS INIT: ...ERROR\r").status == elm::Status::BusInitError);
  CHECK(parse("BUS INIT: OK\r").status == elm::Status::Empty);
  CHECK(parse("CAN ERROR\r").status == elm::Status::Error);
  CHECK(parse("?\r").question);
  CHECK(parse("OK\r").status == elm::Status::Ok);
  CHECK(parse("ELM327 v1.5\r").status == elm::Status::Text);
  CHECK(parse("").status == elm::Status::Empty);
  CHECK(parse("\r\r\r").status == elm::Status::Empty);
  // Binary garbage and overlong input must not crash or overflow.
  char junk[1000];
  for (int i = 0; i < 1000; ++i) junk[i] = static_cast<char>((i * 37) & 0xFF);
  elm::Response r;
  elm::parse(junk, sizeof(junk), nullptr, r);
  char longhex[900];
  std::memset(longhex, 'A', sizeof(longhex));
  elm::parse(longhex, sizeof(longhex), nullptr, r);
  CHECK(r.length <= elm::kMaxBytes);
}

static void test_parser_combo_mask() {
  section("parser: combined PIDs, support masks, MIL, voltage, protocol");
  elm::Response r = parse("41 0C 1A F8 0D 3C\r");
  const uint8_t* d = nullptr;
  CHECK(elm::find_pid(r, 0x0C, d));
  CHECK(elm::find_pid(r, 0x0D, d));
  CHECK(d[0] == 0x3C);
  CHECK(!elm::find_pid(r, 0x05, d));
  r = parse("410C1AF80D3C\r");
  CHECK(elm::find_pid(r, 0x0D, d));
  r = parse("41 0C 1A\r");  // truncated
  CHECK(!elm::find_pid(r, 0x0C, d));

  uint32_t mask = 0;
  r = parse("41 00 BE 3E A8 13\r");
  CHECK(elm::parse_support_mask(r, 0x00, mask));
  CHECK(mask == 0xBE3EA813u);
  CHECK(elm::mask_has_pid(mask, 0x00, 0x0C));
  CHECK(elm::mask_has_pid(mask, 0x00, 0x0D));
  CHECK(!elm::mask_has_pid(mask, 0x00, 0x02));
  CHECK(elm::mask_has_pid(mask, 0x00, 0x20));

  bool mil = false;
  uint8_t n = 0;
  r = parse("41 01 83 07 E5 00\r");
  CHECK(elm::parse_mil(r, mil, n));
  CHECK(mil && n == 3);

  float v = 0;
  CHECK(elm::parse_voltage("ATRV\r12.4V\r", 11, v));
  CHECK_NEAR(v, 12.4f, 0.01);
  CHECK(!elm::parse_voltage("ELM327 v1.5\r", 12, v));

  char proto = 0;
  bool automatic = false;
  CHECK(elm::parse_protocol("A6\r\r", 4, proto, automatic) && proto == '6' && automatic);
  CHECK(elm::parse_protocol("ATDPN\r6\r", 8, proto, automatic) && proto == '6' && !automatic);

  const uint8_t temp[1] = {0x7B};
  CHECK_NEAR(elm::decode_pid(*elm::pid_info(0x05), temp), 83.0f, 0.01);
  const uint8_t load[1] = {0xFF};
  CHECK_NEAR(elm::decode_pid(*elm::pid_info(0x04), load), 100.0f, 0.01);
  const uint8_t volt[2] = {0x38, 0x70};
  CHECK_NEAR(elm::decode_pid(*elm::pid_info(0x42), volt), 14.448f, 0.01);
}

static void test_parser_dtc() {
  section("parser: DTC single/multi-frame, CAN and K-line");
  uint16_t codes[24];
  elm::Response r = parse("43 02 01 33 04 20 00\r");
  CHECK(elm::decode_dtcs(r, 3, true, codes, 24) == 2);
  CHECK(codes[0] == 0x0133 && codes[1] == 0x0420);

  r = parse("43 00 00 00 00 00 00\r");
  CHECK(elm::decode_dtcs(r, 3, true, codes, 24) == 0);

  r = parse("00A\r0: 43 04 01 33 01 34\r1: 02 35 C1 00 00 00 00\r");  // multi-frame with spaces
  CHECK(r.message_count == 1);
  uint8_t n = elm::decode_dtcs(r, 3, true, codes, 24);
  CHECK(n == 4);
  CHECK(codes[0] == 0x0133 && codes[2] == 0x0235 && codes[3] == 0xC100);
  r = parse("00A\r0:430401330134\r1:0235C1000000000\r");  // multi-frame without spaces
  CHECK(elm::decode_dtcs(r, 3, true, codes, 24) == 4);
  r = parse("43 01 01 33 00 00 00\r43 02 01 33 03 00 00\r");  // two ECUs, duplicates merged
  CHECK(r.message_count == 2);
  CHECK(elm::decode_dtcs(r, 3, true, codes, 24) == 2);
  r = parse("43 01 33 01 34 02 35\r43 03 00 00 00 00 00\r");  // K-line layout
  CHECK(elm::decode_dtcs(r, 3, false, codes, 24) == 4);
  r = parse("47 01 03 01 00 00 00\r");
  CHECK(elm::decode_dtcs(r, 7, true, codes, 24) == 1 && codes[0] == 0x0301);
  CHECK(elm::decode_dtcs(r, 3, true, codes, 24) == 0);
  CHECK(elm::is_clear_ack(parse("44\r")));
  CHECK(!elm::is_clear_ack(parse("NO DATA\r")));
}

static void test_ring() {
  section("ring buffer");
  RingBuffer<8> rb;
  for (int i = 0; i < 8; ++i) CHECK(rb.push(static_cast<uint8_t>(i)));
  CHECK(!rb.push(9));
  for (int i = 0; i < 8; ++i) CHECK(rb.pop() == i);
  CHECK(rb.pop() == -1);
  for (int round = 0; round < 100; ++round) {  // wrap-around
    CHECK(rb.push(static_cast<uint8_t>(round)));
    CHECK(rb.pop() == round);
  }
}

// ---- Link rig -------------------------------------------------------------------------------

struct Rig {
  LoopbackTransport tr;
  ObdLink link;
  VehicleState st;
  uint32_t t = 0;
  double max_poll_us = 0.0;

  explicit Rig(const ObdConfig& cfg = ObdConfig()) : link(tr, cfg) {
    tr.enable_emulator(true);
    tr.set_rpm(2500);
    tr.set_speed(60);
    tr.set_coolant(88);
    const uint8_t one[1] = {128};
    tr.set_pid(0x04, one, 1);
    tr.set_pid(0x11, one, 1);
    tr.set_pid(0x0F, one, 1);
    tr.set_pid(0x2F, one, 1);
    const uint8_t volt[2] = {0x38, 0x70};
    tr.set_pid(0x42, volt, 2);
    link.begin(st, 0);
  }

  // The virtual clock advances 1 ms per poll(); there is no sleeping anywhere.
  void run(uint32_t ms) {
    const uint32_t end = t + ms;
    while (t < end) {
      const auto a = std::chrono::steady_clock::now();
      link.poll(st, t);
      const auto b = std::chrono::steady_clock::now();
      const double us = std::chrono::duration<double, std::micro>(b - a).count();
      if (us > max_poll_us) max_poll_us = us;
      ++t;
    }
  }
  bool run_until_connected(uint32_t limit_ms) {
    const uint32_t end = t + limit_ms;
    while (t < end && st.link_state != LinkState::Connected) run(1);
    return st.link_state == LinkState::Connected;
  }
  bool sent_has(const char* needle) const { return std::strstr(tr.sent(), needle) != nullptr; }
};

static void test_normal_session() {
  section("link: normal CAN session, combined requests, rates");
  Rig rig;
  CHECK(rig.link.status() == ObdLink::Status::Searching || rig.link.status() == ObdLink::Status::Initializing);
  CHECK(rig.run_until_connected(5000));
  CHECK(rig.t < 3000);  // init incl. protocol search in the emulator takes ~2 s
  CHECK(rig.link.status() == ObdLink::Status::Connected);
  CHECK(rig.sent_has("ATZ") && rig.sent_has("ATE0") && rig.sent_has("ATST19") && rig.sent_has("ATSP0"));
  CHECK(rig.sent_has("ATDPN"));
  CHECK(rig.sent_has("ATSP6"));  // protocol locked after detection
  CHECK(rig.sent_has("0120") && rig.sent_has("0140"));

  rig.run(6000);
  const ObdLink::Stats& s = rig.link.stats();
  char line[256];
  rig.link.format_stats(line, sizeof(line));
  std::printf("  stats: %s\n", line);
  CHECK(s.combo);
  CHECK(s.protocol == '6');
  CHECK(rig.sent_has("010C0D1"));
  CHECK(s.rpm_hz >= 10.0f);
  CHECK(s.speed_hz >= 10.0f);
  CHECK(rig.st.fresh(SignalId::Rpm, rig.t) && rig.st.fresh(SignalId::Speed, rig.t));
  CHECK_NEAR(rig.st.get(SignalId::Rpm).value, 2500.0f, 0.5);
  CHECK_NEAR(rig.st.get(SignalId::Speed).value, 60.0f, 0.5);
  CHECK_NEAR(rig.st.get(SignalId::CoolantTemp).value, 88.0f, 0.5);
  CHECK_NEAR(rig.st.get(SignalId::EngineLoad).value, 50.2f, 0.5);
  CHECK_NEAR(rig.st.get(SignalId::BatteryVoltage).value, 14.448f, 0.01);
  CHECK(rig.st.fresh(SignalId::FuelLevel, rig.t));
  CHECK(rig.st.fresh(SignalId::BatteryVoltage, rig.t));
  CHECK(rig.st.dtc.mil_known);
  CHECK(!rig.st.get(SignalId::OilTemp).valid);  // never requested: stays "--"
  CHECK(!rig.st.get(SignalId::Maf).valid);
  CHECK(rig.max_poll_us < 2000.0);
  std::printf("  slowest poll() call: %.1f us\n", rig.max_poll_us);

  rig.tr.set_rpm(3100);
  rig.tr.set_speed(95);
  rig.run(300);
  CHECK_NEAR(rig.st.get(SignalId::Rpm).value, 3100.0f, 0.5);
  CHECK_NEAR(rig.st.get(SignalId::Speed).value, 95.0f, 0.5);
}

static void test_realtime_continuity() {
  section("link: RPM/speed never stale during a long session, update gap bound");
  Rig rig;
  CHECK(rig.run_until_connected(5000));
  rig.run(500);
  uint32_t stale = 0;
  uint32_t last_rpm_update = rig.st.get(SignalId::Rpm).updated_ms;
  uint32_t worst_gap = 0;
  for (int i = 0; i < 20000; ++i) {
    rig.run(1);
    if (!rig.st.fresh(SignalId::Rpm, rig.t) || !rig.st.fresh(SignalId::Speed, rig.t)) ++stale;
    const uint32_t u = rig.st.get(SignalId::Rpm).updated_ms;
    if (u != last_rpm_update) {
      const uint32_t gap = u - last_rpm_update;
      if (gap > worst_gap) worst_gap = gap;
      last_rpm_update = u;
    }
  }
  std::printf("  worst gap between RPM updates: %u ms\n", static_cast<unsigned>(worst_gap));
  CHECK(stale == 0);
  CHECK(worst_gap <= 200);  // 25 ms adapter latency + one secondary request in between
  CHECK(rig.link.stats().timeouts == 0);
}

static void test_combo_fallback() {
  section("link: adapter answers only the first PID of 010C0D -> separate requests");
  Rig rig;
  rig.tr.combo = LoopbackTransport::ComboMode::FirstOnly;
  CHECK(rig.run_until_connected(5000));
  rig.run(6000);
  CHECK(!rig.link.stats().combo);
  CHECK(rig.sent_has("010D1"));
  CHECK(rig.link.stats().rpm_hz >= 8.0f);
  CHECK(rig.link.stats().speed_hz >= 8.0f);
  CHECK(rig.st.fresh(SignalId::Speed, rig.t));
  CHECK_NEAR(rig.st.get(SignalId::Speed).value, 60.0f, 0.5);

  Rig rig2;
  rig2.tr.combo = LoopbackTransport::ComboMode::NoData;
  CHECK(rig2.run_until_connected(5000));
  rig2.run(4000);
  CHECK(!rig2.link.stats().combo);
  CHECK(rig2.st.fresh(SignalId::Rpm, rig2.t) && rig2.st.fresh(SignalId::Speed, rig2.t));
}

static void test_response_count_digit_rejected() {
  section("link: adapter rejects the response-count digit with '?'");
  Rig rig;
  rig.tr.accept_response_count = false;
  CHECK(rig.run_until_connected(5000));
  rig.run(4000);
  CHECK(rig.st.fresh(SignalId::Rpm, rig.t) && rig.st.fresh(SignalId::Speed, rig.t));
  CHECK(rig.link.stats().rpm_hz >= 8.0f);
}

static void test_unsupported_pids() {
  section("link: unsupported PIDs stay invalid, supported ones update");
  Rig rig;
  rig.tr.set_pid_supported(0x2F, false);  // no fuel level
  rig.tr.set_pid_supported(0x42, false);  // no PID 42 -> ATRV fallback
  CHECK(rig.run_until_connected(5000));
  rig.run(15000);
  CHECK(!rig.st.get(SignalId::FuelLevel).valid);
  CHECK(!rig.st.fresh(SignalId::FuelLevel, rig.t));
  CHECK(rig.st.fresh(SignalId::CoolantTemp, rig.t));
  CHECK(rig.st.fresh(SignalId::BatteryVoltage, rig.t));
  CHECK_NEAR(rig.st.get(SignalId::BatteryVoltage).value, 12.6f, 0.01);  // from ATRV
  CHECK(rig.sent_has("ATRV"));
  CHECK(rig.st.fresh(SignalId::Rpm, rig.t));
}

static void test_unknown_support_no_data() {
  section("link: PID flagged supported but answered NO DATA is switched off");
  Rig rig;
  CHECK(rig.run_until_connected(5000));
  rig.tr.set_pid_supported(0x11, false);  // the mask was read already; now it vanishes
  rig.run(20000);
  CHECK(rig.link.status() == ObdLink::Status::Connected);
  CHECK(!rig.st.fresh(SignalId::ThrottlePos, rig.t));
  CHECK(rig.st.fresh(SignalId::Rpm, rig.t));
  CHECK(rig.link.stats().no_data >= 3);
}

static void test_timeout_and_recovery() {
  section("link: adapter goes silent -> stale, re-init, recovery; never blocks");
  Rig rig;
  CHECK(rig.run_until_connected(5000));
  rig.run(1000);
  CHECK(rig.st.fresh(SignalId::Rpm, rig.t));
  rig.tr.silent = true;
  rig.run(1600);
  CHECK(!rig.st.fresh(SignalId::Rpm, rig.t));  // stale after ~1.5 s without data
  rig.run(20000);  // 8 timeouts, then ATZ fails 3 times with back-off -> transport reconnect
  CHECK(rig.st.link_state != LinkState::Connected);
  CHECK(rig.link.stats().timeouts > 0);
  CHECK(rig.link.stats().reinits > 0);
  rig.tr.silent = false;
  CHECK(rig.run_until_connected(30000));
  rig.run(1000);
  CHECK(rig.st.fresh(SignalId::Rpm, rig.t));
  CHECK(rig.tr.reconnect_calls >= 1);  // silent adapter -> transport asked to reconnect
  CHECK(rig.max_poll_us < 2000.0);
}

static void test_disconnect_reconnect() {
  section("link: transport drops and returns");
  Rig rig;
  CHECK(rig.run_until_connected(5000));
  rig.run(1000);
  rig.tr.set_connected(false);
  rig.run(5);
  CHECK(rig.st.link_state == LinkState::Connecting);
  CHECK(!rig.st.get(SignalId::Rpm).valid);
  CHECK(rig.link.status() == ObdLink::Status::Searching);
  rig.run(3000);
  CHECK(rig.st.link_state == LinkState::Connecting);
  rig.tr.set_connected(true);
  rig.tr.clear_sent();
  CHECK(rig.run_until_connected(5000));
  CHECK(rig.sent_has("ATSP6"));  // the locked protocol is reused: no new search
  CHECK(!rig.sent_has("ATSP0"));
  rig.run(1000);
  CHECK(rig.st.fresh(SignalId::Rpm, rig.t));
  for (int i = 0; i < 5; ++i) {  // flapping link
    rig.tr.set_connected(false);
    rig.run(50);
    rig.tr.set_connected(true);
    CHECK(rig.run_until_connected(6000));
  }
}

static void test_ecu_asleep() {
  section("link: adapter ok but ignition off (ECU silent), then wake-up");
  Rig rig;
  rig.tr.ecu_awake = false;
  rig.run(20000);
  CHECK(rig.st.link_state == LinkState::Error);
  CHECK(rig.link.status() == ObdLink::Status::NoEcu || rig.link.status() == ObdLink::Status::Initializing);
  CHECK(!rig.st.get(SignalId::Rpm).valid);
  rig.tr.ecu_awake = true;
  CHECK(rig.run_until_connected(30000));
  rig.run(500);
  CHECK(rig.st.fresh(SignalId::Rpm, rig.t));
}

static void test_ecu_goes_off_while_driving() {
  section("link: ignition switched off while connected (NO DATA everywhere)");
  Rig rig;
  CHECK(rig.run_until_connected(5000));
  rig.run(500);
  rig.tr.ecu_awake = false;
  rig.run(3000);
  CHECK(!rig.st.fresh(SignalId::Rpm, rig.t));
  CHECK(rig.st.link_state != LinkState::Connected);
  rig.tr.ecu_awake = true;
  CHECK(rig.run_until_connected(40000));
}

static void test_dtc() {
  section("link: DTC read (multi-frame), clear, safety");
  Rig rig;
  const uint16_t stored[] = {0x0133, 0x0301, 0x0302, 0x0420, 0xC100};
  const uint16_t pending[] = {0x0171};
  rig.tr.set_dtcs(3, stored, 5);
  rig.tr.set_dtcs(7, pending, 1);
  rig.tr.set_mil(true);
  CHECK(rig.run_until_connected(5000));
  rig.run(500);

  CHECK(rig.link.request_dtc_read(rig.st, rig.t));
  CHECK(rig.st.dtc.operation == DtcOperation::Reading);
  CHECK(!rig.link.request_dtc_read(rig.st, rig.t));  // busy
  rig.run(2000);
  CHECK(rig.st.dtc.operation == DtcOperation::Idle);
  CHECK(rig.st.dtc.last_result == DtcResult::ReadOk);
  CHECK(rig.st.dtc.count == 6);
  CHECK(rig.st.dtc.codes[0].code == 0x0133 && rig.st.dtc.codes[0].status == DtcStatus::Stored);
  CHECK(rig.st.dtc.codes[5].code == 0x0171 && rig.st.dtc.codes[5].status == DtcStatus::Pending);
  CHECK(rig.st.dtc.mil_known && rig.st.dtc.mil_on);
  CHECK(rig.st.dtc.scanned);
  CHECK(rig.link.phase() == ObdLink::Phase::Poll);

  CHECK(!rig.link.request_dtc_clear(rig.st, rig.t));  // refused while moving (60 km/h)
  CHECK(rig.st.dtc.last_result == DtcResult::Failed);
  rig.tr.set_speed(0);
  rig.run(400);
  CHECK(rig.link.request_dtc_clear(rig.st, rig.t));
  rig.run(1500);
  CHECK(rig.st.dtc.last_result == DtcResult::ClearOk);
  CHECK(rig.st.dtc.count == 0);
  CHECK(!rig.st.dtc.mil_on);
  CHECK(rig.st.fresh(SignalId::Rpm, rig.t));

  rig.tr.empty_dtc_is_no_data = true;  // empty list: ECU answers NO DATA
  CHECK(rig.link.request_dtc_read(rig.st, rig.t));
  rig.run(1500);
  CHECK(rig.st.dtc.last_result == DtcResult::ReadOk);
  CHECK(rig.st.dtc.count == 0);

  rig.tr.clear_works = false;  // clear refused by the ECU
  CHECK(rig.link.request_dtc_clear(rig.st, rig.t));
  rig.run(1500);
  CHECK(rig.st.dtc.last_result == DtcResult::Failed);
}

static void test_dtc_not_connected_and_timeout() {
  section("link: DTC without connection / with a silent adapter");
  Rig rig;
  CHECK(!rig.link.request_dtc_read(rig.st, 0));
  CHECK(rig.st.dtc.last_result == DtcResult::NotConnected);
  CHECK(rig.run_until_connected(5000));
  rig.run(300);
  rig.tr.silent = true;
  CHECK(rig.link.request_dtc_read(rig.st, rig.t));
  rig.run(6000);  // first timeout aborts the whole read
  CHECK(rig.st.dtc.operation == DtcOperation::Idle);
  CHECK(rig.st.dtc.last_result == DtcResult::Failed);
}

static void test_kline() {
  section("link: non-CAN protocol (no combined request, K-line DTC layout)");
  Rig rig;
  rig.tr.protocol = '3';
  const uint16_t stored[] = {0x0133, 0x0134, 0x0235, 0x0300};
  rig.tr.set_dtcs(3, stored, 4);
  CHECK(rig.run_until_connected(5000));
  rig.run(3000);
  CHECK(!rig.link.stats().combo);
  CHECK(rig.link.stats().protocol == '3');
  CHECK(rig.st.fresh(SignalId::Rpm, rig.t) && rig.st.fresh(SignalId::Speed, rig.t));
  CHECK(rig.link.request_dtc_read(rig.st, rig.t));
  rig.run(2000);
  CHECK(rig.st.dtc.last_result == DtcResult::ReadOk);
  CHECK(rig.st.dtc.count == 4);
}

static void test_slow_adapter() {
  section("link: slow adapter (120 ms per reply) still works, only slower");
  Rig rig;
  rig.tr.latency_ms = 120;
  CHECK(rig.run_until_connected(10000));
  rig.run(6000);
  CHECK(rig.st.fresh(SignalId::Rpm, rig.t));
  CHECK(rig.link.stats().timeouts == 0);
  char line[256];
  rig.link.format_stats(line, sizeof(line));
  std::printf("  slow adapter: %s\n", line);
}

static void test_end_and_restart() {
  section("link: end() releases stale limits");
  Rig rig;
  CHECK(rig.run_until_connected(5000));
  rig.run(500);
  CHECK(ObdLink::current() == &rig.link);
  rig.link.end(rig.st);
  CHECK(ObdLink::current() == nullptr);
  CHECK(rig.st.link_state == LinkState::Disconnected);
  CHECK(rig.link.status() == ObdLink::Status::Off);
  rig.st.set(SignalId::CoolantTemp, 80.0f, 0);
  CHECK(!rig.st.fresh(SignalId::CoolantTemp, 2000));  // default 1.5 s limit again
}

int main() {
  test_parser_basic();
  test_parser_errors();
  test_parser_combo_mask();
  test_parser_dtc();
  test_ring();
  test_normal_session();
  test_realtime_continuity();
  test_combo_fallback();
  test_response_count_digit_rejected();
  test_unsupported_pids();
  test_unknown_support_no_data();
  test_timeout_and_recovery();
  test_disconnect_reconnect();
  test_ecu_asleep();
  test_ecu_goes_off_while_driving();
  test_dtc();
  test_dtc_not_connected_and_timeout();
  test_kline();
  test_slow_adapter();
  test_end_and_restart();
  std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
