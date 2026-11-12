/* dmgdisp.c -- Terra Nova modules M033 (0x27A0D0-0x27A8B2) and M034 (0x27A8C0-0x27ACB1): how a
 * damaged helmet shows in the HUD views -- random view drop-outs, shaking, picture glitches
 * (shifted strips, noise pixels, sheared rows) and rows of static noise.  Decompiled from
 * __FF.EXE v1.09.
 *
 *   0x27A0D0 dmgdisp_27A0D0 random drop-out of the camera view (damage < 10)
 *   0x27A118 dmgdisp_27A118 drop-out level of a view (0 none, 1, 2)
 *   0x27A2BC dmgdisp_27A2BC shake offset of a view
 *   0x27A3FC dmgdisp_27A3FC render a view with the camera in mode 4
 *   0x27A4E8 dmgdisp_27A4E8 byte copy (no callers)
 *   0x27A500 dmgdisp_27A500 copy a canvas rectangle to an offset (6 args, 2 on the stack)
 *   0x27A618 dmgdisp_27A618 random picture glitch + sound
 *   0x27A8C0 dmgdisp_27A8C0 fill the noise table        0x27A908 dmgdisp_27A908 random noise start
 *   0x27A924 dmgdisp_27A924 copy noise into a row       0x27A970 dmgdisp_27A970 noise runs and gaps
 *   0x27AA80 dmgdisp_27AA80 clear the noise lines       0x27AAC0 dmgdisp_27AAC0 start a noise line
 *   0x27AB40 dmgdisp_27AB40 draw the noise lines
 *
 * 0x27A5F8 is the jump table of 0x27A618's switch (data, not code); 0x27A70C ... 0x27A862 are
 * its case labels.  0x27A4E8 shares 0x27A2BC's `pop ecx; ret`.
 *
 * Quirks kept:
 *  - dmgdisp_27A618: glitch kinds above 7 (possible when the chance is high) fall to the noise
 *    pixel case, like kinds 1-3; case 6 offsets the copy horizontally by half the height.
 *  - dmgdisp_27AB40: row k of a line of n rows gets kind 0xFF - |0x1FE*(k+1)/(n+1) - 0xFF|
 *    (a triangle: weakest at the edges of the line).
 *  - dmgdisp_27A500 copies forward byte by byte (overlapping rectangles smear).
 */
#include "g7_c.h"
#include "g5_b.h"     /* FfView, FfCamera, ffpipe_29EDB4, FfCameraSetup */

#define NOLOOPCALL __attribute__((optimize("no-tree-loop-distribute-patterns")))

static uchar player_damage(long kind)
{
    return damage_27DF14(game_player_entity, kind);
}

uchar dmgdisp_27A0D0(void)
{
    uchar d = player_damage(6);

    if (d >= 10)
        return 0;
    return RndRange(&game_rnd_35B704, 0, 100) < 10 - d;
}

long dmgdisp_27A118(long view)
{
    uchar a = 0, b = 0, d;

    if (hudview_26CAC8(view))
        return 0;
    if (hudview_26CAD8(view)) {
        d = player_damage(6);
        if (d < 10 && RndRange(&game_rnd_35B704, 0, 100) < 10 - d)
            return 1;
        return 0;
    }
    if (hudview_26CAAC(view)) {
        d = player_damage(7);
        if (d < 0x3C)
            a = RndRange(&game_rnd_35B704, 0, 200) < 0x3C - d;
        else if (d < 0x50)
            b = RndRange(&game_rnd_35B704, 0, 200) < 0x50 - d;
    } else {
        d = player_damage(6);
        if (d < 0x3C)
            a = RndRange(&game_rnd_35B704, 0, 200) < 0x3C - d;
        if (d < 0x50 && !a)
            b = RndRange(&game_rnd_35B704, 0, d < 0x3C ? 0x46 : 200) < 0x50 - d;
    }
    if (a)
        return 2;
    if (b)
        return 1;
    return 0;
}

void dmgdisp_27A2BC(long view, short *dx, short *dy)
{
    uchar d;
    long s;

    *dy = 0;
    *dx = *dy;
    if (hudview_26CAAC(view) || hudview_26CAC8(view) || hudview_26CAD8(view))
        return;
    d = player_damage(6);
    if (d > 0x28)
        return;
    if (d != dmg_shake_lvl[view]) {
        s = ((0x28 - d) << 13) / 0x28;
        dmg_shake_x[view] = (short)RndRange(&game_rnd_35B704, -s, s);
        dmg_shake_y[view] = (short)RndRange(&game_rnd_35B704, (-s) / 2, s / 2);
        dmg_shake_lvl[view] = d;
    }
    *dx = dmg_shake_x[view];
    *dy = dmg_shake_y[view];
    if (hud_shake) {
        *dx += (short)RndRange(&game_rnd_35B704, -hud_shake, hud_shake);
        *dy += (short)RndRange(&game_rnd_35B704, -hud_shake, hud_shake);
    }
}

static void cam_setup(uchar *cam)
{
    uchar *bm = *(uchar **)(cam + 0x1C);

    FfCameraSetup(cam, *(short *)(bm + 8), *(short *)(bm + 0xA), 0x10000);
}

void dmgdisp_27A3FC(void *view, uchar *cam)
{
    uchar st = cam[0x2C];
    ushort b0, b1;

    if (st == 4) {
        ffpipe_29EDB4((FfView *)view, (FfCamera *)cam);
        return;
    }
    cam[0x2C] = 4;
    b0 = *(ushort *)(cam + 0x2E) & 1;
    b1 = (*(ushort *)(cam + 0x2E) >> 1) & 1;
    cam[0x2E] = (cam[0x2E] & 0xFC) | 3;
    cam_setup(cam);
    ffpipe_29EDB4((FfView *)view, (FfCamera *)cam);
    cam[0x2E] &= 0xFE;
    cam[0x2C] = st;
    *(ushort *)(cam + 0x2E) |= b0;
    cam[0x2E] &= 0xFD;
    *(ushort *)(cam + 0x2E) |= b1 << 1;
    cam_setup(cam);
}

NOLOOPCALL uchar *dmgdisp_27A4E8(uchar *d, uchar *s, long n)
{
    while (n-- != 0)
        *d++ = *s++;
    return d;
}

NOLOOPCALL void dmgdisp_27A500(long x0, long y0, long x1, long y1, long dx, long dy)
{
    grs_bitmap bm;
    short wv, hv;
    uchar *src, *dst;
    long n;

    gr_init_sub_bitmap(&grd_canvas->bm, &bm, (short)x0, (short)y0, (short)(x1 - x0 + 1),
                       (short)(y1 - y0 + 1));
    wv = bm.w;
    hv = bm.h;
    if (dx + bm.w >= grd_canvas->gc.clip.i.right)
        wv = grd_canvas->gc.clip.i.right - dx - 1;
    if (hv + dy >= grd_canvas->gc.clip.i.bot)
        hv = grd_canvas->gc.clip.i.bot - dy - 1;
    src = bm.bits;
    dst = grd_canvas->bm.bits + grd_canvas->bm.row * dy + dx;
    while (hv-- != 0) {
        uchar *s = src, *d = dst;

        for (n = wv; n-- != 0;)
            *d++ = *s++;
        src += bm.row;
        dst += grd_canvas->bm.row;
    }
}

void dmgdisp_27A618(void)
{
    uchar c = dmg_noise_chance[player_damage(6) >> 3];
    long k, w, h, s, i, r;

    if (RndRange(&game_rnd_35B704, 0, 100) >= c)
        return;
    k = RndRange(&game_rnd_35B704, 0, c + 9) * 8 / 0x23;
    h = grd_canvas->bm.h;
    w = grd_canvas->bm.w;
    s = RndRange(&snd_rnd, 0, 0x7FFF) % 6 + 0x7EE;
    switch ((ulong)k <= 7 ? k : 1) {
    case 0:
        r = RndRange(&game_rnd_35B704, 0, h - 8);
        dmgdisp_27A500(0, r, w - 1, r + 7, 0, r + 1);
        break;
    case 1:
    case 2:
    case 3:
        for (i = 0; i < 100; i++) {
            void (**tab)() = grd_pixel_table;
            short y = (short)RndRange(&game_rnd_35B704, 0, h - 1);
            short x = (short)RndRange(&game_rnd_35B704, 0, w - 1);
            long col = RndRange(&game_rnd_35B704, 0, 0xFF);

            WATCOM_CALL(tab[1], col, x, y);
        }
        break;
    case 4: {
        uchar *p = grd_canvas->bm.bits;

        for (i = 0; i < h; i++) {
            if (i & 1)
                memmove(p, p + 2, w - 2);
            else
                memmove(p + 2, p, w - 2);
            p += grd_canvas->bm.row;
        }
        break;
    }
    case 5:
        if (RndRange(&game_rnd_35B704, 0, 1))
            dmgdisp_27A500(0, 0, w / 4, h - 1, 3 * w / 4, 0);
        else
            dmgdisp_27A500(3 * w / 4, 0, w - 1, h - 1, 0, 0);
        break;
    case 6:
        r = RndRange(&game_rnd_35B704, 0, 1);
        dmgdisp_27A500(0, r * h / 2, w - 1, (r + 1) * h / 2, (1 - r) * h / 2, 0);
        break;
    case 7:
        dmgdisp_27A500(0, 0, w / 4, h - 1, 1, 0);
        dmgdisp_27A500(3 * w / 4, 0, w - 1, h - 1, 3 * w / 4 + 1, 0);
        break;
    }
    SndPlay((ushort)s, 0, 0x40, 1, -1, 0x40, 0);
}

void dmgdisp_27A8C0(long lo, long hi)
{
    long i;

    for (i = 0; i < 0x100; i += 2) {
        dmg_noise[i] = (uchar)RndRange(&dmg_rnd, lo, hi);
        dmg_noise[i + 1] = (uchar)RndRange(&dmg_rnd, lo, hi);
    }
}

void dmgdisp_27A908(void)
{
    dmg_noise_pos = RndRange(&dmg_rnd, 0, 0xFF);
}

NOLOOPCALL void dmgdisp_27A924(uchar *d, long n, uchar add)
{
    long pos = dmg_noise_pos, e;

    do {
        e = 0x100 - pos;
        if (e > n)
            e = n;
        n -= e;
        e += pos;
        while (pos < e)
            *d++ = add + dmg_noise[pos++];
        pos = 0;
    } while (n);
    dmg_noise_pos = 0;
}

void dmgdisp_27A970(uchar *d, long n, uchar add, long kind)
{
    long k, a, b, c, e, hi1, lim, tot, r;

    dmg_noise_pos = RndRange(&dmg_rnd, 0, 0xFF);
    k = kind / 32;
    a = dmg_run_a[k] * 4;
    b = dmg_run_b[k] * 4;
    c = dmg_run_c[k] * 4;
    e = (dmg_run_d[k] * 4 - c) * 2 + c;
    hi1 = a + (b - a) * 3 / 2;
    lim = e - c;
    tot = hi1 + e - a - c;
    if (RndRange(&dmg_rnd, 0, tot) < lim)
        goto gap;
    for (;;) {
        if (!n)
            return;
        r = RndRange(&dmg_rnd, a, hi1);
        if (r > n)
            r = n;
        dmgdisp_27A924(d, r, add);
        n -= r;
        d += r;
        if (!n)
            return;
gap:
        r = RndRange(&dmg_rnd, c, e);
        if (r > n)
            r = n;
        n -= r;
        d += r;
    }
}

void dmgdisp_27AA80(void)
{
    long i;

    for (i = 0; i < 8; i++)
        dmg_lines[i].n = 0;
}

void dmgdisp_27AAC0(long amount)
{
    long l, i;

    if (!amount)
        return;
    l = amount / 50;
    if (l > 3)
        l = 3;
    for (i = 0; i < 8; i++)
        if (!dmg_lines[i].n) {
            dmg_lines[i].n = (ushort)(l * 2 + 3);
            dmg_lines[i].y = (ushort)RndRange(&dmg_rnd, 0, 100);
            dmg_lines[i].base = 0x41;
            return;
        }
}

void dmgdisp_27AB40(void)
{
    long i, k, kind, y;
    DmgLine *l;

    for (i = 0; i < 8; i++)
        if (dmg_lines[i].n)
            break;
    if (i == 8)
        return;
    for (i = 0; i < 0x100; i += 2) {
        dmg_noise[i] = (uchar)RndRange(&dmg_rnd, 0x41, 0x48);
        dmg_noise[i + 1] = (uchar)RndRange(&dmg_rnd, 0x41, 0x48);
    }
    for (i = 0; i < 8; i++) {
        l = &dmg_lines[i];
        if (!l->n)
            continue;
        for (k = 0; k < l->n; k++) {
            kind = 0x1FE * (k + 1) / (l->n + 1) - 0xFF;
            if (kind < 0)
                kind = -kind;
            kind = 0xFF - kind;
            y = l->y + k;
            if (grd_canvas->bm.h > y)
                dmgdisp_27A970(grd_canvas->bm.bits + y * grd_canvas->bm.row, grd_canvas->bm.w,
                               (uchar)(l->base - 0x41), kind);
        }
        if (l->n > 1)
            l->n--;
        else if (RndRange(&dmg_rnd, 0, 3) == 0)
            l->n = 0;
    }
}
