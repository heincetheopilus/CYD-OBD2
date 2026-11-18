#!/bin/sh
# Host unit tests for the pure core modules (needs g++ only).
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/carcyd_tests"
mkdir -p "$OUT"
SRC=../../CarCYD/src/core
for t in safety_gate jpeg_info; do
  g++ -std=c++17 -Wall -Wextra -o "$OUT/test_$t" test_$t.cpp $SRC/$t.cpp
  "$OUT/test_$t"
done
g++ -std=c++17 -Wall -Wextra -o "$OUT/test_settings" test_settings.cpp $SRC/settings.cpp $SRC/persist.cpp $SRC/util.cpp $SRC/log.cpp
"$OUT/test_settings"
