/* visor.c -- Freefall: "Visor damage" (Mods page, with Widescreen and a cockpit mod's picture).
 *
 * The game shows the suit's damage by changing pieces of its 4:3 cockpit art (mfddata.c: dents,
 * burnt wiring, a crack on the glass; two stages, by dice once the armour is down to 73%).  A
 * cockpit mod's 16:9 picture stays whole (cockpitpack.c port_ck_whole), so with one the damage is
 * shown on the visor's glass instead (owner): pictures over the main view's 3D, under the helmet's
 * frame and the HUD, the same for the cockpit and the goggles and for the Scout, Standard and
 * Heavy suits (the Pirate suit, worn in one mission, has none).
 *
 *   - Three kinds, for the game's weapons: shrapnel (chips and gouges), a laser's burn trail and
 *     cracks growing in from the visor's edge, each in three styles (owner: so the damage looks
 *     different from mission to mission).  Each picture has a light and a heavy state, the heavy
 *     one the light one grown (port/res/visor, made by tools/visor-damage.py, built into tnfree).
 *   - A mission picks two different kinds, a style of each, one for each side of the visor, and which of the two
 *     turns heavy first.  By the armour's lowest point so far, as the game's own damage goes
 *     (mfddata_26747C: game_44136C of game_44136A): down to 73% the first is there, light; 50%
 *     the second, light; 37% one turns heavy; 20% the other.  It stays for the mission.
 *   - Each step comes with a flash: a short white burst on the new damage, whose pixels are
 *     white-hot for a third of a second.
 *
 * Drawing: the game thread, into the main view's sharp picture after its 3D is drawn (sharp3d.c,
 * where the Arms mod's arm goes in too), so the display lays the frame and the HUD over it as over
 * the 3D, in every one of its paths.  The picture is the game's colours: each pixel is the mix of
 * the picture's colour under it and the damage's by its alpha, then the palette's nearest colour
 * (so the infrared, the palette's flashes and fades take it with them); where the picture has a
 * full colour (True colour) the mix goes there.  The pictures are sized by the screen's height:
 * the same size in the window whatever the 3D resolution, smaller in the cockpit, whose window is
 * half as tall as the goggles' view.
 *
 * All of it is the display's: the game's data and its random numbers are not touched (the picks
 * come from a generator of this file), so lockstep runs and saves are the same with it on or off.
 * FREEFALL_VISOR=<step>[,<first kind>,<second kind>[,<first on the right>[,<first style>,<second style>]]]
 * sets the step (0 to 4;
 * "run": one more every 40 pictures, to see the flashes)
 * and the picks for test pictures (kinds: 0 shrapnel, 1 laser, 2 cracks; styles 0 to 2).
 */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"
#include "platform.h"                   /* port_now_us, port_lockstep */
#include "helmetfx.h"                   /* the helmet's move: port_helmet_sway, port_helmet_get */
#include "fxpack.h"                     /* the palette: port_fx_pal, port_fx_rgb, port_fx_near */
#include "visor.h"

int port_visor = 0;                     /* "Visor damage": 0 Off, 1 On (screen.c) */

extern unsigned short game_44136A, game_44136C;    /* the player's armour: whole, and now (g7_b.h) */
unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp, int req);

#define KINDS 3
#define UNIT 384.0f                     /* the pictures' pixels that are SIZE of the screen's height */
#define SIZE_GOGGLES 0.56f
#define SIZE_COCKPIT 0.44f
#define SWAY_X 7                        /* the helmet's widest move sideways, the game's pixels (platform.c) */
#define FLASH_US 350000                 /* a step's flash */
#define FLASH_FRAMES 9                  /* ... in a lockstep run: by frames */

#define STYLES 3                        /* each kind's pictures to pick from: a, b, c */
#define PIC(k, y) extern const unsigned char port_visor_##k##_##y##_light[], port_visor_##k##_##y##_heavy[]; \
    extern const unsigned port_visor_##k##_##y##_light_len, port_visor_##k##_##y##_heavy_len
PIC(shrapnel, a); PIC(shrapnel, b); PIC(shrapnel, c);
PIC(laser, a); PIC(laser, b); PIC(laser, c);
PIC(cracks, a); PIC(cracks, b); PIC(cracks, c);
#undef PIC

/* a picture (kind * STYLES + style): its two states (RGBA, the same size) */
static struct {
    unsigned char *px[2];
    int w, h, tried;
} pic[KINDS * STYLES];

/* where a kind goes on the left of the visor: its middle, of the view's width and height (the
 * goggles', the cockpit's; mirrored for the right) */
static const struct {
    float gx, gy, cx, cy;
} at[KINDS] = {
    { 0.215f, 0.34f, 0.225f, 0.42f },   /* shrapnel */
    { 0.250f, 0.33f, 0.270f, 0.40f },   /* laser */
    { 0.000f, 0.45f, 0.030f, 0.48f },   /* cracks: gx, cx its left edge */
};

/* a picture at the size it is drawn at: RGBA, the picture's pixels under each of its own averaged
 * (smaller) or the nearest (bigger) */
typedef struct {
    unsigned char *px;
    int w, h, kind, state, flip;
} Scaled;

/* the mission's two pieces */
static struct {
    int kind, right;                    /* the picture (kind * STYLES + style); on the visor's right side (mirrored) */
    Scaled s[2];                        /* light, heavy */
} piece[2];
static int heavy_first;                 /* which of the two turns heavy at 37% */
static int low = -1, whole;             /* the armour's lowest point of the mission, and all of it */
static int step, step_shown = -1;       /* 0 none .. 4 both heavy; the one the last picture showed */
static uint64_t flash_t0;               /* the last step's flash: when it began (0: none) */
static int flash_frames, flash_piece, flash_from;  /* ... its piece, and the state it had before (-1: none) */
static uint32_t rnd_state;

static uint32_t rnd(void)
{
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return rnd_state;
}

static int load(int k)
{
#define PIC(k, y) { port_visor_##k##_##y##_light, port_visor_##k##_##y##_heavy }
    static const unsigned char *const data[KINDS * STYLES][2] = {
        PIC(shrapnel, a), PIC(shrapnel, b), PIC(shrapnel, c), PIC(laser, a), PIC(laser, b), PIC(laser, c),
        PIC(cracks, a), PIC(cracks, b), PIC(cracks, c),
    };
#undef PIC
#define PIC(k, y) { port_visor_##k##_##y##_light_len, port_visor_##k##_##y##_heavy_len }
    const unsigned len[KINDS * STYLES][2] = {
        PIC(shrapnel, a), PIC(shrapnel, b), PIC(shrapnel, c), PIC(laser, a), PIC(laser, b), PIC(laser, c),
        PIC(cracks, a), PIC(cracks, b), PIC(cracks, c),
    };
#undef PIC
    int s, w, h, n;
    if (pic[k].tried)
        return pic[k].px[0] && pic[k].px[1];
    pic[k].tried = 1;
    for (s = 0; s < 2; s++) {
        pic[k].px[s] = stbi_load_from_memory(data[k][s], (int)len[k][s], &w, &h, &n, 4);
        if (!pic[k].px[s] || (s && (w != pic[k].w || h != pic[k].h))) {
            port_log("Visor damage: picture %d/%d not read\n", k, s);
            pic[k].px[0] = pic[k].px[1] = NULL;
            return 0;
        }
        pic[k].w = w;
        pic[k].h = h;
    }
    return 1;
}

/* mfddata.c (game thread): a mission's suit is set up (the game's own damage starts from here
 * too): no damage, the armour's lowest point is what it has now, the mission's picks */
void port_visor_begin(void)
{
    const char *t = SDL_getenv("FREEFALL_VISOR");
    int k0, k1, right, y0, y1;
    whole = game_44136A;
    low = game_44136C;
    step = 0;
    step_shown = -1;
    flash_t0 = 0;
    flash_frames = 0;
    if (!rnd_state)                     /* (a lockstep run: the same picks every run) */
        rnd_state = port_lockstep ? 0x2545F491u : (uint32_t)SDL_GetPerformanceCounter() | 1u;
    k0 = (int)(rnd() >> 8) % KINDS;
    k1 = (k0 + 1 + (int)(rnd() >> 8) % (KINDS - 1)) % KINDS;
    right = (int)(rnd() >> 8) & 1;
    heavy_first = (int)(rnd() >> 8) & 1;
    y0 = (int)(rnd() >> 8) % STYLES;
    y1 = (int)(rnd() >> 8) % STYLES;
    if (t) {                            /* test pictures: the picks as asked */
        int st = 0, a = k0, b = k1, r = right, ya = -1, yb = -1;
        if (sscanf(t, "%d,%d,%d,%d,%d,%d", &st, &a, &b, &r, &ya, &yb) >= 3 && a >= 0 && a < KINDS && b >= 0 && b < KINDS && a != b) {
            k0 = a;
            k1 = b;
            right = r != 0;
            heavy_first = 0;
            if (ya >= 0 && ya < STYLES)
                y0 = ya;
            if (yb >= 0 && yb < STYLES)
                y1 = yb;
        }
    }
    piece[0].kind = k0 * STYLES + y0;
    piece[0].right = right;
    piece[1].kind = k1 * STYLES + y1;
    piece[1].right = !right;
}

/* the step by the armour's lowest point (the game's own limits are 73% and 37%: mfddata_26747C) */
static void update_step(void)
{
    const char *t = SDL_getenv("FREEFALL_VISOR");
    int cur = game_44136C, v = whole, s;
    if (low < 0 || v <= 0)
        return;
    if (cur < low)
        low = cur;
    s = low * 100 <= v * 20 ? 4 : low * 100 <= v * 37 ? 3 : low * 100 <= v * 50 ? 2 : low * 100 <= v * 73 ? 1 : 0;
    if (t && *t == 'r') {               /* "run": a step every 40 pictures (the flashes) */
        static int n;
        s = n++ / 40 > 4 ? 4 : n / 40;
    } else if (t)
        s = atoi(t) < 0 ? 0 : atoi(t) > 4 ? 4 : atoi(t);
    if (s > step || t)
        step = s;
}

/* piece i's state at step s: -1 not there, 0 light, 1 heavy */
static int state_at(int i, int s)
{
    int first = i == 0;
    if (s <= 0 || (!first && s < 2))
        return -1;
    if (s >= 4)
        return 1;
    return s == 3 && i == heavy_first;
}

/* picture k's state st at w x h, mirrored or not: made again when any of that changed */
static const Scaled *scaled(Scaled *sc, int k, int st, int w, int h, int flip)
{
    const unsigned char *src = pic[k].px[st];
    int sw = pic[k].w, sh = pic[k].h, x, y;
    if (sc->px && sc->w == w && sc->h == h && sc->kind == k && sc->state == st && sc->flip == flip)
        return sc;
    free(sc->px);
    sc->px = w > 0 && h > 0 && w <= 8192 && h <= 8192 ? malloc((size_t)w * h * 4) : NULL;
    if (!sc->px)
        return NULL;
    sc->w = w;
    sc->h = h;
    sc->kind = k;
    sc->state = st;
    sc->flip = flip;
    for (y = 0; y < h; y++) {
        int y0 = (int)((long long)y * sh / h), y1 = (int)((long long)(y + 1) * sh / h);
        if (y1 <= y0)
            y1 = y0 + 1;
        for (x = 0; x < w; x++) {
            int X = flip ? w - 1 - x : x, x0 = (int)((long long)X * sw / w), x1 = (int)((long long)(X + 1) * sw / w);
            unsigned long r = 0, g = 0, b = 0, a = 0;
            unsigned char *d = sc->px + ((size_t)y * w + x) * 4;
            int u, v, n;
            if (x1 <= x0)
                x1 = x0 + 1;
            n = (x1 - x0) * (y1 - y0);
            for (v = y0; v < y1; v++)
                for (u = x0; u < x1; u++) {
                    const unsigned char *s = src + ((size_t)v * sw + u) * 4;
                    r += (unsigned long)s[0] * s[3];
                    g += (unsigned long)s[1] * s[3];
                    b += (unsigned long)s[2] * s[3];
                    a += s[3];
                }
            if (a) {
                d[0] = (unsigned char)(r / a);
                d[1] = (unsigned char)(g / a);
                d[2] = (unsigned char)(b / a);
                d[3] = (unsigned char)(a / (unsigned long)n);
            } else
                d[0] = d[1] = d[2] = d[3] = 0;
        }
    }
    return sc;
}

/* the palette's colours, and the nearest colour of a mixed one (5 bits a channel, looked up once
 * a palette) */
static unsigned long pal_key;
static unsigned char pal_rgb[256][3];
static unsigned short near_c[32768];    /* the colour + 1, 0: not looked up yet */

static void pal_ready(void)
{
    unsigned long key = port_fx_pal();
    int c;
    if (key == pal_key && key)
        return;
    pal_key = key;
    memset(near_c, 0, sizeof near_c);
    for (c = 0; c < 256; c++) {
        long r, g, b;
        port_fx_rgb(c, &r, &g, &b);
        pal_rgb[c][0] = (unsigned char)r;
        pal_rgb[c][1] = (unsigned char)g;
        pal_rgb[c][2] = (unsigned char)b;
    }
}

static unsigned char nearest(int r, int g, int b)
{
    int i = (r >> 3) << 10 | (g >> 3) << 5 | b >> 3;
    if (!near_c[i]) {
        unsigned char c = port_fx_near((r & ~7) | 4, (g & ~7) | 4, (b & ~7) | 4);
        if (!c)                         /* (colour 0 is the picture's "nothing here") */
            c = port_fx_near(((r & ~7) | 4) + 8, ((g & ~7) | 4) + 8, ((b & ~7) | 4) + 8);
        near_c[i] = (unsigned short)((c ? c : 1) + 1);
    }
    return (unsigned char)(near_c[i] - 1);
}

/* the picture's pixel (its colour number *hp, its full colour *tp if it has one) mixed with
 * (r, g, b) by a of 255 */
static void mix(uint8_t *hp, uint32_t *tp, int r, int g, int b, int a)
{
    int br, bg, bb;
    if (tp && *tp) {
        br = *tp >> 16 & 0xFF;
        bg = *tp >> 8 & 0xFF;
        bb = *tp & 0xFF;
    } else {
        br = pal_rgb[*hp][0];
        bg = pal_rgb[*hp][1];
        bb = pal_rgb[*hp][2];
    }
    r = br + (r - br) * a / 255;
    g = bg + (g - bg) * a / 255;
    b = bb + (b - bb) * a / 255;
    if (tp && *tp)
        *tp = 0xFE000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    else
        *hp = nearest(r, g, b);
}

void port_visor_draw(uint8_t *high, uint32_t *tc, const uint8_t *tc_rows, int W, int H, int ey, int scr_w,
                     int scr_h, int kx, int ky, int goggles, int suit)
{
    float size = goggles ? SIZE_GOGGLES : SIZE_COCKPIT, glow = 0;
    int i, x, y, sway_x = 0, sway_y = 0;

    if (!port_visor || low < 0 || suit < -1 || suit > 2 || W <= 0 || H <= 0)
        return;
    update_step();
    if (step != step_shown) {           /* a step: its piece's flash */
        if (step_shown >= 0 && step > step_shown) {
            for (i = 0; i < 2; i++)
                if (state_at(i, step) != state_at(i, step_shown)) {
                    flash_piece = i;
                    flash_from = state_at(i, step_shown);
                    flash_t0 = port_now_us() | 1;
                    flash_frames = 0;
                }
        }
        step_shown = step;
    }
    if (!step)
        return;
    if (flash_t0) {
        float t = port_lockstep ? (float)flash_frames++ / FLASH_FRAMES : (float)(port_now_us() - flash_t0) / FLASH_US;
        if (t >= 1)
            flash_t0 = 0;
        else
            glow = (1 - t) * (1 - t);
    }
    /* "Dynamic helmet": the glass is the helmet's, so the damage moves with it as a part at the
     * helmet's own distance does (platform.c hlp_offsets: the same sums, whole pixels of the
     * game's screen, so it steps with the frame's parts) */
    if (port_helmet_sway) {
        port_helmet_state hs;
        const double TX = 1.19175359, Z = 0.125;
        double limy = scr_h <= 200 ? 3.0 : 6.0, a, b;
        port_helmet_get(&hs);
        a = (hs.yaw * 19.2 - hs.ex / Z / TX * 960) / 1920 * ((double)W / kx);
        b = (hs.pitch * 19.2 + hs.ey / Z / TX * 960) / 1080 * scr_h;
        sway_x = (int)floor(SWAY_X * tanh(a / SWAY_X) + 0.5) * kx;
        sway_y = (int)floor(limy * tanh(b / limy) + 0.5) * ky;
    }
    pal_ready();
    for (i = 0; i < 2; i++) {
        int st = state_at(i, step), k = piece[i].kind, dw, dh, X0, Y0, hot = glow > 0 && flash_piece == i;
        const Scaled *sc, *was = NULL;
        float mx, my;
        if (st < 0 || !load(k))
            continue;
        /* the picture's size in the view's pixels: SIZE of the screen's height for UNIT of its
         * pixels, the screen's columns 4:3 of its rows across */
        dh = (int)(size * pic[k].h / UNIT * scr_h * ky + 0.5f);
        dw = (int)(size * pic[k].w / UNIT * 0.75f * scr_w * kx + 0.5f);
        sc = scaled(&piece[i].s[st], k, st, dw, dh, piece[i].right);
        if (!sc)
            continue;
        if (hot && flash_from >= 0)
            was = scaled(&piece[i].s[flash_from], k, flash_from, dw, dh, piece[i].right);
        mx = goggles ? at[k / STYLES].gx : at[k / STYLES].cx;
        my = goggles ? at[k / STYLES].gy : at[k / STYLES].cy;
        /* (the cracks: from the edge, and from beyond it by the helmet's widest move) */
        X0 = k / STYLES == 2 ? (int)(mx * W) - (port_helmet_sway ? SWAY_X * kx : 0) : (int)(mx * W - dw / 2.0f);
        if (piece[i].right)
            X0 = W - X0 - dw;
        Y0 = (int)(my * H - dh / 2.0f);
        X0 += sway_x;
        Y0 += sway_y;
        if (hot) {
            /* the burst: white over the 3D round the middle of the new damage, as wide as it is
             * spread; and the new pixels white-hot */
            double sx = 0, sy = 0, sn = 0, sd = 0;
            int gx0, gy0, gx1, gy1;
            float cx, cy, rad;
            for (y = 0; y < dh; y += 2)
                for (x = 0; x < dw; x += 2) {
                    size_t o = ((size_t)y * dw + x) * 4 + 3;
                    int a = sc->px[o] - (was ? was->px[o] : 0);
                    if (a > 0) {
                        sx += (double)x * a;
                        sy += (double)y * a;
                        sn += a;
                    }
                }
            if (sn > 0) {
                cx = (float)(sx / sn);
                cy = (float)(sy / sn);
                for (y = 0; y < dh; y += 2)
                    for (x = 0; x < dw; x += 2) {
                        size_t o = ((size_t)y * dw + x) * 4 + 3;
                        int a = sc->px[o] - (was ? was->px[o] : 0);
                        if (a > 0)      /* (in rows: the columns are narrower by dw / dh's share) */
                            sd += a * ((double)(x - cx) * (x - cx) * ((double)dh * pic[k].w / ((double)dw * pic[k].h)) *
                                       ((double)dh * pic[k].w / ((double)dw * pic[k].h)) + (double)(y - cy) * (y - cy));
                    }
                rad = (float)sqrt(sd / sn) * 0.8f;      /* the burst's spread, rows */
                if (rad < dh * 0.06f)
                    rad = dh * 0.06f;
                {
                    float ax = (float)dw * pic[k].h / ((float)dh * pic[k].w);    /* columns a row */
                    gx0 = X0 + (int)(cx - 2.6f * rad * ax);
                    gx1 = X0 + (int)(cx + 2.6f * rad * ax) + 1;
                    gy0 = Y0 + (int)(cy - 2.6f * rad);
                    gy1 = Y0 + (int)(cy + 2.6f * rad) + 1;
                    if (gx0 < 0) gx0 = 0;
                    if (gx1 > W) gx1 = W;
                    if (gy0 < -ey) gy0 = -ey;
                    if (gy1 > H + ey) gy1 = H + ey;
                    for (y = gy0; y < gy1; y++) {
                        uint8_t *hrow = high + (ptrdiff_t)y * W;
                        uint32_t *trow = tc && tc_rows[y] ? tc + (ptrdiff_t)y * W : NULL;
                        float dy = (y - Y0 - cy) / rad;
                        for (x = gx0; x < gx1; x++) {
                            float dx = (x - X0 - cx) / (rad * ax), d2 = dx * dx + dy * dy;
                            int a;
                            if (d2 > 6.5f)
                                continue;
                            a = (int)(255 * 0.8f * glow * expf(-d2 * 0.5f));
                            if (a > 3)
                                mix(hrow + x, trow ? trow + x : NULL, 255, 255, 255, a);
                        }
                    }
                }
            }
        }
        for (y = 0; y < dh; y++) {
            int Y = Y0 + y, xa = X0 < 0 ? -X0 : 0, xb = X0 + dw > W ? W - X0 : dw;
            const unsigned char *s = sc->px + (size_t)y * dw * 4, *o = was ? was->px + (size_t)y * dw * 4 : NULL;
            uint8_t *hrow;
            uint32_t *trow;
            if (Y < -ey || Y >= H + ey)
                continue;
            hrow = high + (ptrdiff_t)Y * W;
            trow = tc && tc_rows[Y] ? tc + (ptrdiff_t)Y * W : NULL;
            for (x = xa; x < xb; x++) {
                int a = s[x * 4 + 3], r, g, b;
                if (!a)
                    continue;
                r = s[x * 4];
                g = s[x * 4 + 1];
                b = s[x * 4 + 2];
                if (hot && (!o || o[x * 4 + 3] < a)) {      /* new with this step */
                    r += (int)((255 - r) * glow);
                    g += (int)((255 - g) * glow);
                    b += (int)((255 - b) * glow);
                    a += (int)((255 - a) * glow * 0.6f);
                }
                mix(hrow + X0 + x, trow ? trow + X0 + x : NULL, r, g, b, a);
            }
        }
    }
}
