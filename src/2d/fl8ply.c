/* fl8ply.c -- x:\prj\tech\libsrc\2d\fl8ply.c (Terra Nova v1.09): flat8 solid/translucent
 * polygon setup.  Decompiled from __FF.EXE.
 *
 *   0x32D404 gri_solid_poly_init   0x32D44C gri_poly_init        0x32D48C gri_clut_poly_init
 *   0x32D4DC gri_tpoly_init        0x32D540 gri_clut_tpoly_init
 *
 * The scanline loop gri_poly_loop (0x32D2E0) and the edge function gri_x_edge (0x34B410) are
 * original code outside this module.
 */
#include "2d_tmap.h"

static void poly_common(grs_tmap_loop_info *ti)
{
    ti->d = grd_bm.bits + (int)grd_bm.row * ti->y;
    ti->loop_func = WATCOM_FN(gri_poly_loop);
    ti->left_edge_func = WATCOM_FN(gri_x_edge);
    ti->right_edge_func = WATCOM_FN(gri_x_edge);
}

void gri_solid_poly_init(grs_tmap_loop_info *ti)
{
    ti->bm.bits = ti->clut;        /* fill_parm */
    ti->loop = GRL_OPAQUE;
    poly_common(ti);
}

void gri_poly_init(grs_tmap_loop_info *ti)
{
    ti->loop = GRL_OPAQUE;
    poly_common(ti);
}

void gri_clut_poly_init(grs_tmap_loop_info *ti)
{
    ti->bm.bits = (uchar *)(ulong)ti->clut[(long)ti->bm.bits];
    ti->loop = GRL_OPAQUE;
    poly_common(ti);
}

void gri_tpoly_init(grs_tmap_loop_info *ti)
{
    uchar *t = tluc8tab_p[(uchar)(ulong)ti->bm.bits];
    if (t != NULL) {
        ti->loop = GRL_TLUC8;
        ti->bm.bits = t;
    } else
        ti->loop = GRL_OPAQUE;
    poly_common(ti);
}

void gri_clut_tpoly_init(grs_tmap_loop_info *ti)
{
    uchar *t = tluc8tab_p[(uchar)(ulong)ti->bm.bits];
    if (t != NULL) {
        ti->loop = GRL_TLUC8 | GRL_CLUT;
        ti->bm.bits = t;
    } else {
        ti->bm.bits = (uchar *)(ulong)ti->clut[(long)ti->bm.bits];
        ti->loop = GRL_OPAQUE;
    }
    poly_common(ti);
}
