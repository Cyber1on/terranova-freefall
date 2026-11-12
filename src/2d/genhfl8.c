/* genhfl8.c -- x:\prj\tech\libsrc\2d\genhfl8.c, genchfl8.c (Terra Nova v1.09): generic
 * horizontally flipped flat8 bitmaps (plain and through a clut), drawn pixel by pixel with
 * grd_pixel_table[SET_UPIXEL8], and their clipping entries.  Decompiled from __FF.EXE; same as
 * System Shock's.  The clipped versions call the unclipped one even when nothing is left.
 *
 *   0x32B8E0 gen_hflip_flat8_ubitmap        0x32B96C gen_hflip_flat8_bitmap
 *   0x32BB60 gen_clut_hflip_flat8_ubitmap   0x32BC10 gen_clut_hflip_flat8_bitmap
 */
#include "2d_prim.h"

void gen_hflip_flat8_ubitmap(grs_bitmap *bm, short x, short y)
{
    short r = x + bm->w - 1;           /* right x coordinate */
    short b = y + bm->h - 1;           /* bottom y coordinate */
    short cur_x;
    uchar *src = bm->bits;

    for (; y <= b; y++, src += bm->row - bm->w)
        for (cur_x = r; cur_x >= x; cur_x--, src++)
            WC3(grd_pixel_table[SET_UPIXEL8], *src, cur_x, y);
}

void gen_clut_hflip_flat8_ubitmap(grs_bitmap *bm, short x, short y, uchar *cl)
{
    short r, b, cur_x;
    uchar *src;

    if (cl == NULL) cl = grd_screen->clut;
    r = x + bm->w - 1;
    b = y + bm->h - 1;
    src = bm->bits;
    for (; y <= b; y++, src += bm->row - bm->w)
        for (cur_x = r; cur_x >= x; cur_x--, src++)
            WC3(grd_pixel_table[SET_UPIXEL8], cl[*src], cur_x, y);
}

/* shared clipping of both entries; returns -1 for trivial reject */
static int hflip_clip(grs_bitmap *bm, short *px, short *py)
{
    short x = *px, y = *py;
    short r = x + bm->w - 1;
    short b = y + bm->h - 1;
    int extra;
    int code = CLIP_NONE;

    if (x >= grd_clip.right || r < grd_clip.left ||
        y >= grd_clip.bot || b < grd_clip.top)
        return -1;

    if (x < grd_clip.left) {            /* off left */
        bm->w -= grd_clip.left - x;
        x = grd_clip.left;
        code |= CLIP_LEFT;
    }
    if (r >= grd_clip.right) {          /* off right */
        extra = r - grd_clip.right + 1;
        bm->w -= extra;
        bm->bits += extra;
        code |= CLIP_RIGHT;
    }
    if (y < grd_clip.top) {             /* off top */
        extra = grd_clip.top - y;
        bm->h -= extra;
        bm->bits += bm->row * extra;
        y = grd_clip.top;
        code |= CLIP_TOP;
    }
    if (b >= grd_clip.bot) {            /* off bottom */
        bm->h -= b - grd_clip.bot + 1;
        code |= CLIP_BOT;
    }
    *px = x; *py = y;
    return code;
}

int gen_hflip_flat8_bitmap(grs_bitmap *bm, short x, short y)
{
    short w = bm->w, h = bm->h;
    uchar *p = bm->bits;
    int code = hflip_clip(bm, &x, &y);

    if (code < 0) return CLIP_ALL;
    WC3(grd_canvas_table[HFLIP_FLAT8_UBITMAP], bm, x, y);
    bm->w = w; bm->h = h; bm->bits = p;
    return code;
}

int gen_clut_hflip_flat8_bitmap(grs_bitmap *bm, short x, short y, uchar *cl)
{
    short w = bm->w, h = bm->h;
    uchar *p = bm->bits;
    int code = hflip_clip(bm, &x, &y);

    if (code < 0) return CLIP_ALL;
    WC4(grd_canvas_table[CLUT_HFLIP_FLAT8_UBITMAP], bm, x, y, cl);
    bm->w = w; bm->h = h; bm->bits = p;
    return code;
}
