/**
 * @file elm327_parser.h
 * Pure text -> value parsing of ELM327 replies (no I/O, no Arduino).
 *
 * Tolerates echo, spaces on/off, headers, "N:" multi-frame replies, the ISO-TP
 * length line and the usual status lines (NO DATA, SEARCHING..., UNABLE TO
 * CONNECT, STOPPED, BUS INIT, ...).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "types.h"

namespace core::elm {

enum class Status : uint8_t {
  Empty = 0,     ///< nothing useful received
  Data,          ///< at least one hex data byte
  Ok,            ///< "OK"
  NoData,        ///< "NO DATA"
  Unable,        ///< "UNABLE TO CONNECT"
  Stopped,       ///< "STOPPED"
  BusInitError,  ///< "BUS INIT: ...ERROR"
  Error,         ///< "?" or any other error line (CAN ERROR, BUS ERROR, ...)
  Text,          ///< only text (banner, ATRV voltage, ...)
};

constexpr size_t kMaxBytes = 128;
constexpr size_t kMaxMessages = 8;

/** Parsed reply. Bytes of one OBD message (one ECU, multi-frame already joined) are contiguous. */
struct Response {
  Status status = Status::Empty;
  bool searching = false;  ///< "SEARCHING..." seen
  bool question = false;   ///< adapter answered "?" (unknown command)
  uint8_t length = 0;
  uint8_t message_count = 0;
  uint8_t message_start[kMaxMessages + 1] = {};
  uint8_t bytes[kMaxBytes] = {};

  const uint8_t* message(uint8_t i) const { return bytes + message_start[i]; }
  uint8_t message_length(uint8_t i) const {
    const uint8_t end = (i + 1 < message_count) ? message_start[i + 1] : length;
    return end - message_start[i];
  }
};

/**
 * Parses a raw reply (without the final '>' prompt). `echo` is the command that
 * was sent (lines equal to it are ignored), may be null.
 */
void parse(const char* text, size_t len, const char* echo, Response& out);

// ---- Mode 01 ----------------------------------------------------------------

struct PidInfo {
  uint8_t pid;
  uint8_t data_bytes;
  SignalId signal;  ///< SignalId::Count = not a plain signal (support masks, MIL)
  float scale;
  float offset;
};

/** Table lookup; null if the PID is unknown. */
const PidInfo* pid_info(uint8_t pid);

/** Decodes the raw data bytes of a PID (1 or 2 bytes, big endian) to base units. */
float decode_pid(const PidInfo& info, const uint8_t* data);

/**
 * Finds `pid` in a mode 01 reply ("41 pid data...", also several PIDs in one
 * message: "41 0C aa bb 0D cc"). On success `data` points to the PID's bytes.
 */
bool find_pid(const Response& r, uint8_t pid, const uint8_t*& data);

/** PID 00/20/40 support bitmask reply. */
bool parse_support_mask(const Response& r, uint8_t base_pid, uint32_t& mask);

/** True if `pid` is flagged in `mask` of the range starting at `base_pid`. */
bool mask_has_pid(uint32_t mask, uint8_t base_pid, uint8_t pid);

/** PID 01: MIL flag and number of confirmed codes. */
bool parse_mil(const Response& r, bool& mil_on, uint8_t& code_count);

// ---- DTC modes 03 / 07 / 0A ------------------------------------------------------

/**
 * Extracts SAE J2012 codes (0x0301 = P0301) from a reply of mode `request_mode`
 * (3, 7 or 0x0A). `can` selects the CAN layout (count byte after the mode byte).
 * Returns the number of codes written (duplicates and 0000 are skipped).
 */
uint8_t decode_dtcs(const Response& r, uint8_t request_mode, bool can, uint16_t* out, uint8_t max_out);

/** Mode 04 positive reply ("44"). */
bool is_clear_ack(const Response& r);

// ---- Misc -------------------------------------------------------------------------

/** ATRV reply ("12.4V"). */
bool parse_voltage(const char* text, size_t len, float& volts);

/** ATDPN reply ("A6" = automatic, protocol 6). */
bool parse_protocol(const char* text, size_t len, char& protocol, bool& automatic);

/** ISO 15765 (CAN) protocols 6..C use the count-byte DTC layout. */
bool is_can_protocol(char protocol);

}  // namespace core::elm
