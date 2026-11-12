/* genmono.c -- x:\prj\tech\libsrc\2d\genmono.c (Terra Nova v1.09): monochrome bitmap drawn
 * pixel by pixel, and the clipping monochrome bitmap entry.  Decompiled from __FF.EXE; same as
 * System Shock's: transparent bitmaps go through gen_fill_pixel (Shock gr_set_pixel), opaque ones
 * through grd_function_table[GRC_PIXEL] (Shock gr_set_upixel).
 *
 *   0x32A330 gen_mono_ubitmap   0x32A4A4 gen_mono_bitmap
 */
#include "2d_prim.h"

void gen_mono_ubitmap(grs_bitmap *bm, short x, short y)
{
    short w, h;                         /* working width and height */
    short dst_x;                        /* destination x */
    int bit;                            /* bit from 0-7 in source byte */
    uchar *p_row;                       /* pointer to current row of bitmap */
    uchar *p;                           /* pointer to source byte */

    h = bm->h;
    p_row = bm->bits;
    if (bm->flags & BMF_TRANS) {
        /* transparent bitmap; draw 1's as fcolor, don't draw 0's. */
        while (h-- > 0) {
            bit = bm->align;
            dst_x = x;
            p = p_row;
            w = bm->w;
            while (w-- > 0) {
                if (*p & bitmask[bit])
                    gen_fill_pixel(grd_gc.fcolor, dst_x, y);
                dst_x++;
                if (++bit > 7) {
                    bit = 0;
                    p++;
                }
            }
            y++;
            p_row += bm->row;
        }
    } else {
        /* opaque bitmap; draw 1's as fcolor, 0's as bcolor. */
        while (h-- > 0) {
            bit = bm->align;
            dst_x = x;
            p = p_row;
            w = bm->w;
            while (w-- > 0) {
                if (*p & bitmask[bit])
                    WC3(grd_function_table[GRC_PIXEL], grd_gc.fcolor, dst_x, y);
                else
                    WC3(grd_function_table[GRC_PIXEL], grd_gc.bcolor, dst_x, y);
                dst_x++;
                if (++bit > 7) {
                    bit = 0;
                    p++;
                }
            }
            y++;
            p_row += bm->row;
        }
    }
}

/* clip monochrome bitmap against cliprect and jump to unclipped drawer. */
int gen_mono_bitmap(grs_bitmap *bm, short x, short y)
{
    short w, h;
    uchar align;
    uchar *p;
    int code;

    /* save stuff that clipping changes. */
    w = bm->w; h = bm->h; align = bm->align; p = bm->bits;

    code = gr_clip_mono_bitmap(bm, &x, &y);
    if (code != CLIP_ALL)
        WC3(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * BMT_MONO], bm, x, y);

    /* restore bitmap to normal. */
    bm->w = w; bm->h = h; bm->align = align; bm->bits = p;
    return code;
}
