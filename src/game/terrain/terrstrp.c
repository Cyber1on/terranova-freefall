/* terrstrp.c -- Terra Nova module M083 (0x2A8880-0x2AB0FF; source file name unknown, the map
 * calls 0x2AA398 TerrainStripBuild): the terrain strip builder.  Decompiled from __FF.EXE v1.09.
 *
 *   0x2A8880 terrstrip_2A8880 lowest height around a cell   0x2A88FC terrstrip_2A88FC highest
 *   0x2A8978 terrstrip_2A8978 set up the strip record       0x2A8DCC terrstrip_2A8DCC (no callers)
 *   0x2A8FA0 terrstrip_2A8FA0 one edge of the view cone      0x2A98AC terrstrip_2A98AC span tables
 *   0x2AA2B4 terrstrip_2AA2B4 add a strip (map square)       0x2AA398 TerrainStripBuild
 *   0x2AB0C0 terrstrip_2AB0C0 (no callers)                    0x2AB0D4 terrstrip_2AB0D4 strip pool
 *   0x2AB0E8 terrstrip_2AB0E8 free the pool
 *
 * TerrainStripBuild walks the map squares inside the view cone in rings around the viewer (ring
 * i = squares at distance i along the heading's octant, up to the camera's radius) and emits
 * one TerrBin per square into the pool at strip_427DE0 (two bins below the pool start are the
 * viewer's own).  The cone's two edges (terrstrip_2A8FA0, per octant direction) give, per ring,
 * the first / last square of three runs (TsSpan tables A, B, C); the squares come from the
 * per-ring pattern tables tcache_tab (pairs of first / last).  terrrast_2AE004 then rasterises
 * the bins.
 *
 * Quirks kept:
 *  - terrstrip_2A8880 / 2A88FC compare the cell coordinates with 3 and 0xFD and ignore the
 *    result (an emptied bounds check).
 *  - terrstrip_2A8FA0 returns the ring count in the low word with the high word of the record
 *    pointer above it (callers use the word); it divides by the far edge without checking for 0.
 *  - TerrainStripBuild with a radius of 0 takes the failure path with ring 1 (the record's
 *    counts become 1).
 *  - terrstrip_2AB0E8 frees the pool at strip_427DE0 - 0x20, not at the allocation (- 0x40).
 */
#include "g5_c.h"
#include "g1_b.h"

#define CELL(x, y)  ((uchar *)TN_LOWMEM(GMAP->rows1[y]) + (x) * 3)

#ifndef TN_WATCOM_ABI
#include "mapedge.h"
/* the sharp 3D view in widescreen (port/video/sharp3d.c): the terrain's cone as wide as the picture,
 * as far as the three 45 degree sectors the builder walks reach (port_ts_room).  The game's
 * terrstrip_2A8978 becomes ts_setup, run with the cone's angle in ts_fov (0: the camera's) */
static fixang ts_fov;
#define TS_FOV(cam) (ts_fov ? ts_fov : (cam)->fov_x)
#define TS_HI 0                         /* the edge (d, e) on the side of higher headings */
#define terrstrip_2A8978 ts_setup
void ts_setup(FfCamera *cam, FfView *view, TsRec *r);

/* one-pass widescreen (TerrainStripBuild at the end): the squares of the turned sectors are
 * added to the main ones.  ts_mark: squares and vertices are marked (generation ts_gen) as
 * they are emitted, with each square's ring; ts_extra: a turned build skips the squares and
 * vertices the main one made and adds its own after them */
static int ts_mark, ts_extra;
static ushort ts_gen;
static long ts_ring;
static ushort ts_have[PF_W][PF_W];     /* [y + PF_M][x + PF_M]: the land around the map too */
static ushort ts_ringof[PORT_S3D_MUL_MAX * 0x3EC0 / 0x20];
ushort port_tr_mark[0x10000];           /* terrrast.c: generation of a cell's vertex */
int port_tr_mark_on, port_tr_skip;      /* mark the vertices / keep the marked ones' numbers */
ushort port_tr_gen;
long port_tr_n;                         /* vertices emitted so far */
/* the squares given polygons in this drawing (generation port_poly_gen): "Terrain smoothing"
 * smooths the nearer rings' column spans where no polygon covers them (haze.c) */
ushort port_poly_have[PF_W][PF_W];      /* [y + PF_M][x + PF_M] */
ushort port_poly_gen;
#define TerrainStripBuild ts_build
#endif

long terrstrip_2A8880(long x, long y)
{
    uchar *c = CELL(x, y);
    long h = MAP_CELL_H(c), k;
    for (k = 0; k < 8; k++) {
        long v = MAP_CELL_H(c + ts_nbr[k] * 3);
        if (v < h)
            h = v;
    }
    return h;
}

long terrstrip_2A88FC(long x, long y)
{
    uchar *c = CELL(x, y);
    long h = MAP_CELL_H(c), k;
    for (k = 0; k < 8; k++) {
        long v = MAP_CELL_H(c + ts_nbr[k] * 3);
        if (v > h)
            h = v;
    }
    return h;
}

void terrstrip_2A8978(FfCamera *cam, FfView *view, TsRec *r)
{
    long x, y, rr;
    ushort pitch = view->pitch, head = view->heading;
    fix d, sin_a, cos_a, tan_a, cot_a, s10, c0c, tfov, sin_b, cos_b, tan_p, k18, k28, k2c, k30,
        k38, q, a18, a1c, a20, a24, m;
    fixang ang;
#ifndef TN_WATCOM_ABI
    FfView tv;
    if (port_s3d_on && port_s3d_ts_turn) {     /* widescreen: a side's sectors (port/video/sharp3d.c) */
        tv = *view;
        tv.heading = (ushort)(tv.heading + port_s3d_ts_turn);
        fix_sincos(tv.heading, &tv.sin_h, &tv.cos_h);
        view = &tv;
        head = tv.heading;
    }
#endif

    r->rad = cam->band[cam->band0 - 1].b;
    x = ((GMAP->x0 << 16) + (view->x >> 2) + 0x8000) >> 16;
    y = ((GMAP->y0 << 16) + (view->y >> 2) + 0x8000) >> 16;
    if (pitch < 0x8000) {
        d = ((long)terrstrip_2A8880(x, y) << 16) / 24 - view->z;
        ang = cam->fov_y + pitch;
    } else {
        d = ((long)terrstrip_2A88FC(x, y) << 16) / 24 - view->z;
        if (d <= 0x10000)
            d = 0x10000;
        ang = pitch - cam->fov_y;
    }
    fix_sincos(ang, &sin_a, &cos_a);
    tan_a = fix_div(sin_a, cos_a);
    cot_a = fix_div(cos_a, sin_a);
#ifdef TN_WATCOM_ABI
    fix_sincos(cam->fov_x, &s10, &c0c);
#else
    fix_sincos(TS_FOV(cam), &s10, &c0c);
#endif
    tfov = fix_mul(fix_div(s10, c0c), view->cos_p);
    fix_sincos(fix_atan2(tfov, 0x10000), &sin_b, &cos_b);
    tan_p = fix_div(view->sin_p, view->cos_p);
    k18 = fix_mul(tan_p, tan_a) + 0x10000;
    k30 = k28 = fix_mul(tfov, k18);
    k38 = -fix_mul(d, cot_a);
    k2c = fix_mul(cos_b, fix_mul(k38, fix_mul(tan_p, tan_a)));
    q = fix_mul(k2c, tfov);
    rr = ((r->rad * r->rad) << 16) - fix_mul(q, q);
    if (rr > 0)
        r->f2a = k2c + fix_sqrt(rr) - (long)((long long)k38 * k30 / sin_b);
    else
        r->f2a = (fix)0xFFFF0000;
    r->px = view->x & 0xFFFF;
    r->py = view->y & 0xFFFF;
    m = fix_mul(k38, k28);
    a18 = fix_mul(view->cos_h, k38);
    a1c = fix_mul(-view->sin_h, m);
    a20 = fix_mul(view->sin_h, k38);
    q = fix_mul(view->cos_h, m);
    r->d[0][0] = a18 + a1c;
    r->d[0][1] = a20 + q;
    r->d[1][0] = a18 - a1c;
    r->d[1][1] = a20 - q;
#ifdef TN_WATCOM_ABI
    r->fov = cam->fov_x;
#else
    r->fov = TS_FOV(cam);
#endif
    m = fix_mul(view->cos_h, cos_b);
    a24 = fix_mul(-view->sin_h, sin_b);
    a18 = fix_mul(view->sin_h, cos_b);
    q = fix_mul(view->cos_h, sin_b);
    r->e[0][0] = a24 + m;
    r->e[1][0] = m - a24;
    r->e[0][1] = a18 + q;
    r->e[1][1] = a18 - q;
    if (head > 0xE000) {
        r->oct[0] = 0x201; r->oct[1] = 0x100; r->oct[2] = 0x8007;
    } else if (head <= 0x2000) {
        r->oct[0] = 0x100; r->oct[1] = 0x201; r->oct[2] = 0x402;
    } else if (head <= 0x6000) {
        if (head >= 0x4000) {
            r->oct[0] = 0x402; r->oct[1] = 0x803; r->oct[2] = 0x1004;
        } else {
            r->oct[0] = 0x803; r->oct[1] = 0x402; r->oct[2] = 0x201;
        }
    } else if (head <= 0xA000) {
        if (head >= 0x8000) {
            r->oct[0] = 0x1004; r->oct[1] = 0x2005; r->oct[2] = 0x4006;
        } else {
            r->oct[0] = 0x2005; r->oct[1] = 0x1004; r->oct[2] = 0x803;
        }
    } else if (head >= 0xC000) {
        r->oct[0] = 0x4006; r->oct[1] = 0x8007; r->oct[2] = 0x100;
    } else {
        r->oct[0] = 0x8007; r->oct[1] = 0x4006; r->oct[2] = 0x2005;
    }
}

#ifndef TN_WATCOM_ABI
#undef terrstrip_2A8978
/* How far the builder's sectors reach past heading head, to the side of lower headings (side
 * < 0) or higher ones.  For a heading in sector s (the if-chain at the end of ts_setup) they
 * are sectors s - 1 .. s + 1: 45 degrees past the edges of the heading's own sector. */
int port_ts_room(unsigned head, int side)
{
    long h = head & 0xFFFF, s, t;
    if (h > 0xE000)
        s = 7;
    else if (h <= 0x2000)
        s = 0;
    else if (h <= 0x6000)
        s = h >= 0x4000 ? 2 : 1;
    else if (h <= 0xA000)
        s = h >= 0x8000 ? 4 : 3;
    else
        s = h >= 0xC000 ? 6 : 5;
    t = h - s * 0x2000;
    return (int)(side < 0 ? 0x2000 + t : 0x4000 - t);
}

/* the game's set-up; in the wide sharp view the cone reaches on each side as far as the wide
 * picture or the sectors do, whichever is less (two set-ups, one edge taken from each) */
void terrstrip_2A8978(FfCamera *cam, FfView *view, TsRec *r)
{
    unsigned head;
    long lo, hi;
    TsRec r2;
    if (!port_s3d_on || !port_s3d_fov_x) {
        ts_fov = 0;
        ts_setup(cam, view, r);
        return;
    }
    head = (unsigned)(view->heading + port_s3d_ts_turn) & 0xFFFF;
    lo = port_ts_room(head, -1);
    hi = port_ts_room(head, 1);
    if (lo > port_s3d_fov_x)
        lo = port_s3d_fov_x;
    if (hi > port_s3d_fov_x)
        hi = port_s3d_fov_x;
    if (lo < cam->fov_x)
        lo = cam->fov_x;
    if (hi < cam->fov_x)
        hi = cam->fov_x;
    ts_fov = (fixang)lo;
    ts_setup(cam, view, r);
    if (hi != lo) {
        r2 = *r;
        ts_fov = (fixang)hi;
        ts_setup(cam, view, &r2);
        r->d[TS_HI][0] = r2.d[TS_HI][0];
        r->d[TS_HI][1] = r2.d[TS_HI][1];
        r->e[TS_HI][0] = r2.e[TS_HI][0];
        r->e[TS_HI][1] = r2.e[TS_HI][1];
        if (r2.f2a > r->f2a)
            r->f2a = r2.f2a;
        r->fov = r2.fov > r->fov ? r2.fov : r->fov;
    }
    ts_fov = 0;
}
#endif

uchar terrstrip_2A8DCC(TsIter *it)
{
    uchar *p = it->cur, v;
    ushort k = it->k;
    if (p == NULL) {
        if (!it->back)
            p = it->path->data + (k >> 1);
        else if (k == 0)
            p = it->path->data + it->path->count;
        else
            p = it->path->data + ((long)(k - 1) >> 1);
    }
    if (!it->back) {
        ushort n = k + 1;
        if (k & 1) {
            v = (uchar)(*p << 1) >> 5;
            it->x += (short)ts_dirx[v];
            it->y += (short)ts_diry[v];
            if (n == it->path->count * 2) {
                p = it->path->data;
                n = 0;
            } else {
                p++;
            }
        } else {
            v = *p & 7;
            it->x += (short)ts_dirx[v];
            it->y += (short)ts_diry[v];
        }
        it->k = n;
        it->cur = p;
        return v;
    }
    if (k & 1) {
        v = *p & 7;
        it->x -= (short)ts_dirx[v];
        it->y -= (short)ts_diry[v];
        if ((ushort)(k - 1) == 0) {
            it->k = it->path->count * 2;
            it->cur = it->path->data + it->path->count;
        } else {
            it->k = k - 1;
            it->cur = p - 1;
        }
        return v;
    }
    v = (uchar)(*p << 1) >> 5;
    it->x -= (short)ts_dirx[v];
    it->y -= (short)ts_diry[v];
    it->k = k - 1;
    it->cur = p;
    return v;
}

/* the two ways an edge bounds the squares of a ring: from above (the largest value, rounded
 * up, minus one; the walk stops below 0) or from below (the smallest, rounded down; it stops
 * at 1 or more) */
static void put(long *o, fix *prev, fix a, long up)
{
    if (up) {
        if (*prev <= a)
            *prev = a;
        *o = (short)((*prev + 0xFFFF) >> 16) - 1;
    } else {
        if (*prev >= a)
            *prev = a;
        *o = *prev >> 16;
    }
}

static long stop(fix a, long up)
{
    return up ? a < 0 : a >= 0x10000;
}

static void bound(fix *c, fix v, long up)
{
    if (up ? *c <= v : *c >= v)
        *c = v;
}

static fix clampslope(fix s)
{
    if (s > 0x1F40000)
        return 0x1F40000;
    if (s < -0x1F40000)
        return -0x1F40000;
    return s;
}

#define SLOPE_OK(s) ((s) == 0 || ((s) <= 0x1F40000 && (s) >= -0x1F40000))

ushort terrstrip_2A8FA0(long *out, TsRec *r, uchar dir, uchar side, uchar flag)
{
    fix DX = r->d[side != 0][0], DY = r->d[side != 0][1];
    fix EX = r->e[side != 0][0], EY = r->e[side != 0][1];
    fix X = r->px + DX, Y = r->py + DY, c, a, s = 0, prev, t;
    long sx = X >> 16, sy = Y >> 16, i, up;
    long *p;

    switch (dir) {
    case 2:                                     /* walk along +x */
        up = side == 0;
        c = r->py;
        if (sx > 0) {
            s = fix_div(DY, DX);
            a = fix_mul(0x10000 - r->px, s) + c;
        } else {
            if (EX == 0)
                goto clamp2;
            s = fix_div(EY, EX);
            if (!SLOPE_OK(s))
                goto clamp2;
            if (sx < 0) {
                c = Y + fix_mul(-X, s);
                a = c + s;
            } else {
                bound(&c, Y, up);
                a = fix_mul(0x10000 - X, s) + Y;
            }
            goto first2;
        clamp2:
            s = 0;
            a = up ? 0x1F40000 : -0x1F40000;
        }
    first2:
        put(out, &c, a, up);
        if (flag && stop(a, up))
            return 1;
        prev = a;
        if ((ulong)r->rad <= 1)
            break;
        p = out + 1;
        t = 0x20000 - X;
        for (i = 1; ; ) {
            if (i == sx) {
                s = fix_div(EY, EX);
                a = fix_mul(t, s) + Y;
                bound(&prev, Y, up);
            } else {
                a += s;
            }
            put(p, &prev, a, up);
            if (flag && stop(a, up))
                return i + 1;
            p++;
            prev = a;
            i++;
            t += 0x10000;
            if ((ulong)i >= (ulong)r->rad)
                break;
        }
        break;
    case 4:                                     /* walk along +y */
        up = side != 0;
        c = r->px;
        if (sy > 0) {
            s = fix_div(DX, DY);
            a = fix_mul(0x10000 - r->py, s) + c;
        } else {
            if (EY == 0)
                goto clamp4;
            s = fix_div(EX, EY);
            if (!SLOPE_OK(s))
                goto clamp4;
            if (sy < 0) {
                c = X + fix_mul(-Y, s);
                a = c + s;
            } else {
                bound(&c, X, up);
                a = fix_mul(0x10000 - Y, s) + X;
            }
            goto first4;
        clamp4:
            s = 0;
            a = up ? 0x1F40000 : -0x1F40000;
        }
    first4:
        put(out, &c, a, up);
        if (flag && stop(a, up))
            return 1;
        prev = a;
        if ((ulong)r->rad <= 1)
            break;
        p = out + 1;
        t = 0x20000 - Y;
        for (i = 1; ; ) {
            if (i == sy) {
                s = fix_div(EX, EY);
                a = fix_mul(t, s) + X;
                bound(&prev, X, up);
            } else {
                a += s;
            }
            put(p, &prev, a, up);
            if (flag && stop(a, up))
                return i + 1;
            t += 0x10000;
            prev = a;
            i++;
            p++;
            if ((ulong)i >= (ulong)r->rad)
                break;
        }
        break;
    case 6:                                     /* walk along -x */
        up = side != 0;
        c = r->py;
        if (sx < 0) {
            s = clampslope(fix_div(DY, DX));
            a = c - fix_mul(r->px, s);
        } else {
            if (EX == 0)
                goto clamp6;
            s = fix_div(EY, EX);
            if (!SLOPE_OK(s))
                goto clamp6;
            a = Y - fix_mul(X, s);
            goto first6;
        clamp6:
            s = 0;
            a = up ? 0x1F40000 : -0x1F40000;
        }
    first6:
        put(out, &c, a, up);
        if (flag && stop(a, up))
            return 1;
        prev = a;
        if ((ulong)r->rad <= 1)
            break;
        t = X + 0x10000;
        p = out + 1;
        for (i = 1; ; ) {
            if (i == -sx) {
                s = clampslope(fix_div(EY, EX));
                a = Y - fix_mul(t, s);
                bound(&prev, Y, up);
            } else {
                a -= s;
            }
            put(p, &prev, a, up);
            if (flag && stop(a, up))
                return i + 1;
            t += 0x10000;
            prev = a;
            i++;
            p++;
            if ((ulong)i >= (ulong)r->rad)
                break;
        }
        break;
    case 0:                                     /* walk along -y */
        up = side == 0;
        c = r->px;
        if (sy < 0) {
            s = clampslope(fix_div(DX, DY));
            a = c - fix_mul(r->py, s);
        } else {
            if (EY == 0)
                goto clamp0;
            s = fix_div(EX, EY);
            if (!SLOPE_OK(s))
                goto clamp0;
            a = X - fix_mul(Y, s);
            goto first0;
        clamp0:
            s = 0;
            a = up ? 0x1F40000 : -0x1F40000;
        }
    first0:
        put(out, &c, a, up);
        if (flag && stop(a, up))
            return 1;
        prev = a;
        if ((ulong)r->rad <= 1)
            break;
        t = Y + 0x10000;
        p = out + 1;
        for (i = 1; ; ) {
            if (i == -sy) {
                s = clampslope(fix_div(EX, EY));
                a = X - fix_mul(t, s);
                bound(&prev, X, up);
            } else {
                a -= s;
            }
            put(p, &prev, a, up);
            if (flag && stop(a, up))
                return i + 1;
            t += 0x10000;
            prev = a;
            i++;
            p++;
            if ((ulong)i >= (ulong)r->rad)
                break;
        }
        break;
    }
    return r->rad;
}

/* the span tables of one octant: the near edge (dir1 on side s1, then the other side) and the
 * runs of the far edge (dir3 on side s3) */
static void spans(TsRec *r, long *tmp, TsSpan *A, TsSpan *B, TsSpan *C, uchar *found,
                  uchar d1, uchar s1, uchar d3, uchar s3, long pos, long cfirst)
{
    ulong n = r->rad, i, cnt;
    terrstrip_2A8FA0(tmp, r, d1, s1, 0);
    for (i = 0; i < n; i++) {
        if (pos) {
            B[i].first = 1;
            if (tmp[i] > 0) {
                if (!*found) {
                    r->n34 = i;
                    *found = 1;
                }
                A[i].first = tmp[i];
                B[i].last = 1;
            } else {
                A[i].first = 0;
                B[i].last = 1 - tmp[i];
            }
        } else {
            B[i].first = 0;
            if (tmp[i] < 0) {
                if (!*found && i > 0) {
                    r->n34 = i;
                    *found = 1;
                }
                A[i].first = -tmp[i];
                B[i].last = 0;
            } else {
                A[i].first = 1;
                B[i].last = tmp[i] + 1;
            }
        }
    }
    terrstrip_2A8FA0(tmp, r, d1, !s1, 0);
    for (i = 0; i < n; i++)
        A[i].last = pos ? tmp[i] + 1 : 1 - tmp[i];
    cnt = terrstrip_2A8FA0(tmp, r, d3, s3, 1);
    r->n38 = cnt;
    if (cnt == 0)
        return;
    i = 0;
    do {
        C[i].first = cfirst;
        C[i].last = cfirst ? 1 - tmp[i] : tmp[i] + 1;
        i++;
    } while (i < cnt);
}

void terrstrip_2A98AC(TsRec *r, TsSpan *A, TsSpan *B, TsSpan *C)
{
    uchar found = 0;
    long *tmp = ffmem_2C4F40(r->rad * 4);
    switch (r->oct[0]) {
    case 0x100:  spans(r, tmp, A, B, C, &found, 2, 1, 4, 0, 1, 1); break;
    case 0x201:  spans(r, tmp, A, B, C, &found, 2, 0, 0, 1, 0, 1); break;
    case 0x402:  spans(r, tmp, A, B, C, &found, 4, 1, 6, 0, 0, 1); break;
    case 0x803:  spans(r, tmp, A, B, C, &found, 4, 0, 2, 1, 1, 1); break;
    case 0x1004: spans(r, tmp, A, B, C, &found, 6, 1, 0, 0, 0, 0); break;
    case 0x2005: spans(r, tmp, A, B, C, &found, 6, 0, 4, 1, 1, 0); break;
    case 0x4006: spans(r, tmp, A, B, C, &found, 0, 1, 2, 0, 1, 0); break;
    case 0x8007: spans(r, tmp, A, B, C, &found, 0, 0, 6, 1, 0, 0); break;
    }
    if (!found)
        r->n34 = r->rad;
    ffmem_2C4FE4(tmp);
}

uchar terrstrip_2AA2B4(TsCtx *t)
{
    TerrBin *b;
    long x = t->x, y = t->y;
#ifndef TN_WATCOM_ABI
    /* G34 / G35: the squares past the map's edge too, on the land around it (port/video/mapedge.c);
     * the game let the squares at 512 through with their far corners off the grid */
    if (!PF_IN1(x, y))
        return 0;
#else
    if (x < 0 || x > 0x200 || y < 0 || y > 0x200)
        return 0;
#endif
#ifndef TN_WATCOM_ABI
    if (ts_mark) {
        long k = t->bin - (TerrBin *)strip_427DE0;
        if (ts_extra && ts_have[y + PF_M][x + PF_M] == ts_gen)
            return 1;                   /* the main build has it */
        if (k >= (long)(sizeof ts_ringof / sizeof ts_ringof[0]) ||
            (k + 1) * 0x20 > PORT_S3D_MUL * 0x3EC0 - 0x40)
            return 0;                   /* the pool is full: as at the map's edge */
        ts_have[y + PF_M][x + PF_M] = ts_gen;
        ts_ringof[k] = (ushort)ts_ring;
    }
    port_poly_have[y + PF_M][x + PF_M] = port_poly_gen;
#endif
    b = t->bin;
    b->x = x;
    b->y = y;
#ifdef TN_WATCOM_ABI
    b->key[0] = (((ushort)b->y & 0x3F) << 6) | ((ushort)b->x & 0x3F);
    b->key[1] = (((ushort)b->y & 0x3F) << 6) | ((ushort)(b->x + 1) & 0x3F);
    b->key[3] = ((((ushort)b->y + 1) & 0x3F) << 6) | ((ushort)b->x & 0x3F);
    b->key[2] = ((((ushort)b->y + 1) & 0x3F) << 6) | ((ushort)(b->x + 1) & 0x3F);
#else
    b->key[0] = TR_KEY(b->x, b->y);     /* 128 x 128 with more terrain detail (port/video/sharp3d.c) */
    b->key[1] = TR_KEY(b->x + 1, b->y);
    b->key[3] = TR_KEY(b->x, b->y + 1);
    b->key[2] = TR_KEY(b->x + 1, b->y + 1);
#endif
    b->col = 0;
    b->band = t->band;
    b->count = 0;
    b->list = NULL;
    t->bin++;
    return 1;
}

/* (short)(sign(t) * n) */
static long sg(long t, long n)
{
    short v = (short)t, k = (short)n;
    return v == 0 ? 0 : v > 0 ? k : (short)-k;
}

void TerrainStripBuild(FfCamera *cam, FfView *view, void *strips)
{
    TsRec *r = strips;
    TsCtx ctx;
    TsSpan *A, *B, *C, *sp;
    ulong n, i, j, ia = 1, ib = 1, ic = 1;
    long vx, vy, k0, k1, k2, emitted = 0, lo, hi, e, px, py;
    long r0x, r0y, s0x, s0y, r1x, r1y, s1x, s1y, r2x, r2y, s2x, s2y;
    uchar *pp;
#ifndef TN_WATCOM_ABI
    TerrBin *ring_start;
#endif

    terrstrip_2A8978(cam, view, r);
    n = r->rad;
    A = ffmem_2C4F40(n * 8);
    B = ffmem_2C4F40(n * 8);
    C = ffmem_2C4F40(n * 8);
    terrstrip_2A98AC(r, A, B, C);
    k0 = (uchar)r->oct[0];
    r0x = ts_ring_dx[k0]; r0y = ts_ring_dy[k0]; s0x = ts_step_dx[k0]; s0y = ts_step_dy[k0];
    k1 = (uchar)r->oct[1];
    r1x = ts_ring_dx[k1]; r1y = ts_ring_dy[k1]; s1x = ts_step_dx[k1]; s1y = ts_step_dy[k1];
    k2 = (uchar)r->oct[2];
    r2x = ts_ring_dx[k2]; r2y = ts_ring_dy[k2]; s2x = ts_step_dx[k2]; s2y = ts_step_dy[k2];
#ifdef TN_WATCOM_ABI
    ctx.bin = (TerrBin *)strip_427DE0;
#else
    ctx.bin = (TerrBin *)strip_427DE0 + (ts_extra ? strip_count : 0);
    ts_ring = 0;
#endif
    ctx.band = 0;
    vx = view->x >> 16;
    vy = view->y >> 16;
    ctx.x = vx;
    ctx.y = vy;
    terrstrip_2AA2B4(&ctx);
    if (C[0].last > 1 && (r->oct[2] >> 8 & 0xF0)) {
        ctx.x = vx + s2x;
        ctx.y = vy + s2y;
        terrstrip_2AA2B4(&ctx);
    }
#ifndef TN_WATCOM_ABI
    ring_start = ctx.bin;
#endif
    for (i = 1; i < n; i++) {
        uchar m0 = r->oct[0] >> 8;
        if (cam->band[ctx.band].b < i)
            ctx.band++;
        emitted = 0;
#ifndef TN_WATCOM_ABI
        ts_ring = (long)i;
        ring_start = ctx.bin;
#endif
        if (m0 & 0x0F) {
            if (i < (ulong)A[0].last) {
                ctx.x = vx + sg(s0x, i);
                ctx.y = vy + sg(s0y, i);
                if (!terrstrip_2AA2B4(&ctx))
                    goto fail;
                emitted++;
            }
        } else if (i < r->n38 && C[i].first == 0 && C[i].last > 0) {
            ctx.x = vx + sg(r2x, i);
            ctx.y = vy + sg(r2y, i);
            if (!terrstrip_2AA2B4(&ctx))
                goto fail;
            emitted++;
        }
        if (i < (ulong)B[0].last) {
            ctx.x = vx + sg(s1x, i);
            ctx.y = vy + sg(s1y, i);
            if (!terrstrip_2AA2B4(&ctx))
                goto fail;
            emitted++;
        }
        if ((r->oct[2] >> 8 & 0x0F) && i < (ulong)C[0].last) {
            ctx.x = vx + sg(s2x, i);
            ctx.y = vy + sg(s2y, i);
            if (!terrstrip_2AA2B4(&ctx))
                goto fail;
            emitted++;
        }
        if (m0 & 0x69) {
            if (A[i].first == 0 && A[i].last > 0) {
                ctx.x = vx + sg(r0x, i);
                ctx.y = vy + sg(r0y, i);
                if (!terrstrip_2AA2B4(&ctx))
                    goto fail;
                emitted++;
            }
        } else if (i < r->n34 && B[i].first == 0 && B[i].last > 0) {
            ctx.x = vx + sg(r1x, i);
            ctx.y = vy + sg(r1y, i);
            if (!terrstrip_2AA2B4(&ctx))
                goto fail;
            emitted++;
        }

        /* run A */
        px = vx + sg(r0x, ia);
        py = vy + sg(r0y, ia);
        pp = tcache_tab[i] + ia * 2 - 2;
        for (j = ia, sp = &A[ia]; j <= i; j++, sp++, pp += 2) {
            lo = pp[0] < sp->first ? sp->first : pp[0];
            hi = pp[1] > sp->last ? sp->last : pp[1];
            if (lo >= hi) {
                if (pp[0] >= sp->last)
                    ia = j + 1;
                continue;
            }
            ctx.x = px + sg(s0x, lo);
            ctx.y = py + sg(s0y, lo);
            for (e = lo; e < hi; e++) {
                if (!terrstrip_2AA2B4(&ctx))
                    goto fail;
                ctx.x += s0x;
                emitted++;
                ctx.y += s0y;
            }
            if (hi == sp->last)
                ia = j + 1;
            px += r0x;
            py += r0y;
        }
        /* run B */
        if (i < r->n34) {
            px = vx + sg(r1x, ib);
            py = vy + sg(r1y, ib);
            pp = tcache_tab[i] + ib * 2 - 2;
            for (j = ib, sp = &B[ib]; j <= i; j++, sp++, pp += 2) {
                lo = pp[0] < sp->first ? sp->first : pp[0];
                hi = pp[1] > sp->last ? sp->last : pp[1];
                if (lo >= hi) {
                    if (pp[0] >= sp->last)
                        ib = j + 1;
                    continue;
                }
                ctx.x = px + sg(s1x, lo);
                ctx.y = py + sg(s1y, lo);
                for (e = lo; e < hi; e++) {
                    if (!terrstrip_2AA2B4(&ctx))
                        goto fail;
                    ctx.x += s1x;
                    emitted++;
                    ctx.y += s1y;
                }
                if (hi == sp->last)
                    ib = j + 1;
                px += r1x;
                py += r1y;
            }
        }
        /* run C */
        if (i < r->n38) {
            px = vx + sg(r2x, ic);
            py = vy + sg(r2y, ic);
            pp = tcache_tab[i] + ic * 2 - 2;
            for (j = ic, sp = &C[ic]; j <= i; j++, sp++, pp += 2) {
                lo = pp[0] < sp->first ? sp->first : pp[0];
                hi = pp[1] > sp->last ? sp->last : pp[1];
                if (lo >= hi) {
                    if (pp[0] >= sp->last)
                        ic = j + 1;
                    continue;
                }
                ctx.x = px + sg(s2x, lo);
                ctx.y = py + sg(s2y, lo);
                for (e = lo; e < hi; e++) {
                    if (!terrstrip_2AA2B4(&ctx))
                        goto fail;
                    emitted++;
                    ctx.x += s2x;
                    ctx.y += s2y;
                }
                if (hi == sp->last)
                    ic = j + 1;
                px += r2x;
                py += r2y;
            }
        }
    }
    if (i == n)
        goto done;
fail:
#ifdef TN_WATCOM_ABI
    ctx.bin -= emitted;
#else
    ctx.bin = ring_start;               /* the same: a turned build may skip squares (ts_extra) */
    (void)emitted;
#endif
    r->rad = i;
    r->n34 = i < r->n34 ? i : r->n34;
    r->n38 = i < r->n38 ? i : r->n38;
    for (j = 0; j < i; j++) {
        A[j].first = i < (ulong)A[j].first ? (long)i : A[j].first;
        A[j].last = i < (ulong)A[j].last ? (long)i : A[j].last;
    }
    for (j = 0; j < r->n34; j++) {
        B[j].first = i < (ulong)B[j].first ? (long)i : B[j].first;
        B[j].last = i < (ulong)B[j].last ? (long)i : B[j].last;
    }
    for (j = 0; j < r->n38; j++) {
        C[j].first = i < (ulong)C[j].first ? (long)i : C[j].first;
        C[j].last = i < (ulong)C[j].last ? (long)i : C[j].last;
    }
done:
    strip_count = ((uchar *)ctx.bin - strip_427DE0) >> 5;
    terrrast_2AE004((TrStrip *)r, (TrSpan *)A, (TrSpan *)B, (TrSpan *)C);
    ffmem_2C4FE4(C);
    ffmem_2C4FE4(B);
    ffmem_2C4FE4(A);
}

#ifndef TN_WATCOM_ABI
#undef TerrainStripBuild
/* The builder walks three 45 degree sectors around the heading.  Where the wide sharp view
 * reaches past them on a side, the squares of the sectors turned 22.5 degrees that way are
 * added (with their vertices), and all the squares are put in ring order again, the main ones
 * keeping their order: one drawing for the whole wide view (it was drawn again for each such
 * side).  The record stays the main build's. */
void TerrainStripBuild(FfCamera *cam, FfView *view, void *strips)
{
    TsRec main;
    unsigned head;
    int side, extra = 0;
    long n0, n, k;
    TerrBin *bins, *tmp;
    ushort *rings;
    port_pf_sync();                     /* the map with the land around it (port/video/mapedge.c) */
    if (!port_s3d_ts_turn && ++port_poly_gen == 0) {
        memset(port_poly_have, 0, sizeof port_poly_have);
        port_poly_gen = 1;
    }
    if (!port_s3d_on || !port_s3d_fov_x || port_s3d_ts_turn) {
        ts_build(cam, view, strips);
        return;
    }
    head = view->heading;
    if (++ts_gen == 0) {                /* the marks wrapped: clear them */
        memset(ts_have, 0, sizeof ts_have);
        memset(port_tr_mark, 0, sizeof port_tr_mark);
        ts_gen = 1;
    }
    ts_mark = 1;
    port_tr_n = 0;
    port_tr_mark_on = 1;
    port_tr_gen = ts_gen;
    ts_build(cam, view, strips);
    main = *(TsRec *)strips;
    for (side = -1; side <= 1; side += 2) {
        long room = port_ts_room(head, side);
        if (room >= port_s3d_fov_x)
            continue;
        ts_extra = 1;
        port_tr_skip = 1;
        port_s3d_ts_turn = side * 0x1000;
        ts_build(cam, view, strips);
        port_s3d_ts_turn = 0;
        ts_extra = 0;
        port_tr_skip = 0;
        extra = 1;
    }
    ts_mark = 0;
    port_tr_mark_on = 0;
    *(TsRec *)strips = main;
    if (!extra)
        return;
    /* ring order, stable: a merge of the main run with the runs the turned builds added */
    n = strip_count;
    bins = (TerrBin *)strip_427DE0;
    tmp = ffmem_2C4F40(n * (long)sizeof(TerrBin));
    rings = ffmem_2C4F40(n * 2);
    /* each build's run is in ring order: merge the runs in turn, the earlier one first on a tie */
    for (n0 = 1; n0 < n && ts_ringof[n0] >= ts_ringof[n0 - 1]; n0++)
        ;
    memcpy(tmp, bins, n * sizeof(TerrBin));
    memcpy(rings, ts_ringof, n * 2);
    while (n0 < n) {
        long n1, a = 0, b = n0, o = 0;
        for (n1 = n0 + 1; n1 < n && rings[n1] >= rings[n1 - 1]; n1++)
            ;
        while (a < n0 || b < n1) {
            if (b >= n1 || (a < n0 && rings[a] <= rings[b])) {
                bins[o] = tmp[a];
                ts_ringof[o++] = rings[a++];
            } else {
                bins[o] = tmp[b];
                ts_ringof[o++] = rings[b++];
            }
        }
        for (k = n1; k < n; k++) {
            bins[k] = tmp[k];
            ts_ringof[k] = rings[k];
        }
        memcpy(tmp, bins, n1 * sizeof(TerrBin));
        memcpy(rings, ts_ringof, n1 * 2);
        n0 = n1;
    }
    ffmem_2C4FE4(rings);
    ffmem_2C4FE4(tmp);
}
#endif

long terrstrip_2AB0C0(long n)
{
    long i = 0;
    if ((short)n > 0)
        do
            i++;
        while ((short)i < (short)n);
    return i;
}

void terrstrip_2AB0D4(void)
{
#ifdef TN_WATCOM_ABI
    strip_427DE0 = (uchar *)ffmem_2C4F40(0x3EC0) + 0x40;
#else
    strip_427DE0 = (uchar *)ffmem_2C4F40(PORT_S3D_MUL * 0x3EC0) + 0x40;   /* widescreen, detail */
#endif
}

void terrstrip_2AB0E8(void)
{
    ffmem_2C4FE4(strip_427DE0 - 0x20);
}
