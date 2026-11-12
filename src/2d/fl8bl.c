/* fl8bl.c -- x:\prj\tech\libsrc\2d\fl8bl.c (Terra Nova v1.09): blended translucent clut
 * linear mapper setup (function table NORM/FLAT8/GRC_MORE_POLY).  Decompiled from __FF.EXE.
 *
 *   0x32EFD0 gri_trans_blend_clut_lin_umap_init
 *
 * Doubles the bitmap with smoothing into the unpack buffer and hands the doubled bitmap to the
 * GRC_TRANS_CLUT_BILIN init of the current function table.  Does nothing without an unpack
 * buffer.
 */
#include "2d_tmap.h"

#define GRC_TRANS_CLUT_BILIN 34
#define GRD_FUNCS 65

void gri_trans_blend_clut_lin_umap_init(grs_tmap_loop_info *ti)
{
    grs_bitmap tbm;
    uchar *b = grd_unpack_buf;

    if (b == NULL)
        return;
    tbm.row = 2 * ti->bm.w;
    if (b == ti->bm.bits)
        b = ti->bm.bits + (ushort)ti->bm.row * ti->bm.h;
    tbm.bits = b;
    flat8_flat8_smooth_hv_double_ubitmap(&ti->bm, &tbm);
    ti->n = BMT_FLAT8 * GRD_FUNCS + GRC_TRANS_CLUT_BILIN;
    ti->bm.bits = tbm.bits;
    ti->bm.row = tbm.row;
    ti->bm.w *= 2;
    ti->bm.h *= 2;
    WATCOM_CALL(grd_function_table[ti->n], (long)ti);
}
