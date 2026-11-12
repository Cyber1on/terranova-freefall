/* mxdbm.c -- Terra Nova v1.09 ModeX canvas: blits of a ModeX (device) bitmap onto the ModeX
 * canvas (canvas table 0x36808C DRAW_DEVICE_UBITMAP / DRAW_DEVICE_BITMAP).  Decompiled from
 * __FF.EXE (Watcom C); names ours.  The bitmap's bits are plane bytes too (align = plane of
 * its first pixel).
 *
 *   0x33B4D0 modex_draw_device_ubitmap   0x33BAF8 modex_draw_device_bitmap
 *
 * When the source and destination planes line up and the bitmap is opaque, the copy uses the
 * VGA latches (write mode 1, all planes at once) for the whole columns of 4, with masked
 * passes for the partial first and last columns.  Otherwise it copies plane by plane (read map
 * and map mask per plane; transparent bitmaps skip 0).  The map mask and read map are restored
 * at the end.
 * Quirks kept: in the single-column latch case the middle pass still runs from the source's
 * next byte to its last column; the per-plane pass loops while the remaining width, counted
 * down from w + 3 once per plane, is not 0 (so planes stop early only for w <= 3).
 */
#include "2d_tmap.h"

static void mx_restore(uchar w, uchar r)
{
    if (w != grd_mx_wmask) {
        tm_outw(0x3C4, w << 8 | 2);
        grd_mx_wmask = w;
    }
    if (r != grd_mx_rplane) {
        grd_mx_rplane = r;
        tm_outw(0x3CE, r << 8 | 4);
    }
}

void modex_draw_device_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar save_w = grd_mx_wmask, save_r = grd_mx_rplane;
    int xs = x + grd_bm.align;
    short w, h, last, wcols;
    uchar *rowp, *dst, *dst_end, *src, *src_end, *s, *d;
    int i, k, n, m;

    if ((xs & 3) == bm->align && !(bm->flags & BMF_TRANS)) {
        tm_outb(0x3CE, 5);                          /* write mode 1: latch copy */
        tm_outw(0x3CE, (tm_inb(0x3CF) | 1) << 8 | 5);
        w = bm->w;
        h = bm->h;
        last = (short)(x + grd_bm.align + w - 1);
        rowp = grd_bm.bits + y * (ulong)(ushort)grd_bm.row;
        dst = rowp + ((short)xs >> 2);
        dst_end = rowp + (last >> 2);
        src = bm->bits;
        src_end = src + ((w + bm->align - 1) >> 2);
        if (((xs & 3) != 0 || (short)(last & 3) != 3) && ((short)xs >> 2) == (last >> 2)) {
            /* one partial column */
            s = src;
            m = grd_mx_lmask[(short)(xs & 3)] & grd_mx_rmask[(short)(last & 3)];
            MX_WMASK(m);
            for (i = 0, d = dst; i < h; i++) {
                *d = *s;
                d += (ushort)grd_bm.row;
                s += (ushort)bm->row;
            }
            src++;
        } else {
            if (xs & 3) {                           /* partial first column */
                MX_WMASK(grd_mx_lmask[(short)(xs & 3)]);
                for (i = 0, s = src, d = dst; i < h; i++) {
                    *d = *s;
                    d += (ushort)grd_bm.row;
                    s += (ushort)bm->row;
                }
                src++;
                dst++;
            }
            if ((short)(last & 3) != 3) {           /* partial last column */
                MX_WMASK(grd_mx_rmask[(short)(last & 3)]);
                for (i = 0, s = src_end, d = dst_end; i < h; i++) {
                    *d = *s;
                    d += (ushort)grd_bm.row;
                    s += (ushort)bm->row;
                }
                src_end--;
            }
        }
        n = src_end - src + 1;
        if (n > 0) {
            if (grd_mx_wmask != 0xff) {
                grd_mx_wmask = 0xff;
                tm_outw(0x3C4, 0xff02);
            }
            for (i = 0; i < h; i++) {
                int j;
                for (j = n, s = src, d = dst; j != 0; j--)
                    *d++ = *s++;
                dst += (ushort)grd_bm.row;
                src += (ushort)bm->row;
            }
        }
        tm_outb(0x3CE, 5);                          /* back to write mode 0 */
        tm_outw(0x3CE, (tm_inb(0x3CF) & 0xfc) << 8 | 5);
    } else {
        wcols = (short)(bm->w + 3);
        h = bm->h;
        rowp = grd_bm.bits + y * (ulong)(ushort)grd_bm.row;
        for (k = 0; k < 4; k++) {
            int sx = bm->align + k, dx = x + grd_bm.align, ncols;

            src = bm->bits + (sx >> 2);
            MX_RPLANE(sx & 3);
            dst = rowp + (dx >> 2);
            MX_WMASK(1 << (dx & 3));
            for (ncols = wcols >> 2; ncols > 0; ncols--, src++, dst++) {
                for (i = 0, s = src, d = dst; i < h; i++) {
                    if (!(bm->flags & BMF_TRANS) || *s != 0)
                        *d = *s;
                    s += (ushort)bm->row;
                    d += (ushort)grd_bm.row;
                }
            }
            x++;
            if (--wcols == 0)
                break;
        }
    }
    mx_restore(save_w, save_r);
}

int modex_draw_device_bitmap(grs_bitmap *bm, short x, short y)
{
    short w = bm->w, h = bm->h, d;
    uchar *bits = bm->bits;
    uchar al = bm->align;
    int code = 0, t;

    if (x + w < grd_fix_clip.left >> 16 || x >= grd_clip.right
        || y + h < grd_fix_clip.top >> 16 || y >= grd_clip.bot)
        return CLIP_ALL;
    if (x < grd_clip.left) {
        t = (grd_fix_clip.left >> 16) - x;
        bm->w = (short)(w - t);
        t += bm->align;
        bm->bits += t >> 2;
        bm->align = (uchar)(t & 3);
        x = grd_clip.left;
        code = 1;
    }
    if (x + bm->w > grd_fix_clip.right >> 16) {
        d = (short)(bm->w + x - grd_clip.right);
        bm->w = (short)(bm->w - d);
        code |= 4;
    }
    if (y < grd_clip.top) {
        t = (grd_fix_clip.top >> 16) - y;
        bm->h = (short)(bm->h - t);
        bm->bits += t * (ulong)(ushort)bm->row;
        code |= 2;
        y = grd_clip.top;
    }
    if (y + bm->h > grd_fix_clip.bot >> 16) {
        d = (short)(bm->h + y - grd_clip.bot);
        bm->h = (short)(bm->h - d);
        code |= 8;
    }
    modex_draw_device_ubitmap(bm, x, y);
    bm->w = w;
    bm->h = h;
    bm->bits = bits;
    bm->align = al;
    return code;
}
