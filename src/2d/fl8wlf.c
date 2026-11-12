/* fl8wlf.c -- Terra Nova's lit "water" floor mapper (Terra Nova v1.09; no System Shock
 * counterpart: Shock's FL8LF.C has the same interface).  Hand-written self-modifying asm in the
 * original; this is equivalent C.  Decompiled from __FF.EXE.
 *
 *   0x32D8E0 gri_water_right_edge     right edge function (asm, Watcom args eax, edx, ebx)
 *   0x32DB40 gri_water_left_edge      left edge function
 *   0x32DDA0 gri_water_lit_floor_umap_init   function table NORM/MONO/GRC_LIT_FLOOR
 *   0x32DE50 gri_water_lit_floor_umap_loop   loop function (TN_SFC_improved "WATER_SITE")
 *   inner entry points (no thunks): 0x32E120 pixel loop, 0x32E181 pixel loop with the
 *   row-carry step, 0x32E1A0 next scanline, 0x32E2D6 empty scanline, 0x32DF58 long-span setup
 *
 * Reached through gr_lit_floor_umap with a bitmap of type BMT_MONO.  The original keeps its
 * state in immediates patched into its own code (0xDEADBEEF / 0xBEEF placeholders); here they
 * are the static variables p_*.  Per pixel:
 *   texel  = bits[tex & mask]            tex = (v << wlog) + u, stepped with carries from the
 *                                        u fraction (edi) and the v fraction (high bits of ecx)
 *   light += light step (int part in cl, fraction in ebp; 8-bit light in dh, wraps)
 *   e = water_tab[texel]   if (e & 0xFF) == 0x10:  pixel = ltab[light << 8 | texel]
 *                          else pixel = water_clut_row[src ? src[x + e] : texel]
 * where src is the frame buffer at water_src_off from the destination (the reflection), used
 * only while that row is inside the canvas.  water_src_off moves up two rows per scanline and
 * water_clut_row advances one table per scanline.
 *
 * Quirks kept: the edge functions overwrite ti->n with the current vertex pointer; ti->bm.bits
 * holds the left (low word) and right (high word) x fractions; the left edge stores the integer
 * x step in ti->left.di; for edges whose u/v/i change by less than 1/16 the start and step are
 * u*w (+-1) and u*dw (+-1) instead of being interpolated.
 */
#include "2d_tmap.h"

extern long grd_recip_tab[];       /* 0x45F71C fix 1/n [1024]: [0] = 0x7FFFFFFF */
extern uchar **water_clut_row;     /* 0x44CE30 current row's colour table (advances per row) */
extern long *water_tab;            /* 0x44CE34 per-texel entry: 0x10 = lit, else source offset */
extern long water_src_off;         /* 0x44CE38 offset from destination to reflection source */

typedef unsigned long long u64;
typedef long long s64;
typedef ulong u32;

/* patched immediates */
static uchar *p_bits;              /* 0x32E133 texture bits */
static u32 p_row;                  /* 0x32E023/0x32E0C5 texture row */
static uchar p_wlog;               /* shift counts */
static u32 p_mask;                 /* 0x32E127 (h << wlog) - 1 */
static fix p_dw;                   /* 0x32E252 */
static uchar *p_ltab;              /* 0x32E172 */
static u32 p_crow;                 /* 0x32D936/0x32DB92 canvas row (16 bits) */
static u32 p_cbits;                /* 0x32D94E/0x32DBAD canvas bits */
static long *p_tab;                /* 0x32E148 */
static u32 p_step, p_step2;        /* 0x32E121, 0x32E182 texture offset steps (no carry / v carry) */
static u32 p_ufrac;                /* 0x32E12D */
static u32 p_ifrac;                /* 0x32E13D */
static u32 p_vstep;                /* 0x32E15E/0x32E16C */
static int p_mode_src;             /* 0x32E150: source read (mov al,[ebx+eax+src]) or mov al,dl */
static u32 p_src;                  /* 0x32E153 */
static uchar *p_clut;              /* 0x32E164 */
static u32 p_dst;                  /* 0x32E178 right end of the current span */
static u32 p_rfrac, p_rint;        /* 0x32E1AE, 0x32E1B4 right x step (fraction << 16, int + row) */
static u32 p_lfrac, p_lint;        /* 0x32E21B (16 bits), 0x32E21F */
static fix p_ldu, p_ldv, p_ldi;    /* 0x32E295, 0x32E2B7, 0x32E271 */
static fix p_rdu, p_rdv, p_rdi;    /* 0x32E2A0, 0x32E2C2, 0x32E27C */

int gri_water_lit_floor_umap_loop(grs_tmap_loop_info *ti);
void gri_water_left_edge(grs_tmap_loop_info *ti, grs_vertex **pnext, grs_vertex **pcur);
void gri_water_right_edge(grs_tmap_loop_info *ti, grs_vertex **pnext, grs_vertex **pcur);

static inline fix fdiv(fix a, fix b) { return (fix)(((s64)a << 16) / b); }
static inline fix fmul(fix a, fix b) { return (fix)(((s64)a * b) >> 16); }

/* ---------------- edges ---------------- */

/* one interpolated parameter of an edge (u: off 8, v: 0xC, i: 0x14) */
static fix edge_param(grs_tmap_loop_info *ti, grs_vertex *cur, grs_vertex *next, int off, fix dy,
                      u32 yfrac, fix *step, int round)
{
    fix a = *(fix *)((uchar *)cur + off), b = *(fix *)((uchar *)next + off);
    if ((u32)(b - a + 0x1000) >= 0x2000) {
        fix e = fmul(a, cur->w);
        s64 p;
        fix r;
        *step = fdiv(fmul(b, next->w) - e, dy);
        p = (s64)*step * (long)yfrac;
        r = (fix)(p >> 16);
        if (round && p < 0)
            r++;
        return r + e;
    } else {
        int inc = ((ushort)a >= 0x8000) ? -1 : 1;     /* patched dec eax / inc eax */
        *step = fmul(a, ti->dw) + inc;
        return fmul(a, ti->w) + inc;
    }
}

/* common part: x slope with the -1 adjustment for negative dx, start x, y fraction */
static fix edge_slope(grs_tmap_loop_info *ti, grs_vertex *cur, grs_vertex *next, fix *dy)
{
    fix dx = next->x - cur->x, q;
    *dy = next->y - cur->y;
    ti->n = (int)cur;              /* quirk: the asm parks the vertex pointer in ti->n */
    q = fdiv(dx, *dy);
    if (q != 0x7fffffff && q != (fix)0x80000000)
        q += dx < 0 ? -1 : 0;
    return q;
}

void gri_water_right_edge(grs_tmap_loop_info *ti, grs_vertex **pnext, grs_vertex **pcur)
{
    grs_vertex *cur = *pcur, *next = *pnext;
    fix dy, q, x;
    u32 rowp, yfrac;

    ti->right.y = (next->y + 0xffff) >> 16;
    ((ushort *)&ti->bm.bits)[1] = (ushort)cur->x;
    q = edge_slope(ti, cur, next, &dy);
    p_rfrac = (u32)q << 16;
    p_rint = (q >> 16) + p_crow;
    rowp = (u32)((cur->y + 0xffff) >> 16) * p_crow + p_cbits;
    yfrac = (u32)(-cur->y) & 0xffff;
    x = cur->x + fmul(q, yfrac) + 0xffff;
    ((ushort *)&ti->bm.bits)[1] = (ushort)x;
    p_dst = (x >> 16) + rowp;
    ti->right.u = edge_param(ti, cur, next, 8, dy, yfrac, &p_rdu, 1);
    ti->right.v = edge_param(ti, cur, next, 0xc, dy, yfrac, &p_rdv, 1);
    ti->right.i = edge_param(ti, cur, next, 0x14, dy, yfrac, &p_rdi, 0);
}

void gri_water_left_edge(grs_tmap_loop_info *ti, grs_vertex **pnext, grs_vertex **pcur)
{
    grs_vertex *cur = *pcur, *next = *pnext;
    fix dy, q, x;
    u32 rowp, yfrac;

    ti->left.y = (next->y + 0xffff) >> 16;
    ((ushort *)&ti->bm.bits)[0] = (ushort)cur->x;
    q = edge_slope(ti, cur, next, &dy);
    p_lfrac = (ushort)q;
    ti->left.di = q >> 16;         /* quirk: integer x step parked in left.di */
    p_lint = (q >> 16) + p_crow;
    rowp = (u32)((cur->y + 0xffff) >> 16) * p_crow + p_cbits;
    yfrac = (u32)(-cur->y) & 0xffff;
    x = cur->x + fmul(q, yfrac) + 0xffff;
    ((ushort *)&ti->bm.bits)[0] = (ushort)x;
    ti->left.x = (x >> 16) + rowp;
    ti->left.u = edge_param(ti, cur, next, 8, dy, yfrac, &p_ldu, 1);
    ti->left.v = edge_param(ti, cur, next, 0xc, dy, yfrac, &p_ldv, 1);
    ti->left.i = edge_param(ti, cur, next, 0x14, dy, yfrac, &p_ldi, 0);
}

/* ---------------- init ---------------- */

void gri_water_lit_floor_umap_init(grs_tmap_loop_info *ti)
{
    ti->left_edge_func = WATCOM_FN(gri_water_left_edge);
    ti->right_edge_func = WATCOM_FN(gri_water_right_edge);
    ti->loop_func = WATCOM_FN(gri_water_lit_floor_umap_loop);
    p_bits = ti->bm.bits;
    p_row = (ushort)ti->bm_row;
    p_wlog = ti->wlog;
    p_mask = ((u32)(ushort)ti->bm_h << (p_wlog & 31)) - 1;
    p_dw = ti->dw;
    p_ltab = grd_screen->ltab;
    p_crow = (ushort)grd_bm.row;
    p_cbits = (u32)grd_bm.bits;
    p_tab = water_tab;
}

/* ---------------- loop ---------------- */

/* source row for the reflection: only while the row above is inside the canvas */
static void set_source(void)
{
    u32 e = water_src_off + p_dst - (u32)(short)grd_bm.row;
    if (e >= (u32)grd_bm.bits) {
        p_mode_src = 1;
        p_src = water_src_off + p_dst;
    } else
        p_mode_src = 0;
}

/* the pixel loop (0x32E120): cnt counts up to 0 from minus the span length */
static void span(long cnt, u32 tex, u32 ufrac, u32 lfrac, u32 ecx, u32 edx, int cf)
{
    for (;;) {
        u32 eax = cf ? p_step2 : p_step, c;
        u64 t;
        uchar texel, dh;

        tex &= p_mask;
        t = (u64)ufrac + p_ufrac; ufrac = (u32)t; c = (u32)(t >> 32);
        texel = p_bits[tex];
        tex = tex + eax + c;
        t = (u64)lfrac + p_ifrac; lfrac = (u32)t; c = (u32)(t >> 32);
        dh = (uchar)((edx >> 8) + (uchar)ecx + c);
        edx = (edx & 0xffff0000) | ((u32)dh << 8) | texel;
        eax = (u32)p_tab[texel];
        t = (u64)ecx + p_vstep; ecx = (u32)t; cf = (int)(t >> 32);
        if ((uchar)eax == 0x10)
            texel = p_ltab[edx];
        else {
            if (p_mode_src) {
                u32 s = cnt + eax + p_src;
#ifndef TN_WATCOM_ABI
                /* G48: a texel's offset up a row and left a pixel, at the canvas's first
                 * pixel, is the byte before the canvas */
                if (s < (u32)grd_bm.bits)
                    s = (u32)grd_bm.bits;
#endif
                texel = *(uchar *)s;
            }
            texel = p_clut[texel];
        }
        *(uchar *)(p_dst + cnt) = texel;
        if (++cnt == 0)
            return;
    }
}

/* span setup for up to 0x80 pixels: 1/(w*n) from the reciprocal table */
static void span_short(grs_tmap_loop_info *ti, long n)
{
    u32 ecx, ebp, esi, edi, edx, lo, b;
    long winv, s;
    s64 p;

    winv = (long)(((s64)1 << 32) / ti->w);
    s = (long)(((s64)winv * grd_recip_tab[n]) >> 16);
    p = (s64)(ti->right.i - ti->left.i) * s;
    ecx = (uchar)(p >> 32); p_ifrac = (u32)p;
    p = (s64)(ti->right.u - ti->left.u) * s;
    p_ufrac = (u32)p; ebp = (u32)(p >> 32);
    p = (s64)(ti->right.v - ti->left.v) * s;
    edx = ((u32)(p >> 32) << (p_wlog & 31)) + ebp;
    ebp = (u32)p & 0xffffff00;
    p_step = edx; p_step2 = edx + p_row;
    winv++;
    p = (s64)ti->left.v * winv;
    esi = (u32)(p >> 32); ecx |= (u32)p & 0xffffff00;
    p = (s64)ti->left.u * winv;
    esi = (esi << (p_wlog & 31)) + (u32)(p >> 32); edi = (u32)p;
    p = (s64)ti->left.i * winv;
    edx = (u32)(p >> 32) & 0xf;
    lo = (u32)p; b = lo < p_ifrac; lo -= p_ifrac;
    edx = (uchar)(edx - (uchar)ecx - b); edx |= edx << 8;
    {
        u64 t = (u64)ecx + ebp;
        ecx = (u32)t;
        p_vstep = ebp;
        span(-n, esi, edi, lo, ecx, edx, (int)(t >> 32));
    }
}

/* span setup for longer spans (0x32DFFE): divide by n*w */
static void span_long_tail(grs_tmap_loop_info *ti, long n, u32 ecx, u32 ebp, fix nw)
{
    u32 esi, edi, edx, lo, b;
    fix q;

    q = fdiv(ti->right.v - ti->left.v, nw);
    edx = ((u32)(q >> 16) << (p_wlog & 31)) + ebp;
    ebp = ((u32)q << 16) & 0xffffff00;
    p_step = edx; p_step2 = edx + p_row;
    q = fdiv(ti->left.v, ti->w);
    esi = q >> 16; ecx |= ((u32)q << 16) & 0xffffff00;
    q = fdiv(ti->left.u, ti->w);
    esi = (esi << (p_wlog & 31)) + (q >> 16); edi = (u32)q << 16;
    q = fdiv(ti->left.i, ti->w);
    edx = (q >> 16) & 0xf;
    lo = (u32)q << 16; b = lo < p_ifrac; lo -= p_ifrac;
    edx = (uchar)(edx - (uchar)ecx - b); edx |= edx << 8;
    {
        u64 t = (u64)ecx + ebp;
        ecx = (u32)t;
        p_vstep = ebp;
        span(-n, esi, edi, lo, ecx, edx, (int)(t >> 32));
    }
}

static void span_long_head(grs_tmap_loop_info *ti, fix nw, u32 *ecx, u32 *ebp)
{
    fix q = fdiv(ti->right.i - ti->left.i, nw);
    *ecx = (uchar)(q >> 16); p_ifrac = (u32)q << 16;
    q = fdiv(ti->right.u - ti->left.u, nw);
    p_ufrac = (u32)q << 16; *ebp = q >> 16;
}

static void add_steps(grs_tmap_loop_info *ti)
{
    ti->w += p_dw;
    ti->left.i += p_ldi;
    ti->right.i += p_rdi;
    ti->left.u += p_ldu;
    ti->right.u += p_rdu;
    ti->left.v += p_ldv;
    ti->right.v += p_rdv;
}

int gri_water_lit_floor_umap_loop(grs_tmap_loop_info *ti)
{
    long n;
    u32 ecx, ebp;

    set_source();
    p_clut = *water_clut_row;
    n = (long)(p_dst - (u32)ti->left.x);
    if (n < 0)
        return 1;
    if (n != 0) {
        if ((u32)n > 0x80) {
            fix nw = (fix)((u32)n * (u32)ti->w);
            span_long_head(ti, nw, &ecx, &ebp);
            span_long_tail(ti, n, ecx, ebp, nw);
        } else
            span_short(ti, n);
    }
    for (;;) {
        /* next scanline (0x32E1A0) */
        u32 bits = (u32)ti->bm.bits, lx;
        u64 t = (u64)bits + p_rfrac;
        ushort lo;
        bits = (u32)t;
        p_dst = p_dst + p_rint + (u32)(t >> 32);
        water_src_off -= 2 * (long)(short)grd_bm.row;
        set_source();
        water_clut_row++;
        p_clut = *water_clut_row;
        lo = (ushort)bits;
        lx = (u32)ti->left.x + p_lint + (((u32)lo + (ushort)p_lfrac) >> 16);
        bits = (bits & 0xffff0000) | (ushort)(lo + (ushort)p_lfrac);
        ti->bm.bits = (uchar *)bits;
        n = (long)(p_dst - lx);
        ti->left.x = (fix)lx;
        if (n < 0)
            return 1;
        if (n == 0) {
            /* empty scanline (0x32E2D6) */
            ti->right.u += p_rdu;
            ti->left.u += p_ldu;
            ti->right.v += p_rdv;
            ti->left.v += p_ldv;
            ti->right.i += p_rdi;
            ti->left.i += p_ldi;
            ti->w += p_dw;
            if (--ti->n == 0)
                return 0;
            continue;
        }
        if ((u32)n > 0x80) {
            fix nw;
            ti->w += p_dw;
            nw = (fix)((u32)n * (u32)ti->w);
            ti->left.i += p_ldi;
            ti->right.i += p_rdi;
            {
                fix q = fdiv(ti->right.i - ti->left.i, nw);
                ecx = (uchar)(q >> 16); p_ifrac = (u32)q << 16;
            }
            ti->left.u += p_ldu;
            ti->right.u += p_rdu;
            {
                fix q = fdiv(ti->right.u - ti->left.u, nw);
                p_ufrac = (u32)q << 16; ebp = q >> 16;
            }
            ti->left.v += p_ldv;
            ti->right.v += p_rdv;
            if (--ti->n == 0)
                return 0;
            span_long_tail(ti, n, ecx, ebp, nw);
        } else {
            long winv, s;
            s64 p;
            u32 edx;
            ti->w += p_dw;
            winv = (long)(((s64)1 << 32) / ti->w);
            s = (long)(((s64)winv * grd_recip_tab[n]) >> 16);
            ti->left.i += p_ldi;
            ti->right.i += p_rdi;
            p = (s64)(ti->right.i - ti->left.i) * s;
            p_ifrac = (u32)p; ecx = (uchar)(p >> 32);
            ti->left.u += p_ldu;
            ti->right.u += p_rdu;
            p = (s64)(ti->right.u - ti->left.u) * s;
            p_ufrac = (u32)p; ebp = (u32)(p >> 32);
            ti->left.v += p_ldv;
            ti->right.v += p_rdv;
            if (--ti->n == 0)
                return 0;
            /* 0x32E0B0: the same tail as the first scanline */
            {
                u32 esi, edi, lo, b;
                p = (s64)(ti->right.v - ti->left.v) * s;
                edx = ((u32)(p >> 32) << (p_wlog & 31)) + ebp;
                ebp = (u32)p & 0xffffff00;
                p_step = edx; p_step2 = edx + p_row;
                winv++;
                p = (s64)ti->left.v * winv;
                esi = (u32)(p >> 32); ecx |= (u32)p & 0xffffff00;
                p = (s64)ti->left.u * winv;
                esi = (esi << (p_wlog & 31)) + (u32)(p >> 32); edi = (u32)p;
                p = (s64)ti->left.i * winv;
                edx = (u32)(p >> 32) & 0xf;
                lo = (u32)p; b = lo < p_ifrac; lo -= p_ifrac;
                edx = (uchar)(edx - (uchar)ecx - b); edx |= edx << 8;
                {
                    u64 t2 = (u64)ecx + ebp;
                    ecx = (u32)t2;
                    p_vstep = ebp;
                    span(-n, esi, edi, lo, ecx, edx, (int)(t2 >> 32));
                }
            }
        }
    }
}
