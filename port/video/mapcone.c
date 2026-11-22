/* mapcone.c -- Freefall: the view cone on the squad map and the full-screen map.
 *
 * With "Map view cone" on, the maps (mapdisp_276EF8) shade a see-through cone from the player's
 * position toward where the head looks (heading + the head's turn, hud_441355), as wide as the
 * main view sees (its camera's fov_x, wider in widescreen), under the game's own markers, so the
 * white heading tick keeps showing where the suit walks.  The shading is two colour remap tables
 * built from the game's palette (15% light green inside, 35% on the edges), made again
 * whenever the palette changes.  Off (the default) leaves the maps the original.
 *
 * In the goggles with their screens hidden (Tab), where no map shows, the same cone and the
 * suit's own mark are drawn over the main view just where the middle screen's squad map has them
 * (mapdisp.c port_map_hidden), at the sharp picture's pixels (sharp3d.c); with "Show enemies
 * without MFD" on (port_map_enemies, off by default) the map's contacts too.  With "Radar without
 * MFD" on (port_map_radar, off by default; needs the sharp picture: 3D resolution 2x or more) the
 * middle screen's whole map instead, as a round radar (port_map_radar_draw): its land faded into
 * the view, its marks, the cone out to the rim, arrows on the rim for contacts beyond it.
 */
#include <math.h>
#include <string.h>
#include "2d.h"
#include "port.h"
#include "platform.h"
#include "sharp3d.h"

int port_map_cone;

/* the cone shows where the head looks: with the Advanced controls only (owner; the
 * option is grey with the Classic controls) */
static int cone_on(void)
{
    return port_map_cone && port_mouselook;
}
int port_map_enemies;                   /* "Show enemies without MFD" (mapdisp.c) */
int port_map_radar;                     /* "Radar without MFD" (mapdisp.c) */
fix port_map_cone_wide = 0x10000;       /* main view width / the game's (sharp3d.c, widescreen) */
extern int port_map_kx;                 /* port/video/mapsharp.c */

static uchar pal_seen[768], tab_fill[256], tab_edge[256];
/* ... and without the planet's colours 0xB0..0xE6, which the game's infrared swaps for its ramp:
 * for the one picture drawn before each swap (the infrared going on or off, port_ir_switching),
 * which is still on the screen after it.  The map under the cone is in the other colours
 * then (the game's, through the planet's table), but the tint's nearest colours were the
 * planet's greens, and the cone flashed in the ramp's colours for a frame (P150) */
static uchar tab_fill_sw[256], tab_edge_sw[256];
#define TAB(k) (port_ir_switching() ? ((k) == 2 ? tab_edge_sw : tab_fill_sw) : ((k) == 2 ? tab_edge : tab_fill))
static int pal_ok;
static float pal8[256][3];              /* the palette in 0..255 (the radar's mixes) */
static int pal8i[256][3];
static uchar mixtab[256 * 256], mixok[256 * 256];     /* view colour x land colour: the radar's mix */
static int clip_on;                     /* the radar's window clips the cone (port_map_radar_draw) */
static double clip_cx, clip_cy, clip_r, clip_a;
static uchar cube[1 << 15], cube_ok[1 << 15];   /* a colour (5:5:5) -> its nearest palette colour */

static void build_tables(const uchar *pal)
{
    static const float tint[3] = { 170, 255, 140 };    /* light green */
    int c, k, i, best, max = 0;
    float sc;

    for (i = 0; i < 768; i++)
        if (pal[i] > max)
            max = pal[i];
    sc = max <= 63 ? 63.0f / 255 : 1.0f;                /* VGA palettes are 6-bit */
    for (k = 0; k < 4; k++) {
        float a = k & 1 ? 0.35f : 0.15f;   /* owner's choice: 15% inside, 35% edges */
        uchar *t = k == 0 ? tab_fill : k == 1 ? tab_edge : k == 2 ? tab_fill_sw : tab_edge_sw;
        for (c = 0; c < 256; c++) {
            float r = pal[c * 3] * (1 - a) + tint[0] * sc * a;
            float g = pal[c * 3 + 1] * (1 - a) + tint[1] * sc * a;
            float b = pal[c * 3 + 2] * (1 - a) + tint[2] * sc * a;
            float bd = 1e30f;
            best = c;
            for (i = 1; i < 0xF0; i++) {                /* no 0 (clear) or the cycling colours */
                float dr = pal[i * 3] - r, dg = pal[i * 3 + 1] - g, db = pal[i * 3 + 2] - b;
                float d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
                if (k >= 2 && i >= 0xB0 && i <= 0xE6)
                    continue;
                if (d < bd) {
                    bd = d;
                    best = i;
                }
            }
            t[c] = (uchar)best;
        }
    }
    for (c = 0; c < 256; c++)
        for (k = 0; k < 3; k++)
            pal8i[c][k] = (int)((pal8[c][k] = pal[c * 3 + k] * (max <= 63 ? 255.0f / 63 : 1.0f)) + 0.5f);
    memset(cube_ok, 0, sizeof cube_ok);
    memset(mixok, 0, sizeof mixok);
    memcpy(pal_seen, pal, 768);
    pal_ok = 1;
}

/* the palette colour nearest (r, g, b) (0..255; not 0, clear, or the cycling colours), found once
 * for each 5:5:5 cell */
static uchar nearest(float r, float g, float b)
{
    int ri = (int)r >> 3, gi = (int)g >> 3, bi = (int)b >> 3, k, i, best = 1;
    float bd = 1e30f;
    ri = ri < 0 ? 0 : ri > 31 ? 31 : ri;
    gi = gi < 0 ? 0 : gi > 31 ? 31 : gi;
    bi = bi < 0 ? 0 : bi > 31 ? 31 : bi;
    k = ri << 10 | gi << 5 | bi;
    if (cube_ok[k])
        return cube[k];
    for (i = 1; i < 0xF0; i++) {
        float dr = pal8[i][0] - r, dg = pal8[i][1] - g, db = pal8[i][2] - b;
        float d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    cube[k] = (uchar)best;
    cube_ok[k] = 1;
    return (uchar)best;
}

/* the cone from its point: the view's heading, half its width (angles 0x10000 = 360 degrees),
 * radius r, edges t wide (pixels) */
typedef struct {
    float vx, vy, ch, sh, r, t, ex;
} Cone;

static void cone_set(Cone *c, ushort view, ushort half, float r, float t)
{
    const float U = 6.2831853f / 65536;
    float hv = view * U, hf = half * U;
    if (port_map_cone_wide != 0x10000)              /* wider view: atan(tan(half) * wide) */
        hf = atanf(tanf(hf) * (port_map_cone_wide / 65536.0f));
    if (hf < 0.05f)
        hf = 0.05f;
    if (hf > 1.4f)
        hf = 1.4f;
    c->vx = cosf(hv);
    c->vy = sinf(hv);
    c->ch = cosf(hf);
    c->sh = sinf(hf);
    c->r = r;
    c->t = t;
    c->ex = 0.55f * t;                              /* edge line half width (pixels) */
}

/* (dx, dy) from the point: 0 outside, 1 inside, 2 on the edges (within ex of a side line, or
 * the far rim) */
static int cone_at(const Cone *c, float dx, float dy)
{
    float d = sqrtf(dx * dx + dy * dy), along, side;
    if (d < c->t || d > c->r)
        return 0;
    along = dx * c->vx + dy * c->vy;
    if (along < d * c->ch)
        return 0;
    side = fabsf(dx * c->vy - dy * c->vx);          /* distance from the centre line */
    return along * c->sh - side * c->ch < c->ex || d > c->r - c->t ? 2 : 1;
}

static int tables_ready(void)
{
    if (!grd_pal)
        return 0;
    if (!pal_ok || memcmp(pal_seen, grd_pal, 768))
        build_tables(grd_pal);
    return 1;
}

/* cv: the map's canvas; (sx, sy) the player in its pixels; y pixels are 1 << sh world steps;
 * view: the view's heading, half: half the view's width (angles 0x10000 = 360 degrees) */
void port_map_cone_draw(grs_canvas *cv, long sx, long sy, int sh, ushort view, ushort half)
{
    long x0, x1, y0, y1, x, y, r, ry, t = 1;
    uchar *row;
    Cone c;

    if (!cone_on() || cv->bm.type != BMT_FLAT8 || !tables_ready())
        return;
    r = 15;         /* pixels, on the squad map and the full map alike (2/7 of the squad map) */
    if (port_map_kx > 1) {      /* the sharp map (port_s3d_map): its pixels, edges half as wide */
        t = port_map_kx / 2;
        r *= port_map_kx;
    }
    cone_set(&c, view, half, (float)r, (float)t);
    ry = r << sh;
    x0 = sx - r; x1 = sx + r;
    y0 = sy - ry; y1 = sy + ry;
    if (x0 < cv->gc.clip.i.left) x0 = cv->gc.clip.i.left;
    if (y0 < cv->gc.clip.i.top) y0 = cv->gc.clip.i.top;
    if (x1 >= cv->gc.clip.i.right) x1 = cv->gc.clip.i.right - 1;
    if (y1 >= cv->gc.clip.i.bot) y1 = cv->gc.clip.i.bot - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= cv->bm.w) x1 = cv->bm.w - 1;
    if (y1 >= cv->bm.h) y1 = cv->bm.h - 1;
    for (y = y0; y <= y1; y++) {
        float dy = (float)(y - sy) / (1 << sh);
        row = cv->bm.bits + (long)y * cv->bm.row;
        for (x = x0; x <= x1; x++) {
            int k = cone_at(&c, (float)(x - sx), dy);
            if (k)
                row[x] = TAB(k)[row[x]];
        }
    }
}

/* the pixels of row Y whose middles lie within r of (cx, cy) (rows A times as tall as a column is
 * wide): *xa .. *xb; 0 if none */
static int span(double cx, double cy, double A, double r, long Y, long *xa, long *xb)
{
    double dy = (Y + 0.5 - cy) * A, h2 = r * r - dy * dy, h;
    if (h2 < 0)
        return 0;
    h = sqrt(h2);
    *xa = (long)ceil(cx - h - 0.5);
    *xb = (long)floor(cx + h - 0.5);
    return *xb >= *xa;
}

/* the goggles' cone (mapdisp.c port_map_hidden): its point at (x, y) of the current canvas (the
 * main view's widget; a pixel's middle at .5), r of the game's columns in radius; aspect: the
 * cone's rows per column there (1 / (1 << hud_35A544): as tall as on the squad map); view, half
 * as above.  Into the main view's sharp picture when there is one (with "True colour" its full
 * colours mixed too), else onto the canvas. */
void port_map_cone_view(double x, double y, double r, double aspect, ushort view, ushort half)
{
    static const float tint[3] = { 170, 255, 140 };
    grs_canvas *cv = grd_canvas;
    port_s3d_paint p;
    uint8_t *bits;
    uint32_t *tc = NULL;
    const uint8_t *tc_rows = NULL;
    long row, X0, X1, Y0, Y1, X, Y;
    double cx, cy, kx, ky, t;
    Cone c;

    if (!cone_on() || !tables_ready())
        return;
    if (port_s3d_paint_at(x, y, &p)) {
        bits = p.high;
        tc = p.tc;
        tc_rows = p.tc_rows;
        row = p.w;
        cx = p.X;
        cy = p.Y;
        kx = p.kx;
        ky = p.ky / aspect;             /* the picture's rows per game column's width */
        X0 = 0; Y0 = 0; X1 = p.w; Y1 = p.h;
    } else {
        if (cv->bm.type != BMT_FLAT8)
            return;
        bits = cv->bm.bits;
        row = cv->bm.row;
        cx = x;
        cy = y;
        kx = 1;
        ky = 1 / aspect;
        X0 = cv->gc.clip.i.left; Y0 = cv->gc.clip.i.top;
        X1 = cv->gc.clip.i.right; Y1 = cv->gc.clip.i.bot;
        if (X1 > cv->bm.w) X1 = cv->bm.w;
        if (Y1 > cv->bm.h) Y1 = cv->bm.h;
    }
    t = kx / 2 > 1 ? floor(kx / 2) : 1;
    cone_set(&c, view, half, (float)(r * kx), (float)t);
    /* its box */
    if (X0 < (long)floor(cx - r * kx)) X0 = (long)floor(cx - r * kx);
    if (X1 > (long)ceil(cx + r * kx)) X1 = (long)ceil(cx + r * kx);
    if (Y0 < (long)floor(cy - r * ky)) Y0 = (long)floor(cy - r * ky);
    if (Y1 > (long)ceil(cy + r * ky)) Y1 = (long)ceil(cy + r * ky);
    if (X0 < 0) X0 = 0;
    if (Y0 < 0) Y0 = 0;
    if (X1 <= X0 || Y1 <= Y0)
        return;
    for (Y = Y0; Y < Y1; Y++) {
        uint8_t *d = bits + Y * row;
        uint32_t *tr = tc && tc_rows[Y] ? tc + Y * row : NULL;
        float dy = (float)((Y + 0.5 - cy) * kx / ky);   /* at the columns' scale */
        long xa = X0, xb = X1 - 1;
        if (clip_on) {                  /* the radar's window (port_map_radar_draw) */
            if (!span(clip_cx, clip_cy, clip_a, clip_r, Y, &xa, &xb))
                continue;
            if (xa < X0)
                xa = X0;
            if (xb > X1 - 1)
                xb = X1 - 1;
        }
        for (X = xa; X <= xb; X++) {
            int k = cone_at(&c, (float)(X + 0.5 - cx), dy);
            if (!k)
                continue;
            if (tr && tr[X]) {          /* a full colour: mixed with the tint */
                uint32_t v = tr[X];
                float a = k == 2 ? 0.35f : 0.15f;
                int r8 = (int)((v >> 16 & 0xFF) * (1 - a) + tint[0] * a + 0.5f);
                int g8 = (int)((v >> 8 & 0xFF) * (1 - a) + tint[1] * a + 0.5f);
                int b8 = (int)((v & 0xFF) * (1 - a) + tint[2] * a + 0.5f);
                tr[X] = (v & 0xFF000000u) | (uint32_t)r8 << 16 | (uint32_t)g8 << 8 | (uint32_t)b8;
            } else
                d[X] = TAB(k)[d[X]];
        }
    }
}

/* the goggles' map marks (mapdisp.c port_map_hidden): a cleared canvas as big as the middle
 * screen at the main view's picture's scale, for the map's own drawing of its marks (mapsharp.c),
 * then laid over the picture (their pixels kept sharp by "Anti-aliasing", their full colours
 * dropped), or with no sharp picture over the game's canvas.  0 drawn = nothing there. */
static uint8_t mk_buf[1 << 20];
static grs_canvas mk_cv, *mk_under;
static port_s3d_paint mk_p;
static int mk_sharp;
static long mk_X, mk_Y, mk_W, mk_H;

/* (x, y): the screen's top-left on the current canvas (the main view's), w x h its size; *kx,
 * *ky: the marks' pixels per game pixel.  The canvas to draw them on (pushed and popped by the
 * caller), NULL: nowhere to draw */
grs_canvas *port_map_marks_begin(long x, long y, long w, long h, int *kx, int *ky)
{
    grs_canvas *cv = grd_canvas;
    size_t need;

    mk_under = cv;
    if (port_s3d_paint_at(x, y, &mk_p)) {
        mk_sharp = 1;
        *kx = mk_p.kx;
        *ky = mk_p.ky;
        mk_X = lround(mk_p.X);
        mk_Y = lround(mk_p.Y);
    } else {
        if (cv->bm.type != BMT_FLAT8)
            return NULL;
        mk_sharp = 0;
        *kx = *ky = 1;
        mk_X = x;
        mk_Y = y;
    }
    mk_W = w * *kx;
    mk_H = h * *ky;
    need = (size_t)mk_W * mk_H;
    if (mk_W <= 0 || mk_H <= 0 || mk_W > 0x7FFF || mk_H > 0x7FFF || need > sizeof mk_buf)
        return NULL;
    memset(mk_buf, 0, need);
    gr_init_canvas(&mk_cv, mk_buf, BMT_FLAT8, (short)mk_W, (short)mk_H);
    return &mk_cv;
}

void port_map_marks_end(void)
{
    long X, Y, x0 = mk_W, x1 = -1, y0 = mk_H, y1 = -1, dx0, dy0, dx1, dy1, row;
    uint8_t *dst, *sharp = NULL;
    uint32_t *tc = NULL;

    for (Y = 0; Y < mk_H; Y++) {        /* what was drawn */
        const uint8_t *s = mk_buf + Y * mk_W;
        for (X = 0; X < mk_W; X++)
            if (s[X]) {
                if (X < x0) x0 = X;
                if (X > x1) x1 = X;
                if (Y < y0) y0 = Y;
                y1 = Y;
            }
    }
    if (x1 < 0)
        return;
    if (mk_sharp) {
        dst = mk_p.high;
        row = mk_p.w;
        dx0 = 0; dy0 = 0; dx1 = mk_p.w; dy1 = mk_p.h;
        tc = mk_p.tc;
    } else {
        grs_canvas *cv = mk_under;
        dst = cv->bm.bits;
        row = cv->bm.row;
        dx0 = cv->gc.clip.i.left; dy0 = cv->gc.clip.i.top;
        dx1 = cv->gc.clip.i.right; dy1 = cv->gc.clip.i.bot;
        if (dx1 > cv->bm.w) dx1 = cv->bm.w;
        if (dy1 > cv->bm.h) dy1 = cv->bm.h;
    }
    if (x0 + mk_X < dx0) x0 = dx0 - mk_X;
    if (y0 + mk_Y < dy0) y0 = dy0 - mk_Y;
    if (x1 + mk_X >= dx1) x1 = dx1 - mk_X - 1;
    if (y1 + mk_Y >= dy1) y1 = dy1 - mk_Y - 1;
    if (x1 < x0 || y1 < y0)
        return;
    if (mk_sharp)
        sharp = port_s3d_paint_sharp(&mk_p, (int)(x0 + mk_X), (int)(y0 + mk_Y), (int)(x1 + 1 + mk_X),
                                     (int)(y1 + 1 + mk_Y));
    for (Y = y0; Y <= y1; Y++) {
        const uint8_t *s = mk_buf + Y * mk_W;
        long PY = Y + mk_Y;
        uint8_t *d = dst + PY * row + mk_X;
        uint32_t *tr = tc && mk_p.tc_rows[PY] ? tc + PY * row + mk_X : NULL;
        for (X = x0; X <= x1; X++)
            if (s[X]) {
                d[X] = s[X];
                if (tr)
                    tr[X] = 0;
                if (sharp)
                    sharp[(Y - y0) * (x1 + 1 - x0) + (X - x0)] = 1;
            }
    }
}

/* "Radar without MFD": the main view's picture's pixels per game pixel there, (x, y) on the
 * current canvas; 0: no sharp picture (3D resolution 1x), no radar */
int port_map_radar_k(double x, double y, int *kx, int *ky)
{
    port_s3d_paint p;
    if (!port_s3d_paint_at(x, y, &p) || !tables_ready())
        return 0;
    *kx = p.kx;
    *ky = p.ky;
    return 1;
}

/* a font's (mono or colour) character c, its pixels as k x k blocks of colour col at (X, Y) of
 * the picture (its top-left); (ok: lab marks the pixels sharp) */
static int glyph(const grs_font *f, int c, long X, long Y, int k, uchar col, uint8_t *bits, uint32_t *tc,
                 const uint8_t *tc_rows, long row, long PW, long PH, uint8_t *lab, long lx, long ly, long lw,
                 long lh)
{
    const uchar *base = (const uchar *)f;
    const uchar *buf;
    long i, j, a, b, x0, x1;
    if (!f || c < f->min || c > f->max)
        return 0;
    buf = base + f->buf;
    x0 = f->off_tab[c - f->min];
    x1 = f->off_tab[c - f->min + 1];
    for (j = 0; j < f->h; j++)
        for (i = x0; i < x1; i++) {
            int on = f->id == 0xCCCC ? buf[j * f->w + i] != 0 : (buf[j * f->w + (i >> 3)] >> (7 - (i & 7))) & 1;
            if (!on)
                continue;
            for (b = 0; b < k; b++)
                for (a = 0; a < k; a++) {
                    long PX = X + (i - x0) * k + a, PY = Y + j * k + b;
                    if (PX < 0 || PY < 0 || PX >= PW || PY >= PH)
                        continue;
                    bits[PY * row + PX] = col;
                    if (tc && tc_rows[PY])
                        tc[PY * row + PX] = 0;
                    if (lab && PX >= lx && PY >= ly && PX < lx + lw && PY < ly + lh)
                        lab[(PY - ly) * lw + (PX - lx)] = 1;
                }
        }
    return (int)(x1 - x0);
}

/* the goggles' radar ("Radar without MFD", mapdisp.c port_map_hidden), over the main view's
 * picture where the middle screen's map would be ((x, y): the map's top-left on the current
 * canvas): the map drawn whole at the picture's pixels (port_s3d_radar_map: land, its land alone,
 * pic with its marks, W x H), in a window round on the screen (A: how much taller than wide a
 * picture pixel shows, 1.2 with the game's 4:3) around the player ((px, py): the player's place on
 * it, a pixel's middle at .5), as big as fits the map.  The map inside keeps the game's own
 * proportions, as on the screen.  The land 45% over the
 * view (with "True colour" its full colours mixed, else the nearest palette colour), with "Map
 * view cone" the cone (port_map_cone_view) out to the rim, the map's marks (pic where it is not
 * the land) whole, then the rim in green half a game pixel thick (as the maps' lines), an N above
 * it in the map's font (font: res 503) and, for each of the n contacts (ax, ay on the map as px,
 * py; colour ac) beyond the rim, an arrow outside it pointing in.  The marks, rim, N and arrows
 * are kept sharp by "Anti-aliasing".  cone: the player's view and half its width, as
 * port_map_cone_view's (aspect its rows per column). */
void port_map_radar_draw(double x, double y, const uint8_t *land, const uint8_t *pic, int W, int H,
                         double px, double py, double A, const void *font, int n, const long *ax,
                         const long *ay, const uint8_t *ac, double aspect, ushort view, ushort half)
{
    const grs_font *f = font;
    port_s3d_paint p;
    uint8_t *bits, *lab;
    uint32_t *tc;
    const uint8_t *tc_rows;
    long row, PW, PH, X0, Y0, X, Y, lx0, ly0, lx1, ly1;
    double cx, cy, R, Ri;
    int kx, t, i;
    uchar rim;

    if (!port_s3d_paint_at(x, y, &p) || !tables_ready())
        return;
    bits = p.high;
    tc = p.tc;
    tc_rows = p.tc_rows;
    row = p.w;
    PW = p.w;
    PH = p.h;
    kx = p.kx;
    t = kx / 2 > 1 ? kx / 2 : 1;        /* half a game pixel, at least one: the maps' lines */
    X0 = lround(p.X);
    Y0 = lround(p.Y);
    cx = X0 + px;
    cy = Y0 + py;
    R = floor(fmin(fmin(px, W - px), fmin(py, H - py) * A));   /* in columns; rows: / A */
    if (R < 4 * t || A <= 0)
        return;
    Ri = R - t;                         /* inside the rim */
    rim = nearest(0, 186, 0);
    /* the land, faded in */
    for (Y = (long)floor(cy - Ri / A); Y <= (long)ceil(cy + Ri / A); Y++) {
        const uint8_t *lr;
        uint8_t *d;
        uint32_t *tr;
        long xa, xb;
        if (Y < 0 || Y >= PH || Y - Y0 < 0 || Y - Y0 >= H || !span(cx, cy, A, Ri, Y, &xa, &xb))
            continue;
        if (xa < X0) xa = X0;
        if (xa < 0) xa = 0;
        if (xb > X0 + W - 1) xb = X0 + W - 1;
        if (xb > PW - 1) xb = PW - 1;
        lr = land + (Y - Y0) * W - X0;
        d = bits + Y * row;
        tr = tc && tc_rows[Y] ? tc + Y * row : NULL;
        for (X = xa; X <= xb; X++) {        /* 55% the view, 45% the land */
            const int *l = pal8i[lr[X]];
            if (tr && tr[X]) {
                uint32_t v = tr[X];
                uint32_t r8 = ((v >> 16 & 0xFF) * 141 + l[0] * 115 + 128) >> 8;
                uint32_t g8 = ((v >> 8 & 0xFF) * 141 + l[1] * 115 + 128) >> 8;
                uint32_t b8 = ((v & 0xFF) * 141 + l[2] * 115 + 128) >> 8;
                tr[X] = (v & 0xFF000000u) | r8 << 16 | g8 << 8 | b8;
            } else {
                unsigned k = (unsigned)d[X] << 8 | lr[X];
                if (!mixok[k]) {
                    const int *o = pal8i[d[X]];
                    mixtab[k] = nearest((o[0] * 141 + l[0] * 115) / 256.0f, (o[1] * 141 + l[1] * 115) / 256.0f,
                                        (o[2] * 141 + l[2] * 115) / 256.0f);
                    mixok[k] = 1;
                }
                d[X] = mixtab[k];
            }
        }
    }
    /* the cone, out to the rim (its own far edge beyond the window) */
    if (cone_on()) {
        clip_on = 1;
        clip_cx = cx;
        clip_cy = cy;
        clip_r = Ri;
        clip_a = A;
        port_map_cone_view(x + px / kx, y + py / p.ky, (Ri + 3 * t) / kx, aspect, view, half);
        clip_on = 0;
    }
    /* what stays sharp: the window, the N above it, the arrows around it */
    lx0 = (long)floor(cx - R - 8 * t);
    lx1 = (long)ceil(cx + R + 8 * t);
    ly0 = (long)floor(cy - (R + 8 * t) / A - 8 * t);
    ly1 = (long)ceil(cy + (R + 8 * t) / A);
    if (lx0 < 0) lx0 = 0;
    if (ly0 < 0) ly0 = 0;
    if (lx1 > PW) lx1 = PW;
    if (ly1 > PH) ly1 = PH;
    lab = lx1 > lx0 && ly1 > ly0 ? port_s3d_paint_sharp(&p, (int)lx0, (int)ly0, (int)lx1, (int)ly1) : NULL;
#define INK(X, Y, c) do {                                                                       \
        long X_ = (X), Y_ = (Y);                                                                 \
        if (X_ >= 0 && Y_ >= 0 && X_ < PW && Y_ < PH) {                                          \
            bits[Y_ * row + X_] = (c);                                                           \
            if (tc && tc_rows[Y_])                                                               \
                tc[Y_ * row + X_] = 0;                                                           \
            if (lab && X_ >= lx0 && Y_ >= ly0 && X_ < lx1 && Y_ < ly1)                           \
                lab[(Y_ - ly0) * (lx1 - lx0) + (X_ - lx0)] = 1;                                  \
        }                                                                                        \
    } while (0)
    /* the map's marks and the rim */
    for (Y = (long)floor(cy - R / A); Y <= (long)ceil(cy + R / A); Y++) {
        long xa, xb, ia = 1, ib = 0;
        int inmap = Y - Y0 >= 0 && Y - Y0 < H;
        if (!span(cx, cy, A, R, Y, &xa, &xb))
            continue;
        span(cx, cy, A, Ri, Y, &ia, &ib);
        for (X = xa; X <= xb; X++) {
            long i2 = (Y - Y0) * W + (X - X0);
            if (X < ia || X > ib)
                INK(X, Y, rim);
            else if (inmap && X - X0 >= 0 && X - X0 < W && pic[i2] != land[i2])
                INK(X, Y, pic[i2]);
        }
    }
    /* N above the rim, in the map's font at half a game pixel a pixel */
    if (f && 'N' >= f->min && 'N' <= f->max) {
        long gw = (f->off_tab['N' - f->min + 1] - f->off_tab['N' - f->min]) * t;
        glyph(f, 'N', lround(cx - gw / 2.0), (long)floor(cy - R / A - 2 * t) - f->h * t, t, rim, bits, tc, tc_rows,
              row, PW, PH, lab, lx0, ly0, lx1 - lx0, ly1 - ly0);
    }
    /* the arrows: contacts beyond the rim, at its outside, pointing in */
    for (i = 0; i < n; i++) {
        double dx = X0 + ax[i] + 0.5 - cx, dy = (Y0 + ay[i] + 0.5 - cy) * A, d = sqrt(dx * dx + dy * dy);
        double ux, uy, vx, vy, r0 = R + t, r1 = R + 5 * t, hw = 2.5 * t, x0, y0, x1, y1;
        if (d <= Ri - 2 * t)
            continue;
        ux = dx / d;
        uy = dy / d;
        vx = -uy;
        vy = ux;
        x0 = cx + ux * r0;                          /* the tip just outside the rim, the base r1 */
        y0 = cy + uy * r0;                          /* out, hw each side */
        x1 = cx + ux * r1;
        y1 = cy + uy * r1;
        for (Y = (long)floor(cy + (fmin(y0, y1) - cy - hw - 1) / A);
             Y <= (long)ceil(cy + (fmax(y0, y1) - cy + hw + 1) / A); Y++)
            for (X = (long)floor(fmin(x0, x1) - hw - 1); X <= (long)ceil(fmax(x0, x1) + hw + 1); X++) {
                double qx = X + 0.5 - cx, qy = (Y + 0.5 - cy) * A;
                double along = qx * ux + qy * uy, side = fabs(qx * vx + qy * vy);
                if (along < r0 || along > r1)
                    continue;
                if (side <= hw * (along - r0) / (r1 - r0) + 0.5)
                    INK(X, Y, ac[i]);
            }
    }
#undef INK
}
