/* genpix.c -- generic pixel entry points and the flat8 bitmap clipper (Terra Nova v1.09).
 * Decompiled from __FF.EXE.  System Shock files: gen/genpix.c (gen_fill_pixel etc.),
 * clip/clpf8.c (gr_clip_flat8_bitmap, not in the Shock source).
 *
 *   0x2FAD20 gen_fill_pixel           0x2FAD60 gen_set_pixel
 *   0x2FADA0 gen_set_pixel_interrupt  0x2FADE0 gr_clip_flat8_bitmap
 *
 * Each pixel function returns CLIP_ALL for a point outside the clip rectangle, else calls the
 * current table's routine and returns 0: gen_fill_pixel via grd_function_table[0]
 * (FILL_PIXEL of the current fill type), the set_pixel ones via grd_pixel_table (canvas table
 * of the bitmap's own type) entries SET_PIXEL8 (0) and SET_PIXEL8_INTERRUPT (11).
 */
#include "2d.h"

#define SET_PIXEL8 0
#define SET_PIXEL8_INTERRUPT 11

#define PIX_CLIPPED(x, y) ((x) < grd_int_clip.left || (x) >= grd_int_clip.right || \
                           (y) < grd_int_clip.top || (y) >= grd_int_clip.bot)

int gen_fill_pixel(long c, short x, short y)
{
    if (PIX_CLIPPED(x, y))
        return CLIP_ALL;
    WATCOM_CALL(grd_function_table[0], c, x, y);
    return CLIP_NONE;
}

int gen_set_pixel(long c, short x, short y)
{
    if (PIX_CLIPPED(x, y))
        return CLIP_ALL;
    WATCOM_CALL(grd_pixel_table[SET_PIXEL8], c, x, y);
    return CLIP_NONE;
}

int gen_set_pixel_interrupt(long c, short x, short y)
{
    if (PIX_CLIPPED(x, y))
        return CLIP_ALL;
    WATCOM_CALL(grd_pixel_table[SET_PIXEL8_INTERRUPT], c, x, y);
    return CLIP_NONE;
}

/* Clips a flat8 bitmap drawn at (*x, *y) to the clip rectangle, adjusting bm (bits, w, h) and
 * the position.  Returns the clip code.  Unlike the mono/flat24 clippers (System Shock),
 * the trivial rejects are l > right, left > r, t > bot, b < top (a bitmap that only touches
 * the clip edge is not rejected; it ends up with w or h <= 0). */
int gr_clip_flat8_bitmap(grs_bitmap *bm, short *x, short *y)
{
    int code = CLIP_NONE;
    int l = *x, r = l + bm->w;
    int t = *y, b = t + bm->h;
    int left = grd_fix_clip.left >> 16, right = grd_fix_clip.right >> 16;
    int top = grd_fix_clip.top >> 16, bot = grd_fix_clip.bot >> 16;

    if (l > right || left > r || t > bot || b < top)
        return CLIP_ALL;
    if (l < left) {
        bm->w -= left - l;
        bm->bits += left - l;
        *x = grd_int_clip.left;
        code = CLIP_LEFT;
    }
    if ((grd_fix_clip.right >> 16) < r) {
        bm->w -= r - grd_int_clip.right;
        code |= CLIP_RIGHT;
    }
    top = grd_fix_clip.top >> 16;
    if (t < top) {
        bm->h -= top - t;
        bm->bits += (top - t) * bm->row;
        *y = grd_int_clip.top;
        code |= CLIP_TOP;
    }
    if ((grd_fix_clip.bot >> 16) < b) {
        bm->h -= b - grd_int_clip.bot;
        code |= CLIP_BOT;
    }
    return code;
}
