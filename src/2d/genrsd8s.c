/* genrsd8s.c -- Terra Nova v1.09: scaled solid RSD8 bitmaps (canvas tables 0x36808C / 0x36A558
 * SOLID_SCALE_RSD8_UBITMAP / SOLID_SCALE_RSD8_BITMAP; Shock's enum names them but has no code).
 * Decompiled from __FF.EXE (Watcom C); the names follow Shock's gen_rsd8_* pattern.
 *
 *   0x33C490 gen_rsd8_scale_solid_ubitmap   0x33C9B0 gen_rsd8_scale_solid_bitmap
 *            (bm, x, y, w, h, color): h and color on the stack
 *
 * The source is stepped by du = (bm->w << 16) / w and dv = (bm->h << 16) / h from du/2, dv/2.
 * For each destination row the RSD8 stream is decoded forward to source row v >> 16 (the
 * decoder state is saved per source row, so repeated rows restart from it).  Every opaque
 * (run or literal) source pixel is drawn as `color` through grd_pixel_table[SET_PIXEL8], one
 * call per destination pixel; skips just advance x.  RSD8 codes as in fl8rsd8.c.
 *
 * Quirks kept:
 *  - ubitmap: the part of a code that continues from the previous source row is drawn first
 *    with the SOURCE colours (the run byte / the literal bytes) through grd_pixel_table
 *    [SET_UPIXEL8]; and its run/skip pixel count is computed from the whole continuation,
 *    not from the remaining u.
 *  - bitmap: clips the right edge when x + w >= clip right (an exact fit counts as clipped),
 *    and only the right/left source columns, via the scaled limits.
 *  - The end code leaves the stream on it, so the rest of the bitmap repeats the end; the
 *    current row's decoder state is not saved then.
 */
#include "2d_tmap.h"

#define SET_UPIXEL8 0
#define SET_PIXEL8 1

enum { OP_RUN, OP_SKIP, OP_LIT };

#define PIX(i, c, xx, yy) WATCOM_CALL((void (*)())grd_pixel_table[i], (long)(c), (long)(short)(xx), (long)(short)(yy))
#define CEILS(a) ((short)((fix)((a) + 0xffff) >> 16))
#define NPIX(d) ((short)((tm_fix_div((d), du) + 0xffff) >> 16))

/* reads the next code (first stepping past the previous run's colour byte); returns its length,
 * or -1 at the end code (src left on it, op = skip) */
static int rd(uchar **ps, int *op)
{
    uchar *s = *ps, c;
    ushort w;

    if (*op == OP_RUN)
        s++;
    c = *s;
    if (c == 0) {
        *op = OP_RUN;
        *ps = s + 2;
        return s[1];
    }
    if (c < 0x80) {
        *op = OP_LIT;
        *ps = s + 1;
        return c;
    }
    if (c > 0x80) {
        *op = OP_SKIP;
        *ps = s + 1;
        return c & 0x7f;
    }
    w = *(ushort *)(s + 1);
    *ps = s + 3;
    if (w >= 0xc000) {
        *op = OP_RUN;
        return w & 0x3fff;
    }
    if (w >= 0x8000) {
        *op = OP_LIT;
        return w & 0x7fff;
    }
    *op = OP_SKIP;
    if (w == 0) {
        *ps = s;
        return -1;
    }
    return w;
}

void gen_rsd8_scale_solid_ubitmap(grs_bitmap *bm, short x, short y, short w, short h, long color)
{
    fix du = (bm->w << 16) / w, dv = (bm->h << 16) / h;
    fix v = dv >> 1, u0 = du >> 1, u, t, ustart, i, tt;
    int op = OP_SKIP, n = 0, s_op = OP_SKIP, s_n = 0, len, k;
    short last_row = -1, ye = y + h, xd;
    uchar *src = bm->bits, *s_src = src;

    if (y >= ye)
        return;
    do {
        xd = x;
        if ((v >> 16) == last_row) {
            src = s_src;
            n = s_n;
            op = s_op;
        } else {
            last_row++;
            while ((v >> 16) != last_row) {
                if (n != 0) {
                    k = bm->w < n ? bm->w : n;
                    if (op == OP_LIT)
                        src += k;
                }
                while (bm->w > n) {
                    if ((len = rd(&src, &op)) < 0)
                        goto advance;
                    n += len;
                    k = len;
                    if (n > bm->w)
                        k -= n - bm->w;
                    if (op == OP_LIT)
                        src += k;
                }
                n -= bm->w;
                last_row++;
            }
            s_src = src;
            s_op = op;
            s_n = n;
        }

        u = u0;
        if (n != 0) {                       /* continuation from the previous source row */
            t = (bm->w < n ? bm->w : n) << 16;
            if (op == OP_RUN) {
                k = NPIX(t);
                u += du * k;
                for (k += xd; xd < k; xd++)
                    PIX(SET_UPIXEL8, *src, xd, y);
            } else if (op == OP_SKIP) {
                k = NPIX(t);
                u += du * k;
                xd += k;
            } else if (op == OP_LIT) {
                for (; u < t; u += du)
                    PIX(SET_UPIXEL8, src[u >> 16], xd++, y);
                src += CEILS(t);
            }
        }
        while (bm->w > n) {
            if ((len = rd(&src, &op)) < 0)
                goto advance;
            ustart = n << 16;
            n += len;
            t = (bm->w < n ? bm->w : n) << 16;
            if (op == OP_RUN) {
                k = NPIX(t - u);
                u += du * k;
                for (k += xd; xd < k; xd++)
                    PIX(SET_PIXEL8, color, xd, y);
            } else if (op == OP_SKIP) {
                k = NPIX(t - u);
                u += du * k;
                xd += k;
            } else if (op == OP_LIT) {
                tt = t - ustart;
                for (i = u - ustart; i < tt; i += du)
                    PIX(SET_PIXEL8, color, xd++, y);
                u = ustart + i;
                src += CEILS(tt);
            }
        }
        n -= bm->w;
    advance:
        v += dv;
    } while (++y < ye);
}

int gen_rsd8_scale_solid_bitmap(grs_bitmap *bm, short x, short y, short w, short h, long color)
{
    fix du = (bm->w << 16) / w, dv = (bm->h << 16) / h;
    fix u_start, uright, v, u, t, ustart, i, tt;
    short x_right = x + w, y_bot = y + h, x_off = 0, y_off = 0, xc = x;
    short n = 0, s_n = 0, last_row = -1, sx0, sxr, yc, xd;
    int op = OP_SKIP, s_op = OP_SKIP, code, len, k;
    uchar *src = bm->bits, *s_src = src;

    if (x > grd_gc.clip.i.right || x_right <= grd_gc.clip.i.left
        || y > grd_gc.clip.i.bot || y_bot <= grd_gc.clip.i.top)
        return CLIP_ALL;
    code = CLIP_NONE;
    if (x < grd_gc.clip.i.left) {
        code = 1;
        x_off = grd_gc.clip.i.left - x;
        xc = grd_gc.clip.i.left;
    }
    if (y < grd_gc.clip.i.top) {
        y_off = grd_gc.clip.i.top - y;
        code |= 2;
    }
    if (x_right >= grd_gc.clip.i.right) {
        code |= 4;
        x_right = grd_gc.clip.i.right;
    }
    if (y_bot > grd_gc.clip.i.bot) {
        code |= 8;
        y_bot = grd_gc.clip.i.bot;
    }
    u_start = (du >> 1) + x_off * du;
    sx0 = (short)(u_start >> 16);
    uright = du * (x_right - x);
    sxr = CEILS(uright);
    v = (dv >> 1) + dv * y_off;
    yc = y_off + y;
    if (yc >= y_bot)
        return code;

    do {
        xd = xc;
        if ((v >> 16) == last_row) {
            src = s_src;
            n = s_n;
            op = s_op;
        } else {
            last_row++;
            while ((v >> 16) != last_row) {
                if (n != 0) {
                    k = n > bm->w ? bm->w : n;
                    if (op == OP_LIT)
                        src += k;
                }
                while (n < bm->w) {
                    if ((len = rd(&src, &op)) < 0)
                        goto advance;
                    n += len;
                    k = len;
                    if (n > bm->w)
                        k -= n - bm->w;
                    if (op == OP_LIT)
                        src += k;
                }
                n -= bm->w;
                last_row++;
            }
            s_src = src;
            s_op = op;
            s_n = n;
        }

        /* skip the source columns left of the clip */
        if (n != 0) {
            k = n > sx0 ? sx0 : n;
            if (op == OP_LIT)
                src += k;
        }
        while (n < sx0) {
            if ((len = rd(&src, &op)) < 0)
                goto advance;
            n += len;
            k = len;
            if (n > sx0)
                k -= n - sx0;
            if (op == OP_LIT)
                src += k;
        }

        u = u_start;
        if (n > sx0) {
            t = n >= sxr ? uright : n << 16;
            if (op == OP_RUN) {
                k = NPIX(t - u);
                u += du * k;
                for (k += xd; xd < k; xd++)
                    PIX(SET_PIXEL8, color, xd, yc);
            } else if (op == OP_SKIP) {
                k = NPIX(t - u);
                u += du * k;
                xd += k;
            } else if (op == OP_LIT) {
                for (; t > u; u += du)
                    PIX(SET_PIXEL8, color, xd++, yc);
                src += CEILS(t) - sx0;
            }
        }
        while (n < sxr) {
            if ((len = rd(&src, &op)) < 0)
                goto advance;
            ustart = n << 16;
            n += len;
            t = n >= sxr ? uright : n << 16;
            if (op == OP_RUN) {
                k = NPIX(t - u);
                u += du * k;
                for (k += xd; xd < k; xd++)
                    PIX(SET_PIXEL8, color, xd, yc);
            } else if (op == OP_SKIP) {
                k = NPIX(t - u);
                u += du * k;
                xd += k;
            } else if (op == OP_LIT) {
                tt = t - ustart;
                for (i = u - ustart; i < tt; i += du)
                    PIX(SET_PIXEL8, color, xd++, yc);
                u = ustart + i;
                src += CEILS(tt);
            }
        }

        /* skip the rest of the source row */
        if (n > sxr) {
            k = (n > bm->w ? bm->w : n) - sxr;
            if (op == OP_LIT)
                src += k;
        }
        while (n < bm->w) {
            if ((len = rd(&src, &op)) < 0)
                goto advance;
            n += len;
            k = len;
            if (n > bm->w)
                k -= n - bm->w;
            if (op == OP_LIT)
                src += k;
        }
        n -= bm->w;
    advance:
        v += dv;
    } while (++yc < y_bot);
    return code;
}
