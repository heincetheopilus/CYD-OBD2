#include "elm327_parser.h"

#include <string.h>

namespace core::elm {
namespace {

constexpr size_t kMaxLine = 160;

bool is_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); }

uint8_t hex_value(char c) { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'A' + 10); }

char upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

/** Parsing state shared by the line handlers. */
struct Parser {
  Response& r;
  bool in_multi = false;   // a "0:" frame opened a multi-frame message
  bool no_data = false;
  bool unable = false;
  bool stopped = false;
  bool bus_init_error = false;
  bool error = false;
  bool ok = false;
  bool text = false;

  void open_message() {
    if (r.message_count < kMaxMessages) r.message_start[r.message_count++] = r.length;
  }
  void add_byte(uint8_t b) {
    if (r.length < kMaxBytes) r.bytes[r.length++] = b;
  }
  /** Appends hex pairs of `s` (spaces ignored). Returns false on an odd digit count. */
  bool add_pairs(const char* s, size_t n) {
    uint8_t hi = 0;
    bool have_hi = false;
    for (size_t i = 0; i < n; ++i) {
      if (s[i] == ' ') continue;
      if (!have_hi) {
        hi = hex_value(s[i]);
        have_hi = true;
      } else {
        add_byte(static_cast<uint8_t>(hi << 4 | hex_value(s[i])));
        have_hi = false;
      }
    }
    return !have_hi;
  }
};

bool contains(const char* line, const char* needle) { return strstr(line, needle) != nullptr; }

/** Compares ignoring spaces and case. */
bool same_command(const char* line, const char* echo) {
  while (true) {
    while (*line == ' ') ++line;
    while (*echo == ' ') ++echo;
    if (*line == 0 || *echo == 0) return *line == 0 && *echo == 0;
    if (upper(*line) != upper(*echo)) return false;
    ++line;
    ++echo;
  }
}

/** Status keywords. Returns true if the line was consumed. */
bool handle_keyword(Parser& p, const char* line) {
  if (contains(line, "NO DATA")) {
    p.no_data = true;
  } else if (contains(line, "UNABLE")) {
    p.unable = true;
  } else if (contains(line, "STOPPED")) {
    p.stopped = true;
  } else if (contains(line, "SEARCHING")) {
    p.r.searching = true;
  } else if (contains(line, "BUS INIT")) {
    if (contains(line, "ERROR")) p.bus_init_error = true;  // "BUS INIT: OK" is just progress
  } else if (strcmp(line, "?") == 0) {
    p.r.question = true;
    p.error = true;
  } else if (contains(line, "ERROR") || contains(line, "BUS BUSY") || contains(line, "BUFFER FULL") ||
             contains(line, "LV RESET") || contains(line, "ACT ALERT")) {
    p.error = true;
  } else if (strcmp(line, "OK") == 0) {
    p.ok = true;
  } else {
    return false;
  }
  return true;
}

void handle_hex_line(Parser& p, const char* line, size_t n) {
  const char* colon = static_cast<const char*>(memchr(line, ':', n));
  if (colon != nullptr) {
    // ISO-TP frame "N: xx xx ...": frame 0 opens the message, the others continue it.
    const size_t prefix = static_cast<size_t>(colon - line);
    size_t first = 0;
    while (first < prefix && line[first] == ' ') ++first;
    if (!p.in_multi || (prefix > first && line[first] == '0')) {
      p.open_message();
      p.in_multi = true;
    }
    p.add_pairs(colon + 1, n - prefix - 1);
    return;
  }
  p.in_multi = false;

  // Compact form (no spaces) and token count.
  char compact[kMaxLine];
  size_t cn = 0;
  bool spaced = false;
  for (size_t i = 0; i < n; ++i) {
    if (line[i] == ' ') {
      spaced = true;
    } else {
      compact[cn++] = line[i];
    }
  }
  if (cn == 0) return;
  // ISO-TP total length line before a "0:" frame (e.g. "014"): ignore.
  if (cn == 3) return;

  if (spaced) {
    // First token longer than two digits and odd = CAN header (e.g. "7E8"), skip it.
    size_t i = 0;
    size_t tok = 0;
    while (i < n && line[i] != ' ') {
      ++i;
      ++tok;
    }
    const char* rest = line;
    size_t rest_n = n;
    if (tok >= 3 && (tok & 1) == 1) {
      rest = line + tok;
      rest_n = n - tok;
    }
    p.open_message();
    p.add_pairs(rest, rest_n);
    return;
  }
  // No spaces: an odd length means a 3 digit header precedes the data.
  p.open_message();
  if ((cn & 1) == 1) {
    if (cn < 5) return;
    p.add_pairs(compact + 3, cn - 3);
  } else {
    p.add_pairs(compact, cn);
  }
}

const PidInfo kPids[] = {
    {0x00, 4, SignalId::Count, 1.0f, 0.0f},
    {0x01, 4, SignalId::Count, 1.0f, 0.0f},
    {0x04, 1, SignalId::EngineLoad, 100.0f / 255.0f, 0.0f},
    {0x05, 1, SignalId::CoolantTemp, 1.0f, -40.0f},
    {0x0C, 2, SignalId::Rpm, 0.25f, 0.0f},
    {0x0D, 1, SignalId::Speed, 1.0f, 0.0f},
    {0x0F, 1, SignalId::IntakeTemp, 1.0f, -40.0f},
    {0x11, 1, SignalId::ThrottlePos, 100.0f / 255.0f, 0.0f},
    {0x20, 4, SignalId::Count, 1.0f, 0.0f},
    {0x2F, 1, SignalId::FuelLevel, 100.0f / 255.0f, 0.0f},
    {0x40, 4, SignalId::Count, 1.0f, 0.0f},
    {0x42, 2, SignalId::BatteryVoltage, 0.001f, 0.0f},
};

}  // namespace

void parse(const char* text, size_t len, const char* echo, Response& out) {
  out = Response{};
  Parser p{out};

  size_t i = 0;
  while (i < len) {
    char line[kMaxLine];
    size_t n = 0;
    while (i < len && text[i] != '\r' && text[i] != '\n') {
      const char c = upper(text[i++]);
      if (c == 0 || c == '>') continue;
      if (n < kMaxLine - 1) line[n++] = c;
    }
    while (i < len && (text[i] == '\r' || text[i] == '\n')) ++i;

    while (n > 0 && line[n - 1] == ' ') --n;  // trim
    size_t start = 0;
    while (start < n && line[start] == ' ') ++start;
    if (start > 0) {
      memmove(line, line + start, n - start);
      n -= start;
    }
    line[n] = 0;
    if (n == 0) continue;
    if (echo != nullptr && same_command(line, echo)) continue;
    if (handle_keyword(p, line)) continue;

    bool hex_only = true;
    for (size_t k = 0; k < n; ++k) {
      if (!is_hex(line[k]) && line[k] != ' ' && line[k] != ':') {
        hex_only = false;
        break;
      }
    }
    if (hex_only) {
      handle_hex_line(p, line, n);
    } else {
      p.text = true;
    }
  }

  if (out.length > 0) {
    out.status = Status::Data;
  } else if (p.unable) {
    out.status = Status::Unable;
  } else if (p.stopped) {
    out.status = Status::Stopped;
  } else if (p.bus_init_error) {
    out.status = Status::BusInitError;
  } else if (p.no_data) {
    out.status = Status::NoData;
  } else if (p.error) {
    out.status = Status::Error;
  } else if (p.ok) {
    out.status = Status::Ok;
  } else if (p.text) {
    out.status = Status::Text;
  }
  // Drop empty messages (e.g. a header-only line).
  uint8_t kept = 0;
  for (uint8_t m = 0; m < out.message_count; ++m) {
    if (out.message_length(m) > 0) out.message_start[kept++] = out.message_start[m];
  }
  out.message_count = kept;
}

const PidInfo* pid_info(uint8_t pid) {
  for (const PidInfo& info : kPids) {
    if (info.pid == pid) return &info;
  }
  return nullptr;
}

float decode_pid(const PidInfo& info, const uint8_t* data) {
  const float raw = info.data_bytes >= 2 ? static_cast<float>(data[0] * 256 + data[1]) : static_cast<float>(data[0]);
  return raw * info.scale + info.offset;
}

bool find_pid(const Response& r, uint8_t pid, const uint8_t*& data) {
  for (uint8_t m = 0; m < r.message_count; ++m) {
    const uint8_t* b = r.message(m);
    const uint8_t n = r.message_length(m);
    for (uint8_t start = 0; start < n; ++start) {
      if (b[start] != 0x41) continue;
      // Walk "41 pid data pid data ..." using the known data lengths.
      uint8_t j = static_cast<uint8_t>(start + 1);
      while (j < n) {
        const PidInfo* info = pid_info(b[j]);
        const uint8_t need = info != nullptr ? info->data_bytes : 0;
        if (b[j] == pid) {
          const uint8_t want = info != nullptr ? info->data_bytes : 1;
          if (j + 1 + want > n) break;
          data = b + j + 1;
          return true;
        }
        if (info == nullptr || j + 1 + need > n) break;
        j = static_cast<uint8_t>(j + 1 + need);
      }
    }
  }
  return false;
}

bool parse_support_mask(const Response& r, uint8_t base_pid, uint32_t& mask) {
  const uint8_t* d = nullptr;
  if (!find_pid(r, base_pid, d)) return false;
  mask = (static_cast<uint32_t>(d[0]) << 24) | (static_cast<uint32_t>(d[1]) << 16) |
         (static_cast<uint32_t>(d[2]) << 8) | d[3];
  return true;
}

bool mask_has_pid(uint32_t mask, uint8_t base_pid, uint8_t pid) {
  if (pid <= base_pid || pid > base_pid + 32) return false;
  return ((mask >> (32 - (pid - base_pid))) & 1u) != 0;
}

bool parse_mil(const Response& r, bool& mil_on, uint8_t& code_count) {
  const uint8_t* d = nullptr;
  if (!find_pid(r, 0x01, d)) return false;
  mil_on = (d[0] & 0x80) != 0;
  code_count = d[0] & 0x7F;
  return true;
}

uint8_t decode_dtcs(const Response& r, uint8_t request_mode, bool can, uint16_t* out, uint8_t max_out) {
  const uint8_t reply_mode = static_cast<uint8_t>(request_mode | 0x40);
  uint8_t count = 0;
  for (uint8_t m = 0; m < r.message_count; ++m) {
    const uint8_t* b = r.message(m);
    const uint8_t n = r.message_length(m);
    uint8_t k = 0;
    while (k < n && b[k] != reply_mode) ++k;
    if (k >= n) continue;
    const uint8_t* d = b + k + 1;
    uint8_t left = static_cast<uint8_t>(n - k - 1);
    uint8_t pairs = static_cast<uint8_t>(left / 2);
    if (can) {
      if (left < 1) continue;
      const uint8_t announced = d[0];
      ++d;
      --left;
      pairs = static_cast<uint8_t>(left / 2);
      if (announced < pairs) pairs = announced;
    }
    for (uint8_t p = 0; p < pairs; ++p) {
      const uint16_t code = static_cast<uint16_t>(d[2 * p] << 8 | d[2 * p + 1]);
      if (code == 0) continue;
      bool duplicate = false;
      for (uint8_t q = 0; q < count; ++q) duplicate |= (out[q] == code);
      if (!duplicate && count < max_out) out[count++] = code;
    }
  }
  return count;
}

bool is_clear_ack(const Response& r) {
  for (uint8_t m = 0; m < r.message_count; ++m) {
    const uint8_t* b = r.message(m);
    const uint8_t n = r.message_length(m);
    for (uint8_t k = 0; k < n; ++k) {
      if (b[k] == 0x44) return true;
    }
  }
  return false;
}

bool parse_voltage(const char* text, size_t len, float& volts) {
  for (size_t i = 0; i < len; ++i) {
    if (text[i] < '0' || text[i] > '9') continue;
    float v = 0.0f;
    size_t j = i;
    while (j < len && text[j] >= '0' && text[j] <= '9') v = v * 10.0f + static_cast<float>(text[j++] - '0');
    if (j < len && text[j] == '.') {
      float scale = 0.1f;
      ++j;
      while (j < len && text[j] >= '0' && text[j] <= '9') {
        v += scale * static_cast<float>(text[j++] - '0');
        scale *= 0.1f;
      }
    }
    // A voltage is followed by 'V' (ATRV); this rejects echoes and banners.
    if (j < len && (text[j] == 'V' || text[j] == 'v') && v > 0.5f && v < 40.0f) {
      volts = v;
      return true;
    }
    i = j;
  }
  return false;
}

bool parse_protocol(const char* text, size_t len, char& protocol, bool& automatic) {
  size_t i = 0;
  while (i < len) {
    char line[16];
    size_t n = 0;
    while (i < len && text[i] != '\r' && text[i] != '\n') {
      const char c = upper(text[i++]);
      if (c != ' ' && c != '>' && n < sizeof(line) - 1) line[n++] = c;
    }
    while (i < len && (text[i] == '\r' || text[i] == '\n')) ++i;
    line[n] = 0;
    const bool is_auto = n == 2 && line[0] == 'A';
    if (n != 1 && !is_auto) continue;
    const char c = line[n - 1];
    if ((c >= '1' && c <= '9') || (c >= 'A' && c <= 'C')) {
      protocol = c;
      automatic = is_auto;
      return true;
    }
  }
  return false;
}

bool is_can_protocol(char protocol) { return (protocol >= '6' && protocol <= '9') || (protocol >= 'A' && protocol <= 'C'); }

}  // namespace core::elm
