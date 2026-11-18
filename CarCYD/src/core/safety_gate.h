/**
 * @file safety_gate.h
 * Decides whether decorative media (photos, animations) may be shown.
 *
 * Pure logic (no Arduino, no globals): fed once per main-loop iteration with the
 * current vehicle facts. Media is allowed only while the car is standing still
 * with a fresh speed reading, and is blocked at once (same call) when that stops
 * being true. Becoming allowed needs the safe condition to hold for kSettleMs
 * (hysteresis), so a speed reading that flickers around zero cannot flash media.
 */
#pragma once

#include <stdint.h>

namespace core {

/** Below this speed the vehicle counts as standing still (km/h). */
constexpr float kGateStopKmh = 1.0f;
/** A speed reading older than this is unknown (stricter than core::kSignalStaleMs). */
constexpr uint32_t kGateMaxSpeedAgeMs = 400;
/** The safe condition must hold this long before media is allowed. */
constexpr uint32_t kGateSettleMs = 1000;

enum class GateReason : uint8_t {
  Allowed = 0,
  Settling,       ///< standing still, waiting for the settle time
  Moving,         ///< speed above the threshold
  SpeedStale,     ///< speed unknown (no fresh reading)
  LinkDown,       ///< vehicle link not connected
  CriticalAlert,  ///< a critical alert is active
};

struct GateInput {
  float speed_kmh = 0.0f;
  bool speed_fresh = false;
  bool link_connected = false;
  bool alert_critical = false;
  bool bench_mode = false;  ///< explicit "desk mode": tolerated only while the link is NOT connected
  bool demo_mode = false;   ///< simulated vehicle: speed is meaningless and ignored
  uint32_t now_ms = 0;
};

/** Freshness rule for the speed reading (wrap-safe). */
inline bool gate_speed_fresh(bool valid, uint32_t updated_ms, uint32_t now_ms) {
  return valid && (now_ms - updated_ms) <= kGateMaxSpeedAgeMs;
}

class SafetyGate {
 public:
  explicit SafetyGate(uint32_t settle_ms = kGateSettleMs, float stop_kmh = kGateStopKmh)
      : settle_ms_(settle_ms), stop_kmh_(stop_kmh) {}

  /** Evaluates the inputs; call every loop iteration. Returns allowed(). */
  bool update(const GateInput& in);

  bool allowed() const { return reason_ == GateReason::Allowed; }
  GateReason reason() const { return reason_; }

  /** Forgets the settle timer (next update starts settling again). */
  void reset();

 private:
  uint32_t settle_ms_;
  float stop_kmh_;
  bool safe_ = false;       ///< safe condition currently true
  uint32_t safe_since_ = 0;
  GateReason reason_ = GateReason::SpeedStale;
};

}  // namespace core
