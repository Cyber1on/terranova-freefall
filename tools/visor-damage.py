#!/usr/bin/env python3
"""Makes the "Visor damage" pictures built into tnfree (port/mods/visor.c) from the painted pieces.

    python tools/visor-damage.py

    port/res/visor/src/shrapnel-<a|b|c>.png, cracks-<a|b|c>.png   384 x 384, painted on black
    port/res/visor/src/laser-<a|b|c>.png           448 x 256, a burn trail from left to right, on black
 -> port/res/visor/<kind>-<style>-light.png, -heavy.png   RGBA, the same size for both states

Three styles of each kind (a, b, c): the game picks one of a kind's styles for a mission.  The
pieces are pixel art painted on a black background: a pixel's alpha is its brightness (black =
clear glass), its colour the paint's own.  Light is a part of the picture and heavy all of it, so
heavy grows out of light; where a picture is cut to a part, the cut is an ordered dither (whole
pixels, no soft alpha).  Nothing but the cracks' left edge (they grow in from the visor's edge)
reaches a picture's edge.  A laser trail is turned 24 degrees (nearest pixel) and each end that
stops inside the view thins out to nothing.
"""
import os
import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "port", "res", "visor")
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) / 16 + 1 / 32


def bayer(h, w):
    return np.tile(BAYER, (h // 4 + 1, w // 4 + 1))[:h, :w]


def src(name):
    a = np.asarray(Image.open(os.path.join(ROOT, "src", name + ".png")).convert("RGB"), np.float32) / 255
    return heat_cracks(a, name) if name.startswith("laser") else a


def heat_cracks(a, name):
    """fine pale cracks in the glass along a burn trail (owner): thin broken lines that leave the
    trail to both sides, some forking, drawn the same way every time (the name seeds the dice)"""
    import random
    from PIL import ImageDraw
    rnd = random.Random(sum(map(ord, name)) * 7919)
    h, w = a.shape[:2]
    m = a.max(2)
    yy = np.arange(h, dtype=np.float32)[:, None]
    wgt = np.clip(m - 0.16, 0, None) ** 2
    col = wgt.sum(0)
    yc = (wgt * yy).sum(0) / np.maximum(col, 1e-6)
    lit = m > 0.2
    xs = [x for x in range(8, w - 8) if col[x] > 1e-3]
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)

    def crack(x, y, ang, length, bright, depth=0):
        while length > 0:
            step = rnd.uniform(4, 9)
            ang += rnd.uniform(-0.55, 0.55)
            nx, ny = x + np.cos(ang) * step, y + np.sin(ang) * step
            d.line((x, y, nx, ny), fill=int(bright))
            x, y = nx, ny
            length -= step
            bright *= 0.93
            if depth < 2 and rnd.random() < 0.22:
                crack(x, y, ang + rnd.choice((-1, 1)) * rnd.uniform(0.5, 1.1), length * 0.6, bright * 0.85, depth + 1)

    for i in range(int(len(xs) / 11)):
        x = rnd.choice(xs)
        up = rnd.choice((-1, 1))
        ys = np.nonzero(lit[:, x])[0]
        y = (ys.min() if up < 0 else ys.max()) if len(ys) else yc[x]     # from the burn's rim
        crack(x, y, up * rnd.uniform(0.7, 2.4), rnd.uniform(16, 58), rnd.uniform(170, 255))
    c = np.asarray(mask, np.float32)[..., None] / 255 * np.array([0.93, 0.96, 1.0], np.float32)
    return np.maximum(a, c)


def falloff(n, cx, cy, r0, r1, sx=1.0, sy=1.0, rot=0.0):
    """1 within r0 of (cx, cy), dithered away to 0 at r1 (an ellipse sx by sy, turned by rot)"""
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    dx, dy = xx - cx, yy - cy
    c, s = np.cos(rot), np.sin(rot)
    d = np.hypot((dx * c + dy * s) / sx, (-dx * s + dy * c) / sy)
    return (np.clip((r1 - d) / (r1 - r0), 0, 1) > bayer(n, n)).astype(np.float32)


def pale(name, gain, floor, strength):
    """a pale piece (cracks, chips): alpha from brightness, the colour at full brightness"""
    a = src(name)
    m = a.max(2)
    al = np.clip((m - floor) * gain, 0, 1) * strength
    return a / np.maximum(m, 1e-3)[..., None] * np.clip(m * 1.25, 0, 1)[..., None], al


def trail(name, x0, x1, tl, tr, edge=0):
    """a trail's part x0..x1, its ends thinned by dither over tl / tr px (0: a clean cut, for a
    trail of separate marks); edge: nothing within that many px of the top, bottom and right"""
    a = src(name).copy()
    xx = np.arange(448, dtype=np.float32)
    t = np.ones(448, np.float32)
    if tl:
        t = np.minimum(t, np.clip((xx - x0) / tl, 0, 1))
    else:
        t[xx < x0] = 0
    if tr:
        t = np.minimum(t, np.clip((x1 - xx) / tr, 0, 1))
    else:
        t[xx >= x1] = 0
    keep = t[None, :] > bayer(256, 448) * 0.95
    if edge:
        yy = np.arange(256, dtype=np.float32)[:, None]
        e = np.minimum(np.minimum(yy, 255 - yy), (447 - xx)[None, :]) / edge
        keep &= np.clip(e, 0, 1) > bayer(256, 448)
    a *= keep[..., None]
    a = np.asarray(Image.fromarray((a * 255).astype(np.uint8)).rotate(24, Image.NEAREST, expand=True), np.float32) / 255
    m = a.max(2)
    return np.clip(a * 1.15, 0, 1), np.clip((m - 0.05) * 7, 0, 1) * 0.85


def laser(part, taper_right):
    """the burn trail, its first `part`: the half width allowed round the trail's own centre line
    grows from 0 over 120 px at each end inside the view, and the soot thins out by dither"""
    a = src("laser-a") * 255
    m = a.max(2)
    yy = np.arange(256, dtype=np.float32)[:, None]
    wgt = np.clip(m - 40, 0, None) ** 2
    yc = (wgt * yy).sum(0) / np.maximum(wgt.sum(0), 1e-3)
    yc = np.convolve(np.pad(yc, 15, "edge"), np.ones(31) / 31, "valid")
    xx = np.arange(448, dtype=np.float32)
    end = 448 * part
    t = np.clip((xx - 6) / 120, 0, 1)
    if taper_right or part < 1:
        t = np.minimum(t, np.clip((end - 6 - xx) / 120, 0, 1))
    hw = 1.0 + 62 * t ** 1.5
    d = np.abs(yy - yc[None, :])
    keep = (d <= hw[None, :]) & ((d <= 2.5) | (t[None, :] > bayer(256, 448) * 0.9)) & (t[None, :] > 0)
    a *= keep[..., None]
    a = np.asarray(Image.fromarray(a.astype(np.uint8)).rotate(24, Image.NEAREST, expand=True), np.float32) / 255
    m = a.max(2)
    return np.clip(a * 1.15, 0, 1), np.clip((m - 0.05) * 7, 0, 1) * 0.85


def save(name, rgb, al):
    out = np.dstack([rgb, al])
    out[al <= 0] = 0
    Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(
        os.path.join(ROOT, name + ".png"), optimize=True)
    edge = max(al[0].max(), al[-1].max(), al[:, -1].max(), 0 if name.startswith("cracks") else al[:, 0].max())
    print("%-16s %4d x %-4d  %5.1f%% covered, most alpha at an edge that must be clear: %.2f"
          % (name, al.shape[1], al.shape[0], 100 * (al > 0).mean(), edge))


def main():
    f = falloff
    # style a
    rgb, al = pale("cracks-a", 2.2, 0.10, 0.80)
    save("cracks-a-light", rgb, al * f(384, 0, 190, 90, 190, 1.0, 0.8))
    save("cracks-a-heavy", rgb, al * f(384, 40, 190, 290, 340, 1.0, 0.54))
    rgb, al = pale("shrapnel-a", 1.8, 0.14, 0.62)
    save("shrapnel-a-light", rgb, al * f(384, 150, 150, 50, 120, 1.5, 0.8, -0.7))
    save("shrapnel-a-heavy", rgb, al * f(384, 190, 190, 110, 180, 1.05, 0.85, -0.7))
    save("laser-a-light", *laser(0.5, True))
    save("laser-a-heavy", *laser(1.0, True))
    # style b: thin branching cracks, a few big gouges, a twin burn trail
    rgb, al = pale("cracks-b", 2.2, 0.10, 0.80)
    save("cracks-b-light", rgb, al * f(384, 0, 180, 90, 190, 1.0, 0.8))
    save("cracks-b-heavy", rgb, al * f(384, 40, 185, 290, 340, 1.0, 0.56))
    rgb, al = pale("shrapnel-b", 1.8, 0.14, 0.66)
    save("shrapnel-b-light", rgb, al * f(384, 120, 170, 55, 115, 1.0, 1.5, 0.5))
    save("shrapnel-b-heavy", rgb, al * f(384, 190, 195, 150, 190))
    save("laser-b-light", *trail("laser-b", 0, 250, 70, 90))
    save("laser-b-heavy", *trail("laser-b", 0, 448, 70, 90))
    # style c: crazed glass, a fine sandblast, a sweep ending in a blister
    rgb, al = pale("cracks-c", 1.5, 0.16, 0.55)
    save("cracks-c-light", rgb, al * f(384, 0, 190, 50, 150, 1.0, 1.1))
    save("cracks-c-heavy", rgb, al * f(384, 0, 190, 200, 335, 1.0, 0.56))
    rgb, al = pale("shrapnel-c", 2.4, 0.10, 0.72)
    save("shrapnel-c-light", rgb, al * f(384, 192, 192, 40, 110))
    save("shrapnel-c-heavy", rgb, al * f(384, 192, 192, 120, 188))
    save("laser-c-light", *trail("laser-c", 30, 280, 60, 110))
    save("laser-c-heavy", *trail("laser-c", 30, 448, 60, 0, 14))


if __name__ == "__main__":
    main()
