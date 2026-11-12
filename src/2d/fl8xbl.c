/* fl8xbl.c -- Terra Nova v1.09: unclipped xor blit of a flat8 bitmap onto a flat8 canvas
 * (function table FILL_XOR / BMT_FLAT8 / GRC_BITMAP; Shock has no counterpart, name ours).
 * Decompiled from __FF.EXE (Watcom C).
 *
 *   0x334140 gri_flat8_xor_ubitmap
 *
 * Quirk kept: the row loop is a do-while on the (sign-extended) width, so a bitmap with
 * w <= 0 xors 2^32 + w bytes per row (the original faults).
 */
#include "2d_tmap.h"

void gri_flat8_xor_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *src = bm->bits;
    uchar *dst = grd_bm.bits + y * (ulong)(ushort)grd_bm.row + x;
    short w = bm->w;
    ushort h = bm->h;

    while (h-- != 0) {
        uchar *s = src, *d = dst;
        ulong n = (ulong)(long)w;

        do
            *d++ ^= *s++;
        while (--n != 0);
        src += (ushort)bm->row;
        dst += (ushort)grd_bm.row;
    }
}
