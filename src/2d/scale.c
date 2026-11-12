/* scale.c -- scaler/raster helpers next to the clip code (Terra Nova v1.09).
 * Decompiled from __FF.EXE; no System Shock counterpart.
 *
 *   0x34C490 gri_lin_raster (name from 2d-tmap)   0x34A8E0 gri_scale_compare_loop (name ours)
 *   0x34AA7C gri_scale_34AA7C (2d-prim's name; "scale compare")
 *   0x34AB10 gri_tluc8_scale_loop (name ours)      0x34AC54 gri_scale_34AC54 (2d-prim's name)
 *   data: 0x460810 gri_scale_total, 0x460814 gri_scale_match (names ours)
 *
 * gri_lin_raster(info, n, vpl) is the span walker of h_umap without the setup: left edge
 *   backwards, right edge forwards through vpl, edge functions info->left/right_edge_func,
 *   spans of info->n lines to info->loop_func until it returns nonzero.  Unlike h_umap the
 *   comparisons are signed.  Its return value is what is left in eax: the loop function's
 *   nonzero result, y_max when it finishes, or (no spans) the last vertex scanline computed.
 * gri_scale_34AA7C(bm, n, vpl) "draws" bm scaled to the polygon (a rectangle; edge function
 *   gri_scale_edge) without writing: for every opaque texel it counts gri_scale_total and,
 *   when the canvas pixel under it has the same colour, gri_scale_match; it returns
 *   fix_div(match, total) (the original divides by zero when nothing was opaque).
 * gri_scale_34AC54(bm, n, vpl) draws bm scaled through the translucency tables:
 *   dst = tluc8tab[dst] ? tluc8tab[dst][src] : src for opaque src.  Returns gri_lin_raster's
 *   value.
 * Both loop functions keep u per row from the left edge (du from the left/right u and x),
 *   step v by info->left.dv and stop with 1 when the span is empty.  The compare loop also
 *   advances info->y; the tluc8 one does not (it keeps its own destination row pointer).
 */
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
long gri_scale_total;
long gri_scale_match;
#else
extern long gri_scale_total, gri_scale_match;
#endif

void gri_scale_edge();          /* 0x34B610 (2d-tmap) */

#define FIX_CEIL(a) ((fix)(((a) + 0xFFFF) & 0xFFFF0000))
static short cint(fix a) { return (short)((a + 0xFFFF) >> 16); }

int gri_lin_raster(grs_tmap_loop_info *info, int n, grs_vertex **vpl)
{
    grs_vertex **p_left = vpl, **p_right, **pvp, *prev_l = NULL, *prev_r = NULL;
    int y, y_min, y_max, y_limit, r;
    int last = (vpl[0]->y + 0xFFFF) >> 16;

    y_min = y_max = cint(vpl[0]->y);
    for (pvp = vpl + 1; pvp < vpl + n; pvp++) {
        last = y = cint((*pvp)->y);
        if (y < y_min) {
            y_min = y;
            p_left = pvp;
        }
        if (y > y_max)
            y_max = y;
    }
    if (y_min == y_max)
        return last;
    p_right = p_left;
    for (y = y_min;;) {
        if (cint((*p_left)->y) <= y) {
            do {
                if (cint((*p_left)->y) == y)
                    prev_l = *p_left;
                if (--p_left < vpl)
                    p_left = vpl + n - 1;
            } while (cint((*p_left)->y) <= y);
            WATCOM_CALL(GR_IFN(info->left_edge_func), (long)info, (long)p_left, (long)&prev_l, 1);
        }
        if (cint((*p_right)->y) <= y) {
            do {
                if (cint((*p_right)->y) == y)
                    prev_r = *p_right;
                if (++p_right >= vpl + n)
                    p_right = vpl;
            } while (cint((*p_right)->y) <= y);
            WATCOM_CALL(GR_IFN(info->right_edge_func), (long)info, (long)p_right, (long)&prev_r, 0);
        }
        y_limit = info->right.y < info->left.y ? info->right.y : info->left.y;
        info->n = y_limit - y;
        if ((r = WATCOM_CALL(GR_IFN(info->loop_func), (long)info)) != 0)
            return r;
        y = y_limit;
        if (y == y_max)
            return y_max;
    }
}

int gri_scale_compare_loop(grs_tmap_loop_info *info)
{
    long total = gri_scale_total, match = gri_scale_match;
    int x0 = cint(info->left.x), x1 = cint(info->right.x), x;
    fix du, u0, u;
    uchar *p, *src, s;

    if (x1 <= x0) {
        gri_scale_total = total;
        gri_scale_match = match;
        return 1;
    }
    du = fix_div(info->right.u - info->left.u, info->right.x - info->left.x);
    u0 = info->left.u + fix_mul(du, FIX_CEIL(info->left.x) - info->left.x);
    p = grd_canvas->bm.bits + (ushort)grd_canvas->bm.row * info->y;
    do {
        src = info->bm.bits + (info->left.v >> 16) * (ushort)info->bm.row;
        for (x = x0, u = u0; x < x1; x++, u += du) {
            s = src[u >> 16];
            if (s) {
                total++;
                if (s == p[x])
                    match++;
            }
        }
        info->left.v += info->left.dv;
        info->y++;
        gri_scale_match = match;
        p += (ushort)grd_canvas->bm.row;
        gri_scale_total = total;
    } while (--info->n > 0);
    gri_scale_total = total;
    gri_scale_match = match;
    return 0;
}

long gri_scale_34AA7C(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    grs_tmap_loop_info info;

    gri_scale_match = 0;
    gri_scale_total = 0;
    info.bm = *bm;
    info.right_edge_func = WATCOM_FN(gr_null);
    info.loop_func = WATCOM_FN(gri_scale_compare_loop);
    info.left_edge_func = WATCOM_FN(gri_scale_edge);
    info.y = cint(vpl[0]->y);
    gri_lin_raster(&info, n, vpl);
    return fix_div(gri_scale_match, gri_scale_total);
}

int gri_tluc8_scale_loop(grs_tmap_loop_info *info)
{
    uchar *row = grd_canvas->bm.bits + (ushort)grd_canvas->bm.row * info->y;
    uchar *d, *d0 = row + cint(info->left.x), *end = row + cint(info->right.x), *src, s, *t;
    fix du, u0, u;

    if (end <= d0)
        return 1;
    du = fix_div(info->right.u - info->left.u, info->right.x - info->left.x);
    u0 = info->left.u + fix_mul(du, FIX_CEIL(info->left.x) - info->left.x);
    do {
        src = info->bm.bits + (info->left.v >> 16) * (ushort)info->bm.row;
        for (d = d0, u = u0; d < end; d++, u += du) {
            s = src[u >> 16];
            if (s) {
                t = tluc8tab_p[*d];
                *d = t ? t[s] : s;
            }
        }
        info->left.v += info->left.dv;
        d0 += (ushort)grd_canvas->bm.row;
        end += (ushort)grd_canvas->bm.row;
    } while (--info->n > 0);
    return 0;
}

long gri_scale_34AC54(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    grs_tmap_loop_info info;

    info.bm = *bm;
    info.loop_func = WATCOM_FN(gri_tluc8_scale_loop);
    info.right_edge_func = WATCOM_FN(gr_null);
    info.left_edge_func = WATCOM_FN(gri_scale_edge);
    info.y = cint(vpl[0]->y);
    return gri_lin_raster(&info, n, vpl);
}
