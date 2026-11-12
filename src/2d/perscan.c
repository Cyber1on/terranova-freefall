/* perscan.c -- Terra Nova v1.09 perspective mapper scan engine (TN's own; not in System
 * Shock).  Names ours.
 *
 *   0x34FE40 gri_per_spans        0x3500A8 gri_per_spans_fill (unreferenced)
 *   0x350140 gri_per_skew_spans   0x35017C gri_per_span_columns
 *   0x34CA40 gri_per_scan         0x34C9B4 gri_per_hscan   0x34C8D0 gri_per_vscan
 *   0x34CFD0 gri_per_haddr        0x34CFEC gri_per_vaddr
 *
 * A polygon is turned into a span list {y_min, y_max, (xl, xr) per row}, each row is shifted
 * by -(y * scan_slope) >> 16 (so lines of constant depth become columns), and the result is
 * cut into column lists of runs [y, y_end) (gri_per_span_columns, at most 0x8000 bytes of
 * output at a time).  gri_per_scan walks the columns: for each it computes u/z, v/z, 1/z at
 * both ends, the u, v steps (with or without a carry of the slope fraction) and calls the
 * scanline function (2d-tmap's, which runs the inner loops of perloop.c) for each run.
 * The "hscan" shell swaps the vertex x/y while building the spans (and leaves them swapped
 * when no span list was made: y range empty or out of memory); the "vscan" shell swaps the
 * x and y gradients of the setup instead (and never swaps them back).
 */
#include "2d.h"

#ifndef TN_WATCOM_ABI
/* port/video/sharp3d.c: the sharp 3D view has kx * ky times the runs, so the column lists get as much
 * more room (from its own bigger temporary buffer) */
extern int port_s3d_on, port_s3d_kx, port_s3d_ky;
#define SPAN_COLS_SIZE (port_s3d_on ? 0x8000 * port_s3d_kx * port_s3d_ky : 0x8000)
#else
#define SPAN_COLS_SIZE 0x8000
#endif

/* the perspective setup as the scan engine sees it (0xA8 bytes; see permap.c) */
typedef struct {
    void *scanline_func0;          /* +00 */
    void *shell_func;              /* +04 */
    long fill;                     /* +08 */
    fix scan_slope;                /* +0C */
    long dp_lo, dp_hi;             /* +10 +14 destination steps (no / with slope carry) */
    fix u, v;                      /* +18 +1C */
    fix du_lo, dv_lo, du_hi, dv_hi;    /* +20 +24 +28 +2C */
    uchar *p;                      /* +30 */
    long dp;                       /* +34 */
    fix y;                         /* +38 position along the scan (16.16) */
    void *addr_func;               /* +3C */
    void *scanline_func;           /* +40 */
    long _44, _48;
    fix wux, wuy;                  /* +4C +50 */
    long _54;
    fix wvx, wvy;                  /* +58 +5C */
    long _60[7];
    fix dudx, dudy, dvdx, dvdy, dzdx, dzdy;   /* +7C .. +90 */
    fix u0, v0, z0;                /* +94 +98 +9C */
    int x0, y0;                    /* +A0 +A4 */
} per_ps;
LG_STATIC_ASSERT(sizeof(per_ps) == 0xA8, per_ps_size);

extern uchar gri_per_uv_ok;        /* 0x4608FC (permap.c) */
void gri_per_check_uv(grs_bitmap *bm, int n, grs_vertex **vpl);
void gri_per_swap_xy(int n, grs_vertex **vpl);

#define FIX_CEIL(a) ((fix)(((a) + 0xFFFF) & 0xFFFF0000))
static long cint(fix a) { return (short)((a + 0xFFFF) >> 16); }
static long iabs(long a) { return a < 0 ? -a : a; }
static long d29(long a, long b) { return (long)(((long long)a << 29) / b); }

/* ---------------------------------------------------------------- span list */
long *gri_per_spans(grs_vertex **vpl, int n, long *buf)
{
    grs_vertex **last = vpl + n - 1, **pl, **pr, **p, *vl, *vr;
    long y_min, y_max, y, ly, ry, lim, t, *out;
    fix lx = 0, rx = 0, ldx = 0, rdx = 0, lyf, ryf, ny;

    y_min = y_max = cint(vpl[0]->y);
    pl = vpl;
    for (p = vpl + 1; p <= last; p++) {
        t = cint((*p)->y);
        if (t < y_min) {
            y_min = t;
            pl = p;
        }
        if (t > y_max)
            y_max = t;
    }
    pr = pl;
    ly = ry = y = y_min;
    if (buf == NULL) {
        if (y_max - y_min > 0)
            buf = temp_malloc(8 * (y_max - y_min) + 8);
        if (buf == NULL)
            return NULL;
    }
    buf[0] = y_min;
    buf[1] = y_max;
    out = buf + 2;
    if (y >= y_max)
        return buf;
    do {
        if (y == ly) {
            vl = *pl;
            lyf = vl->y;
            if (--pl < vpl)
                pl = last;
            for (;;) {
                ny = (*pl)->y;
                t = cint(ny);
                if (y < t)
                    break;
                if (y == t) {
                    lyf = ny;
                    vl = *pl;
                }
                if (--pl < vpl)
                    pl = last;
            }
            lx = vl->x;
            ly = t;
            ldx = fix_div((*pl)->x - lx, ny - lyf);
            lx += fix_mul(ldx, FIX_CEIL(lyf) - lyf);
        }
        if (y == ry) {
            vr = *pr;
            ryf = vr->y;
            if (++pr > last)
                pr = vpl;
            for (;;) {
                ny = (*pr)->y;
                t = cint(ny);
                if (y < t)
                    break;
                if (y == t) {
                    ryf = ny;
                    vr = *pr;
                }
                if (++pr > last)
                    pr = vpl;
            }
            rx = vr->x;
            ry = t;
            rdx = fix_div((*pr)->x - rx, ny - ryf);
            rx += fix_mul(rdx, FIX_CEIL(ryf) - ryf);
        }
        lim = ly < ry ? ly : ry;
        do {
            out[0] = cint(lx);
            out[1] = cint(rx);
            out += 2;
            lx += ldx;
            y++;
            rx += rdx;
        } while (y < lim);
    } while (y < y_max);
    return buf;
}

/* fill the polygon with a colour (debug helper, unreferenced) */
void gri_per_spans_fill(int color, int n, grs_vertex **vpl)
{
    long *buf = gri_per_spans(vpl, n, NULL), *row, y, y_end;
    uchar *line, *d;
    ulong cnt, pat;

    if (buf == NULL)
        return;
    y = buf[0];
    line = grd_canvas->bm.bits + (ushort)grd_canvas->bm.row * y;
    y_end = buf[1];
    row = buf + 2;
    pat = (uchar)color * 0x01010101u;
    for (; y < y_end; y++, row += 2) {
        cnt = row[1] - row[0];
        d = line + row[0];
        {
            ulong k;
            for (k = cnt >> 2; k; k--, d += 4)
                *(ulong *)d = pat;
            for (k = cnt & 3; k; k--)
                *d++ = (uchar)pat;
        }
        line += (ushort)grd_canvas->bm.row;
    }
    temp_free(buf);
}

void gri_per_skew_spans(long *buf, fix slope)
{
    long *row = buf, *end = (long *)((char *)buf + ((buf[1] - buf[0]) << 3));
    long acc = buf[0] * slope, sh;

    for (row += 2; row <= end; row += 2) {
        sh = acc >> 16;
        row[0] -= sh;
        row[1] -= sh;
        acc += slope;
    }
}

/* ---------------------------------------------------------------- column lists
 * out[0] -> {x_min, x_max, 0}; out[1 .. W] = per column list heads of runs {y, y_end, next}
 * (W = x_max - x_min), then W tail pointers (link field to append to), then the runs. */
static void col_open(long **s, long y, long **prec)
{
    long *r = *prec;
    r[1] = -1;
    r[2] = 0;
    r[0] = y;
    **(long ***)s = r;
    *prec = r + 3;
}

static void col_close(long **s, long y)
{
    /* a slot closed twice in one pass (only after running out of space) follows a NULL
     * link and writes to low memory, as the original */
    char *r = (char *)**(long **)s + 8;
    *(long *)r = 0;
    *(long *)(r - 4) = y;
    *s = (long *)r;
}

long *gri_per_span_columns(long **pspans)
{
    long *sp = *pspans, *row, *last, *out, *rec, **tails, **A, **B, **L, **R, **s;
    long y0 = sp[0], y1 = sp[1], x_min, x_max, y, prev_xl, prev_xr, W;
    char *end;

    last = (long *)((char *)sp + 8 * y1 - 8 * y0);
    row = sp + 2;
    x_min = row[0];
    if (x_min > row[1])
        row[1] = x_min;
    x_max = row[1];
    for (row += 2; row <= last; row += 2) {
        if (x_min > row[0])
            x_min = row[0];
        if (row[1] < row[0])
            row[1] = row[0];
        if (x_max < row[1])
            x_max = row[1];
    }
    out = temp_malloc(SPAN_COLS_SIZE);
    if (out == NULL)
        return NULL;
    W = x_max - x_min;
    tails = (long **)((char *)out + 4 * x_max - 4 * x_min + 4);
    end = (char *)out + SPAN_COLS_SIZE;
    rec = (long *)((char *)tails + 4 * x_max - 4 * x_min);
    if ((ulong)((char *)rec + 0xC) > (ulong)end) {
        temp_free(out);
        return NULL;
    }
    {
        long *h = out + 1;
        for (s = tails; s < (long **)rec; s++, h++) {
            *h = 0;
            *s = h;
        }
    }
    (void)W;
    out[0] = (long)rec;
    rec[2] = 0;
    rec[0] = x_min;
    rec[1] = x_max;
    A = B = tails;
    row = sp + 2;
    prev_xl = prev_xr = x_min;
    y = y0;
    rec += 3;
    for (; y < y1; y++, row += 2) {
        L = (long **)((char *)tails + 4 * row[0] - 4 * x_min);
        R = (long **)((char *)tails + 4 * row[1] - 4 * x_min);
        if ((ulong)R > (ulong)A && (ulong)L < (ulong)B) {
            if ((ulong)L > (ulong)A) {
                for (s = A; s < L; s++)
                    col_close(s, y);
            } else {
                if ((ulong)((char *)rec + 12 * prev_xl - 12 * row[0]) > (ulong)end)
                    break;
                for (s = A - 1; (ulong)s >= (ulong)L; s--)
                    col_open(s, y, &rec);
            }
            if ((ulong)R > (ulong)B) {
                if ((ulong)((char *)rec + 12 * row[1] - 12 * prev_xr) > (ulong)end)
                    break;
                for (s = B; s < R; s++)
                    col_open(s, y, &rec);
            } else {
                for (s = B - 1; (ulong)s >= (ulong)R; s--)
                    col_close(s, y);
            }
        } else {
            if ((ulong)((char *)rec + 12 * row[1] - 12 * row[0]) > (ulong)end)
                break;
            for (s = A; s < B; s++)
                col_close(s, y);
            for (s = L; s < R; s++)
                col_open(s, y, &rec);
        }
        A = L;
        B = R;
        prev_xr = row[1];
        prev_xl = row[0];
    }
    for (s = A; s < B; s++)
        col_close(s, y);
    if (y == y1) {
        *pspans = NULL;
        return out;
    }
    if (y == y0) {
        temp_free(out);
        return NULL;
    }
    row -= 2;
    row[0] = y;
    row[1] = y1;
    *pspans = row;
    return out;
}

/* ---------------------------------------------------------------- addresses */
uchar *gri_per_haddr(int x, int y)
{
    return grd_canvas->bm.bits + (ushort)grd_canvas->bm.row * y + x;
}

uchar *gri_per_vaddr(int x, int y)
{
    return grd_canvas->bm.bits + x * (ushort)grd_canvas->bm.row + y;
}

/* ---------------------------------------------------------------- scan */
static fix clamp_hi(fix a, int lim)
{
    if ((a >> 16) >= lim)
        a = ((lim - 1) << 16) | 0xFFFF;
    return a;
}

void gri_per_scan(grs_bitmap *bm, long *seg, per_ps *ps)
{
    fix slope = ps->scan_slope, a, b, c, d, m1, m2, z, z2, U2, V2, dU, dV, t;
    long si = slope >> 16, si1 = si + 1, x, x_first, x_end, x_last, len, e, dy, dx;
    ulong sfrac = (ulong)slope << 16, colfix, frac;
    long *rec, *lst, pos;
    long scan[2];
    uchar edge;
    int w = bm->w, h = bm->h;

    if (slope < 0) {
        a = ps->wux - ps->wuy;
        b = ps->wvx - ps->wvy;
    } else if (slope == 0x10000) {
        a = ps->wux + ps->wuy;
        b = ps->wvx + ps->wvy;
    } else {
        a = ps->wux;
        b = ps->wvx;
    }
    c = a + ps->wuy;
    d = b + ps->wvy;
    m1 = iabs(a);
    if (iabs(c) > m1)
        m1 = iabs(c);
    m2 = iabs(b);
    if (iabs(d) > m2)
        m2 = iabs(d);
    rec = (long *)seg[0];
    x_first = x = rec[0];
    x_end = rec[1];
    if (x >= x_end)
        return;
    x_last = x_end - 1;
    colfix = (ulong)x << 16;
    do {
        rec = (long *)*++seg;
        if (rec == NULL)
            goto next;
        for (lst = rec; lst[2]; lst = (long *)lst[2])
            ;
        len = lst[1] - rec[0];
        if (len > 0) {
            edge = x == x_first || x == x_last;
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
                if (ps->u < 0)              /* sic: tests u */
                    ps->v = 0;
                U2 = clamp_hi(U2, w);
                if (U2 < 0)
                    U2 = 0;
                V2 = clamp_hi(V2, h);
                if (V2 < 0)
                    V2 = 0;
            }
            dU = U2 - ps->u;
            dV = V2 - ps->v;
#ifndef TN_WATCOM_ABI
            {
                /* P129 in docs/BUGS.md: the plane terms are kept under 0x200000 so that a run
                 * of up to 1024 pixels fits in 32 bits; a run of the sharp 3D view is up to 6
                 * times longer, the product wrapped, and the u, v steps of the long runs went
                 * wrong (texture smeared from a straight line across a near roof or floor).
                 * The same sums in 64 bits. */
                long long tt = (long long)ps->wux * len + (long long)ps->wuy * e;
                if ((tt < 0 ? -tt : tt) > m1 && !edge) {
                    ps->du_lo = (long)(((long long)a * dU) / tt);
                    ps->du_hi = (long)(((long long)c * dU) / tt);
                } else
                    ps->du_hi = ps->du_lo = dU / len;
                tt = (long long)ps->wvx * len + (long long)ps->wvy * e;
                if ((tt < 0 ? -tt : tt) > m2 && !edge) {
                    ps->dv_lo = (long)(((long long)b * dV) / tt);
                    ps->dv_hi = (long)(((long long)d * dV) / tt);
                } else
                    ps->dv_hi = ps->dv_lo = dV / len;
                (void)t;
            }
#else
            t = ps->wux * len + ps->wuy * e;
            if (iabs(t) > m1 && !edge) {
                ps->du_lo = (long)(((long long)a * dU) / t);
                ps->du_hi = (long)(((long long)c * dU) / t);
            } else
                ps->du_hi = ps->du_lo = dU / len;
            t = ps->wvx * len + ps->wvy * e;
            if (iabs(t) > m2 && !edge) {
                ps->dv_lo = (long)(((long long)b * dV) / t);
                ps->dv_hi = (long)(((long long)d * dV) / t);
            } else
                ps->dv_hi = ps->dv_lo = dV / len;
#endif
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
            }
            ps->y = (pos << 16) + (frac >> 16);
        }
next:
        colfix += 0x10000;
        x++;
    } while (x < x_end);
}

/* ---------------------------------------------------------------- shells */
static void scan_all(grs_bitmap *bm, long *spans, per_ps *ps)
{
    long *cur = spans, *seg;

    gri_per_skew_spans(cur, ps->scan_slope);
    while (cur) {
        seg = gri_per_span_columns(&cur);
        if (seg == NULL)
            break;
        gri_per_scan(bm, seg, ps);
        temp_free(seg);
    }
    temp_free(spans);
}

void gri_per_vscan(grs_bitmap *bm, int n, grs_vertex **vpl, per_ps *ps)
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
    if (spans)
        scan_all(bm, spans, ps);
}

void gri_per_hscan(grs_bitmap *bm, int n, grs_vertex **vpl, per_ps *ps)
{
    long *spans;

    gri_per_check_uv(bm, n, vpl);
    gri_per_swap_xy(n, vpl);
    spans = gri_per_spans(vpl, n, NULL);
    if (spans == NULL)
        return;
    gri_per_swap_xy(n, vpl);
    scan_all(bm, spans, ps);
}
