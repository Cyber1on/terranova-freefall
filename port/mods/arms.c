/* arms.c -- Freefall: "Arms pack" on the Mods page (off by default): the suit's arm that
 * holds the selected weapon, drawn over the main 3D view while the goggles are up (never in the
 * cockpit or the drone's view), aimed at the pointer.
 *
 * A pack is the Arms folder of a mod (Mods/<mod>.zip next to tnfree.exe, with --user in that
 * folder, or a folder Mods/<mod> while it is being made: port_pack_dir) with a folder
 * per suit (scout, standard, heavy, pirate; a suit without one uses standard's) holding the RIGHT
 * arm's pictures and arms.txt:
 *
 *     size W H                         the pictures' size: the whole view, any shape (16:9 made)
 *     view X Y W H                     (optional) the view's part of the pictures, when they are
 *                                      bigger than the view (so that the arm moved by the recoil or
 *                                      between the poses never shows its picture's edge); the muzzle's
 *                                      places are in the whole picture; the whole picture without it
 *     pose FILE U V MX MY              a picture for the pointer at U, V of the view (-1 .. 1 from
 *                                      its middle to its edges, V up) with the weapon's muzzle at
 *                                      MX, MY of the picture; the poses a grid of U and V
 *     recoil FILE U V MX MY            (optional) the middle pose kicked back by a shot: only its
 *                                      muzzle's move is used, as the recoil
 *     sway K MAXU MAXV                 (optional) mouselook's sway: the arm lags K of the view's half
 *                                      width per radian a second of turning, at most MAXU (MAXV up and
 *                                      down); 0.06 0.3 0.2 without it
 *     follow OMEGA ZETA                (optional) the spring the arm follows on: OMEGA its speed (a
 *                                      second's radians; lower lags longer), ZETA its damping (below 1
 *                                      overshoots); 22 0.8 without it
 *     walk BOB SWAY LEAN SPEED RATE    (optional) walking: at SPEED (metres a second) or faster the arm
 *                                      dips BOB of the view's height RATE times a second, swings SWAY
 *                                      of its width to a side and back between two dips, and lags
 *                                      LEAN of its width behind a strafe (half of it in height behind
 *                                      a walk forward or back); slower, less and slower (the rate down
 *                                      to 0.4 of it); BOB 0: no walking moves; 0.012 0.008 0.015 10
 *                                      2.6 without it (the suits walk 10 to 12 m/s, forward or back)
 *
 * The pictures are PNGs with alpha (see-through below half), any colours: each colour is shown as
 * the palette's nearest.  The left arm is the right one mirrored.  The arm follows the pointer on
 * a spring; between the grid's poses the nearest picture is moved by the muzzle's place between
 * them, so a few dozen poses move smoothly.  Past the grid the pose stays at its edge, except
 * below it: there the bottom row's pose moves down with the pointer.  With the
 * Advanced controls' mouselook the pointer stays in the middle and the arm sways behind the turn.
 * Walking, backing up and strafing on the ground bob the arm (by the speed, the same whichever
 * way) and lean it behind the move (moves of the picture only: the pictures' margins of arms.txt's
 * view take them).
 * Selecting a weapon in the other arm lowers the arm out of the view and raises the other.
 * A shot (pbasim.c) kicks the arm back (by the weapon's rate of fire) and lights the muzzle; the
 * player's shots leave from the muzzle drawn (port_arms_muzzle, sharp3d.c's port_arms_shot).  In
 * the game's infrared the arm is in the infrared's colours. */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"
#include "platform.h"                   /* port_now_us */
#include "fxpack.h"
#include "arms.h"
#include "helmetfx.h"
#include "sharp3d.h"

char port_arms_pack[64];                /* "Arms pack": a mod's name (its Arms), "" = off (screen.c) */
int port_ml_frame_turn, port_ml_frame_look;

#define MAX_POSES 512
#define MAX_AXIS 64                     /* the grid's U and V values */

typedef struct {
    float u, v, mx, my;
    unsigned char *rgba;                /* the box of its pixels only (bw x bh) */
    unsigned char *pix;                 /* in the palette's colours (0 see-through), for pix_pal */
    unsigned long pix_pal;
    unsigned char *pix_ir;              /* in the game's infrared ramp (0 see-through) */
    int bx0, by0, bx1, by1, bw, bh;     /* the box of its pixels in the picture, its size */
} Pose;

static Pose pose[MAX_POSES], recoil;
static int npose, have_recoil, pw, ph;
static float vx, vy, vw, vh;            /* the view in the pictures (arms.txt's view) */
static float us[MAX_AXIS], vs[MAX_AXIS];
static int nu, nv;
static short grid[MAX_AXIS][MAX_AXIS];  /* [iu][iv] -> pose, -1 none */
static char loaded[64 + 16];            /* pack/suit in pose[] ("" none, "-" tried and failed) */
static float sway_k, sway_mu, sway_mv, follow_w, follow_z;     /* arms.txt's sway and follow */
static float walk_bob, walk_sway, walk_lean, walk_speed, walk_rate;  /* ... and walk */

/* the arm's state (game thread) */
static float su, sv, vu, vv, kick, kickv;
static float walk_f, walk_s, walk_fv, walk_sv, walk_tf, walk_ts;  /* the speeds on the ground (m/s,
                                         * forward and right; smoothed, their speeds, the measured ones) */
static float walk_ph;                   /* the bob's phase: a dip each half turn */
static uint64_t last_us, fire_us;
static float muz_fx, muz_fy;            /* the muzzle drawn last (picture pixels), muz_mount 0: none */
static int muz_mount;
static float gap;                       /* the time between the shots lately (s), 0 a single shot */
#define RATE_SLOW 0.40f                 /* shots this far apart or more: the whole kick (the Multipulsar
                                         * 0.4 s, owner liked; the Laser 0.1, Railgun 0.15) */
static int fired;

void port_arms_fired(void)
{
    static int logged;
    fired = 1;
    port_helmet_fired();                /* the helmet's kick and muzzle flash light */
    if (!logged++)
        port_log("Arms pack: the player fired (the recoil)\n");
}

static void free_all(void)
{
    int i;
    for (i = 0; i < npose; i++) {
        free(pose[i].rgba);
        free(pose[i].pix);
        free(pose[i].pix_ir);
    }
    free(recoil.rgba);
    free(recoil.pix);
    free(recoil.pix_ir);
    memset(pose, 0, sizeof pose);
    memset(&recoil, 0, sizeof recoil);
    npose = have_recoil = pw = ph = nu = nv = 0;
}

static int add_value(float *t, int *n, float x)
{
    int i, k;
    for (i = 0; i < *n; i++)
        if (fabsf(t[i] - x) < 1e-3f)
            return i;
    if (*n >= MAX_AXIS)
        return -1;
    for (k = *n; k > 0 && t[k - 1] > x; k--)
        t[k] = t[k - 1];
    t[k] = x;
    (*n)++;
    return k;
}

static int load_pic(Pose *p, const char *dir, const char *file)
{
    char path[1400];
    int w, h, x, y;
    snprintf(path, sizeof path, "%s/%s", dir, file);
    p->rgba = port_png_rgba(path, &w, &h);
    if (!p->rgba || w != pw || h != ph) {
        port_log("Arms pack: cannot use %s (a PNG of %dx%d)\n", path, pw, ph);
        free(p->rgba);
        p->rgba = NULL;
        return 0;
    }
    p->bx0 = w;
    p->by0 = h;
    p->bx1 = p->by1 = 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (p->rgba[((size_t)y * w + x) * 4 + 3] >= 128) {
                if (x < p->bx0) p->bx0 = x;
                if (x >= p->bx1) p->bx1 = x + 1;
                if (y < p->by0) p->by0 = y;
                if (y >= p->by1) p->by1 = y + 1;
            }
    p->bw = p->bx1 > p->bx0 ? p->bx1 - p->bx0 : 0;
    p->bh = p->by1 > p->by0 ? p->by1 - p->by0 : 0;
    if (p->bw && p->bh) {               /* keep only the box (a pack has a few hundred poses) */
        unsigned char *box = malloc((size_t)p->bw * p->bh * 4);
        if (box) {
            for (y = 0; y < p->bh; y++)
                memcpy(box + (size_t)y * p->bw * 4, p->rgba + ((size_t)(p->by0 + y) * w + p->bx0) * 4,
                       (size_t)p->bw * 4);
            free(p->rgba);
            p->rgba = box;
            return 1;
        }
    }
    free(p->rgba);                      /* nothing to show (or no memory) */
    p->rgba = NULL;
    p->bx0 = p->by0 = p->bx1 = p->by1 = p->bw = p->bh = 0;
    return 1;
}

static int load_suit(const char *pack, const char *suit)
{
    char base[1200], dir[1300], path[1400], line[600], file[300], *text, *at, *end;
    size_t len;
    float u, v, mx, my;
    int i, j;

    port_pack_dir("Arms", pack, base, sizeof base);
    snprintf(dir, sizeof dir, "%s/%s", base, suit);
    snprintf(path, sizeof path, "%s/arms.txt", dir);
    text = port_mod_load(path, &len);   /* from a folder or a zip (modzip.c) */
    if (!text)
        return 0;
    sway_k = 0.06f;
    sway_mu = 0.3f;
    sway_mv = 0.2f;
    follow_w = 22.0f;
    follow_z = 0.8f;
    walk_bob = 0.012f;
    walk_sway = 0.008f;
    walk_lean = 0.015f;
    walk_speed = 10.0f;
    walk_rate = 2.6f;
    for (at = text; at < text + len; at = end + 1) {
        end = at + strcspn(at, "\n");
        snprintf(line, sizeof line, "%.*s", (int)(end - at), at);
        if (sscanf(line, "size %d %d", &pw, &ph) == 2) {
            vx = vy = 0;
            vw = (float)pw;
            vh = (float)ph;
            continue;
        }
        if (sscanf(line, "view %f %f %f %f", &u, &v, &mx, &my) == 4 && mx > 0 && my > 0) {
            vx = u;
            vy = v;
            vw = mx;
            vh = my;
            continue;
        }
        if (sscanf(line, "sway %f %f %f", &u, &v, &mx) == 3) {
            sway_k = u;
            sway_mu = v;
            sway_mv = mx;
            continue;
        }
        if (sscanf(line, "follow %f %f", &u, &v) == 2) {
            follow_w = u > 1 ? u : 1;
            follow_z = v > 0 ? v : 0;
            continue;
        }
        if (!strncmp(line, "walk", 4)) {    /* (the values left out keep theirs) */
            float w[5] = { walk_bob, walk_sway, walk_lean, walk_speed, walk_rate };
            sscanf(line + 4, "%f %f %f %f %f", &w[0], &w[1], &w[2], &w[3], &w[4]);
            walk_bob = w[0] > 0 ? w[0] : 0;
            walk_sway = w[1];
            walk_lean = w[2];
            walk_speed = w[3] > 0.5f ? w[3] : 0.5f;
            walk_rate = w[4] > 0.1f ? w[4] : 0.1f;
            continue;
        }
        if (sscanf(line, "pose %299s %f %f %f %f", file, &u, &v, &mx, &my) == 5 && npose < MAX_POSES && pw > 0) {
            Pose *p = &pose[npose];
            p->u = u;
            p->v = v;
            p->mx = mx;
            p->my = my;
            if (load_pic(p, dir, file))
                npose++;
        } else if (sscanf(line, "recoil %299s %f %f %f %f", file, &u, &v, &mx, &my) == 5 && pw > 0) {
            recoil.u = u;
            recoil.v = v;
            recoil.mx = mx;
            recoil.my = my;
            have_recoil = load_pic(&recoil, dir, file);
        }
    }
    SDL_free(text);
    for (i = 0; i < npose; i++) {
        add_value(us, &nu, pose[i].u);
        add_value(vs, &nv, pose[i].v);
    }
    for (i = 0; i < nu; i++)
        for (j = 0; j < nv; j++)
            grid[i][j] = -1;
    for (i = 0; i < npose; i++) {
        int iu = add_value(us, &nu, pose[i].u), iv = add_value(vs, &nv, pose[i].v);
        if (iu >= 0 && iv >= 0)
            grid[iu][iv] = (short)i;
    }
    port_log("Arms pack: %d poses (%d x %d) from %s\n", npose, nu, nv, dir);
    return npose > 0;
}

static const char *suit_name(int suit)
{
    static const char *names[4] = { "scout", "standard", "heavy", "pirate" };
    return suit >= 0 && suit < 4 ? names[suit] : "standard";
}

/* the pack and suit to load (key "pack/suit"), "" when they are in (or the pack is off) */
static void wanted(int suit, char *key, size_t n)
{
    key[0] = 0;
    if (!port_arms_pack[0] || !port_mods_dir[0])
        return;
    snprintf(key, n, "%s/%s", port_arms_pack, suit_name(suit));
    if (!strcmp(key, loaded) || (loaded[0] == '-' && !strcmp(key, loaded + 1)))
        key[0] = 0;
}

static void load_key(const char *key)
{
    char pack[sizeof loaded];
    const char *sn = strrchr(key, '/') + 1;
    snprintf(pack, sizeof pack, "%.*s", (int)(sn - 1 - key), key);
    free_all();
    if (load_suit(pack, sn) || (free_all(), load_suit(pack, "standard")))
        snprintf(loaded, sizeof loaded, "%s", key);
    else {
        free_all();
        snprintf(loaded, sizeof loaded, "-%s", key);
        port_log("Arms pack: no arms.txt with poses in its folder %s/%s or /standard\n", pack, sn);
    }
}

/* P78: a pack's poses (165 PNGs in the standard one) take half a second or more to load; loaded
 * in the 3D's drawing they held up the first goggles view, black beside the screen.  They load on
 * a thread of their own instead, the arm not drawn meanwhile (it comes up when they are in), and
 * sharp3d.c starts that in the cockpit already (port_arms_preload) */
static SDL_AtomicInt loading;
static char load_want[sizeof loaded];

static int load_thread(void *data)
{
    (void)data;
    load_key(load_want);
    SDL_SetAtomicInt(&loading, 0);
    return 0;
}

/* whether the pictures for the suit are in (a pack off: none, and nothing to wait for) */
static int update(int suit)
{
    char key[sizeof loaded];
    SDL_Thread *t;

    if (SDL_GetAtomicInt(&loading))
        return 0;
    if (!port_arms_pack[0] || !port_mods_dir[0]) {
        if (loaded[0])
            free_all();
        loaded[0] = 0;
        return 1;
    }
    wanted(suit, key, sizeof key);
    if (!key[0])
        return 1;
    snprintf(load_want, sizeof load_want, "%s", key);
    SDL_SetAtomicInt(&loading, 1);
    t = SDL_CreateThread(load_thread, "arms pack", NULL);
    if (!t) {                           /* no thread: here, as before */
        load_key(load_want);
        SDL_SetAtomicInt(&loading, 0);
        return 1;
    }
    SDL_DetachThread(t);
    return 0;
}

void port_arms_preload(int suit)
{
    update(suit);
}

/* a pose's picture in the palette's colours (each colour's nearest, looked up once a colour) */
static const unsigned char *pix_of(Pose *p)
{
    static unsigned long cache_rgb[4096];   /* rgb | 1 << 24, 0 = free */
    static unsigned char cache_c[4096];
    static unsigned long cache_pal;
    static int cache_n;                     /* slots in use (P108: started again at 3072) */
    unsigned long key = port_fx_pal();
    size_t i, n = (size_t)p->bw * p->bh;

    if (!n)
        return NULL;
    if (p->pix && p->pix_pal == key)
        return p->pix;
    if (!p->pix && !(p->pix = malloc(n)))
        return NULL;
    if (cache_pal != key) {
        memset(cache_rgb, 0, sizeof cache_rgb);
        cache_n = 0;
        cache_pal = key;
    }
    for (i = 0; i < n; i++) {
        const unsigned char *s = p->rgba + i * 4;
        unsigned long rgb, h;
        if (s[3] < 128) {
            p->pix[i] = 0;
            continue;
        }
        rgb = (unsigned long)s[0] << 16 | s[1] << 8 | s[2] | 1ul << 24;
        h = (rgb * 2654435761u >> 20) & 4095;
        while (cache_rgb[h] && cache_rgb[h] != rgb)
            h = (h + 1) & 4095;
        if (!cache_rgb[h]) {
            if (cache_n >= 3072) {          /* P108: start again (the probe ran forever once full) */
                memset(cache_rgb, 0, sizeof cache_rgb);
                cache_n = 0;
                h = (rgb * 2654435761u >> 20) & 4095;
            }
            cache_n++;
            cache_rgb[h] = rgb;
            cache_c[h] = port_fx_near(s[0], s[1], s[2]);
            if (!cache_c[h])
                cache_c[h] = port_fx_near(s[0] + 1, s[1] + 1, s[2] + 1);
        }
        p->pix[i] = cache_c[h] ? cache_c[h] : 1;
    }
    p->pix_pal = key;
    return p->pix;
}

/* ... in the game's infrared (owner: the arm showed its own colours there): the
 * ramp by brightness: the suit warmer than the land (purple to magenta), but no hot thing
 * (Sci-fi outlines those) */
#define ARM_HEAT0 0.22f
#define ARM_HEAT1 0.40f                 /* (0.42 and up: outlined) */
static const unsigned char *pix_ir_of(Pose *p)
{
    size_t i, n = (size_t)p->bw * p->bh;

    if (!n)
        return NULL;
    if (p->pix_ir)
        return p->pix_ir;
    if (!(p->pix_ir = malloc(n)))
        return NULL;
    for (i = 0; i < n; i++) {
        const unsigned char *s = p->rgba + i * 4;
        int r = s[0] * 9 / 5, g = s[1] * 9 / 5, b = s[2] * 9 / 5;    /* (the arm is dark: more of the ramp) */
        p->pix_ir[i] = s[3] < 128 ? 0 : (unsigned char)port_ir_heat(r > 255 ? 255 : r, g > 255 ? 255 : g,
                                                                    b > 255 ? 255 : b, ARM_HEAT0, ARM_HEAT1);
    }
    return p->pix_ir;
}

static int find_cell(const float *t, int n, float x, float *a)
{
    int i;
    if (n < 2) {
        *a = 0;
        return 0;
    }
    if (x <= t[0]) {
        *a = 0;
        return 0;
    }
    for (i = 0; i < n - 2 && x > t[i + 1]; i++)
        ;
    *a = x >= t[n - 1] ? 1.0f : (x - t[i]) / (t[i + 1] - t[i]);
    return i;
}

/* the muzzle at u, v between the grid's poses (each corner's own when one is missing) */
static int muzzle_at(float u, float v, float *mx, float *my)
{
    float a, b, w[4], sx = 0, sy = 0, sw = 0;
    int i = find_cell(us, nu, u, &a), j = find_cell(vs, nv, v, &b), k, c[4];
    c[0] = grid[i][j];
    c[1] = nu > 1 ? grid[i + 1][j] : -1;
    c[2] = nv > 1 ? grid[i][j + 1] : -1;
    c[3] = nu > 1 && nv > 1 ? grid[i + 1][j + 1] : -1;
    w[0] = (1 - a) * (1 - b);
    w[1] = a * (1 - b);
    w[2] = (1 - a) * b;
    w[3] = a * b;
    for (k = 0; k < 4; k++)
        if (c[k] >= 0) {
            sx += pose[c[k]].mx * w[k];
            sy += pose[c[k]].my * w[k];
            sw += w[k];
        }
    if (sw <= 0)
        return 0;
    *mx = sx / sw;
    *my = sy / sw;
    return 1;
}

static void spring(float *x, float *v, float target, float dt, float omega, float zeta)
{
    float a = omega * omega * (target - *x) - 2 * zeta * omega * *v;
    *v += a * dt;
    *x += *v * dt;
}

/* a spring over a frame's time in steps short enough to keep it steady (P111): at most 10 ms and
 * at least 4 steps (as before at 25 frames a second and faster), shorter for a quick or stiff
 * spring.  Four steps of a whole frame's time made the recoil's spring (OMEGA 30) grow at under
 * 7.5 frames a second, and the follow spring (22) at under 5.5: the arm flew off the view */
static void spring_dt(float *x, float *v, float target, float dt, float omega, float zeta)
{
    float h = 0.01f;
    int n, i;
    if (omega * h > 0.5f)
        h = 0.5f / omega;
    if (zeta * omega * h > 0.5f)
        h = 0.5f / (zeta * omega);
    n = (int)ceilf(dt / h);
    n = n < 4 ? 4 : n > 1000 ? 1000 : n;
    for (i = 0; i < n; i++)
        spring(x, v, target, dt / n, omega, zeta);
}

void port_arms_draw(uint8_t *high, uint32_t *tc, const uint8_t *tc_rows, int W, int H, int Hd, double pu,
                    double pv, int mount, int suit)
{
    uint64_t now = port_now_us();
    float dt = last_us ? (float)(now - last_us) / 1e6f : 0.04f, tu, tv, mx, my, ox, oy, kx, ky;
    int left, best = -1, i, x, y, X0, X1, Y0, Y1, want = mount == 2 || mount == 3 ? mount : 0, snap = 0, ir;
    static int last_mount, shown;       /* the arm drawn: 2 left, 3 right, 0 none */
    static float drop = 1;              /* how far it is lowered: 0 up .. 1 below the view */
    const unsigned char *pix;
    Pose *p;

    last_us = now;
    muz_mount = 0;
    if (dt > 0.25f || dt <= 0)
        dt = 0.04f;
    {                                   /* the walk: the suit's speeds on the ground (its move in the
                                           game's time: no move in a frame of the same game time, as
                                           paused, keeps them), smoothed, and the bob's phase in the
                                           game's time (owner: in time with the footsteps
                                           it was quick, 4 dips a second forward, and backing up 6.7) */
        double f, sd, gs;
        float sp;
        if (port_player_motion(&f, &sd, &gs)) {
            if (gs > 0) {
                walk_tf = (float)(f / gs);
                walk_ts = (float)(sd / gs);
            }
        } else
            walk_tf = walk_ts = 0;
        spring_dt(&walk_f, &walk_fv, walk_tf, dt, 7.0f, 1.0f);
        spring_dt(&walk_s, &walk_sv, walk_ts, dt, 7.0f, 1.0f);
        sp = sqrtf(walk_f * walk_f + walk_s * walk_s) / walk_speed;
        sp = sp > 1 ? 1 : sp;
        walk_ph = fmodf(walk_ph + 3.1415927f * walk_rate * (0.4f + 0.6f * sp) * (float)gs, 6.2831853f);
    }
    if (!update(suit)) {
        fired = 0;
        return;
    }
    if (mount != last_mount) {
        port_log("Arms pack: the selected weapon's mount %d\n", mount);
        last_mount = mount;
    }
    if (!npose || !high || W <= 0 || H <= 0) {
        fired = 0;
        return;
    }
    /* a weapon in the other arm selected (owner): the arm drawn goes down out of the view, then
     * the other comes up from below */
    if (shown != want) {
        drop += dt / 0.18f;
        if (drop >= 1) {
            drop = 1;
            snap = shown != 0 || want != 0;
            shown = want;
        }
    } else if (shown && drop > 0) {
        drop -= dt / 0.22f;
        if (drop < 0)
            drop = 0;
    }
    if (!shown) {
        fired = 0;
        return;
    }
    left = shown == 2;
    /* the pointer (mirrored for the left arm), with mouselook's sway: the arm lags behind the
     * turn (0x10000 a full turn; the view's half width about a radian) */
    tu = (float)(left ? -pu : pu);
    tv = (float)pv;
    {
        float turn = port_ml_frame_turn * (6.2831853f / 65536) / dt, look = port_ml_frame_look * (6.2831853f / 65536) / dt;
        float du = -turn * sway_k, dv = look * sway_k;
        du = du > sway_mu ? sway_mu : du < -sway_mu ? -sway_mu : du;
        dv = dv > sway_mv ? sway_mv : dv < -sway_mv ? -sway_mv : dv;
        tu += left ? -du : du;
        tv += dv;
        port_ml_frame_turn = port_ml_frame_look = 0;
    }
    if (nu > 1)
        tu = tu < us[0] ? us[0] : tu > us[nu - 1] ? us[nu - 1] : tu;
    if (nv > 1)
        tv = tv > vs[nv - 1] ? vs[nv - 1] : tv;     /* below the grid: the bottom row's pose, moved down */
    if (snap) {                         /* the arm that comes up starts at the pointer */
        su = tu;
        sv = tv;
        vu = vv = 0;
    }
    spring_dt(&su, &vu, tu, dt, follow_w, follow_z);
    spring_dt(&sv, &vv, tv, dt, follow_w, follow_z);
    /* the recoil by the weapon's rate of fire (owner: the laser's quick shots shook the
     * hand): a slow weapon (a shot every RATE_SLOW s or less often) kicks the whole way and springs
     * straight back; the quicker the shots, the smaller each kick and the slower the arm comes
     * back, and while they keep coming it stays pushed back part of the way (held against the
     * kicks), so a stream of shots pulses gently instead of shaking */
    {
        float rest = 0, amp = 1, w = 30.0f;
        if (fired) {
            float d = fire_us ? (float)(now - fire_us) / 1e6f : 9;
            gap = d > 1.0f ? 0 : gap > 0 ? gap * 0.5f + d * 0.5f : d;
            fire_us = now;
            fired = 0;
        }
        if (gap > 0 && gap < RATE_SLOW) {
            float q = gap / RATE_SLOW;  /* 0 a stream .. 1 slow */
            amp = 0.15f + 0.85f * q;
            w = 10.0f + 20.0f * q;
            if ((float)(now - fire_us) / 1e6f < gap * 1.5f + 0.05f)
                rest = 0.4f * (1 - q);
        }
        if (fire_us == now) {
            float top = rest + amp * (1 - rest);
            if (kick < top)
                kick = top;
            kickv = 0;
        }
        spring_dt(&kick, &kickv, rest, dt, w, 0.9f);
    }

    {                                   /* the nearest pose, moved to the muzzle between the poses */
        float bd = 1e9f;
        for (i = 0; i < npose; i++) {
            float d = (pose[i].u - su) * (pose[i].u - su) + (pose[i].v - sv) * (pose[i].v - sv);
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
    }
    p = &pose[best];
    if (!muzzle_at(su, sv, &mx, &my)) {
        mx = p->mx;
        my = p->my;
    }
    if (nv > 1 && sv < vs[0]) {         /* below the grid the arm goes down with the pointer (owner),
                                           the muzzle no lower than the view's bottom edge */
        float down = (vs[0] - sv) * vh / 2, low = vy + vh - 6.0f - my;
        my += down < low ? down : low > 0 ? low : 0;
    }
    if (have_recoil && npose) {         /* the recoil: the kicked pose's muzzle move, scaled */
        int c = 0;
        float bd = 1e9f;
        for (i = 0; i < npose; i++) {
            float d = (pose[i].u - recoil.u) * (pose[i].u - recoil.u) + (pose[i].v - recoil.v) * (pose[i].v - recoil.v);
            if (d < bd) {
                bd = d;
                c = i;
            }
        }
        mx += (recoil.mx - pose[c].mx) * 2 * kick;
        my += (recoil.my - pose[c].my) * 2 * kick;
    } else {
        mx += 3 * kick;
        my += 2 * kick;
    }
    if (walk_bob > 0) {                 /* walking (owner): the arm dips and swings to a
                                           side and back between two dips (a U), by the speed, and
                                           lags behind a strafe (on the screen: the left arm is
                                           mirrored) and a walk forward or back */
        float sp = sqrtf(walk_f * walk_f + walk_s * walk_s) / walk_speed, lf = walk_f / walk_speed,
              ls = walk_s / walk_speed;
        sp = sp > 1 ? 1 : sp;
        lf = lf > 1 ? 1 : lf < -1 ? -1 : lf;
        ls = ls > 1 ? 1 : ls < -1 ? -1 : ls;
        my += sp * walk_bob * vh * (0.5f + 0.5f * cosf(2 * walk_ph));     /* lowest at 0, pi */
        mx += sp * walk_sway * vw * sinf(walk_ph);
        mx += (left ? ls : -ls) * walk_lean * vw;
        my += lf * walk_lean * 0.5f * vh;
    }
    if (drop > 0) {                     /* lowered for a weapon switch: its top below the view at 1 */
        float e = drop * drop * (3 - 2 * drop);
        my += e * (vy + vh * (Hd > H ? (float)Hd / H : 1) + 4 - p->by0 - (my - p->my));
    }
    ir = port_ir_showing();
    pix = ir ? pix_ir_of(p) : pix_of(p);
    if (!pix)
        return;
    kx = (float)W / vw;
    ky = (float)H / vh;
    ox = (mx - p->mx - vx) * kx;        /* the picture's place in the view's pixels */
    oy = (my - p->my - vy) * ky;
    X0 = (int)floorf(p->bx0 * kx + ox);
    X1 = (int)ceilf(p->bx1 * kx + ox);
    Y0 = (int)floorf(p->by0 * ky + oy);
    Y1 = (int)ceilf(p->by1 * ky + oy);
    if (X0 < 0) X0 = 0;
    if (Y0 < 0) Y0 = 0;
    if (X1 > W) X1 = W;
    if (Y1 > (Hd > H ? Hd : H)) Y1 = Hd > H ? Hd : H;    /* (the rows drawn below the view too) */
    for (y = Y0; y < Y1; y++) {
        int syi = (int)((y - oy) / ky) - p->by0;
        const unsigned char *srow;
        uint8_t *drow = high + (size_t)y * W;
        uint32_t *trow = tc && tc_rows[y] ? tc + (size_t)y * W : NULL;
        if (syi < 0 || syi >= p->bh)
            continue;
        srow = pix + (size_t)syi * p->bw;
        for (x = X0; x < X1; x++) {
            int sxi = (int)((x - ox) / kx) - p->bx0, dx = left ? W - 1 - x : x;
            unsigned char c;
            if (sxi < 0 || sxi >= p->bw || !(c = srow[sxi]))
                continue;
            drow[dx] = c;
            if (trow)
                trow[dx] = 0;
        }
    }
    if (drop < 0.3f) {                  /* the shots leave from here (port_arms_shot) */
        muz_fx = (mx - vx) * kx;
        muz_fy = (my - vy) * ky;
        if (left)
            muz_fx = W - 1 - muz_fx;
        muz_mount = shown;
    }
}

/* the muzzle of the arm drawn by the last port_arms_draw, in its picture's pixels, and its mount
 * (2 left, 3 right); 0 when no arm was up */
int port_arms_muzzle(float *fx, float *fy)
{
    *fx = muz_fx;
    *fy = muz_fy;
    return muz_mount;
}
