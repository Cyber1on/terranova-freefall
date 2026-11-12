/* mapedge.c -- Freefall: the land around the map's 512 x 512 squares in the 3D view (G34, G35).
 *
 * The map has two grids (map.c): the fine one, 0x201 x 0x201 cells for the 512 x 512 squares
 * one plays on, and the coarse one, a cell every 4 squares, for the land around them (the far
 * land of the terrain caster, haze.c, and the heights past the edge, map_2949C4).  The game's
 * near terrain reads the fine grid only, with no check of the map's edge:
 *   - G34: the polygons of the squares at x or y = 512 (terrstrip_2AA2B4 lets them through) take
 *     the corners on their far side from one cell past the row, which is the next row's first
 *     cell (the map's other edge), or from past the grid: a wall one square thick, as high as the
 *     land at the other edge, stands along the east and the south edge (water squares in it
 *     look like waterfalls);
 *   - G35: past every edge the polygons stop and so do the caster's near rings (the fine walk
 *     takes no square off the map), so the land there is missing until the caster's far rings
 *     (the coarse grid) begin: that far land, pale with its haze, then fills the gap down to
 *     the near ground and looks like a see-through wall along the edge.
 * Here the near terrain reads a copy of the fine grid with PF_M squares of land around it, so the
 * game's walks (by cell pointers, PF_W cells a row) go on past the edge.  The land around is the
 * coarse grid's, at every square as map_2949C4 has it there (the coarse heights split into two
 * triangles a cell), the shade likewise, the kind the coarse cell's; the cells at the map's edge
 * are the fine grid's own, so the land goes on from them without a step (the coarse cells on the
 * east and south edges are not the fine grid's there).  The copy of the fine grid is made again
 * every frame (blast marks change it); the land around when the map changes.
 * The game itself (heights, physics, the AI) still reads the map as before.
 */
#include <string.h>
#include "g5_a.h"
#include "mapedge.h"

static unsigned char pf_buf[PF_W * PF_W * 3];
unsigned char *port_pf = pf_buf + ((long)PF_M * PF_W + PF_M) * 3;
static int pf_stale = 1;                /* a frame went by since the last copy */
static unsigned long pf_key;            /* the coarse grid and the edge the land was made from */

void port_pf_frame(void)
{
    pf_stale = 1;
}

static uchar *coarse(GameMap *m, long cx, long cy)
{
    if (cx < 0)
        cx = 0;
    else if (cx >= m->w)
        cx = m->w - 1;
    if (cy < 0)
        cy = 0;
    else if (cy >= m->h)
        cy = m->h - 1;
    return (uchar *)TN_LOWMEM(m->rows1[cy]) + cx * 3;
}

/* the cell at the lattice point (i, j) (square 4i, 4j): the fine grid's on the map and its edge,
 * else the coarse grid's */
static uchar *lattice(GameMap *m, long i, long j)
{
    if (i >= 0 && i <= 0x80 && j >= 0 && j <= 0x80)
        return (uchar *)TN_LOWMEM(m->rows2[j * 4]) + i * 12;
    return coarse(m, i + m->x0, j + m->y0);
}

#define CELL_S(c) ((*(ushort *)(c) >> 6) & 0xE)

/* the value (height or shade) at fractions fx, fy of the lattice square, as map_2949C4 */
static long tri(long v00, long v01, long v10, long v11, long fx, long fy, long odd)
{
    if (!odd) {
        if (fx > fy)
            v10 = v11 + v00 - v01;
        else
            v01 = v11 + v00 - v10;
    } else if (fx + fy >= 0x10000)
        v00 = v10 + v01 - v11;
    return (fx * (v01 - v00) + (v00 << 16) + fy * (v10 - v00)) >> 16;
}

static void land_cell(GameMap *m, long x, long y, uchar *out)
{
    long i = x >> 2, j = y >> 2, fx = (x & 3) << 14, fy = (y & 3) << 14;
    long odd = (i + m->x0 + j + m->y0) & 1, h, s, var;
    uchar *a = lattice(m, i, j), *b = lattice(m, i + 1, j), *c = lattice(m, i, j + 1),
          *d = lattice(m, i + 1, j + 1);
    uchar kind = *coarse(m, i + m->x0, j + m->y0) & 0x3F;
    h = tri(MAP_CELL_H(a), MAP_CELL_H(b), MAP_CELL_H(c), MAP_CELL_H(d), fx, fy, odd);
    s = (tri(CELL_S(a), CELL_S(b), CELL_S(c), CELL_S(d), fx, fy, odd) + 1) >> 1;
    s = s < 0 ? 0 : s > 7 ? 7 : s;
    var = ((ulong)(x * 73856093) ^ (ulong)(y * 19349663)) >> 7 & 3;
    out[0] = (uchar)(kind | (s & 1) << 7);
    out[1] = (uchar)((h << 4 & 0xF0) | var << 2 | s >> 1);
    out[2] = (uchar)(h >> 4);
}

static unsigned long grid_key(GameMap *m)
{
    unsigned long k = 2166136261u;
    long x, y;
#define MIX(v) (k = (k ^ (unsigned long)(v)) * 16777619u)
    MIX((unsigned long)m);
    MIX(m->w);
    MIX(m->h);
    MIX(m->x0);
    MIX(m->y0);
    for (y = 0; y < m->h; y++) {
        const uchar *r = (const uchar *)TN_LOWMEM(m->rows1[y]);
        for (x = 0; x < m->w * 3; x += 3)
            MIX(r[x] | r[x + 1] << 8 | r[x + 2] << 16);
    }
    for (x = 0; x <= 0x200; x++) {      /* the fine edge's heights and shades */
        MIX(*(ushort *)((uchar *)TN_LOWMEM(m->rows2[0]) + x * 3 + 1) ^ *(ushort *)((uchar *)TN_LOWMEM(m->rows2[0]) + x * 3) << 16);
        MIX(*(ushort *)((uchar *)TN_LOWMEM(m->rows2[0x200]) + x * 3 + 1) ^ *(ushort *)((uchar *)TN_LOWMEM(m->rows2[0x200]) + x * 3) << 16);
        MIX(*(ushort *)((uchar *)TN_LOWMEM(m->rows2[x]) + 1) ^ *(ushort *)((uchar *)TN_LOWMEM(m->rows2[x])) << 16);
        MIX(*(ushort *)((uchar *)TN_LOWMEM(m->rows2[x]) + 0x200 * 3 + 1) ^ *(ushort *)((uchar *)TN_LOWMEM(m->rows2[x]) + 0x200 * 3) << 16);
    }
#undef MIX
    return k;
}

void port_pf_sync(void)
{
    GameMap *m = GMAP;
    unsigned long key;
    long x, y;
    if (!pf_stale || m == NULL || m->w <= 0 || m->h <= 0)
        return;
    pf_stale = 0;
    key = grid_key(m);
    if (key != pf_key) {                /* another map: the land around it */
        pf_key = key;
        for (y = -PF_M; y <= 0x200 + PF_M; y++)
            for (x = -PF_M; x <= 0x200 + PF_M; x++) {
                if (y >= 0 && y <= 0x200 && x == 0)
                    x = 0x201;          /* (the map itself is copied below) */
                land_cell(m, x, y, PF_CELL(x, y));
            }
    }
    for (y = 0; y <= 0x200; y++)
        memcpy(PF_CELL(0, y), (uchar *)TN_LOWMEM(m->rows2[y]), 0x201 * 3);
}
