/* fl8mono.c -- x:\prj\tech\libsrc\2d\fl8mono.c (Terra Nova v1.09): monochrome bitmap into a
 * flat8 canvas, unclipped.  Decompiled from __FF.EXE; same as System Shock's.  (The compiled
 * code keeps grd_canvas in a register and stores it back to grd_canvas on return; no effect.)
 *
 *   0x32A1F0 flat8_mono_ubitmap
 */
#include "2d_prim.h"

void flat8_mono_ubitmap(grs_bitmap *bm, short x, short y)
{
    short w, h;                /* working width and height */
    int bit;                   /* bit from 0-7 in source byte */
    uchar *p_row;              /* pointer to current row of bitmap */
    uchar *p_src;              /* pointer to source byte */
    uchar *p_dst;

    h = bm->h;
    p_row = bm->bits;
    p_dst = grd_bm.bits + y * grd_bm.row + x;

    if (bm->flags & BMF_TRANS) {
        /* transparent bitmap; draw 1's as fcolor, don't draw 0's. */
        while (h-- > 0) {
            bit = bm->align;
            p_src = p_row;
            w = bm->w;
            while (w-- > 0) {
                if (*p_src & bitmask[bit])
                    *p_dst++ = (uchar)grd_gc.fcolor;
                else
                    p_dst++;
                if (++bit > 7) {
                    bit = 0;
                    p_src++;
                }
            }
            p_dst += grd_bm.row - bm->w;
            p_row += bm->row;
        }
    } else {
        /* opaque bitmap; draw 1's as fcolor, 0's as bcolor. */
        while (h-- > 0) {
            bit = bm->align;
            p_src = p_row;
            w = bm->w;
            while (w-- > 0) {
                if (*p_src & bitmask[bit])
                    *p_dst++ = (uchar)grd_gc.fcolor;
                else
                    *p_dst++ = (uchar)grd_gc.bcolor;
                if (++bit > 7) {
                    bit = 0;
                    p_src++;
                }
            }
            p_dst += grd_bm.row - bm->w;
            p_row += bm->row;
        }
    }
}
