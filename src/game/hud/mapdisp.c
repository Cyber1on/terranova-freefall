/* mapdisp.c -- Terra Nova module M030 (0x2769D0-0x278CBF): the tactical map display of the HUD
 * (map pane and full-screen map).  Decompiled from __FF.EXE v1.09.
 *
 * A map display (MapDisp, g8_c.h; pool of 4 at 0x390260) keeps an off-screen bitmap of the
 * terrain as seen at its zoom level.  Scrolling moves the bitmap with memmove and renders only
 * the uncovered strips (mapdisp_2774C0); every frame mapdisp_276EF8 copies the bitmap to the
 * current canvas and draws blast rings, annotations, entity markers, member circles, squad
 * paths, near-list contacts (blanked at random while the sensors are damaged), nav markers,
 * member name tags and the nav point on top.
 *
 * Coordinates: world fix (map is 0x2000000 wide); a pixel is `scale` world units.  y is shifted
 * left by hud_35A544 before the scale (half-height views), so screen y = (wy - y) << sh / scale.
 *
 *   0x2769D0 data: colours of the nav markers {0x38, 0x3F, 0x7E, 0x84}, then the jump table of
 *            mapdisp_2769E8 (0x2769D4); 0x276A28 / 2E / 54 / 8C / 9B are its case labels
 *   0x2769E8 marker colour of an entity                 0x276ACC new map (pool slot)
 *   0x276BB4 free a map                                  0x276BC8 change zoom keeping the centre
 *   0x276C4C move to a position                          0x276C6C scroll to a position
 *   0x276CDC scroll by a pixel delta                     0x276EF8 draw everything
 *   0x277424 nav point cross                             0x2774C0 render terrain into a rect
 *   0x27782C / 277874 shift rows right / left            0x2778BC / 277930 shift rows down / up
 *   0x2779A0 clamp a centre to a top-left               0x277A48 / 277A78 clamp x / y (unused)
 *   0x277AB0 nav marker                                  0x277BAC entity marker / outline
 *   0x277F60 member circles (sensor ranges, weapon)      0x278114 aircraft circles
 *   0x278254 squad path                                  0x278488 annotations
 *   0x278688 member name tags                            0x2788C8 name tag layout
 *   0x2789D8 teleport the player to a map pixel (unused) 0x278A24 blast rings
 */
#include "g8_c.h"

#define HI (game_35A540 >> 1)
#define SH (hud_35A544)
#define FCOLOR(c) (grd_canvas->gc.fcolor = (c))
#define MENT(i) (game_entities[(short)(i)])

typedef void (*CanvasFn)();
#define CANVAS(i, ...) WATCOM_CALL((CanvasFn)grd_canvas_table[i], __VA_ARGS__)
#define C_POINT 10
#define C_BOX 18
#define C_CIRCLE 131

/* imul / shrd 16 */
static inline fix MUL(fix a, fix b)
{
    return (fix)(((long long)a * b) >> 16);
}

/* edx = a >> 16, eax = a << 16, idiv b (faults like the original; never optimised away) */
static inline fix DIV(fix a, fix b)
{
    long r, hi = a >> 16;

    __asm__ volatile("idivl %3" : "=a"(r), "+d"(hi) : "0"((long)((ulong)a << 16)), "rm"(b) : "cc");
    return r;
}

#define SX(m, wx) (MUL((wx) - (m)->x, (m)->inv) >> 16)
#define SY(m, wy) (MUL(((wy) - (m)->y) << SH, (m)->inv) >> 16)

static const uchar nav_colours[4] = { 0x38, 0x3F, 0x7E, 0x84 };     /* 0x2769D0 */

#ifndef TN_WATCOM_ABI
/* Freefall: the view cone under the markers (port/video/mapcone.c) */
void port_map_cone_draw(grs_canvas *cv, long sx, long sy, int sh, ushort view, ushort half);
/* Freefall: the map drawn again at the 3D resolution (port/video/sharp3d.c, port_s3d_map; its terrain
 * with port_map_pass set): while port_map_kx > 1 the coordinates are that picture's pixels and
 * the marks drawn a pixel at a time are drawn at its scale (port/video/mapsharp.c) */
extern int port_map_kx, port_map_ky, port_map_sh, port_map_pass, port_map_smooth;
void port_map_point(long x, long y);
void port_map_cross(long x, long y, int ay);
void port_map_box(long x0, long y0, long x1, long y1);
void port_map_navbox(long x, long y);
void port_map_line(fix x0, fix y0, fix x1, fix y1);
void port_map_iline(long x0, long y0, long x1, long y1);
void port_map_poly(long c, long n, void **vp);
void port_map_circle(long x, long y, long r);
void port_map_text(long font, char *s, long x, long y);
void port_map_bitmap(grs_bitmap *bm, long x, long y);
void port_map_disc(long x, long y, fix r);
#define SHARP_OR(sharp, orig) do { if (port_map_kx > 1) sharp; else orig; } while (0)
/* Freefall widescreen: the map drawn with e more columns on each side (port/video/sharp3d.c) keeps them
 * on the land too: its scrolling stops there, and a zoom level that would show past the land's
 * sides shows the land's whole width instead (the farthest zoom of the full map in 16:9) */
int port_map_extra(const void *map, int full);
#define WIDE_E(m) port_map_extra((m), (m) == (MapDisp *)game_camera_38FB20)
static fix wide_scale(MapDisp *m, long zoom)
{
    long e = WIDE_E(m);
    fix s = mapd_scales[zoom];
    if (e && (m->bm.w + 2 * e) * s > 0x2000000)
        s = 0x2000000 / (m->bm.w + 2 * e);
    return s;
}
/* zoom level zoom shows more than the next nearer one (not: both already the land's width) */
int port_map_zoom_ok(const void *map, long zoom)
{
    MapDisp *m = (MapDisp *)map;
    return zoom >= 7 || wide_scale(m, zoom) > wide_scale(m, zoom + 1);
}
#else
#define SHARP_OR(sharp, orig) orig
#endif

static void fill_type_1(void)
{
    grd_canvas->gc.fill_type = 1;
    grd_function_table = grd_function_fill_table + 390;
    grd_uline_fill_vector = grd_uline_fill_table + 9;
}

static void fill_type_0(void)
{
    grd_canvas->gc.fill_type = 0;
    grd_function_table = grd_function_fill_table;
    grd_uline_fill_vector = grd_uline_fill_table;
}

/* ------------------------------------------------------------------ colours */

uchar mapdisp_2769E8(long ent)
{
    GameEntity *e;
    schar m;

    if ((short)ent == targ_current)
        return 0x84;
    e = &MENT(ent);
    switch (e->cls) {
    case 0:
        return 0;
    case 1:
        m = (schar)mfddata_2665E8((short)ent);
        if (m == -1)
            return e->type == 0xD ? 0x6E : 0x44;
        return mfd_35B3ED[m];
    case 2:
        return e->type > 3 ? 0x44 : 0x22;
    case 3:
        if ((short)ent == game_396868)
            return 0x4B;
        if (e->type < 7)
            return 0x63;
        return e->type == 7 ? 0x50 : 0x6E;
    case 4:
        if (instsim_288260(e->type)) {
            if (e->type == 0x45 || e->type == 0x46 || e->type == 0x47)
                return 0x44;
            return 0x7B;
        }
        return 0x12;
    default:
        return 0x90;
    }
}

/* ------------------------------------------------------------------ create / move */

uchar *mapdisp_276ACC(grs_canvas *c, fix x, fix y, long zoom)
{
    MapDisp *m = NULL;
    long i;

    for (i = 0; i < 4; i++)
        if (!(mapd_pool[i].flags & 2)) {
            m = &mapd_pool[i];
            m->flags |= 2;
            break;
        }
    if (i == 4)
        return NULL;
#ifndef TN_WATCOM_ABI
    m->bm.w = c->bm.w;
    m->bm.h = c->bm.h;
    while (zoom < 7 && !port_map_zoom_ok(m, zoom))
        zoom++;
#endif
    m->zoom = (short)zoom;
    m->scale = mapd_scales[zoom];
#ifndef TN_WATCOM_ABI
    m->scale = wide_scale(m, zoom);
#endif
    m->inv = DIV(0x10000, m->scale);
    m->layers = 0;
    m->bm.type = 2;
    m->bm.flags = 0;
    m->bm.align = 0;
    m->canvas = c;
    m->flags = (m->flags | 1) & 0xF3;
    m->bm.w = c->bm.w;
    m->bm.h = c->bm.h;
    m->bm.row = m->bm.w;
    m->bm.bits = f_malloc((ushort)m->bm.row * (long)m->bm.h);
    mapdisp_2779A0(m, x, y, &m->x, &m->y);
    mapdisp_2774C0(m, NULL);
    return (uchar *)m;
}

void mapdisp_276BB4(uchar *map)
{
    MapDisp *m = (MapDisp *)map;

    f_free(m->bm.bits);
    m->flags &= ~2;
}

void mapdisp_276BC8(uchar *map, long zoom)
{
    MapDisp *m = (MapDisp *)map;
    fix cx = m->x + (m->bm.w / 2) * m->scale;
    fix cy = m->y + ((m->bm.h / 2) >> SH) * m->scale;

    m->zoom = (short)zoom;
    m->scale = mapd_scales[zoom];
#ifndef TN_WATCOM_ABI
    m->scale = wide_scale(m, zoom);
#endif
    m->inv = DIV(0x10000, m->scale);
    mapdisp_2779A0(m, cx, cy, &m->x, &m->y);
    mapdisp_2774C0(m, NULL);
}

void mapdisp_276C4C(uchar *map, fix x, fix y)
{
    MapDisp *m = (MapDisp *)map;

    mapdisp_2779A0(m, x, y, &m->x, &m->y);
    mapdisp_2774C0(m, NULL);
}

void mapdisp_276C6C(uchar *map, fix x, fix y)
{
    MapDisp *m = (MapDisp *)map;
    fix nx, ny;
    LGPoint d;

    mapdisp_2779A0(m, x, y, &nx, &ny);
    d.x = (short)(MUL(nx - m->x, m->inv) >> 16);
    d.y = (short)(MUL((ny - m->y) << SH, m->inv) >> 16);
    if (d.x || d.y)
        mapdisp_276CDC(map, d);
}

/* Scroll by d pixels (a step that would leave the map is dropped): move the bitmap, then render
 * the uncovered strips.  Quirk: in hi-res the strip of a vertical scroll is drawn over the whole
 * height. */
void mapdisp_276CDC(uchar *map, LGPoint d)
{
    MapDisp *m = (MapDisp *)map;
    LGRect r;
    long w = m->bm.w, h, v;
    fix p;

    p = d.x * m->scale + m->x;
#ifndef TN_WATCOM_ABI
    {
        long e = WIDE_E(m);
        if (e) {
            p -= e * m->scale;
            w += 2 * e;
        }
    }
#endif
    if (p < 0)
        d.x = 0;
    else if (w * m->scale + p > 0x2000000)
        d.x = 0;
    w = m->bm.w;
    m->x += d.x * m->scale;
    if (d.x < 0) {
        v = -d.x;
        if (v < m->bm.w)
            mapdisp_27782C(m, v);
        r.ul.x = 0;
        r.ul.y = 0;
        r.lr.x = (short)(v < m->bm.w ? v : m->bm.w);
        r.lr.y = m->bm.h;
        mapdisp_2774C0(m, &r);
    } else if (d.x > 0) {
        if (d.x < m->bm.w)
            mapdisp_277874(m, d.x);
        v = m->bm.w - d.x;
        r.ul.x = (short)(v < 0 ? 0 : v);
        r.ul.y = 0;
        r.lr.x = m->bm.w;
        r.lr.y = m->bm.h;
        mapdisp_2774C0(m, &r);
    }
    p = (d.y >> SH) * m->scale + m->y;
    if (p < 0)
        d.y = 0;
    else {
        h = m->bm.h;
        if (p + (h >> SH) * m->scale > 0x2000000)
            d.y = 0;
    }
    m->y += (d.y >> SH) * m->scale;
    if (d.y < 0) {
        v = -d.y;
        if (v < m->bm.h)
            mapdisp_2778BC(m, v);
        r.ul.x = 0;
        r.ul.y = 0;
        r.lr.x = m->bm.w;
        r.lr.y = (short)(v < m->bm.h ? v : m->bm.h);
        if (HI)
            r.lr.y = m->bm.h;
        mapdisp_2774C0(m, &r);
    } else if (d.y > 0) {
        if (d.y < m->bm.h)
            mapdisp_277930(m, d.y);
        r.ul.x = 0;
        v = m->bm.h - d.y;
        r.ul.y = (short)(v < 0 ? 0 : v);
        if (HI)
            r.ul.y = 0;
        r.lr.x = m->bm.w;
        r.lr.y = m->bm.h;
        mapdisp_2774C0(m, &r);
    }
}

/* ------------------------------------------------------------------ draw */

void mapdisp_276EF8(uchar *map, long b)
{
    MapDisp *m = (MapDisp *)map;
    NearEntry buf[0x80];
    NearList *lists[8];
    NearList all;
    uchar d1 = 0x64, d2 = 0x64, draw, vis, *sq;
    short n, i, v;
    long k, cnt, j;
    ObjRingEntry *e;

    if (game_35ADE0) {
        fill_type_1();
        grd_canvas->gc.fill_parm = (long)planet_tab41;
    } else
        fill_type_0();
    CANVAS(DRAW_DEVICE_UBITMAP + 2 * m->bm.type, (long)&m->bm, 0L, 0L);
    for (n = (short)timers_279F94(); n > 0; n--) {
        e = timers_27A014(n);
        if (e)
            mapdisp_278A24(e, m);
    }
    timers_279FAC();
#ifndef TN_WATCOM_ABI
    if (game_player_entity)
        port_map_cone_draw(grd_canvas, SX(m, MENT(game_player_entity).x),
                           SY(m, MENT(game_player_entity).y), SH,
                           (ushort)(MENT(game_player_entity).heading + hud_441355),
                           *(ushort *)(game_camera + 0x18));
#endif
    if (m->layers & 0x18)
        mapdisp_278488(m);
    if (mapd_show_ents)
        for (i = game_entities[0].f05; i; i = MENT(i).f07)
            if (MENT(i).cls >= 1 && MENT(i).cls <= 4)
                mapdisp_277BAC(m, i, (short)b, 1);
    if (m->layers & 6) {
        mapdisp_277F60(m, game_player_entity);
        aihelp_2D3298(0);
        while ((v = (short)aihelp_2D3298(1)) != 0)
            mapdisp_277F60(m, v);
        if (game_396868)
            mapdisp_278114(m, game_396868);
        if (mapd_show_ents)
            while ((v = (short)aihelp_2D313C(1)) != 0)
                mapdisp_277F60(m, v);
    }
    if (m->layers & 0x20)
        for (k = 0; k < 0x20; k++) {
            sq = *(uchar **)((uchar *)&game_squads[k] + 0x95);
            if (!sq || !sq[0] || mapd_390308 == k)
                continue;
            mapdisp_278254(m, sq,
                           ((uchar *)&game_squads[k])[7] == 1 ? (uchar)(k * 2 + 0x1D) : 0x44);
        }
    if (m->flags & 4) {
        draw = 1;
        if ((short)b)
            d1 = damage_27DF14((short)b, 1);
        if (d1 < 0x50 && !mapd_jammed) {
            uchar t = (uchar)(0x50 - (d1 < 0x32 ? 0x32 : d1));

            if (RndRange(&game_rnd_35B704, 0, 0x64) < t) {
                mapd_jammed = 1;
                draw = 0;
            }
        } else
            mapd_jammed = 0;
        if (draw) {
            NearList *l;

            aihelp_2D3298(0);
            vis = 1;
            cnt = 0;
            while ((v = (short)aihelp_2D3298(1)) != 0) {
                l = damage_27D158(v);
                if (l)
                    lists[cnt++] = l;
            }
            if (game_396868) {
                l = damage_27D158(game_396868);
                if (l)
                    lists[cnt++] = l;
            }
            damage_27D140(&all, 0, 0, 0x80, buf);
            k = damage_27D368(&all, lists, cnt);
            if ((short)b)
                d2 = damage_27DF14((short)b, 2);
            if (d2 < 0x2D) {
                uchar t = (uchar)((0x2D - d2) * 2);

                vis = RndRange(&game_rnd_35B704, 0, 0x64) >= t;
            }
            for (j = 0; j < k; j++)
                mapdisp_277BAC(m, *(short *)((uchar *)all.buf + j * 6), (short)b, vis);
            if (strike_entity && MENT(strike_entity).cls == 3)
                mapdisp_277BAC(m, strike_entity, (short)b, vis);
            if (targ_current)
                mapdisp_277BAC(m, targ_current, (short)b, vis);
            mapdisp_277BAC(m, game_player_entity, (short)b, vis);
        }
    }
    for (k = 0; k < 4; k++)
        for (j = 0; j < 10; j++)
            if ((schar)mapd_nav[k][j * 12] >= 0)
                mapdisp_277AB0(m, &mapd_nav[k][j * 12]);
    if (m->layers & 1)
        mapdisp_278688(m);
    if (ffpipe_38FFF0 && ffpipe_38FFF4)
        mapdisp_277424(m);
}

/* dword store over a short global (the original writes 4 bytes) */
static inline void put32(void *p, long v)
{
    __asm__ volatile("movl %1, (%0)" : : "r"(p), "r"(v) : "memory");
}

/* Nav point: a dot (zoom < 4) or a small cross; its position is kept in hud_390300 / 390304
 * (written as dwords). */
void mapdisp_277424(MapDisp *m)
{
    long sx, sy;

    FCOLOR(0x9C);
    sx = SX(m, ffpipe_38FFF0);
    sy = SY(m, ffpipe_38FFF4);
    put32(&hud_390300, sx);
    put32(&hud_390304, sy);
    if (m->zoom < 4)
        SHARP_OR(port_map_point(sx, sy), CANVAS(C_POINT, (long)(short)sx, (long)(short)sy));
    else {
#ifndef TN_WATCOM_ABI
        if (port_map_kx > 1) {
            port_map_cross(sx, sy, 1);
            return;
        }
#endif
        gen_hline((short)(sx - 1), (short)sy, (short)(sx + 1));
        gen_vline((short)sx, (short)(sy - 1), (short)(sy + 1));
    }
}

#ifndef TN_WATCOM_ABI
/* Freefall, the sharp map's terrain (port_map_pass): mapdisp_2774C0's pixels a run at a time
 * (each cell's colour is the same all over it), black beyond the map's edges (the wide map in
 * widescreen reaches past them) */
static void sharp_terrain(MapDisp *m, long x0, long x1, long y0, long y1, grs_bitmap *ov, fix ox0,
                          fix oy0, fix ox1, fix oy1)
{
    uchar *line = m->bm.bits + (long)(ushort)m->bm.row * y0 + x0;
    fix wy = m->y + m->scale * (y0 >> SH), wx, nb;
    long yy, n;

    for (yy = y0; yy < y1; yy++, line += (ushort)m->bm.row, wy += m->scale >> SH) {
        uchar *p = line, *end = line + (x1 - x0), *cell, *shade, c, s;
        long d, t;

        wx = x0 * m->scale + m->x;
        if (wy < 0 || wy >= 0x2000000) {
            memset(p, 0, end - p);
            continue;
        }
        while (p < end) {
            if (wx < 0) {
                c = 0;
                nb = 0;
            } else if (wx >= 0x2000000) {
                c = 0;
                nb = 0x7FFFFFFF;
            } else {
                cell = *(uchar **)(game_map_35BFB4 + 0x84E + (wy >> 16) * 4) + (wx >> 16) * 3;
                shade = mapd_shade;
                nb = ((wx >> 16) + 1) << 16;
                if (wx >= ox0 && wx <= ox1 && wy >= oy0 && wy <= oy1) {
                    c = ov->bits[(ushort)ov->row * ((wy - oy0) >> 16) + ((wx - ox0) >> 16)];
                    if (game_359708 == 8)
                        shade = mapd_shade8;
                } else {
                    t = game_41DD58[cell[0] & 0x3F] << 8;
                    c = planet_res47[t + (((short)(*(short *)(cell + 1) >> 4) + 0x800) >> 4)];
                }
                if (wx <= ox1 && ox1 < nb - 1 && wy >= oy0 && wy <= oy1 && wx >= ox0 - 0x10000)
                    nb = ox1 + 1;       /* the overlay's last column: only its first pixel */
                d = (((long)(short)(*(short *)(cell + 1) >> 4) - (short)(*(short *)(cell + 4) >> 4)) >> 1) + 0x20;
                if (d < 0)
                    d = 0;
                else if (d > 0x3F)
                    d = 0x3F;
                s = shade[d];
                if (m->flags & 8)
                    c = *(planet_light + ((0xF - (long)s) << 9) + c);
                else
                    c = planet_tab36[((long)s << 8) + c];
            }
            n = nb == 0x7FFFFFFF ? end - p : (long)(((long long)nb - wx + m->scale - 1) / m->scale);
            if (n < 1)
                n = 1;
            if (n > end - p)
                n = end - p;
            memset(p, c, n);
            p += n;
            wx += n * m->scale;
        }
    }
}
#endif

#ifndef TN_WATCOM_ABI
/* Freefall "Map smoothing" On (port_map_smooth): each square's values (its ground
 * colour's row, height and light, as mapdisp_2774C0 finds them) taken at the square's middle and
 * blended in between.  A pixel gets the kind of ground (ramp row or overlay colour) that has the
 * most weight among the four squares around it, so the edges between kinds run smooth instead of
 * in steps; the height from those squares of that kind picks the colour in the ramp; the light
 * from all four, rounded (flat colours, smooth outlines). */
typedef struct {
    long key;                           /* ramp row << 8, or 0x10000 | the overlay's colour */
    long h;                             /* height >> 4 */
    long s;                             /* light level (the shade table's) */
} MapSq;

static void map_sq(long i, long j, grs_bitmap *ov, fix ox0, fix oy0, fix ox1, fix oy1, MapSq *q)
{
    uchar *row = *(uchar **)(game_map_35BFB4 + 0x84E + j * 4), *cell = row + i * 3;
    const uchar *shade = mapd_shade;
    long h = (short)(*(short *)(cell + 1) >> 4), hn, d;

    hn = i < 511 ? (short)(*(short *)(cell + 4) >> 4) : h;
    if ((i << 16) >= ox0 && (i << 16) <= ox1 && (j << 16) >= oy0 && (j << 16) <= oy1) {
        q->key = 0x10000 | ov->bits[(ushort)ov->row * ((j << 16) - oy0 >> 16) + ((i << 16) - ox0 >> 16)];
        if (game_359708 == 8)
            shade = mapd_shade8;
    } else
        q->key = (long)game_41DD58[cell[0] & 0x3F] << 8;
    q->h = h;
    d = ((h - hn) >> 1) + 0x20;
    q->s = shade[d < 0 ? 0 : d > 0x3F ? 0x3F : d];
}

static void smooth_terrain(MapDisp *m, long x0, long x1, long y0, long y1, grs_bitmap *ov, fix ox0,
                           fix oy0, fix ox1, fix oy1)
{
    static MapSq sq[2][514];
    static long hv[514], sv[514], kv[514];  /* a row's blend of the two square rows (x 256) */
    const uchar *lt[16];
    long yy, xx, ja = -1, ia, ib, n;
    fix wy = m->y + m->scale * (y0 >> SH), wx0 = x0 * m->scale + m->x, wx;
    uchar *line = m->bm.bits + (long)(ushort)m->bm.row * y0 + x0;

    for (xx = 0; xx < 16; xx++)         /* the light levels' tables (as far as they go) */
        lt[xx] = (m->flags & 8) ? planet_light + ((0xF - (xx < 4 ? 4 : xx)) << 9)
                                : planet_tab36 + ((xx > 7 ? 7 : xx) << 8);
    /* the squares under a row: from the first pixel's to the last's */
    ia = (wx0 - 0x8000) >> 16;
    ib = ((wx0 + (x1 - x0) * m->scale) >> 16) + 1;
    if (ia < 0)
        ia = 0;
    if (ib > 511)
        ib = 511;
    n = ib - ia + 1;
    if (n < 1) {                        /* all beyond the land's sides */
        for (yy = y0; yy < y1; yy++, line += (ushort)m->bm.row)
            memset(line, 0, x1 - x0);
        return;
    }
    for (yy = y0; yy < y1; yy++, line += (ushort)m->bm.row, wy += m->scale >> SH) {
        uchar *p = line;
        long v = wy - 0x8000, j = v >> 16, fy = (v >> 8) & 0xFF, i;

        if (wy < 0 || wy >= 0x2000000) {
            memset(p, 0, x1 - x0);
            continue;
        }
        if (j < 0)
            j = 0, fy = 0;
        if (j >= 511)
            j = 511, fy = 0;
        if (j != ja) {
            for (i = 0; i < n; i++) {
                map_sq(ia + i, j, ov, ox0, oy0, ox1, oy1, &sq[0][i]);
                map_sq(ia + i, j < 511 ? j + 1 : j, ov, ox0, oy0, ox1, oy1, &sq[1][i]);
            }
            ja = j;
        }
        for (i = 0; i < n; i++) {
            hv[i] = (256 - fy) * sq[0][i].h + fy * sq[1][i].h;
            sv[i] = (256 - fy) * sq[0][i].s + fy * sq[1][i].s;
            kv[i] = sq[0][i].key == sq[1][i].key ? sq[0][i].key : -1;
        }
        wx = wx0;
        for (xx = x0; xx < x1; xx++, wx += m->scale) {
            long u, fx, i1, k, key, h, s, c;

            if (wx < 0 || wx >= 0x2000000) {
                *p++ = 0;
                continue;
            }
            u = wx - 0x8000;
            i = (u >> 16) - ia;
            fx = (u >> 8) & 0xFF;
            if (u < 0)
                i = 0, fx = 0;
            if (i >= n - 1)
                i = n - 1, fx = 0;
            i1 = i + (i < n - 1);
            key = kv[i];
            if (key >= 0 && key == kv[i1]) {        /* one kind of ground all round */
                h = (hv[i] * (256 - fx) + hv[i1] * fx + 0x8000) >> 16;
                s = sv[i] * (256 - fx) + sv[i1] * fx;
            } else {
                const MapSq *q[4];
                long w[4], best = -1, hw = 0, kb = 0;
                q[0] = &sq[0][i];
                q[1] = &sq[0][i1];
                q[2] = &sq[1][i];
                q[3] = &sq[1][i1];
                w[0] = (256 - fx) * (256 - fy);
                w[1] = fx * (256 - fy);
                w[2] = (256 - fx) * fy;
                w[3] = fx * fy;
                for (k = 0; k < 4; k++) {           /* the kind with the most weight */
                    long b, sum = 0;
                    for (b = 0; b < 4; b++)
                        if (q[b]->key == q[k]->key)
                            sum += w[b];
                    if (sum > best)
                        best = sum, kb = k;
                }
                key = q[kb]->key;
                s = 0;
                for (k = 0; k < 4; k++) {
                    if (q[k]->key == key)
                        hw += w[k] * q[k]->h;
                    s += w[k] * q[k]->s;
                }
                h = hw >= 0 ? (hw + (best >> 1)) / best : -((-hw + (best >> 1)) / best);
            }
            c = key & 0x10000 ? key & 0xFF : planet_res47[key + (((h + 0x800) >> 4) & 0xFF)];
            s = (s + 0x8000) >> 16;
            *p++ = lt[s > 15 ? 15 : s][c];
        }
    }
}
#endif

/* Render the terrain into rect r of the bitmap (NULL: all).  Mission 0x1D has no terrain (black).
 * Resource 0x18B0000 | mission holds the top-left cell of an overlay bitmap (0x18C + mission)
 * drawn instead of the ground colour (negative x: none).  Each pixel: ground colour of the cell,
 * shaded by the height step to the next row through mapd_shade (mapd_shade8 over the overlay of
 * mission 8), lit (flags 8) through planet_light or else through planet_tab36. */
void mapdisp_2774C0(MapDisp *m, LGRect *r)
{
    long lim, x0, x1, y0, y1, w, yy, ry, rowi;
    fix ox0 = 0, oy0 = 0, ox1 = 0, oy1 = 0, wx, wy;
    grs_bitmap *ov = NULL;
    uchar *line, *p, *end, *cell, *shade, c, s;
    long t, d;

    if (!m)
        return;
    lim = *(long *)RefGet(0x18B0000 | game_359708);
    if ((short)lim >= 0) {
        ov = RefGet((0x18C + game_359708) << 16);
        ov->bits = (uchar *)ov + 0x1C;
        ox0 = (fix)(short)lim << 16;
        oy0 = (fix)(short)(lim >> 16) << 16;
        ox1 = ox0 + ((ov->w - 1) << 16);
        oy1 = oy0 + ((ov->h - 1) << 16);
    } else
        ox0 = 0x4000000;
    if (!r) {
        x0 = 0;
        x1 = m->bm.w;
        y0 = 0;
        y1 = m->bm.h;
    } else {
        x0 = r->ul.x;
        x1 = r->lr.x;
        y0 = r->ul.y;
        y1 = r->lr.y;
    }
    w = x1 - x0;
#ifndef TN_WATCOM_ABI
    if (port_map_pass && game_359708 != 0x1D) {
        if (port_map_smooth)
            smooth_terrain(m, x0, x1, y0, y1, ov, ox0, oy0, ox1, oy1);
        else
            sharp_terrain(m, x0, x1, y0, y1, ov, ox0, oy0, ox1, oy1);
        return;
    }
#endif
    if (game_359708 == 0x1D) {
        p = (ushort)m->bm.row * y0 + m->bm.bits + x0;
        for (yy = y0; yy < y1; yy++, p += (ushort)m->bm.row)
            for (end = p + w, line = p; line < end; line++)
                *line = 0;
        return;
    }
    line = (ushort)m->bm.row * y0 + m->bm.bits + x0;
    wy = m->y + m->scale * (y0 >> SH);
    for (yy = y0; yy < y1; yy++) {
        wx = x0 * m->scale + m->x;
        p = line;
        end = line + w;
        if (p < end) {
            ry = (wy - oy0) >> 16;
            rowi = (wy >> 16) * 4;
            do {
                cell = *(uchar **)(game_map_35BFB4 + 0x84E + rowi) + (wx >> 16) * 3;
                shade = mapd_shade;
                if (wx >= ox0 && wx <= ox1 && wy >= oy0 && wy <= oy1) {
                    c = ov->bits[(ushort)ov->row * ry + ((wx - ox0) >> 16)];
                    if (game_359708 == 8)
                        shade = mapd_shade8;
                } else {
                    t = game_41DD58[cell[0] & 0x3F] << 8;
                    c = planet_res47[t + (((short)(*(short *)(cell + 1) >> 4) + 0x800) >> 4)];
                }
                d = (((long)(short)(*(short *)(cell + 1) >> 4) - (short)(*(short *)(cell + 4) >> 4)) >> 1) + 0x20;
                if (d < 0)
                    d = 0;
                else if (d > 0x3F)
                    d = 0x3F;
                s = shade[d];
                if (m->flags & 8)
                    c = *(planet_light + ((0xF - (long)s) << 9) + c);
                else
                    c = planet_tab36[((long)s << 8) + c];
                *p++ = c;
                wx += m->scale;
            } while (p < end);
        }
        line += (ushort)m->bm.row;
        wy += m->scale >> SH;
    }
}

/* row shifts (memmove within each row; row copies forward) */
void mapdisp_27782C(MapDisp *m, long n)
{
    uchar *line = m->bm.bits;
    long i, k = m->bm.w - n;

    for (i = 0; i < m->bm.h; i++, line += (ushort)m->bm.row)
        memmove(line + n, line, k);
}

void mapdisp_277874(MapDisp *m, long n)
{
    uchar *line = m->bm.bits;
    long i, k = m->bm.w - n;

    for (i = 0; i < m->bm.h; i++, line += (ushort)m->bm.row)
        memmove(line, line + n, k);
}

static void copy_row(uchar *dst, uchar *src, long n)
{
    while (n-- > 0)
        *dst++ = *src++;
}

void mapdisp_2778BC(MapDisp *m, long n)
{
    long i = m->bm.h - 1, row = (ushort)m->bm.row;
    uchar *dst = m->bm.bits + i * row, *src = dst - row * n;

    for (; i >= n; i--, src -= row, dst -= row)
        copy_row(dst, src, m->bm.w);
}

void mapdisp_277930(MapDisp *m, long n)
{
    long i, row = (ushort)m->bm.row;
    uchar *dst = m->bm.bits, *src = dst + row * n;

    for (i = 0; m->bm.h - n > i; i++, src += row, dst += row)
        copy_row(dst, src, m->bm.w);
}

/* top-left for centre (x, y), kept inside the map */
void mapdisp_2779A0(MapDisp *m, fix x, fix y, fix *ox, fix *oy)
{
    fix t;

    x -= (m->bm.w / 2) * m->scale;
    y -= ((m->bm.h / 2) >> SH) * m->scale;
    t = m->bm.w * m->scale;
#ifndef TN_WATCOM_ABI
    {
        fix ew = WIDE_E(m) * m->scale;
        if (ew && t + 2 * ew > 0x2000000)
            *ox = (0x2000000 - t) / 2;          /* wider than the land: its middle */
        else if (ew && x - ew < 0)
            *ox = ew;
        else if (ew && x + t + ew > 0x2000000)
            *ox = 0x2000000 - t - ew;
        else if (x < 0)
            *ox = 0;
        else if (x + t > 0x2000000)
            *ox = 0x2000000 - t;
        else
            *ox = x;
    }
#else
    if (x < 0)
        *ox = 0;
    else if (x + t > 0x2000000)
        *ox = 0x2000000 - t;
    else
        *ox = x;
#endif
    t = (m->bm.h >> SH) * m->scale;
    if (y < 0)
        *oy = 0;
    else if (y + t > 0x2000000)
        *oy = 0x2000000 - t;
    else
        *oy = y;
}

fix mapdisp_277A48(MapDisp *m, fix x)
{
    fix t;

    if (x < 0)
        return 0;
    t = m->scale * m->bm.w;
    if (x + t > 0x2000000)
        return 0x2000000 - t;
    return x;
}

fix mapdisp_277A78(MapDisp *m, fix y)
{
    fix t;

    if (y < 0)
        return 0;
    t = m->scale * (m->bm.h >> SH);
    if (y + t > 0x2000000)
        return 0x2000000 - t;
    return y;
}

/* nav marker rec: schar colour, short ent (0: fix x, y at +3 / +7) */
void mapdisp_277AB0(MapDisp *m, uchar *rec)
{
    uchar col[4];
    short sx, sy;
    short ent = *(short *)(rec + 1);

    memcpy(col, nav_colours, 4);
    if (ent == 0) {
        sx = (short)SX(m, *(fix *)(rec + 3));
        sy = (short)SY(m, *(fix *)(rec + 7));
    } else {
        sx = (short)SX(m, MENT(ent).x);
        sy = (short)SY(m, MENT(ent).y);
    }
    FCOLOR(col[(schar)rec[0]]);
    if (m->zoom < 4)
        SHARP_OR(port_map_point(sx, sy), CANVAS(C_POINT, (long)sx, (long)sy));
    else
        SHARP_OR(port_map_navbox(sx, sy),
                 CANVAS(C_BOX, (long)(short)(sx - 2), (long)(short)(sy - 2), (long)(short)(sx + 1),
                        (long)(short)(sy + 1)));
}

/* Entity marker.  Vehicles (class 4) that are not instruments are drawn as their rotated
 * collision outline; everything else as a dot / cross in its colour (0x90 when !vis; flickering
 * +2 while the sensors (system 1 of sys_ent) are below 90%), and the player with a heading
 * tick. */
void mapdisp_277BAC(MapDisp *m, long ent, long sys_ent, uchar vis)
{
    uchar dmg = 0x64, col;
    long sx, sy;
    GameEntity *e;

    if ((short)sys_ent)
        dmg = damage_27DF14((short)sys_ent, 1);
    e = &MENT(ent);
    sx = SX(m, e->x);
    sy = SY(m, e->y);
    if (e->cls == 4 && !instsim_288260(e->type)) {
        struct { fix x, y; } v[26];
        grs_vertex *vp[26];
        ShapeItem *s = interact_2973E8(4, e->type);
        fix sn, cs, rx, ry;
        long i;

        fix_sincos(e->heading, &sn, &cs);
        for (i = 0; i < s->npts; i++) {
            vp[i] = (grs_vertex *)&v[i];
            rx = MUL(s->pt[i][0], cs) - MUL(s->pt[i][1], sn);
            ry = MUL(s->pt[i][0], sn) + MUL(s->pt[i][1], cs);
            v[i].x = MUL(MENT(ent).x + rx - m->x, m->inv);
            v[i].y = MUL((MENT(ent).y + ry - m->y) << SH, m->inv);
        }
        SHARP_OR(port_map_poly(0x12, s->npts, (void **)vp), gr_wire_poly(0x12, s->npts, vp));
        return;
    }
    col = vis ? mapdisp_2769E8((short)ent) : 0x90;
    if (dmg < 0x5A && (short)ent != game_player_entity && RndRange(&game_rnd_35B704, 0, 1))
        col += 2;
    FCOLOR(col);
#ifndef TN_WATCOM_ABI
    if (port_map_kx > 1) {
        if (m->zoom < 4)
            port_map_point(sx, sy);
        else
            port_map_cross(sx, sy, HI ? 2 : 1);
    } else
#endif
    if (m->zoom < 4)
        CANVAS(C_POINT, (long)(short)sx, (long)(short)sy);
    else if (HI) {
        gen_hline((short)(sx - 1), (short)sy, (short)(sx + 1));
        gen_vline((short)sx, (short)(sy - 2), (short)(sy + 2));
    } else {
        gen_hline((short)(sx - 1), (short)sy, (short)(sx + 1));
        gen_vline((short)sx, (short)(sy - 1), (short)(sy + 1));
    }
    if ((short)ent == game_player_entity) {
        fix sn, cs;

        fix_fastsincos(MENT(ent).heading, &sn, &cs);
        if (HI)
            sn += sn;
#ifndef TN_WATCOM_ABI
        if (port_map_kx > 1) {          /* the tick at the picture's scale */
            port_map_line((sx << 16) + 0x8000, (sy << 16) + 0x8000,
                          (sx << 16) + 0x8000 + cs * 6 * port_map_kx,
                          (sy << 16) + 0x8000 + sn * 6 * port_map_ky);
            return;
        }
#endif
        gr_iline((short)sx, (short)sy, (short)(((cs * 6 + 0x8000) >> 16) + sx),
                 (short)(((sn * 6 + 0x8000) >> 16) + sy));
    }
}

/* member circles: sensor ranges (PBA +0xB2 / +0xB6) and the selected weapon's range */
void mapdisp_277F60(MapDisp *m, long ent)
{
    GameEntity *e = &MENT(ent);
    uchar *p = (uchar *)&game_pba[e->slot] + 6, *q;
    long col = mapdisp_2769E8((short)ent);
    short sx, sy, r;
    fix k;

    FCOLOR(col);
    sx = (short)SX(m, e->x);
    sy = (short)SY(m, e->y);
    if (m->layers & 2) {
        k = MUL(0x60000, 0x10F5B);
        r = (short)(MUL(DIV(*(fix *)(p + 0xAC), k), m->inv) >> 16);
        SHARP_OR(port_map_circle(sx, sy, r), CANVAS(C_CIRCLE, (long)sx, (long)sy, (long)r));
        FCOLOR(col + 2);
        k = MUL(0x60000, 0x10F5B);
        r = (short)(MUL(DIV(*(fix *)(p + 0xB0), k), m->inv) >> 16);
        SHARP_OR(port_map_circle(sx, sy, r), CANVAS(C_CIRCLE, (long)sx, (long)sy, (long)r));
    }
    if (m->layers & 4) {
        q = p + 0x3F + (schar)p[0xC8] * 15;
        if (*q) {
            r = (short)(MUL((fix)*(ushort *)((uchar *)&ord_table[*q] + 0xE) << 16, m->inv) >> 16);
            SHARP_OR(port_map_circle(sx, sy, r), CANVAS(C_CIRCLE, (long)sx, (long)sy, (long)r));
        }
    }
}

/* aircraft circles (cls3 record +0x62 / +0x66; the second one y-scaled) */
void mapdisp_278114(MapDisp *m, long ent)
{
    GameEntity *e = &MENT(ent);
    uchar *c3 = game_cls3[e->slot] + 0x62;
    short sx, sy, r;
    fix k;

    FCOLOR(0x26);
    sx = (short)SX(m, e->x);
    sy = (short)SY(m, e->y);
    if (m->layers & 2) {
        k = MUL(0x60000, 0x10F5B);
        r = (short)(MUL(DIV(*(fix *)c3, k), m->inv) >> 16);
        SHARP_OR(port_map_circle(sx, sy, r), CANVAS(C_CIRCLE, (long)sx, (long)sy, (long)r));
        FCOLOR(0x28);
        k = MUL(0x60000, 0x10F5B);
#ifndef TN_WATCOM_ABI
        if (port_map_kx > 1) {          /* the game's y shift, taken out of the sharp map's */
            r = (short)(MUL(DIV(*(fix *)(c3 + 4), k) << port_map_sh, m->inv) >> 16);
            port_map_circle(sx, sy, r);
            return;
        }
#endif
        r = (short)(MUL(DIV(*(fix *)(c3 + 4), k) << SH, m->inv) >> 16);
        CANVAS(C_CIRCLE, (long)sx, (long)sy, (long)r);
    }
}

/* squad path: short count at +5, 3-byte points from +9 (9-bit x, 9-bit y, bit 18: map
 * coordinates; else relative to the map record's +8 / +0xA origin in 4-unit cells).  Draws the
 * first point, then lines between the midpoints of consecutive points. */
static void path_point(uchar *pt, short *x, short *y)
{
    ushort w = *(ushort *)pt;

    *x = (short)(w & 0x1FF);
    *y = (short)(((pt[2] & 3) << 7) + (w >> 9));
    if (!(pt[2] & 4)) {
        *x = (short)((short)(*x - *(short *)(game_map_35BFB4 + 8)) * 4 + 2);
        *y = (short)((short)(*y - *(short *)(game_map_35BFB4 + 0xA)) * 4 + 2);
    }
}

void mapdisp_278254(MapDisp *m, uchar *path, uchar colour)
{
    short px, py, cx, cy, sx, sy, nx, ny;
    long i, n = *(short *)(path + 5);
    uchar *pt = path + 9;

    FCOLOR(colour);
    path_point(pt, &px, &py);
    sx = (short)SX(m, ((fix)px << 16) | 0x8000);
    sy = (short)SY(m, ((fix)py << 16) | 0x8000);
    SHARP_OR(port_map_point(sx, sy), CANVAS(C_POINT, (long)sx, (long)sy));
    pt += 3;
    for (i = 1; i < n; i++, pt += 3) {
        path_point(pt, &cx, &cy);
        nx = (short)SX(m, ((fix)((px + cx) / 2) << 16) | 0x8000);
        ny = (short)SY(m, ((fix)((py + cy) / 2) << 16) | 0x8000);
        SHARP_OR(port_map_iline(sx, sy, nx, ny), gr_iline(sx, sy, nx, ny));
        px = cx;
        py = cy;
        sx = nx;
        sy = ny;
    }
}

/* map annotations (mapann_list): +0xA kind (0 text, 1 bitmap, 2 box, 3 circle), +0xB colour,
 * +0xC font, +0xD zoom bits, +0xE layer bits, +0xF x, +0x13 y, +0x17 w, +0x1B h, +0x1F bitmap
 * ref, +0x23 text */
void mapdisp_278488(MapDisp *m)
{
    long bit = 1L << mapd_ann_shift[m->zoom];
    uchar lay = (m->layers & 0x18) >> 3;
    uchar *a;
    short x0, y0, x1, y1;
    grs_bitmap *bm;

    for (a = *(uchar **)&mapann_list; a != (uchar *)&mapann_list + 8; a = *(uchar **)a) {
        if (!(a[0xD] & bit) || !(a[0xE] & lay))
            continue;
        x0 = (short)(MUL(*(fix *)(a + 0xF) - m->x, m->inv) >> 16);
        y0 = (short)SY(m, *(fix *)(a + 0x13));
        x1 = (short)SX(m, *(fix *)(a + 0xF) + *(fix *)(a + 0x17));
        y1 = (short)SY(m, *(fix *)(a + 0x13) + *(fix *)(a + 0x1B));
        FCOLOR(a[0xB]);
        switch (a[0xA]) {
        case 0:
            SHARP_OR(port_map_text(mapd_ann_fonts[a[0xC]], (char *)a + 0x23, x0, y0),
                     GfxDrawText(mapd_ann_fonts[a[0xC]], (char *)a + 0x23, x0, y0));
            break;
        case 1:
            bm = RefGet(*(Ref *)(a + 0x1F));
            bm->bits = (uchar *)bm + 0x1C;
            SHARP_OR(port_map_bitmap(bm, x0, y0),
                     CANVAS(DRAW_DEVICE_BITMAP + 2 * bm->type, (long)bm, (long)x0, (long)y0));
            break;
        case 2:
            SHARP_OR(port_map_box(x0, y0, x1, y1), CANVAS(C_BOX, (long)x0, (long)y0, (long)x1, (long)y1));
            break;
        case 3:
            SHARP_OR(port_map_circle((short)((x0 + x1) / 2), (short)((y1 + y0) / 2), (short)((x1 - x0) / 2)),
                     CANVAS(C_CIRCLE, (long)(short)((x0 + x1) / 2), (long)(short)((y1 + y0) / 2),
                            (long)(short)((x1 - x0) / 2)));
            break;
        }
    }
}

/* member name tags: a line from 2 to 9 units off the member in its tag direction, the voice
 * name at its end; the directions are laid out again every 5 seconds */
void mapdisp_278688(MapDisp *m)
{
    grs_font *font;
    GameSpeaker *g = game_391E50;
    long i, pos, ty, tx;
    fix ex, ey, sn, cs, x1, y1, x2, y2;
    char *s;

    if (game_time >= mapd_lbl_time) {
        mapdisp_2788C8(mapd_lbl_pos);
        mapd_lbl_time = game_time + 0x1388;
    }
    font = ResLock(0x1F7);
    for (i = 0; i < 4; i++, g++) {
        if (!g->active)
            continue;
        pos = mapd_lbl_pos[i];
        ex = MENT(*(short *)((uchar *)g + 2)).x;
        ey = MENT(*(short *)((uchar *)g + 2)).y;
        fix_sincos(mapd_lbl_angle[pos], &sn, &cs);
        x1 = ex - m->x + MUL(0x20000, cs);
        y1 = ey - m->y + MUL(0x20000, sn);
        x2 = ex - m->x + MUL(0x90000, cs);
        y2 = ey - m->y + MUL(0x90000, sn);
        FCOLOR(mfd_35B3ED[g->chan]);
        SHARP_OR(port_map_line(MUL(x1, m->inv), MUL(y1 << SH, m->inv), MUL(x2, m->inv), MUL(y2 << SH, m->inv)),
                 gr_fix_line(MUL(x1, m->inv), MUL(y1 << SH, m->inv), MUL(x2, m->inv), MUL(y2 << SH, m->inv)));
        s = RefGet(0x16C0000 + (uchar)g->voice);
#ifndef TN_WATCOM_ABI
        if (port_map_kx > 1) {          /* the tag's offsets in the game's pixels */
            ty = (MUL(y2 << SH, m->inv) >> 16) + mapd_lbl_dy[pos] * port_map_ky;
            tx = (MUL(x2, m->inv) >> 16) + mapd_lbl_dx[pos] * port_map_kx;
            tx -= ((long)gr_font_string_width(font, s) >> mapd_lbl_wsh[pos]) * port_map_kx;
            port_map_text(0x1F7, s, tx, ty);
            continue;
        }
#endif
        ty = (MUL(y2 << SH, m->inv) >> 16) + mapd_lbl_dy[pos];
        tx = (MUL(x2, m->inv) >> 16) + mapd_lbl_dx[pos];
        tx -= (long)gr_font_string_width(font, s) >> mapd_lbl_wsh[pos];
        GfxDrawText(0x1F7, s, tx, ty);
    }
    ResUnlock(0x1F7);
}

/* tag directions: 3 for the northernmost member, 1 for the southernmost, of the others 2 for
 * the western and 0 for the eastern one */
void mapdisp_2788C8(uchar *pos)
{
    long i, mn = -1, mx = -1, a = -1, b = -1;
    fix y, lo = 0x3E70000, hi = (fix)0xFC190000;

    for (i = 0; i < 4; i++)
        if (game_391E50[i].active) {
            y = MENT(*(short *)((uchar *)&game_391E50[i] + 2)).y;
            if (lo > y) {
                lo = y;
                mn = i;
            }
        }
    for (i = 0; i < 4; i++)
        if (game_391E50[i].active) {
            y = MENT(*(short *)((uchar *)&game_391E50[i] + 2)).y;
            if (hi < y) {
                hi = y;
                mx = i;
            }
        }
    for (i = 0; i < 4; i++) {
        if (!game_391E50[i].active || i == mn || i == mx)
            continue;
        if (a < 0)
            a = i;
        else if (MENT(*(short *)((uchar *)&game_391E50[i] + 2)).x <
                 MENT(*(short *)((uchar *)&game_391E50[a] + 2)).x) {
            b = a;
            a = i;
        } else
            b = i;
    }
    if (mn >= 0)
        pos[mn] = 3;
    if (mx >= 0)
        pos[mx] = 1;
    if (a >= 0)
        pos[a] = 2;
    if (b >= 0)
        pos[b] = 0;
}

void mapdisp_2789D8(MapDisp *m, LGPoint p)
{
    physics_2D47D8(game_player_entity, p.x * m->scale + m->x, (p.y >> SH) * m->scale + m->y);
}

/* expanding blast ring of a message-ring entry (a = x, b = y, c = radius, done = step) */
void mapdisp_278A24(ObjRingEntry *e, MapDisp *m)
{
    long sx, sy, r, h, i;
    fix k, rr, ro;
    uchar c;

    sx = SX(m, e->a);
    sy = SY(m, e->b);
    r = e->c >> 16;
    h = e->c >> 17;
    k = MUL(0x60000, 0x1B22);
    rr = (MUL(DIV((HI ? r : h) << 16, k), m->inv) >> 16) << 16;
#ifndef TN_WATCOM_ABI
    if (port_map_kx > 1 && (sx < -16000 || sx > 16000 || sy < -16000 || sy > 16000))
        return;                         /* far off the sharp map (its fix centres would wrap) */
#endif
    if (grd_canvas->gc.fill_type) {
        fill_type_0();
        SHARP_OR(port_map_disc(sx, sy, rr), rndsky_2A6AF8(sx, sy, rr, 0xF7));
        fill_type_1();
    } else
        SHARP_OR(port_map_disc(sx, sy, rr), rndsky_2A6AF8(sx, sy, rr, 0xF7));
    k = MUL(0x60000, 0x1B22);
    ro = (MUL(DIV(r << 16, k), m->inv) >> 16) << 16;
    k = MUL(0x60000, 0x1B22);
    (void)MUL(DIV(h << 16, k), m->inv);         /* computed and dropped by the original */
    c = e->done + 0x82;
    if (h <= r)
        for (i = h << 16; i <= r << 16; i += 0x10000) {
            if (c > 0x91)
                c = 0x82;
            FCOLOR(c);
            k = MUL(0x60000, 0x1B22);
            gen_fix_ring(sx << 16, sy << 16, ro, (MUL(DIV(i, k), m->inv) >> 16) << 16);
            c++;
        }
    if (game_time > mapd_ring_time) {
        e->done++;
        mapd_ring_time = game_time + 0x4B;
    }
}

#ifndef TN_WATCOM_ABI
/* Freefall: the goggles with their screens hidden (Tab, helmet.c), where the main view covers the
 * middle screen's map (view 1).  Drawn over the main view at that map's place ((x, y) of the
 * current canvas: view1's top-left), just where the map has them, in its colours and marks and at
 * its pixels: with "Map view cone" on the player's view cone, the suit's own mark (its cross and
 * heading tick), and with "Show enemies without MFD" on (port_map_enemies) the map's contacts
 * too (the units of the squad's near lists, not the squad's own soldiers, the land's outlines or
 * the air strike).  The map is the one the screen shows (mode 8 hview3d_272CFC, mode 9
 * hview3d_272E84: the goggles'), its scroll after the unit it follows worked out as
 * mapdisp_276C6C would make it (not made: the terrain is not drawn); with another page there,
 * the goggles' map as mapdisp_276ACC makes one; its scale, and the player held at its middle
 * (the owner's ask: still, where the scroll would move it in steps).  As on the map, a damaged
 * sensor now and then
 * jams the contacts and the suit's mark, greys or flickers them: with Freefall's own random
 * numbers, not the game's (whose sequence would change), and nothing of the game's state
 * changes. */
extern int port_map_cone, port_map_enemies, port_map_radar;
extern int port_mouselook;               /* port/core/platform.c: the Advanced controls */
void port_map_cone_view(double x, double y, double r, double aspect, ushort view, ushort half);
grs_canvas *port_map_marks_begin(long x, long y, long w, long h, int *kx, int *ky);
void port_map_marks_end(void);
int port_map_radar_k(double x, double y, int *kx, int *ky);
int port_s3d_radar_map(void *map, long b, int kx, int ky, const uchar **land, const uchar **pic, int *pw,
                       int *ph);
void port_map_radar_draw(double x, double y, const uchar *land, const uchar *pic, int W, int H, double px,
                         double py, double A, const void *font, int n, const long *ax, const long *ay,
                         const uchar *ac, double aspect, ushort view, ushort half);

static unsigned hid_rnd(unsigned n)     /* 0 .. n */
{
    static unsigned s = 0x2545F491u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s % (n + 1);
}

/* entity ent's place on map g at the marks' scale (kx > 1: the sharp map's pixels, sharp3d.c
 * port_s3d_map; 1: the map's own) */
static void hidden_at(const MapDisp *g, long ent, int kx, long *sx, long *sy)
{
    const GameEntity *e = &MENT(ent);
    if (kx > 1) {
        *sx = MUL(e->x - g->x, g->inv * kx) >> 16;
        *sy = MUL(e->y - g->y, g->inv * kx) >> 16;
    } else {
        *sx = SX(g, e->x);
        *sy = SY(g, e->y);
    }
}

/* mapdisp_277BAC's mark (not the outlines) */
static void hidden_mark(const MapDisp *g, long ent, int kx, int ky, uchar dmg, uchar vis)
{
    GameEntity *e = &MENT(ent);
    long sx, sy;
    uchar col;

    if (e->cls == 4 && !instsim_288260(e->type))
        return;
    hidden_at(g, ent, kx, &sx, &sy);
    col = vis ? mapdisp_2769E8((short)ent) : 0x90;
    if (dmg < 0x5A && (short)ent != game_player_entity && hid_rnd(1))
        col += 2;
    FCOLOR(col);
    if (g->zoom < 4)
        port_map_point(sx, sy);
    else
        port_map_cross(sx, sy, HI ? 2 : 1);
    if ((short)ent == game_player_entity) {
        fix sn, cs;

        fix_fastsincos(e->heading, &sn, &cs);
        if (HI)
            sn += sn;
        if (kx > 1)
            port_map_line((sx << 16) + 0x8000, (sy << 16) + 0x8000, (sx << 16) + 0x8000 + cs * 6 * kx,
                          (sy << 16) + 0x8000 + sn * 6 * ky);
        else
            port_map_iline(sx, sy, ((cs * 6 + 0x8000) >> 16) + sx, ((sn * 6 + 0x8000) >> 16) + sy);
    }
}

static int is_member(long ent)
{
    return MENT(ent).cls == 1 && (schar)mfddata_2665E8((short)ent) >= 0;
}

/* "Radar without MFD": the middle screen's map g (the player at its middle) drawn whole, as the
 * screen would show it, as a round radar over the main view at (x, y) (port/video/mapcone.c
 * port_map_radar_draw, port/video/sharp3d.c port_s3d_radar_map); b the unit whose sensors it shows;
 * jam: the contacts are jammed this frame (as port_map_hidden).  The arrows on the rim: the
 * near lists' contacts (as the map draws them, mapdisp_276EF8, while it shows them: flag 4), the
 * target and the nav point.  0: no sharp picture to draw it on (3D resolution 1x) */
static int hidden_radar(MapDisp *g, long b, long x, long y, int jam)
{
    static long ax[0x84], ay[0x84];
    static uchar ac[0x84];
    const uchar *land, *pic;
    int kx, ky, W, H, n = 0;
    long sx, sy;
    grs_font *f;

    if (!port_map_radar_k(x, y, &kx, &ky) || !port_s3d_radar_map(g, b, kx, ky, &land, &pic, &W, &H))
        return 0;
    if ((g->flags & 4) && !jam) {
        NearEntry buf[0x80];
        NearList *lists[8], all, *l;
        short v, save = aihelp_cur_2D3298;
        long cnt = 0, k, j;

        aihelp_2D3298(0);
        while ((v = (short)aihelp_2D3298(1)) != 0)
            if ((l = damage_27D158(v)) != NULL && cnt < 7)
                lists[cnt++] = l;
        aihelp_cur_2D3298 = save;
        if (game_396868 && (l = damage_27D158(game_396868)) != NULL)
            lists[cnt++] = l;
        damage_27D140(&all, 0, 0, 0x80, buf);
        k = damage_27D368(&all, lists, cnt);
        for (j = 0; j <= k && n < 0x82; j++) {
            v = j < k ? buf[j].id : (short)targ_current;
            if (!v || v == game_player_entity || v == strike_entity || is_member(v))
                continue;
            if (MENT(v).cls == 4 && !instsim_288260(MENT(v).type))
                continue;
            hidden_at(g, v, kx, &sx, &sy);
            ax[n] = sx;
            ay[n] = sy;
            ac[n] = mapdisp_2769E8(v);
            n++;
        }
    }
    if (ffpipe_38FFF0 && ffpipe_38FFF4) {       /* the nav point (mapdisp_277424) */
        ax[n] = MUL(ffpipe_38FFF0 - g->x, g->inv * kx) >> 16;
        ay[n] = MUL(ffpipe_38FFF4 - g->y, g->inv * kx) >> 16;
        ac[n] = 0x9C;
        n++;
    }
    if (game_35ADE0) {                  /* the map's colours through the planet's table */
        int i;
        for (i = 0; i < n; i++)
            ac[i] = planet_tab41[ac[i]];
    }
    hidden_at(g, game_player_entity, kx, &sx, &sy);
    f = ResLock(0x1F7);
    /* a picture pixel shows A times as tall as wide: the game's screen is shown 4:3 */
    port_map_radar_draw(x, y, land, pic, W, H, sx + 0.5, sy + 0.5,
                        3.0 * tnui_back_canvas.bm.w / (4.0 * tnui_back_canvas.bm.h) * kx / ky, f, n, ax, ay, ac,
                        1.0 / (1 << SH),
                        (ushort)(MENT(game_player_entity).heading + hud_441355), *(ushort *)(game_camera + 0x18));
    ResUnlock(0x1F7);
    return 1;
}

void port_map_hidden(void *view1, long x, long y)
{
    static uchar jammed;
    HudWidget *v1 = view1;
    MapDisp g, *m = (MapDisp *)hud_views[1].f10;
    uchar mode = hud_views[1].mode;
    grs_canvas *mk;
    long b, sx, sy;
    int kx, ky;
    uchar d1 = 0x64, d2 = 0x64, vis = 1, draw = 1;
    /* the cone and the suit's mark (the white arrow in the view's middle) only with Head view
     * cone on and the Advanced controls (owner, P142) */
    int cone = port_map_cone && port_mouselook;

    if (!game_player_entity || !v1 || (!cone && !port_map_enemies && !port_map_radar))
        return;
    if ((mode == 8 || mode == 9) && m >= mapd_pool && m < mapd_pool + 4 && (m->flags & 2) &&
        m->bm.w == v1->canvas.bm.w && m->bm.h == v1->canvas.bm.h && m->scale > 0) {
        b = mode == 8 ? mfd_38FC81 : hud_38FFFC;    /* hview3d_272CFC, hview3d_272E84 */
        g = *m;
        if (!(g.flags & 1)) {           /* mapdisp_276C6C / 276CDC after b, without drawing */
            fix nx, ny, p;
            long dx, dy, e = WIDE_E(&g), w = g.bm.w;

            mapdisp_2779A0(&g, MENT(b).x, MENT(b).y, &nx, &ny);
            dx = (short)(MUL(nx - g.x, g.inv) >> 16);
            dy = (short)(MUL((ny - g.y) << SH, g.inv) >> 16);
            p = dx * g.scale + g.x - e * g.scale;
            if (p < 0 || (w + 2 * e) * g.scale + p > 0x2000000)
                dx = 0;
            g.x += dx * g.scale;
            p = (dy >> SH) * g.scale + g.y;
            if (p < 0 || p + (g.bm.h >> SH) * g.scale > 0x2000000)
                dy = 0;
            g.y += (dy >> SH) * g.scale;
        }
    } else {                            /* the goggles' map (mode 9) as mapdisp_276ACC makes one */
        long zoom = hud_map_zoom < 0 ? 0 : hud_map_zoom > 7 ? 7 : hud_map_zoom;

        b = hud_38FFFC > 0 && hud_38FFFC < GAME_MAX_ENTITIES && MENT(hud_38FFFC).cls ? hud_38FFFC :
            game_player_entity;
        memset(&g, 0, sizeof g);
        g.bm.w = v1->canvas.bm.w;
        g.bm.h = v1->canvas.bm.h;
        while (zoom < 7 && !port_map_zoom_ok(&g, zoom))
            zoom++;
        g.zoom = (short)zoom;
        g.scale = wide_scale(&g, zoom);
        g.inv = DIV(0x10000, g.scale);
        g.flags = 2 | 4;
        mapdisp_2779A0(&g, MENT(b).x, MENT(b).y, &g.x, &g.y);
    }
    {   /* the player held at the map's middle (owner, after P105): the suit's mark and the cone
         * stand still, as the map's scroll steps and its pixel steps would move them; the
         * contacts are placed around it */
        const GameEntity *pe = &MENT(game_player_entity);

        g.x = pe->x - (g.bm.w / 2) * g.scale;      /* the map's middle as mapdisp_276BC8 has it */
        g.y = pe->y - ((g.bm.h / 2) >> SH) * g.scale;
    }
    if (!(g.flags & 4))
        draw = 0;
    else {                              /* mapdisp_276EF8's near-list contacts */
        if ((short)b)
            d1 = damage_27DF14((short)b, 1);
        if (d1 < 0x50 && !jammed) {
            uchar t = (uchar)(0x50 - (d1 < 0x32 ? 0x32 : d1));
            if (hid_rnd(0x64) < t) {
                jammed = 1;
                draw = 0;
            }
        } else
            jammed = 0;
    }
    if (port_map_radar && hidden_radar(&g, b, x, y, !draw))
        return;                         /* (3D resolution 1x: the marks below instead) */
    if (!cone && !port_map_enemies)
        return;
    mk = port_map_marks_begin(x, y, g.bm.w, g.bm.h, &kx, &ky);
    if (!mk)
        return;
    if (cone) {                         /* the map's cone (port_map_cone_draw), r 15 */
        hidden_at(&g, game_player_entity, kx, &sx, &sy);
        port_map_cone_view(x + (sx + 0.5) / kx, y + (sy + 0.5) / ky, 15, 1.0 / (1 << SH),
                           (ushort)(MENT(game_player_entity).heading + hud_441355),
                           *(ushort *)(game_camera + 0x18));
    }
    if (draw) {
        if ((short)b)
            d2 = damage_27DF14((short)b, 2);
        if (d2 < 0x2D)
            vis = hid_rnd(0x64) >= (unsigned)((0x2D - d2) * 2);
        gr_push_canvas(mk);
        if (game_35ADE0) {              /* the map's colours through the planet's table */
            mk->gc.fill_type = 1;
            mk->gc.fill_parm = (long)planet_tab41;
        }
        port_map_kx = kx;
        port_map_ky = ky;
        if (port_map_enemies) {
            NearEntry buf[0x80];
            NearList *lists[8], all, *l;
            short v, save = aihelp_cur_2D3298;
            long cnt = 0, k, j;

            aihelp_2D3298(0);
            while ((v = (short)aihelp_2D3298(1)) != 0)
                if ((l = damage_27D158(v)) != NULL && cnt < 7)
                    lists[cnt++] = l;
            aihelp_cur_2D3298 = save;
            if (game_396868 && (l = damage_27D158(game_396868)) != NULL)
                lists[cnt++] = l;
            damage_27D140(&all, 0, 0, 0x80, buf);
            k = damage_27D368(&all, lists, cnt);
            for (j = 0; j < k; j++) {
                v = buf[j].id;
                if (v != game_player_entity && v != strike_entity && !is_member(v))
                    hidden_mark(&g, v, kx, ky, d1, vis);
            }
            if (targ_current && targ_current != game_player_entity && !is_member(targ_current))
                hidden_mark(&g, targ_current, kx, ky, d1, vis);
        }
        if (cone)
            hidden_mark(&g, game_player_entity, kx, ky, d1, vis);
        port_map_kx = port_map_ky = 1;
        gr_pop_canvas();
    }
    port_map_marks_end();
}
#endif
