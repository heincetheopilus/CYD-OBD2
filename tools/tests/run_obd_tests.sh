#!/bin/sh
# Host unit tests for the OBD link (parser + state machine), needs g++ only.
set -e
cd "$(dirname "$0")"
CORE=../../CarCYD/src/core
OUT="${TMPDIR:-/tmp}/carcyd_tests"
mkdir -p "$OUT"
g++ -std=gnu++17 -Wall -Wextra -O1 -fno-exceptions -fno-rtti -o "$OUT/test_obd" test_obd.cpp \
  $CORE/elm327_parser.cpp $CORE/obd_link.cpp $CORE/loopback_transport.cpp \
  $CORE/vehicle_state.cpp $CORE/data_link.cpp $CORE/util.cpp $CORE/log.cpp
"$OUT/test_obd"
