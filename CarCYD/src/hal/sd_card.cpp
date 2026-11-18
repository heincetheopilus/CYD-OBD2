#include "sd_card.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

#include "../config/board_config.h"
#include "../core/log.h"

namespace hal::sd_card {
namespace {

constexpr uint32_t kSdHz = 20000000;  // 20 MHz is reliable on the CYD socket; lower it for flaky cards
constexpr uint8_t kDefaultFps = 10;
constexpr uint8_t kMaxFps = 30;

SPIClass g_spi(VSPI);
Status g_status = Status::NotMounted;

char g_photos[kMaxPhotos][kPhotoNameLen];
uint16_t g_photo_count = 0;
Anim g_anims[kMaxAnims];
uint8_t g_anim_count = 0;
uint16_t g_skipped = 0;

KeepGoingFn g_keep = nullptr;
void* g_keep_user = nullptr;
bool g_aborted = false;
uint8_t g_entry_counter = 0;

/** Cooperative yield: every 8 directory entries ask the caller whether to go on. */
bool scan_ok() {
  if (g_aborted) return false;
  if (g_keep != nullptr && (++g_entry_counter & 7u) == 0 && !g_keep(g_keep_user)) g_aborted = true;
  return !g_aborted;
}

/** Entry name without any leading directory (the Arduino core differs between versions). */
const char* base_name(const char* name) {
  const char* slash = strrchr(name, '/');
  return slash != nullptr ? slash + 1 : name;
}

bool ends_with_nocase(const char* s, const char* suffix) {
  const size_t n = strlen(s);
  const size_t m = strlen(suffix);
  return n >= m && strcasecmp(s + n - m, suffix) == 0;
}

bool is_jpeg_name(const char* name) { return ends_with_nocase(name, ".jpg") || ends_with_nocase(name, ".jpeg"); }

/** "0007.jpg" -> 7; returns -1 if the name is not a frame file. */
int frame_number(const char* name) {
  if (strlen(name) != 8 || !ends_with_nocase(name, ".jpg")) return -1;
  int value = 0;
  for (int i = 0; i < 4; ++i) {
    if (name[i] < '0' || name[i] > '9') return -1;
    value = value * 10 + (name[i] - '0');
  }
  return value;
}

/** Keeps g_photos sorted (insertion sort, at most kMaxPhotos entries). */
void add_photo(const char* name) {
  uint16_t pos = g_photo_count;
  while (pos > 0 && strcasecmp(g_photos[pos - 1], name) > 0) {
    memcpy(g_photos[pos], g_photos[pos - 1], kPhotoNameLen);
    --pos;
  }
  strncpy(g_photos[pos], name, kPhotoNameLen - 1);
  g_photos[pos][kPhotoNameLen - 1] = '\0';
  ++g_photo_count;
}

void scan_photos() {
  File dir = SD.open("/photos");
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f && scan_ok(); f = dir.openNextFile()) {
    const bool is_dir = f.isDirectory();
    char name[kPhotoNameLen + 8];
    strncpy(name, base_name(f.name()), sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    f.close();

    if (is_dir || name[0] == '.' || !is_jpeg_name(name)) continue;  // also skips macOS "._x.jpg"
    if (strlen(name) >= kPhotoNameLen || g_photo_count >= kMaxPhotos) {
      ++g_skipped;
      continue;
    }
    add_photo(name);
  }
  dir.close();
}

/** Reads fps / loop from anim.ini (a few bytes, "key=value" lines). */
void read_anim_ini(const char* anim_name, Anim& a) {
  char path[48];
  snprintf(path, sizeof(path), "/anim/%s/anim.ini", anim_name);
  File f = SD.open(path);
  if (!f) return;
  char buf[97];
  const size_t n = f.readBytes(buf, sizeof(buf) - 1);
  f.close();
  buf[n] = '\0';

  for (char* line = strtok(buf, "\r\n"); line != nullptr; line = strtok(nullptr, "\r\n")) {
    int value = 0;
    if (sscanf(line, " fps = %d", &value) == 1) {
      a.fps = static_cast<uint8_t>(value < 1 ? 1 : (value > kMaxFps ? kMaxFps : value));
    } else if (sscanf(line, " loop = %d", &value) == 1) {
      a.loop = value != 0;
    }
  }
}

void scan_anim_frames(const char* anim_name) {
  char path[48];
  snprintf(path, sizeof(path), "/anim/%s", anim_name);
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;

  int lo = kMaxFrames + 1;
  int hi = -1;
  for (File f = dir.openNextFile(); f && scan_ok(); f = dir.openNextFile()) {
    const int n = f.isDirectory() ? -1 : frame_number(base_name(f.name()));
    f.close();
    if (n < 0) continue;
    if (n < lo) lo = n;
    if (n > hi) hi = n;
  }
  dir.close();
  if (hi < 0) return;

  Anim& a = g_anims[g_anim_count];
  memset(&a, 0, sizeof(a));
  strncpy(a.name, anim_name, kAnimNameLen - 1);
  a.first = static_cast<uint16_t>(lo);
  a.span = static_cast<uint16_t>(hi - lo + 1);
  a.fps = kDefaultFps;
  a.loop = true;
  read_anim_ini(anim_name, a);
  ++g_anim_count;
}

void scan_anims() {
  File dir = SD.open("/anim");
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f && scan_ok(); f = dir.openNextFile()) {
    const bool is_dir = f.isDirectory();
    char name[kAnimNameLen + 8];
    strncpy(name, base_name(f.name()), sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    f.close();

    if (!is_dir || name[0] == '.') continue;
    if (strlen(name) >= kAnimNameLen || g_anim_count >= kMaxAnims) {
      ++g_skipped;
      continue;
    }
    scan_anim_frames(name);
  }
  dir.close();
}

}  // namespace

Status mount(KeepGoingFn keep_going, void* user) {
  if (g_status != Status::NotMounted) return g_status;
  g_keep = keep_going;
  g_keep_user = user;
  g_aborted = false;
  g_entry_counter = 0;

  g_photo_count = 0;
  g_anim_count = 0;
  g_skipped = 0;

  g_spi.begin(board::kSdSck, board::kSdMiso, board::kSdMosi, board::kSdCs);
  if (!SD.begin(board::kSdCs, g_spi, kSdHz) || SD.cardType() == CARD_NONE) {
    SD.end();
    g_spi.end();
    g_status = Status::NoCard;
    LOG_W("SD", "no card");
    return g_status;
  }

  scan_photos();
  scan_anims();
  g_keep = nullptr;
  if (g_aborted) {
    SD.end();
    g_spi.end();
    g_photo_count = 0;
    g_anim_count = 0;
    LOG_W("SD", "scan aborted");
    return g_status;  // still NotMounted
  }
  g_status = Status::Ready;
  LOG_I("SD", "%u photos, %u animations, %u skipped", static_cast<unsigned>(g_photo_count),
        static_cast<unsigned>(g_anim_count), static_cast<unsigned>(g_skipped));
  return g_status;
}

Status remount(KeepGoingFn keep_going, void* user) {
  if (g_status == Status::Ready) {
    SD.end();
    g_spi.end();
  }
  g_status = Status::NotMounted;
  return mount(keep_going, user);
}

Status status() { return g_status; }

uint16_t photo_count() { return g_status == Status::Ready ? g_photo_count : 0; }

uint8_t anim_count() { return g_status == Status::Ready ? g_anim_count : 0; }

const Anim* anim(uint8_t index) { return (g_status == Status::Ready && index < g_anim_count) ? &g_anims[index] : nullptr; }

uint16_t skipped_count() { return g_skipped; }

bool photo_path(uint16_t index, char* out, size_t len) {
  if (g_status != Status::Ready || index >= g_photo_count) return false;
  const int n = snprintf(out, len, "/photos/%s", g_photos[index]);
  return n > 0 && static_cast<size_t>(n) < len;
}

bool frame_path(uint8_t anim_index, uint16_t frame, char* out, size_t len) {
  const Anim* a = anim(anim_index);
  if (a == nullptr) return false;
  const int n = snprintf(out, len, "/anim/%s/%04u.jpg", a->name, static_cast<unsigned>(frame));
  return n > 0 && static_cast<size_t>(n) < len;
}

}  // namespace hal::sd_card
