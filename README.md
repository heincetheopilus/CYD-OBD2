# CarCYD OBD2 - Bluetooth OBD-II dashboard for the ESP32 Cheap Yellow Display

A digital gauge cluster and OBD-II screen for the **ESP32-2432S028R "Cheap Yellow Display" (CYD)**. It reads
real-time RPM and speed (plus coolant, load, throttle, intake temperature, fuel level and battery voltage)
from a **Bluetooth Classic ELM327 adapter**, and has a **Media tab** that shows photos and animations from a
microSD card - but only while the car is stopped.

This is an independent project. Parts of its user interface (the LVGL UI layer, simulator and settings framework)
are taken from [Car-Dashboard-ESP32-CYD](https://github.com/muki01/Car-Dashboard-ESP32-CYD) by
Muksin Muksin ("Muki"), released under GPL-3.0; the vehicle link, media features and most of the rest were written
for this project. See [Credits](#credits-and-license) and [CHANGELOG.md](CHANGELOG.md).

> [!WARNING]
> **Status: experimental.** The firmware has been bench-tested on a CYD board, but it has **not been
> road-tested in a real car yet**. Do not rely on it as your only source of speed, warnings or engine data.

> Screenshots: *to be added.* (Place images in `images/` and reference them here before publishing.)

## Features

- Gauge cluster: tachometer ring with red zone and shift light, digital speed, gear indicator, two dashboard
  views (swipe to switch, long-press a value to change it).
- Live data list with charts, OBD-II fault code reader (read / clear), trip computer, driver alerts, and
  more than 25 settings. (UI inherited from upstream.)
- **Real Bluetooth Classic ELM327 link** (new in this project): pick the adapter from a scan list, saved by
  MAC address, with limited automatic reconnect (see [User Guide](docs/USER_GUIDE.md#3-connecting-the-adapter)).
- **Real-time RPM and speed** from the adapter. Settings > Connection shows the measured refresh rate and latency.
- **Media tab** (new in this project): full-screen photo slideshow and frame animations from the SD card, plus an
  optional boot logo. A safety gate allows media only when speed is below 1 km/h with a fresh reading.
- **Demo mode** that simulates a driving car, so you can try everything without a vehicle.
- English UI only. (Turkish and the clock feature of upstream were removed.)

## Hardware needed

| Part | Notes |
|---|---|
| ESP32-2432S028R "CYD" | Default configuration targets the **2-USB variant** (micro-USB + USB-C, `ILI9341_2`, colour inversion on). For a 1-USB board, see [Board variants](#board-variants). |
| Bluetooth **Classic** ELM327 adapter | BLE-only adapters are **not supported**. See [Classic or BLE?](docs/USER_GUIDE.md#classic-or-ble-how-to-tell). |
| microSD card, FAT32 *(optional)* | Only for the Media tab and boot logo. |
| USB cable | For flashing and bench power. |
| 12 V to 5 V automotive-grade converter *(in the car)* | Load-dump protected. Never wire the CYD directly to 12 V. See [docs/hardware.md](docs/hardware.md#in-car-installation). |
| 8 ohm speaker *(optional)* | JST 1.25 mm "SPEAK" connector, for alert sounds. |

## Quick start

Full instructions: **[docs/FLASHING.md](docs/FLASHING.md)**.

```
python -m pip install platformio
python -m platformio run -e classic -t upload --upload-port COM5
```

Replace `COM5` with your board's port. The first build downloads about 500 MB of toolchains and libraries and
can take 15 minutes or more. Serial monitor (115200 baud): `python -m platformio device monitor -b 115200`.

Then:

1. On first boot, tap the four touch-calibration targets.
2. Settings > Connection: choose **Bluetooth**, tap **Scan for adapters**, tap your adapter, then **Connect**.
   (Choose **Demo** to try the UI without a car.)
3. Make sure no phone is connected to the adapter - it accepts only one connection.

Details, SD card preparation and troubleshooting are in the **[User Guide](docs/USER_GUIDE.md)**.

### Board variants

`CarCYD/src/config/board_config.h` selects the display. The repository default is `CYD_PANEL_ILI9341_2` with
`CYD_PANEL_INVERT 1`. For a one-micro-USB board, either change those two defines in `board_config.h`, or add
these lines to `build_flags` in `platformio.ini` and rebuild:

```
-DCYD_PANEL=CYD_PANEL_ILI9341
-DCYD_PANEL_INVERT=0
```

For 2-USB boards with an ST7789 use `-DCYD_PANEL=CYD_PANEL_ST7789 -DCYD_PANEL_INVERT=0`. More in
[docs/hardware.md](docs/hardware.md#board-variants).

## Media tab and safety

Media is full-screen picture/animation only (no gauges). The firmware allows it only when **all** of these hold:

- speed is below 1 km/h, with a reading no older than 0.4 s, for at least 1 second;
- the Bluetooth link is connected (or Demo mode is on, or the explicit "Desk mode" is enabled with no link);
- no critical alert is active.

It closes automatically (back to the dashboard) when the car moves, when the link is lost or the speed reading
goes stale, or when a critical alert appears. A safety notice is shown the first time you open the tab.

## Documentation

- [User Guide](docs/USER_GUIDE.md) - pages, connection, media/SD preparation, troubleshooting, FAQ
- [Flashing guide](docs/FLASHING.md) - PlatformIO, prebuilt binary, web flasher, recovery
- [Build notes](README_BUILD.md) (Indonesian, short) - [Hardware](docs/hardware.md) - [Architecture](docs/architecture.md) - [UI guidelines](docs/ui-guidelines.md)
- [Media preparation tool](tools/media_prep/README.md) (Indonesian) - [Changelog](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md) - [Code of Conduct](CODE_OF_CONDUCT.md) - [Security](SECURITY.md)

## Status and known limitations

- Experimental: bench-tested on a CYD, **not yet road-tested in a car**.
- Bluetooth Classic ELM327 only. BLE and Wi-Fi adapters are not supported.
- Memory is tight: about 41 KB of free heap with Bluetooth running; the board has no PSRAM.
- Firmware is about 1.72 MB in a 3 MB `huge_app` partition: **no OTA updates** (flash over USB).
- `BluetoothSerial` is deprecated in the Arduino-ESP32 core; the build prints a deprecation warning.
- Connecting or scanning cannot be cancelled once started; it may take 5-10 seconds to finish.
- The boot logo cannot check vehicle speed at boot (it is shown for at most 1.5 s and only if enabled).
- Only the signals listed above are read from the car; availability depends on the vehicle (unsupported
  signals stay at "--" - needs confirmation on real vehicles).

## Disclaimer

> [!WARNING]
> This is a hobby project, not a certified instrument. **Do not watch media or operate the touchscreen while
> driving.** Use it at your own risk. Check the laws of your country about displays visible to the driver and
> about installing devices in your vehicle. Mount the screen where it does not block your view. Use an
> automotive-grade 12 V to 5 V converter. The authors accept no responsibility for damage, malfunction, fines
> or accidents.

## Credits and license

- Portions taken from: **[Car-Dashboard-ESP32-CYD](https://github.com/muki01/Car-Dashboard-ESP32-CYD)** by Muksin Muksin
  ("muki01"): UI, architecture, simulator, fonts and settings framework.
- Libraries and assets: [LVGL](https://lvgl.io), [LovyanGFX](https://github.com/lovyan03/LovyanGFX),
  [Arduino-ESP32](https://github.com/espressif/arduino-esp32), Barlow typeface, Material Design Icons. See
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This program is free software: you can redistribute it and/or modify it under the terms of the **GNU General
Public License version 3** as published by the Free Software Foundation. It is distributed in the hope that it
will be useful, but **without any warranty**; without even the implied warranty of merchantability or fitness
for a particular purpose. See the [LICENSE](LICENSE) file.

Copyright (c) 2026 Muksin Muksin (upstream).
Copyright (c) 2026 Heince Theopilus (Bluetooth Classic ELM327 link, Media tab, memory and UI fixes, documentation).
Because portions of the upstream GPL-3.0 code are included, this project as a whole is also released under GPL-3.0.
