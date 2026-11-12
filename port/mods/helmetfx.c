/* helmetfx.c -- Freefall: the helmet's moves ("Helmet motion" on the Mods page, with a cockpit pack's picture: cockpitpack.c, platform.c, sharp3d.c).
 *
 * The helmet is worn: it goes where the head goes, so the world moves on the screen and the
 * helmet stays, but for a small lag and spring (the visor style, Crysis or Halo).  This file works
 * out that small move from what the game's eye did and hands it to the display (its turn moves
 * the helmet 19.2 pixels a degree of a 1920 screen, the eye's offset by the parallax at each
 * part's distance: platform.c hlp_offsets).  The pack's picture is in parts (its -parts.png, cockpitpack.c): each
 * part moves by its own share of that move (the nearer the eye, the more), the 3D, the crosshair
 * and the targets' boxes stay still under them (platform.c hlp_draw).
 *
 * The game thread calls port_helmet_eye once a frame, from hud_eye_player, after the eye was set
 * and before the game's own shake of the view is added.  From the eye of the last frame and the
 * game's time between: the turn and the look up or down (degrees a second; the helmet lags behind
 * by LAG seconds' worth, at most MAXLAG degrees, on a spring that rings a little after the turn
 * stops), the walk (the eye's speed on the ground: a bob on the arms' clock, 2.6 dips a second at
 * full speed, a little after the head), and a landing (the eye's fall stopping: the helmet goes on
 * down a few pixels and springs back).  A shot of the player (port_helmet_fired, from arms.c's
 * port_arms_fired) kicks the helmet up a little; a hit on the player (port_helmet_hit, from the
 * game's damage shake with the damage taken) shakes it and breaks the screens into static.
 *
 * All of it is the display's state: the game's data, its random numbers and its frames are not
 * touched (the shake's directions come from a generator of this file), so lockstep runs, demos and
 * saves are the same with it on or off.  A pause (no game time) holds the helmet where it is.
 */
#include <math.h>
#include <string.h>
#include "g7_a.h"
#include "g5_c.h"
#include "sup_c.h"
#include "g8_c.h"
#include "port.h"
#include "platform.h"
#include "helmetfx.h"

long labs(long);                        /* (stdlib.h is not included: its valloc clashes with 2d.h) */

int port_helmet_motion = 0;             /* "Helmet motion": 0 Off, 1 On (screen.c) */

#define PI 3.14159265358979
#define LAG 0.045                       /* seconds of the turn the helmet is behind */
#define MAXLAG 2.6                      /* ... at most (degrees) */
#define WALK_SPEED 10.0                 /* m/s: the suits walk 10 to 12 */
#define WALK_RATE 2.6                   /* dips a second at full speed (the arms') */

/* a spring: x follows target, omega rad/s, zeta damping */
typedef struct { double x, v; } Spring;
static void spring(Spring *s, double target, double dt, double omega, double zeta)
{
    double a = omega * omega * (target - s->x) - 2 * zeta * omega * s->v;
    s->v += a * dt;
    s->x += s->v * dt;
}

/* the game thread's state */
static int have;                        /* the last eye is known */
static long lx, ly, lz;
static unsigned short lh, lp;
static unsigned long lt;
static double vz_last;                  /* the eye's last speed up (m/s) */
static Spring yaw, pit;                 /* the lag behind the turn and the look (degrees) */
static Spring land_y, land_p;           /* the landing (m, degrees) */
static Spring kick_p;                   /* a shot (degrees) */
static Spring hit_p, hit_x;             /* a hit (degrees, m) */
static Spring walk_v;                   /* the walk's speed, smoothed (m/s) */
static double walk_ph;                  /* the bob's phase: a dip each half turn */
static volatile int fired, hit_amt;
static unsigned rnd = 0x9E3779B9u;

/* what the display reads (double buffered: pub is the one to read) */
static port_helmet_state st[2];
static volatile int pub;           /* one writer (the game thread), a struct copied by the reader */
static unsigned long fire_t, hit_t;    /* the game's time of the last shot, hit */
static int fire_set, hit_set;
static double hit_k;

static double frand(void)               /* -1 .. 1, this file's own numbers */
{
    rnd ^= rnd << 13;
    rnd ^= rnd >> 17;
    rnd ^= rnd << 5;
    return (rnd & 0xFFFF) / 32767.5 - 1;
}

static void reset(void)
{
    have = 0;
    memset(&yaw, 0, sizeof yaw);
    memset(&pit, 0, sizeof pit);
    memset(&land_y, 0, sizeof land_y);
    memset(&land_p, 0, sizeof land_p);
    memset(&kick_p, 0, sizeof kick_p);
    memset(&hit_p, 0, sizeof hit_p);
    memset(&hit_x, 0, sizeof hit_x);
    memset(&walk_v, 0, sizeof walk_v);
    vz_last = 0;
}

void port_helmet_fired(void)
{
    fired = 1;
    fire_t = game_time;
    fire_set = 1;
}

void port_helmet_hit(int amount)
{
    if (amount <= 0)
        return;
    hit_amt += amount;
    hit_k = amount >= 24 ? 1.0 : 0.45 + 0.55 * amount / 24.0;
    hit_t = game_time;
    hit_set = 1;
}

/* the hit's static for the game's time now (into o) */
static void lights(port_helmet_state *o)
{
    double t;
    o->hit = 0;
    t = (double)(long)(game_time - hit_t) / 1000;
    if (hit_set && port_helmet_motion && t >= 0 && t < 0.35)
        o->hit = hit_k * (1 - t / 0.35);
}

static void publish(const port_helmet_state *v)
{
    st[pub ^ 1] = *v;
    __sync_synchronize();
    pub ^= 1;
}

void port_helmet_eye(void)
{
    GameEntity *e;
    double dt, wy, wp, k, sp, vz;
    long dx, dy;
    int i, n, ground;

    if (!port_helmet_motion || !game_player_entity) {
        port_helmet_state z;
        if (have)
            reset();
        fired = hit_amt = 0;
        memset(&z, 0, sizeof z);
        lights(&z);
        publish(&z);
        return;
    }
    if (have && game_time == lt)        /* the same frame again (or paused): hold */
        return;
    e = &game_entities[game_player_entity];
    dt = have ? (double)(long)(game_time - lt) / 1000 : 0;
    dx = hud_ffview.x - lx;
    dy = hud_ffview.y - ly;
    if (!have || dt <= 0 || dt > 0.25 || labs(dx) > 0x10000 || labs(dy) > 0x10000) {
        reset();                        /* placed, loaded or long ago: start at rest */
        have = 1;
        lx = hud_ffview.x;
        ly = hud_ffview.y;
        lz = hud_ffview.z;
        lh = hud_ffview.heading;
        lp = hud_ffview.pitch;
        lt = game_time;
        fired = hit_amt = 0;
        {
            port_helmet_state z;
            memset(&z, 0, sizeof z);
            lights(&z);
            publish(&z);
        }
        return;
    }
    /* the turn and the look in degrees a second (0x10000 a full turn) */
    wy = (short)(unsigned short)(hud_ffview.heading - lh) * (360.0 / 65536) / dt;
    wp = (short)(unsigned short)(hud_ffview.pitch - lp) * (360.0 / 65536) / dt;
    /* the eye's speed on the ground and up (a map unit 0x10000 is 6 m) */
    sp = sqrt((double)dx * dx + (double)dy * dy) * 6 / 65536 / dt;
    vz = (double)(hud_ffview.z - lz) * 6 / 65536 / dt;
    ground = e->z - map_2949C4(e->x, e->y) <= 0x8000;
    lx = hud_ffview.x;
    ly = hud_ffview.y;
    lz = hud_ffview.z;
    lh = hud_ffview.heading;
    lp = hud_ffview.pitch;
    lt = game_time;

    /* a landing: the fall stopped (the speed up rose by more than 2.5 m/s in a frame) */
    {
        double dv = vz - vz_last;
        vz_last = vz;
        if (dv > 2.5) {
            double a = dv > 14 ? 1 : dv / 14;
            land_y.v += 0.0035 * a * 15;    /* the helmet goes on down (the eye up in it) */
            land_p.v += 1.2 * a * 15;
        }
    }
    if (fired) {                        /* a shot: a small kick up, less in a stream of shots */
        static unsigned long last;
        static int last_set;
        double gap = last_set ? (double)(long)(fire_t - last) / 1000 : 9;
        last = fire_t;
        last_set = 1;
        kick_p.v -= (gap < 0.3 ? 0.2 : 0.5) * 38;
        fired = 0;
    }
    if (hit_amt) {                      /* a hit: a short shake */
        double a = hit_k;
        hit_p.v += 1.4 * a * 56 * (frand() < 0 ? -1 : 1);
        hit_x.v += 0.0025 * a * 44 * (frand() < 0 ? -1 : 1);
        hit_amt = 0;
    }
    n = (int)(dt / 0.002) + 1;          /* the springs in steps of at most 2 ms */
    for (i = 0; i < n; i++) {
        double h = dt / n;
        double ty = -LAG * wy, tp = -LAG * wp;
        ty = ty > MAXLAG ? MAXLAG : ty < -MAXLAG ? -MAXLAG : ty;
        tp = tp > MAXLAG ? MAXLAG : tp < -MAXLAG ? -MAXLAG : tp;
        spring(&yaw, ty, h, 12, 0.45);
        spring(&pit, tp, h, 12, 0.45);
        spring(&land_y, 0, h, 15, 0.3);
        spring(&land_p, 0, h, 15, 0.3);
        spring(&kick_p, 0, h, 38, 0.32);
        spring(&hit_p, 0, h, 56, 0.09);
        spring(&hit_x, 0, h, 44, 0.11);
        spring(&walk_v, ground ? (sp > 30 ? 30 : sp) : 0, h, 7, 1);
    }
    k = walk_v.x / WALK_SPEED;
    k = k > 1 ? 1 : k < 0 ? 0 : k;
    walk_ph = fmod(walk_ph + PI * WALK_RATE * (0.4 + 0.6 * k) * dt, 2 * PI);

    {
        port_helmet_state v;
        double m = 1.0;                 /* On: the full move (the old Full; Light was half) */
        k *= k < 0.05 ? 0 : 1;
        v.yaw = m * yaw.x;
        v.pitch = m * (pit.x + land_p.x + kick_p.x + hit_p.x);
        v.ex = m * (k * 0.0010 * sin(walk_ph - 0.9) - 0.00004 * (wy > 60 ? 60 : wy < -60 ? -60 : wy) + hit_x.x);
        v.ey = m * (k * 0.0012 * sin(2 * walk_ph - 0.9) + land_y.x);
        lights(&v);
        publish(&v);
    }
}

/* display: the helmet's move now and the hit's static (0 .. 1) */
volatile int port_helmet_sway;

void port_helmet_get(port_helmet_state *out)
{
    *out = st[pub];
    if (!port_helmet_motion)
        out->yaw = out->pitch = out->ex = out->ey = out->hit = 0;
}
