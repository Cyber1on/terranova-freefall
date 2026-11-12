/* fl8dbl.c -- x:\prj\tech\libsrc\2d\fl8dbl.asm (Terra Nova v1.09): flat8 bitmaps doubled into
 * the canvas / into another bitmap, horizontally and smoothed.  The originals are hand written,
 * self-modifying assembler (they patch the source address, the start index and the blend table
 * into their own instructions; all registers are preserved).  This is their effect in C:
 *
 *   0x32BD70 flat8_flat8_h_double_ubitmap(bm): canvas row r, pixels 2i and 2i+1 = bm row r,
 *            pixel i (written as aligned words, or byte+words for an odd start; same result).
 *   0x32BE3A flat8_flat8_smooth_h_double_ubitmap(src, dst): dst pixel 2i = src i, 2i+1 =
 *            grd_half_blend[src[i] << 8 | src[i+1]], the last pixel doubled.  Returns at once
 *            when grd_half_blend is NULL.
 *   0x32BEE1 flat8_flat8_smooth_hv_double_ubitmap(src, dst): smooth_h into every second row of
 *            dst (dst->row doubled for the call), then odd rows = grd_half_blend[below << 8 |
 *            above], the last row copied.  Quirk: dst->row is stored back as a dword, which
 *            zeroes dst->wlog and dst->hlog.  No grd_half_blend NULL check in this part.
 *
 * Widths and heights are read as unsigned 16-bit; the originals run away (and fault) for a
 * width of 0 (h_double), a width below 2 (smooth_h) or a height below 2 (smooth_hv), which
 * the C versions do not try to reproduce.
 */
#include "2d_prim.h"

void flat8_flat8_h_double_ubitmap(grs_bitmap *bm)
{
    uchar *src = bm->bits, *dst = grd_bm.bits;
    unsigned w = (ushort)bm->w, h = (ushort)bm->h, i;
    unsigned srow = (ushort)bm->row, drow = (ushort)grd_bm.row;

    if (h == 0) return;
    do {
        for (i = 0; i < w; i++)
            dst[2 * i] = dst[2 * i + 1] = src[i];
        src += srow;
        dst += drow;
    } while (--h);
}

void flat8_flat8_smooth_h_double_ubitmap(grs_bitmap *srcb, grs_bitmap *dstb)
{
    uchar *src = srcb->bits, *dst = dstb->bits;
    unsigned w = (ushort)srcb->w, h = (ushort)srcb->h, i;
    unsigned srow = (ushort)srcb->row, drow = (ushort)dstb->row;
    uchar *blend = grd_half_blend;
    uchar prev, next;

    if (blend == NULL || h == 0) return;
    do {
        prev = src[0];
        for (i = 1; i < w; i++) {
            next = src[i];
            dst[2 * i - 2] = prev;
            dst[2 * i - 1] = blend[(prev << 8) | next];
            prev = next;
        }
        dst[2 * w - 2] = prev;          /* double the last pixel */
        dst[2 * w - 1] = prev;
        src += srow;
        dst += drow;
    } while (--h);
}

void flat8_flat8_smooth_hv_double_ubitmap(grs_bitmap *src, grs_bitmap *dst)
{
    unsigned row, w2, n, x;
    uchar *p, *blend;

    dst->row = (ushort)(dst->row * 2);
    flat8_flat8_smooth_h_double_ubitmap(src, dst);
    row = (ushort)dst->row >> 1;
    *(ulong *)&dst->row = row;          /* sic: clobbers wlog, hlog */

    w2 = (ushort)src->w * 2;
    n = (ushort)src->h - 1;
    blend = grd_half_blend;
    p = dst->bits;
    for (;;) {
        for (x = 0; x < w2; x++)
            p[row + x] = blend[(p[2 * row + x] << 8) | p[x]];
        if (--n == 0) break;
        p += 2 * row;
    }
    /* last row: copy of the one above */
    p += 2 * row;
    for (x = 0; x < w2; x++)
        p[row + x] = p[x];
}
