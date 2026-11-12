/* objgrid.c -- Terra Nova entity table, class pools and object spatial grid (module M071,
 * 0x2953F0-0x2972C0; source file name unknown).  Decompiled from __FF.EXE v1.09; names ours
 * except grid_xxxxxx map names kept for functions other modules call.
 *
 *   0x2953F0 grid_2953F0 (reset)  0x295588 grid_295588 (create)  0x295638 grid_set_pos
 *   0x29565C grid_free_square     0x295690 grid_free_cellrec     0x2956B0 grid_add_square
 *   0x2956EC grid_add_cellrec     0x295730 grid_295730 (destroy) 0x295788 grid_change_class
 *   0x29582C grid_29582C (update) 0x295CB4 grid_hash_pop_free    0x295CD4 grid_hash_free
 *   0x295CFC grid_295CFC (lookup) 0x295DF0 grid_hash_remove      0x295F50 grid_iter_reset
 *   0x295F5C grid_iter_next       0x295FB0 grid_hash_stats       0x296020 grid_debug_key
 *   0x296120 grid_check           0x2967B4 grid_debug_ent        0x296828 ent_alloc
 *   0x2968C0 ent_free             0x29696C sq_alloc              0x2969B0 cellrec_alloc
 *   0x296A0C sq_free              0x296A3C cellrec_free          0x296A84 pool_alloc
 *   0x296AEC pool_free            0x296B64 pool_copy             0x296BB8 sq_hash_unlink
 *   0x296D38 cellrec_cell_unlink  0x296DC0 sq_ent_link           0x296E2C cellrec_ent_link
 *   0x296ED0 sq_ent_unlink        0x296F58 cellrec_ent_unlink    0x297018 sq_hash_insert
 *   0x297114 cellrec_cell_link    0x297164 ent_free_squares      0x2971E4 ent_free_cellrecs
 *   0x29727C ent_free_record
 *
 * Entities (game_entities, 300 x 0x25): entity 0 is the list header (+5 first used entity,
 * +7 first free entity).  Per entity: +3 pool slot, +5 first grid square (circular list through
 * GridSquare.ent_next), +7 next / +D previous entity in the used list, +9 / +B first cell
 * record of layer 0 / 1 (circular through GridCellRec.ent_next).  Each class has a pool
 * (game_pools, game.h) whose record w0 holds the entity id.
 * Grid squares are hashed by key (GRID_HASH): a hash entry holds the first square with that key,
 * squares with the same key chain through GridSquare.next; buckets 0x200..0x3FF overflow into
 * entries 1..0x1FF.  Cells (2 layers x 0x200) hold lists of cell records.
 * Quirks kept:
 *  - grid_change_class clears the entity's active byte and does not set it again.
 *  - grid_29582C returns the result of the last layer only (a failed layer 0 is forgotten).
 *  - sq_ent_unlink / cellrec_ent_unlink return the entity (or its +0 word with stack garbage
 *    above it) when the record was the only one, 0 otherwise; the only-one case leaves the
 *    record's entity field set.
 *  - grid_295CFC moves a found entry into the bucket slot by swapping with the bucket, and
 *    returns ax = 0 (eax high word from the bucket * 4) when nothing is found.
 *  - grid_iter_next reads past the table when the iterator is above 0x400.
 *  - grid_hash_pop_free drops the entry it takes off the free list (dead code).
 */
#include "g2_a.h"

#define ENT(e) game_entities[(short)(e)]
#define E_SQ(e) (*(short *)&ENT(e).f05)
#define E_NEXT(e) (*(short *)&ENT(e).f07)
#define E_LAYER(e, l) (((short *)&ENT(e).f09)[l])
#define E_PREV(e) (*(short *)&ENT(e).f0d)
#define SQ(s) grid_squares[(short)(s)]
#define CR(l, r) grid_cellrecs[l][(short)(r)]
#define POOLREC(p, i) ((GamePoolRec *)((p)->base + (long)(short)(i) * (p)->size))

static int key_eq(void *a, void *b) { return *(ulong *)a == *(ulong *)b; }

#ifndef TN_WATCOM_ABI
/* Freefall (G19 in docs/BUGS.md): in a big fight the game's 127 effect objects (explosions,
 * smoke, tracers, particles, bodies) can all be in use, and a unit killed then vanishes with
 * only its sound.  Here they have room for 255.  Ordinary effects still stop at 127 in use, as
 * in the game (the same records and entities then); only the effects of a unit, vehicle or
 * building being destroyed (port_sfx_kill) take the others, and only while 96 entities stay
 * free for units and shots. */
uchar port_cls6[PORT_CLS6_N][0x2C];
long port_sfx_kill;

static bool cls6_room(void)
{
    GamePool *p = &game_pools[6];
    long n = 0;
    short s;
    for (s = ((GamePoolRec *)p->base)->w0 & 0x7FFF; s; s = POOLREC(p, s)->next)
        n++;
    if (n < 0x7F)
        return 1;
    if (!port_sfx_kill)
        return 0;
    for (n = 0, s = E_NEXT(0); s; s = E_NEXT(s))
        n++;
    return n >= 96;
}
#endif

/* reset the entity table, the pools and the grid */
void grid_2953F0(void)
{
    uchar i;
    long k;
#ifndef TN_WATCOM_ABI
    game_pools[6].count = PORT_CLS6_N;      /* G19 */
    game_pools[6].base = port_cls6[0];
#endif
    memset(game_entities, 0, 0x2B5C);
    memset(grid_squares, 0, 0x4B00);
    memset(grid_cellrecs, 0, 0x2580);
    memset(grid_cells, 0, 0x800);
    for (i = 0; i < 9; i++)
        memset(game_pools[i].base, 0, (long)game_pools[i].count * game_pools[i].size);
    for (k = 0; k < 0x12B; k++)
        E_NEXT(k) = (short)(k + 1);
    E_NEXT(0x12B) = 0;
    E_SQ(0) = 0;
    for (k = 0; k < 0x4AF; k++)
        SQ(k).next = (short)(k + 1);
    SQ(0x4AF).next = 0;
    for (k = 0; k < 0x257; k++) {
        CR(0, k).next = (short)(k + 1);
        CR(1, k).next = (short)(k + 1);
    }
    CR(0, 0x257).next = 0;
    CR(1, 0x257).next = 0;
    for (i = 0; i < 9; i++) {
        GamePool *p = &game_pools[i];
        long j = 0;
        ((GamePoolRec *)p->base)->w0 &= 0x8000;
        while (j < p->count - 1) {
            POOLREC(p, j)->next = (short)(j + 1);
            j++;
        }
        POOLREC(p, p->count - 1)->next = 0;
    }
    memset(grid_hash, 0, 0x1000);
    for (k = 0; k < 0x1FF; k++)
        grid_hash[k].next = (short)(k + 1);
    grid_hash[0x1FF].next = 0;
}

/* new entity of class cls with a record in the class pool */
bool grid_295588(uchar cls, short *ent, short *slot)
{
    GamePool *p;
    GamePoolRec *r;
    *ent = ent_alloc();
    if (*ent == 0)
        return 0;
    *slot = pool_alloc(cls);
    if (*slot == 0) {
        ent_free(*ent);
        return 0;
    }
    ENT(*ent).cls = cls;
    ENT(*ent).slot = *slot;
    p = &game_pools[cls];
    r = POOLREC(p, *slot);
    r->w0 = (r->w0 & 0x8000) | (*ent & 0x7FFF);
    return 1;
}

/* copy x, y, z, heading and the two words after it (0x12 bytes) */
bool grid_set_pos(short ent, void *pos)
{
    memcpy(&ENT(ent).x, pos, 0x12);
    return 1;
}

void grid_free_square(short sq)
{
    sq_hash_unlink(sq);
    sq_ent_unlink(sq);
    SQ(sq).next = SQ(0).next;
    SQ(0).next = sq;
}

void grid_free_cellrec(short rec, long layer)
{
    cellrec_cell_unlink(rec, layer);
    cellrec_free(rec, 1, layer);
}

short grid_add_square(short ent, long d1, long d2, long d3, GridKey key)
{
    short sq = sq_alloc();
    if (sq) {
        sq_ent_link(sq, ent);
        sq_hash_insert(sq, 0, 0, 0, key);
    }
    return sq;
}

short grid_add_cellrec(short ent, short cell, long layer)
{
    short rec = cellrec_alloc(layer);
    if (rec) {
        cellrec_ent_link(rec, ent, layer);
        cellrec_cell_link(rec, cell, layer);
    }
    return rec;
}

/* destroy an entity: its squares, cell records, the entity and its pool record */
bool grid_295730(short ent)
{
    uchar cls;
    short slot;
    ent_free_squares(ent);
    ent_free_cellrecs(ent, 0);
    ent_free_cellrecs(ent, 1);
    cls = ENT(ent).cls;
    slot = ENT(ent).slot;
    ent_free(ent);
    pool_free(cls, slot);
    return 1;
}

/* move an entity to another class pool */
bool grid_change_class(short ent, uchar cls)
{
    short slot;
    GamePool *p;
    GamePoolRec *r;
    ENT(ent).active = 0;
    if (!pool_free(ENT(ent).cls, ENT(ent).slot))
        return 0;
    slot = pool_alloc(cls);
    if (slot == 0)
        return 0;
    ENT(ent).cls = cls;
    ENT(ent).slot = slot;
    p = &game_pools[cls];
    r = POOLREC(p, slot);
    r->w0 = (r->w0 & 0x8000) | (ent & 0x7FFF);
    return 1;
}

/* set an entity's position, squares and cells from a descriptor: keeps the squares and cell
 * records that are still wanted, frees the others and adds the new ones */
bool grid_29582C(GridObjDesc *d)
{
    short ent = d->ent;
    short old[0x7A];            /* squares of the entity (esp+0x4C0) */
    GridKey add[0x79];          /* new keys (esp+0) */
    short oldc[0x7A];           /* cell records (esp+0x2D8) */
    short addc[0x7A];           /* new cells (esp+0x3CC) */
    long nold = 0, nadd = 0, nc, naddc, layer, j, n;
    GridKey *k;
    short *c;
    short head, s;
    bool res = 0;

    memcpy(&ENT(ent).x, d->pos, 0x12);
    head = E_SQ(ent);
    if (head) {
        s = head;
        do {
            old[nold++] = s;
            s = SQ(s).ent_next;
        } while (s != head);
    }
    for (k = d->sq; k->x != -1 || k->y != -1; k++) {
        n = nold;
        for (j = 0; j < n; j++) {
            if (key_eq(&SQ(old[j]), k)) {
                *(ushort *)&SQ(old[j]).e0 = *(ushort *)&k->e0;
                old[j] = old[nold - 1];
                nold--;
                goto found;
            }
        }
        add[nadd++] = *k;
    found:;
    }
    for (j = 0; j < nold; j++) {
        sq_hash_unlink(old[j]);
        sq_free(old[j], 1);
    }
    for (j = 0; j < nadd; j++) {
        GridKey key = add[j];
        s = sq_alloc();
        if (s) {
            sq_ent_link(s, ent);
            sq_hash_insert(s, 0, 0, 0, key);
        }
        if (s == 0)
            break;
    }
    for (layer = 0; layer <= 1; layer++) {
        nc = 0;
        naddc = 0;
        head = E_LAYER(ent, layer);
        if (head) {
            s = head;
            do {
                oldc[nc++] = s;
                s = CR(layer, s).ent_next;
            } while (s != head);
        }
        for (c = d->layers[layer]; *c != -1; c++) {
            n = nc;
            for (j = 0; j < n; j++) {
                if (CR(layer, oldc[j]).cell == *c) {
                    oldc[j] = oldc[nc - 1];
                    nc--;
                    goto cfound;
                }
            }
            addc[naddc++] = *c;
        cfound:;
        }
        for (j = 0; j < nc; j++) {
            cellrec_cell_unlink(oldc[j], layer);
            cellrec_free(oldc[j], 1, layer);
        }
        res = 1;
        for (j = 0; j < naddc; j++) {
            if (grid_add_cellrec(ent, addc[j], layer) == 0) {
                res = 0;
                break;
            }
        }
    }
    return res;
}

/* takes the first entry off the overflow free list and forgets it */
void grid_hash_pop_free(void)
{
    short f = grid_hash[0].next;
    if (f)
        grid_hash[0].next = grid_hash[f].next;
}

void grid_hash_free(short i)
{
    grid_hash[i].sq = 0;
    grid_hash[i].next = grid_hash[0].next;
    grid_hash[0].next = i;
}

/* look for key in the chain after bucket; a hit is swapped into the bucket slot.  With alloc,
 * a miss takes a free entry, moves the bucket's square into it (appended to the chain) and
 * returns the bucket with sq = 0 for the caller to fill.  0 = not found / no free entry. */
short grid_295CFC(GridXY key, bool alloc, short bucket)
{
    short e = bucket, nx, f, t;
    for (;;) {
        nx = grid_hash[e].next;
        if (nx == 0)
            break;
        e = nx;
        if (key_eq(&grid_squares[grid_hash[nx].sq], &key)) {
            t = grid_hash[bucket].sq;
            grid_hash[bucket].sq = grid_hash[nx].sq;
            grid_hash[nx].sq = t;
            return bucket;
        }
    }
    if (!alloc)
        return 0;
    f = grid_hash[0].next;
    if (f)
        grid_hash[0].next = grid_hash[f].next;
    if (f == 0)
        return 0;
    grid_hash[e].next = f;
    grid_hash[f].sq = grid_hash[bucket].sq;
    grid_hash[f].next = 0;
    grid_hash[bucket].sq = 0;
    return bucket;
}

/* drop the hash entry of key */
bool grid_hash_remove(GridXY key)
{
    short h = GRID_HASH(key.x, key.y), nx, prev, cur;
    if (grid_hash[h].sq == 0)
        return 0;
    if (key_eq(&grid_squares[grid_hash[h].sq], &key)) {
        nx = grid_hash[h].next;
        if (nx == 0) {
            grid_hash[h].sq = 0;
            return 1;
        }
        grid_hash[h].sq = grid_hash[nx].sq;
        grid_hash[h].next = grid_hash[nx].next;
        grid_hash[nx].sq = 0;
        grid_hash[nx].next = grid_hash[0].next;
        grid_hash[0].next = nx;
        return 1;
    }
    prev = h;
    cur = grid_hash[h].next;
    while (cur) {
        if (key_eq(&grid_squares[grid_hash[cur].sq], &key)) {
            grid_hash[prev].next = grid_hash[cur].next;
            grid_hash[cur].sq = 0;
            grid_hash[cur].next = grid_hash[0].next;
            grid_hash[0].next = cur;
            return 1;
        }
        prev = cur;
        cur = grid_hash[cur].next;
    }
    return 0;
}

void grid_iter_reset(void)
{
    game_entity_3C323C = 0;
}

/* next used hash entry: *sq = its first square */
bool grid_iter_next(short *sq)
{
    long i = game_entity_3C323C;
    bool r;
    if (i < 0x400) {
        while (grid_hash[i].sq == 0) {
            i++;
            if (i * 4 >= 0x1000)
                break;
        }
    }
    if (i == 0x400) {
        r = 0;
    } else {
        *sq = grid_hash[i].sq;
        i++;
        r = 1;
    }
    game_entity_3C323C = i;
    return r;
}

/* histogram of bucket chain lengths (0..10) into game_entity_lists */
void grid_hash_stats(void)
{
    long b, n, e;
    lg_memset32((ulong *)game_entity_lists, 0, 0, 11);
    for (b = 0x200; b < 0x400; b++) {
        n = 0;
        if (grid_hash[b].sq != 0) {
            for (e = b; e; e = grid_hash[e].next)
                n++;
        }
        if (n > 10)
            n = 10;
        game_entity_lists[n]++;
    }
}

/* debug print of the squares at key (into a local buffer) */
void grid_debug_key(GridXY key)
{
    char buf[0x50];
    short e, s;
    grid_hash_cur = GRID_HASH(key.x, key.y);
    if (grid_hash[grid_hash_cur].sq == 0)
        e = 0;
    else if (key_eq(&grid_squares[grid_hash[grid_hash_cur].sq], &key))
        e = grid_hash_cur;
    else
        e = grid_295CFC(key, 0, grid_hash_cur);
    s = grid_hash[e].sq;
    if (s)
        sprintf(buf, "%d %d", key.x, key.y);
    buf[0] = 0;
    for (; s; s = SQ(s).next)
        sprintf(buf, "%d %d", SQ(s).e0, SQ(s).e1);
}

/* consistency check of the entity lists, pools and grid (debug, unreferenced) */
bool grid_check(void)
{
    uchar sqmark[0x4B0];
    GridXY keys[0x79];
    char buf[0x50], buf2[0x50];
    uchar mark[300];
    short a, s, e, head, cur;
    GridXY key;
    long i, n, j;

    memset(mark, 0, 300);
    for (a = E_NEXT(0); a; a = E_NEXT(a)) {
        if (a < 0 || a >= 300)
            return 0;
        if (ENT(a).active != 0)
            return 0;
        if (mark[a] == 1)
            return 0;
        mark[a] = 1;
    }
    for (a = E_SQ(0); a; a = E_NEXT(a)) {
        if (a < 0 || a >= 300)
            return 0;
        if (mark[a] == 1)
            return 0;
        if (mark[a] == 2)
            return 0;
        mark[a] = 2;
    }
    for (a = 1; a < 300; a++)
        if (mark[a] == 0)
            return 0;
    memset(sqmark, 0, 0x4B0);
    for (a = SQ(0).next; a; a = SQ(a).next) {
        if (a < 0 || a >= 0x4B0)
            return 0;
        if (sqmark[a] == 1)
            return 0;
        sqmark[a] = 1;
    }
    for (i = 0; i < 9; i++) {
        GamePool *p = &game_pools[i];
        memset(mark, 0, 300);
        for (a = ((GamePoolRec *)p->base)->next; a; a = POOLREC(p, a)->next) {
            if (a < 0 || a >= p->count)
                return 0;
            if (mark[a] == 1)
                return 0;
            mark[a] = 1;
        }
        for (a = (short)(((GamePoolRec *)p->base)->w0 << 1) >> 1; a; a = POOLREC(p, a)->next) {
            if (a < 0 || a >= p->count)
                return 0;
            if (mark[a] == 1)
                return 0;
            if (mark[a] == 2)
                return 0;
            mark[a] = 2;
        }
        for (a = 1; a < p->count; a++)
            if (mark[a] == 0)
                return 0;
    }
    game_entity_3C323C = 0;
    while (grid_iter_next(&s)) {
        key = *(GridXY *)&SQ(s);
        if (s == 0)
            continue;
        for (;;) {
            uchar m = sqmark[s];
            if (m & 1) {
                sprintf(buf, "%d %d", key.x, key.y);
                return 0;
            }
            if (m & 4) {
                sprintf(buf, "%d %d", key.x, key.y);
                return 0;
            }
            if (!key_eq(&SQ(s), &key)) {
                sprintf(buf, "%d %d", key.x, key.y);
                sprintf(buf2, "%d %d", SQ(s).x, SQ(s).y);
                return 0;
            }
            if (SQ(s).ent == 0) {
                sprintf(buf, "%d %d", key.x, key.y);
                return 0;
            }
            sqmark[s] |= 4;
            s = SQ(s).next;
            if (s == 0)
                break;
        }
    }
    for (e = E_SQ(0); e; e = E_NEXT(e)) {
        n = 0;
        if (ENT(e).active == 0)
            continue;
        head = E_SQ(e);
        cur = head;
        if (head) {
            do {
                if (n == 0x79)
                    return 0;
                if (SQ(cur).ent != e)
                    return 0;
                for (j = 0; j < n; j++) {
                    if (key_eq(&keys[j], &SQ(cur))) {
                        sprintf(buf, "%d %d", keys[j].x, keys[j].y);
                        return 0;
                    }
                }
                keys[n++] = *(GridXY *)&SQ(cur);
                cur = SQ(cur).ent_next;
            } while (cur != head);
        }
        if (ENT(e).slot != 0) {
            GamePool *p;
            GamePoolRec *r;
            if (ENT(e).cls >= 9)
                return 0;
            p = &game_pools[ENT(e).cls];
            r = POOLREC(p, ENT(e).slot);
            if ((short)(r->w0 << 1) >> 1 != e || (r->w0 & 0x8000))
                return 0;
        }
    }
    return 1;
}

/* debug print of an entity's squares (into a local buffer) */
void grid_debug_ent(short ent)
{
    char buf[0x50];
    short head, s;
    if (ENT(ent).active == 0)
        return;
    head = E_SQ(ent);
    if (head == 0)
        return;
    s = head;
    do {
        sprintf(buf, "sq %d %d ext %d", SQ(s).x, SQ(s).y, SQ(s).e0);
        s = SQ(s).ent_next;
    } while (s != head);
}

/* take an entity off the free list and put it at the head of the used list */
short ent_alloc(void)
{
    short e = E_NEXT(0);
    if (e == 0)
        return 0;
    E_NEXT(0) = E_NEXT(e);
    E_NEXT(e) = E_SQ(0);
    E_PREV(E_SQ(0)) = e;
    E_PREV(e) = 0;
    E_SQ(0) = e;
    E_SQ(e) = 0;
    E_LAYER(e, 0) = 0;
    E_LAYER(e, 1) = 0;
    pool_2953DC((uchar *)&ENT(e) + 0x21);
    return e;
}

bool ent_free(short e)
{
    ENT(e).active = 0;
    if (E_PREV(e) == 0)
        E_SQ(0) = E_NEXT(e);
    else
        E_NEXT(E_PREV(e)) = E_NEXT(e);
    E_PREV(E_NEXT(e)) = E_PREV(e);
    E_NEXT(e) = E_NEXT(0);
    E_NEXT(0) = e;
    return 1;
}

short sq_alloc(void)
{
    short s = SQ(0).next;
    if (s == 0)
        return 0;
    SQ(0).next = SQ(s).next;
    SQ(s).ent = 0;
    SQ(s).next = 0;
    SQ(s).ent_next = 0;
    return s;
}

short cellrec_alloc(long layer)
{
    short r = CR(layer, 0).next;
    if (r == 0)
        return 0;
    CR(layer, 0).next = CR(layer, r).next;
    CR(layer, r).ent = 0;
    CR(layer, r).next = 0;
    CR(layer, r).ent_next = 0;
    return r;
}

short sq_free(short sq, bool unlink)
{
    short r = unlink ? sq_ent_unlink(sq) : 0;
    SQ(sq).next = SQ(0).next;
    SQ(0).next = sq;
    return r;
}

short cellrec_free(short rec, bool unlink, long layer)
{
    short r = unlink ? cellrec_ent_unlink(rec, layer) : 0;
    CR(layer, rec).next = CR(layer, 0).next;
    CR(layer, 0).next = rec;
    return r;
}

short pool_alloc(uchar cls)
{
    GamePool *p = &game_pools[cls];
    GamePoolRec *h = (GamePoolRec *)p->base;
    short s = h->next;
    GamePoolRec *r;
    if (s == 0)
        return 0;
#ifndef TN_WATCOM_ABI
    if (cls == 6 && !cls6_room())
        return 0;
#endif
    r = POOLREC(p, s);
    h->next = r->next;
    r->next = h->w0;
    POOLREC(p, (short)h->w0)->prev = s;
    r->prev = 0;
    h->w0 = s;
    return s;
}

bool pool_free(uchar cls, short slot)
{
    GamePool *p = &game_pools[cls];
    GamePoolRec *h = (GamePoolRec *)p->base;
    GamePoolRec *r = POOLREC(p, slot);
    if (r->prev == 0)
        h->w0 = r->next;
    else
        POOLREC(p, r->prev)->next = r->next;
    POOLREC(p, r->next)->prev = r->prev;
    r->next = h->next;
    h->next = slot;
    return 1;
}

/* copy a record's data (everything after the 6-byte links) */
bool pool_copy(uchar cls, short src, short dst)
{
    GamePool *p = &game_pools[cls];
    long n = p->size - 6;
    if (n > 0)
        memcpy((uchar *)POOLREC(p, dst) + 6, (uchar *)POOLREC(p, src) + 6, n);
    return 1;
}

/* take a square out of the hash */
void sq_hash_unlink(short sq)
{
    short e, nx, *pp;
    GridXY key = *(GridXY *)&SQ(sq);
    grid_hash_cur = GRID_HASH(SQ(sq).x, SQ(sq).y);
    if (grid_hash[grid_hash_cur].sq == 0)
        e = 0;
    else if (key_eq(&grid_squares[grid_hash[grid_hash_cur].sq], &SQ(sq)))
        e = grid_hash_cur;
    else
        e = grid_295CFC(key, 0, grid_hash_cur);
    if (e == 0)
        return;
    if (grid_hash[e].sq == sq) {
        nx = SQ(sq).next;
        if (nx == 0) {
            if (!grid_hash_remove(key))
                return;
        } else {
            grid_hash[e].sq = nx;
        }
    } else {
        pp = &grid_squares[grid_hash[e].sq].next;
        while (*pp != sq)
            pp = &grid_squares[*pp].next;
        *pp = SQ(sq).next;
    }
    SQ(sq).next = 0;
    SQ(sq).x = -1;
    SQ(sq).y = -1;
}

void cellrec_cell_unlink(short rec, long layer)
{
    short *pp = &grid_cells[layer][CR(layer, rec).cell];
    while (*pp != rec)
        pp = &CR(layer, *pp).next;
    *pp = CR(layer, rec).next;
    CR(layer, rec).next = 0;
    CR(layer, rec).cell = -1;
}

bool sq_ent_link(short sq, short ent)
{
    short h = E_SQ(ent);
    if (h == 0) {
        E_SQ(ent) = sq;
        SQ(sq).ent_next = sq;
    } else {
        SQ(sq).ent_next = SQ(h).ent_next;
        SQ(h).ent_next = sq;
    }
    SQ(sq).ent = ent;
    return 1;
}

bool cellrec_ent_link(short rec, short ent, long layer)
{
    short h = E_LAYER(ent, layer);
    if (h == 0) {
        E_LAYER(ent, layer) = rec;
        CR(layer, rec).ent_next = rec;
    } else {
        CR(layer, rec).ent_next = CR(layer, h).ent_next;
        CR(layer, h).ent_next = rec;
    }
    CR(layer, rec).ent = ent;
    return 1;
}

short sq_ent_unlink(short sq)
{
    short nx = SQ(sq).ent_next, ent = SQ(sq).ent, p;
    if (nx == sq) {
        E_SQ(ent) = 0;
        return ent;
    }
    p = sq;
    while (SQ(p).ent_next != sq)
        p = SQ(p).ent_next;
    SQ(p).ent_next = SQ(sq).ent_next;
    E_SQ(ent) = p;
    SQ(sq).ent = 0;
    return 0;
}

short cellrec_ent_unlink(short rec, long layer)
{
    short ent = CR(layer, rec).ent, p;
    if (CR(layer, rec).ent_next == rec) {
        E_LAYER(ent, layer) = 0;
        return ent;
    }
    p = rec;
    while (CR(layer, p).ent_next != rec)
        p = CR(layer, p).ent_next;
    CR(layer, p).ent_next = CR(layer, rec).ent_next;
    E_LAYER(ent, layer) = p;
    CR(layer, rec).ent = 0;
    return 0;
}

/* put a square into the hash under key (fails when no overflow entry is free) */
bool sq_hash_insert(short sq, long d1, long d2, long d3, GridKey key)
{
    char buf[0x50];
    GridXY xy = *(GridXY *)&key;
    short e;
    grid_hash_cur = GRID_HASH(key.x, key.y);
    if (grid_hash[grid_hash_cur].sq == 0)
        e = grid_hash_cur;
    else if (key_eq(&grid_squares[grid_hash[grid_hash_cur].sq], &key))
        e = grid_hash_cur;
    else
        e = grid_295CFC(xy, 1, grid_hash_cur);
    if (e == 0) {
        sprintf(buf, "%d %d", key.x, key.y);
        return 0;
    }
    SQ(sq).next = grid_hash[e].sq;
    grid_hash[e].sq = sq;
    *(GridXY *)&SQ(sq) = xy;
    *(ushort *)&SQ(sq).e0 = *(ushort *)&key.e0;
    return 1;
}

bool cellrec_cell_link(short rec, short cell, long layer)
{
    short *pp = &grid_cells[layer][cell];
    CR(layer, rec).next = *pp;
    *pp = rec;
    CR(layer, rec).cell = cell;
    return 1;
}

bool ent_free_squares(short ent)
{
    short s = E_SQ(ent), nx;
    if (s == 0)
        return 1;
    for (;;) {
        nx = SQ(s).ent_next;
        sq_hash_unlink(s);
        sq_ent_unlink(s);
        SQ(s).next = SQ(0).next;
        SQ(0).next = s;
        if (s == nx)
            break;
        s = nx;
    }
    E_SQ(ent) = 0;
    return 1;
}

bool ent_free_cellrecs(short ent, long layer)
{
    short r = E_LAYER(ent, layer), nx;
    if (r == 0)
        return 1;
    for (;;) {
        nx = CR(layer, r).ent_next;
        cellrec_cell_unlink(r, layer);
        cellrec_free(r, 1, layer);
        if (r == nx)
            break;
        r = nx;
    }
    E_LAYER(ent, layer) = 0;
    return 1;
}

/* free the entity and its pool record (no grid cleanup) */
bool ent_free_record(short ent)
{
    uchar cls = ENT(ent).cls;
    short slot = ENT(ent).slot;
    ent_free(ent);
    pool_free(cls, slot);
    return 1;
}
