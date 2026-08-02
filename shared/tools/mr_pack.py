#!/usr/bin/env python3
"""
MicroRender modern asset packer.

Designed for a modern host PC, not for DOS:
  - Tiled JSON maps/layers using CSV tile data
  - Aseprite JSON frame metadata over an indexed 8-bit BMP sprite sheet
  - raw indexed BMP sprites/tilesets

Output is a simple little-endian .MRP pack plus an optional C header.
"""
import argparse
import json
import os
import struct
import sys
import wave
from dataclasses import dataclass
from typing import Dict, List, Tuple

MAGIC = b"MRP1"
ENTRY_SPRITE_RAW = 1
ENTRY_SPRITE_RLE = 2
ENTRY_TILEMAP_U16 = 3
ENTRY_PALETTE_RGB = 4
ENTRY_ANIM = 5
ENTRY_COLLISION_U8 = 6
ENTRY_SPAWNS = 7
ENTRY_TRIGGERS = 8
ENTRY_TILE_FLAGS = 9
ENTRY_AUDIO_U8 = 10
ENTRY_PROJECT_INFO = 11

@dataclass
class Entry:
    name: str
    kind: int
    payload: bytes


def _u16(v: int) -> bytes:
    return struct.pack("<H", v & 0xFFFF)


def _u32(v: int) -> bytes:
    return struct.pack("<I", v & 0xFFFFFFFF)


def read_bmp8(path: str) -> Tuple[int, int, List[int], bytes]:
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    off = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40:
        raise ValueError(f"{path}: unsupported BMP DIB header")
    w = struct.unpack_from("<i", data, 18)[0]
    h_signed = struct.unpack_from("<i", data, 22)[0]
    planes = struct.unpack_from("<H", data, 26)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    comp = struct.unpack_from("<I", data, 30)[0]
    colors_used = struct.unpack_from("<I", data, 46)[0]
    if planes != 1 or bpp != 8 or comp != 0:
        raise ValueError(f"{path}: expected uncompressed 8-bit indexed BMP")
    top_down = h_signed < 0
    h = abs(h_signed)
    if w <= 0 or h <= 0:
        raise ValueError(f"{path}: invalid dimensions")
    colors = colors_used or 256
    palette = []
    pal_off = 14 + dib_size
    for i in range(min(colors, 256)):
        b, g, r, _ = struct.unpack_from("BBBB", data, pal_off + i * 4)
        palette.extend([r, g, b])
    while len(palette) < 256 * 3:
        palette.extend([0, 0, 0])
    stride = ((w + 3) // 4) * 4
    pixels = bytearray(w * h)
    for y in range(h):
        src_y = y if top_down else (h - 1 - y)
        row = data[off + src_y * stride: off + src_y * stride + w]
        pixels[y * w:(y + 1) * w] = row
    return w, h, palette, bytes(pixels)


def sprite_payload_raw(w: int, h: int, key: int, pixels: bytes) -> bytes:
    return _u16(w) + _u16(h) + _u16(key) + _u16(0) + _u32(len(pixels)) + pixels


def sprite_payload_rle(w: int, h: int, key: int, pixels: bytes) -> bytes:
    runs = []
    pool = bytearray()
    for y in range(h):
        x = 0
        while x < w:
            while x < w and pixels[y * w + x] == key:
                x += 1
            if x >= w:
                break
            start = x
            off = len(pool)
            while x < w and pixels[y * w + x] != key:
                pool.append(pixels[y * w + x])
                x += 1
            runs.append((start, y, x - start, off))
    out = bytearray()
    out += _u16(w) + _u16(h) + _u16(key) + _u16(len(runs)) + _u32(len(pool))
    for x, y, n, off in runs:
        out += struct.pack("<hhhI", x, y, n, off)
    out += pool
    return bytes(out)


def add_bmp_sprite(entries: List[Entry], path: str, name: str, rle: bool, key: int):
    w, h, pal, pix = read_bmp8(path)
    entries.append(Entry(name + ("_rle" if rle else ""), ENTRY_SPRITE_RLE if rle else ENTRY_SPRITE_RAW,
                         sprite_payload_rle(w, h, key, pix) if rle else sprite_payload_raw(w, h, key, pix)))
    if not any(e.kind == ENTRY_PALETTE_RGB for e in entries):
        entries.append(Entry("palette", ENTRY_PALETTE_RGB, bytes(pal)))


def add_aseprite(entries: List[Entry], bmp_path: str, json_path: str, prefix: str, key: int, rle: bool):
    w, h, pal, sheet = read_bmp8(bmp_path)
    meta = json.load(open(json_path, "r", encoding="utf-8"))
    frames_obj = meta.get("frames", [])
    if isinstance(frames_obj, dict):
        frames_iter = [(k, v) for k, v in frames_obj.items()]
    else:
        frames_iter = [(f.get("filename", f"frame{i}"), f) for i, f in enumerate(frames_obj)]
    anim_frames = []
    for i, (fname, f) in enumerate(frames_iter):
        fr = f.get("frame", f)
        x, y, fw, fh = int(fr["x"]), int(fr["y"]), int(fr["w"]), int(fr["h"])
        pix = bytearray(fw * fh)
        for row in range(fh):
            pix[row * fw:(row + 1) * fw] = sheet[(y + row) * w + x:(y + row) * w + x + fw]
        sname = f"{prefix}_{i:02d}"
        payload = sprite_payload_rle(fw, fh, key, bytes(pix)) if rle else sprite_payload_raw(fw, fh, key, bytes(pix))
        entries.append(Entry(sname, ENTRY_SPRITE_RLE if rle else ENTRY_SPRITE_RAW, payload))
        duration = int(f.get("duration", 100))
        ticks = max(1, duration // 16)
        anim_frames.append((sname, ticks))
    anim = bytearray()
    anim += _u16(len(anim_frames)) + _u16(1)  # loop=1
    for sname, ticks in anim_frames:
        nb = sname.encode("ascii", "replace")[:31]
        anim += bytes([len(nb)]) + nb + bytes(31 - len(nb)) + _u16(ticks)
    entries.append(Entry(prefix + "_anim", ENTRY_ANIM, bytes(anim)))
    if not any(e.kind == ENTRY_PALETTE_RGB for e in entries):
        entries.append(Entry("palette", ENTRY_PALETTE_RGB, bytes(pal)))



def add_wav8(entries: List[Entry], path: str, name: str):
    with wave.open(path, "rb") as w:
        ch = w.getnchannels()
        sw = w.getsampwidth()
        rate = w.getframerate()
        n = w.getnframes()
        raw = w.readframes(n)
    if ch != 1:
        # downmix by taking the first channel. Good enough for DOS asset prep.
        if sw == 1:
            raw = raw[0::ch]
        elif sw == 2:
            raw = b"".join(raw[i:i+2] for i in range(0, len(raw), ch * 2))
    if sw == 1:
        pcm = raw
    elif sw == 2:
        pcm = bytearray()
        for i in range(0, len(raw), 2):
            v = struct.unpack_from("<h", raw, i)[0]
            pcm.append(max(0, min(255, (v + 32768) >> 8)))
        pcm = bytes(pcm)
    else:
        raise ValueError(f"{path}: expected 8-bit or 16-bit PCM WAV")
    payload = _u16(rate) + _u16(8) + _u32(len(pcm)) + pcm
    entries.append(Entry(name, ENTRY_AUDIO_U8, payload))


def add_project_info(entries: List[Entry], manifest_path: str):
    text = open(manifest_path, "rb").read()
    entries.append(Entry("project_info", ENTRY_PROJECT_INFO, text))


def _tiled_property_dict(obj):
    return {p.get("name", ""): p.get("value") for p in obj.get("properties", [])}


def _tile_flag_from_props(props):
    flags = 0
    if props.get("solid") is True or props.get("collision") is True:
        flags |= 1
    if props.get("damage") is True or props.get("hurt") is True:
        flags |= 2
    if props.get("pickup") is True:
        flags |= 4
    if props.get("water") is True or props.get("slow") is True:
        flags |= 8
    return flags

def add_tiled(entries: List[Entry], path: str, name: str):
    m = json.load(open(path, "r", encoding="utf-8"))
    tw = int(m.get("tilewidth", 16))
    th = int(m.get("tileheight", 16))
    mw = int(m["width"])
    mh = int(m["height"])
    layers = m.get("layers", [])
    # First visible-ish tile layer with CSV/list data that is not named collision/solid.
    layer = None
    for l in layers:
        lname = str(l.get("name", "")).lower()
        if l.get("type") == "tilelayer" and isinstance(l.get("data"), list) and \
           ("collision" not in lname and "solid" not in lname):
            layer = l
            break
    if layer is None:
        for l in layers:
            if l.get("type") == "tilelayer" and isinstance(l.get("data"), list):
                layer = l
                break
    if layer is None:
        raise ValueError(f"{path}: expected finite Tiled JSON tile layer with CSV/list data")
    raw = layer["data"]
    if len(raw) != mw * mh:
        raise ValueError(f"{path}: layer size mismatch")
    payload = bytearray()
    payload += _u16(mw) + _u16(mh) + _u16(tw) + _u16(th)
    for gid in raw:
        payload += _u16(max(0, int(gid) - 1))
    entries.append(Entry(name, ENTRY_TILEMAP_U16, bytes(payload)))

    # Optional Tiled tile properties.  Tiled JSON stores properties inside
    # tilesets[].tiles[].properties.  We pack a compact flag table so the DOS
    # runtime can apply solid/damage/pickup/water behavior without parsing JSON.
    tile_flags = {}
    max_tile_id = -1
    firstgid_biases = []
    for ts in m.get("tilesets", []):
        firstgid = int(ts.get("firstgid", 1))
        firstgid_biases.append(firstgid)
        for tile in ts.get("tiles", []):
            tid = int(tile.get("id", 0)) + firstgid - 1
            props = _tiled_property_dict(tile)
            fl = _tile_flag_from_props(props)
            if fl:
                tile_flags[tid] = fl
                if tid > max_tile_id:
                    max_tile_id = tid
    if tile_flags:
        tf = bytearray()
        tf += _u16(max_tile_id + 1)
        for tid in range(max_tile_id + 1):
            tf.append(tile_flags.get(tid, 0) & 0xFF)
        entries.append(Entry(name + "_tileflags", ENTRY_TILE_FLAGS, bytes(tf)))

    # Optional Tiled collision layer.  Supports either:
    #   - tilelayer named Collision/Solid, nonzero GIDs are solid
    #   - objectgroup named Collision/Solid, rectangle objects mark solid tiles
    collision = bytearray(mw * mh)
    found_collision = False
    for l in layers:
        lname = str(l.get("name", "")).lower()
        if "collision" not in lname and "solid" not in lname:
            continue
        if l.get("type") == "tilelayer" and isinstance(l.get("data"), list):
            data = l["data"]
            if len(data) != mw * mh:
                raise ValueError(f"{path}: collision layer size mismatch")
            for i, gid in enumerate(data):
                if int(gid) != 0:
                    collision[i] = 1
                    found_collision = True
        elif l.get("type") == "objectgroup":
            for obj in l.get("objects", []):
                ox = int(float(obj.get("x", 0)))
                oy = int(float(obj.get("y", 0)))
                ow = int(float(obj.get("width", 0)))
                oh = int(float(obj.get("height", 0)))
                if ow <= 0 or oh <= 0:
                    continue
                tx0 = max(0, ox // tw)
                ty0 = max(0, oy // th)
                tx1 = min(mw - 1, (ox + ow - 1) // tw)
                ty1 = min(mh - 1, (oy + oh - 1) // th)
                for ty in range(ty0, ty1 + 1):
                    for tx in range(tx0, tx1 + 1):
                        collision[ty * mw + tx] = 1
                        found_collision = True
    if found_collision:
        cpayload = bytearray()
        cpayload += _u16(mw) + _u16(mh) + _u16(tw) + _u16(th)
        cpayload += collision
        entries.append(Entry(name + "_collision", ENTRY_COLLISION_U8, bytes(cpayload)))

    # Engine Pass 3: Tiled object layers for spawns/triggers.
    # Object group names containing Spawn/Spawns produce packed spawn records.
    # Object group names containing Trigger/Event produce packed trigger records.
    # Record format: u16 count, repeated {s16 x,y,w,h, u16 type, char name[24]}.
    spawns = []
    triggers = []
    for l in layers:
        lname = str(l.get("name", "")).lower()
        if l.get("type") != "objectgroup":
            continue
        for obj in l.get("objects", []):
            ox = int(float(obj.get("x", 0)))
            oy = int(float(obj.get("y", 0)))
            ow = int(float(obj.get("width", 0)))
            oh = int(float(obj.get("height", 0)))
            oname = str(obj.get("name", obj.get("type", "object")))[:24]
            otype = str(obj.get("type", "")).lower()
            props = {p.get("name", ""): p.get("value") for p in obj.get("properties", [])}
            typenum = int(props.get("type", 0) or 0)
            if not typenum:
                if "pickup" in otype or "pickup" in oname.lower(): typenum = 1
                elif "message" in otype or "trigger" in otype or "event" in otype: typenum = 2
                else: typenum = 1 if "spawn" in lname else 2
            rec = (ox, oy, ow, oh, typenum, oname)
            if "spawn" in lname:
                spawns.append(rec)
            if "trigger" in lname or "event" in lname:
                triggers.append(rec)
    def pack_objects(objs):
        out = bytearray()
        out += _u16(len(objs))
        for ox, oy, ow, oh, typenum, oname in objs:
            nb = oname.encode("ascii", "replace")[:24]
            out += struct.pack("<hhhhH", ox, oy, ow, oh, typenum)
            out += nb + bytes(24 - len(nb))
        return bytes(out)
    if spawns:
        entries.append(Entry(name + "_spawns", ENTRY_SPAWNS, pack_objects(spawns)))
    if triggers:
        entries.append(Entry(name + "_triggers", ENTRY_TRIGGERS, pack_objects(triggers)))


def write_pack(entries: List[Entry], out_path: str):
    directory = bytearray()
    data_blob = bytearray()
    for e in entries:
        nb = e.name.encode("ascii", "replace")[:31]
        offset = len(data_blob)
        data_blob += e.payload
        directory += bytes([len(nb)]) + nb + bytes(31 - len(nb))
        directory += _u16(e.kind) + _u16(0) + _u32(offset) + _u32(len(e.payload))
    header = MAGIC + _u16(len(entries)) + _u16(0) + _u32(12 + len(directory))
    open(out_path, "wb").write(header + directory + data_blob)


def write_c_header(pack_path: str, h_path: str, symbol: str):
    data = open(pack_path, "rb").read()
    with open(h_path, "w", encoding="utf-8") as f:
        f.write("/* Generated by mr_pack.py */\n")
        f.write(f"#ifndef {symbol.upper()}_H\n#define {symbol.upper()}_H\n")
        f.write(f"static const unsigned char {symbol}[] = {{\n")
        for i, b in enumerate(data):
            if i % 12 == 0:
                f.write("    ")
            f.write(f"0x{b:02X}")
            if i + 1 != len(data):
                f.write(",")
            if i % 12 == 11:
                f.write("\n")
            else:
                f.write(" ")
        f.write("\n};\n")
        f.write(f"static const unsigned long {symbol}_size = {len(data)}UL;\n")
        f.write(f"#endif /* {symbol.upper()}_H */\n")



def load_manifest_entries(entries: List[Entry], manifest_path: str, default_key: int):
    base = os.path.dirname(os.path.abspath(manifest_path))
    m = json.load(open(manifest_path, "r", encoding="utf-8"))
    key = int(m.get("transparent_index", default_key))
    for item in m.get("sprites", []):
        name = item["name"]
        path = os.path.join(base, item["file"])
        rle = bool(item.get("rle", True))
        add_bmp_sprite(entries, path, name, rle, int(item.get("key", key)))
    for item in m.get("aseprite", []):
        prefix = item["prefix"]
        sheet = os.path.join(base, item["sheet"])
        meta = os.path.join(base, item["json"])
        add_aseprite(entries, sheet, meta, prefix, int(item.get("key", key)), bool(item.get("rle", True)))
    for item in m.get("maps", []):
        add_tiled(entries, os.path.join(base, item["file"]), item["name"])
    for item in m.get("audio", []):
        add_wav8(entries, os.path.join(base, item["file"]), item["name"])
    add_project_info(entries, manifest_path)

def main(argv=None):
    ap = argparse.ArgumentParser(description="Build MicroRender .MRP packs from Tiled/Aseprite/BMP assets")
    ap.add_argument("-o", "--out", default="demo.mrp")
    ap.add_argument("--header", help="also emit C header with embedded pack bytes")
    ap.add_argument("--symbol", default="demo_mrp")
    ap.add_argument("--bmp", action="append", nargs=2, metavar=("NAME", "BMP"), default=[])
    ap.add_argument("--bmp-rle", action="append", nargs=2, metavar=("NAME", "BMP"), default=[])
    ap.add_argument("--aseprite", action="append", nargs=3, metavar=("PREFIX", "SHEET_BMP", "JSON"), default=[])
    ap.add_argument("--tiled", action="append", nargs=2, metavar=("NAME", "JSON"), default=[])
    ap.add_argument("--wav", action="append", nargs=2, metavar=("NAME", "WAV"), default=[])
    ap.add_argument("--manifest", help="project manifest JSON describing sprites, Aseprite sheets, Tiled maps and audio")
    ap.add_argument("--key", type=int, default=0)
    args = ap.parse_args(argv)

    entries: List[Entry] = []
    if args.manifest:
        load_manifest_entries(entries, args.manifest, args.key)
    for name, path in args.bmp:
        add_bmp_sprite(entries, path, name, False, args.key)
    for name, path in args.bmp_rle:
        add_bmp_sprite(entries, path, name, True, args.key)
    for name, path in args.tiled:
        add_tiled(entries, path, name)
    for prefix, bmp, js in args.aseprite:
        add_aseprite(entries, bmp, js, prefix, args.key, True)
    for name, path in args.wav:
        add_wav8(entries, path, name)
    if not entries:
        ap.error("no inputs; use --manifest, --bmp, --bmp-rle, --aseprite, --tiled, or --wav")
    write_pack(entries, args.out)
    if args.header:
        write_c_header(args.out, args.header, args.symbol)
    print(f"wrote {args.out} with {len(entries)} entries")

if __name__ == "__main__":
    main()
