#include "app.h"

#include <Arduino.h>
#include <lvgl.h>

#include "../config/app_config.h"
#include "../config/board_config.h"
#include "../core/adapter_scanner.h"
#include "../core/alerts.h"
#include "../core/data_link.h"
#include "../core/i18n.h"
#include "../core/log.h"
#include "../core/obd_link.h"
#include "../core/persist.h"
#include "../core/platform.h"
#include "../core/settings.h"
#include "../core/sim_link.h"
#include "../core/trip.h"
#include "../core/util.h"
#include "../core/vehicle_state.h"
#include "../hal/backlight.h"
#include "../hal/bt_classic_transport.h"
#include "../hal/button.h"
#include "../hal/buzzer.h"
#include "../hal/display.h"
#include "../hal/light_sensor.h"
#include "../hal/media_service.h"
#include "../hal/nvs_storage.h"
#include "../hal/rgb_led.h"
#include "../hal/touch.h"
#include "../ui/ui.h"

#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "CarCYD requires the ESP32 Arduino core 3.x (Boards Manager: esp32 by Espressif Systems)"
#endif
#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR < 6
#error "CarCYD requires LVGL 9.6 or newer (9.x)"
#endif

namespace app {
namespace {

constexpr const char* kNvsNamespace = "carcyd";

hal::NvsStorage g_storage;
core::SimLink g_sim_link;
core::NullLink g_null_link;
hal::BtClassicTransport g_bt;
core::ObdLink g_obd_link(g_bt);

constexpr uint32_t kObdLogPeriodMs = 5000;  // link rate / latency report on the serial log
uint32_t g_last_obd_log_ms = 0;

bool g_media_ready = false;  // media service initialised (only when Settings > Media > Media is On)

uint32_t g_last_backlight_ms = 0;
uint32_t g_last_trip_save_ms = 0;
uint32_t g_last_critical_sound_ms = 0;
core::Severity g_sounded[core::kAlertCount] = {};

// ---- Logging -------------------------------------------------------------------------------

void serial_sink(const char* line) { Serial.println(line); }

uint32_t millis_cb() { return millis(); }

void lvgl_log_cb(lv_log_level_t, const char* text) { Serial.print(text); }

// ---- Services ------------------------------------------------------------------------------

void select_data_source(uint32_t now_ms) {
  // Connection mode: Demo (simulator) or Bluetooth Classic ELM327. Called again whenever the
  // adapter target changes; the transport reconnects by itself, the link re-initialises.
  const core::Settings& s = core::settings::get();
  core::DataLink* link = &g_sim_link;
  if (s.demo_mode) {
    g_bt.set_enabled(false);
  } else {
    g_bt.set_target(s.bt_name, s.bt_mac, s.bt_use_mac);
    g_bt.set_enabled(true);
    link = &g_obd_link;
  }
  core::link::set_active(link, now_ms);
  core::alerts().reset();
  for (auto& sounded : g_sounded) sounded = core::Severity::Normal;
}

uint8_t target_brightness() {
  const core::Settings& s = core::settings::get();
  if (!s.auto_brightness) return s.brightness;
  const float ambient = hal::light_sensor::level() / 100.0f;
  const float night = core::clamp<float>(s.night_brightness, 5.0f, s.brightness);
  return static_cast<uint8_t>(core::lerp(night, s.brightness, ambient) + 0.5f);
}

void update_backlight(uint32_t now_ms) {
  if (core::elapsed(now_ms, g_last_backlight_ms) < APP_BACKLIGHT_TICK_MS) return;
  g_last_backlight_ms = now_ms;
  hal::light_sensor::update(now_ms);
  hal::backlight::set_target(target_brightness());
  hal::backlight::update(now_ms);
  // The status LED follows the screen so it never dazzles at night.
  hal::rgb_led::set_intensity(core::clamp<uint8_t>(hal::backlight::level(), 10, 100));
}

void update_status_led(uint32_t now_ms) {
  using namespace hal::rgb_led;
  const core::Settings& s = core::settings::get();
  if (!s.status_led) {
    off();
    return;
  }
  const core::Severity alert = core::alerts().highest();
  const float rpm = core::vehicle().value_or(core::SignalId::Rpm, now_ms, 0.0f);

  if (alert == core::Severity::Critical) {
    set(colors::kRed, Pattern::Blink, 300);
  } else if (s.shift_light && rpm >= s.shift_rpm) {
    set(colors::kRed, Pattern::Blink, 120);
  } else if (s.shift_light && rpm >= s.shift_rpm - 600) {
    set(colors::kAmber);
  } else if (alert == core::Severity::Warning) {
    set(colors::kAmber, Pattern::Pulse, 2000);
  } else if (core::vehicle().link_state != core::LinkState::Connected) {
    set(colors::kBlue, Pattern::Pulse, 3000);
  } else {
    off();
  }
}

void update_alert_sounds(uint32_t now_ms) {
  core::AlertManager& alerts = core::alerts();
  bool critical_pending = false;
  for (uint8_t i = 0; i < core::kAlertCount; ++i) {
    const core::Alert& a = alerts.get(static_cast<core::AlertId>(i));
    if (!a.active()) {
      g_sounded[i] = core::Severity::Normal;
      continue;
    }
    if (a.acknowledged) continue;
    if (a.severity == core::Severity::Critical) critical_pending = true;
    if (a.severity > g_sounded[i]) {
      g_sounded[i] = a.severity;
      platform::play(a.severity == core::Severity::Critical ? platform::Sound::Critical : platform::Sound::Warning);
      if (a.severity == core::Severity::Critical) g_last_critical_sound_ms = now_ms;
    }
  }
  // Unacknowledged critical alerts keep reminding the driver.
  if (critical_pending && core::elapsed(now_ms, g_last_critical_sound_ms) >= APP_ALERT_REPEAT_MS) {
    g_last_critical_sound_ms = now_ms;
    platform::play(platform::Sound::Critical);
  }
}

/** Keeps the vehicle link and the alerts alive while a media frame decodes. */
void media_pump(uint32_t now_ms) {
  core::link::poll(now_ms);
  core::alerts().update(core::vehicle(), core::settings::get(), now_ms);
}

void handle_button(uint32_t now_ms) {
  switch (hal::button::poll(now_ms)) {
    case hal::button::Event::Short:
      ui::on_hardware_button_short();
      break;
    case hal::button::Event::Long:
      ui::navigate(ui::PageId::Dashboard);
      break;
    case hal::button::Event::None:
      break;
  }
}

void save_trip(bool force) {
  if (core::trip().take_dirty() || force) {
    core::persist::save(g_storage, core::storage_key::kTrip, core::kTripVersion, &core::trip().data(),
                        sizeof(core::TripData));
  }
}

void load_trip() {
  core::TripData data{};
  const auto result = core::persist::load(g_storage, core::storage_key::kTrip, &data, sizeof(data));
  if (result == core::persist::LoadResult::Ok || result == core::persist::LoadResult::Upgraded) {
    core::trip().restore(data);
  }
}

void on_settings_changed(uint32_t changes, void*) {
  const core::Settings& s = core::settings::get();
  if (changes & core::settings::kChangeOrientation) hal::display::set_flipped(s.flip_screen);
  if (changes & core::settings::kChangeSound) hal::buzzer::set_volume(s.volume);
  if (changes & core::settings::kChangeDataSource) select_data_source(millis());
  // Media switched on at run time: the service starts now (the card is mounted lazily by the Media page).
  if ((changes & core::settings::kChangeMediaEnable) && s.media_enabled && !g_media_ready) {
    hal::media::init(media_pump);
    g_media_ready = true;
  }
}

/** Renders the splash while the start-up continues. */
void boot_step(uint8_t percent, core::Str text) {
  ui::splash_progress(percent, core::tr(text));
  lv_timer_handler();
  lv_refr_now(nullptr);
}

void log_heap(const char* where) {
  LOG_I("HEAP", "%s: free %lu B, largest block %lu B", where, static_cast<unsigned long>(ESP.getFreeHeap()),
        static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

void halt_with_error(const char* what) {
  LOG_E("APP", "fatal: %s", what);
  pinMode(board::kLedRed, OUTPUT);
  while (true) {
    digitalWrite(board::kLedRed, !digitalRead(board::kLedRed));
    delay(250);
  }
}

#if defined(APP_UI_HARNESS)
// ---- Debug harness: loop timing + programmatic page switching over serial ----------------------
#include <esp_heap_caps.h>
uint32_t g_h_worst_loop_us = 0, g_h_worst_lv_us = 0, g_h_last_report = 0, g_h_iters = 0;
bool g_h_auto = false;
uint32_t g_h_auto_ms = 0;
int g_h_auto_idx = 0;
uint32_t g_h_nav_count = 0, g_h_lv_nav_worst = 0;

void harness_alloc_failed(size_t size, uint32_t caps, const char* fn) {
  Serial.printf("[H] ALLOC FAIL size=%u caps=0x%x fn=%s\n", (unsigned)size, (unsigned)caps, fn);
}

void harness_nav(int idx) {
  { uint32_t fu, fn; hal::display::take_flush_stats(fu, fn); Serial.printf("[H] flush total %lu us in %lu flushes\n", (unsigned long)fu, (unsigned long)fn); }
  Serial.printf("[H] prev page lv_timer_handler worst %lu us\n", (unsigned long)g_h_lv_nav_worst);
  g_h_lv_nav_worst = 0;
  const uint32_t t0 = micros();
  ui::navigate(static_cast<ui::PageId>(idx));
  const uint32_t t1 = micros();
  Serial.printf("[H] nav #%lu -> %d: navigate %lu us, heap free %lu largest %lu min %lu\n",
                (unsigned long)++g_h_nav_count, idx, (unsigned long)(t1 - t0), (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap(), (unsigned long)ESP.getMinFreeHeap());
}

void harness_poll(uint32_t now) {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c >= '0' && c <= '5') harness_nav(c - '0');
    if (c == 'a') g_h_auto = !g_h_auto;
    if (c == 'h') log_heap("manual");
    if (c == 'N') {  // forget the saved adapter (back to the default name, no MAC)
      core::Settings& st = core::settings::edit();
      memset(st.bt_mac, 0, sizeof(st.bt_mac));
      st.bt_use_mac = false;
      core::settings::commit(core::settings::kChangeDataSource);
    }
    if (c == 'l') {  // A/B for the RAM report: force media on + boot logo on (SD mounted at boot)
      core::Settings& st = core::settings::edit();
      st.media_enabled = true;
      st.boot_logo = true;
      core::settings::commit(core::settings::kChangeMediaEnable);
    }
    if (c == 'D') core::settings::restore_defaults();  // back to factory values (saved after the delay)
    if (c == 'S') g_bt.start_scan();  // user-requested inquiry
    if (c == 'k') g_bt.connect_now();  // soak test: one more round of connect attempts
    if (c == 'M') {                    // fake saved adapter MAC (nothing answers): MAC connect path
      core::Settings& st = core::settings::edit();
      const uint8_t fake[6] = {0x00, 0x1D, 0xA5, 0x12, 0x34, 0x56};
      memcpy(st.bt_mac, fake, 6);
      st.bt_use_mac = true;
      core::settings::commit(core::settings::kChangeDataSource);
    }
    if (c == 'p') {  // persistence test: change several values, flushed after the save delay
      core::Settings& st = core::settings::edit();
      st.brightness = 42;
      st.rpm_max = 7000;
      st.media_enabled = !st.media_enabled;
      st.boot_logo = !st.boot_logo;
      core::settings::commit(core::settings::kChangeDisplay | core::settings::kChangeMediaEnable);
      Serial.printf("[H] settings edited: brightness 42 rpm_max 7000 media %d boot_logo %d\n", st.media_enabled, st.boot_logo);
    }
    if (c == 'r') platform::restart();
    if (c == 's') {
      const core::Settings& st = core::settings::get();
      Serial.printf("[H] settings: brightness %u rpm_max %u media %d boot_logo %d\n", st.brightness, st.rpm_max, st.media_enabled, st.boot_logo);
    }
  }
  if (g_h_auto && now - g_h_auto_ms >= 700) {
    g_h_auto_ms = now;
    harness_nav(g_h_auto_idx);
    g_h_auto_idx = (g_h_auto_idx + 1) % static_cast<int>(ui::PageId::Count);
  }
}
#endif

}  // namespace

void setup() {
  Serial.begin(APP_SERIAL_BAUD);
  core::log::init(serial_sink, millis_cb, static_cast<core::log::Level>(APP_LOG_LEVEL));
  LOG_I("APP", "%s %s (%s) on %s, LVGL %d.%d.%d", APP_NAME, APP_VERSION, __DATE__, CYD_BOARD_NAME,
        LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

  hal::button::init();
  const bool calibration_requested = hal::button::held();

  // Settings first: they define orientation, language and brightness.
  g_storage.begin(kNvsNamespace);
  core::settings::load(g_storage);
  const core::Settings& s = core::settings::get();
  log_heap("after settings");

  // ---- Graphics -----------------------------------------------------------------------
  lv_init();
  lv_tick_set_cb(millis_cb);
  lv_log_register_print_cb(lvgl_log_cb);
#if defined(APP_UI_HARNESS)
  heap_caps_register_failed_alloc_callback(harness_alloc_failed);
#endif

  hal::backlight::init();
  lv_display_t* display = hal::display::init(s.flip_screen);
  if (display == nullptr) halt_with_error("display");
  log_heap("after display");
  ui::init(display);
  ui::splash_show();
  log_heap("after ui init");
  lv_refr_now(nullptr);
  const uint32_t splash_start = millis();

  // Media Off: no service, no SD access at all. Boot logo (/boot/logo.jpg on the SD card, <= 1.5 s,
  // falls back to the splash) is read only when explicitly enabled, so the default boot never mounts the card.
  bool custom_logo = false;
  if (s.media_enabled) {
    hal::media::init(media_pump);
    g_media_ready = true;
    if (s.boot_logo) custom_logo = hal::media::show_boot_logo();
  }

  hal::light_sensor::init();
  hal::backlight::set_target(target_brightness());  // fades in while booting

  // ---- Hardware -------------------------------------------------------------------------
  boot_step(20, core::Str::BOOT_SETTINGS);
  hal::touch::init(g_storage);
  hal::rgb_led::init();
  hal::buzzer::init();
  hal::buzzer::set_volume(s.volume);
  boot_step(50, core::Str::BOOT_HARDWARE);

  // ---- Data -------------------------------------------------------------------------------
  load_trip();
  core::adapter_scanner::set(&g_bt);
  log_heap("before data source");
  select_data_source(millis());
  core::settings::add_listener(on_settings_changed, nullptr);
  boot_step(80, core::Str::BOOT_VEHICLE);

  while (core::elapsed(millis(), splash_start) < APP_SPLASH_MIN_MS) {
    hal::backlight::update(millis());
    core::link::poll(millis());  // the adapter connects while the splash is shown
    lv_timer_handler();
    yield();
  }
  boot_step(100, core::Str::BOOT_READY);
  platform::play(platform::Sound::Startup);

  const bool run_calibration = calibration_requested || !hal::touch::calibrated();
  if (run_calibration) LOG_I("APP", "touch calibration %s", calibration_requested ? "requested" : "required");
  if (custom_logo) hal::media::end_boot_logo();
  ui::start(run_calibration);
  g_last_trip_save_ms = millis();
  log_heap("ready");
  LOG_I("APP", "ready, free heap %lu B", static_cast<unsigned long>(ESP.getFreeHeap()));
}

void loop() {
  const uint32_t now = millis();
#if defined(APP_UI_HARNESS)
  const uint32_t h_t0 = micros();
  harness_poll(now);
#endif

  core::link::poll(now);
  core::alerts().update(core::vehicle(), core::settings::get(), now);
  if (g_media_ready && core::settings::get().media_enabled) hal::media::poll(now);
  core::trip().update(core::vehicle(), core::settings::get(), now);

  update_alert_sounds(now);
  update_status_led(now);
  update_backlight(now);
  handle_button(now);
  hal::buzzer::update(now);
  hal::rgb_led::update(now);

  core::settings::service(now);
  if (core::elapsed(now, g_last_trip_save_ms) >= APP_TRIP_SAVE_PERIOD_MS) {
    g_last_trip_save_ms = now;
    save_trip(false);
  }

  if (core::elapsed(now, g_last_obd_log_ms) >= kObdLogPeriodMs && core::link::active() == &g_obd_link) {
    g_last_obd_log_ms = now;
    char stats[160];
    g_obd_link.format_stats(stats, sizeof(stats));
    LOG_I("OBD", "%s", stats);
  }

  // Cooperative loop: no delay(), so the OBD link is polled as often as possible.
#if defined(APP_UI_HARNESS)
  const uint32_t h_lv0 = micros();
  lv_timer_handler();
  const uint32_t h_t1 = micros();
  if (h_t1 - h_lv0 > g_h_worst_lv_us) g_h_worst_lv_us = h_t1 - h_lv0;
  if (h_t1 - h_lv0 > g_h_lv_nav_worst) g_h_lv_nav_worst = h_t1 - h_lv0;
  yield();
  const uint32_t h_t2 = micros();
  if (h_t2 - h_t0 > g_h_worst_loop_us) g_h_worst_loop_us = h_t2 - h_t0;
  g_h_iters++;
  if (now - g_h_last_report >= 1000) {
    g_h_last_report = now;
    Serial.printf("[H] loop worst %lu us, lv_timer_handler worst %lu us, iters/s %lu, heap %lu/%lu stackmin %lu\n",
                  (unsigned long)g_h_worst_loop_us, (unsigned long)g_h_worst_lv_us, (unsigned long)g_h_iters,
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap(),
                  (unsigned long)uxTaskGetStackHighWaterMark(NULL));
    g_h_worst_loop_us = g_h_worst_lv_us = g_h_iters = 0;
  }
#else
  lv_timer_handler();
  yield();
#endif
}

}  // namespace app

// ---- Platform services that need application data ------------------------------------------

namespace platform {

void restart() {
  LOG_I("APP", "restart requested");
  core::settings::flush();
  app::save_trip(true);
  delay(100);
  ESP.restart();
  while (true) {
  }
}

void factory_reset() {
  LOG_W("APP", "factory reset");
  app::g_storage.erase_all();
  delay(100);
  ESP.restart();
  while (true) {
  }
}

}  // namespace platform
