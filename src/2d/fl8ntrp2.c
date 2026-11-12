/* fl8ntrp2.c -- x:\prj\tech\libsrc\2d\fl8ntrp2.c, fl8fltr2.c (Terra Nova v1.09): scale a
 * flat8 bitmap up by 2 with interpolation / down by 2 with filtering, through the blend and
 * inverse palettes.  Decompiled from __FF.EXE; same as System Shock's.
 *
 *   0x329980 flat8_interp2_ubitmap   0x329B10 flat8_filter2_ubitmap
 */
#include "2d_prim.h"

/* rgb (gr_bind_rgb layout, components 10 bits apart) -> inverse palette entry */
static uchar ipal_of(grs_rgb a)
{
    uchar *c = grd_ipal;
    a = a >> 5;
    c += a & 0x1f;
    a = a >> 6;
    c += a & 0x3e0;
    a = a >> 6;
    c += a & 0x7c00;
    return *c;
}

/* Assumes bm is flat, assumes canvas is big enough to
   hold it, as it is, unclipped */
void flat8_interp2_ubitmap(grs_bitmap *bm)
{
    grs_rgb a, b;
    int i, j;
    uchar *src, *dst;
    int dd, ds;

    /* Cycle through rows and do horizontal strips */
    dd = 2 * grd_bm.row - grd_bm.w;
    ds = bm->row - bm->w;

    dst = grd_bm.bits;
    src = bm->bits;

    for (j = bm->h; j > 0; j--) {
        for (i = bm->w; i > 0; i--) {
            a = *src;
            src++;
            b = *src;
            *dst = a;
            dst++;
            a = grd_bpal[a];
            b = grd_bpal[b];
            *dst = ipal_of((a >> 1) + (b >> 1));
            dst++;
        }
        dst += dd;
        src += ds;
    }

    /* Cycle through rows and fill in missing strips */
    for (i = bm->w * 2 - 1; i >= 0; i--) {
        src = grd_bm.bits + i;
        for (j = bm->h; j > 1; j--) {
            a = *src;
            src += 2 * grd_bm.row;
            b = *src;
            src -= grd_bm.row;
            a = grd_bpal[a];
            b = grd_bpal[b];
            *src = ipal_of((a >> 1) + (b >> 1));
            src += grd_bm.row;
        }
        src += grd_bm.row;
        *src = 0;
    }
}

/* Assumes bm is flat, assumes canvas is big enough to
   hold it, as it is, unclipped */
void flat8_filter2_ubitmap(grs_bitmap *bm)
{
    grs_rgb a;
    int i, j;
    uchar *src, *dst;
    int ds, dd;

    dst = grd_bm.bits;
    src = bm->bits;

    ds = 2 * bm->row - bm->w;
    dd = grd_bm.row - grd_bm.w;

    /* Cycle through rows and do horizontal strips */
    for (j = bm->h; j > 0; j -= 2) {
        for (i = bm->w; i > 0; i -= 2) {
            a = (grd_bpal[*src] >> 2) & 0x3fc7f8ff;
            src++;
            a += (grd_bpal[*src] >> 2) & 0x3fc7f8ff;
            src += bm->row;
            a += (grd_bpal[*src] >> 2) & 0x3fc7f8ff;
            src--;
            a += (grd_bpal[*src] >> 2) & 0x3fc7f8ff;
            src -= bm->row;
            src += 2;
            *dst = ipal_of(a);
            dst++;
        }
        src += ds;
        dst += dd;
    }
}
