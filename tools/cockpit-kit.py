#!/usr/bin/env python3
"""cockpit-kit.py -- make the 16:9 cockpit starter kit from the player's own game files.

    python tools/cockpit-kit.py <game folder (TNOVA, or its DATA)> <output folder>

Writes, for each suit (scout, standard, heavy, pirate) and detail level (200 = low, 400 = high
detail rows):

    originals/<suit>-<rows>-cockpit.png     the game's art as it is (320 wide)
    originals/<suit>-<rows>-goggles.png
    Mods/MyCockpits/Cockpits/<suit>-<rows>-cockpit.png   426 wide drafts to paint over (a mod
    Mods/MyCockpits/Cockpits/<suit>-<rows>-goggles.png   folder: zip MyCockpits to share it)
    guides/<rows>-guide.png                 the 4:3 middle marked (a layer to put on top)
    palette/terra-nova-cockpit.gpl          the mission palette (Aseprite, LibreSprite, GIMP)
    palette/palette.png                     the same as numbered swatches, unsafe ones crossed

All pictures are 8-bit indexed PNGs in the mission palette; index 0 is see-through (the 3D
view shows there).  The drafts keep the game's 320 columns in the middle (x 53-372) and fill 53
columns on each side: rows of the window get their side pillar moved out (a wider window),
the other rows continue the outermost metal by repeating its pattern.  They are starting points,
not finished art.  Nothing here goes into the repository: it is the game's art.

Needs Pillow and numpy (pip install pillow numpy).
"""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

E = 53                                  # columns added on each side: 320 at 4:3 -> 426 at 16:9
SUITS = [("S", "scout"), ("N", "standard"), ("H", "heavy"), ("P", "pirate")]
UNSAFE = set(range(17, 21)) | set(range(146, 256))   # change with the planet, or blink


def res_dir(data):
    d = data
    off = struct.unpack_from("<I", d, 0x7C)[0]
    n, pos = struct.unpack_from("<HI", d, off)
    p = off + 6
    out = {}
    for _ in range(n):
        rid, a, b = struct.unpack_from("<HII", d, p)
        p += 10
        csize = b & 0xFFFFFF
        out[rid] = (pos, csize, a >> 24)
        pos += (csize + 3) & ~3
    return out


def res_get(path, rid):
    d = open(path, "rb").read()
    pos, size, flags = res_dir(d)[rid]
    if flags & 1:
        raise SystemExit("%s: resource %d is LZW-packed (not expected here)" % (path, rid))
    return d[pos:pos + size]


def rsd(b, p, n):
    """the Looking Glass run-length ("RSD") bitmap code"""
    out = bytearray()
    while len(out) < n and p < len(b):
        op = b[p]
        p += 1
        if op == 0:
            out += bytes([b[p + 1]]) * b[p]
            p += 2
        elif op < 128:
            out += b[p:p + op]
            p += op
        elif op > 128:
            out += bytes(op - 128)
        else:
            c = struct.unpack_from("<H", b, p)[0]
            p += 2
            if c == 0:
                break
            if c < 0x8000:
                out += bytes(c)
            elif c < 0xC000:
                c &= 0x3FFF
                out += b[p:p + c]
                p += c
            else:
                c &= 0x3FFF
                out += bytes([b[p]]) * c
                p += 1
    return bytes(out[:n]).ljust(n, b"\0")


def frame(path, rid, k=0):
    """frame k of a compound bitmap resource: (h x w index array, palette block or None)"""
    b = res_get(path, rid)
    n = struct.unpack_from("<H", b, 0)[0]
    offs = [struct.unpack_from("<I", b, 2 + 4 * i)[0] for i in range(n + 1)]
    f = b[offs[k]:offs[k + 1]]
    typ, _, _, w, h, row = struct.unpack_from("<BBHHHH", f, 4)
    if typ == 4:
        px = rsd(f, 28, w * h)
    else:
        px = b"".join(f[28 + y * row:28 + y * row + w] for y in range(h))
    pal = None
    po = struct.unpack_from("<I", f, 24)[0]
    if po:
        start, cnt = struct.unpack_from("<HH", b, po)
        pal = (start, b[po + 4:po + 4 + cnt * 3])
    return np.frombuffer(px, np.uint8).reshape(h, w).copy(), pal


def find(folder, name):
    for d in (folder, os.path.join(folder, "DATA"), os.path.join(folder, "TNOVA", "DATA")):
        if os.path.isdir(d):
            for f in os.listdir(d):
                if f.upper() == name.upper():
                    return os.path.join(d, f)
    raise SystemExit("cannot find %s in %s" % (name, folder))


def side_fill(a, rgb):
    """E columns left of a: each row repeats its outermost pattern (period chosen over 7 rows)"""
    h, w = a.shape
    out = np.zeros((h, E), np.uint8)
    for y in range(h):
        y0, y1 = max(0, y - 3), min(h, y + 4)
        best = None
        for p in range(2, 17):          # short: no copies of buttons further in
            n = min(24, w - p)
            err = np.abs(rgb[y0:y1, :n] - rgb[y0:y1, p:p + n]).sum() / (n * (y1 - y0)) + p * 0.6
            if best is None or err < best[0]:
                best = (err, p)
        p = best[1]
        for d in range(E):
            out[y, E - 1 - d] = a[y, p - 1 - d % p]
    return out


def window(a):
    """the main view's window: the see-through pixels joined to the middle of its upper part"""
    h, w = a.shape
    win = np.zeros((h, w), bool)
    seeds = [(y, w // 2) for y in range(h // 5, h // 2) if a[y, w // 2] == 0][:1]
    while seeds:
        y, x = seeds.pop()
        if 0 <= y < h and 0 <= x < w and not win[y, x] and a[y, x] == 0:
            win[y, x] = True
            seeds += [(y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)]
    return win


def widen(a, pal_rgb, goggles):
    h, w = a.shape
    win = window(a)
    rgb = pal_rgb[a]
    left = side_fill(a, rgb)
    right = side_fill(a[:, ::-1], rgb[:, ::-1])[:, ::-1]
    out = np.hstack([left, a, right])
    for y in range(h):
        for flip in (0, 1):
            r = a[y, ::-1] if flip else a[y]
            rw = win[y, ::-1] if flip else win[y]
            if rw[0] or (r[0] == 0 and (goggles or y == 0)):
                part = np.zeros(E, np.uint8)        # the window (or see-through) goes on
            elif r[0] == 0:                         # a screen at the edge: the side above goes on
                part = out[y - 1, -E:] if flip else out[y - 1, :E]
            else:
                part = None
            if part is not None:
                if flip:
                    out[y, -E:] = part
                else:
                    out[y, :E] = part
                continue
            z = np.nonzero(rw)[0]
            if not len(z) or z[0] >= 60:
                continue                # no window beside a pillar (or buttons): metal
            xw = z[0]                   # the pillar moved out by E, the window wider
            new = np.concatenate([r[:xw], np.zeros(E, np.uint8), r[xw:w // 2]])
            if flip:
                out[y, -(E + w // 2):] = new[::-1]
            else:
                out[y, :E + w // 2] = new
    return out


def save(a, pal, path):
    im = Image.fromarray(a, "P")
    im.putpalette(pal)
    im.info["transparency"] = 0
    im.save(path, transparency=0)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    hel = frame(find(src, "RESHELMN.RES"), 750)[1]
    planet = res_get(find(src, "RESPLNT0.RES"), 50)
    pal = bytearray(768)
    pal[hel[0] * 3:hel[0] * 3 + len(hel[1])] = hel[1]
    pal[51:51 + len(planet)] = planet[:768 - 51]
    pal_rgb = np.frombuffer(bytes(pal), np.uint8).reshape(256, 3).astype(int)
    pack = os.path.join(dst, "Mods", "MyCockpits", "Cockpits")
    for d in ("originals", pack, "guides", "palette"):
        os.makedirs(os.path.join(dst, d), exist_ok=True)
    for letter, suit in SUITS:
        for rows, lo, hi in ((200, "RESHELM", 0), (400, "RESHLMH", 1)):
            path = find(src, lo + letter + ".RES")
            for rid, part in ((750, "cockpit"), (751, "goggles")):
                a = frame(path, rid)[0]
                name = "%s-%d-%s.png" % (suit, rows, part)
                save(a, pal, os.path.join(dst, "originals", name))
                save(widen(a, pal_rgb, part == "goggles"), pal, os.path.join(pack, name))
                print(name)
    for rows in (200, 400):                     # the 4:3 middle: a layer to put on top
        g = Image.new("RGBA", (320 + 2 * E, rows), (0, 0, 0, 0))
        dr = ImageDraw.Draw(g)
        for x in (E - 1, E + 320):
            dr.line([(x, 0), (x, rows - 1)], fill=(255, 0, 255, 255))
        g.save(os.path.join(dst, "guides", "%d-guide.png" % rows))
    with open(os.path.join(dst, "palette", "terra-nova-cockpit.gpl"), "w") as f:
        f.write("GIMP Palette\nName: Terra Nova cockpit (mission)\nColumns: 16\n#\n")
        for i in range(256):
            r, g_, b = pal[i * 3:i * 3 + 3]
            note = "see-through" if i == 0 else "AVOID (planet/blink)" if i in UNSAFE else "safe"
            f.write("%3d %3d %3d\t%d %s\n" % (r, g_, b, i, note))
    sw = Image.new("RGB", (16 * 24, 16 * 24), (0, 0, 0))
    dr = ImageDraw.Draw(sw)
    for i in range(256):
        x, y = i % 16 * 24, i // 16 * 24
        c = tuple(pal[i * 3:i * 3 + 3])
        dr.rectangle([x, y, x + 23, y + 23], fill=c)
        ink = (0, 0, 0) if sum(c) > 380 else (255, 255, 255)
        dr.text((x + 2, y + 2), str(i), fill=ink)
        if i in UNSAFE or i == 0:
            dr.line([(x, y), (x + 23, y + 23)], fill=(255, 0, 0), width=2)
    sw.save(os.path.join(dst, "palette", "palette.png"))


if __name__ == "__main__":
    main()
