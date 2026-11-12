/* pathfind.c -- Terra Nova path planner (module M013, 0x25BD30-0x25EB5F).
 * Decompiled from __FF.EXE v1.09.  Called by the squad / unit AI (G4a, G4b) and PathPlan (M012).
 *
 * The searches work on a window of the map (path_25D13C sets path_ox/oy/x1/y1/w/h) with one
 * PathNode per square in path_pool (16 bytes, ffmem stack memory), a bitmap of squares that have
 * a node (path_seen) and the open list path_heap (M014).  Steps go to the 4 neighbours
 * (path_dirs[1..4]: up, right, down, left).  path_25C704 prices one step from the heights and
 * terrain kinds of the two squares (path_cost_tab per mode, path_slope_cost per height
 * difference, -1 = impassable); path_25CA70 is the heuristic.  A found path is written into a
 * path record (0x309 bytes, see g4_c2.h) by path_25CC1C, which follows the parent chain(s).
 *
 *   0x25BD30 path_25BD30  rotate a point by an angle, rounded (unreferenced)
 *   0x25BDBC path_25BDBC  rotate (x, y) by an angle
 *   0x25BE1C path_25BE1C  rotate (x, y) by an angle, add an origin, clamp to the map
 *   0x25BEB0 (data)       step table path_dirs {0,0},{0,-1},{1,0},{0,1},{-1,0}; 0x25BEC8 is
 *                         data as well (4 bytes 00 03 00 02, pointed to by 0x359914)
 *   0x25BED0 path_25BED0  create the two open lists
 *   0x25BF04 path_25BF04  bidirectional search between two squares (fine grid when near,
 *                         else coarse); falls back to the open node nearest to b
 *   0x25C52C path_25C52C  create the first node of a search at a square and queue it
 *   0x25C6A8 path_25C6A8  pyth distance of two squares times k (fix)
 *   0x25C704 path_25C704  price of one step (returns the step kind)
 *   0x25CA34 path_25CA34  price of one step on the fine grid without a node
 *   0x25CA70 path_25CA70  heuristic from a square to the target
 *   0x25CC1C path_25CC1C  write the path(s) ending at a / b into a path record
 *   0x25CEDC path_25CEDC  length of the parent chain of a node (cut at 0x100)
 *   0x25CFB4 path_25CFB4  headings of the record's entries
 *   0x25D114 path_25D114  copy a path record (unreferenced)
 *   0x25D13C path_25D13C  set up the search window for a and b
 *   0x25D35C path_25D35C  refine the record's next coarse step on the fine grid
 *   0x25D9A0 path_25D9A0  search from a until within r of b (coarse grid, cost order)
 *   0x25DDEC path_25DDEC  A* from a to b on the coarse grid (crossing water squares through
 *                         path_25E36C), or path_25BF04 when near
 *   0x25E36C path_25E36C  price of a coarse step through the fine squares of the cell
 *
 * Quirks kept:
 *  - every search writes heap->ents[pos].node after inserting without (path_25C52C,
 *    PathPlan, path_25E36C) or with a -1 check; without it a full heap makes it write 4 bytes
 *    before the entry array;
 *  - path_25C704 returns an uninitialised byte when the step is impassable (callers ignore it;
 *    0 here) and loses the "impassable" of modes 2 / 3 for height steps 20..39;
 *  - path_25D13C's window width is x1 - x0 (one column short; the rows overlap by one square)
 *    and the searches accept x == x1 but not y == y1;
 *  - path_25CEDC cuts a chain longer than 0x100 by clearing the parent of the node it is about
 *    to visit;
 *  - path_25BF04 compares the open list pointer with the start point before using its fallback;
 *  - path_25D35C's "lower the key" branch is dead (the position is always -1);
 *  - path_25DDEC passes the fine goal (not the coarse one) to path_25D13C (ignored on the
 *    coarse grid);
 *  - path_25E36C leaves bits 14-15 of its local nodes uninitialised (never read).
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_c2.h"
#include "g5_a.h"     /* added at the G5-G8 merge */
#include "g9_b.h"     /* added at the G9/G10 merge */

#ifndef TN_LINK_ORIGINAL_DATA
uchar path_trace_lines;
ulong path_none = 0xFFFFFFFF;
long path_map_h = 0x200, path_map_w = 0x200;
long path_ox, path_oy;
short path_obj_a, path_obj_b;
PathHeap *path_heap, *path_heap_local;
uchar path_trace;
fix path_cost_tab[4][6] = {      /* data of __FF.EXE (unchanged at run time) */
    { 0x20000, 0x18000, 0x40000, 0x8000, 0x4000, 0x10000 },
    { 0x80000, 0x50000, 0x140000, 0x20000, 0x14000, 0x50000 },
    { 0x190000, 0x10000, 0x320000, 0x64000, 0x4000, 0xC8000 },
    { 0x80000, 0x10000, 0xA0000, 0x20000, 0x4000, 0x28000 } };
short path_slope_cost[40] = { 4, 4, 4, 4, 4, 4, 8, 12, 16, 20, 60, 62, 64, 66, 68, 70, 72, 74,
    76, 78, 80, 82, 84, 86, 88, 90, 92, 94, 96, 98, 100, 101, 102, 103, 104, 105, 106, 107,
    108, 109 };
uchar *path_los_seen;
uchar path_seen[0x800];
long path_w, path_x1, path_y1, path_h;
PathNode *path_pool;
uchar path_sel;
ulong path_41DD80;
#endif

static const short path_dirs[5][2] = { {0, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, 0} };

#define labs(v) ((long)(v) < 0 ? -(long)(v) : (long)(v))
#define PX G4C2_PX
#define PY G4C2_PY
#define PT G4C2_PT
#define FIXMUL(a, b) ((fix)(((long long)(long)(a) * (long)(b)) >> 16))

/* the node of a square / its bit in path_seen (index may be out of the window: no checks) */
#define IDX(x, y)    (((long)(y) - path_oy) * path_w + ((long)(x) - path_ox))
#define NODE(x, y)   (path_pool + IDX(x, y))
#define SEEN(x, y)   ((path_seen[IDX(x, y) >> 3] >> (IDX(x, y) & 7)) & 1)
#define SETSEEN(x, y) (path_seen[IDX(x, y) >> 3] |= (uchar)(1 << (IDX(x, y) & 7)))

#define DIR(n)       (((n)->bits >> 10) & 7)
#define SIDE(n)      (((n)->bits >> 13) & 1)
#define SETDIR(n, d) ((n)->bits = (ushort)(((n)->bits & 0xE3FF) | (((d) & 7) << 10)))
#define COPYSIDE(n, p) ((n)->bits = (ushort)(((n)->bits & 0xDFFF) | ((p)->bits & 0x2000)))

/* a cell of grid sel (3 bytes: type, height << 4) */
static uchar *cell(long sel, long x, long y)
{
    return *(uchar **)(game_map_35BFB4 + 0x4A + (sel * 0x201 + y) * 4) + x * 3;
}

static short cell_z(long sel, long x, long y)
{
    return (short)(*(short *)(cell(sel, x, y) + 1) >> 4);
}

static long kind_bit(uchar type)
{
    return 1L << game_41DD58[type & 0x3F];
}

#define MAP_X0 (*(short *)(game_map_35BFB4 + 8))
#define MAP_Y0 (*(short *)(game_map_35BFB4 + 0xA))

/* x and y of record entry e */
static long ent_x(const uchar *e) { return *(const ushort *)e & 0x1FF; }
static long ent_y(const uchar *e) { return (*(const ushort *)e >> 9) + ((e[2] & 3) << 7); }

static void ent_set(uchar *e, ulong pt)
{
    *(ushort *)e = (ushort)((*(ushort *)e & 0xFE00) | (PX(pt) & 0x1FF));
    e[1] &= 1;
    *(ushort *)e |= (ushort)((PY(pt) & 0x7F) << 9);
    e[2] = (uchar)((e[2] & 0xFC) | ((PY(pt) >> 7) & 3));
}

/* ------------------------------------------------------------------ geometry helpers */

ulong path_25BD30(ulong pt, fixang a)
{
    fix s, c, x = (fix)PX(pt) << 16, y = (fix)PY(pt) << 16;
    short ox, oy;

    fix_sincos(a, &s, &c);
    ox = (short)((FIXMUL(x, c) - FIXMUL(y, s) + 0x8000) >> 16);
    oy = (short)((FIXMUL(x, s) + FIXMUL(y, c) + 0x8000) >> 16);
    return PT(ox, oy);
}

void path_25BDBC(fix x, fix y, fixang a, fix *ox, fix *oy)
{
    fix s, c;

    fix_sincos(a, &s, &c);
    *ox = FIXMUL(x, c) - FIXMUL(y, s);
    *oy = FIXMUL(x, s) + FIXMUL(y, c);
}

void path_25BE1C(fix x, long y, long a, fix bx, fix by, fix *ox, fix *oy)
{
    fix rx, ry, lim;

    path_25BDBC(x, y, (fixang)a, &rx, &ry);
    rx += bx;
    ry += by;
    if (rx < 0x10000)
        rx = 0x10000;
    else {
        lim = (path_map_w - 1) << 16;
        if (!(lim > rx))
            rx = lim;
    }
    if (ry < 0x10000)
        ry = 0x10000;
    else {
        lim = (path_map_h - 1) << 16;
        if (!(lim > ry))
            ry = lim;
    }
    *ox = rx;
    *oy = ry;
}

void path_25BED0(void)
{
    if (path_heap == NULL)
        path_heap = pathheap_25EB60(0x200);
    if (path_heap_local == NULL)
        path_heap_local = pathheap_25EB60(0x40);
}

/* ------------------------------------------------------------------ window, nodes, costs */

uchar path_25D13C(ulong a, ulong b)
{
    short x0, y0, x1, y1, dx, dy, big, m;
    long q, quarter;

    if (path_sel == 0) {
        path_ox = MAP_X0;
        path_oy = MAP_Y0;
        path_x1 = path_ox + 0x80;
        path_w = 0x80;
        path_h = 0x80;
        path_y1 = path_oy + 0x80;
        return 1;
    }
    x0 = PX(a); x1 = PX(b);
    if (x0 > x1) { short t = x0; x0 = x1; x1 = t; }
    y0 = PY(a); y1 = PY(b);
    if (y0 > y1) { short t = y0; y0 = y1; y1 = t; }
    dx = (short)(x1 - x0);
    dy = (short)(y1 - y0);
    big = dx > dy ? dx : dy;
    if ((long)dx * dy > 0x4000)
        return 0;
    quarter = ((long)dx + dy + 3) / 4;
    if (big > 0x80) {
        long d = (long)dx - dy;
        q = (short)(long_sqrt((short)(d * d / 4 + 0x4000)));
        q = q / 2;
    } else
        q = 0x40;
    m = (short)(q - quarter);
    x0 = (short)(x0 - m); y0 = (short)(y0 - m);
    x1 = (short)(x1 + m); y1 = (short)(y1 + m);
    if (x0 < 0) {
        x1 = (short)(x1 - x0);
        x0 = 0;
    } else if (x1 >= 0x200) {
        x0 = (short)(x0 + (0x1FF - x1));
        x1 = 0x1FF;
    }
    if (y0 < 0) {
        y1 = (short)(y1 - y0);
        y0 = 0;
    } else if (y1 >= 0x200) {
        y0 = (short)(y0 + (0x1FF - y1));
        y1 = 0x1FF;
    }
    path_ox = x0;
    path_oy = y0;
    path_x1 = x1;
    path_y1 = y1;
    path_w = path_x1 - path_ox;
    path_h = path_y1 - path_oy;
    return 1;
}

uchar path_25C52C(ulong pt, ulong unused, long side, long far, long dir, long unused2)
{
    PathNode *n = NODE(PX(pt), PY(pt));
    long i;

    (void)unused; (void)unused2;
    n->pt = pt;
    n->parent = 0xFFFFFFFF;
    SETSEEN(PX(pt), PY(pt));
    n->bits = (ushort)((n->bits & 0xDFFF) | ((side & 1) << 13));
    SETDIR(n, dir);
    n->g = 0;
    n->z = cell_z(path_sel, PX(pt), PY(pt));
    n->h = (uchar)far ? 30000 : 0;
    i = pathheap_25EBC0(path_heap, n, (short)(n->g + n->h), 0);
    n->bits = (ushort)((n->bits & 0xFC00) | (i & 0x3FF));
    path_heap->ents[(short)i].node = n;
    return 1;
}

long path_25C6A8(ulong a, ulong b, short k)
{
    long dx = PX(a) - PX(b), dy = PY(a) - PY(b);
    fix d;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    d = fix_pyth_dist((fix)(short)dx << 16, (fix)(short)dy << 16);
    return (d * k + 0x8000) >> 16;
}

uchar path_25C704(ulong from, ulong to, PathNode *fn, PathNode *tn, long dir, short *cost,
                  short *mult, short *z, long mode)
{
    long zf, zt, d, onobj = 0, kb;
    fix f;
    uchar ret = 0;
    const fix *tab = path_cost_tab[(uchar)mode];

    (void)tn;
    if (fn != NULL)
        zf = fn->z;
    else
        zf = cell_z(path_sel, PX(from), PY(from));
    zt = cell_z(path_sel, PX(to), PY(to));
    if (path_sel == 1) {
        short o = map_295000(to);
        if (o != 0) {
            uchar t = game_entities[o].type;
            if (t == 0x3A || t == 0x42 || t == 0x43) {
                GameEntity *e = &game_entities[o];
                ShapeItem *s = interact_2973E8(e->cls, e->type);
                fix top = *(fix *)((uchar *)SHAPE_EXT(s) + 0x50) + e->z;
                long h = (top * 24) >> 16;
                if (zf - zt > h - zf || h - zt < 0x18) {
                    zt = h;
                    onobj = 1;
                }
            } else if (o != path_obj_a && o != path_obj_b) {
                *cost = -1;
                f = 0x320000;
                goto priced;
            }
        }
    }
    kb = kind_bit(*cell(path_sel, PX(to), PY(to)));
    if (onobj || (kb & path_41DD80))
        f = tab[1];
    else if (kb & terrain_bitmask)
        f = tab[2];
    else
        f = tab[0];
    d = zt - zf;
    if (d < 0)
        d = -d;
    if (path_sel == 0)
        d >>= 2;
    if (((uchar)mode == 2 || (uchar)mode == 3) && d >= 0x14)
        *cost = -1;
    if (d >= 0x28) {
        if ((uchar)mode != 0)
            *cost = -1;
        else
            *cost = 0x6E;
    } else
        *cost = path_slope_cost[d];
priced:
    if (*cost != -1) {
        if (fn != NULL && (fn->bits & 0x1C00) && ((DIR(fn) + (uchar)dir) & 1)) {
            *cost = (short)(*cost * 3);
            *mult = 3;
            ret = 0;
        } else {
            *cost = (short)(*cost << 3);
            *mult = 8;
            ret = (uchar)dir;
        }
        *cost = (short)(FIXMUL(f, *cost) / 8);
    }
    *z = (short)zt;
    return ret;
}

short path_25CA34(ulong a, ulong b, long dir, long mode)
{
    short cost, mult, z;

    path_sel = 1;
    path_25C704(a, b, NULL, NULL, (uchar)dir, &cost, &mult, &z, (uchar)mode);
    return cost;
}

long path_25CA70(long side, ulong a, ulong b, long mode)
{
    long d, dz, kb;
    fix f, r;
    const fix *tab = path_cost_tab[(uchar)mode];

    (void)side;
    d = path_25C6A8(a, b, 8);
    dz = NODE(PX(a), PY(a))->z - cell_z(path_sel, PX(b), PY(b));
    if (dz < 0)
        dz = -dz;
    dz = dz / 8;
    d = dz * 4 + d;
    kb = kind_bit(*cell(path_sel, PX(a), PY(a)));   /* terrain of a (height of b) */
    if (kb & terrain_bitmask)
        f = (tab[5] + tab[3]) >> 1;
    else if (kb & path_41DD80)
        f = (tab[4] + tab[3]) >> 1;
    else
        f = tab[3];
    r = FIXMUL((short)d, f);
    return FIXMUL((short)r, 0x30000);
}

/* ------------------------------------------------------------------ path records */

long path_25CEDC(ulong pt)
{
    ulong p = NODE(PX(pt), PY(pt))->parent;
    long n = 0;

    while (!(PX(p) == -1 && PY(p) == -1)) {
        n++;
        if ((short)n > 0x100) {
            NODE(PX(p), PY(p))->parent = 0xFFFFFFFF;
            break;
        }
        p = NODE(PX(p), PY(p))->parent;
    }
    return n;
}

static void ent_store(uchar *e, ulong pt)
{
    ent_set(e, pt);
    e[2] = (uchar)((e[2] & 0xFB) | ((path_sel == 1) << 2));
}

uchar path_25CC1C(void *rec, ulong a, ulong b)
{
    long na, nb, i, k;
    short start;
    ulong cur;
    PathNode *n;

    na = (short)path_25CEDC(a);
    if (PX(b) == -1 && PY(b) == -1)
        start = 0;
    else {
        n = NODE(PX(b), PY(b));
        nb = (short)path_25CEDC(b);
        if (na + nb + 2 > 0x100)
            return 0;
        start = (short)(nb + 1);
        cur = b;
        for (i = nb, k = nb; i >= 0; i--, k--) {
            ent_store(PATH_E(rec, k), cur);
            cur = n->parent;
            n = NODE(PX(cur), PY(cur));
        }
    }
    cur = a;
    n = NODE(PX(a), PY(a));
    for (i = na, k = start; i >= 0; i--, k++) {
        ent_store(PATH_E(rec, k), cur);
        cur = n->parent;
        n = NODE(PX(cur), PY(cur));
    }
    PATH_W(rec, 5) = (short)(k - 1);
    PATH_W(rec, 7) = (short)(k - 2);
    path_25CFB4(rec);
    return 1;
}

void path_25CFB4(void *rec)
{
    short k;
    long hi, lo, last;
    uchar *ek, *el, *eh;
    fixang ang;

    for (k = (short)(PATH_W(rec, 5) - 1); k >= 0; k--) {
        last = PATH_W(rec, 5) - 1;
        hi = k + 4;
        if (!(hi < last))
            hi = last;
        lo = k - 4;
        if (!(lo > 0))
            lo = 0;
        ek = PATH_E(rec, k);
        while (((PATH_E(rec, (short)hi)[2] >> 2) & 1) != ((ek[2] >> 2) & 1))
            hi--;
        while (((PATH_E(rec, (short)lo)[2] >> 2) & 1) != ((ek[2] >> 2) & 1))
            lo++;
        el = PATH_E(rec, (short)lo);
        eh = PATH_E(rec, (short)hi);
        ang = fix_atan2((fix)(short)(ent_y(el) - ent_y(eh)) << 16,
                        (fix)(short)(ent_x(el) - ent_x(eh)) << 16);
        ek[2] = (uchar)((ek[2] & 7) | ((((ang & 0xFFFF) >> 11) & 0x1F) << 3));
    }
}

void path_25D114(const void *src, void *dst)
{
    const uchar *s = src;
    uchar *d = dst;
    int i;

    if (src == NULL || dst == NULL)
        return;
    for (i = 0; i < 0x309; i++)
        d[i] = s[i];
}

/* ------------------------------------------------------------------ searches */

/* fill a new node (all searches use the same order of stores) */
static void node_step(PathNode *nn, ulong nb, PathNode *n, ulong cur, long rc, short z, short cost)
{
    nn->pt = nb;
    COPYSIDE(nn, n);
    nn->z = z;
    nn->parent = cur;
    SETDIR(nn, rc);
    nn->g = (short)(n->g + cost);
}

static void heap_put(PathNode *nn, short key, int check)
{
    long i = pathheap_25EBC0(path_heap, nn, key, 1);
    if (check && (short)i == -1)
        return;
    path_heap->ents[(short)i].node = nn;
}

uchar path_25BF04(ulong a, ulong b, long x, void *rec, long near, long mode)
{
    long iter, i, ad;
    uchar adjacent, ok = 0, rc;
    PathNode *n, *nn, *best;
    long bestd, d;
    short bestkey;

    if (rec == NULL)
        return 0;
    if (PX(a) == PX(b) && PY(a) == PY(b))
        return 0;
    if (PX(a) <= 0 || PY(a) <= 0 || PX(b) <= 0 || PY(b) <= 0)
        return 0;
    if (PX(a) >= 0x200 || PY(a) >= 0x200 || PX(b) >= 0x200 || PY(b) >= 0x200)
        return 0;
    path_obj_a = map_295000(a);
    path_obj_b = map_295000(b);
    path_pool = ffmem_2C4F40(0x40000);
    if (!(uchar)near) {
        ad = labs((long)PY(a) - PY(b)) + labs((long)PX(a) - PX(b));
        if (ad < 0x14)
            near = 1;
    }
    if ((uchar)near)
        path_sel = 1;
    else {
        short mx = MAP_X0, my = MAP_Y0;
        a = PT((PX(a) >> 2) + mx, (PY(a) >> 2) + my);
        b = PT((PX(b) >> 2) + mx, (PY(b) >> 2) + my);
        path_sel = 0;
    }
    adjacent = labs((long)PY(a) - PY(b)) + labs((long)PX(a) - PX(b)) <= 3;
    if (!path_25D13C(a, b))
        goto done;
    pathheap_25EBB8(path_heap);
    memset(path_seen, 0, 0x800);
    if (!path_25C52C(a, b, 1, 0, (uchar)x, (uchar)mode))
        goto done;
    if (!path_25C52C(b, a, 0, adjacent, 0, (uchar)mode))
        goto done;
    for (iter = 0; path_heap->count > 0 && iter < 0x800; iter++) {
        ulong cur;
        uchar dir;

        n = pathheap_25EDAC(path_heap);
        n->bits |= 0x3FF;
        cur = n->pt;
        for (dir = 1; dir <= 4; dir++) {
            ulong nb = PT(PX(cur) + path_dirs[dir][0], PY(cur) + path_dirs[dir][1]);
            long nx = PX(nb), ny = PY(nb);
            short cost, mult, z;

            if (nb == n->parent)
                continue;
            if (nx < path_ox || nx > path_x1)
                continue;
            if (ny < path_oy || ny >= path_y1)
                continue;
            nn = NODE(nx, ny);
            rc = path_25C704(cur, nb, n, nn, dir, &cost, &mult, &z, (uchar)mode);
            if (cost == -1)
                continue;
            if (SEEN(nx, ny)) {
                if (SIDE(nn) != !(n->bits & 0x2000))
                    continue;
                if (n->bits & 0x2000)
                    ok = path_25CC1C(rec, cur, nb);
                else
                    ok = path_25CC1C(rec, nb, cur);
                if (!ok)
                    goto done;
                *(ulong *)((uchar *)rec + 1) = b;
                ok = 1;
                goto done;
            }
            nn->pt = nb;
            COPYSIDE(nn, n);
            nn->z = z;
            nn->h = (short)path_25CA70(SIDE(nn), nb, SIDE(nn) ? b : a, (uchar)mode);
            SETSEEN(nx, ny);
            nn->parent = cur;
            SETDIR(nn, rc);
            nn->z = z;
            nn->g = (short)(n->g + cost);
            {
                short key = (short)(nn->g + nn->h);
                if (key >= 30000)
                    continue;
                heap_put(nn, key, 1);
            }
        }
    }
    best = NULL;
    bestd = 0x4000;
    bestkey = 0x4000;
    for (i = 0; i < path_heap->count; i++) {
        PathHeapEnt *e = &path_heap->ents[i];
        PathNode *p = e->node;
        if (!(p->bits & 0x2000))
            continue;
        d = fix_fast_pyth_dist(PX(p->pt) - PX(b), PY(p->pt) - PY(b));
        if (d > bestd)
            continue;
        if (d < bestd || e->key < bestkey) {
            bestkey = e->key;
            best = e->node;
            bestd = d;
        }
    }
    ok = 0;
    if (best == NULL)
        goto done;
    if ((ulong)path_heap == a)
        goto done;
    if (!path_25CC1C(rec, best->pt, 0xFFFFFFFF))
        goto done;
    *(ulong *)((uchar *)rec + 1) = 0xFFFFFFFF;
    ok = 1;
done:
    ffmem_2C4FE4(path_pool);
    return ok;
}

uchar path_25D35C(ulong start, long x, void *rec, long mode)
{
    short k;
    ulong t1, t2, tgt;
    long iter;
    uchar ok = 0;

    if (rec == NULL)
        return 0;
    path_obj_a = map_295000(start);
    path_obj_b = 0;
    path_pool = ffmem_2C4F40(0x40000);
    k = PATH_W(rec, 7);
    t1 = PT(ent_x(PATH_E(rec, k)), ent_y(PATH_E(rec, k)));
    if (PATH_W(rec, 7) > 0)
        t2 = PT(ent_x(PATH_E(rec, k - 1)), ent_y(PATH_E(rec, k - 1)));
    else
        t2 = t1;
    tgt = PT((PX(t2) - MAP_X0) << 2, (PY(t2) - MAP_Y0) << 2);
    if (path_trace) {
        main_245468();
        main_245468();
    }
    path_sel = 1;
    if (!path_25D13C(start, tgt))
        goto done;
    pathheap_25EBB8(path_heap);
    memset(path_seen, 0, 0x800);
    if (!path_25C52C(start, tgt, 1, 0, (uchar)x, (uchar)mode))
        goto done;
    for (iter = PATH_W(rec, 7); path_heap->count > 0 && iter < 0x800; iter++) {
        PathNode *n = pathheap_25EDAC(path_heap);
        ulong cur;
        uchar dir;

        n->bits |= 0x3FF;
        cur = n->pt;
        if (path_trace)
            main_245468();
        for (dir = 1; dir <= 4; dir++) {
            ulong nb = PT(PX(cur) + path_dirs[dir][0], PY(cur) + path_dirs[dir][1]);
            long nx = PX(nb), ny = PY(nb);
            short cost, mult, z, hpos, key;
            PathNode *nn;
            uchar rc;
            long i;

            if (nb == n->parent)
                continue;
            if (nx < path_ox || nx > path_x1)
                continue;
            if (ny < path_oy || ny >= path_y1)
                continue;
            nn = NODE(nx, ny);
            rc = path_25C704(cur, nb, n, nn, dir, &cost, &mult, &z, (uchar)mode);
            if (cost == -1)
                continue;
            if (SEEN(nx, ny))
                continue;
            if (path_trace)
                main_245468();
            nn->pt = nb;
            COPYSIDE(nn, n);
            nn->z = z;
            nn->h = (short)path_25CA70(1, nb, tgt, (uchar)mode);
            SETSEEN(nx, ny);
            nn->parent = cur;
            hpos = -1;
            SETDIR(nn, rc);
            nn->z = z;
            nn->g = (short)(n->g + cost);
            key = (short)(nn->g + nn->h);
            if (((nx >> 2) + MAP_X0 == PX(t1) && (ny >> 2) + MAP_Y0 == PY(t1)) ||
                ((nx >> 2) + MAP_X0 == PX(t2) && (ny >> 2) + MAP_Y0 == PY(t2))) {
                /* reached the coarse step: replace the record's entries from k on */
                ulong p = nb;
                short kk = PATH_W(rec, 7);
                uchar *e;

                PATH_W(rec, 5) = PATH_W(rec, 7);
                PATH_W(rec, 7) = (short)(PATH_W(rec, 7) - 1);
                e = PATH_E(rec, kk);
                while (!(PX(p) == -1 && PY(p) == -1)) {
                    ent_set(e, p);
                    e[2] |= 4;
                    p = NODE(PX(p), PY(p))->parent;
                    e += 3;
                    PATH_W(rec, 5)++;
                    PATH_W(rec, 7)++;
                }
                path_25CFB4(rec);
                ok = 1;
                goto done;
            }
            if (key >= 30000)
                continue;
            if (hpos == -1)
                i = pathheap_25EBC0(path_heap, nn, key, 1);
            else
                i = pathheap_25ECCC(path_heap, hpos, key);
            if ((short)i == -1)
                continue;
            path_heap->ents[(short)i].node = nn;
        }
    }
done:
    ffmem_2C4FE4(path_pool);
    return ok;
}

uchar path_25D9A0(ulong a, ulong b, long r, long x, void *rec, long mode)
{
    long r2, r4, iter;
    uchar ok = 0;

    if (rec == NULL)
        return 0;
    path_obj_a = map_295000(a);
    path_obj_b = 0;
    path_pool = ffmem_2C4F40(0x40000);
    path_sel = 0;
    a = PT((PX(a) >> 2) + MAP_X0, (PY(a) >> 2) + MAP_Y0);
    b = PT((PX(b) >> 2) + MAP_X0, (PY(b) >> 2) + MAP_Y0);
    r2 = (r * r) >> 4;
    r4 = r >> 2;
    if (!path_25D13C(a, b))
        goto done;
    pathheap_25EBB8(path_heap);
    memset(path_seen, 0, 0x800);
    if (!path_25C52C(a, a, 1, 0, (uchar)x, (uchar)mode))
        goto done;
    NODE(PX(a), PY(a))->h = 0;
    for (iter = 0; path_heap->count > 0 && iter < 0x800; iter++) {
        PathNode *n = pathheap_25EDAC(path_heap);
        ulong cur = n->pt;
        long dx, dy;
        uchar dir;

        n->bits |= 0x3FF;
        dx = PX(b) - PX(cur);
        dy = PY(b) - PY(cur);
        if (labs(dx) <= r4 && labs(dy) <= r4 && dx * dx + dy * dy <= r2) {
            if (!path_25CC1C(rec, cur, path_none))
                goto done;
            *(ulong *)((uchar *)rec + 1) =
                PT((PX(cur) - MAP_X0) << 2, (PY(cur) - MAP_Y0) << 2);
            ok = 1;
            goto done;
        }
        for (dir = 1; dir <= 4; dir++) {
            ulong nb = PT(PX(cur) + path_dirs[dir][0], PY(cur) + path_dirs[dir][1]);
            long nx = PX(nb), ny = PY(nb);
            short cost, mult, z;
            PathNode *nn;
            uchar rc;

            if (n->parent == nb)
                continue;
            if (nx < path_ox || nx > path_x1)
                continue;
            if (ny < path_oy || ny >= path_y1)
                continue;
            if (SEEN(nx, ny))
                continue;
            nn = NODE(nx, ny);
            rc = path_25C704(cur, nb, n, nn, dir, &cost, &mult, &z, (uchar)mode);
            if (cost == -1)
                continue;
            nn->pt = nb;
            COPYSIDE(nn, n);
            nn->parent = cur;
            SETDIR(nn, rc);
            nn->z = z;
            nn->g = (short)(n->g + cost);
            SETSEEN(nx, ny);
            if (nn->g >= 30000)
                continue;
            heap_put(nn, nn->g, 1);
        }
    }
done:
    ffmem_2C4FE4(path_pool);
    return ok;
}

uchar path_25DDEC(ulong a, ulong b, long x, void *rec, long near, long mode)
{
    ulong ca, cb;
    long iter;
    uchar ok = 0;

    if (rec == NULL)
        return 0;
    if (PX(a) == PX(b) && PY(a) == PY(b))
        return 0;
    if ((PX(a) == 0 && PY(a) == 0) || (PX(b) == 0 && PY(b) == 0))
        return 0;
    path_obj_a = map_295000(a);
    path_obj_b = map_295000(b);
    if (!(uchar)near) {
        if (labs((long)PX(a) - PX(b)) + labs((long)PY(a) - PY(b)) < 0x14)
            near = 1;
    }
    if ((uchar)near)
        return path_25BF04(a, b, (uchar)x, rec, (uchar)near, (uchar)mode);
    path_pool = ffmem_2C4F40(0x40000);
    ca = PT((PX(a) >> 2) + MAP_X0, (PY(a) >> 2) + MAP_Y0);
    cb = PT((PX(b) >> 2) + MAP_X0, (PY(b) >> 2) + MAP_Y0);
    path_sel = (uchar)near;
    if (!path_25D13C(ca, b))
        goto done;
    pathheap_25EBB8(path_heap);
    memset(path_seen, 0, 0x800);
    if (!path_25C52C(ca, cb, 1, 0, (uchar)x, (uchar)mode))
        goto done;
    for (iter = 0; path_heap->count > 0 && iter < 0x800; iter++) {
        PathNode *n = pathheap_25EDAC(path_heap);
        ulong cur;
        uchar dir;

        n->bits |= 0x3FF;
        cur = n->pt;
        for (dir = 1; dir <= 4; dir++) {
            ulong nb = PT(PX(cur) + path_dirs[dir][0], PY(cur) + path_dirs[dir][1]);
            long nx = PX(nb), ny = PY(nb);
            short cost, mult, z, flag = 1;
            PathNode *nn;
            uchar rc;

            if (n->parent == nb)
                continue;
            if (nx < path_ox || nx > path_x1)
                continue;
            if (ny < path_oy || ny >= path_y1)
                continue;
            nn = NODE(nx, ny);
            if ((*cell(0, PX(cur), PY(cur)) & 0x40) || (*cell(0, nx, ny) & 0x40))
                rc = path_25E36C(cur, nb, n, nn, DIR(n), &cost, &mult, &flag, &z, (uchar)mode);
            else
                rc = path_25C704(cur, nb, n, nn, DIR(n), &cost, &mult, &z, (uchar)mode);
            if (cost == -1)
                continue;
            if (nb == cb) {
                nn->parent = cur;
                if (!path_25CC1C(rec, nb, path_none))
                    goto done;
                *(ulong *)((uchar *)rec + 1) = b;
                ok = 1;
                goto done;
            }
            if (SEEN(nx, ny))
                continue;
            nn->pt = nb;
            COPYSIDE(nn, n);
            nn->z = z;
            nn->h = (short)path_25CA70(SIDE(nn), nb, SIDE(nn) ? cb : ca, (uchar)mode);
            SETSEEN(nx, ny);
            nn->parent = cur;
            SETDIR(nn, rc);
            nn->z = z;
            nn->g = (short)(n->g + cost);
            nn->bits = (ushort)((nn->bits & 0x3FFF) | ((flag & 3) << 14));
            {
                short key = (short)(nn->g + nn->h);
                if (key >= 30000)
                    continue;
                heap_put(nn, key, 1);
            }
        }
    }
done:
    ffmem_2C4FE4(path_pool);
    return ok;
}

/* The price of the coarse step a -> b when one of the squares is water: a search on the fine
 * squares of cell a (and the cell of b), from the fine square where the path entered a. */
uchar path_25E36C(ulong a, ulong b, PathNode *fn, PathNode *tn, long dir, short *cost,
                  short *mult, short *flag, short *z, long mode)
{
    PathNode loc[8][8];
    uchar seen[8];
    short sx, sy;
    long x0, y0, lx, ly;
    ulong par;
    PathNode *an, *s;
    long i;
    uchar ret;

    (void)tn;
    memset(seen, 0, 8);
    path_sel = 1;
    pathheap_25EBB8(path_heap_local);
    sx = (short)((PX(a) - MAP_X0) << 2);
    sy = (short)((PY(a) - MAP_Y0) << 2);
    x0 = sx - 2;
    y0 = sy - 2;
    an = NODE(PX(a), PY(a));
    par = an->parent;
    if (PX(par) == -1 && PY(par) == -1) {
        sx++;
        sy++;
    } else if (PX(par) < PX(a))
        sy = (short)(sy + (an->bits >> 14));
    else if (PX(par) > PX(a)) {
        sx = (short)(sx + 3);
        sy = (short)(sy + (an->bits >> 14));
    } else if (PY(par) < PY(a))
        sx = (short)(sx + (an->bits >> 14));
    else if (PY(par) > PY(a)) {
        sy = (short)(sy + 3);
        sx = (short)(sx + (an->bits >> 14));
    } else {
        sx++;
        sy++;
    }
    lx = sx - x0;
    ly = sy - y0;
    s = &loc[lx][ly];
    s->parent = 0xFFFFFFFF;
    s->bits |= 0x2000;
    s->pt = PT(sx, sy);
    seen[(lx + ly * 8) >> 3] |= (uchar)(1 << ((lx + ly * 8) & 7));
    s->g = 0;
    SETDIR(s, dir);
    s->h = 0;
    i = pathheap_25EBC0(path_heap_local, s, 0, 0);
    s->bits = (ushort)((s->bits & 0xFC00) | (i & 0x3FF));
    s->z = fn->z;
    *z = s->z;
    path_heap_local->ents[(short)(s->bits << 6) >> 6].node = s;
    while (path_heap_local->count > 0) {
        PathNode *n = pathheap_25EDAC(path_heap_local);
        ulong cur = n->pt;
        uchar d;

        if ((PX(cur) >> 2) + MAP_X0 == PX(b) && (PY(cur) >> 2) + MAP_Y0 == PY(b)) {
            ret = (uchar)DIR(n);
            *flag = (short)((PX(a) == PX(b) ? PX(cur) : PY(cur)) & 3);
            *cost = (short)(n->g >> 2);
            *mult = 0;
            *z = n->z;
            goto out;
        }
        for (d = 1; d <= 4; d++) {
            ulong nb = PT(PX(cur) + path_dirs[d][0], PY(cur) + path_dirs[d][1]);
            long nx = PX(nb), ny = PY(nb), bi;
            short c, m, nz;
            PathNode *nn;
            uchar rc;

            if (n->parent == nb)
                continue;
            if (!(nx >= x0 + 2 && nx < x0 + 6 && ny >= y0 + 2 && ny < y0 + 6)) {
                if (!((nx >> 2) + MAP_X0 == PX(b) && (ny >> 2) + MAP_Y0 == PY(b)))
                    continue;
            }
            bi = (nx - x0) + (ny - y0) * 8;
            if ((seen[bi >> 3] >> (bi & 7)) & 1)
                continue;
            nn = &loc[nx - x0][ny - y0];
            rc = path_25C704(cur, nb, n, nn, d, &c, &m, &nz, (uchar)mode);
            if (c == -1)
                continue;
            nn->pt = nb;
            COPYSIDE(nn, n);
            nn->parent = cur;
            SETDIR(nn, rc);
            nn->z = nz;
            nn->g = (short)(n->g + c);
            seen[bi >> 3] |= (uchar)(1 << (bi & 7));
            i = pathheap_25EBC0(path_heap_local, nn, nn->g, 1);
            path_heap_local->ents[(short)i].node = nn;
        }
    }
    *cost = -1;
    ret = 0;
out:
    path_sel = 0;
    return ret;
}
