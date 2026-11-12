/* fl8clutb.c -- x:\prj\tech\libsrc\2d\fl8fl8c.c, the function-table part (Terra Nova v1.09):
 * unclipped flat8 bitmap through a colour lookup table.  Decompiled from __FF.EXE.
 * (The canvas-table wrappers of the same Shock file belong to the primitives module.)
 *
 *   0x32E6C0 gri_flat8_fill_clut_ubitmap   CLUT FLAT8 GRC_BITMAP / GRC_CLUT_BITMAP
 *   0x32E6D8 gri_flat8_clut_ubitmap        NORM/BLEND FLAT8 GRC_CLUT_BITMAP
 */
#include "2d_tmap.h"

#define BMF_TRANS 1

/* Returns the final source pointer: Shock's is void, the original leaves it in eax. */
long gri_flat8_clut_ubitmap(grs_bitmap *bm, short x, short y, uchar *cl)
{
    int w = bm->w;
    ushort h = bm->h;              /* 16-bit row counter */
    int sskip = (ushort)bm->row - w;
    int dskip = (int)(ushort)grd_bm.row - w;
    uchar *dst = grd_bm.bits + (int)(ushort)grd_bm.row * y + x;
    uchar *src = bm->bits;

    if (bm->flags & BMF_TRANS) {
        while (h-- != 0) {
            uchar *end = src + w;
            for (; src < end; src++, dst++)
                if (*src)
                    *dst = cl[*src];
            dst += dskip;
            src += sskip;
        }
    } else {
        while (h-- != 0) {
            uchar *end = src + w;
            for (; src < end; src++, dst++)
                *dst = cl[*src];
            dst += dskip;
            src += sskip;
        }
    }
    return (long)src;
}

long gri_flat8_fill_clut_ubitmap(grs_bitmap *bm, short x, short y)
{
    return gri_flat8_clut_ubitmap(bm, x, y, (uchar *)grd_gc.fill_parm);
}
