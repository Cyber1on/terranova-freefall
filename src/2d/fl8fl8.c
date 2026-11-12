/* fl8fl8.c -- x:\prj\tech\libsrc\2d\fl8fl8.c (Terra Nova v1.09): flat8 bitmap into a flat8
 * canvas, unclipped.  Decompiled from __FF.EXE; same as System Shock's (C version).
 *
 *   0x329E70 flat8_flat8_ubitmap
 */
#include "2d_prim.h"

void flat8_flat8_ubitmap(grs_bitmap *bm, short x, short y)
{
    short w = bm->w;
    short h = bm->h;
    uchar *src = bm->bits;
    uchar *dst = grd_bm.bits + (long)grd_bm.row * y + x;
    short i;

    if (bm->flags & BMF_TRANS) {
        while (h--) {
            for (i = 0; i < w; i++)
                if (src[i] != 0) dst[i] = src[i];
            src += bm->row;
            dst += grd_bm.row;
        }
    } else {
        while (h--) {
            /* inline memmove: rep movsd of w>>2 dwords, rep movsb of w&3 bytes (forward) */
            int n = bm->w;
            uchar *d = dst, *s = src;
            unsigned k;
            for (k = (unsigned)n >> 2; k; k--, d += 4, s += 4) *(ulong *)d = *(ulong *)s;
            for (k = n & 3; k; k--) *d++ = *s++;
            src += bm->row;
            dst += grd_bm.row;
        }
    }
}
