/* clpbm.c -- clip/clpmono.c and clip/clpf24.c (Terra Nova v1.09): mono and flat24 bitmap
 * clippers.  Decompiled from __FF.EXE.
 *
 *   0x34AEA0 gr_clip_mono_bitmap    0x34B030 gr_clip_flat24_bitmap
 *
 * Both clip a bitmap drawn at (*x, *y) to the clip rectangle (fix clip >> 16 for the tests,
 * the int clip words for the stored values), adjusting bm->bits, w, h (and align for mono)
 * and the position; they return CLIP_ALL or the OR of the clipped edges.
 * They differ in the trivial reject: mono rejects a bitmap that only touches the clip
 * rectangle (r <= left, l >= right, b <= top, t >= bot, as System Shock), flat24 only one
 * strictly outside it (l > right, r < left, t > bot, b < top, like gr_clip_flat8_bitmap),
 * which then leaves w or h <= 0.
 */
#include "2d.h"

int gr_clip_mono_bitmap(grs_bitmap *bm, short *x, short *y)
{
    int code = CLIP_NONE;
    int l = *x, r = l + bm->w;
    int t = *y, b = t + bm->h;
    int d;
    uchar align;

    if ((grd_fix_clip.left >> 16) >= r || l >= (grd_fix_clip.right >> 16) ||
        b <= (grd_fix_clip.top >> 16) || t >= (grd_fix_clip.bot >> 16))
        return CLIP_ALL;
    if (l < (grd_fix_clip.left >> 16)) {
        d = (grd_fix_clip.left >> 16) - l;
        bm->w -= d;
        bm->bits += d / 8;
        align = (uchar)(bm->align + d % 8);
        bm->align = align;
        if (align > 7) {                /* carry into the next byte */
            bm->align = align - 8;
            bm->bits++;
        }
        code = CLIP_LEFT;
        *x = grd_int_clip.left;
    }
    if ((grd_fix_clip.right >> 16) < r) {
        /* 16 bit: w -= (*x + w) - right, i.e. w = right - *x */
        bm->w = (short)(bm->w - (short)(*x + bm->w - grd_int_clip.right));
        code |= CLIP_RIGHT;
    }
    if (t < (grd_fix_clip.top >> 16)) {
        d = (grd_fix_clip.top >> 16) - t;
        bm->h -= d;
        bm->bits += d * bm->row;
        code |= CLIP_TOP;
        *y = grd_int_clip.top;
    }
    if ((grd_fix_clip.bot >> 16) < b) {
        bm->h -= b - grd_int_clip.bot;
        code |= CLIP_BOT;
    }
    return code;
}

int gr_clip_flat24_bitmap(grs_bitmap *bm, short *x, short *y)
{
    int code = CLIP_NONE;
    int l = *x, r = l + (bm->w);
    int t = *y, b = t + bm->h;
    int left;

    left = grd_fix_clip.left >> 16;
    if (l > (grd_fix_clip.right >> 16) || r < left ||
        t > (grd_fix_clip.bot >> 16) || b < (grd_fix_clip.top >> 16))
        return CLIP_ALL;
    if (l < left) {
        bm->w -= left - l;
        bm->bits += 3 * (left - l);
        *x = grd_int_clip.left;
        code = CLIP_LEFT;
    }
    if ((grd_fix_clip.right >> 16) < r) {
        bm->w -= (short)(r - grd_int_clip.right);
        code |= CLIP_RIGHT;
    }
    if (t < (grd_fix_clip.top >> 16)) {
        int dt = (grd_fix_clip.top >> 16) - t;
        bm->h -= dt;
        bm->bits += dt * bm->row;
        *y = grd_int_clip.top;
        code |= CLIP_TOP;
    }
    if (b > (grd_fix_clip.bot >> 16)) {
        bm->h -= b - grd_int_clip.bot;
        code |= CLIP_BOT;
    }
    return code;
}
