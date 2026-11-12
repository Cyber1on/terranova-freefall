/* fl8cply.c -- x:\prj\tech\libsrc\2d\fl8cply.c (Terra Nova v1.09): flat8 RGB-shaded
 * polygons (u,v,i of the tmap info carry r,g,b).  Decompiled from __FF.EXE.
 *
 *   0x32ED00 gri_cpoly_loop      0x32EF44 gri_cpoly_init      0x32EF84 gri_clut_cpoly_init
 *
 * Unlike System Shock the colour gradients are computed (and divided) before the span length
 * is checked, and any loop type other than GRL_OPAQUE/GRL_CLUT draws nothing.
 */
#include "2d_tmap.h"

int gri_cpoly_loop(grs_tmap_loop_info *ti)
{
    do {
        fix dx = ti->right.x - ti->left.x;
        fix frac = fix_ceil(ti->left.x) - ti->left.x;
        fix r, g, b, dr, dg, db;
        int x, d;

        r = ti->left.u;
        dr = tm_fix_div(ti->right.u - r, dx);
        r += tm_fix_mul(frac, dr);
        g = ti->left.v;
        dg = tm_fix_div(ti->right.v - g, dx);
        g += tm_fix_mul(frac, dg);
        b = ti->left.i;
        db = tm_fix_div(ti->right.i - b, dx);
        b += tm_fix_mul(frac, db);

        x = fix_cint(ti->left.x);
        d = fix_cint(ti->right.x) - x;
        if (d > 0) {
            switch (ti->loop) {
            case GRL_OPAQUE:
                for (; x < fix_cint(ti->right.x); x++) {
                    int j = ((r >> 19) & 0x1f) | ((g >> 14) & 0x3e0) | ((b >> 9) & 0x7c00);
                    ti->d[x] = grd_ipal[j];
                    r += dr, g += dg, b += db;
                }
                break;
            case GRL_CLUT:
                for (; x < fix_cint(ti->right.x); x++) {
                    int j = ((r >> 19) & 0x1f) | ((g >> 14) & 0x3e0) | ((b >> 9) & 0x7c00);
                    ti->d[x] = ti->clut[grd_ipal[j]];
                    r += dr, g += dg, b += db;
                }
                break;
            }
        } else if (d < 0)
            return TRUE;
        ti->left.x += ti->left.dx;
        ti->right.x += ti->right.dx;
        ti->left.u += ti->left.du;
        ti->right.u += ti->right.du;
        ti->left.v += ti->left.dv;
        ti->right.v += ti->right.dv;
        ti->left.i += ti->left.di;
        ti->right.i += ti->right.di;
        ti->d += grd_bm.row;
    } while (--ti->n > 0);
    return FALSE;
}

static void cpoly_common(grs_tmap_loop_info *ti)
{
    ti->d = grd_bm.bits + (int)grd_bm.row * ti->y;
    ti->loop_func = WATCOM_FN(gri_cpoly_loop);
    ti->left_edge_func = WATCOM_FN(gri_rgbx_edge);
    ti->right_edge_func = WATCOM_FN(gri_rgbx_edge);
}

void gri_cpoly_init(grs_tmap_loop_info *ti)
{
    ti->loop = GRL_OPAQUE;
    cpoly_common(ti);
}

void gri_clut_cpoly_init(grs_tmap_loop_info *ti)
{
    ti->loop = GRL_CLUT;
    cpoly_common(ti);
}
