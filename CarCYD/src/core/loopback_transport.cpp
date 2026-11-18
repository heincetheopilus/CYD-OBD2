#include "loopback_transport.h"

#include <stdio.h>
#include <string.h>

#include "elm327_parser.h"
#include "util.h"

namespace core {
namespace {

char upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parse_byte(const char* s, uint8_t& out) {
  const int hi = hex_nibble(s[0]);
  const int lo = hex_nibble(s[0] != 0 ? s[1] : 0);
  if (hi < 0 || lo < 0) return false;
  out = static_cast<uint8_t>(hi << 4 | lo);
  return true;
}

/** Bounded string builder. */
struct Out {
  char* s;
  size_t cap;
  size_t n = 0;
  Out(char* buffer, size_t capacity) : s(buffer), cap(capacity) { s[0] = 0; }
  void add(const char* text) {
    while (*text != 0 && n + 1 < cap) s[n++] = *text++;
    s[n] = 0;
  }
  void addc(char c) {
    if (n + 1 < cap) {
      s[n++] = c;
      s[n] = 0;
    }
  }
};

}  // namespace

// ---- Raw pipe -------------------------------------------------------------------

void LoopbackTransport::set_connected(bool up) {
  connected_ = up;
  if (!up) {
    rx_.clear();
    pending_valid_ = false;
    line_len_ = 0;
  }
}

void LoopbackTransport::push_rx(const char* text) {
  while (*text != 0) rx_.push(static_cast<uint8_t>(*text++));
}

size_t LoopbackTransport::write(const uint8_t* data, size_t len) {
  if (!connected_) return 0;
  for (size_t i = 0; i < len; ++i) {
    if (sent_len_ + 1 < sizeof(sent_)) {
      sent_[sent_len_++] = static_cast<char>(data[i]);
      sent_[sent_len_] = 0;
    }
    if (!emulate_) continue;
    const char c = static_cast<char>(data[i]);
    if (c == '\r' || c == '\n') {
      line_[line_len_] = 0;
      if (line_len_ > 0) handle_line(line_);
      line_len_ = 0;
    } else if (c != ' ' && line_len_ + 1 < sizeof(line_)) {
      line_[line_len_++] = upper(c);
    }
  }
  return len;
}

void LoopbackTransport::service(uint32_t now_ms) {
  now_ = now_ms;
  if (!connected_ || !pending_valid_) return;
  if (static_cast<int32_t>(now_ms - pending_due_) < 0) return;
  for (size_t i = 0; i < pending_len_; ++i) rx_.push(static_cast<uint8_t>(pending_[i]));
  pending_valid_ = false;
}

// ---- Emulator configuration ---------------------------------------------------------

void LoopbackTransport::set_pid(uint8_t pid, const uint8_t* data, uint8_t len) {
  if (pid > 0x60 || len > 4) return;
  memcpy(pid_data_[pid], data, len);
  pid_len_[pid] = len;
  pid_supported_[pid] = true;
}

void LoopbackTransport::set_pid_supported(uint8_t pid, bool supported) {
  if (pid <= 0x60) pid_supported_[pid] = supported;
}

void LoopbackTransport::set_rpm(float rpm) {
  const uint16_t raw = static_cast<uint16_t>(rpm * 4.0f);
  const uint8_t d[2] = {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw & 0xFF)};
  set_pid(0x0C, d, 2);
}

void LoopbackTransport::set_speed(float kmh) {
  const uint8_t d[1] = {static_cast<uint8_t>(kmh)};
  set_pid(0x0D, d, 1);
}

void LoopbackTransport::set_coolant(float celsius) {
  const uint8_t d[1] = {static_cast<uint8_t>(celsius + 40.0f)};
  set_pid(0x05, d, 1);
}

void LoopbackTransport::set_dtcs(uint8_t mode, const uint16_t* codes, uint8_t count) {
  const uint8_t idx = mode == 3 ? 0 : (mode == 7 ? 1 : 2);
  if (count > 12) count = 12;
  for (uint8_t i = 0; i < count; ++i) dtc_[idx][i] = codes[i];
  dtc_count_[idx] = count;
}

uint32_t LoopbackTransport::support_mask(uint8_t base) const {
  uint32_t mask = 0;
  for (uint8_t offset = 1; offset <= 32; ++offset) {
    const uint8_t pid = static_cast<uint8_t>(base + offset);
    if (pid <= 0x60 && pid_supported_[pid]) mask |= 1u << (32 - offset);
  }
  // The "next range" bit is set when anything beyond this range exists.
  const uint8_t next = static_cast<uint8_t>(base + 32);
  bool beyond = false;
  for (uint8_t pid = static_cast<uint8_t>(next + 1); pid <= 0x60 && next < 0x60; ++pid) beyond |= pid_supported_[pid];
  if (beyond) mask |= 1u;
  return mask;
}

// ---- Reply construction ---------------------------------------------------------------

void LoopbackTransport::reply(const char* body, uint32_t delay_ms, const char* cmd) {
  Out out(pending_, sizeof(pending_));
  if (echo) {
    out.add(cmd);
    out.addc('\r');
  }
  out.add(body);
  out.add("\r>");
  pending_len_ = out.n;
  pending_valid_ = true;
  pending_due_ = now_ + delay_ms;
}

/** Formats one OBD message: single frame, or ISO-TP multi-frame when longer than 7 bytes. */
void LoopbackTransport::append_frame(char* buf, size_t cap, const uint8_t* payload, uint8_t len) const {
  Out out(buf + strlen(buf), cap - strlen(buf));
  const char* sep = spaces ? " " : "";
  char tmp[8];
  auto byte = [&](uint8_t b, bool first) {
    if (!first) out.add(sep);
    snprintf(tmp, sizeof(tmp), "%02X", b);
    out.add(tmp);
  };

  if (len <= 7) {
    bool first = true;
    if (headers) {
      out.add("7E8");
      first = false;
      byte(len, false);
    }
    for (uint8_t i = 0; i < len; ++i) {
      byte(payload[i], first);
      first = false;
    }
    out.addc('\r');
    return;
  }
  snprintf(tmp, sizeof(tmp), "%03X", len);
  out.add(tmp);
  out.addc('\r');
  uint8_t pos = 0;
  uint8_t index = 0;
  while (pos < len) {
    const uint8_t chunk = static_cast<uint8_t>(index == 0 ? 6 : 7);
    snprintf(tmp, sizeof(tmp), "%X:", index & 0xF);
    out.add(tmp);
    bool first = true;  // "0:49..." without spaces, "0: 49 ..." with spaces
    if (spaces) out.add(" ");
    for (uint8_t i = 0; i < chunk && pos < len; ++i) {
      byte(payload[pos++], first);
      first = false;
    }
    out.addc('\r');
    ++index;
  }
}

void LoopbackTransport::reply_pids(const uint8_t* pids, uint8_t count, const char* cmd) {
  uint32_t delay = latency_ms;
  char body[512];
  body[0] = 0;
  Out out(body, sizeof(body));

  if (!ecu_awake) {
    if (pids[0] == 0x00) {
      out.add("SEARCHING...\rUNABLE TO CONNECT\r");
      delay = search_latency_ms;
    } else {
      out.add("NO DATA\r");
    }
    reply(body, delay, cmd);
    return;
  }

  if (count > 1) {
    if (combo == ComboMode::NoData) {
      reply("NO DATA\r", delay, cmd);
      return;
    }
    if (combo == ComboMode::FirstOnly) count = 1;
  }

  uint8_t payload[64];
  uint8_t n = 0;
  payload[n++] = 0x41;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t pid = pids[i];
    uint8_t data[4] = {};
    uint8_t len = 0;
    if (pid == 0x00 || pid == 0x20 || pid == 0x40) {
      const uint32_t mask = support_mask(pid);
      data[0] = static_cast<uint8_t>(mask >> 24);
      data[1] = static_cast<uint8_t>(mask >> 16);
      data[2] = static_cast<uint8_t>(mask >> 8);
      data[3] = static_cast<uint8_t>(mask);
      len = 4;
    } else if (pid == 0x01) {
      data[0] = static_cast<uint8_t>((mil_ ? 0x80 : 0x00) | (dtc_count_[0] & 0x7F));
      data[1] = 0x07;
      data[2] = 0xE5;
      len = 4;
    } else if (pid <= 0x60 && pid_supported_[pid]) {
      len = pid_len_[pid];
      memcpy(data, pid_data_[pid], len);
    } else {
      continue;
    }
    if (static_cast<size_t>(n) + 1 + len > sizeof(payload)) break;
    payload[n++] = pid;
    for (uint8_t k = 0; k < len; ++k) payload[n++] = data[k];
  }
  if (n == 1) {
    reply("NO DATA\r", delay, cmd);
    return;
  }

  if (pids[0] == 0x00 && !protocol_locked_ && first_probe_ && searching_banner) {
    out.add("SEARCHING...\r");
    delay = search_latency_ms;
  }
  first_probe_ = false;
  append_frame(body, sizeof(body), payload, n);
  reply(body, delay, cmd);
}

void LoopbackTransport::reply_dtc(uint8_t mode, const char* cmd) {
  const uint8_t idx = mode == 3 ? 0 : (mode == 7 ? 1 : 2);
  const uint8_t count = dtc_count_[idx];
  if (!ecu_awake || (count == 0 && empty_dtc_is_no_data)) {
    reply("NO DATA\r", latency_ms, cmd);
    return;
  }
  char body[512];
  body[0] = 0;
  const uint8_t reply_mode = static_cast<uint8_t>(0x40 | mode);
  uint8_t payload[64];

  if (elm::is_can_protocol(protocol)) {
    uint8_t n = 0;
    payload[n++] = reply_mode;
    payload[n++] = count;
    for (uint8_t i = 0; i < count; ++i) {
      payload[n++] = static_cast<uint8_t>(dtc_[idx][i] >> 8);
      payload[n++] = static_cast<uint8_t>(dtc_[idx][i] & 0xFF);
    }
    while (n < 7) payload[n++] = 0;  // CAN single frames are padded
    append_frame(body, sizeof(body), payload, n);
  } else {
    // K-line: one message per three codes, no count byte.
    uint8_t i = 0;
    do {
      uint8_t n = 0;
      payload[n++] = reply_mode;
      for (uint8_t k = 0; k < 3; ++k, ++i) {
        const uint16_t code = i < count ? dtc_[idx][i] : 0;
        payload[n++] = static_cast<uint8_t>(code >> 8);
        payload[n++] = static_cast<uint8_t>(code & 0xFF);
      }
      append_frame(body, sizeof(body), payload, n);
    } while (i < count);
  }
  reply(body, latency_ms, cmd);
}

void LoopbackTransport::handle_line(const char* cmd) {
  ++commands_received;
  if (silent) return;

  if (cmd[0] == 'A' && cmd[1] == 'T') {
    const char* a = cmd + 2;
    if (strcmp(a, "Z") == 0) {
      // Reset restores the power-on settings.
      echo = true;
      spaces = true;
      headers = false;
      protocol_locked_ = false;
      first_probe_ = true;
      reply("\rELM327 v1.5\r", reset_latency_ms, "ATZ");
      return;
    }
    const char* ack = "OK\r";
    const bool echo_before = echo;
    if (strcmp(a, "E0") == 0) echo = false;
    else if (strcmp(a, "E1") == 0) echo = true;
    else if (strcmp(a, "S0") == 0) spaces = false;
    else if (strcmp(a, "S1") == 0) spaces = true;
    else if (strcmp(a, "H0") == 0) headers = false;
    else if (strcmp(a, "H1") == 0) headers = true;
    else if (strncmp(a, "SP", 2) == 0) {
      protocol_locked_ = a[2] != '0';
      if (a[2] != '0' && a[2] != 0) protocol = a[2];
    } else if (strcmp(a, "DPN") == 0) {
      static char proto[8];
      snprintf(proto, sizeof(proto), "%c%c\r", protocol_locked_ ? ' ' : 'A', protocol);
      ack = protocol_locked_ ? proto + 1 : proto;
    } else if (strcmp(a, "RV") == 0) ack = "12.6V\r";
    else if (strcmp(a, "I") == 0) ack = "ELM327 v1.5\r";
    const bool keep = echo;
    echo = echo_before;
    reply(ack, latency_ms / 2, cmd);
    echo = keep;
    return;
  }

  uint8_t mode = 0;
  if (!parse_byte(cmd, mode)) {
    reply("?\r", latency_ms, cmd);
    return;
  }
  const size_t len = strlen(cmd);
  if (mode == 0x01) {
    size_t used = len;
    if (len > 2 && (len & 1) == 1) {  // trailing "expected responses" digit
      if (!accept_response_count) {
        reply("?\r", latency_ms, cmd);
        return;
      }
      used = len - 1;
    }
    uint8_t pids[6];
    uint8_t count = 0;
    for (size_t pos = 2; pos + 2 <= used && count < sizeof(pids); pos += 2) {
      if (!parse_byte(cmd + pos, pids[count])) break;
      ++count;
    }
    if (count == 0) {
      reply("?\r", latency_ms, cmd);
      return;
    }
    reply_pids(pids, count, cmd);
  } else if (mode == 0x03 || mode == 0x07 || mode == 0x0A) {
    reply_dtc(mode, cmd);
  } else if (mode == 0x04) {
    if (!ecu_awake) {
      reply("NO DATA\r", latency_ms, cmd);
    } else if (clear_works) {
      for (auto& c : dtc_count_) c = 0;
      mil_ = false;
      reply("44\r", latency_ms, cmd);
    } else {
      reply("NO DATA\r", latency_ms, cmd);
    }
  } else {
    reply("?\r", latency_ms, cmd);
  }
}

}  // namespace core
