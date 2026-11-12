/* help.c -- Freefall's help screen (H or ? in a mission).
 *
 * With the Classic preset's settings H shows the game's own help screen (RESGAME.RES 0x187,
 * options.c options_2CCC28).  With any other settings the game still opens its help screen (it
 * pauses the mission and closes it on a key or a click, as before), but the display draws
 * Freefall's over it: the player's own HUD as it was when H was pressed, with callouts to its
 * parts, and the controls of the current controls style (Vanilla or Advanced) in five lists.
 * Every key shown is the player's own binding (keys.c), so a key changed on the Keys page shows
 * here too.  H on Freefall's screen shows the game's own; any other key or a click closes it.
 *
 * The picture: when the game opens its help screen (game thread, port_help_open) it waits, up
 * to a moment, for the display (main thread, port_help_present) to read the window back while
 * the mission's screen is still up.  The callouts' places are the HUD parts' places on the
 * game's 320 x 400 screen for each suit's cockpit and goggles (measured in the four suits'
 * first missions: 1 Scout, 3 Standard, 9 Heavy, 39 Pirate), carried through the way the screen
 * was laid out in the window (widescreen, cockpit packs).  A part a suit does not have (the
 * squad in the Pirate's suit, most of the HUD in the goggles) gets its label without a line.
 *
 * The screen is drawn at 1920 x 1080 design pixels scaled to the window, on the CPU (fonts:
 * Liberation Sans, built into tnfree, stb_truetype; the title is Freefall's logo line, as on the
 * options screen), in two pictures: everything but the HUD picture's own parts (made again only
 * when the window's size, the controls style or the bindings change), and over the HUD picture
 * its frame, the callouts' lines and the view's box (made again only when the picture's place or
 * the suit changes too).  The first is drawn ahead, on a thread of its own, while the menus or
 * the mission are up, so opening the screen only reads the window back (P117). */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "keys.h"
#include "cockpitpack.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"

extern const unsigned char port_font_sans[], port_font_sans_bold[];
extern const unsigned port_font_sans_len, port_font_sans_bold_len;
extern int port_lockstep, port_mouselook, port_bench;
int port_help_wanted(void);                     /* options.c */
void port_save_settings(void);                  /* main.c */

/* ------------------------------------------------------------------ the hint at a mission's start */
/* New players do not know the two keys that explain everything else, so a line on the main view
 * names them for the first seconds of a mission (helmet.c draws it, in the gimbal label's font):
 * with Freefall's own help screen in force (not the Classic preset), until the help screen has
 * been opened once (help_seen in tnfree.ini), and not in lockstep runs or the benchmark. */
int port_help_seen;                             /* the help screen was opened once (main.c: the ini) */
static Uint64 hint_from;                        /* the mission started then (0: none) */
#define HINT_MS 12000

void port_hint_mission(void)
{
    hint_from = SDL_GetTicks() + 1;
}

int port_hint_show(void)
{
    if (port_help_seen || !hint_from || port_lockstep || port_bench || !port_help_wanted())
        return 0;
    if (SDL_GetTicks() - hint_from > HINT_MS) {
        hint_from = 0;
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ state */
static SDL_AtomicInt snap_req, snap_done;       /* game thread asks, the display reads back */
static volatile int help_up;                    /* the game's help screen is up */
static volatile int help_new;                   /* ... with Freefall's drawn over it */
static volatile int snap_part = -2, snap_suit = -1;     /* the cockpit (0) or goggles (1), suit */
static SDL_Surface *snap;                       /* main thread: the window as it was */
static SDL_Texture *snap_tex;
static float snap_f0, snap_fw;                  /* the game's own picture within it (fractions) */
static int snap_stretched;                      /* the HUD stretched to all of it (Widescreen) */
static int snap_style;                          /* the controls style when opened */
static SDL_Texture *base_tex;                   /* the help screen under the picture */
static int base_w, base_h, base_style = -1;
static unsigned base_keys;                      /* the bindings it was drawn with */
static SDL_Thread *base_thr;                    /* drawing it ahead (main thread starts, waits) */
static SDL_AtomicInt base_thr_done;
static uint8_t *base_px;                        /* ... into this, not shown yet */
static int base_px_w, base_px_h, base_px_style;
static float base_px_k;
static unsigned base_px_keys;
static SDL_Texture *pic_tex;                    /* what goes over the picture */
static SDL_Rect pic_area;                       /* ... its place in the screen's window pixels */
typedef struct { int cw, style, part, suit, stretched; unsigned keys; float pw, ph, f0, fw; } PicKey;
static PicKey pic_key;                          /* ... and what it was drawn for */

/* ------------------------------------------------------------------ the HUD's parts */
enum { E_COMPASS, E_BARS, E_ZOOM, E_JUMP, E_GOGGLES, E_INFRA, E_RETICLE, E_TARGET, E_ARMOR,
       E_SQ1, E_SQ2, E_SQ3, E_SQALL, E_DROP, E_WEAPONS, E_ASF, E_DRONE, E_MAP, E_COUNT };
typedef struct { short x, y; } Pt;              /* on the game's 320 x 400 screen; x < 0: none */
#define NO { -1, -1 }
static const Pt cockpit_at[4][E_COUNT] = {
    /* Scout */
    { {97, 15}, {120, 31}, {55, 28}, {272, 30}, {25, 110}, {40, 180}, {160, 205}, {165, 192},
      {232, 205}, {42, 285}, {42, 310}, {42, 335}, {80, 365}, {22, 365}, {250, 300}, {237, 384},
      {262, 384}, {160, 320} },
    /* Standard */
    { {97, 12}, {120, 31}, {52, 40}, {272, 34}, {30, 110}, {40, 180}, {160, 205}, {165, 192},
      {233, 205}, {40, 293}, {40, 318}, {40, 345}, {80, 367}, {20, 372}, {255, 305}, {237, 386},
      {262, 386}, {170, 330} },
    /* Heavy */
    { {97, 15}, {120, 31}, {52, 38}, {272, 34}, {30, 110}, {40, 170}, {160, 205}, {165, 192},
      {232, 205}, {30, 293}, {30, 320}, {30, 348}, {80, 372}, {10, 362}, {255, 305}, {260, 378},
      {250, 392}, {160, 320} },
    /* Pirate: no squad, no zoom or jump gauges, the compass in the view */
    { {110, 122}, {120, 10}, NO, NO, {60, 150}, {70, 200}, {160, 205}, {165, 192}, {233, 160},
      NO, NO, NO, NO, {298, 208}, {275, 300}, NO, {300, 378}, {160, 320} },
};
static const Pt goggles_at[2][E_COUNT] = {
    /* Scout, Standard and Heavy: the gauges along the top, the weapon at the bottom */
    { {110, 30}, {120, 8}, {58, 10}, {270, 5}, {30, 120}, {40, 230}, {160, 205}, {165, 192},
      {247, 357}, NO, NO, NO, NO, NO, {160, 372}, NO, NO, NO },
    /* Pirate */
    { {110, 125}, {120, 10}, NO, NO, {60, 160}, {70, 250}, {160, 205}, {165, 192}, {240, 357},
      NO, NO, NO, NO, NO, {160, 42}, NO, NO, NO },
};

/* ------------------------------------------------------------------ what it says */
/* A line: segments split by ';', each "keys|text".  In keys: {action} the action's first key,
 * {action:1} / {action:2} its first / second slot, [Label] a key of the game's own, <L> <R> <M>
 * <W> <X> the mouse's left, right, middle button, wheel and moving it, ~ "–", + "+", / "/". */
typedef struct { int y, e; const char *adv, *van; } Callout;   /* van NULL: as adv */
static const Callout left_co[] = {
    { 100, E_COMPASS, "|Compass", NULL },
    { 160, E_BARS, "|Health / energy", NULL },
    { 220, E_ZOOM, "{zoom_in:2}{zoom_in:1}{zoom_out:1}|HUD zoom", "{zoom_in:1}{zoom_out:1}|HUD zoom" },
    { 300, E_GOGGLES, "{helmet}|Full-screen goggles", NULL },
    { 370, E_INFRA, "{infrared}|Infrared", NULL },
    { 470, E_SQ1, "{squad1}|Talk to squadmate", NULL },
    { 515, E_SQ2, "{squad2}|", NULL },
    { 560, E_SQ3, "{squad3}|", NULL },
    { 615, E_SQALL, "{squad_all}|Talk to the squad", NULL },
    { 670, E_DROP, "{command}|Call dropship", NULL },
};
static const Callout right_co[] = {
    { 100, E_JUMP, "{jump}|Jump jets", NULL },
    { 175, E_TARGET, "<R>|Lock target;{target_prev}{target_next}|Cycle", NULL },
    { 250, E_RETICLE, "<L>|Fire at the reticle", NULL },
    { 330, E_ARMOR, "|Armor indicator", NULL },
    { 440, E_WEAPONS, "{weapon1}~{weapon4}|Cycle weapons", NULL },
    { 560, E_ASF, "{fire}|Use ASF", NULL },
    { 615, E_DRONE, "{drone}|Drone (hold)", NULL },
    { 670, E_MAP, "{map}|Map to HUD;{map_out}{map_in}|Map zoom", NULL },
};
static const char *under_row =
    "{view360}|360-degree view;{nav}|Recenter map;{mfd}|MFDs in full-screen;"
    "{view_9}{view_0}|View modes;{map_squad}~{map_mission}|Map info";
static const char *view_box[2] = {
    "{look_up}{look_down}|Look up/down \xC2\xB7;{head_left}{head_right}|Look left/right \xC2\xB7;"
    "{look_ahead}|Center",
    "<X>|Mouse: look around \xC2\xB7;{gimbal}|Gimbal mode \xC2\xB7;{look_ahead}|Align body",
};

typedef struct { const char *title; const char *rows[2][7]; } List;    /* rows[style] */
static const List lists[5] = {
    { "MOVE", {
        { "{forward}{back}|Forward / back", "{turn_left}{turn_right}|Turn left / right",
          "{strafe_left}{strafe_right}|Sidestep left / right", "{forward_slow}|Forward at half speed",
          "{jump}|Jump jets", "{bound}|Short bound (hold)" },
        { "{forward}{back}|Forward / back", "{turn_left}{turn_right}|Turn left / right",
          "{strafe_left}{strafe_right}|Strafe left / right", "{forward_slow}|Forward at half speed",
          "{jump}|Jump jets", "{strafe_mod}+[\xE2\x86\x90][\xE2\x86\x92]|Strafe with the arrows" } } },
    { NULL, {
        { "{look_up}{look_down}|Look up / down", "{head_left}{head_right}|Look left / right",
          "{look_ahead}|Center view", "{zoom_in:1}{zoom_out:1}|HUD zoom" },
        { "<X>|Look around (mouselook)", "{look_ahead}|Align body to head view",
          "{gimbal}|Gimbal mode on / off", "{mouselook}|Free the pointer / look again",
          "{mouselook_hold}|Free the pointer (hold)", "{zoom_in:2}{zoom_in:1}{zoom_out:1}|Zoom in / out" } } },
    { "FIGHT", {
        { "<L>|Fire weapon", "<R>|Lock target", "{weapon1}~{weapon4}|Weapon select", "{fire}|Use ASF",
          "{target_prev}{target_next}|Previous / next target", "{objectives}|Mission objectives" },
        { "<L>|Fire weapon", "<R>|Lock target", "{weapon1}~{weapon4}|Weapon select", "{fire}|Use ASF",
          "{target_prev}{target_next}|Previous / next target", "{objectives}|Mission objectives" } } },
    { "SQUAD", {
        { "{squad1}~{squad3}|Talk to squadmate", "{squad_all}|Talk to the squad",
          "{drone}<L>|Quick comm menu", "{drone}<R>|Attack this", "{drone}|Drone control (hold)",
          "{command}|Call dropship" },
        { "{squad1}~{squad3}|Talk to squadmate", "{squad_all}|Talk to the squad",
          "{drone}<L>|Quick comm menu", "{drone}<R>|Attack this", "{drone}|Drone control (hold)",
          "{command}|Call dropship" } } },
    { "GAME", {
        { "{pause}|Pause", "{mfd}|MFDs in full-screen", "[Alt+O]|Game options",
          "[Esc][O]|Freefall options", "[Alt+Enter]|Window / full screen", "[Ctrl+Esc]|Abort mission" },
        { "{pause}|Pause", "{mfd}|MFDs in full-screen", "[Alt+O]|Game options",
          "[Esc][O]|Freefall options", "[Alt+Enter]|Window / full screen", "[Ctrl+Esc]|Abort mission" } } },
};
static const short list_x[5] = { 40, 408, 786, 1134, 1502 };

/* ------------------------------------------------------------------ the game thread's side */
/* options_2CCC28: the game opens its help screen */
void port_help_open(void)
{
    int i;
    help_new = 0;
    if (!port_help_wanted()) {
        help_up = 1;
        return;
    }
    if (!port_help_seen) {              /* no more hint at the missions' start */
        port_help_seen = 1;
        port_save_settings();
    }
    snap_part = port_ck_goggles_suit() > -2 ? 1 : port_ck_view_suit() > -2 ? 0 : -2;
    snap_suit = port_ck_view_suit();
    snap_style = port_mouselook ? 1 : 0;
    SDL_SetAtomicInt(&snap_done, 0);
    SDL_SetAtomicInt(&snap_req, 1);
    for (i = 0; i < 300 && !SDL_GetAtomicInt(&snap_done); i++)   /* the display reads it back */
        SDL_Delay(1);
    SDL_SetAtomicInt(&snap_req, 0);
    help_new = 1;
    help_up = 1;
}

/* sharp3d.c: the help screen is up (the display does not stretch it for widescreen) */
int port_help_screen_up(void)
{
    return help_up;
}

/* options_2CCB58: it closes */
void port_help_close(void)
{

    help_up = 0;
    help_new = 0;
}

/* options_2CCB38: a key went down on the help screen (the press that opened it is not seen
 * here, and the game's keyboard handler drops a held key's repeats).  H on Freefall's shows the
 * game's own; 1 = taken */
int port_help_key(int code)
{
    int c = code & 0xFF;
    if (!help_up || !help_new || (c != 'h' && c != 'H'))
        return 0;
    help_new = 0;
    return 1;
}

/* ------------------------------------------------------------------ drawing on the CPU */
typedef struct { uint8_t *px; int w, h, ox, oy; float k; } Canvas;    /* RGBA, k = window px / design px,
                                                                         * (ox, oy) its top left there */
static stbtt_fontinfo font[2];
static int fonts_ok = -1;

static void blend(Canvas *c, int x, int y, uint32_t rgb, float a)
{
    uint8_t *p;
    float ia;
    x -= c->ox;
    y -= c->oy;
    if (x < 0 || y < 0 || x >= c->w || y >= c->h || a <= 0)
        return;
    if (a > 1)
        a = 1;
    p = c->px + ((size_t)y * c->w + x) * 4;
    if (p[3] == 0 || a == 1) {          /* over nothing, or covering it: the colour as it is */
        p[0] = (uint8_t)(rgb >> 16);
        p[1] = (uint8_t)(rgb >> 8);
        p[2] = (uint8_t)rgb;
        p[3] = (uint8_t)(a * 255 + .5f);
        return;
    }
    {
        float da = p[3] / 255.f, oa = a + da * (1 - a);
        float r = (rgb >> 16 & 255), g = (rgb >> 8 & 255), b = (rgb & 255);
        if (oa <= 0)
            return;
        ia = da * (1 - a);
        p[0] = (uint8_t)((r * a + p[0] * ia) / oa + .5f);
        p[1] = (uint8_t)((g * a + p[1] * ia) / oa + .5f);
        p[2] = (uint8_t)((b * a + p[2] * ia) / oa + .5f);
        p[3] = (uint8_t)(oa * 255 + .5f);
    }
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    int i;
    uint32_t o = 0;
    for (i = 0; i < 24; i += 8) {
        float va = (float)(a >> i & 255), vb = (float)(b >> i & 255);
        o |= (uint32_t)(va + (vb - va) * t + .5f) << i;
    }
    return o;
}

/* a rounded box's signed distance (design pixels scaled: all in window pixels) */
static float sd_box(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    float qx = fabsf(px - cx) - hw + r, qy = fabsf(py - cy) - hh + r;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

/* a rounded box in design pixels: fill top colour to bottom colour, a border, alpha */
static void rbox(Canvas *c, float x, float y, float w, float h, float r, uint32_t top, uint32_t bot,
                 uint32_t border, float bw, float alpha)
{
    float k = c->k, X = x * k, Y = y * k, W = w * k, H = h * k, R = r * k, B = bw * k;
    int x0 = (int)floorf(X) - 1, x1 = (int)ceilf(X + W) + 1, y0 = (int)floorf(Y) - 1, y1 = (int)ceilf(Y + H) + 1;
    int i, j;
    for (j = y0; j <= y1; j++)
        for (i = x0; i <= x1; i++) {
            float d = sd_box(i + .5f, j + .5f, X + W / 2, Y + H / 2, W / 2, H / 2, R);
            float cov = .5f - d;
            if (cov <= 0)
                continue;
            if (cov > 1)
                cov = 1;
            if (B > 0 && d > -B)
                blend(c, i, j, border, cov * alpha);
            else
                blend(c, i, j, mix(top, bot, H > 0 ? (j + .5f - Y) / H : 0), cov * alpha);
        }
}

static void line_px(Canvas *c, int i, int j, float AX, float AY, float dx, float dy, float l2, float hw,
                    uint32_t col)
{
    float px = i + .5f - AX, py = j + .5f - AY, t = l2 > 0 ? (px * dx + py * dy) / l2 : 0, d;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    px -= t * dx;
    py -= t * dy;
    d = sqrtf(px * px + py * py);
    blend(c, i, j, col, hw + .5f - d);
}

/* a line with round ends; only the pixels near it are looked at: along its longer axis, in each
 * column (row) a band around the line's place there, as wide as a pixel the line touches can be
 * from it (the line's slope is at most 1 that way: twice the half width and a pixel) */
static void line(Canvas *c, float ax, float ay, float bx, float by, float wd, uint32_t col)
{
    float k = c->k, AX = ax * k, AY = ay * k, BX = bx * k, BY = by * k, hw = wd * k / 2;
    float dx = BX - AX, dy = BY - AY, l2 = dx * dx + dy * dy, band = 2 * (hw + .5f) + 1;
    int along_x = fabsf(dx) >= fabsf(dy);
    float a0 = along_x ? (AX < BX ? AX : BX) : (AY < BY ? AY : BY);
    float a1 = along_x ? (AX > BX ? AX : BX) : (AY > BY ? AY : BY);
    int u0 = (int)floorf(a0 - hw - 1), u1 = (int)ceilf(a1 + hw + 1), u, v;
    for (u = u0; u <= u1; u++) {
        /* the line's place across, at this column (row), its ends held */
        float m = u + .5f, f, at;
        if (along_x)
            f = dx != 0 ? (m - AX) / dx : 0;
        else
            f = dy != 0 ? (m - AY) / dy : 0;
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        at = along_x ? AY + f * dy : AX + f * dx;
        for (v = (int)floorf(at - band); v <= (int)ceilf(at + band); v++) {
            if (along_x)
                line_px(c, u, v, AX, AY, dx, dy, l2, hw, col);
            else
                line_px(c, v, u, AX, AY, dx, dy, l2, hw, col);
        }
    }
}

static void dot(Canvas *c, float x, float y, float r, uint32_t col)
{
    rbox(c, x - r, y - r, 2 * r, 2 * r, r, col, col, 0, 0, 1);
}

/* a filled triangle (design pixels) */
static void tri(Canvas *c, float ax, float ay, float bx, float by, float cx, float cy, uint32_t col)
{
    float k = c->k, P[3][2] = { { ax * k, ay * k }, { bx * k, by * k }, { cx * k, cy * k } };
    float minx = P[0][0], maxx = P[0][0], miny = P[0][1], maxy = P[0][1];
    int i, j, e;
    float area = (P[1][0] - P[0][0]) * (P[2][1] - P[0][1]) - (P[2][0] - P[0][0]) * (P[1][1] - P[0][1]);
    for (e = 1; e < 3; e++) {
        minx = P[e][0] < minx ? P[e][0] : minx;
        maxx = P[e][0] > maxx ? P[e][0] : maxx;
        miny = P[e][1] < miny ? P[e][1] : miny;
        maxy = P[e][1] > maxy ? P[e][1] : maxy;
    }
    for (j = (int)minx * 0 + (int)floorf(miny) - 1; j <= (int)ceilf(maxy) + 1; j++)
        for (i = (int)floorf(minx) - 1; i <= (int)ceilf(maxx) + 1; i++) {
            float cov = 1e9f;
            for (e = 0; e < 3; e++) {
                float *p = P[e], *q = P[(e + 1) % 3];
                float ex = q[0] - p[0], ey = q[1] - p[1], len = sqrtf(ex * ex + ey * ey);
                float d = ((i + .5f - p[0]) * ey - (j + .5f - p[1]) * ex) / (len > 0 ? len : 1);
                if (area > 0)
                    d = -d;
                d = .5f + d;
                cov = d < cov ? d : cov;
            }
            blend(c, i, j, col, cov);
        }
}

/* ---- text */
static int fonts_init(void)
{
    if (fonts_ok < 0)
        fonts_ok = stbtt_InitFont(&font[0], port_font_sans, 0) &&
                   stbtt_InitFont(&font[1], port_font_sans_bold, 0);
    return fonts_ok;
}

static int utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) {
        c = (c & 7) << 18 | (p[0] & 63) << 12 | (p[1] & 63) << 6 | (p[2] & 63);
        p += 3;
    } else if (c >= 0xE0 && p[0] && p[1]) {
        c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63);
        p += 2;
    } else if (c >= 0xC0 && p[0]) {
        c = (c & 31) << 6 | (p[0] & 63);
        p += 1;
    }
    *s = (const char *)p;
    return c;
}

/* the text's width in design pixels (size in design pixels, the font's em... its line height) */
static float text_w(const char *s, float size, int bold, float spacing)
{
    stbtt_fontinfo *f = &font[bold];
    float sc = stbtt_ScaleForPixelHeight(f, size), w = 0;
    int prev = 0;
    while (*s) {
        int ch = utf8_next(&s), adv, lsb;
        stbtt_GetCodepointHMetrics(f, ch, &adv, &lsb);
        if (prev)
            w += sc * stbtt_GetCodepointKernAdvance(f, prev, ch);
        w += sc * adv + spacing;
        prev = ch;
    }
    return w;
}

/* text with its middle (between the baseline and the capitals' top) at design y `cy` */
static float text(Canvas *c, float x, float cy, const char *s, float size, int bold, uint32_t col, float spacing)
{
    stbtt_fontinfo *f = &font[bold];
    float k = c->k, sc = stbtt_ScaleForPixelHeight(f, size * k), pen = x * k;
    float base = cy * k + size * k * 0.36f;      /* Liberation's capitals are ~0.72 of the size */
    int prev = 0;
    while (*s) {
        int ch = utf8_next(&s), adv, lsb, bw, bh, ox, oy, i, j;
        unsigned char *bm;
        float fx;
        stbtt_GetCodepointHMetrics(f, ch, &adv, &lsb);
        if (prev)
            pen += sc * stbtt_GetCodepointKernAdvance(f, prev, ch);
        fx = pen - floorf(pen);
        bm = stbtt_GetCodepointBitmapSubpixel(f, sc, sc, fx, 0, ch, &bw, &bh, &ox, &oy);
        if (bm) {
            int bx = (int)floorf(pen) + ox, by = (int)floorf(base + .5f) + oy;
            for (j = 0; j < bh; j++)
                for (i = 0; i < bw; i++)
                    if (bm[j * bw + i])
                        blend(c, bx + i, by + j, col, bm[j * bw + i] / 255.f);
            stbtt_FreeBitmap(bm, NULL);
        }
        pen += sc * adv + spacing * k;
        prev = ch;
    }
    return pen / k;
}

/* ---- the mouse: b holds L R M W X (design: 30 x 44 at size s = its width) */
static void mouse_icon(Canvas *c, float x, float cy, float s, const char *b)
{
    float u = s / 30, y = cy - 22 * u;
    uint32_t on = 0x7FE0A0, off = 0x1B2624, ln = 0xCFD8DA;
    int L = strchr(b, 'L') != NULL, R = strchr(b, 'R') != NULL, M = strchr(b, 'M') || strchr(b, 'W');
    float k = c->k;
    int i, j;
    /* the body, its buttons: a rounded box, the top 16 units the buttons */
    for (j = (int)floorf(y * k) - 1; j <= (int)ceilf((y + 44 * u) * k) + 1; j++)
        for (i = (int)floorf(x * k) - 1; i <= (int)ceilf((x + 30 * u) * k) + 1; i++) {
            float px = i + .5f, py = j + .5f;
            float d = sd_box(px, py, (x + 15 * u) * k, (y + 22 * u) * k, 14 * u * k, 21 * u * k, 13 * u * k);
            float cov = .5f - d, lw = 1.8f * u * k;
            float uy = py / k - y, ux = px / k - x;
            uint32_t fill = off;
            if (cov <= 0)
                continue;
            if (uy < 17 * u)
                fill = ux < 15 * u ? (L ? on : off) : (R ? on : off);
            if (d > -lw || (uy < 17 * u && fabsf(ux - 15 * u) < .7f * u) || fabsf(uy - 17 * u) < .7f * u)
                fill = ln;
            blend(c, i, j, fill, cov > 1 ? 1 : cov);
        }
    rbox(c, x + 12.5f * u, y + 5 * u, 5 * u, 9 * u, 2.5f * u, M ? on : off, M ? on : off, ln, 1.1f * u, 1);
    if (strchr(b, 'W')) {
        tri(c, x + 15 * u, y - 1 * u, x + 11.5f * u, y + 3 * u, x + 18.5f * u, y + 3 * u, 0xFFD76A);
        tri(c, x + 15 * u, y + 19 * u, x + 11.5f * u, y + 15.5f * u, x + 18.5f * u, y + 15.5f * u, 0xFFD76A);
    }
    if (strchr(b, 'X')) {
        float mx = x + 15 * u, my = y + 30 * u;
        tri(c, mx - 9 * u, my, mx - 5 * u, my - 3.5f * u, mx - 5 * u, my + 3.5f * u, 0xFFD76A);
        tri(c, mx + 9 * u, my, mx + 5 * u, my - 3.5f * u, mx + 5 * u, my + 3.5f * u, 0xFFD76A);
        tri(c, mx, my - 7 * u, mx - 3.5f * u, my - 3 * u, mx + 3.5f * u, my - 3 * u, 0xFFD76A);
        tri(c, mx, my + 7 * u, mx - 3.5f * u, my + 3 * u, mx + 3.5f * u, my + 3 * u, 0xFFD76A);
    }
}

/* ------------------------------------------------------------------ a line's parts */
enum { P_KEY, P_MOUSE, P_SEP, P_TEXT, P_BOLD };
typedef struct { int kind; char s[40]; } Part;
#define MAXP 24
#define KEY_H 26.f
#define KEY_FONT 16.f
#define MOUSE_S 21.f

static const char *short_name(int k, char *buf, int size)
{
    const char *n = port_keys_name(k);
    static const char *const map[][2] = {
        { "Return", "Enter" }, { "Escape", "Esc" }, { "Left Shift", "L-Shift" }, { "Right Shift", "R-Shift" },
        { "Left Ctrl", "Ctrl" }, { "Right Ctrl", "R-Ctrl" }, { "Left Alt", "Alt" }, { "Right Alt", "AltGr" },
        { "=", "+" }, { "-", "\xE2\x88\x92" }, { "Backspace", "Bksp" }, { "CapsLock", "Caps" },
        { "Up", "\xE2\x86\x91" }, { "Down", "\xE2\x86\x93" }, { "Left", "\xE2\x86\x90" },
        { "Right", "\xE2\x86\x92" }, { "Mouse 4", "M4" }, { "Mouse 5", "M5" }, { "PageUp", "PgUp" },
        { "PageDown", "PgDn" }, { "Delete", "Del" }, { "Insert", "Ins" },
    };
    size_t i;
    for (i = 0; i < sizeof map / sizeof map[0]; i++)
        if (!strcmp(n, map[i][0]))
            return map[i][1];
    if (!strncmp(n, "Keypad ", 7)) {
        snprintf(buf, size, "Num %s", n + 7);
        return buf;
    }
    return n;
}

/* the keys part of a segment -> parts */
static int key_parts(const char *s, Part *p, int n)
{
    while (*s && n < MAXP) {
        if (*s == '{') {
            char name[40], buf[40];
            const char *e = strchr(s, '}');
            int slot = -1, len, k;
            char *colon;
            if (!e)
                break;
            len = (int)(e - s - 1) < (int)sizeof name - 1 ? (int)(e - s - 1) : (int)sizeof name - 1;
            memcpy(name, s + 1, len);
            name[len] = 0;
            if ((colon = strchr(name, ':')) != NULL) {
                slot = atoi(colon + 1) - 1;
                *colon = 0;
            }
            k = port_keys_bound(name, slot);
            s = e + 1;
            if (k == PORT_KEY_MOUSE_MIDDLE) {
                p[n].kind = P_MOUSE;
                strcpy(p[n++].s, "M");
            } else if (k == PORT_KEY_WHEEL_UP || k == PORT_KEY_WHEEL_DOWN) {
                if (n && p[n - 1].kind == P_MOUSE && !strcmp(p[n - 1].s, "W"))
                    continue;
                p[n].kind = P_MOUSE;
                strcpy(p[n++].s, "W");
            } else if (k) {
                p[n].kind = P_KEY;
                snprintf(p[n++].s, sizeof p[0].s, "%s", short_name(k, buf, sizeof buf));
            } else if (slot < 0) {          /* not bound: a dim cap */
                p[n].kind = P_KEY;
                strcpy(p[n++].s, "\xE2\x80\x94");
            }
        } else if (*s == '[') {
            const char *e = strchr(s, ']');
            int len;
            if (!e)
                break;
            len = (int)(e - s - 1) < (int)sizeof p[0].s - 1 ? (int)(e - s - 1) : (int)sizeof p[0].s - 1;
            p[n].kind = P_KEY;
            memcpy(p[n].s, s + 1, len);
            p[n++].s[len] = 0;
            s = e + 1;
        } else if (*s == '<') {
            p[n].kind = P_MOUSE;
            p[n].s[0] = s[1];
            p[n++].s[1] = 0;
            s += 3;
        } else if (*s == '~' || *s == '+' || *s == '/') {
            p[n].kind = P_SEP;
            strcpy(p[n++].s, *s == '~' ? "\xE2\x80\x93" : *s == '+' ? "+" : "/");
            s++;
        } else
            s++;
    }
    return n;
}

/* a whole line: segments "keys|text;..."; keys_first: keys then text in each segment */
static int line_parts(const char *spec, Part *p, int keys_first)
{
    char seg[256];
    int n = 0;
    while (*spec && n < MAXP - 2) {
        const char *e = strchr(spec, ';'), *bar;
        int len = e ? (int)(e - spec) : (int)strlen(spec);
        Part kp[MAXP];
        int nk, i;
        if (len > (int)sizeof seg - 1)
            len = sizeof seg - 1;
        memcpy(seg, spec, len);
        seg[len] = 0;
        bar = strchr(seg, '|');
        if (bar)
            seg[bar - seg] = 0;
        nk = key_parts(seg, kp, 0);
        if (!keys_first && bar && bar[1]) {
            p[n].kind = P_TEXT;
            snprintf(p[n++].s, sizeof p[0].s, "%s", bar + 1);
        }
        for (i = 0; i < nk && n < MAXP - 1; i++)
            p[n++] = kp[i];
        if (keys_first && bar && bar[1]) {
            p[n].kind = P_TEXT;
            snprintf(p[n++].s, sizeof p[0].s, "%s", bar + 1);
        }
        spec += len + (e ? 1 : 0);
    }
    return n;
}

static float key_w(const char *s)
{
    float w = text_w(s, KEY_FONT, 1, .3f) + 14;
    return w < 34 ? 34 : w;
}

static float part_w(const Part *p, float size)
{
    switch (p->kind) {
    case P_KEY: return key_w(p->s);
    case P_MOUSE: return MOUSE_S;
    case P_SEP: return text_w(p->s, 18, 0, 0);
    default: return text_w(p->s, size, p->kind == P_BOLD, 0);
    }
}

#define GAP 6.f
static float parts_w(const Part *p, int n, float size)
{
    float w = 0;
    int i;
    for (i = 0; i < n; i++)
        w += part_w(&p[i], size) + (i ? GAP : 0);
    return w;
}

static float draw_parts(Canvas *c, float x, float cy, const Part *p, int n, float size, uint32_t col)
{
    int i;
    for (i = 0; i < n; i++) {
        float w = part_w(&p[i], size);
        if (i)
            x += GAP;
        switch (p[i].kind) {
        case P_KEY: {
            int none = !strcmp(p[i].s, "\xE2\x80\x94");
            rbox(c, x, cy - KEY_H / 2 + 2, w, KEY_H, 5, 0x59646A, 0x59646A, 0, 0, 1);    /* shadow */
            rbox(c, x, cy - KEY_H / 2, w, KEY_H, 5, none ? 0x6A7478 : 0xF4F6F6,
                 none ? 0x4A5458 : 0xB9C2C4, none ? 0x8A979B : 0xFFFFFF, 1, 1);
            text(c, x + (w - text_w(p[i].s, KEY_FONT, 1, .3f)) / 2, cy, p[i].s, KEY_FONT, 1,
                 none ? 0xCFD8DA : 0x111111, .3f);
            break;
        }
        case P_MOUSE:
            mouse_icon(c, x, cy, MOUSE_S, p[i].s);
            break;
        case P_SEP:
            text(c, x, cy, p[i].s, 18, 0, 0x8A979B, 0);
            break;
        default:
            text(c, x, cy, p[i].s, size, p[i].kind == P_BOLD, col, 0);
        }
        x += w;
    }
    return x;
}

/* ------------------------------------------------------------------ the screen */
#define LOGO_H 44.f                     /* the title's logo line, high */
#define PIC_X0 470.f                    /* the picture's room: 470..1450 x 72..698 */
#define PIC_W 980.f
#define PIC_Y 72.f
#define PIC_H 626.f
static SDL_FRect pic;                   /* the picture's place, design pixels */

static void pic_place(float aspect)
{
    float w = PIC_W, h = w / aspect;
    if (h > PIC_H) {
        h = PIC_H;
        w = h * aspect;
    }
    pic.x = PIC_X0 + (PIC_W - w) / 2;
    pic.y = PIC_Y;
    pic.w = w;
    pic.h = h;
}

/* a part drawn over the 3D view (at the game's own picture's place with Widescreen) */
static int in_view(int e, int suit)
{
    if (snap_part == 1)
        return 1;
    return e == E_GOGGLES || e == E_INFRA || e == E_RETICLE || e == E_TARGET || e == E_ARMOR ||
           (e == E_COMPASS && suit == 3);
}

static int anchor(int e, float *x, float *y)
{
    const Pt *t;
    int s = snap_suit;
    if (!snap || s < 0 || s > 3 || snap_part < 0)
        return 0;
    t = snap_part == 1 ? &goggles_at[s == 3][e] : &cockpit_at[s][e];
    if (t->x < 0)
        return 0;
    if (snap_stretched && !in_view(e, s))
        *x = pic.x + t->x / 320.f * pic.w;      /* the HUD's art, stretched */
    else
        *x = pic.x + (snap_f0 + t->x / 320.f * snap_fw) * pic.w;
    *y = pic.y + t->y / 400.f * pic.h;
    return 1;
}

/* the background: dark, a little green at the top middle (a colour by the distance from there;
 * whole numbers in the loop: a table by the distance squared) */
#define BG_STEPS 4096
static void background(Canvas *c)
{
    static uint32_t lut[BG_STEPS + 1];
    static int lut_ok;
    int i, j;
    float k = c->k;
    int32_t *col = (int32_t *)malloc(sizeof(int32_t) * c->w);
    if (!col)
        return;
    if (!lut_ok) {
        for (i = 0; i <= BG_STEPS; i++) {
            uint32_t m = mix(0x0D1A17, 0x050808, sqrtf((float)i / BG_STEPS));
            uint8_t px[4] = { (uint8_t)(m >> 16), (uint8_t)(m >> 8), (uint8_t)m, 255 };
            memcpy(&lut[i], px, 4);
        }
        lut_ok = 1;
    }
    /* (distance / 0.7)^2 = dx^2 + dy^2 with dx = (x - 960) / (1400 * 0.7), dy = (y - 324) / (900 * 0.7):
     * in 1/65536ths of BG_STEPS, up to 1 (and more is 1) */
    for (i = 0; i < c->w; i++) {
        float dx = ((c->ox + i) / k - 960) / 980, v = dx * dx;
        col[i] = v >= 1 ? (BG_STEPS << 16) : (int32_t)(v * (BG_STEPS << 16));
    }
    for (j = 0; j < c->h; j++) {
        float dy = ((c->oy + j) / k - 324) / 630, v = dy * dy;
        int32_t row = v >= 1 ? (BG_STEPS << 16) : (int32_t)(v * (BG_STEPS << 16));
        uint32_t *q = (uint32_t *)(c->px + (size_t)j * c->w * 4);
        for (i = 0; i < c->w; i++) {
            int32_t d = col[i] + row;
            q[i] = lut[d >= (BG_STEPS << 16) ? BG_STEPS : d >> 16];
        }
    }
    free(col);
}

/* Freefall's logo line (port/res/logo-line.png, as on the options screen), top left at design
 * (x, y), h design pixels high: each window pixel the average of the logo's pixels under it */
extern const unsigned char port_logo_line[];
extern const unsigned port_logo_line_len;
unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp, int req);
static unsigned char *logo_px;
static int logo_w, logo_h;

static float logo(Canvas *c, float x, float y, float h)
{
    float k = c->k, X = floorf(x * k + .5f), Y = floorf(y * k + .5f), H = h * k, W, sx, sy;
    int i, j, n;
    if (!logo_px && !(logo_px = stbi_load_from_memory(port_logo_line, (int)port_logo_line_len, &logo_w, &logo_h, &n, 4)))
        return x;
    W = H * logo_w / logo_h;
    sx = logo_w / W;                    /* the logo's pixels across a window pixel */
    sy = logo_h / H;
    for (j = 0; j < (int)ceilf(H); j++)
        for (i = 0; i < (int)ceilf(W); i++) {
            float x0 = i * sx, x1 = (i + 1) * sx, y0 = j * sy, y1 = (j + 1) * sy;
            float sum[4] = { 0, 0, 0, 0 }, area = 0;
            int u, v;
            if (x1 > logo_w)
                x1 = (float)logo_w;
            if (y1 > logo_h)
                y1 = (float)logo_h;
            for (v = (int)y0; v < y1; v++) {
                float wy = (v + 1 < y1 ? v + 1 : y1) - (v > y0 ? v : y0);
                for (u = (int)x0; u < x1; u++) {
                    float wx = (u + 1 < x1 ? u + 1 : x1) - (u > x0 ? u : x0), wt = wx * wy;
                    const unsigned char *q = logo_px + ((size_t)v * logo_w + u) * 4;
                    float a = q[3] * wt;
                    sum[0] += q[0] * a;
                    sum[1] += q[1] * a;
                    sum[2] += q[2] * a;
                    sum[3] += a;
                    area += wt;
                }
            }
            if (sum[3] > 0 && area > 0)
                blend(c, (int)X + i, (int)Y + j,
                      (uint32_t)(sum[0] / sum[3] + .5f) << 16 | (uint32_t)(sum[1] / sum[3] + .5f) << 8 |
                      (uint32_t)(sum[2] / sum[3] + .5f), sum[3] / area / 255);
        }
    return (X + W) / k;
}

/* the screen under the picture (the picture's own parts: draw_pic) */
static void draw_base(Canvas *c, int st)
{
    Part p[MAXP];
    int i, j, n;
    float x;
    char sub[96];

    background(c);
    /* the title: the logo line (its letters 108 of its 165 rows, their middle at y 37), the
     * controls style */
    x = logo(c, 40, 37 - LOGO_H * 54 / 165, LOGO_H);
    x -= LOGO_H * 28.f / 165;           /* the logo's own room right of its last letter */
    snprintf(sub, sizeof sub, "CONTROLS \xC2\xB7 %s", st ? "ADVANCED (MOUSELOOK)" : "CLASSIC (THE GAME'S KEYS)");
    text(c, x + 22, 37, sub, 24, 0, 0x7FE0A0, .5f);
    {
        const char *h = "Any key or click: back to the mission";
        text(c, 1880 - text_w(h, 20, 0, 0), 37, h, 20, 0, 0x9FB0B4, 0);
    }

    /* the callouts' words (their lines: draw_pic) */
    for (j = 0; j < 2; j++) {
        const Callout *co = j ? right_co : left_co;
        int nc = j ? (int)(sizeof right_co / sizeof right_co[0]) : (int)(sizeof left_co / sizeof left_co[0]);
        for (i = 0; i < nc; i++) {
            const char *spec = !st && co[i].van ? co[i].van : co[i].adv;
            float w;
            n = line_parts(spec, p, j);
            w = parts_w(p, n, 23);
            draw_parts(c, j ? 1468 : 452 - w, (float)co[i].y, p, n, 23, 0xE8EEF0);
        }
    }
    /* the row under the picture */
    n = line_parts(under_row, p, 1);
    {
        float w = 0, sx;
        for (i = 0; i < n; i++)
            w += part_w(&p[i], 20) + (i ? (p[i].kind == P_KEY && p[i - 1].kind == P_TEXT ? 26 : GAP) : 0);
        sx = 960 - w / 2;
        for (i = 0; i < n; i++) {
            if (i)
                sx += p[i].kind == P_KEY && p[i - 1].kind == P_TEXT ? 26 : GAP;
            sx = draw_parts(c, sx, 726, &p[i], 1, 20, 0xE8EEF0);
        }
    }
    /* the five lists */
    for (j = 0; j < 5; j++) {
        const char *title = lists[j].title ? lists[j].title : st ? "LOOK (MOUSE)" : "LOOK";
        float lx = list_x[j], lw = j == 1 ? 370.f : j == 2 ? 340.f : 360.f;
        text(c, lx, 770, title, 19, 1, 0x7FE0A0, 2);
        line(c, lx, 788.5f, lx + lw, 788.5f, 1, 0x2C4A40);
        for (i = 0; i < 7 && lists[j].rows[st][i]; i++) {
            char spec[160];
            const char *row = lists[j].rows[st][i], *bar = strchr(row, '|');
            Part kp[MAXP];
            int nk;
            float kw, cy = 811 + 32.f * i, kx;
            snprintf(spec, sizeof spec, "%.*s", (int)(bar - row), row);
            nk = key_parts(spec, kp, 0);
            kw = parts_w(kp, nk, 20);
            kx = kw <= 118 ? lx + 118 - kw : lx;
            draw_parts(c, kx, cy, kp, nk, 20, 0xE8EEF0);
            text(c, kx + kw + 7, cy, bar + 1, 20, 0, 0xE8EEF0, 0);
        }
    }
    /* the footer */
    line(c, 40, 1031.5f, 1880, 1031.5f, 1, 0x2C4A40);
    x = text(c, 40, 1049, "Controls style ", 18, 0, 0x9FB0B4, 0);
    x = text(c, x, 1049, st ? "Advanced" : "Classic", 18, 1, 0xE8EEF0, 0);
    text(c, x, 1049, " \xC2\xB7 the keys shown are your own (Freefall Options \xE2\x80\xBA Gameplay \xE2\x80\xBA Key bindings)",
         18, 0, 0x9FB0B4, 0);
    n = line_parts("[H]|again: the original help screen", p, 1);
    draw_parts(c, 1880 - parts_w(p, n, 18), 1049, p, n, 18, 0x9FB0B4);
}

/* over the picture, on a clear canvas: its frame, the callouts' lines to the HUD's parts, what
 * the mouse or the look keys do in the view (design x PICA_X0..PICA_X1, y PICA_Y0..PICA_Y1) */
#define PICA_X0 448.f
#define PICA_X1 1472.f
#define PICA_Y0 62.f
#define PICA_Y1 710.f
static void draw_pic(Canvas *c, int st)
{
    Part p[MAXP];
    int i, j, n;

    line(c, pic.x - .5f, pic.y - .5f, pic.x + pic.w + .5f, pic.y - .5f, 1, 0x2C4A40);
    line(c, pic.x - .5f, pic.y + pic.h + .5f, pic.x + pic.w + .5f, pic.y + pic.h + .5f, 1, 0x2C4A40);
    line(c, pic.x - .5f, pic.y - .5f, pic.x - .5f, pic.y + pic.h + .5f, 1, 0x2C4A40);
    line(c, pic.x + pic.w + .5f, pic.y - .5f, pic.x + pic.w + .5f, pic.y + pic.h + .5f, 1, 0x2C4A40);
    for (j = 0; j < 2; j++) {
        const Callout *co = j ? right_co : left_co;
        int nc = j ? (int)(sizeof right_co / sizeof right_co[0]) : (int)(sizeof left_co / sizeof left_co[0]);
        for (i = 0; i < nc; i++) {
            float ax, ay;
            if (anchor(co[i].e, &ax, &ay)) {
                float ex = j ? 1462.f : 458.f, bx = j ? 1450.f : 470.f;
                line(c, ex, (float)co[i].y, bx, (float)co[i].y, 1.6f, 0xCFD8DA);
                line(c, bx, (float)co[i].y, ax, ay, 1.6f, 0xCFD8DA);
                dot(c, ax, ay, 4, 0xFFD76A);
            }
        }
    }
    {
        float bw, bh = 44, bx, by;
        n = line_parts(view_box[st], p, 1);
        bw = parts_w(p, n, 22) + 28;
        bx = pic.x + (pic.w - bw) / 2;
        by = pic.y + pic.h * (snap_part == 1 ? .2f : .16f);
        rbox(c, bx, by, bw, bh, 6, 0x040A08, 0x040A08, 0x7FE0A0, 1, .93f);
        draw_parts(c, bx + 14, by + bh / 2, p, n, 22, 0xE8EEF0);
    }
}

/* a sum of the bindings shown, to see them change */
static unsigned keys_sum(void)
{
    static const char *const names[] = { "forward", "back", "turn_left", "turn_right", "strafe_left",
        "strafe_right", "strafe_mod", "bound", "jump", "look_up", "look_down", "head_left", "head_right",
        "look_ahead", "mouselook", "mouselook_hold", "gimbal", "fire", "weapon1", "weapon4",
        "target_prev", "target_next", "drone", "helmet", "view360", "infrared", "map", "nav", "zoom_in",
        "zoom_out", "map_out", "map_in", "view_9", "view_0", "mfd", "command", "squad1", "squad2",
        "squad3", "squad_all", "objectives", "map_squad", "map_mission", "pause" };
    unsigned h = 2166136261u;
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        h = (h ^ (unsigned)port_keys_bound(names[i], 0)) * 16777619u;
        h = (h ^ (unsigned)port_keys_bound(names[i], 1)) * 16777619u;
    }
    return h;
}

/* ------------------------------------------------------------------ the display's side */
/* platform.c: before the renderer is destroyed (a screen filter switched shaders on or off) */
void port_help_renderer_gone(void)
{
    if (snap_tex)
        SDL_DestroyTexture(snap_tex);
    if (base_tex)
        SDL_DestroyTexture(base_tex);
    if (pic_tex)
        SDL_DestroyTexture(pic_tex);
    snap_tex = base_tex = pic_tex = NULL;
}

/* the screen's size in the window: 1920 x 1080 design pixels scaled by k, as big as fits */
static float canvas_k(int ww, int wh, int *cw, int *ch)
{
    float k = (float)ww / 1920 < (float)wh / 1080 ? (float)ww / 1920 : (float)wh / 1080;
    *cw = (int)(1920 * k + .5f);
    *ch = (int)(1080 * k + .5f);
    return k;
}

static int SDLCALL base_worker(void *unused)
{
    Canvas c;
    (void)unused;
    c.px = base_px;
    c.w = base_px_w;
    c.h = base_px_h;
    c.ox = c.oy = 0;
    c.k = base_px_k;
    draw_base(&c, base_px_style);
    SDL_SetAtomicInt(&base_thr_done, 1);
    return 0;
}

static void base_wait(void)
{
    if (base_thr) {
        SDL_WaitThread(base_thr, NULL);
        base_thr = NULL;
    }
}

/* draws the screen under the picture into base_px for this window, style and bindings, unless
 * it is there already; `ahead`: on its own thread (else here, now) */
static void base_draw(float k, int cw, int ch, int st, unsigned ks, int ahead)
{
    if ((base_tex && base_w == cw && base_h == ch && base_style == st && base_keys == ks) ||
        (base_px && base_px_w == cw && base_px_h == ch && base_px_style == st && base_px_keys == ks))
        return;
    base_wait();                        /* drawn for something else (a change during it) */
    free(base_px);
    base_px = (uint8_t *)malloc((size_t)cw * ch * 4);
    if (!base_px)
        return;
    base_px_k = k;
    base_px_w = cw;
    base_px_h = ch;
    base_px_style = st;
    base_px_keys = ks;
    SDL_SetAtomicInt(&base_thr_done, 0);
    if (ahead && (base_thr = SDL_CreateThread(base_worker, "tnfree help", NULL)) != NULL)
        return;
    base_worker(NULL);
}

/* while the help screen is not up: draw it ahead for the window as it is (a look twice a
 * second; not in recorded runs or the benchmark) */
static void base_ahead(int ww, int wh)
{
    static Uint64 next;
    Uint64 now = SDL_GetTicks();
    int cw, ch;
    float k;
    if (now < next)
        return;
    next = now + 500;
    if (base_thr && !SDL_GetAtomicInt(&base_thr_done))
        return;
    base_wait();
    if (port_lockstep || port_bench || !port_help_wanted() || !fonts_init())
        return;
    k = canvas_k(ww, wh, &cw, &ch);
    base_draw(k, cw, ch, port_mouselook ? 1 : 0, keys_sum(), 1);
}

/* present (main thread), after the game's screen is drawn and before anything over it: `shown`
 * is where the screen's picture went in the window, the game's own picture of w columns in the
 * middle of w + 2 ex; `stretched`: the HUD's art stretched evenly to all of it (Widescreen
 * without a cockpit pack), what is in the view (and what the HUD draws there) in the middle.  Reads the window back when the game thread asks; draws the help screen when it is
 * up.  Returns 1 when it drew. */
int port_help_present(SDL_Renderer *r, int ww, int wh, const SDL_FRect *shown, int w, int ex, int stretched)
{
    if (SDL_GetAtomicInt(&snap_req) && !SDL_GetAtomicInt(&snap_done)) {
        SDL_Rect rc;
        rc.x = (int)SDL_floorf(shown->x < 0 ? 0 : shown->x);
        rc.y = (int)SDL_floorf(shown->y < 0 ? 0 : shown->y);
        rc.w = (int)SDL_ceilf(shown->w);
        rc.h = (int)SDL_ceilf(shown->h);
        if (rc.x + rc.w > ww)
            rc.w = ww - rc.x;
        if (rc.y + rc.h > wh)
            rc.h = wh - rc.y;
        if (snap)
            SDL_DestroySurface(snap);
        if (snap_tex)
            SDL_DestroyTexture(snap_tex);
        snap_tex = NULL;
        snap = rc.w > 0 && rc.h > 0 && w > 0 ? SDL_RenderReadPixels(r, &rc) : NULL;
        if (snap) {
            float cols = (float)(w + 2 * ex);
            /* the game's screen's columns within the part read back (shown may start left of 0) */
            float x0 = shown->x + shown->w * ex / cols, xw = shown->w * w / cols;
            snap_f0 = (x0 - rc.x) / rc.w;
            snap_fw = xw / rc.w;
            snap_stretched = stretched;
        }
        SDL_SetAtomicInt(&snap_done, 1);
    }
    if (!help_up || !help_new) {
        base_ahead(ww, wh);
        return 0;
    }
    if (!fonts_init())
        return 0;
    {
        int cw, ch;
        float k = canvas_k(ww, wh, &cw, &ch);
        unsigned ks = keys_sum();
        SDL_FRect dst;
        if (snap && !snap_tex) {
            snap_tex = SDL_CreateTextureFromSurface(r, snap);
            if (snap_tex)
                SDL_SetTextureScaleMode(snap_tex, SDL_SCALEMODE_LINEAR);
        }
        if (!base_tex || base_w != cw || base_h != ch || base_style != snap_style || base_keys != ks) {
            base_draw(k, cw, ch, snap_style, ks, 0);    /* unless drawn ahead */
            base_wait();
            if (!base_px)
                return 0;
            if (base_tex)
                SDL_DestroyTexture(base_tex);
            base_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, cw, ch);
            if (base_tex)
                SDL_UpdateTexture(base_tex, NULL, base_px, cw * 4);
            free(base_px);
            base_px = NULL;
            base_w = cw;
            base_h = ch;
            base_style = snap_style;
            base_keys = ks;
        }
        if (snap) {
            /* what goes over the picture: drawn again only when it would come out different */
            PicKey key;
            pic_place((float)snap->w / snap->h);
            memset(&key, 0, sizeof key);
            key.cw = cw;
            key.style = snap_style;
            key.part = snap_part;
            key.suit = snap_suit;
            key.stretched = snap_stretched;
            key.keys = ks;
            key.pw = pic.w;
            key.ph = pic.h;
            key.f0 = snap_f0;
            key.fw = snap_fw;
            if (!pic_tex || memcmp(&key, &pic_key, sizeof key)) {
                Canvas c;
                c.ox = (int)floorf(PICA_X0 * k);
                c.oy = (int)floorf(PICA_Y0 * k);
                c.w = (int)ceilf(PICA_X1 * k) - c.ox;
                c.h = (int)ceilf(PICA_Y1 * k) - c.oy;
                c.k = k;
                c.px = (uint8_t *)calloc((size_t)c.w * c.h, 4);
                if (!c.px)
                    return 0;
                draw_pic(&c, snap_style);
                if (pic_tex)
                    SDL_DestroyTexture(pic_tex);
                pic_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, c.w, c.h);
                if (pic_tex) {
                    SDL_UpdateTexture(pic_tex, NULL, c.px, c.w * 4);
                    SDL_SetTextureBlendMode(pic_tex, SDL_BLENDMODE_BLEND);
                }
                free(c.px);
                pic_area.x = c.ox;
                pic_area.y = c.oy;
                pic_area.w = c.w;
                pic_area.h = c.h;
                pic_key = key;
            }
        }
        SDL_SetRenderDrawColor(r, 5, 8, 8, 255);
        SDL_RenderClear(r);
        dst.x = SDL_floorf((ww - cw) / 2.f);
        dst.y = SDL_floorf((wh - ch) / 2.f);
        dst.w = (float)cw;
        dst.h = (float)ch;
        if (base_tex)
            SDL_RenderTexture(r, base_tex, NULL, &dst);
        if (snap_tex) {
            SDL_FRect pr;
            pr.x = dst.x + SDL_floorf(pic.x * k);
            pr.y = dst.y + SDL_floorf(pic.y * k);
            pr.w = SDL_ceilf((pic.x + pic.w) * k) - SDL_floorf(pic.x * k);
            pr.h = SDL_ceilf((pic.y + pic.h) * k) - SDL_floorf(pic.y * k);
            SDL_RenderTexture(r, snap_tex, NULL, &pr);
            if (pic_tex) {
                SDL_FRect pa;
                pa.x = dst.x + pic_area.x;
                pa.y = dst.y + pic_area.y;
                pa.w = (float)pic_area.w;
                pa.h = (float)pic_area.h;
                SDL_RenderTexture(r, pic_tex, NULL, &pa);
            }
        }
    }
    return 1;
}
