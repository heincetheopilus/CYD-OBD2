/**
 * @file byte_transport.h
 * Byte pipe between the OBD link and the adapter (Bluetooth Classic SPP, a
 * loopback for tests, ...). Pure interface, no Arduino dependency.
 *
 * Contract: every method returns immediately. Implementations buffer data
 * internally and never wait for the radio inside these calls.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace core {

class ByteTransport {
 public:
  virtual ~ByteTransport() = default;

  /** True while the byte pipe to the adapter is up. */
  virtual bool connected() = 0;

  /** Queues `len` bytes for sending; returns how many were accepted. */
  virtual size_t write(const uint8_t* data, size_t len) = 0;

  /** Number of received bytes ready to be read. */
  virtual int available() = 0;

  /** Next received byte, or -1 if none. */
  virtual int read() = 0;

  /** Optional housekeeping, called by the link on every poll (drain radio buffers, ...). */
  virtual void service(uint32_t now_ms) { (void)now_ms; }

  /** Asks the transport to drop the connection and connect again. */
  virtual void reconnect() {}

  /** True when the transport gave up connecting and waits for a user action (reconnect()). */
  virtual bool idle() const { return false; }
};

}  // namespace core
