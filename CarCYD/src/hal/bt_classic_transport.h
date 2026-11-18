/**
 * @file bt_classic_transport.h
 * Bluetooth Classic (SPP) transport for ELM327 adapters.
 *
 * BluetoothSerial::connect()/discover() block for seconds, so they run in a
 * small connector task on core 0 (PIN 1234 / 0000 tried alternately).
 *
 * Connection policy (heap is tight and every failed attempt costs memory inside the stack):
 *  - automatic attempts only with a saved MAC (connect by address, no inquiry); at most
 *    kMaxAttempts per round with a growing pause, then the transport goes idle()
 *    ("Not connected - tap Connect") until the user acts (reconnect()/connect_now(),
 *    a new target) - no endless retry;
 *  - an inquiry (scan) runs only when the user asks for it (start_scan()); the stack's
 *    result list is freed right after it was copied;
 *  - "Connect" without a saved MAC makes one by-name attempt (the stack runs an inquiry). The main loop only touches
 * non-blocking calls: service() drains the SPP queue into a ring buffer,
 * read()/available() serve from that ring, write() hands a few bytes to the
 * stack.
 *
 * TX is queued in a ring and sent by the connector task, so write() never waits
 * on the radio. Limitation: BluetoothSerial::connect()/discover() cannot be
 * aborted; a target change or scan request made during a connect attempt takes
 * effect when that call returns (bounded by the stack's own timeout, ~5-10 s).
 *
 * Also implements core::AdapterScanner (Bluetooth inquiry) for the settings page.
 */
#pragma once

#include <stdint.h>

#include "../core/adapter_scanner.h"
#include "../core/byte_transport.h"
#include "../core/ring_buffer.h"

namespace hal {

class BtClassicTransport final : public core::ByteTransport, public core::AdapterScanner {
 public:
  /** Starts the connector task (idempotent). The radio stays off until set_enabled(true). */
  void begin();

  /** Radio on (connect/reconnect) or idle (disconnect and stay quiet). */
  void set_enabled(bool enabled);

  /**
   * Adapter to connect to: by MAC when `use_mac`, otherwise by name (empty =
   * "OBDII"). A changed target drops the current connection.
   */
  void set_target(const char* name, const uint8_t mac[6], bool use_mac);

  // core::ByteTransport
  bool connected() override { return up_; }
  size_t write(const uint8_t* data, size_t len) override;
  int available() override { return static_cast<int>(rx_.size()); }
  int read() override { return rx_.pop(); }
  void service(uint32_t now_ms) override;
  void reconnect() override;
  bool idle() const override { return idle_; }

  // core::AdapterScanner
  bool start_scan() override;
  bool scanning() const override { return scanning_; }
  uint32_t revision() const override { return scan_revision_; }
  uint8_t count() const override { return scan_count_; }
  bool get(uint8_t index, core::AdapterInfo& out) const override;
  void connect_now() override { reconnect(); }

 private:
  static constexpr uint8_t kMaxResults = 12;

  static void task_entry(void* self);
  void task_main();
  void run_scan();
  bool attempt_connect();
  void wait_interruptible(uint32_t ms);
  void begin_round(bool by_user);

  core::RingBuffer<1024> rx_;
  core::RingBuffer<256> tx_;  // main loop -> connector task (guarded by the critical section)
  void flush_tx();
  bool up_ = false;  // main-loop view, refreshed by service()

  // Shared with the connector task (word sized flags, target guarded by a critical section).
  volatile bool enabled_ = false;
  volatile bool reconnect_requested_ = false;
  volatile bool target_changed_ = false;
  volatile bool scan_requested_ = false;
  volatile bool scanning_ = false;
  volatile bool started_ = false;
  volatile bool idle_ = false;  ///< gave up, waiting for the user
  volatile uint32_t scan_revision_ = 0;
  volatile uint8_t scan_count_ = 0;

  char target_name_[24] = {};
  uint8_t target_mac_[6] = {};
  bool target_use_mac_ = false;

  core::AdapterInfo results_[kMaxResults] = {};
  uint8_t attempt_ = 0;        // PIN alternation
  uint8_t attempts_left_ = 0;  // connector task only
  uint8_t failures_ = 0;
  bool allow_name_ = false;    // by-name connect (inquiry) only when the user asked
  bool was_connected_ = false;
};

}  // namespace hal
