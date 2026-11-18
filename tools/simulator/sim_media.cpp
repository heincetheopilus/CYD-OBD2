/**
 * @file sim_media.cpp
 * core/media_port.h for the desktop simulator: there is no SD card, so the
 * Media page shows its "No SD card" message.
 */
#include "../../CarCYD/src/core/media_port.h"

namespace core::media {

bool safety_acknowledged() { return true; }
void acknowledge_safety() {}
void activate() {}
void deactivate() {}
State state() { return State::NoCard; }
Mode mode() { return Mode::Slideshow; }
GateReason reason() { return GateReason::Settling; }
void next() {}
void toggle_mode() {}
bool take_forced_exit() { return false; }
Library library() { return Library{true, false, 0, 0}; }

}  // namespace core::media
