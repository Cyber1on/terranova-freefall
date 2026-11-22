/* infrared.c -- Freefall: "Infrared: Sci-fi" on the Detail page, a thermal camera's look for the
 * game's infrared view (the I key).
 *
 * The game's infrared (helmet.c, game_35ADE0 == 2) draws the 3D through the planet's colour
 * tables (planet_tab3b, tab3c..3e for the lit objects) into its 55 colours 0xB0..0xE6, which it
 * sets to a ramp from white (0xB0, the hottest) through yellow, orange and brown to black (0xE6,
 * the coldest).  The ground and the sky sit at the cold end (about 0xD3..0xE0), the units, fires
 * and shots in the warm part.  With Sci-fi:
 *  - thermal colours: the display shows those 55 colours as a thermal camera's, dark blue for
 *    the cold ground through purple, red, orange and yellow to white for the hottest; only the
 *    palette, so at every 3D resolution and on every renderer (platform.c, port_ir_palette);
 *  - hot outlines and the sensor look: the sharp view's picture (sharp3d.c draws it at 3D
 *    resolution Original too while the infrared is on) gets a glowing ring around the hot
 *    things, faint lines, a moving grain and a slow sweep down the view, all as steps along the
 *    same ramp (port_ir_sensor).
 * The game's own picture, and everything the game reads, stay the original's. */
#include <string.h>
#include "g2_c.h"
#include "g5_b.h"
#include "g5_c.h"
#include "g9_b.h"
#include "support.h"
#include "port.h"
#include "sharp3d.h"

#define IR_HOT 0xB0                     /* the ramp: the hottest colour ... */
#define IR_COLD 0xE6                    /* ... the coldest */
#define IR_WARM 0xCF                    /* this or hotter: a hot thing (outlined) */
#define IR_GLOW 0xB6                    /* the outline's colour (pale yellow) */
#define RING_MAX 4                      /* the outline's width, at most (picture pixels) */
#define COLS_MAX 4096

int port_ir_style;                      /* "Infrared": 0 Original, 1 Sci-fi */

/* the game's infrared ramp in the palette, in a mission (P74: the game leaves game_35ADE0 at 2
 * after a mission ended in infrared, until the next mission's HudInit, and the options screen in
 * a mission puts up colours of its own over the ramp) */
static int ramp_set(void)
{
    return game_in_mission && grd_pal && !memcmp(grd_pal + 0xB0 * 3, planet_pal_part, sizeof planet_pal_part);
}

/* Sci-fi on and the game drawing its infrared */
int port_ir_on(void)
{
    return port_ir_style && port_ir_showing();
}

/* the game drawing its infrared (Original or Sci-fi): the arms (arms.c) go into its ramp */
int port_ir_showing(void)
{
    return game_35ADE0 == 2 && ramp_set();
}

/* Sci-fi on and an infrared picture on the screen, for the display's thermal colours: also
 * while the game draws the picture that switches its infrared off (game_35ADE0 3), when the
 * last infrared picture still shows and the ramp is still in the palette (P147) */
int port_ir_screen(void)
{
    return port_ir_style && (game_35ADE0 == 2 || game_35ADE0 == 3) && ramp_set();
}

/* the game's infrared off, or the picture that switches it on not drawn yet: the display's
 * tuning in (platform.c, ir_tune) starts only at a switch from here or to here, not when the
 * thermal colours go and come back with the options screen, or go at a mission's end */
int port_ir_idle(void)
{
    return game_35ADE0 == 0 || game_35ADE0 == 1;
}

/* the game is drawing the one picture before its infrared goes on or off: the palette's planet
 * colours 0xB0..0xE6 are swapped (for the ramp, or back) while that picture is on the screen */
int port_ir_switching(void)
{
    return game_35ADE0 == 1 || game_35ADE0 == 3;
}

/* The game draws one picture through the planet's tint table (planet_tab3a: the cockpit's
 * oranges 0x7A..0x81) each time its infrared goes on or off.  Those colours are the same under
 * both palettes, and the palette changes while that picture is on the screen.  With Sci-fi the
 * picture goes through this table instead: the same tint in the cockpit's blues (0x30 the
 * palest), so the thermal view comes and goes with a wash of its own cold colours and not with
 * one picture in the old infrared's orange (P147). */
#define WASH_PALE 0x30
#define WASH_DEEP 0x3A
#define WASH_FROM 5                     /* the tint's lightest: this far down the blues */
static uchar wash[2][256];              /* the tint's table; the clouds' (planet_tab3f) */

static void wash_of(uchar *to, const uchar *tab)
{
    int first = 0x100 - planet_pal_count, c, k;
    for (c = 0; c < 256; c++) {
        /* the tint's colour by day (the planet's, where the ramp may be over it) */
        int t = tab[c];
        const uchar *rgb = t >= first ? planet_pal + (t - first) * 3 : grd_pal + t * 3;
        if (t == c) {                   /* a colour the tint leaves alone */
            to[c] = (uchar)c;
            continue;
        }
        k = WASH_FROM + (240 - (30 * rgb[0] + 59 * rgb[1] + 11 * rgb[2]) / 100) / 24;
        to[c] = (uchar)(WASH_PALE + (k < 0 ? 0 : k > WASH_DEEP - WASH_PALE ? WASH_DEEP - WASH_PALE : k));
    }
}

/* the table for the switch-over picture (hud_26B8F8, src/game/hud/hudmain.c) */
uchar *port_ir_wash(uchar *tab)
{
    int c;
    if (!port_ir_style || !tab || !grd_pal)
        return tab;
    for (c = WASH_PALE; c <= WASH_DEEP; c++)    /* no blues there (a changed palette): the game's */
        if (grd_pal[c * 3 + 2] <= grd_pal[c * 3] || grd_pal[c * 3 + 2] <= grd_pal[c * 3 + 1])
            return tab;
    wash_of(wash[0], tab);
    wash_of(wash[1], planet_tab3f);
    return wash[0];
}

/* the clouds' and the mirrored sky's table tab under the canvas's table parm (rndsky.c): in the
 * blues too when that is the switch-over picture's */
uchar *port_ir_wash_sky(long parm, uchar *tab)
{
    return parm == (long)wash[0] && tab == planet_tab3f ? wash[1] : tab;
}

/* a colour (r, g, b) in the infrared's ramp at heat t0 .. t1 by its brightness (0 the coldest,
 * 1 the hottest; IR_WARM's heat, about 0.43, and above are hot things, outlined by Sci-fi) */
int port_ir_heat(int r, int g, int b, float t0, float t1)
{
    float l = (0.30f * r + 0.59f * g + 0.11f * b) / 255.0f, t = t0 + (t1 - t0) * l;
    int c = IR_COLD - (int)(t * (IR_COLD - IR_HOT) + 0.5f);
    return c < IR_HOT ? IR_HOT : c > IR_COLD ? IR_COLD : c;
}

/* the thermal colours, from the coldest (t 0) to the hottest (t 1) */
static const struct {
    float t;
    uint8_t r, g, b;
} stops[] = {
    { 0.00f, 4, 4, 18 },      { 0.10f, 12, 18, 62 },    { 0.18f, 24, 40, 116 },
    { 0.26f, 48, 52, 150 },   { 0.34f, 86, 48, 160 },   { 0.40f, 150, 40, 140 },
    { 0.46f, 220, 60, 70 },   { 0.55f, 250, 122, 30 },  { 0.68f, 255, 182, 32 },
    { 0.82f, 255, 232, 104 }, { 1.00f, 255, 255, 245 },
};

/* the display's colours pal (0xAARRGGBB) with the ramp as a thermal camera's */
void port_ir_palette(uint32_t *pal)
{
    int i, k, n = (int)(sizeof stops / sizeof stops[0]);
    for (i = IR_HOT; i <= IR_COLD; i++) {
        float t = (float)(IR_COLD - i) / (IR_COLD - IR_HOT), f;
        uint32_t r, g, b;
        for (k = 1; k < n - 1 && stops[k].t < t; k++)
            ;
        f = (t - stops[k - 1].t) / (stops[k].t - stops[k - 1].t);
        r = (uint32_t)(stops[k - 1].r + (stops[k].r - stops[k - 1].r) * f + 0.5f);
        g = (uint32_t)(stops[k - 1].g + (stops[k].g - stops[k - 1].g) * f + 0.5f);
        b = (uint32_t)(stops[k - 1].b + (stops[k].b - stops[k - 1].b) * f + 0.5f);
        pal[i] = 0xFF000000u | r << 16 | g << 8 | b;
    }
}

/* an object's colour table in the sharp view while the game's infrared shows (drawobj.c): with
 * Sci-fi a tree or a bush (class 0) no warmer than the cool end of the ground's colours, so the
 * few warm pixels of its picture are not outlined as hot things; then the fog of its place (fog.c) */
#define COOL_MAX 0xD2                   /* the trees' and the land's warmest */
#define COOLS 4
const uchar *port_ir_obj_clut(const void *ent, const uchar *clut)
{
    static uchar src[COOLS][256], tab[COOLS][256];
    static const uchar *from[COOLS];
    static int next;
    const GameEntity *e = ent;
    int i, c;
    if (port_ir_on() && e && e->cls == 0 && clut) {
        for (i = 0; i < COOLS && !(from[i] == clut && !memcmp(src[i], clut, 256)); i++)
            ;
        if (i == COOLS) {               /* new (or changed: another planet's) */
            i = next;
            next = (next + 1) % COOLS;
            from[i] = clut;
            memcpy(src[i], clut, 256);
            for (c = 0; c < 256; c++)
                tab[i][c] = clut[c] >= IR_HOT && clut[c] < COOL_MAX ? COOL_MAX : clut[c];
        }
        clut = tab[i];
    }
    return port_fog_clut(clut, port_fog_obj);
}

/* the land's colour table (the camera's) in the sharp view while Sci-fi shows: the ground no
 * warmer than its cool end either.  A few colours of some planets' textures come out hot through
 * the game's table (in the original too: a texel of the far hills drawn white), single hot pixels
 * that the outline turned into bright specks on the far land.  The caster's columns (haze.c), the
 * squares (weather.c) and the fog's rows (fog.c) */
const uchar *port_ir_land_clut(const uchar *clut)
{
    static uchar src[256], tab[256];
    static const uchar *from;
    int c;
    if (!port_ir_on() || !clut)
        return clut;
    if (from != clut || memcmp(src, clut, 256)) {
        from = clut;
        memcpy(src, clut, 256);
        for (c = 0; c < 256; c++)
            tab[c] = clut[c] >= IR_HOT && clut[c] < COOL_MAX ? COOL_MAX : clut[c];
    }
    return tab;
}

static int warm(uint8_t c)
{
    return c >= IR_HOT && c <= IR_WARM;
}

/* The sharp view's picture p (W x H, kx x ky of its pixels to each of the game's) while the
 * infrared is on.  Row by row: the outline where a cold pixel is within r of a hot one (r = a
 * third of a game pixel; a ring of 2r + 1 rows of the hot pixels widened by r), then the steps:
 * a line at the bottom third of each of the game's rows a little colder, the grain (cells of half a
 * game pixel, a step or two either way, new each frame), and the sweep, a faint bright line going
 * down the view every 5 seconds with a short fading trail above it (three steps at most: more
 * looked like a different land above it). */
void port_ir_sensor(uint8_t *p, int W, int H, int kx, int ky)
{
    static uint8_t ring[2 * RING_MAX + 1][COLS_MAX];
    static uint16_t cnt[COLS_MAX];
    static uint8_t rows_hot[2 * RING_MAX + 1];
    static int8_t grain[256];
    static uint32_t seed = 0x2545F491u, frame;
    static int grain_made;
    int r = kx / 3 < 1 ? 1 : kx / 3 > RING_MAX ? RING_MAX : kx / 3, R = 2 * r + 1;
    int lp = kx < 2 ? 2 : kx, ld = lp / 3 < 1 ? 1 : lp / 3;     /* the lines: period, rows */
    int gs = kx / 2 < 1 ? 1 : kx / 2;   /* the grain's cell */
    int trail = H / 10 < 4 ? 4 : H / 10, lw = gs;   /* the sweep: its trail, its line, its row now */
    long sweep = (long)(tn_ticks % 1250) * (H + trail) / 1250;
    int y, x, hot_in = 0;

    (void)ky;
    if (W <= 0 || W > COLS_MAX || H <= 0)
        return;
    if (!grain_made) {                  /* -2, -1, +1, +2 a twelfth each, else 0 */
        uint32_t s = 12345;
        for (x = 0; x < 256; x++) {
            int v;
            s = s * 1103515245u + 12345u;
            v = (int)(s >> 16) % 12;
            grain[x] = (int8_t)(v == 0 ? -2 : v == 1 ? -1 : v == 2 ? 1 : v == 3 ? 2 : 0);
        }
        grain_made = 1;
    }
    frame++;
    memset(cnt, 0, (size_t)W * sizeof cnt[0]);
    memset(rows_hot, 0, sizeof rows_hot);
    for (y = 0; y < H + r; y++) {
        int c = y - r, slot = y % R;
        if (rows_hot[slot]) {           /* the row leaving the ring */
            for (x = 0; x < W; x++)
                cnt[x] -= ring[slot][x];
            hot_in--;
        }
        rows_hot[slot] = 0;
        if (y < H) {                    /* the hot pixels of row y, widened by r */
            const uint8_t *s = p + (size_t)y * W;
            int n = 0, any = 0;
            for (x = 0; x < r && x < W; x++)
                n += warm(s[x]);
            for (x = 0; x < W; x++) {
                if (x + r < W)
                    n += warm(s[x + r]);
                ring[slot][x] = n > 0;
                any |= n;
                if (x - r >= 0)
                    n -= warm(s[x - r]);
            }
            if (any) {
                for (x = 0; x < W; x++)
                    cnt[x] += ring[slot][x];
                rows_hot[slot] = 1;
                hot_in++;
            }
        }
        if (c < 0)
            continue;
        {
            uint8_t *d = p + (size_t)c * W;
            long ds = sweep - c;        /* rows above the sweep's line */
            int step = (c % lp >= lp - ld ? 2 : 0) -
                       (ds >= 0 && ds < lw ? 3 : ds >= lw && ds < trail ? (int)(3 * (trail - ds) / trail) : 0);
            uint32_t o;
            if (hot_in)
                for (x = 0; x < W; x++)
                    if (cnt[x] && d[x] > IR_WARM && d[x] <= IR_COLD)
                        d[x] = IR_GLOW;
            seed = seed * 1664525u + 1013904223u + frame;
            o = (seed >> 8) + (uint32_t)(c / gs) * 0x9E3779B1u;
            o ^= o >> 15;
            for (x = 0; x < W; x++) {
                int v = d[x];
                if (v < IR_HOT || v > IR_COLD)
                    continue;
                v += step + grain[(uint8_t)(x / gs + o)];
                d[x] = (uint8_t)(v < IR_HOT ? IR_HOT : v > IR_COLD ? IR_COLD : v);
            }
        }
    }
}
