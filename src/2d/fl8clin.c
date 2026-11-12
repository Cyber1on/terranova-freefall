/* fl8clin.c -- fl8clin.c (Terra Nova v1.09): fixed-point unclipped rgb-shaded lines on a flat8
 * canvas (pixel table entry GRL_UCLINE).  Decompiled from __FF.EXE (Watcom C); the driver is
 * Shock's fl8clin.h and the plot macros are Shock's.  TN's table uses the norm version for
 * fills 0-3 (FILL_SOLID's ucline is gri_flat8_uline_ns): each pixel is grd_ipal[] of the
 * interpolated 5:5:5 colour.  The clut, xor and blend versions are linked in but no table or
 * code refers to them (blend is Shock's "punt": the clut plot).  The endpoint colours are the
 * low bytes of the vertices' u, v, w.
 *
 *   0x3369E0 gri_flat8_ucline_norm  0x337340 gri_flat8_ucline_clut (unreferenced)
 *   0x337D30 gri_flat8_ucline_xor (unreferenced)  0x33873C gri_flat8_ucline_blend (unreferenced)
 */
#include "2d_tmap.h"

#define fix_trunc(a) ((a) & 0xffff0000)
#define fix_abs(a) ((a) < 0 ? -(a) : (a))
#define IPAL(r, g, b) (((r) >> 19 & 0x1f) | ((g) >> 14 & 0x3e0) | ((b) >> 9 & 0x7c00))
#define PLOT(x) do { uchar *q_ = p + (x), i_ = grd_ipal[IPAL(r, g, b)];   \
        if (mode == C_CLUT) *q_ = ((uchar *)parm)[i_];                         \
        else if (mode == C_XOR) *q_ ^= i_;                                      \
        else *q_ = i_; } while (0)

enum { C_NORM, C_CLUT, C_XOR };
#define STEP() (r += dr, g += dg, b += db)

static void ucline(int mode, long parm, grs_vertex *v0, grs_vertex *v1)
{
    fix x0 = v0->x, y0 = v0->y, x1 = v1->x, y1 = v1->y;
    fix dx, dy, t, r, g, b, dr, dg, db, n;
    uchar r0 = (uchar)v0->u, g0 = (uchar)v0->v, b0 = (uchar)v0->w;
    uchar r1 = (uchar)v1->u, g1 = (uchar)v1->v, b1 = (uchar)v1->w;
    uchar *p;

    if (x0 < x1)
        x1 -= 1;
    else if (x0 > x1)
        x0 -= 1;
    if (y0 < y1)
        y1 -= 1;
    else if (y0 > y1)
        y0 -= 1;

    dx = fix_trunc(x1) - fix_trunc(x0);
    dx = fix_abs(dx);
    dy = fix_trunc(y1) - fix_trunc(y0);
    dy = fix_abs(dy);
    if (dx == 0 && dy == 0)
        return;

    /* the setup common to the three cases: walk from the lower end of the long axis */
#define SETUP(lt, n_, prow)                                                     \
    if (lt) {                                                                   \
        r = r0 << 16; g = g0 << 16; b = b0 << 16;                               \
        dr = tm_fix_div((r1 - r0) << 16, n_);                                   \
        dg = tm_fix_div((g1 - g0) << 16, n_);                                   \
        db = tm_fix_div((b1 - b0) << 16, n_);                                   \
    } else {                                                                    \
        t = x0; x0 = x1; x1 = t;                                                \
        t = y0; y0 = y1; y1 = t;                                                \
        r = r1 << 16; g = g1 << 16; b = b1 << 16;                               \
        dr = tm_fix_div((r0 - r1) << 16, n_);                                   \
        dg = tm_fix_div((g0 - g1) << 16, n_);                                   \
        db = tm_fix_div((b0 - b1) << 16, n_);                                   \
    }                                                                           \
    p = grd_bm.bits + (ushort)grd_bm.row * (prow)

    if (dx > dy) {
        x0 = fix_int(x0);
        x1 = fix_int(x1);
        SETUP(x0 < x1, dx, fix_int(y0));
        if (fix_int(y0) == fix_int(y1)) {
            for (; x0 <= x1; x0++, STEP())
                PLOT(x0);
        } else if (y0 < y1) {
            dy = tm_fix_div(y1 - y0, dx);
            for (; x0 <= x1; x0++, STEP()) {
                PLOT(x0);
                y0 += dy;
                p += (ushort)grd_bm.row & -(ulong)(fix_frac(y0) < dy);
            }
        } else {
            dy = tm_fix_div(y0 - y1, dx);
            for (; x0 <= x1; x0++, STEP()) {
                PLOT(x0);
                p -= (ushort)grd_bm.row & -(ulong)(fix_frac(y0) < dy);
                y0 -= dy;
            }
        }
    } else if (dy > dx) {
        y0 = fix_int(y0);
        y1 = fix_int(y1);
        SETUP(y0 < y1, dy, y0);
        if (fix_int(x0) == fix_int(x1)) {
            x0 = fix_int(x0);
            for (; y0 <= y1; y0++, p += (ushort)grd_bm.row, STEP())
                PLOT(x0);
        } else {
            n = tm_fix_div(x1 - x0, dy);
            for (; y0 <= y1; y0++, p += (ushort)grd_bm.row, STEP()) {
                PLOT(fix_int(x0));
                x0 += n;
            }
        }
    } else {
        x0 = fix_int(x0);
        x1 = fix_int(x1);
        y0 = fix_int(y0);
        y1 = fix_int(y1);
        SETUP(x0 < x1, dx, y0);
        if (y0 < y1) {
            for (; y0 <= y1; x0++, y0++, p += (ushort)grd_bm.row, STEP())
                PLOT(x0);
        } else {
            for (; y0 >= y1; x0++, y0--, p -= (ushort)grd_bm.row, STEP())
                PLOT(x0);
        }
    }
}

void gri_flat8_ucline_norm(long c, long parm, grs_vertex *v0, grs_vertex *v1) { ucline(C_NORM, parm, v0, v1); }
void gri_flat8_ucline_clut(long c, long parm, grs_vertex *v0, grs_vertex *v1) { ucline(C_CLUT, parm, v0, v1); }
void gri_flat8_ucline_xor(long c, long parm, grs_vertex *v0, grs_vertex *v1) { ucline(C_XOR, parm, v0, v1); }
void gri_flat8_ucline_blend(long c, long parm, grs_vertex *v0, grs_vertex *v1) { ucline(C_CLUT, parm, v0, v1); }
