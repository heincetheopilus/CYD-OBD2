# CarCYD OBD2 - User Guide

Status: **experimental** (bench-tested on a CYD, not yet road-tested in a car). Read the
[safety rules](#7-safety-behavior) before using the Media tab.

Contents: [1 First boot](#1-first-boot-and-touch-calibration) - [2 Pages](#2-pages) -
[3 Connecting the adapter](#3-connecting-the-adapter) - [4 Settings](#4-settings-reference) -
[5 Media and SD card](#5-media-and-sd-card-preparation) - [6 Using the Media tab](#6-using-the-media-tab) -
[7 Safety](#7-safety-behavior) - [8 Troubleshooting](#8-troubleshooting) - [9 FAQ](#9-faq)

## 1. First boot and touch calibration

1. Power the CYD (USB or a 12 V to 5 V automotive-grade converter; never connect 12 V directly).
2. A splash screen with a progress arc appears. If "Boot logo" is on and the SD card has `/boot/logo.jpg`, that
   picture is shown instead (up to 1.5 s).
3. On first start, a touch calibration runs: tap the center of each target (the last one is a verification tap).
   A stylus or fingernail is more accurate than a fingertip. If it is "not accurate enough", repeat.
4. The dashboard opens. The default data source is **Bluetooth**; if no adapter is saved it will not connect
   until you pick one (see [section 3](#3-connecting-the-adapter)). Choose **Demo** to see the UI without a car.

Calibration again later: **Settings > System > Touch calibration**, or hold the **BOOT** button while powering up.
Calibration is also requested automatically when the panel type or orientation setting changes.

## 2. Pages

A navigation rail on the left switches pages: Dashboard, Live Data, Diagnostics, Trip, Settings, and **Media**
(only if Media is enabled). The BOOT button: short press = acknowledge alert / next page, long press = back to the
dashboard (inherited from upstream).

### Dashboard
- Gauge cluster: tachometer ring with red zone, speed, engine speed, gear indicator and four corner values;
  the shift light flashes at the configured shift RPM.
- Swipe left/right (or tap the dots) for the second view with six mini gauges.
- Long-press any value to choose another signal for that slot. Reset in Settings > Gauges.
- Alerts appear as a banner (coolant, oil, battery low/high, fuel, speed limit, check engine, link lost). Tap
  the banner to acknowledge. Critical alerts repeat their sound until acknowledged.

### Live Data
A list of engine signals with severity colours. Tap one for a real-time chart with min / average / max.
With a real adapter the firmware requests: RPM, speed, coolant temperature, engine load, throttle position,
intake air temperature, fuel level and battery/module voltage. Signals the car does not report, or that this
firmware does not poll, show "--" (needs confirmation per vehicle).

### Diagnostics
Shows check-engine (MIL) status and fault codes. **Scan** reads stored/pending/permanent codes from the ECU;
tap a code for description, severity and advice. **Clear** erases codes after a confirmation dialog (also resets
readiness monitors; do it with ignition on, engine off). Requires a connected adapter.

### Trip
Distance, drive time, average and maximum speed, fuel used, average consumption, and a 0-100 km/h (0-60 mph)
acceleration timer ("Stop to arm", then "Armed, go!"). Trip data is saved periodically. Reset with confirmation.

### Media
Full-screen photos and animations from the SD card. Only available while the car is stopped:
see [sections 5-7](#5-media-and-sd-card-preparation).

### Settings
See [section 4](#4-settings-reference).

## 3. Connecting the adapter

### Supported adapters
**Bluetooth Classic (SPP) ELM327 only.** Bluetooth Low Energy (BLE) and Wi-Fi adapters do not work.

#### Classic or BLE? How to tell
- **Android:** open system Settings > Bluetooth and scan. If the adapter appears in the normal list and asks for
  a PIN (usually 1234 or 0000) when pairing, it is Bluetooth Classic. A BLE-only adapter does not show up
  or cannot be paired from the system menu.
- **nRF Connect or LightBlue (free app):** if the adapter shows up there with services such as FFF0, FFE0 or
  6E400001..., it is BLE. If it appears both here and in the Android Bluetooth menu, it is dual-mode (Classic
  works with CarCYD).
- **iPhone:** iOS cannot use Bluetooth Classic for OBD apps. If your adapter works with an OBD app on an iPhone
  over Bluetooth, it is BLE. If the iPhone must join a Wi-Fi network (e.g. "WiFi_OBDII", "V-LINK"), it is a
  Wi-Fi adapter. Neither is supported.
- Names are only a hint (a name like "OBDII" is often Classic); do not decide by name alone.

### Steps
1. Plug the adapter into the car's OBD-II port, ignition ON.
2. **Turn off Bluetooth on your phone** (or forget the adapter there). The adapter accepts **one** connection
   at a time; if the phone holds it, the CYD cannot connect.
3. **Settings > Connection**: set **Data source = Bluetooth**.
4. Tap **Scan for adapters** (about 8 seconds). Tap your adapter in the list: it is saved by its **MAC address**
   ("Adapter selected").
5. Tap **Connect**. Status goes through: Searching adapter, Initializing, Connected. "Adapter OK, no vehicle
   reply" means the adapter answered but the car did not (ignition off, or unsupported protocol).
6. When connected, the Refresh rate row shows RPM / speed update rates (Hz) and latency. The serial log
   prints the same every 5 seconds.

The PIN 1234 and 0000 are tried alternately and automatically. The default name is `OBDII`; "Use default name
(OBDII)" clears the saved adapter.

### Reconnect behavior
- At start-up and after a drop, the firmware reconnects **automatically only if a MAC is saved**: at most
  **3 attempts** with growing pauses (2 s, then 6 s). After that it stops and the status reads
  **"Not connected - tap Connect"**. Tap **Connect** to try again.
- A scan only runs when you tap **Scan for adapters**.
- Without a saved MAC, **Connect** makes one attempt by name (`OBDII` unless changed).
- A connect or scan cannot be cancelled once started; changes you make during it apply after it ends
  (about 5-10 seconds).

## 4. Settings reference

| Section | Contents |
|---|---|
| Display | Auto brightness (ambient light sensor), day / night brightness, ambient light readout, rotate screen 180 degrees, accent colour |
| Units | Speed (km/h, mph), temperature (C, F), pressure (kPa, bar, psi) |
| Gauges | Tachometer range (5000-9000), redline, shift light on/off, shift point, reset gauge layout |
| Warnings | Coolant temperature, low battery voltage, low fuel level (off or 1-30 %), speed limit (off by default) |
| Sound & light | Touch sounds, warning sounds, volume, status LED |
| Connection | Data source (Demo / Bluetooth), status, refresh rate, adapter, Connect, Scan for adapters, use default name |
| Media | see below |
| System | Touch calibration, Restart device, Factory reset (erases settings, trip data, calibration) |
| About | Firmware version, build date, board, display, chip, flash, free memory (live), uptime, LVGL version |

### Settings > Media
| Item | Default | Meaning |
|---|---|---|
| Media | On | Media tab and SD card access. Off: no SD access at all, no Media tab. |
| Boot logo | Off | Show `/boot/logo.jpg` from the SD card at the next start (at most 1.5 s). |
| Photo interval | 8 s | Slideshow interval, 3-60 s. |
| Shuffle photos | Off | Random instead of alphabetical order. |
| Max animation speed | 10 fps | Upper limit for animations, 1-15 fps. |
| Desk mode | Off | Allow media **without** a vehicle link (bench use). Ignored while an adapter is connected. |
| SD card | - | Shows "N photos - M animations", "No card" or "Not scanned yet". |

## 5. Media and SD card preparation

Card: microSD, **FAT32** (32 GB or smaller is safest; reformat larger cards as FAT32). SD pins are
VSPI GPIO 18 / 19 / 23, chip select 5 (fixed by the board).

```
/photos/name.jpg               slideshow pictures (max 64 files, names up to 31 characters)
/anim/<name>/0000.jpg ...      animation frames + anim.ini (max 8 animations, folder name up to 23 characters)
/boot/logo.jpg                 optional boot logo
```

`anim.ini` (written by the tool):

```
fps=10
loop=1
```

Image rules: **baseline JPEG (not progressive)**, ideally 320x240 and under 200 KB. The firmware rejects
progressive files, files over 256 KB and images larger than 1280x960. Smaller images are centred on black. Use
plain ASCII file names; files starting with `.` (e.g. `._photo.jpg` from macOS) are ignored.

### Prepare with the PC tool
Needs Python 3.9+ and Pillow: `python -m pip install pillow`. `--out` is the SD card root (for example `E:/`) or a
temporary folder to copy later. Run from `tools/media_prep/`:

```
# Photos (files or folders). Default crops to fill the screen; --fit contain adds black bars
python media_prep.py photo holiday1.jpg C:/Pictures/car/ --out E:/

# Animation from GIF / WebP / APNG
python media_prep.py anim fire.gif --name fire --fps 10 --out E:/

# Animation from video: export frames with ffmpeg first, then give the folder
ffmpeg -i clip.mp4 -vf fps=10,scale=320:240 frames/%04d.png
python media_prep.py anim frames/ --name clip --fps 10 --no-loop --out E:/

# Boot logo
python media_prep.py logo logo.png --out E:/

# Check the card (progressive files, too large, names too long)
python media_prep.py check E:/
```

Options: `--fit crop|contain`, `--quality 50-95`, `--fps 1-30`, `--no-loop` (play once and hold the last frame),
`--max-frames` (default 300). See [tools/media_prep/README.md](../tools/media_prep/README.md) (Indonesian).

Performance: JPEG decoding takes roughly 100-300 ms per picture, so animations realistically run at 3-8 fps
even if `fps` is higher (`fps` is only a cap). Use fewer frames or quality 70-80 if an animation stutters.

## 6. Using the Media tab

1. Pick the grid icon in the left rail. The first time, a **safety notice** must be accepted.
2. The screen becomes full-screen picture: no speed, gauges or status bar.
3. **Tap** = next photo/animation. **Tap top-right corner** (letter P/A) = switch slideshow / animation.
   **Tap top-left corner** (x) or **long-press** = back to the dashboard.
4. Messages you may see: "No SD card" (insert a FAT32 card with a `/photos` folder), "No pictures found",
   "Waiting for the vehicle to stop...", "Blocked: vehicle moving", "Blocked: speed unknown",
   "Blocked: critical alert". Blocked messages disappear after about 2.5 s and return to the dashboard.

## 7. Safety behavior

Media is allowed only when **all** are true:

- Speed is **below 1 km/h** and the speed reading is **fresh (no older than 0.4 s)**, and has stayed so for **1 second**.
- A vehicle link is connected (in **Demo** mode the simulated speed is ignored; with **Desk mode** on and no
  adapter connected, media is allowed - this is meant for the bench, not the car).
- No critical alert is active.

Media is stopped at once, and you are returned to the dashboard, when the car moves, when the link drops or the
speed becomes stale (even with Desk mode on, if the adapter is connected), or when a critical alert appears. The
boot logo is skipped if a movement/critical-alert is already known, but it cannot check speed at boot because
the link is not up yet.

**Never watch media while driving.** These safeguards are a convenience, not a guarantee. Check your local laws.

## 8. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| Black screen | Power: use a good 5 V supply/cable. If the backlight is on but nothing is drawn, the panel variant is wrong: try other `CYD_PANEL` values. Check brightness (Settings > Display) and auto-brightness in a dark room. |
| White / blank screen | Wrong `CYD_PANEL` variant; see [hardware.md](hardware.md#orientation-and-mirroring). |
| Boot loop (keeps restarting) | Reflash the full `firmware.factory.bin`; if still looping, `erase_flash` and reflash ([FLASHING.md](FLASHING.md#6-if-something-goes-wrong)). Check power supply stability. |
| Cannot find the adapter in the scan | Adapter plugged in and ignition on? Phone Bluetooth off? Adapter is Classic (not BLE)? Scan again. |
| No Bluetooth connection | Phone still connected to the adapter (it allows one connection). Wrong PIN is not the usual cause (1234 and 0000 are tried automatically). Tap Connect again. |
| Stuck at "Not connected - tap Connect" | The 3 automatic attempts failed. Fix the cause above, then tap **Connect**. |
| "Adapter OK, no vehicle reply" | Ignition off, or the car's protocol is not answering. Turn ignition on and retry. |
| Media blocked | The car is moving (speed >= 1 km/h), speed is unknown (link down/stale), or a critical alert is active. In the bench, enable Desk mode (without a connected adapter). |
| "No SD card" / SD not detected | Use FAT32, 32 GB or smaller; reseat the card; check Media is On; try another card. A flaky card may need a lower SD clock (firmware change). Restart after inserting. |
| "No pictures found" | Files must be in `/photos` (JPG) or `/anim/<name>/` frames; run `media_prep.py check`. Progressive JPEGs are rejected. |
| Wrong colours (photo negative) | Toggle `CYD_PANEL_INVERT` in `board_config.h` (2-USB default is 1; 1-USB boards use 0) and rebuild. |
| Picture mirrored (text backwards) | Check `CYD_PANEL`; if still mirrored set `CYD_PANEL_ROTATION_OFFSET` to 4 or 6, and rebuild. |
| Picture upside down | Settings > Display > Rotate screen 180 degrees (no rebuild). |
| Touch is off or mirrored | Settings > System > Touch calibration, or hold BOOT while powering up. |
| Noise / artefacts on screen | Lower `CYD_LCD_SPI_HZ` (for example to 27 MHz) and rebuild. |
| Sluggish or resets while Bluetooth is up | Memory is tight (about 41 KB free heap). Check Settings > About > Free memory; turn Media off if you do not use it. |

## 9. FAQ

**Which cars work?** Cars supporting standard OBD-II mode 01 PIDs through an ELM327 adapter. Not tested on a real
car yet; results vary by vehicle and adapter (cheap clones differ).

**Why only Bluetooth Classic?** BLE support was dropped from the plan; Classic fits in the CYD's memory. BLE and
Wi-Fi adapters are not supported.

**Why can't I connect from my phone and the CYD at once?** The adapter accepts a single connection.

**Is there a clock, or Turkish language?** No. Both were removed in this fork (the CYD has no real-time clock);
the UI is English only.

**Can I update over the air?** No (no OTA partition). Flash by USB.

**Can I show media while driving?** No, by design. Do not attempt to bypass it.

**Does the Media tab slow the OBD data?** Decoding is time-sliced so the OBD link keeps being serviced, but
JPEG decoding is slow (3-8 fps animations).

**Where are the settings stored?** In the ESP32's flash (NVS). Factory reset in Settings > System erases them.

**License?** GPL-3.0; fork of Car-Dashboard-ESP32-CYD. See the [README](../README.md).
