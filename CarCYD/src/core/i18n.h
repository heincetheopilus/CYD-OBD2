/**
 * @file i18n.h
 * User interface strings (English only).
 *
 * Strings are declared once in I18N_STRINGS as X(id, english). Source files are UTF-8.
 */
#pragma once

#include <stdint.h>

#include "types.h"

// clang-format off
#define I18N_STRINGS(X) \
  /* ---- Common ---------------------------------------------------------- */ \
  X(APP_TAGLINE,          "Vehicle Dashboard") \
  X(OK,                   "OK") \
  X(CANCEL,               "Cancel") \
  X(CLOSE,                "Close") \
  X(ON,                   "On") \
  X(OFF,                  "Off") \
  X(RESET,                "Reset") \
  X(AUTO,                 "Auto") \
  X(JUST_NOW,             "just now") \
  X(FMT_SECONDS_AGO,      "%lu s ago") \
  X(FMT_MINUTES_AGO,      "%lu min ago") \
  X(FMT_HOURS_AGO,        "%lu h ago") \
  /* ---- Boot ------------------------------------------------------------ */ \
  X(BOOT_SETTINGS,        "Loading settings") \
  X(BOOT_HARDWARE,        "Starting hardware") \
  X(BOOT_VEHICLE,         "Connecting to vehicle") \
  X(BOOT_READY,           "Ready") \
  /* ---- Pages ----------------------------------------------------------- */ \
  X(PAGE_DASHBOARD,       "Dashboard") \
  X(PAGE_LIVE,            "Live Data") \
  X(PAGE_DIAG,            "Diagnostics") \
  X(PAGE_TRIP,            "Trip") \
  X(PAGE_SETTINGS,        "Settings") \
  X(PAGE_MEDIA,           "Media") \
  /* ---- Signals: full names -------------------------------------------- */ \
  X(SIG_SPEED,            "Vehicle speed") \
  X(SIG_RPM,              "Engine speed") \
  X(SIG_COOLANT,          "Coolant temperature") \
  X(SIG_OIL_TEMP,         "Oil temperature") \
  X(SIG_INTAKE_TEMP,      "Intake air temperature") \
  X(SIG_AMBIENT_TEMP,     "Outside temperature") \
  X(SIG_BATTERY,          "Battery voltage") \
  X(SIG_FUEL_LEVEL,       "Fuel level") \
  X(SIG_LOAD,             "Engine load") \
  X(SIG_THROTTLE,         "Throttle position") \
  X(SIG_MAP,              "Manifold pressure") \
  X(SIG_MAF,              "Mass air flow") \
  X(SIG_TIMING,           "Ignition timing") \
  X(SIG_FUEL_RATE,        "Fuel rate") \
  X(SIG_STFT,             "Short term fuel trim") \
  X(SIG_LTFT,             "Long term fuel trim") \
  X(SIG_RUN_TIME,         "Engine run time") \
  X(SIG_GEAR,             "Gear") \
  /* ---- Signals: short names (tiles) ----------------------------------- */ \
  X(SHORT_SPEED,          "SPEED") \
  X(SHORT_RPM,            "RPM") \
  X(SHORT_COOLANT,        "COOLANT") \
  X(SHORT_OIL_TEMP,       "OIL") \
  X(SHORT_INTAKE_TEMP,    "INTAKE") \
  X(SHORT_AMBIENT_TEMP,   "OUTSIDE") \
  X(SHORT_BATTERY,        "BATTERY") \
  X(SHORT_FUEL_LEVEL,     "FUEL") \
  X(SHORT_LOAD,           "LOAD") \
  X(SHORT_THROTTLE,       "THROTTLE") \
  X(SHORT_MAP,            "MAP") \
  X(SHORT_MAF,            "MAF") \
  X(SHORT_TIMING,         "TIMING") \
  X(SHORT_FUEL_RATE,      "FUEL RATE") \
  X(SHORT_STFT,           "STFT") \
  X(SHORT_LTFT,           "LTFT") \
  X(SHORT_RUN_TIME,       "RUN TIME") \
  X(SHORT_GEAR,           "GEAR") \
  /* ---- Dashboard ------------------------------------------------------- */ \
  X(DASH_PICK_TITLE,      "Show on this gauge") \
  /* ---- Live data ------------------------------------------------------- */ \
  X(LIVE_MIN,             "Min") \
  X(LIVE_AVG,             "Avg") \
  X(LIVE_MAX,             "Max") \
  X(LIVE_NO_DATA,         "No data") \
  /* ---- Diagnostics ----------------------------------------------------- */ \
  X(DIAG_MIL_CAPTION,     "Check engine") \
  X(DIAG_MIL_ON,          "ON") \
  X(DIAG_MIL_OFF,         "OFF") \
  X(DIAG_MIL_UNKNOWN,     "Unknown") \
  X(DIAG_FMT_CODE_ONE,    "%u fault code") \
  X(DIAG_FMT_CODE_MANY,   "%u fault codes") \
  X(DIAG_FMT_LAST_SCAN,   "Last scan %s") \
  X(DIAG_NEVER_SCANNED,   "Not scanned yet") \
  X(DIAG_SCAN,            "Scan") \
  X(DIAG_CLEAR,           "Clear") \
  X(DIAG_SCANNING,        "Reading fault codes…") \
  X(DIAG_CLEARING,        "Clearing fault codes…") \
  X(DIAG_EMPTY_TITLE,     "No fault codes") \
  X(DIAG_EMPTY_TEXT,      "The ECU reports no stored or pending codes.") \
  X(DIAG_IDLE_TITLE,      "Ready to scan") \
  X(DIAG_IDLE_TEXT,       "Tap Scan to read the fault codes from the ECU.") \
  X(DIAG_CLEAR_TITLE,     "Clear fault codes?") \
  X(DIAG_CLEAR_TEXT,      "Stored codes and freeze frame data will be erased and readiness monitors reset. Ignition on, engine off.") \
  X(DIAG_CLEARED,         "Fault codes cleared") \
  X(DIAG_SCAN_DONE,       "Scan complete") \
  X(DIAG_FAILED,          "The ECU did not respond") \
  X(DIAG_NOT_CONNECTED,   "Not connected to the vehicle") \
  X(DTC_STORED,           "Stored") \
  X(DTC_PENDING,          "Pending") \
  X(DTC_PERMANENT,        "Permanent") \
  X(DTC_SYS_POWERTRAIN,   "Powertrain") \
  X(DTC_SYS_CHASSIS,      "Chassis") \
  X(DTC_SYS_BODY,         "Body") \
  X(DTC_SYS_NETWORK,      "Network") \
  X(DTC_GENERIC,          "Generic (SAE J2012)") \
  X(DTC_MANUFACTURER,     "Manufacturer specific") \
  X(DTC_SYSTEM,           "System") \
  X(DTC_TYPE,             "Code type") \
  X(DTC_STATUS,           "Status") \
  X(DTC_ADVICE_CRITICAL,  "Critical: stop safely and have the vehicle inspected.") \
  X(DTC_ADVICE_WARNING,   "Have the vehicle inspected soon.") \
  X(DTC_ADVICE_INFO,      "Minor fault. Monitor and check at the next service.") \
  /* ---- Trip ------------------------------------------------------------ */ \
  X(TRIP_DISTANCE,        "Distance") \
  X(TRIP_TIME,            "Drive time") \
  X(TRIP_AVG_SPEED,       "Avg speed") \
  X(TRIP_MAX_SPEED,       "Max speed") \
  X(TRIP_AVG_CONS,        "Avg consumption") \
  X(TRIP_FUEL_USED,       "Fuel used") \
  X(TRIP_ACCEL,           "Acceleration") \
  X(TRIP_LAST,            "Last") \
  X(TRIP_BEST,            "Best") \
  X(TRIP_ACCEL_IDLE,      "Stop to arm") \
  X(TRIP_ACCEL_ARMED,     "Armed, go!") \
  X(TRIP_ACCEL_RUNNING,   "Measuring…") \
  X(TRIP_RESET_TITLE,     "Reset trip?") \
  X(TRIP_RESET_TEXT,      "Distance, time, consumption and speed statistics will be reset.") \
  X(TRIP_RESET_DONE,      "Trip reset") \
  /* ---- Settings: categories -------------------------------------------- */ \
  X(SET_DISPLAY,          "Display") \
  X(SET_UNITS,            "Units") \
  X(SET_GAUGES,           "Gauges") \
  X(SET_WARNINGS,         "Warnings") \
  X(SET_SOUND,            "Sound & light") \
  X(SET_CONNECTION,       "Connection") \
  X(SET_SYSTEM,           "System") \
  X(SET_ABOUT,            "About") \
  X(SET_MEDIA,            "Media") \
  /* ---- Settings: display ----------------------------------------------- */ \
  X(SET_BRIGHTNESS,       "Brightness") \
  X(SET_AUTO_BRIGHTNESS,  "Auto brightness") \
  X(SET_AUTO_BRIGHTNESS_SUB, "Uses the ambient light sensor") \
  X(SET_DAY_BRIGHTNESS,   "Day brightness") \
  X(SET_NIGHT_BRIGHTNESS, "Night brightness") \
  X(SET_AMBIENT,          "Ambient light") \
  X(SET_FLIP,             "Rotate screen 180°") \
  X(SET_ACCENT,           "Accent color") \
  /* ---- Settings: units ------------------------------------------------- */ \
  X(SET_UNIT_SPEED,       "Speed & distance") \
  X(SET_UNIT_TEMP,        "Temperature") \
  X(SET_UNIT_PRESSURE,    "Pressure") \
  /* ---- Settings: gauges ------------------------------------------------ */ \
  X(SET_RPM_MAX,          "Tachometer range") \
  X(SET_REDLINE,          "Redline") \
  X(SET_SHIFT_LIGHT,      "Shift light") \
  X(SET_SHIFT_RPM,        "Shift point") \
  X(SET_LAYOUT_RESET,     "Reset gauge layout") \
  X(SET_LAYOUT_HINT,      "Long-press any dashboard value to change it.") \
  X(SET_LAYOUT_DONE,      "Gauge layout restored") \
  /* ---- Settings: warnings ---------------------------------------------- */ \
  X(SET_COOLANT_WARN,     "Coolant temperature") \
  X(SET_VOLTAGE_WARN,     "Low battery voltage") \
  X(SET_FUEL_WARN,        "Low fuel level") \
  X(SET_OVERSPEED,        "Speed limit") \
  /* ---- Settings: sound & light ----------------------------------------- */ \
  X(SET_TOUCH_SOUND,      "Touch sounds") \
  X(SET_ALERT_SOUND,      "Warning sounds") \
  X(SET_VOLUME,           "Volume") \
  X(SET_STATUS_LED,       "Status LED") \
  X(SET_STATUS_LED_SUB,   "Shift light and warnings") \
  /* ---- Settings: connection -------------------------------------------- */ \
  X(SET_DATA_SOURCE,      "Data source") \
  X(SET_SRC_DEMO,         "Demo") \
  X(SET_SRC_LINK,         "Bluetooth") \
  X(SET_LINK_STATUS,      "Status") \
  X(SET_LINK_INTERFACE,   "Interface") \
  X(SET_DEMO_HINT,        "Demo mode simulates a driving car. Select Bluetooth to read a Bluetooth Classic ELM327 adapter.") \
  X(LINK_CONNECTED,       "Connected") \
  X(LINK_CONNECTING,      "Connecting…") \
  X(LINK_DISCONNECTED,    "Not connected") \
  X(LINK_ERROR,           "Error") \
  X(LINK_IDLE,            "Not connected - tap Connect") \
  X(LINK_SEARCHING,       "Searching adapter…") \
  X(LINK_INITIALIZING,    "Initializing…") \
  X(LINK_NO_ECU,          "Adapter OK, no vehicle reply") \
  X(SET_LINK_RATE,        "Refresh rate") \
  X(SET_BT_ADAPTER,       "Adapter") \
  X(SET_BT_DEFAULT_NAME,  "OBDII (default name)") \
  X(SET_BT_USE_DEFAULT,   "Use default name (OBDII)") \
  X(SET_BT_SCAN,          "Scan for adapters") \
  X(SET_BT_CONNECT,       "Connect") \
  X(SET_BT_SCANNING,      "Scanning…") \
  X(SET_BT_SCAN_NONE,     "No devices found") \
  X(SET_BT_SELECTED,      "Adapter selected") \
  X(SET_BT_HINT,          "Plug the adapter in, ignition on, and make sure no phone is connected to it. PIN 1234 / 0000 is tried automatically.") \
  /* ---- Settings: system ------------------------------------------------ */ \
  X(SET_LANGUAGE,         "Language") \
  X(SET_CALIBRATE,        "Touch calibration") \
  X(SET_RESTART,          "Restart device") \
  X(SET_RESTART_TEXT,     "The device will restart now.") \
  X(SET_FACTORY_RESET,    "Factory reset") \
  X(SET_FACTORY_TEXT,     "All settings, trip data and the touch calibration will be erased. The device will restart.") \
  /* ---- Settings: about ------------------------------------------------- */ \
  X(ABOUT_FIRMWARE,       "Firmware") \
  X(ABOUT_BUILD,          "Build") \
  X(ABOUT_BOARD,          "Board") \
  X(ABOUT_DISPLAY,        "Display") \
  X(ABOUT_CHIP,           "Processor") \
  X(ABOUT_FLASH,          "Flash") \
  X(ABOUT_MEMORY,         "Free memory") \
  X(ABOUT_UPTIME,         "Uptime") \
  X(ABOUT_GRAPHICS,       "Graphics") \
  /* ---- Media (photos / animations while parked) ------------------------ */ \
  X(SET_MEDIA_ENABLE,     "Media") \
  X(SET_MEDIA_ENABLE_SUB, "Media tab and SD card") \
  X(SET_BOOT_LOGO,        "Boot logo") \
  X(SET_BOOT_LOGO_SUB,    "/boot/logo.jpg from SD, next start") \
  X(SET_MEDIA_INTERVAL,   "Photo interval") \
  X(SET_MEDIA_SHUFFLE,    "Shuffle photos") \
  X(SET_MEDIA_FPS,        "Max animation speed") \
  X(SET_MEDIA_BENCH,      "Desk mode") \
  X(SET_MEDIA_BENCH_SUB,  "Allow media without a vehicle link") \
  X(SET_MEDIA_SD,         "SD card") \
  X(SET_MEDIA_HINT,       "Photos and animations are shown only while the vehicle is stopped. Prepare the card with tools/media_prep.") \
  X(MEDIA_FMT_LIBRARY,    "%u photos · %u animations") \
  X(MEDIA_NO_CARD_SHORT,  "No card") \
  X(MEDIA_NOT_SCANNED,    "Not scanned yet") \
  X(MEDIA_NO_CARD,        "No SD card") \
  X(MEDIA_NO_CARD_TEXT,   "Insert a FAT32 card with a /photos folder.") \
  X(MEDIA_NO_MEDIA,       "No pictures found") \
  X(MEDIA_NO_MEDIA_TEXT,  "Put JPG files into /photos or frame folders into /anim.") \
  X(MEDIA_BLOCKED_MOVING, "Blocked: vehicle moving") \
  X(MEDIA_BLOCKED_UNKNOWN, "Blocked: speed unknown") \
  X(MEDIA_BLOCKED_ALERT,  "Blocked: critical alert") \
  X(MEDIA_SETTLING,       "Waiting for the vehicle to stop…") \
  /* ---- Touch calibration ----------------------------------------------- */ \
  X(CAL_TITLE,            "Touch calibration") \
  X(CAL_TAP,              "Tap the center of the target") \
  X(CAL_VERIFY,           "Tap the target once more to verify") \
  X(CAL_FMT_STEP,         "Point %u of %u") \
  X(CAL_HINT,             "A stylus or fingernail gives the best accuracy") \
  X(CAL_DONE,             "Calibration saved") \
  X(CAL_FAILED,           "Not accurate enough, please try again") \
  /* ---- Alerts ---------------------------------------------------------- */ \
  X(ALERT_COOLANT,        "Engine temperature high") \
  X(ALERT_OIL,            "Oil temperature high") \
  X(ALERT_BATTERY_LOW,    "Battery voltage low") \
  X(ALERT_BATTERY_HIGH,   "Charging voltage high") \
  X(ALERT_FUEL,           "Fuel level low") \
  X(ALERT_OVERSPEED,      "Speed limit exceeded") \
  X(ALERT_MIL,            "Check engine light on") \
  X(ALERT_MIL_TEXT,       "Open Diagnostics to read the fault codes") \
  X(ALERT_LINK,           "Vehicle connection lost") \
  X(ALERT_LINK_TEXT,      "Check the vehicle interface") \
  X(ALERT_TAP_HINT,       "Tap to dismiss") \
  X(SETTINGS_RESTORED,    "Settings restored to defaults")
// clang-format on

namespace core {

enum class Str : uint16_t {
#define I18N_ENUM(id, en) id,
  I18N_STRINGS(I18N_ENUM)
#undef I18N_ENUM
  Count
};

namespace i18n {

void set_language(Language lang);
Language language();

/** Returns the UTF-8 text of `id` in the active language. Never returns null. */
const char* get(Str id);

/** Native name of a language, independent of the active language. */
const char* language_name(Language lang);

}  // namespace i18n

/** Shorthand used throughout the UI. */
inline const char* tr(Str id) { return i18n::get(id); }

}  // namespace core
