/* genscl.c -- Terra Nova v1.09: generic (device-independent) flat8 and tluc8 bitmap scalers used
 * by the 8-bit banked VESA function table (0x3686E0, BMT_FLAT8 / BMT_TLUC8 GRC_SCALE..
 * GRC_TRANS_CLUT_SCALE).  Decompiled from __FF.EXE (Watcom C); the init names follow Shock's
 * Gen/GENTF.H, the loop names are ours.  Every texel goes through grd_function_table[GRC_PIXEL].
 *
 *   0x33F188 gri_gen_scale_umap_loop
 *   0x33F430 gri_gen_trans_scale_umap_init         (loop 1)
 *   0x33F450 gri_gen_opaque_scale_umap_init        (loop 0)
 *   0x33F470 gri_gen_trans_clut_scale_umap_init    (loop 5)
 *   0x33F490 gri_gen_opaque_clut_scale_umap_init   (loop 4)
 *   0x33F4B0 gri_gen_trans_solid_scale_umap_init   (loop 9, not in any table)
 *   0x33F4D0 gri_gen_solid_scale_umap_init         (gri_gen_poly_loop with the scale edges)
 *   0x33F508 gri_gen_tluc8_scale_umap_loop
 *   0x33F890 gri_gen_tluc8_trans_scale_umap_init        (loop 1)
 *   0x33F8B0 gri_gen_tluc8_opaque_scale_umap_init       (loop 0)
 *   0x33F8D0 gri_gen_tluc8_trans_clut_scale_umap_init   (loop 5)
 *   0x33F8F0 gri_gen_tluc8_opaque_clut_scale_umap_init  (loop 4)
 *
 * Quirks kept: the span (x range) and starting u are computed once from the first row and used
 * for all rows; an empty span returns 1 (abort) before anything is drawn; unknown loop types
 * draw nothing.  The tluc8 loop draws tluc8tab[s][dest pixel] when s has a translucency table
 * and s (through the clut for loops 4/5, which the table lookup bypasses) otherwise.  The solid
 * init's poly loop draws the low byte of bm.bits (a bitmap pointer) as a solid span.
 */
#include "2d_tmap.h"

#define GET_UPIXEL8 2
#define PIX(c, x, y) WATCOM_CALL((void (*)())grd_function_table[0], (long)(c), (long)(short)(x), (long)(short)(y))

int gri_gen_poly_loop(grs_tmap_loop_info *ti);

int gri_gen_scale_umap_loop(grs_tmap_loop_info *ti)
{
    int xl = fix_cint(ti->left.x), xr = fix_cint(ti->right.x), x;
    fix u, du, uu;
    uchar *src, s;

    if (xr <= xl)
        return 1;
    du = tm_fix_div(ti->right.u - ti->left.u, ti->right.x - ti->left.x);
    u = ti->left.u + tm_fix_mul(du, fix_ceil(ti->left.x) - ti->left.x);
    do {
        src = ti->bm.bits + (ti->left.v >> 16) * (long)(ushort)ti->bm_row;
        switch (ti->loop) {
        case 0:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                PIX(src[uu >> 16], x, ti->y);
            break;
        case 1:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                if ((s = src[uu >> 16]) != 0)
                    PIX(s, x, ti->y);
            break;
        case 4:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                PIX(ti->clut[src[uu >> 16]], x, ti->y);
            break;
        case 5:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                if ((s = src[uu >> 16]) != 0)
                    PIX(ti->clut[s], x, ti->y);
            break;
        case 9:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                if (src[uu >> 16] != 0)
                    PIX((uchar)(ulong)ti->clut, x, ti->y);
            break;
        }
        ti->left.v += ti->left.dv;
        ti->y++;
    } while (--ti->n > 0);
    return 0;
}

static void scale_init(grs_tmap_loop_info *ti, int loop)
{
    ti->loop = (uchar)loop;
    ti->loop_func = WATCOM_FN(gri_gen_scale_umap_loop);
    ti->right_edge_func = WATCOM_FN(gr_null);
    ti->left_edge_func = WATCOM_FN(gri_scale_edge);
}

void gri_gen_trans_scale_umap_init(grs_tmap_loop_info *ti) { scale_init(ti, 1); }
void gri_gen_opaque_scale_umap_init(grs_tmap_loop_info *ti) { scale_init(ti, 0); }
void gri_gen_trans_clut_scale_umap_init(grs_tmap_loop_info *ti) { scale_init(ti, 5); }
void gri_gen_opaque_clut_scale_umap_init(grs_tmap_loop_info *ti) { scale_init(ti, 4); }
void gri_gen_trans_solid_scale_umap_init(grs_tmap_loop_info *ti) { scale_init(ti, 9); }

void gri_gen_solid_scale_umap_init(grs_tmap_loop_info *ti)
{
    ti->loop = 0;
    ti->loop_func = WATCOM_FN(gri_gen_poly_loop);
    ti->right_edge_func = WATCOM_FN(gr_null);
    ti->left_edge_func = WATCOM_FN(gri_scale_edge);
}

/* one tluc8 texel: through its translucency table if it has one */
static void tluc8_pix(grs_tmap_loop_info *ti, uchar s, int x, int clut)
{
    if (tluc8tab_p[s] != NULL) {
        long p = WATCOM_CALL((long (*)())grd_pixel_table[GET_UPIXEL8], (long)(short)x, (long)(short)ti->y);
        PIX(tluc8tab_p[s][p], x, ti->y);
    } else
        PIX(clut ? ti->clut[s] : s, x, ti->y);
}

int gri_gen_tluc8_scale_umap_loop(grs_tmap_loop_info *ti)
{
    int xl = fix_cint(ti->left.x), xr = fix_cint(ti->right.x), x;
    fix u, du, uu;
    uchar *src, s;

    if (xr <= xl)
        return 1;
    du = tm_fix_div(ti->right.u - ti->left.u, ti->right.x - ti->left.x);
    u = ti->left.u + tm_fix_mul(du, fix_ceil(ti->left.x) - ti->left.x);
    do {
        src = ti->bm.bits + (ti->left.v >> 16) * (long)(ushort)ti->bm_row;
        switch (ti->loop) {
        case 0:
        case 4:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                tluc8_pix(ti, src[uu >> 16], x, ti->loop == 4);
            break;
        case 1:
        case 5:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                if ((s = src[uu >> 16]) != 0)
                    tluc8_pix(ti, s, x, ti->loop == 5);
            break;
        }
        ti->left.v += ti->left.dv;
        ti->y++;
    } while (--ti->n > 0);
    return 0;
}

static void tluc8_scale_init(grs_tmap_loop_info *ti, int loop)
{
    ti->loop = (uchar)loop;
    ti->loop_func = WATCOM_FN(gri_gen_tluc8_scale_umap_loop);
    ti->right_edge_func = WATCOM_FN(gr_null);
    ti->left_edge_func = WATCOM_FN(gri_scale_edge);
}

void gri_gen_tluc8_trans_scale_umap_init(grs_tmap_loop_info *ti) { tluc8_scale_init(ti, 1); }
void gri_gen_tluc8_opaque_scale_umap_init(grs_tmap_loop_info *ti) { tluc8_scale_init(ti, 0); }
void gri_gen_tluc8_trans_clut_scale_umap_init(grs_tmap_loop_info *ti) { tluc8_scale_init(ti, 5); }
void gri_gen_tluc8_opaque_clut_scale_umap_init(grs_tmap_loop_info *ti) { tluc8_scale_init(ti, 4); }
