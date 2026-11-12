/* fl8hfl8.c -- x:\prj\tech\libsrc\2d\fl8hfl8.c, fl8chfl8.c (Terra Nova v1.09): horizontally
 * flipped flat8 bitmap into a flat8 canvas, unclipped, plain and through a clut.  Decompiled
 * from __FF.EXE; same as System Shock's (the width is re-read from the bitmap every row).
 *
 *   0x32B840 flat8_hflip_flat8_ubitmap   0x32BAC0 flat8_clut_hflip_flat8_ubitmap
 */
#include "2d_prim.h"

void flat8_hflip_flat8_ubitmap(grs_bitmap *bm, short x, short y)
{
    short w, h;
    uchar *src, *dst;

    h = bm->h;
    src = bm->bits;
    dst = grd_bm.bits + y * grd_bm.row + x + bm->w - 1;
    while (h--) {
        w = bm->w;
        while (w--)
            *dst-- = *src++;
        src += bm->row - bm->w;
        dst += grd_bm.row + bm->w;
    }
}

void flat8_clut_hflip_flat8_ubitmap(grs_bitmap *bm, short x, short y, uchar *cl)
{
    short w, h;
    uchar *src, *dst;

    if (cl == NULL) cl = grd_screen->clut;
    h = bm->h;
    src = bm->bits;
    dst = grd_bm.bits + y * grd_bm.row + x + bm->w - 1;
    while (h--) {
        w = bm->w;
        while (w--)
            *dst-- = cl[*src++];
        src += bm->row - bm->w;
        dst += grd_bm.row + bm->w;
    }
}
