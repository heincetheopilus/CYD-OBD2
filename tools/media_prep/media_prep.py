#!/usr/bin/env python3
"""Prepare photos, animations and a boot logo for the CarCYD microSD card.

Everything is converted to what the firmware decodes: 320x240, baseline
(non-progressive) JPEG, small files, short ASCII names.

Layout written below the SD root:
    photos/<name>.jpg           still pictures (slideshow)
    anim/<name>/0000.jpg ...    animation frames + anim.ini (fps, loop)
    boot/logo.jpg               optional boot logo

Usage:
    python media_prep.py photo  IMG [IMG ...] --out E:/
    python media_prep.py anim   clip.gif --name flames --fps 10 --out E:/
    python media_prep.py anim   frames_folder/ --name intro --fps 12 --no-loop --out E:/
    python media_prep.py logo   logo.png --out E:/
    python media_prep.py check  E:/

Needs Pillow (pip install pillow). Video: export frames first, e.g.
    ffmpeg -i clip.mp4 -vf fps=10,scale=320:240 frames/%04d.png
and pass the folder to the "anim" command.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

try:
    from PIL import Image, ImageOps, ImageSequence
except ImportError:  # pragma: no cover
    sys.exit("Pillow is required: pip install pillow")

SCREEN_W, SCREEN_H = 320, 240
MAX_FILE_BYTES = 200 * 1024        # the firmware rejects > 256 KB; stay well below
MAX_PHOTO_NAME = 31                # incl. ".jpg" (firmware limit: 32 bytes with terminator)
MAX_ANIM_NAME = 23
MAX_PHOTOS = 64                    # firmware index limits
MAX_ANIMS = 8
MAX_FRAMES = 9999
DEFAULT_QUALITY = 85
MIN_QUALITY = 50
IMAGE_EXT = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".webp", ".tif", ".tiff"}


# ---- image helpers --------------------------------------------------------------------------

def fit_image(img: Image.Image, mode: str) -> Image.Image:
    """Scales to exactly 320x240: 'crop' fills the screen, 'contain' letterboxes on black."""
    img = ImageOps.exif_transpose(img)
    if img.mode in ("RGBA", "LA", "P"):
        rgba = img.convert("RGBA")
        bg = Image.new("RGB", rgba.size, (0, 0, 0))
        bg.paste(rgba, mask=rgba.split()[3])
        img = bg
    else:
        img = img.convert("RGB")
    size = (SCREEN_W, SCREEN_H)
    if mode == "contain":
        scaled = ImageOps.contain(img, size, Image.LANCZOS)
        canvas = Image.new("RGB", size, (0, 0, 0))
        canvas.paste(scaled, ((SCREEN_W - scaled.width) // 2, (SCREEN_H - scaled.height) // 2))
        return canvas
    return ImageOps.fit(img, size, Image.LANCZOS, centering=(0.5, 0.5))


def save_baseline(img: Image.Image, path: Path, quality: int) -> int:
    """Writes a baseline JPEG, lowering the quality until it is small enough. Returns the size."""
    path.parent.mkdir(parents=True, exist_ok=True)
    q = quality
    while True:
        img.save(path, "JPEG", quality=q, optimize=False, progressive=False, subsampling=2)
        size = path.stat().st_size
        if size <= MAX_FILE_BYTES or q <= MIN_QUALITY:
            return size
        q -= 5


def safe_name(text: str, limit: int) -> str:
    name = re.sub(r"[^A-Za-z0-9_-]+", "_", text).strip("_") or "item"
    return name[:limit]


def unique_path(folder: Path, stem: str, ext: str = ".jpg") -> Path:
    candidate = folder / f"{stem}{ext}"
    n = 1
    while candidate.exists():
        suffix = f"_{n}"
        candidate = folder / f"{stem[:MAX_PHOTO_NAME - len(ext) - len(suffix)]}{suffix}{ext}"
        n += 1
    return candidate


# ---- JPEG validation ------------------------------------------------------------------------

def jpeg_problem(path: Path) -> str | None:
    """Mirror of firmware core/jpeg_info: returns a problem text or None if the file is fine."""
    data = path.read_bytes()
    if len(data) > 256 * 1024:
        return f"file too large ({len(data) // 1024} KB, max 256 KB)"
    if data[:2] != b"\xff\xd8":
        return "not a JPEG"
    pos = 2
    for _ in range(64):
        if pos + 4 > len(data):
            return "truncated"
        if data[pos] != 0xFF:
            return "bad marker structure"
        marker = data[pos + 1]
        if marker == 0xFF:
            pos += 1
            continue
        pos += 2
        if marker == 0x01 or 0xD0 <= marker <= 0xD8:
            continue
        if marker in (0xD9, 0xDA):
            return "no frame header"
        length = int.from_bytes(data[pos:pos + 2], "big")
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            if marker == 0xC2:
                return "progressive JPEG (re-save as baseline)"
            if marker != 0xC0:
                return "unsupported JPEG type"
            precision = data[pos + 2]
            height = int.from_bytes(data[pos + 3:pos + 5], "big")
            width = int.from_bytes(data[pos + 5:pos + 7], "big")
            comps = data[pos + 7]
            if precision != 8 or comps not in (1, 3):
                return "unsupported JPEG type"
            if width > 1280 or height > 960:
                return f"image too large ({width}x{height}, max 1280x960)"
            if (width, height) != (SCREEN_W, SCREEN_H):
                return f"size {width}x{height} (works, but {SCREEN_W}x{SCREEN_H} is faster)"
            return None
        pos += length
    return "no frame header"


# ---- commands ---------------------------------------------------------------------------------

def collect_images(inputs: list[str]) -> list[Path]:
    files: list[Path] = []
    for item in inputs:
        p = Path(item)
        if p.is_dir():
            files += sorted(f for f in p.iterdir() if f.suffix.lower() in IMAGE_EXT)
        elif p.is_file():
            files.append(p)
        else:
            print(f"skip (not found): {item}")
    return files


def cmd_photo(args) -> int:
    folder = Path(args.out) / "photos"
    existing = len(list(folder.glob("*.jpg"))) if folder.exists() else 0
    count = 0
    for src in collect_images(args.inputs):
        if existing + count >= MAX_PHOTOS:
            print(f"limit of {MAX_PHOTOS} photos reached, remaining files skipped")
            break
        try:
            with Image.open(src) as im:
                out = fit_image(im, args.fit)
        except OSError as exc:
            print(f"skip {src.name}: {exc}")
            continue
        dest = unique_path(folder, safe_name(src.stem, MAX_PHOTO_NAME - 4))
        size = save_baseline(out, dest, args.quality)
        print(f"{src.name} -> {dest.relative_to(args.out)} ({size // 1024} KB)")
        count += 1
    print(f"{count} photo(s) written")
    return 0 if count else 1


def load_frames(source: Path, max_frames: int):
    """Yields frames from an animated image or a folder of numbered images."""
    if source.is_dir():
        for f in sorted(p for p in source.iterdir() if p.suffix.lower() in IMAGE_EXT)[:max_frames]:
            with Image.open(f) as im:
                yield im.copy()
        return
    with Image.open(source) as im:
        for i, frame in enumerate(ImageSequence.Iterator(im)):
            if i >= max_frames:
                break
            yield frame.copy()


def cmd_anim(args) -> int:
    source = Path(args.input)
    if not source.exists():
        print(f"not found: {source}")
        return 1
    name = safe_name(args.name or source.stem, MAX_ANIM_NAME)
    root = Path(args.out) / "anim"
    folder = root / name
    if folder.exists() and any(folder.iterdir()):
        print(f"{folder} exists and is not empty; choose another --name or delete it")
        return 1
    if root.exists() and len([d for d in root.iterdir() if d.is_dir()]) >= MAX_ANIMS:
        print(f"limit of {MAX_ANIMS} animations reached")
        return 1

    max_frames = min(args.max_frames, MAX_FRAMES)
    total = 0
    n = 0
    for n, frame in enumerate(load_frames(source, max_frames)):
        out = fit_image(frame, args.fit)
        total += save_baseline(out, folder / f"{n:04d}.jpg", args.quality)
    if total == 0:
        print("no frames found")
        return 1
    frames = n + 1
    (folder / "anim.ini").write_text(f"fps={args.fps}\nloop={0 if args.no_loop else 1}\n", encoding="ascii")
    print(f"{frames} frame(s), {total // 1024} KB -> {folder.relative_to(args.out)} "
          f"(fps={args.fps}, loop={'off' if args.no_loop else 'on'})")
    print(f"note: decoding is ~100-300 ms per frame, so the real speed is about 3-8 fps")
    return 0


def cmd_logo(args) -> int:
    with Image.open(args.input) as im:
        out = fit_image(im, args.fit)
    dest = Path(args.out) / "boot" / "logo.jpg"
    size = save_baseline(out, dest, args.quality)
    print(f"boot logo -> {dest.relative_to(args.out)} ({size // 1024} KB)")
    return 0


def cmd_check(args) -> int:
    root = Path(args.root)
    bad = 0
    files = sorted((root / "photos").glob("*")) if (root / "photos").exists() else []
    files += sorted((root / "anim").glob("*/*.jpg")) if (root / "anim").exists() else []
    logo = root / "boot" / "logo.jpg"
    if logo.exists():
        files.append(logo)
    for f in files:
        if f.suffix.lower() not in (".jpg", ".jpeg") or f.name.startswith("."):
            continue
        if f.parent.name == "photos" and len(f.name) > MAX_PHOTO_NAME:
            print(f"{f.relative_to(root)}: name longer than {MAX_PHOTO_NAME} characters (ignored by the firmware)")
            bad += 1
        problem = jpeg_problem(f)
        if problem:
            print(f"{f.relative_to(root)}: {problem}")
            bad += 1
    print(f"{len(files)} file(s) checked, {bad} problem(s)")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        p.add_argument("--out", required=True, help="SD card root (e.g. E:/ or a local folder)")
        p.add_argument("--fit", choices=("crop", "contain"), default="crop",
                       help="crop = fill the screen (default), contain = black bars")
        p.add_argument("--quality", type=int, default=DEFAULT_QUALITY, help="JPEG quality 50-95")

    p = sub.add_parser("photo", help="convert photos into /photos")
    p.add_argument("inputs", nargs="+", help="image files or folders")
    common(p)
    p.set_defaults(fn=cmd_photo)

    p = sub.add_parser("anim", help="build a frame animation in /anim/<name>")
    p.add_argument("input", help="GIF/WebP/APNG file or a folder of frame images")
    p.add_argument("--name", help="animation name (ASCII, max 23 chars)")
    p.add_argument("--fps", type=int, default=10, help="playback rate 1-30 (default 10)")
    p.add_argument("--no-loop", action="store_true", help="play once and hold the last frame")
    p.add_argument("--max-frames", type=int, default=300)
    common(p)
    p.set_defaults(fn=cmd_anim)

    p = sub.add_parser("logo", help="create /boot/logo.jpg")
    p.add_argument("input")
    common(p)
    p.set_defaults(fn=cmd_logo)

    p = sub.add_parser("check", help="validate an SD card / folder")
    p.add_argument("root")
    p.set_defaults(fn=cmd_check)

    args = ap.parse_args()
    if hasattr(args, "quality"):
        args.quality = max(MIN_QUALITY, min(95, args.quality))
    if hasattr(args, "fps"):
        args.fps = max(1, min(30, args.fps))
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
