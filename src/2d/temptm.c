/* temptm.c -- x:\prj\tech\libsrc\2d\temptm.c, tempbm.c (Terra Nova v1.09): "temporary"
 * dispatchers from the canvas table to the function table and the polygon / texture mappers,
 * plus the poly and lit-texture helpers they use.  Decompiled from __FF.EXE.
 *
 *   0x327560 temp_upoint            0x32757C temp_point             0x327594 temp_rsd8_ubitmap
 *   0x3275A4 temp_rsd8_bitmap       0x327840 gri_lit_clut           0x3278A4 temp_flat8_mask_bitmap
 *   0x3278B4 temp_flat8_clut_ubitmap 0x3278C4 temp_tluc8_ubitmap    0x3278D4 temp_poly
 *   0x327914 temp_upoly             0x327954 temp_spoly             0x327990 temp_uspoly
 *   0x3279CC temp_cpoly             0x327A08 temp_ucpoly            0x327A44 temp_tpoly
 *   0x327A7C temp_utpoly            0x327AB8 temp_stpoly            0x327AF4 temp_ustpoly
 *   0x327B30 temp_per_map           0x327B68 temp_per_umap          0x327BA0 temp_clut_per_map
 *   0x327BE4 temp_clut_per_umap     0x327C28 temp_lit_per_umap      0x327C90 temp_lin_umap
 *   0x327E04 temp_lit_lin_umap      0x327FA0 temp_clut_lin_umap     0x3280F4 temp_bilin_umap
 *   0x328138 temp_clut_bilin_umap   0x328164 temp_lit_bilin_umap    0x3281A8 temp_lin_map
 *   0x3281F4 temp_lit_lin_map       0x328240 temp_clut_lin_map      0x328290 temp_floor_umap
 *   0x3282CC temp_clut_floor_umap   0x328310 temp_lit_floor_umap    0x32837C temp_wall_umap
 *   0x3283B8 temp_clut_wall_umap    0x3283FC temp_lit_wall_umap     0x328468 temp_scale_umap
 *   0x328504 temp_clut_scale_umap   0x3285AC gri_scale_clip         0x3286EC temp_scale_map2
 *   0x328784 temp_scale_map3        0x32882C temp_scale_map         0x3288E8 temp_clut_scale_map
 *   0x32A530 temp_flat8_ubitmap
 *
 * Terra Nova differences from System Shock: temp_lin_umap & co. split polygons into a fan of
 * triangles and call the function table's GRC_LIN mapper directly when the bitmap's rows are
 * a power of two (unless gr_lin_no_split); the lit mappers fall back to a clut mapper with a
 * light table row when the vertex intensities are close (gri_lit_clut); the *_lin_map
 * versions clip with gr_clip_poly; temp_poly passes only the low byte of the color.
 * 0x3280F4, 0x328138, 0x328164, 0x3286EC and 0x328784 are not referenced by anything (older
 * versions left in the library); the names are ours.
 */
#include "2d_prim.h"

#define FT(i) (grd_function_table[i])
#define ROW_POW2(bm) ((bm)->row == (1u << ((bm)->wlog & 31)))

void temp_upoint(short x, short y)
{
    WC3(FT(GRC_PIXEL), grd_gc.fcolor, x, y);
}

int temp_point(short x, short y)
{
    return gen_fill_pixel(grd_gc.fcolor, x, y);
}

long temp_rsd8_ubitmap(grs_bitmap *bm, int x, int y)
{
    return WC3(FT(GRC_BITMAP + BMT_RSD8 * GRD_FUNCS), bm, x, y);
}

long temp_rsd8_bitmap(grs_bitmap *bm, int x, int y)
{
    return WC4(FT(GRC_STENCIL_BITMAP + BMT_RSD8 * GRD_FUNCS), bm, x, y, grd_clip.sten);
}

long temp_flat8_mask_bitmap(grs_bitmap *bm, int x, int y, grs_stencil *sten)
{
    return WC4(FT(GRC_STENCIL_BITMAP + BMT_FLAT8 * GRD_FUNCS), bm, x, y, sten);
}

/* Shock dispatches GRC_BITMAP here; Terra Nova GRC_CLUT_BITMAP */
long temp_flat8_clut_ubitmap(grs_bitmap *bm, int x, int y, uchar *cl)
{
    return WC4(FT(GRC_CLUT_BITMAP + BMT_FLAT8 * GRD_FUNCS), bm, x, y, cl);
}

long temp_tluc8_ubitmap(grs_bitmap *bm, int x, int y)
{
    return WC3(FT(GRC_BITMAP + BMT_TLUC8 * GRD_FUNCS), bm, x, y);
}

/* tempbm.c */
long temp_flat8_ubitmap(grs_bitmap *bm, int x, int y)
{
    return WC3(FT(GRC_BITMAP + BMT_FLAT8 * GRD_FUNCS), bm, x, y);
}

/* If the intensities of the polygon's vertices differ by less than gr_lit_tolerance, returns
 * the light table row for their average (so a lit mapper can be replaced by a clut mapper);
 * else NULL.  Also NULL for n <= 0. */
uchar *gri_lit_clut(int n, grs_vertex **vpl)
{
    grs_vertex **p;
    fix lo, hi, v;

    if (n <= 0)
        return NULL;
#ifndef TN_WATCOM_ABI
    {
        extern int port_lit_dither;     /* "Terrain smoothing": no one-level shortcut (perloop.c) */
        if (port_lit_dither)
            return NULL;
    }
#endif
    p = vpl + n - 1;
    hi = lo = (*p)->i;
    for (p--; p >= vpl; p--) {
        v = (*p)->i;
        if (v < lo)
            lo = v;
        if (v > hi)
            hi = v;
    }
    if (hi - lo >= gr_lit_tolerance)
        return NULL;
    return grd_screen->ltab + (((hi + lo) >> 9) & ~0xff);
}

/* ---------------- polygons ---------------- */
enum { POLY, SPOLY, CPOLY, TPOLY, STPOLY };

#define POLY_SETUP(kind)              \
    grs_tmap_info ti;                 \
    grs_bitmap bm;                    \
    ti.tmap_type = GRC_POLY;          \
    ti.flags = 0;                     \
    bm.bits = (uchar *)(c & 0xff);    \
    bm.type = kind;                   \
    bm.flags = 0

int temp_poly(long c, int n, grs_vertex **vpl) { POLY_SETUP(POLY); return h_map(&bm, n, vpl, &ti); }
void temp_upoly(long c, int n, grs_vertex **vpl) { POLY_SETUP(POLY); h_umap(&bm, n, vpl, &ti); }
int temp_spoly(long c, int n, grs_vertex **vpl) { POLY_SETUP(SPOLY); return h_map(&bm, n, vpl, &ti); }
void temp_uspoly(long c, int n, grs_vertex **vpl) { POLY_SETUP(SPOLY); h_umap(&bm, n, vpl, &ti); }
int temp_cpoly(long c, int n, grs_vertex **vpl) { POLY_SETUP(CPOLY); return h_map(&bm, n, vpl, &ti); }
void temp_ucpoly(long c, int n, grs_vertex **vpl) { POLY_SETUP(CPOLY); h_umap(&bm, n, vpl, &ti); }
int temp_tpoly(long c, int n, grs_vertex **vpl) { POLY_SETUP(TPOLY); return h_map(&bm, n, vpl, &ti); }
void temp_utpoly(long c, int n, grs_vertex **vpl) { POLY_SETUP(TPOLY); h_umap(&bm, n, vpl, &ti); }
int temp_stpoly(long c, int n, grs_vertex **vpl) { POLY_SETUP(STPOLY); return h_map(&bm, n, vpl, &ti); }
void temp_ustpoly(long c, int n, grs_vertex **vpl) { POLY_SETUP(STPOLY); h_umap(&bm, n, vpl, &ti); }

/* ---------------- perspective ---------------- */
void temp_per_map(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    if (ROW_POW2(bm)) {
        grs_tmap_info ti;
        ti.tmap_type = GRC_PER;
        ti.flags = 0;
        per_map(bm, n, vpl, &ti);
    }
}

void temp_per_umap(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    if (ROW_POW2(bm)) {
        grs_tmap_info ti;
        ti.tmap_type = GRC_PER;
        ti.flags = 0;
        per_umap(bm, n, vpl, &ti);
    }
}

void temp_clut_per_map(grs_bitmap *bm, int n, grs_vertex **vpl, uchar *clut)
{
    if (ROW_POW2(bm)) {
        grs_tmap_info ti;
        ti.tmap_type = GRC_CLUT_PER;
        ti.flags = TMF_CLUT;
        ti.clut = clut;
        per_map(bm, n, vpl, &ti);
    }
}

void temp_clut_per_umap(grs_bitmap *bm, int n, grs_vertex **vpl, uchar *clut)
{
    if (ROW_POW2(bm)) {
        grs_tmap_info ti;
        ti.tmap_type = GRC_CLUT_PER;
        ti.flags = TMF_CLUT;
        ti.clut = clut;
        per_umap(bm, n, vpl, &ti);
    }
}

void temp_lit_per_umap(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    if (ROW_POW2(bm)) {
        grs_tmap_info ti;
        ti.clut = gri_lit_clut(n, vpl);
        if (ti.clut == NULL) {
            ti.tmap_type = GRC_LIT_PER;
            ti.flags = 0;
        } else {
            ti.tmap_type = GRC_CLUT_PER;
            ti.flags = TMF_CLUT;
        }
        per_umap(bm, n, vpl, &ti);
    }
}

/* ---------------- linear ---------------- */

/* Draws polygon vpl[0..n-1] as the triangle fan (0,1,2), (0,2,3), ... with f(bm, 3, tri, parm). */
static void tri_fan(void *f, grs_bitmap *bm, int n, grs_vertex **vpl, long parm)
{
    grs_vertex *tri[3];
    int i;

    WC4(f, bm, 3, vpl, parm);
    if (n <= 3)
        return;
    tri[0] = vpl[0];
    tri[1] = vpl[2];
    tri[2] = vpl[3];
    WC4(f, bm, 3, tri, parm);
    for (i = 4; i < n; i++) {
        tri[1] = tri[2];
        tri[2] = vpl[i];
        WC4(f, bm, 3, tri, parm);
    }
}

void temp_lin_umap(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    grs_tmap_info ti;
    long parm = grd_gc.fill_parm;

    if (!gr_lin_no_split && ROW_POW2(bm)) {
        void *f = FT(bm->type * GRD_FUNCS + GRC_LIN + (bm->flags & BMF_TRANS));
        if (f != WATCOM_FN(gr_null)) {
            tri_fan(f, bm, n, vpl, parm);
            return;
        }
    } else if (!ROW_POW2(bm) && grd_gc.fill_type != FILL_CLUT)
        return;
    ti.tmap_type = GRC_BILIN;
    ti.flags = 0;
    h_umap(bm, n, vpl, &ti);
}

void temp_lit_lin_umap(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    grs_tmap_info ti;
    uchar *cl = gri_lit_clut(n, vpl);

    if (cl) {
        temp_clut_lin_umap(bm, n, vpl, cl);
        return;
    }
    if (!gr_lin_no_split && ROW_POW2(bm)) {
        long parm = grd_gc.fill_parm;
        void *f = FT(bm->type * GRD_FUNCS + GRC_LIT_LIN + (bm->flags & BMF_TRANS));
        if (f != WATCOM_FN(gr_null)) {
            tri_fan(f, bm, n, vpl, parm);
            return;
        }
    } else if (!ROW_POW2(bm) && grd_gc.fill_type != FILL_CLUT)
        return;
    ti.tmap_type = GRC_LIT_BILIN;
    ti.flags = 0;
    h_umap(bm, n, vpl, &ti);
}

/* With a FILL_CLUT canvas the fill_parm clut replaces cl, but only on the split path. */
void temp_clut_lin_umap(grs_bitmap *bm, int n, grs_vertex **vpl, uchar *cl)
{
    grs_tmap_info ti;

    if (!gr_lin_no_split && ROW_POW2(bm)) {
        int idx = bm->type * GRD_FUNCS + GRC_CLUT_LIN + (bm->flags & BMF_TRANS);
        void *f;
        if (grd_gc.fill_type == FILL_CLUT)
            cl = (uchar *)grd_gc.fill_parm;
        f = FT(idx);
        if (f != WATCOM_FN(gr_null)) {
            tri_fan(f, bm, n, vpl, (long)cl);
            return;
        }
    }
    ti.tmap_type = GRC_CLUT_BILIN;
    ti.flags = TMF_CLUT;
    ti.clut = cl;
    h_umap(bm, n, vpl, &ti);
}

/* unreferenced: Shock-style temp_lin_umap without the triangle split (always bilinear) */
void temp_bilin_umap(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    if (ROW_POW2(bm) || grd_gc.fill_type == FILL_CLUT) {
        grs_tmap_info ti;
        ti.tmap_type = GRC_BILIN;
        ti.flags = 0;
        h_umap(bm, n, vpl, &ti);
    }
}

/* unreferenced */
void temp_clut_bilin_umap(grs_bitmap *bm, int n, grs_vertex **vpl, uchar *cl)
{
    grs_tmap_info ti;
    ti.tmap_type = GRC_CLUT_BILIN;
    ti.flags = TMF_CLUT;
    ti.clut = cl;
    h_umap(bm, n, vpl, &ti);
}

/* unreferenced */
void temp_lit_bilin_umap(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    if (ROW_POW2(bm) || grd_gc.fill_type == FILL_CLUT) {
        grs_tmap_info ti;
        ti.tmap_type = GRC_LIT_BILIN;
        ti.flags = 0;
        h_umap(bm, n, vpl, &ti);
    }
}

/* clipped versions: clip the polygon (l = number of fix fields to interpolate), draw it,
 * free the clipped vertex list.  Return CLIP_NONE or CLIP_ALL. */
int temp_lin_map(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    grs_vertex **cpl = NULL;
    int m = gr_clip_poly(n, 4, vpl, &cpl);
    if (m > 2)
        temp_lin_umap(bm, m, cpl);
    temp_free(cpl);
    return m > 2 ? CLIP_NONE : CLIP_ALL;
}

int temp_lit_lin_map(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    grs_vertex **cpl = NULL;
    int m = gr_clip_poly(n, 6, vpl, &cpl);
    if (m > 2)
        temp_lit_lin_umap(bm, m, cpl);
    temp_free(cpl);
    return m > 2 ? CLIP_NONE : CLIP_ALL;
}

int temp_clut_lin_map(grs_bitmap *bm, int n, grs_vertex **vpl, uchar *cl)
{
    grs_vertex **cpl = NULL;
    int m = gr_clip_poly(n, 4, vpl, &cpl);
    if (m > 2)
        temp_clut_lin_umap(bm, m, cpl, cl);
    temp_free(cpl);
    return m > 2 ? CLIP_NONE : CLIP_ALL;
}

/* ---------------- floor / wall ---------------- */
void temp_floor_umap(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    if (ROW_POW2(bm)) {
        grs_tmap_info ti;
        ti.tmap_type = GRC_FLOOR;
        ti.flags = TMF_FLOOR;
        h_umap(bm, n, vpl, &ti);
    }
}

void temp_clut_floor_umap(grs_bitmap *bm, int n, grs_vertex **vpl, uchar *cl)
{
    if (ROW_POW2(bm)) {
        grs_tmap_info ti;
        ti.tmap_type = GRC_CLUT_FLOOR;
        ti.flags = TMF_CLUT | TMF_FLOOR;
        ti.clut = cl;
        h_umap(bm, n, vpl, &ti);
    }
}

void temp_lit_floor_umap(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    if (ROW_POW2(bm)) {
        grs_tmap_info ti;
        ti.clut = gri_lit_clut(n, vpl);
        if (ti.clut == NULL) {
            ti.tmap_type = GRC_LIT_FLOOR;
            ti.flags = TMF_FLOOR;
        } else {
            ti.tmap_type = GRC_CLUT_FLOOR;
            ti.flags = TMF_CLUT | TMF_FLOOR;
        }
        h_umap(bm, n, vpl, &ti);
    }
}

void temp_wall_umap(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    if (ROW_POW2(bm)) {
        grs_tmap_info ti;
        ti.tmap_type = GRC_WALL2D;
        ti.flags = TMF_WALL;
        v_umap(bm, n, vpl, &ti);
    }
}

void temp_clut_wall_umap(grs_bitmap *bm, int n, grs_vertex **vpl, uchar *cl)
{
    if (ROW_POW2(bm)) {
        grs_tmap_info ti;
        ti.tmap_type = GRC_CLUT_WALL2D;
        ti.flags = TMF_CLUT | TMF_WALL;
        ti.clut = cl;
        v_umap(bm, n, vpl, &ti);
    }
}

void temp_lit_wall_umap(grs_bitmap *bm, int n, grs_vertex **vpl)
{
    if (ROW_POW2(bm)) {
        grs_tmap_info ti;
        ti.clut = gri_lit_clut(n, vpl);
        if (ti.clut == NULL) {
            ti.tmap_type = GRC_LIT_WALL2D;
            ti.flags = TMF_WALL;
        } else {
            ti.tmap_type = GRC_CLUT_WALL2D;
            ti.flags = TMF_CLUT | TMF_WALL;
        }
        v_umap(bm, n, vpl, &ti);
    }
}

/* ---------------- scalers ---------------- */

/* take int _x,_y; fix _u,_v; stuff them into grs_vertex _vertex */
#define make_vertex(_vertex, _x, _y, _u, _v) \
    _vertex.x = fix_make(_x, 0), _vertex.y = fix_make(_y, 0), _vertex.u = _u, _vertex.v = _v

void temp_scale_umap(grs_bitmap *bm, short x, short y, short w, short h)
{
    grs_tmap_info ti;
    grs_vertex *vpl[2];
    grs_vertex v0, v1;

    vpl[0] = &v0;
    vpl[1] = &v1;
    make_vertex(v0, x, y, 0, 0);
    make_vertex(v1, x + w, y + h, fix_make(bm->w, 0), fix_make(bm->h, 0));
    ti.tmap_type = GRC_SCALE;
    ti.flags = 0;
    h_umap(bm, 2, vpl, &ti);
}

void temp_clut_scale_umap(grs_bitmap *bm, short x, short y, short w, short h, uchar *cl)
{
    grs_tmap_info ti;
    grs_vertex *vpl[2];
    grs_vertex v0, v1;

    vpl[0] = &v0;
    vpl[1] = &v1;
    make_vertex(v0, x, y, 0, 0);
    make_vertex(v1, x + w, y + h, fix_make(bm->w, 0), fix_make(bm->h, 0));
    ti.tmap_type = GRC_CLUT_SCALE;
    ti.flags = TMF_CLUT;
    ti.clut = cl;
    h_umap(bm, 2, vpl, &ti);
}

/* Clips the scaled rectangle v0-v1 (u, v of v1 = bitmap size) to the canvas.  Quirk kept from
 * Shock: clipping the bottom edge reports CLIP_RIGHT. */
int gri_scale_clip(grs_vertex *v0, grs_vertex *v1)
{
    int code;
    fix u_scale, v_scale;

    if (v0->x >= grd_fix_clip.right || v1->x <= grd_fix_clip.left ||
        v0->y >= grd_fix_clip.bot || v1->y <= grd_fix_clip.top)
        return CLIP_ALL;

    code = CLIP_NONE;
    u_scale = fix_div(v1->u, v1->x - v0->x);
    v_scale = fix_div(v1->v, v1->y - v0->y);

    if (v0->x < grd_fix_clip.left) {
        v0->u = fix_mul(u_scale, grd_fix_clip.left - v0->x);
        v0->x = grd_fix_clip.left;
        code |= CLIP_LEFT;
    }
    if (v1->x > grd_fix_clip.right) {
        v1->x = grd_fix_clip.right;
        v1->u = v0->u + fix_mul(u_scale, v1->x - v0->x);
        code |= CLIP_RIGHT;
    }
    if (v0->y < grd_fix_clip.top) {
        v0->v = fix_mul(v_scale, grd_fix_clip.top - v0->y);
        v0->y = grd_fix_clip.top;
        code |= CLIP_TOP;
    }
    if (v1->y > grd_fix_clip.bot) {
        v1->y = grd_fix_clip.bot;
        v1->v = v0->v + fix_mul(v_scale, v1->y - v0->y);
        code |= CLIP_RIGHT;
    }
    return code;
}

/* unreferenced: clipped scale through the scaler at 0x34AA7C; returns 0 when fully clipped */
long temp_scale_map2(grs_bitmap *bm, short x, short y, short w, short h)
{
    grs_vertex *vpl[2];
    grs_vertex v0, v1;

    vpl[0] = &v0;
    vpl[1] = &v1;
    make_vertex(v0, x, y, 0, 0);
    make_vertex(v1, x + w, y + h, fix_make(bm->w, 0), fix_make(bm->h, 0));
    if (gri_scale_clip(&v0, &v1) == CLIP_ALL)
        return 0;
    return gri_scale_34AA7C(bm, 2, vpl);
}

/* unreferenced: clipped scale through the scaler at 0x34AC54; returns the clip code */
int temp_scale_map3(grs_bitmap *bm, short x, short y, short w, short h)
{
    grs_vertex *vpl[2];
    grs_vertex v0, v1;
    int code;

    vpl[0] = &v0;
    vpl[1] = &v1;
    make_vertex(v0, x, y, 0, 0);
    make_vertex(v1, x + w, y + h, fix_make(bm->w, 0), fix_make(bm->h, 0));
    code = gri_scale_clip(&v0, &v1);
    if (code != CLIP_ALL)
        gri_scale_34AC54(bm, 2, vpl);
    return code;
}

int temp_scale_map(grs_bitmap *bm, short x, short y, short w, short h)
{
    grs_tmap_info ti;
    grs_vertex *vpl[2];
    grs_vertex v0, v1;
    int code;

    vpl[0] = &v0;
    vpl[1] = &v1;
    make_vertex(v0, x, y, 0, 0);
    make_vertex(v1, x + w, y + h, fix_make(bm->w, 0), fix_make(bm->h, 0));
    code = gri_scale_clip(&v0, &v1);
    if (code == CLIP_ALL)
        return code;
    ti.tmap_type = GRC_SCALE;
    ti.flags = 0;
    h_umap(bm, 2, vpl, &ti);
    return code;
}

int temp_clut_scale_map(grs_bitmap *bm, short x, short y, short w, short h, uchar *cl)
{
    grs_tmap_info ti;
    grs_vertex *vpl[2];
    grs_vertex v0, v1;
    int code;

    vpl[0] = &v0;
    vpl[1] = &v1;
    make_vertex(v0, x, y, 0, 0);
    make_vertex(v1, x + w, y + h, fix_make(bm->w, 0), fix_make(bm->h, 0));
    code = gri_scale_clip(&v0, &v1);
    if (code == CLIP_ALL)
        return code;
    ti.tmap_type = GRC_CLUT_SCALE;
    ti.flags = TMF_CLUT;
    ti.clut = cl;
    h_umap(bm, 2, vpl, &ti);
    return CLIP_NONE;
}
