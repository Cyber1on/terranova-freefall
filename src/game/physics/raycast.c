/* raycast.c -- Terra Nova terrain / object raycast for EDMS (module M118; the game side of
 * Shock's ff_raycast, FF_FTL.CC calls it through ff_raycast 0x2F0847).  Decompiled from
 * __FF.EXE v1.09; FF_raycast and FF_raycast_generic_obj are the binary's names (error string),
 * RaycastCylinder is a community name, the rest are the map's placeholders.
 *
 *   0x2DA0D0 raycast_2DA0D0  test the objects of the current map cell (grid entities of class
 *                            0 / 4 / 6, then the tile objects of the 4x4 tile group)
 *   0x2DA3C0 raycast_2DA3C0  the (up to) two terrain triangles a ray piece crosses in a cell
 *   0x2DA454 raycast_2DA454  which triangle of a cell a point lies in (1 or 3)
 *   0x2DA48C raycast_2DA48C  step to the next cell edge
 *   0x2DA678 FF_raycast      cast a ray over the map: terrain triangles and objects
 *   0x2DB1A4 raycast_2DB1A4  ray against an entity's shape
 *   0x2DB20C raycast_2DB20C  ray against a tile object's shape
 *   0x2DB27C FF_raycast_generic_obj  dispatch on the shape type
 *   0x2DB310 raycast_2DB310  3x3 determinant (fix)
 *   0x2DB3B4 raycast_2DB3B4  ray against one plane through three points (Cramer)
 *   0x2DB4B4 raycast_2DB4B4  ray against a polygon shape (a box: top, 4 walls, floor)
 *   0x2DB8D8 raycast_2DB8D8  ray against a sphere (shape type 2)
 *   0x2DBAA0 RaycastCylinder ray against a cylinder with a height (shape type 3)
 * Data: 0x2DA648 / 0x2DA658 / 0x2DA668 / 0x2DB26C jump tables (switches below); 0x35ED5C
 * {2, 3, 0, 1} the opposite cell edge (C initializer).
 *
 * Coordinates: the map works in cells of 6 world units (x, y divided by 6) and heights * 4;
 * where_hit is converted back (x * 6, y * 6, z / 4).  Object shapes use z * 0xAAA (1 / 24).
 * Cell edges: 0 low y, 1 high x, 2 high y, 3 low x; the cell is split into two triangles along
 * one diagonal or the other by the parity of cx + cy (triangles 0 / 1 or 2 / 3).
 * The tff's DATA1 gets the entity hit (low 12 bits) or, for a tile object, bit 31 + the
 * template (low 12 bits) + the 4x4 tile cell (bits 22..28 x, 15..21 y).
 */
#include "g3_b.h"
#include "g2_a.h"      /* grid_hash, grid_squares, grid_295CFC, tile_tmpls (owner g2a) */
#include "g5_a.h"     /* added at the G5-G8 merge */
#include "g6_c.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
terrain_ff *rc_tff;
long rc_sy, rc_sx;
fix rc_fy, rc_fx, rc_dx, rc_dy, rc_dz, rc_nx, rc_ny, rc_nz, rc_ex, rc_ey, rc_ez;
long rc_ecy, rc_cy, rc_tcx, rc_tcy, rc_ecx, rc_cx;
fix rc_x, rc_y, rc_z;
#endif

static const signed char rc_opposite[4] = { 2, 3, 0, 1 };   /* 0x35ED5C */

#define FIXMUL G3B_FIXMUL
#define FIXDIV G3B_FIXDIV
#define ABS(v) ((v) < 0 ? -(v) : (v))
#define DIV4(v) ((v) / 4)                       /* signed: sar 31; shl 2; sbb; sar 2 */
#define MULDIV(a, b, c) ((fix)((long long)(a) * (b) / (c)))   /* imul; idiv (faults on overflow) */

/* map corner height (cell x, row y) as fix: the word at +1 of the 3-byte cell, >> 4 */
static fix corner(uchar *row, long cx)
{
    return (fix)((short)(*(short *)(row + cx * 3 + 1)) >> 4) << 16;
}

void raycast_2DA0D0(short *hit_ent, short *hit_tile, fix *dist, long *is_tile)
{
    union { struct { short x, y; } s; ulong key; } k;
    short i, sq, ent;
    uchar t;
    ushort tmpl;
    fix best = 0x20000, d;
    fix pos[5];

    *is_tile = 0;
    k.s.x = (short)rc_cx;
    k.s.y = (short)rc_cy;
    grid_hash_cur = (short)((((k.s.x << 2) + k.s.y) & 0x1FF) + 0x200);
    if (grid_hash[grid_hash_cur].sq == 0)
        i = 0;                  /* QUIRK: an empty bucket reads the list of hash entry 0 */
    else if (*(ulong *)&grid_squares[(short)grid_hash[grid_hash_cur].sq] == k.key)
        i = grid_hash_cur;
    else
        i = (short)grid_295CFC(*(GridXY *)&k.s, 0, grid_hash_cur);
    for (sq = grid_hash[i].sq; sq; sq = grid_squares[sq].next) {
        uchar cls;
        ent = grid_squares[sq].ent;
        if (ent == (&last_shooter)[rc_tff->caller])
            continue;
        cls = G3B_ENT(ent).cls;
        if (cls != 0 && cls != 4 && !(cls == 6 && sfx_28B9E0(ent)))
            continue;
        if (raycast_2DB1A4(ent, rc_x, rc_y, rc_z, rc_nx, rc_ny, rc_nz, rc_tff->my_size, &d) && best > d) {
            *hit_ent = ent;
            best = d;
        }
    }
    rc_tcx = rc_cx >> 2;
    rc_tcy = rc_cy >> 2;
    t = game_3C5E1C[rc_tcx][rc_tcy];
    if (t) {
        t = ((uchar *)tile_groups)[t * 0x13 + (rc_cx & 3) * 4 + (rc_cy & 3)];   /* .cell[x][y] */
        while (t) {
            tmpl = *(ushort *)&((uchar *)tile_objs)[t * 16 + 6];               /* .tmpl */
            if (tile_tmpls[tmpl].cls == 0 || tile_tmpls[tmpl].cls == 4) {
                memcpy(pos, tile_tmpls[tmpl].pos, 0x12);
                pos[0] += rc_tcx << 18;
                pos[1] += rc_tcy << 18;
                pos[2] += map_2949C4(pos[0], pos[1]);
                if (raycast_2DB20C(tmpl, pos, rc_x, rc_y, rc_z, rc_nx, rc_ny, rc_nz,
                                   rc_tff->my_size, &d) && best > d) {
                    *is_tile = 1;
                    *hit_tile = tmpl;
                    best = d;
                }
            }
            t = ((uchar *)tile_objs)[t * 16 + 8];                              /* .old, low byte */
        }
    }
    *dist = best;
}

void raycast_2DA3C0(long *tri, long entry, long exit)
{
    tri[1] = 0xF;
    if ((rc_cx + rc_cy) & 1) {
        tri[0] = (entry == 0 || entry == 3) ? 2 : 3;
        if (exit == 0 || exit == 3) {
            if (tri[0] != 2)
                tri[1] = 2;
        } else if (tri[0] != 3)
            tri[1] = 3;
    } else {
        tri[0] = (entry == 0 || entry == 1) ? 0 : 1;
        if (exit == 0 || exit == 1) {
            if (tri[0] != 0)
                tri[1] = 0;
        } else if (tri[0] != 1)
            tri[1] = 1;
    }
}

long raycast_2DA454(fix fx, fix fy)
{
    if ((rc_cx + rc_cy) & 1)
        return fy + fx >= 0x10000 ? 1 : 3;
    return fx - fy >= 0 ? 1 : 3;
}

void raycast_2DA48C(fix *ox, fix *oy, fix *oz, fix *fx, fix *fy, long *edge)
{
    fix tx = rc_dx > 0 ? 0x10000 - rc_fx : -rc_fx;     /* to the next x edge */
    fix ty = rc_dy > 0 ? 0x10000 - rc_fy : -rc_fy;     /* to the next y edge */
    fix mx, my, mz;
    bool xfirst;

    if (rc_dx == 0)
        xfirst = 0;
    else if (rc_dy == 0)
        xfirst = 1;
    else {
        fix a = FIXMUL(tx, rc_dy), b = FIXMUL(ty, rc_dx);
        xfirst = ABS(b) > ABS(a);
    }
    if (xfirst) {
        my = MULDIV(tx, rc_dy, rc_dx);
        mz = MULDIV(tx, rc_dz, rc_dx);
        if (rc_sx == 1) {
            *edge = 1;
            *fx = 0x10000;
        } else {
            *edge = 3;
            *fx = 0;
        }
        *ox = rc_x + tx;
        *oy = rc_y + my;
        *fy = *oy - (rc_cy << 16);
    } else {
        mx = MULDIV(ty, rc_dx, rc_dy);
        mz = MULDIV(ty, rc_dz, rc_dy);
        if (rc_sy == 1) {
            *edge = 2;
            *fy = 0x10000;
        } else {
            *edge = 0;
            *fy = 0;
        }
        *ox = rc_x + mx;
        *fx = *ox - (rc_cx << 16);
        *oy = rc_y + ty;
    }
    *oz = rc_z + mz;
}

/* where_hit from map coordinates */
static void put_hit(fix *where_hit, fix x, fix y, fix z)
{
    where_hit[0] = x * 6;
    where_hit[1] = y * 6;
    where_hit[2] = DIV4(z);
}

bool FF_raycast(fix x, fix y, fix z, fix vec[3], fix range, fix where_hit[3], terrain_ff *tff)
{
    long tri[2];
    long exit_edge = 0, entry_edge;
    long is_tile, cur_tri;
    fix dist;
    fix nfx = 0, nfy = 0;               /* fraction of the next point in its cell */
    fix s_fx, s_fy, s_z, s_nfx, s_nfy, s_nz;
    long count;
    fix d_a, d_b, d_c;                  /* corner sums of the other triangle */
    fix h00, h01, h10, h11, hmax, hmin, top, bot;
    short hit_ent, hit_tile;
    uchar first = 1, done = 0, lifted = 0, inside = 0;
    fix r6;
    long k;

    rc_tff = tff;
    rc_x = x / 6;
    rc_y = y / 6;
    rc_cx = rc_x >> 16;
    rc_cy = rc_y >> 16;
    rc_fx = rc_x & 0xFFFF;
    rc_fy = rc_y & 0xFFFF;
    tff->DATA1 = 0;
    tff->DATA2 = 0;
    rc_dx = vec[0];
    rc_z = z << 2;
    rc_dy = vec[1];
    rc_dz = vec[2];
    rc_sx = rc_dx < 0 ? -1 : 1;
    rc_sy = rc_dy < 0 ? -1 : 1;
    r6 = range / 6;
    rc_ex = FIXMUL(r6, rc_dx) + rc_x;
    rc_ecx = rc_ex >> 16;
    rc_ey = FIXMUL(r6, rc_dy) + rc_y;
    rc_ecy = rc_ey >> 16;
    rc_ez = FIXMUL(range * 4, rc_dz) + rc_z;
    count = ABS(rc_ecx - rc_cx) + ABS(rc_ecy - rc_cy) + 1;
    rc_dz = FIXDIV(rc_dz * 4, 0x2AAA);
    entry_edge = raycast_2DA454(rc_fx, rc_fy);

    do {
        /* the next point: the cell edge, or the end point in the last cell */
        if (count > 0 && !(rc_cx == rc_ecx && rc_cy == rc_ecy))
            raycast_2DA48C(&rc_nx, &rc_ny, &rc_nz, &nfx, &nfy, &exit_edge);
        else {
            rc_nx = rc_ex;
            rc_ny = rc_ey;
            rc_nz = rc_ez;
            nfx = rc_ex & 0xFFFF;
            nfy = rc_ey & 0xFFFF;
            first = 1;
            done = 1;
            exit_edge = raycast_2DA454(nfx, nfy);
        }
        if (ABS(rc_x - rc_nx) + ABS(rc_y - rc_ny) < 0x10)
            goto next_cell;
        raycast_2DA0D0(&hit_ent, &hit_tile, &dist, &is_tile);
        {
            uchar *row0 = *(uchar **)(game_map_35BFB4 + 0x84E + rc_cy * 4);
            uchar *row1 = *(uchar **)(game_map_35BFB4 + 0x852 + rc_cy * 4);
            h00 = corner(row0, rc_cx);
            h01 = corner(row1, rc_cx);
            h10 = corner(row0, rc_cx + 1);
            h11 = corner(row1, rc_cx + 1);
        }
        {
            fix ma = h00 > h01 ? h00 : h01, mb = h11 >= h10 ? h11 : h10;
            hmax = ma > mb ? ma : mb;
        }
        if (hmax > rc_z || hmax > rc_nz) {
            raycast_2DA3C0(tri, entry_edge, exit_edge);
            if (first && ABS(rc_z - rc_nz) > 0x320000) {
                /* a steep piece: clip it to the slab of the cell's heights first */
                fix ma = h00 >= h01 ? h01 : h00, mb = h11 > h10 ? h10 : h11;
                lifted = 1;
                top = hmax + 0x10000;
                hmin = ma < mb ? ma : mb;
                bot = hmin - 0x10000;
                s_fx = rc_fx;
                s_fy = rc_fy;
                s_z = rc_z;
                s_nfx = nfx;
                s_nfy = nfy;
                s_nz = rc_nz;
                if (ABS(top - s_nz) >> 16 < ABS(rc_z - rc_nz) / 2) {
                    fix t = FIXDIV(top - rc_nz, rc_z - rc_nz);
                    if (t > 0 && t < 0x10000) {
                        rc_fx = nfx + FIXMUL(rc_fx - nfx, t);
                        rc_fy = nfy + FIXMUL(rc_fy - nfy, t);
                        rc_z = top;
                    }
                }
                if (ABS(rc_z - bot) >> 16 < ABS(rc_z - rc_nz) / 2) {
                    fix t = FIXDIV(rc_z - bot, rc_z - rc_nz);
                    if (t > 0 && t < 0x10000) {
                        nfx = rc_fx - FIXMUL(rc_fx - nfx, t);
                        nfy = rc_fy - FIXMUL(rc_fy - nfy, t);
                        rc_nz = bot;
                    }
                }
            }
            d_a = h01 + h10 - h11;
            d_b = h11 + h00 - h10;
            d_c = h11 + h00 - h01;
            for (k = 0; k < 2; k++) {
                fix base, cx_, cy_, z0, z1;
                if (tri[k] == 0xF)
                    break;
                base = h00;
                cy_ = h01;
                cx_ = h10;
                cur_tri = tri[k];
                switch (tri[k]) {
                case 0: cy_ = d_b; break;
                case 1: cx_ = d_c; break;
                case 2: break;
                case 3: base = d_a; break;
                }
                cx_ -= base;
                cy_ -= base;
                z0 = base + FIXMUL(rc_fx, cx_) + FIXMUL(rc_fy, cy_);
                z1 = base + FIXMUL(nfx, cx_) + FIXMUL(nfy, cy_);
                if (z0 < rc_z && z1 < rc_nz)
                    continue;                   /* above the triangle at both ends */
                if (z0 >= rc_z && z1 >= rc_nz) {
                    put_hit(where_hit, rc_x, rc_y, rc_z);   /* starts below: hit here */
                    return 1;
                }
                {
                    fix a = z0 - rc_z, b = z1 - rc_nz, t, hx, hy;
                    if (ABS(a) >> 16 >= ABS(a - b) / 2)
                        continue;
                    t = FIXDIV(a, a - b);
                    if (t > dist)
                        continue;
                    hx = rc_fx + FIXMUL(nfx - rc_fx, t);
                    hy = rc_fy + FIXMUL(nfy - rc_fy, t);
                    if (t < 0 || t > 0x10000)
                        continue;
                    switch (cur_tri) {          /* inside the triangle? */
                    case 0: inside = hx - hy > 0; break;
                    case 1: inside = hx - hy < 0; break;
                    case 2: inside = hx + hy < 0x10000; break;
                    case 3: inside = hx + hy > 0x10000; break;
                    }
                    if (!inside)
                        continue;
                    where_hit[0] = ((rc_cx << 16) | hx) * 6;
                    where_hit[1] = ((rc_cy << 16) | hy) * 6;
                    where_hit[2] = DIV4(rc_z + FIXMUL(rc_nz - rc_z, t));
                    return 1;
                }
            }
            if (first && lifted) {
                rc_fx = s_fx;
                rc_fy = s_fy;
                rc_z = s_z;
                nfx = s_nfx;
                nfy = s_nfy;
                rc_nz = s_nz;
            }
        }
        /* objects of the cell */
        if (dist <= 0x10000) {
            where_hit[0] = (rc_x + FIXMUL(rc_nx - rc_x, dist)) * 6;
            where_hit[1] = (rc_y + FIXMUL(rc_ny - rc_y, dist)) * 6;
            where_hit[2] = DIV4(rc_z + FIXMUL(rc_nz - rc_z, dist));
            if (is_tile == 0) {
                tff->DATA1 = (tff->DATA1 & 0xFFFFF000) | (long)hit_ent;
            } else {
                long d = (tff->DATA1 & 0x7FFFFFFF) | 0x80000000;
                d = (d & 0xFFFFF000) | (ushort)hit_tile;
                d = (d & 0xE03FFFFF) | (rc_tcx << 22);
                d = (d & 0xFFC07FFF) | (rc_tcy << 15);
                tff->DATA1 = d;
            }
            return 1;
        }
    next_cell:
        lifted = 0;
        first = 0;
        rc_x = rc_nx;
        rc_y = rc_ny;
        rc_z = rc_nz;
        count--;
        entry_edge = rc_opposite[exit_edge];
        switch (exit_edge) {
        case 0: rc_fy = 0x10000; rc_fx = nfx; rc_cy--; break;
        case 1: rc_fx = 0; rc_fy = nfy; rc_cx++; break;
        case 2: rc_fx = nfx; rc_fy = 0; rc_cy++; break;
        case 3: rc_fx = 0x10000; rc_fy = nfy; rc_cx--; break;
        }
    } while (!done);
    put_hit(where_hit, rc_ex, rc_ey, rc_ez);
    return 0;
}

bool raycast_2DB1A4(short ent, fix x, fix y, fix z, fix nx, fix ny, fix nz, fix size, fix *t)
{
    return FF_raycast_generic_obj(&G3B_ENT(ent).x, interact_2973E8(G3B_ENT(ent).cls, G3B_ENT(ent).type),
                                  x, y, z, nx, ny, nz, size, t);
}

bool raycast_2DB20C(ushort tmpl, fix *pos, fix x, fix y, fix z, fix nx, fix ny, fix nz, fix size, fix *t)
{
    return FF_raycast_generic_obj(pos, interact_2973E8(tile_tmpls[tmpl].cls, tile_tmpls[tmpl].type),
                                  x, y, z, nx, ny, nz, size, t);
}

bool FF_raycast_generic_obj(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix nx, fix ny, fix nz,
                            fix size, fix *t)
{
    switch ((ushort)s->type) {
    case 0:
        return 0;
    case 1:
        return raycast_2DB4B4(pos, s, x, y, z, nx, ny, nz, size, t);
    case 2:
        return raycast_2DB8D8(pos, s, x, y, z, nx, ny, nz, size, t);
    case 3:
        return RaycastCylinder(pos, s, x, y, z, nx, ny, nz, size, t);
    }
    DbgReportError(1, "FF_raycast_generic_obj: bad shape %d\n", (long)s->type);
    return 0;
}

fix raycast_2DB310(fix *m)
{
    fix d = FIXMUL(FIXMUL(m[0], m[4]), m[8]);
    d -= FIXMUL(FIXMUL(m[0], m[7]), m[5]);
    d += FIXMUL(FIXMUL(m[3], m[7]), m[2]);
    d -= FIXMUL(FIXMUL(m[3], m[1]), m[8]);
    d += FIXMUL(FIXMUL(m[6], m[1]), m[5]);
    d -= FIXMUL(FIXMUL(m[6], m[4]), m[2]);
    return d;
}

/* plane through p[0..2] (3 points of 3 fixes): *t = where the ray (a, b, c) + t * (dx, dy, dz)
 * meets it; returns TRUE when the start is on the inner side (det <= 0). */
bool raycast_2DB3B4(fix *p, fix a, fix b, fix c, fix dx, fix dy, fix dz, fix *t)
{
    fix m[9], d1, d2;

    m[0] = p[0] - p[6];
    m[1] = p[1] - p[7];
    m[2] = p[2] - p[8];
    m[3] = p[3] - p[6];
    m[4] = p[4] - p[7];
    m[5] = p[5] - p[8];
    m[6] = a - p[6];
    m[7] = b - p[7];
    m[8] = c - p[8];
    d1 = raycast_2DB310(m);
    m[6] = -dx;
    m[7] = -dy;
    m[8] = -dz;
    d2 = raycast_2DB310(m);
    if (ABS(d1) >> 16 >= ABS(d2) / 2)
        *t = d1 > 0 ? 0x20000 : (fix)0xFFFF0000;
    else
        *t = FIXDIV(d1, d2);
    return d1 <= 0;
}

bool raycast_2DB4B4(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix nx, fix ny, fix nz,
                    fix size, fix *tout)
{
    fix *h = SHAPE_TAIL(s);             /* heights of the 4 corners */
    fix *pt = &s->pt[0][0];
    fix sn, cs;
    fix lx, ly, lz, nlx, dlx, dly, dlz, dx, dy, nz_;
    fix p[9], T[6], tmin, tmax;
    uchar in[6];
    long i;

    dx = x - pos[0];
    dy = y - pos[1];
    lz = FIXMUL(z, 0xAAA) - pos[2];
    nlx = nx - pos[0];
    fix ndy = ny - pos[1];
    nz_ = FIXMUL(nz, 0xAAA) - pos[2];
    fix_sincos(*(ushort *)&pos[3], &sn, &cs);
    lx = FIXMUL(dx, cs) + FIXMUL(dy, sn);
    ly = FIXMUL(dy, cs) - FIXMUL(x - pos[0], sn);
    nlx = FIXMUL(nlx, cs) + FIXMUL(ndy, sn);
    dly = FIXMUL(ndy, cs) - FIXMUL(nx - pos[0], sn) - ly;
    dlx = nlx - lx;
    dlz = nz_ - lz;
    /* top: corners 0, 1, 2 at their heights */
    p[0] = pt[0]; p[1] = pt[1]; p[2] = h[0];
    p[3] = pt[2]; p[4] = pt[3]; p[5] = h[1];
    p[6] = pt[4]; p[7] = pt[5]; p[8] = h[2];
    in[0] = raycast_2DB3B4(p, lx, ly, lz, dlx, dly, dlz, &T[0]);
    /* walls: edge i -> i + 1, vertical */
    p[0] = pt[0]; p[1] = pt[1]; p[2] = 0;
    p[3] = pt[2]; p[4] = pt[3]; p[5] = 0;
    p[6] = pt[2]; p[7] = pt[3]; p[8] = h[1];
    in[1] = raycast_2DB3B4(p, lx, ly, lz, dlx, dly, dlz, &T[1]);
    p[0] = pt[2]; p[1] = pt[3]; p[2] = 0;
    p[3] = pt[4]; p[4] = pt[5]; p[5] = 0;
    p[6] = pt[4]; p[7] = pt[5]; p[8] = h[2];
    in[2] = raycast_2DB3B4(p, lx, ly, lz, dlx, dly, dlz, &T[2]);
    p[0] = pt[4]; p[1] = pt[5]; p[2] = 0;
    p[3] = pt[6]; p[4] = pt[7]; p[5] = 0;
    p[6] = pt[6]; p[7] = pt[7]; p[8] = h[3];
    in[3] = raycast_2DB3B4(p, lx, ly, lz, dlx, dly, dlz, &T[3]);
    p[0] = pt[6]; p[1] = pt[7]; p[2] = 0;
    p[3] = pt[0]; p[4] = pt[1]; p[5] = 0;
    p[6] = pt[0]; p[7] = pt[1]; p[8] = h[0];
    in[4] = raycast_2DB3B4(p, lx, ly, lz, dlx, dly, dlz, &T[4]);
    /* floor z = 0 (corners 2, 1, 0) */
    p[0] = pt[4]; p[1] = pt[5]; p[2] = 0;
    p[3] = pt[2]; p[4] = pt[3]; p[5] = 0;
    p[6] = pt[0]; p[7] = pt[1]; p[8] = 0;
    in[5] = raycast_2DB3B4(p, lx, ly, lz, dlx, dly, dlz, &T[5]);
    tmax = 0x10000;
    tmin = 0;
    for (i = 0; i < 6; i++) {
        if (in[i]) {
            if (T[i] >= 0 && tmax > T[i])
                tmax = T[i];
        } else if (T[i] < 0)
            tmax = T[i];
        else if (tmin < T[i])
            tmin = T[i];
    }
    if (tmin > tmax || tmin > 0x10000)
        return 0;
    *tout = tmin;
    return 1;
}

bool raycast_2DB8D8(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix nx, fix ny, fix nz,
                    fix size, fix *tout)
{
    fix r = SHAPE_TAIL(s)[0];
    fix px = x - pos[0], py = y - pos[1], pz = FIXMUL(z, 0xAAA) - pos[2];
    fix dx = (nx - pos[0]) - px;
    fix dy = (ny - pos[1]) - py;
    fix dz = (FIXMUL(nz, 0xAAA) - pos[2]) - pz;
    fix a, b, c, disc, sq, t;

    a = FIXMUL(dx, dx) + FIXMUL(dy, dy) + FIXMUL(dz, dz);
    b = FIXMUL(dx, px) + FIXMUL(dy, py) + FIXMUL(dz, pz);
    c = FIXMUL(px, px) + FIXMUL(py, py) + FIXMUL(pz, pz) - FIXMUL(r, r);
    disc = FIXMUL(b, b) - FIXMUL(a, c);
    if (disc <= 0)
        return 0;
    if (a > 0) {
        if (-b < 0)
            return 0;
    } else if (-b > 0)
        return 0;
    sq = fix_sqrt(disc);
    if (ABS(-b - sq) >> 16 >= ABS(a) / 2)
        return 0;
    t = FIXDIV(-b - sq, a);
    if (t < 0 || t > 0x10000)
        return 0;
    *tout = t;
    return 1;
}

bool RaycastCylinder(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix nx, fix ny, fix nz,
                     fix size, fix *tout)
{
    fix *tail = SHAPE_TAIL(s);
    fix px = x - pos[0], py = y - pos[1];
    fix z0 = FIXMUL(z, 0xAAA) - pos[2];
    fix ex = nx - pos[0], ey = ny - pos[1];
    fix z1 = FIXMUL(nz, 0xAAA) - pos[2];
    fix r = tail[0], top = tail[1];
    fix dz, dx, dy, a, b, c, disc, sq, inv, t0, t1, th = 0;
    uchar cap = 0;

    if (z0 > top && top < z1)
        return 0;                       /* above the top at both ends */
    dz = z1 - z0;
    dx = ex - px;
    dy = ey - py;
    a = FIXMUL(dx, dx) + FIXMUL(dy, dy);
    b = FIXMUL(px, dx) + FIXMUL(py, dy);
    c = FIXMUL(px, px) + FIXMUL(py, py) - FIXMUL(r, r);
    disc = FIXMUL(b, b) - FIXMUL(a, c);
    if (a < 8)
        return 0;                       /* (nearly) vertical ray */
    if (disc < 0)
        return 0;
    sq = fix_sqrt(disc);
    inv = FIXDIV(0x10000, a);
    t0 = FIXMUL(-b - sq, inv);
    t1 = FIXMUL(sq - b, inv);
    if (t1 < 0 || t0 > 0x10000)
        return 0;
    if (z0 > top && top > z1 && ABS(top - z0) >> 16 < ABS(dz) / 2) {
        th = FIXDIV(top - z0, dz);      /* enters through the top */
        cap = 1;
    }
    if (cap) {
        if (t0 <= th) {
            if (t1 <= th)
                return 0;
            t0 = th;
        }
    } else if (t0 <= 0)
        t0 = 0;
    *tout = t0;
    return 1;
}
