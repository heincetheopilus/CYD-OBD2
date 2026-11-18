#include "media_service.h"

#include <Arduino.h>

#include "../core/alerts.h"
#include "../core/log.h"
#include "../core/media_port.h"
#include "../core/safety_gate.h"
#include "../core/settings.h"
#include "../core/vehicle_state.h"
#include "display.h"
#include "nvs_storage.h"
#include "jpeg_stream.h"
#include "lcd.h"
#include "sd_card.h"

namespace hal::media {
namespace {

using core::media::Mode;
using core::media::State;

constexpr int16_t kScreenW = 320;
constexpr int16_t kScreenH = 240;
constexpr uint32_t kBootLogoMaxMs = 1500;
constexpr const char* kBootLogoPath = "/boot/logo.jpg";

constexpr const char* kFlagsKey = "media_flags";  // bit0: safety notice accepted

core::SafetyGate g_gate;
hal::NvsStorage g_flags_store;
bool g_safety_ack = false;
PumpFn g_pump = nullptr;

State g_state = State::Off;
Mode g_mode = Mode::Slideshow;
bool g_active = false;       // Media page visible
bool g_forced_exit = false;
bool g_request_next = false;
uint32_t g_next_due_ms = 0;
uint16_t g_failures = 0;
uint32_t g_rng = 2463534242u;

int32_t g_photo = -1;        // current photo index
uint8_t g_anim = 0;          // current animation
uint16_t g_frame = 0;        // next frame offset within the animation (0 .. span-1)
bool g_anim_done = false;    // non-looping animation reached its last frame

// ---- Safety gate ----------------------------------------------------------------------------

void evaluate(uint32_t now_ms) {
  const core::VehicleState& v = core::vehicle();
  core::GateInput in;
  const core::SignalValue& speed = v.get(core::SignalId::Speed);
  in.speed_kmh = speed.value;
  in.speed_fresh = core::gate_speed_fresh(speed.valid, speed.updated_ms, now_ms);
  in.demo_mode = core::settings::get().demo_mode;
  in.link_connected = v.link_state == core::LinkState::Connected;
  in.alert_critical = core::alerts().highest() == core::Severity::Critical;
  in.bench_mode = core::settings::get().media_bench_mode;
  in.now_ms = now_ms;
  g_gate.update(in);
}

/** Called by the decoder every few ms: keep the link alive, re-check the gate. */
bool keep_going(void*) {
  const uint32_t now = millis();
  if (g_pump != nullptr) g_pump(now);
  evaluate(now);
  return g_active && g_gate.allowed();
}

// ---- Helpers --------------------------------------------------------------------------------

uint32_t next_random() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

uint32_t frame_period_ms(const sd_card::Anim& a) {
  uint8_t fps = a.fps < 1 ? 1 : a.fps;
  const uint8_t cap = core::settings::get().media_fps_cap;
  if (cap >= 1 && fps > cap) fps = cap;
  return 1000u / fps;
}

/** Small controls drawn over the picture: exit (top left) and mode (top right). */
void draw_overlay() {
  lgfx::LGFX_Device& dev = lcd();
  for (int pass = 0; pass < 2; ++pass) {  // pass 0 = shadow
    const int o = pass == 0 ? 1 : 0;
    const uint16_t c = pass == 0 ? 0x0000 : 0xFFFF;
    dev.drawLine(7 + o, 7 + o, 17 + o, 17 + o, c);
    dev.drawLine(17 + o, 7 + o, 7 + o, 17 + o, c);
    dev.setTextSize(2);
    dev.setTextColor(c);
    dev.drawString(g_mode == Mode::Slideshow ? "P" : "A", kScreenW - 20 + o, 5 + o);
  }
}

bool has_content(Mode m) { return m == Mode::Slideshow ? sd_card::photo_count() > 0 : sd_card::anim_count() > 0; }

void stop_playing(State next_state, bool forced) {
  display::set_direct_mode(false);
  g_state = next_state;
  if (forced) g_forced_exit = true;
  LOG_I("MEDIA", "stopped (%s)", forced ? "gate closed" : "no media");
}

void begin(uint32_t now_ms) {
  // Normally mounted at boot. A late mount / retry scans in slices (keep_going services the link
  // and re-checks the gate); an aborted scan leaves us Waiting and is retried on a later loop.
  sd_card::Status st = sd_card::status();
  if (st == sd_card::Status::NotMounted) st = sd_card::mount(keep_going, nullptr);
  else if (st == sd_card::Status::NoCard) st = sd_card::remount(keep_going, nullptr);  // card inserted since boot?
  if (st == sd_card::Status::NotMounted) return;
  if (st != sd_card::Status::Ready) {
    g_state = State::NoCard;
    return;
  }

  if (!has_content(g_mode)) {
    const Mode other = g_mode == Mode::Slideshow ? Mode::Animation : Mode::Slideshow;
    if (!has_content(other)) {
      g_state = State::NoMedia;
      return;
    }
    g_mode = other;
  }

  g_rng ^= now_ms | 1u;
  g_failures = 0;
  g_photo = -1;
  g_frame = 0;
  g_anim_done = false;
  if (g_anim >= sd_card::anim_count()) g_anim = 0;

  display::set_direct_mode(true);
  lcd().fillScreen(0x0000);
  g_state = State::Playing;
  g_request_next = true;
  LOG_I("MEDIA", "playing (%s)", g_mode == Mode::Slideshow ? "slideshow" : "animation");
}

/** Picks the next photo; returns false if there is none. */
bool pick_photo(char* path, size_t len) {
  const uint16_t n = sd_card::photo_count();
  if (n == 0) return false;
  if (core::settings::get().media_shuffle && n > 1) {
    int32_t idx;
    do {
      idx = static_cast<int32_t>(next_random() % n);
    } while (idx == g_photo);
    g_photo = idx;
  } else {
    g_photo = (g_photo + 1) % n;
  }
  return sd_card::photo_path(static_cast<uint16_t>(g_photo), path, len);
}

void draw_step(uint32_t now_ms) {
  char path[64];
  uint16_t failure_limit = 1;
  uint32_t period = 0;
  bool animation = g_mode == Mode::Animation;
  const sd_card::Anim* a = nullptr;

  if (!animation) {
    if (!pick_photo(path, sizeof(path))) return stop_playing(State::NoMedia, false);
    failure_limit = sd_card::photo_count();
  } else {
    a = sd_card::anim(g_anim);
    if (a == nullptr) return stop_playing(State::NoMedia, false);
    if (!sd_card::frame_path(g_anim, static_cast<uint16_t>(a->first + g_frame), path, sizeof(path))) {
      return stop_playing(State::NoMedia, false);
    }
    failure_limit = a->span;
    period = frame_period_ms(*a);
  }

  const jpeg::Result r = jpeg::draw_file(path, kScreenW, kScreenH, keep_going, nullptr);
  if (r == jpeg::Result::Aborted || !g_gate.allowed() || !g_active) {
    if (g_state == State::Playing && !g_gate.allowed()) stop_playing(State::Waiting, true);
    return;
  }

  if (r != jpeg::Result::Ok) {
    LOG_W("MEDIA", "%s: %s", path, jpeg::result_name(r));
    if (animation) ++g_frame;  // skip the missing / broken frame
    if (++g_failures >= failure_limit) return stop_playing(State::NoMedia, false);
    g_next_due_ms = millis();  // try the next item on the next loop iteration
    if (animation && g_frame >= a->span) g_frame = 0;
    return;
  }

  g_failures = 0;
  draw_overlay();

  const uint32_t done = millis();
  if (!animation) {
    g_next_due_ms = done + static_cast<uint32_t>(core::settings::get().media_interval_s) * 1000u;
    return;
  }
  ++g_frame;
  if (g_frame >= a->span) {
    g_frame = 0;
    if (!a->loop) g_anim_done = true;
  }
  // Fixed frame rate when the decode is fast enough; never try to catch up.
  g_next_due_ms = (done - now_ms >= period) ? done : now_ms + period;
}

}  // namespace

void init(PumpFn pump) {
  g_pump = pump;
  g_gate.reset();
  // Own handle on the firmware's NVS namespace for the "safety notice accepted" flag
  // (kept out of core::Settings; erased together with the rest by a factory reset).
  if (g_flags_store.begin("carcyd")) {
    uint8_t flags = 0;
    if (g_flags_store.read(kFlagsKey, &flags, sizeof(flags))) g_safety_ack = (flags & 1u) != 0;
  }
}

void poll(uint32_t now_ms) {
  evaluate(now_ms);
  if (!g_active) return;

  if (g_state == State::Playing) {
    if (!g_gate.allowed()) return stop_playing(State::Waiting, true);
    const bool due = static_cast<int32_t>(now_ms - g_next_due_ms) >= 0 && !g_anim_done;
    if (g_request_next || due) {
      g_request_next = false;
      draw_step(now_ms);
    }
    return;
  }
  if (g_state == State::Waiting && g_gate.allowed()) begin(now_ms);
}

/** The boot logo is a static picture that is only ever shown if the file exists on the card
 *  (delete /boot/logo.jpg to disable it). It is skipped / aborted as soon as the vehicle link
 *  reports movement or a critical alert, and never lasts longer than kBootLogoMaxMs. */
static bool boot_logo_safe() {
  const core::SignalValue& s = core::vehicle().get(core::SignalId::Speed);
  const bool moving = s.valid && s.value >= core::kGateStopKmh;
  return !moving && core::alerts().highest() != core::Severity::Critical;
}

bool show_boot_logo() {
  if (!boot_logo_safe()) return false;
  if (sd_card::mount() != sd_card::Status::Ready) return false;  // boot: no link to service yet
  display::set_direct_mode(true);

  static uint32_t start;
  start = millis();
  const jpeg::Result r = jpeg::draw_file(
      kBootLogoPath, kScreenW, kScreenH,
      [](void*) { return millis() - start < kBootLogoMaxMs && boot_logo_safe(); }, nullptr);
  if (r != jpeg::Result::Ok) {
    if (r != jpeg::Result::OpenFailed) LOG_W("MEDIA", "boot logo: %s", jpeg::result_name(r));
    display::set_direct_mode(false);
    return false;
  }
  return true;
}

void end_boot_logo() { display::set_direct_mode(false); }

}  // namespace hal::media

// ---- core/media_port.h ------------------------------------------------------------------------

namespace core::media {

bool safety_acknowledged() { return hal::media::g_safety_ack; }

void acknowledge_safety() {
  hal::media::g_safety_ack = true;
  const uint8_t flags = 1;
  hal::media::g_flags_store.write(hal::media::kFlagsKey, &flags, sizeof(flags));
}

void activate() {
  hal::media::g_active = true;
  hal::media::g_forced_exit = false;
  hal::media::g_request_next = false;
  hal::media::g_state = State::Waiting;
}

void deactivate() {
  if (hal::media::g_state == State::Playing) hal::display::set_direct_mode(false);
  hal::media::g_active = false;
  hal::media::g_state = State::Off;
}

State state() { return hal::media::g_state; }
Mode mode() { return hal::media::g_mode; }
GateReason reason() { return hal::media::g_gate.reason(); }

void next() {
  if (hal::media::g_state != State::Playing) return;
  hal::media::g_request_next = true;
  if (hal::media::g_mode == Mode::Animation) {
    const uint8_t n = hal::sd_card::anim_count();
    if (n > 0) hal::media::g_anim = static_cast<uint8_t>((hal::media::g_anim + 1) % n);
    hal::media::g_frame = 0;
    hal::media::g_anim_done = false;
  }
}

void toggle_mode() {
  if (hal::media::g_state != State::Playing) return;
  const Mode other = hal::media::g_mode == Mode::Slideshow ? Mode::Animation : Mode::Slideshow;
  if (!hal::media::has_content(other)) return;
  hal::media::g_mode = other;
  hal::media::g_frame = 0;
  hal::media::g_anim_done = false;
  hal::media::g_failures = 0;
  hal::media::g_request_next = true;
}

bool take_forced_exit() {
  const bool f = hal::media::g_forced_exit;
  hal::media::g_forced_exit = false;
  return f;
}

Library library() {
  Library l{};
  l.scanned = hal::sd_card::status() != hal::sd_card::Status::NotMounted;
  l.card = hal::sd_card::status() == hal::sd_card::Status::Ready;
  l.photos = hal::sd_card::photo_count();
  l.animations = hal::sd_card::anim_count();
  return l;
}

}  // namespace core::media
