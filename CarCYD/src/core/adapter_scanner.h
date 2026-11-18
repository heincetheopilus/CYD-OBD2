/**
 * @file adapter_scanner.h
 * Optional service that lists nearby OBD adapters (Bluetooth inquiry).
 * The HAL registers an implementation; the UI uses it without knowing the radio.
 */
#pragma once

#include <stdint.h>

namespace core {

struct AdapterInfo {
  char name[24];
  uint8_t mac[6];
};

class AdapterScanner {
 public:
  virtual ~AdapterScanner() = default;

  /** Starts an asynchronous scan (drops the current connection). False if unavailable. */
  virtual bool start_scan() = 0;
  virtual bool scanning() const = 0;
  /** Incremented whenever the result list changed (cheap change detection for the UI). */
  virtual uint32_t revision() const = 0;
  virtual uint8_t count() const = 0;
  virtual bool get(uint8_t index, AdapterInfo& out) const = 0;

  /** User pressed "Connect": starts a fresh round of connection attempts. */
  virtual void connect_now() {}
};

namespace adapter_scanner {

inline AdapterScanner*& slot() {
  static AdapterScanner* instance = nullptr;
  return instance;
}
inline void set(AdapterScanner* scanner) { slot() = scanner; }
inline AdapterScanner* get() { return slot(); }

}  // namespace adapter_scanner
}  // namespace core
