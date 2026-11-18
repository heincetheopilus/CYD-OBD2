# Changelog

Format based on "Keep a Changelog". Versions follow `APP_VERSION` in `CarCYD/src/config/app_config.h`.

## [1.1.0] - 2026-11-18

First release of the CarCYD OBD2 fork of
[Car-Dashboard-ESP32-CYD](https://github.com/muki01/Car-Dashboard-ESP32-CYD) (GPL-3.0). Experimental: bench-tested
on a CYD, not yet road-tested in a car.

### Added
- Real Bluetooth Classic (SPP) ELM327 link replacing the "coming soon" vehicle link: asynchronous ELM327 session,
  real-time RPM and speed, plus coolant temperature, engine load, throttle, intake air temperature, fuel level,
  battery voltage and fault code read/clear.
- Settings > Connection: Demo / Bluetooth data source, adapter scan list, adapter saved by MAC address, Connect
  button, status text, measured refresh rate and latency. PIN 1234 and 0000 tried alternately.
- Reconnect policy: automatic attempts only with a saved MAC, at most 3 per round, then idle
  ("Not connected - tap Connect"); scan only on request.
- Media tab: full-screen JPEG photo slideshow and frame animations from a FAT32 microSD card (`/photos`,
  `/anim/<name>`), tap/long-press controls, first-use safety notice.
- Safety gate: media allowed only when speed < 1 km/h with a fresh reading (<= 400 ms) held for 1 s; stops at once
  when moving, link lost/stale, or a critical alert appears. Optional "Desk mode" (no link only).
- Settings > Media: Media on/off, Boot logo, photo interval, shuffle, max animation speed, Desk mode, SD status.
- Optional boot logo from `/boot/logo.jpg` (off by default, at most 1.5 s).
- `tools/media_prep/media_prep.py` (Pillow) to prepare photos, animations and the boot logo, and check a card.
- PlatformIO project (`platformio.ini`) with pioarduino (Arduino-ESP32 core 3.x); environments `classic`,
  `classic_debug`, `classic_harness`.
- Host unit tests in `tools/tests/` (safety gate, JPEG info, settings, OBD parser).
- Documentation: README, User Guide, Flashing guide, this changelog.

### Changed
- Default data source is Bluetooth (upstream: demo only). Demo mode remains available.
- Partition table `huge_app` (3 MB app, no OTA); firmware about 1.72 MB.
- Display draw buffers reduced to 12 lines (about 15 KB) to leave heap for Bluetooth; Arduino loop task stack
  increased to 12 KB.
- Settings structure extended (append-only, settings version 3).
- README replaced and re-scoped for this fork.

### Removed
- Turkish language (UI is English only).
- Clock feature.
- BLE support (never implemented; Classic only).

### Known issues
- Not tested in a real car yet.
- About 41 KB free heap with Bluetooth running; no PSRAM.
- `BluetoothSerial` deprecation warning at build time.
- Connect and scan cannot be cancelled once started (about 5-10 s).
- The boot logo cannot check speed at boot.
