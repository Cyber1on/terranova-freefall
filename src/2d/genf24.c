/* genf24.c -- x:\prj\tech\libsrc\2d\genf24.c (Terra Nova v1.09): flat24 bitmaps drawn pixel by
 * pixel with grd_pixel_table[SET_UPIXEL24].  Decompiled from __FF.EXE; same as System Shock's.
 *
 *   0x32A560 gen_flat24_ubitmap   0x32A690 gen_flat24_bitmap
 */
#include "2d_prim.h"

void gen_flat24_ubitmap(grs_bitmap *bm, short x0, short y0)
{
    short x, y;
    uchar *p, *lp;

    p = bm->bits;
    if (bm->flags & BMF_TRANS)
        for (y = y0; y < y0 + bm->h; y++) {
            lp = p;
            for (x = x0; x < x0 + bm->w; x++, p += 3)
                if (*((long *)p) & 0x00ffffff)
                    WC3(grd_pixel_table[SET_UPIXEL24], *((long *)p) & 0x00ffffff, x, y);
            p = lp + bm->row;
        }
    else
        for (y = y0; y < y0 + bm->h; y++) {
            lp = p;
            for (x = x0; x < x0 + bm->w; x++, p += 3)
                WC3(grd_pixel_table[SET_UPIXEL24], *((long *)p) & 0x00ffffff, x, y);
            p = lp + bm->row;
        }
}

int gen_flat24_bitmap(grs_bitmap *bm, short x0, short y0)
{
    int r;
    short w, h;
    uchar *b;

    b = bm->bits; w = bm->w; h = bm->h;
    r = gr_clip_flat24_bitmap(bm, &x0, &y0);
    if (r != CLIP_ALL)
        WC3(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * BMT_FLAT24], bm, x0, y0);
    bm->bits = b; bm->w = w; bm->h = h;
    return r;
}
