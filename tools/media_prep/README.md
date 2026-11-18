# media_prep - prepare an SD card for the Media tab

The **Media** tab shows full-screen photos/animations **only while the car is stopped** (speed < 1 km/h
for at least 1 s, with fresh speed data). As soon as the car moves, the OBD link is lost with stale data, or a
critical alert appears, the display returns to the dashboard. "Desk mode" (Settings > Media) allows media
without a vehicle link; it is off by default.

## 1. Card format

- microSD **FAT32** (cards up to 32 GB are safest; reformat larger cards to FAT32).
- Folder layout:

```
/photos/name.jpg           slideshow photos (max 64 files, name <= 31 characters)
/anim/<name>/0000.jpg ...  animation frames + anim.ini (max 8 animations, folder name <= 23 characters)
/boot/logo.jpg             optional boot logo (read from SD, not flash; only when Settings > Media > Boot logo = On; shown <= 1.5 s)
```

`anim.ini`:

```
fps=10
loop=1
```

- All images: **baseline JPEG (not progressive)**, ideally 320x240, <= 200 KB. The firmware rejects progressive
  files, files > 256 KB, or images > 1280x960. Smaller images are centered on a black background.
- File names: ASCII letters/digits; files starting with `.` (for example `._photo.jpg` from macOS) are ignored.

## 2. PC tool

Needs Python 3.9+ and Pillow: `pip install pillow`. `--out` is the SD card root (for example `E:/`) or a temp folder.

```bash
# Photos (file or folder) -> cropped to fill the screen; --fit contain adds black bars
python media_prep.py photo holiday1.jpg ~/Pictures/car/ --out E:/

# Animation from GIF / WebP / APNG
python media_prep.py anim fire.gif --name fire --fps 10 --out E:/

# Animation from video: export frames with ffmpeg first, then pass the folder
ffmpeg -i clip.mp4 -vf fps=10,scale=320:240 frames/%04d.png
python media_prep.py anim frames/ --name clip --fps 10 --no-loop --out E:/

# Boot logo
python media_prep.py logo logo.png --out E:/

# Check the card (progressive, too large, names too long)
python media_prep.py check E:/
```

## 3. Using it on the device

- Tap the grid icon on the left rail (Media tab). The screen becomes full screen: no speed, gauges or status bar.
- **Tap** = next photo/animation. **Tap the top-right corner** (letter P/A) = switch slideshow <-> animation.
- **Tap the top-left corner** (x) or **long-press** = exit to the dashboard.
- Settings > Media: photo interval (3-60 s), shuffle, animation fps cap, desk mode, Media on/off, Boot logo on/off.

## 4. Performance notes

JPEG decoding on the ESP32 takes about 100-300 ms per image, so animations realistically run at 3-8 fps even if
`fps` is higher (`fps` is only an upper limit). Use fewer frames or JPEG quality 70-80 if an animation stutters.
