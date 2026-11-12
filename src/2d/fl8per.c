/* fl8per.c -- Terra Nova v1.09 flat8 perspective mapper inits and scanline functions
 * (function table GRC_PER .. GRC_TRANS_CLUT_PER_VSCAN for BMT_FLAT8).  Decompiled from
 * __FF.EXE (Shock's fl8p*.c family).
 *
 *   0x331870 gri_per_umap_scanline              0x3318AC gri_per_umap_hscan_init
 *   0x331928 gri_per_umap_vscan_init
 *   0x331990 gri_trans_per_umap_scanline        0x3319CC gri_trans_per_umap_hscan_init
 *   0x331A5C gri_trans_per_umap_vscan_init
 *   0x331AD0 gri_lit_per_umap_scanline          0x331B0C gri_lit_per_umap_hscan_init
 *   0x331B88 gri_lit_per_umap_vscan_init
 *   0x331BF0 gri_trans_lit_per_umap_scanline    0x331C2C gri_trans_lit_per_umap_hscan_init
 *   0x331CA8 gri_trans_lit_per_umap_vscan_init
 *   0x331D10 gri_clut_per_umap_scanline         0x331D4C gri_clut_per_umap_hscan_init
 *   0x331DC8 gri_clut_per_umap_vscan_init
 *   0x331E30 gri_trans_clut_per_umap_scanline   0x331E6C gri_trans_clut_per_umap_hscan_init
 *   0x331EE8 gri_trans_clut_per_umap_vscan_init
 *   0x3336F0 gri_tluc8_trans_per_umap_scanline  0x333830 gri_tluc8_trans_per_umap_hscan_init
 *   0x3338A4 gri_tluc8_trans_per_umap_vscan_init (BMT_TLUC8 GRC_TRANS_PER; all C, no setup call)
 *
 * The inits are called by the perspective core (2d-core, 0x34A1F0..) with the bitmap and the
 * perspective setup; they install the shell (hscan 0x34C9B4 / vscan 0x34C8D0), the scanline
 * (lit: own shells 0x34D61C / 0x34D510), the scanline function and the destination address function (0x34CFD0 / 0x34CFEC), set the destination
 * steps and call the setup of the inner loop (2d-core asm).  The scanline functions are called
 * by the shells with the scanline's [x0, x1] and the setup, compute the end address and run
 * the inner loop.  (The asm scanline functions return with ebx = the new +38 value; their
 * callers reload ebx, so the C versions keep the Watcom convention.)
 *
 * The lit inits call their setup first and do not call the generic setup 0x34C690.
 */
#include "2d_tmap.h"

/* View of 2d-core's grs_per_setup (src/2d/permap.c, 0xA8 bytes; not in 2d.h) as the scan
 * inits, scanline functions and shells use it: +0C = its scan_slope, +10 = its dp (the
 * function index, overwritten here by the destination step), +14.. its scan state. */
typedef struct {
    long _00;
    void *shell_func;              /* +04 */
    long _08;
    fix dyx;                       /* +0C x step per scanline (sign selects the steps) */
    long dp_lo, dp_hi;             /* +10 +14 destination steps */
    fix u, v;                      /* +18 +1C texture position */
    fix du_lo, dv_lo;              /* +20 +24 steps without a y carry */
    fix du_hi, dv_hi;              /* +28 +2C steps with a y carry */
    uchar *p;                      /* +30 current destination address */
    long dp;                       /* +34 previous - current address */
    fix y;                         /* +38 scanline position (fix) */
    void *addr_func;               /* +3C uchar *f(int x, int y) */
    void *scanline_func;           /* +40 */
} grs_per_scan;

/* 2d-core: shells, address functions, setups and inner loops (all original) */
void gri_per_hscan(void);                                              /* 0x34C9B4 shell */
void gri_per_vscan(void);                                              /* 0x34C8D0 shell */
uchar *gri_per_haddr(int x, int y);                                    /* 0x34CFD0 */
uchar *gri_per_vaddr(int x, int y);                                    /* 0x34CFEC */
void gri_lit_per_hscan(void);                                          /* 0x34D61C shell */
void gri_lit_per_vscan(void);                                          /* 0x34D510 shell */
#define PER_HSCAN WATCOM_FN(gri_per_hscan)
#define LIT_PER_HSCAN WATCOM_FN(gri_lit_per_hscan)
#define LIT_PER_VSCAN WATCOM_FN(gri_lit_per_vscan)
#define PER_VSCAN WATCOM_FN(gri_per_vscan)
#define PER_HADDR WATCOM_FN(gri_per_haddr)
#define PER_VADDR WATCOM_FN(gri_per_vaddr)
int gri_per_setup(grs_bitmap *bm, grs_per_scan *ps);                  /* 0x34C690 */
int gri_solid_per_setup(grs_bitmap *bm, grs_per_scan *ps);            /* 0x34D010 */
int gri_trans_per_setup(grs_bitmap *bm, grs_per_scan *ps);            /* 0x34D031 */
int gri_lit_per_hsetup(grs_bitmap *bm, grs_per_scan *ps);             /* 0x34D2E6 */
int gri_lit_per_vsetup(grs_bitmap *bm, grs_per_scan *ps);             /* 0x34D2B0 */
int gri_trans_lit_per_hsetup(grs_bitmap *bm, grs_per_scan *ps);       /* 0x34DF36 */
int gri_trans_lit_per_vsetup(grs_bitmap *bm, grs_per_scan *ps);       /* 0x34DF00 */
int gri_clut_per_setup(grs_bitmap *bm, grs_per_scan *ps);             /* 0x34E160 */
int gri_trans_clut_per_setup(grs_bitmap *bm, grs_per_scan *ps);       /* 0x34E410 */
void gri_per_loop(grs_per_scan *ps);                                  /* 0x34C725 */
void gri_trans_per_loop(grs_per_scan *ps);                            /* 0x34D0E2 */
void gri_lit_per_loop(grs_per_scan *ps);                              /* 0x34D390 */
void gri_trans_lit_per_loop(grs_per_scan *ps);                        /* 0x34DFE0 */
void gri_clut_per_loop(grs_per_scan *ps);                             /* 0x34E223 */
void gri_trans_clut_per_loop(grs_per_scan *ps);                       /* 0x34E4CA */

static grs_per_scan *scanline(int *x, grs_per_scan *ps, void (*loop)(grs_per_scan *))
{
    fix y = ps->y + (x[1] - x[0]) * ps->dyx;
    uchar *p0 = ps->p;
    uchar *p = (uchar *)WATCOM_CALL(ps->addr_func, (long)x[1], (long)(y >> 16));
    ps->p = p;
    ps->dp = (long)(p0 - p);
    loop(ps);
    ps->y = y;
    return ps;
}

/* destination steps of a horizontal scan */
static void hsteps(grs_per_scan *ps)
{
    ulong row = (ushort)grd_bm.row;
    if (ps->dyx >= 0) {
        ps->dp_hi = row + 1;
        ps->dp_lo = ps->dyx == 0x10000 ? (long)(row + 1) : 1;
    } else {
        ps->dp_hi = 1;
        ps->dp_lo = 1 - row;
    }
}

static void vsteps(grs_per_scan *ps)
{
    ulong row = (ushort)grd_bm.row;
    if (ps->dyx >= 0) {
        ps->dp_lo = row;
        ps->dp_hi = row + 1;
    } else {
        ps->dp_lo = row - 1;
        ps->dp_hi = row;
    }
}

#define SCANLINE(name, loop)                                          \
grs_per_scan *gri_##name##_scanline(int *x, grs_per_scan *ps)       \
{                                                                     \
    return scanline(x, ps, loop);                                     \
}

SCANLINE(per_umap, gri_per_loop)
SCANLINE(trans_per_umap, gri_trans_per_loop)
SCANLINE(lit_per_umap, gri_lit_per_loop)
SCANLINE(trans_lit_per_umap, gri_trans_lit_per_loop)
SCANLINE(clut_per_umap, gri_clut_per_loop)
SCANLINE(trans_clut_per_umap, gri_trans_clut_per_loop)

static void funcs(grs_per_scan *ps, void *shell, void *scan, void *addr)
{
    ps->shell_func = shell;
    ps->scanline_func = scan;
    ps->addr_func = addr;
}

int gri_per_umap_hscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    funcs(ps, PER_HSCAN, WATCOM_FN(gri_per_umap_scanline), PER_HADDR);
    hsteps(ps);
    return gri_per_setup(bm, ps);
}

int gri_per_umap_vscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    funcs(ps, PER_VSCAN, WATCOM_FN(gri_per_umap_scanline), PER_VADDR);
    vsteps(ps);
    return gri_per_setup(bm, ps);
}

/* transparent (and, with FILL_SOLID, solid color) */
static int trans_setup(grs_bitmap *bm, grs_per_scan *ps)
{
    if (grd_gc.fill_type == FILL_SOLID)
        return gri_solid_per_setup(bm, ps);
    return gri_trans_per_setup(bm, ps);
}

int gri_trans_per_umap_hscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    funcs(ps, PER_HSCAN, WATCOM_FN(gri_trans_per_umap_scanline), PER_HADDR);
    hsteps(ps);
    return trans_setup(bm, ps);
}

int gri_trans_per_umap_vscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    funcs(ps, PER_VSCAN, WATCOM_FN(gri_trans_per_umap_scanline), PER_VADDR);
    vsteps(ps);
    return trans_setup(bm, ps);
}

/* The lit inits return what the asm leaves in eax (callers test it for nonzero). */
static long lit_hret(grs_per_scan *ps)
{
    ulong row = (ushort)grd_bm.row;
    if (ps->dyx < 0)
        return row;
    return ps->dyx == 0x10000 ? (long)(row + 1) : (long)grd_canvas;
}

static long lit_vret(grs_per_scan *ps)
{
    ulong row = (ushort)grd_bm.row;
    return ps->dyx < 0 ? (long)row : (long)(row + 1);
}

long gri_lit_per_umap_hscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    gri_lit_per_hsetup(bm, ps);
    funcs(ps, LIT_PER_HSCAN, WATCOM_FN(gri_lit_per_umap_scanline), PER_HADDR);
    hsteps(ps);
    return lit_hret(ps);
}

long gri_lit_per_umap_vscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    gri_lit_per_vsetup(bm, ps);
    funcs(ps, LIT_PER_VSCAN, WATCOM_FN(gri_lit_per_umap_scanline), PER_VADDR);
    vsteps(ps);
    return lit_vret(ps);
}

long gri_trans_lit_per_umap_hscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    gri_trans_lit_per_hsetup(bm, ps);
    funcs(ps, LIT_PER_HSCAN, WATCOM_FN(gri_trans_lit_per_umap_scanline), PER_HADDR);
    hsteps(ps);
    return lit_hret(ps);
}

long gri_trans_lit_per_umap_vscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    gri_trans_lit_per_vsetup(bm, ps);
    funcs(ps, LIT_PER_VSCAN, WATCOM_FN(gri_trans_lit_per_umap_scanline), PER_VADDR);
    vsteps(ps);
    return lit_vret(ps);
}

int gri_clut_per_umap_hscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    funcs(ps, PER_HSCAN, WATCOM_FN(gri_clut_per_umap_scanline), PER_HADDR);
    hsteps(ps);
    return gri_clut_per_setup(bm, ps);
}

int gri_clut_per_umap_vscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    funcs(ps, PER_VSCAN, WATCOM_FN(gri_clut_per_umap_scanline), PER_VADDR);
    vsteps(ps);
    return gri_clut_per_setup(bm, ps);
}

int gri_trans_clut_per_umap_hscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    funcs(ps, PER_HSCAN, WATCOM_FN(gri_trans_clut_per_umap_scanline), PER_HADDR);
    hsteps(ps);
    return gri_trans_clut_per_setup(bm, ps);
}

int gri_trans_clut_per_umap_vscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    funcs(ps, PER_VSCAN, WATCOM_FN(gri_trans_clut_per_umap_scanline), PER_VADDR);
    vsteps(ps);
    return gri_trans_clut_per_setup(bm, ps);
}

/* ---------------- BMT_TLUC8 trans per (Watcom C) ---------------- */
grs_per_scan *gri_tluc8_trans_per_umap_scanline(int *x, grs_per_scan *ps, grs_bitmap *bm)
{
    int xi = x[0];
    fix yi = ps->dyx >> 16;
    ulong yf = (ulong)ps->y << 16, yfstep = (ulong)ps->dyx << 16;
    long yint = ps->y >> 16;
    uchar *bits = bm->bits;
    ulong umask = (1u << (bm->wlog & 31)) - 1, vmask = (1u << (bm->hlog & 31)) - 1;
    int wlog = (signed char)bm->wlog;
    uchar *p = ps->p;

    for (; xi < x[1]; xi++) {
        uchar t = bits[(((ulong)(ps->v >> 16) & vmask) << (wlog & 31)) + ((ulong)(ps->u >> 16) & umask)];
        ulong o = yf;
        if (t != 0) {
            uchar *tab = tluc8tab_p[t];
            *p = tab ? tab[*p] : t;
        }
        yf += yfstep;
        if (yf < o) {
            p += ps->dp_hi;
            ps->u += ps->du_hi;
            ps->v += ps->dv_hi;
            yint += yi + 1;
        } else {
            p += ps->dp_lo;
            ps->u += ps->du_lo;
            ps->v += ps->dv_lo;
            yint += yi;
        }
    }
    ps->p = p;
    ps->y = (yint << 16) + (yf >> 16);
    return ps;
}

long gri_tluc8_trans_per_umap_hscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    funcs(ps, PER_HSCAN, WATCOM_FN(gri_tluc8_trans_per_umap_scanline), PER_HADDR);
    hsteps(ps);
    return lit_hret(ps);
}

long gri_tluc8_trans_per_umap_vscan_init(grs_bitmap *bm, grs_per_scan *ps)
{
    funcs(ps, PER_VSCAN, WATCOM_FN(gri_tluc8_trans_per_umap_scanline), PER_VADDR);
    vsteps(ps);
    return lit_vret(ps);
}
