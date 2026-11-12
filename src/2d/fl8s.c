/* fl8s.c -- x:\prj\tech\libsrc\2d\fl8s.c (Terra Nova v1.09): flat8 bitmap scalers (function
 * table GRC_xxx_SCALE).  The loop is hand-written self-modifying asm in the original
 * (unrolled four pixels per block, entered in the middle for the remainder); this is
 * equivalent C.  Decompiled from __FF.EXE.
 *
 *   0x32E7C0 gri_opaque_solid_scale_umap_init   0x32E804 gri_trans_solid_scale_umap_init
 *   0x32E826 gri_trans_scale_umap_init          0x32E83E gri_opaque_scale_umap_init
 *   0x32E856 gri_opaque_clut_scale_umap_init    0x32E872 gri_trans_clut_scale_umap_init
 *   0x32E8CD gri_scale_umap_loop (loop function; sets up and jumps into 0x32E994)
 *   inner entry points (no thunks): 0x32E994 row loop, 0x32E9AA remainder, 0x32E9B6 next row,
 *   unrolled pixel blocks 0x32EA00 (opaque clut), 0x32EA60/0x32EAC0 (trans clut),
 *   0x32EB20 (opaque), 0x32EB80/0x32EBE0 (trans solid), 0x32EC40/0x32ECA0 (trans).
 *
 * The init functions select the pixel block by patching two jump displacements (main entry and
 * remainder entry) and, for the solid variant, turn "test dl,0FFh" into "mov dl,colour".  Each
 * row starts at the same u; v advances by left.dv with the fraction accumulated in right.v.
 * Quirk kept: the destination row step (an immediate preset by the init to row+4) is
 * decremented by the span length on every call of the loop function, so only the first call
 * after an init steps correctly.  (The remainder jump is decremented the same way; with the
 * single call per init that the 2D library makes it always matches the pixel count.)
 */
#include "2d_tmap.h"

typedef ulong u32;
typedef unsigned long long u64;

enum { SC_OPAQUE, SC_OPAQUE_CLUT, SC_TRANS, SC_TRANS_CLUT, SC_TRANS_SOLID };

static int sc_kind;                /* patched jump displacements at 0x32E9A3 / 0x32E9B2 */
static u32 sc_dstep;               /* 0x32E9B8 destination step to the next row */

int gri_scale_umap_loop(grs_tmap_loop_info *ti);

static void scale_init(grs_tmap_loop_info *ti, int kind)
{
    sc_kind = kind;
    ti->left_edge_func = WATCOM_FN(gri_scale_edge);
    ti->right_edge_func = WATCOM_FN(gr_null);
    ti->loop_func = WATCOM_FN(gri_scale_umap_loop);
    sc_dstep = (ushort)grd_bm.row + 4;
    ti->d = (uchar *)((u32)(ushort)grd_bm.row * (u32)ti->y + (u32)grd_bm.bits);
}

void gri_opaque_solid_scale_umap_init(grs_tmap_loop_info *ti)
{
    ti->left_edge_func = WATCOM_FN(gri_scale_edge);
    ti->right_edge_func = WATCOM_FN(gr_null);
    ti->loop = GRL_OPAQUE;
    ti->bm.bits = ti->clut;        /* fill colour */
    ti->loop_func = WATCOM_FN(gri_poly_loop);
    ti->d = (uchar *)((u32)(ushort)grd_bm.row * (u32)ti->y + (u32)grd_bm.bits);
}

void gri_trans_solid_scale_umap_init(grs_tmap_loop_info *ti) { scale_init(ti, SC_TRANS_SOLID); }
void gri_trans_scale_umap_init(grs_tmap_loop_info *ti) { scale_init(ti, SC_TRANS); }
void gri_opaque_scale_umap_init(grs_tmap_loop_info *ti) { scale_init(ti, SC_OPAQUE); }
void gri_opaque_clut_scale_umap_init(grs_tmap_loop_info *ti) { scale_init(ti, SC_OPAQUE_CLUT); }
void gri_trans_clut_scale_umap_init(grs_tmap_loop_info *ti) { scale_init(ti, SC_TRANS_CLUT); }

int gri_scale_umap_loop(grs_tmap_loop_info *ti)
{
    u32 row = (ushort)ti->bm_row;
    u32 vfstep = (u32)ti->left.dv << 16;
    u32 vistep = (u32)((ti->left.dv >> 16) * (long)row);
    u32 lx = (u32)ti->left.x, cl = (lx + 0xffff) & 0xffff0000;
    u32 count = (((u32)ti->right.x + 0xffff) >> 16) - (cl >> 16);
    fix dx = ti->right.x - ti->left.x, du, dudx, u;
    u32 ustart, fstep, x0;
    long istep;
    uchar *dst, *clut = ti->clut;
    uchar colour = (uchar)(u32)ti->clut;

    ti->bm.bits += ((u32)ti->left.v >> 16) * row;
    ti->right.v = (u32)ti->left.v << 16;       /* v fraction accumulator */
    x0 = cl >> 16;
    ti->d += x0;
    sc_dstep -= count;
    du = ti->right.u - ti->left.u;
    if (du > 0)
        du++;
    else if (du < 0)
        du--;
    dudx = (fix)(((long long)du << 16) / dx);
    u = ti->left.u + (fix)(((long long)dudx * (long)(cl - lx)) >> 16);
    ustart = (count >> 2) + ((u32)u << 16);
    ti->bm.bits += (u32)u >> 16;
    fstep = (u32)dudx << 16;
    istep = dudx >> 16;
    dst = ti->d;
    do {
        uchar *src = ti->bm.bits;
        u32 acc = ustart, k, c;
        u64 t;
#ifndef TN_WATCOM_ABI
        /* the port, the same pixels faster (3D resolution draws a close sprite, an explosion
         * in front of the goggles, over millions of pixels): one loop per kind with a 32-bit
         * carry, and a magnified sprite in runs */
#define SC_ROW(put) \
        for (k = 0; k < count; k++) { \
            uchar texel = *src; \
            u32 n = acc + fstep; \
            put; \
            src += istep + (n < acc); \
            acc = n; \
        }
        if (istep == 0 && fstep != 0 && fstep <= 0x40000000u) {
            /* magnified (a close sprite): each texel covers a run of pixels, up to and with
             * the one whose step carries to the next texel */
            int opaque = sc_kind == SC_OPAQUE || sc_kind == SC_OPAQUE_CLUT;
            const uchar *tab = sc_kind == SC_OPAQUE_CLUT || sc_kind == SC_TRANS_CLUT ? clut : NULL;
            /* after the first run acc < fstep, so a run is rb or rb + 1 pixels */
            u32 rb = (u32)(0x100000000ull / fstep);
            for (k = 0; k < count; src++) {
                uchar texel = *src, v;
                u32 r = acc < fstep ? ((u64)acc + (u64)rb * fstep > 0xFFFFFFFFull ? rb : rb + 1)
                                    : (0xFFFFFFFFu - acc) / fstep + 1;
                if (r > count - k)
                    r = count - k;
                acc += r * fstep;
                if (texel || opaque) {
                    uchar *d = dst + k;
                    v = sc_kind == SC_TRANS_SOLID ? colour : tab ? tab[texel] : texel;
                    if (r < 16) {
                        u32 i;
                        for (i = 0; i < r; i++)
                            d[i] = v;
                    } else
                        memset(d, v, r);
                }
                k += r;
            }
        } else
        switch (sc_kind) {
        case SC_OPAQUE: SC_ROW(dst[k] = texel) break;
        case SC_OPAQUE_CLUT: SC_ROW(dst[k] = clut[texel]) break;
        case SC_TRANS: SC_ROW(if (texel) dst[k] = texel) break;
        case SC_TRANS_CLUT: SC_ROW(if (texel) dst[k] = clut[texel]) break;
        case SC_TRANS_SOLID: SC_ROW(if (texel) dst[k] = colour) break;
        }
#undef SC_ROW
        (void)c;
        if (0)
#endif
        for (k = 0; k < count; k++) {
            uchar texel = *src;
            t = (u64)acc + fstep; acc = (u32)t; c = (u32)(t >> 32);
            switch (sc_kind) {
            case SC_OPAQUE: dst[k] = texel; break;
            case SC_OPAQUE_CLUT: dst[k] = clut[texel]; break;
            case SC_TRANS: if (texel) dst[k] = texel; break;
            case SC_TRANS_CLUT: if (texel) dst[k] = clut[texel]; break;
            case SC_TRANS_SOLID: if (texel) dst[k] = colour; break;
            }
            src += istep + c;
        }
        dst += count - 4 + sc_dstep;
        t = (u64)(u32)ti->right.v + vfstep;
        ti->right.v = (fix)(u32)t;
        ti->bm.bits += ((t >> 32) ? row : 0) + vistep;
    } while (--ti->n != 0);
    return 0;
}
