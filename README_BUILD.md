# Build and flash (CarCYD OBD2, Bluetooth Classic)

Target: CYD 2-USB (ESP32-2432S028R, ILI9341_2 panel, already the default in `board_config.h`).
Adapter: ELM327 Bluetooth Classic (SPP). Connection mode: Demo or Bluetooth (Settings > Connection).

For the full guides see [docs/FLASHING.md](docs/FLASHING.md) and [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

## PlatformIO

1. Install PlatformIO (VS Code extension or `python -m pip install platformio`).
2. From this folder:
   ```
   python -m platformio run                          # build env "classic"
   python -m platformio run -t upload --upload-port COM7   # flash over USB
   python -m platformio device monitor               # serial log, 115200
   ```
3. `platformio.ini` uses the pioarduino platform (Arduino-ESP32 core 3.x), board `esp32dev`,
   `huge_app` partition (3 MB, no OTA), LVGL 9.6 + LovyanGFX. Env `classic_debug` raises the log level.
   The first build downloads about 500 MB and can take 15+ minutes.
4. If colors look like a photo negative, change `CYD_PANEL_INVERT` in
   `CarCYD/src/config/board_config.h` (or via `build_flags`).

## Arduino IDE

Arduino-ESP32 core 3.x, libraries lvgl 9.6+ and LovyanGFX 1.2+, open `CarCYD/CarCYD.ino`.
Tools: Board "ESP32 Dev Module", Partition Scheme "Huge APP (3MB No OTA/1MB SPIFFS)".

## First use

1. Plug the adapter into the car, ignition ON. Make sure no phone is connected to the adapter.
2. Settings > Connection > Data source = Bluetooth. Default name is `OBDII`; if your adapter has
   another name, press "Scan for adapters" and tap it (stored as a MAC address in NVS).
3. PINs 1234 and 0000 are tried automatically. Auto-connect makes at most 3 attempts, then goes
   idle ("Not connected - tap Connect").
4. The serial log prints the RPM/speed refresh rate (Hz) and adapter latency every 5 seconds.

## Host tests (no PlatformIO needed, only g++)

```
sh tools/tests/run_obd_tests.sh
sh tools/tests/run_tests.sh
```

PC screen simulator: `python tools/simulator/build.py` (see `tools/simulator/README.md`).
