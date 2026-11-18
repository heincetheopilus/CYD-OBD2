// Host test for core::SafetyGate. Build: see tools/tests/run_tests.sh
#include <stdio.h>
#include <math.h>

#include "../../CarCYD/src/core/safety_gate.h"

using namespace core;

static int g_fail = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    if (!(c)) {                                                          \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);               \
      ++g_fail;                                                          \
    }                                                                    \
  } while (0)

static GateInput stopped(uint32_t t) {
  GateInput in;
  in.speed_kmh = 0;
  in.speed_fresh = true;
  in.link_connected = true;
  in.now_ms = t;
  return in;
}

int main() {
  {  // stopped: Settling for 1 s, then Allowed
    SafetyGate g;
    CHECK(!g.update(stopped(0)));
    CHECK(g.reason() == GateReason::Settling);
    CHECK(!g.update(stopped(999)));
    CHECK(g.update(stopped(1000)));
    CHECK(g.reason() == GateReason::Allowed);
  }
  {  // moving blocks in the same call; re-entry needs a full settle
    SafetyGate g;
    g.update(stopped(0));
    CHECK(g.update(stopped(1500)));
    GateInput mv = stopped(1520);
    mv.speed_kmh = 10;
    CHECK(!g.update(mv));
    CHECK(g.reason() == GateReason::Moving);
    CHECK(!g.update(stopped(1540)));
    CHECK(!g.update(stopped(2500)));
    CHECK(g.update(stopped(2540)));
  }
  {  // threshold: 0.9 stopped, 1.0 moving
    SafetyGate g;
    GateInput in = stopped(0);
    in.speed_kmh = 0.9f;
    g.update(in);
    in.now_ms = 1000;
    CHECK(g.update(in));
    in.speed_kmh = 1.0f;
    in.now_ms = 1020;
    CHECK(!g.update(in));
    CHECK(g.reason() == GateReason::Moving);
  }
  {  // flicker around zero restarts the settle timer
    SafetyGate g;
    g.update(stopped(0));
    GateInput mv = stopped(800);
    mv.speed_kmh = 5;
    g.update(mv);
    CHECK(!g.update(stopped(1000)));
    CHECK(!g.update(stopped(1999)));
    CHECK(g.update(stopped(2000)));
  }
  {  // stale speed / link down block (not bench)
    SafetyGate g;
    g.update(stopped(0));
    CHECK(g.update(stopped(1000)));
    GateInput st = stopped(1020);
    st.speed_fresh = false;
    CHECK(!g.update(st));
    CHECK(g.reason() == GateReason::SpeedStale);
    GateInput down = stopped(1040);
    down.link_connected = false;
    CHECK(!g.update(down));
    CHECK(g.reason() == GateReason::LinkDown);
    GateInput nan_in = stopped(1060);
    nan_in.speed_kmh = NAN;
    CHECK(!g.update(nan_in));
  }
  {  // critical alert overrides everything, even bench mode
    SafetyGate g;
    GateInput in = stopped(0);
    in.bench_mode = true;
    g.update(in);
    in.now_ms = 1000;
    CHECK(g.update(in));
    in.alert_critical = true;
    in.now_ms = 1020;
    CHECK(!g.update(in));
    CHECK(g.reason() == GateReason::CriticalAlert);
  }
  {  // bench mode: no link / stale tolerated (after settling), known motion still blocks
    SafetyGate g;
    GateInput in;
    in.bench_mode = true;
    in.now_ms = 0;
    CHECK(!g.update(in));
    CHECK(g.reason() == GateReason::Settling);
    in.now_ms = 1000;
    CHECK(g.update(in));
    GateInput mv = stopped(1020);
    mv.bench_mode = true;
    mv.speed_kmh = 50;
    CHECK(!g.update(mv));
    CHECK(g.reason() == GateReason::Moving);
  }
  {  // bench mode does NOT help when the link is up but speed is stale
    SafetyGate g;
    GateInput in = stopped(0);
    in.bench_mode = true;
    in.speed_fresh = false;
    CHECK(!g.update(in));
    CHECK(g.reason() == GateReason::SpeedStale);
    in.now_ms = 5000;
    CHECK(!g.update(in));
  }
  {  // demo mode: simulated speed ignored (still needs the settle time), critical alert still blocks
    SafetyGate g;
    GateInput in = stopped(0);
    in.demo_mode = true;
    in.speed_kmh = 80;
    CHECK(!g.update(in));
    CHECK(g.reason() == GateReason::Settling);
    in.now_ms = 1000;
    CHECK(g.update(in));
    in.alert_critical = true;
    in.now_ms = 1020;
    CHECK(!g.update(in));
  }
  {  // speed freshness limit 400 ms
    CHECK(gate_speed_fresh(true, 1000, 1400));
    CHECK(!gate_speed_fresh(true, 1000, 1401));
    CHECK(!gate_speed_fresh(false, 1000, 1000));
    CHECK(gate_speed_fresh(true, 0xFFFFFF00u, 0x00000050u));  // wrap
  }
  {  // default: bench off and nothing known -> blocked
    SafetyGate g;
    GateInput in;
    in.now_ms = 5000;
    CHECK(!g.update(in));
    CHECK(g.reason() == GateReason::LinkDown);
  }
  {  // millis() wrap-around
    SafetyGate g;
    const uint32_t t0 = 0xFFFFFF00u;
    g.update(stopped(t0));
    CHECK(!g.update(stopped(t0 + 500)));
    CHECK(g.update(stopped(t0 + 1000)));  // wraps past 2^32
  }
  printf(g_fail == 0 ? "safety_gate: all tests passed\n" : "safety_gate: %d FAILED\n", g_fail);
  return g_fail == 0 ? 0 : 1;
}
