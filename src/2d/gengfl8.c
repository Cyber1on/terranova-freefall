/* gengfl8.c -- x:\prj\tech\libsrc\2d\gengfl8.c (Terra Nova v1.09): generic capture of a canvas
 * area into a flat8 bitmap (pixel by pixel), and the clipping entry.  Decompiled from __FF.EXE;
 * same as System Shock's.  gr_push_state/gr_pop_state are grd_pixel_table entries, called with
 * the table pointer in eax as the compiled code does.  gen_get_flat8_bitmap calls the unclipped
 * capture even when clipping left nothing (Shock too).
 *
 *   0x32B5A0 gen_get_flat8_ubitmap   0x32B6B0 gen_get_flat8_bitmap
 */
#include "2d_prim.h"

#define get_upixel(x, y) WC2(grd_pixel_table[GET_UPIXEL8], (x), (y))

void gen_get_flat8_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *dst = bm->bits;
    short right = x + bm->w;
    short bot = y + bm->h;
    short cur_x;

    WC1(grd_pixel_table[PUSH_STATE], grd_pixel_table);
    if (bm->flags & BMF_TRANS) {
        for (; y < bot; y++, dst += bm->row - bm->w) {
            for (cur_x = x; cur_x < right; cur_x++, dst++)
                if (get_upixel(cur_x, y))
                    *dst = (uchar)get_upixel(cur_x, y);
        }
    } else {
        for (; y < bot; y++, dst += bm->row - bm->w) {
            for (cur_x = x; cur_x < right; cur_x++, dst++)
                *dst = (uchar)get_upixel(cur_x, y);
        }
    }
    WC1(grd_pixel_table[POP_STATE], grd_pixel_table);
}

int gen_get_flat8_bitmap(grs_bitmap *bm, short x, short y)
{
    short w, h;
    uchar *p;
    int extra;
    int code = CLIP_NONE;

    w = bm->w; h = bm->h; p = bm->bits;

    if (x + bm->w < grd_clip.left || x >= grd_clip.right ||
        y + bm->h < grd_clip.top || y >= grd_clip.bot)
        return CLIP_ALL;

    if (x < grd_clip.left) {            /* off left edge */
        extra = grd_clip.left - x;
        bm->w = w - extra;
        bm->bits += extra;
        x = grd_clip.left;
        code |= CLIP_LEFT;
    }
    if (x + bm->w > grd_clip.right) {   /* off right edge */
        bm->w -= x + bm->w - grd_clip.right;
        code |= CLIP_RIGHT;
    }
    if (y < grd_clip.top) {             /* off top */
        extra = grd_clip.top - y;
        bm->h -= extra;
        bm->bits += bm->row * extra;
        y = grd_clip.top;
        code |= CLIP_TOP;
    }
    if (y + bm->h > grd_clip.bot) {     /* off bottom */
        bm->h -= y + bm->h - grd_clip.bot;
        code |= CLIP_BOT;
    }
    WC3(grd_canvas_table[GET_DEVICE_UBITMAP + 2 * bm->type], bm, x, y);

    bm->w = w; bm->h = h; bm->bits = p;
    return code;
}
