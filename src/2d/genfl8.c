/* genfl8.c -- x:\prj\tech\libsrc\2d\genfl8.c (Terra Nova v1.09): generic flat8 bitmap drawer
 * (pixel by pixel through the fill function table) and the clipping flat8 bitmap entry.
 * Decompiled from __FF.EXE; same as System Shock's except that the pixel is drawn with
 * grd_function_table[GRC_PIXEL] (the current fill type's DEVICE pixel routine).
 *
 *   0x329F70 gen_flat8_ubitmap   0x32A050 gen_flat8_bitmap
 */
#include "2d_prim.h"

/* bozo flat8 bitmap drawer. */
void gen_flat8_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *src = bm->bits;
    short right = x + bm->w;
    short bot = y + bm->h;
    short cur_x;

    if (bm->flags & BMF_TRANS) {
        for (; y < bot; y++, src += bm->row - bm->w) {
            for (cur_x = x; cur_x < right; cur_x++, src++)
                if (*src)
                    WC3(grd_function_table[GRC_PIXEL], *src, cur_x, y);
        }
    } else {
        for (; y < bot; y++, src += bm->row - bm->w) {
            for (cur_x = x; cur_x < right; cur_x++, src++)
                WC3(grd_function_table[GRC_PIXEL], *src, cur_x, y);
        }
    }
}

/* clip flat8 bitmap against cliprect and jump to unclipped drawer (gr_ubitmap). */
int gen_flat8_bitmap(grs_bitmap *bm, short x, short y)
{
    short w, h;
    uchar *p;
    int extra;
    int code = CLIP_NONE;

    /* save stuff that clipping changes. */
    w = bm->w; h = bm->h; p = bm->bits;

    /* check for trivial reject. */
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
    if (bm->h > 0 && bm->w > 0)
        WC3(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], bm, x, y);

    /* restore bitmap to normal. */
    bm->w = w; bm->h = h; bm->bits = p;
    return code;
}
