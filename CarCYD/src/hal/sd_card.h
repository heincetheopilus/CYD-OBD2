/**
 * @file sd_card.h
 * microSD on its own SPI bus (VSPI: SCK 18, MISO 19, MOSI 23, CS 5 - see
 * board_config.h), separate from the TFT (HSPI) and the touch (software SPI).
 *
 * Mounting builds a small cached index of the media folders so the UI never has
 * to walk the card again:
 *   /photos/<name>.jpg          still pictures (slideshow)
 *   /anim/<name>/NNNN.jpg       animation frames, optional /anim/<name>/anim.ini
 *                               (fps=<1..30>, loop=<0|1>)
 *
 * The index uses fixed char arrays only (no String, no heap). Names that are too
 * long, hidden (leading '.'), or beyond the limits are skipped and counted.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace hal::sd_card {

constexpr uint8_t kPhotoNameLen = 32;  ///< incl. terminator
constexpr uint8_t kAnimNameLen = 24;   ///< incl. terminator
constexpr uint16_t kMaxPhotos = 64;
constexpr uint8_t kMaxAnims = 8;
constexpr uint16_t kMaxFrames = 9999;

enum class Status : uint8_t {
  NotMounted,  ///< mount() was not called yet
  NoCard,      ///< no card inserted, or it could not be initialised
  Ready,
};

struct Anim {
  char name[kAnimNameLen];
  uint16_t first;  ///< number of the first frame file (NNNN)
  uint16_t span;   ///< frames first .. first+span-1 (missing files are skipped when playing)
  uint8_t fps;
  bool loop;
};

/** Called every few directory entries while indexing; return false to abort the scan. */
using KeepGoingFn = bool (*)(void* user);

/**
 * Mounts the card and builds the index (once; cached afterwards). Normally done
 * at boot. The directory scan calls `keep_going` every few entries (service the
 * OBD link, re-check the safety gate); if it returns false the scan is aborted,
 * the card is released and NotMounted is returned (try again later). SD.begin()
 * itself cannot be sliced (up to a few 100 ms without a card).
 */
Status mount(KeepGoingFn keep_going = nullptr, void* user = nullptr);

/** Forgets the cached result and mounts again (card swapped). */
Status remount(KeepGoingFn keep_going = nullptr, void* user = nullptr);

Status status();

uint16_t photo_count();
uint8_t anim_count();
const Anim* anim(uint8_t index);

/** Entries ignored while indexing (name too long, over the limits). */
uint16_t skipped_count();

bool photo_path(uint16_t index, char* out, size_t len);
bool frame_path(uint8_t anim_index, uint16_t frame, char* out, size_t len);

}  // namespace hal::sd_card
