/* fl8bilin.c -- Terra Nova v1.09 flat8 "bilinear" mappers (function table GRC_BILIN ..
 * GRC_TRANS_CLUT_BILIN for BMT_FLAT8): u and v are interpolated along both edges and
 * linearly across each scanline (1/n from grd_recip_tab), so they work for any polygon.
 * Decompiled from __FF.EXE; the original is self-modifying asm.
 *
 *   0x32FC50 gri_bilin_right_edge             0x32FD40 gri_bilin_left_edge
 *   0x32FE20 gri_bilin_umap_init              0x32FEC0 gri_bilin_umap_loop
 *   0x330080 gri_trans_bilin_right_edge       0x330170 gri_trans_bilin_left_edge
 *   0x330250 gri_solid_bilin_umap_init        0x3302A0 gri_trans_bilin_umap_init
 *   0x330350 gri_trans_bilin_umap_loop
 *   0x330500 gri_lit_bilin_right_edge         0x330610 gri_lit_bilin_left_edge
 *   0x330720 gri_lit_bilin_umap_init          0x3307D0 gri_lit_bilin_umap_loop
 *   0x330A50 gri_trans_lit_bilin_right_edge   0x330B60 gri_trans_lit_bilin_left_edge
 *   0x330C70 gri_trans_lit_bilin_umap_init    0x330D30 gri_trans_lit_bilin_umap_loop
 *   0x330F80 gri_clut_bilin_right_edge        0x331070 gri_clut_bilin_left_edge
 *   0x331150 gri_clut_bilin_umap_init         0x3311F0 gri_clut_bilin_umap_loop (terrain)
 *   0x3313E0 gri_vbilin_right_edge            0x3314D0 gri_vbilin_left_edge
 *   0x3315B0 gri_solid_vbilin_umap_init (unreferenced)  0x3315CA gri_clut_vbilin_umap_init (unreferenced)
 *   0x3315E3 gri_trans_clut_vbilin_umap_init  0x3316A0 gri_vbilin_umap_loop (sprites)
 *   0x333250 gri_tluc8_trans_bilin_right_edge 0x333340 gri_tluc8_trans_bilin_left_edge
 *   0x333420 gri_tluc8_trans_bilin_umap_init  0x3334E0 gri_tluc8_trans_bilin_umap_loop
 *   (BMT_TLUC8 GRC_TRANS_BILIN: the trans bilin mapper drawing tluc8tab[t] ? tluc8tab[t][dst] : t)
 *
 * The inits are h_umap scanline inits (eax = tmap info, return it unchanged).  The "vbilin"
 * mapper addresses texels through a row table (gri_init_vtab), so it needs no power-of-two
 * texture and does not wrap; gri_solid_bilin_umap_init (fill type FILL_SOLID) uses it for
 * textures whose row/height are not powers of two.
 *
 * Tmap info use (see fl8lin.c): +08 holds the x fractions of both edges, left.x is the left
 * destination pointer, left/right u, v (and i for lit) are the values at the edge ends, the
 * right destination pointer and all per-edge/per-scanline steps live in the patched code
 * (static state here).  The lit mappers keep their destination pointers one byte low.
 *
 * Quirks kept:
 *  - bilin and clut bilin: on an empty scanline right.v is set to the new left.v.
 *  - bilin: no span length limit (grd_recip_tab is read past its end for long spans);
 *    the others stop (return 1) on spans of 0x400 or more.
 *  - texel index masks are (h << wlog) - 1 except trans bilin ((1 << (wlog+hlog)) - 1).
 *  - lit: the intensity fraction register keeps the light table byte's carries in its low
 *    bits; the v fraction step loses its low byte and the v fraction register's bits 8-15
 *    hold the last pixel, so pixels feed into the v carry (as in the asm).
 *  - trans lit: after a transparent texel the low byte of the intensity fraction is cleared
 *    when the next texel needs a v carry.
 */
#include "2d_tmap.h"

extern long grd_recip_tab[];       /* 0x45F71C fix 1/n */
long *gri_init_vtab(grs_bitmap *bm);                    /* 0x34C640 (temp_malloc) */

#define fix_cint32(a) ((fix)((a) + 0xffff) >> 16)
#define TI_FRAC(ti) (*(ulong *)&(ti)->bm.bits)
#define TI_FRAC_L(ti) (((ushort *)&(ti)->bm.bits)[0])
#define TI_FRAC_R(ti) (((ushort *)&(ti)->bm.bits)[1])
#define CARRY_ADD(r, a, c) do { ulong t_ = (r) + (a); (c) = t_ < (r); (r) = t_; } while (0)

typedef struct {
    uchar *dbits;                  /* canvas bits/row at init (edge functions) */
    ulong drow;
    uchar *rp;                     /* right end pointer */
    ulong rfrac16, rint;           /* right edge x steps */
    ushort lfrac;                  /* left edge x steps */
    ulong lint;
    fix dudy_l, dvdy_l, didy_l;    /* per line steps along the edges */
    fix dudy_r, dvdy_r, didy_r;
    uchar *bits;
    ulong mask, vmask, w;
    int wlog, wshift;
    ulong step, step2;
    uchar *clut, *ltab;
    uchar color;
    int mode;
    long *vtab;
} bilin_state;

enum { BL_TEXEL, BL_SOLID, BL_CLUT, BL_TRANS_CLUT };

static bilin_state bl_s, tbl_s, lbl_s, tlbl_s, cbl_s, vbl_s, ttbl_s;

/* ---------------- edges ---------------- */
static fix edge_slope(grs_vertex *v0, grs_vertex *v1)
{
    fix dx = v1->x - v0->x;
    fix q = (fix)((long long)dx * 65536 / (v1->y - v0->y));
    if (q != 0x7fffffff && q != (fix)0x80000000)
        q += dx < 0 ? -1 : 0;
    return q;
}

static uchar *edge_start(bilin_state *s, grs_vertex *v0, fix q, ushort *frac)
{
    fix ypre = (-v0->y) & 0xffff;
    fix x = v0->x + tm_fix_mul(q, ypre) + 0xffff;
    uchar *base = s->dbits + (long)((ulong)fix_cint32(v0->y) * s->drow);
    *frac = (ushort)x;
    return base + (x >> 16);
}

/* parameter p (byte offset into the vertex) along the edge: step per line, first value */
static fix edge_param(grs_vertex *v0, grs_vertex *v1, int off, fix *d)
{
    fix a = *(fix *)((char *)v0 + off), b = *(fix *)((char *)v1 + off);
    fix ypre = (-v0->y) & 0xffff;
    *d = tm_fix_div(b - a, v1->y - v0->y);
    return tm_fix_mul(*d, ypre) + a;
}

static void bl_right_edge(bilin_state *s, grs_tmap_loop_info *ti, grs_vertex **pv1, grs_vertex **pv0, int lit)
{
    grs_vertex *v0 = *pv0, *v1 = *pv1;
    fix q;
    ti->right.y = fix_cint32(v1->y);
    TI_FRAC_R(ti) = (ushort)v0->x;
    ti->n = (int)v0;
    q = edge_slope(v0, v1);
    s->rfrac16 = (ulong)q << 16;
    s->rint = (q >> 16) + (ushort)s->drow;
    s->rp = edge_start(s, v0, q, &TI_FRAC_R(ti)) - lit;
    ti->right.u = edge_param(v0, v1, 8, &s->dudy_r);
    ti->right.v = edge_param(v0, v1, 12, &s->dvdy_r);
    if (lit)
        ti->right.i = edge_param(v0, v1, 20, &s->didy_r);
}

static void bl_left_edge(bilin_state *s, grs_tmap_loop_info *ti, grs_vertex **pv1, grs_vertex **pv0, int lit)
{
    grs_vertex *v0 = *pv0, *v1 = *pv1;
    fix q;
    ti->left.y = fix_cint32(v1->y);
    TI_FRAC_L(ti) = (ushort)v0->x;
    ti->n = (int)v0;
    q = edge_slope(v0, v1);
    s->lfrac = (ushort)q;
    ti->left.di = q >> 16;
    s->lint = (q >> 16) + (ushort)s->drow;
    ti->left.x = (fix)(edge_start(s, v0, q, &TI_FRAC_L(ti)) - lit);
    ti->left.u = edge_param(v0, v1, 8, &s->dudy_l);
    ti->left.v = edge_param(v0, v1, 12, &s->dvdy_l);
    if (lit)
        ti->left.i = edge_param(v0, v1, 20, &s->didy_l);
}

#define EDGES(name, s, lit)                                                                        \
grs_tmap_loop_info *gri_##name##_right_edge(grs_tmap_loop_info *ti, grs_vertex **pv1, grs_vertex **pv0) \
{                                                                                                  \
    bl_right_edge(&s, ti, pv1, pv0, lit);                                                          \
    return ti;                                                                                     \
}                                                                                                  \
grs_tmap_loop_info *gri_##name##_left_edge(grs_tmap_loop_info *ti, grs_vertex **pv1, grs_vertex **pv0) \
{                                                                                                  \
    bl_left_edge(&s, ti, pv1, pv0, lit);                                                           \
    return ti;                                                                                     \
}

EDGES(bilin, bl_s, 0)
EDGES(trans_bilin, tbl_s, 0)
EDGES(lit_bilin, lbl_s, 1)
EDGES(trans_lit_bilin, tlbl_s, 1)
EDGES(clut_bilin, cbl_s, 0)
EDGES(vbilin, vbl_s, 0)
EDGES(tluc8_trans_bilin, ttbl_s, 0)

/* next scanline's end pointers; returns rp - lp */
static int bl_advance(bilin_state *s, grs_tmap_loop_info *ti)
{
    ulong f = TI_FRAC(ti), g, lo;
    uchar *l;
    g = f + s->rfrac16;
    s->rp += s->rint + (g < f);
    lo = (g & 0xffff) + s->lfrac;
    TI_FRAC(ti) = (g & 0xffff0000) | (lo & 0xffff);
    l = (uchar *)ti->left.x + s->lint + (lo >> 16);
    ti->left.x = (fix)l;
    return (fix)s->rp - (fix)l;
}

static void bl_add_uv(bilin_state *s, grs_tmap_loop_info *ti)
{
    ti->left.v += s->dvdy_l;
    ti->right.v += s->dvdy_r;
    ti->left.u += s->dudy_l;
    ti->right.u += s->dudy_r;
}

/* empty scanline of bilin / clut bilin: right.v gets the new left.v */
static void bl_add_uv_bug(bilin_state *s, grs_tmap_loop_info *ti)
{
    ti->left.u += s->dudy_l;
    ti->right.u += s->dudy_r;
    ti->left.v += s->dvdy_l;
    ti->right.v = ti->left.v;
}

/* u, v per pixel: fractions (32 bit) and the texel index steps */
static void bl_steps(bilin_state *s, grs_tmap_loop_info *ti, int cnt, ulong *du, ulong *dv)
{
    long r = grd_recip_tab[cnt];
    long long p = (long long)(ti->right.u - ti->left.u) * r;
    long long q = (long long)(ti->right.v - ti->left.v) * r;
    *du = (ulong)p;
    *dv = (ulong)q;
    s->step = ((ulong)(q >> 32) << (s->wlog & 31)) + (ulong)(p >> 32);
    s->step2 = s->step + s->w;
}

static ulong bl_index(bilin_state *s, grs_tmap_loop_info *ti)
{
    return ((ulong)(ti->left.v >> (s->wshift & 31)) & s->vmask) + (ulong)(ti->left.u >> 16);
}

static void bl_span(bilin_state *s, grs_tmap_loop_info *ti, int cnt, ulong du, ulong dv)
{
    ulong idx = bl_index(s, ti);
    ulong vf = (ulong)ti->left.v << 16, uf = (ulong)ti->left.u << 16;
    int c, cu;
    uchar *d = s->rp;
    CARRY_ADD(vf, dv, c);
    for (cnt = -cnt; cnt != 0; cnt++) {
        uchar px;
        idx &= s->mask;
        CARRY_ADD(uf, du, cu);
        px = s->bits[idx];
        idx += (c ? s->step2 : s->step) + cu;
        CARRY_ADD(vf, dv, c);
        d[cnt] = s->clut ? s->clut[px] : px;
    }
}

/* ---------------- bilin / clut bilin ---------------- */
static int bl_loop(bilin_state *s, grs_tmap_loop_info *ti, int limit)
{
    int cnt = (fix)s->rp - ti->left.x;
    ulong du, dv;

    if (limit ? (ulong)cnt >= 0x400 && cnt != 0 : cnt < 0)
        return TRUE;
    if (cnt != 0) {
        bl_steps(s, ti, cnt, &du, &dv);
        goto draw;
    }
    for (;;) {
        cnt = bl_advance(s, ti);
        if (cnt == 0) {
            bl_add_uv_bug(s, ti);
            if (--ti->n == 0)
                return FALSE;
            continue;
        }
        if (limit ? (ulong)cnt >= 0x400 : cnt < 0)
            return TRUE;
        bl_add_uv(s, ti);
        bl_steps(s, ti, cnt, &du, &dv);
        if (--ti->n == 0)
            return FALSE;
    draw:
        bl_span(s, ti, cnt, du, dv);
    }
}

int gri_bilin_umap_loop(grs_tmap_loop_info *ti)
{
    return bl_loop(&bl_s, ti, 0);
}

int gri_clut_bilin_umap_loop(grs_tmap_loop_info *ti)
{
    return bl_loop(&cbl_s, ti, 1);
}

static void bl_common(bilin_state *s, grs_tmap_loop_info *ti, ulong mask)
{
    int wlog = ti->wlog;
    s->bits = ti->bm.bits;
    s->wlog = wlog;
    s->w = 1u << (wlog & 31);
    s->vmask = ~(s->w - 1);
    s->wshift = (uchar)(16 - wlog);
    s->mask = mask;
    s->drow = grd_bm.row;
    s->dbits = grd_bm.bits;
}

grs_tmap_loop_info *gri_bilin_umap_init(grs_tmap_loop_info *ti)
{
    ti->left_edge_func = WATCOM_FN(gri_bilin_left_edge);
    ti->right_edge_func = WATCOM_FN(gri_bilin_right_edge);
    ti->loop_func = WATCOM_FN(gri_bilin_umap_loop);
    bl_common(&bl_s, ti, ((ulong)(ushort)ti->bm.h << (ti->wlog & 31)) - 1);
    bl_s.clut = NULL;
    return ti;
}

grs_tmap_loop_info *gri_clut_bilin_umap_init(grs_tmap_loop_info *ti)
{
    ti->left_edge_func = WATCOM_FN(gri_clut_bilin_left_edge);
    ti->right_edge_func = WATCOM_FN(gri_clut_bilin_right_edge);
    ti->loop_func = WATCOM_FN(gri_clut_bilin_umap_loop);
    bl_common(&cbl_s, ti, ((ulong)(ushort)ti->bm.h << (ti->wlog & 31)) - 1);
    cbl_s.clut = ti->clut;
    return ti;
}

/* ---------------- trans bilin (and solid), tluc8 trans bilin ---------------- */
static int tr_loop(bilin_state *s, grs_tmap_loop_info *ti, int tl)
{
    for (;;) {
        int cnt = (fix)s->rp - ti->left.x;
        if (cnt != 0) {
            ulong du, dv, idx, vf, uf;
            int c, cu;
            if ((ulong)cnt >= 0x400)
                return TRUE;
            bl_steps(s, ti, cnt, &du, &dv);
            idx = bl_index(s, ti);
            vf = (ulong)ti->left.v << 16;
            uf = (ulong)ti->left.u << 16;
            if (!tl) {
                du &= ~0xffu;
                uf |= s->color;
            }
            CARRY_ADD(vf, dv, c);
            for (cnt = -cnt; cnt != 0; cnt++) {
                uchar px;
                idx &= s->mask;
                CARRY_ADD(uf, du, cu);
                px = s->bits[idx];
                idx += (c ? s->step2 : s->step) + cu;
                CARRY_ADD(vf, dv, c);
                if (px == 0)
                    continue;
                if (tl) {
                    uchar *tab = ((uchar **)s->clut)[px];     /* tluc8tab_p at init */
                    s->rp[cnt] = tab ? tab[s->rp[cnt]] : px;
                } else
                    s->rp[cnt] = s->mode == BL_SOLID ? (uchar)uf : px;
            }
        }
        bl_advance(s, ti);
        bl_add_uv(s, ti);
        if (--ti->n == 0)
            return FALSE;
    }
}

int gri_trans_bilin_umap_loop(grs_tmap_loop_info *ti)
{
    return tr_loop(&tbl_s, ti, 0);
}

/* BMT_TLUC8, GRC_TRANS_BILIN (fill norm/xor/blend): texel t drawn as tluc8tab[t][dst] */
int gri_tluc8_trans_bilin_umap_loop(grs_tmap_loop_info *ti)
{
    return tr_loop(&ttbl_s, ti, 1);
}

grs_tmap_loop_info *gri_tluc8_trans_bilin_umap_init(grs_tmap_loop_info *ti)
{
    ti->left_edge_func = WATCOM_FN(gri_tluc8_trans_bilin_left_edge);
    ti->right_edge_func = WATCOM_FN(gri_tluc8_trans_bilin_right_edge);
    ti->loop_func = WATCOM_FN(gri_tluc8_trans_bilin_umap_loop);
    bl_common(&ttbl_s, ti, (1u << ((uchar)(ti->wlog + ti->loop) & 31)) - 1);
    ttbl_s.clut = (uchar *)tluc8tab_p;
    return ti;
}

static grs_tmap_loop_info *vbilin_init(grs_tmap_loop_info *ti, int mode);

grs_tmap_loop_info *gri_trans_bilin_umap_init(grs_tmap_loop_info *ti)
{
    tbl_s.mode = BL_TEXEL;
    ti->left_edge_func = WATCOM_FN(gri_trans_bilin_left_edge);
    ti->right_edge_func = WATCOM_FN(gri_trans_bilin_right_edge);
    ti->loop_func = WATCOM_FN(gri_trans_bilin_umap_loop);
    bl_common(&tbl_s, ti, (1u << ((uchar)(ti->wlog + ti->loop) & 31)) - 1);
    return ti;
}

static ushort pow2_16(int log)
{
    log &= 31;
    return log < 16 ? (ushort)(1 << log) : 0;
}

/* FILL_SOLID: transparent texels skipped, the others drawn in the fill color */
grs_tmap_loop_info *gri_solid_bilin_umap_init(grs_tmap_loop_info *ti)
{
    if (pow2_16(ti->wlog) != (ushort)ti->bm.row || pow2_16(ti->loop) != (ushort)ti->bm.h)
        return vbilin_init(ti, BL_SOLID);
    tbl_s.color = (uchar)(long)ti->clut;
    gri_trans_bilin_umap_init(ti);
    tbl_s.mode = BL_SOLID;
    return ti;
}

/* ---------------- lit bilin / trans lit bilin ---------------- */

/* intensity setup at the start of a span: eax (fraction register), dh/cl of the asm */
typedef struct { ulong ilo, eax, ecx_lo, dh, cl; } lit_regs;

static void lit_span_regs(grs_tmap_loop_info *ti, int cnt, lit_regs *L)
{
    long r = grd_recip_tab[cnt];
    long long p = (long long)(ti->right.i - ti->left.i) * r;
    ulong rot = ((ulong)ti->left.i >> 16) | ((ulong)ti->left.i << 16), t;
    int b;
    L->ilo = (ulong)p;
    rot = (rot & 0xffff00ff) | (((ulong)(p >> 32) & 0xff) << 8);
    L->dh = rot & 0xff;            /* i integer byte */
    L->cl = (rot >> 8) & 0xff;     /* di integer byte */
    t = rot & 0xffff0000;
    b = t < L->ilo;
    L->eax = t - L->ilo;           /* one step back */
    L->dh = (L->dh - L->cl - b) & 0xff;
}

static int lit_bl_loop(bilin_state *s, grs_tmap_loop_info *ti, int trans)
{
    int cnt = (fix)s->rp - ti->left.x;
    ulong du, dv;
    lit_regs L;

    if (cnt == 0)
        goto next;
    if ((ulong)cnt >= 0x400)
        return TRUE;
    for (;;) {
        lit_span_regs(ti, cnt, &L);
        bl_steps(s, ti, cnt, &du, &dv);
        dv &= ~0xffu;
        {
            ulong idx = bl_index(s, ti);
            ulong vf = ((ulong)ti->left.v << 16) | L.cl;
            ulong uf = (ulong)ti->left.u << 16;
            ulong eax = L.eax, edx = L.dh << 8;
            int c, cu, ci, pc = 0;
            CARRY_ADD(vf, dv, c);
            for (cnt = -cnt; cnt != 0;) {
                uchar px;
                if (trans && pc && c)          /* skipped texel, then a v carry */
                    eax &= 0xffffff00;
                idx &= s->mask;
                CARRY_ADD(uf, du, cu);
                px = s->bits[idx];
                edx = (edx & 0xff00) | px;
                idx += (c ? s->step2 : s->step) + cu;
                pc = !c;
                CARRY_ADD(eax, L.ilo, ci);
                edx = (edx & 0xff) | ((((edx >> 8) + L.cl + ci) & 0xff) << 8);
                CARRY_ADD(vf, dv, c);
                cnt++;
                if (trans && px == 0)
                    continue;
                pc = 0;
                px = s->ltab[edx];
                vf = (vf & 0xffff00ff) | ((ulong)px << 8);
                s->rp[cnt] = px;
            }
        }
        if (trans) {
            bl_advance(s, ti);
            ti->left.i += s->didy_l;
            ti->right.i += s->didy_r;
            bl_add_uv(s, ti);
            if (--ti->n == 0)
                return FALSE;
            cnt = (fix)s->rp - ti->left.x;
            if (cnt == 0)
                goto next;
            if ((ulong)cnt >= 0x400)
                return TRUE;
            continue;
        }
    next:
        for (;;) {
            cnt = bl_advance(s, ti);
            if (trans) {
                ti->left.i += s->didy_l;
                ti->right.i += s->didy_r;
                bl_add_uv(s, ti);
                if (--ti->n == 0)
                    return FALSE;
                cnt = (fix)s->rp - ti->left.x;
                if (cnt == 0)
                    continue;
                if ((ulong)cnt >= 0x400)
                    return TRUE;
                break;
            }
            if (cnt == 0) {
                bl_add_uv_bug(s, ti);
                ti->left.i += s->didy_l;
                ti->right.i += s->didy_r;
                if (--ti->n == 0)
                    return FALSE;
                continue;
            }
            if ((ulong)cnt >= 0x400)
                return TRUE;
            ti->left.i += s->didy_l;
            ti->right.i += s->didy_r;
            ti->left.u += s->dudy_l;
            ti->right.u += s->dudy_r;
            ti->left.v += s->dvdy_l;
            ti->right.v += s->dvdy_r;
            if (--ti->n == 0)
                return FALSE;
            break;
        }
    }
}

int gri_lit_bilin_umap_loop(grs_tmap_loop_info *ti)
{
    return lit_bl_loop(&lbl_s, ti, 0);
}

int gri_trans_lit_bilin_umap_loop(grs_tmap_loop_info *ti)
{
    return lit_bl_loop(&tlbl_s, ti, 1);
}

grs_tmap_loop_info *gri_lit_bilin_umap_init(grs_tmap_loop_info *ti)
{
    ti->left_edge_func = WATCOM_FN(gri_lit_bilin_left_edge);
    ti->right_edge_func = WATCOM_FN(gri_lit_bilin_right_edge);
    ti->loop_func = WATCOM_FN(gri_lit_bilin_umap_loop);
    lbl_s.ltab = grd_screen->ltab;
    bl_common(&lbl_s, ti, ((ulong)(ushort)ti->bm.h << (ti->wlog & 31)) - 1);
    return ti;
}

grs_tmap_loop_info *gri_trans_lit_bilin_umap_init(grs_tmap_loop_info *ti)
{
    ti->left_edge_func = WATCOM_FN(gri_trans_lit_bilin_left_edge);
    ti->right_edge_func = WATCOM_FN(gri_trans_lit_bilin_right_edge);
    ti->loop_func = WATCOM_FN(gri_trans_lit_bilin_umap_loop);
    tlbl_s.ltab = grd_screen->ltab;
    bl_common(&tlbl_s, ti, ((ulong)(ushort)ti->bm.h << (ti->wlog & 31)) - 1);
    return ti;
}

/* ---------------- vbilin (row table, no wrapping) ---------------- */
int gri_vbilin_umap_loop(grs_tmap_loop_info *ti)
{
    bilin_state *s = &vbl_s;
    for (;;) {
        int cnt = (fix)s->rp - ti->left.x;
        if (cnt != 0) {
            long r;
            long long p, q;
            ulong du, dv, idx, vf, uf;
            int c, cu;
            if ((ulong)cnt >= 0x400)
                return TRUE;
            r = grd_recip_tab[cnt];
            q = (long long)(ti->right.v - ti->left.v) * r;
            p = (long long)(ti->right.u - ti->left.u) * r;
            dv = (ulong)q;
            du = (ulong)p & ~0xffu;
            s->step = (ulong)(p >> 32) + (ulong)(q >> 32) * (ushort)s->w;
            s->step2 = s->step + (ushort)s->w;
            idx = (ulong)s->vtab[(ulong)ti->left.v >> 16] + ((ulong)ti->left.u >> 16);
            vf = (ulong)ti->left.v << 16;
            uf = ((ulong)ti->left.u << 16) | s->color;
            CARRY_ADD(vf, dv, c);
            for (cnt = -cnt; cnt != 0; cnt++) {
                uchar px;
                CARRY_ADD(uf, du, cu);
                px = s->bits[(long)idx];
                idx += (c ? s->step2 : s->step) + cu;
                if (s->mode == BL_CLUT)
                    idx &= 0xc0ffffff;
                CARRY_ADD(vf, dv, c);
                if (px == 0 && s->mode != BL_CLUT)
                    continue;
                s->rp[cnt] = s->mode == BL_SOLID ? s->color : s->clut[px];
            }
        }
        bl_advance(s, ti);
        bl_add_uv(s, ti);
        if (--ti->n == 0)
            return FALSE;
    }
}

static grs_tmap_loop_info *vbilin_init(grs_tmap_loop_info *ti, int mode)
{
    bilin_state *s = &vbl_s;
    s->mode = mode;
    if (mode == BL_SOLID)
        s->color = (uchar)(long)ti->clut;
    else
        s->clut = ti->clut;
    ti->vtab = gri_init_vtab(&ti->bm);
    ti->left_edge_func = WATCOM_FN(gri_vbilin_left_edge);
    ti->right_edge_func = WATCOM_FN(gri_vbilin_right_edge);
    ti->loop_func = WATCOM_FN(gri_vbilin_umap_loop);
    s->bits = ti->bm.bits;
    s->w = (ushort)ti->bm.row;           /* row, kept in w */
    s->vtab = ti->vtab;
    s->drow = grd_bm.row;
    s->dbits = grd_bm.bits;
    return ti;
}

/* 0x3315B0: unreferenced entry of the FILL_SOLID setup (gri_solid_bilin_umap_init jumps
 * past its pushad for textures that are not powers of two) */
grs_tmap_loop_info *gri_solid_vbilin_umap_init(grs_tmap_loop_info *ti)
{
    return vbilin_init(ti, BL_SOLID);
}

/* unreferenced: opaque variant; also masks the texel index with 0xC0FFFFFF */
grs_tmap_loop_info *gri_clut_vbilin_umap_init(grs_tmap_loop_info *ti)
{
    return vbilin_init(ti, BL_CLUT);
}

grs_tmap_loop_info *gri_trans_clut_vbilin_umap_init(grs_tmap_loop_info *ti)
{
    return vbilin_init(ti, BL_TRANS_CLUT);
}
