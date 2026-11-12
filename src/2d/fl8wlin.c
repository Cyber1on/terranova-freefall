/* fl8wlin.c -- fl8wlin.c / fl8wclin.c / genwlin.c / genwclin.c (Terra Nova v1.09): the lines
 * that wire-frame polygons are drawn with (pixel table entries GRL_WIRE_POLY_LINE and
 * GRL_WIRE_POLY_CLINE).  Decompiled from __FF.EXE (Watcom C); the drivers are Shock's (the
 * flat8 rgb ones are commented out in the Shock tree but are in TN).  Each row of the line is
 * a horizontal run from the previous x to the next, so adjacent edges overdraw consistently.
 *
 *   0x339130 gri_flat8_wire_poly_uline         (fills NORM, CLUT, SOLID; and SOLID's cline)
 *   0x339380 gri_flat8_wire_poly_ucline_norm   0x339998 gri_flat8_wire_poly_ucline_clut
 *   0x33A020 gri_gen_wire_poly_uline           0x33A280 gri_gen_wire_poly_ucline
 *            (XOR and BLEND on flat8, every fill on the device tables: each pixel goes
 *             through grd_function_table[GRC_PIXEL] as (c, x, y))
 *
 * Differences from Shock: fix_cint is a short (Watcom's fix.h), and the colour is a byte: the
 * flat8 uline's FILL_CLUT lookup indexes parm with the low byte of c.
 */
#include "2d_tmap.h"

#define CINT(a) ((int)(short)(((a) + 0xffff) >> 16))
#define CEIL(a) (((a) + 0xffff) & 0xffff0000)
#define IPAL(r, g, b) (((r) >> 19 & 0x1f) | ((g) >> 14 & 0x3e0) | ((b) >> 9 & 0x7c00))

static fix wfix_mul(fix a, fix b) { return (fix)(((long long)a * b) >> 16); }

enum { W_FL8, W_FL8_RGB, W_FL8_RGB_CLUT, W_GEN, W_GEN_RGB };

#define RGB(m) ((m) == W_FL8_RGB || (m) == W_FL8_RGB_CLUT || (m) == W_GEN_RGB)

/* one pixel at (x, y); the rgb modes step the colour after it */
#define PIX(xx) do {                                                            \
        if (mode == W_FL8)                                                      \
            p[xx] = c8;                                                         \
        else if (mode == W_GEN)                                                 \
            WATCOM_CALL((void (*)())grd_function_table[0], (long)c8, (long)(short)(xx), (long)(short)y);              \
        else {                                                                  \
            uchar i_ = grd_ipal[IPAL(r0, g0, b0)];                              \
            if (mode == W_FL8_RGB) p[xx] = i_;                                  \
            else if (mode == W_FL8_RGB_CLUT) p[xx] = ((uchar *)parm)[i_];       \
            else WATCOM_CALL((void (*)())grd_function_table[0], (long)i_, (long)(short)(xx), (long)(short)y);         \
            r0 += dr; g0 += dg; b0 += db;                                       \
        }                                                                       \
    } while (0)
#define HLINE_INC() do { PIX(x); x++; } while (x < x_new)
#define HLINE_DEC() do {                                                        \
        if (x == x_new)                                                         \
            PIX(x);                                                             \
        else                                                                    \
            do { x--; PIX(x); } while (x > x_new);                              \
    } while (0)

static void wire_line(int mode, uchar c8, long parm, grs_vertex *v0, grs_vertex *v1)
{
    int d, y, y_max, x, x_new;
    fix x0, y0, x1, y1, x_fix, dx;
    fix r0 = 0, g0 = 0, b0 = 0, r1 = 0, g1 = 0, b1 = 0, dr = 0, dg = 0, db = 0;
    uchar *p = NULL;

    if (v1->y > v0->y) {
        y = CINT(v0->y);
        y_max = CINT(v1->y);
    } else {
        y = CINT(v1->y);
        y_max = CINT(v0->y);
    }
    if (mode < W_GEN)
        p = grd_bm.bits + y * (ulong)(ushort)grd_bm.row;

    if (y_max - y <= 1) {                       /* horizontal */
        if (v1->x > v0->x) {
            x_new = CINT(v1->x); x = CINT(v0->x);
            if (RGB(mode)) {
                r0 = v0->u << 16; g0 = v0->v << 16; b0 = v0->w << 16;
                r1 = v1->u << 16; g1 = v1->v << 16; b1 = v1->w << 16;
            }
        } else {
            x_new = CINT(v0->x); x = CINT(v1->x);
            if (RGB(mode)) {
                r0 = v1->u << 16; g0 = v1->v << 16; b0 = v1->w << 16;
                r1 = v0->u << 16; g1 = v0->v << 16; b1 = v0->w << 16;
            }
        }
        if (RGB(mode)) {
            d = x_new - x;
            if (d <= 0)
                return;
            dr = (r1 - r0) / d;
            dg = (g1 - g0) / d;
            db = (b1 - b0) / d;
        }
        HLINE_INC();
        return;
    }

    if (v1->y > v0->y) {
        y0 = v0->y; x0 = v0->x;
        y1 = v1->y; x1 = v1->x;
        if (RGB(mode)) {
            r0 = v0->u << 16; g0 = v0->v << 16; b0 = v0->w << 16;
            r1 = v1->u << 16; g1 = v1->v << 16; b1 = v1->w << 16;
        }
    } else {
        y1 = v0->y; x1 = v0->x;
        y0 = v1->y; x0 = v1->x;
        if (RGB(mode)) {
            r1 = v0->u << 16; g1 = v0->v << 16; b1 = v0->w << 16;
            r0 = v1->u << 16; g0 = v1->v << 16; b0 = v1->w << 16;
        }
    }
    dx = tm_fix_div(x1 - x0, y1 - y0);
    if (RGB(mode)) {
        if ((dx < 0 ? -dx : dx) > 0x10000) {
            d = CINT(x1) - CINT(x0);
            if (d < 0)
                d = -d;
        } else
            d = y_max - y;
        dr = (r1 - r0) / d;
        dg = (g1 - g0) / d;
        db = (b1 - b0) / d;
    }
    x = CINT(x0);
    x_fix = x0 + wfix_mul(CEIL(y0) - y0, dx);
    x_new = CINT(x_fix);

    if (dx >= 0) {
        HLINE_INC();
        do {
            x = x_new;
            x_new = CINT(x_fix += dx);
            HLINE_INC();
            if (mode < W_GEN)
                p += (ushort)grd_bm.row;
        } while (++y < y_max - 1);
        x = x_new;
        x_new = CINT(x1);
        HLINE_INC();
    } else {
        do {
            HLINE_DEC();
            x_new = CINT(x_fix += dx);
            if (mode < W_GEN)
                p += (ushort)grd_bm.row;
        } while (++y < y_max - 1);
        x_new = CINT(x1);
        HLINE_DEC();
    }
}

void gri_flat8_wire_poly_uline(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    uchar c8 = (uchar)c;

    if (grd_gc.fill_type == FILL_SOLID)
        c8 = (uchar)parm;
    else if (grd_gc.fill_type == FILL_CLUT)
        c8 = ((uchar *)parm)[c8];
    wire_line(W_FL8, c8, parm, v0, v1);
}

void gri_flat8_wire_poly_ucline_norm(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    wire_line(W_FL8_RGB, 0, parm, v0, v1);
}

void gri_flat8_wire_poly_ucline_clut(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    wire_line(W_FL8_RGB_CLUT, 0, parm, v0, v1);
}

void gri_gen_wire_poly_uline(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    wire_line(W_GEN, (uchar)c, parm, v0, v1);
}

void gri_gen_wire_poly_ucline(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    wire_line(W_GEN_RGB, 0, parm, v0, v1);
}
