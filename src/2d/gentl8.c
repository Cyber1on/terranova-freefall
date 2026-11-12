/* gentl8.c -- x:\prj\tech\libsrc\2d\gentl8.c (Terra Nova v1.09): generic translucent (tluc8)
 * bitmap drawer and its clipping entry.  Decompiled from __FF.EXE; same as System Shock's, with
 * the pixel get/set through grd_pixel_table and the translucency tables through tluc8tab_p.
 * gen_tluc8_bitmap calls the unclipped drawer even when clipping left nothing (Shock too).
 *
 *   0x32A700 gen_tluc8_ubitmap   0x32A8C0 gen_tluc8_bitmap
 */
#include "2d_prim.h"

#define set_upixel(c, x, y) WC3(grd_pixel_table[SET_UPIXEL8], (c), (x), (y))
#define get_upixel(x, y) ((uchar)WC2(grd_pixel_table[GET_UPIXEL8], (x), (y)))

/* bozo tluc8 bitmap drawer. */
void gen_tluc8_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *src = bm->bits;
    short right = x + bm->w;
    short bot = y + bm->h;
    short cur_x;

    if (bm->flags & BMF_TRANS) {
        for (; y < bot; y++, src += bm->row - bm->w) {
            for (cur_x = x; cur_x < right; cur_x++, src++)
                if (*src) {
                    uchar **t = &tluc8tab_p[*src];
                    if (*t == NULL)
                        set_upixel(*src, cur_x, y);
                    else {
                        uchar g = get_upixel(cur_x, y);
                        set_upixel((*t)[g], cur_x, y);
                    }
                }
        }
    } else {
        for (; y < bot; y++, src += bm->row - bm->w) {
            for (cur_x = x; cur_x < right; cur_x++, src++) {
                uchar **t = &tluc8tab_p[*src];
                if (*t == NULL)
                    set_upixel(*src, cur_x, y);
                else {
                    uchar g = get_upixel(cur_x, y);
                    set_upixel((*t)[g], cur_x, y);
                }
            }
        }
    }
}

/* clip tluc8 bitmap against cliprect and jump to unclipped drawer. */
int gen_tluc8_bitmap(grs_bitmap *bm, short x, short y)
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
    WC3(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * BMT_TLUC8], bm, x, y);

    bm->w = w; bm->h = h; bm->bits = p;
    return code;
}
