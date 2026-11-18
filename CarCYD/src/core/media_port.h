/**
 * @file media_port.h
 * Port between the Media page (ui) and the media service (hal/media_service.cpp).
 * Keeps ui/ free of hardware headers so the PC simulator can stub it.
 *
 * All functions are called from the main loop context.
 */
#pragma once

#include <stdint.h>

#include "safety_gate.h"

namespace core::media {

enum class Mode : uint8_t { Slideshow = 0, Animation };

enum class State : uint8_t {
  Off,        ///< page not shown
  Waiting,    ///< page shown, not allowed (yet): see reason()
  Playing,    ///< the service owns the screen and draws frames
  NoCard,     ///< no SD card / mount failed
  NoMedia,    ///< card present but nothing playable in the current mode
};

struct Library {
  bool scanned;  ///< the card was looked at (it is only mounted when Media is used or the boot logo is on)
  bool card;  ///< an SD card was mounted
  uint16_t photos;
  uint8_t animations;
};

/** First-use safety notice: shown once, the acceptance is persisted. */
bool safety_acknowledged();
void acknowledge_safety();

/** Page became visible / hidden. deactivate() releases the screen at once. */
void activate();
void deactivate();

State state();
Mode mode();
GateReason reason();

/** Next photo / animation (restarts). */
void next();
/** Slideshow <-> animation, only if the other mode has content. */
void toggle_mode();

/** True once after playback was stopped because the gate closed (page must leave). */
bool take_forced_exit();

/** Index sizes (valid after the card was mounted). */
Library library();

}  // namespace core::media
