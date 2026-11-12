/* cockpitpack.c -- Freefall: "Cockpit pack" on the Mods page (off by default): the cockpit's
 * and the goggles' art painted for 16:9, shown with Widescreen in place of the evenly stretched
 * screen.
 *
 * A pack is the Cockpits folder of a mod (Mods/<mod>.zip next to tnfree.exe, with --user in that
 * folder, or a folder Mods/<mod> while it is being made: port_pack_dir) of
 * indexed PNGs whose colour numbers are the game's palette entries (0 = see-through: the 3D view
 * there), named
 *
 *     <suit>-<rows>-<part>.png       426 x rows: the game's 320 columns in the middle (53..372),
 *                                    53 more on each side (320 at 4:3 is 426 at 16:9)
 *     <suit>-<rows>-<part>-2x.png    852 x (2 x rows): the same at twice the pixels
 *
 * suit scout, standard, heavy or pirate (the game's RESHELMS/N/H/P.RES, RESHLMHS/N/H/P.RES), rows
 * 200 or 400 (the 320 X 200 and 320 X 400 screens), part cockpit (res 750, with the frame 752 the
 * game draws over its view) or goggles (751, the goggles' frame).  A picture that is missing, or
 * not the right size, leaves that screen stretched as before; the 2x one is used when the game's
 * screen takes at least twice its rows of the window's pixels, else the 1x one (either when only
 * one is there).
 *
 * The game is the same: it still draws its 320 columns.  The display (platform.c, ck_compose)
 * lays the game's screen 1:1 in the middle, the main view's wide picture where the pack's pixel
 * is 0, and the pack's picture over it: its pixels wherever the game's screen still shows the
 * game's own art at that place (so the HUD's screens, gauges, text and help boxes stay the game's
 * pixels), and everywhere beside the screen.  The game thread hands over the art as it draws it
 * (helmet.c helm_draw_frame); the display loads a picture when it is first wanted.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"
#include "fxpack.h"
#include "sharp3d.h"
#include "cockpitpack.h"

char port_ck_pack[64];                  /* "Cockpit pack": a mod's name (its Cockpits), "" = off */
volatile int port_ck_on;

#define SCR_W 320
#define MAXH 400

static const char suit_letters[4] = { 'S', 'N', 'H', 'P' };
static const char *suit_names[4] = { "scout", "standard", "heavy", "pirate" };
static const char *part_names[2] = { "cockpit", "goggles" };

/* ------------------------------------------------------------------ the game thread's art */
static SDL_Mutex *art_mutex;
static uint8_t cockpit750[SCR_W * MAXH];        /* the cockpit's picture as last drawn */
static int c750_h;
static struct {
    unsigned gen;
    int w, h, x, y, set;
} piece[2];                                     /* the piece over the view as last put in art */
static uint8_t frame752[SCR_W * MAXH];          /* the cockpit's frame as last drawn */
static uint8_t art[2][SCR_W * MAXH];            /* the art the screen shows (750 + 752, 751) */
static uint8_t marks[2][SCR_W * MAXH];          /* 1 where a damaged piece of the frame is drawn (P95) */
static int marks_n[2];                          /* how many */
static int art_h[2], art_suit[2];
static unsigned art_serial[2];
static volatile int part_up = -1;

int port_ck_cols(int w)
{
    return w + 2 * ((w * 4 / 3 - w) / 2);
}

static int suit_of(int letter)
{
    int i;
    for (i = 0; i < 4; i++)
        if ((letter & ~0x20) == suit_letters[i])
            return i;
    return -1;
}

static void lock(void)
{
    if (!art_mutex)
        art_mutex = SDL_CreateMutex();
    SDL_LockMutex(art_mutex);
}

/* the cockpit's art: 750 with the frame 752 put over it at its place */
static void make_cockpit(void)
{
    const uint8_t *px = frame752;
    int y, x;
    memcpy(art[0], cockpit750, (size_t)SCR_W * c750_h);
    if (piece[0].set)
        for (y = 0; y < piece[0].h; y++)
            for (x = 0; x < piece[0].w; x++) {
                int ax = piece[0].x + x, ay = piece[0].y + y;
                uint8_t c = px[(size_t)y * piece[0].w + x];
                if (c && ax >= 0 && ax < SCR_W && ay >= 0 && ay < c750_h)
                    art[0][(size_t)ay * SCR_W + ax] = c;
            }
    art_h[0] = c750_h;
    memset(marks[0], 0, sizeof marks[0]);
    marks_n[0] = 0;
    art_serial[0]++;
}

void port_ck_art(int letter, const uint8_t *px, int w, int h)
{
    if (w != SCR_W || h <= 0 || h > MAXH)
        return;
    lock();
    if (h != c750_h || memcmp(cockpit750, px, (size_t)w * h) || suit_of(letter) != art_suit[0]) {
        memcpy(cockpit750, px, (size_t)w * h);
        c750_h = h;
        art_suit[0] = suit_of(letter);
        make_cockpit();
    } else if (marks_n[0]) {            /* drawn again whole: the damaged pieces come after */
        memset(marks[0], 0, sizeof marks[0]);
        marks_n[0] = 0;
        art_serial[0]++;
    }
    SDL_UnlockMutex(art_mutex);
}

void port_ck_piece(int part, int letter, const uint8_t *px, unsigned gen, int w, int h, int x, int y)
{
    if (part < 0 || part > 1 || !px || w <= 0 || h <= 0 || w > SCR_W || h > MAXH) {
        part_up = -1;
        return;
    }
    if (!piece[part].set || piece[part].gen != gen || piece[part].w != w || piece[part].h != h ||
        piece[part].x != x || piece[part].y != y || suit_of(letter) != art_suit[part]) {
        lock();
        piece[part].gen = gen;
        piece[part].w = w;
        piece[part].h = h;
        piece[part].x = x;
        piece[part].y = y;
        piece[part].set = 1;
        if (part == 0) {
            memcpy(frame752, px, (size_t)w * h);
            if (c750_h)
                make_cockpit();
        } else {
            art_suit[1] = suit_of(letter);
            if (w == SCR_W && x == 0 && y == 0) {
                memcpy(art[1], px, (size_t)w * h);
                art_h[1] = h;
            } else
                art_h[1] = 0;
            memset(marks[1], 0, sizeof marks[1]);
            marks_n[1] = 0;
            art_serial[1]++;
        }
        SDL_UnlockMutex(art_mutex);
    }
    part_up = part;
}

/* mfddata.c draw_part (game thread): the game drew a piece of the suit's damage on the frame at
 * x0..x1, y0..y1 of the screen scr: where it differs from the art it is marked, so that a pack's
 * picture shows its own window there (the pieces are painted for the game's 4:3 frame: on the
 * pack's glass they hung in the view, P95); where the pack has its frame they still show */
void port_ck_mark(const uint8_t *scr, long pitch, int x0, int y0, int x1, int y1)
{
    int part = part_up, x, y, changed = 0;
    if (part < 0 || part > 1 || !scr || pitch <= 0)
        return;
    lock();
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > SCR_W)
        x1 = SCR_W;
    if (y1 > art_h[part])
        y1 = art_h[part];
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            size_t i = (size_t)y * SCR_W + x;
            uint8_t m = scr[(size_t)y * pitch + x] != art[part][i];
            if (m != marks[part][i]) {
                marks[part][i] = m;
                marks_n[part] += m ? 1 : -1;
                changed = 1;
            }
        }
    if (changed)
        art_serial[part]++;
    SDL_UnlockMutex(art_mutex);
}

/* mfddata.c draw_part (game thread): with a pack's picture shown, a piece of the suit's damage
 * (pc, w x h, 0 = none of it) drawn at x0, y0 of the screen scr undamaged: the art's own pixels
 * where the art has them, the piece's where it has the game's window (its cracks on the glass:
 * port_ck_mark notes them, the display shows the view there, P95).  The pieces are painted for
 * the game's 4:3 frame: on the pack's 16:9 art they were patches of another picture, and with the
 * layered helmet they stood still while its parts moved (owner: the pack's art stays
 * whole; its own damage later).  0 if not (no pack's picture, its art not known) */
int port_ck_whole(uint8_t *scr, long pitch, const uint8_t *pc, int w, int h, int x0, int y0)
{
    int part = part_up, x, y;
    if (!port_ck_on || part < 0 || part > 1 || !scr || pitch <= 0 || !pc)
        return 0;
    lock();
    if (art_h[part] <= 0) {
        SDL_UnlockMutex(art_mutex);
        return 0;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int sx = x0 + x, sy = y0 + y;
            uint8_t c = pc[(size_t)y * w + x], a;
            if (!c || sx < 0 || sx >= SCR_W || sy < 0 || sy >= art_h[part])
                continue;
            a = art[part][(size_t)sy * SCR_W + sx];
            scr[(size_t)sy * pitch + sx] = a ? a : c;
        }
    SDL_UnlockMutex(art_mutex);
    return 1;
}

/* sharp3d.c (game thread): the suit of the cockpit or goggles up (-1 not known), else -2 */
int port_ck_view_suit(void)
{
    return part_up >= 0 ? art_suit[part_up] : -2;
}

/* arms.c via sharp3d.c (game thread): the goggles' frame is over the view (not the cockpit's, not
 * the drone's view): its suit (0 scout .. 3 pirate, -1 not known), else -2 */
int port_ck_goggles_suit(void)
{
    return part_up == 1 ? art_suit[1] : -2;
}

/* ------------------------------------------------------------------ the display's pictures */
typedef struct {
    int tried, ok, s;
    uint8_t *pix, *win;
    uint8_t *lay;                       /* -layers.png (the screens' static), else NULL */
    /* the layered helmet (<name>-parts.png, -hidden.png), else NULL */
    uint8_t *parts, *hidden;
    const uint8_t *border;              /* -border.png, bb pixels beyond each edge */
    int bb;
    int np;
    unsigned char pmove[HELM_PARTS];
    float pz[HELM_PARTS];
} Pic;
static Pic pics[2][4][2][2];                    /* part, suit, rows 200 / 400, 1x / 2x */
static char pics_pack[64];                      /* the pack they are from */
static uint8_t disp_art[2][SCR_W * MAXH];       /* the display's copy of art */
static uint8_t disp_marks[2][SCR_W * MAXH];     /* ... and of marks */
static unsigned disp_serial[2], pics_gen;

static void pics_free(void)
{
    Pic *p = &pics[0][0][0][0];
    size_t i;
    for (i = 0; i < sizeof pics / sizeof pics[0][0][0][0]; i++) {
        free(p[i].pix);
        free(p[i].win);
        free(p[i].lay);
        free(p[i].parts);
        free(p[i].hidden);
        free((void *)p[i].border);
    }
    memset(pics, 0, sizeof pics);
    pics_gen++;
}

/* ------------------------------------------------------------------ the helmet's screens
 * Beside a picture <name>.png (cockpitpack.h), optional:
 *
 *     <name>-layers.png   each pixel's part of the helmet, 0 .. 5: chin console and screens,
 *                         bezels and hump, brow, wall and rim, plates, upper corners and hoses
 *                         (an indexed PNG of the picture's size): a hit's static goes on the
 *                         screens of parts 0 .. 2 only (without it, on any part) */
static void load_helmet(Pic *p, const char *dir, int part, int suit, int h, int s)
{
    char path[1400];
    int W = port_ck_cols(SCR_W) * s, H = h * s, w = 0, hh = 0;
    size_t n = (size_t)W * H, i;
    uint8_t *lay = NULL;

    snprintf(path, sizeof path, "%s/%s-%d-%s%s-layers.png", dir, suit_names[suit], h, part_names[part],
             s == 2 ? "-2x" : "");
    if (port_mod_type(path) != 1 || !(lay = port_png_indexed(path, &w, &hh)))
        return;
    if (w != W || hh != H) {
        port_log("Cockpit pack: cannot use %s (an indexed PNG of %dx%d)\n", path, W, H);
        free(lay);
        return;
    }
    for (i = 0; i < n; i++)
        if (lay[i] >= HELM_LAYERS)
            lay[i] = 0;
    p->lay = lay;
}

/* ------------------------------------------------------------------ the layered helmet's files
 * Beside a picture <name>.png:
 *
 *     <name>-parts.png    each pixel's part of the helmet, 1 .. 8, drawn back to front (0: the
 *                         window, where the 3D shows; an indexed PNG of the picture's size).  A
 *                         part's pixels are the picture's and the screen's there (the HUD on it).
 *     <name>-hidden-N.png part N's art under the parts beside it (in front and behind), shown when they
 *                         move apart (the picture's size and colours, 0 = none; optional)
 *     <suit>-helmet.txt   lines "cockpit|goggles part N NAME still|side|side-down DIST": how part N
 *                         moves (still: it meets the screen's edges; side: it meets the top edge;
 *                         side-down: the bottom edge, it moves down, never up) and its distance
 *                         from the eye in metres (the nearer, the more it moves)
 *
 * Without -parts.png the picture is one piece and stays still. */
/* The picture's see-through holes away from the 3D window (the pirate's console screens and map,
 * owner): in the parts map they are the window's (0) and stood still while their
 * console moved, so the frame slid over them and showed what lies under its edges (black bars,
 * the screen's cut text at 320 x 200; the weapon's box, where the 3D drawn taller showed
 * through its black).  A hole that does not reach the picture's edge, is less than half the
 * biggest one (the window) and shares none of its rows, or is less than an eighth of it, goes
 * with the part around it (the one most of its edge touches): what the game shows there moves
 * with its console. */
static void holes_to_parts(uint8_t *parts, int W, int H)
{
    int *lab = malloc((size_t)W * H * sizeof *lab), *st = malloc((size_t)W * H * sizeof *st);
    int nl = 0, big = -1, i, x, y, k;
    struct Hole { int n, y0, y1, edge, nb[HELM_PARTS + 1]; } *hs = NULL;
    if (!lab || !st)
        goto out;
    for (k = 0; k < W * H; k++)
        lab[k] = -1;
    for (k = 0; k < W * H; k++) {
        int sp = 0;
        struct Hole *h2;
        if (parts[k] || lab[k] >= 0)
            continue;
        if (!(h2 = realloc(hs, (size_t)(nl + 1) * sizeof *hs)))
            goto out;
        hs = h2;
        memset(&hs[nl], 0, sizeof *hs);
        hs[nl].y0 = H;
        hs[nl].y1 = -1;
        lab[k] = nl;
        st[sp++] = k;
        while (sp) {
            int q = st[--sp], qx = q % W, qy = q / W, d;
            hs[nl].n++;
            hs[nl].y0 = qy < hs[nl].y0 ? qy : hs[nl].y0;
            hs[nl].y1 = qy > hs[nl].y1 ? qy : hs[nl].y1;
            if (qx == 0 || qy == 0 || qx == W - 1 || qy == H - 1)
                hs[nl].edge = 1;
            for (d = 0; d < 4; d++) {
                int ax = qx + (d == 0) - (d == 1), ay = qy + (d == 2) - (d == 3), a;
                if (ax < 0 || ay < 0 || ax >= W || ay >= H)
                    continue;
                a = ay * W + ax;
                if (parts[a])
                    hs[nl].nb[parts[a]]++;
                else if (lab[a] < 0) {
                    lab[a] = nl;
                    st[sp++] = a;
                }
            }
        }
        if (big < 0 || hs[nl].n > hs[big].n)
            big = nl;
        nl++;
    }
    for (i = 0; i < nl; i++) {
        int best = 0, j;
        if (i == big || hs[i].edge || 2 * hs[i].n >= hs[big].n ||
            (!(hs[i].y1 < hs[big].y0 || hs[i].y0 > hs[big].y1) && 8 * hs[i].n >= hs[big].n))
            continue;
        for (j = 1; j <= HELM_PARTS; j++)
            if (hs[i].nb[j] > hs[i].nb[best])
                best = j;
        if (!best)
            continue;
        for (y = hs[i].y0; y <= hs[i].y1; y++)
            for (x = 0; x < W; x++)
                if (lab[y * W + x] == i)
                    parts[y * W + x] = (uint8_t)best;
        port_log("Cockpit pack: a hole of %d pixels (rows %d-%d) goes with part %d\n", hs[i].n, hs[i].y0, hs[i].y1, best);
    }
out:
    free(lab);
    free(st);
    free(hs);
}

static void load_parts(Pic *p, const char *dir, int part, int suit, int h, int s)
{
    char path[1400], *txt, *line, *next;
    int pw = port_ck_cols(SCR_W), W = pw * s, H = h * s, w = 0, hh = 0, np = 0, i;
    size_t n = (size_t)W * H, k, len;
    uint8_t *parts, *hidden = NULL;

    snprintf(path, sizeof path, "%s/%s-%d-%s%s-parts.png", dir, suit_names[suit], h, part_names[part],
             s == 2 ? "-2x" : "");
    if (port_mod_type(path) != 1 || !(parts = port_png_indexed(path, &w, &hh)))
        return;
    if (w != W || hh != H) {
        port_log("Cockpit pack: cannot use %s (an indexed PNG of %dx%d)\n", path, W, H);
        free(parts);
        return;
    }
    for (k = 0; k < n; k++) {
        if (parts[k] > HELM_PARTS)
            parts[k] = HELM_PARTS;
        if (parts[k] > np)
            np = parts[k];
        if (!parts[k] && p->pix[k])
            parts[k] = 1;               /* art where the map has the window: the back part's */
    }
    holes_to_parts(parts, W, H);
    if (!np || !(hidden = calloc((size_t)np * n, 1))) {
        free(parts);
        return;
    }
    for (i = 0; i < np; i++) {          /* each part's hidden art, if it has any */
        uint8_t *hp;
        snprintf(path, sizeof path, "%s/%s-%d-%s%s-hidden-%d.png", dir, suit_names[suit], h, part_names[part],
                 s == 2 ? "-2x" : "", i + 1);
        if (port_mod_type(path) != 1 || !(hp = port_png_indexed(path, &w, &hh)))
            continue;
        if (w == W && hh == H)
            memcpy(hidden + (size_t)i * n, hp, n);
        else
            port_log("Cockpit pack: cannot use %s (an indexed PNG of %dx%d)\n", path, W, H);
        free(hp);
    }
    for (i = 0; i < HELM_PARTS; i++) {
        p->pmove[i] = HELM_STILL;
        p->pz[i] = 0.13f;
    }
    snprintf(path, sizeof path, "%s/%s-helmet.txt", dir, suit_names[suit]);
    if ((txt = port_mod_load(path, &len)) != NULL && (txt = SDL_realloc(txt, len + 1)) != NULL) {
        txt[len] = 0;
        for (line = txt; *line; line = next) {
            char pn[32], kw[32], nm[32], mv[32];
            int num;
            float d;
            next = strchr(line, '\n');
            if (next)
                *next++ = 0;
            else
                next = line + strlen(line);
            if (line[0] == '#' || sscanf(line, "%31s %31s %d %31s %31s %f", pn, kw, &num, nm, mv, &d) != 6 ||
                strcmp(pn, part_names[part]) || strcmp(kw, "part") || num < 1 || num > HELM_PARTS)
                continue;
            p->pmove[num - 1] = !strcmp(mv, "side") ? HELM_SIDE : !strcmp(mv, "side-down") ? HELM_SIDE_DOWN :
                                !strcmp(mv, "move") ? HELM_MOVE : HELM_STILL;
            if (d > 0.03f && d < 1.0f)
                p->pz[num - 1] = d;
        }
        SDL_free(txt);
    }
    p->parts = parts;
    p->hidden = hidden;
    p->np = np;
    /* the art beyond the picture's edges, for the parts that move off them */
    snprintf(path, sizeof path, "%s/%s-%d-%s%s-border.png", dir, suit_names[suit], h, part_names[part],
             s == 2 ? "-2x" : "");
    if (port_mod_type(path) == 1 && (p->border = port_png_indexed(path, &w, &hh)) != NULL &&
        (w - W != hh - H || w <= W || (w - W) % 2 || w - W > 128)) {
        port_log("Cockpit pack: cannot use %s (an indexed PNG the picture's size and as many pixels more on each side)\n", path);
        free((void *)p->border);
        p->border = NULL;
    }
    p->bb = p->border ? (w - W) / 2 : 0;
    port_log("Cockpit pack: its layered helmet loaded (%d parts%s)\n", np, p->border ? ", borders" : "");
}

static void load(Pic *p, int part, int suit, int h, int s)
{
    char dir[1200], path[1400];
    int pw = port_ck_cols(SCR_W), w = 0, ph = 0, x, y, i, j;
    unsigned char *pix;

    p->tried = 1;
    port_pack_dir("Cockpits", port_ck_pack, dir, sizeof dir);
    snprintf(path, sizeof path, "%s/%s-%d-%s%s.png", dir, suit_names[suit], h, part_names[part], s == 2 ? "-2x" : "");
    if (port_mod_type(path) != 1)
        return;
    pix = port_png_indexed(path, &w, &ph);
    if (!pix || w != pw * s || ph != h * s) {
        port_log("Cockpit pack: cannot use %s (an indexed PNG of %dx%d)\n", path, pw * s, h * s);
        free(pix);
        return;
    }
    p->win = malloc((size_t)pw * h);
    if (!p->win) {
        free(pix);
        return;
    }
    for (y = 0; y < h; y++)                     /* where the 3D shows through any of its pixels */
        for (x = 0; x < pw; x++) {
            int any = 0;
            for (j = 0; j < s; j++)
                for (i = 0; i < s; i++)
                    any |= !pix[(size_t)(y * s + j) * pw * s + x * s + i];
            p->win[(size_t)y * pw + x] = (uint8_t)any;
        }
    p->pix = pix;
    p->s = s;
    p->ok = 1;
    port_log("Cockpit pack: %s loaded\n", path);
    load_helmet(p, dir, part, suit, h, s);
    load_parts(p, dir, part, suit, h, s);
}

/* how many of every 4th pixel of the art (rows 0 .. h, every 2nd row) the screen scr does not show,
 * in 1/256ths (the lock held) */
static int art_off(int part, const uint8_t *scr, int pitch, int h)
{
    int x, y, n = 0, d = 0;
    if (art_h[part] != h)
        return 256;
    for (y = 1; y < h; y += 2)
        for (x = 1; x < SCR_W; x += 4) {
            uint8_t a = art[part][(size_t)y * SCR_W + x];
            if (!a || marks[part][(size_t)y * SCR_W + x])
                continue;
            n++;
            d += scr[(size_t)y * pitch + x] != a;
        }
    return n ? d * 256 / n : 256;
}

/* the display's present: the pack's picture for the screen scr (w x h, pitch), out_h rows tall in
 * the window.  The cockpit or the goggles is the one the game handed over last, unless the screen
 * still shows the other one's art (the game hands over the new art as it draws the frame, before
 * the frame is copied to the screen: on the switch the display may show the last frame once more,
 * with the new art the game's own pixels in the middle, P107) */
int port_ck_get(int w, int h, int out_h, const uint8_t *scr, int pitch, port_ckpic *out)
{
    int part = part_up, suit, r, want, k;
    Pic *p = NULL;

    if (strcmp(pics_pack, port_ck_pack)) {      /* another pack: its pictures when wanted */
        pics_free();
        snprintf(pics_pack, sizeof pics_pack, "%s", port_ck_pack);
    }
    if (!port_ck_pack[0] || !port_mods_dir[0] || part < 0 || w != SCR_W || (h != 200 && h != 400))
        return 0;
    lock();
    if (scr && pitch >= w && art_off(part, scr, pitch, h) > 128 && art_off(!part, scr, pitch, h) < 26)
        part = !part;
    if (disp_serial[part] != art_serial[part]) {
        memcpy(disp_art[part], art[part], sizeof disp_art[part]);
        memcpy(disp_marks[part], marks[part], sizeof disp_marks[part]);
        disp_serial[part] = art_serial[part];
    }
    suit = art_h[part] == h ? art_suit[part] : -1;
    SDL_UnlockMutex(art_mutex);
    if (suit < 0)
        return 0;
    r = h == 400;
    want = out_h >= 2 * h ? 1 : 0;
    for (k = 0; k < 2 && !p; k++) {             /* the one wanted, else the other */
        Pic *q = &pics[part][suit][r][k ? !want : want];
        if (!q->tried)
            load(q, part, suit, h, (k ? !want : want) + 1);
        if (q->ok)
            p = q;
    }
    if (!p)
        return 0;
    out->pix = p->pix;
    out->s = p->s;
    out->pw = port_ck_cols(w);
    out->win = p->win;
    out->art = disp_art[part];
    out->marks = disp_marks[part];
    out->lay = p->lay;
    out->parts = p->parts;
    out->hidden = p->hidden;
    out->np = p->np;
    out->border = p->border;
    out->bb = p->bb;
    memcpy(out->pmove, p->pmove, sizeof out->pmove);
    memcpy(out->pz, p->pz, sizeof out->pz);
    out->serial = (pics_gen * 64u + (unsigned)(p - &pics[0][0][0][0])) * 0x10001u + disp_serial[part];
    out->id = pics_gen * 64u + (unsigned)(p - &pics[0][0][0][0]);
    return 1;
}
