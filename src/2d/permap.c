/* permap.c -- x:\prj\tech\libsrc\2d\permap.c (Terra Nova v1.09): perspective mapper
 * dispatchers.  Decompiled from __FF.EXE (System Shock permap.c).
 *
 *   0x34A1F0 gri_per_lin_fan (name ours)   0x34A2EC per_map   0x34A33C per_umap
 *   persetup.c: 0x34FC24 gri_per_umap_setup, 0x350A00 gri_per_pick_vertices,
 *   0x350C00 gri_per_gradients, 0x34F80C gri_per_plane, 0x34F750 gri_per_check_uv,
 *   0x34F7A4 gri_per_swap_xy (names ours but the first)
 *   data: 0x36444C flat8_per_ltol (= 4), 0x4608FC gri_per_uv_ok (name ours)
 *
 * per_umap differs from System Shock:
 *  - the quad-blend shortcut (commented out in Shock) is live, keyed on gr_sub_bm_enabled
 *    (0x36DF30): a flat8 texture whose used area is under 4x4 texels is magnified 4x into a
 *    sub bitmap (gri_flat8_hv_quadruple_sub_bitmap), one under 8x8 2x
 *    (gri_flat8_hv_double_sub_bitmap); the vertex u,v are rescaled around the sub bitmap,
 *    per_umap recurses with the shortcut disabled, and u,v are scaled back (with the
 *    rounding of the shifts);
 *  - percode GR_PER_CODE_LIN draws a fan of linear-mapped triangles (gri_per_lin_fan) instead
 *    of h_umap with BILIN; WALL and FLOOR draw nothing; BIGSLOPE / SMALLSLOPE only run when
 *    |scan_slope| <= 1.0 (SMALLSLOPE: < 1.0); the scan init function is
 *    grd_function_table[dp] (hscan) or [dp + 6] (vscan, GRC_PER_VSCAN);
 *  - ps.dp is set before gri_per_umap_setup and the type offset added after it.
 * gri_per_lin_fan(bm, n, vpl, ti): uses the GRC_LIN function of the same flavour
 *   (ti->tmap_type - 30, i.e. PER -> LIN); if that is gr_null, ti->tmap_type -= 24
 *   (PER -> BILIN, left changed) and h_umap draws the polygon; else the function is called
 *   for the triangles {v0, v[i-1], v[i]} with the clut ti->clut, or the fill parm when the
 *   fill type is FILL_CLUT.
 */
#include "2d_prim.h"

#define GR_PER_CODE_BADPLANE 2
#define GR_PER_CODE_LIN 5
#define GR_PER_CODE_BIGSLOPE 8
#define GR_PER_CODE_SMALLSLOPE 9

/* Terra Nova's perspective setup (0xA8 bytes; differs from System Shock after +0x10.  The
 * scan inits of 2d-tmap reuse +0x10.. for their own state). */
typedef struct {
    void (*scanline_func)();       /* +00 */
    void (*shell_func)();          /* +04 */
    union { uchar *clut; long fill_parm; };   /* +08 */
    fix scan_slope;                /* +0C */
    int dp;                        /* +10 function table index */
    long _14[14];                  /* +14 .. +48 (scan init / shell state) */
    fix wux, wuy;                  /* +4C +50 plane x u-gradient terms (3.29 products) */
    long _54;                      /* +54 */
    fix wvx, wvy;                  /* +58 +5C */
    long _60[7];                   /* +60 .. +78 */
    fix dudx, dudy, dvdx, dvdy;    /* +7C +80 +84 +88 of u/w, v/w */
    fix dzdx, dzdy;                /* +8C +90 of 1/w */
    fix u0, v0, z0;                /* +94 +98 +9C at the first vertex's pixel centre */
    int x0, y0;                    /* +A0 +A4 that pixel */
} grs_per_setup;
LG_STATIC_ASSERT(sizeof(grs_per_setup) == 0xA8, grs_per_setup_size);

void gri_flat8_hv_quadruple_sub_bitmap(grs_bitmap *sbm, grs_bitmap *dbm, int u, int v);
void gri_flat8_hv_double_sub_bitmap(grs_bitmap *sbm, grs_bitmap *dbm, int u, int v);

/* ---------------------------------------------------------------- persetup
 * gri_per_umap_setup(n, vpl, ps) returns GR_PER_CODE_LIN when w varies less than
 *   w_max >> flat8_per_ltol or the polygon is under 30 pixels in both x and y,
 *   GR_PER_CODE_BADPLANE when no three vertices are apart (gri_per_pick_vertices, first in
 *   u,v then in x,y), else fills the gradients (gri_per_gradients with w_max, gri_per_plane
 *   with w_min) and returns SMALLSLOPE with scan_slope = -dzdy/dzdx when |dzdy| < |dzdx|
 *   (unsigned compare of the absolute values), else BIGSLOPE with -dzdx/dzdy.  Unlike System
 *   Shock it never returns WALL or FLOOR and allocates nothing.
 * gri_per_pick_vertices(n, vpl, idx, off) picks i < j < k maximising the smallest pairwise
 *   Chebyshev distance of the coordinate pair at long offset off of the vertices (2: u,v,
 *   0: x,y); returns that distance (0: none, idx then stays -1 or the last best).  With off 2
 *   and no result it retries with off 0.
 * gri_per_gradients(ps, v3, w_max): z = w/w_max (3.29), u*z, v*z per vertex; solves the
 *   screen gradients of u*z, v*z, z like gri_lin_gradients and their values at the pixel
 *   centre of v3[0].
 * gri_per_plane(ps, v3, w_min): eye-space points (x, y centred on half the larger canvas
 *   dimension and scaled by it, z = w_min/w), the plane normal (a, b, c) and the u, v
 *   gradient cross products; wux/wuy and wvx/wvy are shifted down by 3 until both of a
 *   pair are within +-0x200000.
 * gri_per_check_uv(bm, n, vpl): gri_per_uv_ok = 1 if every vertex u < w, v < h (unsigned,
 *   fix), else 0.  gri_per_swap_xy(n, vpl): swaps x and y of every vertex and reverses vpl.
 */
#ifndef TN_LINK_ORIGINAL_DATA
uchar flat8_per_ltol = 4;
uchar gri_per_uv_ok;
#else
extern uchar flat8_per_ltol, gri_per_uv_ok;
#endif

static long m29(long a, long b) { return (long)(((long long)a * b) >> 29); }
static long d29(long a, long b) { return (long)(((long long)a << 29) / b); }
static long mul_div(long a, long b, long c) { return (long)(((long long)a * b) / c); }
static long labs_(long a) { return a < 0 ? -a : a; }

int gri_per_pick_vertices(int n, grs_vertex **vpl, int *idx, int off)
{
    long best = 0, m, d, ex, ey;
    long *pi, *pj, *pk;
    int i, j, k;

    idx[0] = idx[1] = idx[2] = -1;
    for (i = 0; i < n - 2; i++) {
        pi = (long *)vpl[i] + off;
        for (j = i + 1; j < n - 1; j++) {
            pj = (long *)vpl[j] + off;
            ex = labs_(pj[0] - pi[0]);
            ey = labs_(pj[1] - pi[1]);
            m = ey >= ex ? ey : ex;
            for (k = j + 1; k < n; k++) {
                pk = (long *)vpl[k] + off;
                ex = labs_(pk[0] - pi[0]);
                ey = labs_(pk[1] - pi[1]);
                d = ey >= ex ? ey : ex;
                if (m > d)
                    m = d;
                ex = labs_(pk[0] - pj[0]);
                ey = labs_(pk[1] - pj[1]);
                d = ey >= ex ? ey : ex;
                if (m > d)
                    m = d;
                if (m > best) {
                    idx[2] = k;
                    idx[0] = i;
                    best = m;
                    idx[1] = j;
                }
            }
        }
    }
    if (best == 0 && off == 2)
        best = gri_per_pick_vertices(n, vpl, idx, 0);
    return best;
}

void gri_per_gradients(grs_per_setup *ps, grs_vertex **v3, fix w_max)
{
    fix dx1 = v3[1]->x - v3[0]->x, dy1 = v3[1]->y - v3[0]->y;
    fix dx2 = v3[2]->x - v3[0]->x, dy2 = v3[2]->y - v3[0]->y;
    fix z[3], uz[3], vz[3], du1, du2, dv1, dv2, dz1, dz2, den, fx, fy;
    int i;

    for (i = 0; i < 3; i++) {
        z[i] = d29(v3[i]->w, w_max);
        uz[i] = m29(v3[i]->u, z[i]);
        vz[i] = m29(v3[i]->v, z[i]);
    }
    du1 = uz[1] - uz[0]; du2 = uz[2] - uz[0];
    dv1 = vz[1] - vz[0]; dv2 = vz[2] - vz[0];
    dz1 = z[1] - z[0];   dz2 = z[2] - z[0];
    if (labs_(dy1) > labs_(dy2)) {
        den = dx2 - mul_div(dy2, dx1, dy1);
        ps->dudx = fix_div(du2 - mul_div(dy2, du1, dy1), den);
        ps->dvdx = fix_div(dv2 - mul_div(dy2, dv1, dy1), den);
        ps->dzdx = fix_div(dz2 - mul_div(dy2, dz1, dy1), den);
    } else {
        den = dx1 - mul_div(dy1, dx2, dy2);
        ps->dudx = fix_div(du1 - mul_div(dy1, du2, dy2), den);
        ps->dvdx = fix_div(dv1 - mul_div(dy1, dv2, dy2), den);
        ps->dzdx = fix_div(dz1 - mul_div(dy1, dz2, dy2), den);
    }
    if (labs_(dx1) > labs_(dx2)) {
        den = dy2 - mul_div(dx2, dy1, dx1);
        ps->dudy = fix_div(du2 - mul_div(dx2, du1, dx1), den);
        ps->dvdy = fix_div(dv2 - mul_div(dx2, dv1, dx1), den);
        ps->dzdy = fix_div(dz2 - mul_div(dx2, dz1, dx1), den);
    } else {
        den = dy1 - mul_div(dx1, dy2, dx2);
        ps->dudy = fix_div(du1 - mul_div(dx1, du2, dx2), den);
        ps->dvdy = fix_div(dv1 - mul_div(dx1, dv2, dx2), den);
        ps->dzdy = fix_div(dz1 - mul_div(dx1, dz2, dx2), den);
    }
    ps->x0 = (short)((v3[0]->x + 0xFFFF) >> 16);
    fx = ((v3[0]->x + 0xFFFF) & 0xFFFF0000) - v3[0]->x;
    ps->y0 = (short)((v3[0]->y + 0xFFFF) >> 16);
    fy = ((v3[0]->y + 0xFFFF) & 0xFFFF0000) - v3[0]->y;
    ps->u0 = uz[0] + fix_mul(ps->dudx, fx) + fix_mul(ps->dudy, fy);
    ps->v0 = vz[0] + fix_mul(ps->dvdx, fx) + fix_mul(ps->dvdy, fy);
    ps->z0 = z[0] + fix_mul(ps->dzdx, fx) + fix_mul(ps->dzdy, fy);
}

void gri_per_plane(grs_per_setup *ps, grs_vertex **v3, fix w_min)
{
    struct { fix x, y, z; } l[3];
    fix scale, half, dx1, dx2, dy1, dy2, dz1, dz2, a, b, c;
    fix du1, du2, dv1, dv2, ux, uy, uz, vx, vy, vz;
    int i;

    scale = fix_make(grd_canvas->bm.w > grd_canvas->bm.h ? grd_canvas->bm.w : grd_canvas->bm.h, 0);
    half = scale >> 1;
    for (i = 0; i < 3; i++) {
        l[i].z = d29(w_min, v3[i]->w);
        l[i].x = mul_div(v3[i]->x - half, l[i].z, scale);
        l[i].y = mul_div(v3[i]->y - half, l[i].z, scale);
    }
    dx1 = l[1].x - l[0].x; dx2 = l[2].x - l[0].x;
    dy1 = l[1].y - l[0].y; dy2 = l[2].y - l[0].y;
    dz1 = l[1].z - l[0].z; dz2 = l[2].z - l[0].z;
    a = m29(dy1, dz2) - m29(dy2, dz1);
    b = m29(dz1, dx2) - m29(dz2, dx1);
    c = m29(dx1, dy2) - m29(dx2, dy1);
    du1 = v3[1]->u - v3[0]->u; du2 = v3[2]->u - v3[0]->u;
    dv1 = v3[1]->v - v3[0]->v; dv2 = v3[2]->v - v3[0]->v;
    ux = m29(dx2, du1) - m29(dx1, du2);
    uy = m29(dy2, du1) - m29(dy1, du2);
    uz = m29(dz2, du1) - m29(dz1, du2);
    vx = m29(dx2, dv1) - m29(dx1, dv2);
    vy = m29(dy2, dv1) - m29(dy1, dv2);
    vz = m29(dz2, dv1) - m29(dz1, dv2);
    ps->wux = m29(b, uz) - m29(c, uy);
    ps->wuy = m29(c, ux) - m29(a, uz);
    while (labs_(ps->wux) > 0x200000 || labs_(ps->wuy) > 0x200000) {
        ps->wux >>= 3;
        ps->wuy >>= 3;
    }
    ps->wvx = m29(b, vz) - m29(c, vy);
    ps->wvy = m29(c, vx) - m29(a, vz);
    while (labs_(ps->wvx) > 0x200000 || labs_(ps->wvy) > 0x200000) {
        ps->wvx >>= 3;
        ps->wvy >>= 3;
    }
}

int gri_per_umap_setup(int n, grs_vertex **vpl, grs_per_setup *ps)
{
    fix w_min, w_max, x_min, x_max, y_min, y_max;
    int i, idx[3];
    grs_vertex *v3[3];

    w_min = w_max = vpl[0]->w;
    for (i = 1; i < n; i++) {
        if (w_min > vpl[i]->w) w_min = vpl[i]->w;
        if (w_max < vpl[i]->w) w_max = vpl[i]->w;
    }
    if ((w_max >> flat8_per_ltol) >= w_max - w_min)
        return GR_PER_CODE_LIN;
    x_min = x_max = vpl[0]->x;
    y_min = y_max = vpl[0]->y;
    for (i = 1; i < n; i++) {
        if (x_min > vpl[i]->x) x_min = vpl[i]->x;
        if (x_max < vpl[i]->x) x_max = vpl[i]->x;
        if (y_min > vpl[i]->y) y_min = vpl[i]->y;
        if (y_max < vpl[i]->y) y_max = vpl[i]->y;
    }
    if (y_max - y_min < fix_make(30, 0) && x_max - x_min < fix_make(30, 0))
        return GR_PER_CODE_LIN;
    if (gri_per_pick_vertices(n, vpl, idx, 2) == 0)
        return GR_PER_CODE_BADPLANE;
    v3[0] = vpl[idx[0]];
    v3[1] = vpl[idx[1]];
    v3[2] = vpl[idx[2]];
    gri_per_gradients(ps, v3, w_max);
    gri_per_plane(ps, v3, w_min);
    if ((ulong)labs_(ps->dzdy) < (ulong)labs_(ps->dzdx)) {
        ps->scan_slope = -fix_div(ps->dzdy, ps->dzdx);
        return GR_PER_CODE_SMALLSLOPE;
    }
    ps->scan_slope = -fix_div(ps->dzdx, ps->dzdy);
    return GR_PER_CODE_BIGSLOPE;
}

void gri_per_check_uv(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    ulong w = (ulong)bm->w << 16, h = (ulong)bm->h << 16;
    int i;

    gri_per_uv_ok = 1;
    for (i = 0; i < n; i++)
        if ((ulong)vpl[i]->u >= w || (ulong)vpl[i]->v >= h) {
            gri_per_uv_ok = 0;
            return;
        }
}

void gri_per_swap_xy(int n, grs_vertex **vpl)
{
    grs_vertex *t;
    fix x;
    int i;

    for (i = 0; i < n; i++) {
        x = vpl[i]->x;
        vpl[i]->x = vpl[i]->y;
        vpl[i]->y = x;
    }
    for (i = 0; i < n / 2; i++) {
        t = vpl[i];
        vpl[i] = vpl[n - 1 - i];
        vpl[n - 1 - i] = t;
    }
}

/* ---------------------------------------------------------------- dispatchers */
void gri_per_lin_fan(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti)
{
    void (*f)();
    grs_vertex *tri[3];
    long fill;
    int i;

    f = grd_function_table[(bm->flags & 1) + ti->tmap_type + GRD_FUNCS * bm->type - (GRC_PER - GRC_LIN)];
    if ((void *)f == WATCOM_FN(gr_null)) {
        ti->tmap_type -= GRC_PER - GRC_BILIN;
        h_umap(bm, n, vpl, ti);
        return;
    }
    fill = (long)ti->clut;
    if (grd_canvas->gc.fill_type == FILL_CLUT)
        fill = grd_canvas->gc.fill_parm;
    WATCOM_CALL(f, (long)bm, 3, (long)vpl, fill);
    if (n <= 3)
        return;
    tri[0] = vpl[0];
    tri[1] = vpl[2];
    tri[2] = vpl[3];
    WATCOM_CALL(f, (long)bm, 3, (long)tri, fill);
    for (i = 4; i < n; i++) {
        tri[1] = tri[2];
        tri[2] = vpl[i];
        WATCOM_CALL(f, (long)bm, 3, (long)tri, fill);
    }
}

int per_map(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti)
{
    grs_vertex **cpl = NULL;
    int m = gr_clip_poly(n, 5, vpl, &cpl);

    if (m > 2)
        per_umap(bm, m, cpl, ti);
    temp_free(cpl);
    return m > 2 ? CLIP_NONE : CLIP_ALL;
}

/* Rescale the vertex u,v for a sub bitmap at texel (u, v) magnified by 1 << sh, and back. */
static void sub_bm_uv(int n, grs_vertex **vpl, int u, int v, int sh)
{
    int i;
    for (i = 0; i < n; i++) {
        vpl[i]->u = ((vpl[i]->u - (u << 16)) << sh) + 0x80000;
        vpl[i]->v = ((vpl[i]->v - (v << 16)) << sh) + 0x80000;
    }
}
static void sub_bm_uv_back(int n, grs_vertex **vpl, int u, int v, int sh)
{
    int i;
    for (i = 0; i < n; i++) {
        vpl[i]->u = ((vpl[i]->u - 0x80000) >> sh) + (u << 16);
        vpl[i]->v = ((vpl[i]->v - 0x80000) >> sh) + (v << 16);
    }
}

void per_umap(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti)
{
    grs_per_setup ps;
    ushort percode;
    uchar *save_bits;

    ps.dp = bm->flags & 1;
    if (2 * grd_canvas->gc.fill_type + ps.dp == 2 * FILL_SOLID) {
        h_umap(bm, n, vpl, ti);
        return;
    }
    if (bm->type == BMT_FLAT8 && gr_sub_bm_enabled) {
        fix u_min, u_max, v_min, v_max;
        int i, u, v, sh, size;
        grs_bitmap tbm;

        u_min = u_max = vpl[0]->u;
        v_min = v_max = vpl[0]->v;
        for (i = 1; i < n; i++) {
            if (vpl[i]->u > u_max) u_max = vpl[i]->u;
            if (u_min > vpl[i]->u) u_min = vpl[i]->u;
            if (vpl[i]->v > v_max) v_max = vpl[i]->v;
            if (v_min > vpl[i]->v) v_min = vpl[i]->v;
        }
        if ((u_max >> 16) - (u_min >> 16) < 4 && (v_max >> 16) - (v_min >> 16) < 4) {
            sh = 2; size = 4;
        } else if ((u_max >> 16) - (u_min >> 16) < 8 && (v_max >> 16) - (v_min >> 16) < 8) {
            sh = 1; size = 8;
        } else
            goto general;
        u = u_min >> 16;
        v = v_min >> 16;
        if (u + size >= bm->w)
            u = bm->w - size;
        if (v + size >= bm->h)
            v = bm->h - size;
        tbm.type = BMT_FLAT8;
        tbm.flags = bm->flags;
        if (sh == 2)
            gri_flat8_hv_quadruple_sub_bitmap(bm, &tbm, u, v);
        else
            gri_flat8_hv_double_sub_bitmap(bm, &tbm, u, v);
        sub_bm_uv(n, vpl, u, v, sh);
        gr_sub_bm_enabled = 0;
        per_umap(&tbm, n, vpl, ti);
        gr_sub_bm_enabled = 1;
        sub_bm_uv_back(n, vpl, u, v, sh);
        return;
    }
general:
    percode = gri_per_umap_setup(n, vpl, &ps);
    ps.shell_func = WATCOM_FN(gr_null);
    ps.dp += ti->tmap_type + GRD_FUNCS * bm->type;
    if (grd_canvas->gc.fill_type != FILL_NORM)
        ps.fill_parm = grd_canvas->gc.fill_parm;
    else if (ti->flags & TMF_CLUT)
        if ((ps.clut = ti->clut) == NULL)
            ps.clut = grd_screen->clut;
    save_bits = bm->bits;
    switch (percode) {
    case GR_PER_CODE_LIN:
        gri_per_lin_fan(bm, n, vpl, ti);
        break;
    case GR_PER_CODE_BIGSLOPE:
        if ((ulong)(ps.scan_slope + 0x10000) <= 0x20000) {
            WATCOM_CALL(grd_function_table[ps.dp], (long)bm, (long)&ps);
            WATCOM_CALL(ps.shell_func, (long)bm, n, (long)vpl, (long)&ps);
        }
        break;
    case GR_PER_CODE_SMALLSLOPE:
        if ((ulong)(ps.scan_slope + 0x10000) < 0x20000) {
            WATCOM_CALL(grd_function_table[ps.dp + GRC_PER_VSCAN - GRC_PER], (long)bm, (long)&ps);
            WATCOM_CALL(ps.shell_func, (long)bm, n, (long)vpl, (long)&ps);
        }
        break;
    }
    bm->bits = save_bits;
}
