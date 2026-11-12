#!/usr/bin/env python3
"""menu-wide.py -- the wide main menu's own parts (port/ui/menuwide.c) from the owner's locked art.

    python tools/menu-wide.py <game folder (TNOVA, or its DATA)> <art kit folder> <v2 picture>

<art kit folder> is the locked main menu kit (main-menu-LOCKED-v1: the owner's AI picture
source-owner-ai-1708x960.png and the finished 854 x 480 picture in the game's colours), <v2
picture> the kit's picture before its stars (main-menu-final-v2, 854 x 480).  Writes

    port/res/menu-wide.png     854 x 480, grey + alpha: grey = the game's colour number of our
                               pixel, alpha = how much of it goes over the game's (255 in the 107
                               columns each side, 0 in the game's own 640 except the planet band's
                               outer 56 columns, where the owner's AI band fades into the game's)
    port/ui/menuwide-stars.h      the sides' stars: each takes a star pixel of the player's own
                               picture (the locked art's stars are the game's, copied in 32 x 32
                               tiles with flips), and the check of the picture's pixels we use

Nothing of the game's picture is written: the stars and the band's mix are made from the player's
own picture when the main menu is on the screen.  The stars are placed as the kit's stars.py placed
them (the same random numbers), so the result is the locked picture; the check fails, so no stars
are written, if it is not.  Needs numpy, Pillow and OpenCV (pip install numpy pillow opencv-python).
"""
import os
import sys
import importlib.util

import cv2
import numpy as np
from PIL import Image

E = 107                                 # columns each side of the game's 640: 854 x 480 is 16:9
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

spec = importlib.util.spec_from_file_location("ck", os.path.join(ROOT, "tools", "cockpit-kit.py"))
ck = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ck)

B4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16 - 0.5


def quant(rgb, P, used):
    """nearest of the picture's own colours, light ordered dither (none in near-black)"""
    cols = P[used].astype(np.float32)
    h, w, _ = rgb.shape
    t = np.tile(B4, (h // 4 + 1, w // 4 + 1))[:h, :w, None] * 10
    t = t * np.clip(rgb.astype(np.float32).sum(2, keepdims=True) / 150, 0, 1)
    x = (rgb.astype(np.float32) + t).reshape(-1, 1, 3)
    out = np.empty(h * w, np.int64)
    for i in range(0, h * w, 20000):
        d = ((x[i:i + 20000] - cols[None]) ** 2).sum(2)
        out[i:i + 20000] = d.argmin(1)
    return used[out].reshape(h, w).astype(np.uint8)


def fnv(values):
    h = 0x811C9DC5
    for v in values:
        h = ((h ^ int(v)) * 0x01000193) & 0xFFFFFFFF
    return h


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    a, pal = ck.frame(ck.find(sys.argv[1], "RESBASE.RES"), 205, 0)    # the main menu
    P = np.zeros((256, 3), np.uint8)
    p = np.frombuffer(pal[1], np.uint8).reshape(-1, 3)
    P[pal[0]:pal[0] + len(p)] = p[:256 - pal[0]]
    used = np.unique(a)
    kit = sys.argv[2]
    locked = np.array(Image.open(os.path.join(kit, "main-menu-854x480-game-colours.png")))
    v2 = np.array(Image.open(sys.argv[3]))
    s = np.array(Image.open(os.path.join(kit, "source-owner-ai-1708x960.png")).convert("RGB")
                 .resize((854, 480), Image.LANCZOS)).astype(np.float32)

    # the stars, as stars.py placed them, with each one's place in the game's picture
    sum3 = s.sum(2)
    rng = np.random.default_rng(3)
    src = [(0, 35, 0, 250), (0, 35, 420, 640), (0, 160, 3, 75), (0, 150, 575, 637)]
    T = 32
    yy, xx = np.mgrid[0:T, 0:T]

    def tile():
        while True:
            r0, r1, c0, c1 = src[rng.integers(len(src))]
            if r1 - r0 < T or c1 - c0 < T:
                continue
            y = rng.integers(r0, r1 - T + 1)
            x = rng.integers(c0, c1 - T + 1)
            sy, sx = y + yy, x + xx
            if rng.random() < .5:
                sy, sx = sy[:, ::-1], sx[:, ::-1]
            if rng.random() < .5:
                sy, sx = sy[::-1], sx[::-1]
            return sy, sx

    sm = cv2.GaussianBlur(sum3, (0, 0), 6)
    sky = np.clip((40 - sm) / 25, 0, 1)
    rows = np.arange(480)[:, None]
    band = sm[120:320] > 55
    limb = np.where(band.any(0), band.argmax(0) + 120, 320).astype(np.float32)
    limb = cv2.GaussianBlur(limb[None, :], (0, 0), 8)[0]
    sky = sky * np.clip((limb[None, :] - 6 - rows) / 6, 0, 1)
    SY = np.full((480, 854), -1, np.int32)
    SX = np.full((480, 854), -1, np.int32)
    for y in range(0, 480, T):
        for x in list(range(0, E, T)) + list(range(E + 640, 854, T)):
            sy, sx = tile()
            h, w = min(T, 480 - y), min(T, 854 - x)
            SY[y:y + h, x:x + w] = sy[:h, :w]
            SX[y:y + h, x:x + w] = sx[:h, :w]
    has = SY >= 0
    layer = np.where(has, a[SY.clip(0), SX.clip(0)], 0)
    lum = P[layer].astype(np.float32).sum(2)
    side = np.zeros(854, bool)
    side[:E] = side[E + 640:] = True
    put = has & (lum > 20) & side[None, :] & (rng.random((480, 854)) < sky)
    check = v2.copy()
    check[put] = layer[put]
    if not (check == locked).all():
        raise SystemExit("the stars are not the locked picture's: nothing written")

    # the planet band's outer 56 columns: the AI's band over the game's, fading out by column 56
    W, br0, br1 = 56, 165, 285
    wb = np.zeros(480, np.float32)
    wb[br0:br1] = 1
    wb = cv2.GaussianBlur(wb[:, None], (0, 0), 10)[:, 0]
    wb[wb < 0.02] = 0
    m = np.zeros((480, 854), np.float32)
    for k in range(W):
        t = (k / W) ** 1.3
        m[:, E + k] = m[:, E + 639 - k] = (1 - t) * wb
    q = quant(s.clip(0, 255), P, used)
    grey = np.zeros((480, 854), np.uint8)
    alpha = np.zeros((480, 854), np.uint8)
    grey[:, :E], grey[:, E + 640:] = locked[:, :E], locked[:, E + 640:]
    alpha[:, :E] = alpha[:, E + 640:] = 255
    A = np.round(m * 255).astype(np.uint8)
    band = A > 0
    grey[band], alpha[band] = q[band], A[band]
    grey[put] = alpha[put] = 0
    Image.fromarray(np.dstack([grey, alpha]), "LA").save(os.path.join(ROOT, "port", "res", "menu-wide.png"),
                                                        optimize=True)

    # the check: the game's colour numbers at the star pixels (in the table's order), then at the
    # band's pixels (row by row): the picture is on the screen when they all match
    ys, xs = np.nonzero(put)
    stars = list(zip(xs, ys, SX[put], SY[put]))
    by, bx = np.nonzero(band)
    h = fnv([a[sy, sx] for _, _, sx, sy in stars] + [a[y, x - E] for y, x in zip(by, bx)])
    with open(os.path.join(ROOT, "port", "menuwide-stars.h"), "w") as f:
        f.write("/* generated by tools/menu-wide.py: the wide main menu's stars (port/ui/menuwide.c): the place\n"
                " * in the 854 x 480 picture, then the star pixel of the game's own picture it shows */\n")
        f.write("#define MW_STARS %d\n#define MW_BAND %d\n" % (len(stars), int(band.sum())))
        f.write("#define MW_CHECK 0x%08Xu  /* FNV-1a of the picture's colour numbers we use */\n" % h)
        f.write("static const unsigned short mw_star[MW_STARS][4] = {\n")
        for i in range(0, len(stars), 6):
            f.write("    " + " ".join("{%d,%d,%d,%d}," % tuple(int(v) for v in st) for st in stars[i:i + 6]) + "\n")
        f.write("};\n")
    print("stars %d, band pixels %d, check %08X" % (len(stars), int(band.sum()), h))


if __name__ == "__main__":
    main()
