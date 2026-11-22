/* rndsky.c -- Terra Nova module M082 (0x2A63C0-0x2A8872; r:\prj\tn\v109\tn\src\tn\rndsky.c):
 * the sky.  Sky colour, star field and sky objects (sun, moons) drawn at their heading /
 * pitch; the cloud layer mapped row by row from the view (perspective per row, affine along
 * the row; an optional warp) with distance shading; the water reflection.  Decompiled from
 * __FF.EXE v1.09; the map's names are kept.
 *
 *   0x2A63C0 grey table          0x2A6478 pixel       0x2A6564 disc       0x2A677C shimmer
 *   0x2A695C build the discs     0x2A6AF8 / 0x2A6B44 disc of a size       0x2A6B80 colour table
 *   0x2A6BC4 set a colour table  0x2A6BD0 force the sky colour (not in the map; no callers)
 *   0x2A6BE0 sky colour, stars, sun and objects                           0x2A6DEC cloud scroll
 *   0x2A6EB8 warped span (no callers)   0x2A6F38 warp tables   0x2A7078 cloud rows
 *   0x2A7654 cloud span   0x2A79A8 cloud spans   0x2A7A6C draw the sky
 *   0x2A7AEC / 0x2A7BA4 lock / unlock a sky record   0x2A7C38 lock a bitmap (no callers)
 *   0x2A7C4C scaled column   0x2A7CA0 bitmap   0x2A7E28 scaled bitmap   0x2A80FC sky object
 *   0x2A82D0 sky colour fill   0x2A8344 init   0x2A83BC tables   0x2A844C shutdown
 *   0x2A84A4 reflection set-up   0x2A8658 reflection
 *
 * The inner loops of the cloud spans and of the reflection are the asm in skyasm.c (M093).
 *
 * Quirks kept:
 *  - rndsky_2A6564 / rndsky_2A677C: a line that is left or right of the clip rectangle does
 *    not advance the line pointer (the next lines are drawn one row too high).
 *  - rndsky_2A8658 runs the reflection asm for (bottom - row) pixels but advances its own
 *    pointer by (min(column height, bottom) - row) rows.
 *  - rndsky_2A7654 masks u and v to 24 bits only at the start of a span (and of its second
 *    half).
 */
#include "g5_b.h"
#include "g1_b.h"
#include "g1_c.h"
#include "g5_a.h"
#include "g5_c.h"
#include "3d_a.h"
#ifndef TN_WATCOM_ABI
#include <math.h>
#endif

static fix muldiv(fix a, fix b, fix c) { return (fix)(((long long)a * b) / c); }

/* 0x2A63C0: the grey table: 0x2C - brightness / 16 of each palette colour */
void rndsky_2A63C0(void)
{
    uchar pal[0x300];
    long i;
    gr_get_pal(0, 0x100, pal);
    for (i = 0; i < 0x100; i++) {
        long avg = (pal[i * 3] + pal[i * 3 + 1] + pal[i * 3 + 2]) / 3;
        sky_grey[i] = (uchar)(0x2C - (avg << 4) / 256);
    }
}

/* 0x2A6478: one pixel, through the colour's translucency table */
#ifndef TN_WATCOM_ABI
/* the sharp view (port/video/sharp3d.c): the pixels and discs of the particles are k times bigger
 * there, so they cover what they cover in the game's view */
static int s3d_inner;

/* "True colour" (port/video/truecol.c): the smoke's see-through discs over the sky in full colour.
 * The game mixes a disc with what is behind it through its colour's translucency table (the
 * nearest of the 256 colours to the mix), so over the full-colour sky it showed the mix with the
 * game's own sky colour, a greenish or bluish grey beside the smooth sky.  A sky pixel (noted
 * PORT_TC_SKY or PORT_TC_FILL) that a disc goes over is noted with the table and how many
 * discs (bits 8..15 and 16..19), and the pass mixes the full-colour sky as the table does
 * (gr_init_translucency_table's colour model, from the table's opacity, purity and colour).  A
 * pixel with discs of two tables, a lit disc (the canvas's light table) or more than 15 discs
 * keeps the game's colour, as before. */
static fix sky_tparm[0x100][3];         /* rndsky_2A6B80's opacity, purity, colour; */
static uchar sky_tparm_ok[0x100];       /* ... known (not a table set from outside) */

static void tc_smoke_note(const uchar *p, long id)
{
    ulong k = (ulong)(p - port_tc_base), v, n, kind;
    if (!port_tc_on || k >= port_tc_n)
        return;
    v = port_tc_info[k];
    kind = v >> 24;
    n = (v >> 16) & 0xF;
    if ((kind != PORT_TC_SKY && kind != PORT_TC_FILL) || port_tc_smoke_px(v) != *p)
        return;                         /* drawn over since: the game's colour */
    if (sky_tparm_ok[id & 0xFF] && n < 15 && (!n || ((v >> 8) & 0xFF) == (ulong)(id & 0xFF)))
        port_tc_info[k] = (v & 0xFF0000FFu) | (n + 1) << 16 | (ulong)(id & 0xFF) << 8;
    else
        port_tc_info[k] = v & 0xFF0000FFu;  /* (no longer the same colour: the game's) */
}

/* the colour the picture has at a noted sky pixel: the one drawn, through its discs' table */
ulong port_tc_smoke_px(ulong v)
{
    ulong n = (v >> 16) & 0xF, c = v & 0xFF;
    const uchar *tab = sky_trans[(v >> 8) & 0xFF];
    if (!n || !tab)
        return c;
    while (n--)
        c = tab[c];
    return c;
}

/* the full-colour sky rgb (0xRRGGBB) behind a noted pixel's discs, mixed as their table does */
ulong port_tc_smoke_rgb(ulong v, ulong rgb)
{
    ulong n = (v >> 16) & 0xF, id = (v >> 8) & 0xFF;
    float op, filter, clarity, c[3], f[3], o[3];
    int i;
    if (!n || !sky_tparm_ok[id])
        return rgb;
    op = sky_tparm[id][0] / 65536.0f;
    filter = (1.0f - op) * (sky_tparm[id][1] / 65536.0f);
    clarity = 1.0f - op - filter;
    c[0] = (float)((sky_tparm[id][2] & 0x3FF) >> 2);            /* glomped 10-bit components */
    c[1] = (float)(((sky_tparm[id][2] >> 11) & 0x3FF) >> 2);
    c[2] = (float)((((ulong)sky_tparm[id][2] >> 22) & 0x3FF) >> 2);
    o[0] = (float)((rgb >> 16) & 0xFF);
    o[1] = (float)((rgb >> 8) & 0xFF);
    o[2] = (float)(rgb & 0xFF);
    for (i = 0; i < 3; i++)
        f[i] = clarity + filter * c[i] / 256.0f;
    while (n--)
        for (i = 0; i < 3; i++)
            o[i] = o[i] * f[i] + c[i] * op;
    for (i = 0; i < 3; i++)
        o[i] = o[i] < 0 ? 0 : o[i] > 255 ? 255 : o[i] + 0.5f;
    return (ulong)o[0] << 16 | (ulong)o[1] << 8 | (ulong)o[2];
}

static long s3d_isqrt(long long v)
{
    long long r = 0, b = 1LL << 40;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return (long)r;
}

/* row j (0..h-1) of a w x h ellipse: first column and length */
static void s3d_ellipse_row(long w, long h, long j, long *start, long *len)
{
    long long dy = 2 * j + 1 - h;           /* in units of 1/h */
    long long q = (long long)h * h - dy * dy;
    long half = q > 0 ? s3d_isqrt((long long)w * w * q) / (2 * h) : 0;
    if (half < 1)
        half = 1;
    *start = w / 2 - half;
    *len = 2 * half;
}

/* rndsky_2A6564 at k times the size: a w x h ellipse centred on x, y */
static void s3d_disc(long x, long y, long w, long h, long color)
{
    grs_canvas *cv = grd_canvas;
    uchar *tab = sky_trans[color];
    uchar *ltab = cv->gc.fill_type ? (uchar *)cv->gc.fill_parm : 0;
    long l = cv->gc.clip.f.left >> 16, r = cv->gc.clip.f.right >> 16,
         t = cv->gc.clip.f.top >> 16, b = cv->gc.clip.f.bot >> 16, j;
    int note = port_tc_on && tab && !ltab;  /* "True colour": the smoke over the sky */
    if (ltab && !tab) {
        tab = ltab;
        ltab = 0;
    } else if (ltab) {
        if (!sky_grey[5])
            rndsky_2A63C0();
        tab = sky_grey;
        ltab = planet_tab3e;
    }
    x -= w / 2;
    y -= h / 2;
    for (j = 0; j < h; j++) {
        long start, len, a, e;
        uchar *p;
        if (y + j < t || y + j >= b)
            continue;
        s3d_ellipse_row(w, h, j, &start, &len);
        a = x + start;
        e = a + len;
        if (a < l)
            a = l;
        if (e > r)
            e = r;
        p = cv->bm.bits + (y + j) * cv->bm.row;
        for (; a < e; a++) {
            if (note)
                tc_smoke_note(p + a, color);
            p[a] = ltab ? ltab[tab[p[a]]] : tab ? tab[p[a]] : (uchar)color;
        }
    }
}

/* the map's blast disc on the sharp map (port/video/mapsharp.c) */
void port_sky_disc(long x, long y, long w, long h, long color)
{
    s3d_disc(x, y, w, h, color);
}

/* rndsky_2A677C at k times the size */
static void s3d_shimmer(long x, long y, long w, long h, long k)
{
    grs_canvas *cv = grd_canvas;
    long m = k % 5, dx = sky_dx5[m] * port_s3d_kx, dy = sky_dy5[m] * port_s3d_ky;
    long l = cv->gc.clip.f.left >> 16, r = cv->gc.clip.f.right >> 16,
         t = cv->gc.clip.f.top >> 16, b = cv->gc.clip.f.bot >> 16, j;
    x -= w / 2;
    y -= h / 2;
    for (j = 0; j < h; j++) {
        long start, len, a, e, yy = y + j;
        uchar *p;
        if (yy < t || yy >= b || yy + dy < t || yy + dy >= b)
            continue;
        s3d_ellipse_row(w, h, j, &start, &len);
        a = x + start;
        e = a + len;
        if (a < l || a + dx < l)
            a = dx < 0 ? l - dx : l;
        if (e > r || e + dx > r)
            e = dx > 0 ? r - dx : r;
        if (a >= e)
            continue;
        p = cv->bm.bits + yy * cv->bm.row;
        memmove(p + a, p + a + dy * cv->bm.row + dx, e - a);
    }
}
#endif

void rndsky_2A6478(long x, long y, long color)
{
    grs_canvas *cv = grd_canvas;
    uchar *tab = sky_trans[color];
    uchar *p = cv->bm.bits + cv->bm.row * y + x;
    uchar *ltab = cv->gc.fill_type ? (uchar *)cv->gc.fill_parm : 0;
    uchar *lt2 = ltab;
    long idx;
#ifndef TN_WATCOM_ABI
    if (port_s3d_on && !s3d_inner) {    /* one pixel of the game's view */
        long i, j, x0 = x - port_s3d_kx / 2, y0 = y - port_s3d_ky / 2;
        s3d_inner = 1;
        for (j = y0; j < y0 + port_s3d_ky; j++)
            for (i = x0; i < x0 + port_s3d_kx; i++)
                rndsky_2A6478(i, j, color);
        s3d_inner = 0;
        return;
    }
#endif
    cv = grd_canvas;
    if (x < cv->gc.clip.f.left >> 16 || x >= cv->gc.clip.f.right >> 16 ||
        y < cv->gc.clip.f.top >> 16 || y >= cv->gc.clip.f.bot >> 16)
        return;
    if (tab && ltab) {
        if (!sky_grey[5])
            rndsky_2A63C0();
        lt2 = planet_tab3e;
        tab = sky_grey;
    }
    if (grd_canvas->gc.fill_type) {
        idx = tab ? tab[*p] : color;
        *p = lt2[idx];
    } else {
#ifndef TN_WATCOM_ABI
        if (s3d_inner && tab && port_tc_on)     /* "True colour": the smoke over the sky */
            tc_smoke_note(p, color);
#endif
        *p = tab ? tab[*p] : (uchar)color;
    }
}

/* 0x2A6564: a run-length disc (header dx, dy, lines; per line start, length) centred on x, y;
 * lines are doubled on canvases taller than 200 */
void rndsky_2A6564(long x, long y, uchar *data, long color)
{
    uchar *tab = sky_trans[color];
    uchar *ltab = grd_canvas->gc.fill_type ? (uchar *)grd_canvas->gc.fill_parm : 0;
    long dbl = grd_canvas->bm.h > 200;
    long n, row, start, len, l, r;
    uchar *line, *p;
    x -= (schar)data[0];
    y -= (schar)data[1] * (dbl + 1);
    row = grd_canvas->bm.row;
    line = grd_canvas->bm.bits + row * y + x;
    n = (dbl + 1) * data[2];
    data += 3;
    if (y >= grd_canvas->gc.clip.f.bot >> 16)
        return;
    if (y + n < grd_canvas->gc.clip.f.top >> 16)
        return;
    y--;
    if (ltab && !tab) {
        tab = ltab;
        ltab = 0;
    } else if (ltab) {
        if (!sky_grey[5])
            rndsky_2A63C0();
        tab = sky_grey;
        ltab = planet_tab3e;
    }
    while (n-- != 0) {
        grs_canvas *cv;
        start = data[0];
        len = data[1];
        if (!(n & dbl))
            data += 2;
        cv = grd_canvas;
        y++;
        if (y < cv->gc.clip.f.top >> 16 || y >= cv->gc.clip.f.bot >> 16) {
            line += row;
            continue;
        }
        l = cv->gc.clip.f.left >> 16;
        if (x + start + len < l)
            continue;                       /* quirk: line not advanced */
        r = cv->gc.clip.f.right >> 16;
        if (x + start >= r)
            continue;                       /* quirk */
        if (r <= x + start + len)
            len = r - x - start - 1;
        if (x + start < grd_canvas->gc.clip.f.left >> 16) {
            long s2 = (grd_canvas->gc.clip.f.left >> 16) - x;
            len -= s2 - start;
            start = s2;
        }
        p = line + start;
        if (ltab) {
            for (; len != 0; len--, p++)
                *p = ltab[tab[*p]];
        } else if (tab) {
            for (; len != 0; len--, p++)
                *p = tab[*p];
        } else
            memset(p, color, len);
        line += row;
    }
}

/* 0x2A677C: shimmer: each line of the disc is copied from k % 5 of the offsets */
void rndsky_2A677C(long x, long y, uchar *data, long k)
{
    grs_canvas *cv = grd_canvas;
    long row, n, m, off, bot, top, l, r, lx, rx;
    uchar *line;
    x -= (schar)data[0];
    y -= (schar)data[1];
    row = cv->bm.row;
    line = cv->bm.bits + row * y + x;
    n = data[2];
    m = k % 5;
    off = sky_dy5[m] * row + sky_dx5[m];
    data += 3;
    bot = cv->gc.clip.f.bot >> 16;
    if (bot <= sky_dy5[m] + y)
        return;
    top = cv->gc.clip.f.top >> 16;
    if (n + y < top)
        return;
    if (n + y > bot)
        n = bot - y;
    if (bot < sky_dy5[m] + n + y)
        n = bot - y - sky_dy5[m];
    l = grd_canvas->gc.clip.f.left >> 16;
    y--;
    if (sky_dx5[m] < 0)
        l -= sky_dx5[m];
    r = grd_canvas->gc.clip.f.right >> 16;
    if (sky_dx5[m] > 0)
        r -= sky_dx5[m];
    lx = l - x;
    rx = r - x;
    while (n-- != 0) {
        long start, len;
        cv = grd_canvas;
        data += 2;
        y++;
        start = data[-2];
        len = data[-1];
        if (y < cv->gc.clip.f.top >> 16 || y >= cv->gc.clip.f.bot >> 16) {
            line += row;
            continue;
        }
        if (x + start + len < l)
            continue;                       /* quirk: line not advanced */
        if (x + start >= r)
            continue;
        if (x + start + len >= r)
            len = rx - start - 1;
        if (x + start < l) {
            len -= lx - start;
            start = lx;
        }
        memmove(line + start, line + start + off, len);
        line += row;
    }
}

/* 0x2A695C: run-length discs of radius 1..64 (sky_disc[r]) */
void rndsky_2A695C(void)
{
    long pos = 0, r;
    sky_disc_ok = 1;
    sky_disc[0] = sky_disc_data;
    for (r = 1; r <= 0x40; r++) {
        sky_disc_data[pos] = (uchar)(r / 2);
        sky_disc_data[pos + 1] = (uchar)(r / 2);
        sky_disc_data[pos + 2] = (uchar)r;
        sky_disc[r] = sky_disc_data + pos;
        pos += 3;
        if (r > 0) {
            fix lim = (r << 16) / 2 - 0x10000;
            fix c = ((r - 1) << 16) / 2;
            fix rr = ((r - 1) * (r - 1) << 16) / 4;
            fix end = c - (r << 16);
            fix e = c;
            do {
                fix a = c - fix_sqrt(rr - fix_mul(e, e));
                uchar w;
                if ((a * 2) >> 16 >= r)
                    a = lim;
                if (a < 0)
                    a = 0;
                sky_disc_data[pos] = (uchar)(a >> 16);
                w = (uchar)(r - ((a + a) >> 16));
                sky_disc_data[pos + 1] = w;
                if (w == 0)
                    sky_disc_data[pos + 1] = 1;
                pos += 2;
                e -= 0x10000;
            } while (e > end);
        }
    }
}

/* 0x2A6AF8: a translucent disc of a size (a pixel below 1/2) */
void rndsky_2A6AF8(long x, long y, fix size, long color)
{
    long r;
    size += 0x8000;
    if (size < 0x10000) {
        rndsky_2A6478(x, y, color);
        return;
    }
    r = size / 0x8000;
    if ((ulong)r >= 0x41)
        r = 0x40;
#ifndef TN_WATCOM_ABI
    if (port_s3d_on) {
        s3d_disc(x, y, r * port_s3d_kx, r * port_s3d_ky * (port_s3d_low_h > 200 ? 2 : 1), color);
        return;
    }
#endif
    rndsky_2A6564(x, y, sky_disc[r], color);
}

/* 0x2A6B44: a shimmering disc of a size (nothing below 1) */
void rndsky_2A6B44(long x, long y, fix size, long k)
{
    long r;
    if (size < 0x10000)
        return;
    r = size / 0x8000;
    if ((ulong)r >= 0x41)
        r = 0x40;
#ifndef TN_WATCOM_ABI
    if (port_s3d_on) {
        s3d_shimmer(x, y, r * port_s3d_kx, r * port_s3d_ky, k);
        return;
    }
#endif
    rndsky_2A677C(x, y, sky_disc[r], k);
}

/* 0x2A6B80: the translucency table of a colour */
void rndsky_2A6B80(long id, fix opacity, fix purity, long color)
{
    if (!sky_disc_ok)
        rndsky_2A695C();
    if (!sky_trans[id])
        sky_trans[id] = gr_malloc(0x100);
    gr_init_translucency_table(sky_trans[id], opacity, purity, color);
#ifndef TN_WATCOM_ABI
    sky_tparm[id & 0xFF][0] = opacity;
    sky_tparm[id & 0xFF][1] = purity;
    sky_tparm[id & 0xFF][2] = color;
    sky_tparm_ok[id & 0xFF] = 1;
#endif
}

/* 0x2A6BC4 */
void rndsky_2A6BC4(int id, uchar *tab)
{
    sky_trans[id] = tab;
#ifndef TN_WATCOM_ABI
    sky_tparm_ok[id & 0xFF] = 0;
#endif
}

/* 0x2A6BD0 (not in the map, no callers) */
void rndsky_2A6BD0(long on)
{
    sky_force = on == 0;
}

/* 0x2A6BE0: sky colour or stars, then the sun and the sky objects in front of the view */
void rndsky_2A6BE0(void)
{
    fix s, t;
    short d;
    long i;
    SkyObj *o;
    if (FF_CAM_NOSKY(&ff_cam))
        sky_cur = &sky_none;
    else if (game_35CADC) {
        sky_cur = &sky_flat;
        if (game_35CADC < 0x10 || grd_canvas->gc.fill_type)
            sky_cur->color = 0;
        else
            sky_flat.color = 0x20;
    } else
        sky_cur = ff_cam.sky;
#ifndef TN_WATCOM_ABI
    {   /* the sharp view: the sizes of the game's view (rndsky_2A80FC makes them k times) */
        long cw = port_s3d_on ? port_s3d_low_w : ff_cam.canvas->bm.w;
        d = (short)(cw - sky_ref_w);
        if ((d < 0 ? -d : d) < 0x20)
            s = 0x10000;
        else
            s = fix_div(cw << 16, sky_ref_w << 16);
    }
#else
    d = (short)(ff_cam.canvas->bm.w - sky_ref_w);
    if ((d < 0 ? -d : d) < 0x20)
        s = 0x10000;
    else
        s = fix_div(ff_cam.canvas->bm.w << 16, sky_ref_w << 16);
#endif
    t = s;
    if ((ff_cam.flags & 1) && !(ff_cam.flags & 2))
        t += t;
    if (ff_cam.zoom > 0x10000) {
        s = fix_mul(s, ff_cam.zoom);
        t = fix_mul(t, ff_cam.zoom);
    }
    sky_row = grd_canvas->bm.row;
    if (sky_cur->stars) {
        rndsky_2A82D0(0);
        star_reset_bounds();
        g3_star_render();
    } else if (!sky_cur->cloud || sky_force)
        rndsky_2A82D0(sky_cur->color);
    if (sky_cur->sun) {
        ushort *sun = *(ushort **)(game_map_35BFB4 + 0x3A);
        ushort a = sun[0] - ff_view.heading;
        if (a < 0x3000 || a > 0xD000)
            rndsky_2A80FC(sky_cur->sun, a, (ushort)(-sun[1] - ff_view.pitch), 0, s, t);
    }
    for (i = 0, o = sky_cur->obj; i < sky_cur->nobj; i++, o++) {
        ushort a = o->heading - ff_view.heading;
        if (a < 0x3000 || a > 0xD000)
            rndsky_2A80FC(o->bm, a, (ushort)(-o->pitch - ff_view.pitch), 1, s, t);
    }
}

/* 0x2A6DEC: scroll the clouds with the wind; advance the warp phases */
void rndsky_2A6DEC(void)
{
    fix c, s;
    long dt = game_time - sky_time, sp;
    sky_time = game_time;
    fix_sincos(game_442441, &s, &c);
    sp = game_442443 * dt;
    sky_wind_dx = fix_mul(c, sp);
    sky_wind_dy = fix_mul(s, sp);
    sky_cloud_x += sky_wind_dx;
    sky_cloud_y += sky_wind_dy;
    ((uchar *)&sky_cloud_x)[3] = 0;
    ((uchar *)&sky_cloud_y)[3] = 0;
    sky_phase1 += dt * 993;
    sky_phase2 += dt * 193;
}

/* 0x2A6EB8: one warped span (no callers; the asm 0x2B2810 does the same) */
void rndsky_2A6EB8(uchar *dst, long n, ulong u, ulong v, long du, long dv)
{
    while (n-- != 0) {
        uchar bu = (uchar)((long)u >> 16), bv = (uchar)((long)v >> 16);
        uchar hi = bv + sky_warp_b[bu], lo = bu + sky_warp_a[bv];
        *dst++ = sky_tex[(hi << 8) | lo];
        v += dv;
        u += du;
    }
}

/* 0x2A6F38: the warp tables: two layers of sine waves moving with the time */
void rndsky_2A6F38(void)
{
    long t = game_time, o, i;
    memset(sky_warp_a, 0, 0x200);
    for (o = 0; o < 2; o++) {
        long *p = sky_warp[o];
        long a = p[0] >> 8, b = p[1] / 256, c = p[2] / 256, acc = 0;
        fix e1 = fix_mul(p[3] << 6, t), e2 = fix_mul(p[4] << 6, t);
        for (i = 0; i < 0x100; i++) {
            sky_warp_a[i] += (b * sky_sin8[((acc + e1) >> 6) & 0x3FF]) >> 16;
            sky_warp_b[i] += (sky_sin8[((acc + e2) >> 6) & 0x3FF] * c) >> 16;
            acc += a;
        }
    }
}

#ifndef TN_WATCOM_ABI
/* The sharp view's clouds without steps.  The game shades each row of clouds by its distance in
 * steps (sky_dist: the clouds' own colours, then 7 tables ever closer to the haze, then 3 plain
 * haze colours); at the sharp view's sizes the rows where the table changes show as lines across
 * the sky.  There each row keeps where it is between two steps (sky_band: step * 16 + 0..15,
 * the change centred on the game's), and its pixels take one step's colour or the next's by a
 * 4x4 dither (sky_span_dither). */
static short sky_band[2048];           /* sharp3d.c MAX_ROWS; -1: the game's row */
static short sky_bandq[2048];          /* ... in 256ths of a step (True colour) */
static long sky_tdu[2048], sky_tdv[2048];      /* its steps, also where the game's are 0 */
static long sky_fade;
static long sky_rows_n;                /* the rows rndsky_2A7078 made */
static uchar sky_tc_used[256];         /* True colour: the texel values of the cloud texture */
static const uchar *sky_tc_tex;
static const uchar sky_bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };

static void sky_span_dither(uchar *dst, long row, long x, long len)
{
    long b = sky_band[row], n = b >> 4, f = b & 15, i, du = sky_tdu[row], dv = sky_tdv[row];
    ulong u = sky_u[row] + du * x, v = sky_v[row] + dv * x;
    const uchar *by = sky_bayer[row & 3], *ta, *tb;
    long ca, cb;
    /* step k: 0 the clouds' colours, 1..7 a shade table, 8..11 a haze colour */
    ta = n == 0 ? NULL : n < 8 ? planet_tab38 + (n << 8) : NULL;
    ca = n < 8 ? -1 : sky_fade + 3 - (n > 11 ? 3 : n - 8);
    n = f ? n + 1 : n;
    tb = n == 0 ? NULL : n < 8 ? planet_tab38 + (n << 8) : NULL;
    cb = n < 8 ? -1 : sky_fade + 3 - (n > 11 ? 3 : n - 8);
    for (i = 0; i < len; i++, x++) {
        int nb = by[x & 3] < f;
        long c = nb ? cb : ca;
        if (c < 0) {
            uchar t = sky_tex[((u >> 16) & 0xFF) | (((v >> 16) & 0xFF) << 8)];
            const uchar *tab = nb ? tb : ta;
            c = tab ? tab[t] : t;
        }
        dst[i] = (uchar)c;
        u += du;
        v += dv;
    }
    if (port_tc_on) {                   /* "True colour" (port/video/truecol.c) */
        ulong k = (ulong)(dst - port_tc_base);
        if (len > 0 && k < port_tc_n && k + len <= port_tc_n) {
            ulong *inf = port_tc_info + k;
            for (i = 0; i < len; i++)
                inf[i] = (ulong)PORT_TC_SKY << 24 | dst[i];
            port_tc_row[row] = 1;
        }
    }
}

static ulong tc_mix(ulong a, ulong b, long f)  /* a to b, f 0..256 */
{
    ulong rb = (((a & 0xFF00FF) * (ulong)(256 - f) + (b & 0xFF00FF) * (ulong)f) >> 8) & 0xFF00FF;
    ulong g = (((a & 0xFF00) * (ulong)(256 - f) + (b & 0xFF00) * (ulong)f) >> 8) & 0xFF00;
    return rb | g;
}

/* True colour's clouds: the texture's own colours softened (a blur of about a texel, so the
 * blocks of a few colours become soft shapes when the sharp view stretches them), and each
 * distance step as a mix toward a colour: step k = the clouds' colour mixed toward tc_h[k] by
 * tc_w[k], fitted to the game's shade tables (1..7: planet_tab38's rows, toward the first haze
 * colour), 8..11 the haze colours themselves.  Made again when the texture, the palette or the
 * tables change (rndsky_2A7078, before the pass) */
static ulong sky_tc_soft[0x10000];
static float sky_tc_tmp[3][0x10000];
static ulong sky_tc_key;
static float tc_w[13], tc_h[13][3];
static short sky_tc_fine[0x10000];     /* the finer layer: the soft clouds' light less its mean */

static void sky_tc_prep(void)
{
    static const float g[7] = { 0.0044f, 0.054f, 0.242f, 0.399f, 0.242f, 0.054f, 0.0044f };  /* sigma 1 */
    long cnt[256], t, k, i, x, y, c;
    ulong key = (ulong)sky_tex * 0x9E3779B1u ^ (ulong)sky_fade;
    for (t = 0; t < 256; t++)
        if (sky_tc_used[t])
            key = key * 31 + port_tc_pal[t];
    for (k = 1; k < 8; k++)
        for (t = 0; t < 256; t += 17)
            key = key * 7 + planet_tab38[(k << 8) + t];
    for (t = 0; t < 4; t++)
        key = key * 31 + port_tc_pal[(sky_fade + t) & 0xFF];
    if (key == sky_tc_key)
        return;
    sky_tc_key = key;
    memset(cnt, 0, sizeof cnt);
    for (i = 0; i < 0x10000; i++)
        cnt[sky_tex[i]]++;
    for (c = 0; c < 3; c++) {          /* across, then down (the texture wraps) */
        int sh = 16 - 8 * (int)c;
        for (y = 0; y < 256; y++)
            for (x = 0; x < 256; x++) {
                float v = 0;
                for (i = -3; i <= 3; i++)
                    v += g[i + 3] * (float)((port_tc_pal[sky_tex[(y << 8) | ((x + i) & 0xFF)]] >> sh) & 0xFF);
                sky_tc_tmp[c][(y << 8) | x] = v;
            }
    }
    for (y = 0; y < 256; y++)
        for (x = 0; x < 256; x++) {
            ulong o = 0;
            for (c = 0; c < 3; c++) {
                float v = 0;
                long q;
                for (i = -3; i <= 3; i++)
                    v += g[i + 3] * sky_tc_tmp[c][(((y + i) & 0xFF) << 8) | x];
                q = (long)(v + 0.5f);
                o |= (ulong)(q < 0 ? 0 : q > 255 ? 255 : q) << (16 - 8 * c);
            }
            sky_tc_soft[(y << 8) | x] = o;
        }
    {
        long sum = 0;
        for (i = 0; i < 0x10000; i++) {
            ulong o = sky_tc_soft[i];
            sum += (long)((o >> 16 & 0xFF) * 77 + (o >> 8 & 0xFF) * 150 + (o & 0xFF) * 29) >> 8;
        }
        sum >>= 16;
        for (i = 0; i < 0x10000; i++) {
            ulong o = sky_tc_soft[i];
            sky_tc_fine[i] = (short)((((long)((o >> 16 & 0xFF) * 77 + (o >> 8 & 0xFF) * 150 + (o & 0xFF) * 29) >> 8) - sum));
        }
    }
    /* the steps */
    tc_w[0] = 0;
    tc_h[0][0] = tc_h[0][1] = tc_h[0][2] = 0;
    for (k = 1; k <= 12; k++) {
        ulong hc = port_tc_pal[(sky_fade + 3 - (k < 8 ? 0 : k > 11 ? 3 : k - 8)) & 0xFF];
        float h[3] = { (float)(hc >> 16 & 0xFF), (float)(hc >> 8 & 0xFF), (float)(hc & 0xFF) }, num = 0, den = 0;
        for (c = 0; c < 3; c++)
            tc_h[k][c] = h[c];
        if (k >= 8) {
            tc_w[k] = 1;
            continue;
        }
        for (t = 0; t < 256; t++) {
            ulong a = port_tc_pal[t], b = port_tc_pal[planet_tab38[(k << 8) + t]];
            if (!cnt[t])
                continue;
            for (c = 0; c < 3; c++) {
                int sh = 16 - 8 * (int)c;
                float pa = (float)(a >> sh & 0xFF), pb = (float)(b >> sh & 0xFF);
                num += cnt[t] * (pb - pa) * (h[c] - pa);
                den += cnt[t] * (h[c] - pa) * (h[c] - pa);
            }
        }
        tc_w[k] = den > 0 ? num / den : k / 8.0f;
        tc_w[k] = tc_w[k] < 0 ? 0 : tc_w[k] > 1 ? 1 : tc_w[k];
    }
}

/* the soft clouds' colour at (u, v), toward the haze: the colour * (256 - W) + H, 256ths */
static ulong tc_cloud(ulong u, ulong v, long W, const long *H, long dm)
{
    ulong tu = (u >> 16) & 0xFF, tv = ((v >> 16) & 0xFF) << 8, tu1 = (tu + 1) & 0xFF, tv1 = (tv + 0x100) & 0xFF00;
    ulong u3 = u * 3 + 0x5A0000, v3 = v * 3 + 0x370000, t;
    long fu = (long)((u >> 8) & 0xFF), fv = (long)((v >> 8) & 0xFF), d, r, g, b;
    t = tc_mix(tc_mix(sky_tc_soft[tv | tu], sky_tc_soft[tv | tu1], fu),
               tc_mix(sky_tc_soft[tv1 | tu], sky_tc_soft[tv1 | tu1], fu), fv);
    /* the finer layer: the clouds' own light at three times the frequency */
    d = (sky_tc_fine[((v3 >> 8) & 0xFF00) | ((u3 >> 16) & 0xFF)] * dm) >> 8;
    r = (((long)(t >> 16 & 0xFF) * (256 - W) + H[0]) >> 8) + d;
    g = (((long)(t >> 8 & 0xFF) * (256 - W) + H[1]) >> 8) + d;
    b = (((long)(t & 0xFF) * (256 - W) + H[2]) >> 8) + d;
    return (ulong)(r < 0 ? 0 : r > 255 ? 255 : r) << 16 | (ulong)(g < 0 ? 0 : g > 255 ? 255 : g) << 8 |
           (ulong)(b < 0 ? 0 : b > 255 ? 255 : b);
}

/* a row's clouds as port_tc_sky_row works them out: kind 0 none (no sky row, or not this
 * sky's), 1 the haze (one colour c), 2 the clouds (tc_sky_px) */
typedef struct {
    int kind;
    ulong c, u0, v0;
    long W, H[3], du, dv, dm;
} TcSkyRow;

typedef struct {                        /* the two worked-out columns beside x */
    long e0;
    ulong c0, c1;
} TcSkyPair;

static void tc_sky_rowinfo(long row, TcSkyRow *r)
{
    long q, n, ch;
    float fr;
    r->kind = 0;
    if (row < 0 || row >= 2048 || sky_band[row] < 0 || !sky_tex || sky_tex != sky_tc_tex)
        return;
    q = sky_bandq[row];
    n = q >> 8;
    n = n > 11 ? 11 : n;
    fr = (q & 0xFF) / 256.0f;
    r->W = (long)(((1 - fr) * tc_w[n] + fr * tc_w[n + 1]) * 256 + 0.5f);
    for (ch = 0; ch < 3; ch++)          /* (toward the haze: w * h, mixed between the steps) */
        r->H[ch] = (long)(((1 - fr) * tc_w[n] * tc_h[n][ch] + fr * tc_w[n + 1] * tc_h[n + 1][ch]) * 256 + 0.5f);
    if (r->W >= 256) {                  /* the haze: one colour */
        r->kind = 1;
        r->c = (ulong)(r->H[0] >> 8) << 16 | (ulong)(r->H[1] >> 8) << 8 | (ulong)(r->H[2] >> 8);
        return;
    }
    r->kind = 2;
    r->du = sky_tdu[row];
    r->dv = sky_tdv[row];
    r->u0 = sky_u[row] - 0x8000;        /* the texels' middles */
    r->v0 = sky_v[row] - 0x8000;
    r->dm = ((256 - r->W) << 8) / 1280; /* the finer layer: a fifth (of what the haze leaves) */
}

/* the clouds at column x of a kind 2 row: worked out at every second column, the ones between
 * mixed from the two beside them (the clouds are soft: the same to the eye, half the work);
 * p->e0 -4 before a row's first */
static inline ulong tc_sky_px(const TcSkyRow *r, long x, TcSkyPair *p)
{
    if ((x & ~1L) != p->e0) {
        long xe = x & ~1L;
        p->c0 = xe == p->e0 + 2 ? p->c1 : tc_cloud(r->u0 + r->du * xe, r->v0 + r->dv * xe, r->W, r->H, r->dm);
        p->c1 = tc_cloud(r->u0 + r->du * (xe + 2), r->v0 + r->dv * (xe + 2), r->W, r->H, r->dm);
        p->e0 = xe;
    }
    return x & 1 ? (((p->c0 ^ p->c1) & 0xFEFEFE) >> 1) + (p->c0 & p->c1) : p->c0;
}

/* a noted pixel's full colour c with the smoke's discs over it */
#define TC_SMOKED(v, c) ((v) & 0xF0000 ? port_tc_smoke_rgb((v), (c)) : (c))

static inline int tc_sky_noted(const ulong *io, const uchar *hi, long x)
{
    return io[x] >> 24 == PORT_TC_SKY && (io[x] & 0xF0000 ? port_tc_smoke_px(io[x]) : (uchar)io[x]) == hi[x];
}

/* "True colour" (port/video/truecol.c): the clouds of row row in full colour where io notes them
 * and the picture hi still has them (to 0xFD000000 | RGB, in place): each pixel the softened
 * clouds at its point (the four texels around it mixed) and a finer layer, then toward the haze
 * by where the row is between its two steps (sky_bandq).  On the pass's threads after the
 * drawing: reads only the sky's tables */
void port_tc_sky_row(long row, ulong *io, const uchar *hi, long w)
{
    TcSkyRow r;
    TcSkyPair p = { -4, 0, 0 };
    long x;
    tc_sky_rowinfo(row, &r);
    if (r.kind == 1) {
        for (x = 0; x < w; x++)
            if (tc_sky_noted(io, hi, x))
                io[x] = 0xFD000000u | TC_SMOKED(io[x], r.c);
    } else if (r.kind == 2)
        for (x = 0; x < w; x++)
            if (tc_sky_noted(io, hi, x))
                io[x] = 0xFD000000u | TC_SMOKED(io[x], tc_sky_px(&r, x, &p));
}

/* the rows y0 .. y1 - 1 of plane (w wide, its picture base, the rows noted in rows) as
 * port_tc_sky_row does them, but the clouds of every odd row mixed from the even rows above and
 * below it (speed step E: the clouds are soft, and half the rows' clouds are not worked out); a
 * pixel whose neighbours above and below were not worked out is as before.  Each thread keeps an
 * even row's colours in its own buffers, with the number of the working-out that made them */
#define TC_SKY_W 8192
static ulong tcs_val[PORT_THREADS][2][TC_SKY_W];
static unsigned tcs_id[PORT_THREADS][2][TC_SKY_W], tcs_n[PORT_THREADS];

/* the even row e: noted pixels to their colours, kept in val (with id) at the columns noted in
 * row m (e itself, or below the part: the odd row above it, not written) */
static void tc_sky_even(ulong *io, const ulong *mio, const uchar *mhi, const TcSkyRow *r, ulong *val,
                        unsigned *ids, unsigned id, long w)
{
    TcSkyPair p = { -4, 0, 0 };
    long x;
    for (x = 0; x < w; x++)
        if (tc_sky_noted(mio, mhi, x)) {
            ulong c = tc_sky_px(r, x, &p);
            val[x] = c;
            ids[x] = id;
            if (io)
                io[x] = 0xFD000000u | TC_SMOKED(io[x], c);
        }
}

/* the next working-out's number on thread t (when they go round, none kept is valid: *ok 0) */
static unsigned tc_sky_id(int t, int *ok)
{
    if (++tcs_n[t] == 0) {
        memset(tcs_id[t], 0, sizeof tcs_id[t]);
        tcs_n[t] = 1;
        *ok = 0;
    }
    return tcs_n[t];
}

void port_tc_sky_rows(long y0, long y1, ulong *plane, const uchar *base, long w, const uchar *rows, int thread)
{
    ulong *va, *vb;
    unsigned *ia, *ib, na = 0, nb = 0;
    long e, x, y;
    TcSkyRow ra, rb, ry;
    if (w > TC_SKY_W || (y0 & 1) || thread < 0 || thread >= PORT_THREADS) {
        for (y = y0; y < y1; y++)
            if (rows[y])
                port_tc_sky_row(y, plane + (size_t)y * w, base + (size_t)y * w, w);
        return;
    }
    va = tcs_val[thread][0];
    vb = tcs_val[thread][1];
    ia = tcs_id[thread][0];
    ib = tcs_id[thread][1];
    ra.kind = 0;
    for (e = y0; e <= y1; e += 2) {
        int in = e < y1, ok = 1;
        rb.kind = 0;
        if (in && rows[e]) {            /* the even row (its own noted pixels) */
            ulong *io = plane + (size_t)e * w;
            const uchar *hi = base + (size_t)e * w;
            tc_sky_rowinfo(e, &rb);
            if (rb.kind == 1)
                port_tc_sky_row(e, io, hi, w);
            else if (rb.kind == 2) {
                nb = tc_sky_id(thread, &ok);
                tc_sky_even(io, io, hi, &rb, vb, ib, nb, w);
            }
        } else if (!in && e - 1 >= y0 && rows[e - 1]) {   /* below the part: for the odd row */
            tc_sky_rowinfo(e, &rb);
            if (rb.kind == 2) {
                nb = tc_sky_id(thread, &ok);
                tc_sky_even(NULL, plane + (size_t)(e - 1) * w, base + (size_t)(e - 1) * w, &rb, vb, ib, nb, w);
            }
        }
        y = e - 1;                      /* the odd row above it */
        if (y >= y0 && y < y1 && rows[y]) {
            ulong *io = plane + (size_t)y * w;
            const uchar *hi = base + (size_t)y * w;
            tc_sky_rowinfo(y, &ry);
            if (ry.kind == 2 && ra.kind == 2 && rb.kind == 2 && ok) {
                TcSkyPair p = { -4, 0, 0 };
                for (x = 0; x < w; x++)
                    if (tc_sky_noted(io, hi, x))
                        io[x] = 0xFD000000u | TC_SMOKED(io[x], ia[x] == na && ib[x] == nb ?
                                 (((va[x] ^ vb[x]) & 0xFEFEFE) >> 1) + (va[x] & vb[x]) : tc_sky_px(&ry, x, &p));
            } else
                port_tc_sky_row(y, io, hi, w);
        }
        {   /* the even row's colours are the next odd row's above */
            ulong *tv = va;
            unsigned *ti = ia;
            va = vb, vb = tv;
            ia = ib, ib = ti;
            na = nb;
            ra = rb;
        }
    }
}

/* the sun for the glow, with the sharp camera (rndsky_2A7A6C) */
static void tc_sun(void)
{
    SkyRec *sk = FF_CAM_NOSKY(&ff_cam) ? NULL : ff_cam.sky;
    ushort *sun = game_map_35BFB4 ? *(ushort **)(game_map_35BFB4 + 0x3A) : NULL;
    int ok = sk && sun && !sk->stars && !game_35CADC && !sky_force;
    if (!ok) {
        port_tc_sun(0, 0, 0, 0, 0, 0, 0, 0, 0);
        return;
    }
    port_tc_sun(1, (ushort)(sun[0] - ff_view.heading), (ushort)(-sun[1] - ff_view.pitch), (ushort)sun[1],
                (ushort)(-ff_view.pitch), ff_cam.half_w, ff_cam.half_h, ff_cam.zx, ff_cam.zy);
}
#endif

/* 0x2A7078: per screen row (0..n-1) the cloud texture position and steps, and the shade */
void rndsky_2A7078(long n)
{
    fix s, c, cu, cv, zf, y, yprev, dy, a3, a4, den, num, pu, pv, dist;
    fix L0, L1, L2, L3, L4, L5, L6, L7, L8;
    fix *m = ff_view.m;
    long fade, i, hz;
    fade = (game_442448 >> 24) - 1 == 1 ? 0x20 : 0xA2;
#ifndef TN_WATCOM_ABI
    sky_fade = fade;
    sky_rows_n = n;
#endif
    if (!sky_sin8_ok) {
        sky_sin8_ok = 1;
        for (i = 0; i < 0x400; i++) {
            fix_sincos((fixang)(i << 6), &s, &c);
            sky_sin8[i] = (uchar)((s + 0x10000) / 0x201);
        }
    }
    sky_frame++;
    if (game_44243F)
        rndsky_2A6F38();
    if (game_44243F) {
        fix_sincos((fixang)(sky_phase1 >> 8), &s, &c);
        sky_427DAC = ((((s * 40) >> 16) << 8) & 0xFF00) | (((c * 40) >> 16) & 0xFF);
        fix_sincos((fixang)(sky_phase2 >> 8), &s, &c);
        cu = s * 30;
        cv = c * 30;
    } else
        cu = cv = 0;
    sky_tex = sky_tex2 = sky_parm.tex = ff_cam.sky->cloud->bits;
    rasm_2B27DA(sky_tex);
#ifndef TN_WATCOM_ABI
    if (port_tc_on) {                   /* "True colour": the texels it has, its soft colours */
        if (sky_tex != sky_tc_tex) {
            memset(sky_tc_used, 0, sizeof sky_tc_used);
            for (i = 0; i < 0x10000; i++)
                sky_tc_used[sky_tex[i]] = 1;
            sky_tc_tex = sky_tex;
        }
        sky_tc_prep();
    }
#endif
    zf = (game_442445 >> 16) << 16;
    if (!zf)
        zf = 0x4000000;
    hz = ff_cam.half_h + (-muldiv(fix_mul(ff_cam.zy, 0x11999), ff_view.sin_p, ff_view.cos_p) >> 16);
    zf -= ff_view.z << 3;
    y = -0x10000;                           /* fix_div(0, half_h << 16) - 1 */
    L0 = fix_mul(m[6], -0x10000) - m[0];
    L1 = fix_mul(m[8], -0x10000) - m[2];
    L2 = m[1] - fix_mul(m[7], -0x10000);
    L3 = fix_mul(m[6], y) - m[3];
    L4 = fix_mul(m[8], y) - m[5];
    L5 = m[4] - fix_mul(m[7], y);
    yprev = y;
    L6 = fix_mul(m[6], 0x10000) - m[0];
    L7 = fix_mul(m[8], 0x10000) - m[2];
    L8 = m[1] - fix_mul(m[7], 0x10000);
    for (i = 0; i < n; i++) {
        long sh, w;
        if (i >= hz - 1) {
            sky_dv[i] = 0;
            sky_du[i] = 0;
            sh = fade;
#ifndef TN_WATCOM_ABI
            if (i < 2048)
                sky_band[i] = -1;
#endif
        } else {
            y = fix_div(i << 16, ff_cam.half_h << 16) - 0x10000;
            dy = y - yprev;
            yprev = y;
#ifndef TN_WATCOM_ABI
            if (port_s3d_on) {
                /* the sharp view (port/video/sharp3d.c): each row's from y itself; stepped, the
                 * rounding of every step adds up over its many rows and jumps as the head
                 * turns (the clouds shake) */
                a3 = fix_mul(m[6], y) - m[3];
                a4 = fix_mul(m[8], y) - m[5];
                L5 = m[4] - fix_mul(m[7], y);
            } else
#endif
            {
                a3 = L3 + fix_mul(m[6], dy);
                a4 = L4 + fix_mul(m[8], dy);
                L5 = L5 - fix_mul(m[7], dy);
            }
            den = fix_mul(L0, a4) - fix_mul(L1, a3);
            L3 = a3;
            num = fix_mul(L2, a4) - fix_mul(L1, L5);
            pu = muldiv(num, zf, den);
            num = fix_mul(L0, L5) - fix_mul(L2, L3);
            L4 = a4;
            pv = muldiv(num, zf, den);
            dist = fix_safe_pyth_dist(pu >> 8, pv >> 8, "r:\\prj\\tn\\v109\\tn\\src\\tn\\rndsky.c", 0x28C);
            den = fix_mul(L6, L4) - fix_mul(L7, L3);
            num = fix_mul(L8, L4) - fix_mul(L7, L5);
            w = grd_canvas->bm.w;
            sky_du[i] = (muldiv(num, zf, den) - pu) / w;
            num = fix_mul(L6, L5) - fix_mul(L8, L3);
            sky_dv[i] = (muldiv(num, zf, den) - pv) / grd_canvas->bm.w;
            sky_u[i] = pu + (ff_view.y + sky_cloud_x + cu);
            sky_v[i] = ff_view.x + sky_cloud_y + cv + pv;
            if (dist < sky_dist[0])
                sh = 0;
            else {
                long k = 1, j = 1;
                if (dist >= sky_dist[1]) {
                    do {
                        j++;
                        k++;
                        if (j >= 11)
                            break;
                    } while (dist >= sky_dist[j]);
                }
#ifndef TN_WATCOM_ABI
                if (port_s3d_on && i < 2048) {
                    /* where between its step's distances: the change from one step to the next
                     * centred on the game's (half a step on each side) */
                    long lo = sky_dist[k - 1], hi = k < 11 ? sky_dist[k] : lo, b = k * 16, bq = k * 256;
                    if (hi > lo) {
                        b += (long)(((long long)(dist - lo) << 4) / (hi - lo)) - 8;
                        bq += (long)(((long long)(dist - lo) << 8) / (hi - lo)) - 128;
                    }
                    sky_band[i] = (short)(b < 0 ? 0 : b > 11 * 16 ? 11 * 16 : b);
                    sky_bandq[i] = (short)(bq < 0 ? 0 : bq > 11 * 256 ? 11 * 256 : bq);
                    sky_tdu[i] = sky_du[i];
                    sky_tdv[i] = sky_dv[i];
                }
#endif
                if (k < 8)
                    sh = (long)(planet_tab38 + (k << 8));
                else {
                    sh = fade + 3 - (k - 8);
                    sky_dv[i] = 0;
                    sky_du[i] = 0;
                }
            }
#ifndef TN_WATCOM_ABI
            if (port_s3d_on && i < 2048 && dist < sky_dist[0]) {
                long b = (long)(((long long)dist << 4) / (sky_dist[0] > 0 ? sky_dist[0] : 1)) - 8,
                     bq = (long)(((long long)dist << 8) / (sky_dist[0] > 0 ? sky_dist[0] : 1)) - 128;
                sky_band[i] = (short)(b < 0 ? 0 : b);
                sky_bandq[i] = (short)(bq < 0 ? 0 : bq);
                sky_tdu[i] = sky_du[i];
                sky_tdv[i] = sky_dv[i];
            }
#endif
        }
#ifndef TN_WATCOM_ABI
        if (port_fog_ir && sh >= fade && sh < fade + 3)
            sh = fade + 3;              /* the fog's colour in the infrared (port/video/fog.c) */
#endif
        sky_shade[i] = sh;
    }
}

/* 0x2A7654: cloud span of row `row` from column x, len pixels */
void rndsky_2A7654(long row, long x, long len)
{
    uchar *lt, *dst, *sh;
    ulong u, v, uv, frac;
    long du, dv;
    if (grd_canvas->gc.fill_type)
        lt = grd_canvas->gc.fill_parm == (long)planet_tab3b ? planet_tab3c : planet_tab3f;
    else
        lt = 0;
#ifndef TN_WATCOM_ABI
    lt = port_ir_wash_sky(grd_canvas->gc.fill_parm, lt);    /* Infrared: Sci-fi, the switch-over picture */
#endif
    dst = grd_canvas->bm.bits + (ulong)grd_canvas->bm.row * row + x;
#ifndef TN_WATCOM_ABI
    if (port_s3d_on && !lt && !game_44243F && row < 2048 && sky_band[row] >= 0) {
        sky_span_dither(dst, row, x, len);
        return;
    }
#endif
    v = sky_v[row];
    du = sky_du[row];
    dv = sky_dv[row];
    u = sky_u[row];
    if (du == 0 && dv == 0) {
        long col = sky_shade[row];
        if (lt)
            col = lt[col];
        memset(dst, col, len);
#ifndef TN_WATCOM_ABI
        if (port_tc_on > 1 && !lt) {    /* "True colour" with the sun's glow */
            ulong k = (ulong)(dst - port_tc_base), i;
            if (len > 0 && k < port_tc_n && k + len <= port_tc_n && row < 2048) {
                for (i = 0; i < (ulong)len; i++)
                    port_tc_info[k + i] = (ulong)PORT_TC_FILL << 24 | (uchar)col;
                port_tc_row[row] = 1;
            }
        }
#endif
        return;
    }
    if (x) {
        u += du * x;
        v += dv * x;
    }
    sky_parm.du_lo = du & 0xFFFF;
    sky_parm.dv16 = (dv & 0xFFFF) << 16;
    sky_parm.du_i = (uchar)(du >> 16);
    sky_parm.dv_i = (uchar)(dv >> 16);
    u &= 0xFFFFFF;
    sh = (uchar *)sky_shade[row];
    sky_parm.len4 = (uchar)(len / 4);
    sky_parm.four = 4;
    sky_parm.z12 = 0;
    sky_parm.len = (ushort)len;
    v &= 0xFFFFFF;
    sky_parm.ac = sky_427DAC;
    if (sh && lt) {
        while (len-- != 0) {
            ulong idx = ((u >> 16) & 0xFF) | (((v >> 16) & 0xFF) << 8);
            *dst++ = lt[sh[sky_tex[idx]]];
            u += du;
            v += dv;
        }
        return;
    }
    if (sh || lt) {
        if (lt)
            sh = lt;
        if (sh != sky_last_tab) {
            rasm_2B27FB(sh);
            sky_last_tab = sh;
        }
#ifndef TN_WATCOM_ABI
        if (game_44243F == 0 && port_s3d_on && len > 0x1FE) {
            /* the sharp 3D view (port/video/sharp3d.c): rasm_2B2740 counts in a byte, so a row longer
             * than two halves of 255 goes in pieces */
            while (len > 0) {
                long n = len > 0xFF ? 0xFF : len;
                sky_parm.len = (ushort)n;
                frac = (v << 16) | (u & 0xFFFF);
                uv = ((long)u >> 16) | (((long)v >> 8) & 0xFF00);
                rasm_2B2740(dst, &sky_parm, uv, frac);
                u = (u + du * n) & 0xFFFFFF;
                v = (v + dv * n) & 0xFFFFFF;
                dst += n;
                len -= n;
            }
            return;
        }
#endif
        if (game_44243F == 0) {
            long half;
            if (len > 0xFF)
                sky_parm.len = (ushort)(len / 2);
            frac = (v << 16) | (u & 0xFFFF);
            uv = ((long)u >> 16) | (((long)v >> 8) & 0xFF00);
            rasm_2B2740(dst, &sky_parm, uv, frac);
            if (len <= 0xFF)
                return;
            half = sky_parm.len;
            u += du * half;
            u &= 0xFFFFFF;
            v += dv * half;
            v &= 0xFFFFFF;
            dst += half;
            if (len & 1)
                sky_parm.len = (ushort)(half + 1);
            frac = (v << 16) | (u & 0xFFFF);
            uv = ((long)u >> 16) | (((long)v >> 8) & 0xFF00);
            rasm_2B2740(dst, &sky_parm, uv, frac);
            return;
        }
        frac = (v << 16) | (u & 0xFFFF);
        uv = ((long)u >> 16) | (((long)v >> 8) & 0xFF00);
        rasm_2B28A0(dst, &sky_parm, uv, frac);
        return;
    }
    frac = (v << 16) | (u & 0xFFFF);
    uv = ((long)u >> 16) | (((long)v >> 8) & 0xFF00);
    if (game_44243F == 0)
        rasm_2B26E0(dst, &sky_parm, uv, frac);
    else
        rasm_2B2810(dst, &sky_parm, uv, frac);
}

#ifndef TN_WATCOM_ABI
/* The sharp view's cloud spans on all threads (port_parallel, speed step A): the same spans as
 * rndsky_2A79A8 makes, listed first (row, column, length) and sorted into blocks of SKY_ROWS
 * rows, then each block drawn by one thread.  Only where every span is the sharp view's own
 * kind (sky_span_dither, or one colour): those read the row tables and write only their own
 * pixels; the game's asm spans share sky_parm and the asm's table, so they stay on one thread. */
#define SKY_ROWS 16
#define SKY_SPANS 0x10000
typedef struct {
    short row, x, len;
} SkySpan;
static SkySpan sky_sp[SKY_SPANS], sky_sp_sorted[SKY_SPANS];
static long sky_sp_n, sky_blk[2048 / SKY_ROWS + 2];

static void sky_part(void *arg, int part, int thread)
{
    long i;
    (void)arg;
    (void)thread;
    for (i = sky_blk[part]; i < sky_blk[part + 1]; i++)
        rndsky_2A7654(sky_sp_sorted[i].row, sky_sp_sorted[i].x, sky_sp_sorted[i].len);
}

/* 1: drawn on the threads; 0: rndsky_2A79A8 draws them as the game does */
static int sky_spans_parallel(long n)
{
    long w = grd_canvas->bm.w, prev = 0, col, r, h, s, i, nb;
    if (!port_s3d_on || game_44243F || grd_canvas->gc.fill_type || n <= 0 || n > 2048 || w > 0x7FFF)
        return 0;
    for (r = 0; r < n; r++)             /* every row the sharp view's own kind */
        if (sky_band[r] < 0 && (sky_du[r] || sky_dv[r]))
            return 0;
    sky_sp_n = 0;
    for (col = 0; col < w; col++) {     /* the spans, as rndsky_2A79A8 */
        h = hview_cols[col];
        if (h < 0)                      /* P145: land's top above the picture: no sky rows */
            h = 0;
        if (h > n)
            return 0;
        if (prev > h) {
            if (sky_sp_n + (prev - h) > SKY_SPANS)
                return 0;
            for (r = h; r < prev; r++, sky_sp_n++) {
                s = sky_start[r];
                sky_sp[sky_sp_n].row = (short)r;
                sky_sp[sky_sp_n].x = (short)s;
                sky_sp[sky_sp_n].len = (short)(col - s);
            }
        } else if (prev < h) {
            for (r = prev; r < h; r++)
                sky_start[r] = col;
        }
        prev = h;
    }
    if (sky_sp_n + prev > SKY_SPANS)
        return 0;
    for (r = 0; r < prev; r++, sky_sp_n++) {
        s = sky_start[r];
        sky_sp[sky_sp_n].row = (short)r;
        sky_sp[sky_sp_n].x = (short)s;
        sky_sp[sky_sp_n].len = (short)(w - s);
    }
    nb = (n + SKY_ROWS - 1) / SKY_ROWS;
    memset(sky_blk, 0, (size_t)(nb + 2) * sizeof sky_blk[0]);
    for (i = 0; i < sky_sp_n; i++)      /* sorted into blocks of rows */
        sky_blk[sky_sp[i].row / SKY_ROWS + 2]++;
    for (i = 2; i < nb + 2; i++)
        sky_blk[i] += sky_blk[i - 1];
    for (i = 0; i < sky_sp_n; i++)
        sky_sp_sorted[sky_blk[sky_sp[i].row / SKY_ROWS + 1]++] = sky_sp[i];
    port_parallel(sky_part, NULL, (int)nb);
    return 1;
}
#endif

/* 0x2A79A8: the cloud spans above the terrain columns (row spans between column changes) */
void rndsky_2A79A8(void)
{
    long w = grd_canvas->bm.w, prev = 0, col, r, h, s;
#ifndef TN_WATCOM_ABI
    if (sky_spans_parallel(sky_rows_n))
        return;
#endif
    for (col = 0; col < w; col++) {
        h = hview_cols[col];
#ifndef TN_WATCOM_ABI
        if (h < 0)                      /* P145 */
            h = 0;
#endif
        if (prev > h) {
            r = h;
            do {
                s = sky_start[r];
                rndsky_2A7654(r, s, col - s);
                r++;
            } while (r < prev);
        } else if (prev < h) {
            for (r = prev; r < h; r++)
                sky_start[r] = col;
        }
        prev = h;
    }
    for (r = 0; r < prev; r++) {
        s = sky_start[r];
        rndsky_2A7654(r, s, w - s);
    }
}

/* 0x2A7A6C: draw the sky above the terrain */
void rndsky_2A7A6C(void)
{
    long i, m = 0;
#ifndef TN_WATCOM_ABI
    if (port_tc_on > 1)
        tc_sun();
#endif
    if (FF_CAM_NOSKY(&ff_cam) || ff_cam.sky->stars || sky_force || game_35CADC >= 1) {
        rndsky_2A6BE0();
        return;
    }
    for (i = grd_canvas->bm.w; i != 0; i--)
        if (hview_cols[i - 1] > m)
            m = hview_cols[i - 1];
    rndsky_2A7078(m);
    rndsky_2A79A8();
    ff_cam.a5 = m - sky_427DB8;
}

/* 0x2A7AEC: lock a sky record and everything it refers to */
SkyRec *rndsky_2A7AEC(Id id)
{
    SkyRec *sky = RefLock((Ref)id << 16);
    long i;
    if (sky->cloud_ref) {
        grs_bitmap *bm = RefLock(sky->cloud_ref);
        sky->cloud = bm;
        bm->bits = (uchar *)bm + 0x1C;
    }
    if (sky->stars_id)
        sky->stars = ResLock(sky->stars_id);
    if (sky->sun_ref) {
        grs_bitmap *bm = RefLock(sky->sun_ref);
        sky->sun = bm;
        bm->bits = (uchar *)bm + 0x1C;
    }
    sky->obj = sky->objs;
    for (i = 0; i < sky->nobj; i++) {
        grs_bitmap *bm = RefLock(sky->objs[i].ref);
        sky->objs[i].bm = bm;
        bm->bits = (uchar *)bm + 0x1C;
    }
    sky_count = *(short *)ResGet(id);
    return sky;
}

/* 0x2A7BA4: unlock a sky record */
void rndsky_2A7BA4(Id id)
{
    SkyRec *sky = RefGet((Ref)id << 16);
    long i;
    if (sky->cloud_ref)
        RefUnlock(sky->cloud_ref);
    if (sky->stars_id)
        ResUnlock(sky->stars_id);
    if (sky->sun_ref)
        RefUnlock(sky->sun_ref);
    for (i = 0; i < sky->nobj; i++)
        RefUnlock(sky->objs[i].ref);
    ResUnlock(id);
    sky_count = 0;
}

/* 0x2A7C38 (no callers) */
void rndsky_2A7C38(grs_bitmap **dst, Ref ref)
{
    grs_bitmap *bm = RefLock(ref);
    *dst = bm;
    bm->bits = (uchar *)bm + 0x1C;
}

/* 0x2A7C4C: one scaled column through a light table; step = {step0, step1, frac step, dst
 * step, frac} (the fraction is accumulated in step[4]) */
void rndsky_2A7C4C(uchar *dst, uchar *src, long n, uchar *ltab, long *step)
{
    while (n-- > 0) {
        if (*src)
            *dst = ltab[*src];
        if (step[4] >> 16) {
            src += step[1];
            ((ushort *)step)[9] = 0;
        } else
            src += step[0];
        step[4] += step[2];
        dst += step[3];
    }
}

/* 0x2A7CA0: a bitmap (colour 0 transparent), cut at the terrain columns */
void rndsky_2A7CA0(grs_bitmap *bm, short x, short y)
{
    grs_bitmap b = *bm;
    short xx = x, yy = y;
    long n, h, ch;
    uchar *src, *dst, *s, *d;
    if (gr_clip_flat8_bitmap(&b, &xx, &yy) == CLIP_ALL)
        return;
    src = b.bits;
    dst = grd_canvas->bm.bits + grd_canvas->bm.row * yy + xx;
    n = b.w;
    if (grd_canvas->gc.fill_type) {
        while (n-- > 0) {
            uchar *lt;
            ushort r;
            h = b.h;
            ch = hview_cols[xx];
            if (yy + h > ch)
                h = ch - yy;
            s = src++;
            r = b.row;
            lt = (uchar *)grd_canvas->gc.fill_parm;
            d = dst;
            if (h > 0)
                do {
                    if (*s)
                        *d = lt[*s];
                    s += r;
                    d += sky_row;
                } while (--h);
            xx++;
            dst++;
        }
    } else {
        while (n-- > 0) {
            ushort r, cr;
            h = b.h;
            ch = hview_cols[xx];
            if (yy + h > ch)
                h = ch - yy;
            r = b.row;
            cr = grd_canvas->bm.row;
            s = src++;
            d = dst;
            if (h > 0)
                do {
                    if (*s)
                        *d = *s;
                    s += r;
                    d += cr;
                } while (--h);
            xx++;
            dst++;
        }
    }
}

/* 0x2A7E28: a bitmap scaled to w x h, cut at the terrain columns */
void rndsky_2A7E28(grs_bitmap *bm, short x, short y, short w, short h)
{
    grs_canvas *cv = grd_canvas;
    long step[5];
    fix xs, ys, xacc, yacc;
    uchar *srow, *dst;
    long yfrac;
    if (x >= cv->gc.clip.i.right)
        return;
    if (x + w < cv->gc.clip.f.left >> 16)
        return;
    if (y >= cv->gc.clip.i.bot)
        return;
    if (y + h < cv->gc.clip.f.top >> 16)
        return;
    xs = fix_div(bm->w << 16, w << 16);
    ys = fix_div(bm->h << 16, h << 16);
    xacc = 0;
    yacc = 0;
    if (x < grd_canvas->gc.clip.i.left) {
        w -= grd_canvas->gc.clip.i.left - x;
        xacc = xs * ((grd_canvas->gc.clip.f.left >> 16) - x);
        x = grd_canvas->gc.clip.i.left;
    }
    if (y < grd_canvas->gc.clip.i.top) {
        long d = grd_canvas->gc.clip.i.top - y;
        h -= d;
        yacc += ((grd_canvas->gc.clip.f.top >> 16) - y) * ys;
        y = grd_canvas->gc.clip.i.top;
    }
    if (x + w > grd_canvas->gc.clip.f.right >> 16)
        w = grd_canvas->gc.clip.i.right - x;
    if (y + h > grd_canvas->gc.clip.f.bot >> 16)
        h = grd_canvas->gc.clip.i.bot - y;
    step[0] = (ys >> 16) * bm->row;
    step[1] = ((ys + 0xFFFF) >> 16) * bm->row;
    step[2] = ys & 0xFFFF;
    step[3] = grd_canvas->bm.row;
    srow = bm->bits + bm->row * (yacc >> 16);
    dst = grd_canvas->bm.bits + y * grd_canvas->bm.row + x;
    yfrac = yacc & 0xFFFF;
    while (w-- > 0) {
        long n = h, ch;
        uchar *src;
        step[4] = yfrac;
        ch = hview_cols[x];
        if (y + h > ch)
            n = ch - y;
        src = srow + (xacc >> 16);
        if (grd_canvas->gc.fill_type) {
            rndsky_2A7C4C(dst, src, n, (uchar *)grd_canvas->gc.fill_parm, step);
            dst++;
        } else {
            uchar *d = dst++;
            ulong f = step[4];
            if (n > 0)
                do {
                    if (*src)
                        *d = *src;
                    if (f >> 16) {
                        src += step[1];
                        f &= 0xFFFF;
                    } else
                        src += step[0];
                    f += step[2];
                    d += step[3];
                } while (--n);
        }
        x++;
        xacc += xs;
    }
}

/* 0x2A80FC: a sky object at heading / pitch difference a, b from the view; scaled by sx, sy
 * (flat: placed by the view height) */
void rndsky_2A80FC(grs_bitmap *bm, ushort a, ushort b, uchar flat, fix sx, fix sy)
{
    grs_bitmap tmp;
    fix c, s, q;
    long x, y;
    c = fix_cos(a);
    s = fix_sin(a);
    x = ff_cam.half_w + (muldiv(ff_cam.zx, s, c) >> 16);
    c = fix_cos(b);
    s = fix_sin(b);
    q = muldiv(ff_cam.zy, s, c);
    if (flat)
        q = fix_mul(ff_view.z + 0x400000, ff_cam.zy / 0x180) + q;
    y = ff_cam.half_h + (q >> 16);
    if (sx > 0x10000 && sy > 0x10000)
        sx = sy = 0x10000;
    if (game_35A540 >> 1)
        sy += sy;
#ifndef TN_WATCOM_ABI
    if (port_s3d_on) {
        sx *= port_s3d_kx;
        sy *= port_s3d_ky;
    }
#endif
    if (sx == 0x10000 && sy == sx) {
        if (bm->type == BMT_RSD8) {
            if (!grd_unpack_buf)
                return;
            if (gr_rsd8_convert(bm, &tmp))
                return;
            y -= bm->h >> 1;
            x -= bm->w >> 1;
            rndsky_2A7CA0(&tmp, (short)x, (short)y);
        } else {
            y -= bm->h >> 1;
            x -= bm->w >> 1;
            rndsky_2A7CA0(bm, (short)x, (short)y);
        }
    } else {
        long ws = bm->w * sx + 0x8000, hs = bm->h * sy + 0x8000;
        x -= ws >> 17;
        y -= hs >> 17;
        if (bm->type == BMT_RSD8) {
            if (!grd_unpack_buf)
                return;
            if (gr_rsd8_convert(bm, &tmp))
                return;
            rndsky_2A7E28(&tmp, (short)x, (short)y, (short)(ws >> 16), (short)(hs >> 16));
        } else
            rndsky_2A7E28(bm, (short)x, (short)y, (short)(ws >> 16), (short)(hs >> 16));
    }
}

/* 0x2A82D0: fill the sky (above the terrain columns) with a colour */
void rndsky_2A82D0(long color)
{
    long c = color & 0xFF, x, n;
    if (grd_canvas->gc.fill_type == 1)
        c = ((uchar *)grd_canvas->gc.fill_parm)[c];
#ifndef TN_WATCOM_ABI
    if (port_s3d_on) {
        /* the sharp 3D view: the same pixels a row at a time.  Its columns are too tall for the
         * CPU cache, so going down them was several times slower in the goggles' tall view */
        long w = grd_canvas->bm.w, top = 0, low = 0x7FFFFFFF, y;
        uchar *p = grd_canvas->bm.bits;
        for (x = 0; x < w; x++) {
            if (hview_cols[x] > top)
                top = hview_cols[x];
            if (hview_cols[x] < low)
                low = hview_cols[x];
        }
        for (y = 0; y < top; y++, p += grd_canvas->bm.row) {
            if (y < low) {              /* above the land in every column */
                memset(p, (uchar)c, w);
                if (port_tc_on > 1 && !grd_canvas->gc.fill_type) {     /* "True colour", the glow */
                    ulong k = (ulong)(p - port_tc_base);
                    if (k < port_tc_n && k + w <= port_tc_n && y < 2048) {
                        for (x = 0; x < w; x++)
                            port_tc_info[k + x] = (ulong)PORT_TC_FILL << 24 | (uchar)c;
                        port_tc_row[y] = 1;
                    }
                }
                continue;
            }
            for (x = 0; x < w; x++)
                if (hview_cols[x] > y) {
                    p[x] = (uchar)c;
                    if (port_tc_on > 1 && !grd_canvas->gc.fill_type && y < 2048) {
                        port_tc_mark(p + x, PORT_TC_FILL, 0, 0);
                        port_tc_row[y] = 1;
                    }
                }
        }
        return;
    }
#endif
    for (x = 0; x < grd_canvas->bm.w; x++) {
        uchar *p = grd_canvas->bm.bits + x;
        ushort row = grd_canvas->bm.row;
        n = hview_cols[x];
        for (; n > 0; n--) {
            *p = (uchar)c;
            p += row;
        }
    }
}

/* 0x2A8344: init: the reflection tables and the water colours */
void rndsky_2A8344(void)
{
    long i;
    for (i = 0; i < 16; i++)
        sky_refl_tab[i] = gr_malloc(0x100);
    for (i = 0; i < 0x100; i++)
        sky_water[i] = i >= 0xA2 && i <= 0xAF;
    rndsky_2A83BC();
}

/* 0x2A83BC: the reflection translucency tables (colour of the planet's sky) */
void rndsky_2A83BC(void)
{
    long k = game_41DE33, r, g, b, i, o;
    grs_rgb color;
    r = sky_rgb_r[k];
    g = sky_rgb_g[k];
    b = sky_rgb_b[k];
    if (game_359708 == 2)
        r = g = b = 0x80;
    if (!sky_refl_tab[0])
        return;
    color = (r << 2) | (g << 13) | (b << 24);
    for (i = 0, o = 0x100000; i < 16; i++, o += 0x30000)
        gr_init_translucency_table(sky_refl_tab[i], o / 64, 0x5555, color);
}

/* 0x2A844C: shutdown */
void rndsky_2A844C(void)
{
    long i;
    for (i = 0; i < 16; i++)
        gr_free(sky_refl_tab[i]);
}

#ifndef TN_WATCOM_ABI
/* "Water reflection: Medium" and "High" (port, the sharp 3D view only): the original mirrors
 * the rows of the picture above the horizon, so the reflection ends where the mirror line reaches the top of
 * the view (and is gone looking down), the water beyond it nearly flat in the sky's colour.
 * Medium draws the reflection there from the sky itself: the clouds the reflected ray meets (the
 * view's matrix with the height column negated, then the cloud rows of rndsky_2A7078), and
 * fades the sky colour over the water by the angle below the horizon instead of by rows, down
 * to a floor so the clouds overhead still show looking straight down.  The close water (the
 * smooth ground, drawn after the original's reflection) reflects too: port_water_full_pass. */
#define WF_ROWS 2048                    /* sharp3d.c MAX_ROWS */
#define WF_FLOOR 11                     /* the strongest sky colour table: 0..15 */
static ulong wf_u[WF_ROWS], wf_v[WF_ROWS];
static long wf_du[WF_ROWS], wf_dv[WF_ROWS], wf_sh[WF_ROWS];
static long wf_r0, wf_r1, wf_cloud, wf_flat;
static uchar *wf_lt, *wf_tex;
static signed char wf_dy[0x100], wf_dx[0x100];
static long wf_off[0x100];
static long wf_top;                 /* the horizon's row, not clamped to the view */
static uchar wf_tab[16][0x100];
static ulong wf_tab_key;
/* per row the next table down the ramp and how far toward it (0..16), mixed by a 4x4 dither so
 * the ramp has no band edges */
static uchar *wf_tab2[WF_ROWS];
static uchar wf_frac[WF_ROWS];
/* ripples: each row's place on the ramp in 16ths, the water's height */
static long wf_t16[WF_ROWS];
static fix wf_wz;
static double wf_fpx, wf_span;
static int wf_wz_ok;
static const uchar wf_bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* the 16 sky colour tables as rndsky_2A83BC makes them, each blend matched to the water colours
 * only (0xA2..0xAF): the game's tables pick from the whole palette, and some blends came out as
 * the green 0xA1, specks along the clouds' edges */
static uchar wr_lite[0x100], wr_dark[0x100];
static uchar wr_white = 0xA2;           /* the lightest water colour (the splashes' foam and spray) */

static void water_full_tables(void)
{
    long k = game_41DE33, r, g, b, i, j, o;
    ulong key;
    grs_rgb color;
    r = sky_rgb_r[k];
    g = sky_rgb_g[k];
    b = sky_rgb_b[k];
    if (game_359708 == 2)
        r = g = b = 0x80;
    color = (r << 2) | (g << 13) | (b << 24);
    key = color ^ grd_bpal[0xA2] * 3 ^ grd_bpal[0xAF] * 7 ^ grd_bpal[0x80] * 11;
    if (key == wf_tab_key)
        return;
    wf_tab_key = key;
    for (j = 0, o = 0x100000; j < 16; j++, o += 0x30000) {
        fix op = o / 64, filter = fix_mul(0x10000 - op, 0x5555), clarity = 0x10000 - op - filter;
        fix cr = (fix)((color & 0x3FF) << 12), cg = (fix)((color & 0x1FF800) << 1), cb = (fix)((color & 0xFFC00000) >> 10);
        fix fr = clarity + (fix_mul(cr, filter) >> 6), fg = clarity + (fix_mul(cg, filter) >> 6), fb = clarity + (fix_mul(cb, filter) >> 6);
        for (i = 0; i < 0x100; i++) {
            grs_rgb bg = grd_bpal[i];
            /* 8-bit components of the blend (gr_init_translucency_table's sums, 22.10 << 2) */
            long br = (fix_mul((fix)((bg & 0x3FF) << 12), fr) + fix_mul(cr, op)) >> 16;
            long bgc = (fix_mul((fix)((bg & 0x1FF800) << 1), fg) + fix_mul(cg, op)) >> 16;
            long bb = (fix_mul((fix)((bg & 0xFFC00000) >> 10), fb) + fix_mul(cb, op)) >> 16;
            long best = 0x7FFFFFFF, c, pick = 0xA2;
            for (c = 0xA2; c <= 0xAF; c++) {
                grs_rgb w = grd_bpal[c];
                long dr = br - (long)((w & 0x3FF) << 12 >> 16), dg = bgc - (long)(((w & 0x1FF800) << 1) >> 16),
                     db = bb - (long)(((w & 0xFFC00000) >> 10) >> 16), d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
                if (d < best)
                    best = d, pick = c;
            }
            wf_tab[j][i] = (uchar)pick;
        }
    }
    /* the water colours a step lighter and darker, for the rings' crests and troughs */
    {
        long lum[0x10], c2;
        for (i = 0xA2; i <= 0xAF; i++) {
            grs_rgb w = grd_bpal[i];
            lum[i - 0xA0] = (long)((w & 0x3FF) >> 2) * 3 + (long)((w & 0x1FF800) >> 13) * 6 + (long)(w >> 24) * 1;
        }
        for (i = 0; i < 0x100; i++)
            wr_lite[i] = wr_dark[i] = (uchar)i;
        for (i = 0xA2; i <= 0xAF; i++)
            if (lum[i - 0xA0] > lum[wr_white - 0xA0])
                wr_white = (uchar)i;
        for (i = 0xA2; i <= 0xAF; i++) {
            long up = 0x7FFFFFFF, dn = 0x7FFFFFFF;
            for (c2 = 0xA2; c2 <= 0xAF; c2++) {
                long d = lum[c2 - 0xA0] - lum[i - 0xA0];
                if (d > 0 && d < up)
                    up = d, wr_lite[i] = (uchar)c2;
                if (d < 0 && -d < dn)
                    dn = -d, wr_dark[i] = (uchar)c2;
            }
        }
    }
}

static void water_full_setup(void)
{
    fix m[9], s, c, cu, cv, zf, y, a3, a4, l5, den, num, pu, pv, dist, f;
    fix L0, L1, L2, L6, L7, L8;
    long h = grd_canvas->bm.h, w = grd_canvas->bm.w, i, top, fade;
    double ff, span;

    if (h > WF_ROWS)
        h = WF_ROWS;
    /* the horizon's row, not clamped to the view */
    f = fix_mul(ff_cam.zy, 0x11999);
    top = (-muldiv(f, ff_view.sin_p, ff_view.cos_p) >> 16) + ff_cam.half_h - 1;
    /* the sky colour by the angle below the horizon: the original's ramp of a level view (from
     * the horizon to the bottom of the view, half_h rows), then held at the floor */
    water_full_tables();
    ff = f / 65536.0;
    span = atan2((double)ff_cam.half_h, ff);
    for (i = top + 1 > 0 ? top + 1 : 0; i < h; i++) {
        double t = 16.0 * atan2((double)(i - top), ff) / span;
        long k = (long)t;
        if (k < 0)
            k = 0, t = 0;
        if (k >= WF_FLOOR)
            k = WF_FLOOR, t = WF_FLOOR;
        sky_refl_row[i] = wf_tab[k];
        wf_tab2[i] = wf_tab[k < WF_FLOOR ? k + 1 : k];
        wf_frac[i] = (uchar)((t - k) * 16.0);
        wf_t16[i] = (long)(t * 16.0);
    }
    wf_fpx = ff;
    wf_span = span;
    /* no shimmer offsets: the original moves each water colour's reflection by a random row and
     * column, new every frame, so the reflection jittered; fixed offsets still broke the
     * mirrored land into specks where the water's colours change.  Ripples move it instead.
     * (rndsky_2A84A4's random draws are still made, for the game's random numbers.) */
    for (i = 0; i < 0x100; i++) {
        wf_dy[i] = wf_dx[i] = 0;
        wf_off[i] = 0;
    }
    if (grd_canvas->gc.fill_type)
        wf_lt = grd_canvas->gc.fill_parm == (long)planet_tab3b ? planet_tab3c : planet_tab3f;
    else
        wf_lt = 0;
    wf_lt = port_ir_wash_sky(grd_canvas->gc.fill_parm, wf_lt);
    {   /* the sky rndsky_2A6BE0 picks (sky_cur is set later in the frame) */
        SkyRec *sk = FF_CAM_NOSKY(&ff_cam) || game_35CADC ? NULL : ff_cam.sky;
        wf_cloud = sk && sk->cloud && !sk->stars && !sky_force;
        wf_tex = wf_cloud ? sk->cloud->bits : NULL;
        wf_flat = sk && !sk->stars ? sk->color & 0xFF : 0;
    }
    if (grd_canvas->gc.fill_type == 1)
        wf_flat = ((uchar *)grd_canvas->gc.fill_parm)[wf_flat];
    wf_top = top;
    wf_r0 = top + 2 > 0 ? top + 2 : 0;
    wf_r1 = h;
    if (!wf_cloud)
        return;
    /* rndsky_2A7078's rows, each from its own y, through the mirrored matrix */
    for (i = 0; i < 9; i++)
        m[i] = i % 3 == 1 ? -ff_view.m[i] : ff_view.m[i];
    fade = (game_442448 >> 24) - 1 == 1 ? 0x20 : 0xA2;
    if (game_44243F) {
        fix_sincos((fixang)(sky_phase2 >> 8), &s, &c);
        cu = s * 30;
        cv = c * 30;
    } else
        cu = cv = 0;
    zf = (game_442445 >> 16) << 16;
    if (!zf)
        zf = 0x4000000;
    zf -= ff_view.z << 3;
    L0 = fix_mul(m[6], -0x10000) - m[0];
    L1 = fix_mul(m[8], -0x10000) - m[2];
    L2 = m[1] - fix_mul(m[7], -0x10000);
    L6 = fix_mul(m[6], 0x10000) - m[0];
    L7 = fix_mul(m[8], 0x10000) - m[2];
    L8 = m[1] - fix_mul(m[7], 0x10000);
    for (i = wf_r0; i < wf_r1; i++) {
        long sh;
        y = fix_div(i << 16, ff_cam.half_h << 16) - 0x10000;
        a3 = fix_mul(m[6], y) - m[3];
        a4 = fix_mul(m[8], y) - m[5];
        l5 = m[4] - fix_mul(m[7], y);
        den = fix_mul(L0, a4) - fix_mul(L1, a3);
        if (den == 0) {
            wf_du[i] = wf_dv[i] = 0;
            wf_sh[i] = fade;
            continue;
        }
        num = fix_mul(L2, a4) - fix_mul(L1, l5);
        pu = muldiv(num, zf, den);
        num = fix_mul(L0, l5) - fix_mul(L2, a3);
        pv = muldiv(num, zf, den);
        dist = fix_safe_pyth_dist(pu >> 8, pv >> 8, "r:\\prj\\tn\\v109\\tn\\src\\tn\\rndsky.c", 0x28C);
        den = fix_mul(L6, a4) - fix_mul(L7, a3);
        num = fix_mul(L8, a4) - fix_mul(L7, l5);
        wf_du[i] = den ? (muldiv(num, zf, den) - pu) / w : 0;
        num = fix_mul(L6, l5) - fix_mul(L8, a3);
        wf_dv[i] = den ? (muldiv(num, zf, den) - pv) / w : 0;
        wf_u[i] = pu + (ff_view.y + sky_cloud_x + cu);
        wf_v[i] = ff_view.x + sky_cloud_y + cv + pv;
        if (dist < sky_dist[0])
            sh = 0;
        else {
            long k = 1;
            while (k < 11 && dist >= sky_dist[k])
                k++;
            /* rndsky_2A7078 turns the farthest clouds (from step 8) into the plain haze colour;
             * the reflection keeps them, at step 7's shade, out to the last step, so the far
             * water still shows clouds (and the ripples on them) */
            if (k < 8)
                sh = (long)(planet_tab38 + (k << 8));
            else if (k < 11)
                sh = (long)(planet_tab38 + (7 << 8));
            else {
                sh = fade + 3 - (k - 8);
                wf_du[i] = wf_dv[i] = 0;
            }
        }
        wf_sh[i] = sh;
    }
}

/* the sky the reflected rays of row r meet, columns -1 .. w (the canvas's colours) */
static void water_full_row(uchar *out, long r, long w)
{
    ulong u, v;
    long du, dv, x;
    uchar *sh, c;
    if (r < wf_r0)
        r = wf_r0;
    if (r >= wf_r1)
        r = wf_r1 - 1;
    if (!wf_cloud || r < wf_r0 || (wf_du[r] == 0 && wf_dv[r] == 0)) {
        c = (uchar)(!wf_cloud ? wf_flat : r < wf_r0 ? 0xA2 : wf_sh[r]);
        memset(out, wf_cloud && wf_lt ? wf_lt[c] : c, w + 2);
        return;
    }
    du = wf_du[r];
    dv = wf_dv[r];
    u = wf_u[r] - du;
    v = wf_v[r] - dv;
    sh = (uchar *)wf_sh[r];
    for (x = 0; x < w + 2; x++, u += du, v += dv) {
        uchar bu = (uchar)(u >> 16), bv = (uchar)(v >> 16);
        if (game_44243F)
            c = wf_tex[((uchar)(bv + sky_warp_b[bu]) << 8) | (uchar)(bu + sky_warp_a[bv])];
        else
            c = wf_tex[(bv << 8) | bu];
        if (sh)
            c = sh[c];
        out[x] = wf_lt ? wf_lt[c] : c;
    }
}
#endif

#ifndef TN_WATCOM_ABI
/* ------------------------------------------------------------ ripples ("Water ripples") */
/* Waves on the water: a few sine waves fixed in the world, moving with time, tilt the surface;
 * the tilt moves the reflection sideways and lighter or darker (up or down the angle ramp), as a
 * mirror that is not flat.  Each pixel's place on the water comes from its row's ray (the cloud
 * rows' geometry, the mirror of the ray down) and the water's height under the camera.  Waves
 * finer than a few pixels fade out, so far water stays smooth instead of flickering. */
#define WR_N 5
#define WR_SHADE 2.5      /* the light and dark of the waves over the plain tilt (looks flat at 1) */
static const struct { double len, ang, per, steep; } wr_waves[WR_N] = {
    /* wavelength (map cells), direction from the wind (degrees), period (s), slope */
    { 1.92, 0, 3.1, 0.050 }, { 1.27, 37, 2.5, 0.045 }, { 0.82, -29, 2.0, 0.040 }, { 0.50, 71, 1.6, 0.030 },
    { 6.0, -12, 5.5, 0.030 },       /* a long swell, still seen far out where the others are too fine */
};
static short wr_cos[1024];

/* rings on the water, each spreading and fading: around the wading units (now and then, more
 * often while they move), and with "Water effects" the splashes and the wakes */
#define WR_RINGS 192
enum { WK_WADE, WK_SPLASH, WK_WAKE };
static const struct { double len, speed, life; } wr_kinds[3] = {
    /* the ring's wavelength (map squares; a soldier is about 0.6 high), the squares a second it
     * spreads, its seconds */
    { 0.30, 0.6, 2.4 },         /* a wading unit's */
    { 0.36, 1.0, 2.2 },         /* a splash's */
    { 0.42, 0.7, 2.0 },         /* a wake's: one every tenth of a second behind a moving vehicle */
};
#define WR_RBAND 1000.0   /* the rings' light and dark (1/16 colour steps per unit of slope) */
/* (+ the band's half width, its radii^2 this frame) */
static struct { double x, y, t0, a, len, half, life, R, amp, ro2, ri2; int kind; } wr_ring[WR_RINGS];
static int wr_nring;
/* the splashes' spray (port_water_after): where, when, how big */
#define WR_SPLASHES 16
#define WR_SPRAY_LIFE 1.6
static struct { double x, y, t0, size; } wr_spl[WR_SPLASHES];
static int wr_nspl;
/* the ring's profile across: exp(-u^2) * sin(u * 0.6 * 2 pi) for u = (d - R) / (0.6 wavelength)
 * from -2.5 to 2.5 */
#define WR_PN 256
static float wr_prof[WR_PN + 1];
/* one row's waves (ripple_row; each thread has its own, port_water_full_pass): the phases at
 * column 0 and per column, the slope weights; its rings: those its line on the water crosses,
 * with the columns (of the field, every second canvas column) where it does and each one's fade
 * by the pixel's size (pxs: its mean size in squares); its place on the water: column 0, per
 * column, the row's own across and away */
typedef struct {
    ulong ph0[WR_N], dph[WR_N];
    long ce[WR_N], cf[WR_N];
    int nact, act[WR_RINGS];
    long alo[WR_RINGS], ahi[WR_RINGS], ilo[WR_RINGS], ihi[WR_RINGS];
    float rw[WR_RINGS];
    double pxs, X0, Y0, eX, eY, ex, ey, fX, fY;
} WrRow;

static void wr_add(int kind, double x, double y, double t0, double a)
{
    if (wr_nring >= WR_RINGS)
        return;
    wr_ring[wr_nring].x = x;
    wr_ring[wr_nring].y = y;
    wr_ring[wr_nring].t0 = t0;
    wr_ring[wr_nring].a = a;
    wr_ring[wr_nring].kind = kind;
    wr_ring[wr_nring].len = wr_kinds[kind].len;
    wr_ring[wr_nring].half = 2.5 * wr_kinds[kind].len * 0.6;
    wr_ring[wr_nring].life = wr_kinds[kind].life;
    wr_nring++;
}

/* (x, y) is on the water: the ground there is the water's height (wz), on the map */
static int wr_wet(double x, double y, double wz)
{
    if (!(x >= 0 && y >= 0 && x < 512.0 && y < 512.0))
        return 0;
    return map_2949C4((fix)(x * 65536.0), (fix)(y * 65536.0)) / 65536.0 <= wz + 0.02;
}

/* a splash at (x, y) of a size (0.7 a shot's, 2 a body's): two rings, the white foam in the
 * first (wr_ring_tilt) and the spray (port_water_after); one near another just made is that one */
static void wr_splash(double x, double y, double t, double size)
{
    int i;
    for (i = 0; i < wr_nspl; i++)
        if (t - wr_spl[i].t0 < 0.3 && fabs(wr_spl[i].x - x) + fabs(wr_spl[i].y - y) < 0.8) {
            if (size > wr_spl[i].size)
                wr_spl[i].size = size;
            return;
        }
    if (wr_nspl >= WR_SPLASHES)
        return;
    wr_spl[wr_nspl].x = x;
    wr_spl[wr_nspl].y = y;
    wr_spl[wr_nspl].t0 = t;
    wr_spl[wr_nspl].size = size;
    wr_nspl++;
    wr_add(WK_SPLASH, x, y, t, 0.10 * size);
    wr_add(WK_SPLASH, x, y, t + 0.2, 0.05 * size);
}

/* "Water effects": a weapon's hit on the ground (ordnance.c, with the effect fx it makes there):
 * on the water, a splash by the effect: a shot's (1, the blue sparks), a shell's (2, the brown
 * sparks), a missile's or a grenade's (the big blasts 0x18 .. 0x1B) */
void port_water_hit(long x, long y, long z, long fx)
{
    double wz = wf_wz / 65536.0, X = x / 65536.0, Y = y / 65536.0;
    if (!port_water_full || !port_water_fx || !wf_wz_ok || fabs(z / 65536.0 - wz) > 0.3 || !wr_wet(X, Y, wz))
        return;
    wr_splash(X, Y, game_time / 1000.0, fx >= 0x18 && fx <= 0x1B ? 1.8 : fx == 2 ? 1.2 : 0.7);
}

/* "Water effects", each frame: the other splashes and the wakes.  Splashes: a soldier or a body
 * (class 1) dropping into the water, or gone while it stood in it.  Wakes: a vehicle (class 2)
 * moving on the water (the V of its rings, one every tenth of a second).  (No rain rings: it
 * rains only on the planets whose water the game draws without the sky, game_41DE34 not 0, and
 * there the water passes are off, ffpipe.c; the rings never showed.) */
static void wr_effects(double t, double dt, double wz)
{
    static struct { fix x, y, z; uchar cls; double next; } trk[GAME_MAX_ENTITIES];
    double vx = ff_view.x / 65536.0, vy = ff_view.y / 65536.0;
    int i, n, fresh = dt <= 0 || dt > 1.0;     /* first frame, or a jump: only look this time */
    for (i = n = 0; i < wr_nspl; i++)
        if (t - wr_spl[i].t0 < WR_SPRAY_LIFE && t >= wr_spl[i].t0)
            wr_spl[n++] = wr_spl[i];
    wr_nspl = n;
    for (i = 0; i < GAME_MAX_ENTITIES; i++) {
        GameEntity *e = &game_entities[i];
        uchar cls = e->active ? e->cls : 0;
        double x = e->x / 65536.0, y = e->y / 65536.0, z = e->z / 65536.0;
        double lx = trk[i].x / 65536.0, ly = trk[i].y / 65536.0, lz = trk[i].z / 65536.0;
        int near = (x - vx) * (x - vx) + (y - vy) * (y - vy) < 40.0 * 40.0;
        /* (falling: at least 2.5 squares a second down, not put there: less than a square across) */
        if (!fresh && cls == 1 && trk[i].cls == 1 && near && lz > wz + 0.05 && z <= wz + 0.05 && (lz - z) / dt > 2.5 &&
            fabs(x - lx) + fabs(y - ly) < 1.0 && wr_wet(x, y, wz))
            wr_splash(x, y, t, 2.0);
        if (!fresh && trk[i].cls == 1 && cls != 1 && (lx - vx) * (lx - vx) + (ly - vy) * (ly - vy) < 40.0 * 40.0 &&
            fabs(lz - wz) < 1.2 && wr_wet(lx, ly, wz))
            wr_splash(lx, ly, t, 2.0);
        if (!fresh && cls == 2 && trk[i].cls == 2 && near && z > wz - 0.6 && z < wz + 2.0 && wr_wet(x, y, wz)) {
            double sp = sqrt((x - lx) * (x - lx) + (y - ly) * (y - ly)) / dt;
            if (sp > 0.4 && t >= trk[i].next) {
                wr_add(WK_WAKE, lx, ly, t, sp * 0.02 < 0.06 ? 0.04 + sp * 0.02 : 0.10);
                trk[i].next = t + 0.1;
            }
        }
        trk[i].x = e->x;
        trk[i].y = e->y;
        trk[i].z = e->z;
        if (cls != trk[i].cls)
            trk[i].next = 0;
        trk[i].cls = cls;
    }
}

static void wr_rings(void)
{
    static struct { fix x, y; double next; } last[GAME_MAX_ENTITIES];
    static ulong at;
    static double t_last;
    double t = game_time / 1000.0, wz = wf_wz / 65536.0;
    int i, n;
    if (wr_prof[WR_PN / 2 + 10] == 0)
        for (i = 0; i <= WR_PN; i++) {
            double u = (i - WR_PN / 2) * (5.0 / WR_PN);
            wr_prof[i] = (float)(exp(-u * u) * sin(u * 0.6 * 2 * M_PI));
        }
    if (game_time == at)
        return;
    at = game_time;
    for (i = n = 0; i < wr_nring; i++)          /* the old rings go (and all after a jump back) */
        if (t - wr_ring[i].t0 < wr_ring[i].life && t >= wr_ring[i].t0 - 1.0)
            wr_ring[n++] = wr_ring[i];
    wr_nring = n;
    for (i = 0; i < GAME_MAX_ENTITIES; i++) {
        GameEntity *e = &game_entities[i];
        double ex, ey, z, mv;
        if (!e->active || e->cls != 1 || e->x < 0 || e->y < 0 || e->x >= 0x2000000 || e->y >= 0x2000000) {
            last[i].next = 0;
            continue;
        }
        ex = e->x / 65536.0 - ff_view.x / 65536.0;
        ey = e->y / 65536.0 - ff_view.y / 65536.0;
        z = e->z / 65536.0;
        /* near, standing in the water (the ground there is the water's height) */
        if (ex * ex + ey * ey > 48.0 * 48.0 || z - wz > 1.2 || z < wz - 1.0 ||
            map_2949C4(e->x, e->y) / 65536.0 > wz + 0.02) {
            last[i].next = 0;
            continue;
        }
        mv = fabs((e->x - last[i].x) / 65536.0) + fabs((e->y - last[i].y) / 65536.0);
        last[i].x = e->x;
        last[i].y = e->y;
        if (last[i].next == 0)
            last[i].next = t + (i % 7) * 0.1;
        if (t < last[i].next || wr_nring >= WR_RINGS)
            continue;
        wr_add(WK_WADE, e->x / 65536.0, e->y / 65536.0, t, mv > 0.01 ? 0.05 : 0.025);
        last[i].next = t + (mv > 0.01 ? 0.45 : 1.3);
    }
    if (port_water_fx)
        wr_effects(t, t - t_last, wz);
    t_last = t;
    for (i = 0; i < wr_nring; i++) {            /* this frame's radius and strength */
        double age = t - wr_ring[i].t0, ro, ri;
        if (age < 0) {                          /* not yet (a splash's second ring) */
            wr_ring[i].R = -1e9;
            wr_ring[i].amp = 0;
            wr_ring[i].ro2 = wr_ring[i].ri2 = -1;
            continue;
        }
        wr_ring[i].R = age * wr_kinds[wr_ring[i].kind].speed;
        ro = wr_ring[i].R + wr_ring[i].half;
        ri = wr_ring[i].R - wr_ring[i].half;
        wr_ring[i].amp = wr_ring[i].a * (1 - age / wr_ring[i].life) / (1 + wr_ring[i].R * 1.5);
        wr_ring[i].ro2 = ro * ro;
        wr_ring[i].ri2 = ri > 0 ? ri * ri : -1;
    }
}

/* the rings row q's line (column xx at (X0, Y0) + xx * 2 * (eX, eY)) crosses, each faded out
 * from 6 to 3 pixels per wavelength (by the pixel's mean size): they are few and brief, so they
 * stay longer than the waves; a splash while it has its foam (wr_ring_tilt) */
static void wr_rings_row(WrRow *q, long n)
{
    double ux = 2 * q->eX, uy = 2 * q->eY, l2 = ux * ux + uy * uy;
    int i;
    q->nact = 0;
    if (l2 <= 0)
        return;
    for (i = 0; i < wr_nring; i++) {
        double cx = wr_ring[i].x - q->X0, cy = wr_ring[i].y - q->Y0, t0 = (cx * ux + cy * uy) / l2,
               ro = wr_ring[i].R + wr_ring[i].half, d2 = cx * cx + cy * cy - t0 * t0 * l2, half, ri, rw;
        long lo, hi;
        int foam = wr_ring[i].kind == WK_SPLASH && wr_ring[i].R < 0.6 * wr_kinds[WK_SPLASH].speed;
        if (ro <= 0 || d2 >= ro * ro)
            continue;
        rw = (wr_ring[i].len / q->pxs - 3.0) / 3.0;
        if (rw <= 0 && !foam)
            continue;
        ri = wr_ring[i].R - wr_ring[i].half;
        half = sqrt((ro * ro - d2) / l2);
        lo = (long)floor(t0 - half);
        hi = (long)ceil(t0 + half);
        if (hi < 0 || lo > n)
            continue;
        q->act[q->nact] = i;
        q->rw[q->nact] = (float)(rw < 0 ? 0 : rw > 1 ? 1 : rw);
        q->alo[q->nact] = lo < 0 ? 0 : lo;
        q->ahi[q->nact] = hi > n ? n : hi;
        /* inside the ring's inner edge the water is still: no need to look there */
        q->ilo[q->nact] = 1;
        q->ihi[q->nact] = 0;
        if (ri > 0 && d2 < ri * ri && !foam) {
            double hi2 = sqrt((ri * ri - d2) / l2);
            q->ilo[q->nact] = (long)ceil(t0 - hi2);
            q->ihi[q->nact] = (long)floor(t0 + hi2);
        }
        q->nact++;
    }
}

/* water colour c lighter or darker by v / 16 colour steps, or the foam's white where fo (in
 * 16ths) is over the dither (b) */
static uchar wr_ring_px(uchar c, long v, long fo, long b)
{
    long st = v >= 0 ? (v + b) >> 4 : -((-v + b) >> 4);
    if (fo > b)
        return wr_white;
    for (; st > 0; st--)
        c = wr_lite[c];
    for (; st < 0; st++)
        c = wr_dark[c];
    return c;
}
#define WR_PX(c) (fany && (frg[x >> 1] || ffo[x >> 1]) ? wr_ring_px((c), frg[x >> 1], ffo[x >> 1], bay[(x + 2) & 3]) : (c))

/* the rings' tilt at (X, Y): *e across the row, *f away from the eye (as the waves' slopes), *g
 * away from the ring's middle (its light and dark bands all round); *fo a splash's foam (16ths):
 * a white disc growing from its middle for 0.6 s, thinner at its edge and as it goes */
static void wr_ring_tilt(const WrRow *q, long xx, double *ea, double *fa, double *ga, double *fo)
{
    double X = q->X0 + xx * 2 * q->eX, Y = q->Y0 + xx * 2 * q->eY;
    int j;
    for (j = 0; j < q->nact; j++) {
        int i = q->act[j];
        double vx, vy, R = wr_ring[i].R, d, u, e;
        if (xx < q->alo[j] || xx > q->ahi[j] || (xx >= q->ilo[j] && xx <= q->ihi[j]))
            continue;
        vx = X - wr_ring[i].x;
        vy = Y - wr_ring[i].y;
        d = vx * vx + vy * vy;
        if (d >= wr_ring[i].ro2 || d <= wr_ring[i].ri2)
            continue;
        d = sqrt(d);
        if (wr_ring[i].kind == WK_SPLASH) {
            double age = R / wr_kinds[WK_SPLASH].speed, rf = (0.12 + age * 0.45) * (wr_ring[i].a > 0.07 ? 1.0 : 0.6);
            if (age < 0.6 && d < rf)
                *fo += 14.0 * (1 - age / 0.6) * sqrt(1 - d / rf);
        }
        u = (d - R) / (wr_ring[i].len * 0.6);
        if (u < -2.5 || u > 2.5 || d < 1e-3)
            continue;
        /* a short wave train on the ring, weaker as it grows and ages */
        e = wr_ring[i].amp * q->rw[j] * wr_prof[(int)((u + 2.5) * (WR_PN / 5.0))];
        *ea += e * (vx * q->ex + vy * q->ey) / d;
        *fa += e * (vx * q->fX + vy * q->fY) / d;
        *ga += e;
    }
}

/* the game's heights (ff_view.z, map_2949C4) are in map squares: the matrix's 1/24 applies to
 * the map's raw heights, and map_2949C4 returns them / 24 (checked: a lake's far shore is drawn
 * where a ray meets the water as far away as the map has it) */
#define WF_HSCALE 1.0
/* the ray of pixel (x, r) in the world (map squares), from the view's angles as ffpipe_29EB64
 * builds the matrix */
static void water_ray(double x, double r, double *dx, double *dy, double *dz)
{
    double sh = ff_view.sin_h / 65536.0, ch = ff_view.cos_h / 65536.0, sp = ff_view.sin_p / 65536.0,
           cp = ff_view.cos_p / 65536.0, sb = ff_view.sin_b / 65536.0, cb = ff_view.cos_b / 65536.0;
    double u = (x - ff_cam.half_w) / (ff_cam.zx * 1.1 / 65536.0), v = (r - ff_cam.half_h) / (ff_cam.zy * 1.1 / 65536.0);
    *dy = sh * cp + u * (ch * cb + sb * sp * sh) + v * (ch * sb - cb * sp * sh);
    *dx = cp * ch + u * (sb * sp * ch - cb * sh) + v * (-sb * sh - cb * sp * ch);
    *dz = -sp + u * (sb * cp) + v * (-cb * cp);
}

/* where pixel (x, r) meets the water plane hw cells below the eye; 0 when it looks up */
static int water_point(double x, double r, double hw, double *X, double *Y)
{
    double dx, dy, dz, t;
    water_ray(x, r, &dx, &dy, &dz);
    if (dz > -1e-6)
        return 0;
    t = hw / -dz;
    *X = ff_view.x / 65536.0 + t * dx;
    *Y = ff_view.y / 65536.0 + t * dy;
    return 1;
}

/* where a ray met the ground at height wz: the water there when the map is level around it, no
 * point half a square away lower.  A ray that met land first (the slope of a hill it skimmed on
 * the way to the water far off, or a shore square's water colours on a slope) has lower land on
 * the eye's side; that height, up to two squares above the water, made the water's passes see
 * the eye that much nearer the water, for the frame and after it: the shallows' light blue over
 * a wide band of the water, as a flash (docs/BUGS.md P58).  And the square's texture has water
 * colours: a lone pixel of them on level land (a gap between the near ground's polygons, the
 * view's bottom row by a pier) met the land itself, the concrete of a pier 1.5 squares above the
 * water, the same flash (P73) */
static int water_flat(double X, double Y, double wz)
{
    static const double d[8][2] = { { 0.5, 0 }, { -0.5, 0 }, { 0, 0.5 }, { 0, -0.5 },
                                    { 0.35, 0.35 }, { 0.35, -0.35 }, { -0.35, 0.35 }, { -0.35, -0.35 } };
    long t, k = 0;
    uchar *row;
    int i;
    if (X < 0 || X >= 0x2000000 || Y < 0 || Y >= 0x2000000)
        return 0;
    t = *((uchar *)TN_LOWMEM(GMAP->rows2[(long)Y >> 16]) + ((long)X >> 16) * 3) & 0x3F;
    row = t < planet_tex_count ? planet_tex_rows[t] : NULL;
    for (i = 0; row && i < 0x100 && !k; i++)
        k = sky_water[planet_light[(8 << 8) + row[i]]];
    if (!k)
        return 0;
    for (i = 0; i < 8; i++) {
        double x = X + d[i][0] * 65536.0, y = Y + d[i][1] * 65536.0;
        if (x < 0 || x >= 0x1FF0000 || y < 0 || y >= 0x1FF0000)
            continue;
        if (map_2949C4((fix)x, (fix)y) / 65536.0 < wz - 0.03)
            return 0;
    }
    return 1;
}

/* the water's height: the ray of a water pixel near the bottom of the view walked over the map
 * (out to `across` squares, `res` squares a step) until it meets the ground (map_2949C4) where
 * the map is level (water_flat; at most 8 rays that met land first); 0 if none found.  Up a
 * column the rays only get flatter: once one passes over all the land within `across`, the rows
 * above it in that column are not walked (with the water far off, every water pixel's ray was
 * walked to the end, up to 10 ms a frame at 6x) */
static int water_level_to(double across, double res)
{
    long w = grd_canvas->bm.w, h = grd_canvas->bm.h, r, x, i, k, land = 0;
    uchar *bits = grd_canvas->bm.bits, past[33];
    memset(past, 0, sizeof past);
    if (h > WF_ROWS)
        h = WF_ROWS;
    if (w > 4096)
        w = 4096;
    for (r = h - 1; r >= wf_r0; r -= 4)
        for (i = 0; i < 33; i++) {
            double dx, dy, dz, len, d, step, z0 = ff_view.z / 65536.0, lo, hi;
            x = w / 2 + ((i & 1) ? 1 : -1) * ((i + 1) / 2) * (w / 32);
            if (past[i] || x < 0 || x >= w || !sky_water[bits[r * grd_canvas->bm.row + x]] || r < hview_cols[x])
                continue;
            water_ray(x, r, &dx, &dy, &dz);
            if (dz > -1e-6)
                continue;
            /* d: the depth below the eye in the game's heights; d / WF_HSCALE squares down the ray */
            len = sqrt(dx * dx + dy * dy) / -dz / WF_HSCALE;     /* cells across per height unit */
            step = len > 0 ? res / len : 1.0;
            if (step > 1.0)
                step = 1.0;
            lo = 0;
            for (d = step; d * len < across && d < 256.0; lo = d, d += step) {
                double X = ff_view.x + dx / -dz * d / WF_HSCALE * 65536.0, Y = ff_view.y + dy / -dz * d / WF_HSCALE * 65536.0;
                if (X < 0 || X >= 0x1FF0000 || Y < 0 || Y >= 0x1FF0000) {
                    d = 256.0;          /* off the map */
                    break;
                }
                if (map_2949C4((fix)X, (fix)Y) / 65536.0 >= z0 - d)
                    break;
            }
            if (d * len >= across || d >= 256.0) {
                past[i] = 1;
                continue;
            }
            for (hi = d, k = 0; k < 12; k++) {
                d = (lo + hi) / 2;
                if (map_2949C4((fix)(ff_view.x + dx / -dz * d / WF_HSCALE * 65536.0), (fix)(ff_view.y + dy / -dz * d / WF_HSCALE * 65536.0)) / 65536.0 >= z0 - d)
                    hi = d;
                else
                    lo = d;
            }
            if (!water_flat(ff_view.x + dx / -dz * hi / WF_HSCALE * 65536.0,
                            ff_view.y + dy / -dz * hi / WF_HSCALE * 65536.0, z0 - hi)) {
                if (++land >= 8)
                    return 0;           /* the height found before stays */
                continue;
            }
            wf_wz = (fix)((z0 - hi) * 65536.0);
            return 1;
        }
    return 0;
}

static int water_level(void)
{
    return water_level_to(48.0, 0.0625);
}

/* row r's wave phases at column 0 and per column, and the slope weights (sideways in pixels, up
 * the ramp in 16ths of a table), 0 when the waves are too fine for the row */
static int ripple_row(WrRow *q, long r, long w)
{
    double hw = (ff_view.z - wf_wz) / 65536.0 / WF_HSCALE, X0, Y0, X1, Y1, Xn, Yn, Xc, Yc;
    double eX, eY, ex, ey, fX, fY, px, pxd, t = game_time / 1000.0, wind;
    long i, any = 0;
    if (hw <= 0 || !water_point(0, r, hw, &X0, &Y0) || !water_point(w, r, hw, &X1, &Y1) ||
        !water_point(w / 2, r, hw, &Xc, &Yc) || !water_point(w / 2, r + 1, hw, &Xn, &Yn))
        return 0;
    eX = (X1 - X0) / w;
    eY = (Y1 - Y0) / w;
    px = sqrt(eX * eX + eY * eY);
    pxd = sqrt((Xn - Xc) * (Xn - Xc) + (Yn - Yc) * (Yn - Yc));
    if (px <= 0 || pxd <= 0)
        return 0;
    ex = eX / px;
    ey = eY / px;
    /* away from the eye along the water */
    fX = (Xc - Xn) / pxd;
    fY = (Yc - Yn) / pxd;
    q->pxs = sqrt(px * pxd);            /* the rings' fade (wr_rings_row) */
    if (pxd > px)
        px = pxd;
    q->X0 = X0;
    q->Y0 = Y0;
    q->eX = eX;
    q->eY = eY;
    q->ex = ex;
    q->ey = ey;
    q->fX = fX;
    q->fY = fY;
    q->nact = 0;
    if (wr_nring)
        wr_rings_row(q, w / 2 + 1);
    if (q->nact)
        any = 1;
    wind = game_442441 * (2 * M_PI / 65536.0);
    for (i = 0; i < WR_N; i++) {
        double a = wind + wr_waves[i].ang * (M_PI / 180), kk = 2 * M_PI / wr_waves[i].len;
        double kx = cos(a) * kk, ky = sin(a) * kk, ph, n = wr_waves[i].len / px, fade;
        fade = port_water_ripples ? (n - 6.0) / 6.0 : 0;     /* (Water effects alone: no waves) */
        if (fade <= 0) {
            q->ce[i] = q->cf[i] = 0;
            q->ph0[i] = q->dph[i] = 0;
            continue;
        }
        if (fade > 1)
            fade = 1;
        ph = kx * X0 + ky * Y0 - t * 2 * M_PI / wr_waves[i].per;
        ph -= floor(ph / (2 * M_PI)) * 2 * M_PI;
        q->ph0[i] = (ulong)(long long)(ph / (2 * M_PI) * 4294967296.0);
        q->dph[i] = (ulong)(long long)((kx * eX + ky * eY) / (2 * M_PI) * 4294967296.0);
        /* a tilt e turns the reflected ray by 2e: sideways 2e * the focal length in pixels, up
         * the ramp 2e of the ramp's 16 steps over wf_span */
        q->ce[i] = (long)(wr_waves[i].steep * fade * (cos(a) * ex + sin(a) * ey) * 2 * wf_fpx * 256);
        q->cf[i] = (long)(wr_waves[i].steep * fade * (cos(a) * fX + sin(a) * fY) * 2 * 256 / wf_span * 256 * WR_SHADE);
        any = 1;
    }
    return any;
}
#endif

#ifndef TN_WATCOM_ABI
/* Medium and High: rndsky_2A8658's work once the smooth ground is drawn too (ffpipe.c), on
 * every water pixel: the original reflects only in the column terrain beyond the smooth ground,
 * so close water showed none.  Row by row from the top: above the horizon the water colour flat, then the
 * picture mirrored at the horizon while the mirror row is in the view, then the sky the reflected
 * ray meets; the land only (below hview_cols: the sky above it has the water colours too).  The
 * sky colour over it by the angle below the horizon (water_full_setup). */
/* ---- "Water reflection: High": the reflected ray of each water pixel followed over the map's
 * heights and buildings to what it meets; that pixel in the view is the reflection.  Rays are
 * traced every 2 game pixels and the places between are interpolated.  Land the view doesn't show
 * (above its top) fades into the sky as in Medium.  The objects drawn this frame are reflected
 * from their own pixels (port_water_obj_begin and on).  The rows run on several threads
 * (port/core/workers.c). */
#define WT_N 0x200                      /* the fine grid's squares across */
static float wt_h[(WT_N + 1) * (WT_N + 1)];     /* its heights in squares */
static ulong wt_sum;
static float wt_max;
static double wt_f[3], wt_a[3], wt_b[3], wt_sx, wt_sy;   /* the camera: forward, per u, per v */

static float wt_ground(double x, double y);

/* the highest point in each block of 4 x 4 squares of the whole map (a coarse square): a ray above it crosses the block in one step */
#define WT_BN 520
static float wt_bm[WT_BN * WT_BN];
static long wt_bw, wt_bh, wt_bx0, wt_by0;       /* blocks across, down; square of block 0 */

static float wt_bm1[(WT_BN / 4 + 1) * (WT_BN / 4 + 1)], wt_bm2[(WT_BN / 16 + 1) * (WT_BN / 16 + 1)];
static long wt_bw1, wt_bw2;     /* the same per 16 and 64 squares */

static void wt_bmax(long bx, long by, float v)
{
    if (bx >= 0 && by >= 0 && bx < wt_bw && by < wt_bh && v > wt_bm[by * wt_bw + bx])
        wt_bm[by * wt_bw + bx] = v;
}

static void wt_blocks(void)
{
    long x, y, i;
    wt_bw = GMAP->w;
    wt_bh = GMAP->h;
    if (wt_bw > WT_BN)
        wt_bw = WT_BN;
    if (wt_bh > WT_BN)
        wt_bh = WT_BN;
    wt_bx0 = -4L * GMAP->x0;
    wt_by0 = -4L * GMAP->y0;
    for (i = 0; i < wt_bw * wt_bh; i++)
        wt_bm[i] = -1e9f;
    /* a corner counts in every block it touches */
    for (y = 0; y < GMAP->h; y++) {
        uchar *c = (uchar *)TN_LOWMEM(GMAP->rows1[y]);
        for (x = 0; x < GMAP->w; x++) {
            float v = MAP_CELL_H(c + x * 3) / 24.0f;
            wt_bmax(x, y, v);
            wt_bmax(x - 1, y, v);
            wt_bmax(x, y - 1, v);
            wt_bmax(x - 1, y - 1, v);
        }
    }
    for (y = 0; y <= WT_N; y++)
        for (x = 0; x <= WT_N; x++) {
            long fx = x - wt_bx0, fy = y - wt_by0;
            float v = wt_h[y * (WT_N + 1) + x];
            wt_bmax(fx >> 2, fy >> 2, v);
            if (!(fx & 3)) {
                wt_bmax((fx >> 2) - 1, fy >> 2, v);
                if (!(fy & 3))
                    wt_bmax((fx >> 2) - 1, (fy >> 2) - 1, v);
            }
            if (!(fy & 3))
                wt_bmax(fx >> 2, (fy >> 2) - 1, v);
        }
    for (i = 0; i < MAP_BLDGS; i++)
        if (!(map_bldgs[i].x == -1 && map_bldgs[i].y == -1)) {
            float v = map_bldgs[i].height / 24.0f;
            long fx = map_bldgs[i].x - wt_bx0, fy = map_bldgs[i].y - wt_by0;
            wt_bmax(fx >> 2, fy >> 2, v);
            wt_bmax((fx + 1) >> 2, fy >> 2, v);
            wt_bmax(fx >> 2, (fy + 1) >> 2, v);
            wt_bmax((fx + 1) >> 2, (fy + 1) >> 2, v);
        }
    wt_bw1 = (wt_bw + 3) / 4;
    wt_bw2 = (wt_bw + 15) / 16;
    for (i = 0; i < wt_bw1 * ((wt_bh + 3) / 4); i++)
        wt_bm1[i] = -1e9f;
    for (i = 0; i < wt_bw2 * ((wt_bh + 15) / 16); i++)
        wt_bm2[i] = -1e9f;
    for (y = 0; y < wt_bh; y++)
        for (x = 0; x < wt_bw; x++) {
            float v = wt_bm[y * wt_bw + x];
            if (v > wt_bm1[(y >> 2) * wt_bw1 + (x >> 2)])
                wt_bm1[(y >> 2) * wt_bw1 + (x >> 2)] = v;
            if (v > wt_bm2[(y >> 4) * wt_bw2 + (x >> 4)])
                wt_bm2[(y >> 4) * wt_bw2 + (x >> 4)] = v;
        }
}

static void wt_heights(void)
{
    ulong sum = 0;
    long x, y;
    float mx = -1e9f;
    for (y = 0; y <= WT_N; y += 7)
        for (x = 0; x <= WT_N; x += 5)
            sum = sum * 31 + (ushort)MAP_CELL_H((uchar *)TN_LOWMEM(GMAP->rows2[y]) + x * 3);
    for (x = 0; x < MAP_BLDGS; x++)       /* the buildings: their heights count */
        if (!(map_bldgs[x].x == -1 && map_bldgs[x].y == -1))
            sum = sum * 31 + (ushort)map_bldgs[x].height;
    if (sum == wt_sum && wt_max > -1e8f)
        return;
    for (x = 0; x < MAP_BLDGS; x++)
        if (!(map_bldgs[x].x == -1 && map_bldgs[x].y == -1) && map_bldgs[x].height / 24.0f > mx)
            mx = map_bldgs[x].height / 24.0f;
    wt_sum = sum;
    for (y = 0; y <= WT_N; y++) {
        uchar *c = (uchar *)TN_LOWMEM(GMAP->rows2[y]);
        for (x = 0; x <= WT_N; x++) {
            float v = MAP_CELL_H(c + x * 3) / 24.0f;
            wt_h[y * (WT_N + 1) + x] = v;
            if (v > mx)
                mx = v;
        }
    }
    /* the coarse grid around it too (land farther than the fine grid) */
    for (y = 0; y < GMAP->h; y++) {
        uchar *c = (uchar *)TN_LOWMEM(GMAP->rows1[y]);
        for (x = 0; x < GMAP->w; x++) {
            float v = MAP_CELL_H(c + x * 3) / 24.0f;
            if (v > mx)
                mx = v;
        }
    }
    wt_max = mx;
    wt_blocks();
}

/* the height anywhere on the map: the fine grid's, else the coarse grid's (map_2949C4); -1e9 off
 * the map */
static float wt_height(double x, double y)
{
    double cx, cy;
    fix X, Y;
    if (x >= 0 && y >= 0 && x < WT_N && y < WT_N) {
        float g = wt_ground(x, y);
        long xi = (long)x, yi = (long)y;
        /* a building's square (marked in the coarse grid, at the row the game marks it: the
         * row from x0) is a box as high as the building */
        uchar *cc = (uchar *)TN_LOWMEM(GMAP->rows1[(yi >> 2) + GMAP->x0]) + (GMAP->x0 + (xi >> 2)) * 3;
        if (*cc & 0x40) {
            float b = map_2950A0(((ulong)yi << 16) | (ulong)xi) / 24.0f;
            if (b > g)
                return b;
        }
        return g;
    }
    cx = x / 4 + GMAP->x0;
    cy = y / 4 + GMAP->y0;
    if (cx < 0 || cy < 0 || cx >= GMAP->w - 1 || cy >= GMAP->h - 1)
        return -1e9f;
    /* exactly 512 squares: map_2949C4 would read the fine grid's row or column 513 (there is
     * none: a crash) */
    X = (fix)(x * 65536.0);
    Y = (fix)(y * 65536.0);
    if (X == 0x2000000)
        X++;
    if (Y == 0x2000000)
        Y++;
    return map_2949C4(X, Y) / 65536.0f;
}

/* the ground's height at (x, y) squares, the two triangles of a square as map_2949C4 splits them */
static float wt_ground(double x, double y)
{
    long xi = (long)x, yi = (long)y;
    float fx = (float)(x - xi), fy = (float)(y - yi), h00, h01, h10, h11;
    const float *p = &wt_h[yi * (WT_N + 1) + xi];
    h00 = p[0];
    h01 = p[1];
    h10 = p[WT_N + 1];
    h11 = p[WT_N + 2];
    if ((xi + yi) % 2 == 0) {
        if (fx > fy)
            h10 = h11 + h00 - h01;
        else
            h01 = h11 + h00 - h10;
    } else if (fx + fy >= 1.0f)
        h00 = h10 + h01 - h11;
    return h00 + fx * (h01 - h00) + fy * (h10 - h00);
}

static void wt_camera(void)
{
    double sh = ff_view.sin_h / 65536.0, ch = ff_view.cos_h / 65536.0, sp = ff_view.sin_p / 65536.0,
           cp = ff_view.cos_p / 65536.0, sb = ff_view.sin_b / 65536.0, cb = ff_view.cos_b / 65536.0;
    /* as water_ray: components (map x, map y, height) */
    wt_f[0] = cp * ch;                  wt_f[1] = sh * cp;                  wt_f[2] = -sp;
    wt_a[0] = sb * sp * ch - cb * sh;   wt_a[1] = ch * cb + sb * sp * sh;   wt_a[2] = sb * cp;
    wt_b[0] = -sb * sh - cb * sp * ch;  wt_b[1] = ch * sb - cb * sp * sh;   wt_b[2] = -cb * cp;
    wt_sx = ff_cam.zx * 1.1 / 65536.0;
    wt_sy = ff_cam.zy * 1.1 / 65536.0;
}

/* the reflection of pixel (x, r): the view's pixel (*sx, *sy) of the land the reflected ray meets
 * and how far that is from the eye across the map (*d, squares); 0: none (the sky), 2: land above
 * the view's top (*sy < 0), 3: not known (the water point is under the land) */
static int wt_trace(double x, double r, double hw, double *sx, double *sy, double *d)
{
    double dx, dy, dz, X, Y, Z, t, step, len, cx = ff_view.x / 65536.0, cy = ff_view.y / 65536.0,
           cz = ff_view.z / 65536.0, ex, ey, ez, s, lo, hi = 1e9;
    int i;
    *d = 1e9;
    water_ray(x, r, &dx, &dy, &dz);
    if (dz > -1e-6)
        return 0;
    t = hw / -dz;
    X = cx + t * dx;
    Y = cy + t * dy;
    Z = cz - hw;
    len = sqrt(dx * dx + dy * dy);
    if (len < 1e-6)
        return 0;
    /* a water point under the land: a pixel by the shore whose ray meets the water's plane
     * behind the land */
    if (wt_height(X, Y) > Z + 0.05)
        return 3;
    /* the reflected ray, per square across */
    dx /= len;
    dy /= len;
    dz = -dz / len;
    step = 0.25 + t * len * 0.02;
    lo = 0;
    for (s = step; s < 600.0 && s < hi; lo = s, s += step, step *= 1.03) {
        double px = X + s * dx, py = Y + s * dy, pz = Z + s * dz, fx, fy, ex2, ey2;
        long bx, by;
        float g;
        if (pz > wt_max)
            break;
        fx = px - wt_bx0;
        fy = py - wt_by0;
        if (fx < 0 || fy < 0)
            break;
        bx = (long)fx >> 2;
        by = (long)fy >> 2;
        if (bx >= wt_bw || by >= wt_bh)
            break;
        {
            /* above the whole block of 64, 16 or 4 squares (and the ray climbs): on to where
             * it leaves it */
            long sh = pz > wt_bm2[(by >> 4) * wt_bw2 + (bx >> 4)] ? 6 :
                      pz > wt_bm1[(by >> 2) * wt_bw1 + (bx >> 2)] ? 4 : pz > wt_bm[by * wt_bw + bx] ? 2 : 0;
            if (sh) {
                double bs = (double)(1L << sh), ox = (double)(((long)fx >> sh) << sh), oy = (double)(((long)fy >> sh) << sh);
                ex2 = dx > 1e-9 ? (ox + bs - fx) / dx : dx < -1e-9 ? (ox - fx) / dx : 1e9;
                ey2 = dy > 1e-9 ? (oy + bs - fy) / dy : dy < -1e-9 ? (oy - fy) / dy : 1e9;
                ex2 = (ex2 < ey2 ? ex2 : ey2) + 0.01;
                if (ex2 > step)
                    s += ex2 - step;    /* the loop adds step */
                continue;
            }
        }
        g = wt_height(px, py);
        if (g < -1e8f)
            break;
        if (g >= pz) {
            for (hi = s, i = 0; i < 5; i++) {
                double m = (lo + hi) / 2;
                if (wt_height(X + m * dx, Y + m * dy) >= Z + m * dz)
                    hi = m;
                else
                    lo = m;
            }
            break;
        }
    }
    if (hi >= 1e8)
        return 0;
    ex = X + hi * dx - cx;
    ey = Y + hi * dy - cy;
    ez = Z + hi * dz - cz;
    s = ex * wt_f[0] + ey * wt_f[1] + ez * wt_f[2];
    if (s <= 0.01)
        return 0;
    *sx = ff_cam.half_w + (ex * wt_a[0] + ey * wt_a[1] + ez * wt_a[2]) / s * wt_sx;
    *sy = ff_cam.half_h + (ex * wt_b[0] + ey * wt_b[1] + ez * wt_b[2]) / s * wt_sy;
    *d = sqrt(ex * ex + ey * ey);
    return *sy < 0 ? 2 : 1;
}

/* the traced grid: per sample the view's pixel (fix 16.16 packed in two shorts * 4) or none */
#define WT_GX 1100
#define WT_GY 520
static short wt_gx[WT_GY][WT_GX], wt_gy[WT_GY][WT_GX];   /* 1/4 pixels; wt_gy WT_NONE or WT_BAD */
static float wt_gd[WT_GY][WT_GX];      /* how far the land met is across the map (squares), 1e9 none */
#define WT_NONE (-0x8000)               /* the sky */
#define WT_BAD (-0x7FFF)                /* not traced: no water there, or its water point under land */
static long wt_stx, wt_sty;

/* a water pixel within d of (x, r) along the row or the column */
static int wt_near_water(long x, long r, long d, long w, long h)
{
    uchar *bits = grd_canvas->bm.bits;
    long row = grd_canvas->bm.row;
    if (x >= w)
        x = w - 1;
    if (r >= h)
        r = h - 1;
    return sky_water[bits[r * row + x]] || (x + d - 1 < w && sky_water[bits[r * row + x + d - 1]]) ||
           (r + d - 1 < h && sky_water[bits[(r + d - 1) * row + x]]) ||
           (r >= d - 1 && sky_water[bits[(r - d + 1) * row + x]]) || (x >= d - 1 && sky_water[bits[r * row + x - d + 1]]);
}

static void wt_sample(long gx, long gy, long w, long h, double hw)
{
    double sx, sy, d;
    int k = wt_trace(gx * wt_stx + 0.5, gy * wt_sty + 0.5, hw, &sx, &sy, &d);
    wt_gd[gy][gx] = (float)d;
    if (k == 3) {
        wt_gy[gy][gx] = WT_BAD;
        return;
    }
    if (!k || sx < 0 || sx >= w || sy >= h) {
        wt_gy[gy][gx] = WT_NONE;
        wt_gd[gy][gx] = 1e9f;
        return;
    }
    if (sy < -0x1F00 / 4)
        sy = -0x1F00 / 4;
    wt_gx[gy][gx] = (short)(sx * 4);
    wt_gy[gy][gx] = (short)(sy * 4);
}

/* the grid's frame values for its parts (wt_grid_part, on several threads: pass 0 traces every
 * second sample, pass 1 then reads only those) */
static struct {
    double hw;
    long w, h, r0, nx, ny, pass;
} wg;

/* grid rows 4 * part .. +3.  The samples: every second one traced first; one between is traced
 * only where those around it differ (sky and land, or land far apart in the view), else it takes
 * theirs */
static void wt_grid_part(void *arg, int part, int thread)
{
    double hw = wg.hw;
    long gx, gy, nx = wg.nx, ny = wg.ny, pass = wg.pass, w = wg.w, h = wg.h, r0 = wg.r0;
    (void)arg;
    (void)thread;
    for (gy = part * 4; gy < part * 4 + 4 && gy < ny; gy++) {
        long r = gy * wt_sty;
        if (r < r0 || r >= h)
            continue;
        for (gx = 0; gx < nx; gx++) {
            long x = gx * wt_stx, nb[4][2], n = 0, i, sky = 0, land = 0, x0 = 0x7FFF, x1 = -0x8000,
                 y0 = 0x7FFF, y1 = -0x8000, sumx = 0, sumy = 0;
            float sumd = 0;
            int coarse = !(gx & 1) && !(gy & 1);
            if (pass == 0) {
                if (coarse && wt_near_water(x, r, 2 * wt_stx, w, h))
                    wt_sample(gx, gy, w, h, hw);
                continue;
            }
            if (coarse || !wt_near_water(x, r, wt_stx, w, h))
                continue;
            /* the traced samples around it */
            if (gy & 1) {
                if (gx & 1) {
                    nb[0][0] = gx - 1, nb[0][1] = gy - 1, nb[1][0] = gx + 1, nb[1][1] = gy - 1;
                    nb[2][0] = gx - 1, nb[2][1] = gy + 1, nb[3][0] = gx + 1, nb[3][1] = gy + 1;
                    n = 4;
                } else
                    nb[0][0] = gx, nb[0][1] = gy - 1, nb[1][0] = gx, nb[1][1] = gy + 1, n = 2;
            } else
                nb[0][0] = gx - 1, nb[0][1] = gy, nb[1][0] = gx + 1, nb[1][1] = gy, n = 2;
            for (i = 0; i < n; i++) {
                short c;
                if (nb[i][0] < 0 || nb[i][0] >= nx || nb[i][1] < 0 || nb[i][1] >= ny ||
                    (c = wt_gy[nb[i][1]][nb[i][0]]) == WT_BAD)
                    break;
                if (c == WT_NONE)
                    sky++;
                else {
                    short cx = wt_gx[nb[i][1]][nb[i][0]];
                    land++;
                    sumx += cx;
                    sumy += c;
                    sumd += wt_gd[nb[i][1]][nb[i][0]];
                    x0 = cx < x0 ? cx : x0;
                    x1 = cx > x1 ? cx : x1;
                    y0 = c < y0 ? c : y0;
                    y1 = c > y1 ? c : y1;
                }
            }
            if (i == n) {
                if (sky == n) {
                    wt_gy[gy][gx] = WT_NONE;
                    wt_gd[gy][gx] = 1e9f;
                    continue;
                }
                if (land == n && x1 - x0 <= 4 * 3 * wt_stx && y1 - y0 <= 4 * 3 * wt_sty) {
                    wt_gx[gy][gx] = (short)(sumx / n);
                    wt_gy[gy][gx] = (short)(sumy / n);
                    wt_gd[gy][gx] = sumd / n;
                    continue;
                }
            }
            wt_sample(gx, gy, w, h, hw);
        }
    }
}

static int wt_grid(long w, long h, long r0)
{
    long gx, gy, nx = w / wt_stx + 2, ny = h / wt_sty + 2;
    if (nx > WT_GX)
        nx = WT_GX;
    if (ny > WT_GY)
        ny = WT_GY;
    for (gy = 0; gy < ny; gy++)
        for (gx = 0; gx < nx; gx++)
            wt_gy[gy][gx] = WT_BAD;
    wg.hw = (ff_view.z - wf_wz) / 65536.0 / WF_HSCALE;
    if (wg.hw <= 0)
        return 0;
    wg.w = w;
    wg.h = h;
    wg.r0 = r0;
    wg.nx = nx;
    wg.ny = ny;
    for (wg.pass = 0; wg.pass < 2; wg.pass++)
        port_parallel(wt_grid_part, NULL, (int)((ny + 3) / 4));
    return 1;
}

/* the view's pixels for row r from the grid, into rx / ry (ry WT_NONE: the sky):
 * bilinear where all four samples meet land, else the nearest sample that does or shows sky (a
 * sample whose water point is under the land is skipped), its land carried on per pixel as a
 * mirror carries it; WT_BAD when all four are (a shore
 * square's water colours on a slope: those are left as drawn).  With
 * dist, how far the land is too (rd, 1e9 none): the objects' reflections are in front of it
 * or not */

static void wt_row(long r, long w, int dist, short *rx, short *ry, float *rd)
{
    long gy = r / wt_sty, fy = r - gy * wt_sty, gx, x = 0, n = wt_stx * wt_sty * 4;
    long inv = (65536 + n / 2) / n;
    const short *ga = wt_gx[gy], *gb = wt_gx[gy + 1], *ya = wt_gy[gy], *yb = wt_gy[gy + 1];
    for (gx = 0; x < w; gx++) {
        short y00 = ya[gx], y01 = ya[gx + 1], y10 = yb[gx], y11 = yb[gx + 1];
        long fx, end = x + wt_stx < w ? x + wt_stx : w, x0 = x;
        if (y00 > WT_BAD && y01 > WT_BAD && y10 > WT_BAD && y11 > WT_BAD) {
            long ax = ga[gx] * (wt_sty - fy) + gb[gx] * fy, bx = ga[gx + 1] * (wt_sty - fy) + gb[gx + 1] * fy;
            long ay = y00 * (wt_sty - fy) + y10 * fy, by = y01 * (wt_sty - fy) + y11 * fy;
            for (fx = 0; x < end; x++, fx++) {
                rx[x] = (short)(((ax * (wt_stx - fx) + bx * fx) * inv) >> 16);
                ry[x] = (short)(((ay * (wt_stx - fx) + by * fx) * inv) >> 16);
            }
            if (dist) {
                float vy = (float)fy / wt_sty, ad = wt_gd[gy][gx] + (wt_gd[gy + 1][gx] - wt_gd[gy][gx]) * vy,
                      bd = wt_gd[gy][gx + 1] + (wt_gd[gy + 1][gx + 1] - wt_gd[gy][gx + 1]) * vy, st = (bd - ad) / wt_stx;
                for (x = x0; x < end; x++, ad += st)
                    rd[x] = ad;
            }
            continue;
        }
        for (fx = 0; x < end; x++, fx++) {
            /* the nearest corner first, then the others */
            long ix = fx * 2 >= wt_stx, iy = fy * 2 >= wt_sty, k;
            short vy = WT_NONE, vx = 0;
            float vd = 1e9f;
            for (k = 0; k < 4; k++) {
                long cx = gx + (ix ^ (k & 1)), cy = gy + (iy ^ (k >> 1));
                short c = wt_gy[cy][cx];
                if (c == WT_BAD)
                    continue;
                vy = c;
                vx = wt_gx[cy][cx];
                vd = c == WT_NONE ? 1e9f : wt_gd[cy][cx];
                break;
            }
            if (k < 4 && vy != WT_NONE) {
                /* land: from the corners that meet land, each moved as a mirror moves its land
                 * (a pixel to the right shows the land to the right, a pixel higher up the land
                 * lower down), weighted by nearness.  One corner's land for the whole half cell
                 * made flat blocks along far shores */
                long j, sw = 0, sx = 0, sy = 0;
                for (j = 0; j < 4; j++) {
                    long cx = gx + (j & 1), cy = gy + (j >> 1), wg;
                    short c = wt_gy[cy][cx];
                    if (c <= WT_BAD)
                        continue;
                    wg = ((j & 1) ? fx + 1 : wt_stx - fx) * ((j >> 1) ? fy + 1 : wt_sty - fy);
                    sx += wg * (wt_gx[cy][cx] + 4 * (x - cx * wt_stx));
                    sy += wg * (c - 4 * (r - cy * wt_sty));
                    sw += wg;
                }
                vx = (short)(sx / sw);
                vy = (short)(sy / sw);
            }
            rx[x] = vx >> 2;
            ry[x] = vy == WT_NONE ? (k == 4 ? WT_BAD : WT_NONE) : vy >= 0 ? vy >> 2 : -((-vy + 3) >> 2);
            rd[x] = vd;
        }
    }
}

/* High: the objects' reflections (units, vehicles, boats, trees, explosions, splashes).  Each
 * object the view draws (viewdist_2A2284) is caught by its pixels: the screen around its
 * rectangle before and after (port_water_obj_begin / _end).  A 3D model (a unit, a vehicle, a
 * boat, a building) is then drawn once more from the eye mirrored in the water's plane (the
 * view's matrix reflected in height and its columns flipped, so the polygons keep their turn),
 * into an offscreen picture whose columns are read back flipped: what the water shows of it,
 * underside and all.  A flat sprite (a tree, an explosion, a splash) is flipped from its own
 * pixels along the upright line through it, the rows each at their height mirrored.  The water
 * pass shows an object's reflection where nothing the reflected ray meets is nearer (wt_row), and
 * the land the ray meets is read from the screen as it was behind any object drawn over it.
 * Nothing changes the frame's drawing outside the water: the objects are drawn into
 * port-only buffers. */
#define WO_MAX 255
#define WO_POOL (4L << 20)
#define WO_S 0xFF                       /* the offscreen picture's blank (the colour the game's own
                                         * probe marks a pixel with: nothing draws it) */
typedef struct {
    short x0, y0, w, h;                 /* its pixels' region on the screen */
    short rx0, ry0, rx1, ry1;           /* where its reflection went (wo_rid); rx1 < rx0 none */
    long pool;                          /* its region in wo_pb / wo_pa / wo_pf */
    float d, dmin;                      /* how far it is across the map, its nearest side */
    double s0, a0, b0;                  /* its upright line in the camera (wt_camera) */
} WO;
static WO wo[WO_MAX];
static long wo_n, wo_used, wo_w, wo_h, wo_cur = -1, wo_seen;
static fix wo_wz;                       /* the plane the model being drawn again is mirrored in */
#define WO_FEET1 0.25                   /* a unit's feet up to this high above the water (in its
                                         * heights: the eye stands about 0.34 above the feet)... */
#define WO_FEET2 0.5                    /* ...are its mirror's plane, and from here up the water */
static int wo_on, wo_any, wo_blank;
static long wo_mx0, wo_my0, wo_mx1, wo_my1;     /* the mirrored drawing's region (its columns) */
/* per screen pixel: the last object drawn there (1..), its reflection's object and colour, the
 * offscreen picture */
static uchar wo_last[4096 * WF_ROWS], wo_rid[4096 * WF_ROWS], wo_rc[4096 * WF_ROWS], wo_mir[4096 * WF_ROWS];
/* per object pixel: the screen before (the background: what was there before any object), after,
 * whether the object drew it */
static uchar wo_pb[WO_POOL], wo_pa[WO_POOL], wo_pf[WO_POOL];
static grs_canvas wo_canvas;
static g3s_matrix wo_sv_view, wo_sv_unsc, wo_sv_wtoo;
static g3s_vector wo_sv_pos, wo_sv_wtoop;
static ulong wo_sv_light[27];
static long wo_sv_depth;
static void *wo_sv_cb[16], *wo_sv_mstk, *wo_sv_cstk;
long port_water_low = -0x7FFFFFFF;

/* the start of a frame of the sharp view (ffpipe_29E2D8): last frame's marks off */
void port_water_frame(void)
{
    long i, y, w = grd_canvas->bm.w, h = grd_canvas->bm.h;
    for (i = 0; i < wo_n; i++) {
        WO *o = &wo[i];
        for (y = o->y0; y < o->y0 + o->h; y++)
            memset(wo_last + y * wo_w + o->x0, 0, o->w);
        if (o->rx1 >= o->rx0)
            for (y = o->ry0; y <= o->ry1; y++)
                memset(wo_rid + y * wo_w + o->rx0, 0, o->rx1 - o->rx0 + 1);
    }
    wo_n = wo_used = 0;
    wo_cur = -1;
    wo_on = wo_any = 0;
    if (wo_seen > 0)
        wo_seen--;
    /* only while the water is seen ("High" found it in the last frames: its height is known) */
    if (!port_s3d_on || port_water_full != 2 || !wf_wz_ok || !wo_seen || w > 4096 || h > WF_ROWS)
        return;
    if (!wo_blank) {
        memset(wo_mir, WO_S, sizeof wo_mir);
        wo_blank = 1;
    }
    wo_w = w;
    wo_h = h;
    wt_camera();
    wo_on = 1;
}

/* object o's upright line: the height (camera's, squares above the eye) at view row y */
static int wo_height(const WO *o, double y, double *t)
{
    double k = (y - ff_cam.half_h) / wt_sy, den = wt_b[2] - k * wt_f[2];
    if (den > -1e-9 && den < 1e-9)
        return 0;
    *t = (k * o->s0 - o->b0) / den;
    return 1;
}

/* the view's row and column of height t on o's line, and its depth */
static int wo_point(const WO *o, double t, double *x, double *y, double *s)
{
    *s = o->s0 + wt_f[2] * t;
    if (*s < 0.05)
        return 0;
    *x = ff_cam.half_w + (o->a0 + wt_a[2] * t) / *s * wt_sx;
    *y = ff_cam.half_h + (o->b0 + wt_b[2] * t) / *s * wt_sy;
    return 1;
}

/* where view point (x, y) of o is seen in the water: its height on the line mirrored in the
 * water's plane, the column scaled as far as the depth changes; 0 under the water */
static int wo_flip(const WO *o, double x, double y, double tw, double *fx, double *fy)
{
    double t, cx, cy, s, cx2, s2;
    if (!wo_height(o, y, &t) || !wo_point(o, t, &cx, &cy, &s))
        return 0;
    if (t < tw)
        t = tw;
    if (!wo_point(o, 2 * tw - t, &cx2, fy, &s2))
        return 0;
    *fx = cx2 + (x - cx) * s / s2;
    return 1;
}

static void wo_put(WO *o, long x, long y, uchar c)
{
    long p = y * wo_w + x;
    uchar id = wo_rid[p];
    if (id && wo[id - 1].d < o->d)
        return;                         /* a nearer object's reflection is there */
    wo_rid[p] = (uchar)(o - wo + 1);
    wo_rc[p] = c;
    if (x < o->rx0)
        o->rx0 = (short)x;
    if (x > o->rx1)
        o->rx1 = (short)x;
    if (y < o->ry0)
        o->ry0 = (short)y;
    if (y > o->ry1)
        o->ry1 = (short)y;
    wo_any = 1;
}

/* the screen around an object about to be drawn (rect: its rectangle in the view, inclusive) */
void port_water_obj_begin(const void *ent, const short *rect)
{
    const GameEntity *e = ent;
    long x0 = rect[0] - 2, y0 = rect[1] - 2, x1 = rect[2] + 2, y1 = rect[3] + 2, y, row = grd_canvas->bm.row;
    double vx, vy, vz, d, s, rad;
    WO *o;
    wo_cur = -1;
    port_water_low = -0x7FFFFFFF;
    if (!wo_on || wo_n >= WO_MAX)
        return;
    vx = e->x / 65536.0 - ff_view.x / 65536.0;
    vy = e->y / 65536.0 - ff_view.y / 65536.0;
    vz = e->z / 65536.0 - ff_view.z / 65536.0;
    d = sqrt(vx * vx + vy * vy);
    if (d > 64.0 || (e->z - wf_wz) > 3 * 65536)
        return;                         /* too far, or high above the water: not seen in it */
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 >= wo_w ? wo_w - 1 : x1;
    y1 = y1 >= wo_h ? wo_h - 1 : y1;
    if (x1 < x0 || y1 < y0 || wo_used + (x1 - x0 + 1) * (y1 - y0 + 1) > WO_POOL)
        return;
    o = &wo[wo_n];
    o->x0 = (short)x0;
    o->y0 = (short)y0;
    o->w = (short)(x1 - x0 + 1);
    o->h = (short)(y1 - y0 + 1);
    o->pool = wo_used;
    o->rx0 = o->ry0 = 0x7FFF;
    o->rx1 = o->ry1 = -1;
    o->s0 = vx * wt_f[0] + vy * wt_f[1];
    o->a0 = vx * wt_a[0] + vy * wt_a[1];
    o->b0 = vx * wt_b[0] + vy * wt_b[1];
    s = o->s0 + vz * wt_f[2];
    /* its nearest side: half as wide as its rectangle at its depth (and a little) */
    rad = (rect[2] - rect[0] + 1) * (s > 0.1 ? s : 0.1) / wt_sx / 2;
    o->d = (float)d;
    o->dmin = (float)(d - (rad < 8 ? rad : 8) - 0.3);
    for (y = y0; y <= y1; y++)
        memcpy(wo_pb + o->pool + (y - y0) * o->w, grd_canvas->bm.bits + y * row + x0, o->w);
    wo_cur = wo_n;
}

/* sprite o flipped from its own pixels (bbox px0..px1, py0..py1) */
static void wo_sprite(WO *o, long px0, long py0, long px1, long py1)
{
    double tw = (wf_wz - ff_view.z) / 65536.0, ttop, tbot, k, t, t2, cs, ct, ys, s, s2, q, dum;
    long r, r0, r1, x, xa, xb;
    if (!wo_height(o, py0, &ttop) || !wo_height(o, py1 + 1, &tbot) || ttop <= tw)
        return;                         /* all of it under the water's plane */
    if (tbot < tw)
        tbot = tw;
    if (!wo_point(o, 2 * tw - tbot, &dum, &k, &s) || !wo_point(o, 2 * tw - ttop, &dum, &ys, &s))
        return;
    r0 = (long)floor(k < ys ? k : ys);
    r1 = (long)ceil(k < ys ? ys : k);
    r0 = r0 < 0 ? 0 : r0;
    r1 = r1 >= wo_h ? wo_h - 1 : r1;
    for (r = r0; r <= r1; r++) {
        /* the height of row r on the line under the water, the one above it it mirrors */
        if (!wo_height(o, r + 0.5, &t2) || t2 > tw)
            continue;
        t = 2 * tw - t2;
        if (!wo_point(o, t, &cs, &ys, &s) || !wo_point(o, t2, &ct, &dum, &s2))
            continue;
        if (ys < py0 || ys >= py1 + 1)
            continue;
        q = s2 / s;
        xa = (long)floor(ct + (px0 - cs) / q);
        xb = (long)ceil(ct + (px1 + 1 - cs) / q);
        xa = xa < 0 ? 0 : xa;
        xb = xb >= wo_w ? wo_w - 1 : xb;
        {
            long yy = (long)ys, base = o->pool + (yy - o->y0) * o->w - o->x0;
            for (x = xa; x <= xb; x++) {
                long xs = (long)floor(cs + (x + 0.5 - ct) * q);
                if (xs < px0 || xs > px1 || !wo_pf[base + xs])
                    continue;
                wo_put(o, x, r, wo_pa[base + xs]);
            }
        }
    }
}

/* the object just drawn: its pixels (and the background behind them); kind 1: a 3D model, then
 * 1 back when it is to be drawn again from under the water (between this and
 * port_water_mirror_done); kind 2: a flat sprite */
int port_water_obj_end(const void *ent, int kind)
{
    WO *o;
    long x, y, n = 0, id, row = grd_canvas->bm.row, px0 = 0x7FFF, py0 = 0x7FFF, px1 = -1, py1 = -1;
    ulong *ls;
    (void)ent;
    if (wo_cur < 0)
        return 0;
    o = &wo[wo_cur];
    wo_cur = -1;
    id = wo_n + 1;
    for (y = o->y0; y < o->y0 + o->h; y++) {
        uchar *sp = grd_canvas->bm.bits + y * row, *last = wo_last + y * wo_w;
        long i = o->pool + (y - o->y0) * o->w - o->x0;
        for (x = o->x0; x < o->x0 + o->w; x++) {
            uchar a = sp[x], b = wo_pb[i + x], l = last[x];
            if (a == b) {
                wo_pf[i + x] = 0;
                continue;
            }
            wo_pf[i + x] = 1;
            wo_pa[i + x] = a;
            if (l) {
                /* drawn over an object still there: the background is that object's */
                const WO *p = &wo[l - 1];
                long j = p->pool + (y - p->y0) * p->w + x - p->x0;
                if (wo_pf[j] && wo_pa[j] == b)
                    wo_pb[i + x] = wo_pb[j];
            }
            last[x] = (uchar)id;
            n++;
            px0 = x < px0 ? x : px0;
            px1 = x > px1 ? x : px1;
            py0 = y < py0 ? y : py0;
            py1 = y > py1 ? y : py1;
        }
    }
    if (!n)
        return 0;                       /* nothing of it in the view */
    wo_used += (long)o->w * o->h;
    wo_n++;
    if (kind == 2) {
        wo_sprite(o, px0, py0, px1, py1);
        return 0;
    }
    if (kind != 1)
        return 0;
    {
        /* the plane it is mirrored in: the water's, but a unit wading in it (a skeleton: its
         * lowest joint) has its feet a little above the water, more and less with each step, and
         * mirrored in the water its reflection began below its feet and jumped as it walked; so
         * its feet's height while they are just above the water, back to the water's by degrees
         * higher up */
        double g = port_water_low > -0x7FFFFFFF ? (-port_water_low - wf_wz) / 65536.0 : 0;
        wo_wz = wf_wz;
        if (g > 0 && g < WO_FEET2)
            wo_wz += (fix)(65536 * (g <= WO_FEET1 ? g : WO_FEET1 * (WO_FEET2 - g) / (WO_FEET2 - WO_FEET1)));
    }
    {
        /* the region its reflection may reach: its rectangle's corners flipped, and around them
         * (it is solid, not on its line: half its size more) */
        double tw = (wo_wz - ff_view.z) / 65536.0, fx, fy, xa = 1e9, xb = -1e9, ya = 1e9, yb = -1e9;
        long c, nn = (2 * _biasx) >> 16, mx = (px1 - px0) / 2 + 8, my = (py1 - py0) / 2 + 8;
        for (c = 0; c < 4; c++) {
            if (!wo_flip(o, c & 1 ? px1 + 1 : px0, c & 2 ? py1 + 1 : py0, tw, &fx, &fy))
                return 0;
            xa = fx < xa ? fx : xa;
            xb = fx > xb ? fx : xb;
            ya = fy < ya ? fy : ya;
            yb = fy > yb ? fy : yb;
        }
        if (xb - xa > 4 * wo_w || yb - ya > 4 * wo_h)
            return 0;
        xa -= mx;
        xb += mx;
        ya -= my;
        yb += my;
        if (ya < 0)
            ya = 0;
        if (yb > wo_h - 1)
            yb = wo_h - 1;
        /* in the offscreen picture's flipped columns */
        wo_mx0 = nn - 1 - (long)ceil(xb);
        wo_mx1 = nn - 1 - (long)floor(xa);
        wo_my0 = (long)floor(ya);
        wo_my1 = (long)ceil(yb);
        if (wo_mx0 < 0)
            wo_mx0 = 0;
        if (wo_mx1 > wo_w - 1)
            wo_mx1 = wo_w - 1;
        if (wo_my1 > wo_h - 1)
            wo_my1 = wo_h - 1;
        if (wo_mx1 < wo_mx0 || wo_my1 < wo_my0)
            return 0;
    }
    /* the eye under the water: the height column reflected (y in, through the water's plane)
     * and the columns flipped (x out); the light stays the world's */
    wo_sv_view = view_matrix;
    wo_sv_unsc = unscaled_matrix;
    wo_sv_pos = _view_position;
    wo_sv_wtoo = _wtoo_matrix;
    wo_sv_wtoop = _wtoo_position;
    for (x = 0, ls = (ulong *)&_g3d_light_src; x < 27; x++)  /* as save_context: 27 dwords */
        wo_sv_light[x] = ls[x];
    wo_sv_depth = g3d_stack_depth;
    wo_sv_mstk = g3d_matrix_stackp;
    wo_sv_cstk = g3d_context_stackp;
    memcpy(wo_sv_cb, g3d_interp_callbacks, sizeof wo_sv_cb);
    memset(g3d_interp_callbacks, 0, sizeof wo_sv_cb);   /* no effects made by its drawing again */
#define WO_MIRROR(m) ((m).m1 = -(m).m1, (m).m5 = -(m).m5, (m).m6 = -(m).m6, (m).m7 = -(m).m7)
    WO_MIRROR(view_matrix);
    WO_MIRROR(unscaled_matrix);
#undef WO_MIRROR
    _view_position.y = 2 * -wo_wz - _view_position.y;
    wo_canvas.bm = grd_canvas->bm;
    wo_canvas.bm.bits = wo_mir;
    wo_canvas.bm.row = wo_w;
    wo_canvas.gc = grd_canvas->gc;
    wo_canvas.gc.clip.f.left = wo_mx0 << 16;
    wo_canvas.gc.clip.f.top = wo_my0 << 16;
    wo_canvas.gc.clip.f.right = (wo_mx1 + 1) << 16;
    wo_canvas.gc.clip.f.bot = (wo_my1 + 1) << 16;
    wo_canvas.ytab = NULL;
    gr_push_canvas(&wo_canvas);
    return 1;
}

/* the model drawn from under the water: its pixels into the reflections, the picture blank again */
void port_water_mirror_done(void)
{
    WO *o = &wo[wo_n - 1];
    long y, x, nn = (2 * _biasx) >> 16;
    ulong *ls;
    gr_pop_canvas();
    view_matrix = wo_sv_view;
    unscaled_matrix = wo_sv_unsc;
    _view_position = wo_sv_pos;
    _wtoo_matrix = wo_sv_wtoo;
    _wtoo_position = wo_sv_wtoop;
    for (x = 0, ls = (ulong *)&_g3d_light_src; x < 27; x++)
        ls[x] = wo_sv_light[x];
    g3d_stack_depth = wo_sv_depth;
    g3d_matrix_stackp = wo_sv_mstk;
    g3d_context_stackp = wo_sv_cstk;
    memcpy(g3d_interp_callbacks, wo_sv_cb, sizeof wo_sv_cb);
    for (y = wo_my0; y <= wo_my1; y++) {
        uchar *p = wo_mir + y * wo_w;
        for (x = wo_mx0; x <= wo_mx1; x++) {
            long t;
            if ((x & 7) == 0 && x + 8 <= wo_mx1 + 1) {
                unsigned long long v;
                memcpy(&v, p + x, 8);
                if (v == ~0ULL) {       /* blank (WO_S) eight at a time */
                    x += 7;
                    continue;
                }
            }
            if (p[x] == WO_S)
                continue;
            t = nn - 1 - x;
            if (t >= 0 && t < wo_w)
                wo_put(o, t, y, p[x]);
            p[x] = WO_S;
        }
    }
}

/* Medium mirrors the picture's rows above the horizon, but above the land's top in a column
 * (hview_cols) it took the sky's reflection, so whatever stands higher than the land behind it
 * (a wall, a tower, a unit by the shore) was cut there, along the far hills' outline behind it,
 * and the cut jumped as that outline moved with the view.  The sky as drawn is kept before the
 * objects standing above the land are drawn over it (port_water_sky_keep, from ffpipe.c), so a
 * pixel there that differs from it is an object's and is mirrored like the land */
static uchar wf_sky[WF_ROWS * 4096];
static long wf_sky_rows, wf_sky_w;

void port_water_sky_keep(void)
{
    long w = grd_canvas->bm.w, h = grd_canvas->bm.h, row = grd_canvas->bm.row, m = 0, i;
    uchar *bits = grd_canvas->bm.bits;
    wf_sky_rows = 0;
    if (w > 4096 || h > WF_ROWS)
        return;
    for (i = 0; i < w; i++)
        if (hview_cols[i] > m)
            m = hview_cols[i];
    if (m > wf_top)
        m = wf_top;                     /* the mirror reads only rows above the horizon */
    if (m > h)
        m = h;
    for (i = 0; i < m; i++)
        memcpy(wf_sky + i * w, bits + i * row, w);
    wf_sky_rows = m;
    wf_sky_w = w;
}

/* the water pass's frame values, for its rows on several threads (water_rows) */
static struct {
    long w, h, row, top, bot, fade;
    uchar *bits;
    int rip, full, objs;
} wp;

/* each thread's rows of the reflected sky, wave field and traced places, its row's waves */
typedef struct {
    uchar ring[3][4096 + 2];
    short fdx[2048 + 2], fd16[2048 + 2], frg[2048 + 2], ffo[2048 + 2];
    short rx[4096], ry[4096];
    float rd[4096];
    WrRow q;
} WaterTh;
static WaterTh wth[PORT_THREADS];

/* rows r0 .. r1 - 1, from the mirror's start (wf_r0) on: each reads only the rows above the
 * horizon (done first) and, in High, rows above it only where they show no water (which no row's
 * work changes), so the threads' rows don't depend on each other */
static void water_rows(long r0, long r1, WaterTh *t)
{
    long w = wp.w, row = wp.row, fade = wp.fade, x, r, got = -0x10000, frow = -2;
    uchar *bits = wp.bits;
    int rip = wp.rip, full = wp.full, objs = wp.objs, fany = 0;
    short *fdx = t->fdx, *fd16 = t->fd16, *frg = t->frg, *ffo = t->ffo, *rx = t->rx, *ry = t->ry;
    float *rd = t->rd;
    WrRow *q = &t->q;
    for (r = r0; r < r1; r++) {
        uchar *p = bits + r * row;
        const uchar *bay = wf_bayer[r & 3];
        /* rows r - 1 .. r + 1 of the reflected sky (the shimmer moves a row up or down);
         * the land mirrored over it while the mirror row is in the view: the sky drawn there
         * is not used, so the sky has no seam where the mirror leaves the view */
        long mr = 2 * wf_top - r, i, t16 = wf_t16[r];
        uchar *src = r < wp.bot ? bits + mr * row : NULL;
        if (got < r - 1)
            got = r - 2;
        for (; got < r + 1; got++)
            water_full_row(t->ring[(got + 4) % 3], got + 1, w);
        if (full)
            wt_row(r, w, wo_any, rx, ry, rd);
        for (x = 0; x < w; x++) {
            uchar c = p[x], s;
            long dxp, k, xs, sx, m;
            if (!sky_water[c] || r < hview_cols[x])
                continue;
            if (rip && frow != r && (frow != r - 1 || !(r & 1))) {
                /* the wave field every second column, made on even rows and kept for the next */
                long xx;
                frow = r;
                fany = ripple_row(q, r, w);
                if (fany) {
                    ulong ph[WR_N], d2[WR_N];
                    for (i = 0; i < WR_N; i++)
                        ph[i] = q->ph0[i], d2[i] = q->dph[i] * 2;
                    for (xx = 0; xx <= w / 2 + 1; xx++) {
                        long sdx = 0, sd = 0;
                        for (i = 0; i < WR_N; i++) {
                            long cs = wr_cos[ph[i] >> 22];
                            sdx += q->ce[i] * cs;
                            sd += q->cf[i] * cs;
                            ph[i] += d2[i];
                        }
                        sdx >>= 20;
                        sd >>= 20;
                        if (q->nact) {
                            /* a tilt e: sideways 2e * the focal length, up the ramp 2e of its
                             * 16 steps over wf_span (as ripple_row's waves) */
                            double ea = 0, fa = 0, ga = 0, fo = 0;
                            wr_ring_tilt(q, xx, &ea, &fa, &ga, &fo);
                            sdx += (long)(ea * 2 * wf_fpx);
                            sd += (long)(fa * 2 * 256 / wf_span * WR_SHADE);
                            ga *= WR_RBAND;
                            frg[xx] = (short)(ga < -40 ? -40 : ga > 40 ? 40 : ga);
                            ffo[xx] = (short)(fo > 16 ? 16 : fo);
                        } else
                            frg[xx] = ffo[xx] = 0;
                        fdx[xx] = (short)(sdx < -64 ? -64 : sdx > 64 ? 64 : sdx);
                        fd16[xx] = (short)(sd < -0x7000 ? -0x7000 : sd > 0x7000 ? 0x7000 : sd);
                    }
                }
            }
            if (!fany) {
                dxp = 0;
                k = (t16 + bay[x & 3]) >> 4;
            } else {
                dxp = fdx[x >> 1];
                k = (t16 + fd16[x >> 1] + bay[x & 3]) >> 4;
            }
            k = k < 0 ? 0 : k > 15 ? 15 : k;
            xs = x + dxp;
            xs = xs < 0 ? 0 : xs >= w ? w - 1 : xs;
            sx = xs + wf_dx[c];
            sx = sx < 0 ? 0 : sx >= w ? w - 1 : sx;
            m = mr + wf_dy[c];
            if (wo_any) {
                /* High: an object's reflection, when nothing the ray meets is nearer */
                uchar id = wo_rid[r * w + xs];
                if (id && wo[id - 1].dmin < rd[x]) {
                    p[x] = WR_PX(wf_tab[k][wo_rc[r * w + xs]]);
                    continue;
                }
            }
            /* the land mirrored while its mirror row is in the view, faded into the sky over
             * the top rows (a 4x4 dither), so land cut by the view's top doesn't end in a line */
            if (full && ry[x] == WT_BAD)
                continue;               /* High: its water point is under the land (water colours
                                         * of a shore square on a slope): left as drawn */
            if (full) {
                /* High: the land the reflected ray meets, where the view shows it */
                long tx = rx[x] + dxp, ty = ry[x];
                if (ty != WT_NONE) {
                    if (tx >= 0 && tx < w && ty < r && (ty >= fade || (ty >= 0 && ty * 16 > bay[(x + 1) & 3] * fade))) {
                        uchar v = bits[ty * row + tx], o = objs ? wo_last[ty * w + tx] : 0;
                        if (o) {
                            /* an object drawn over it: the land behind it */
                            const WO *ob = &wo[o - 1];
                            long j = ob->pool + (ty - ob->y0) * ob->w + tx - ob->x0;
                            if (wo_pf[j] && wo_pa[j] == v)
                                v = wo_pb[j];
                        }
                        if (!sky_water[v] || ty < hview_cols[tx]) {
                            p[x] = WR_PX(wf_tab[k][v]);
                            continue;
                        }
                    }
                }
            } else if (src && (m >= hview_cols[sx] ||
                               (m >= 0 && m < wf_sky_rows && src[xs + wf_off[c]] != wf_sky[m * w + sx])) &&
                       (m >= fade || m * 16 > bay[(x + 1) & 3] * fade)) {
                /* the land, or an object standing above it (port_water_sky_keep) */
                p[x] = WR_PX(wf_tab[k][src[xs + wf_off[c]]]);
                continue;
            }
            s = t->ring[(r + wf_dy[c] + 3) % 3][xs + 1 + wf_dx[c]];
            p[x] = WR_PX(wf_tab[k][s]);
        }
    }
}

/* rows of 32 from an even row (the wave field of an odd row is its even row's) */
#define WP_ROWS 16

static void water_part(void *arg, int part, int thread)
{
    long base = wf_r0 & ~1L, r0 = base + part * WP_ROWS, r1 = r0 + WP_ROWS;
    (void)arg;
    water_rows(r0 < wf_r0 ? wf_r0 : r0, r1 < wp.h ? r1 : wp.h, &wth[thread]);
}

/* rows WP_ROWS * part .. above the horizon: the water colour flat (on land the view shows higher
 * than the eye; the sky above it has the water's colours too) */
static void water_flat_part(void *arg, int part, int thread)
{
    long r, x, w = wp.w, r1 = (part + 1) * WP_ROWS < wp.top ? (part + 1) * WP_ROWS : wp.top;
    uchar flat = planet_tab38[0x3BF];
    (void)arg;
    (void)thread;
    for (r = part * WP_ROWS; r < r1; r++) {
        uchar *p = wp.bits + r * wp.row;
        for (x = 0; x < w; x++)
            if (sky_water[p[x]] && r >= hview_cols[x])
                p[x] = flat;
    }
}

void port_water_full_pass(void)
{
    long w = grd_canvas->bm.w, h = grd_canvas->bm.h, row = grd_canvas->bm.row, x, r;
    long top = sky_refl_top;
    uchar *bits = grd_canvas->bm.bits;
    int rip = 0, full = 0;
    if (h > WF_ROWS)
        h = WF_ROWS;
    if (w > 4096)
        w = 4096;
    if (port_water_ripples || port_water_fx || port_water_full == 2) {
        if (!wr_cos[0])
            for (x = 0; x < 1024; x++)
                wr_cos[x] = (short)lround(4096 * cos(x * (2 * M_PI / 1024)));
        if (water_level())
            wf_wz_ok = 1;
        rip = (port_water_ripples || port_water_fx) && wf_wz_ok;
        if (rip)
            wr_rings();
    }
    if (port_water_full == 2 && wf_wz_ok && GMAP && GMAP->f10 && GMAP->f12) {
        wt_stx = 2 * port_s3d_kx;
        wt_sty = 2 * port_s3d_ky;
        if (w / wt_stx + 2 <= WT_GX && h / wt_sty + 2 <= WT_GY) {
            wt_heights();
            wt_camera();
            if (wt_grid(w, h, wf_r0)) {
                full = 1;
                wo_seen = 8;            /* the objects' reflections for the next frames too */
            }
        }
    }
    if (!full || wo_w != w)
        wo_any = 0;
    wp.w = w;
    wp.h = h;
    wp.row = row;
    wp.top = top < h ? top : h;
    wp.bot = sky_refl_bot;
    wp.fade = h / 10 > 4 ? h / 10 : 4;  /* rows over which the mirrored land fades at the top */
    wp.bits = bits;
    wp.rip = rip;
    wp.full = full;
    wp.objs = full && wo_n && wo_w == w;
    /* the rows above the mirror's start first: above the horizon the water colour flat, then the
     * row next to the horizon the picture mirrored (these read the rows just made flat) */
    port_parallel(water_flat_part, NULL, (int)((wp.top + WP_ROWS - 1) / WP_ROWS));
    for (r = wp.top; r < h && r < wf_r0; r++) {
        uchar *p = bits + r * row, *src = bits + (2 * top - r) * row, *tab = sky_refl_row[r];
        for (x = 0; x < w; x++) {
            uchar c = p[x];
            if (sky_water[c] && r >= hview_cols[x])
                p[x] = tab[src[x + wf_off[c]]];
        }
    }
    if (wf_sky_w != w)
        wf_sky_rows = 0;
    if (h > wf_r0)
        port_parallel(water_part, NULL, (int)((h - (wf_r0 & ~1L) + WP_ROWS - 1) / WP_ROWS));
    wf_sky_rows = 0;                    /* kept for this drawing only */
}

/* "Fog" (port/video/fog.c): the water's pixels, after the reflections (ffpipe.c), through the fog of
 * where they meet the water (water_level's height): every 32 columns of a row the fog, in between
 * interpolated, dithered per pixel */
static double fw_hw;
static long fw_r0;                      /* the first row with a column of the land's (hview_cols) */

/* the fog where a ray meets the water at X, Y (map units): near the horizon that is farther than
 * a fix holds (P63: the conversion's result was the processor's, all fog by luck) */
static long fog_water_at(double X, double Y)
{
    double dx = X * 65536.0 - ff_view.x, dy = Y * 65536.0 - ff_view.y;
    if (dx < -2e9 || dx > 2e9 || dy < -2e9 || dy > 2e9)
        return 0x1000;
    return port_fog_at((fix)dx, (fix)dy);
}

static void fog_water_part(void *arg, int part, int thread)
{
    long w = grd_canvas->bm.w, h = grd_canvas->bm.h, row = grd_canvas->bm.row, r, x;
    long r0 = fw_r0 + part * 16, r1 = r0 + 16 < h ? r0 + 16 : h;
    uchar *bits = grd_canvas->bm.bits;
    (void)arg;
    (void)thread;
    if (w > 4096)
        w = 4096;
    for (r = r0; r < r1; r++) {
        uchar *p = bits + r * row;
        const uchar *dd;
        long xs, xe, fa = -1, fb;
        int tc = port_tc_on && r < 2048, marked = 0;
        double X, Y;
        for (xs = 0; xs < w; xs = xe) {
            xe = xs + 32 < w ? xs + 32 : w;
            for (x = xs; x < xe && !(sky_water[p[x]] && r >= hview_cols[x]); x++)
                ;
            if (x == xe) {
                fa = -1;
                continue;
            }
            if (fa < 0)
                fa = water_point(xs, r + 0.5, fw_hw, &X, &Y) ? fog_water_at(X, Y) : 0;
            fb = water_point(xe, r + 0.5, fw_hw, &X, &Y) ? fog_water_at(X, Y) : 0;
            if (fa == 0 && fb == 0) {   /* no fog yet: fog level 0 keeps the water's colours */
                fa = fb;
                continue;
            }
            {
                long f = (fa << 8) + ((fb - fa) << 8) * (x - xs) / (xe - xs), df = ((fb - fa) << 8) / (xe - xs);
                for (; x < xe; x++, f += df) {
                    uchar c = p[x];
                    long fi;
                    if (!sky_water[c] || r < hview_cols[x])
                        continue;
                    dd = port_fog_dd[x & 7];
                    fi = ((f >> 8) + dd[r & 7]) >> 8;
                    p[x] = port_fog_tab[fi > 16 ? 16 : fi][c];
                    if (tc) {           /* "True colour" (port/video/truecol.c) */
                        long a = f >> 12;
                        port_tc_mark(p + x, PORT_TC_FOG, a < 0 ? 0 : a > 255 ? 255 : a, c);
                        marked = 1;
                    }
                }
            }
            fa = fb;
        }
        if (marked && !port_tc_row[r]) /* (once a row: the rows' flags share cache lines) */
            port_tc_row[r] = 1;
    }
}

void port_fog_water(void)
{
    long h = grd_canvas->bm.h;
    if (h > WF_ROWS)
        h = WF_ROWS;
    port_fog_view(&ff_cam);
    if (!port_water_full)
        wf_r0 = 0;                      /* water_level looks from the bottom up to here */
    if (water_level_to(port_fog_ring() + 8.0, 0.25))     /* out to the fog's end */
        wf_wz_ok = 1;
    fw_hw = (ff_view.z - wf_wz) / 65536.0 / WF_HSCALE;

    if (!wf_wz_ok || fw_hw <= 0)
        return;
    {   /* above the land's highest row no pixel is the water's */
        long x, w = grd_canvas->bm.w < 4096 ? grd_canvas->bm.w : 4096;
        fw_r0 = h;
        for (x = 0; x < w; x++)
            if (hview_cols[x] < fw_r0)
                fw_r0 = hview_cols[x];
        fw_r0 = fw_r0 < 0 ? 0 : fw_r0;
    }
    if (fw_r0 < h)
        port_parallel(fog_water_part, NULL, (int)((h - fw_r0 + 15) / 16));
}
#endif

#ifndef TN_WATCOM_ABI
/* ------------------------------------------------------------ "Water effects": after the water
 * (and the fog's water, ffpipe.c): the shallows, a lighter blue within a square of the shore
 * near the eye and 3 squares far off (less in a small lake or pond: there all of it is near the
 * shore), then the splashes' spray (wr_splash) drawn over the water's place */
#define WS_N 512                        /* the map's squares across */
#define WS_BAND 96                      /* the shallows' width, 1/32 squares: far ... */
#define WS_BAND0 32                     /* ... near (ws_part) */
#define WS_NEAR 12.0                    /* (squares from the eye: WS_BAND0 to here ...) */
#define WS_FAR 64.0                     /* (... WS_BAND from here) */
#define WS_K 6                          /* the steps toward the shallows' blue */
static uchar ws_dist[(WS_N + 1) * (WS_N + 1)];  /* each corner's distance to the land, 1/32 squares */
static uchar ws_blk[(WS_N / 4) * (WS_N / 4)];   /* a block of 4 x 4 squares with a corner by the land */
static uchar ws_amp[(WS_N + 1) * (WS_N + 1)];   /* each corner's shallows' strength, 0..255: its water's size */
static long ws_stack[(WS_N + 1) * (WS_N + 1)];
static long ws_mission = -1;
static fix ws_wz;
static uchar ws_tab[WS_K + 1][0x100];   /* the water colours 0 .. WS_K steps toward the shallows' */
static ulong ws_key;
static double ws_hw;

/* the corners' distances to the land (a corner above the water, or by a square whose texture is
 * mostly land: the shores), a chamfer of 32 / 45 to a step, at most 8 squares */
static void ws_field(void)
{
    double wz = wf_wz / 65536.0;
    long x, y, n = WS_N + 1, t, i;
    uchar wet[0x40];
    if (ws_mission == game_359708 && ws_wz - wf_wz < 0x400 && wf_wz - ws_wz < 0x400)
        return;
    ws_mission = game_359708;
    ws_wz = wf_wz;
    for (t = 0; t < 0x40; t++) {        /* the textures at least half water (water, the wetter shores) */
        uchar *row = t < planet_tex_count ? planet_tex_rows[t] : NULL;
        long k = 0;
        for (i = 0; row && i < 0x100; i++)
            k += sky_water[planet_light[(8 << 8) + row[i]]];
        wet[t] = k >= 0x80;
    }
    /* a corner is land where the ground there is above the water or a square by it is land; the
     * last corners a hair inside (map_2949C4 at 512 would read a row past the grid) */
    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++) {
            long land = map_2949C4(x < WS_N ? (fix)(x << 16) : 0x1FFFFFF, y < WS_N ? (fix)(y << 16) : 0x1FFFFFF) /
                        65536.0 > wz + 0.05, cx, cy;
            for (cy = y - 1; cy <= y && !land; cy++)
                for (cx = x - 1; cx <= x && !land; cx++)
                    if (cx >= 0 && cy >= 0 && cx < WS_N && cy < WS_N)
                        land = !wet[*((uchar *)TN_LOWMEM(GMAP->rows2[cy]) + cx * 3) & 0x3F];
            ws_dist[y * n + x] = land ? 0 : 255;
        }
    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++) {
            long d = ws_dist[y * n + x];
            if (x > 0 && ws_dist[y * n + x - 1] + 32 < d)
                d = ws_dist[y * n + x - 1] + 32;
            if (y > 0) {
                if (ws_dist[(y - 1) * n + x] + 32 < d)
                    d = ws_dist[(y - 1) * n + x] + 32;
                if (x > 0 && ws_dist[(y - 1) * n + x - 1] + 45 < d)
                    d = ws_dist[(y - 1) * n + x - 1] + 45;
                if (x + 1 < n && ws_dist[(y - 1) * n + x + 1] + 45 < d)
                    d = ws_dist[(y - 1) * n + x + 1] + 45;
            }
            ws_dist[y * n + x] = (uchar)(d > 255 ? 255 : d);
        }
    for (y = n - 1; y >= 0; y--)
        for (x = n - 1; x >= 0; x--) {
            long d = ws_dist[y * n + x];
            if (x + 1 < n && ws_dist[y * n + x + 1] + 32 < d)
                d = ws_dist[y * n + x + 1] + 32;
            if (y + 1 < n) {
                if (ws_dist[(y + 1) * n + x] + 32 < d)
                    d = ws_dist[(y + 1) * n + x] + 32;
                if (x + 1 < n && ws_dist[(y + 1) * n + x + 1] + 45 < d)
                    d = ws_dist[(y + 1) * n + x + 1] + 45;
                if (x > 0 && ws_dist[(y + 1) * n + x - 1] + 45 < d)
                    d = ws_dist[(y + 1) * n + x - 1] + 45;
            }
            ws_dist[y * n + x] = (uchar)d;
        }
    /* each body of water's strength by its farthest corner from the land: none where that is a
     * square from it (a pond), full from 4 squares (the shallows a band along the shore) */
    memset(ws_amp, 0, sizeof ws_amp);
    for (i = 0; i < n * n; i++) {
        long top = 0, far = 0, k, a;
        if (!ws_dist[i] || ws_amp[i])
            continue;
        ws_amp[i] = 1;                  /* (1: met, its strength still to come) */
        ws_stack[top++] = i;
        for (k = 0; k < top; k++) {     /* its corners, side by side */
            long c = ws_stack[k], cx = c % n, cy = c / n, nb[4], j;
            far = ws_dist[c] > far ? ws_dist[c] : far;
            nb[0] = cx > 0 ? c - 1 : -1;
            nb[1] = cx + 1 < n ? c + 1 : -1;
            nb[2] = cy > 0 ? c - n : -1;
            nb[3] = cy + 1 < n ? c + n : -1;
            for (j = 0; j < 4; j++)
                if (nb[j] >= 0 && ws_dist[nb[j]] && !ws_amp[nb[j]]) {
                    ws_amp[nb[j]] = 1;
                    ws_stack[top++] = nb[j];
                }
        }
        a = (far - 32) * 255 / (128 - 32);
        a = a < 1 ? 1 : a > 255 ? 255 : a;
        for (k = 0; k < top; k++)
            ws_amp[ws_stack[k]] = (uchar)a;
    }
    /* the shore's corners (counted as land: the shores' textures are mostly land, though drawn
     * as water where the ground is under it) its water's, two corners out */
    for (t = 0; t < 2; t++) {
        uchar *was = (uchar *)ws_stack;
        memcpy(was, ws_amp, (size_t)n * n);
        for (y = 0; y < n; y++)
            for (x = 0; x < n; x++) {
                long c = y * n + x, m = 0;
                if (ws_dist[c] || was[c])
                    continue;
                if (x > 0 && was[c - 1] > m)
                    m = was[c - 1];
                if (x + 1 < n && was[c + 1] > m)
                    m = was[c + 1];
                if (y > 0 && was[c - n] > m)
                    m = was[c - n];
                if (y + 1 < n && was[c + n] > m)
                    m = was[c + n];
                ws_amp[c] = (uchar)m;
            }
    }
    memset(ws_blk, 0, sizeof ws_blk);
    for (y = 0; y < WS_N; y++)          /* (the blocks' edges included: the corners between them) */
        for (x = 0; x < WS_N; x++)
            if (ws_dist[y * n + x] < WS_BAND || ws_dist[y * n + x + 1] < WS_BAND ||
                ws_dist[(y + 1) * n + x] < WS_BAND || ws_dist[(y + 1) * n + x + 1] < WS_BAND)
                ws_blk[(y >> 2) * (WS_N / 4) + (x >> 2)] = 1;
}

/* the water colours (0xA2..0xAF) a step at a time toward a light blue, each the nearest of the
 * water's own colours and two light blues by them (0x93, 0x95) */
static void ws_tables(void)
{
    ulong key = grd_bpal[0xA0] ^ grd_bpal[0xA2] * 3 ^ grd_bpal[0xAF] * 7 ^ grd_bpal[0xA8] * 11;
    long k, i, c;
    if (key == ws_key)
        return;
    ws_key = key;
    for (k = 0; k <= WS_K; k++)
        for (i = 0; i < 0x100; i++) {
            grs_rgb p = grd_bpal[i];
            double f = 0.13 * k, r = (double)((p & 0x3FF) >> 2), g = (double)((p & 0x1FF800) >> 13),
                   b = (double)(p >> 24), best = 1e30;
            ws_tab[k][i] = (uchar)i;
            if (!k || i < 0xA2 || i > 0xAF)
                continue;
            r += (170 - r) * f;
            g += (212 - g) * f;
            b += (232 - b) * f;
            for (c = 0x93; c <= 0xAF; c++) {
                grs_rgb w = grd_bpal[c];
                if (c < 0xA2 && c != 0x93 && c != 0x95)
                    continue;
                double dr = r - (double)((w & 0x3FF) >> 2), dg = g - (double)((w & 0x1FF800) >> 13),
                       db = b - (double)(w >> 24), d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
                if (d < best)
                    best = d, ws_tab[k][i] = (uchar)c;
            }
        }
}

/* the shallows' strength at (X, Y) (1/65536 squares): its nearest corner's */
static long ws_strength(long long X, long long Y)
{
    long xi = (long)((X + 0x8000) >> 16), yi = (long)((Y + 0x8000) >> 16);
    if (xi < 0 || yi < 0 || xi > WS_N || yi > WS_N)
        return 0;
    return ws_amp[yi * (WS_N + 1) + xi];
}

/* the distance to the land at (X, Y) (1/65536 squares) in 1/32 squares times 65536, between
 * the corners; the shallows' width off the map */
static long ws_d(long long X, long long Y)
{
    long n = WS_N + 1, xi, yi, u, v, a, b;
    const uchar *q;
    if ((unsigned long long)X >= (unsigned long long)WS_N << 16 || (unsigned long long)Y >= (unsigned long long)WS_N << 16)
        return (long)WS_BAND << 16;
    xi = (long)(X >> 16);
    yi = (long)(Y >> 16);
    u = (long)(X & 0xFFFF);
    v = (long)(Y & 0xFFFF);
    q = ws_dist + yi * n + xi;
    a = (q[0] << 16) + (q[1] - q[0]) * u;
    b = (q[n] << 16) + (q[n + 1] - q[n]) * u;
    return a + (b - a) / 256 * (v >> 8);
}

static void ws_part(void *arg, int part, int thread)
{
    long w = grd_canvas->bm.w, h = grd_canvas->bm.h, row = grd_canvas->bm.row, r, x;
    long r0 = wf_r0 + part * 16, r1 = r0 + 16 < h ? r0 + 16 : h;
    uchar *bits = grd_canvas->bm.bits;
    (void)arg;
    (void)thread;
    if (w > 4096)
        w = 4096;
    for (r = r0; r < r1; r++) {
        uchar *p = bits + r * row;
        const uchar *bay = wf_bayer[r & 3];
        double X0, Y0, X1, Y1, eX, eY, px, fade, mx, my, dist;
        long long X, Y, dX, dY, dX4, dY4;
        long f64, dn = 0, bw;
        int hn = 0;
        if (!water_point(0, r, ws_hw, &X0, &Y0) || !water_point(w, r, ws_hw, &X1, &Y1))
            continue;
        eX = (X1 - X0) / w;
        eY = (Y1 - Y0) / w;
        px = sqrt(eX * eX + eY * eY);
        /* the band: full while a pixel is at most half a square, none from 1.5 squares (a
         * gentle edge across the rows, not a line) */
        fade = px > 0 ? 1.5 - px : 0;
        if (fade <= 0)
            continue;
        f64 = fade > 1 ? 64 : (long)(fade * 64.0);
        /* its width by the row's distance from the eye: a square out to WS_NEAR squares,
         * then wider to the full 3 at WS_FAR (near, a wide band looked like a halo around the
         * shores, the piers and the boats; far, a narrow one is lost) */
        mx = (X0 + X1) * 0.5 - ff_view.x / 65536.0;
        my = (Y0 + Y1) * 0.5 - ff_view.y / 65536.0;
        dist = sqrt(mx * mx + my * my + ws_hw * ws_hw);
        bw = dist <= WS_NEAR ? WS_BAND0 : dist >= WS_FAR ? WS_BAND :
             WS_BAND0 + (long)((dist - WS_NEAR) * (WS_BAND - WS_BAND0) / (WS_FAR - WS_NEAR));
        /* along the row in 1/65536 squares: 8 pixels at a time (at most 4 squares: 2 x 2
         * blocks) none by the land skipped, the distance every 4th pixel and in a line between
         * (worked out only for 4 pixels with a shallows' colour, the one at the next 4's start
         * kept for them) */
        X = (long long)(X0 * 65536.0);
        Y = (long long)(Y0 * 65536.0);
        dX = (long long)(eX * 65536.0);
        dY = (long long)(eY * 65536.0);
        dX4 = 4 * dX;
        dY4 = 4 * dY;
        for (x = 0; x < w; x += 4, X += dX4, Y += dY4) {
            long j, n = w - x < 4 ? w - x : 4, d = 0, dd = 0, a = 0;
            int got = 0;
            if (!(x & 7)) {
                long long Xe = X + 7 * dX, Ye = Y + 7 * dY, M = ((long long)WS_N << 16) - 1;
                long ba = (long)((X < 0 ? 0 : X > M ? M : X) >> 18), bb = (long)((Xe < 0 ? 0 : Xe > M ? M : Xe) >> 18),
                     ca = (long)((Y < 0 ? 0 : Y > M ? M : Y) >> 18), cb = (long)((Ye < 0 ? 0 : Ye > M ? M : Ye) >> 18);
                if (!ws_blk[ca * (WS_N / 4) + ba] && !ws_blk[ca * (WS_N / 4) + bb] && !ws_blk[cb * (WS_N / 4) + ba] &&
                    !ws_blk[cb * (WS_N / 4) + bb]) {
                    x += 4;
                    X += dX4;
                    Y += dY4;
                    hn = 0;
                    continue;
                }
            }
            for (j = 0; j < n; j++) {
                uchar c = p[x + j];
                long k, dj;
                if (c < 0xA2 || c > 0xAF || r < hview_cols[x + j])
                    continue;
                if (!got) {
                    d = hn ? dn : ws_d(X, Y);
                    dn = ws_d(X + dX4, Y + dY4);
                    dd = (dn - d) / 4;
                    a = ws_strength(X + 2 * dX, Y + 2 * dY);
                    got = 1;
                }
                dj = d + j * dd;
                if (dj >= bw << 16)
                    continue;
                /* WS_K steps at the shore, none at the band's edge, in 16ths (dithered) */
                k = (((((bw << 16) - dj) / bw * WS_K >> 6) * f64 >> 12) * a >> 8) + bay[(x + j) & 3];
                k >>= 4;
                p[x + j] = ws_tab[k > WS_K ? WS_K : k][c];
            }
            hn = got;
        }
    }
}

/* the eye's axes (water_ray's matrix): forward, right, down */
static double ws_f[3], ws_a[3], ws_b[3];

static void ws_axes(void)
{
    double sh = ff_view.sin_h / 65536.0, ch = ff_view.cos_h / 65536.0, sp = ff_view.sin_p / 65536.0,
           cp = ff_view.cos_p / 65536.0, sb = ff_view.sin_b / 65536.0, cb = ff_view.cos_b / 65536.0;
    ws_f[0] = cp * ch, ws_f[1] = sh * cp, ws_f[2] = -sp;
    ws_a[0] = sb * sp * ch - cb * sh, ws_a[1] = ch * cb + sb * sp * sh, ws_a[2] = sb * cp;
    ws_b[0] = -sb * sh - cb * sp * ch, ws_b[1] = ch * sb - cb * sp * sh, ws_b[2] = -cb * cp;
}

/* the world point (X, Y, Z in squares) on the canvas: *sx, *sy and the pixels a square there */
static int ws_proj(double X, double Y, double Z, double *sx, double *sy, double *k)
{
    double d[3], t, fx = ff_cam.zx * 1.1 / 65536.0, fy = ff_cam.zy * 1.1 / 65536.0;
    d[0] = X - ff_view.x / 65536.0;
    d[1] = Y - ff_view.y / 65536.0;
    d[2] = Z - ff_view.z / 65536.0;
    t = d[0] * ws_f[0] + d[1] * ws_f[1] + d[2] * ws_f[2];
    if (t < 0.3)
        return 0;
    *sx = ff_cam.half_w + (d[0] * ws_a[0] + d[1] * ws_a[1] + d[2] * ws_a[2]) / t * fx;
    *sy = ff_cam.half_h + (d[0] * ws_b[0] + d[1] * ws_b[1] + d[2] * ws_b[2]) / t * fy;
    *k = fx / t;
    return 1;
}

static void ws_blob(double x, double y, double rr, uchar c)
{
    long w = grd_canvas->bm.w, h = grd_canvas->bm.h, row = grd_canvas->bm.row, i, j;
    long x0 = (long)floor(x - rr + 0.5), x1 = (long)floor(x + rr + 0.5), y0 = (long)floor(y - rr + 0.5),
         y1 = (long)floor(y + rr + 0.5);
    uchar *bits = grd_canvas->bm.bits;
    if (x1 <= x0)
        x1 = x0 + 1;
    if (y1 <= y0)
        y1 = y0 + 1;
    for (j = y0 < 0 ? 0 : y0; j < y1 && j < h; j++)
        for (i = x0 < 0 ? 0 : x0; i < x1 && i < w; i++) {
            double dx = i + 0.5 - x, dy = j + 0.5 - y;
            if (rr < 1.0 || dx * dx + dy * dy <= rr * rr)
                bits[j * row + i] = c;
        }
}

/* the splash's middle, (x, y) on the water at wz, seen from the eye: no land on the way rises
 * above the line to it (the map's heights every quarter of a square, but for the half square at
 * either end: the ground under the eye, the shore at the splash).  The splash is not judged by
 * the canvas's pixel there (P118): the hit's own sparks (the game's particles, drawn before this
 * pass) covered it, so the spray was hidden while they lasted, about the first 0.4 s, and its
 * column never showed; each new shot near it hid it again */
static int ws_seen(double x, double y, double wz)
{
    double ex = ff_view.x / 65536.0, ey = ff_view.y / 65536.0, ez = ff_view.z / 65536.0;
    double dx = x - ex, dy = y - ey, d = sqrt(dx * dx + dy * dy), s;
    for (s = 0.5; s < d - 0.5; s += 0.25) {
        double f = s / d, px = ex + dx * f, py = ey + dy * f;
        if (px >= 0 && py >= 0 && px < 512.0 && py < 512.0 &&
            map_2949C4((fix)(px * 65536.0), (fix)(py * 65536.0)) / 65536.0 > ez + (wz - ez) * f + 0.02)
            return 0;
    }
    return 1;
}

/* the spray: drops thrown up from the splash's middle, falling back (in squares: a soldier is
 * about 0.6 high, gravity 3.3 a second^2), white, later the next water colour down; a column
 * for its first third of a second; none where land on the way hides the splash's middle (ws_seen) */
static void ws_spray(void)
{
    double t = game_time / 1000.0, wz = wf_wz / 65536.0;
    long w = grd_canvas->bm.w, h = grd_canvas->bm.h;
    int i, k;
    ws_axes();
    for (i = 0; i < wr_nspl; i++) {
        double age = t - wr_spl[i].t0, sz = wr_spl[i].size, bx, by, bk;
        unsigned int r = (unsigned int)(wr_spl[i].t0 * 1000.0) * 2654435761u ^ (unsigned int)(wr_spl[i].x * 64.0);
        long ix, iy;
        uchar c;
        if (age < 0 || age >= WR_SPRAY_LIFE || !ws_proj(wr_spl[i].x, wr_spl[i].y, wz, &bx, &by, &bk))
            continue;
        ix = (long)bx;
        iy = (long)by;
        if (ix < 0 || ix >= w || iy < 0 || iy >= h || iy < hview_cols[ix] || !ws_seen(wr_spl[i].x, wr_spl[i].y, wz))
            continue;
        c = age < 0.8 ? wr_white : wr_dark[wr_white];
        if (age < 0.5) {                /* the spout: drops in a column narrowing upward, up and down again */
            double ch = (0.2 + 0.1 * sz) * sz * sin(age / 0.5 * M_PI), cw = 0.03 + 0.02 * sz;
            for (k = 0; k < 30 + (int)(50 * sz); k++) {
                double u, rho, a, sx, sy, kk;
                r = r * 1664525u + 1013904223u;
                u = pow((r >> 8) / 16777216.0, 0.7);
                r = r * 1664525u + 1013904223u;
                rho = cw * (1 - 0.7 * u) * ((r >> 8) / 16777216.0);
                r = r * 1664525u + 1013904223u;
                a = (r >> 8) / 16777216.0 * 2 * M_PI;
                if (ws_proj(wr_spl[i].x + cos(a) * rho, wr_spl[i].y + sin(a) * rho, wz + u * ch, &sx, &sy, &kk))
                    ws_blob(sx, sy, 0.010 * sqrt(sz) * kk, c);
            }
        }
        for (k = 0; k < 24 + (int)(40 * sz); k++) {
            double a, hs, vz, z, sx, sy, kk;
            r = r * 1664525u + 1013904223u;
            a = (r >> 8) / 16777216.0 * 2 * M_PI;
            r = r * 1664525u + 1013904223u;
            hs = (0.15 + 0.6 * ((r >> 8) / 16777216.0)) * sqrt(sz);
            r = r * 1664525u + 1013904223u;
            vz = (1.0 + 1.6 * ((r >> 8) / 16777216.0)) * sqrt(sz);
            z = vz * age - 1.65 * age * age;
            if (z < 0)
                continue;               /* back in the water */
            if (ws_proj(wr_spl[i].x + cos(a) * hs * age, wr_spl[i].y + sin(a) * hs * age, wz + z, &sx, &sy, &kk))
                ws_blob(sx, sy, 0.008 * sqrt(sz) * kk, c);
        }
    }
}

void port_water_after(void)
{
    long h = grd_canvas->bm.h;
    if (h > WF_ROWS)
        h = WF_ROWS;
    if (!wf_wz_ok || ff_view.z <= wf_wz)
        return;
    ws_hw = (ff_view.z - wf_wz) / 65536.0 / WF_HSCALE;
    ws_field();
    ws_tables();
    if (h > wf_r0)
        port_parallel(ws_part, NULL, (int)((h - wf_r0 + 15) / 16));
    if (wr_nspl)
        ws_spray();
}
#endif

/* 0x2A84A4: reflection set-up: keep the column heights, random offsets for the water
 * colours, translucency table per row below the horizon */
void rndsky_2A84A4(void)
{
    long i, top, n, k;
#ifdef TN_WATCOM_ABI
    memcpy(sky_cols_save, hview_cols, 0x500);
#else
    memcpy(sky_cols_save, hview_cols, 4 * port_s3d_cols);     /* sharp 3D: its columns */
#endif
    for (i = 0; i < 0x100; i++) {
        if (sky_water[i]) {
            long a = (RndRange(game_rnd_text, 0, 0x7FFF) % 3 - 1) * grd_canvas->bm.row;
            sky_refl_off[i] = RndRange(game_rnd_text, 0, 0x7FFF) % 3 + a - 1;
        } else
            sky_refl_off[i] = 0x10;
    }
    top = (-muldiv(fix_mul(ff_cam.zy, 0x11999), ff_view.sin_p, ff_view.cos_p) >> 16) + ff_cam.half_h - 1;
    if (top < 0)
        top = 0;
    n = grd_canvas->bm.h;
    if (n / 2 > top)
        n = 2 * top - 1;
    if (n < 0)
        n = 0;
#ifndef TN_WATCOM_ABI
    /* P64: looking up steeply the horizon's row is below the view (twice its height at the
     * steepest): in the sharp 3D view at 6x past the end of its 2048 rows' table.  The rows
     * below the view are never drawn.  G32: the game's own view at 320 X 400 went past the
     * 512 rows of its table too (up to row 800), over the 16 translucency tables after it (all
     * then the first: freed 16 times at the mission's end) and the water flags after them.  The
     * game's own view fills its table as the original, to its end */
    for (i = 0; i < top && i < (port_s3d_on ? grd_canvas->bm.h : 0x200); i++)
#else
    for (i = 0; i < top; i++)
#endif
        sky_refl_row[i] = sky_refl_tab[0];
    if (n > top)
        for (i = top, k = 0; i < n; i++, k += 16)
            sky_refl_row[i] = sky_refl_tab[k / (n - top)];
    for (i = n; i < grd_canvas->bm.h; i++)
        sky_refl_row[i] = sky_refl_tab[15];
    sky_refl_bot = n;
    sky_refl_top = top;
#ifndef TN_WATCOM_ABI
    if (port_s3d_on && port_water_full)
        water_full_setup();
#endif
}

#ifndef TN_WATCOM_ABI
/* the sharp 3D view's rndsky_2A8658: the same work on bands of 64 rows, each band across all
 * the columns, because its columns are too tall for the CPU cache (the goggles' view was
 * several times slower than the cockpit's).  Per column the rows are the original's: the rows
 * above the horizon [a0, a1) turn the water colour flat, the reflected rows [m0, bot) read the
 * mirror image (rasm_2B2785), then [lim, h0) the darkest table.  Only a pixel whose mirror
 * image a neighbouring column changes first can come out differently. */
/* which groups of four columns of the rows may hold a water colour (0xA2..0xAF; 0xA0..0xAF
 * tested, four bytes at a time): grp[x / 4]; whether any does */
static int band_water(uchar *grp, const uchar *p, long row, long w, long n)
{
    long x, ng = (w + 3) / 4, any = 0;
    memset(grp, 0, ng);
    for (; n > 0; n--, p += row) {
        for (x = 0; x + 4 <= w; x += 4) {
            ulong v;
            memcpy(&v, p + x, 4);
            v = (v & 0xF0F0F0F0) ^ 0xA0A0A0A0;
            if ((v - 0x01010101) & ~v & 0x80808080)
                any = grp[x >> 2] = 1;
        }
        for (; x < w; x++)
            if (sky_water[p[x]])
                any = grp[x >> 2] = 1;
    }
    return any;
}

static void refl_bands(long top, long bot)
{
    static long a0[4096], a1[4096], m0[4096], l0[4096];   /* sharp3d.c MAX_COLS */
    static uchar grp[4096 / 4];
    long w = grd_canvas->bm.w, row = grd_canvas->bm.row, h = grd_canvas->bm.h, x, y0, y1, r, e;
    uchar *t15 = sky_refl_tab[15], flat = planet_tab38[0x3BF], *bits = grd_canvas->bm.bits;
    long *off = sky_refl_off;
    if (w > 4096)
        w = 4096;
    for (x = 0; x < w; x++) {
        long h0 = sky_cols_save[x], lim;
        r = hview_cols[x];
        lim = h0 < bot ? h0 : bot;
        a0[x] = a1[x] = r;
        if (r < top && top < lim)
            a1[x] = r = top;
        else if (r < top && r < lim)
            a1[x] = r = lim;
        if (r < top)
            r = top;
        m0[x] = r < lim ? r : bot;          /* no reflected rows */
        l0[x] = r < lim ? lim : r;
    }
    for (y0 = 0; y0 < h; y0 = y1) {
        y1 = y0 + 64 < h ? y0 + 64 : h;
        if (!band_water(grp, bits + y0 * row, row, w, y1 - y0))
            continue;                   /* no water colour: nothing in the band changes */
        for (x = 0; x < w; x++) {
            uchar *base = bits + x, *p;
            if (!grp[x >> 2])
                continue;
            r = a0[x] > y0 ? a0[x] : y0;
            e = a1[x] < y1 ? a1[x] : y1;
            for (p = base + r * row; r < e; r++, p += row)
                if (sky_water[*p])
                    *p = flat;
            r = m0[x] > y0 ? m0[x] : y0;
            e = bot < y1 ? bot : y1;
            for (p = base + r * row; r < e; r++, p += row) {
                uchar c = *p;
                if (sky_water[c])
                    *p = sky_refl_row[r][base[(2 * top - r) * row + off[c]]];
            }
            r = l0[x] > y0 ? l0[x] : y0;
            e = sky_cols_save[x] < y1 ? sky_cols_save[x] : y1;
            for (p = base + r * row; r < e; r++, p += row)
                if (sky_water[*p])
                    *p = t15[*p];
        }
    }
}
#endif

/* 0x2A8658: the reflection of the sky in the water colours below the terrain line */
void rndsky_2A8658(void)
{
    long top = sky_refl_top, bot = sky_refl_bot, row, x, r, lim, h0;
    uchar *t15 = sky_refl_tab[15];
#ifndef TN_WATCOM_ABI
    if (port_s3d_on) {
        if (!port_water_full)           /* Medium / High: port_water_full_pass, after the smooth ground */
            refl_bands(top, bot);
        return;
    }
#endif
    row = grd_canvas->bm.row;
    for (x = 0; x < grd_canvas->bm.w; x++) {
        uchar *p, *base, *src;
        h0 = sky_cols_save[x];
        r = hview_cols[x];
        p = grd_canvas->bm.bits + x;
        lim = h0 < bot ? h0 : bot;
        if (r < top && top < lim) {
            p += row * r;
            for (; r < top; r++, p += row)
                if (sky_water[*p])
                    *p = planet_tab38[0x3BF];
        } else if (r < top && r < lim) {
            p += row * r;
            for (; r < lim; r++, p += row)
                if (sky_water[*p])
                    *p = planet_tab38[0x3BF];
        }
        base = grd_canvas->bm.bits + x;
        if (r < top)
            r = top;
        src = base + (2 * top - r) * row;
        p = base + row * r;
        if (r < lim) {
            void *blk[3];
            blk[0] = &sky_refl_row[r];
            blk[1] = sky_refl_off;
            blk[2] = sky_water;
            rasm_2B2785(p, src, row, bot - r, blk);
            p += (lim - r) * row;
            r = lim;
        }
        for (; r < h0; r++, p += row)
            if (sky_water[*p])
                *p = t15[*p];
    }
}
