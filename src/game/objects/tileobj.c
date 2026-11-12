/* tileobj.c -- Terra Nova module M073 (0x298C30-0x299582; source file name unknown): objects
 * that come with map tiles.  Decompiled from __FF.EXE v1.09; map names (tileobj_xxxxxx) kept
 * for the functions other modules call.
 *
 *   0x298C30 tile_reset_groups   0x298C74 tileobj_298C74 reset   0x298D2C tileobj_298D2C next group
 *   0x298D68 tile_id_alloc       0x298D84 tile_obj_place          0x298DEC tile_obj_link
 *   0x298E64 tileobj_298E64 add object     0x298EBC tileobj_298EBC add template
 *   0x299004 tileobj_299004 number groups  0x299048 tileobj_299048 clear placements
 *   0x299080 tileobj_299080 add placement  0x2991B4 tileobj_2991B4 find placement
 *   0x299200 tile_number                   0x299288 tileobj_299288 create a group's objects
 *
 * A template (TileTmpl) says which entity to create: class, type, a pool record to copy, a
 * position offset and the +0x21 dword.  Templates are numbered from 1 and collected in groups
 * (TileGroup: first template, count).  Objects (TileObj, ids 1..254) put a template on a cell
 * of the group's 4x4 pattern; the objects of one template form a circular list.
 * tileobj_299288 creates every template of a group at a map square: an entity per template,
 * a grid square per object of the template (pattern cell + square * 4).
 * Quirks kept:
 *  - tileobj_299288 first checks that the group fits by creating (and destroying again) count
 *    entities, all of the class of the group's first template.  The original keeps those
 *    entity ids in a 16-entry stack array, so a group of more than 16 templates overwrites its
 *    other locals (not reproduced; groups hold at most 16 here).
 *  - tileobj_299288 sets the entity's first-square word to the last square added.
 *  - tileobj_299080 sets the numbering base of the next free placement record (place + 1).
 *  - The placement lookups follow only the low byte of TilePlace.next.
 *  - Squares outside 0..127 are not checked: tileobj_299080 then reads / sets bits in the
 *    memory after tile_placed (tile_places).
 */
#include "g2_a.h"
#include "g5_a.h"     /* added at the G5-G8 merge */

#ifndef TN_WATCOM_ABI
#undef tile_places
#undef tile_3C9E1C
TilePlace *const port_tile_places_game = tile_places;
TilePlace port_tile_places_big[PORT_TILE_PLACES];
uchar *const port_tile_seen_game = tile_3C9E1C;
uchar port_tile_seen_big[PORT_TILE_IDS / 8];
#define tile_places (port_s3d_on ? port_tile_places_big : port_tile_places_game)
#define tile_3C9E1C (port_s3d_on ? port_tile_seen_big : port_tile_seen_game)
#endif

static void tile_reset_counters(void)
{
    tile_group = 1;
    tile_tmpl_next = 1;
    tile_id_next = 1;
    tile_grp_idx = 0;
    memset(tile_groups, 0, 0x260);
}

void tile_reset_groups(void)
{
    tile_reset_counters();
    tile_groups[1].first = 1;
}

void tileobj_298C74(void)
{
    tile_reset_counters();
    memset(tile_tmpls, 0, 0x1CE3);
    memset(tile_group_bits, 0, 4);
    memset(tile_places, 0, 0x1000);
    memset(tile_placed, 0, 0x800);
    memset(tile_3C9E1C, 0, 0x80);
    memset(tile_objs, 0, 0xFF0);
    memset(game_3C5E1C, 0, 0x4000);
    tile_groups[1].first = 1;
}

void tileobj_298D2C(void)
{
    TileGroup *g;
    tile_group++;
    g = &tile_groups[tile_group];
    g->first = tile_tmpl_next;
    tile_grp_idx = 0;
    g->count = 0;
}

uchar tile_id_alloc(void)
{
    if (tile_id_next >= 0xFF)
        return 0;
    return tile_id_next++;
}

bool tile_obj_place(uchar id, long d1, long d2, long d3, GridXY cell, long w)
{
    uchar *c = &tile_groups[tile_group].cell[0][0] + cell.x * 4 + cell.y;
    (void)d1; (void)d2; (void)d3;
    tile_objs[id].old = *c;
    *c = id;
    *(GridXY *)&tile_objs[id].x = cell;
    tile_objs[id].w = (short)w;
    return 1;
}

bool tile_obj_link(uchar id, ushort tmpl)
{
    uchar first = tile_tmpls[tmpl].first;
    if (first == 0) {
        tile_tmpls[tmpl].first = id;
        tile_objs[id].link = id;
    } else {
        tile_objs[id].link = tile_objs[first].link;
        tile_objs[first].link = id;
    }
    tile_objs[id].tmpl = tmpl;
    return 1;
}

uchar tileobj_298E64(ushort tmpl, long d1, long d2, long d3, GridXY cell, long w)
{
    uchar id = tile_id_alloc();
    if (id == 0)
        return 0;
    if (!tile_obj_link(id, tmpl))
        return 0;
    if (!tile_obj_place(id, d1, d2, d3, cell, w))
        return 0;
    return id;
}

bool tileobj_298EBC(uchar cls, ushort *tmpl, short *slot)
{
    ushort t = tile_tmpl_next;
    TileTmpl *p;
    GamePoolRec *r;
    if (t >= 0xFF)
        return 0;
    *tmpl = t;
    tile_tmpl_next = t + 1;
    *slot = pool_alloc(cls);
    if (*slot == 0) {
        tile_tmpl_next--;
        return 0;
    }
    p = &tile_tmpls[*tmpl];
    p->cls = cls;
    p->slot = *slot;
    p->idx = tile_grp_idx;
    p->group = (uchar)tile_group;
    tile_grp_idx++;
    tile_groups[tile_group].count++;
    r = (GamePoolRec *)(game_pools[cls].base + (long)*slot * game_pools[cls].size);
    r->w0 = (r->w0 & 0x8000) | (*tmpl & 0x7FFF);
    r->w0 |= 0x8000;
    return 1;
}

void tileobj_299004(void)
{
    short start = 1;
    long b;
    for (b = 1; b <= tile_group; b++) {
        tile_places[b].start = start;
        start += (signed char)tile_groups[b].count;
    }
}

void tileobj_299048(void)
{
#ifndef TN_WATCOM_ABI
    if (port_s3d_on)                    /* the groups' own records and numbering bases */
        memcpy(port_tile_places_big, port_tile_places_game, (tile_group + 1) * sizeof(TilePlace));
#endif
    tile_place_next = tile_group;
    memset(tile_group_bits, 0, 4);
    memset(tile_placed, 0, 0x800);
}

void tileobj_299080(uchar grp, GridXY sq)
{
    ushort n = tile_place_next;
    uchar *bits = &tile_placed[0][0] + sq.x * 16 + (sq.y >> 3);
    ulong key = *(ulong *)&sq;
    if (!(*bits & (1 << (sq.y & 7)))) {
        if (!(tile_group_bits[grp >> 3] & (1 << (grp & 7)))) {
            tile_places[grp].key = key;
            tile_places[grp].next = 0;
            tile_group_bits[grp >> 3] |= 1 << (grp & 7);
        } else {
#ifndef TN_WATCOM_ABI
            if (port_s3d_on && n + 1 >= PORT_TILE_PLACES)
                return;                 /* full: the square's objects are not drawn */
#endif
            tile_places[n].next = tile_places[grp].next;
            tile_places[grp].next = n;
            tile_places[n].key = key;
            tile_places[n + 1].start = (signed char)tile_groups[grp].count + tile_places[n].start;
            n++;
        }
        /* a square outside 0..127 indexes past tile_placed into tile_places: reload the byte */
        __asm__ volatile("" ::: "memory");
        *bits |= 1 << (sq.y & 7);
    }
    tile_place_next = n;
}

short tileobj_2991B4(uchar grp, GridXY sq)
{
#ifndef TN_WATCOM_ABI
    ushort i = grp;                     /* the bigger table's records past 255 too */
#else
    uchar i = grp;
#endif
    if (!(tile_group_bits[grp >> 3] & (1 << (grp & 7))))
        return 0;
    while (i != 0) {
        if (*(ulong *)&sq == tile_places[i].key)
            return i;
#ifndef TN_WATCOM_ABI
        i = port_s3d_on ? tile_places[i].next : (uchar)tile_places[i].next;
#else
        i = (uchar)tile_places[i].next;
#endif
    }
    return 0;
}

short tile_number(ushort tmpl, GridXY sq)
{
    uchar i = tile_tmpls[tmpl].group;
    if (!(tile_group_bits[i >> 3] & (1 << (i & 7))))
        return 0;
    while (i != 0) {
        if (*(ulong *)&sq == tile_places[i].key)
            return tile_places[i].start + (signed char)tile_tmpls[tmpl].idx;
        i = (uchar)tile_places[i].next;
    }
    return 0;
}

long tileobj_299288(uchar grp, GridXY sq, TileOut *out)
{
    short x4 = sq.x * 4, y4 = sq.y * 4;
    ushort t = tile_groups[grp].first;
    uchar cls = tile_tmpls[t].cls;
    short tslot = tile_tmpls[t].slot;
    signed char left = (signed char)tile_groups[grp].count;
    short ents[128], dummy, ent, slot;
    long n, made = 0;
    bool ok = 1;

    /* room check: create count entities and destroy them again */
    for (n = 0; n < left; n++) {
        if (!grid_295588(cls, &ents[n], &dummy)) {
            ok = 0;
            break;
        }
        game_entities[ents[n]].active = 1;
    }
    while (--n >= 0)
        grid_295730(ents[n]);
    if (!ok)
        return 0;

    while (left-- > 0) {
        GameEntity *e;
        TileTmpl *p;
        uchar id;
        short s = 0;
        if (!grid_295588(cls, &ent, &slot))
            return 0;
        e = &game_entities[ent];
        e->active = 1;
        p = &tile_tmpls[t];
        e->f05 = 0;
        e->type = p->type;
        memcpy(&e->x, p->pos, 0x12);
        e->x += (fix)x4 << 16;
        e->y += (fix)y4 << 16;
        e->z += map_2949C4(e->x, e->y);
        *(ulong *)&game_entities[ent].f21 = p->f21;
        if (!pool_copy(cls, tslot, slot))
            return 0;
        id = p->first;
        if (id != 0) {
            uchar k = id;
            do {
                GridKey key;
                TileObj *o = &tile_objs[k];
                key.x = o->x + x4;
                key.y = o->y + y4;
                *(short *)&key.e0 = o->w;
                s = grid_add_square(ent, 0, 0, 0, key);
                if (s == 0)
                    return 0;
                k = (uchar)o->link;
            } while (k != id);
            game_entities[ent].f05 = s;
        }
        if (out) {
            out->xy = *(ulong *)&sq;
            out->tmpl = t;
            out->ent = ent;
            out++;
        }
        t++;
        made++;
        cls = tile_tmpls[t].cls;
        tslot = tile_tmpls[t].slot;
    }
    game_3C5E1C[sq.x][sq.y] = 0;
    return made;
}
