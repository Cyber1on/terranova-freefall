/* fl8gfl8.c -- x:\prj\tech\libsrc\2d\fl8gfl8.c (Terra Nova v1.09): capture a flat8 canvas area
 * into a flat8 bitmap, unclipped.  Decompiled from __FF.EXE; same as System Shock's.
 *
 *   0x32B510 flat8_get_flat8_ubitmap
 */
#include "2d_prim.h"

void flat8_get_flat8_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *src, *dst;
    short h = bm->h;

    src = grd_bm.bits + grd_bm.row * y + x;
    dst = bm->bits;
    while (h--) {
        /* inline memmove: rep movsd of w>>2 dwords, rep movsb of w&3 bytes (forward) */
        int n = bm->w;
        uchar *d = dst, *s = src;
        unsigned k;
        for (k = (unsigned)n >> 2; k; k--, d += 4, s += 4) *(ulong *)d = *(ulong *)s;
        for (k = n & 3; k; k--) *d++ = *s++;
        src += grd_bm.row;
        dst += bm->row;
    }
}
