/* fl8sply.c -- x:\prj\tech\libsrc\2d\fl8sply.c (Terra Nova v1.09): flat8 shaded (gouraud)
 * polygons.  Decompiled from __FF.EXE.
 *
 *   0x32D5C0 gri_spoly_loop      0x32D7BC gri_spoly_init      0x32D7FC gri_clut_spoly_init
 *   0x32D83C gri_stpoly_init     0x32D88C gri_clut_stpoly_init
 *
 * Differences from System Shock: the opaque case goes through the screen's light table with
 * the polygon colour (ltab[fix_fint(i)*256 + colour]) instead of writing fix_fint(i), and the
 * loop keeps its state in *ti (updated every line) rather than in locals.
 */
#include "2d_tmap.h"

int gri_spoly_loop(grs_tmap_loop_info *ti)
{
    do {
        fix i, di;
        int x, d;
        uchar *lt;

        di = tm_fix_div(ti->right.i - ti->left.i, ti->right.x - ti->left.x);
        i = ti->left.i + tm_fix_mul(fix_ceil(ti->left.x) - ti->left.x, di);
        x = fix_cint(ti->left.x);
        d = fix_cint(ti->right.x) - x;
        if (d > 0) {
            switch (ti->loop) {
            case GRL_OPAQUE:
                lt = grd_screen->ltab + (uchar)(ulong)ti->bm.bits;
                for (; x < fix_cint(ti->right.x); x++, i += di)
                    ti->d[x] = lt[(i >> 16) << 8];
                break;
            case GRL_CLUT:
                for (; x < fix_cint(ti->right.x); x++, i += di)
                    ti->d[x] = ti->clut[i >> 16];
                break;
            case GRL_TLUC8:
                for (; x < fix_cint(ti->right.x); x++, i += di)
                    ti->d[x] = tluc8stab[((i >> 16) << 8) + ti->d[x]];
                break;
            case GRL_TLUC8 | GRL_CLUT:
                for (; x < fix_cint(ti->right.x); x++, i += di)
                    ti->d[x] = ti->clut[tluc8stab[((i >> 16) << 8) + ti->d[x]]];
                break;
            }
        } else if (d < 0)
            return TRUE;
        ti->d += grd_bm.row;
        ti->left.x += ti->left.dx;
        ti->right.x += ti->right.dx;
        ti->left.i += ti->left.di;
        ti->right.i += ti->right.di;
    } while (--ti->n > 0);
    return FALSE;
}

static void spoly_common(grs_tmap_loop_info *ti)
{
    ti->d = grd_bm.bits + (int)grd_bm.row * ti->y;
    ti->loop_func = WATCOM_FN(gri_spoly_loop);
    ti->left_edge_func = WATCOM_FN(gri_ix_edge);
    ti->right_edge_func = WATCOM_FN(gri_ix_edge);
}

void gri_spoly_init(grs_tmap_loop_info *ti)
{
    ti->loop = GRL_OPAQUE;
    spoly_common(ti);
}

void gri_clut_spoly_init(grs_tmap_loop_info *ti)
{
    ti->loop = GRL_CLUT;
    spoly_common(ti);
}

void gri_stpoly_init(grs_tmap_loop_info *ti)
{
    ti->loop = tluc8stab != NULL ? GRL_TLUC8 : GRL_OPAQUE;
    spoly_common(ti);
}

void gri_clut_stpoly_init(grs_tmap_loop_info *ti)
{
    ti->loop = tluc8stab != NULL ? GRL_TLUC8 | GRL_CLUT : GRL_CLUT;
    spoly_common(ti);
}
