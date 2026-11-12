/* objshape.c -- Terra Nova object shapes: footprints on the map grid and collision with
 * objects (module M072; source name unknown; the binary names ObjectInteraction and
 * ObjectTiledInteraction).  Decompiled from __FF.EXE v1.09; other names are the map's.
 *
 *   0x2972C0 interact_2972C0  load the shape resources (base id + class 0..8, type 8 = compound)
 *   0x297380 interact_297380  unload them
 *   0x2973E8 interact_2973E8  shape item of (class, type)
 *   0x297400 interact_297400  fill an object footprint (map cells covered) for the grid
 *   0x29775C interact_29775C  is (x, y) inside an object's polygon
 *   0x297924 ObjectInteraction       collision of a point with an object
 *   0x2979AC ObjectTiledInteraction  ... with a tile object
 *   0x297A40 interact_297A40  dispatch on the shape type (0 none, 1 polygon, 2 cylinder, 3 dome)
 *   0x297AE0 interact_297AE0  precompute a polygon (ShapeExt: padded corners, edges, floor plane)
 *   0x297DAC interact_297DAC  polygon collision: push-out vector, floor normal
 *   0x2980CC interact_2980CC  cylinder collision: top normal only (always returns FALSE)
 *   0x298218 interact_298218  dome collision
 *   0x29837C interact_29837C  aim point on an object for a viewer at (x, y)
 *   0x2986C0 interact_2986C0  shape height
 *   0x298730 interact_298730  object position + height (mode 0 middle, 1 top, 2 base)
 *   0x298854 / 0x298864 / 0x298870  modes 1 / 0 / 2
 *   0x298880 interact_298880  polygon corner sector of a local point (0..3)
 *   0x298980 interact_298980  signed distance of a point to an object
 *   0x2989E8 interact_2989E8  ... to a tile object
 *   0x298A50 interact_298A50  ... to a shape at pos
 * Data: jump tables at 0x297A30, 0x2986B0, 0x298700, 0x298720, 0x298A40.
 *
 * Shape item (compound resource per class, one item per type): short type, short npts,
 * npts points {x, y}, then per type: polygon 4 corner heights + ShapeExt pointer (+0x10 after
 * the points), cylinder radius, dome radius + height.  pos arguments point at an entity's x
 * (x, y, z, then the heading word at +0xC).
 * Quirks kept:
 *  - the crossing tables of interact_29775C / interact_297DAC have room for two crossings;
 *    the two edge tests always use both slots (zero / stale when only one was found);
 *  - interact_2980CC never reports a push-out (returns FALSE) and its fallback normal signs
 *    differ between x (dx < 0 -> +2) and y (dy > 0 -> +2);
 *  - interact_298A50 for domes returns the distance without subtracting the radius when the
 *    point is above or below the dome; interact_29837C's z argument (ecx) is unused.
 */
#include "g2_c.h"
#include "g2_a.h"      /* tile_tmpls (TileTmpl, owner g2a) */
#include "g5_c.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
ushort shape_base_id;
RefTable *shape_tables[9];
Pool shape_pool;
fix shape_radius;
#endif

#define R shape_radius
#define SHAPE_OF(cls, type) \
    ((ShapeItem *)((uchar *)shape_tables[(uchar)(cls)] + shape_tables[(uchar)(cls)]->offset[(uchar)(type)]))

void interact_2972C0(long base)
{
    uchar c;

    if (shape_base_id)
        return;
    PoolInit(&shape_pool, 0x5C, 8, 0);
    shape_base_id = (ushort)base;
    for (c = 0; c < 9; c++) {
        long i;
        if ((*(ushort *)&gResDesc2[(ushort)shape_base_id + c] & 0xFF00) != 0x800)
            continue;
        shape_tables[c] = ResLock((ushort)(shape_base_id + c));
        for (i = 0; i < shape_tables[c]->numRefs; i++) {
            ShapeItem *s = (ShapeItem *)((uchar *)shape_tables[c] + shape_tables[c]->offset[i]);
            if (s->type == 1)
                interact_297AE0(s);
        }
    }
}

void interact_297380(void)
{
    uchar c;

    if (!shape_base_id)
        return;
    for (c = 0; c < 9; c++)
        if ((*(ushort *)&gResDesc2[(ushort)shape_base_id + c] & 0xFF00) == 0x800)
            ResUnlock((ushort)(shape_base_id + c));
    PoolFreeAll(&shape_pool);
    shape_base_id = 0;
}

ShapeItem *interact_2973E8(long cls, long type)
{
    return SHAPE_OF(cls, type);
}

uchar interact_297400(ObjFoot *f, long cls, long type)
{
    ShapeItem *s = SHAPE_OF(cls, type);
    fix sn, cs;
    long xmin, xmax, ymin, ymax, pxmin, pxmax, pymin, pymax;
    long i, k, x, y;
    uchar c = (uchar)cls;

    xmin = xmax = pxmin = pxmax = f->x;
    ymin = ymax = pymin = pymax = f->y;
    fix_sincos(f->heading, &sn, &cs);
    for (i = 0; i < s->npts; i++) {
        fix px = s->pt[i][0], py = s->pt[i][1];
        fix rx = G2C_FIXMUL(px, cs) - G2C_FIXMUL(py, sn) + f->x;
        fix ry = G2C_FIXMUL(px, sn) + G2C_FIXMUL(py, cs) + f->y;
        if (rx < xmin)
            xmin = rx;
        if (rx > xmax)
            xmax = rx;
        if (ry < ymin)
            ymin = ry;
        if (ry > ymax)
            ymax = ry;
        if (c == 0 || c == 4) {
            /* padded box: corners pushed away from the centre by the collision radius */
            rx = rx > f->x ? rx + R : rx - R;
            ry = ry > f->y ? ry + R : ry - R;
            if (rx < pxmin)
                pxmin = rx;
            if (rx > pxmax)
                pxmax = rx;
            if (ry < pymin)
                pymin = ry;
            if (ry > pymax)
                pymax = ry;
        }
    }
    if (c != 0 && c != 4) {
        pxmin = xmin;
        pymin = ymin;
        pxmax = xmax;
        pymax = ymax;
    }
    if ((pxmin >>= 16) < 0)
        pxmin = 0;
    if ((pxmax >>= 16) >= 0x200)
        pxmax = 0x1FF;
    if ((pymin >>= 16) < 0)
        pymin = 0;
    if ((pymax >>= 16) >= 0x200)
        pymax = 0x1FF;
    if ((xmin >>= 16) < 0)
        xmin = 0;
    if ((xmax >>= 16) >= 0x200)
        xmax = 0x1FF;
    if ((ymin >>= 16) < 0)
        ymin = 0;
    if ((ymax >>= 16) >= 0x200)
        ymax = 0x1FF;
    if ((pxmax - pxmin + 1) * (pymax - pymin + 1) > 0x79)
        return 0;
    k = 0;
    for (x = pxmin; x <= pxmax; x++) {
        uchar ex = (x < xmin || x > xmax);
        for (y = pymin; y <= pymax; y++, k++) {
            f->cells[k].x = (short)x;
            f->cells[k].y = (short)y;
            f->cells[k].edge = (ex || y < ymin || y > ymax);
            f->cells[k].zero = 0;
        }
    }
    f->cells[k].x = -1;
    f->cells[k].y = -1;
    if (xmax - xmin + 1 > 11)
        return 0;
    if (ymax - ymin + 1 > 11)
        return 0;
    for (k = 0, x = xmin; x <= xmax; x++)
        f->xs[k++] = (short)x;
    f->xs[k] = -1;
    for (k = 0, y = ymin; y <= ymax; y++)
        f->ys[k++] = (short)y;
    f->ys[k] = -1;
    return 1;
}

/* The crossing table of the two point-in-polygon tests: for each polygon edge crossed by the
 * line x = lx (in the shape frame), the edge's end points.  The original keeps four arrays of
 * two entries in consecutive stack slots and does not bound the count; L models that frame. */
uchar interact_29775C(long obj, fix x, fix y)
{
    GameEntity *e = &G2C_ENT(obj);
    ShapeItem *s = SHAPE_OF(e->cls, e->type);
    long L[13] = {0};                    /* 0 C, 2 B, 4 A, 6 D, 8 sin, 9 cos, 10 ly */
    fix lx, dx, dy;
    long n = 0, i, k;
    uchar prev, cur;

    fix_sincos((ushort)-e->heading, (fix *)&L[8], (fix *)&L[9]);
    dx = x - e->x;
    dy = y - e->y;
    lx = G2C_FIXMUL(dx, L[9]) - G2C_FIXMUL(dy, L[8]);
    L[10] = G2C_FIXMUL(dx, L[8]) + G2C_FIXMUL(dy, L[9]);
    prev = lx < s->pt[0][0];
    for (i = 1; i < s->npts; i++) {
        cur = lx < s->pt[i][0];
        if (cur != prev) {
            L[4 + n] = s->pt[i - 1][0];
            L[2 + n] = s->pt[i - 1][1];
            L[0 + n] = s->pt[i][0];
            L[6 + n] = s->pt[i][1];
            n++;
        }
        prev = cur;
    }
    cur = lx < s->pt[0][0];
    if (cur != prev) {
        L[4 + n] = s->pt[i - 1][0];
        L[2 + n] = s->pt[i - 1][1];
        L[0 + n] = s->pt[0][0];
        L[6 + n] = s->pt[0][1];
        n++;
    }
    if (n == 0)
        return 0;
    for (k = 0; k < 2; k++) {
        fix t1 = G2C_FIXMUL(L[0 + k] - L[4 + k], L[10] - L[2 + k]);
        fix t2 = G2C_FIXMUL(L[6 + k] - L[2 + k], lx - L[4 + k]);
        if (t1 - t2 < 0)
            return 0;
    }
    return 1;
}

uchar ObjectInteraction(long obj, fix x, fix y, fix z, fix *vec, fix *nrm, uchar *hit, long *edge)
{
    short o = (short)obj;
    GameEntity *e = &G2C_ENT(o);
    ShapeItem *s = SHAPE_OF(e->cls, e->type);

    if (s == NULL)
        DbgReportError(1, "ObjectInteraction: object %d has bad pshape!\n", (long)o);
    return interact_297A40(&e->x, s, x, y, z, vec, nrm, hit, edge);
}

uchar ObjectTiledInteraction(long tile, fix *pos, fix x, fix y, fix z, fix *vec, fix *nrm,
                             uchar *hit, long *edge)
{
    ulong t = tile & 0xFFFF;
    uchar cls = tile_tmpls[t].cls, type = tile_tmpls[t].type;
    ShapeItem *s = SHAPE_OF(cls, type);

    if (s == NULL)
        DbgReportError(1, "ObjectTiledInteraction: tileobj %d, class %d subclass %d has bad pshape!\n",
                       t, (long)cls, (long)type);
    return interact_297A40(pos, s, x, y, z, vec, nrm, hit, edge);
}

uchar interact_297A40(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix *vec, fix *nrm, uchar *hit,
                      long *edge)
{
    *edge = 0;
    switch ((ushort)s->type) {
    case 0:
        vec[2] = 0;
        vec[1] = vec[2];
        vec[0] = vec[2];
        return 0;
    case 1:
        return interact_297DAC(pos, s, x, y, z, vec, nrm, hit, edge);
    case 2:
        return interact_2980CC(pos, s, x, y, z, vec, nrm, hit);
    case 3:
        return interact_298218(pos, s, x, y, z, vec, nrm, hit);
    }
    DbgReportError(1, "ObjectInteraction: bad shape type: %d\n", (long)s->type);
    return 0;
}

void interact_297AE0(ShapeItem *s)
{
    fix *h = SHAPE_TAIL(s);
    ShapeExt *x = PoolAlloc(&shape_pool);
    ushort ang[4];
    int i, a, b;

    *(ShapeExt **)(h + 4) = x;
    for (i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        ang[i] = fix_atan2(s->pt[j][1] - s->pt[i][1], s->pt[j][0] - s->pt[i][0]);
    }
    /* corners pushed out along the bisector so that every edge moves out by R */
    for (i = 0; i < 4; i++) {
        long half = ((long)ang[(i + 3) % 4] - (long)ang[i] + 0x8000) / 2;
        fix r = G2C_FIXDIV(R, fix_sin((ushort)half));
        fix sn, cs;
        fix_sincos((ushort)(half + ang[i] + 0x8000), &sn, &cs);
        x->pt[i][0] = G2C_FIXMUL(r, cs) + s->pt[i][0];
        x->pt[i][1] = G2C_FIXMUL(r, sn) + s->pt[i][1];
    }
    for (i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        fix dx = x->pt[j][0] - x->pt[i][0], dy = x->pt[j][1] - x->pt[i][1];
        fix len = fix_pyth_dist(dx, dy);
        x->n[i][0] = G2C_FIXDIV(dy, len);
        x->n[i][1] = G2C_FIXDIV(-dx, len);
        x->d[i] = -(G2C_FIXMUL(x->n[i][0], x->pt[i][0]) + G2C_FIXMUL(x->n[i][1], x->pt[i][1]));
    }
    /* floor plane through corner 0 and a corner that differs from it in x and y */
    a = 0;
    b = 2;
    if (s->pt[0][0] == s->pt[2][0] || s->pt[2][1] == s->pt[0][1]) {
        b = 1;
        if (s->pt[0][0] == s->pt[1][0] || s->pt[0][1] == s->pt[1][1])
            b = 3;
    }
    x->zx = G2C_FIXDIV(h[b] - h[a], s->pt[b][0] - s->pt[a][0]);
    x->zy = G2C_FIXDIV(h[b] - h[a], s->pt[b][1] - s->pt[a][1]);
    x->z0 = h[a] - G2C_FIXMUL(s->pt[a][0], x->zx) - G2C_FIXMUL(s->pt[a][1], x->zy);
}

uchar interact_297DAC(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix *vec, fix *nrm, uchar *hit,
                      long *edge)
{
    /* M: the original's frame from esp: 0..2 push-out, 3/5/7/9 crossing tables, 11 sin,
       12 cos, 14 dz */
    long M[15] = {0};
    ShapeExt *x_ = SHAPE_EXT(s);
    fix dx, dy, lx, ly, dist, pen, pz;
    long n = 0, i, k, e;
    uchar prev, cur;

    vec[0] = vec[1] = vec[2] = 0;
    *hit = 0;
    fix_sincos((ushort)-*(ushort *)&pos[3], (fix *)&M[11], (fix *)&M[12]);
    dx = x - pos[0];
    dy = y - pos[1];
    lx = G2C_FIXMUL(dx, M[12]) - G2C_FIXMUL(dy, M[11]);
    ly = G2C_FIXMUL(dx, M[11]) + G2C_FIXMUL(dy, M[12]);
    M[14] = z - pos[2];
    if (M[14] < -0x4000)
        return 0;
    prev = lx < x_->pt[0][0];
    for (i = 1; i < 4; i++) {
        cur = lx < x_->pt[i][0];
        if (cur != prev) {
            M[5 + n] = x_->pt[i - 1][0];
            M[3 + n] = x_->pt[i - 1][1];
            M[7 + n] = x_->pt[i][0];
            M[9 + n] = x_->pt[i][1];
            n++;
        }
        prev = cur;
    }
    cur = lx < x_->pt[0][0];
    if (cur != prev) {
        M[5 + n] = x_->pt[3][0];
        M[3 + n] = x_->pt[3][1];
        M[7 + n] = x_->pt[0][0];
        M[9 + n] = x_->pt[0][1];
        n++;
    }
    if (n == 0)
        return 0;
    for (k = 0; k < 2; k++) {
        fix t1 = G2C_FIXMUL(M[7 + k] - M[5 + k], ly - M[3 + k]);
        fix t2 = G2C_FIXMUL(M[9 + k] - M[3 + k], lx - M[5 + k]);
        if (t1 - t2 < 0)
            return 0;
    }
    e = interact_298880(lx, ly, x_);
    dist = G2C_FIXMUL(x_->n[e][0], lx) + x_->d[e] + G2C_FIXMUL(x_->n[e][1], ly) + R;
    pen = R - dist;
    if (pen > R)
        pen = R;
    else if (pen < 0)
        pen = 0;
    M[0] = G2C_FIXMUL(x_->n[e][0], pen);
    M[1] = G2C_FIXMUL(x_->n[e][1], pen);
    *edge = e;
    M[2] = 0;
    pz = G2C_FIXMUL(x_->zx, lx) + x_->z0 + G2C_FIXMUL(x_->zy, ly);
    if (dist < 0) {
        *hit = 1;
        nrm[0] = x_->zx;
        nrm[1] = x_->zy;
        nrm[2] = pos[2] + pz;
    }
    if (pz + R < M[14])
        return 0;
    /* rotate the push-out back to the world frame */
    vec[0] = G2C_FIXMUL(M[0], M[12]) + G2C_FIXMUL(M[1], M[11]);
    vec[1] = G2C_FIXMUL(M[1], M[12]) - G2C_FIXMUL(M[0], M[11]);
    vec[2] = M[2];
    return 1;
}

uchar interact_2980CC(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix *vec, fix *nrm, uchar *hit)
{
    fix r = SHAPE_TAIL(s)[0];
    fix dx, dy, dz, d2, r2, k, ax;

    vec[2] = 0;
    vec[1] = vec[2];
    vec[0] = vec[2];
    *hit = 0;
    dx = x - pos[0];
    dy = y - pos[1];
    dz = z - pos[2];
    d2 = G2C_FIXMUL(dx, dx) + G2C_FIXMUL(dy, dy);
    r2 = G2C_FIXMUL(r, r);
    if (d2 - r2 > 0)
        return 0;
    *hit = 1;
    nrm[2] = fix_sqrt(r2 - d2);
    k = G2C_FIXMUL(dz, 0x200000);
    ax = dx < 0 ? -dx : dx;
    if (k > ax)
        nrm[0] = -G2C_FIXDIV(dx, dz);
    else
        nrm[0] = dx < 0 ? 0x200000 : -0x200000;
    ax = dy < 0 ? -dy : dy;
    if (k > ax)
        nrm[1] = -G2C_FIXDIV(dy, dz);
    else
        nrm[1] = dy > 0 ? 0x200000 : -0x200000;
    nrm[2] += pos[2];
    return 0;
}

uchar interact_298218(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix *vec, fix *nrm, uchar *hit)
{
    fix *t = SHAPE_TAIL(s);
    fix dx, dy, dz, d2, d, e, pen, rr, v0 = 0, v1 = 0;
    uchar res = 0;

    vec[2] = 0;
    vec[1] = vec[2];
    vec[0] = vec[2];
    *hit = 0;
    dz = z - pos[2];
    dx = x - pos[0];
    dy = y - pos[1];
    d2 = G2C_FIXMUL(dx, dx) + G2C_FIXMUL(dy, dy);
    rr = G2C_FIXMUL(t[0] + R, t[0] + R);
    if (d2 > rr)
        return 0;
    d = fix_sqrt(d2);
    e = d - t[0];
    if (e > R)
        return 0;
    if (e < 0)
        e = 0;
    pen = R - e;
    if (t[1] >= dz && d > t[0] / 2) {
        fix q = G2C_FIXDIV(pen, d);
        v0 = G2C_FIXMUL(dx, q);
        v1 = G2C_FIXMUL(dy, q);
        res = 1;
    }
    if (pen == R) {
        *hit = 1;
        nrm[0] = 0;
        nrm[1] = 0;
        nrm[2] = pos[2] + t[1];
    }
    vec[0] = v0;
    vec[1] = v1;
    return res;
}

uchar interact_29837C(long obj, fix x, fix y, fix z, fix *ox, fix *oy, fix *oz)
{
    short o = (short)obj;
    GameEntity *e;
    ShapeItem *s;
    fix *t;

    (void)z;
    if (viewdist_2A2204(o, ox, oy, oz))
        return 1;
    e = &G2C_ENT(o);
    s = SHAPE_OF(e->cls, e->type);
    t = SHAPE_TAIL(s);
    switch ((ushort)s->type) {
    case 1: {
        ShapeExt *xe = *(ShapeExt **)(t + 4);
        fix sn, cs, dx, dy, lx, ly, mx, my, mh;
        long i, j;
        fix_sincos((ushort)-e->heading, &sn, &cs);
        dx = x - e->x;
        dy = y - e->y;
        lx = G2C_FIXMUL(dx, cs) - G2C_FIXMUL(dy, sn);
        ly = G2C_FIXMUL(dx, sn) + G2C_FIXMUL(dy, cs);
        i = interact_298880(lx, ly, xe);
        j = (i + 1) & 3;
        mx = (xe->pt[i][0] + xe->pt[j][0]) / 2;
        my = (xe->pt[i][1] + xe->pt[j][1]) / 2;
        mh = (t[i] + t[j]) / 2;
        mx = G2C_FIXMUL(mx, 0x11000);
        my = G2C_FIXMUL(my, 0x11000);
        mh = G2C_FIXMUL(mh, 0xC000);
        *ox = G2C_FIXMUL(mx, cs) + G2C_FIXMUL(my, sn);
        *oy = G2C_FIXMUL(my, cs) - G2C_FIXMUL(mx, sn);
        *oz = mh;
        return 1;
    }
    case 2: {
        fix sn, cs;
        fix_sincos(fix_atan2(y - e->y, x - e->x), &sn, &cs);
        *ox = G2C_FIXMUL(cs, t[0]);
        *oy = G2C_FIXMUL(sn, t[0]);
        *oz = t[0] / 2;
        return 1;
    }
    case 3: {
        fix sn, cs;
        fix_sincos(fix_atan2(y - e->y, x - e->x), &sn, &cs);
        *ox = G2C_FIXMUL(cs, t[0]);
        *oy = G2C_FIXMUL(sn, t[0]);
        *oz = t[1] / 2;
        *ox = G2C_FIXMUL(*ox, 0x11000);
        *oy = G2C_FIXMUL(*oy, 0x11000);
        return 1;
    }
    }
    return 1;
}

fix interact_2986C0(ShapeItem *s)
{
    switch ((ushort)s->type) {
    case 1:
        return SHAPE_EXT(s)->z0;
    case 2:
        return SHAPE_TAIL(s)[0];
    case 3:
        return SHAPE_TAIL(s)[1];
    }
    return 0;
}

uchar interact_298730(long obj, fix *out, ulong mode)
{
    short o = (short)obj;
    GameEntity *e = &G2C_ENT(o);
    ShapeItem *s;
    uchar half = 0;
    fix h = 0;

    switch (e->cls) {
    case 1:
    case 2:
    case 3:
        half = 1;
        break;
    case 5:
    case 6:
    case 7:
        out[2] = 0;
        out[1] = out[2];
        out[0] = out[2];
        return 0;
    }
    out[0] = e->x;
    out[1] = e->y;
    out[2] = e->z;
    s = SHAPE_OF(e->cls, e->type);
    switch ((ushort)s->type) {
    case 1:
        h = SHAPE_EXT(s)->z0;
        break;
    case 2:
        h = SHAPE_TAIL(s)[0];
        break;
    case 3:
        h = SHAPE_TAIL(s)[1];
        break;
    }
    if (half)
        out[2] -= h / 2;
    if (mode == 0)
        h /= 2;
    else if (mode == 2)
        h = 0;
    out[2] += h;
    return 1;
}

uchar interact_298854(long obj, fix *out)
{
    return interact_298730((short)obj, out, 1);
}

uchar interact_298864(long obj, fix *out)
{
    return interact_298730((short)obj, out, 0);
}

uchar interact_298870(long obj, fix *out)
{
    return interact_298730((short)obj, out, 2);
}

/* sign of the cross product of corner i with the point: which corner sector holds (lx, ly) */
#define CORNER(e, i, lx, ly) (G2C_FIXMUL((e)->pt[i][0], ly) - G2C_FIXMUL(lx, (e)->pt[i][1]))

long interact_298880(fix lx, fix ly, ShapeExt *e)
{
    if (CORNER(e, 0, lx, ly) > 0) {
        if (CORNER(e, 1, lx, ly) <= 0)
            return 0;
        if (CORNER(e, 2, lx, ly) <= 0)
            return 1;
        return CORNER(e, 3, lx, ly) > 0 ? 3 : 2;
    }
    if (CORNER(e, 3, lx, ly) > 0)
        return 3;
    if (CORNER(e, 2, lx, ly) > 0)
        return 2;
    if (CORNER(e, 1, lx, ly) > 0)
        return 1;
    return 0;
}

fix interact_298980(long obj, fix x, fix y, fix z, long *edge)
{
    short o = (short)obj;
    GameEntity *e = &G2C_ENT(o);

    return interact_298A50(o, &e->x, SHAPE_OF(e->cls, e->type), x, y, z, edge);
}

fix interact_2989E8(long tile, fix *pos, fix x, fix y, fix z, long *edge)
{
    ushort t = (ushort)tile;

    return interact_298A50(0, pos, SHAPE_OF(tile_tmpls[t].cls, tile_tmpls[t].type), x, y, z, edge);
}

fix interact_298A50(long obj, fix *pos, ShapeItem *s, fix x, fix y, fix z, long *edge)
{
    fix dx = x - pos[0], dy = y - pos[1], dz = z - pos[2];
    fix *t;

    *edge = 0;
    switch ((ushort)s->type) {
    case 1: {
        ShapeExt *xe = SHAPE_EXT(s);
        fix sn, cs, lx, ly;
        long i;
        fix_sincos((ushort)-*(ushort *)&pos[3], &sn, &cs);
        lx = G2C_FIXMUL(dx, cs) - G2C_FIXMUL(dy, sn);
        ly = G2C_FIXMUL(dx, sn) + G2C_FIXMUL(dy, cs);
        i = interact_298880(lx, ly, xe);
        *edge = i;
        return G2C_FIXMUL(xe->n[i][0], lx) + xe->d[i] + G2C_FIXMUL(xe->n[i][1], ly) + R;
    }
    case 2:
        t = SHAPE_TAIL(s);
        return fix_sqrt(G2C_FIXMUL(dx, dx) + G2C_FIXMUL(dy, dy) + G2C_FIXMUL(dz, dz)) - t[0];
    case 3: {
        fix r2;
        t = SHAPE_TAIL(s);
        if ((short)obj && G2C_ENT(obj).body)
            dz -= t[1] / 2;
        if (dz > t[1])
            dz -= t[1] / 2;
        else if (dz > 0)
            dz = 0;
        r2 = G2C_FIXMUL(dx, dx) + G2C_FIXMUL(dy, dy);
        if (dz == 0)
            return fix_sqrt(r2) - t[0];
        return fix_sqrt(G2C_FIXMUL(dz, dz) + r2);
    }
    }
    return 0;
}
