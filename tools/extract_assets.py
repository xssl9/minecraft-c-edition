#!/usr/bin/env python3
"""
Minecraft C Edition asset importer.

Reads the vanilla Minecraft client jar and the PrismLauncher asset store from
the machine this game is being built on.  No Mojang files are shipped with the
source tree; they are imported locally for personal use.

Outputs:
    assets/minecraft/...              extracted vanilla resources
    assets/generated/world_atlas.rgb  1024x1024 RGB texture atlas
    assets/generated/world_atlas.txt  name -> atlas tile map
    assets/generated/menu.rgb         1024x1024 menu panorama
    assets/generated/logo.rgb         1024x1024 texture (logo in top 1024x256)
    assets/generated/font_ascii.alpha 128x128 font coverage map
    assets/sounds/...                 a small, useful sound set
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import struct
import sys
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PRISM = Path.home() / ".local/share/PrismLauncher"
VERSION = "1.21.1"
ATLAS = 1024
TILE = 16
TILES = ATLAS // TILE


# ---------------------------------------------------------------------------
# Minimal PNG codec (stdlib only)
# ---------------------------------------------------------------------------

def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter(raw: bytes, w: int, h: int, bpp: int, stride: int) -> bytearray:
    out = bytearray(w * h * bpp)
    pos = 0
    prev = bytearray(stride)
    for y in range(h):
        ft = raw[pos]
        pos += 1
        row = bytearray(raw[pos:pos + stride])
        pos += stride
        if ft == 1:
            for i in range(bpp, stride):
                row[i] = (row[i] + row[i - bpp]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                upleft = prev[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + _paeth(left, prev[i], upleft)) & 0xFF
        elif ft != 0:
            raise ValueError(f"bad PNG filter {ft}")
        out[y * stride:(y + 1) * stride] = row
        prev = row
    return out


def decode_png(data: bytes):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    width = height = bit_depth = color_type = 0
    palette = None
    trns = None
    idat = bytearray()
    while pos + 8 <= len(data):
        length, typ = struct.unpack(">I4s", data[pos:pos + 8])
        pos += 8
        payload = data[pos:pos + length]
        pos += length + 4
        if typ == b"IHDR":
            width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack(
                ">IIBBBBB", payload)
            if comp != 0 or filt != 0:
                raise ValueError("unsupported PNG compression/filter")
            if interlace:
                raise ValueError("interlaced PNG is not supported")
        elif typ == b"PLTE":
            palette = [tuple(payload[i:i + 3]) for i in range(0, len(payload), 3)]
        elif typ == b"tRNS":
            trns = payload
        elif typ == b"IDAT":
            idat.extend(payload)
        elif typ == b"IEND":
            break
    if not width or not height:
        raise ValueError("missing IHDR")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color_type]
    bits_per_pixel = channels * bit_depth
    bpp = max(1, (bits_per_pixel + 7) // 8)
    stride = (width * bits_per_pixel + 7) // 8
    raw = zlib.decompress(idat)
    scan = _unfilter(raw, width, height, bpp, stride)

    rgba = bytearray(width * height * 4)

    def sample(row: bytes, x: int, n: int, depth: int) -> int:
        if depth == 8:
            return row[x * n]
        if depth == 16:
            return row[x * n * 2]
        if depth == 1:
            return (row[x >> 3] >> (7 - (x & 7))) & 1
        if depth == 2:
            return (row[x >> 2] >> (6 - ((x & 3) << 1))) & 3
        if depth == 4:
            return (row[x >> 1] >> (4 - ((x & 1) << 2))) & 15
        raise ValueError("bad bit depth")

    maxval = (1 << bit_depth) - 1
    for y in range(height):
        row = scan[y * stride:(y + 1) * stride]
        for x in range(width):
            o = (y * width + x) * 4
            if color_type == 6:
                r, g, b, a = row[x * 4:x * 4 + 4]
            elif color_type == 2:
                r, g, b = row[x * 3:x * 3 + 3]
                a = 255
            elif color_type == 0:
                v = sample(row, x, 1, bit_depth)
                v = v * 255 // maxval if bit_depth < 8 else v
                r = g = b = v
                a = 255
            elif color_type == 4:
                v = sample(row, x, 1, bit_depth)
                v = v * 255 // maxval if bit_depth < 8 else v
                a = row[x * 2 + 1] if bit_depth == 8 else 255
                r = g = b = v
            elif color_type == 3:
                idx = sample(row, x, 1, bit_depth)
                if palette is None or idx >= len(palette):
                    r = g = b = 255
                    a = 0
                else:
                    r, g, b = palette[idx]
                    a = trns[idx] if trns is not None and idx < len(trns) else 255
            else:
                raise ValueError("bad color type")
            rgba[o:o + 4] = bytes((r, g, b, a))
    return width, height, rgba


def encode_png(path: Path, w: int, h: int, rgba: bytes):
    def chunk(t, p):
        return struct.pack(">I", len(p)) + t + p + struct.pack(">I", zlib.crc32(t + p) & 0xFFFFFFFF)
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw.extend(rgba[y * stride:(y + 1) * stride])
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    data += chunk(b"IEND", b"")
    path.write_bytes(data)


def resize_nearest(src, sw: int, sh: int, dw: int, dh: int):
    out = bytearray(dw * dh * 4)
    for y in range(dh):
        sy = min(sh - 1, y * sh // dh)
        for x in range(dw):
            sx = min(sw - 1, x * sw // dw)
            si = (sy * sw + sx) * 4
            di = (y * dw + x) * 4
            out[di:di + 4] = src[si:si + 4]
    return out


# ---------------------------------------------------------------------------
# Asset locating / extracting
# ---------------------------------------------------------------------------

def find_client_jar(prism: Path, version: str) -> Path:
    p = prism / "libraries/com/mojang/minecraft" / version / f"minecraft-{version}-client.jar"
    if p.exists():
        return p
    candidates = list((prism / "libraries/com/mojang/minecraft").glob("*/minecraft-*-client.jar"))
    if not candidates:
        raise FileNotFoundError("Minecraft client jar not found under PrismLauncher")
    return sorted(candidates)[-1]


def load_asset_index(prism: Path, version: str) -> tuple[dict, Path]:
    meta_path = prism / "meta/net.minecraft" / f"{version}.json"
    if not meta_path.exists():
        raise FileNotFoundError(f"Minecraft metadata not found: {meta_path}")
    meta = json.loads(meta_path.read_text())
    index_id = meta.get("assetIndex", {}).get("id")
    if not index_id:
        raise ValueError("asset index id missing in metadata")
    index_path = prism / "assets/indexes" / f"{index_id}.json"
    return json.loads(index_path.read_text())["objects"], index_path


def object_path(prism: Path, h: str) -> Path | None:
    base = prism / "assets/objects" / h[:2]
    # Prism usually uses the first two characters as the directory and the
    # rest as the file name.  Some local stores keep the complete hash.
    for name in (h[2:], h):
        p = base / name
        if p.exists():
            return p
    return None


def extract_jar(jar: Path, out: Path):
    mine = out / "minecraft"
    mine.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(jar) as z:
        for info in z.infolist():
            name = info.filename
            if not name.startswith("assets/minecraft/"):
                continue
            if info.is_dir():
                continue
            dest = mine / name[len("assets/minecraft/"):]
            dest.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, open(dest, "wb") as f:
                shutil.copyfileobj(src, f)


def copy_sound(prism: Path, objects: dict, key: str, out: Path) -> bool:
    info = objects.get(key)
    if not info:
        return False
    src = object_path(prism, info["hash"])
    if not src:
        return False
    dest = out / "minecraft" / key[len("minecraft/"):]
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dest)
    return True


def copy_external_png(prism: Path, objects: dict, key: str, dest: Path) -> bool:
    info = objects.get(key)
    if not info:
        return False
    src = object_path(prism, info["hash"])
    if not src:
        return False
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dest)
    return True


# ---------------------------------------------------------------------------
# Atlas generation
# ---------------------------------------------------------------------------

def load_texture(p: Path):
    w, h, rgba = decode_png(p.read_bytes())
    # Animated block/item textures are vertical strips.  Use their first frame.
    if h > w and h % w == 0:
        h = w
        rgba = rgba[:w * h * 4]
    return w, h, rgba


def solidify(rgba: bytearray, w: int, h: int, passes: int = 3):
    """Replace transparent texels with nearby colour so cutouts have no black fringe."""
    for _ in range(passes):
        changed = []
        for y in range(h):
            for x in range(w):
                o = (y * w + x) * 4
                if rgba[o + 3] != 0:
                    continue
                rs = gs = bs = n = 0
                for dy in (-1, 0, 1):
                    yy = y + dy
                    if yy < 0 or yy >= h:
                        continue
                    for dx in (-1, 0, 1):
                        xx = x + dx
                        if xx < 0 or xx >= w:
                            continue
                        q = (yy * w + xx) * 4
                        if rgba[q + 3] != 0:
                            rs += rgba[q]
                            gs += rgba[q + 1]
                            bs += rgba[q + 2]
                            n += 1
                if n:
                    changed.append((o, (rs // n, gs // n, bs // n)))
        for o, (r, g, b) in changed:
            rgba[o:o + 4] = bytes((r, g, b, 255))
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 4
            if rgba[o + 3] == 0:
                rgba[o:o + 4] = b"\x30\x30\x30\xff"


def build_world_atlas(assets: Path, gen: Path):
    textures = []
    for sub in ("block", "item"):
        base = assets / "minecraft/textures" / sub
        if not base.exists():
            continue
        for p in sorted(base.rglob("*.png")):
            rel = p.relative_to(assets / "minecraft/textures").with_suffix("").as_posix()
            textures.append((rel, p))
    if not textures:
        raise RuntimeError("no block/item textures found")

    atlas = bytearray([0] * (ATLAS * ATLAS * 3))
    atlas_rgba = bytearray([0] * (ATLAS * ATLAS * 4))
    # debug background
    for i in range(ATLAS * ATLAS):
        atlas[i * 3 + 0] = 20
        atlas[i * 3 + 1] = 18
        atlas[i * 3 + 2] = 24

    mapping = {}
    for i, (rel, p) in enumerate(textures):
        if i >= TILES * TILES:
            print(f"warning: atlas full, skipping {rel}", file=sys.stderr)
            break
        try:
            w, h, rgba = load_texture(p)
            if w <= 0 or h <= 0:
                continue
            rgba = resize_nearest(rgba, w, h, TILE, TILE)
            # Store pure RGBA before any RGB-only solidification
            tx, ty = (i % TILES) * TILE, (i // TILES) * TILE
            for yy in range(TILE):
                for xx in range(TILE):
                    si = (yy * TILE + xx) * 4
                    di = ((ty + yy) * ATLAS + tx + xx) * 4
                    atlas_rgba[di:di + 4] = rgba[si:si + 4]
            solidify(rgba, TILE, TILE)
        except Exception as exc:
            print(f"warning: {rel}: {exc}", file=sys.stderr)
            continue
        for yy in range(TILE):
            for xx in range(TILE):
                si = (yy * TILE + xx) * 4
                di = ((ty + yy) * ATLAS + tx + xx) * 3
                atlas[di:di + 3] = rgba[si:si + 3]
        mapping[rel] = i

    gen.mkdir(parents=True, exist_ok=True)
    (gen / "world_atlas.rgba").write_bytes(atlas_rgba)
    (gen / "world_atlas.rgb").write_bytes(atlas)
    with open(gen / "world_atlas.txt", "w", encoding="utf-8") as f:
        for rel, i in sorted(mapping.items()):
            f.write(f"{rel} {i}\n")

    # Small preview (may use a lot of memory but only runs at import time).
    prev = bytearray(ATLAS * ATLAS * 4)
    for i in range(ATLAS * ATLAS):
        prev[i * 4 + 0] = atlas[i * 3 + 0]
        prev[i * 4 + 1] = atlas[i * 3 + 1]
        prev[i * 4 + 2] = atlas[i * 3 + 2]
        prev[i * 4 + 3] = 255
    encode_png(gen / "world_atlas_preview.png", ATLAS, ATLAS, prev)
    print(f"world atlas: {len(mapping)} textures")
    return mapping


def write_rgb(gen: Path, name: str, w: int, h: int, rgba: bytes):
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        rgb[i * 3:i * 3 + 3] = rgba[i * 4:i * 4 + 3]
    (gen / name).write_bytes(rgb)


def build_menu(assets: Path, prism: Path, objects: dict, gen: Path):
    gen.mkdir(parents=True, exist_ok=True)
    pano = assets / "minecraft/textures/gui/title/background/panorama_0.png"
    # The jar contains a 1x1 placeholder panorama; the real image lives in the
    # object store.  Prefer the object-store version whenever it is available.
    real = gen / "_panorama_source.png"
    if copy_external_png(prism, objects, "minecraft/textures/gui/title/background/panorama_0.png", real):
        pano = real
        if pano.exists() and pano.stat().st_size > 1000:
            shutil.copyfile(real, assets / "minecraft/textures/gui/title/background/panorama_0.png")
    if not pano.exists():
        pano = None
    if pano and pano.exists():
        try:
            w, h, rgba = decode_png(pano.read_bytes())
            rgba = resize_nearest(rgba, w, h, ATLAS, ATLAS)
            write_rgb(gen, "menu.rgb", ATLAS, ATLAS, rgba)
            print("menu panorama imported")
        except Exception as exc:
            print(f"warning: panorama: {exc}", file=sys.stderr)

    logo = assets / "minecraft/textures/gui/title/minecraft.png"
    if logo.exists():
        try:
            w, h, rgba = decode_png(logo.read_bytes())
            # Logo is 1024x256 in 1.21.  Keep its 4:1 aspect ratio.
            lw, lh = ATLAS, max(1, ATLAS // 4)
            rgba = resize_nearest(rgba, w, h, lw, lh)
            canvas = bytearray(lw * ATLAS * 4)
            for y in range(lh):
                canvas[y * lw * 4:(y + 1) * lw * 4] = rgba[y * lw * 4:(y + 1) * lw * 4]
            write_rgb(gen, "logo.rgb", lw, ATLAS, canvas)
            print("title logo imported")
        except Exception as exc:
            print(f"warning: logo: {exc}", file=sys.stderr)


def build_font(assets: Path, gen: Path):
    p = assets / "minecraft/textures/font/ascii.png"
    if not p.exists():
        print("warning: ascii font missing", file=sys.stderr)
        return
    w, h, rgba = decode_png(p.read_bytes())
    alpha = bytearray(w * h)
    for i in range(w * h):
        alpha[i] = rgba[i * 4 + 3]
    (gen / "font_ascii.alpha").write_bytes(alpha)
    print(f"font imported: {w}x{h}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prism", type=Path, default=DEFAULT_PRISM)
    ap.add_argument("--version", default=VERSION)
    ap.add_argument("--out", type=Path, default=ROOT / "assets")
    ap.add_argument("--all-sounds", action="store_true",
                    help="copy every sound object (about 1.5 GiB)")
    args = ap.parse_args()

    prism = args.prism.expanduser()
    out = args.out
    objects, index_path = load_asset_index(prism, args.version)
    jar = find_client_jar(prism, args.version)
    print(f"client jar: {jar}")
    print(f"asset index: {index_path} ({len(objects)} objects)")

    extract_jar(jar, out)
    print(f"jar assets: {out / 'minecraft'}")

    # The 1.21 jar has placeholder panoramas; pull real ones from objects.
    build_menu(out, prism, objects, out / "generated")
    build_world_atlas(out, out / "generated")
    build_font(out, out / "generated")

    sounds = out / "sounds"
    wanted = []
    for name in ("random/click", "random/break"):
        wanted.append(f"minecraft/sounds/{name}.ogg")
    for kind in ("stone", "wood", "grass"):
        for i in range(1, 5):
            wanted.append(f"minecraft/sounds/dig/{kind}{i}.ogg")
            wanted.append(f"minecraft/sounds/step/{kind}{i}.ogg")
    wanted.append("minecraft/sounds/music/menu/moog_city_2.ogg")
    wanted.append("minecraft/sounds/ambient/cave/cave1.ogg")
    for key in wanted:
        copy_sound(prism, objects, key, sounds)

    if args.all_sounds:
        for key in objects:
            if key.startswith("minecraft/sounds/") and key.endswith((".ogg", ".wav")):
                copy_sound(prism, objects, key, sounds)
        print("all sounds copied")

    print("done.  Run: cmake -S . -B build && cmake --build build -j")


if __name__ == "__main__":
    main()
