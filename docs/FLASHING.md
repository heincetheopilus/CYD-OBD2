# Flashing the firmware to the CYD

Goal: get CarCYD OBD2 onto an ESP32-2432S028R. Windows is the main path; Linux/macOS notes are at the end.
Three routes: **PlatformIO** (build from source), **prebuilt binary with esptool**, or a **web flasher**.

> There is **no OTA update**: the firmware uses the 3 MB `huge_app` partition. Every update is done over USB.

## 1. Prerequisites

- A CYD board and a data-capable USB cable (some cables are charge-only and will not show a COM port).
- The USB-serial driver. Boards use a **CH340** or **CP210x** chip. On Windows 10/11 the driver often installs
  itself; if no new port appears in Device Manager (Ports (COM & LPT)) when you plug the board in, install the
  CH340 or CP210x driver from the chip vendor.
- Find the port: Device Manager > Ports (COM & LPT) > "USB-SERIAL CH340 (COMx)". Unplug and replug to see which
  entry appears and disappears.
- Python 3.9 or newer (for PlatformIO or esptool).

## 2. Route A - PlatformIO (build and upload)

```
python -m pip install platformio
cd CarCYD_OBD2
python -m platformio run -e classic -t upload --upload-port COM5
```

Replace `COM5` with your port. Notes:

- The first build downloads the pioarduino platform (Arduino-ESP32 core 3.x), toolchains and libraries:
  **about 500 MB and 15+ minutes**. Later builds are much faster.
- Environment `classic` is the normal firmware. `classic_debug` adds verbose logging
  (`APP_LOG_LEVEL=3`); `classic_harness` is a developer debug harness.
- The default board setting is the 2-USB variant (`ILI9341_2`, inversion on). For other variants edit
  `CarCYD/src/config/board_config.h` or add `-DCYD_PANEL=...` / `-DCYD_PANEL_INVERT=...` to `build_flags` in
  `platformio.ini`; see [hardware.md](hardware.md#board-variants).
- Size: about 1.72 MB of the 3 MB application partition.
- On Windows, if you redirect or pipe the output to a file and see encoding errors, set
  `PYTHONUTF8=1` first (PowerShell: `$env:PYTHONUTF8=1`; cmd: `set PYTHONUTF8=1`).
- Upload speed is 921600 baud (from `platformio.ini`). If uploads fail midway, lower `upload_speed` to 460800
  or 115200.

If PlatformIO cannot connect ("Failed to connect to ESP32: Wrong boot mode"): **hold the BOOT button**, tap
RESET (or replug USB), start the upload and release BOOT when "Writing at..." appears.

## 3. Route B - prebuilt binary with esptool

After a build, the files are in `.pio\build\classic\`:

| File | Flash offset |
|---|---|
| `bootloader.bin` | `0x1000` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` (from the Arduino-ESP32 core; not copied into the build folder) |
| `firmware.bin` | `0x10000` |
| **`firmware.factory.bin`** | **`0x0`** (all of the above merged in one image) |

The simplest way is the merged image:

```
python -m pip install esptool
python -m esptool --chip esp32 --port COM5 --baud 460800 write_flash 0x0 firmware.factory.bin
```

(Older esptool versions use `write-flash`/`write_flash`; both spellings are accepted by current versions.)

Flashing the factory image erases/overwrites bootloader, partition table and app, so **stored settings may be
reset** and the touch calibration screen will appear again. To update only the application without touching the
rest, write `firmware.bin` at `0x10000`.

`*.bin` files are ignored by git; attach `firmware.factory.bin` to a GitHub Release if you want to share it.

## 4. Route C - web flasher (no install)

1. Open a Chromium-based browser (Chrome or Edge; Web Serial is required) and go to the Espressif
   [esptool-js demo](https://espressif.github.io/esptool-js/), or any "ESP Web Tools" page.
2. Click *Connect* and choose the board's serial port.
3. Set the flash address to **`0x0`**, choose `firmware.factory.bin`, click *Program*.
4. Press RESET on the board when finished.

Close the Arduino serial monitor / PlatformIO monitor first: only one program can use the port.

## 5. After flashing

Serial monitor at **115200** baud:

```
python -m platformio device monitor -b 115200
```

Expected: boot log lines, then the splash screen and, on first boot, the touch calibration. Settings > About
shows firmware version (1.1.0), board and free memory.

## 6. If something goes wrong

| Symptom | What to do |
|---|---|
| No COM port appears | Try another cable (data cable) and USB port; install the CH340/CP210x driver. |
| "Failed to connect" / "Wrong boot mode" | Hold BOOT, press RESET, start upload, release BOOT after writing starts. |
| Upload fails at high speed | Lower `upload_speed` in `platformio.ini`, or `--baud 115200` for esptool. |
| "Access denied" / port busy | Close serial monitors and other tools using the port. |
| Black or white screen after flash | Wrong panel variant. Try other `CYD_PANEL` values; see [hardware.md](hardware.md#orientation-and-mirroring). |
| Boot loop (repeated reset messages) | Reflash the full `firmware.factory.bin` (this also rewrites bootloader and partition table). If it persists, erase flash first (below). |
| Corrupt settings after upgrade | Settings > System > Factory reset, or erase flash (below). |

### Recovering from a bad flash

Erase the entire flash, then flash again:

```
python -m esptool --chip esp32 --port COM5 erase_flash
python -m esptool --chip esp32 --port COM5 --baud 460800 write_flash 0x0 firmware.factory.bin
```

Erasing also deletes settings, calibration and trip data. The ROM bootloader of the ESP32 cannot be
overwritten, so USB flashing remains possible even after a failed flash, using the BOOT-button procedure above.

### Rollback to a previous version

Before flashing a new version, **keep a copy of the previous `firmware.factory.bin`** (for example in a
`releases\` folder, named with the version: `firmware-1.1.0.factory.bin`). To roll back, flash that file at
`0x0` as in Route B. Settings are stored in a versioned format and older firmware may not understand settings
saved by a newer one; if the device behaves strangely after a rollback, use Factory reset or `erase_flash`
(needs confirmation on real upgrade paths, as only version 1.1.0 exists so far).

## 7. Linux and macOS notes

- Same commands; the port is like `/dev/ttyUSB0` (Linux) or `/dev/cu.usbserial-*` (macOS). Replace
  `--upload-port COM5` accordingly.
- Linux: add your user to the `dialout` group (`sudo usermod -aG dialout $USER`, then log out and in) or the
  port will be "Permission denied". Some distributions need `brltty` removed because it grabs CH340 devices.
- macOS: recent versions include CH340/CP210x support; otherwise install the vendor driver.
- Use `python3 -m ...` if `python` is not available.
