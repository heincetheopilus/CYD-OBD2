#include "bt_classic_transport.h"

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "../core/log.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED) || !defined(CONFIG_BT_SPP_ENABLED)
#error "Bluetooth Classic (SPP) is not enabled for this board/core: use esp32dev with Arduino-ESP32 3.x"
#endif

namespace hal {
namespace {

constexpr char kLocalName[] = "CarCYD";
constexpr char kDefaultAdapterName[] = "OBDII";
constexpr uint8_t kMaxAttempts = 3;                      // per round, then idle until the user acts
constexpr uint32_t kBackoffMs[kMaxAttempts - 1] = {2000, 6000};  // pause after attempt n (growing)
constexpr uint32_t kScanMs = 8000;
constexpr uint32_t kTaskStack = 6144;  // measured use ~2 KB while connecting (see log_heap)
// Bluedroid + controller (Classic only) need roughly this much internal heap; below it the stack
// would hit a NULL malloc inside its own task and panic, so the start is deferred instead.
constexpr size_t kMinHeapForBt = 70 * 1024;
constexpr size_t kMinBlockForBt = 32 * 1024;

BluetoothSerial g_bt;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool g_radio_started = false;
bool g_low_mem_logged = false;

void log_heap(const char* what);

}  // namespace

// ---- Main-loop side (all non-blocking) -----------------------------------------------------

void BtClassicTransport::begin() {
  if (started_) return;
  started_ = true;
  // Core 0 hosts the Bluetooth stack; the Arduino loop runs on core 1.
  xTaskCreatePinnedToCore(task_entry, "bt_link", kTaskStack, this, 1, nullptr, 0);
}

void BtClassicTransport::set_enabled(bool enabled) {
  if (enabled) begin();
  enabled_ = enabled;
}

void BtClassicTransport::set_target(const char* name, const uint8_t mac[6], bool use_mac) {
  portENTER_CRITICAL(&g_mux);
  char next_name[sizeof(target_name_)] = {};
  strncpy(next_name, (name != nullptr && name[0] != 0) ? name : kDefaultAdapterName, sizeof(next_name) - 1);
  const bool changed = strcmp(next_name, target_name_) != 0 || memcmp(mac, target_mac_, 6) != 0 ||
                       use_mac != target_use_mac_;
  memcpy(target_name_, next_name, sizeof(target_name_));
  memcpy(target_mac_, mac, 6);
  target_use_mac_ = use_mac;
  if (changed) target_changed_ = true;
  portEXIT_CRITICAL(&g_mux);
}

void BtClassicTransport::reconnect() { reconnect_requested_ = true; }

size_t BtClassicTransport::write(const uint8_t* data, size_t len) {
  if (!up_) return 0;
  size_t queued = 0;
  portENTER_CRITICAL(&g_mux);
  while (queued < len && tx_.push(data[queued])) ++queued;
  portEXIT_CRITICAL(&g_mux);
  return queued;  // sent by the connector task within ~1 ms
}

void BtClassicTransport::flush_tx() {
  uint8_t chunk[64];
  size_t n = 0;
  portENTER_CRITICAL(&g_mux);
  for (int c; n < sizeof(chunk) && (c = tx_.pop()) >= 0;) chunk[n++] = static_cast<uint8_t>(c);
  portEXIT_CRITICAL(&g_mux);
  if (n > 0 && g_bt.connected()) g_bt.write(chunk, n);
}

void BtClassicTransport::service(uint32_t) {
  const bool now_up = enabled_ && g_radio_started && !scanning_ && g_bt.connected();
  if (now_up && !up_) rx_.clear();  // drop anything left from a previous session
  up_ = now_up;
  if (!up_) return;
  // Move what the stack received into the ring; both calls return immediately.
  while (rx_.space() > 0 && g_bt.available() > 0) {
    const int c = g_bt.read();
    if (c < 0) break;
    rx_.push(static_cast<uint8_t>(c));
  }
}

bool BtClassicTransport::start_scan() {
  if (!enabled_ || scanning_) return false;
  scan_requested_ = true;
  scanning_ = true;
  return true;
}

bool BtClassicTransport::get(uint8_t index, core::AdapterInfo& out) const {
  if (index >= scan_count_) return false;
  portENTER_CRITICAL(&g_mux);
  out = results_[index];
  portEXIT_CRITICAL(&g_mux);
  return true;
}

// ---- Connector task ---------------------------------------------------------------------------

void BtClassicTransport::task_entry(void* self) {
  static_cast<BtClassicTransport*>(self)->task_main();
  vTaskDelete(nullptr);
}

void BtClassicTransport::wait_interruptible(uint32_t ms) {
  // This runs in the connector task, never in the main loop.
  while (ms > 0 && enabled_ && !target_changed_ && !scan_requested_ && !reconnect_requested_) {
    const uint32_t slice = ms < 50 ? ms : 50;
    vTaskDelay(pdMS_TO_TICKS(slice));
    ms -= slice;
  }
}

namespace {
void log_heap(const char* what) {
  LOG_I("BT", "%s: free %lu B, block %lu B, task stack min free %lu B", what,
        static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
}
}  // namespace

void BtClassicTransport::begin_round(bool by_user) {
  attempts_left_ = kMaxAttempts;
  failures_ = 0;
  allow_name_ = by_user;
  idle_ = false;
}

bool BtClassicTransport::attempt_connect() {
  char name[sizeof(target_name_)];
  uint8_t mac[6];
  bool use_mac;
  portENTER_CRITICAL(&g_mux);
  memcpy(name, target_name_, sizeof(name));
  memcpy(mac, target_mac_, 6);
  use_mac = target_use_mac_;
  target_changed_ = false;
  portEXIT_CRITICAL(&g_mux);

  if (!use_mac && name[0] == 0) strncpy(name, kDefaultAdapterName, sizeof(name) - 1);
  // A by-name connect runs a full inquiry inside the stack (slow, costs heap): one attempt only.
  if (!use_mac) attempts_left_ = 1;

  // Cheap adapters ship with PIN 1234 or 0000: alternate between them per attempt.
  const bool first_pin = (attempt_++ & 1) == 0;
  g_bt.setPin(first_pin ? "1234" : "0000", 4);

  LOG_I("BT", "connecting to %s (PIN %s)", use_mac ? "saved MAC" : name, first_pin ? "1234" : "0000");
  return use_mac ? g_bt.connect(BTAddress(mac)) : g_bt.connect(String(name));
}

void BtClassicTransport::run_scan() {
  scan_requested_ = false;
  if (g_bt.connected()) g_bt.disconnect();
  LOG_I("BT", "scanning for adapters (user request)");
  BTScanResults* found = g_bt.discover(kScanMs);

  // Build the list outside the critical section (std::string allocates), publish it with a copy.
  core::AdapterInfo local[kMaxResults] = {};
  uint8_t n = 0;
  if (found != nullptr) {
    const int total = found->getCount();
    for (int i = 0; i < total && n < kMaxResults; ++i) {
      BTAdvertisedDevice* dev = found->getDevice(i);
      if (dev == nullptr) continue;
      core::AdapterInfo& info = local[n];
      BTAddress address = dev->getAddress();
      memcpy(info.mac, *address.getNative(), 6);
      strncpy(info.name, dev->getName().c_str(), sizeof(info.name) - 1);
      if (info.name[0] == 0) snprintf(info.name, sizeof(info.name), "%s", address.toString().c_str());
      ++n;
    }
  }
  g_bt.discoverClear();  // free the stack's result list (heap); our fixed array keeps what the UI needs
  portENTER_CRITICAL(&g_mux);
  memcpy(results_, local, sizeof(local));
  scan_count_ = n;
  scan_revision_ = scan_revision_ + 1;
  portEXIT_CRITICAL(&g_mux);
  scanning_ = false;
  idle_ = true;  // no automatic connect after a scan: pick a device or tap Connect
  LOG_I("BT", "scan done, %u device(s)", n);
  log_heap("after scan");
}

void BtClassicTransport::task_main() {
  bool was_enabled = false;
  for (;;) {
    if (!enabled_) {
      if (was_enabled && g_radio_started) g_bt.disconnect();
      was_enabled = false;
      was_connected_ = false;
      idle_ = false;
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!was_enabled) begin_round(false);
    was_enabled = true;

    if (!g_radio_started) {
      const size_t free_now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      const size_t block_now = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if (free_now < kMinHeapForBt || block_now < kMinBlockForBt) {
        if (!g_low_mem_logged) {
          LOG_E("BT", "not enough memory for Bluetooth (free %lu B, block %lu B)",
                static_cast<unsigned long>(free_now), static_cast<unsigned long>(block_now));
          g_low_mem_logged = true;
        }
        wait_interruptible(2000);
        continue;
      }
      LOG_I("BT", "starting radio, free %lu B, block %lu B", static_cast<unsigned long>(free_now),
            static_cast<unsigned long>(block_now));
      // true = master (client) mode; disableBLE = Classic-only controller, frees the BLE memory
      // (about 10 KB). First start allocates the controller/Bluedroid heap.
      g_radio_started = g_bt.begin(kLocalName, true, true);
      if (!g_radio_started) {
        LOG_E("BT", "Bluetooth start failed");
        wait_interruptible(2000);
        continue;
      }
      log_heap("radio up");
    }

    if (scan_requested_) {
      run_scan();
      continue;
    }
    if (reconnect_requested_ || target_changed_) {
      const bool by_user = reconnect_requested_;
      reconnect_requested_ = false;
      target_changed_ = false;  // attempt_connect() reads the new target
      if (g_bt.connected()) g_bt.disconnect();
      was_connected_ = false;
      begin_round(by_user);
    }

    if (g_bt.connected()) {
      if (!was_connected_) {
        was_connected_ = true;
        failures_ = 0;
        idle_ = false;
      }
      flush_tx();
      vTaskDelay(1);  // 1 tick: TX latency, not the main loop
      continue;
    }
    if (was_connected_) {  // the link dropped (adapter off, out of range): a fresh round of attempts
      was_connected_ = false;
      LOG_W("BT", "link dropped");
      begin_round(false);
    }
    portENTER_CRITICAL(&g_mux);
    tx_.clear();  // nothing may be sent into a stale session
    portEXIT_CRITICAL(&g_mux);

    if (idle_) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    bool have_mac;
    portENTER_CRITICAL(&g_mux);
    have_mac = target_use_mac_;
    portEXIT_CRITICAL(&g_mux);
    if (!have_mac && !allow_name_) {
      // No saved adapter: connecting by name would start an inquiry. Wait for the user instead.
      idle_ = true;
      LOG_I("BT", "no saved adapter, idle (tap Scan or Connect)");
      continue;
    }

    if (attempt_connect() && g_bt.connected()) {
      LOG_I("BT", "connected");
      log_heap("connected");
      continue;
    }
    ++failures_;
    if (attempts_left_ > 0) --attempts_left_;
    if (g_bt.connected()) g_bt.disconnect();  // never leave a half-open session behind
    log_heap("connect failed");
    if (attempts_left_ == 0) {
      idle_ = true;
      LOG_W("BT", "giving up after %u attempt(s), idle (tap Connect)", failures_);
      continue;
    }
    const uint32_t pause = kBackoffMs[failures_ - 1 < kMaxAttempts - 1 ? failures_ - 1 : kMaxAttempts - 2];
    LOG_W("BT", "attempt %u failed, retry in %lu ms", failures_, static_cast<unsigned long>(pause));
    wait_interruptible(pause);
  }
}

}  // namespace hal
