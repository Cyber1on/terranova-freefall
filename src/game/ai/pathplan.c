/* pathplan.c -- Terra Nova AI path planner, line-of-sight search (module M012, 0x25B380-0x25BD25).
 * Decompiled from __FF.EXE v1.09.
 *
 *   0x25B380 PathPlan  best-first search (fine grid, 4-neighbour, key = cost from the start) for a
 *            square from which the goal is (flag7) or is not (!flag7) in line of sight.
 *
 * Arguments (Watcom: eax, edx, ebx, ecx, then 6 on the stack; ret 0x18):
 *   from  start square            to    unused
 *   dest  goal square (the target to look at)
 *   a4    height offset added to the eye at every square (hoff)
 *   a5    height subtracted from every square on the sight line (hsub)
 *   path  path record (0x309 bytes), filled by path_25CC1C
 *   a7    flag: 1 = find a square that sees the goal, 0 = one that is hidden from it
 *   a8    movement mode (index of path_cost_tab), byte
 *   a9    minimum cost from the start before a square may be accepted
 *   a10   squares closer to the goal than this (map squares) are only kept as a fallback
 * Returns the square chosen (its path is in `path`) or 0xFFFFFFFF.
 *
 * With a7 set and a goal more than 20 squares away (either axis, or Euclidean), it just plans a
 * plain path with path_25D9A0(from, dest, 15, 0, path, a8) and returns path +1 (the end point).
 *
 * Line of sight: the line from the goal to the candidate square is walked with a 16-bit
 * Bresenham that splits each diagonal step in two orthogonal half-steps (order chosen by the sign
 * of the error).  Every square on it gets the slope (height - hsub + hoff) / steps from the goal
 * as 8.8 fixed point clamped to 16 bits, cached in node->h and marked in path_los_seen; the line
 * is blocked when the slope from the goal to the candidate's eye is below a square's slope.
 *
 * Quirks kept:
 *  - when path_25D13C fails (no search window) the stale path_los_seen pointer of the previous
 *    call is passed to ffmem_2C4FE4 again; ffmem_2C4F40 results are not checked;
 *  - a NULL path record is only checked after the setup (window, heap, first node) is done;
 *  - the search stops after 0x80 expanded nodes;
 *  - the slope cache in node->h is keyed on the square only, not on the goal or hoff/hsub, and
 *    the node itself is not otherwise initialised (squares not yet in the open list keep garbage
 *    in their other fields, as in the original);
 *  - neighbours are inserted with evict = 1 and the heap position returned is used unchecked
 *    (a -1 writes the entry before the array, as the original does);
 *  - a found square of (-1, -1) is returned but not stored in path +1.
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_c2.h"
#include "g5_a.h"     /* added at the G5-G8 merge */
#include "g9_b.h"     /* added at the G9/G10 merge */

static const short pp_dirs[5][2] = { {0, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, 0} };

#define PX G4C2_PX
#define PY G4C2_PY
#define PT G4C2_PT
#define IDX(x, y)  (((long)(y) - path_oy) * path_w + ((long)(x) - path_ox))

/* 16.16 slope of h over n steps, as 8.8 in 16 bits (0x7FFF / 0x8000 when out of range) */
static ushort pp_slope(short h, short n)
{
    long q;
    if (n == 0)
        q = (long)((ulong)(long)h << 16);
    else
        q = (long)(((long long)h << 32) / (long)((ulong)(long)n << 16));
    if ((q < 0 ? -(ulong)q : (ulong)q) > 0x7FFFFF)
        return (ushort)((q <= 0) + 0x7FFF);
    return (ushort)(q >> 8);
}

/* the original returns the same way in both places */
static ulong pp_found(void *rec, ulong pt)
{
    if (!(PX(pt) == -1 && PY(pt) == -1))
        *(ulong *)((uchar *)rec + 1) = pt;
    return pt;
}

ulong PathPlan(ulong from, ulong to, ulong dest, long a4, long a5, void *path, long a7,
               long a8, long a9, long a10)
{
    uchar flag7 = (uchar)a7, mode = (uchar)a8;
    long hoff = a4, iter;
    fix dist = (fix)((ulong)a10 << 16);
    uchar near_ok = 0;              /* b88: a fallback square was found */
    uchar near_now;                 /* b84 */
    uchar sel = 0;                  /* b90 */
    ulong fallback = 0, ret = 0xFFFFFFFF;
    (void)to;

    if (flag7) {
        short dx = (short)(PX(dest) - PX(from)), dy = (short)(PY(dest) - PY(from));
        long ax = dx < 0 ? -(long)dx : dx, ay = dy < 0 ? -(long)dy : dy;
        if (ax > 20 || ay > 20 || (long)dx * dx + (long)dy * dy > 400) {
            if (path_25D9A0(from, dest, 15, 0, path, mode))
                return *(ulong *)((uchar *)path + 1);
            return 0xFFFFFFFF;
        }
        hoff = 0;
    }

    path_pool = (PathNode *)ffmem_2C4F40(0x40000);
    path_sel = 1;
    if (!path_25D13C(from, dest))
        goto out;
    pathheap_25EBB8(path_heap);
    memset(path_seen, 0, 0x800);
    path_25C52C(from, from, 1, 0, 0, mode);
    path_los_seen = (uchar *)ffmem_2C4F40(0x800);
    memset(path_los_seen, 0, 0x800);
    if (path == NULL)
        goto out;

    for (iter = 0; path_heap->count > 0 && iter < 0x80; iter++) {
        PathNode *node = pathheap_25EDAC(path_heap);
        ulong cur = node->pt;
        short hc = (short)(map_2950A0(cur) + hoff);
        short hg = map_2950A0(dest);
        short dx, dy, adx, ady, sx, sy, major, s4c, err, cnt, steps, e6c;
        short x1[2], y1[2], x2[2], y2[2], stx, sty, s58, s60, s64, s80;
        uchar b8c, half;
        ushort slope;
        ulong pos;
        uchar *c;
        int reached;
        long d;

        if (path_trace)
            main_245468();
        c = *(uchar **)(game_map_35BFB4 + 0x84E + (long)PY(cur) * 4) + (long)PX(cur) * 3;
        if ((1UL << (game_41DD58[*c & 0x3F] & 31)) & terrain_bitmask)
            goto expand;
        if ((long)node->g < a9)
            goto expand;
        dx = (short)(PX(cur) - PX(dest));
        dy = (short)(PY(cur) - PY(dest));
        d = fix_fast_pyth_dist((fix)((ulong)(long)dx << 16), (fix)((ulong)(long)dy << 16));
        if (d < dist) {
            if (near_ok)
                goto expand;
            near_now = 1;
        } else
            near_now = 0;
        s4c = (short)(hc - hg);
        if (dx == 0 && dy == 0) {
            if (flag7)
                goto reach;
            goto expand;
        }

        if (dx > 0) { sx = 1; adx = dx; } else { sx = -1; adx = (short)-dx; }
        if (dy > 0) { sy = 1; ady = dy; } else { sy = -1; ady = (short)-dy; }
        if (adx > ady) {
            err = (short)(2 * ady - adx);
            s64 = ady;
            e6c = (short)(ady - adx);
            s60 = (short)(2 * ady);
            s58 = (short)(2 * ady - 2 * adx);
            cnt = adx;
            s80 = (short)(ady - 2 * adx);
            x1[0] = 0;  x1[1] = sx; y1[0] = sy; y1[1] = 0;
            x2[0] = sx; x2[1] = 0;  y2[0] = 0;  y2[1] = sy;
            stx = sx; sty = 0;
            major = adx;
        } else {
            err = (short)(2 * adx - ady);
            s64 = adx;
            e6c = (short)(adx - ady);
            s60 = (short)(2 * adx);
            s58 = (short)(2 * adx - 2 * ady);
            cnt = ady;
            s80 = (short)(adx - 2 * ady);
            x1[0] = sx; x1[1] = 0;  y1[0] = 0;  y1[1] = sy;
            x2[0] = 0;  x2[1] = sx; y2[0] = sy; y2[1] = 0;
            stx = 0; sty = sy;
            major = ady;
        }
        b8c = sx != 1;
        half = 0;
        slope = pp_slope(s4c, major);   /* s4c * 65536 / major (major > 0 here) */
        pos = dest;
        steps = 0;
        reached = -1;
        for (;;) {
            PathNode *ln;
            long idx;
            if (cnt-- == 0) {
                reached = flag7 ? 1 : 0;
                break;
            }
            if (half) {
                steps++;
                e6c = (short)(s80 + err);
                half = 0;
                err = (short)(err + s58);
                pos = PT(PX(pos) + x2[sel], PY(pos) + y2[sel]);
            } else if (err > 0 || (err == 0 && b8c)) {
                sel = (uchar)(((ushort)(e6c - 1 + b8c)) >> 15);
                pos = PT(PX(pos) + x1[sel], PY(pos) + y1[sel]);
                cnt++;
                half = 1;
            } else {
                steps++;
                e6c = (short)(s64 + err);
                pos = PT(PX(pos) + stx, PY(pos) + sty);
                err = (short)(err + s60);
            }
            idx = IDX(PX(pos), PY(pos));
            ln = path_pool + idx;
            if (!((path_los_seen[idx >> 3] >> (idx & 7)) & 1)) {
                short h = (short)(map_2950A0(pos) + hoff - a5);
                ln->h = (short)pp_slope(h, steps);
                path_los_seen[idx >> 3] |= (uchar)(1 << (idx & 7));
                if (path_trace_lines)
                    main_245468();
            }
            if ((short)slope < ln->h) {
                reached = flag7 ? 0 : 1;
                break;
            }
        }
        if (!reached)
            goto expand;
reach:
        if (near_now) {
            fallback = cur;
            near_ok = 1;
            goto expand;
        }
        if (path_25CC1C(path, cur, path_none)) {
            ret = pp_found(path, cur);
            goto out;
        }
expand:
        {
            long dir;
            for (dir = 1; dir <= 4; dir++) {
                ulong np = PT(PX(cur) + pp_dirs[dir][0], PY(cur) + pp_dirs[dir][1]);
                long x = PX(np), y = PY(np), i;
                short cost, mult, z;
                uchar k;
                PathNode *nn;
                if (np == node->parent)
                    continue;
                if (x < path_ox || x > path_x1 || y < path_oy || y >= path_y1)
                    continue;
                i = IDX(x, y);
                if ((path_seen[i >> 3] >> (i & 7)) & 1)
                    continue;
                nn = path_pool + i;
                k = path_25C704(cur, np, node, nn, dir, &cost, &mult, &z, mode);
                if (cost == -1)
                    continue;
                nn->bits &= 0xE3FF;
                nn->pt = np;
                nn->parent = cur;
                nn->bits |= (ushort)((k & 7) << 10);
                nn->g = (short)(node->g + cost);
                nn->bits &= 0xDFFF;
                nn->bits |= node->bits & 0x2000;
                nn->z = z;
                path_seen[i >> 3] |= (uchar)(1 << (i & 7));
                i = pathheap_25EBC0(path_heap, nn, nn->g, 1);
                path_heap->ents[(short)i].node = nn;
            }
        }
    }
    if (near_ok && path_25CC1C(path, fallback, path_none))
        ret = pp_found(path, fallback);
out:
    ffmem_2C4FE4(path_los_seen);
    ffmem_2C4FE4(path_pool);
    return ret;
}
