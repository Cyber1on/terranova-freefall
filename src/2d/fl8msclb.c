/* fl8msclb.c -- x:\prj\tech\libsrc\2d\fl8mscl.c (Terra Nova v1.09): scaled monochrome bitmap
 * into a flat8 canvas, unclipped and clipped.  Decompiled from __FF.EXE; same as System
 * Shock's, quirks kept:
 *  - flat8_mono_scale_ubitmap: the opaque path starts at x_scale>>4 instead of x_scale>>1.
 *  - flat8_mono_scale_bitmap: the opaque path never updates index/bit (Shock source bug).  In the
 *    compiled code "index" lives in edi, which still holds y (the clipped top) and has the row
 *    pointer added to it once per row, and "bit" lives in ebp, which is never set (the caller's
 *    ebp).  We read through the same accumulated pointer and use bit 0; from the second row on
 *    the original (and this) reads a wild address.  Only the first row with the caller's ebp
 *    == 0 can match; the path is unreachable in practice.
 *
 *   0x32AF70 flat8_mono_scale_ubitmap   0x32B1C0 flat8_mono_scale_bitmap
 */
#include "2d_prim.h"

void flat8_mono_scale_ubitmap(grs_bitmap *bm, short x, short y, short w, short h)
{
    fix x_scale, y_scale;              /* scale factors */
    fix x_src, y_src;                  /* fractional position in source bitmap */
    uchar *p_src, *p_dst;
    int index, bit;
    int i;

    if (w == 0 || h == 0)
        return;

    x_scale = (bm->w << 16) / w;
    y_scale = (bm->h << 16) / h;
    y_src = y_scale >> 1;
    p_dst = grd_bm.bits + y * grd_bm.row + x;

    if (bm->flags & BMF_TRANS)
        while (h-- > 0) {
            p_src = bm->bits + fix_int(y_src) * bm->row;
            x_src = (bm->align << 16) + (x_scale >> 1);
            for (i = 0; i < w; i++) {
                index = x_src >> (16 + 3);
                bit = (x_src >> 16) & 0x0007;
                if (p_src[index] & bitmask[bit])
                    *p_dst++ = (uchar)grd_gc.fcolor;
                else
                    p_dst++;
                x_src += x_scale;
            }
            p_dst += grd_bm.row - w;
            y_src += y_scale;
        }
    else
        while (h-- > 0) {
            p_src = bm->bits + fix_int(y_src) * bm->row;
            x_src = (bm->align << 16) + (x_scale >> 4);    /* sic */
            for (i = 0; i < w; i++) {
                index = x_src >> (16 + 3);
                bit = (x_src >> 16) & 0x0007;
                if (p_src[index] & bitmask[bit])
                    *p_dst++ = (uchar)grd_gc.fcolor;
                else
                    *p_dst++ = (uchar)grd_gc.bcolor;
                x_src += x_scale;
            }
            p_dst += grd_bm.row - w;
            y_src += y_scale;
        }
}

int flat8_mono_scale_bitmap(grs_bitmap *bm, short x, short y, short w, short h)
{
    fix x_left;
    fix x_scale, y_scale;
    fix x_src, y_src;
    uchar *p_src, *p_dst;
    int code;
    int index, bit;
    long acc;                          /* the opaque path's "index" register (see above) */
    int i;

    if (w == 0 || h == 0)
        return CLIP_ALL;

    /* check for trivial reject clip. */
    if (x > grd_clip.right || x + w <= grd_clip.left ||
        y > grd_clip.bot || y + h <= grd_clip.top)
        return CLIP_ALL;

    x_scale = (bm->w << 16) / w;
    y_scale = (bm->h << 16) / h;
    x_left = y_src = 0;
    code = CLIP_NONE;

    if (x < grd_clip.left) {
        x_left = x_scale * (grd_clip.left - x);
        w -= grd_clip.left - x;
        x = grd_clip.left;
        code |= CLIP_LEFT;
    }
    if (x + w > grd_clip.right) {
        w = grd_clip.right - x;
        code |= CLIP_RIGHT;
    }
    if (y < grd_clip.top) {
        y_src = y_scale * (grd_clip.top - y);
        h -= grd_clip.top - y;
        y = grd_clip.top;
        code |= CLIP_TOP;
    }
    if (y + h > grd_clip.bot) {
        h = grd_clip.bot - y;
        code |= CLIP_BOT;
    }
    x_left += (bm->align << 16);
    x_left += (x_scale >> 1);
    y_src += y_scale >> 1;
    p_dst = grd_bm.bits + y * grd_bm.row + x;

    if (bm->flags & BMF_TRANS)
        while (h-- > 0) {
            p_src = bm->bits + fix_int(y_src) * bm->row;
            x_src = x_left;
            for (i = w; i > 0; i--) {
                index = x_src >> (16 + 3);
                bit = (x_src >> 16) & 0x0007;
                if (p_src[index] & bitmask[bit])
                    *p_dst++ = (uchar)grd_gc.fcolor;
                else
                    p_dst++;
                x_src += x_scale;
            }
            p_dst += grd_bm.row - w;
            y_src += y_scale;
        }
    else {
        acc = y;
        bit = 0;
        while (h-- > 0) {
            p_src = bm->bits + fix_int(y_src) * bm->row;
            x_src = x_left;
            if (w > 0) acc += (long)p_src;
            for (i = w; i > 0; i--) {
                if (*(uchar *)acc & bitmask[bit])
                    *p_dst++ = (uchar)grd_gc.fcolor;
                else
                    *p_dst++ = (uchar)grd_gc.bcolor;
                x_src += x_scale;
            }
            p_dst += grd_bm.row - w;
            y_src += y_scale;
        }
    }
    return code;
}
