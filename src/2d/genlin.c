/* genlin.c -- genulin.c / genuhlin.c / genuvlin.c / genuslin.c / genuclin.c (Terra Nova v1.09):
 * the generic unclipped lines, which plot each pixel through grd_function_table[GRC_PIXEL] as
 * (c, x, y).  Decompiled from __FF.EXE (Watcom C); the drivers are Shock's (Gen/GENU*LIN.C),
 * whose pixels go through grd_pixel_fill (c, parm, x, y) instead.  TN uses them in the ModeX
 * pixel table (0x36859C).
 *
 *   0x33D280 gri_gen_uline_fill    (c, parm, v0, v1)
 *   0x33E110 gri_gen_uhline_fill   (x0, y, x1, c, parm)   ModeX CLUT / XOR / BLEND
 *   0x33D620 gri_gen_uvline_fill   (x, y0, y1, c, parm)
 *   0x33D660 gri_gen_usline_fill   (c, parm, v0, v1)      colour = the vertices' i, fix_int'ed
 *   0x33D960 gri_gen_ucline_fill   (c, parm, v0, v1)      colour = grd_ipal of the 5:5:5 rgb
 *                                                         (low bytes of u, v, w)
 */
#include "2d_tmap.h"

#define PIX(c, x, y) WATCOM_CALL((void (*)())grd_function_table[0], (long)(c), (long)(short)(x), (long)(short)(y))
#define fix_trunc(a) ((fix)((a) & 0xffff0000))
#define fix_abs(a) ((a) < 0 ? -(a) : (a))
#define IPAL(r, g, b) (((r) >> 19 & 0x1f) | ((g) >> 14 & 0x3e0) | ((b) >> 9 & 0x7c00))

/* the endpoint policy shared by the three vertex lines: the right and bottom ends exclude the
 * ceiling (x - epsilon); returns 0 for an empty line */
static int ends(fix *x0, fix *y0, fix *x1, fix *y1, fix *dx, fix *dy)
{
    if (*x0 < *x1)
        *x1 -= 1;
    else if (*x0 > *x1)
        *x0 -= 1;
    if (*y0 < *y1)
        *y1 -= 1;
    else if (*y0 > *y1)
        *y0 -= 1;
    *dx = fix_abs(fix_trunc(*x1) - fix_trunc(*x0));
    *dy = fix_abs(fix_trunc(*y1) - fix_trunc(*y0));
    return *dx != 0 || *dy != 0;
}

#define SWAP(a, b) (t = a, a = b, b = t)

void gri_gen_uline_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    fix x0 = v0->x, y0 = v0->y, x1 = v1->x, y1 = v1->y, dx, dy, t;

    if (!ends(&x0, &y0, &x1, &y1, &dx, &dy))
        return;
    if (dx > dy) {
        x0 = fix_int(x0);
        x1 = fix_int(x1);
        if (x0 > x1) {
            SWAP(x0, x1);
            SWAP(y0, y1);
        }
        if (fix_int(y0) == fix_int(y1)) {
            for (y0 = fix_int(y1); x0 <= x1; x0++)
                PIX(c, x0, y0);
        } else {
            dy = tm_fix_div(y1 - y0, dx);
            for (; x0 <= x1; x0++, y0 += dy)
                PIX(c, x0, fix_int(y0));
        }
    } else if (dy > dx) {
        y0 = fix_int(y0);
        y1 = fix_int(y1);
        if (y0 > y1) {
            SWAP(x0, x1);
            SWAP(y0, y1);
        }
        if (fix_int(x0) == fix_int(x1)) {
            for (x0 = fix_int(x1); y0 <= y1; y0++)
                PIX(c, x0, y0);
        } else {
            dx = tm_fix_div(x1 - x0, dy);
            for (; y0 <= y1; y0++, x0 += dx)
                PIX(c, fix_int(x0), y0);
        }
    } else {
        x0 = fix_int(x0);
        x1 = fix_int(x1);
        y0 = fix_int(y0);
        y1 = fix_int(y1);
        if (x0 > x1) {
            SWAP(x0, x1);
            SWAP(y0, y1);
        }
        if (y0 < y1) {
            for (; y0 <= y1; x0++, y0++)
                PIX(c, x0, y0);
        } else {
            for (; y0 >= y1; x0++, y0--)
                PIX(c, x0, y0);
        }
    }
}

void gri_gen_uhline_fill(short x0, short y, short x1, long c, long parm)
{
    short t;

    if (x0 > x1)
        SWAP(x0, x1);
    while (x0 <= x1) {
        PIX(c, x0, y);
        x0++;
    }
}

void gri_gen_uvline_fill(short x, short y0, short y1, long c, long parm)
{
    short t;

    if (y0 > y1)
        SWAP(y0, y1);
    while (y0 <= y1) {
        PIX(c, x, y0);
        y0++;
    }
}

void gri_gen_usline_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    fix x0 = v0->x, y0 = v0->y, x1 = v1->x, y1 = v1->y, dx, dy, t;
    fix i0 = v0->i, i1 = v1->i, di;

    if (!ends(&x0, &y0, &x1, &y1, &dx, &dy))
        return;
    if (dx > dy) {
        x0 = fix_int(x0);
        x1 = fix_int(x1);
        if (x0 > x1) {
            SWAP(x0, x1);
            SWAP(y0, y1);
            SWAP(i0, i1);
        }
        di = tm_fix_div(i1 - i0, dx);
        if (fix_int(y0) == fix_int(y1)) {
            for (y0 = fix_int(y1); x0 <= x1; x0++, i0 += di)
                PIX(fix_int(i0), x0, y0);
        } else {
            dy = tm_fix_div(y1 - y0, dx);
            for (; x0 <= x1; x0++, y0 += dy, i0 += di)
                PIX(fix_int(i0), x0, fix_int(y0));
        }
    } else if (dy > dx) {
        y0 = fix_int(y0);
        y1 = fix_int(y1);
        if (y0 > y1) {
            SWAP(x0, x1);
            SWAP(y0, y1);
            SWAP(i0, i1);
        }
        di = tm_fix_div(i1 - i0, dy);
        if (fix_int(x0) == fix_int(x1)) {
            for (x0 = fix_int(x1); y0 <= y1; y0++, i0 += di)
                PIX(fix_int(i0), x0, y0);
        } else {
            dx = tm_fix_div(x1 - x0, dy);
            for (; y0 <= y1; y0++, x0 += dx, i0 += di)
                PIX(fix_int(i0), fix_int(x0), y0);
        }
    } else {
        x0 = fix_int(x0);
        x1 = fix_int(x1);
        y0 = fix_int(y0);
        y1 = fix_int(y1);
        if (x0 > x1) {
            SWAP(x0, x1);
            SWAP(y0, y1);
            SWAP(i0, i1);
        }
        di = tm_fix_div(i1 - i0, dx);
        if (y0 < y1) {
            for (; y0 <= y1; x0++, y0++, i0 += di)
                PIX(fix_int(i0), x0, y0);
        } else {
            for (; y0 >= y1; x0++, y0--, i0 += di)
                PIX(fix_int(i0), x0, y0);
        }
    }
}

void gri_gen_ucline_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    fix x0 = v0->x, y0 = v0->y, x1 = v1->x, y1 = v1->y, dx, dy, t, n;
    fix r, g, b, dr, dg, db;
    uchar r0 = (uchar)v0->u, g0 = (uchar)v0->v, b0 = (uchar)v0->w;
    uchar r1 = (uchar)v1->u, g1 = (uchar)v1->v, b1 = (uchar)v1->w;

    if (!ends(&x0, &y0, &x1, &y1, &dx, &dy))
        return;

#define RGB_SETUP(lt, n_) do {                                                  \
        if (lt) {                                                               \
            r = r0 << 16; g = g0 << 16; b = b0 << 16;                           \
            dr = tm_fix_div((r1 - r0) << 16, n_);                               \
            dg = tm_fix_div((g1 - g0) << 16, n_);                               \
            db = tm_fix_div((b1 - b0) << 16, n_);                               \
        } else {                                                                \
            SWAP(x0, x1);                                                       \
            SWAP(y0, y1);                                                       \
            r = r1 << 16; g = g1 << 16; b = b1 << 16;                           \
            dr = tm_fix_div((r0 - r1) << 16, n_);                               \
            dg = tm_fix_div((g0 - g1) << 16, n_);                               \
            db = tm_fix_div((b0 - b1) << 16, n_);                               \
        }                                                                       \
    } while (0)
#define PLOT(x, y) PIX(grd_ipal[IPAL(r, g, b)], x, y)
#define STEP() (r += dr, g += dg, b += db)

    if (dx > dy) {
        x0 = fix_int(x0);
        x1 = fix_int(x1);
        RGB_SETUP(x0 < x1, dx);
        if (fix_int(y0) == fix_int(y1)) {
            for (y0 = fix_int(y1); x0 <= x1; x0++, STEP())
                PLOT(x0, y0);
        } else {
            n = tm_fix_div(y1 - y0, dx);
            for (; x0 <= x1; x0++, y0 += n, STEP())
                PLOT(x0, fix_int(y0));
        }
    } else if (dy > dx) {
        y0 = fix_int(y0);
        y1 = fix_int(y1);
        RGB_SETUP(y0 < y1, dy);
        if (fix_int(x0) == fix_int(x1)) {
            for (x0 = fix_int(x1); y0 <= y1; y0++, STEP())
                PLOT(x0, y0);
        } else {
            n = tm_fix_div(x1 - x0, dy);
            for (; y0 <= y1; y0++, x0 += n, STEP())
                PLOT(fix_int(x0), y0);
        }
    } else {
        x0 = fix_int(x0);
        x1 = fix_int(x1);
        y0 = fix_int(y0);
        y1 = fix_int(y1);
        RGB_SETUP(x0 < x1, dx);
        if (y0 < y1) {
            for (; y0 <= y1; x0++, y0++, STEP())
                PLOT(x0, y0);
        } else {
            for (; y0 >= y1; x0++, y0--, STEP())
                PLOT(x0, y0);
        }
    }
}
