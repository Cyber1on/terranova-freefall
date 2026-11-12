/* terrmesh.c -- Terra Nova module M078 (0x29EEF0-0x2A0D62): the map buildings in view and the
 * back-to-front order of the mesh objects.  Decompiled from __FF.EXE v1.09.
 *
 *   terrmesh_29EEF0  first map building (resource 0x5F boxes) near the view cone, or -1
 *   terrmesh_29F2B4  depth sort of every object of the mesh groups (mesh_groups) into one
 *                    circular list at the group header below strip_427DE0
 *   terrmesh_29F56C  object position, heading and viewer in object space
 *   terrmesh_29F6EC  silhouette edge by shape kind (29F770 polygon, 2A00F8 circle)
 *   terrmesh_2A01F8  which of two objects is in front (1 / 2 / 3)
 *   terrmesh_2A0A2C  does a segment cross a box
 * Fixed point: fix_mul / fix_div (inline imul / idiv in the original); terrmesh_29F770 also
 * works in 8.24 (m24 / d24).
 */
#include "g5_b.h"
#include "g1_b.h"
#include "g2_a.h"
#include "g2_c.h"
#include "g5_a.h"
#include "g5_c.h"
#include <string.h>

#define fmul fix_mul
#define fdiv fix_div

/* imul / shr eax,8 / shl edx,24 / or: the low 32 bits of the product >> 8 */
static long m24(long a, long b) { return (long)(((long long)a * b) >> 8); }
/* edx = a >> 24, eax = a << 8, idiv */
static long d24(long a, long b) { return (long)(((long long)a << 8) / b); }

static long sh16(short v) { return (long)((ulong)(long)v << 16); }

/* 0x29EEF0 */
long terrmesh_29EEF0(FfView *view, void *strips)
{
    TsRec *F = strips;
    fix p10[2], p8[2], p20[2], o[2], r0[2], r1[2];
    short *b;
    long i;
    if (F->f2a > 0) {
        p10[0] = fmul(F->f2a, F->e[0][0]) + F->d[0][0];
        p10[1] = fmul(F->f2a, F->e[0][1]) + F->d[0][1];
        p8[0] = fmul(F->f2a, F->e[1][0]) + F->d[1][0];
        p8[1] = fmul(F->f2a, F->e[1][1]) + F->d[1][1];
    } else {
        fix d = fix_pyth_dist(F->d[0][0], F->d[0][1]);
        fix k = fdiv(F->rad << 16, d);
        p10[0] = fmul(F->d[0][0], k);
        p10[1] = fmul(F->d[0][1], k);
        p8[0] = fmul(F->d[1][0], k);
        p8[1] = fmul(F->d[1][1], k);
    }
    p20[0] = F->rad * view->cos_h;
    p20[1] = F->rad * view->sin_h;
    b = GMAP->f1056;
    for (i = 0; i < (long)GMAP->f1052; i++, b += 4) {
        long R, a0, a1, c0, c1;
        r0[0] = sh16(b[0]) - view->x;
        r0[1] = sh16(b[1]) - view->y;
        r1[0] = sh16(b[2]) - view->x;
        r1[1] = sh16(b[3]) - view->y;
        a0 = (r0[0] >> 16) * (r0[0] >> 16);
        a1 = (r0[1] >> 16) * (r0[1] >> 16);
        c0 = (r1[0] >> 16) * (r1[0] >> 16);
        c1 = (r1[1] >> 16) * (r1[1] >> 16);
        R = (F->rad + 1) * (F->rad + 1);
        if (!(R > a0 + a1 || R > a0 + c1 || R > a1 + c0 || c1 + c0 < R))
            continue;
        if (!(fmul(r0[0], view->cos_h) + fmul(r0[1], view->sin_h) > 0
              || fmul(r0[0], view->cos_h) + fmul(r1[1], view->sin_h) > 0
              || fmul(r1[0], view->cos_h) + fmul(r0[1], view->sin_h) > 0
              || fmul(r1[0], view->cos_h) + fmul(r1[1], view->sin_h) > 0))
            continue;
        /* the outline of the view (the result does not matter: the building is taken) */
        o[0] = o[1] = 0;
        if (F->f2a > 0) {
            (void)(terrmesh_2A0A2C(o, F->d[0], r0, r1) || terrmesh_2A0A2C(F->d[0], p10, r0, r1)
                   || terrmesh_2A0A2C(p10, p20, r0, r1) || terrmesh_2A0A2C(p20, p8, r0, r1)
                   || terrmesh_2A0A2C(p8, F->d[1], r0, r1) || terrmesh_2A0A2C(F->d[1], o, r0, r1));
        } else {
            (void)(terrmesh_2A0A2C(o, p10, r0, r1) || terrmesh_2A0A2C(p10, p20, r0, r1)
                   || terrmesh_2A0A2C(p20, p8, r0, r1) || terrmesh_2A0A2C(p8, o, r0, r1));
        }
        return i;
    }
    return -1;
}

#ifndef TN_WATCOM_ABI
/* G45 in docs/BUGS.md: standing on a building (or flying over it), the game's in-front test
 * (terrmesh_2A01F8, a top-down 2D test of the silhouette edges, then the heights) could put the
 * soldiers, vehicles and objects on the ground around it in front of its roof: they showed
 * through the roof with their target brackets.  A building is a convex block, so with the eye
 * inside its footprint and above its top everything is behind its roof except what stands on
 * that roof.  Such buildings (seldom more than one) are found once per sort. */
#define TM_ROOFS 4
static MeshRec *tm_roof[TM_ROOFS];
static int tm_nroof;

static ShapeItem *tm_shape(MeshNode *nd)
{
    if (nd->ent) {
        GameEntity *e = &game_entities[nd->ent];
        return interact_2973E8(e->cls, e->type);
    } else {
        TileTmpl *t = &tile_tmpls[nd->tmpl];
        return interact_2973E8(t->cls, t->type);
    }
}

/* the top of a shape at (lx, ly) in object space (above its base); *in: the point is inside
 * its footprint.  0 for shapes without one. */
static fix tm_top(ShapeItem *sh, fix lx, fix ly, int *in)
{
    fix *t;
    *in = 0;
    if (!sh)
        return 0;
    t = SHAPE_TAIL(sh);
    switch ((ushort)sh->type) {
    case 1: {
        int k, pos = 0, neg = 0;
        if (sh->npts != 4)
            return 0;
        for (k = 0; k < 4; k++) {
            int j = (k + 1) & 3;
            long long cr = (long long)(sh->pt[j][0] - sh->pt[k][0]) * (ly - sh->pt[k][1])
                         - (long long)(sh->pt[j][1] - sh->pt[k][1]) * (lx - sh->pt[k][0]);
            if (cr > 0) pos++;
            else if (cr < 0) neg++;
        }
        *in = !(pos && neg);
        if (t[0] == t[1] && t[0] == t[2] && t[0] == t[3])
            return t[0];
        {   /* a sloping top: the floor plane the soldiers walk on */
            ShapeExt *x = SHAPE_EXT(sh);
            fix m = t[0];
            if (x)
                return fmul(x->zx, lx) + x->z0 + fmul(x->zy, ly);
            for (k = 1; k < 4; k++)
                if (t[k] > m) m = t[k];
            return m;
        }
    }
    case 2:                             /* a ball: radius */
    case 3:                             /* an upright cylinder: radius, height */
        *in = (long long)lx * lx + (long long)ly * ly < (long long)t[0] * t[0];
        return sh->type == 2 ? t[0] : t[1];
    }
    return 0;
}

static void tm_roofs(MeshRec *buf, MeshRec *end)
{
    MeshRec *r;
    tm_nroof = 0;
    for (r = buf; r < end && tm_nroof < TM_ROOFS; r++) {
        int in;
        fix top = tm_top(tm_shape(r->node), r->lx, r->ly, &in);
        if (in && top > 0 && -r->dz > top)
            tm_roof[tm_nroof++] = r;
    }
}

/* does object O stand on roof R (its base at or above the top under it, less 1/8)? */
static int tm_on(MeshRec *R, MeshRec *O)
{
    fix dx = O->x - R->x, dy = O->y - R->y;
    int in;
    fix top = tm_top(tm_shape(R->node), fmul(dx, R->c) - fmul(dy, R->s),
                     fmul(dx, R->s) + fmul(dy, R->c), &in);
    return O->dz - R->dz >= top - 0x2000;
}
#endif

/* 0x29F2B4: sort the objects of all groups back to front.  tab[i] is row i of the "in front
 * of" matrix (row[-1]: the first column still to compare; -1: done / self; 1, 2, 3: results of
 * terrmesh_2A01F8, mirrored into row j); an object with nothing left in front of it (no 2) is
 * emitted next, and when every object waits the first one left is taken. */
void terrmesh_29F2B4(void)
{
    MeshGroup *s = (MeshGroup *)(strip_427DE0 - 0x40), *g;
    MeshRec *buf, *rec;
    long cnt;
    s->count = 0;
    s->first = 0;
    buf = ffmem_2C4F40(0x4B00);
    rec = buf;
    for (g = mesh_groups; g; g = g->next) {
        MeshNode *first, *nd;
        if (!g->count)
            continue;
        s->count += g->count;
        nd = first = g->first;
        for (;;) {
            terrmesh_29F56C(nd, rec);
            rec->node = nd;
            if (terrmesh_29F6EC(nd, rec))
                rec++;
            else
                s->count--;
            if (nd->next == first)
                break;
            nd = nd->next;
        }
    }
    cnt = s->count;
#ifndef TN_WATCOM_ABI
    tm_roofs(buf, rec);                 /* G45 */
#endif
    if (cnt > 0) {
        long rowsz = 4 * cnt + 4, i, k;
        long **tab = ffmem_2C4F40(4 * cnt * (cnt + 1) + rowsz);
        long *row;
        MeshNode **link = &s->first;
        memset((uchar *)tab + rowsz, 0, 4 * cnt * (cnt + 1));
        row = (long *)((uchar *)tab + rowsz + 4);
        for (k = 0; k < cnt; k++) {
            row[k] = -1;
            tab[k] = row;
            row = (long *)((uchar *)row + rowsz);
        }
        tab[cnt] = (long *)-1;
        i = 0;
        do {
            MeshNode *nd;
            for (; i < cnt; i++) {
                long *ri = tab[i], j;
                int wait = 0;
                if (!ri)
                    continue;
                j = ri[-1];
                do {
                    uchar flag = 0;
                    if (ri[j] == 0) {
                        ri[j] = terrmesh_2A01F8(&buf[i], &buf[j]);
                        flag = 1;
                    }
                    switch (ri[j]) {
                    case 1:
                        if (flag) tab[j][i] = 2;
                        break;
                    case 2:
                        if (flag) tab[j][i] = 1;
                        ri[-1] = j;
                        wait = 1;
                        break;
                    case 3:
                        if (flag) tab[j][i] = 3;
                        break;
                    }
                    if (wait)
                        break;
                } while (++j < cnt);
                if (!wait)
                    break;
            }
            if (i == cnt)
                for (i = 0; i < cnt && !tab[i]; i++)
                    ;
            nd = buf[i].node;
            *link = nd;
            link = &nd->next;
            tab[i] = 0;
            for (k = 0; k < cnt; k++)
                if (tab[k])
                    tab[k][i] = -1;
            for (i = 0; !tab[i]; i++)
                ;
        } while (i < cnt);
        *link = s->first;
        ffmem_2C4FE4(tab);
    }
    ffmem_2C4FE4(buf);
}

/* 0x29F56C */
void terrmesh_29F56C(MeshNode *node, MeshRec *r)
{
    if (node->ent) {
        GameEntity *e = &game_entities[node->ent];
        fix dx, dy, s, c;
        r->x = e->x;
        r->y = e->y;
        dx = ff_view.x - e->x;
        dy = ff_view.y - e->y;
        fix_sincos(e->heading, &s, &c);
        r->lx = fmul(dx, c) - fmul(dy, s);
        r->ly = fmul(dx, s) + fmul(dy, c);
        r->s = s;
        r->c = c;
        r->dz = e->z - ff_view.z;
    } else {
        TilePlace *p = &tile_places[node->place];
        TileTmpl *t = &tile_tmpls[node->tmpl];
        fix h;
        r->x = (long)((ulong)(long)(short)p->key << 18) + *(fix *)&t->pos[0];
        r->y = (long)((ulong)(long)(short)(p->key >> 16) << 18) + *(fix *)&t->pos[4];
        r->lx = ff_view.x - r->x;
        r->ly = ff_view.y - r->y;
        h = map_2949C4(r->x, r->y);
        r->s = 0;
        r->c = 0x10000;
        r->dz = *(fix *)&t->pos[8] + h - ff_view.z;
    }
}

/* 0x29F6EC */
uchar terrmesh_29F6EC(MeshNode *node, MeshRec *r)
{
    ShapeItem *sh;
    if (node->ent) {
        GameEntity *e = &game_entities[node->ent];
        sh = interact_2973E8(e->cls, e->type);
    } else {
        TileTmpl *t = &tile_tmpls[node->tmpl];
        sh = interact_2973E8(t->cls, t->type);
    }
    switch ((ushort)sh->type) {
    case 1:
        return terrmesh_29F770(sh, r);
    case 2:
    case 3:
        return terrmesh_2A00F8(sh, r);
    }
    return 0;
}

/* the cosine-like measure of the angle at the viewer between point k and point 0 (8.24) */
static long tm_cos(fix (*P)[2], long k, MeshRec *R, fix v2)
{
    fix bx = P[k][0] - R->lx, by = P[k][1] - R->ly;
    fix B = fmul(bx, bx) + fmul(by, by);
    fix cx = P[k][0] - P[0][0], cy = P[k][1] - P[0][1];
    fix C = fmul(cx, cx) + fmul(cy, cy);
    long s = (v2 + B - C) >> 8;
    long t = m24(s, s);
    long q = m24(v2 >> 8, B >> 8);
    long c = d24(t, q);
    return s > 0 ? c : -c;
}

/* 0x29F770: a four-point shape: a = the corner on the viewer's side of the others' split
 * (point 0, or the one alone on its side), b = the other corner at the widest angle */
uchar terrmesh_29F770(void *shape, MeshRec *R)
{
    fix (*P)[2] = ((ShapeItem *)shape)->pt;
    long pos[3], neg[3], np = 0, nn = 0, k, sel, c0, c1, c2;
    fix vx = R->lx - P[0][0], vy = R->ly - P[0][1];
    fix v2 = fmul(vx, vx) + fmul(vy, vy);
    for (k = 1; k < 4; k++) {
        if (fmul(P[k][0] - P[0][0], vy) - fmul(P[k][1] - P[0][1], vx) > 0)
            pos[np++] = k;
        else
            neg[nn++] = k;
    }
    if (np == 0) {
        R->a[0] = P[0][0];
        R->a[1] = P[0][1];
        c0 = tm_cos(P, neg[0], R, v2);
        c1 = tm_cos(P, neg[1], R, v2);
        c2 = tm_cos(P, neg[2], R, v2);
        if (c0 < c1)
            sel = c0 < c2 ? neg[0] : neg[2];
        else
            sel = c2 >= c1 ? neg[1] : neg[2];
    } else if (nn == 0) {
        R->a[0] = P[0][0];
        R->a[1] = P[0][1];
        c0 = tm_cos(P, pos[0], R, v2);
        c1 = tm_cos(P, pos[1], R, v2);
        c2 = tm_cos(P, pos[2], R, v2);
        if (c0 < c1)
            sel = c0 < c2 ? pos[0] : pos[2];
        else
            sel = c1 <= c2 ? pos[1] : pos[2];
    } else if (np == 1) {
        R->a[0] = P[pos[0]][0];
        R->a[1] = P[pos[0]][1];
        c0 = tm_cos(P, neg[0], R, v2);
        c1 = tm_cos(P, neg[1], R, v2);
        sel = c1 >= c0 ? neg[0] : neg[1];
    } else if (nn == 1) {
        R->a[0] = P[neg[0]][0];
        R->a[1] = P[neg[0]][1];
        c0 = tm_cos(P, pos[0], R, v2);
        c1 = tm_cos(P, pos[1], R, v2);
        sel = c1 >= c0 ? pos[0] : pos[1];
    } else
        return 1;
    R->b[0] = P[sel][0];
    R->b[1] = P[sel][1];
    return 1;
}

/* 0x2A00F8: a circle (radius after the points): the two tangent points seen from the viewer */
uchar terrmesh_2A00F8(void *shape, MeshRec *R)
{
    ShapeItem *sh = shape;
    fix r = sh->pt[sh->npts][0];
    fix r2 = fmul(r, r);
    fix d2 = fmul(R->lx, R->lx) + fmul(R->ly, R->ly);
    fix a, e, f, gg, h;
#ifndef TN_WATCOM_ABI
    if (d2 < r2) {
        /* G44 in docs/BUGS.md: with the eye inside the circle (against a round building's wall, on
         * its base or above it) the game had no edge and left the building out of the drawing: it
         * vanished.  Its edge is then the diameter across the line from the viewer, the edge
         * the tangent points tend to from far away; straight above the middle, across the view. */
        fix d = fix_sqrt(d2), ux, uy;
        if (d >= 0x100) {
            ux = fdiv(R->lx, d);
            uy = fdiv(R->ly, d);
        } else {
            ux = -(fmul(ff_view.cos_h, R->c) - fmul(ff_view.sin_h, R->s));
            uy = -(fmul(ff_view.cos_h, R->s) + fmul(ff_view.sin_h, R->c));
        }
        R->a[0] = fmul(r, uy);
        R->a[1] = -fmul(r, ux);
        R->b[0] = -R->a[0];
        R->b[1] = -R->a[1];
        return 1;
    }
#else
    if (d2 < r2)
        return 0;
#endif
    a = fmul(r, fix_sqrt(d2 - r2));
    e = fmul(r2, R->lx);
    f = fmul(a, R->ly);
    R->a[0] = fdiv(f + e, d2);
    R->b[0] = fdiv(e - f, d2);
    gg = fmul(r2, R->ly);
    h = fmul(a, R->lx);
    R->a[1] = fdiv(gg - h, d2);
    R->b[1] = fdiv(gg + h, d2);
    return 1;
}

/* where q lies against the line p1 p2 (all relative to the viewer): 0 = not between the rays
 * to p1 and p2, else 1 = on the far side, 2 = on the near side */
static long tm_side(fix p1x, fix p1y, fix p2x, fix p2y, fix qx, fix qy)
{
    fix cr = fmul(p1x, p2y) - fmul(p2x, p1y);
    fix c1 = fmul(qx, p1y) - fmul(p1x, qy);
    fix c2 = fmul(qx, p2y) - fmul(p2x, qy);
    fix dy, dx, w, X;
    int on;
    if (cr > 0)
        on = c1 < 0 && c2 > 0;
    else
        on = c1 > 0 && c2 < 0;
    if (!on)
        return 0;
    dy = p1y - p2y;
    dx = p1x - p2x;
    w = fmul(p1x, dy) - fmul(p1y, dx);
    X = w - fmul(qx, dy) + fmul(qy, dx);
    return (w > 0 ? X > 0 : X < 0) ? 1 : 2;
}

/* 0x2A01F8: 1 / 2 by the silhouette edges, else by height (3: opposite sides of the viewer) */
long terrmesh_2A01F8(MeshRec *A, MeshRec *B)
{
    fix a1x = fmul(A->a[0], A->c) + fmul(A->a[1], A->s) + A->x - ff_view.x;
    fix a1y = A->y + (fmul(A->a[1], A->c) - fmul(A->a[0], A->s)) - ff_view.y;
    fix a2x = fmul(A->b[0], A->c) + fmul(A->b[1], A->s) + A->x - ff_view.x;
    fix a2y = fmul(A->b[1], A->c) - fmul(A->b[0], A->s) + A->y - ff_view.y;
    fix b1x = fmul(B->a[0], B->c) + fmul(B->a[1], B->s) + B->x - ff_view.x;
    fix b1y = fmul(B->a[1], B->c) - fmul(B->a[0], B->s) + B->y - ff_view.y;
    fix b2x = fmul(B->b[0], B->c) + fmul(B->b[1], B->s) + B->x - ff_view.x;
    fix b2y = fmul(B->b[1], B->c) - fmul(B->b[0], B->s) + B->y - ff_view.y;
    long fc = 0, f10 = 0, f30 = 0, f34 = 0, k, r;
#ifndef TN_WATCOM_ABI
    for (k = 0; k < tm_nroof; k++) {    /* G45: a roof under the eye */
        if (tm_roof[k] == A)
            return tm_on(A, B) ? 1 : 2;
        if (tm_roof[k] == B)
            return tm_on(B, A) ? 2 : 1;
    }
#endif
    k = tm_side(a1x, a1y, a2x, a2y, b1x, b1y);
    if (k) fc = k;
    k = tm_side(a1x, a1y, a2x, a2y, b2x, b2y);
    if (k) f10 = k;
    k = tm_side(b1x, b1y, b2x, b2y, a1x, a1y);
    if (k == 1) f34 = 2; else if (k == 2) f30 = 1;
    k = tm_side(b1x, b1y, b2x, b2y, a2x, a2y);
    if (k == 1) f30 = 2; else if (k == 2) f30 = 1;
    r = f34 | f30 | fc | f10;
    if (r)
        return r;
    if (A->dz > 0) {
        if (B->dz < 0) return 3;
        return B->dz < A->dz ? 1 : 2;
    }
    if (B->dz > 0) return 3;
    return B->dz > A->dz ? 1 : 2;
}

/* 0x2A0A2C: does the segment a b cross the open box (r0, r1)?  Per direction of the segment:
 * the end points against the box, then the box corners against the line. */
uchar terrmesh_2A0A2C(fix *a, fix *b, fix *r0, fix *r1)
{
    if (a[0] < b[0]) {
        if (a[1] < b[1]) {
            if (a[0] >= r1[0] || b[0] <= r0[0] || a[1] >= r1[1] || b[1] <= r0[1]) return 0;
            if (fmul(r0[0] - a[0], b[1] - a[1]) >= fmul(b[0] - a[0], r1[1] - a[1])) return 0;
            if (fmul(r1[0] - a[0], b[1] - a[1]) <= fmul(b[0] - a[0], r0[1] - a[1])) return 0;
            return 1;
        }
        if (b[1] < a[1]) {
            if (a[0] >= r1[0] || b[0] <= r0[0] || b[1] >= r1[1] || a[1] <= r0[1]) return 0;
            if (fmul(r0[0] - a[0], a[1] - b[1]) >= fmul(b[0] - a[0], a[1] - r0[1])) return 0;
            if (fmul(r1[0] - a[0], a[1] - b[1]) <= fmul(b[0] - a[0], a[1] - r1[1])) return 0;
            return 1;
        }
        return a[0] < r1[0] && b[0] > r0[0] && a[1] < r1[1] && a[1] > r0[1];
    }
    if (b[0] < a[0]) {
        if (a[1] < b[1]) {
            if (b[0] >= r1[0] || a[0] <= r0[0] || a[1] >= r1[1] || b[1] <= r0[1]) return 0;
            if (fmul(a[0] - r1[0], b[1] - a[1]) >= fmul(a[0] - b[0], r1[1] - a[1])) return 0;
            if (fmul(a[0] - r0[0], b[1] - a[1]) <= fmul(a[0] - b[0], r0[1] - a[1])) return 0;
            return 1;
        }
        if (b[1] < a[1]) {
            if (b[0] >= r1[0] || a[0] <= r0[0] || b[1] >= r1[1] || a[1] <= r0[1]) return 0;
            if (fmul(a[0] - r1[0], a[1] - b[1]) >= fmul(a[0] - b[0], a[1] - r0[1])) return 0;
            if (fmul(a[0] - r0[0], a[1] - b[1]) <= fmul(a[0] - b[0], a[1] - r1[1])) return 0;
            return 1;
        }
        return b[0] < r1[0] && a[0] > r0[0] && a[1] < r1[1] && a[1] > r0[1];
    }
    if (a[1] < b[1])
        return a[0] < r1[0] && a[0] > r0[0] && a[1] < r1[1] && b[1] > r0[1];
    if (b[1] < a[1])
        return a[0] < r1[0] && a[0] > r0[0] && b[1] < r1[1] && a[1] > r0[1];
    return a[0] < r1[0] && a[0] > r0[0] && a[1] < r1[1] && a[1] > r0[1];
}
