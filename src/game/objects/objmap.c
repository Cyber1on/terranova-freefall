/* objmap.c -- Terra Nova module M080 (source name unknown): objects stamped onto the 512 x 512
 * map grid.  Decompiled from __FF.EXE v1.09; names ours (map names objlist_*).
 *
 *   0x2A3230 objmap_add          0x2A32E4 objmap_find         0x2A3320 objmap_kill_cb
 *   0x2A3324 objmap_remove       0x2A3390 objmap_update       0x2A33B8 objmap_clear
 *   0x2A33E8 objmap_update_one   0x2A347C objmap_hash_init    0x2A34E0 objmap_node_pop
 *   0x2A34FC objmap_node_push    0x2A3514 objmap_node_free    0x2A359C objmap_node_get
 *   0x2A364C objmap_node_find    0x2A36AC objmap_node_move    0x2A36C8 objmap_node_sub
 *   0x2A3704 objmap_cell_add     0x2A37A8 objmap_cell_sub     0x2A381C objmap_stamp
 *   0x2A39BC objmap_unstamp      0x2A3B1C objmap_query
 *
 * Up to 20 objects (objmap_tab) each put an 8 x 8 stamp (objmap_stamps[type]: centre offset
 * and 64 signed values, column major) onto the map around their grid position.  A map cell
 * (3 bytes, row pointers at +0x84E of the map record *game_map_35BFB4) keeps a 3-bit level in
 * bits 7..9 of its first word; what a stamp adds above that level goes into a hashed overflow
 * node (512 nodes of {x, y, count, next}, 256 chains keyed (x ^ y) & 0xFF, free list), and the
 * value each object actually added is remembered in its 8 x 8 copy so objmap_unstamp can take
 * it back.  objmap_update re-stamps the objects that moved to another cell.
 * Quirks kept:
 *  - objmap_cell_add stores 0 as the cell level when it raises it (the overflow node keeps
 *    the difference) and otherwise level - value, ORed in without masking (a negative stamp
 *    value sets bits above bit 9);
 *  - objmap_cell_add / objmap_stamp take the stamp value as a signed byte;
 *  - objmap_query tests the row range as "y0 > py or y0 >= y0 + 8" (the second test is always
 *    false), so points below the stamp read past its column;
 *  - objmap_node_move (unreferenced) adds to a node's count with a 16-bit add.
 */
#include "g2_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
ObjMapEnt objmap_tab[OBJMAP_MAX];
long objmap_count;
long objmap_free;
short objmap_hash[256];
ObjMapNode objmap_nodes[512];
ObjMapStamp objmap_stamps[1];
uchar *game_map_35BFB4;
#endif

#define CELL(x, y) ((ushort *)(((uchar **)(game_map_35BFB4 + 0x84E))[y] + (x) * 3))

uchar objmap_add(long ent, long type)
{
    ObjMapEnt *e;
    if (ENT(ent)->active == 0)
        return 0;
    if (objmap_count == OBJMAP_MAX)
        return 0;
    e = &objmap_tab[objmap_count];
    e->gx = (short)((ENT(ent)->x + 0x8000) >> 16);
    e->ent = (short)ent;
    e->type = type;
    e->gy = (short)((ENT(ent)->y + 0x8000) >> 16);
    objmap_stamp(objmap_count);
    objmap_count++;
    return 1;
}

uchar objmap_find(long ent)
{
    long i;
    for (i = 0; i < objmap_count; i++)
        if ((short)ent == objmap_tab[i].ent)
            return 1;
    return 0;
}

/* killobj callback: same as objmap_remove (the original sign-extends ax and falls into it) */
uchar objmap_kill_cb(long ent)
{
    return objmap_remove((short)ent);
}

uchar objmap_remove(long ent)
{
    long i;
    for (i = 0; i < objmap_count; i++) {
        if ((short)ent == objmap_tab[i].ent) {
            objmap_unstamp(i);
            objmap_count--;
            objmap_tab[i] = objmap_tab[objmap_count];
            return 1;
        }
    }
    return 0;
}

void objmap_update(void)
{
    long i;
    for (i = 0; i < objmap_count; i++)
        objmap_update_one(i);
}

void objmap_clear(void)
{
    long i;
    for (i = 0; i < objmap_count; i++)
        objmap_unstamp(i);
    objmap_count = 0;
}

void objmap_update_one(long i)
{
    ObjMapEnt *e = &objmap_tab[i];
    GameEntity *o = ENT(e->ent);
    long nx = (o->x + 0x8000) >> 16;
    long ny = (o->y + 0x8000) >> 16;
    if (nx == e->gx && ny == e->gy)
        return;
    objmap_unstamp(i);
    objmap_tab[i].gx = (short)nx;
    objmap_tab[i].gy = (short)ny;
    objmap_stamp(i);
}

void objmap_hash_init(void)
{
    long k;
    for (k = 0; k < 512; k++)
        objmap_nodes[k].next = (short)(k - 1);
    objmap_free = 0x1FF;
    lg_memset(objmap_hash, 0xFFFFFFFF, 0, 0x200);
}

/* unreferenced */
long objmap_node_pop(void)
{
    long n = objmap_free;
    if (n >= 0)
        objmap_free = objmap_nodes[n].next;
    return n;
}

/* unreferenced */
void objmap_node_push(long n)
{
    short f = (short)objmap_free;
    objmap_free = n;
    objmap_nodes[n].next = f;
}

void objmap_node_free(long n)
{
    ObjMapNode *nd = &objmap_nodes[n];
    long h = ((nd->y & 0xFF) ^ (nd->x & 0xFF));
    long p = objmap_hash[h];
    if (p == n)
        objmap_hash[h] = nd->next;
    else {
        while (objmap_nodes[p].next != n)
            p = objmap_nodes[p].next;
        objmap_nodes[p].next = objmap_nodes[n].next;
    }
    objmap_nodes[n].next = (short)objmap_free;
    objmap_free = n;
}

long objmap_node_get(long x, long y)
{
    long h = (y & 0xFF) ^ (x & 0xFF);
    long n, f;
    for (n = objmap_hash[h]; n >= 0; n = objmap_nodes[n].next)
        if ((long)(ushort)objmap_nodes[n].x == x && (long)(ushort)objmap_nodes[n].y == y)
            return n;
    f = objmap_free;
    if (f >= 0)
        objmap_free = objmap_nodes[f].next;
    if (f < 0)
        return f;
    objmap_nodes[f].x = (short)x;
    objmap_nodes[f].y = (short)y;
    objmap_nodes[f].count = 0;
    objmap_nodes[f].next = objmap_hash[h];
    objmap_hash[h] = (short)f;
    return f;
}

long objmap_node_find(long x, long y)
{
    long n;
    for (n = objmap_hash[(x & 0xFF) ^ (y & 0xFF)]; n >= 0; n = objmap_nodes[n].next)
        if ((long)(ushort)objmap_nodes[n].x == x && (long)(ushort)objmap_nodes[n].y == y)
            break;
    return n;
}

/* unreferenced */
long objmap_node_move(long x, long y, long from, long to)
{
    long n = objmap_node_get(x, y);
    if (n < 0)
        return from;
    objmap_nodes[n].count = (short)(objmap_nodes[n].count + (to - from));
    return to;
}

long objmap_node_sub(long x, long y, long d)
{
    long n = objmap_node_find(x, y);
    long c;
    if (n < 0)
        return d;
    c = (ushort)objmap_nodes[n].count - d;
    if (c > 0) {
        objmap_nodes[n].count = (short)c;
        return 0;
    }
    objmap_node_free(n);
    return -c;
}

void objmap_cell_add(long x, long y, uchar *out, long val)
{
    long cur = (*CELL(x, y) >> 7) & 7;
    long lvl;
    *out = (uchar)val;
    if (cur < val) {
        long n = objmap_node_get(x, y);
        if (n >= 0) {
            objmap_nodes[n].count = (short)(objmap_nodes[n].count + (val - cur));
            cur = val;
        }
        *out = (uchar)cur;
        lvl = 0;
    } else
        lvl = cur - val;
    *CELL(x, y) &= 0xFC7F;
    *CELL(x, y) |= (ushort)(lvl << 7);
}

void objmap_cell_sub(long x, long y, long val)
{
    long lvl = (*CELL(x, y) >> 7) & 7;
    if (lvl == 0)
        lvl = objmap_node_sub(x, y, val);
    else
        lvl += val;
    *CELL(x, y) &= 0xFC7F;
    *CELL(x, y) |= (ushort)(lvl << 7);
}

void objmap_stamp(long i)
{
    ObjMapEnt *e = &objmap_tab[i];
    long gy = e->gy, gx = e->gx;
    ObjMapStamp *st = &objmap_stamps[e->type];
    long x0, x1, y0, y1, x, y, sx, sy;

    x0 = gx - st->cx;
    if (x0 < 0)
        x0 = 0;
    x1 = gx - st->cx + 8;
    if (x1 >= 0x200)
        x1 = 0x200;
    y0 = gy - st->cy;
    if (y0 < 0)
        y0 = 0;
    y1 = gy - st->cy + 8;
    if (y1 >= 0x200)
        y1 = 0x200;
    sx = st->cx + x0 - gx;
    for (x = x0; x < x1; x++, sx++) {
        sy = st->cy + y0 - gy;
        for (y = y0; y < y1; y++, sy++)
            objmap_cell_add(x, y, &objmap_tab[i].cells[sx * 8 + sy], st->v[sx * 8 + sy]);
    }
}

void objmap_unstamp(long i)
{
    ObjMapEnt *e = &objmap_tab[i];
    long gy = e->gy, gx = e->gx;
    ObjMapStamp *st = &objmap_stamps[e->type];
    long x0, x1, y0, y1, x, y, sx, sy;

    x0 = gx - st->cx;
    if (x0 < 0)
        x0 = 0;
    x1 = gx - st->cx + 8;
    if (x1 >= 0x200)
        x1 = 0x200;
    y0 = gy - st->cy;
    if (y0 < 0)
        y0 = 0;
    y1 = gy - st->cy + 8;
    if (y1 >= 0x200)
        y1 = 0x200;
    sx = st->cx + x0 - gx;
    for (x = x0; x < x1; x++, sx++) {
        sy = st->cy + y0 - gy;
        for (y = y0; y < y1; y++, sy++)
            objmap_cell_sub(x, y, (signed char)objmap_tab[i].cells[sx * 8 + sy]);
    }
}

/* the strongest stamp value at map point pt (x | y << 16); out: x, y (16.16 grid position of
 * that object), +0xC the value */
uchar objmap_query(ulong pt, long *out)
{
    long px = (short)pt, py = (long)pt >> 16;
    long best = 0, i;
    for (i = 0; i < objmap_count; i++) {
        ObjMapEnt *e = &objmap_tab[i];
        long gx = e->gx, gy = e->gy, x0, y0, d, v;
        ObjMapStamp *st;
        d = gx - px;
        if ((d < 0 ? -d : d) >= 5)
            continue;
        d = gy - py;
        if ((d < 0 ? -d : d) >= 5)
            continue;
        st = &objmap_stamps[e->type];
        x0 = gx - st->cx;
        y0 = gy - st->cy;
        if (px < x0 || px >= x0 + 8)
            continue;
        if (y0 > py || y0 >= y0 + 8)
            continue;
        v = st->v[(px - x0) * 8 + (py - y0)];
        if (v <= best)
            continue;
        out[0] = (long)objmap_tab[i].gx << 16;
        out[3] = v;
        best = v;
        out[1] = (long)objmap_tab[i].gy << 16;
    }
    return best != 0;
}
