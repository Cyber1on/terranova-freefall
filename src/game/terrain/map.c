/* map.c -- Terra Nova module M068 (0x293AF0-0x295180; map.c per the "[map.c:]" message):
 * loading a map (MapLoad), the map record (GameMap, g5_a.h), terrain height / shade at a point
 * and the building table.  Decompiled from __FF.EXE v1.09.
 *
 *   0x293AF0 map_pair_tab (data)        0x293BB0 MapTaskHook (frame hook 22)
 *   0x293C00 map_293C00 no file          0x293C0C map_293C0C install hook  0x293C2C map_293C2C
 *   0x293C34 map_293C34 layout           0x293D5C map_293D5C resource 0x5F
 *   0x293DCC map_293DCC release          0x293E20 map_293E20 open           0x293EC8 map_293EC8
 *   0x293EFC map_293EFC extract          0x293F50 MapLoad
 *   0x294148 map_294148 close            0x294190 map_294190 planet         0x2941A4 map_2941A4
 *   0x2941E8 map_2941E8 map object       0x29431C map_29431C variants       0x294468 map_294468
 *   0x29462C map_29462C mixed squares    0x294900 map_294900 max shade      0x294960 map_294960
 *   0x2949A0 map_2949A0                  0x2949C4 map_2949C4 height         0x294B18 map_294B18 shade
 *   0x294C78 map_294C78 clear bldgs      0x294CFC map_294CFC buildings      0x294ED4 map_294ED4 add
 *   0x295000 map_295000 bldg entity      0x2950A0 map_2950A0 height with buildings
 *
 * Map files: resource 0x50 the header (GameMap +0..+0x33), 0x51 / 0x52 two tables at the start
 * of the buffer, 0x55 the coarse grid, 0x56 the fine grid, 0x57 one byte, 0x5F an optional
 * table (f_malloc'ed).  MapLoad(name, ...) opens the file; MapLoad(NULL, ., 2) from the map
 * hook loads it in blocks (0x303 / 0x603 bytes) through ResExtractInBlocks.
 *
 * Quirks kept:
 *  - map_29462C walks coarse rows from x0 (not y0) to y0 + 0x80; map_294CFC marks the coarse
 *    cell at row (y >> 2) + x0, column (x >> 2) + x0 (x0 used for both).
 *  - The building table clear (map_294C78 / map_294CFC) skips entry 0.
 *  - map_29431C reseeds map_rnd from the first word of the map buffer on every call, so every
 *    row gets the same sequence.
 *  - map_2949C4 / map_294B18 accept x or y == 0x2000000 and then read row / column 0x201 of
 *    the fine grid (row pointer 0x201 is the f1052 field).
 *  - map_293C34 reports a too-big map or a missing buffer but carries on.
 *  - map_293DCC clears f1052 only when f1056 was set.
 *  - map_2941E8 ignores the result of interact_297400; an unterminated square list runs on
 *    into the slot / template locals after the descriptor (same frame layout here).
 */
#include "g5_a.h"
#include "g4_c2.h"     /* added at the G5-G8 merge */
#include "g5_c.h"     /* added at the G5-G8 merge */

#define HOOK_SLOT 22

/* ------------------------------------------------------------------ hook / file */

void MapTaskHook(void)
{
    if (map_state == 0 && map_file >= 0) {
        game_hook_mask &= ~(1UL << HOOK_SLOT);
        MapLoad(NULL, NULL, 2);
        game_hooks[HOOK_SLOT] = (GameHook)WATCOM_FN(MapTaskHook);
        game_hook_mask |= 1UL << HOOK_SLOT;
    }
}

void map_293C00(void)
{
    map_file = -1;
}

void map_293C0C(void)
{
    game_hooks[HOOK_SLOT] = (GameHook)WATCOM_FN(MapTaskHook);
    game_hook_mask |= 1UL << HOOK_SLOT;
}

void map_293C2C(void)
{
    game_hook_mask &= ~(1UL << HOOK_SLOT);
}

uchar *map_293C34(GameMap *m)
{
    long size = m->w * m->h * 3 + 0x89B;
    uchar *p;
    long i;
    if (m->f10 != 0 && m->f12 != 0)
        size += 0xC0C03;
    if (size > 0xF1AA1)
        DbgReportError(1, "Map is not 989857 bytes\n");
    p = game_big_buf;
    if (p == NULL)
        DbgReportError(1, "Could not allocate map memory\n");
    m->base = p;
    m->res52 = p + 0x88D;
    m->grid1 = p + 0x89B;
    p += 0x89B + m->w * m->h * 3;
    memset(m->grid1, 0, m->w * m->h * 3);
    {
        uchar *r = m->grid1;
        for (i = 0; i < m->h; i++) {
            m->rows1[i] = r;
            r += m->w * 3;
        }
    }
    if (m->f10 != 0 && m->f12 != 0) {
        m->grid2 = p;
        memset(p, 0, 0xC0C03);
        for (i = 0; i < 0x201; i++)
            m->rows2[i] = m->grid2 + i * 0x603;
    }
    m->f1052 = 0;
    m->f1056 = NULL;
    m->f10aa = 0;
    return m->grid2;
}

void map_293D5C(void)
{
    if (ResInUse(0x5F)) {
        GMAP->f1052 = ResSize(0x5F) >> 3;
        GMAP->f1056 = Malloc(ResSize(0x5F));
        ResExtract(0x5F, GMAP->f1056);
    } else {
        GMAP->f1052 = 0;
        GMAP->f1056 = NULL;
    }
}

void map_293DCC(GameMap *m)
{
    if (m == NULL)
        return;
    if (m->base)
        m->base = NULL;
    m->res52 = NULL;
    m->grid1 = NULL;
    m->grid2 = NULL;
    if (m->f1056) {
        Free(m->f1056);
        m->f1056 = NULL;
        m->f1052 = 0;
    }
}

long map_293E20(char *fname, GameMap *m)
{
    FnParts parts;
    map_file = ResOpenResFile(fname, 0, 0);
    {
        char *d = map_name, *q = fname;
        while ((*d++ = *q++) != 0)
            ;
    }
    if (map_file < 0)
        return -1;
    map_cur = m;
    map_state = 0;
    aihelp_2D3010();
    game_map_35BFB4 = (uchar *)map_cur;
    map_293DCC(map_cur);
    fn_split(&parts, fname);
    parts.drive = 0;
    parts.dir[0] = 0;
    fn_merge(GMAP->path, &parts);
    return 0;
}

long map_293EC8(void *buf, long n)
{
    memcpy(map_load_ptr, buf, n);
    map_load_ptr += n;
    return n;
}

void *map_293EFC(Id id, void *buf, long block)
{
    void *tmp;
    if (map_state != 2)
        return ResExtract(id, buf);
    map_res_id = id;
    map_load_ptr = buf;
    tmp = ffmem_2C4F40(block);
    ResExtractInBlocks(id, tmp, block, WATCOM_FN(map_293EC8));
    ffmem_2C4FE4(tmp);
    return buf;
}

static void map_close(void)
{
    if (map_file >= 0) {
        ResCloseFile(map_file);
        map_file = -1;
    }
    if (map_39A3EC != NULL && game_39A3F4) {
        ffmem_2C4FE4(map_39A3EC);
        map_39A3EC = NULL;
    }
}

long MapLoad(char *fname, GameMap *m, long state)
{
    long r;
    long i;
    if (fname != NULL) {
        r = map_293E20(fname, m);
        if (r != 0)
            return r;
    }
    map_state = state;
    if (game_map_35BFB4 == NULL)
        DbgReportError(1, "MapLoad: gMap is NULL\n");
    ResExtract(0x50, game_map_35BFB4);
    r = PlanetUse(GMAP->planet);
    if (r < 0) {
        map_close();
        return -4;
    }
    if (GMAP->grid1 == NULL)
        map_293C34(GMAP);
    ResExtract(0x51, GMAP->base);
    ResExtract(0x52, GMAP->res52);
    GMAP->f3e = &map_35C00C;
    GMAP->f10aa = 0;
    if (!(GMAP->flags & 1)) {
        map_293EFC(0x55, GMAP->grid1, 0x303);
        if (GMAP->grid2 != NULL)
            map_293EFC(0x56, GMAP->grid2, 0x603);
        if (ResInUse(0x57))
            ResExtract(0x57, &GMAP->f10aa);
        GMAP->flags |= 2;
    } else {
        r = aihelp_2D30D8();
        if (r != 0)
            return r;
    }
    if (GMAP->f34 & 1)
        map_294468();
    grid_2953F0();
    aihelp_2D301C();
    blast_2AC6D0();
    map_293D5C();
    map_close();
    map_29462C();
    map_294900();
    for (i = 0; i < 0x201; i++)
        map_29431C(1, i);
    map_state = 3;
    return 0;
}

void map_294148(void)
{
    map_close();
}

long map_294190(void)
{
    return PlanetUse(GMAP->planet);
}

long map_2941A4(char *name)
{
    long i;
    for (i = 0; map_terrain_names[i] != NULL; i++)
        if (strcmpi(name, map_terrain_names[i]) == 0)
            return i;
    return -1;
}

/* rec: +0 class, +1 type, +8 position (0x12 bytes), +0x1A class-0 data.
 * The original's frame is the descriptor (0x320 bytes) followed by the slot (short, +0x320)
 * and the template (ushort, +0x324) that tileobj_298EBC fills; the square loop does not look at
 * the result of interact_297400 and walks the descriptor's squares until (-1, -1), x >= 4 or
 * y >= 4 or tileobj_298E64 fails, so an unterminated list (interact_297400 failing early, e.g.
 * off-map positions) runs on into the layers and then into slot / template: kept by laying out
 * the same frame (fr).  Past +0x328 the original reads its saved registers (not reproduced). */
void map_2941E8(void *vrec)
{
    uchar *rec = vrec;
    struct {
        GridObjDesc d;          /* +0x000 */
        short slot, g322;       /* +0x320 */
        ushort tmpl, g326;      /* +0x324 */
    } fr;
    long i;
    if (!tileobj_298EBC(rec[0], &fr.tmpl, &fr.slot))
        return;
    tile_tmpls[fr.tmpl].type = rec[1];
    if (rec[0] != 0)
        return;
    if (!damage_27D6E8(game_pools[0].base + fr.slot * 9 + 6, rec[1], rec + 0x1A))
        return;
    memcpy(tile_tmpls[fr.tmpl].pos, rec + 8, 0x12);
    fr.d.ent = fr.tmpl;
    memcpy(fr.d.pos, rec + 8, 0x12);
    fr.d.z = 0;
    interact_297400(&fr.d, rec[0], rec[1]);
    for (i = 0;; i++) {
        GridKey *k = (GridKey *)((uchar *)&fr + 0x14 + i * 6);
        if (k->x == -1 && k->y == -1)
            break;
        if (k->x >= 4 || k->y >= 4)
            break;
        if (!tileobj_298E64(fr.tmpl, i * 6, rec[1], 4, *(GridXY *)k, *(long *)&k->e0))
            break;
    }
}

void map_29431C(short sel, short row)
{
    long n = sel ? 0x201 : 0x101;
    long end = n * (row + 1);
    long i;
    map_rnd.f_Seed(&map_rnd, *(ushort *)GMAP->base);
    for (i = end - n; i < end; i++) {
        uchar *g = sel ? GMAP->grid2 : GMAP->grid1;
        uchar t = g[i * 3] & 0x3F;
        uchar *cb = &terrain_cornerbits[t * 4];
        if ((ulong)cb[0] * 0x01010101 == *(ulong *)cb && ((1UL << cb[0]) & map_vary_mask)) {
            long r;
            g = sel ? GMAP->grid2 : GMAP->grid1;
            r = RndRange(&map_rnd, 0, 3);
            g[i * 3 + 1] = (g[i * 3 + 1] & 0xF3) | ((r & 3) << 2);
        }
    }
}

void map_294468(void)
{
    GameMap *m = GMAP;
    long j, i, a, b;
    for (j = m->y0; j < m->y1; j++) {
        for (i = m->x0; i < m->x1; i++) {
            uchar *fine = m->grid2 + (j - m->y0) * 0x180C + (i - m->x0) * 12;
            uchar *coarse = m->grid1 + j * m->w * 3 + i * 3;
            long ci = j * m->w + i;
            coarse[0] = fine[0];
            coarse[1] = fine[1];
            coarse[2] = fine[2];
            for (a = 0; a < 12; a += 3) {
                for (b = 0; b < 0x180C; b += 0x603) {
                    uchar t = m->grid2[(j - m->y0) * 0x180C + (i - m->x0) * 12 + a + b] & 0x3F;
                    if ((1UL << game_41DD58[t]) & path_41DD80) {
                        uchar *c = m->grid1 + ci * 3;
                        c[0] = (c[0] & 0xC0) | t;
                        goto next;
                    }
                }
            }
next:;
        }
    }
    game_map_35BFB4 = (uchar *)m;
}

void map_29462C(void)
{
    short pairs[24][4];
    uchar *c[4][4];
    long i, j, a, b, k;
    memcpy(pairs, map_pair_tab, sizeof(pairs));
    for (i = GMAP->x0; i < GMAP->x0 + 0x80; i++) {
        for (j = GMAP->x0; j < GMAP->y0 + 0x80; j++) {
            uchar *cell = GMAP->rows1[j] + i * 3;
            long slow = 0, bad = 0;
            *cell &= ~0x40;
            for (a = 0; a < 4; a++)
                for (b = 0; b < 4; b++)
                    c[a][b] = GMAP->rows2[(j - GMAP->y0) * 4 + b] + ((i - GMAP->x0) * 4 + a) * 3;
            for (k = 0; k < 24; k++) {
                long d = MAP_CELL_H(c[pairs[k][0]][pairs[k][1]]) - MAP_CELL_H(c[pairs[k][2]][pairs[k][3]]);
                if (d < 0)
                    d = -d;
                if (d > 0x24)
                    goto mixed;
            }
            for (a = 0; a < 4; a++)
                for (b = 0; b < 4; b++) {
                    ulong bit = 1UL << game_41DD58[c[a][b][0] & 0x3F];
                    if (bit & path_41DD80)
                        slow++;
                    else if (bit & terrain_bitmask)
                        bad++;
                }
            if ((slow != 0 && slow != 16) || (bad != 0 && bad != 16))
                goto mixed;
            continue;
mixed:
            *cell |= 0x40;
        }
    }
}

void map_294900(void)
{
    GameMap *m = GMAP;
    long best = 0;
    long c, r;
    for (c = 0; c < 0x600; c += 3)
        for (r = 0; r < 0x200; r++) {
            long s = (*(ushort *)(m->rows2[r] + c) >> 7) & 7;
            if (s > best) {
                best = s;
                if (s == 7)
                    goto done;
            }
        }
done:
    map_shade_max = best;
    game_map_35BFB4 = (uchar *)m;
}

void map_294960(GameMap *m)
{
    uchar *p = m->grid1;
    long i;
    for (i = 0; i < m->h; i++) {
        m->rows1[i] = p;
        p += m->w * 3;
    }
}

void map_2949A0(GameMap *m)
{
    uchar *p = m->grid2;
    long i;
    for (i = 0; i < 0x201; i++) {
        m->rows2[i] = p;
        p += 0x603;
    }
}

/* row pointer table: sel 1 fine (x, y in 0..0x2000000), 0 coarse */
static uchar **map_rows(long sel)
{
    return (uchar **)((uchar *)game_map_35BFB4 + 0x4A + sel * 0x804);
}

fix map_2949C4(fix x, fix y)
{
    long sel = 1;
    long xi, yi, fx, fy, h00, h01, h10, h11;
    uchar *r0, *r1;
    if (x < 0 || x > 0x2000000 || y < 0 || y > 0x2000000) {
        x = (x >> 2) + (GMAP->x0 << 16);
        y = (y >> 2) + (GMAP->y0 << 16);
        sel = 0;
    }
    xi = x >> 16;
    yi = y >> 16;
    r0 = map_rows(sel)[yi];
    r1 = map_rows(sel)[yi + 1];
    h00 = MAP_CELL_H(r0 + xi * 3);
    h10 = MAP_CELL_H(r1 + xi * 3);
    h01 = MAP_CELL_H(r0 + (xi + 1) * 3);
    h11 = MAP_CELL_H(r1 + (xi + 1) * 3);
    fx = x & 0xFFFF;
    fy = y & 0xFFFF;
    if ((xi + yi) % 2 == 0) {
        if (fx > fy)
            h10 = h11 + h00 - h01;
        else
            h01 = h11 + h00 - h10;
    } else {
        if (fx + fy >= 0x10000)
            h00 = h10 + h01 - h11;
    }
    return (fx * (h01 - h00) + (h00 << 16) + fy * (h10 - h00)) / 0x18;
}

#define MAP_CELL_S(c) ((*(ushort *)(c) >> 6) & 0xE)

fix map_294B18(fix x, fix y)
{
    long sel = 1;
    long xi, yi, fx, fy, a00, a01, a10, a11, r;
    uchar *r0, *r1;
    if (x < 0 || x > 0x2000000 || y < 0 || y > 0x2000000) {
        x = (x >> 2) + (GMAP->x0 << 16);
        y = (y >> 2) + (GMAP->y0 << 16);
        sel = 0;
    }
    xi = x >> 16;
    yi = y >> 16;
    fx = x & 0xFFFF;
    fy = y & 0xFFFF;
    r0 = map_rows(sel)[yi];
    r1 = map_rows(sel)[yi + 1];
    a00 = MAP_CELL_S(r0 + xi * 3);
    a10 = MAP_CELL_S(r1 + xi * 3);
    a01 = MAP_CELL_S(r0 + (xi + 1) * 3);
    a11 = MAP_CELL_S(r1 + (xi + 1) * 3);
    if ((yi + xi) % 2 == 0) {
        if (fx > fy)
            a10 = a11 + a00 - a01;
        else
            a01 = a11 + a00 - a10;
    } else {
        if (fy + fx >= 0x10000)
            a00 = a10 + a01 - a11;
    }
    r = fx * (a01 - a00) + (a00 << 16) + fy * (a10 - a00);
    if (game_35CADC >= 0x10)
        r = 0;
    return r >> 16;
}

/* ------------------------------------------------------------------ building table */

static void map_bldg_clear(void)
{
    long i;
    map_bldg_count = 0;
    for (i = 1; i < MAP_BLDGS; i++) {   /* entry 0 is not cleared (original loop) */
        map_bldgs[i].x = -1;
        map_bldgs[i].y = -1;
    }
}

void map_294C78(void)
{
    map_bldg_clear();
}

static ulong map_bldg_hash(ulong pt)
{
    return ((ulong)((long)(short)(pt >> 16) << 8) + (ulong)((long)(short)pt << 4)) % MAP_BLDGS;
}

static long map_bldg_free(ulong h)
{
    return map_bldgs[h].x == -1 && map_bldgs[h].y == map_bldgs[h].x;
}

static ulong map_bldg_find(ulong pt)
{
    ulong h = map_bldg_hash(pt);
    ulong n = 0;
    while (!map_bldg_free(h) && pt != *(ulong *)&map_bldgs[h]) {
        n++;
        h = (h + n) % MAP_BLDGS;
    }
    return h;
}

void map_294CFC(void)
{
    short cur, ent, sq, first;
    uchar top;
    map_bldg_clear();
    cur = (short)(*(ushort *)game_cls4[0] << 1) >> 1;
    top = *(ushort *)game_cls4[cur] >> 15;
    while (cur != 0) {
        if (!top) {
            ent = (short)(*(ushort *)game_cls4[cur] << 1) >> 1;
            if (game_entities[ent].active) {
                GameEntity *e = &game_entities[ent];
                if (e->type < 0x49 && (short)e->f05 > 0) {
                    first = e->f05;
                    sq = first;
                    do {
                        GridSquare *s = &grid_squares[sq];
                        ulong key = *(ulong *)&s->x;
                        short x = s->x, y = s->y;
                        map_294ED4(key, ent);
                        GMAP->rows1[(y >> 2) + GMAP->x0][(GMAP->x0 + (x >> 2)) * 3] |= 0x40;
                        sq = s->ent_next;
                    } while (sq != first);
                }
            }
        }
        cur = *(short *)(game_cls4[cur] + 2);
        top = *(ushort *)game_cls4[cur] >> 15;
    }
}

void map_294ED4(ulong pt, short ent)
{
    ulong h = map_bldg_find(pt);
    short ground;
    fix r;
    if (map_bldg_count == MAP_BLDGS) {
        DbgReportError(1, "[map.c:] BldgTable is Full (%d elements)", map_bldg_count);
        return;
    }
    ground = MAP_CELL_H((uchar *)TN_LOWMEM(GMAP->rows2[(short)(pt >> 16)]) + (short)pt * 3);
    r = interact_2986C0(interact_2973E8(game_entities[ent].cls, game_entities[ent].type));
    map_bldgs[h].ent = ent;
    *(ulong *)&map_bldgs[h] = pt;
    map_bldg_count++;
    map_bldgs[h].height = (short)(((long)((ulong)r * 24)) >> 16) + ground;
}

short map_295000(ulong pt)
{
    ulong h = map_bldg_find(pt);
    if (map_bldg_free(h))
        return 0;
    return map_bldgs[h].ent;
}

short map_2950A0(ulong pt)
{
    ulong h = map_bldg_find(pt);
    if (!map_bldg_free(h))
        return map_bldgs[h].height;
    return MAP_CELL_H((uchar *)TN_LOWMEM(GMAP->rows2[(short)(pt >> 16)]) + (short)pt * 3);
}
