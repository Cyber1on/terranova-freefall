/* mxbm.c -- Terra Nova v1.09 ModeX canvas: flat8 bitmap blits (function table 0x36AB1C,
 * BMT_FLAT8 GRC_BITMAP: FILL_NORM and FILL_XOR).  Decompiled from __FF.EXE (Watcom C); names
 * ours.  The bitmap is drawn one plane at a time: source column k, k + 4, .. go to plane
 * (x + align + k) & 3, one byte per column of 4 pixels.
 *
 *   0x341090 modex_flat8_ubitmap       0x3414A0 modex_xor_flat8_ubitmap
 *
 * The map mask (and for xor the read map) is forced to "unknown" (0xFF) on entry, so the first
 * plane always programs the VGA, and the value saved on entry is written back at the end.
 * Opaque bitmaps of more than 0x4000 pixels are copied in bands of 0x1000 / w rows.
 * Quirks kept: an empty bitmap (w or h <= 0; for xor w or h == 0) returns with the mask (and
 * read map) cache left at 0xFF, nothing written; the opaque copies step rows with signed
 * (short) row sizes where the transparent ones use unsigned ones; the opaque inner loops are
 * do-whiles (a negative height runs 4G rows; in the banded copy a plane past the bitmap width
 * runs 4G columns, possible only for w < 4).
 */
#include "2d_tmap.h"

static void mx_end(uchar save)
{
    grd_mx_wmask = 0xff;
    if (save != 0xff) {
        grd_mx_wmask = save;
        tm_outw(0x3C4, save << 8 | 2);
    }
}

void modex_flat8_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar save = grd_mx_wmask, *rowbase, *s, *d, *sc, *dc;
    short w, h, xs, y0;
    long chunk, r, c, k, j;
    ulong n;

    grd_mx_wmask = 0xff;
    w = bm->w;
    h = bm->h;
    if (h <= 0 || w <= 0)
        return;
    rowbase = grd_bm.bits + y * (long)(ushort)grd_bm.row;
    xs = x + grd_bm.align;
    if (bm->flags & BMF_TRANS) {
        for (k = 0; k < 4; k++) {
            sc = bm->bits + k;
            dc = rowbase + (xs >> 2);
            MX_WMASK(1 << (xs & 3));
            for (c = (w + 3) >> 2; c > 0; c--, sc += 4, dc++)
                for (j = 0, s = sc, d = dc; j < h; j++, s += (ushort)bm->row, d += (ushort)grd_bm.row)
                    if (*s != 0)
                        *d = *s;
            xs++;
            if (--w == 0)
                break;
        }
    } else if (w * h > 0x4000) {
        chunk = 0x1000 / w;
        y0 = 0;
        do {
            if (h < chunk)
                chunk = h;
            for (k = 0; k < 4; k++) {
                short xk = xs + k;

                sc = bm->bits + k + (ushort)bm->row * (long)y0;
                dc = rowbase + (xk >> 2) + y0 * (long)(ushort)grd_bm.row;
                MX_WMASK(1 << (xk & 3));
                for (r = chunk; r > 0; r--) {
                    n = (ulong)(long)(short)((w + 3) >> 2);
                    s = sc;
                    d = dc;
                    do {
                        *d++ = *s;
                        s += 4;
                    } while (--n != 0);
                    sc += (ushort)bm->row;
                    dc += (ushort)grd_bm.row;
                }
                w--;
            }
            w += 4;
            y0 += chunk;
            h -= chunk;
        } while (h != 0);
    } else {
        for (k = 0; k < 4; k++) {
            sc = bm->bits + k;
            dc = rowbase + (xs >> 2);
            MX_WMASK(1 << (xs & 3));
            for (c = (w + 3) >> 2; c > 0; c--, sc += 4, dc++) {
                n = (ulong)(long)h;
                s = sc;
                d = dc;
                do {
                    *d = *s;
                    s += (short)bm->row;
                    d += (short)grd_bm.row;
                } while (--n != 0);
            }
            xs++;
            if (--w == 0)
                break;
        }
    }
    mx_end(save);
}

void modex_xor_flat8_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar save_w = grd_mx_wmask, save_r = grd_mx_rplane, *rowbase, *s, *d, *sc, *dc;
    short w, h, xs;
    long c, k, j;
    ulong n;

    grd_mx_wmask = 0xff;
    grd_mx_rplane = 0xff;
    rowbase = grd_bm.bits + y * (long)(ushort)grd_bm.row;
    w = bm->w;
    h = bm->h;
    xs = x + grd_bm.align;
    if (h == 0 || w == 0)
        return;
    for (k = 0; k < 4; k++) {
        sc = bm->bits + k;
        dc = rowbase + (xs >> 2);
        MX_WMASK(1 << (xs & 3));
        MX_RPLANE((short)(xs & 3));
        for (c = (w + 3) >> 2; c > 0; c--, sc += 4, dc++) {
            if (bm->flags & BMF_TRANS) {
                for (j = 0, s = sc, d = dc; j < h; j++, s += (ushort)bm->row, d += (ushort)grd_bm.row)
                    if (*s != 0)
                        *d ^= *s;
            } else {
                n = (ulong)(long)h;
                s = sc;
                d = dc;
                do {
                    *d ^= *s;
                    s += (short)bm->row;
                    d += (short)grd_bm.row;
                } while (--n != 0);
            }
        }
        xs++;
        if (--w == 0)
            break;
    }
    grd_mx_wmask = 0xff;
    grd_mx_rplane = 0xff;
    if (save_r != 0xff) {
        grd_mx_rplane = save_r;
        tm_outw(0x3CE, save_r << 8 | 4);
    }
    if (save_w != 0xff) {
        grd_mx_wmask = save_w;
        tm_outw(0x3C4, save_w << 8 | 2);
    }
}
