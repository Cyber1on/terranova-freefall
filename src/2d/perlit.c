/* perlit.c -- Terra Nova v1.09 lit perspective mapper scan (TN's own; names ours).
 *
 *   0x34D510 gri_lit_per_vscan    0x34D61C gri_lit_per_hscan    0x34D6B0 gri_lit_per_scan
 *   data 0x460820 gri_per_lit_proj
 *
 * As the plain scan of perscan.c, plus a light value: the vertices are projected onto the
 * scan direction (y - x * scan_slope), the light i (vertex +0x14) is interpolated along the
 * two polygon edges from the vertex with the lowest projection (one walking down the vertex
 * list, one walking up) and across each column ((i_up - i_down) / len per pixel).
 * Unlike the plain scan the u, v steps are simply (end - start) / len (no slope-fraction
 * weighting by the world gradients).
 * Quirks kept: the V clamp tests u < 0 (as perscan.c); when the column reaches the highest
 * projection the light starts from that vertex and the neighbour on the down side sets the
 * up-side value (and vice versa); the run loop starts from the end of the previous scanline
 * call, which may be stale.  The vscan shell swaps the gradients and never swaps them back,
 * and runs the lit scan with the vertex x/y swapped; the hscan shell leaves the vertices
 * swapped when no span list was made.
 */
#include "2d.h"

typedef struct {
    void *scanline_func0;          /* +00 */
    void *shell_func;              /* +04 */
    long fill;                     /* +08 */
    fix scan_slope;                /* +0C */
    long dp_lo, dp_hi;             /* +10 +14 */
    fix u, v;                      /* +18 +1C */
    fix du_lo, dv_lo, du_hi, dv_hi;    /* +20 +24 +28 +2C */
    uchar *p;                      /* +30 */
    long dp;                       /* +34 */
    fix y;                         /* +38 */
    void *addr_func;               /* +3C */
    void *scanline_func;           /* +40 */
    long _44, _48;
    fix wux, wuy;                  /* +4C +50 */
    fix i;                         /* +54 */
    fix wvx, wvy;                  /* +58 +5C */
    fix di;                        /* +60 */
    long _64[6];
    fix dudx, dudy, dvdx, dvdy, dzdx, dzdy;   /* +7C .. +90 */
    fix u0, v0, z0;                /* +94 +98 +9C */
    int x0, y0;                    /* +A0 +A4 */
} per_ps;
LG_STATIC_ASSERT(sizeof(per_ps) == 0xA8, per_ps_lit_size);

extern uchar gri_per_uv_ok;        /* 0x4608FC (permap.c) */
void gri_per_check_uv(grs_bitmap *bm, int n, grs_vertex **vpl);
void gri_per_swap_xy(int n, grs_vertex **vpl);
long *gri_per_spans(grs_vertex **vpl, int n, long *buf);
void gri_per_skew_spans(long *buf, fix slope);
long *gri_per_span_columns(long **pspans);
void gri_lit_per_scan(grs_bitmap *bm, long *seg, per_ps *ps, int n, grs_vertex **vpl);

extern fix gri_per_lit_proj[];
#ifndef TN_LINK_ORIGINAL_DATA
fix gri_per_lit_proj[55];          /* 0x460820: projection of each vertex (up to 0x4608FC) */
#endif

#define FIX_CEIL(a) ((fix)(((a) + 0xFFFF) & 0xFFFF0000))
static long cint(fix a) { return (short)((a + 0xFFFF) >> 16); }
static long d29(long a, long b) { return (long)(((long long)a << 29) / b); }
static fix mul16(fix a, fix b) { return (fix)(((long long)a * b) >> 16); }
static fix div16(fix a, fix b) { return (fix)(((long long)a << 16) / b); }

static fix clamp_hi(fix a, int lim)
{
    if ((a >> 16) >= lim)
        a = ((lim - 1) << 16) | 0xFFFF;
    return a;
}

void gri_lit_per_scan(grs_bitmap *bm, long *seg, per_ps *ps, int n, grs_vertex **vpl)
{
    fix *proj = gri_per_lit_proj;
    fix slope = ps->scan_slope, z, z2, U2, V2, pa, pb, ia, ib, dia, dib;
    long si = slope >> 16, si1 = si + 1, x, x_end, len, e, dy, dx, c, mn, mx, end, pos;
    ulong sfrac = (ulong)slope << 16, colfix, colend, frac;
    int imin, imax, la, lb, prev, j, k;
    long *rec, *lst;
    long scan[2];
    int w = bm->w, h = bm->h;

    x = ((long *)seg[0])[0];
    x_end = ((long *)seg[0])[1];
    proj[0] = vpl[0]->y - mul16(vpl[0]->x, slope);
    mn = mx = cint(proj[0]);
    imin = imax = 0;
    for (k = 1; k < n; k++) {
        proj[k] = vpl[k]->y - mul16(vpl[k]->x, slope);
        c = cint(proj[k]);
        if (c < mn) {
            mn = c;
            imin = k;
        }
        if (c > mx) {
            mx = c;
            imax = k;
        }
    }
    la = lb = imin;
    pa = pb = proj[imin];
    ia = ib = vpl[imin]->i;
    dia = dib = 0;
    if (x >= x_end)
        return;
    do {
        if (x >= mx) {
            /* past the far vertex: constant light from it (and its neighbours if level) */
            ib = ia = vpl[imax]->i;
            j = imax - 1;
            if (j < 0)
                j = n - 1;
            if (cint(proj[j]) == mx)
                ib = vpl[j]->i;
            j = imax + 1;
            if (j == n)
                j = 0;
            if (cint(proj[j]) == mx)
                ia = vpl[j]->i;
            dib = dia = 0;
            end = x_end;
        } else {
            if (cint(pa) <= x) {        /* edge walking down the vertex list */
                do {
                    prev = la;
                    if (--la < 0)
                        la = n - 1;
                    pa = proj[la];
                } while (cint(pa) <= x);
                ia = vpl[prev]->i;
                dia = div16(vpl[la]->i - ia, pa - proj[prev]);
                ia += mul16(dia, FIX_CEIL(proj[prev]) - proj[prev]);
            }
            if (cint(pb) <= x) {        /* edge walking up the vertex list */
                do {
                    prev = lb;
                    if (++lb >= n)
                        lb = 0;
                    pb = proj[lb];
                } while (cint(pb) <= x);
                ib = vpl[prev]->i;
                dib = div16(vpl[lb]->i - ib, pb - proj[prev]);
                ib += mul16(dib, FIX_CEIL(proj[prev]) - proj[prev]);
            }
            end = pa > pb ? cint(pb) : cint(pa);
            if (end > x_end)
                end = x_end;
        }
        colend = (ulong)end << 16;
        colfix = (ulong)x << 16;
        for (; (long)colfix < (long)colend; colfix += 0x10000, ia += dia, ib += dib, x++) {
            rec = (long *)*++seg;
            if (rec == NULL)
                continue;
            for (lst = rec; lst[2]; lst = (long *)lst[2])
                ;
            len = lst[1] - rec[0];
            if (len > 0) {
                ps->y = rec[0] * slope + colfix;
                dy = rec[0] - ps->x0;
                dx = (ps->y >> 16) - ps->y0;
                z = ps->z0 + ps->dzdx * dy + ps->dzdy * dx;
                ps->u = ps->u0 + ps->dudx * dy + ps->dudy * dx;
                ps->v = ps->v0 + ps->dvdx * dy + ps->dvdy * dx;
                e = ((ps->y + slope * len) >> 16) - (ps->y >> 16);
                U2 = ps->u + ps->dudx * len + ps->dudy * e;
                V2 = ps->v + ps->dvdx * len + ps->dvdy * e;
                ps->u = d29(ps->u, z);
                ps->v = d29(ps->v, z);
                z2 = z + (ps->dzdx * len + ps->dzdy * e);
                U2 = d29(U2, z2);
                V2 = d29(V2, z2);
                if (gri_per_uv_ok) {
                    ps->u = clamp_hi(ps->u, w);
                    if (ps->u < 0)
                        ps->u = 0;
                    ps->v = clamp_hi(ps->v, h);
                    if (ps->u < 0)          /* sic: tests u */
                        ps->v = 0;
                    U2 = clamp_hi(U2, w);
                    if (U2 < 0)
                        U2 = 0;
                    V2 = clamp_hi(V2, h);
                    if (V2 < 0)
                        V2 = 0;
                }
                ps->du_hi = ps->du_lo = (U2 - ps->u) / len;
                ps->dv_hi = ps->dv_lo = (V2 - ps->v) / len;
                ps->di = (ib - ia) / len;
                ps->i = ia;
                ps->p = (uchar *)WATCOM_CALL(GR_IFN(ps->addr_func), rec[0], ps->y >> 16);
            }
            for (;;) {
                if (rec[1] > rec[0]) {
                    scan[0] = rec[0];
                    scan[1] = rec[1];
                    WATCOM_CALL(GR_IFN(ps->scanline_func), (long)scan, (long)ps, (long)bm);
                }
                rec = (long *)rec[2];
                if (rec == NULL)
                    break;
                frac = (ulong)ps->y << 16;
                pos = ps->y >> 16;
                for (len = scan[1]; len < rec[0]; len++) {
                    ulong f = frac + sfrac;
                    if (f < frac) {
                        ps->p += ps->dp_hi;
                        ps->u += ps->du_hi;
                        ps->v += ps->dv_hi;
                        pos += si1;
                    } else {
                        ps->p += ps->dp_lo;
                        ps->u += ps->du_lo;
                        ps->v += ps->dv_lo;
                        pos += si;
                    }
                    frac = f;
                    ps->i += ps->di;
                }
                ps->y = (pos << 16) + (frac >> 16);
            }
        }
    } while (x < x_end);
}

/* ---------------------------------------------------------------- shells */
static void scan_all(grs_bitmap *bm, long *spans, per_ps *ps, int n, grs_vertex **vpl)
{
    long *cur = spans, *seg;

    while (cur) {
        seg = gri_per_span_columns(&cur);
        if (seg == NULL)
            break;
        gri_lit_per_scan(bm, seg, ps, n, vpl);
        temp_free(seg);
    }
}

void gri_lit_per_vscan(grs_bitmap *bm, int n, grs_vertex **vpl, per_ps *ps)
{
    fix t;
    long *spans;

#define SWAP(a, b) (t = (a), (a) = (b), (b) = t)
    SWAP(ps->dudx, ps->dudy);
    SWAP(ps->dvdx, ps->dvdy);
    SWAP(ps->dzdx, ps->dzdy);
    SWAP(ps->wux, ps->wuy);
    SWAP(ps->wvx, ps->wvy);
    SWAP(ps->x0, ps->y0);
#undef SWAP
    gri_per_check_uv(bm, n, vpl);
    spans = gri_per_spans(vpl, n, NULL);
    if (spans == NULL)
        return;
    gri_per_skew_spans(spans, ps->scan_slope);
    gri_per_swap_xy(n, vpl);
    scan_all(bm, spans, ps, n, vpl);
    gri_per_swap_xy(n, vpl);
    temp_free(spans);
}

void gri_lit_per_hscan(grs_bitmap *bm, int n, grs_vertex **vpl, per_ps *ps)
{
    long *spans;

    gri_per_check_uv(bm, n, vpl);
    gri_per_swap_xy(n, vpl);
    spans = gri_per_spans(vpl, n, NULL);
    if (spans == NULL)
        return;
    gri_per_swap_xy(n, vpl);
    gri_per_skew_spans(spans, ps->scan_slope);
    scan_all(bm, spans, ps, n, vpl);
    temp_free(spans);
}
