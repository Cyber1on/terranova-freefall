/* viewdist.c -- Terra Nova module M079 (0x2A0D70-0x2A322F; source file name unknown): view
 * distances, the per-strip lists of visible objects, their depth sort and drawing with the
 * terrain occlusion spans.  Decompiled from __FF.EXE v1.09.
 *
 *   0x2A0D70 viewdist_2A0D70 load the distance tables   0x2A0E14 viewdist_2A0E14 (unused) in range
 *   0x2A0E54 viewdist_2A0E54 entity within view range   0x2A0EC8 viewdist_2A0EC8 frame start
 *   0x2A0F48 viewdist_2A0F48 frame end (free)           0x2A0F68 viewdist_2A0F68 occlusion span
 *   0x2A10E8 viewdist_2A10E8 add an entity node         0x2A1160 viewdist_2A1160 add + span
 *   0x2A11B0 viewdist_2A11B0 neighbours of a big object  0x2A13B8 viewdist_2A13B8 collect a strip
 *   0x2A1840 viewdist_2A1840 squared distance of a node  0x2A18F8 viewdist_2A18F8 qsort compare
 *   0x2A197C viewdist_2A197C sort a strip list           0x2A1CC8 viewdist_2A1CC8 save canvas setup
 *   0x2A1D80 viewdist_2A1D80 save screen rectangle      0x2A1E84 viewdist_2A1E84 restore it
 *   0x2A2088 viewdist_2A2088 highest span value          0x2A20C4 viewdist_2A20C4 in front (z)
 *   0x2A213C viewdist_2A213C point in the view cone       0x2A2204 viewdist_2A2204 object hit point
 *   0x2A2284 viewdist_2A2284 draw one object              0x2A250C viewdist_2A250C draw a strip
 *   0x2A2864 viewdist_2A2864 (unused) node id             0x2A28D0 viewdist_2A28D0 screen -> world
 *   0x2A2CA8 viewdist_2A2CA8 side of a plane              0x2A2D88 viewdist_2A2D88 behind a building
 *   0x2A3158 viewdist_2A3158 has a polygon shape          0x2A3190 viewdist_2A3190 partition
 *
 * The terrain strip builder (terrstrp.c) cuts the view into strips (TerrBin, 0x20 bytes, the
 * array ends at strip_427DE0; the last strip is the one under the viewer, the one before it is
 * special too).  viewdist_2A13B8 collects the objects on a strip's map square into a circular
 * list of VdNodes (tail pointer in the strip): entities from the object grid and the objects
 * placed with map tiles.  viewdist_2A250C sorts the list (far to near; buildings, class 4, by
 * their polygon) and draws every node; objects of the viewer's strip are drawn through a save
 * canvas (vd_canvas) and copied back only above the terrain silhouette (vd_spans).
 *
 * Quirks kept:
 *  - viewdist_2A0D70 fills a short table with its first byte (not the last one) and only
 *    reports a table that is too big, then extracts it anyway.
 *  - viewdist_2A13B8 reads grid_hash[0] when the square's bucket is empty; when the node pool
 *    is full it reuses the last node again (count - 1).  For the strip before the viewer's it
 *    computes the span twice.
 *  - viewdist_2A250C: on odd strips (x ^ y odd) the half-plane test never rejects an object
 *    (the mode mask is applied only on even strips).
 *  - viewdist_2A197C with two nodes also runs the three-node ordering on the list (the third
 *    node is the first again).
 *  - The fake entities built for tile objects leave +0, +5..+0xE uninitialised (stack).
 */
#include "g5_c.h"
#include "res.h"
#include "2d.h"
#include "g1_b.h"
#include "g2_a.h"
#include "g2_c.h"
#include "g3_a.h"
#include "t1_c.h"
#include "g6_a.h"     /* added at the G5-G8 merge */
#include "g7_a.h"     /* added at the G5-G8 merge */
#include "g7_b.h"     /* added at the G5-G8 merge */

#ifndef ENT
#define ENT(i)      (&game_entities[(short)(i)])
#endif
#define BIN_LAST    ((TerrBin *)strip_427DE0 - 1)
#define BIN_LAST2   ((TerrBin *)strip_427DE0 - 2)
#define SKIP_ENT    (*(short *)ff_view.g_5a)
#ifdef TN_WATCOM_ABI
#define VD_NODES    0x190
#else
/* the sharp 3D view's farther terrain ("Terrain distance", port/video/sharp3d.c) meets more objects than
 * the game's 0x190 nodes: the far objects (haze.c, viewdist_2A1160) took nodes past the pool's
 * end and broke the lists (P35).  A bigger pool there (its scratch memory, enter_scratch). */
#define VD_NODES    (port_s3d_on ? 0x1000 : 0x190)
#endif

static short vd_grid_find(GridXY key)
{
    grid_hash_cur = GRID_HASH(key.x, key.y);
    if (grid_hash[grid_hash_cur].sq == 0)
        return 0;
    if (*(ulong *)&grid_squares[grid_hash[grid_hash_cur].sq] == *(ulong *)&key)
        return grid_hash_cur;
    return grid_295CFC(key, 0, grid_hash_cur);
}

static int seen(short e)
{
    return ai_seen_bits[e >> 3] & (1 << (e & 7));
}

static long cls6(VdNode *n)
{
    return n->ent && ENT(n->ent)->cls == 6;
}

void viewdist_2A0D70(void)
{
    long i, j;
    for (i = 0; i < 9; i++) {
        long sz = ResSize(0x560 + i);
        if (sz > 0x80)
            DbgReportError(1, "ViewDistance table for class %d too big.\n", i);
        ResExtract(0x560 + i, vd_dist_tab[i]);
        for (j = sz; j < 0x80; j++)
            vd_dist_tab[i][j] = vd_dist_tab[i][0];
    }
}

uchar viewdist_2A0E14(uchar *p, long d)
{
    long f = ((ulong)ff_cam.fov_x + 0x100) >> 11;
    return d * f <= vd_dist_tab[p[1]][p[2]] * 4;
}

#ifndef TN_WATCOM_ABI
/* "Object distance" leaves the enemy's (side 2, as the game's own enemy checks) shooters to
 * "Enemy distance": its soldiers, vehicles and aircraft and the shots they fire (class 5, owner
 * +6), 2; its armed installations (a weapon in the class 4 record, +0x33), 1, always at the
 * game's own distance; anything else 0. */
static int vd_enemy(GameEntity *e)
{
    if (e->cls == 5) {
        short owner = *(short *)(game_cls5[e->slot] + 6);
        if ((ushort)owner >= GAME_MAX_ENTITIES)
            return 0;
        e = ENT(owner);
    }
    if (e->cls < 1 || e->cls > 4 || !e->f21 || game_brains[e->f21].f005 != 2)
        return 0;
    if (e->cls == 4)
        return game_cls4[e->slot][0x33] != 0;
    return 2;
}
#endif

uchar viewdist_2A0E54(GameEntity *e)
{
    long dx = (e->x - ff_view.x) >> 16, dy = (e->y - ff_view.y) >> 16, d;
    long f;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    d = dx > dy ? dx : dy;
    f = ((ulong)ff_cam.fov_x + 0x100) >> 11;
#ifndef TN_WATCOM_ABI
    /* "Object distance" (port/video/screen.c): Far twice, Farthest 4 times as far in the sharp view;
     * the enemy's by "Enemy distance": Far twice */
    if (port_s3d_on && (port_obj_dist || port_enemy_dist)) {
        long en = vd_enemy(e), sh = en == 2 ? port_enemy_dist : en ? 0 : port_obj_dist;
        if (sh)
            return d * f <= vd_dist_tab[e->cls][e->type] * 4 << sh;
    }
#endif
    return d * f <= vd_dist_tab[e->cls][e->type] * 4;
}

void viewdist_2A0EC8(void)
{
    vd_spans_used = 1;
    viewdist_2A1CC8();
    vd_420148 = 0;
    vd_420144 = 0;
    tileobj_299048();
    memset(ai_seen_bits, 0, 0x26);
#ifndef TN_WATCOM_ABI
    /* the sharp view: its bigger table of tile object ids (g2_a.h) */
    memset(tile_3C9E1C, 0, port_s3d_on ? PORT_TILE_IDS / 8 : 0x80);
    vd_ent_info = ffmem_2C4F40(0x4B0);
    vd_tile_info = ffmem_2C4F40(port_s3d_on ? PORT_TILE_IDS * 4 : 0x1000);
#else
    memset(tile_3C9E1C, 0, 0x80);
    vd_ent_info = ffmem_2C4F40(0x4B0);
    vd_tile_info = ffmem_2C4F40(0x1000);
#endif
    vd_nodes = ffmem_2C4F40(VD_NODES * 0xC);
    vd_node_count = 0;
#ifndef TN_WATCOM_ABI
    if (port_s3d_on)
        port_burn_frame();              /* "Tree explosion" (port/mods/burn.c) */
#endif
}

void viewdist_2A0F48(void)
{
    ffmem_2C4FE4(vd_nodes);
    ffmem_2C4FE4(vd_tile_info);
    ffmem_2C4FE4(vd_ent_info);
}

long viewdist_2A0F68(long a, GameEntity *e)
{
    long *tab = hview_cols;
    long lo, hi, n, res = a, k;
    short r[4];
    ushort *p;

    if (e && drawobj_29CCE0(e, r)) {
        long j;
        lo = r[0];
        hi = r[2];
        for (j = lo; j < hi; j++)
            if (r[3] > tab[j])
                break;
        if (j == hi)
            return 999;
        res = tab[(lo + hi) >> 1];
    } else {
#ifdef TN_WATCOM_ABI
        lo = vd_center_col - 10;
        hi = vd_center_col + 10;
        if (lo < 0) lo = 0;
        if (hi > 0x13F) hi = 0x13F;
#else
        /* the sharp 3D view (port/video/sharp3d.c): its canvas is port_s3d_cols wide */
        lo = vd_center_col - 10 * port_s3d_kx;
        hi = vd_center_col + 10 * port_s3d_kx;
        if (lo < 0) lo = 0;
        if (hi > port_s3d_cols - 1) hi = port_s3d_cols - 1;
#endif
    }
    n = hi - lo + 1;
#ifdef TN_WATCOM_ABI
    if (lo > 0x13F || hi < 0)
        return res;
    if (n + vd_spans_used + 2 > 0x800)
        return res;
#else
    if (lo > port_s3d_cols - 1 || hi < 0)
        return res;
    if (n + vd_spans_used + 2 > port_vd_spans_max)
        return res;
#endif
    res = -(long)vd_spans_used;
    p = &vd_spans[vd_spans_used];
    vd_spans_used += n + 2;
    p[0] = lo;
    p[1] = n;
    for (k = 0; k < n; k++)
        p[2 + k] = (ushort)tab[lo + k];
    return res;
}

void viewdist_2A10E8(TerrBin *b, short ent)
{
    VdNode *n = &vd_nodes[vd_node_count];
#ifndef TN_WATCOM_ABI
    if (port_s3d_on && vd_node_count >= VD_NODES)
        return;                         /* the pool is full: no more objects in the sharp view */
#endif
    if (b->list == NULL) {
        b->list = n;
        n->next = n;
    } else {
        n->next = b->list->next;
        b->list->next = n;
    }
    b->count++;
    n->tmpl = 0;
    n->place = 0;
    n->id = 0;
    n->ent = ent;
    ai_seen_bits[ent >> 3] |= 1 << (ent & 7);
    vd_node_count++;
}

void viewdist_2A1160(TerrBin *b, short ent, long col, long a)
{
    viewdist_2A10E8(b, ent);
    vd_center_col = col;
    vd_ent_info[ent].a = viewdist_2A0F68(a, ENT(ent));
    vd_ent_info[ent].b = (short)hz_level;
}

void viewdist_2A11B0(TerrBin *b, short ent, long x, long y, long unused)
{
    short sq0, sq;
    if (ffpipe_mesh != -1) {
        short *r = (short *)((uchar *)GMAP->f1056 + ffpipe_mesh * 8);
        if (r[0] <= x || r[2] > x || r[1] <= y || r[3] > y)
            return;
    }
    sq0 = *(short *)((uchar *)ENT(ent) + 5);
    if (sq0 == 0)
        return;
    sq = sq0;
    do {
        GridSquare *s = &grid_squares[sq];
        if (!(s->x == x && s->y == y)) {
            short h = vd_grid_find(*(GridXY *)s);
            short o = h ? grid_hash[h].sq : 0;
            while (o) {
                if (o != sq) {
                    short e2 = grid_squares[o].ent;
                    if (!seen(e2) && e2 != SKIP_ENT && viewdist_2A0E54(ENT(e2)))
                        viewdist_2A10E8(b, e2);
                }
                o = grid_squares[o].next;
            }
        }
        sq = grid_squares[sq].ent_next;
    } while (sq != sq0);
}

void viewdist_2A13B8(TerrBin *b, uchar flag)
{
    long x = b->x, y = b->y;
    GridXY key;
    short sq, qx, qy;
    long cx, cy;
    uchar v, obj;

    key.x = x;
    key.y = y;
    sq = grid_hash[vd_grid_find(key)].sq;
    while (sq) {
        GridSquare *s = &grid_squares[sq];
        short e = s->ent;
        if (s->e0 == 0 && e != SKIP_ENT && !seen(e) && viewdist_2A0E54(ENT(e))) {
            if (vd_node_count >= VD_NODES)
                vd_node_count--;
            if (b == BIN_LAST) {
                long col = b->col;
                viewdist_2A10E8(b, e);
                vd_center_col = 0;
                vd_ent_info[e].a = viewdist_2A0F68(col, ENT(e));
                vd_ent_info[e].b = (short)hz_level;
                vd_ent_info[e].a = viewdist_2A0F68(b->col, ENT(e));
                vd_ent_info[e].b = (short)hz_level;
            } else {
                uchar cls;
                viewdist_2A10E8(b, e);
                cls = ENT(e)->cls;
                if (cls == 4 || (flag == 0 && cls == 2))
                    viewdist_2A11B0(b, e, x, y, flag);
            }
        }
        sq = s->next;
    }
#ifndef TN_WATCOM_ABI
    if (port_s3d_on && port_burn) {     /* "Tree explosion" (port/mods/burn.c): the burns and stumps on the square */
        int ids[16], nb = port_burn_square(x, y, ids, 16), k;
        for (k = 0; k < nb; k++) {
            VdNode *n;
            if (vd_node_count >= VD_NODES)
                break;
            n = &vd_nodes[vd_node_count];
            if (b->list == NULL) {
                b->list = n;
                n->next = n;
            } else {
                n->next = b->list->next;
                b->list->next = n;
            }
            b->count++;
            n->ent = 0;
            n->tmpl = (ushort)(0xFF00 + ids[k]);
            n->place = 0;
            n->id = 0;
            if (b == BIN_LAST && port_burn_set(ids[k]))
                port_burn_span(ids[k], viewdist_2A0F68(b->col, &port_burn_ent), hz_level, 0, NULL, NULL);
            vd_node_count++;
        }
    }
    /* the squares past the map's edge (port/video/mapedge.c, G35) and the row at 512 (G34) have no tile
     * objects: their 4 x 4 group would be read from past game_3C5E1C */
    if ((ulong)x >= 0x200 || (ulong)y >= 0x200)
        return;
#endif

    qx = x >> 2;
    qy = y >> 2;
    cx = (short)(x & 3);
    cy = y & 3;
    v = game_3C5E1C[qx][qy];
    if (v == 0 || flag != 0)
        return;
    key.x = qx;
    key.y = qy;
    tileobj_299080(v, key);
    obj = tile_groups[v].cell[cx][cy];
    while (obj) {
        TileObj *o = &tile_objs[obj];
        ushort tmpl, pl, id;
        if (*(uchar *)&o->w != 0)
            return;
        tmpl = o->tmpl;
        pl = tileobj_2991B4(v, key);
        id = tile_places[pl].start + (signed char)tile_tmpls[tmpl].idx;
#ifndef TN_WATCOM_ABI
        if (port_s3d_on && (!pl || id >= PORT_TILE_IDS))
            return;                     /* not placed (the table is full) */
#endif
        if (!(tile_3C9E1C[id >> 3] & (1 << (id & 7)))) {
            VdNode *n;
            if (vd_node_count >= VD_NODES)
                vd_node_count--;
            n = &vd_nodes[vd_node_count];
            if (b->list == NULL) {
                b->list = n;
                n->next = n;
            } else {
                n->next = b->list->next;
                b->list->next = n;
            }
            b->count++;
            n->ent = 0;
            n->tmpl = tmpl;
            n->place = pl;
            n->id = id;
            if (b == BIN_LAST) {
                GameEntity fe;                  /* +0, +5..+0xE stay unset (as the original) */
                TileTmpl *t = &tile_tmpls[tmpl];
                memcpy(&fe.x, t->pos, 0x12);
                fe.cls = t->cls;
                fe.type = t->type;
                fe.slot = t->slot;
                fe.x += (long)qx << 18;
                fe.y += (long)qy << 18;
                fe.body = 0;
                fe.f21 = 0;
                fe.z += map_2949C4(fe.x, fe.y);
                vd_tile_info[id].a = viewdist_2A0F68(b->col, &fe);
                vd_tile_info[id].b = (short)hz_level;
            } else {
                vd_tile_info[id].b = 9999;
            }
            tile_3C9E1C[id >> 3] |= 1 << (id & 7);
            vd_node_count++;
        }
        obj = *(uchar *)&tile_objs[obj].old;
    }
}

fix viewdist_2A1840(VdNode *n)
{
    fix dx, dy;
#ifndef TN_WATCOM_ABI
    if (PORT_BURN_NODE(n)) {            /* "Tree explosion": a burn that cannot be drawn is no tile (P70) */
        if (!port_burn_set(n->tmpl & 0xFF))
            return 0;
        dx = port_burn_ent.x - ff_view.x;
        dy = port_burn_ent.y - ff_view.y;
        return fix_mul(dx, dx) + fix_mul(dy, dy);
    }
#endif
    if (n->ent == 0) {
        TileTmpl *t = &tile_tmpls[n->tmpl];
        TilePlace *p = &tile_places[n->place];
        dx = *(fix *)t->pos + ((long)((GridXY *)&p->key)->x << 18) - ff_view.x;
        dy = *(fix *)(t->pos + 4) + ((long)((GridXY *)&p->key)->y << 18) - ff_view.y;
    } else {
        GameEntity *e = ENT(n->ent);
        dx = e->x - ff_view.x;
        dy = e->y - ff_view.y;
    }
    return fix_mul(dx, dx) + fix_mul(dy, dy);
}

long viewdist_2A18F8(VdNode **a, VdNode **b)
{
    fix da = viewdist_2A1840(*a), db = viewdist_2A1840(*b);
    if (da < db) return 1;
    if (da > db) return -1;
    if (cls6(*a)) return 1;
    if (cls6(*b)) return -1;
    return 0;
}

void viewdist_2A197C(TerrBin *b)
{
    long n = b->count, i, k, m;
    VdNode *A, *B, *C;
    fix di, ci, ai;

    if (n <= 1)
        return;
    if (n > 3) {
        VdNode **arr = ffmem_2C4F40(n * 4), **q = arr, *p = b->list, *last;
        do {
            *q++ = p;
            p = p->next;
        } while (p != b->list);
        for (i = 0; i < n; i++)
            if (ENT(arr[i]->ent)->cls == 4)
                break;
        if (i < n && viewdist_2A3158(arr[i]->ent)) {
            k = viewdist_2A3190(arr, n, i);
            if (k)
                lg_qsort(arr, k, 4, WATCOM_FN(viewdist_2A18F8));
            m = n - k - 1;
            if (m)
                lg_qsort(arr + k + 1, m, 4, WATCOM_FN(viewdist_2A18F8));
        } else {
            lg_qsort(arr, n, 4, WATCOM_FN(viewdist_2A18F8));
        }
        last = b->list = arr[0];
        for (i = 1; i < n; i++) {
            last->next = arr[i];
            last = arr[i];
        }
        last->next = arr[0];
        ffmem_2C4FE4(arr);
        return;
    }
    if (n == 2) {
        A = b->list;
        B = A->next;
        ci = viewdist_2A1840(B);
        ai = viewdist_2A1840(A);
        if (ci > ai || (ai == ci && cls6(A))) {
            b->list = B;
            B->next = A;
            A->next = B;
        }
    }
    /* three nodes (also reached with two) */
    A = b->list;
    B = A->next;
    C = B->next;
    di = viewdist_2A1840(A);
    ci = viewdist_2A1840(B);
    ai = viewdist_2A1840(C);
    if (di > ci) {
        if (ai > ci) {
            if (ai > di || (di == ai && cls6(A))) {
                b->list = C;
                return;
            }
            A->next = C; C->next = B; B->next = A;
        } else if (ci == ai && cls6(B)) {
            A->next = C; C->next = B; B->next = A;
        }
        return;
    }
    if (ci > di) {
        if (ai > ci) {
            b->list = C; C->next = B; B->next = A; A->next = C;
            return;
        }
        if (ci > ai) {
            if (ai > di) {
                b->list = B;
                return;
            }
            if (di > ai)
                goto cb0;
            b->list = B;
            if (cls6(C))
                goto cb3;
            return;
        }
        if (cls6(B)) {
            b->list = C; C->next = B; B->next = A; A->next = C;
        } else {
            b->list = B;
        }
        return;
    }
    if (ai > di) {
        b->list = C;
        if (cls6(A)) {
            C->next = B; B->next = A; A->next = C;
        }
        return;
    }
    if (di > ai && cls6(A))
        goto cb0;
    return;
cb0:
    b->list = B;
cb3:
    B->next = A; A->next = C; C->next = B;
}

void viewdist_2A1CC8(void)
{
    long w = grd_canvas->bm.w, h = grd_canvas->bm.h;
    memset(vd_canvas.bm.bits, 0, w * h);
    vd_canvas.bm.type = 2;
    vd_canvas.bm.align = 0;
    vd_canvas.bm.flags = 0;
    vd_canvas.bm.w = w;
    vd_canvas.bm.h = h;
    vd_canvas.bm.row = w;
    memcpy(&vd_canvas.gc, &grd_canvas->gc, 0x2C);  /* first from grd_defgc, then overwritten */
}

void viewdist_2A1D80(short *r, long lim)
{
    ulong rs = grd_canvas->bm.row, rd = vd_canvas.bm.row;
    long xa = r[0] - 1, xb = r[2] + 1, ya = r[1] - 1, yb = r[3] + 2, y;
    if (lim > 0 && lim < yb) yb = lim;
    if (xa < 0) xa = 0;
    if (xb >= grd_canvas->bm.w) xb = grd_canvas->bm.w - 1;
    if (ya < 0) ya = 0;
    if (grd_canvas->bm.h < yb) yb = grd_canvas->bm.h;
    if (xb < xa || ya >= yb)
        return;
    for (y = ya; y < yb; y++)
        memcpy(vd_canvas.bm.bits + rd * y + xa, grd_canvas->bm.bits + rs * y + xa, xb - xa + 1);
}

void viewdist_2A1E84(short *r, long lim)
{
    ulong rs = grd_canvas->bm.row, rd = vd_canvas.bm.row;
    long xa = r[0], xb = r[2], ya = r[1], yb = r[3] + 1, y, x;
    if (lim > 0 && lim < yb) yb = lim;
    if (xa < 0) xa = 0;
    if (grd_canvas->bm.w <= xb) xb = grd_canvas->bm.w - 1;
    if (ya < 0) ya = 0;
    if (grd_canvas->bm.h < yb) yb = grd_canvas->bm.h;
    if (xa > xb || ya >= yb)
        return;
    if (lim >= 0) {
        for (y = ya; y < yb; y++)
            memcpy(grd_canvas->bm.bits + rs * y + xa, vd_canvas.bm.bits + rd * y + xa, xb - xa + 1);
        return;
    }
    {
        ushort *p = &vd_spans[-lim];
        for (x = xa; x <= xb; x++) {
            long lo = p[0], top, cnt;
            uchar *src, *dst;
            if (lo > x)
                top = p[2];
            else if (lo + p[1] <= x)
                top = p[p[1] + 1];
            else
                top = p[x - lo + 2];
            src = vd_canvas.bm.bits + ya * rd + x;
            dst = grd_canvas->bm.bits + ya * rs + x;
            cnt = (top < yb ? top : yb) - ya;
            while (cnt-- > 0) {
                *dst = *src;
                src += rd;
                dst += rs;
            }
        }
    }
}

ulong viewdist_2A2088(ushort *p)
{
    ulong m = p[2];
    long i;
    for (i = 1; i < p[1]; i++)
        if (p[2 + i] > m)
            m = p[2 + i];
    return m;
}

uchar viewdist_2A20C4(fix *p)
{
    fix z = fix_mul(p[1] - ff_view.y, ff_view.m[6]) + fix_mul(p[2] * 24, ff_view.m[7]);
    z = fix_mul(p[0] - ff_view.x, ff_view.m[8]) + z + ff_view.mz[2];
    return z >= 0x2000;
}

uchar viewdist_2A213C(fix x, fix y, fix z)
{
    fix dx = x - ff_view.x, dy = y - ff_view.y, dz = z * 24, a, c;
    a = fix_mul(dy, ff_view.m[0]) + fix_mul(dz, ff_view.m[1]);
    a = fix_mul(dx, ff_view.m[2]) + a + ff_view.mz[0];
    c = fix_mul(dy, ff_view.m[6]) + fix_mul(dz, ff_view.m[7]);
    c = ff_view.mz[2] + fix_mul(dx, ff_view.m[8]) + c + 0x40000;
    if (c < 0x800)
        return 0;
    if (a < 0) a = -a;
    return a * 3 < c * 5;
}

uchar viewdist_2A2204(long obj, fix *a, fix *b, fix *c)
{
    uchar buf[16];
    short o = obj;
    long t;
    if (!drawobj_29ACA8(ENT(o), buf))
        return 0;
    if (buf[8] == 0)
        return 0;
    t = gResDesc2[*(ushort *)(buf + 2)].type;
    if (t != 0xF && t != 0)
        return 0;
    return drawobj_29B170(o, buf, a, b, c) ? 1 : 0;
}

#ifndef TN_WATCOM_ABI
/* "Fog" (port/video/fog.c): an object drawn through its light table and the fog of its place, at most
 * 3/4 of the way (a unit in the fog can still be seen) */
static void obj_fog(const GameEntity *e, long lvl)
{
    long k = lvl > 1 ? ((lvl - 2) >> 1 > 2 ? 2 : (lvl - 2) >> 1) : -1, f;
    uchar *lt;
    port_fog_view(&ff_cam);
    f = (port_fog_at(e->x - ff_view.x, e->y - ff_view.y) + 0x80) >> 8;
    if (port_fog_ir) {                  /* the infrared: through its colour table (drawobj.c) */
        port_fog_obj = f > 12 ? 12 : f;
        return;
    }
    lt = (uchar *)port_fog_ltab(17 + k, k < 0 ? planet_light : planet_shaded[k], f > 12 ? 12 : f);
    grd_screen->ltab = lt;
    ffpipe_29E6F8(lt);
}
#endif

uchar viewdist_2A2284(GameEntity *e, uchar fl, long span, long lvl, short arg)
{
    uchar buf[12];
    short rect[4];
    long l14, l18, lim2 = 0, sp, r;
    uchar saved = 0, has_lim2 = 0;

    sp = span;
    if (fl && span == 999)
        fl = 0;
    if (game_41DE34 < 2 && e->cls != 4 && helm_263410(e->x, e->y)) {
        fix h = map_2949C4(e->x, e->y);
        if (drawobj_29DA60(e->x, e->y, h, NULL, NULL, NULL, &l18, &l14)) {
            long yy = l14 >> 16;
            if (yy < grd_canvas->bm.h) {
                if (l14 < 0)
                    return 0;
                if (fl) {
                    lim2 = yy;
                    has_lim2 = 1;
                } else {
                    span = yy;
                    fl = 1;
                }
            }
        }
    }
    vd_draw_arg = arg;
    r = drawobj_29CCE0(e, rect);
    if (fl && r) {
        saved = 1;
        if (span < 0) {
            sp = span;
            span = viewdist_2A2088(&vd_spans[-span]);
        } else {
            sp = span;
        }
        if (has_lim2 && span > lim2)
            span = lim2;
        if (span <= rect[1])
            return 0;
    }
    if (!drawobj_29ACA8(e, buf))
        return 0;
    if (arg != 0 && r)
        hud_26A11C(rect, arg);
    if (lvl > 1) {
        long k = (lvl - 2) >> 1;
        if (k > 2) k = 2;
        grd_screen->ltab = planet_shaded[k];
        ffpipe_29E6F8(planet_shaded[k]);
    }
#ifndef TN_WATCOM_ABI
    if (port_fog_on)
        obj_fog(e, lvl);
    if (r && port_s3d_on && port_water_full == 2)
        port_water_obj_begin(e, rect);  /* its reflection in "Water reflection: High" (rndsky.c) */
#endif
    if (saved) {
        if (has_lim2 && rect[3] > lim2)
            rect[3] = lim2;
        viewdist_2A1D80(rect, sp);
        gr_push_canvas(&vd_canvas);
        if (buf[8] == 0 || (*(ushort *)&gResDesc2[*(ushort *)(buf + 2)] & 0xFF00) != 0x200
            || viewdist_2A20C4(&e->x))
            drawobj_29AF98(e, buf);
        gr_pop_canvas();
        viewdist_2A1E84(rect, sp);
    } else {
        if (buf[8] == 0 || (*(ushort *)&gResDesc2[*(ushort *)(buf + 2)] & 0xFF00) != 0x200
            || viewdist_2A20C4(&e->x))
            drawobj_29AF98(e, buf);
    }
#ifndef TN_WATCOM_ABI
    if (r && port_s3d_on && port_water_full == 2) {
        /* a 3D model (a unit, a vehicle, a ragdoll, a debris plate) is drawn again from under
         * the water; a sprite (a tree, an explosion, a splash) is flipped; tracers, particles
         * and bolts are not reflected */
        long t = buf[8] ? gResDesc2[*(ushort *)(buf + 2)].type : -1;
        int kind = t == 0 || t == 0xF ? 1 : t == 2 ? 2 :
                   !buf[8] && e->cls == 6 && (e->type == 0x20 || e->type == 0x21) ? 1 : 0;
        if (port_water_obj_end(e, kind)) {
            drawobj_29AF98(e, buf);
            port_water_mirror_done();
        }
    }
#endif
#ifndef TN_WATCOM_ABI
    port_fog_obj = -1;
    if (lvl > 1 || port_fog_on) {
#else
    if (lvl > 1) {
#endif
        grd_screen->ltab = planet_light;
        ffpipe_29E6F8(planet_light);
    }
    return 1;
}

/* the half-plane test of viewdist_2A250C: is (x, y) on the drawing side of the strip */
static long side_ok(TerrBin *b, fix x, fix y, uchar mode)
{
    ulong bx = (ushort)b->x, by = (ushort)b->y;
    if ((bx ^ by) & 1)
        return 1;                               /* (x + y - ((bx + by + 1) << 16) >= 0) + 1 */
    return mode & ((x - y - (long)((bx - by) << 16) >= 0) + 1);
}

void viewdist_2A250C(void *p, long n)
{
    TerrBin *b = p;
    uchar mode = n;
    VdNode *first, *nd;

    if (b != BIN_LAST2) {
        if (b != BIN_LAST)
            viewdist_2A197C(b);
        else if (b->list)
            b->list = b->list->next;
    }
    first = nd = b->list;
    do {
        if (nd->ent != 0) {
            GameEntity *e = ENT(nd->ent);
            long r;
            if (mode != 3 && !side_ok(b, e->x, e->y, mode))
                goto next;
            if (b == BIN_LAST)
                r = viewdist_2A2284(e, 1, vd_ent_info[nd->ent].a, vd_ent_info[nd->ent].b, nd->ent);
            else
                r = viewdist_2A2284(e, 0, 0, 0, nd->ent);
            if (r && ff_cam.probe && *ff_cam.probe_ptr != 0xFF) {
                if (vd_pick_cls[e->cls]) {
                    ff_cam.probe_hit = nd->ent;
                    ff_cam.g_b7[0] = 0;
                }
                ff_cam.probe_save = *ff_cam.probe_ptr;
                *ff_cam.probe_ptr = 0xFF;
            }
#ifndef TN_WATCOM_ABI
        } else if (PORT_BURN_NODE(nd)) {    /* "Tree explosion" (port/mods/burn.c) */
            long a = 0, lv = 0;
            if (!port_burn_set(nd->tmpl & 0xFF) || (mode != 3 && !side_ok(b, port_burn_ent.x, port_burn_ent.y, mode)))
                goto next;
            if (b == BIN_LAST) {
                port_burn_span(nd->tmpl & 0xFF, 0, 0, 1, &a, &lv);
                viewdist_2A2284(&port_burn_ent, 1, a, lv, 0);
            } else
                viewdist_2A2284(&port_burn_ent, 0, 0, 0, 0);
#endif
        } else {
            TileTmpl *t;
            TilePlace *pl;
            GameEntity fe;                      /* +0, +5..+0xE stay unset (as the original) */
            fix X, Y;
            uchar qx, qy;
            long r;
            if (ff_cam.probe && *ff_cam.probe_ptr != 0xFF) {
                ff_cam.probe_save = *ff_cam.probe_ptr;
                *ff_cam.probe_ptr = 0xFF;
            }
            qx = (short)b->x >> 2;
            qy = (short)b->y >> 2;
            t = &tile_tmpls[nd->tmpl];
            pl = &tile_places[nd->place];
            X = ((long)((GridXY *)&pl->key)->x << 18) + *(fix *)t->pos;
            Y = ((long)((GridXY *)&pl->key)->y << 18) + *(fix *)(t->pos + 4);
            if (mode != 3 && !side_ok(b, X, Y, mode))
                goto next;
            fe.cls = t->cls;
            fe.type = t->type;
            fe.slot = t->slot;
            fe.x = X;
            fe.y = Y;
            fe.z = *(fix *)(t->pos + 8) + map_2949C4(X, Y);
            fe.heading = *(ushort *)(t->pos + 0xC);
            fe.f1d = *(ushort *)(t->pos + 0xE);
            fe.f1f = *(ushort *)(t->pos + 0x10);
            *(ulong *)&fe.f21 = t->f21;
            if (b == BIN_LAST)
                r = viewdist_2A2284(&fe, 1, vd_tile_info[nd->id].a, vd_tile_info[nd->id].b, 0);
            else
                r = viewdist_2A2284(&fe, 0, 0, 0, 0);
            if (r && ff_cam.probe && *ff_cam.probe_ptr != 0xFF) {
                ff_cam.probe_save = *ff_cam.probe_ptr;
                ff_cam.probe_hit = nd->tmpl;
                ff_cam.g_b7[0] = 1;
                ff_cam.g_b7[1] = qx;
                *ff_cam.probe_ptr = 0xFF;
                ff_cam.g_b7[2] = qy;
            }
        }
    next:
        nd = nd->next;
    } while (nd != first);
}

long viewdist_2A2864(uchar i)
{
    VdNode *n = &vd_nodes[i - 1];
    GridXY k;
    if (n->ent)
        return n->ent;
    k = *(GridXY *)&tile_places[n->place].key;
    return ((ulong)k.x << 24) | 0x80000000 | ((ulong)k.y << 16) | n->tmpl;
}

long viewdist_2A28D0(ulong pt, fix *x, fix *y, fix *z, uchar *cam, void *view)
{
    FfView *v = view;
    FfCamera *c = (FfCamera *)cam;
    fix *m = v->m;
    fix d, i0, i1, i2, i3, i4, i5, i6, i7, i8, sx, sy, sz, a, b2, cc, len;

    d = fix_mul(m[0], fix_mul(m[4], m[8]) - fix_mul(m[5], m[7]));
    d -= fix_mul(m[1], fix_mul(m[3], m[8]) - fix_mul(m[5], m[6]));
    d += fix_mul(m[2], fix_mul(m[3], m[7]) - fix_mul(m[4], m[6]));
    i0 = fix_div(fix_mul(m[4], m[8]) - fix_mul(m[5], m[7]), d);
    i1 = fix_div(fix_mul(m[2], m[7]) - fix_mul(m[1], m[8]), d);
    i2 = fix_div(fix_mul(m[1], m[5]) - fix_mul(m[2], m[4]), d);
    i3 = fix_div(fix_mul(m[5], m[6]) - fix_mul(m[3], m[8]), d);
    i4 = fix_div(fix_mul(m[0], m[8]) - fix_mul(m[2], m[6]), d);
    i5 = fix_div(fix_mul(m[2], m[3]) - fix_mul(m[0], m[5]), d);
    i6 = fix_div(fix_mul(m[3], m[7]) - fix_mul(m[4], m[6]), d);
    i7 = fix_div(fix_mul(m[1], m[6]) - fix_mul(m[0], m[7]), d);
    i8 = fix_div(fix_mul(m[0], m[4]) - fix_mul(m[1], m[3]), d);
    sx = fix_div((long)(short)pt << 16, c->half_w << 16) - 0x10000 - v->mz[0];
    sy = fix_div((long)pt >> 16 << 16, c->half_h << 16) - 0x10000 - v->mz[1];
    sz = 0x10000 - v->mz[2];
    a = fix_mul(i6, sx) + fix_mul(i7, sy) + fix_mul(i8, sz);
    b2 = fix_mul(i0, sx) + fix_mul(i1, sy) + fix_mul(i2, sz);
    cc = fix_mul(fix_mul(i3, sx) + fix_mul(i4, sy) + fix_mul(i5, sz), 0xAAA) - v->z;
    len = fix_sqrt(fix_mul(a, a) + fix_mul(b2, b2) + fix_mul(cc, cc));
    *x = v->x + fix_div(fix_mul(a, 0xA0000), len);
    *y = v->y + fix_div(fix_mul(b2, 0xA0000), len);
    *z = v->z + fix_div(fix_mul(cc, 0xA0000), len);
    return (long)z;
}

uchar viewdist_2A2CA8(fix *t, fix px, fix py, fix pz)
{
    fix A = fix_mul(t[4] - t[1], t[8] - t[2]) - fix_mul(t[7] - t[1], t[5] - t[2]);
    fix B = fix_mul(t[3] - t[0], t[8] - t[2]) - fix_mul(t[5] - t[2], t[6] - t[0]);
    fix C = fix_mul(t[3] - t[0], t[7] - t[1]) - fix_mul(t[4] - t[1], t[6] - t[0]);
    fix r = fix_mul(px - t[0], A) - fix_mul(py - t[1], B);
    return fix_mul(pz - t[2], C) + r > 0;
}

long viewdist_2A2D88(short a, short b)
{
    GameEntity *ea = ENT(a), *eb = ENT(b);
    ShapeItem *s = interact_2973E8(ea->cls, ea->type);
    fix *P, *H, t[9], s_, c_, bx, by, bz, cx, cy, cz, rbx, rby, rcx, rcy;
    long k;
    static const uchar tri[5][6] = {   /* point / height index per corner, 4 = zero height */
        {0, 0, 1, 1, 2, 2}, {0, 4, 1, 4, 1, 1}, {1, 4, 2, 4, 2, 2}, {2, 4, 3, 4, 3, 3}, {3, 4, 0, 4, 0, 0}};

    if (s->type != 1)
        return 0;
    P = (fix *)((uchar *)s + 4);
    H = (fix *)((uchar *)s + 4 + s->npts * 8);
    bx = eb->x - ea->x;
    by = eb->y - ea->y;
    bz = eb->z - ea->z;
    cx = ff_view.x - ea->x;
    cy = ff_view.y - ea->y;
    cz = ff_view.z - ea->z;
    fix_sincos(ea->heading, &s_, &c_);
    rcx = fix_mul(cx, c_) + fix_mul(cy, s_);
    rcy = fix_mul(cy, c_) - fix_mul(cx, s_);
    rbx = fix_mul(bx, c_) + fix_mul(by, s_);
    rby = fix_mul(by, c_) - fix_mul(bx, s_);
    for (k = 0; k < 5; k++) {
        long j;
        for (j = 0; j < 3; j++) {
            t[j * 3] = P[tri[k][j * 2] * 2];
            t[j * 3 + 1] = P[tri[k][j * 2] * 2 + 1];
            t[j * 3 + 2] = tri[k][j * 2 + 1] == 4 ? 0 : H[tri[k][j * 2 + 1]];
        }
        if (viewdist_2A2CA8(t, rbx, rby, bz))
            return viewdist_2A2CA8(t, rcx, rcy, cz) ? -1 : 1;
        if (k == 4)
            return -1;
    }
    return -1;
}

long viewdist_2A3158(short a)
{
    return interact_2973E8(ENT(a)->cls, ENT(a)->type)->type == 1;
}

long viewdist_2A3190(VdNode **arr, long n, long i)
{
    VdNode *tmp;
    long b = 1, c = n;
    tmp = arr[0]; arr[0] = arr[i]; arr[i] = tmp;
    if (c > b) {
    l1:
        for (;;) {
            if (viewdist_2A2D88(arr[0]->ent, arr[b]->ent) < 0)
                break;
            b++;
            if (b >= c)
                goto done;
        }
        c--;
        if (c <= b)
            goto done;
        for (;;) {
            if (viewdist_2A2D88(arr[0]->ent, arr[c]->ent) >= 0) {
                tmp = arr[b]; arr[b] = arr[c]; arr[c] = tmp;
                b++;
                if (b < c)
                    goto l1;
                goto done;
            }
            c--;
            if (c <= b)
                goto done;
        }
    }
done:
    tmp = arr[0]; arr[0] = arr[c - 1]; arr[c - 1] = tmp;
    return c - 1;
}
