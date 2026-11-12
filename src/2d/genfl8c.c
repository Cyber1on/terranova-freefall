/* genfl8c.c -- x:\prj\tech\libsrc\2d\genfl8c.c (Terra Nova v1.09): generic flat8 bitmap drawn
 * through a color lookup table, and its clipping entry.  Decompiled from __FF.EXE; same as
 * System Shock's (pixels through grd_pixel_table[SET_UPIXEL8]).  gen_flat8_clut_bitmap calls the
 * unclipped drawer even when clipping left nothing (Shock too).
 *
 *   0x32AA50 gen_flat8_clut_ubitmap   0x32AB64 gen_flat8_clut_bitmap
 */
#include "2d_prim.h"

void gen_flat8_clut_ubitmap(grs_bitmap *bm, short x, short y, uchar *cl)
{
    uchar *src = bm->bits;
    short right = x + bm->w;
    short bot = y + bm->h;
    short cur_x;

    if (cl == NULL) cl = grd_screen->clut;
    if (bm->flags & BMF_TRANS) {
        for (; y < bot; y++, src += bm->row - bm->w) {
            for (cur_x = x; cur_x < right; cur_x++, src++)
                if (*src)
                    WC3(grd_pixel_table[SET_UPIXEL8], cl[*src], cur_x, y);
        }
    } else {
        for (; y < bot; y++, src += bm->row - bm->w) {
            for (cur_x = x; cur_x < right; cur_x++, src++)
                WC3(grd_pixel_table[SET_UPIXEL8], cl[*src], cur_x, y);
        }
    }
}

int gen_flat8_clut_bitmap(grs_bitmap *bm, short x, short y, uchar *cl)
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
    WC4(grd_canvas_table[CLUT_DRAW_DEVICE_UBITMAP + 2 * BMT_FLAT8], bm, x, y, cl);

    bm->w = w; bm->h = h; bm->bits = p;
    return code;
}
