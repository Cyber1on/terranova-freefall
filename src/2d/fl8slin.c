/* fl8slin.c -- fl8slin.c (Terra Nova v1.09): fixed-point unclipped shaded (palette-index
 * interpolated) lines on a flat8 canvas, one per fill type (pixel table entry GRL_USLINE).
 * Decompiled from __FF.EXE (Watcom C); the driver is Shock's fl8slin.h and the plot macros
 * are Shock's: norm and blend store the index, clut stores parm[index], xor xors it in.
 * FILL_SOLID's usline entry is gri_flat8_uline_ns (a solid line in the fill colour).
 *
 *   0x3359C0 gri_flat8_usline_norm  0x335D80 gri_flat8_usline_clut
 *   0x3361D8 gri_flat8_usline_xor   0x33661C gri_flat8_usline_blend
 */
#include "2d_tmap.h"

#define fix_trunc(a) ((a) & 0xffff0000)
#define fix_abs(a) ((a) < 0 ? -(a) : (a))

enum { S_STORE, S_CLUT, S_XOR };

#define PLOT(x, i) do { uchar *q_ = p + (x);                                     \
        if (mode == S_CLUT) *q_ = ((uchar *)parm)[(i) >> 16];                   \
        else if (mode == S_XOR) *q_ ^= (uchar)((i) >> 16);                      \
        else *q_ = (uchar)((i) >> 16); } while (0)

static void usline(int mode, long parm, grs_vertex *v0, grs_vertex *v1)
{
    fix x0 = v0->x, y0 = v0->y, x1 = v1->x, y1 = v1->y;
    fix i0 = v0->i, i1 = v1->i;
    fix dx, dy, di, t;
    ulong row = (ushort)grd_bm.row;
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

    if (dx > dy) {
        x0 = fix_int(x0);
        x1 = fix_int(x1);
        if (x0 > x1) {
            t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
            t = i0; i0 = i1; i1 = t;
        }
        p = grd_bm.bits + row * fix_int(y0);
        di = tm_fix_div(i1 - i0, dx);
        if (fix_int(y0) == fix_int(y1)) {
            for (; x0 <= x1; x0++, i0 += di)
                PLOT(x0, i0);
        } else if (y0 < y1) {
            dy = tm_fix_div(y1 - y0, dx);
            for (; x0 <= x1; x0++, i0 += di) {
                PLOT(x0, i0);
                y0 += dy;
                p += row & -(ulong)(fix_frac(y0) < dy);
            }
        } else {
            dy = tm_fix_div(y0 - y1, dx);
            for (; x0 <= x1; x0++, i0 += di) {
                PLOT(x0, i0);
                p -= row & -(ulong)(fix_frac(y0) < dy);
                y0 -= dy;
            }
        }
    } else if (dy > dx) {
        y0 = fix_int(y0);
        y1 = fix_int(y1);
        if (y0 > y1) {
            t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
            t = i0; i0 = i1; i1 = t;
        }
        p = grd_bm.bits + row * y0;
        di = tm_fix_div(i1 - i0, dy);
        if (fix_int(x0) == fix_int(x1)) {
            x0 = fix_int(x0);
            for (; y0 <= y1; y0++, p += (ushort)grd_bm.row, i0 += di)
                PLOT(x0, i0);
        } else {
            dx = tm_fix_div(x1 - x0, dy);
            for (; y0 <= y1; y0++, p += (ushort)grd_bm.row, i0 += di) {
                PLOT(fix_int(x0), i0);
                x0 += dx;
            }
        }
    } else {
        x0 = fix_int(x0);
        x1 = fix_int(x1);
        y0 = fix_int(y0);
        y1 = fix_int(y1);
        if (x0 > x1) {
            t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
            t = i0; i0 = i1; i1 = t;
        }
        p = grd_bm.bits + row * y0;
        di = tm_fix_div(i1 - i0, dx);
        if (y0 < y1) {
            for (; y0 <= y1; x0++, y0++, p += (ushort)grd_bm.row, i0 += di)
                PLOT(x0, i0);
        } else {
            for (; y0 >= y1; x0++, y0--, p -= (ushort)grd_bm.row, i0 += di)
                PLOT(x0, i0);
        }
    }
}

void gri_flat8_usline_norm(long c, long parm, grs_vertex *v0, grs_vertex *v1) { usline(S_STORE, parm, v0, v1); }
void gri_flat8_usline_clut(long c, long parm, grs_vertex *v0, grs_vertex *v1) { usline(S_CLUT, parm, v0, v1); }
void gri_flat8_usline_xor(long c, long parm, grs_vertex *v0, grs_vertex *v1) { usline(S_XOR, parm, v0, v1); }
void gri_flat8_usline_blend(long c, long parm, grs_vertex *v0, grs_vertex *v1) { usline(S_STORE, parm, v0, v1); }
