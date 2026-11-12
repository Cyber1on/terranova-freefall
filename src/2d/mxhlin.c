/* mxhlin.c -- Terra Nova v1.09 ModeX canvas: the solid horizontal line (ModeX pixel table
 * 0x36859C GRL_UHLINE for FILL_NORM and FILL_SOLID; the other fills use gri_gen_uhline_fill).
 * Decompiled from __FF.EXE (Watcom C); name ours.
 *
 *   0x33D4C0 modex_uhline_fill (x0, y, x1, c, parm)
 *
 * Masked writes for the partial first and last columns, then all four planes at once for the
 * columns between.  Quirks kept: FILL_SOLID's line still draws c, not parm; the middle is a rep
 * stosd / stosb whose dword is built as ((c & 0xffff00ff) | (c & 0xff) << 8) twice.
 */
#include "2d_tmap.h"

void modex_uhline_fill(short x0, short y, short x1, long c, long parm)
{
    short t, c0, c1, n;
    uchar *p;
    ulong a, d, i;

    if (x0 > x1) {
        t = x0; x0 = x1; x1 = t;
    }
    x0 += grd_bm.align;
    x1 += grd_bm.align;
    p = grd_bm.bits + (ushort)grd_bm.row * y;
    c0 = x0 >> 2;
    c1 = x1 >> 2;
    if (c0 == c1) {
        MX_WMASK(grd_mx_lmask[x0 & 3] & grd_mx_rmask[x1 & 3]);
        p[c0] = (uchar)c;
        return;
    }
    MX_WMASK(grd_mx_lmask[x0 & 3]);
    p[c0] = (uchar)c;
    MX_WMASK(grd_mx_rmask[x1 & 3]);
    c0++;
    p[c1] = (uchar)c;
    if (c0 >= c1)
        return;
    MX_WMASK(0xf);
    p += c0;
    n = c1 - c0;
    a = ((ulong)c & 0xffff00ff) | (((ulong)c & 0xff) << 8);
    d = (a << 16) | a;
    for (i = 0; i < ((ulong)(long)n >> 2); i++, p += 4)
        *(ulong *)p = d;
    for (i = 0; i < ((ulong)(long)n & 3); i++)
        *p++ = (uchar)c;
}
