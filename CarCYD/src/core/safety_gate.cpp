#include "safety_gate.h"

#include <math.h>

namespace core {

namespace {

/** Hard blocks, in priority order. Returns Allowed if none applies. */
GateReason hard_block(const GateInput& in, float stop_kmh) {
  if (in.alert_critical) return GateReason::CriticalAlert;

  if (in.demo_mode) return GateReason::Allowed;  // simulated car: nothing to protect

  const bool speed_known = in.link_connected && in.speed_fresh && !isnan(in.speed_kmh);
  if (speed_known) return in.speed_kmh < stop_kmh ? GateReason::Allowed : GateReason::Moving;
  if (in.link_connected) return GateReason::SpeedStale;  // link up but speed unknown: bench mode does not help
  if (in.bench_mode) return GateReason::Allowed;         // no link at all, explicit desk mode
  return GateReason::LinkDown;
}

}  // namespace

bool SafetyGate::update(const GateInput& in) {
  const GateReason block = hard_block(in, stop_kmh_);
  if (block != GateReason::Allowed) {
    safe_ = false;  // immediate block, settle timer restarts
    reason_ = block;
    return false;
  }

  if (!safe_) {
    safe_ = true;
    safe_since_ = in.now_ms;
  }
  // Wrap-safe elapsed time.
  reason_ = (in.now_ms - safe_since_) >= settle_ms_ ? GateReason::Allowed : GateReason::Settling;
  return allowed();
}

void SafetyGate::reset() {
  safe_ = false;
  reason_ = GateReason::SpeedStale;
}

}  // namespace core
