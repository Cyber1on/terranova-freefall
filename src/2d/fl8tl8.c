/* fl8tl8.c -- Terra Nova v1.09 flat8 canvas: BMT_TLUC8 (translucent) bitmaps, scaler and
 * linear mapper (function table rows of bitmap type 5).  Decompiled from __FF.EXE (Watcom C).
 *
 *   0x332640 gri_tluc8_ubitmap                     (GRC_BITMAP)
 *   0x3327A8 gri_tluc8_scale_umap_loop
 *   0x332A54 gri_tluc8_trans_scale_umap_init       0x332A94 gri_tluc8_opaque_scale_umap_init
 *   0x332AD4 gri_tluc8_trans_clut_scale_umap_init  0x332B14 gri_tluc8_opaque_clut_scale_umap_init
 *   0x332B80 gri_tluc8_lin_umap_loop
 *   0x333094 gri_tluc8_lin_umap_init               (GRC_BILIN, fill norm/xor/blend)
 *   0x333124 gri_tluc8_trans_clut_lin_umap_init    (GRC_TRANS_CLUT_BILIN; fill clut: all trans)
 *   0x3331B4 gri_tluc8_clut_lin_umap_init          (GRC_CLUT_BILIN; fill clut: all opaque)
 *
 * A tluc8 texel t is drawn as tluc8tab[t] ? tluc8tab[t][dst] : t (tluc8tab_p -> uchar *[256]);
 * the clut variants then look the result up in ti->clut.  The loop type is kept in ti->loop
 * (bm.hlog): GRL_TRANS 1, GRL_LOG2 2 (power-of-two texture, masked index), GRL_CLUT 4.
 *
 * Quirks kept:
 *  - gri_tluc8_ubitmap returns eax = the bitmap pointer's high word (low word 0).
 *  - the lin inits return the destination row pointer (ti->d), the scale inits return ti.
 *  - the lin loop in mode GRL_TRANS|GRL_CLUT|GRL_LOG2 (7) steps u and v twice per pixel.
 *  - the lin loop's GRL_TRANS (1, 3) cases are unreachable: no init sets them (GRC_TRANS_BILIN
 *    uses the asm mapper in fl8bilin.c).
 */
#include "2d_tmap.h"

void gri_uvx_edge(void);                    /* 0x34B890 (2d-core; Shock's name) */
long *gri_init_vtab(grs_bitmap *bm);        /* 0x34C640 */

#define fix_cint32(a) ((fix)((a) + 0xffff) >> 16)

static uchar tluc(uchar t, uchar *d)
{
    uchar *tab = tluc8tab_p[t];
    return tab ? tab[*d] : t;
}

/* ---------------- unclipped bitmap ---------------- */
long gri_tluc8_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *d = grd_bm.bits + (long)y * (ushort)grd_bm.row + x;
    uchar *s = bm->bits;
    short w = bm->w, h = bm->h, i;
    int trans = bm->flags & BMF_TRANS;

    while (h-- != 0) {
        for (i = 0; i < w; i++)
            if (!trans || s[i] != 0)
                d[i] = tluc(s[i], d + i);
        s += (ushort)bm->row;
        d += (ushort)grd_bm.row;
    }
    return (long)bm & 0xffff0000;
}

/* ---------------- scaler ---------------- */
int gri_tluc8_scale_umap_loop(grs_tmap_loop_info *ti)
{
    uchar *p0 = ti->d + (short)fix_cint32(ti->left.x);
    uchar *p1 = ti->d + (short)fix_cint32(ti->right.x);
    fix dudx, u0;

    if (p1 <= p0)
        return 1;
    dudx = tm_fix_div(ti->right.u - ti->left.u, ti->right.x - ti->left.x);
    u0 = ti->left.u + tm_fix_mul(dudx, ((ti->left.x + 0xffff) & 0xffff0000) - ti->left.x);
    do {
        uchar *s = (ti->left.v >> 16) * (ushort)ti->bm_row + ti->bm.bits;
        uchar *p;
        fix u = u0;
        int mode = ti->loop;
        if (mode <= 5 && mode != 2 && mode != 3 && p0 < p1) {
            for (p = p0; p < p1; p++, u += dudx) {
                uchar t = s[u >> 16];
                if ((mode & GRL_TRANS) && t == 0)
                    continue;
                t = tluc(t, p);
                *p = (mode & GRL_CLUT) ? ti->clut[t] : t;
            }
        }
        ti->left.v += ti->left.dv;
        p0 += (ushort)grd_bm.row;
        p1 += (ushort)grd_bm.row;
    } while (--ti->n > 0);
    return 0;
}

static grs_tmap_loop_info *scale_init(grs_tmap_loop_info *ti, int mode)
{
    ti->loop = mode;
    ti->loop_func = WATCOM_FN(gri_tluc8_scale_umap_loop);
    ti->right_edge_func = WATCOM_FN(gr_null);
    ti->left_edge_func = WATCOM_FN(gri_scale_edge);
    ti->d = grd_bm.bits + (ulong)(ushort)grd_bm.row * ti->y;
    return ti;
}

grs_tmap_loop_info *gri_tluc8_trans_scale_umap_init(grs_tmap_loop_info *ti)
{
    return scale_init(ti, GRL_TRANS);
}

grs_tmap_loop_info *gri_tluc8_opaque_scale_umap_init(grs_tmap_loop_info *ti)
{
    return scale_init(ti, GRL_OPAQUE);
}

grs_tmap_loop_info *gri_tluc8_trans_clut_scale_umap_init(grs_tmap_loop_info *ti)
{
    return scale_init(ti, GRL_TRANS | GRL_CLUT);
}

grs_tmap_loop_info *gri_tluc8_opaque_clut_scale_umap_init(grs_tmap_loop_info *ti)
{
    return scale_init(ti, GRL_CLUT);
}

/* ---------------- linear mapper (u, v linear along the edges and the scanlines) ---------------- */
int gri_tluc8_lin_umap_loop(grs_tmap_loop_info *ti)
{
    do {
        fix xl = ti->left.x + 0xffff, xr = ti->right.x + 0xffff;
        fix span = (xr & 0xffff0000) - (xl & 0xffff0000);
        if (span > 0) {
            fix dx = ti->right.x - ti->left.x;
            uchar *p = ti->d + (short)(xl >> 16);
            uchar *end = ti->d + (short)(xr >> 16);
            fix pre = (xl & 0xffff0000) - ti->left.x;
            fix dudx = tm_fix_div(ti->right.u - ti->left.u, dx);
            fix dvdx = tm_fix_div(ti->right.v - ti->left.v, dx);
            fix u = ti->left.u + tm_fix_mul(dudx, pre);
            fix v = ti->left.v + tm_fix_mul(dvdx, pre);
            int mode = ti->loop;
            if (mode <= 7) {
                for (; p < end; p++, u += dudx, v += dvdx) {
                    uchar t;
                    if (mode & GRL_LOG2)
                        t = ti->bm.bits[((u >> 16) + ((v >> 16) << (ti->wlog & 31))) & ti->v_mask];
                    else
                        t = ti->bm.bits[ti->vtab[v >> 16] + (u >> 16)];
                    if (mode == (GRL_TRANS | GRL_CLUT | GRL_LOG2)) {
                        u += dudx;      /* steps twice per pixel */
                        v += dvdx;
                    }
                    if ((mode & GRL_TRANS) && t == 0)
                        continue;
                    t = tluc(t, p);
                    *p = (mode & GRL_CLUT) ? ti->clut[t] : t;
                }
            }
        } else if (span < 0)
            return 1;
        ti->left.u += ti->left.du;
        ti->right.u += ti->right.du;
        ti->left.v += ti->left.dv;
        ti->right.v += ti->right.dv;
        ti->left.x += ti->left.dx;
        ti->right.x += ti->right.dx;
        ti->d += (ushort)grd_bm.row;
    } while (--ti->n > 0);
    return 0;
}

static uchar *lin_init(grs_tmap_loop_info *ti, int mode)
{
    int wlog = ti->wlog, hlog = ti->loop;
    if ((ulong)(ushort)ti->bm_row == 1u << (wlog & 31) && ti->bm_h == 1 << (hlog & 31)) {
        ti->loop = mode | GRL_LOG2;
        ti->v_mask = (1u << ((wlog + hlog) & 31)) - 1;
    } else {
        long *vt = gri_init_vtab(&ti->bm);
        ti->loop = mode;
        ti->vtab = vt;
    }
    ti->loop_func = WATCOM_FN(gri_tluc8_lin_umap_loop);
    ti->right_edge_func = WATCOM_FN(gri_uvx_edge);
    ti->left_edge_func = WATCOM_FN(gri_uvx_edge);
    ti->d = grd_bm.bits + (ulong)(ushort)grd_bm.row * ti->y;
    return ti->d;
}

uchar *gri_tluc8_lin_umap_init(grs_tmap_loop_info *ti)
{
    return lin_init(ti, GRL_OPAQUE);
}

uchar *gri_tluc8_trans_clut_lin_umap_init(grs_tmap_loop_info *ti)
{
    return lin_init(ti, GRL_TRANS | GRL_CLUT);
}

uchar *gri_tluc8_clut_lin_umap_init(grs_tmap_loop_info *ti)
{
    return lin_init(ti, GRL_CLUT);
}
