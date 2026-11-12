/* strip.c -- Terra Nova module M085 (0x2AB800-0x2AC52F; source file name unknown): terrain
 * strips: the incremental transform of map points for the strip builder and the column runs
 * handed to the asm rasteriser (thunder_2B29B0).  Decompiled from __FF.EXE v1.09.
 *
 *   0x2AB800 strip_2AB800 height step tables     0x2AB8BC strip_2AB8BC project the current point
 *   0x2ABA1C strip_2ABA1C rescale the steps      0x2ABB10 strip_2ABB10 message handler
 *   0x2AC098 strip_2AC098 (no callers) key test  0x2AC0C0 strip_2AC0C0 reciprocal tables
 *   0x2AC124 strip_2AC124 (empty)                0x2AC128 strip_2AC128 row with blast marks
 *   0x2AC15C strip_2AC15C mark columns           0x2AC19C strip_2AC19C draw a column run
 *   0x2AC33C strip_2AC33C shade table            0x2AC378 strip_2AC378 run with fading
 *   0x2AC4B8 strip_2AC4B8 (no callers)           0x2AC4E4 strip_2AC4E4 camera set-up
 *
 * strip_2ABB10 keeps a point in view space (strip_cur_x/y/z): message 2..5 starts it at a map
 * square (and sets the step scale 1, 2, 4 or 8), 6 moves it one square in one of 8 directions
 * (strip_stepx/dy/dz, built from the view matrix by message 0), 7 projects it with a height added
 * (strip_tx/ty/tz tables or the matrix column times the height).
 *
 * Quirks kept:
 *  - strip_2ABA1C loops for ever when the current scale is 0 (it doubles it until it reaches
 *    the wanted one) and rescales only the first 8 of the 9 direction steps (the 9th is 0).
 *  - Message 7 passes the caller's edx / ebx (sx, sy) through as the output pointers of
 *    strip_2AB8BC; the other messages ignore them (the game only sends messages 0 and 1).
 *  - strip_2AC0C0 falls through into the empty strip_2AC124 (no effect).
 *  - strip_2AC19C remembers the run for strip_2AC15C only when strip_mark is set.
 */
#include "g5_c.h"
#include "g1_b.h"
#include "g6_a.h"     /* added at the G5-G8 merge */

void strip_2AB800(fix *t0, fix *t1, fix *t2, fix s0, fix s1, fix s2)
{
    fix a = 0, b = 0, c = 0;
    long i;
    for (i = 0; i < 0x80; i++) {
        *t0++ = a;
        a += s0;
    }
    for (i = 0; i < 0x80; i++) {
        *t1++ = b;
        b += s1;
    }
    for (i = 0; i < 0x80; i++) {
        *t2++ = c;
        c += s2;
    }
    t0 += 0x80;
    t1 += 0x80;
    t2 += 0x80;
    a = b = c = 0;
    for (i = 0; i < 0x80; i++) {
        a -= s0;
        *--t0 = a;
    }
    for (i = 0; i < 0x80; i++) {
        b -= s1;
        *--t1 = b;
    }
    for (i = 0; i < 0x80; i++) {
        c -= s2;
        *--t2 = c;
    }
}

void strip_2AB8BC(StripMsg *m, long *sx, long *sy)
{
    long d = m->dir, x, y, z, n;
    uchar s, hi;
    strip_cur_x += strip_stepx[d];
    strip_cur_y += strip_stepy[d];
    strip_cur_z += strip_stepz[d];
    n = m->n;
    if (m->near) {
        x = strip_cur_x + strip_tx[n];
        y = strip_cur_y + strip_ty[n];
        z = strip_cur_z + strip_tz[n];
    } else {
        x = strip_cur_x + hz_mat[1] * n;
        y = strip_cur_y + hz_mat[4] * n;
        z = strip_cur_z + hz_mat[7] * n;
    }
    hi = (ulong)z >> 24;
    if (hi == 0)
        s = m->log2[(uchar)((ulong)z >> 16)];
    else
        s = m->log2[hi] + 8;
    z >>= s & 31;
    x >>= s & 31;
    y >>= s & 31;
    n = (uchar)((ulong)z >> 8);
    *sx = ((fix_mul(x, m->div_x[n]) + 0x8000) >> 16) + m->cx;
    *sy = fix_mul(y, m->div_y[n]) + (m->cy << 16);
}

void strip_2ABA1C(long cur, long want)
{
    long s = 0, i;
    while (cur < want) {
        cur += cur;
        s++;
    }
    for (i = 0; i < 8; i++) {
        strip_stepx[i] <<= s & 31;
        strip_stepy[i] <<= s & 31;
        strip_stepz[i] <<= s & 31;
    }
}

/* the view-space point of map square (x, y) */
static void start(StripMsg *m)
{
    FfView *v = m->view;
    fix dy = (m->y << 16) - v->y, dx = (m->x << 16) - v->x;
    strip_cur_x = fix_mul(dy, hz_mat[0]) + fix_mul(dx, hz_mat[2]) + v->mz[0];
    strip_cur_y = fix_mul(dy, hz_mat[3]) + fix_mul(dx, hz_mat[5]) + v->mz[1];
    strip_cur_z = fix_mul(dy, hz_mat[6]) + fix_mul(dx, hz_mat[8]) + v->mz[2];
}

static void scale(uchar want)
{
    if (strip_shift == want)
        return;
    strip_2ABA1C(strip_shift, want);
    strip_shift = want;
}

/* the 9 direction steps of one coordinate from two matrix entries */
static void dirs(fix *d, fix p, fix q)
{
    d[0] = -p;
    d[1] = q - p;
    d[2] = q;
    d[3] = p + q;
    d[4] = p;
    d[5] = p - q;
    d[6] = -q;
    d[7] = -p - q;
    d[8] = 0;
}

void strip_2ABB10(void *msg, long *sx, long *sy)
{
    StripMsg *m = msg;
    long d;
    switch (m->code) {
    case 0:
        memcpy(hz_mat, m->view->m, 0x24);
        dirs(strip_stepx, hz_mat[0], hz_mat[2]);
        dirs(strip_stepy, hz_mat[3], hz_mat[5]);
        dirs(strip_stepz, hz_mat[6], hz_mat[8]);
        strip_tx = ffmem_2C4F40(0x400);
        strip_ty = ffmem_2C4F40(0x400);
        strip_tz = ffmem_2C4F40(0x400);
        strip_2AB800(strip_tx, strip_ty, strip_tz, hz_mat[1] << 4, hz_mat[4] << 4, hz_mat[7] << 4);
        strip_shift = 1;
        break;
    case 1:
        ffmem_2C4FE4(strip_tz);
        ffmem_2C4FE4(strip_ty);
        ffmem_2C4FE4(strip_tx);
        break;
    case 2:
        start(m);
        strip_shift = 1;
        break;
    case 3:
        start(m);
        scale(2);
        break;
    case 4:
        start(m);
        scale(4);
        break;
    case 5:
        start(m);
        scale(8);
        break;
    case 6:
        d = m->dir;
        strip_cur_x += strip_stepx[d];
        strip_cur_y += strip_stepy[d];
        strip_cur_z += strip_stepz[d];
        break;
    case 7:
        strip_2AB8BC(m, sx, sy);
        break;
    }
}

uchar strip_2AC098(short *p)
{
    return *p == 0x204 || *p == 0x406 || *p == 0x600 || *p == 2;
}

void strip_2AC0C0(void)
{
    long sum = strip_recip_sum, i;
    if (strip_recip_ok == 0) {
        strip_recip[0] = 0;
        sum = 0;
        strip_recip_ok = 1;
        for (i = 1; i <= 0x100; i++) {
            long r = 0x10000 / i;
            sum += r;
            strip_recip[i] = r;
            strip_recip2[i] = r;
        }
    }
    strip_recip_sum = sum;
}

void strip_2AC124(void)
{
}

#ifndef TN_WATCOM_ABI
const uchar *port_marks_noise(void);    /* port/mods/marks.c: the effects pack's BlastMarks */
#endif

uchar *strip_2AC128(long tex)
{
    uchar *src = planet_tex_rows[tex & 0x3F];
    long i;
#ifndef TN_WATCOM_ABI
    const uchar *nz = port_marks_noise();
#endif
    for (i = 0; i < 0x100; i++) {
#ifndef TN_WATCOM_ABI
        uchar c = nz ? nz[i] : blast_noise_tmp[i];
#else
        uchar c = blast_noise_tmp[i];
#endif
        if (c == 0)
            c = src[i];
        strip_row[i] = c;
    }
    return strip_row;
}

void strip_2AC15C(uchar *p, long n, fix y, fix dy)
{
    long row = strip_row_len;
    while (n-- != 0) {
        if (y >= 0 && y <= ff_cam.half_h << 17)
            p[(y >> 16) * row] = 2;
        p++;
        y += dy;
    }
    strip_row_len = row;
}

void strip_2AC19C(long a, long x, long tex, long lvl)
{
    long l = 15 - lvl, step, fade;
    long m_x = 0, m_dy = 0;
    fix m_y = 0;
    uchar *m_p = NULL;

    strip_ras.dst = strip_bits + a;
    if (a < 0) {
        a = -a;
        strip_ras.ybuf += a;
        strip_ras.dst += a;
        strip_ras.v += a * strip_ras.dv;
    } else {
        x -= a;
    }
    if (strip_mark) {
        m_y = strip_ras.v;
        m_p = strip_ras.dst;
        m_x = x;
        m_dy = strip_ras.dv;
    }
    if (game_35CADC >= 0x10) {
        strip_shade = -1;
        l = 15;
    }
    if (l > -1 && strip_shade > -1) {
#ifndef TN_WATCOM_ABI
        /* P62: the sharp 3D view's strips are up to its width long, the table only 256: past
         * it the light's step came from the next tables (the view's points and pointers), and
         * the light ran off its 16 rows, up to 64 KB past them.  G31: the original's own view
         * is 320 wide, and a strip over 256 long (rare) took its step
         * from strip_recip2 after the table, 0x10000 / (x - 257): 5 to 258 times too steep */
        if (x > 0x100)
            step = ((0x10000 / x) * (l - strip_shade)) >> 8;
        else
#endif
        step = (strip_recip[x] * (l - strip_shade)) >> 8;
        fade = (strip_shade << 8) + 0x80;
    } else {
        step = 0;
        fade = 0;
    }
    strip_shade = l;
    if (hz_clut == 0) {
        if (step == 0) {
            strip_ras.fade = 0;
            strip_ras.light = planet_light + (l << 8);
        } else {
            strip_ras.fade = fade;
            strip_ras.light = planet_light;
        }
        strip_ras.fade_step = step;
    }
    if (tex & 0x40)
        strip_ras.tex = strip_2AC128(tex);
    else
        strip_ras.tex = planet_tex_rows[tex & 0x3F];
    {
        uchar *hs = (uchar *)hview_strip;
        strip_ras.u = (((*(long *)(hs + 0xE) >> 16) ^ (*(long *)(hs + 0x10) >> 16)) << 4) & 0xFF;
    }
    if (strip_neg != 0 || strip_ras.v < 0)
        strip_2AC378(x);
#ifdef TN_WATCOM_ABI
    else if (x < 0x140)
#else
    else if (x < (port_s3d_on ? port_s3d_cols : 0x140))     /* the sharp 3D view: its columns */
#endif
        thunder_2B29B0(strip_ras.u, x, &strip_ras, x);
    if (strip_mark)
        strip_2AC15C(m_p, m_x, m_y, m_dy);
}

void strip_2AC33C(void)
{
    if (hz_clut) {
        strip_ras.shade = (uchar *)hz_clut;
        strip_ras.light = hz_shade_row;
        strip_ras.fade = 0;
        strip_ras.fade_step = 0;
    } else {
        strip_ras.shade = hz_shade_row;
    }
}

void strip_2AC378(long n)
{
    long k;
    if (strip_neg != 0) {
        fix v = strip_ras.v;
        if (v >= 0) {
            fix dv = strip_ras.dv;
            k = (v - dv - 1) / -dv;
            if (n <= k)
                goto tail;
            if (k > 0) {
                thunder_2B29B0(k, 0, &strip_ras, k);
                strip_ras.dst += k;
                strip_ras.ybuf += k;
                n -= k;
            }
        }
        strip_ras.v = 0;
        strip_ras.dv = 0;
        thunder_2B29B0(0, 0, &strip_ras, n);
        return;
    }
    {
        fix dv = strip_ras.dv, v = strip_ras.v;
        k = (dv - v - 1) / dv;
        if (k > 0) {
            strip_ras.v = 0;
            strip_ras.dv = 0;
            if (n > k) {
                thunder_2B29B0(k, 0, &strip_ras, k);
                strip_ras.dst += k;
                strip_ras.ybuf += k;
                n -= k;
                strip_ras.v = v + dv * k;
                strip_ras.dv = dv;
            }
        }
    }
tail:
    thunder_2B29B0(0, 0, &strip_ras, n);
}

void strip_2AC4B8(void)
{
    strip_ras.rows = strip_rows;
    strip_ras.row = strip_row_len;
    thunder_2B2AD8(0, 0, &strip_ras);
}

void strip_2AC4E4(FfCamera *cam)
{
    strip_rows = cam->rows;
    strip_row_len = (ushort)cam->canvas->bm.row;
    strip_bits = cam->canvas->bm.bits;
    strip_ras.row = strip_row_len;
    strip_ras.rows = strip_rows;
    thunder_2B2AD8(0, 0, &strip_ras);
    strip_2AC0C0();
}
