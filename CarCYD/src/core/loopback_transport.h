/**
 * @file loopback_transport.h
 * In-memory transport for the PC simulator and the unit tests.
 *
 * Two uses:
 *  - raw pipe: push_rx() injects bytes towards the link, sent() returns what the
 *    link wrote;
 *  - ELM327 emulator (enable_emulator): answers commands like a real adapter, with
 *    configurable latency, echo/spaces/headers, multi-frame DTC replies and
 *    fault injection (silence, ECU asleep, link drop).
 *
 * The emulator is driven by service(now_ms), which ObdLink::poll() calls, so it
 * follows the (virtual) clock of the caller and never sleeps.
 */
#pragma once

#include "byte_transport.h"
#include "ring_buffer.h"

namespace core {

class LoopbackTransport final : public ByteTransport {
 public:
  enum class ComboMode : uint8_t { Full, FirstOnly, NoData };

  // ---- ByteTransport ----
  bool connected() override { return connected_; }
  size_t write(const uint8_t* data, size_t len) override;
  int available() override { return static_cast<int>(rx_.size()); }
  int read() override { return rx_.pop(); }
  void service(uint32_t now_ms) override;
  void reconnect() override { ++reconnect_calls; }

  // ---- Raw pipe ----
  void set_connected(bool up);
  void push_rx(const char* text);
  const char* sent() const { return sent_; }
  void clear_sent() { sent_len_ = 0; sent_[0] = 0; }

  // ---- Emulator ----
  void enable_emulator(bool on) { emulate_ = on; }

  void set_pid(uint8_t pid, const uint8_t* data, uint8_t len);  ///< also marks it supported
  void set_pid_supported(uint8_t pid, bool supported);
  void set_rpm(float rpm);
  void set_speed(float kmh);
  void set_coolant(float celsius);

  /** DTC list of mode 3, 7 or 0x0A. */
  void set_dtcs(uint8_t mode, const uint16_t* codes, uint8_t count);
  void set_mil(bool on) { mil_ = on; }

  // Behaviour knobs (public on purpose: test fixtures poke them directly).
  uint32_t latency_ms = 25;         ///< delay of a normal reply
  uint32_t reset_latency_ms = 400;  ///< delay of ATZ
  uint32_t search_latency_ms = 800; ///< delay of the first 0100
  bool silent = false;              ///< swallow every command (timeouts)
  bool ecu_awake = true;            ///< false: UNABLE TO CONNECT / NO DATA like ignition off
  bool echo = true;                 ///< ATE state (adapters start with echo on)
  bool spaces = true;               ///< ATS state
  bool headers = false;             ///< ATH state
  bool searching_banner = true;     ///< first 0100 prints "SEARCHING..."
  bool accept_response_count = true;///< false: "010C1" answered with "?"
  bool empty_dtc_is_no_data = false;
  bool clear_works = true;
  ComboMode combo = ComboMode::Full;
  char protocol = '6';

  // Observation
  uint32_t commands_received = 0;
  uint32_t reconnect_calls = 0;

 private:
  void handle_line(const char* cmd);
  void reply(const char* body, uint32_t delay_ms, const char* cmd);
  void reply_pids(const uint8_t* pids, uint8_t count, const char* cmd);
  void reply_dtc(uint8_t mode, const char* cmd);
  void append_frame(char* out, size_t cap, const uint8_t* payload, uint8_t len) const;
  uint32_t support_mask(uint8_t base) const;

  bool connected_ = true;
  bool emulate_ = false;
  bool first_probe_ = true;
  bool protocol_locked_ = false;
  bool mil_ = false;
  uint32_t now_ = 0;

  RingBuffer<1024> rx_;

  char sent_[256] = {};
  size_t sent_len_ = 0;

  char line_[32] = {};
  size_t line_len_ = 0;

  char pending_[640] = {};
  size_t pending_len_ = 0;
  bool pending_valid_ = false;
  uint32_t pending_due_ = 0;

  uint8_t pid_data_[0x61][4] = {};
  uint8_t pid_len_[0x61] = {};
  bool pid_supported_[0x61] = {};

  uint16_t dtc_[3][12] = {};
  uint8_t dtc_count_[3] = {};
};

}  // namespace core
