/* fl8uline.c -- x:\prj\tech\libsrc\2d\fl8lin.c (Terra Nova v1.09): fixed-point unclipped lines
 * on a flat8 canvas, one per fill type (pixel table entry GRL_ULINE).  Decompiled from
 * __FF.EXE (Watcom C); the driver is Shock's fl8lin.h, the per-fill pixel macros are Shock's.
 *
 *   0x3341C0 gri_flat8_uline_ns     (FILL_NORM and FILL_SOLID)
 *   0x3344C4 gri_flat8_uline_clut   0x3347BC gri_flat8_uline_xor   0x334AC0 gri_flat8_uline_blend
 *
 * Quirks kept: the xor and blend versions skip the last pixel of a horizontal run (their row
 * macro loops while x0 < x1); the xor version maps the colour through parm as a clut first
 * (Shock never redefined the init macro after the clut version); blend indexes grd_bpal with
 * the whole colour argument; the norm row fill stores dwords built as (c | (c & 0xff) << 8)
 * twice, so colour bits above 7 leak into bytes 2 and 3 of each dword.
 */
#include "2d_tmap.h"

extern grs_rgb *grd_bpal;                 /* 0x35F8C4 */

#define fix_trunc(a) ((a) & 0xffff0000)
#define fix_abs(a) ((a) < 0 ? -(a) : (a))

static fix fm(fix a, fix b) { return (fix)(((long long)a * b) >> 16); }

static uchar blend(long c, uchar d, fix parm)
{
    grs_rgb prev = grd_bpal[d], nw = grd_bpal[c];
    fix r1 = fm((fix)((nw & 0x3ff) << 12), parm) + fm((fix)((prev & 0x3ff) << 12), 0x10000 - parm);
    fix g1 = fm((fix)((nw & 0x1ff800) << 1), parm) + fm((fix)((prev & 0x1ff800) << 1), 0x10000 - parm);
    fix b1 = fm((fix)((nw & 0xffc00000) >> 10), parm) + fm((fix)((prev & 0xffc00000) >> 10), 0x10000 - parm);
    return grd_ipal[((r1 >> 17) & 0x1f) + ((g1 >> 12) & 0x3e0) + ((b1 >> 7) & 0x7c00)];
}

enum { F_NS, F_CLUT, F_XOR, F_BLEND };

#define PIX(i) do { uchar *q_ = p + (i);                                        \
        if (mode == F_XOR) *q_ ^= (uchar)c;                                     \
        else if (mode == F_BLEND) *q_ = blend(c, *q_, parm);             \
        else *q_ = (uchar)c; } while (0)

static void uline(int mode, long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    fix x0 = v0->x, y0 = v0->y, x1 = v1->x, y1 = v1->y;
    fix dx, dy, t;
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

    if (mode == F_NS) {
        if (grd_gc.fill_type == FILL_SOLID)
            c = (uchar)parm;
    } else if (mode == F_CLUT || mode == F_XOR)
        c = ((uchar *)parm)[c];             /* xor: Shock's clut init macro left in place */

    if (dx > dy) {
        x0 = fix_int(x0);
        x1 = fix_int(x1);
        if (x0 > x1) {
            t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
        }
        p = grd_bm.bits + row * fix_int(y0);
        if (fix_int(y0) == fix_int(y1)) {
            if (mode == F_XOR || mode == F_BLEND) {
                for (; x0 < x1; x0++)
                    PIX(x0);
            } else {
                /* rep stosd/stosb of the colour with ah = al: the high bytes of the dwords
                 * are (al | c bits 16-23) and (al | c bits 24-31) */
                ulong a = ((ulong)c & 0xffff00ff) | (((ulong)c & 0xff) << 8);
                ulong d = (a << 16) | a, n = (ulong)(x1 - x0 + 1), i;
                uchar *q = p + x0;
                for (i = 0; i < (n >> 2); i++, q += 4)
                    *(ulong *)q = d;
                for (i = 0; i < (n & 3); i++)
                    *q++ = (uchar)c;
            }
        } else if (y0 < y1) {
            dy = tm_fix_div(y1 - y0, dx);
            while (x0 <= x1) {
                PIX(x0);
                x0++;
                y0 += dy;
                p += row & -(ulong)(fix_frac(y0) < dy);
            }
        } else {
            dy = tm_fix_div(y0 - y1, dx);
            while (x0 <= x1) {
                PIX(x0);
                x0++;
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
        }
        p = grd_bm.bits + row * y0;
        if (fix_int(x0) == fix_int(x1)) {
            x0 = fix_int(x0);
            while (y0 <= y1) {
                PIX(x0);
                y0++;
                p += row;
            }
        } else {
            dx = tm_fix_div(x1 - x0, dy);
            while (y0 <= y1) {
                PIX(fix_int(x0));
                x0 += dx;
                y0++;
                p += row;
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
        }
        p = grd_bm.bits + row * y0;
        if (y0 < y1) {
            while (y0 <= y1) {
                PIX(x0);
                x0++;
                y0++;
                p += row;
            }
        } else {
            while (y0 >= y1) {
                PIX(x0);
                x0++;
                y0--;
                p -= row;
            }
        }
    }
}

void gri_flat8_uline_ns(long c, long parm, grs_vertex *v0, grs_vertex *v1) { uline(F_NS, c, parm, v0, v1); }
void gri_flat8_uline_clut(long c, long parm, grs_vertex *v0, grs_vertex *v1) { uline(F_CLUT, c, parm, v0, v1); }
void gri_flat8_uline_xor(long c, long parm, grs_vertex *v0, grs_vertex *v1) { uline(F_XOR, c, parm, v0, v1); }
void gri_flat8_uline_blend(long c, long parm, grs_vertex *v0, grs_vertex *v1) { uline(F_BLEND, c, parm, v0, v1); }
