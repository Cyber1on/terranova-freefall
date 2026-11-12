/* genel.c -- ellipse span generator and the ellipse, oval and ring drawers (Terra Nova v1.09).
 * Decompiled from __FF.EXE.  Rewritten in TN; System Shock's gen/genel.c, gen/genov.c
 * (gr_int_elipse / gr_int_oval: Foley-van Dam with fix24) work differently.
 *
 *   0x314C60 gri_elipse_spans (name ours)
 *   0x34ACC0 gen_fix_elipse   0x34ADF8 gen_int_elipse (name ours, unreferenced)
 *   0x34AE20 gen_fix_oval     0x34AE6C gen_int_oval   (name ours, unreferenced)
 *   0x2F3D90 gen_fix_ring     0x2F3F9C gen_int_ring   (names ours; int version unreferenced)
 *
 * gri_elipse_spans(x0, y0, a, b) (fix centre and radii) returns a temp_malloc'd array
 *   long { ytop, ybot, then (ybot - ytop) pairs {xleft, xright} }, rows ytop..ybot-1, pixel
 *   x inclusive, or NULL if a or b < 1.0, the ellipse covers no row or the allocation fails.
 *   It steps an implicit function e = u^2 + v^2 - 1 (u = dx/a, v = dy/b, fix 3.29) with
 *   incremental differences, first downwards along the right edge, then back up along the
 *   left edge.  64-bit multiply/divide sequences as in the original (a quotient overflow
 *   faults there; here it wraps).
 * The int versions take short centre and radii: centre fix = ((c-1) << 16) | 0x8000,
 *   radius fix = r << 16.
 * gen_fix_elipse draws the outline as horizontal pieces (the first and last row whole, the
 *   others from the edge to the neighbour rows' edge, or one pixel); gen_fix_oval fills it;
 *   gen_fix_ring(x0, y0, r1, r2) fills between two circles (radii scaled by grd_cap->w / 320,
 *   y radius divided by grd_cap->aspect).  All return CLIP_ALL if there are no spans, else 0.
 */
#include "2d.h"

static long mul29(long a, long b) { return (long)(((long long)a * b) >> 29); }
static long div29(long a, long b) { return (long)(((long long)a << 29) / b); }

long *gri_elipse_spans(fix x0, fix y0, fix a, fix b)
{
    long ix, yt, yb, dx, dy, u, v, e, e2;
    long ddx, dddx, ddy, dddy, *buf, *p, *end;

    if (a < 0x10000)
        return NULL;
    if (b < 0x10000)
        return NULL;
    ix = (x0 + 0x8000) >> 16;
    yt = (y0 - b) >> 16;
    dx = (ix << 16) - x0;
    dy = (yt << 16) - y0;
    u = -div29(dx, a);
    v = -div29(dy, b);
    e = mul29(u, u) + mul29(v, v);
    ddx = 2 * fix_div(div29(dx + 0x8000, a), a);
    dddx = fix_div(div29(0x20000, a), a);
    ddy = 2 * fix_div(div29(dy + 0x8000, b), b);
    dddy = fix_div(div29(0x20000, b), b);
    e -= 0x20000000;
    while (e >= 0) {            /* first row inside */
        e += ddy;
        ddy += dddy;
        yt++;
    }
    /* last row: the original runs this as a do-while (e < 0 here), on a copy of ddy */
    e2 = e;
    yb = yt;
    {
        long d = ddy;
        do {
            e2 += d;
            d += dddy;
            yb++;
        } while (e2 < 0);
    }
    if (yt >= yb)
        return NULL;
    buf = temp_malloc((yb - yt) * 8 + 8);
    if (buf == NULL)
        return NULL;
    buf[0] = yt;
    buf[1] = yb;
    p = buf + 2;
    end = buf + 2 * buf[1] - 2 * buf[0];     /* last row */
    /* right edge, upper part: x grows */
    while (ddy < dddy) {
        while (e < 0) {
            ix++;
            e += ddx;
            ddx += dddx;
        }
        p[1] = ix - 1;
        e += ddy;
        ddy += dddy;
        p += 2;
    }
    if (p <= end) {
        /* right edge, lower part: x shrinks, down to the last row */
        for (;;) {
            while (e >= 0) {
                ddx -= dddx;
                ix--;
                e -= ddx;
            }
            p[1] = ix;
            if (p == end)
                break;
            e += ddy;
            p += 2;
            ddy += dddy;
        }
    } else {
        do {                    /* overshot the last row: step back */
            ddy -= dddy;
            e -= ddy;
            p -= 2;
            while (e >= 0) {
                ddx -= dddx;
                e -= ddx;
                ix--;
            }
        } while (p > end);
    }
    /* left edge, lower part, going up */
    while (0 < ddy) {
        while (e < 0) {
            ddx -= dddx;
            ix--;
            e -= ddx;
        }
        p[0] = ix + 1;
        ddy -= dddy;
        p -= 2;
        e -= ddy;
    }
    /* left edge, upper part */
    while (p > buf) {
        while (e >= 0) {
            ix++;
            e += ddx;
            ddx += dddx;
        }
        p -= 2;
        ddy -= dddy;
        p[2] = ix;
        e -= ddy;
    }
    return buf;
}

int gen_fix_elipse(fix x0, fix y0, fix a, fix b)
{
    long *spans = gri_elipse_spans(x0, y0, a, b);
    long *p, *end, y, prev, cur, next, m;

    if (spans == NULL)
        return CLIP_ALL;
    p = spans + 2;
    y = spans[0];
    end = p + 2 * spans[1] - 2 * spans[0];
    gen_hline(p[0], y, p[1]);           /* first row whole */
    p += 2;
    y++;
    /* left side */
    prev = p[-2];
    cur = 0;            /* the original leaves it unset when there is a single row */
    if (p < end)
        cur = p[0];
    for (; p + 2 < end; p += 2, y++) {
        next = p[2];
        m = prev >= next ? prev : next;
        if (cur < m)
            gen_hline(cur, y, m - 1);
        else
            gen_fill_pixel(grd_canvas->gc.fcolor, cur, y);
        prev = cur;
        cur = next;
    }
    if (p < end)
        gen_hline(p[0], y, p[1]);       /* last row whole */
    /* right side */
    p = spans + 2;
    y = spans[0];
    prev = p[1];
    p += 2;
    y++;
    if (p < end)
        cur = p[1];
    for (; p + 2 < end; p += 2, y++) {
        next = p[3];
        m = prev <= next ? prev : next;
        if (cur > m)
            gen_hline(m + 1, y, cur);
        else
            gen_fill_pixel(grd_canvas->gc.fcolor, cur, y);
        prev = cur;
        cur = next;
    }
    temp_free(spans);
    return CLIP_NONE;
}

int gen_int_elipse(short x0, short y0, short a, short b)
{
    return gen_fix_elipse(((x0 - 1) << 16) | 0x8000, ((y0 - 1) << 16) | 0x8000, a << 16, b << 16);
}

int gen_fix_oval(fix x0, fix y0, fix a, fix b)
{
    long *spans = gri_elipse_spans(x0, y0, a, b);
    long *p, y, yb;

    if (spans == NULL)
        return CLIP_ALL;
    y = spans[0];
    yb = spans[1];
    for (p = spans + 2; y < yb; p += 2, y++)
        gen_hline(p[0], y, p[1]);
    temp_free(spans);
    return CLIP_NONE;
}

int gen_int_oval(short x0, short y0, short a, short b)
{
    return gen_fix_oval(((x0 - 1) << 16) | 0x8000, ((y0 - 1) << 16) | 0x8000, a << 16, b << 16);
}

int gen_fix_ring(fix x0, fix y0, fix r1, fix r2)
{
    fix ro = r1, ri = r2, scale, rio, rii;
    long *so, *si, *p, *q, y, ybo, ybi, curl, curr, prevl, prevr, nl, nr, l, r;

    if (r1 < r2) {
        ro = r2;
        ri = r1;
    }
    scale = fix_make(grd_cap->w, 0) / 320;
    ro = fix_mul(ro, scale);
    ri = fix_mul(ri, scale);
    rio = fix_div(ro, grd_cap->aspect);
    rii = fix_div(ri, grd_cap->aspect);
    so = gri_elipse_spans(x0, y0, ro, rio);
    if (so == NULL)
        return CLIP_ALL;
    y = so[0];
    ybo = so[1];
    p = so + 2;
    si = gri_elipse_spans(x0, y0, ri, rii);
    if (si != NULL) {
        q = si + 2;
        ybi = si[1];
        for (; y < si[0]; y++, p += 2)          /* rows above the hole */
            gen_hline(p[0], y, p[1]);
        curl = p[0];
        curr = p[1];
        if (p == so + 2) {
            prevl = curr + 1;
            prevr = curl - 1;
        } else {
            prevl = p[-2];
            prevr = p[-1];
        }
        if (ybi == ybo)
            ybi--;
        for (; y < ybi; y++, p += 2, q += 2) {
            nl = p[2];
            nr = p[3];
            l = q[0];
            r = q[1];
            if (l < nl)
                l = nl - 1;
            if (l < prevl)
                l = prevl - 1;
            if (r > nr)
                r = nr + 1;
            if (r > prevr)
                r = prevr + 1;
            if (r <= l) {
                gen_hline(p[0], y, p[1]);
            } else {
                gen_hline(p[0], y, l);
                gen_hline(r, y, p[1]);
            }
            prevl = curl;
            prevr = curr;
            curl = nl;
            curr = nr;
        }
        temp_free(si);
    }
    for (; y < ybo; y++, p += 2)
        gen_hline(p[0], y, p[1]);
    temp_free(so);
    return CLIP_NONE;
}

int gen_int_ring(short x0, short y0, short r1, short r2)
{
    return gen_fix_ring(((x0 - 1) << 16) | 0x8000, ((y0 - 1) << 16) | 0x8000, r1 << 16, r2 << 16);
}
