/* fl8fl8m.c -- x:\prj\tech\libsrc\2d\fl8fl8m.c (Terra Nova v1.09): flat8 bitmaps drawn
 * through a stencil (function table GRC_STENCIL_BITMAP).  Decompiled from __FF.EXE.
 *
 *   0x32E340 gri_flat8_mask_bitmap             NORM/BLEND FLAT8 GRC_STENCIL_BITMAP
 *   0x32E4DC gri_flat8_mask_fill_clut_bitmap   CLUT FLAT8 GRC_STENCIL_BITMAP
 *
 * Without a stencil they fall back to gen_flat8_bitmap / gen_flat8_clut_bitmap (0x32A050,
 * 0x32AB64, original code of the primitives module).
 */
#include "2d_tmap.h"

#define BMF_TRANS 1

/* rep movsd / rep movsb, as the compiler's inline memmove does it */
static void rep_movs(uchar *d, uchar *s, ulong n)
{
    ulong k;
    for (k = n >> 2; k; k--, d += 4, s += 4)
        *(ulong *)d = *(ulong *)s;
    for (k = n & 3; k; k--)
        *d++ = *s++;
}

int gri_flat8_mask_bitmap(grs_bitmap *bm, short x, short y, grs_stencil *sten)
{
    int yf;
    uchar *src, *dst;

    if (sten == NULL)
        return gen_flat8_bitmap(bm, x, y);
    yf = y + bm->h;
    src = bm->bits - x;
    if (yf > grd_clip.bot)
        yf = grd_clip.bot;
    if (y < grd_clip.top) {
        src += (grd_clip.top - y) * (int)bm->row;
        y = grd_clip.top;
    }
    dst = grd_bm.bits + y * (int)grd_bm.row;
    for (; y < yf; y++) {
        int xi = x, xf = x + bm->w;
        grs_sten_elem *s = &sten->elem[y];
        while (s->r <= xi) {
            s = s->n;
            if (s == NULL)
                break;
        }
        if (s != NULL) {
            if (s->l > xi)
                xi = s->l;
            if (s->r < xf)
                xf = s->r;
            if (xf > xi) {
                if (bm->flags & BMF_TRANS) {
                    int i;
                    for (i = xi; i < xf; i++)
                        if (src[i])
                            dst[i] = src[i];
                } else
                    rep_movs(dst + xi, src + xi, xf - xi);
            }
        }
        src += bm->row;
        dst += grd_bm.row;
    }
    return 0;      /* CLIP_NONE */
}

int gri_flat8_mask_fill_clut_bitmap(grs_bitmap *bm, short x, short y, grs_stencil *sten)
{
    uchar *clut = (uchar *)grd_gc.fill_parm;
    int yf;
    uchar *src, *dst;

    if (sten == NULL)
        return gen_flat8_clut_bitmap(bm, x, y, clut);
    yf = y + bm->h;
    src = bm->bits - x;
    if (yf > grd_clip.bot)
        yf = grd_clip.bot;
    if (y < grd_clip.top) {
        src += (grd_clip.top - y) * (int)bm->row;
        y = grd_clip.top;
    }
    dst = grd_bm.bits + y * (int)grd_bm.row;
    for (; y < yf; y++) {
        int xi = x, xf = x + bm->w;
        grs_sten_elem *s = &sten->elem[y];
        while (s->r <= xi) {
            s = s->n;
            if (s == NULL)
                break;
        }
        if (s != NULL) {
            if (s->l > xi)
                xi = s->l;
            if (s->r < xf)
                xf = s->r;
            if (xf > xi) {
                int i;
                if (bm->flags & BMF_TRANS) {
                    for (i = xi; i < xf; i++)
                        if (src[i])
                            dst[i] = clut[src[i]];
                } else {
                    for (i = xi; i < xf; i++)
                        dst[i] = clut[src[i]];
                }
            }
        }
        src += bm->row;
        dst += grd_bm.row;
    }
    return 0;
}
