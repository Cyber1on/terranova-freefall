/* missions.c -- the Mission Selector (Terra Nova: Freefall): a screen that starts any campaign
 * mission from the game's main menu, as --mission N does from the command line.
 *
 * Its button is on the main menu, in the middle above the credit line (game pixels 248..392 x
 * 440..464, drawn by src/game/shell/uiwidget.c like the game's Exit button; port_menu_msbutton).
 * The screen is drawn in the style of the game's Random Scenario Builder (owner): its
 * background, palette, arrows and colours (RESCUSTM.RES 0x972) and the game's two fonts
 * (RESGAME.RES 0x164, 0x165), read here from the game's files with a reader of our own (the
 * main thread must not use the game's resource system).  It is drawn by the main thread over the
 * game's main menu, which runs on under it (its pointer stays where it was; this screen draws the
 * game's own pointer at the mouse, options.c).
 *
 * Left: the campaign's missions in the game's order (RESMISS.RES 0x6AD: 39 missions, the two
 * training missions first), 17 a page; a mission with a live-action story scene is marked FMV.
 * By default only the missions reached show (Gameplay page: Mission select: Reached / All).
 * Right: the mission's name, its campaign position and suit, a map of the mission area (or of
 * the whole map) in full colour with the drop point, the map's labels and the forces (the Database
 * Viewer's preview), the game's whole briefing text (RESPTERM.RES 1455) scrolled, the video
 * briefing (BRIEFnnA.SMK) and the story scene played here, and the player's record.
 * Play Mission closes the screen and presses New Game (Skip Instruction) on the game's menu (New
 * Game for the two training missions) with the mission in port_ms_mission, so the campaign starts
 * there the way --mission N starts it (src/game/mission/camplist.c) and goes on after it.
 *
 * Progress (the furthest campaign position reached, and per mission: completed, best score,
 * medals) is kept in Saves/missions.ini, written by the game thread when a mission ends, a game
 * is loaded or a campaign starts (port_ms_progress, port_ms_record).  Without that file it is
 * worked out once from the saved games in Saves.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "port.h"
#include "screen.h"
#include "videosmooth.h"
#include "gameart.h"

int port_missions_open;                 /* the screen is up */
int port_ms_all;                        /* Mission select: All (options.c, tnfree.ini) */
volatile int port_ms_mission;           /* the mission Play Mission starts (camplist.c), 0 none */
volatile int port_ms_clicks;            /* +1 a button click: uiwidget.c plays the game's click sound */
int port_ms_fading;                     /* fading in or out: the main menu's Freefall Options button
                                         * waits (options.c) */
void port_music_pause(int on);          /* xmidi.c */
extern volatile int port_audio_mute;    /* miles.c: all the game's sound faded out */
extern int port_smooth_videos;          /* "Smooth videos: 30 fps" (RIFE, videosmooth.c) */
volatile int port_ms_walk;              /* Play Mission walks to the outfitting (uiwidget.c ms_walk) */
static SDL_SpinLock walk_lock;
static uint64_t walk_at, skip_until;    /* the walk's start; the game's movies skipped until then */
static volatile int walk_ended;         /* 1 the briefing's door was clicked, 2 the walk gave up */
static int hold_pos, hold_made;         /* the black's mission (campaign position); hold_tex has it */
static uint64_t walk_end_at, walk_skip_at;    /* the briefing's door clicked; its movie skipped */

/* uiwidget.c ms_walk (the game's thread): a door of the way was clicked (1 the quarters', 2 the
 * briefing's), 0 the walk gave up */
void port_ms_walked(int step)
{
    uint64_t now = port_now_us();
    SDL_LockSpinlock(&walk_lock);
    skip_until = step == 2 ? now + 6000000 : 0;     /* the briefing's video (and a story scene) */
    if (step != 1) {
        walk_end_at = now;
        walk_skip_at = 0;
        walk_ended = step == 2 ? 1 : 2;
    }
    SDL_UnlockSpinlock(&walk_lock);
    port_log("Mission Selector: %s\n", step == 1 ? "Enter Base" : step == 2 ? "Start Mission Briefing" :
             "a screen not on the way: the walk to the outfitting stops");
}

/* movie.c intro_2DC254 (the game's thread): 1 skips the movie, as a key would. On the way from
 * Play Mission to the outfitting: the campaign's opening, a story scene on entering the base,
 * then the briefing (all can be watched in the selector; the briefing also from the
 * outfitting) */
int port_ms_skip_movie(void)
{
    uint64_t now = port_now_us();
    int skip;
    SDL_LockSpinlock(&walk_lock);
    skip = port_ms_walk || now < skip_until;   /* (a story scene on entering the base too) */
    if (skip && skip_until < now + 4000000)
        skip_until = now + 4000000;     /* the next one of a row of movies too */
    if (skip && walk_ended == 1)
        walk_skip_at = now;
    SDL_UnlockSpinlock(&walk_lock);
    if (skip)
        port_log("Mission Selector: a movie skipped on the way to the outfitting\n");
    return skip;
}
extern int port_menu_msbutton;          /* its button is on the main menu (uiwidget.c) */
extern int port_lockstep, port_bench;
extern char port_user_dir[1024];
extern long camp_pos;                   /* the game's campaign position (camplist.c) */
extern long *save_vars[6];              /* what a saved game's resource 0x6F0 holds (saveload.c) */
extern signed char mission_movie[][2];  /* per mission: its story scene, when (movie.c) */
typedef struct { char name[15]; unsigned char hires; } MsMovieEntry;
extern MsMovieEntry movie_tab[];        /* the movies' file names (movie.c) */
extern unsigned char snd_sfx_vol;       /* the game's sound effects volume 0..127 */
void port_options_pointer(SDL_Renderer *r, const SDL_FRect *view);
void port_options_show(void);
void *port_smk_open_sound(const char *host, float gain, int *w, int *h, uint32_t *frames, double *ms);
int port_smk_next(void *m, const uint8_t **pixels, const uint8_t **pal6);
void port_smk_close(void *m);
void port_mouse_click(void);

#define W 640
#define H 480

/* the game's resource files, bitmaps and fonts (gameart.c) */
typedef GaRes ResFile;
typedef GaBitmap Bitmap;
typedef GaFont Font;
#define rd32(...) ga_rd32(__VA_ARGS__)
#define rd16(...) ga_rd16(__VA_ARGS__)
#define res_open(...) ga_res_open(__VA_ARGS__)
#define res_close(...) ga_res_close(__VA_ARGS__)
#define res_read(...) ga_res_read(__VA_ARGS__)
#define item(...) ga_item(__VA_ARGS__)
#define bm_decode(...) ga_bm_decode(__VA_ARGS__)
#define font_load(...) ga_font_load(__VA_ARGS__)
#define glyph(...) ga_glyph(__VA_ARGS__)
#define text_w(...) ga_text_w(__VA_ARGS__)

/* ------------------------------------------------------------------ the screen's picture */
static uint8_t cv[H][W];                /* the picture in the builder's colours */
static uint8_t back[H][W];              /* the builder's background */
static uint8_t pal[768];
static uint32_t rgba[H * W];
static Bitmap arrow_l, arrow_r, arrow_lh, arrow_rh;     /* the builder's arrows, normal and lit */
static Font fsmall, fbig;
static int teal;
static int art_ok = -1;                 /* the game's art loaded: 1, failed: 0, not tried: -1 */
#define LIGHT 167
#define DARK 165
#define OLIVE 164
#define BLACK 0
#define WHITE 162
#define GREY 36
#define LOCKED 39

static void fill(int x0, int y0, int x1, int y1, int c)
{
    int y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (y = y0; y < y1; y++)
        if (x1 > x0)
            memset(&cv[y][x0], c, x1 - x0);
}

static void panel(int x0, int y0, int x1, int y1)
{
    fill(x0, y0, x1, y1, DARK);
    fill(x0, y0, x1, y0 + 1, LIGHT);
    fill(x0, y0, x0 + 1, y1, LIGHT);
}

static void box(int x0, int y0, int x1, int y1, int c)     /* light line right and below */
{
    fill(x0, y0, x1, y1, c);
    fill(x0, y1, x1 + 1, y1 + 1, LIGHT);
    fill(x1, y0, x1 + 1, y1 + 1, LIGHT);
}

static void text_at(const Font *f, int x, int y, const char *s, int col)
{
    static const struct { uint8_t from, to; } ramp[] = {    /* the colour font's own ramp, greys */
        { 255, WHITE }, { 244, WHITE }, { 245, 32 }, { 246, 34 }, { 247, 36 }, { 248, 38 }, { 249, 36 }, { 251, 39 } };
    for (; *s; s++) {
        int x0, w = glyph(f, (unsigned char)*s, &x0), xx, yy, k;
        for (yy = 0; yy < f->h; yy++)
            for (xx = 0; xx < w; xx++) {
                int sx = x0 + xx, px = x + xx, py = y + yy;
                if (px < 0 || px >= W || py < 0 || py >= H)
                    continue;
                if (f->colour) {
                    uint8_t v = f->b[f->buf + yy * f->row + sx];
                    if (v) {
                        for (k = 0; k < (int)(sizeof ramp / sizeof ramp[0]); k++)
                            if (ramp[k].from == v)
                                v = ramp[k].to;
                        cv[py][px] = v;
                    }
                } else if (f->b[f->buf + yy * f->row + (sx >> 3)] & (0x80 >> (sx & 7)))
                    cv[py][px] = (uint8_t)col;
            }
        x += w;
    }
}

static void ctext(const Font *f, int x0, int x1, int y, const char *s, int col)
{
    text_at(f, x0 + (x1 - x0 - text_w(f, s)) / 2, y, s, col);
}

static void titlebox(int x0, int y0, int x1, int y1, const char *s)
{
    box(x0, y0, x1, y1, BLACK);
    ctext(&fsmall, x0, x1, y0 + (y1 - y0 - fsmall.h) / 2 + 1, s, WHITE);
}

static void blit(const Bitmap *b, int x, int y)
{
    int yy;
    if (b->px)
        for (yy = 0; yy < b->h && y + yy < H; yy++)
            memcpy(&cv[y + yy][x], b->px + yy * b->w, x + b->w <= W ? b->w : W - x);
}

static void blit_turned(const Bitmap *b, int x, int y)  /* turned a quarter clockwise */
{
    int i, j;
    if (b->px)
        for (i = 0; i < b->w; i++)
            for (j = 0; j < b->h; j++)
                if (y + i < H && x + j < W)
                    cv[y + i][x + j] = b->px[(b->h - 1 - j) * b->w + i];
}

static void film_icon(int fx, int fy, int c, int bg)    /* a small film strip */
{
    int k;
    fill(fx, fy, fx + 12, fy + 9, c);
    for (k = 0; k < 3; k++) {
        fill(fx + 1 + 4 * k, fy + 1, fx + 3 + 4 * k, fy + 2, bg);
        fill(fx + 1 + 4 * k, fy + 7, fx + 3 + 4 * k, fy + 8, bg);
    }
    fill(fx + 1, fy + 3, fx + 11, fy + 6, bg);
    fill(fx + 2, fy + 3, fx + 5, fy + 6, c);
    fill(fx + 7, fy + 3, fx + 10, fy + 6, c);
}

static int load_art(void)
{
    ResFile f = { 0 };
    uint8_t *b;
    uint32_t n, in;
    int c;
    Bitmap bg = { 0 }, sel = { 0 };
    const uint8_t *it;

    if (art_ok >= 0)
        return art_ok;
    art_ok = 0;
    if (!res_open(&f, "C:\\TNOVA\\DATA\\RESCUSTM.RES")) {
        port_log("Mission Selector: RESCUSTM.RES not found\n");
        return 0;
    }
    b = res_read(&f, 0x972, &n, &c);
    res_close(&f);
    if (!b || !c) {
        port_log("Mission Selector: the builder's art (RESCUSTM.RES 0x972) not read\n");
        SDL_free(b);
        return 0;
    }
    it = item(b, n, 0, &in);
    if (!bm_decode(it, in, &bg, b, n, pal) || bg.w != W || bg.h != H) {
        SDL_free(b);
        SDL_free(bg.px);
        return 0;
    }
    memcpy(back, bg.px, sizeof back);
    SDL_free(bg.px);
    it = item(b, n, 2, &in);
    teal = OLIVE;
    if (bm_decode(it, in, &sel, NULL, 0, NULL) && sel.w > 100 && sel.h > 8)
        teal = sel.px[8 * sel.w + 100];
    SDL_free(sel.px);
    it = item(b, n, 21, &in);
    bm_decode(it, in, &arrow_l, NULL, 0, NULL);
    it = item(b, n, 23, &in);
    bm_decode(it, in, &arrow_r, NULL, 0, NULL);
    it = item(b, n, 22, &in);           /* lit while clicked, as in the builder (rsb_2CF11C) */
    bm_decode(it, in, &arrow_lh, NULL, 0, NULL);
    it = item(b, n, 24, &in);
    bm_decode(it, in, &arrow_rh, NULL, 0, NULL);
    SDL_free(b);
    if (!res_open(&f, "C:\\TNOVA\\DATA\\RESGAME.RES"))
        return 0;
    if (!font_load(&f, 0x164, &fsmall) || !font_load(&f, 0x165, &fbig)) {
        res_close(&f);
        port_log("Mission Selector: the game's fonts (RESGAME.RES 0x164 / 0x165) not read\n");
        return 0;
    }
    res_close(&f);
    art_ok = arrow_l.px && arrow_r.px;
    return art_ok;
}

/* ------------------------------------------------------------------ the campaign */
typedef struct {
    int m;                              /* the game's mission number */
    char name[48];
    int suit;                           /* 0 Scout, 1 Standard, 2 Heavy, 3 Pirate */
    int story[2], nstory;               /* its live-action scenes (movie_tab) */
} Mission;
static Mission list[48];
static int nlist = -1;
static const char *suit_names[4] = { "Scout", "Standard", "Heavy", "Pirate" };

static void title_case(char *d, size_t n, const char *s)
{
    size_t o = 0;
    while (*s && o + 1 < n) {
        const char *e = s;
        int digit = 0, first = 1;
        while (*e && *e != ' ')
            digit |= *e >= '0' && *e <= '9', e++;
        for (; s < e && o + 1 < n; s++) {
            char c = *s;
            if (!digit && c >= 'A' && c <= 'Z' && !first)
                c = (char)(c - 'A' + 'a');
            if (!digit && c >= 'a' && c <= 'z' && first)
                c = (char)(c - 'a' + 'A');
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                first = 0;
            d[o++] = c;
        }
        while (*s == ' ' && o + 1 < n)
            d[o++] = *s++;
    }
    d[o] = 0;
}

static int load_campaign(void)
{
    ResFile f = { 0 };
    uint8_t *order, *names, *kit[2];
    uint32_t on, nn, kn[2], in;
    int c, i;

    if (nlist >= 0)
        return nlist > 0;
    nlist = 0;
    if (!res_open(&f, "C:\\TNOVA\\DATA\\RESMISS.RES"))
        return 0;
    order = res_read(&f, 0x6AD, &on, &c);
    names = res_read(&f, 0x6AA, &nn, &c);
    kit[0] = res_read(&f, 0x6A8, &kn[0], &c);
    kit[1] = res_read(&f, 0x6A9, &kn[1], &c);
    res_close(&f);
    if (order && on >= 6 && rd32(order + 2) + 0x2F <= on) {
        const uint8_t *p = order + rd32(order + 2);
        for (i = 0; i < 0x2F; i++) {
            int m = (signed char)p[i];
            Mission *ms;
            const uint8_t *s;
            if (m <= 0 || (m >= 41 && m <= 45))  /* the scenario builder's own missions follow */
                break;
            ms = &list[nlist++];
            ms->m = m;
            s = item(names, nn, (unsigned)(m - 1), &in);
            if (s && in) {
                char raw[48];
                SDL_snprintf(raw, sizeof raw, "%.*s", (int)(in < 47 ? in : 47), (const char *)s);
                title_case(ms->name, sizeof ms->name, raw);
            } else
                SDL_snprintf(ms->name, sizeof ms->name, "Mission %d", m);
            /* the suit (equip.c): the mission's kit record, else the first one */
            {
                int k = m > 0x29, mm = m > 0x28 ? m - 0x29 : m;
                const uint8_t *src = item(kit[k], kn[k], (unsigned)(mm * 6), &in);
                if (!src || !in || !src[0])
                    src = item(kit[k], kn[k], 0, &in);
                ms->suit = src && in > 5 ? (src[5] > 3 ? 1 : src[5]) : 1;
            }
            ms->nstory = 0;
            if (m == 46) {              /* the campaign's opening scenes (intro_2DBED8) */
                ms->story[ms->nstory++] = 0x38;
                ms->story[ms->nstory++] = 0x39;
            } else if (m < 0x38 && mission_movie[m][0] > 0)
                ms->story[ms->nstory++] = mission_movie[m][0];
        }
    }
    SDL_free(order);
    SDL_free(names);
    SDL_free(kit[0]);
    SDL_free(kit[1]);
    if (!nlist)
        port_log("Mission Selector: the campaign (RESMISS.RES) not read\n");
    return nlist > 0;
}

/* a movie's host path: the game folder's MOVIE, else the CD's */
static int movie_path(const char *name, char *host, int size)
{
    char dos[64];
    SDL_PathInfo inf;
    SDL_snprintf(dos, sizeof dos, "C:\\TNOVA\\MOVIE\\%s", name);
    port_path(dos, host, size);
    if (SDL_GetPathInfo(host, &inf) && inf.type == SDL_PATHTYPE_FILE)
        return 1;
    SDL_snprintf(dos, sizeof dos, "D:\\MOVIE\\%s", name);
    port_path(dos, host, size);
    return SDL_GetPathInfo(host, &inf) && inf.type == SDL_PATHTYPE_FILE;
}

static int has_briefing_video(int m)
{
    char name[32], host[1100];
    SDL_snprintf(name, sizeof name, "BRIEF%02dA.SMK", m);
    return movie_path(name, host, sizeof host);
}

/* ------------------------------------------------------------------ progress (Saves/missions.ini) */
typedef struct {
    unsigned char played, done, medals;
    long best;
} Record;
static Record rec[64];
static int reached;                     /* the furthest campaign position reached */
static int pr_loaded;
static SDL_SpinLock pr_lock;

static void pr_file(char *out, size_t n)
{
    SDL_snprintf(out, n, "%s/missions.ini", port_user_dir);
}

static void pr_save(void)
{
    char path[1100];
    FILE *f;
    int m;
    pr_file(path, sizeof path);
    if (!(f = fopen(path, "w")))
        return;
    fprintf(f, "; Terra Nova: Freefall - Mission Selector progress\n");
    fprintf(f, "reached=%d\n", reached);
    for (m = 1; m < 64; m++)
        if (rec[m].played)
            fprintf(f, "m%d=%d,%ld,%d\n", m, rec[m].done, rec[m].best, rec[m].medals);
    fclose(f);
}

/* without missions.ini: the furthest campaign position of the saved games */
static void pr_seed(void)
{
    int slot, k, var = -1;
    for (k = 0; k < 6; k++)
        if (save_vars[k] == &camp_pos)
            var = k;
    if (var < 1)
        return;
    for (slot = 1; slot <= 9; slot++) {
        char dos[64];
        ResFile f = { 0 };
        uint8_t *v;
        uint32_t n;
        SDL_snprintf(dos, sizeof dos, "save%02d.res", slot);
        if (!res_open(&f, dos))
            continue;
        v = res_read(&f, 0x6F0, &n, NULL);
        if (v && n >= 24 && rd32(v) == 0x6EF0018) {
            long pos = (long)rd32(v + var * 4);
            if (pos > reached && pos < 0x30)
                reached = (int)pos;
        }
        SDL_free(v);
        res_close(&f);
    }
    port_log("Mission Selector: progress from the saved games: campaign position %d\n", reached);
}

static void pr_load(void)               /* (pr_lock held) */
{
    char path[1100], line[128];
    FILE *f;
    if (pr_loaded)
        return;
    pr_loaded = 1;
    pr_file(path, sizeof path);
    if (!(f = fopen(path, "r"))) {
        pr_seed();
        pr_save();
        return;
    }
    while (fgets(line, sizeof line, f)) {
        int m, d, md;
        long b;
        if (sscanf(line, "reached=%d", &d) == 1)
            reached = d;
        else if (sscanf(line, "m%d=%d,%ld,%d", &m, &d, &b, &md) == 4 && m > 0 && m < 64) {
            rec[m].played = 1;
            rec[m].done = d != 0;
            rec[m].best = b;
            rec[m].medals = (unsigned char)md;
        }
    }
    fclose(f);
}

/* game thread: the campaign is at position pos (a mission ended, a game was loaded) */
void port_ms_progress(long pos)
{
    if (port_lockstep || port_bench || pos <= 0 || pos >= 0x30)
        return;
    SDL_LockSpinlock(&pr_lock);
    pr_load();
    if (pos > reached) {
        reached = (int)pos;
        pr_save();
    }
    SDL_UnlockSpinlock(&pr_lock);
}

/* game thread: mission m ended: won, its score, the player's medals (a bit each) */
void port_ms_record(long m, int won, long score, unsigned medals)
{
    int n = 0;
    if (port_lockstep || port_bench || m <= 0 || m >= 64)
        return;
    for (; medals; medals &= medals - 1)
        n++;
    SDL_LockSpinlock(&pr_lock);
    pr_load();
    if (!rec[m].played || score > rec[m].best || (won && !rec[m].done)) {
        if (!rec[m].played || score > rec[m].best)
            rec[m].best = score;
        if (n > rec[m].medals)
            rec[m].medals = (unsigned char)n;
    }
    rec[m].played = 1;
    rec[m].done |= won != 0;
    pr_save();
    SDL_UnlockSpinlock(&pr_lock);
}

static int pos_open(int pos)            /* the list's position pos can be chosen */
{
    return port_ms_all || pos <= (reached < 3 ? 3 : reached);
}

static int furthest(void)               /* the furthest position reached (at least the first
                                         * after the training) */
{
    int p = reached < 3 ? 3 : reached;
    return p > nlist ? nlist : p;
}

/* ------------------------------------------------------------------ the chosen mission */
#define ROWS 17
#define BW 281                          /* the map's box inside (game pixels 344..624 x 85..231) */
#define BH 147
#define BX 344
#define BY 85
#define VIS 8                           /* briefing lines shown */
static int sel = -1, page_top, whole_map;
static char blines[240][64];
static int nbl, btop;
static int shown_m = -1, shown_whole = -1;
static uint32_t mapimg[BH][BW];
static int map_ok;

static void briefing_load(int m)
{
    ResFile f = { 0 };
    uint8_t *b;
    uint32_t n, in;
    int c;
    const uint8_t *t;
    char cur[64];
    int cl = 0;
    const int tw = 250;

    nbl = btop = 0;
    if (!res_open(&f, "C:\\TNOVA\\DATA\\RESPTERM.RES"))
        return;
    b = res_read(&f, 1455, &n, &c);
    res_close(&f);
    t = c ? item(b, n, (unsigned)m, &in) : NULL;
    if (t) {
        uint32_t i = 0;
        cur[0] = 0;
        while (i < in && t[i] && nbl < 239) {
            char word[64];
            int wl = 0;
            if (t[i] == '\r') {
                i++;
                continue;
            }
            if (t[i] == '\n') {
                SDL_snprintf(blines[nbl++], 64, "%s", cur);
                cur[0] = 0;
                cl = 0;
                i++;
                continue;
            }
            if (t[i] == ' ' || t[i] == '\t') {
                i++;
                continue;
            }
            while (i < in && t[i] && t[i] != ' ' && t[i] != '\t' && t[i] != '\n' && t[i] != '\r' && wl < 62)
                word[wl++] = (char)t[i++];
            word[wl] = 0;
            {
                char trial[130];
                SDL_snprintf(trial, sizeof trial, "%s%s%s", cur, cl ? " " : "", word);
                if (cl && (text_w(&fsmall, trial) > tw || strlen(trial) > 63)) {
                    SDL_snprintf(blines[nbl++], 64, "%s", cur);
                    SDL_snprintf(cur, sizeof cur, "%s", word);
                } else
                    SDL_snprintf(cur, sizeof cur, "%s", trial);
                cl = 1;
            }
        }
        if (cl && nbl < 240)
            SDL_snprintf(blines[nbl++], 64, "%s", cur);
        while (nbl && !blines[nbl - 1][0])
            nbl--;
    }
    SDL_free(b);
}

/* --- the map preview (the Database Viewer's missionMapData) */
static const uint8_t slope_n[64] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
                                     4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7,
                                     7, 7, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 4, 4 };
typedef struct { float x, y; int side; } Dot;
typedef struct { float x, y, w, h; char t[48]; } Label;
static uint32_t *map512;                /* the whole map, 0xRRGGBB */
static Dot dots[256];
static Label labels[32];
static int ndots, nlabels, has_drop;
static float drop_x, drop_y;

static int map_load(int m)
{
    char dos[128], name[64], planet[40];
    ResFile mf = { 0 }, mp = { 0 }, pf = { 0 }, rg = { 0 };
    uint8_t *mn = NULL, *g = NULL, *info = NULL, *kind = NULL, *col = NULL, *shade = NULL, *ppal = NULL;
    uint8_t *lim = NULL, *ovr = NULL, *ann = NULL, *dropr = NULL, *sides = NULL, *objs = NULL;
    uint32_t n, gn = 0, kn = 0, cn = 0, sn = 0, pn = 0, limn = 0, ovn = 0, an = 0, on = 0, sdn = 0, dn = 0, in;
    uint8_t rgb[768];
    Bitmap ov = { 0 };
    int ovx = 0, ovy = 0, c, x, y, ok = 0;
    const char *why = "MISSn.RES";

    ndots = nlabels = has_drop = 0;
    SDL_snprintf(dos, sizeof dos, "C:\\TNOVA\\DATA\\MISS%d.RES", m);
    if (!res_open(&mf, dos))
        return 0;
    mn = res_read(&mf, 170, &n, &c);
    if (!mn)
        goto out;
    SDL_snprintf(name, sizeof name, "%.*s", (int)(n < 60 ? n : 60), (const char *)mn);
    SDL_snprintf(dos, sizeof dos, "D:\\MAPS\\%s", name);
    if (!res_open(&mp, dos)) {
        port_log("Mission Selector: map %s not found (the CD's MAPS)\n", name);
        goto out;
    }
    why = "the map's heights";
    info = res_read(&mp, 0x50, &n, &c);
    g = res_read(&mp, 0x56, &gn, &c);
    if (!info || n < 0x34 || !g || gn < 512u * 0x603u)
        goto out;
    why = "the planet's file";
    SDL_snprintf(planet, sizeof planet, "%.*s", 0x20, (const char *)info + 0x14);
    SDL_snprintf(dos, sizeof dos, "C:\\TNOVA\\DATA\\%s", planet);
    if (!res_open(&pf, dos))
        goto out;
    kind = res_read(&pf, 0x2B, &kn, &c);
    col = res_read(&pf, 0x47, &cn, &c);
    shade = res_read(&pf, 0x36, &sn, &c);
    ppal = res_read(&pf, 0x32, &pn, &c);
    why = "the planet's colours";
    if (!kind || !kn || !col || !shade || !ppal || pn > 768)
        goto out;
    memset(rgb, 0, sizeof rgb);
    memcpy(rgb + 768 - pn, ppal, pn);
    if (res_open(&rg, "C:\\TNOVA\\DATA\\RESGAME.RES")) {    /* a mission's own drawn area */
        const uint8_t *l;
        lim = res_read(&rg, 395, &limn, &c);
        l = item(lim, limn, (unsigned)m, &in);
        if (l && in >= 4 && !(l[1] & 0x80) && (ovr = res_read(&rg, 396 + m, &ovn, &c)) != NULL) {
            const uint8_t *b = c ? item(ovr, ovn, 0, &in) : ovr;
            if (!c)
                in = ovn;
            if (bm_decode(b, in, &ov, NULL, 0, NULL)) {
                ovx = l[0] | l[1] << 8;
                ovy = (short)(l[2] | l[3] << 8);
            }
        }
    }
    if (!map512)
        map512 = SDL_malloc(512 * 512 * 4);
    if (!map512)
        goto out;
    for (y = 0; y < 512; y++)
        for (x = 0; x < 512; x++) {
            uint32_t o = (uint32_t)y * 0x603 + (uint32_t)x * 3, ci, si;
            int h0 = (short)(g[o + 1] | g[o + 2] << 8) >> 4, h1 = (short)(g[o + 4] | g[o + 5] << 8) >> 4, d, s;
            int cc;
            const uint8_t *sl = slope_n;
            uint8_t s8[64];
            if (ov.px && x >= ovx && x < ovx + ov.w && y >= ovy && y < ovy + ov.h) {
                cc = ov.px[(y - ovy) * ov.w + x - ovx];
                if (m == 8) {
                    memset(s8, 4, 32);
                    memset(s8 + 32, 5, 32);
                    sl = s8;
                }
            } else {
                ci = ((uint32_t)((g[o] & 0x3Fu) < kn ? kind[g[o] & 0x3F] : 0) << 8) + (uint32_t)((h0 + 0x800) >> 4);
                cc = ci < cn ? col[ci] : 0;
            }
            d = ((h0 - h1) >> 1) + 0x20;
            d = d < 0 ? 0 : d > 63 ? 63 : d;
            s = sl[d];
            si = ((uint32_t)s << 8) + (uint32_t)cc;
            c = si < sn ? shade[si] : cc;
            map512[y * 512 + x] = (uint32_t)rgb[c * 3] << 16 | (uint32_t)rgb[c * 3 + 1] << 8 | rgb[c * 3 + 2];
        }
    ok = 1;
    /* the map's labels, the drop point and the forces (MISSn.RES) */
    if ((ann = res_read(&mf, 0xAF, &an, &c)) != NULL && c) {
        unsigned k;
        for (k = 0; k < rd16(ann) && nlabels < 32; k++) {
            const uint8_t *a = item(ann, an, k, &in);
            Label *lb;
            if (!a || in < 0x1A || a[0] != 0)
                continue;
            lb = &labels[nlabels++];
            lb->x = (int32_t)rd32(a + 5) / 65536.0f;
            lb->y = (int32_t)rd32(a + 9) / 65536.0f;
            lb->w = (int32_t)rd32(a + 13) / 65536.0f;
            lb->h = (int32_t)rd32(a + 17) / 65536.0f;
            SDL_snprintf(lb->t, sizeof lb->t, "%.*s", (int)(in - 0x19 < 47 ? in - 0x19 : 47), (const char *)a + 0x19);
        }
    }
    if ((dropr = res_read(&mf, 0xB0, &dn, &c)) != NULL && dn >= 8) {
        has_drop = 1;
        drop_x = (int32_t)rd32(dropr) / 65536.0f;
        drop_y = (int32_t)rd32(dropr + 4) / 65536.0f;
    }
    sides = res_read(&mf, 0xAE, &sdn, &c);
    if ((objs = res_read(&mf, 0xAD, &on, &c)) != NULL && c) {
        unsigned k;
        for (k = 0; k < rd16(objs) && ndots < 256; k++) {
            const uint8_t *o = item(objs, on, k, &in);
            int cls, squad, ai;
            if (!o || in < 0x20)
                continue;
            cls = o[0];
            if (cls < 1 || cls > 4)
                continue;
            squad = o[7];
            ai = cls != 4 || squad || o[0x1E] || o[0x1F];
            dots[ndots].x = (int32_t)rd32(o + 8) / 65536.0f;
            dots[ndots].y = (int32_t)rd32(o + 12) / 65536.0f;
            dots[ndots].side = !ai ? 0 : ((sides && (uint32_t)squad < sdn ? sides[squad] : 2) == 1 ? 1 : 2);
            ndots++;
        }
    }
out:
    if (!ok)
        port_log("Mission Selector: no map preview for mission %d (%s)\n", m, why);
    SDL_free(mn); SDL_free(g); SDL_free(info); SDL_free(kind); SDL_free(col); SDL_free(shade); SDL_free(ppal);
    SDL_free(lim); SDL_free(ovr); SDL_free(ann); SDL_free(dropr); SDL_free(sides); SDL_free(objs);
    SDL_free(ov.px);
    res_close(&mf);
    res_close(&mp);
    res_close(&pf);
    res_close(&rg);
    return ok;
}

static void mput(int x, int y, uint32_t c)
{
    if (x >= 0 && x < BW && y >= 0 && y < BH)
        mapimg[y][x] = c;
}

/* map text: the small font, with a black outline */
static void mtext(int x, int y, const char *s, uint32_t c)
{
    int pass, dx, dy;
    for (pass = 0; pass < 2; pass++) {
        int cx = x;
        const char *p;
        for (p = s; *p; p++) {
            int x0, w = glyph(&fsmall, (unsigned char)*p, &x0), xx, yy;
            for (yy = 0; yy < fsmall.h; yy++)
                for (xx = 0; xx < w; xx++) {
                    int sx = x0 + xx;
                    if (!(fsmall.b[fsmall.buf + yy * fsmall.row + (sx >> 3)] & (0x80 >> (sx & 7))))
                        continue;
                    if (pass)
                        mput(cx + xx, y + yy, c);
                    else
                        for (dy = -1; dy <= 1; dy++)
                            for (dx = -1; dx <= 1; dx++)
                                mput(cx + xx + dx, y + yy + dy, 0);
                }
            cx += w;
        }
    }
}

static void map_make(int m, int whole)
{
    float fx0 = 1e9f, fx1 = -1e9f, fy0 = 1e9f, fy1 = -1e9f, fw, fh, x0, y0, s;
    int i, j, a, b, k;

    if (m != shown_m)
        map_ok = map_load(m);
    shown_m = m;
    shown_whole = whole;
    memset(mapimg, 0, sizeof mapimg);
    if (!map_ok)
        return;
#define TAKE(x, y) do { if ((x) < fx0) fx0 = (x); if ((x) > fx1) fx1 = (x); if ((y) < fy0) fy0 = (y); if ((y) > fy1) fy1 = (y); } while (0)
    if (has_drop)
        TAKE(drop_x, drop_y);
    for (k = 0; k < nlabels; k++) {
        TAKE(labels[k].x, labels[k].y);
        TAKE(labels[k].x + (labels[k].w > 30 ? labels[k].w : 30), labels[k].y + (labels[k].h > 12 ? labels[k].h : 12));
    }
    for (k = 0; k < ndots; k++)
        TAKE(dots[k].x, dots[k].y);
#undef TAKE
    if (whole || fx1 < fx0) {           /* the whole map, its middle in the box */
        s = (float)BH / 512;
        x0 = -(BW - BH) / 2.0f / s;
        y0 = 0;
    } else {
        fh = fy1 - fy0 + 60;
        fw = fh * BW / BH;
        if (fx1 - fx0 + 60 > fw) {
            fw = fx1 - fx0 + 60;
            fh = fw * BH / BW;
        }
        x0 = (fx0 + fx1) / 2 - fw / 2;
        y0 = (fy0 + fy1) / 2 - fh / 2;
        x0 = fw >= 512 ? (512 - fw) / 2 : x0 < 0 ? 0 : x0 > 512 - fw ? 512 - fw : x0;
        y0 = fh >= 512 ? (512 - fh) / 2 : y0 < 0 ? 0 : y0 > 512 - fh ? 512 - fh : y0;
        s = BW / fw;
    }
    for (j = 0; j < BH; j++)            /* each pixel: the mean of 3 x 3 points of its square */
        for (i = 0; i < BW; i++) {
            unsigned r = 0, gg = 0, bb = 0;
            for (b = 0; b < 3; b++)
                for (a = 0; a < 3; a++) {
                    int sx = (int)SDL_floorf(x0 + (i + (a + 0.5f) / 3) / s);
                    int sy = (int)SDL_floorf(y0 + (j + (b + 0.5f) / 3) / s);
                    if (sx >= 0 && sx < 512 && sy >= 0 && sy < 512) {
                        uint32_t p = map512[sy * 512 + sx];
                        r += p >> 16 & 0xFF;
                        gg += p >> 8 & 0xFF;
                        bb += p & 0xFF;
                    }
                }
            mapimg[j][i] = (r / 9) << 16 | (gg / 9) << 8 | bb / 9;
        }
#define MX(x) ((int)SDL_floorf(((x) - x0) * s))
#define MY(y) ((int)SDL_floorf(((y) - y0) * s))
    for (k = 0; k < ndots; k++) {       /* the forces: neutral grey, allied green, hostile red */
        static const uint32_t cols[3] = { 0xD0D0D0, 0x5FE08A, 0xFF5A4A };
        int x = MX(dots[k].x), y = MY(dots[k].y);
        for (b = -2; b <= 2; b++)
            for (a = -2; a <= 2; a++)
                mput(x + a, y + b, a > -2 && a < 2 && b > -2 && b < 2 ? cols[dots[k].side] : 0);
    }
    if (has_drop) {                     /* the drop point: a yellow ring and cross */
        int x = MX(drop_x), y = MY(drop_y), pass;
        for (pass = 0; pass < 2; pass++)
            for (b = -9; b <= 9; b++)
                for (a = -9; a <= 9; a++) {
                    float d = SDL_sqrtf((float)(a * a + b * b)), lim = pass ? 0.5f : 1.5f;
                    int on = SDL_fabsf(d - 5) <= lim || ((a == 0 || (!pass && (a == 1 || a == -1))) && b >= -8 && b <= 8) ||
                             ((b == 0 || (!pass && (b == 1 || b == -1))) && a >= -8 && a <= 8);
                    if (on)
                        mput(x + a, y + b, pass ? 0xFFE066 : 0);
                }
    }
    for (k = 0; k < nlabels; k++)
        mtext(MX(labels[k].x), MY(labels[k].y), labels[k].t, 0xFFFFFF);
    if (has_drop) {                     /* under the cross, above, right or left: where no label is */
        int w = text_w(&fsmall, "DROP POINT"), cx = MX(drop_x), cy = MY(drop_y), best = 0, least = 1 << 30, t;
        const int px[4] = { cx - w / 2, cx - w / 2, cx + 10, cx - 10 - w };
        const int py[4] = { cy + 9, cy - 9 - fsmall.h, cy - fsmall.h / 2, cy - fsmall.h / 2 };
        for (t = 0; t < 4 && least; t++) {
            int over = 0;
            for (k = 0; k < nlabels; k++) {
                int lx = MX(labels[k].x), ly = MY(labels[k].y), lw = text_w(&fsmall, labels[k].t);
                int ox = SDL_min(px[t] + w + 2, lx + lw + 2) - SDL_max(px[t], lx);
                int oy = SDL_min(py[t] + fsmall.h, ly + fsmall.h) - SDL_max(py[t], ly);
                if (ox > 0 && oy > 0)
                    over += ox * oy;
            }
            if (px[t] < 0 || px[t] + w > BW || py[t] < 0 || py[t] + fsmall.h > BH)
                over += 10000;          /* (partly off the box) */
            if (over < least) {
                least = over;
                best = t;
            }
        }
        mtext(px[best], py[best], "DROP POINT", 0xFFE066);
    }
#undef MX
#undef MY
}

static void choose(int pos)             /* the list's position pos (1-based) */
{
    if (pos < 1 || pos > nlist || !pos_open(pos))
        return;
    if (sel != pos) {
        sel = pos;
        briefing_load(list[pos - 1].m);
    }
    page_top = (pos - 1) / ROWS * ROWS;
}

/* ------------------------------------------------------------------ drawing the screen */
enum { HIT_NONE, HIT_ROW, HIT_PGL, HIT_PGR, HIT_MAPL, HIT_MAPR, HIT_UP, HIT_DOWN, HIT_TRACK, HIT_BRIEF,
       HIT_VIDEO, HIT_STORY, HIT_MENU, HIT_OPTIONS, HIT_PLAY };
typedef struct { short x0, y0, x1, y1; } Rect;
static const Rect hits[] = {
    [HIT_PGL] = { 16, 404, 158, 424 }, [HIT_PGR] = { 158, 404, 301, 424 },
    [HIT_MAPL] = { 342, 237, 484, 255 }, [HIT_MAPR] = { 484, 237, 627, 255 },
    [HIT_UP] = { 611, 260, 626, 274 }, [HIT_DOWN] = { 611, 356, 626, 371 }, [HIT_TRACK] = { 611, 275, 626, 356 },
    [HIT_BRIEF] = { 342, 258, 611, 373 },
    [HIT_VIDEO] = { 342, 376, 483, 394 }, [HIT_STORY] = { 486, 376, 627, 394 },
    [HIT_MENU] = { 16, 444, 201, 470 }, [HIT_OPTIONS] = { 229, 444, 414, 470 }, [HIT_PLAY] = { 443, 444, 626, 470 },
};
static int hover = HIT_NONE, hover_row = -1;
static int lit = HIT_NONE;              /* the arrow just clicked, lit for 250 ms like the builder's */
static uint64_t lit_until;

/* the arrow's art: lit when it was just clicked */
static const Bitmap *arrow(int hit, int right)
{
    const Bitmap *b = right ? &arrow_rh : &arrow_lh;
    if (lit == hit && b->px)
        return b;
    return right ? &arrow_r : &arrow_l;
}

static int hit_at(int x, int y, int *row)
{
    int i;
    *row = -1;
    if (x >= 16 && x < 301 && y >= 62 && y < 62 + ROWS * 20 && (y - 62) % 20 < 18) {
        *row = page_top + (y - 62) / 20 + 1;
        return *row <= nlist ? HIT_ROW : HIT_NONE;
    }
    for (i = HIT_PGL; i <= HIT_PLAY; i++)
        if (x >= hits[i].x0 && x < hits[i].x1 && y >= hits[i].y0 && y < hits[i].y1)
            return i;
    return HIT_NONE;
}

static int movie_in_box(void);          /* a movie plays in the map's box */

static void draw(void)
{
    char buf[96];
    int i, y;
    const Mission *ms = sel >= 1 ? &list[sel - 1] : NULL;

    memcpy(cv, back, sizeof cv);
    fill(0, 39, W, 440, BLACK);
    fill(17, 6, 625, 33, BLACK);
    ctext(&fbig, 16, 625, 10, "Mission Selector", 0);
    /* left: the campaign */
    panel(2, 40, 313, 437);
    titlebox(16, 43, 300, 58, "Campaign Missions");
    for (i = 0, y = 62; i < ROWS; i++, y += 20) {
        int pos = page_top + i + 1, s = pos == sel, open = pos <= nlist && pos_open(pos);
        int c = s ? WHITE : open ? (hover == HIT_ROW && hover_row == pos ? WHITE : GREY) : LOCKED, bg = s ? teal : OLIVE;
        const Mission *m;
        if (pos > nlist) {
            box(16, y, 300, y + 17, OLIVE);
            continue;
        }
        m = &list[pos - 1];
        box(16, y, 300, y + 17, bg);
        SDL_snprintf(buf, sizeof buf, "%2d", pos);
        text_at(&fsmall, 22, y + 3, buf, c);
        if (!open) {
            text_at(&fsmall, 42, y + 3, "Not reached yet", c);
            continue;
        }
        text_at(&fsmall, 42, y + 3, m->name, c);
        if (m->nstory) {
            int lx = 298 - text_w(&fsmall, "FMV") - 4;
            text_at(&fsmall, lx, y + 3, "FMV", c);
            film_icon(lx - 15, y + 4, c, bg);
        }
    }
    box(16, 404, 300, 423, OLIVE);
    if (page_top > 0)
        blit(arrow(HIT_PGL, 0), 18, 407);
    if (page_top + ROWS < nlist)
        blit(arrow(HIT_PGR, 1), 284, 407);
    SDL_snprintf(buf, sizeof buf, "Missions %d-%d of %d", page_top + 1,
                 page_top + ROWS < nlist ? page_top + ROWS : nlist, nlist);
    ctext(&fsmall, 16, 300, 408, buf, WHITE);
    /* right: the chosen mission */
    panel(328, 40, 638, 437);
    titlebox(342, 43, 626, 58, ms ? ms->name : "");
    box(342, 62, 626, 79, OLIVE);
    if (ms) {
        SDL_snprintf(buf, sizeof buf, "Mission %d of %d    Suit: %s", sel, nlist, suit_names[ms->suit & 3]);
        ctext(&fsmall, 342, 626, 66, buf, WHITE);
    }
    box(342, 83, 626, 233, BLACK);      /* the map goes in here, in full colour (to_rgba) */
    if (movie_in_box()) {               /* the movie in the map's box: its two buttons under it */
        box(342, 237, 482, 254, OLIVE);
        ctext(&fsmall, 342, 482, 241, "Stop", hover == HIT_MAPL ? WHITE : GREY);
        box(486, 237, 626, 254, OLIVE);
        ctext(&fsmall, 486, 626, 241, "Fullscreen", hover == HIT_MAPR ? WHITE : GREY);
    } else {
        box(342, 237, 626, 254, OLIVE);
        blit(arrow(HIT_MAPL, 0), 344, 239);
        blit(arrow(HIT_MAPR, 1), 610, 239);
        ctext(&fsmall, 342, 626, 241, whole_map ? "Map: Whole Map" : "Map: Mission Area",
              hover == HIT_MAPL || hover == HIT_MAPR ? WHITE : GREY);
    }
    box(342, 258, 626, 372, BLACK);
    for (i = 0; i < VIS && btop + i < nbl; i++)
        text_at(&fsmall, 350, 264 + 13 * i, blines[btop + i], WHITE);
    blit_turned(arrow(HIT_UP, 0), 611, 260);
    blit_turned(arrow(HIT_DOWN, 1), 611, 356);
    fill(611, 275, 625, 355, DARK);
    {
        int th = nbl > VIS ? 80 * VIS / nbl : 80, ty;
        if (th < 10)
            th = 10;
        ty = 276 + (nbl > VIS ? (80 - th) * btop / (nbl - VIS) : 0);
        fill(612, ty, 624, ty + th - 1, OLIVE);
        fill(612, ty + th - 1, 625, ty + th, LIGHT);
        fill(624, ty, 625, ty + th, LIGHT);
    }
    {
        int vid = ms && has_briefing_video(ms->m), sto = ms && ms->nstory;
        box(342, 376, 482, 393, OLIVE);
        ctext(&fsmall, 342, 482, 379, "Video Briefing", !vid ? LOCKED : hover == HIT_VIDEO ? WHITE : GREY);
        box(486, 376, 626, 393, OLIVE);
        ctext(&fsmall, 486, 626, 379, "Story FMV", !sto ? LOCKED : hover == HIT_STORY ? WHITE : GREY);
    }
    titlebox(342, 397, 626, 412, "Your Record");
    box(342, 416, 626, 433, OLIVE);
    if (ms) {
        Record r;
        SDL_LockSpinlock(&pr_lock);
        r = rec[ms->m & 63];
        SDL_UnlockSpinlock(&pr_lock);
        if (!r.played)
            SDL_snprintf(buf, sizeof buf, "Not played yet");
        else
            SDL_snprintf(buf, sizeof buf, "%s    Medals: %d    Best: %ld", r.done ? "Completed" : "Not completed",
                         r.medals, r.best);
        ctext(&fsmall, 342, 626, 419, buf, GREY);
    }
    /* the bottom buttons in the builder's own frames (no light under the mouse, as there) */
    {
        static const short bx[3][2] = { { 16, 200 }, { 229, 413 }, { 443, 625 } };
        static const char *labels_b[3] = { "Cancel", "Freefall Options", "Play Mission" };
        for (i = 0; i < 3; i++) {
            fill(bx[i][0] + 1, 447, bx[i][1] - 1, 467, BLACK);
            ctext(&fsmall, bx[i][0], bx[i][1], 452, labels_b[i], WHITE);
        }
    }
}

static void to_rgba(void)
{
    uint32_t p32[256];
    int i, y;
    for (i = 0; i < 256; i++)
        p32[i] = 0xFF000000u | (uint32_t)pal[i * 3] << 16 | (uint32_t)pal[i * 3 + 1] << 8 | pal[i * 3 + 2];
    for (y = 0; y < H; y++)
        for (i = 0; i < W; i++)
            rgba[y * W + i] = p32[cv[y][i]];
    if (sel >= 1) {
        if (list[sel - 1].m != shown_m || whole_map != shown_whole)
            map_make(list[sel - 1].m, whole_map);
        for (y = 0; y < BH; y++)
            for (i = 0; i < BW; i++)
                rgba[(BY + y) * W + BX + i] = 0xFF000000u | mapimg[y][i];
    }
}

/* ------------------------------------------------------------------ movies */
static void *mv;                        /* the movie playing */
static int mv_full;                     /* it fills the screen (else it plays in the map's box) */
/* "Smooth videos": in-between pictures as for the game's movies (smacker.c smooth_open): a second
 * decoder runs ahead and hands the pictures to RIFE, the one halfway is shown at half time */
static vsmooth *mv_vs;
static void *mv_ahead;
static uint32_t mv_pushed, mv_mid_done;
static uint8_t *mv_mid;                 /* RGB */
static uint32_t *mv_mid_rgb;            /* ARGB */

static void smooth_close(void)
{
    if (mv_vs)
        vsmooth_end(mv_vs);
    if (mv_ahead)
        port_smk_close(mv_ahead);
    mv_vs = NULL;
    mv_ahead = NULL;
    SDL_free(mv_mid);
    SDL_free(mv_mid_rgb);
    mv_mid = NULL;
    mv_mid_rgb = NULL;
}

static int movie_in_box(void)
{
    return mv && !mv_full;
}
static int mv_queue[3], mv_n, mv_i, mv_w, mv_h;
static int mv_hires;                    /* the game shows it in 640 x 480 (else 320 x 200) */
static int mv_music_off;                /* the music was paused for the movies */
static uint32_t mv_frames, mv_frame;
static double mv_ms;
static uint64_t mv_start;
static SDL_Texture *mv_tex;
static uint32_t *mv_rgb;

static void movie_stop(void)
{
    smooth_close();
    if (mv)
        port_smk_close(mv);
    mv = NULL;
    mv_n = mv_i = 0;
    mv_full = 0;
    SDL_free(mv_rgb);
    mv_rgb = NULL;
    if (mv_music_off) {                 /* the music goes on where it stopped */
        port_music_pause(0);
        mv_music_off = 0;
    }
}

/* the movie table's "hires" flag of a movie file: the game plays it in a 640 x 480 or a
 * 320 x 200 screen (movie.c intro_2DC254) */
static int movie_hires(const char *name, int w)
{
    int i;
    for (i = 0; i < 0x5A; i++)
        if (movie_tab[i].name[0] && !SDL_strncasecmp(movie_tab[i].name, name, sizeof movie_tab[i].name))
            return movie_tab[i].hires == 1;
    return w > 320;
}

static int movie_next(void)             /* opens the queue's next movie */
{
    while (mv_i < mv_n) {
        char host[1100], name[32];
        int id = mv_queue[mv_i++];
        if (id >= 1000)
            SDL_snprintf(name, sizeof name, "BRIEF%02dA.SMK", id - 1000);
        else
            SDL_snprintf(name, sizeof name, "%.14s", movie_tab[id].name);
        if (!movie_path(name, host, sizeof host)) {
            port_log("Mission Selector: movie %s not found\n", name);
            continue;
        }
        if (mv)
            port_smk_close(mv);
        smooth_close();
        mv = port_smk_open_sound(host, ((snd_sfx_vol * 6) >> 3 << 8) / 32768.0f, &mv_w, &mv_h, &mv_frames, &mv_ms);
        if (!mv)
            continue;
        SDL_free(mv_rgb);
        mv_rgb = SDL_calloc((size_t)mv_w * mv_h, 4);
        if (mv_tex) {
            float tw, th;
            if (!SDL_GetTextureSize(mv_tex, &tw, &th) || (int)tw != mv_w || (int)th != mv_h) {
                SDL_DestroyTexture(mv_tex);
                mv_tex = NULL;
            }
        }
        mv_frame = 0;
        mv_start = 0;
        mv_hires = movie_hires(name, mv_w);
        if (port_smooth_videos && mv_ms >= 50 && vsmooth_start(0, -1) == 1) {
            int aw, ah;
            uint32_t af;
            double ams;
            mv_ahead = port_smk_open(host, &aw, &ah, &af, &ams);
            mv_vs = mv_ahead ? vsmooth_begin(mv_w, mv_h) : NULL;
            mv_mid = SDL_malloc((size_t)mv_w * mv_h * 3);
            mv_mid_rgb = SDL_malloc((size_t)mv_w * mv_h * 4);
            if (!mv_vs || !mv_mid || !mv_mid_rgb)
                smooth_close();
            mv_pushed = mv_mid_done = 0;
        }
        if (!mv_music_off) {            /* no music under the movie */
            port_music_pause(1);
            mv_music_off = 1;
        }
        port_log("Mission Selector: playing %s (%dx%d in %s)\n", name, mv_w, mv_h, mv_hires ? "640x480" : "320x200");
        return 1;
    }
    movie_stop();
    return 0;
}

static void movie_play(const int *ids, int n)
{
    int i;
    movie_stop();
    for (i = 0; i < n && i < 3; i++)
        mv_queue[i] = ids[i];
    mv_n = n;
    mv_i = 0;
    movie_next();
}

/* the movie's frames as time goes; drawn filling the screen (full) or in the map's box */
static void movie_render(SDL_Renderer *r, const SDL_FRect *view, int full)
{
    uint64_t now = port_now_us();
    int shown = 0;
    if (!mv_start)
        mv_start = now;
    while (mv && mv_frame < mv_frames && now >= mv_start + (uint64_t)(mv_frame * mv_ms * 1000)) {
        const uint8_t *px, *p6;
        int i;
        if (!port_smk_next(mv, &px, &p6)) {
            mv_frame = mv_frames;
            break;
        }
        mv_frame++;
        for (i = 0; i < mv_w * mv_h; i++) {
            const uint8_t *c = p6 + px[i] * 3;
            mv_rgb[i] = 0xFF000000u | (uint32_t)(c[0] * 255 / 63) << 16 | (uint32_t)(c[1] * 255 / 63) << 8 |
                        (uint32_t)(c[2] * 255 / 63);
        }
        shown = 1;
    }
    if (mv_vs) {
        /* RIFE gets the pictures up to the one after the next; halfway to the next picture its
         * in-between one is shown (FREEFALL_SMOOTH_WAIT=1, tests: waited for) */
        while (mv_pushed < mv_frames && mv_pushed <= mv_frame + 1) {
            const uint8_t *px, *p6;
            if (!port_smk_next(mv_ahead, &px, &p6)) {
                mv_pushed = mv_frames;
                break;
            }
            vsmooth_push(mv_vs, mv_pushed++, px, p6);
        }
        if (!shown && mv_frame >= 1 && mv_frame < mv_frames && mv_mid_done < mv_frame &&
            now >= mv_start + (uint64_t)((mv_frame - 0.5) * mv_ms * 1000)) {
            int wait = SDL_getenv("FREEFALL_SMOOTH_WAIT") != NULL, r, i;
            uint64_t t0 = port_now_us();
            r = vsmooth_mid(mv_vs, mv_frame, mv_mid, wait ? 5000 : 0);
            if (wait)
                mv_start += port_now_us() - t0;
            if (r == 1) {
                for (i = 0; i < mv_w * mv_h; i++)
                    mv_mid_rgb[i] = 0xFF000000u | (uint32_t)mv_mid[3 * i] << 16 | (uint32_t)mv_mid[3 * i + 1] << 8 |
                                    mv_mid[3 * i + 2];
                if (mv_tex)
                    SDL_UpdateTexture(mv_tex, NULL, mv_mid_rgb, mv_w * 4);
                mv_mid_done = mv_frame;
            } else if (r < 0 || now >= mv_start + (uint64_t)((mv_frame - 0.25) * mv_ms * 1000)) {
                if (r == 0)
                    vsmooth_count_late(mv_vs);  /* no use once a quarter picture late */
                mv_mid_done = mv_frame;
            }
        }
    }
    if (mv && mv_frame >= mv_frames && now >= mv_start + (uint64_t)(mv_frames * mv_ms * 1000)) {
        if (!movie_next())
            return;
        shown = 0;
    }
    if (!mv)
        return;
    if (!mv_tex) {
        mv_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, mv_w, mv_h);
        if (mv_tex)
            SDL_SetTextureScaleMode(mv_tex, SDL_SCALEMODE_LINEAR);
    }
    if (mv_tex && shown)
        SDL_UpdateTexture(mv_tex, NULL, mv_rgb, mv_w * 4);
    if (mv_tex && !full) {
        /* in the map's box, black around it: its shape as the game shows it (a 320 x 200
         * screen's pixels are drawn taller) */
        int sw = mv_hires ? 640 : 320, sh = mv_hires ? 480 : 200;
        float dw = (float)mv_w * W / sw, dh = (float)mv_h * H / sh, k, sx = view->w / W, sy = view->h / H;
        SDL_FRect b, d;
        k = BW / dw < BH / dh ? BW / dw : BH / dh;
        b.x = view->x + BX * sx;
        b.y = view->y + BY * sy;
        b.w = BW * sx;
        b.h = BH * sy;
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderFillRect(r, &b);
        d.w = dw * k * sx;
        d.h = dh * k * sy;
        d.x = b.x + (b.w - d.w) / 2;
        d.y = b.y + (b.h - d.h) / 2;
        SDL_RenderTexture(r, mv_tex, NULL, &d);
    } else if (mv_tex) {
        /* where the game itself shows it: in the middle of its 640 x 480 or 320 x 200 screen,
         * that screen drawn 4:3 like the game's; "Movie size: Fill screen" as big as the window
         * allows (screen.c, as platform.c does for the game's movies) */
        int sw = mv_hires ? 640 : 320, sh = mv_hires ? 480 : 200, ww = 0, wh = 0;
        int mx = (sw - mv_w) / 2, my = (sh - mv_h) / 2;
        SDL_FRect scr = *view, d;
        SDL_GetCurrentRenderOutputSize(r, &ww, &wh);
        if (mx < 0 || my < 0) {         /* bigger than the game's screen: fit it */
            sw = mv_w > sw ? mv_w : sw;
            sh = mv_h > sh ? mv_h : sh;
            mx = (sw - mv_w) / 2;
            my = (sh - mv_h) / 2;
        }
        port_screen_movie_rect(sw, sh, ww, wh, mx, my, mv_w, mv_h, &scr);
        d.x = scr.x + mx * scr.w / sw;
        d.y = scr.y + my * scr.h / sh;
        d.w = mv_w * scr.w / sw;
        d.h = mv_h * scr.h / sh;
        SDL_RenderTexture(r, mv_tex, NULL, &d);
    }
}

/* ------------------------------------------------------------------ input */
static SDL_FRect view_was;              /* where the game's screen was drawn */
static SDL_Renderer *rend;
static int dirty = 1;
static int start_step;                  /* Play Mission: 1 pointer moved to the game's button, 2 clicked */
static uint64_t start_at;
static int start_x, start_y;
static uint64_t menu_gone;

/* fades, as the Random Scenario Builder's (its script fades the palette to black and back): the
 * menu goes black, the selector comes up from black, and the same way back. Nothing is clicked
 * while one runs, so the click that left cannot land on the menu's button under it */
enum { F_NONE, F_OPEN_OUT, F_OPEN_IN, F_CLOSE_OUT, F_HOLD, F_CLOSE_IN };
#define FADE_US 300000
#define HOLD_US 3000000                 /* Play Mission: black at most this long */
#define WALK_HOLD_US 20000000           /* (on the way to the outfitting) */
extern int port_movies_open;            /* the game plays a movie (smacker.c) */
static int fade, fade_play;             /* fade_play: Play Mission's campaign position, 0 none */
static uint64_t fade_at;
static int eat_up;                      /* the left button's release after a click that left */
static int fade_restart;                /* F_OPEN_IN: its clock starts after the first drawing */

static void set_fade(int f)
{
    fade = f;
    fade_at = port_now_us();
    port_ms_fading = fade != F_NONE || eat_up;
}

static void click_sound(void)
{
    port_ms_clicks++;
}

static int to_px(float wx, float wy, int *gx, int *gy)
{
    float rx = wx, ry = wy;
    if (rend)
        SDL_RenderCoordinatesFromWindow(rend, wx, wy, &rx, &ry);
    if (view_was.w <= 0 || view_was.h <= 0)
        return 0;
    *gx = (int)SDL_floorf((rx - view_was.x) * W / view_was.w);
    *gy = (int)SDL_floorf((ry - view_was.y) * H / view_was.h);
    return 1;
}

static void open_screen(void)
{
    if (!load_art() || !load_campaign()) {
        port_log("Mission Selector: the game's files could not be read\n");
        return;
    }
    SDL_LockSpinlock(&pr_lock);
    pr_load();
    SDL_UnlockSpinlock(&pr_lock);
    port_ms_mission = 0;
    port_missions_open = 1;
    hover = HIT_NONE;
    if (sel < 1 || !pos_open(sel)) {    /* the furthest mission reached */
        sel = -1;
        choose(furthest());
    } else
        page_top = (sel - 1) / ROWS * ROWS;
    menu_gone = 0;
    dirty = 1;
}

static void close_screen(void)
{
    movie_stop();
    port_missions_open = 0;
}

static void begin_close(int pos)        /* pos: Play Mission's, 0 back to the main menu */
{
    if (fade == F_CLOSE_OUT)
        return;
    fade_play = pos;
    set_fade(F_CLOSE_OUT);
}

static void play(void)
{
    if (sel < 1 || sel > nlist || !pos_open(sel))
        return;
    begin_close(sel);
}

static void start_mission(int pos)      /* after the fade: the game's button is clicked */
{
    port_ms_mission = list[pos - 1].m;
    /* the game's New Game (Skip Instruction) button, New Game for the training missions
     * (uiw_rects 1 and 2 of the main menu) */
    start_x = 186;
    start_y = pos <= 2 ? 369 : 424;
    port_mouse_event(start_x, start_y, 0, 1, 0);
    start_step = 1;
    start_at = port_now_us();
    if (pos > 2) {                      /* New Game (Skip Instruction): straight to the outfitting */
        SDL_LockSpinlock(&walk_lock);
        walk_at = start_at;
        skip_until = start_at + 20000000;   /* the campaign's opening movies */
        walk_ended = 0;
        hold_pos = pos;
        hold_made = 0;
        SDL_UnlockSpinlock(&walk_lock);
        port_ms_walk = 1;
    }
    port_log("Mission Selector: New Campaign at %s (campaign position %d)\n", list[pos - 1].name, pos);
}

static void scroll(int d)
{
    btop += d;
    if (btop > nbl - VIS)
        btop = nbl - VIS;
    if (btop < 0)
        btop = 0;
}

static void page(int d)
{
    int t = page_top + d * ROWS;
    if (t >= 0 && t < nlist)
        page_top = t;
}

static void step_sel(int d)
{
    int p = sel + d;
    while (p >= 1 && p <= nlist && !pos_open(p))
        p += d;
    if (p >= 1 && p <= nlist)
        choose(p);
}

static void click(int what, int row, int clicks)
{
    if (movie_in_box() && (what == HIT_MAPL || what == HIT_MAPR)) {
        click_sound();                  /* the map's arrows are Stop and Fullscreen meanwhile */
        if (what == HIT_MAPL)
            movie_stop();
        else
            mv_full = 1;
        return;
    }
    if (mv && (what == HIT_OPTIONS || what == HIT_PLAY || what == HIT_MENU))
        movie_stop();
    switch (what) {
    case HIT_ROW:
        if (row == sel && clicks >= 2)
            play();
        else
            choose(row);
        return;
    case HIT_PGL:
        if (page_top <= 0)
            return;
        page(-1);
        break;
    case HIT_PGR:
        if (page_top + ROWS >= nlist)
            return;
        page(1);
        break;
    case HIT_MAPL: case HIT_MAPR: whole_map = !whole_map; break;
    case HIT_UP: scroll(-1); break;
    case HIT_DOWN: scroll(1); break;
    case HIT_TRACK: scroll(VIS - 1); return;
    case HIT_VIDEO:
        if (sel < 1 || !has_briefing_video(list[sel - 1].m))
            return;
        click_sound();
        {
            int id = 1000 + list[sel - 1].m;
            movie_play(&id, 1);
        }
        return;
    case HIT_STORY:
        if (sel < 1 || !list[sel - 1].nstory)
            return;
        click_sound();
        movie_play(list[sel - 1].story, list[sel - 1].nstory);
        return;
    case HIT_MENU: click_sound(); begin_close(0); return;
    case HIT_OPTIONS: click_sound(); port_options_show(); return;
    case HIT_PLAY:
        if (sel >= 1 && sel <= nlist && pos_open(sel))
            click_sound();
        play();
        return;
    default: return;
    }
    /* an arrow: the builder's click and its lit art for 250 ms (rescustm.c rsb_hint_time) */
    click_sound();
    lit = what;
    lit_until = port_now_us() + 250000;
}

/* the main menu's button (window coordinates of game pixels 248..392 x 440..464) */
static int on_button(float wx, float wy)
{
    int x, y;
    return port_menu_msbutton && port_video.w == W && port_video.h == H && to_px(wx, wy, &x, &y) &&
           x >= 248 && x < 392 && y >= 440 && y < 464;
}

/* key, mouse: 1 when the event was the Mission Selector's (the game must not see it) */
static int missions_input(const SDL_Event *e, int button);

int port_missions_event(const SDL_Event *e)
{
    int button = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP;
    if (eat_up && button && e->button.button == SDL_BUTTON_LEFT) {
        /* the release of the click that left goes to nobody (it opened Freefall Options under it) */
        eat_up = 0;
        port_ms_fading = fade != F_NONE;
        if (e->type == SDL_EVENT_MOUSE_BUTTON_UP)
            return 1;
    }
    if (start_step) {                   /* starting the mission: the pointer stays on the game's button */
        return e->type == SDL_EVENT_MOUSE_MOTION || button;
    }
    if (!port_missions_open) {
        if (fade == F_HOLD || fade == F_CLOSE_IN)       /* the menu comes back: its buttons wait */
            return button || e->type == SDL_EVENT_MOUSE_WHEEL;
        if (button && e->button.button == SDL_BUTTON_LEFT && on_button(e->button.x, e->button.y)) {
            if (e->type == SDL_EVENT_MOUSE_BUTTON_UP) {
                open_screen();          /* (no click sound: the main menu's buttons have none) */
                if (port_missions_open)
                    set_fade(F_OPEN_OUT);
            }
            return 1;
        }
        return 0;
    }
    if (fade != F_NONE)                 /* fading: nothing is clicked or pressed */
        return e->type != SDL_EVENT_QUIT;
    if (mv && mv_full) {                /* a key or a button: back to the map's box */
        if ((e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) || e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            mv_full = 0;
            dirty = 1;
        }
        return e->type != SDL_EVENT_QUIT;
    }
    if (mv && e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_ESCAPE) {
        movie_stop();                   /* Esc stops the movie in the box first */
        dirty = 1;
        return 1;
    }
    {
        int was = sel, r = missions_input(e, button);
        if (mv && sel != was)           /* another mission: its movie is not this one */
            movie_stop();
        return r;
    }
}

static int missions_input(const SDL_Event *e, int button)
{
    int x, y, row;
    (void)button;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        switch (e->key.scancode) {
        case SDL_SCANCODE_ESCAPE: begin_close(0); break;
        case SDL_SCANCODE_UP: step_sel(-1); break;
        case SDL_SCANCODE_DOWN: step_sel(1); break;
        case SDL_SCANCODE_PAGEUP: page(-1); break;
        case SDL_SCANCODE_PAGEDOWN: page(1); break;
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT: whole_map = !whole_map; break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: play(); break;
        default: break;
        }
        dirty = 1;
        return 1;
    case SDL_EVENT_KEY_UP:
        return 1;
    case SDL_EVENT_MOUSE_MOTION:
        if (to_px(e->motion.x, e->motion.y, &x, &y)) {
            int h = hit_at(x, y, &row);
            if (h != hover || row != hover_row)
                dirty = 1;
            hover = h;
            hover_row = row;
        }
        return 1;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (to_px(e->button.x, e->button.y, &x, &y)) {
            int h = hit_at(x, y, &row);
            if (e->button.button == SDL_BUTTON_LEFT) {
                click(h, row, e->button.clicks);
                if (fade == F_CLOSE_OUT) {
                    eat_up = 1;
                    port_ms_fading = 1;
                }
            } else if (e->button.button == SDL_BUTTON_RIGHT)
                begin_close(0);
        }
        dirty = 1;
        return 1;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return 1;
    case SDL_EVENT_MOUSE_WHEEL:
        if (SDL_GetMouseState(NULL, NULL), 1) {
            float mx, my;
            int d = e->wheel.y > 0 ? -1 : e->wheel.y < 0 ? 1 : 0;
            SDL_GetMouseState(&mx, &my);
            if (to_px(mx, my, &x, &y) && x < 320)
                page(d);
            else
                scroll(d);
        }
        dirty = 1;
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ each frame (main thread) */
static SDL_Texture *tex;

static SDL_Texture *hold_tex;           /* Play Mission's black: the mission's name on it */

void port_missions_renderer_gone(void)
{
    if (tex)
        SDL_DestroyTexture(tex);
    if (mv_tex)
        SDL_DestroyTexture(mv_tex);
    if (hold_tex)
        SDL_DestroyTexture(hold_tex);
    tex = mv_tex = hold_tex = NULL;
    hold_made = 0;
    dirty = 1;
}

/* the name of the mission on the way to its outfitting, in the game's fonts on clear (black
 * drawn under it) */
static void hold_text(SDL_Renderer *r, float a)
{
    if (a <= 0 || hold_pos < 1 || hold_pos > nlist)
        return;
    if (!hold_tex) {
        hold_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
        if (!hold_tex)
            return;
        SDL_SetTextureScaleMode(hold_tex, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(hold_tex, SDL_BLENDMODE_BLEND);
        hold_made = 0;
    }
    if (!hold_made) {
        char buf[96];
        int x, y;
        memset(cv, BLACK, sizeof cv);
        ctext(&fbig, 0, W - 1, 214, list[hold_pos - 1].name, 0);
        SDL_snprintf(buf, sizeof buf, "A new campaign from mission %d of %d", hold_pos, nlist);
        ctext(&fsmall, 0, W - 1, 246, buf, GREY);
        ctext(&fsmall, 0, W - 1, 262, "Going to the outfitting...", GREY);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                const uint8_t *c = pal + cv[y][x] * 3;
                rgba[y * W + x] = cv[y][x] == BLACK ? 0 :
                                  0xFF000000u | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
            }
        SDL_UpdateTexture(hold_tex, NULL, rgba, W * 4);
        hold_made = 1;
        dirty = 1;                      /* (the selector's picture is drawn again when it opens) */
    }
    SDL_SetTextureAlphaModFloat(hold_tex, a > 1 ? 1 : a);
    SDL_RenderTexture(r, hold_tex, NULL, &view_was);
}

/* the game's palette is all (nearly) black: its own fade out is done */
static int dac_dark(void)
{
    int i, top = port_video.dac_bits == 8 ? 12 : 3;
    for (i = 0; i < 256 * 3; i++)
        if (port_video.dac[i] > top)
            return 0;
    return 1;
}

/* the whole window black, a = 0 (clear) .. 1 */
static void cover(SDL_Renderer *r, float a)
{
    SDL_BlendMode was = SDL_BLENDMODE_NONE;
    if (a <= 0)
        return;
    SDL_GetRenderDrawBlendMode(r, &was);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, (Uint8)(a >= 1 ? 255 : a * 255 + 0.5f));
    SDL_RenderFillRect(r, NULL);
    SDL_SetRenderDrawBlendMode(r, was);
}

void port_missions_render(SDL_Renderer *r, const SDL_FRect *view)
{
    uint64_t now = port_now_us();
    float k = fade ? (float)(now - fade_at) / FADE_US : 0;
    rend = r;
    view_was = *view;
    if (k > 1)
        k = 1;
    if (start_step == 1 && now - start_at >= 250000) {  /* the game saw the pointer on its button */
        port_mouse_click();
        start_step = 2;
        start_at = now;
    } else if (start_step == 2 && now - start_at >= 100000)
        start_step = 0;
    if (port_ms_walk && now - walk_at > 60000000) {
        port_ms_walk = 0;               /* never got there: the player goes on by hand */
        port_log("Mission Selector: the walk to the outfitting timed out\n");
    }
    if (lit != HIT_NONE && now >= lit_until) {
        lit = HIT_NONE;
        dirty = 1;
    }
    if (fade == F_HOLD) {
        /* Play Mission: black until the game has faded its main menu out itself (or gone on to
         * a movie or another screen), so the menu does not show again on the way */
        int done;
        cover(r, 1);
        if (hold_pos) {                 /* on the way to the outfitting: its sounds not heard */
            if (!port_audio_mute) {
                port_audio_mute = 1;
                port_music_pause(1);    /* (General MIDI goes to Windows, not through the mix) */
            }
            hold_text(r, (float)(now - fade_at) / FADE_US);
        }
        if (start_step)
            return;
        if (port_ms_walk || walk_ended) {
            /* on the way to the outfitting: the quarters and the base hall stay under the black
             * (the outfitting comes up from it) */
            /* (the outfitting comes after the briefing's movie, skipped, when the game's palette
             * is no longer black; without a movie a few seconds after the door) */
            uint64_t end, skipped;
            SDL_LockSpinlock(&walk_lock);
            end = walk_end_at;
            skipped = walk_skip_at;
            SDL_UnlockSpinlock(&walk_lock);
            done = walk_ended == 2 || now - fade_at >= WALK_HOLD_US ||
                   (walk_ended == 1 && (skipped ? (!dac_dark() && now - skipped >= 200000) ||
                                                  now - skipped >= 4000000 : now - end >= 6000000));
        } else
            done = dac_dark() || port_movies_open > 0 || port_video.w != W || port_video.h != H ||
                   now - fade_at >= HOLD_US;
        if (done) {
            walk_ended = 0;
            if (port_audio_mute) {      /* the outfitting's sound comes up with its picture */
                port_audio_mute = 0;
                port_music_pause(0);
            }
            set_fade(F_CLOSE_IN);
        }
        return;
    }
    if (fade == F_CLOSE_IN) {           /* the main menu (or the game) comes up from black */
        cover(r, 1 - k);
        if (hold_pos)
            hold_text(r, 1 - k);
        if (k >= 1) {
            set_fade(F_NONE);
            hold_pos = 0;
        }
        return;
    }
    if (!port_missions_open)
        return;
    if (fade == F_OPEN_OUT) {           /* the main menu goes black */
        if (k < 1) {
            cover(r, k);
            return;
        }
        /* black: the selector is drawn behind it (its map can take a while) and comes up from
         * there, its fade timed from the end of this drawing */
        set_fade(F_OPEN_IN);
        fade_restart = 1;
    }
    if (!port_menu_msbutton || port_video.w != W || port_video.h != H) {
        if (!menu_gone)                 /* the main menu went away (the game's own doing) */
            menu_gone = now;
        else if (now - menu_gone > 1000000) {
            close_screen();
            set_fade(F_NONE);
            return;
        }
    } else
        menu_gone = 0;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderFillRect(r, NULL);
    if (mv && mv_full) {
        movie_render(r, view, 1);
        if (mv)
            return;
        dirty = 1;
    }
    if (!tex) {
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
        if (tex)
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        dirty = 1;
    }
    if (!tex)
        return;
    if (dirty) {
        draw();
        to_rgba();
        SDL_UpdateTexture(tex, NULL, rgba, W * 4);
        dirty = 0;
    }
    SDL_RenderTexture(r, tex, NULL, view);
    if (mv) {
        int full = mv_full;
        movie_render(r, view, 0);
        if (!mv || mv_full != full)
            dirty = 1;                  /* the map and its arrows come back */
    }
    if (!port_options_open)
        port_options_pointer(r, view);
    if (fade == F_OPEN_IN) {
        if (fade_restart) {
            fade_restart = 0;
            fade_at = port_now_us();
            k = 0;
        }
        cover(r, 1 - k);
        if (k >= 1)
            set_fade(F_NONE);
    } else if (fade == F_CLOSE_OUT) {
        cover(r, k);
        if (k >= 1) {                   /* black: the main menu is back under it */
            int pos = fade_play;
            close_screen();
            if (pos >= 1 && pos <= nlist) {
                start_mission(pos);
                set_fade(F_HOLD);
            } else
                set_fade(F_CLOSE_IN);
        }
    }
}

/* options.c: Mission select changed */
void port_missions_changed(void)
{
    dirty = 1;
    if (port_missions_open && (sel < 1 || !pos_open(sel)))
        choose(furthest());
}
