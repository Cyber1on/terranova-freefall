/* terrrast.c -- Terra Nova module M091 (0x2ADF00-0x2AF4E0; source file name unknown): terrain
 * vertices.  TerrainStripBuild (G5c) works out, per 45 degree sector of the view heading, how far
 * the visible terrain reaches along the rows of a circle around the camera; terrrast_2AE004 then
 * walks those rows over the fine map grid, turns every cell corner into a view space vertex
 * (heights, shade, colour) and hands it to weather_2B1218 to project.  Decompiled from
 * __FF.EXE v1.09.
 *
 *   0x2ADF00 terrrast_2ADF00 allocate the buffers   0x2ADF34 terrrast_2ADF34 free them
 *   0x2ADF58 terrrast_2ADF58 vertex colour          0x2ADFD4 terrrast_2ADFD4 vertex shade
 *   0x2AE004 terrrast_2AE004 emit the vertices of a strip
 *
 * The walk: the camera cell is (cx, cy) (or cx + 1 / cy + 1 by sector); "a" is the step away
 * from the camera along the sector's main axis and "b" the step across.  The view transform of a
 * point is computed once with the ff_view matrix and then advanced by adding matrix entries (one
 * cell is 1.0, so the per-cell deltas are the matrix entries themselves); the height enters as
 * an integer multiple of the height column of the matrix.  tr_idx[(y & 63) << 6 | (x & 63)]
 * records the number of the vertex emitted for a cell.
 * Quirk kept: an octant word that is none of the eight sector codes leaves the walk unset (the
 * original then runs with whatever is on its stack; here everything is zero).
 */
#include "g5_a.h"
#include "g5_c.h"     /* added at the G5-G8 merge */
#include "g6_b.h"     /* added at the G5-G8 merge */

static fix fmul(fix a, fix b)
{
    return (fix)(((long long)a * b) >> 16);
}

void terrrast_2ADF00(void)
{
#ifdef TN_WATCOM_ABI
    tr_idx = ffmem_2C4F40(0x4000);
    tr_verts = ffmem_2C4F40(0x7242);
    tr_buf30 = ffmem_2C4F40(0x7D00);
#else
    /* the sharp 3D view in widescreen and with more terrain detail sees more terrain squares
     * (port/video/sharp3d.c) */
    tr_idx = ffmem_2C4F40(port_s3d_on && port_s3d_tr128 ? 0x10000 : 0x4000);
    tr_verts = ffmem_2C4F40(0x7242 * PORT_S3D_MUL);
    tr_buf30 = ffmem_2C4F40(0x7D00 * PORT_S3D_MUL);
#endif
    weather_2B0520();
}

void terrrast_2ADF34(void)
{
    weather_2B053C();
    ffmem_2C4FE4(tr_buf30);
    ffmem_2C4FE4(tr_verts);
    ffmem_2C4FE4(tr_idx);
}

void terrrast_2ADF58(uchar *cell, TrVert *v)
{
    ulong c = ((ulong *)grd_bpal)[planet_res2c[*(ushort *)cell & 0x3FF]];
    v->r = (((c >> 2) & 0xFF) << 16) | 0x8000;
    v->g = (((c >> 13) & 0xFF) << 16) | 0x8000;
    v->kind = 0x10;
    v->b = ((c >> 24) << 16) | 0x8000;
}

fix terrrast_2ADFD4(uchar *cell)
{
    if (game_35CADC >= 0x10)
        return 0xF0000;
    return ((15 - ((*(ushort *)cell >> 6) & 0xE)) << 16) | 0x8000;
}

/* the walk state shared by the emitting loops */
typedef struct {
    long n;             /* next vertex number */
    TrVert *v;          /* next vertex */
    uchar colour;       /* ff_cam.g_5c == 3 */
} Emit;

#ifndef TN_WATCOM_ABI
#include "mapedge.h"
/* one-pass widescreen (terrstrp.c): the main build marks its vertices; a turned build keeps
 * the marked cells' numbers and adds its vertices after the main ones */
extern ushort port_tr_mark[0x10000], port_tr_gen;
extern int port_tr_mark_on, port_tr_skip;
extern long port_tr_n;
#endif

static void emit(Emit *e, uchar *cell, long row, long col, fix x, fix y, fix z)
{
    TrVert *v = e->v;
    ushort w1;
#ifndef TN_WATCOM_ABI
    if (!PF_IN(col, row))               /* past the land around the map (a camera far out) */
        cell = PF_CELL(col < -PF_M ? -PF_M : col > 0x200 + PF_M ? 0x200 + PF_M : col,
                       row < -PF_M ? -PF_M : row > 0x200 + PF_M ? 0x200 + PF_M : row);
#endif
    w1 = *(ushort *)(cell + 1);
    long h = (short)w1 >> 4;
#ifdef TN_WATCOM_ABI
    tr_idx[((row & 0x3F) << 6) | (col & 0x3F)] = e->n;
#else
    if (port_tr_mark_on) {
        long key = TR_KEY(col, row);
        if (port_tr_skip && port_tr_mark[key] == port_tr_gen)
            return;                     /* the main build's vertex stays */
        if ((e->n + 1) * 0x27 > 0x7242 * PORT_S3D_MUL)
            return;                     /* the buffer is full */
        port_tr_mark[key] = port_tr_gen;
        tr_idx[key] = e->n;
    } else
        tr_idx[TR_KEY(col, row)] = e->n;
#endif
    v->variant = (uchar)((ushort)(w1 << 12) >> 14);
    v->shade = ((15 - ((*(ushort *)cell >> 6) & 0xE)) << 16) | 0x8000;
    e->n++;
    if (e->colour)
        terrrast_2ADF58(cell, v);
    else
        v->kind = 5;
    v->x = x + ff_view.m[1] * h;
    v->y = y + ff_view.m[4] * h;
    v->z = z + ff_view.m[7] * h;
    weather_2B1218(v);
    e->v = (TrVert *)((uchar *)v + 0x27);
#ifndef TN_WATCOM_ABI
    port_tr_n = e->n;
#endif
}

static long imin(long a, long b) { return a > b ? b : a; }
static long imax(long a, long b) { return a > b ? a : b; }

void terrrast_2AE004(TrStrip *s, TrSpan *a, TrSpan *b, TrSpan *c)
{
    fix *m = ff_view.m;
    long rad = s->rad;
    uchar *prof = tcache_tab[rad - 1];
    Emit e;
    ulong i;
    long cx, cy;
    long col0 = 0, row0 = 0;           /* start cell */
    long da_c = 0, da_r = 0, da_p = 0; /* step "a": column, row, cells */
    long db_c = 0, db_r = 0, db_p = 0; /* step "b" */
    fix ax = 0, ay = 0, az = 0;        /* transform delta of step a */
    fix bx = 0, by = 0, bz = 0;        /* of step b */
    long row, col;
    uchar *cell, *cell0;
    fix x0, y0, z0, x, y, z;
    long n80, n84, n88, n90, lim, j, k;

#ifdef TN_WATCOM_ABI
    e.n = 0;
    e.v = tr_verts;
#else
    e.n = port_tr_skip ? port_tr_n : 0;
    e.v = (TrVert *)((uchar *)tr_verts + e.n * 0x27);
#endif
    e.colour = ff_cam.g_5c == 3;
    for (i = 0; i < s->n34; i++) {
        a[i].first = 0;
        b[i].first = 1;
    }
    for (i = 0; i < s->n38; i++)
        c[i].first = 1;
    cx = ff_view.x >> 16;
    cy = ff_view.y >> 16;
    switch (s->octant) {
    case 0x100:
        col0 = cx; row0 = cy;
        da_c = 1; da_r = 0; db_c = 0; db_r = 1; da_p = 1; db_p = 0x201;
        ax = m[2]; ay = m[5]; az = m[8]; bx = m[0]; by = m[3]; bz = m[6];
        break;
    case 0x201:
        col0 = cx; row0 = cy + 1;
        da_c = 1; da_r = 0; db_c = 0; db_r = -1; da_p = 1; db_p = -0x201;
        ax = m[2]; ay = m[5]; az = m[8]; bx = -m[0]; by = -m[3]; bz = -m[6];
        break;
    case 0x402:
        col0 = cx + 1; row0 = cy;
        da_c = 0; da_r = 1; db_c = -1; db_r = 0; da_p = 0x201; db_p = -1;
        ax = m[0]; ay = m[3]; az = m[6]; bx = -m[2]; by = -m[5]; bz = -m[8];
        break;
    case 0x803:
        col0 = cx; row0 = cy;
        da_c = 0; da_r = 1; db_c = 1; db_r = 0; da_p = 0x201; db_p = 1;
        ax = m[0]; ay = m[3]; az = m[6]; bx = m[2]; by = m[5]; bz = m[8];
        break;
    case 0x1004:
        col0 = cx + 1; row0 = cy + 1;
        da_c = -1; da_r = 0; db_c = 0; db_r = -1; da_p = -1; db_p = -0x201;
        ax = -m[2]; ay = -m[5]; az = -m[8]; bx = -m[0]; by = -m[3]; bz = -m[6];
        break;
    case 0x2005:
        col0 = cx + 1; row0 = cy;
        da_c = -1; da_r = 0; db_c = 0; db_r = 1; da_p = -1; db_p = 0x201;
        ax = -m[2]; ay = -m[5]; az = -m[8]; bx = m[0]; by = m[3]; bz = m[6];
        break;
    case 0x4006:
        col0 = cx; row0 = cy + 1;
        da_c = 0; da_r = -1; db_c = 1; db_r = 0; da_p = -0x201; db_p = 1;
        ax = -m[0]; ay = -m[3]; az = -m[6]; bx = m[2]; by = m[5]; bz = m[8];
        break;
    case 0x8007:
        col0 = cx + 1; row0 = cy + 1;
        da_c = 0; da_r = -1; db_c = -1; db_r = 0; da_p = -0x201; db_p = -1;
        ax = -m[0]; ay = -m[3]; az = -m[6]; bx = -m[2]; by = -m[5]; bz = -m[8];
        break;
    }
#ifndef TN_WATCOM_ABI
    /* the map's copy with the land around it (port/video/mapedge.c, G34 / G35): PF_W cells a row */
    port_pf_sync();
    if (da_p == 0x201 || da_p == -0x201)
        da_p = da_p > 0 ? PF_W : -PF_W;
    if (db_p == 0x201 || db_p == -0x201)
        db_p = db_p > 0 ? PF_W : -PF_W;
#endif
    da_p *= 3;
    db_p *= 3;

    /* the camera cell */
    row = row0;
    col = col0;
#ifndef TN_WATCOM_ABI
    cell0 = cell = PF_CELL(col, row);
#else
    cell0 = cell = (uchar *)TN_LOWMEM(GMAP->rows2[row]) + col * 3;
#endif
    {
        fix ex = (col << 16) - ff_view.x;
        fix ey = (row << 16) - ff_view.y;
        x0 = x = fmul(ey, m[0]) + fmul(ex, m[2]) + ff_view.mz[0];
        y0 = y = fmul(ey, m[3]) + fmul(ex, m[5]) + ff_view.mz[1];
        z0 = z = fmul(ey, m[6]) + fmul(ex, m[8]) + ff_view.mz[2];
    }
    emit(&e, cell, row, col, x, y, z);
    if (c[0].last > 1) {
        emit(&e, cell - da_p, row - da_r, col - da_c, x - ax, y - ay, z - az);
        n80 = c[0].last;
    } else
        n80 = 1;

    /* row 0 of the circle, going along b: c spans backwards along a */
    if (s->octant & 0x0F00)
        n90 = imin(imax(a[0].last, 1), prof[1]);
    else {
        ulong t = s->n38 > 1 ? s->n38 : 1;
        n90 = t >= prof[1] ? prof[1] : (long)t;
    }
    lim = n90;
    for (k = 1; k <= lim; k++) {
        long r = row, cc = col;
        uchar *p;
        fix sx, sy, sz;
        col += db_c; row += db_r; cell += db_p;
        x += bx; y += by; z += bz;
        emit(&e, cell, row, col, x, y, z);
        if ((ulong)k < s->n38) {
            long t = imin(prof[2 * k - 1], c[k].last);
            n84 = imax(t, n80);
            n80 = t;
        } else
            n84 = n80;
        if ((ulong)k > s->n38 || n84 <= 1)
            continue;
        r = row; cc = col; p = cell; sx = x; sy = y; sz = z;
        for (j = 1; (ulong)j < (ulong)n84; j++) {
            cc -= da_c; p -= da_p; r -= da_r;
            sx -= ax; sy -= ay; sz -= az;
            emit(&e, p, r, cc, sx, sy, sz);
        }
    }

    /* the other way along b: b spans */
    {
        long r = row0, cc = col0;
        uchar *p = cell0;
        fix sx = x0, sy = y0, sz = z0;
        n88 = imin(prof[1], b[0].last);
        for (j = 1; j < n88; j++) {
            cc -= db_c; r -= db_r; p -= db_p;
            sx -= bx; sy -= by; sz -= bz;
            emit(&e, p, r, cc, sx, sy, sz);
        }
    }

    /* rows 1..rad along a */
    for (k = 1; k <= rad; k++) {
        long n120, n11c, f, r, cc;
        uchar *p;
        fix sx, sy, sz;
        if (k < rad) {
            long t = imin(prof[2 * k - 1], a[k].last);
            n120 = imax(t, n90);
            n90 = t;
        } else
            n120 = n90;
        col0 += da_c;
        row0 += da_r;
        cell0 += da_p;
        x0 += ax; y0 += ay; z0 += az;
        cc = col0; r = row0; p = cell0; sx = x0; sy = y0; sz = z0;
        f = a[k - 1].first;
        if (f >= n120)
            return;
        if (f != 0) {
            cc = col0 + db_c * f;
            r = row0 + db_r * f;
            p += db_p * f;
            sx = x0 + bx * f;
            sy = by * f + y0;
            sz = z0 + bz * f;
        }
        emit(&e, p, r, cc, sx, sy, sz);
        for (j = f + 1; j <= n120; j++) {
            cc += db_c; r += db_r; p += db_p;
            sx += bx; sy += by; sz += bz;
            emit(&e, p, r, cc, sx, sy, sz);
        }
        if ((ulong)k > s->n34)
            continue;
        sx = x0; sy = y0; sz = z0; p = cell0; r = row0; cc = col0;
        if ((ulong)k < s->n34) {
            long t = imin(prof[2 * k - 1], b[k].last);
            n11c = imax(t, n88);
            n88 = t;
        } else
            n11c = n88;
        for (j = 1; j < n11c; j++) {
            cc -= db_c; p -= db_p; r -= db_r;
            sx -= bx; sy -= by; sz -= bz;
            emit(&e, p, r, cc, sx, sy, sz);
        }
    }
}
