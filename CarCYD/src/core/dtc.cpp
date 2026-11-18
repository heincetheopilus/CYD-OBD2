#include "dtc.h"

#include <ctype.h>
#include <stdio.h>

namespace core::dtc {
namespace {

struct Entry {
  uint16_t code;
  Severity severity;
  const char* en;
};

constexpr Severity I = Severity::Info;
constexpr Severity W = Severity::Warning;
constexpr Severity C = Severity::Critical;

// Sorted by code (binary search). Descriptions follow SAE J2012 wording.
// clang-format off
const Entry kDatabase[] = {
  {0x0100, W, "Mass air flow circuit malfunction"},
  {0x0101, W, "Mass air flow circuit range/performance"},
  {0x0102, W, "Mass air flow circuit low input"},
  {0x0103, W, "Mass air flow circuit high input"},
  {0x0106, W, "Manifold pressure circuit range/performance"},
  {0x0107, W, "Manifold pressure circuit low input"},
  {0x0108, W, "Manifold pressure circuit high input"},
  {0x0110, W, "Intake air temperature circuit malfunction"},
  {0x0112, W, "Intake air temperature circuit low input"},
  {0x0113, W, "Intake air temperature circuit high input"},
  {0x0115, W, "Engine coolant temperature circuit malfunction"},
  {0x0116, W, "Engine coolant temperature circuit range/performance"},
  {0x0117, W, "Engine coolant temperature circuit low input"},
  {0x0118, W, "Engine coolant temperature circuit high input"},
  {0x0120, W, "Throttle position sensor A circuit malfunction"},
  {0x0121, W, "Throttle position sensor A range/performance"},
  {0x0122, W, "Throttle position sensor A circuit low input"},
  {0x0123, W, "Throttle position sensor A circuit high input"},
  {0x0125, I, "Insufficient coolant temperature for closed loop fuel control"},
  {0x0128, I, "Coolant thermostat below regulating temperature"},
  {0x0130, W, "O2 sensor circuit malfunction (bank 1, sensor 1)"},
  {0x0131, W, "O2 sensor circuit low voltage (bank 1, sensor 1)"},
  {0x0132, W, "O2 sensor circuit high voltage (bank 1, sensor 1)"},
  {0x0133, W, "O2 sensor circuit slow response (bank 1, sensor 1)"},
  {0x0134, W, "O2 sensor circuit no activity (bank 1, sensor 1)"},
  {0x0135, W, "O2 sensor heater circuit malfunction (bank 1, sensor 1)"},
  {0x0141, W, "O2 sensor heater circuit malfunction (bank 1, sensor 2)"},
  {0x0171, W, "System too lean (bank 1)"},
  {0x0172, W, "System too rich (bank 1)"},
  {0x0174, W, "System too lean (bank 2)"},
  {0x0175, W, "System too rich (bank 2)"},
  {0x0200, W, "Injector circuit malfunction"},
  {0x0201, W, "Injector circuit malfunction, cylinder 1"},
  {0x0202, W, "Injector circuit malfunction, cylinder 2"},
  {0x0203, W, "Injector circuit malfunction, cylinder 3"},
  {0x0204, W, "Injector circuit malfunction, cylinder 4"},
  {0x0217, C, "Engine coolant over temperature condition"},
  {0x0219, C, "Engine overspeed condition"},
  {0x0230, W, "Fuel pump primary circuit malfunction"},
  {0x0234, C, "Turbocharger overboost condition"},
  {0x0299, W, "Turbocharger underboost condition"},
  {0x0300, C, "Random/multiple cylinder misfire detected"},
  {0x0301, C, "Cylinder 1 misfire detected"},
  {0x0302, C, "Cylinder 2 misfire detected"},
  {0x0303, C, "Cylinder 3 misfire detected"},
  {0x0304, C, "Cylinder 4 misfire detected"},
  {0x0305, C, "Cylinder 5 misfire detected"},
  {0x0306, C, "Cylinder 6 misfire detected"},
  {0x0325, W, "Knock sensor 1 circuit malfunction"},
  {0x0327, W, "Knock sensor 1 circuit low input"},
  {0x0335, C, "Crankshaft position sensor A circuit malfunction"},
  {0x0340, C, "Camshaft position sensor A circuit malfunction"},
  {0x0351, W, "Ignition coil A primary/secondary circuit malfunction"},
  {0x0352, W, "Ignition coil B primary/secondary circuit malfunction"},
  {0x0353, W, "Ignition coil C primary/secondary circuit malfunction"},
  {0x0354, W, "Ignition coil D primary/secondary circuit malfunction"},
  {0x0400, W, "Exhaust gas recirculation flow malfunction"},
  {0x0401, W, "Exhaust gas recirculation flow insufficient"},
  {0x0402, W, "Exhaust gas recirculation flow excessive"},
  {0x0420, W, "Catalyst system efficiency below threshold (bank 1)"},
  {0x0430, W, "Catalyst system efficiency below threshold (bank 2)"},
  {0x0440, I, "Evaporative emission system malfunction"},
  {0x0441, I, "Evaporative emission system incorrect purge flow"},
  {0x0442, I, "Evaporative emission system leak detected (small)"},
  {0x0446, I, "Evaporative emission vent control circuit malfunction"},
  {0x0455, I, "Evaporative emission system leak detected (large)"},
  {0x0456, I, "Evaporative emission system leak detected (very small)"},
  {0x0500, W, "Vehicle speed sensor malfunction"},
  {0x0505, W, "Idle control system malfunction"},
  {0x0506, I, "Idle control system RPM lower than expected"},
  {0x0507, I, "Idle control system RPM higher than expected"},
  {0x0520, C, "Engine oil pressure sensor/switch circuit malfunction"},
  {0x0524, C, "Engine oil pressure too low"},
  {0x0562, W, "System voltage low"},
  {0x0563, W, "System voltage high"},
  {0x0600, W, "Serial communication link malfunction"},
  {0x0601, C, "Control module memory checksum error"},
  {0x0606, C, "Control module processor fault"},
  {0x0700, W, "Transmission control system malfunction"},
  {0x0715, W, "Input/turbine speed sensor circuit malfunction"},
  {0x0720, W, "Output speed sensor circuit malfunction"},
  {0x0730, W, "Incorrect gear ratio"},
  {0x0741, W, "Torque converter clutch performance or stuck off"},
  {0x4035, W, "Left front wheel speed sensor circuit"},
  {0x4040, W, "Right front wheel speed sensor circuit"},
  {0x4045, W, "Left rear wheel speed sensor circuit"},
  {0x4050, W, "Right rear wheel speed sensor circuit"},
  {0xC100, C, "Lost communication with ECM/PCM"},
  {0xC101, W, "Lost communication with TCM"},
  {0xC121, W, "Lost communication with ABS control module"},
  {0xC140, W, "Lost communication with body control module"},
  {0xC155, I, "Lost communication with instrument cluster"},
};
// clang-format on

constexpr size_t kDatabaseSize = sizeof(kDatabase) / sizeof(kDatabase[0]);

const Entry* find(uint16_t code) {
  size_t lo = 0;
  size_t hi = kDatabaseSize;
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    if (kDatabase[mid].code == code) return &kDatabase[mid];
    if (kDatabase[mid].code < code) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return nullptr;
}

uint8_t first_digit(uint16_t code) { return (code >> 12) & 0x3; }
uint8_t second_digit(uint16_t code) { return (code >> 8) & 0xF; }

}  // namespace

void format(uint16_t code, char* out, size_t len) {
  static const char kSystems[] = {'P', 'C', 'B', 'U'};
  snprintf(out, len, "%c%u%03X", kSystems[code >> 14], static_cast<unsigned>(first_digit(code)),
           static_cast<unsigned>(code & 0x0FFF));
}

bool parse(const char* text, uint16_t& code) {
  if (text == nullptr) return false;
  uint16_t system;
  switch (toupper(static_cast<unsigned char>(text[0]))) {
    case 'P': system = 0; break;
    case 'C': system = 1; break;
    case 'B': system = 2; break;
    case 'U': system = 3; break;
    default: return false;
  }
  if (text[1] < '0' || text[1] > '3') return false;
  uint16_t value = static_cast<uint16_t>((system << 14) | ((text[1] - '0') << 12));
  for (int i = 2; i < 5; ++i) {
    const char c = static_cast<char>(toupper(static_cast<unsigned char>(text[i])));
    uint16_t nibble;
    if (c >= '0' && c <= '9') {
      nibble = c - '0';
    } else if (c >= 'A' && c <= 'F') {
      nibble = c - 'A' + 10;
    } else {
      return false;
    }
    value |= nibble << ((4 - i) * 4);
  }
  if (text[5] != '\0') return false;
  code = value;
  return true;
}

System system_of(uint16_t code) { return static_cast<System>(code >> 14); }

Str system_name(uint16_t code) {
  switch (system_of(code)) {
    case System::Chassis: return Str::DTC_SYS_CHASSIS;
    case System::Body: return Str::DTC_SYS_BODY;
    case System::Network: return Str::DTC_SYS_NETWORK;
    case System::Powertrain:
    default: return Str::DTC_SYS_POWERTRAIN;
  }
}

bool is_generic(uint16_t code) {
  const uint8_t d1 = first_digit(code);
  if (system_of(code) == System::Powertrain) {
    // P0xxx, P2xxx and P34xx-P39xx are SAE controlled.
    if (d1 == 0 || d1 == 2) return true;
    if (d1 == 3) return second_digit(code) >= 4;
    return false;
  }
  return d1 == 0 || d1 == 3;
}

Severity severity_of(uint16_t code) {
  if (const Entry* e = find(code)) return e->severity;
  return Severity::Warning;
}

const char* describe(uint16_t code) {
  if (const Entry* e = find(code)) return e->en;

  switch (system_of(code)) {
    case System::Chassis: return "Chassis system fault";
    case System::Body: return "Body system fault";
    case System::Network: return "Network communication fault";
    case System::Powertrain: break;
  }

  if (!is_generic(code)) return "Manufacturer specific powertrain fault";
  if (first_digit(code) != 0) return "Fuel, air or emission control fault";

  switch (second_digit(code)) {
    case 0x0: return "Fuel and air metering, auxiliary emission control";
    case 0x1: return "Fuel and air metering";
    case 0x2: return "Fuel and air metering (injector circuit)";
    case 0x3: return "Ignition system or misfire";
    case 0x4: return "Auxiliary emission controls";
    case 0x5: return "Vehicle speed, idle control or auxiliary inputs";
    case 0x6: return "Control module or output circuit";
    case 0x7:
    case 0x8:
    case 0x9: return "Transmission";
    case 0xA: return "Hybrid propulsion system";
    default: return "Powertrain fault";
  }
}

}  // namespace core::dtc
