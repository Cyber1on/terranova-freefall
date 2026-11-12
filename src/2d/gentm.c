/* gentm.c -- x:\prj\tech\libsrc\2d\gentm.c (Terra Nova v1.09): horizontal and vertical
 * scanning texture mapper dispatchers.  Decompiled from __FF.EXE (System Shock gen/gentm.c).
 *
 *   0x317FB0 h_umap   0x31847C h_map   0x3184CC v_umap   0x3189C4 v_map (unreferenced)
 *
 * As System Shock, with these differences:
 *  - the init function is grd_function_table[n] (Shock: grd_tmap_init_table[n]); h_umap
 *    passes (&info, vpl), v_umap only &info;
 *  - with no fill type and TMF_CLUT but ti->clut NULL the clut is grd_screen->clut
 *    (gr_get_clut inlined);
 *  - the edge vertex "prev" passed to the edge functions keeps its value from earlier spans
 *    (it is only set when a vertex lies exactly on the current scanline);
 *  - extrema and span limits compare as unsigned (ulong y_min/y_max, ulong_min), the edge
 *    walks compare fix_cint as signed short against y.
 * h_umap walks the left edge backwards through vpl and the right edge forwards; v_umap walks
 * the top edge forwards and the bottom edge backwards.  With TMF_FLOOR (h) / TMF_WALL (v)
 * the vertex w's are made linear in y (x) and restored afterwards (temp_malloc'd copy, only
 * when w_max > 2.0; not checked for NULL).
 */
#include "2d_prim.h"

#define TMS_LEFT 1
#define TMS_RIGHT 0

#define FIX_CEIL(a) ((fix)(((a) + 0xFFFF) & 0xFFFF0000))

static short cint(fix a) { return (short)((a + 0xFFFF) >> 16); }

void h_umap(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti)
{
    grs_vertex **p_left, **p_right, **pvp, *prev_l = NULL, *prev_r = NULL;
    ulong y_min, y_max;
    fix w_min, w_max, y0, dw;
    int y, y_limit, i;
    fix *old_w = NULL;
    void (*tm_init)();
    grs_tmap_loop_info info;

    info.n = (bm->flags & 1) + ti->tmap_type + GRD_FUNCS * bm->type;
    if (grd_canvas->gc.fill_type != FILL_NORM)
        info.clut = (uchar *)grd_canvas->gc.fill_parm;
    else if (ti->flags & TMF_CLUT)
        if ((info.clut = ti->clut) == NULL)
            info.clut = grd_screen->clut;
    tm_init = grd_function_table[info.n];
    info.left_edge_func = info.right_edge_func = info.loop_func = WATCOM_FN(gr_null);

    p_left = vpl;
    y_min = y_max = cint(vpl[0]->y);
    w_min = w_max = vpl[0]->w;
    for (pvp = vpl + 1; pvp < vpl + n; pvp++) {
        y = cint((*pvp)->y);
        if ((ulong)y < y_min) {
            y_min = y;
            p_left = pvp;
            w_min = (*pvp)->w;
        }
        if ((ulong)y > y_max) {
            y_max = y;
            w_max = (*pvp)->w;
        }
    }
    if (y_min == y_max)
        return;

    if (ti->flags & TMF_FLOOR) {
        y0 = (*p_left)->y;
        if (w_max > 0x20000) {
            old_w = temp_malloc(n * sizeof(fix));
            do {
                w_max >>= 2;
                w_min >>= 2;
            } while (w_max > 0x20000);
            for (i = 0; i < n; i++)
                old_w[i] = vpl[i]->w;
        }
        info.dw = dw = (w_max - w_min) / (long)(y_max - y_min);
        info.w = w_min + fix_mul(dw, FIX_CEIL(y0) - y0);
        for (pvp = vpl; pvp < vpl + n; pvp++)
            (*pvp)->w = w_min + fix_mul((*pvp)->y - y0, dw);
    }
    info.bm = *bm;
    info.y = y_min;
    info.u_mask = (1 << bm->wlog) - 1;
    p_right = p_left;
    info.v_mask = ((1 << bm->hlog) - 1) << bm->wlog;
    info.vtab = NULL;

    WATCOM_CALL(tm_init, (long)&info, (long)vpl);
    for (y = y_min; y != y_max; y = y_limit) {
        if (cint((*p_left)->y) <= y) {
            do {
                if (cint((*p_left)->y) == y)
                    prev_l = *p_left;
                if (--p_left < vpl)
                    p_left = vpl + n - 1;
            } while (cint((*p_left)->y) <= y);
            WATCOM_CALL(GR_IFN(info.left_edge_func), (long)&info, (long)p_left, (long)&prev_l, TMS_LEFT);
        }
        if (cint((*p_right)->y) <= y) {
            do {
                if (cint((*p_right)->y) == y)
                    prev_r = *p_right;
                if (++p_right >= vpl + n)
                    p_right = vpl;
            } while (cint((*p_right)->y) <= y);
            WATCOM_CALL(GR_IFN(info.right_edge_func), (long)&info, (long)p_right, (long)&prev_r, TMS_RIGHT);
        }
        y_limit = (ulong)info.right.y < (ulong)info.left.y ? info.right.y : info.left.y;
        info.n = y_limit - y;
        if (WATCOM_CALL(GR_IFN(info.loop_func), (long)&info))
            break;
    }
    if (info.vtab)
        temp_free(info.vtab);
    if (old_w) {
        for (i = 0; i < n; i++)
            vpl[i]->w = old_w[i];
        temp_free(old_w);
    }
}

int h_map(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti)
{
    grs_vertex **cpl = NULL;
    int m = gr_clip_poly(n, 4, vpl, &cpl);

    if (m > 2)
        h_umap(bm, m, cpl, ti);
    temp_free(cpl);
    return m > 2 ? CLIP_NONE : CLIP_ALL;
}

void v_umap(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti)
{
    grs_vertex **p_top, **p_bot, **pvp, *prev_t = NULL, *prev_b = NULL;
    ulong x_min, x_max;
    fix w_min, w_max, x0, dw;
    int x, x_limit, i;
    fix *old_w = NULL;
    void (*tm_init)();
    grs_tmap_loop_info info;

    info.n = bm->flags & 1;
    if (info.n + 2 * grd_canvas->gc.fill_type == 2 * FILL_SOLID) {
        h_umap(bm, n, vpl, ti);
        return;
    }
    info.n += GRD_FUNCS * bm->type + ti->tmap_type;
    if (grd_canvas->gc.fill_type != FILL_NORM)
        info.clut = (uchar *)grd_canvas->gc.fill_parm;
    else if (ti->flags & TMF_CLUT)
        if ((info.clut = ti->clut) == NULL)
            info.clut = grd_screen->clut;
    tm_init = grd_function_table[info.n];
    info.loop_func = info.right_edge_func = info.left_edge_func = WATCOM_FN(gr_null);

    p_top = vpl;
    x_min = x_max = cint(vpl[0]->x);
    w_min = w_max = vpl[0]->w;
    for (pvp = vpl + 1; pvp < vpl + n; pvp++) {
        x = cint((*pvp)->x);
        if ((ulong)x < x_min) {
            x_min = x;
            p_top = pvp;
            w_min = (*pvp)->w;
        }
        if ((ulong)x > x_max) {
            x_max = x;
            w_max = (*pvp)->w;
        }
    }
    if (x_min == x_max)
        return;

    if (ti->flags & TMF_WALL) {
        x0 = (*p_top)->x;
        if (w_max > 0x20000) {
            old_w = temp_malloc(n * sizeof(fix));
            do {
                w_max >>= 2;
                w_min >>= 2;
            } while (w_max > 0x20000);
            for (i = 0; i < n; i++)
                old_w[i] = vpl[i]->w;
        }
        info.dw = dw = (w_max - w_min) / (long)(x_max - x_min);
        info.w = w_min + fix_mul(dw, FIX_CEIL(x0) - x0);
        for (pvp = vpl; pvp < vpl + n; pvp++)
            (*pvp)->w = w_min + fix_mul((*pvp)->x - x0, dw);
    }
    info.bm = *bm;
    info.x = x_min;
    info.u_mask = (1 << bm->wlog) - 1;
    p_bot = p_top;
    info.v_mask = ((1 << bm->hlog) - 1) << bm->wlog;
    info.vtab = NULL;

    WATCOM_CALL(tm_init, (long)&info);
    for (x = x_min; x != x_max; x = x_limit) {
        if (cint((*p_top)->x) <= x) {
            do {
                if (cint((*p_top)->x) == x)
                    prev_t = *p_top;
                if (++p_top >= vpl + n)
                    p_top = vpl;
            } while (cint((*p_top)->x) <= x);
            WATCOM_CALL(GR_IFN(info.left_edge_func), (long)&info, (long)p_top, (long)&prev_t, TMS_LEFT);
        }
        if (cint((*p_bot)->x) <= x) {
            do {
                if (cint((*p_bot)->x) == x)
                    prev_b = *p_bot;
                if (--p_bot < vpl)
                    p_bot = vpl + n - 1;
            } while (cint((*p_bot)->x) <= x);
            WATCOM_CALL(GR_IFN(info.right_edge_func), (long)&info, (long)p_bot, (long)&prev_b, TMS_RIGHT);
        }
        x_limit = (ulong)info.bot.x < (ulong)info.top.x ? info.bot.x : info.top.x;
        info.n = x_limit - x;
        if (WATCOM_CALL(GR_IFN(info.loop_func), (long)&info))
            break;
    }
    if (info.vtab)
        temp_free(info.vtab);
    if (old_w) {
        for (i = 0; i < n; i++)
            vpl[i]->w = old_w[i];
        temp_free(old_w);
    }
}

int v_map(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti)
{
    grs_vertex **cpl = NULL;
    int m = gr_clip_poly(n, 4, vpl, &cpl);

    if (m > 2)
        v_umap(bm, m, cpl, ti);
    temp_free(cpl);
    return m > 2 ? CLIP_NONE : CLIP_ALL;
}
