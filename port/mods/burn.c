/* burn.c -- Freefall: "Tree explosion" on the Detail page (Original / Realistic): a tree
 * or bush that dies bursts on its own picture in the sharp 3D view, in place of the game's
 * fireball, and what is left of it stays standing.  The owner's pick of the trees thread's
 * mock-ups, its "Explosive v2" (the rules, times and colours are that thread's,
 * docs/modding/EFFECT-PACKS.md "Tree explosion"; the other pick, Flash fire, was taken out on the owner's
 * word, for its cost with many plants):
 *
 *  - A light-up that spreads from the blast's centre over the plant (3 frames, 180 ms), then the
 *    crown breaks into 12 to 60 spinning shards of its own pixels (most of them burning, with
 *    sparks), a spray of 40 to 150 needles, 8 to 50 pieces of wood, bark and leaf (the pack's
 *    debris sheets in the plant's own colours), 10 to 35 burnt leaves thrown high that flutter
 *    down and sparks streaking up; the pack's plant blast from its picture 3 (the light-up makes
 *    the start), in front of what is left, brown dust (1170) rolling out along the ground and a
 *    smoke plume (1175).  0.18 s + 2.4 s + 1 s.  The mark on the ground is the game's own (res
 *    1164, the pack's BlastMarks when it has them: marks.c).
 *  - What is left: the pack's leftover picture of the plant (Leftovers), its broken top glowing
 *    and shedding ash for a while; it stays until the mission ends (the "stumps", up to 240, the
 *    oldest first to go).  A plant without one keeps the first version's
 *    rule: a jagged trunk that chars and falls apart in scattered pixels, some of them ash, onto
 *    a small pile at the foot that fades.
 *  - The fewer pieces, the smaller the plant (by its crown's pixels).  The game's fireball stays
 *    as the blast when the pack has neither a PlantBlast nor a 1171.
 *  - A plant with a broken picture (13 kinds of tree and bush; the game's rule, flash_28E6C0: hit
 *    for at least its hit points and at most twice them) that breaks goes whole, the same as one
 *    without, and what is left is the pack's leftover of the plant (the owner: no
 *    sticks of a pine left standing over the pack's stump).  Since P109 the game no longer keeps
 *    it as its broken picture with Tree explosion on: flash_28E040 removes it at once
 *    (KillObjMark, as the second hit would), so no unseen obstacle stays (owner).  Its
 *    broken picture is still left out of the drawing for that frame (port_burn_covers).  A
 *    broken picture that broke before (in a saved game, or with Tree explosion Original) is
 *    drawn, and when it is destroyed nothing bursts: a puff of dust and smoke (rubble_smoke,
 *    1.6 s; the pack's 1170 and 1175, else the game's fireball), leaving the pack's leftover of
 *    the broken picture.
 *
 *  - Picture only, in the sharp view: the game (its effect object, the plant's death, what is
 *    hit) is the same but for the broken plants above (P109), and nothing of it is kept in a saved game (after loading one, only the
 *    plants that die after show what is left).  Time: the animations' clock (game_time), so a
 *    paused game pauses it.  The mock-ups' frames were 60 ms ("frames" here) and their plant 240
 *    pixels high: the rules scale with the plant's picture (s = its height / 240).  The pixels
 *    and the shards follow the time of the frame drawn; the pieces and particles move in steps of
 *    a quarter frame.
 *  - Cost: the pictures are worked out once when the plant dies; each frame only copies them to
 *    the screen and moves a few hundred pieces (the glowing top: the pixels of its band).
 *  - The plant's picture: the objects pack's frame when it has one, else the game's.  The mixes
 *    of its colours with fire and char are the palette's nearest colours (Lab, as the effects
 *    pack's see-through pixels), made once per palette.  The plant's own pixels, the char, the
 *    ash and the smoke go through the plant's light table (its place's light, the fog), the
 *    glowing top, the burning pieces and the sparks through an effect's (full light; hot in the
 *    infrared), the light-up from the one to the other as it grows (the light rows between).
 *  - Drawn from the view's square lists (viewdist.c), so the land, buildings and nearer objects
 *    hide it as they hid the plant; a burn's box (drawobj_29CCE0) is 3 plant widths by 1.8
 *    heights or the blast's size, room for the smoke and the flying pieces, a stump's the plant's,
 *    and the drawing is cut to it.
 *  - Memory: a fixed arena (the game's heap stays the original's), a block of the biggest size
 *    for each of the 16 burns, so every burn has room (a 17th at a time keeps its fireball, and
 *    its stump stands from the start).
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "g5_b.h"
#include "g5_c.h"
#include "g6_a.h"
#include "g5_a.h"
#include "port.h"
#include "fxpack.h"

int port_burn;                          /* the setting (screen.c): 0 Original, 1 Realistic */
GameEntity port_burn_ent;               /* the burn or stump being drawn, as an entity (viewdist.c, drawobj.c) */

#define BURNS 16
#define STUMPS 240                      /* ids BURNS .. BURNS + STUMPS - 1 (the nodes' tmpl 0xFF00 + id) */
#define IDS (BURNS + STUMPS)
#define GRID_PX 262144l                 /* a picture's pixels at most (512 x 512; bigger halved) */
#define BLOCK (((size_t)GRID_PX * 7 + 64 + 15) & ~(size_t)15)    /* a burn's memory at most (burn_ready) */
#define ARENA (BURNS * BLOCK)
#define PARTS 2500
#define SHARDS 60
#define FLAKES 96
#define SPARKS 32
#define PIECES 50
#define COLS 640                        /* a picture's columns at most */
#define NF_BLAST 40                     /* the main burst's frames */
#define THR 40000.0f                    /* the fade's thresholds' 1.0 */
#define LU_N 16                         /* the light-up's mixes: 0 .. 0.6 of its colour */
#define LU_MS 60                        /* what follows the light-up is the first version's frame 2 on, this much later */
#define SMOKE_MS 1600                   /* a broken picture's puff */

enum { OUT_GLOW = 0x100, OUT_LIFT = 9 };    /* OUT_LIFT: the light-up's 1..14 fifteenths of the way to full light */

typedef struct {
    float x, y, vx, vy, g, life, size;  /* the picture's pixels, per frame */
    uchar c, glow;                      /* colour; through the effect's light table */
} Part;

typedef struct {
    float x, y, vx, vy, ang, spin, life;
    short bx, by, n;                    /* the block of the picture: top left, side */
    float e[3][3];                      /* its triangle's edges: e0 x + e1 y + e2 >= 0 inside */
    uchar burn;
} Shard;

typedef struct {                        /* ash and burnt leaves fluttering down */
    float x, y, vx, vy, life, ph, fq;
    uchar c, st, landed;                /* c: 0..7 ash, 8 an ember */
} Flake;

typedef struct {
    float x, y, vx, vy, life;
} Spark;

typedef struct {                        /* a piece of wood, bark or leaf from the debris sheets */
    float x, y, vx, vy, a, spin, life;
    uchar kind, row, hot;               /* kind: 0 wood, 1 bark, 2 leaf (the sheets) */
} Piece;

typedef struct {
    int used, ready, failed;
    long slot;
    void *anim;
    ulong t0;                           /* game_time at the death */
    long ref;
    fix w, x, y, z;
    uchar type, mirror;
    int stump;                          /* its stump (0 .. STUMPS - 1), -1 none */
    int smoke;                          /* the broken picture went: dust and smoke only */
    int unseen;                         /* smoke: that picture was not drawn (gone) */
    unsigned stamp, seed, rng;
    long span_a, span_b;                /* the viewer's square: its occlusion span, haze */
    /* the picture (the arena's block mem) */
    int gw, gh, L;                      /* L: picture pixels per mock-up pixel (s rounded) */
    float s;
    int lmode;                          /* 1: the pack's leftover stays; 0: the first version's trunk */
    uchar *mem;
    size_t len;
    uchar *pix, *n2, *aux;              /* n2: noise, then (after burn_ready) the light-up's distance;
                                         * aux: 1 what is left (the trunk), 2 the trunk's glowing top */
    uchar *left;                        /* lmode: the leftover's colours (0 none) */
    int32_t *hot;                       /* lmode: the glowing top's pixels */
    long nhot;
    ushort *thr, *out;                  /* the fade's threshold (not lmode); the colours drawn */
    long ncrown;
    short ctop[COLS];                   /* lmode: each column's top row of the leftover (-1 none) */
    short lb[4];                        /* lmode: the box of the leftover's pixels (x0, y0, x1, y1) */
    uchar tramp[7], cramp[5];           /* the trunk's and the crown's colours, dark to light */
    int ev_stage, ev_tip;
    /* the colours of the palette pal */
    ulong pal, pc_pal;
    uchar pc_cold[2][16], pc_hot[2][16][8];     /* the pieces' (by the trunk's, the crown's ramp) */
    /* the motion */
    long steps, ev_ms;
    float lim_prev, lost;               /* the fade's last threshold; pixels lost (mock-up's) */
    int np, ns, nfl, nspk, npc;
    Part part[PARTS];
    Shard sh[SHARDS];
    Flake fl[FLAKES];
    Spark spk[SPARKS];
    Piece pc[PIECES];
} Burn;

typedef struct {                        /* what is left of a plant, standing until the mission ends */
    int used, burn;                     /* burn: the one drawing it while it lasts (-1 none) */
    ulong t0;
    long ref;
    fix w, x, y, z;
    uchar type;
    unsigned seed, stamp;
    long span_a, span_b;
} Stump;

static Burn burns[BURNS];
static Stump stumps[STUMPS];
/* a plant that broke: its broken picture is not drawn (at, its place: the entity is the same plant) */
static struct { uchar on; fix x, y; } gone[GAME_MAX_ENTITIES];
static short head[256], nxt[IDS];       /* the ids on a square's bucket (id + 1, 0 none) */
static int cur = -1;                    /* port_burn_set's id */
static const FxPic *cur_left;           /* a stump's leftover, this drawing */
static unsigned stamp;                  /* the drawings */
static uchar arena[ARENA];
static float nb[GRID_PX], nt[GRID_PX];  /* noise, scratch */

/* the fire's ramp (dark to light), char */
static const uchar FIRE[9][3] = { { 99, 41, 2 }, { 120, 56, 20 }, { 172, 92, 19 }, { 200, 104, 13 }, { 227, 115, 6 },
                                  { 255, 149, 41 }, { 255, 167, 78 }, { 239, 211, 83 }, { 243, 223, 123 } };
static const uchar CHAR_RGB[3] = { 28, 24, 20 };
static const uchar BAYER[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
/* a flake's four shapes, (dy, dx) in mock-up pixels: 3 across, a \ pair, one, a / pair */
static const signed char FLK[4][3][2] = { { { 0, -1 }, { 0, 0 }, { 0, 1 } }, { { -1, -1 }, { 0, 0 }, { 99, 0 } },
                                          { { 0, 0 }, { 99, 0 }, { 99, 0 } }, { { -1, 1 }, { 0, 0 }, { 99, 0 } } };

/* palette colours shared by all burns, for the palette col_pal */
static ulong col_pal;
static uchar fire_c[33], fire9[9], grey_c[8], pile_c[4], flk_c[9][3], spark_head;

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

static unsigned rnd(Burn *b)
{
    unsigned x = b->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return b->rng = x;
}

static float frand(Burn *b) { return (rnd(b) >> 8) * (1.0f / 16777216.0f); }
static float nrand(Burn *b) { return (frand(b) + frand(b) + frand(b) + frand(b) - 2.0f) * 1.7320508f; }
static int irand(Burn *b, int lo, int hi) { return lo + (int)(frand(b) * (hi - lo)); }    /* lo .. hi - 1 */

static unsigned hash(unsigned x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static void fire_rgb(float v, float *o)
{
    int i, j, c;
    float f;
    v = clampf(v, 0, 1) * 8;
    i = (int)v;
    j = i < 8 ? i + 1 : 8;
    f = v - i;
    for (c = 0; c < 3; c++)
        o[c] = FIRE[i][c] * (1 - f) + FIRE[j][c] * f;
}

static uchar near3(const float *o)
{
    return port_fx_near((long)(o[0] + 0.5f), (long)(o[1] + 0.5f), (long)(o[2] + 0.5f));
}

static uchar near_k(const float *o, float k)
{
    float q[3] = { o[0] * k, o[1] * k, o[2] * k };
    return near3(q);
}

static float lum(int c)
{
    long r, g, b;
    port_fx_rgb(c, &r, &g, &b);
    return r * 0.3f + g * 0.55f + b * 0.15f;
}

/* the shared colours, for the palette in use */
static void colours(ulong key)
{
    float o[3];
    int i, c;
    if (col_pal == key)
        return;
    for (i = 0; i <= 32; i++) {
        fire_rgb(i / 32.0f, o);
        fire_c[i] = near3(o);
    }
    for (i = 0; i < 9; i++) {
        for (c = 0; c < 3; c++)
            o[c] = FIRE[i][c];
        fire9[i] = near3(o);
    }
    for (i = 0; i < 9; i++) {           /* the ash: 403c38 +- 14, and an ember; a flake's shades */
        if (i < 8) {
            o[0] = 64 - 14 + i * 4.0f;
            o[1] = 60 - 14 + i * 4.0f;
            o[2] = 56 - 14 + i * 4.0f;
        } else
            for (c = 0; c < 3; c++)
                o[c] = FIRE[3][c];
        flk_c[i][0] = near3(o);
        flk_c[i][1] = near_k(o, 1.15f);
        flk_c[i][2] = near_k(o, 0.85f);
        if (i < 8)
            grey_c[i] = flk_c[i][0];
    }
    for (i = 0; i < 4; i++)             /* the pile: 343028 +- 10 */
        pile_c[i] = port_fx_near(42 + i * 6, 38 + i * 6, 34 + i * 6);
    spark_head = port_fx_near(255, 240, 190);
    col_pal = key;
}

/* the palette's colours mixed with the light-up's (0 .. 0.6 of it), dark, char, the trunk's
 * charring and the shards' fire (for the palette pal) */
static struct {
    ulong pal;
    uchar lu[LU_N][256], dark[256], schar[256], trunk[8][256], shot[8][256];
} mx;

/* the plant's colours mixed: (1 - k) of its own and k of rgb, by the table */
static void mix_table(uchar *t, float keep, const float *rgb, float k)
{
    int c;
    for (c = 1; c < 256; c++) {
        long r, g, b;
        float o[3];
        port_fx_rgb(c, &r, &g, &b);
        o[0] = r * keep + rgb[0] * k;
        o[1] = g * keep + rgb[1] * k;
        o[2] = b * keep + rgb[2] * k;
        t[c] = near3(o);
    }
    t[0] = 0;
}

static void burn_colours(Burn *b, ulong key)
{
    float o[3], ch[3] = { CHAR_RGB[0], CHAR_RGB[1], CHAR_RGB[2] }, z[3] = { 0, 0, 0 };
    int q, c;
    colours(key);
    if (b->pal == key)
        return;
    if (mx.pal != key) {                /* (the same for every plant: once per palette) */
        o[0] = 255; o[1] = 200; o[2] = 120;
        for (q = 0; q < LU_N; q++) {
            float f = 0.6f * q / (LU_N - 1);
            mix_table(mx.lu[q], 1 - f, o, f);
        }
        for (c = 0; c < 256; c++)
            mx.lu[0][c] = (uchar)c;
        mix_table(mx.dark, 0.5f, z, 0);
        mix_table(mx.schar, 0.4f, ch, 0.6f);
        for (q = 0; q < 8; q++) {       /* the trunk charring, the shards burning (by q / 7) */
            float tt = q / 7.0f, e = q / 7.0f;
            for (c = 0; c < 3; c++)
                o[c] = ch[c] * (0.5f + 0.5f * tt);
            mix_table(mx.trunk[q], (1 - tt) * 0.5f, o, 1);
            fire_rgb(0.35f + 0.6f * e, o);
            mix_table(mx.shot[q], 0.3f, o, 0.7f);
        }
        mx.pal = key;
    }
    b->pal = key;
    b->ev_stage = 0;                    /* the colours drawn are made again */
}

/* the pieces' colours: a sheet's grey (by 16ths) along the trunk's ramp (wood, bark: its 7
 * colours, a darker end x0.4 and a lighter x1.15) or the crown's (leaf), 80 % of it with 20 %
 * char, or burning: 40 % with 60 % fire, by heat q / 7 */
static void piece_colours(Burn *b, ulong key)
{
    float ramp[9][3], ch[3] = { CHAR_RGB[0], CHAR_RGB[1], CHAR_RGB[2] };
    int r, n, g, q, c, i;
    if (b->pc_pal == key)
        return;
    for (r = 0; r < 2; r++) {
        n = r ? 5 : 9;
        for (i = 0; i < n; i++) {
            long cr, cg, cb;
            float k = 1;
            int idx = r ? b->cramp[i] : b->tramp[i < 1 ? 0 : i > 7 ? 6 : i - 1];
            if (!r && i == 0)
                k = 0.4f;
            else if (!r && i == 8)
                k = 1.15f;
            port_fx_rgb(idx, &cr, &cg, &cb);
            ramp[i][0] = cr * k;
            ramp[i][1] = cg * k;
            ramp[i][2] = cb * k;
        }
        for (g = 0; g < 16; g++) {
            float v = (g + 0.5f) / 16 * (n - 1), f, o[3], w[3];
            int a = (int)v, a1 = a + 1 < n ? a + 1 : n - 1;
            f = v - a;
            for (c = 0; c < 3; c++) {
                o[c] = ramp[a][c] * (1 - f) + ramp[a1][c] * f;
                w[c] = o[c] * 0.8f + ch[c] * 0.2f;
            }
            b->pc_cold[r][g] = near3(w);
            for (q = 0; q < 8; q++) {
                float fr[3];
                fire_rgb(0.3f + 0.5f * q / 7, fr);
                for (c = 0; c < 3; c++)
                    w[c] = o[c] * 0.4f + fr[c] * 0.6f;
                b->pc_hot[r][g][q] = near3(w);
            }
        }
    }
    b->pc_pal = key;
}

/* ------------------------------------------------------------------ the arena */

/* the burn's own block: n bytes at most BLOCK (with the 3x objects packs a few burns took the
 * whole of a shared 12 MB, so the next ones had none: P70) */
static uchar *arena_get(const Burn *b, size_t n)
{
    return n <= BLOCK ? arena + (size_t)(b - burns) * BLOCK : NULL;
}

/* ------------------------------------------------------------------ the squares */

static int bucket(fix x, fix y) { return (int)(((x >> 16) * 31 + (y >> 16)) & 255); }

static void link_id(int id, fix x, fix y)
{
    int k = bucket(x, y);
    nxt[id] = head[k];
    head[k] = (short)(id + 1);
}

static void unlink_id(int id, fix x, fix y)
{
    short *p = &head[bucket(x, y)];
    while (*p) {
        if (*p - 1 == id) {
            *p = nxt[id];
            return;
        }
        p = &nxt[*p - 1];
    }
}

static void burn_free(Burn *b)
{
    if (b->stump >= 0 && stumps[b->stump].burn == b - burns)
        stumps[b->stump].burn = -1;
    unlink_id((int)(b - burns), b->x, b->y);
    b->used = 0;
    b->mem = NULL;
    b->len = 0;
}

static void stump_free(int j)
{
    Stump *st = &stumps[j];
    if (st->burn >= 0 && burns[st->burn].used && burns[st->burn].stump == j)
        burns[st->burn].stump = -1;
    unlink_id(BURNS + j, st->x, st->y);
    st->used = 0;
}

/* ffmain.c: a mission starts or ends; what is left of its plants goes */
void port_burn_reset(void)
{
    int i;
    for (i = 0; i < BURNS; i++)
        burns[i].used = 0;
    for (i = 0; i < STUMPS; i++)
        stumps[i].used = 0;
    memset(head, 0, sizeof head);
    memset(gone, 0, sizeof gone);
}

/* ------------------------------------------------------------------ noise */

/* one box blur of radius r over a (w x h) */
static void box(float *a, int w, int h, int r)
{
    int x, y, k;
    float inv = 1.0f / (2 * r + 1);
    for (y = 0; y < h; y++) {
        float *p = a + (long)y * w, sum = 0;
        for (k = -r; k <= r; k++)
            sum += p[k < 0 ? 0 : k >= w ? w - 1 : k];
        for (x = 0; x < w; x++) {
            nt[x] = sum * inv;
            sum += p[x + r + 1 < w ? x + r + 1 : w - 1] - p[x - r > 0 ? x - r : 0];
        }
        memcpy(p, nt, w * sizeof *p);
    }
    for (x = 0; x < w; x++) {
        float sum = 0;
        for (k = -r; k <= r; k++)
            sum += a[(long)(k < 0 ? 0 : k >= h ? h - 1 : k) * w + x];
        for (y = 0; y < h; y++) {
            nt[y] = sum * inv;
            sum += a[(long)(y + r + 1 < h ? y + r + 1 : h - 1) * w + x] - a[(long)(y - r > 0 ? y - r : 0) * w + x];
        }
        for (y = 0; y < h; y++)
            a[(long)y * w + x] = nt[y];
    }
}

/* nb[0 .. w * h) = smooth noise (white noise blurred to about sigma pixels), its mean 0.5 and
 * its spread sd, as the mock-ups' (a Gaussian blur, scaled 0..1) */
static void noise(Burn *b, int w, int h, float sigma, float sd)
{
    long n = (long)w * h, i;
    double m = 0, v = 0;
    float k;
    for (i = 0; i < n; i++)
        nb[i] = frand(b);
    if (sigma < 1.0f)
        box(nb, w, h, 1);
    else {
        int r = (int)((sqrtf(1 + 4 * sigma * sigma) - 1) / 2 + 0.5f), p;
        for (p = 0; p < 3; p++)
            box(nb, w, h, r < 1 ? 1 : r);
    }
    for (i = 0; i < n; i++) {
        m += nb[i];
        v += (double)nb[i] * nb[i];
    }
    m /= n;
    v = v / n - m * m;
    k = v > 1e-12 ? (float)(sd / sqrt(v)) : 0;
    for (i = 0; i < n; i++)
        nb[i] = clampf(0.5f + (nb[i] - (float)m) * k, 0, 1);
}

/* ------------------------------------------------------------------ the leftovers */

/* the plant's picture as drawn: the objects pack's frame, else the game's; 0 if not known */
static int plant_src(long ref, int *w, int *h)
{
    FxAnim *a = port_obj_set((ushort)((ulong)ref >> 16));
    long k = REFINDEX((ulong)ref);
    if (a && k < a->n && a->pic[k].pix) {
        *w = a->pic[k].w;
        *h = a->pic[k].h;
        return 1;
    }
    return port_plant_size(ref, w, h);
}

/* the effects pack's leftover of the plant whose picture is sw x sh: the 1x or 3x one of that
 * size, else the 3x one (scaled by the width); NULL: the pack has none (the first version's
 * trunk).  A picture with no pixels: nothing is left */
static const FxPic *leftover(long ref, int sw, int sh)
{
    ushort id = (ushort)((ulong)ref >> 16);
    long k = REFINDEX((ulong)ref);
    FxAnim *a1 = port_fx_left(0, id), *a3 = port_fx_left(1, id);
    const FxPic *p1 = a1 && k < a1->n && a1->pic[k].pix ? &a1->pic[k] : NULL,
                *p3 = a3 && k < a3->n && a3->pic[k].pix ? &a3->pic[k] : NULL;
    if (p1 && p1->w == sw && p1->h == sh)
        return p1;
    if (p3 && p3->w == sw && p3->h == sh)
        return p3;
    return p3 ? p3 : p1;
}

static int has_leftover(long ref)
{
    int w = 0, h = 0;
    plant_src(ref, &w, &h);
    return leftover(ref, w, h) != NULL;
}

/* a new stump at the plant's place (the oldest goes when there is no room) */
static int stump_add(const Burn *from, long ref, uchar type, fix w, fix x, fix y, fix z)
{
    int j, k = -1;
    for (j = 0; j < STUMPS && k < 0; j++)
        if (!stumps[j].used)
            k = j;
    if (k < 0) {
        for (j = 0; j < STUMPS; j++)
            if (stumps[j].burn < 0 && (k < 0 || stumps[j].t0 < stumps[k].t0))
                k = j;
        if (k < 0)
            return -1;
        stump_free(k);
    }
    stumps[k].used = 1;
    stumps[k].burn = from ? (int)(from - burns) : -1;
    stumps[k].t0 = game_time;
    stumps[k].ref = ref;
    stumps[k].type = type;
    stumps[k].w = w;
    stumps[k].x = x;
    stumps[k].y = y;
    stumps[k].z = z;
    stumps[k].seed = hash((unsigned)x * 0x9E3779B1u ^ (unsigned)y * 0x85EBCA77u ^ (unsigned)ref) | 1;
    stumps[k].stamp = stamp - 1;
    link_id(BURNS + k, x, y);
    return k;
}

/* ------------------------------------------------------------------ the list */

void port_burn_start(long slot, void *anim, long type, long ref, fix w, fix x, fix y, fix z, long ent, int broken)
{
    int i, k = -1, was_gone = 0;
    Burn *b;
    if (!port_burn || w <= 0)
        return;
    if (ent >= 0 && ent < GAME_MAX_ENTITIES) {
        was_gone = broken == 2 && gone[ent].on && gone[ent].x == x && gone[ent].y == y;
        /* a plant that breaks goes whole, as one without a broken picture: the game keeps it, as
         * its broken picture, but that is not drawn (the owner: no sticks of a pine left over
         * the pack's stump), and what is left is the pack's leftover of the plant */
        gone[ent].on = broken == 1;
        gone[ent].x = x;
        gone[ent].y = y;
    }
    for (i = 0; i < BURNS && k < 0; i++)
        if (!burns[i].used)
            k = i;
    if (k < 0) {                        /* no room: its fireball (and what is left) */
        if (!was_gone && has_leftover(ref))
            stump_add(NULL, ref, (uchar)type, w, x, y, z);
        return;
    }
    b = &burns[k];
    memset(b, 0, offsetof(Burn, part));
    b->used = 1;
    b->slot = slot;
    b->anim = anim;
    b->t0 = game_time;
    b->ref = ref;
    b->type = (uchar)type;
    b->w = w;
    b->x = x;
    b->y = y;
    b->z = z;
    b->seed = hash((unsigned)x * 0x9E3779B1u ^ (unsigned)y * 0x85EBCA77u ^ (unsigned)ref) | 1;
    b->rng = b->seed;
    b->mirror = (uchar)(b->seed >> 9 & 1);
    b->ev_ms = -1;
    b->smoke = broken == 2;
    b->unseen = was_gone;
    b->stump = -1;
    if (b->smoke) {                     /* the broken picture's leftover stands from the start */
        if (has_leftover(ref) && !was_gone)     /* (the plant's stands already) */
            stump_add(NULL, ref, (uchar)type, w, x, y, z);
    } else if (has_leftover(ref))
        b->stump = stump_add(b, ref, (uchar)type, w, x, y, z);
    link_id(k, x, y);
}

static long duration(const Burn *b)
{
    return b->smoke ? SMOKE_MS + 60 : (long)(NF_BLAST * 1.4f * 60) + LU_MS + 60;
}

/* drawing starts (viewdist_2A0EC8, the sharp view): the burns that are over go */
void port_burn_frame(void)
{
    int i;
    stamp++;
    for (i = 0; i < BURNS; i++) {
        Burn *b = &burns[i];
        if (b->used && (game_time < b->t0 || (long)(game_time - b->t0) > duration(b) || !port_burn))
            burn_free(b);
    }
    for (i = 0; i < STUMPS; i++)        /* an earlier time: another game */
        if (stumps[i].used && game_time < stumps[i].t0)
            stump_free(i);
}

int port_burn_hides(long slot, void *anim)
{
    int i;
    for (i = 0; i < BURNS; i++) {
        Burn *b = &burns[i];
        if (b->used && b->slot == slot && b->anim == anim && !b->failed)
            return b->smoke ? port_fx_pic(1175, 0, NULL) != NULL || port_fx_pic(1170, 0, NULL) != NULL :
                              port_fx_pic(FX_PLANTBLAST, 0, NULL) != NULL || port_fx_pic(1171, 0, NULL) != NULL;
    }
    return 0;
}

/* drawobj_29ACA8: e, a plant that broke, is left out (its broken picture) */
int port_burn_covers(const GameEntity *e)
{
    uintptr_t a = (uintptr_t)e, g = (uintptr_t)game_entities;
    long id;
    if (!port_burn || a < g || a >= g + sizeof game_entities)
        return 0;
    id = (long)((a - g) / sizeof *e);
    return gone[id].on && gone[id].x == e->x && gone[id].y == e->y;
}

/* a stump is drawn by itself when its burn does not draw it */
static int stump_shown(int j)
{
    const Stump *st = &stumps[j];
    const Burn *b = st->burn >= 0 ? &burns[st->burn] : NULL;
    return st->used && !(b && b->used && b->stump == j && !b->failed);
}

/* viewdist_2A13B8: the burns and stumps on the square (x, y) not in a list yet this drawing */
int port_burn_square(long x, long y, int *out, int max)
{
    int n = 0, id;
    if (!port_burn)
        return 0;
    for (id = head[(x * 31 + y) & 255] - 1; id >= 0 && n < max; id = nxt[id] - 1) {
        fix px, py;
        unsigned *st;
        if (id < BURNS) {
            Burn *b = &burns[id];
            if (!b->used || b->failed)
                continue;
            px = b->x;
            py = b->y;
            st = &b->stamp;
        } else {
            Stump *s = &stumps[id - BURNS];
            if (!stump_shown(id - BURNS))
                continue;
            px = s->x;
            py = s->y;
            st = &s->stamp;
        }
        if (*st != stamp && px >> 16 == x && py >> 16 == y) {
            *st = stamp;
            out[n++] = id;
        }
    }
    return n;
}

void port_burn_span(int i, long a, long b, int get, long *pa, long *pb)
{
    long *sa, *sb;
    if (i < 0 || i >= IDS)
        return;
    sa = i < BURNS ? &burns[i].span_a : &stumps[i - BURNS].span_a;
    sb = i < BURNS ? &burns[i].span_b : &stumps[i - BURNS].span_b;
    if (get) {
        *pa = *sa;
        *pb = *sb;
    } else {
        *sa = a;
        *sb = b;
    }
}

/* the trunk's colours (the run of the plant's pixels nearest its middle on its bottom rows, for
 * every 240 of its height 10, at 5 % .. 95 % of their brightness) and the crown's (at 15, 35,
 * 55, 75 and 90 %), as the plant's own colour numbers */
static void ramps(Burn *b)
{
    static const float tq[7] = { 0.05f, 0.2f, 0.35f, 0.5f, 0.65f, 0.8f, 0.95f }, cq[5] = { 0.15f, 0.35f, 0.55f, 0.75f, 0.9f };
    long cnt[2][256], tot[2] = { 0, 0 }, i;
    int ord[256], c, d, y, x, bot = -1, r, q;
    float lv[256], cx = b->gw / 2.0f;
    memset(cnt, 0, sizeof cnt);
    for (y = b->gh - 1; y >= 0 && bot < 0; y--)
        for (x = 0; x < b->gw && bot < 0; x++)
            if (b->pix[(long)y * b->gw + x])
                bot = y;
    for (y = bot - (int)(10 * b->s); bot >= 0 && y <= bot; y++) {
        const uchar *row = b->pix + (long)(y < 0 ? 0 : y) * b->gw;
        int best = -1, ba = 0, bb = 0;
        float bd = 1e9f;
        for (x = 0; x < b->gw;) {       /* the runs of the row: the one nearest the middle */
            int a;
            if (!row[x]) {
                x++;
                continue;
            }
            for (a = x; x < b->gw && row[x]; x++)
                ;
            if (fabsf((a + x - 1) / 2.0f - cx) < bd) {
                bd = fabsf((a + x - 1) / 2.0f - cx);
                best = 1;
                ba = a;
                bb = x;
            }
        }
        for (x = ba; best > 0 && x < bb; x++) {
            cnt[0][row[x]]++;
            tot[0]++;
        }
    }
    for (i = 0; i < (long)b->gw * b->gh; i++)
        if (b->pix[i] && !(b->aux[i] & 1)) {
            cnt[1][b->pix[i]]++;
            tot[1]++;
        }
    for (r = 0; r < 2; r++)             /* none: all the plant's pixels */
        if (!tot[r])
            for (i = 0; i < (long)b->gw * b->gh; i++)
                if (b->pix[i]) {
                    cnt[r][b->pix[i]]++;
                    tot[r]++;
                }
    for (c = 0; c < 256; c++) {
        ord[c] = c;
        lv[c] = lum(c);
    }
    for (c = 1; c < 256; c++)           /* the colours by brightness */
        for (d = c; d > 0 && lv[ord[d - 1]] > lv[ord[d]]; d--) {
            int t = ord[d];
            ord[d] = ord[d - 1];
            ord[d - 1] = t;
        }
    for (r = 0; r < 2; r++) {
        int nq = r ? 5 : 7;
        for (q = 0; q < nq; q++) {
            long at = (long)((r ? cq[q] : tq[q]) * (tot[r] - 1)), sum = 0;
            int pick = 0;
            for (c = 0; c < 256; c++) {
                sum += cnt[r][ord[c]];
                if (sum > at) {
                    pick = ord[c];
                    break;
                }
            }
            if (r)
                b->cramp[q] = (uchar)pick;
            else
                b->tramp[q] = (uchar)pick;
        }
    }
    b->pc_pal = 0;
}

/* the plant's picture into the arena, its noise, what is left of it and its masks */
/* the blast picture's scale: by the plant's area, so a low plant gets a small one */
static float blast_scale(const Burn *b)
{
    return sqrtf((float)b->gw * b->gh) * 1.6f / 136;
}

static int burn_ready(Burn *b)
{
    ulong id = (ulong)b->ref >> 16, k = REFINDEX((ulong)b->ref);
    const uchar *src = NULL;
    const FxPic *lp;
    int sw = 0, sh = 0, srow = 0, f, x, y, gw, gh;
    long n, i;
    FxAnim *a;
    uchar *p = NULL, *mem;
    float s, cx, col[1024];

    if (b->ready)
        return 1;
    if (b->failed)
        return 0;
    b->failed = 1;
    a = port_obj_set((ushort)id);
    if (a && (long)k < a->n && a->pic[k].pix) {
        src = a->pic[k].pix;
        sw = srow = a->pic[k].w;
        sh = a->pic[k].h;
    } else {
        grs_bitmap *bm;
        p = RefLock(b->ref);
        if (!p)
            return 0;
        bm = (grs_bitmap *)p;
        sw = bm->w;
        sh = bm->h;
        if (sw <= 0 || sh <= 0 || (long)sw * sh > GRID_PX * 4) {
            ResUnlock((Id)id);
            return 0;
        }
        if (bm->type == BMT_RSD8) {     /* into the scratch, a row of sw */
            uchar *t = (uchar *)nt;
            gr_rsd8_unpack(p + 0x1C, t);
            src = t;
            srow = sw;
        } else {
            src = p + 0x1C;
            srow = bm->row;
        }
    }
    for (f = 1; sw / f > COLS || sh / f > 512 || (long)(sw / f) * (sh / f) > GRID_PX; f++)
        ;
    gw = sw / f;
    gh = sh / f;
    n = (long)gw * gh;
    s = gh / 240.0f;
    b->L = s < 1.5f ? 1 : (int)(s + 0.5f);
    b->len = (size_t)n * 7 + 64;
    mem = arena_get(b, b->len);
    if (!mem || gw < 4 || gh < 4) {
        if (p)
            ResUnlock((Id)id);
        b->len = 0;
        return 0;
    }
    lp = leftover(b->ref, sw, sh);
    b->lmode = lp != NULL;
    b->mem = mem;
    b->gw = gw;
    b->gh = gh;
    b->s = s;
    b->pix = mem;
    b->n2 = b->pix + n;
    b->aux = b->n2 + n;
    if (b->lmode) {                     /* pix, n2, aux, left, out, the glowing top */
        b->left = b->aux + n;
        b->out = (ushort *)(((uintptr_t)(b->left + n) + 3) & ~(uintptr_t)3);
        b->hot = (int32_t *)(((uintptr_t)(b->out + n) + 3) & ~(uintptr_t)3);
        b->thr = NULL;
    } else {                            /* pix, n2, aux, thr, out */
        b->left = NULL;
        b->hot = NULL;
        b->thr = (ushort *)(((uintptr_t)(b->aux + n) + 1) & ~(uintptr_t)1);
        b->out = b->thr + n;
    }
    for (y = 0; y < gh; y++)
        for (x = 0; x < gw; x++)
            b->pix[(long)y * gw + x] = src[(long)y * f * srow + (long)x * f];
    if (p)
        ResUnlock((Id)id);

    cx = gw / 2.0f;
    memset(b->aux, 0, n);
    if (b->lmode) {
        /* the leftover on the plant's picture, by the width, standing on its foot */
        float r = (float)lp->w / gw;
        long nmax = (long)(n / 4) - 1;
        int lx[COLS], x0 = gw, x1 = -1, y0 = gh, y1 = -1, nx, ny;
        for (x = 0; x < gw; x++) {
            lx[x] = (int)((x + 0.5f) * r);
            b->ctop[x] = -1;
        }
        for (y = 0; y < gh; y++) {      /* (and each column's top) */
            long ly = (long)floorf(lp->h - (gh - (y + 0.5f)) * r);
            const uchar *row = ly >= 0 && ly < lp->h ? lp->pix + ly * lp->w : NULL;
            for (x = 0; x < gw; x++) {
                uchar c = row && lx[x] < lp->w ? row[lx[x]] : 0;
                i = (long)y * gw + x;
                b->left[i] = c;
                b->aux[i] = c ? 1 : 0;
                if (c && b->ctop[x] < 0) {
                    b->ctop[x] = (short)y;
                    x0 = x < x0 ? x : x0;
                    x1 = x > x1 ? x : x1;
                    y0 = y < y0 ? y : y0;
                    y1 = y;
                }
                if (c)
                    b->lb[3] = (short)(y + 1);
            }
        }
        b->lb[0] = (short)(x1 < 0 ? 0 : x0);
        b->lb[1] = (short)(x1 < 0 ? 0 : y0);
        b->lb[2] = (short)(x1 + 1);
        if (x1 < 0)
            nx = ny = 0;
        else {                          /* N2 only where the band can be: the highest top to 4.5 rows below the lowest */
            nx = x1 - x0 + 1;
            ny = (y1 + (int)ceilf(4.5f * s) + 1 < gh ? y1 + (int)ceilf(4.5f * s) + 1 : gh) - y0;
            noise(b, nx, ny, 1.2f * s, 0.128f);
            for (y = 0; y < ny; y++)
                for (x = 0; x < nx; x++)
                    b->n2[(long)(y0 + y) * gw + x0 + x] = (uchar)(nb[(long)y * nx + x] * 255 + 0.5f);
        }
        for (x = x0; x <= x1; x++)      /* its broken top glows: a band from each column's top */
            for (y = b->ctop[x]; b->ctop[x] >= 0 && y < gh; y++) {
                i = (long)y * gw + x;
                if (y - b->ctop[x] >= (3 + 1.5f * b->n2[i] / 255.0f) * s)
                    break;
                if (b->left[i] && b->n2[i] > 0.4f * 255 && b->nhot < nmax) {
                    b->hot[b->nhot++] = (int32_t)i;
                    b->aux[i] |= 2;
                }
            }
    } else {
        noise(b, gw, gh, 1.2f * s, 0.128f);     /* N2 */
        for (i = 0; i < n; i++)
            b->n2[i] = (uchar)(nb[i] * 255 + 0.5f);
        for (y = 0; y < gh; y++) {
            float yn = 1 - (float)y / gh;
            for (x = 0; x < gw; x++) {
                i = (long)y * gw + x;
                b->thr[i] = (ushort)((BAYER[(y / b->L) & 3][(x / b->L) & 3] / 16.0f * 0.35f + b->n2[i] / 255.0f * 0.35f + yn * 0.3f) * THR);
            }
        }
        noise(b, gw, 1, 2 * s, 0.14f);  /* the trunk: a jagged broken top */
        memcpy(col, nb, sizeof *col * (gw < 1024 ? gw : 1024));
        for (y = 0; y < gh; y++) {
            float yn = 1 - (float)y / gh;
            for (x = 0; x < gw; x++) {
                float n2 = b->n2[(long)y * gw + x] / 255.0f;
                if (fabsf(x - cx) < 0.075f * gw * (1 - 0.7f * yn) + 1.5f * s * (n2 - 0.5f) &&
                    yn < 0.40f + 0.16f * (col[x < 1024 ? x : 1023] - 0.5f) + 0.05f * (n2 - 0.5f))
                    b->aux[(long)y * gw + x] = 1 | (yn > 0.28f && n2 > 0.45f ? 2 : 0);
            }
        }
    }
    for (i = 0; i < n; i++)
        b->ncrown += b->pix[i] && !(b->aux[i] & 1);
    ramps(b);
    {   /* the light-up's distance from the blast's centre (the plant's bottom middle, 48 of the
         * blast's pixels up), 255 at the plant's farthest pixel: n2 from here on */
        float cy = gh - 48 * blast_scale(b), dmax = 1e-6f;
        for (y = 0; y < gh; y++)
            for (x = 0; x < gw; x++) {
                float dx = (x - cx) / gw, dy = (y - cy) / gh;
                i = (long)y * gw + x;
                nb[i] = sqrtf(dx * dx + dy * dy);
                if (b->pix[i] && nb[i] > dmax)
                    dmax = nb[i];
            }
        for (i = 0; i < n; i++)
            b->n2[i] = (uchar)(nb[i] < dmax ? nb[i] / dmax * 255 + 0.5f : 255);
    }
    b->failed = 0;
    b->ready = 1;
    return 1;
}

/* a stump's picture this drawing: its leftover (NULL: none) */
static const FxPic *stump_left(const Stump *st)
{
    int w = 0, h = 0;
    plant_src(st->ref, &w, &h);
    return leftover(st->ref, w, h);
}

/* viewdist.c: port_burn_ent the burn or stump i, for the view's drawing; 0 if it cannot be */
int port_burn_set(int i)
{
    fix x, y, z;
    uchar type;
    if (i < 0 || i >= IDS)
        return 0;
    if (i < BURNS) {
        Burn *b = &burns[i];
        if (!b->used || !burn_ready(b))
            return 0;
        x = b->x;
        y = b->y;
        z = b->z;
        type = b->type;
    } else {
        Stump *st = &stumps[i - BURNS];
        if (!stump_shown(i - BURNS))
            return 0;
        cur_left = stump_left(st);
        if (!cur_left)
            return 0;
        x = st->x;
        y = st->y;
        z = st->z;
        type = st->type;
    }
    memset(&port_burn_ent, 0, sizeof port_burn_ent);
    port_burn_ent.active = 1;
    port_burn_ent.cls = 0;
    port_burn_ent.type = type;
    port_burn_ent.x = x;
    port_burn_ent.y = y;
    port_burn_ent.z = z;
    cur = i;
    return 1;
}

int port_burn_model(long *ref, fix *scale)
{
    if (cur < 0)
        return 0;
    if (cur < BURNS) {
        Burn *b = &burns[cur];
        if (!b->used || !b->ready)
            return 0;
        *ref = b->ref;
        *scale = port_tree_scale(b->ref, b->x, b->y, b->w);
    } else {
        Stump *st = &stumps[cur - BURNS];
        if (!st->used)
            return 0;
        *ref = st->ref;
        *scale = port_tree_scale(st->ref, st->x, st->y, st->w);
    }
    return 1;
}

/* a burn's box: its width in plant widths, its height in the picture's pixels */
static void burn_box(const Burn *b, float *wf, float *hp)
{
    float sb = blast_scale(b);
    *wf = 144 * sb / b->gw > 3 ? 144 * sb / b->gw : 3;
    *hp = 172 * sb > b->gh * 1.8f ? 172 * sb : b->gh * 1.8f;
}

/* how far a burn draws below its plant's foot (the blast and its dust), in the plant's pixels */
static float burn_below(const Burn *b)
{
    return 6 * blast_scale(b);
}

static float box_below;                 /* port_burn_box's: below the foot, in its rows */
static short box_h;

/* drawobj_29CCE0: the box on the foot: a burn's (burn_box), a stump's the plant's */
void port_burn_box(uchar *hdr, fix *scale)
{
    grs_bitmap *bm = (grs_bitmap *)hdr;
    memset(hdr, 0, 0x1C);
    if (cur >= BURNS) {
        int w = 0, h = 0;
        const Stump *st = &stumps[cur - BURNS];
        if (cur_left) {
            w = cur_left->w;
            h = cur_left->h;
        } else
            plant_src(st->ref, &w, &h);
        bm->w = (short)(w > 0 ? w : 1);
        bm->h = (short)(h > 0 ? h : 1);
        box_below = 0;
    } else {
        Burn *b = &burns[cur < 0 ? 0 : cur];
        float wf, hp;
        burn_box(b, &wf, &hp);
        bm->w = (short)(b->gw * wf);
        bm->h = (short)hp;
        *scale = (fix)(*scale * wf);
        box_below = burn_below(b);
    }
    box_h = bm->h;
}

/* drawobj_29CCE0: the box port_burn_box gave, h of the screen's rows high, goes on this far below
 * the foot, so the ground in front hides what is drawn there too (P72: what was drawn below the
 * foot, behind a rise, showed through it) */
long port_burn_below(long h)
{
    return box_h > 0 ? (long)ceilf(h * box_below / box_h) : 0;
}

/* ------------------------------------------------------------------ the motion */

static Part *part_add(Burn *b, float x, float y, float vx, float vy, float life, uchar c, int glow, float size, float g)
{
    Part *q;
    if (b->np >= PARTS)
        return NULL;
    q = &b->part[b->np++];
    q->x = x;
    q->y = y;
    q->vx = vx;
    q->vy = vy;
    q->life = life;
    q->c = c;
    q->glow = (uchar)glow;
    q->size = size;
    q->g = g;
    return q;
}

static void flake_add(Burn *b, float x, float y, float vx, float vy, float life, int c)
{
    Flake *q;
    if (b->nfl >= FLAKES)
        return;
    q = &b->fl[b->nfl++];
    q->x = x;
    q->y = y;
    q->vx = vx;
    q->vy = vy;
    q->life = life;
    q->c = (uchar)c;
    q->ph = frand(b) * 6.2831853f;
    q->fq = 0.25f + frand(b) * 0.2f;
    q->st = (uchar)irand(b, 0, 4);
    q->landed = 0;
}

static int crown(const Burn *b, long i)
{
    return b->pix[i] && !(b->aux[i] & 1);
}

/* a random pixel of the crown, by trying; 0 none */
static int crown_px(Burn *b, int *x, int *y)
{
    int k;
    for (k = 0; k < 64; k++) {
        *x = irand(b, 0, b->gw);
        *y = irand(b, 0, b->gh);
        if (crown(b, (long)*y * b->gw + *x))
            return 1;
    }
    return 0;
}

/* the pieces' rows: the debris sheets' cells of 24 across 8 turns */
static int sheet_rows(int kind)
{
    FxAnim *a = port_fx_loaded() ? port_fx_anim(FX_PIECES) : NULL;
    return a && a->pic[kind].w >= 8 * 24 ? a->pic[kind].h / 24 : 0;
}

/* the crown breaks: shards of its own pixels, a spray, burnt leaves thrown high and pieces of
 * wood, bark and leaf; the fewer, the smaller the crown (its pixels on a plant 240 high) */
static void shatter(Burn *b)
{
    float s = b->s, cxp = b->gw / 2.0f, cyp = b->gh * 0.55f, cn = b->ncrown / (s * s);
    int nsh = (int)clampf(cn / 60, 12, SHARDS), nsp = (int)clampf(cn / 25, 40, 150), nlv = (int)clampf(cn / 150, 10, 35),
        npc = (int)clampf(cn / 120, 8, PIECES), rows[3], all, t, j;
    if (!b->ncrown)
        return;
    for (t = 0; t < nsh * 2 && b->ns < nsh; t++) {
        Shard *c = &b->sh[b->ns];
        float sz = (float)irand(b, 5, 12), p[3][2], area;
        int x0 = 0, y0 = 0, ok, x, y, n;
        if (!crown_px(b, &x0, &y0))
            continue;
        n = (int)(sz * s + 0.5f);
        n = n < 2 ? 2 : n;
        p[0][0] = frand(b) * sz * 0.4f;
        p[0][1] = frand(b) * sz;
        p[1][0] = sz * 0.6f + frand(b) * sz * 0.4f;
        p[1][1] = frand(b) * sz * 0.5f;
        p[2][0] = sz * 0.4f + frand(b) * sz * 0.6f;
        p[2][1] = sz * 0.6f + frand(b) * sz * 0.4f;
        for (j = 0; j < 3; j++) {
            p[j][0] *= n / sz;
            p[j][1] *= n / sz;
        }
        area = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[1][1] - p[0][1]) * (p[2][0] - p[0][0]);
        if (fabsf(area) < 0.5f)
            continue;
        for (j = 0; j < 3; j++) {       /* edge j: from p[j] to p[j + 1], the inside >= 0 */
            const float *a = p[j], *bb = p[(j + 1) % 3];
            float ex = bb[0] - a[0], ey = bb[1] - a[1], sg = area > 0 ? 1.0f : -1.0f;
            c->e[j][0] = -ey * sg;
            c->e[j][1] = ex * sg;
            c->e[j][2] = (ey * a[0] - ex * a[1]) * sg;
        }
        for (ok = 0, y = 0; y < n && !ok; y++)
            for (x = 0; x < n && !ok; x++) {
                float fx = x + 0.5f, fy = y + 0.5f;
                if (x0 + x < b->gw && y0 + y < b->gh && b->pix[(long)(y0 + y) * b->gw + x0 + x] &&
                    c->e[0][0] * fx + c->e[0][1] * fy + c->e[0][2] >= 0 && c->e[1][0] * fx + c->e[1][1] * fy + c->e[1][2] >= 0 &&
                    c->e[2][0] * fx + c->e[2][1] * fy + c->e[2][2] >= 0)
                    ok = 1;
            }
        if (!ok)
            continue;
        c->bx = (short)x0;
        c->by = (short)y0;
        c->n = (short)n;
        c->x = (float)x0;
        c->y = (float)y0;
        c->vx = (x0 - cxp) * 0.07f + nrand(b) * 0.8f * s;
        c->vy = -1.5f * s - frand(b) * 3 * s + (y0 - cyp) * 0.02f;
        c->life = (float)irand(b, 18, 34);
        c->burn = frand(b) > 0.35f;
        c->ang = frand(b) * 6.2831853f;
        c->spin = nrand(b) * 25 * 0.0174533f;
        b->ns++;
    }
    for (t = 0; t < nsp; t++) {         /* the spray */
        int x, y;
        long i;
        if (!crown_px(b, &x, &y))
            continue;
        i = (long)y * b->gw + x;
        if (frand(b) < 0.6f)
            part_add(b, (float)x, (float)y, (x - cxp) * 0.09f + nrand(b) * 1.2f * s, (y - cyp) * 0.05f - frand(b) * 2.5f * s,
                     (float)irand(b, 8, 22), mx.dark[b->pix[i]], 0, s, 0.2f * s);
        else
            part_add(b, (float)x, (float)y, (x - cxp) * 0.09f + nrand(b) * 1.2f * s, (y - cyp) * 0.05f - frand(b) * 2.5f * s,
                     (float)irand(b, 8, 22), fire9[irand(b, 3, 7)], 1, s, 0.2f * s);
    }
    for (t = 0; t < nlv; t++) {         /* burnt leaves thrown high */
        int x, y;
        if (crown_px(b, &x, &y))
            flake_add(b, (float)x, (float)y, (x - cxp) * 0.05f + nrand(b) * 0.6f * s, (-2.5f - frand(b) * 2.5f) * s,
                      (float)irand(b, 35, 60), irand(b, 0, 8));
    }
    for (j = 0, all = 0; j < 3; j++)
        all += rows[j] = sheet_rows(j);
    for (t = 0; all && t < npc; t++) {  /* the pieces: a row of a sheet, from the crown */
        Piece *q = &b->pc[b->npc];
        int x, y, r = irand(b, 0, all), kind = 0;
        if (!crown_px(b, &x, &y))
            continue;
        while (r >= rows[kind]) {
            r -= rows[kind];
            kind++;
        }
        q->kind = (uchar)kind;
        q->row = (uchar)r;
        q->x = (float)x;
        q->y = (float)y;
        q->vx = (x - cxp) * 0.08f + nrand(b) * 1.0f * s;
        q->vy = (-2 - frand(b) * 3.5f) * s + (y - cyp) * 0.02f;
        q->a = frand(b) * 8;
        q->spin = nrand(b) * 0.6f;
        q->life = (float)irand(b, 22, 40);
        q->hot = frand(b) < (kind < 2 ? 0.25f : 0.1f);
        b->npc++;
    }
}

/* the fade's limit at frame k: pixels whose threshold is under it are gone (not lmode) */
static float fade_limit(const Burn *b, float k)
{
    float t = clampf(k / NF_BLAST, 0, 1), t0 = 0.6f, t1 = 0.97f;
    (void)b;
    return t <= t0 ? -1 : clampf((t - t0) / (t1 - t0), 0, 1) * 1.1f * THR;
}

/* frame k begins: what it makes */
static void frame_start(Burn *b, int k)
{
    float s = b->s;
    int i;
    if (k == 2)
        shatter(b);
    for (i = 0; k >= 2 && k <= 5 && i < 7 && b->nspk < SPARKS; i++) {     /* sparks streaking up */
        Spark *q = &b->spk[b->nspk++];
        float a = -3.1415927f * (0.92f - 0.84f * frand(b)), v = (3 + frand(b) * 3.5f) * s;
        q->x = b->gw / 2.0f + nrand(b) * 0.12f * b->gw;
        q->y = b->gh * 0.45f + nrand(b) * 0.1f * b->gh;
        q->vx = cosf(a) * v;
        q->vy = sinf(a) * v;
        q->life = (float)irand(b, 5, 13);
    }
    if (b->lmode && k >= 2 && k < 24 && frand(b) < 0.7f) {     /* the hot top sheds a little ash */
        int x = 0, t, ok = 0;
        for (t = 0; t < 32 && !ok; t++) {
            x = irand(b, 0, b->gw < COLS ? b->gw : COLS);
            ok = b->ctop[x] >= 0;
        }
        if (ok) {
            int c = frand(b) < 0.8f ? irand(b, 0, 8) : 8;
            flake_add(b, (float)x, b->ctop[x] + frand(b) * 3 * s, (0.2f + nrand(b) * 0.3f) * s, -0.3f * s,
                      (float)irand(b, 20, 36), c);
        }
    }
    for (i = 0; i < b->ns; i++) {       /* the burning shards' sparks */
        Shard *c = &b->sh[i];
        if (c->life > 0 && c->burn && frand(b) < 0.35f)
            part_add(b, c->x + 2 * s, c->y, nrand(b) * 0.3f * s, -0.8f * s, 8, fire9[irand(b, 4, 8)], 1, s, -0.02f * s);
    }
    if (!b->lmode) {                    /* the first version's trunk falls apart */
        float lim = fade_limit(b, (float)k);
        if (lim > b->lim_prev) {        /* the pixels that fell apart since: some are ash */
            long n = (long)b->gw * b->gh, j, lost = 0;
            float p = 0.18f / (s * s), cxp = b->gw / 2.0f;
            for (j = 0; j < n; j++) {
                if (!b->pix[j] || b->thr[j] < b->lim_prev || b->thr[j] >= lim || !(b->aux[j] & 1))
                    continue;
                lost++;
                if (frand(b) < p) {     /* of the 18%: 6% flakes, 0.8% dim embers (of all) */
                    float rr = frand(b), side = ((j % b->gw) - cxp) / cxp;
                    int hot = rr < 0.008f / 0.18f;
                    part_add(b, (float)(j % b->gw), (float)(j / b->gw), nrand(b) * 0.7f * s + (0.25f + 0.6f * side) * s,
                             (0.1f + nrand(b) * 0.4f) * s, (float)irand(b, 16, 40),
                             hot ? fire9[irand(b, 2, 4)] : grey_c[irand(b, 0, 8)], hot, rr < 0.06f / 0.18f ? 2 * s : s,
                             (0.04f + frand(b) * 0.12f) * s);
                }
            }
            b->lost += lost / (s * s);
            b->lim_prev = lim;
        }
    }
}

static void step(Burn *b, float dt)
{
    /* the per-frame dampings to the power of dt (a quarter) */
    static const float FL_VX = 0.98984606f, SP_VX = 0.97937316f, SP_VY = 0.98726235f;
    float s = b->s, ground = b->gh - 2 * s, low = b->gh + 10 * s;
    int i, n = 0;
    for (i = 0; i < b->np; i++) {
        Part *q = &b->part[i];
        q->x += q->vx * dt;
        q->y += q->vy * dt;
        q->vy += q->g * dt;
        q->life -= dt;
        if (q->life > 0 && q->y < low && q->x > -2.0f * b->gw && q->x < 3.0f * b->gw)
            b->part[n++] = *q;
    }
    b->np = n;
    for (i = 0; i < b->ns; i++) {
        Shard *c = &b->sh[i];
        if (c->life <= 0)
            continue;
        c->x += c->vx * dt;
        c->y += c->vy * dt;
        c->vy += 0.25f * s * dt;
        c->life -= dt;
        c->ang += c->spin * dt;
        if (c->y > ground) {
            c->y = ground;
            c->vy *= -0.25f;
            c->vx *= 0.5f;
            c->spin *= 0.3f;
        }
    }
    for (i = 0; i < b->npc; i++) {
        Piece *c = &b->pc[i];
        if (c->life <= 0)
            continue;
        c->x += c->vx * dt;
        c->y += c->vy * dt;
        c->vy += 0.25f * s * dt;
        c->a += c->spin * dt;
        c->life -= dt;
        if (c->y > ground) {
            c->y = ground;
            c->vy *= -0.2f;
            c->vx *= 0.4f;
            c->spin *= 0.3f;
        }
    }
    for (i = 0, n = 0; i < b->nfl; i++) {       /* flakes: light, they flutter and fall slowly */
        Flake *q = &b->fl[i];
        q->life -= dt;
        if (q->life <= 0)
            continue;
        if (!q->landed) {
            float top = (0.55f + 0.25f * sinf(q->ph * 0.5f)) * s;
            q->ph += q->fq * dt;
            q->vy = q->vy + 0.05f * s * dt < top ? q->vy + 0.05f * s * dt : top;
            q->x += (q->vx + 0.5f * s * sinf(q->ph)) * dt;
            q->y += q->vy * dt;
            q->vx *= FL_VX;
            if (q->y > b->gh - s) {     /* on the ground: it stays a moment */
                q->y = b->gh - s;
                q->landed = 1;
                q->life = q->life < 6 ? q->life : 6;
            }
        }
        b->fl[n++] = *q;
    }
    b->nfl = n;
    for (i = 0, n = 0; i < b->nspk; i++) {
        Spark *q = &b->spk[i];
        q->x += q->vx * dt;
        q->y += q->vy * dt;
        q->vy += 0.3f * s * dt;
        q->vx *= SP_VX;
        q->vy *= SP_VY;
        q->life -= dt;
        if (q->life > 0)
            b->spk[n++] = *q;
    }
    b->nspk = n;
}

/* the motion up to ms after the death (quarter frames of 15 ms) */
static void advance(Burn *b, long ms)
{
    long target = ms / 15;
    while (b->steps < target) {
        if (!(b->steps & 3))
            frame_start(b, (int)(b->steps >> 2));
        step(b, 0.25f);
        b->steps++;
    }
}

/* ------------------------------------------------------------------ the picture */

/* the light-up before frame 2 (kf from -1): r = 1/3 .. 1 over its 3 frames, each pixel mixed
 * towards ffc878 by fm = clamp(1.6 r - 0.6 d, 0, 1)^1.5 * 0.6 (d its distance, n2) and lit that
 * share (fm / 0.6) of the way from the plant's light to full light; made again when r moves on
 * by 1/32 */
static void light_up(Burn *b, float kf)
{
    ushort lv[256];
    int rq = (int)(clampf((kf + 2) / 3, 0, 1) * 32), d;
    float r = rq / 32.0f;
    long n = (long)b->gw * b->gh, i;
    if (b->ev_stage == 1 && b->ev_tip == rq)
        return;
    for (d = 0; d < 256; d++) {
        float f = clampf(r * 1.6f - d / 255.0f * 0.6f, 0, 1);
        int j;
        f *= sqrtf(f);                  /* fm / 0.6 */
        j = (int)(f * 15 + 0.5f);
        lv[d] = (ushort)((int)(f * (LU_N - 1) + 0.5f) | (j >= 15 ? OUT_GLOW : j << OUT_LIFT));
    }
    for (i = 0; i < n; i++) {
        uchar c = b->pix[i];
        ushort l = lv[b->n2[i]];
        b->out[i] = c ? mx.lu[l & 0xFF][c] | (l & 0xFF00) : 0;
    }
    b->ev_stage = 1;
    b->ev_tip = rq;
}

/* out: each pixel's colour at frame kf (0 none; OUT_GLOW through the effect's table).  With a
 * leftover only what changes is made again: the light-up as it grows, the leftover once, then its
 * glowing top while it cools */
static void evaluate(Burn *b, float kf)
{
    int q = (int)(clampf((kf - 2) / 14, 0, 1) * 7 + 0.5f),
        tip = (int)(clampf(0.7f - 0.7f * clampf((kf - 2) / 14, 0, 1), 0, 1) * 32 + 0.5f);
    long n = (long)b->gw * b->gh, i;
    if (kf < 2) {
        light_up(b, kf);
        return;
    }
    if (b->lmode) {
        tip = (int)(clampf(0.8f - 0.85f * clampf((kf - 2) / 14, 0, 1), 0, 1) * 32 + 0.5f);
        if (b->ev_stage != 2) {
            for (i = 0; i < n; i++)
                b->out[i] = b->left[i];
            b->ev_stage = 2;
            b->ev_tip = -1;
        }
        if (tip != b->ev_tip) {
            for (i = 0; i < b->nhot; i++) {
                long j = b->hot[i];
                b->out[j] = tip > 0 ? fire_c[tip] | (tip > 2 ? OUT_GLOW : 0) : b->left[j];
            }
            b->ev_tip = tip;
        }
        return;
    }
    {
        float lim = fade_limit(b, kf);
        for (i = 0; i < n; i++) {
            uchar c = b->pix[i];
            if (!c)
                b->out[i] = 0;
            else if (!(b->aux[i] & 1) || b->thr[i] < lim)
                b->out[i] = 0;
            else if (b->aux[i] & 2)
                b->out[i] = fire_c[tip] | (tip > 2 ? OUT_GLOW : 0);
            else
                b->out[i] = mx.trunk[q][c];
        }
    }
}

/* ------------------------------------------------------------------ drawing */

typedef struct {
    float x0, y0, kx, ky;               /* the screen point of the picture's (0, 0); screen pixels per its pixel */
    long xa, xb, ya, yb;                /* the clip */
    const uchar *lit, *glow;
    const uchar *lift[16];              /* the light-up's: j fifteenths of the way from lit to glow */
} View;

static void fill(const View *v, float gx0, float gy0, float gx1, float gy1, uchar c, long a)
{
    grs_canvas *cv = grd_canvas;
    long xa = (long)floorf(v->x0 + gx0 * v->kx), ya = (long)floorf(v->y0 + gy0 * v->ky),
         xb = (long)floorf(v->x0 + gx1 * v->kx), yb = (long)floorf(v->y0 + gy1 * v->ky), x, y;
    if (xb <= xa)
        xb = xa + 1;
    if (yb <= ya)
        yb = ya + 1;
    xa = xa < v->xa ? v->xa : xa;
    ya = ya < v->ya ? v->ya : ya;
    xb = xb > v->xb ? v->xb : xb;
    yb = yb > v->yb ? v->yb : yb;
    if (xa >= xb)
        return;
    for (y = ya; y < yb; y++) {
        uchar *d = cv->bm.bits + y * cv->bm.row;
        if (a >= 240)
            memset(d + xa, c, xb - xa);
        else
            for (x = xa; x < xb; x++)
                d[x] = port_fx_mix(d[x], c, a);
    }
}

/* a pack picture over the picture's box (gx0, gy0) .. +gw x gh, by op (0..256) */
static void pic(const View *v, const FxPic *p, float gx0, float gy0, float gw, float gh, const uchar *clut, float op, int mirror)
{
    long x0 = (long)floorf(v->x0 + gx0 * v->kx), y0 = (long)floorf(v->y0 + gy0 * v->ky),
         w = (long)(gw * v->kx + 0.5f), h = (long)(gh * v->ky + 0.5f);
    if (p && op > 0.01f)
        port_fx_draw_m(p, x0, y0, w, h, clut, (long)(op * 256), mirror);
}

static void smoke(const View *v, const Burn *b, float t, float start, float amt, float dx, float sw, float sh, int seed, float kf)
{
    int n;
    const FxPic *p;
    float a, s = b->s;
    if (t < start)
        return;
    a = clampf((t - start) / 0.15f, 0, 1) * (1 - clampf((t - 0.82f) / 0.18f, 0, 1)) * amt;
    p = port_fx_pic(1175, 0, &n);
    if (!p || a <= 0)
        return;
    p = port_fx_pic(1175, ((int)(kf * 0.8f) + seed) % n, NULL);
    sw *= s;
    sh *= s;
    pic(v, p, b->gw / 2.0f - sw / 2 + dx * s + 14 * s * t, b->gh * 0.35f - sh * 0.6f - 50 * s * t, sw, sh, v->lit, a, 0);
}

static void pile(const View *v, const Burn *b, float T, float amount)
{
    float s = b->s, n = b->lost, hg, wd, a;
    int x, y, nw, nh;
    if (n <= 0)
        return;
    hg = 2 + n / 700;
    hg = hg > 10 ? 10 : hg;
    wd = 30 + n / 40;
    wd = wd > b->gw / s * 0.75f ? b->gw / s * 0.75f : wd;
    a = T < 1.12f ? 1 : clampf(1 - (T - 1.12f) / 0.25f, 0, 1);
    if (a * amount <= 0.02f)
        return;
    nw = (int)wd;
    nh = (int)hg + 2;
    for (y = 0; y < nh; y++)
        for (x = 0; x < nw; x++) {
            float dx = (x - wd / 2) / (wd / 2), dy = (y - hg) / hg, gx, gy;
            unsigned h = hash(b->seed ^ (unsigned)(y * 977 + x * 131));
            uchar c;
            if (dx * dx + dy * dy >= 1 || (h & 0xFF) < 38)
                continue;
            c = (h >> 8 & 0xFF) < 10 && T < 0.95f ? v->glow[fire9[4]] : v->lit[pile_c[(h >> 16) & 3]];
            gx = b->gw / 2.0f - (int)(wd / 2) * s + x * s;
            gy = b->gh - ((int)hg + 1) * s + y * s;
            fill(v, gx, gy, gx + s, gy + s, c, (long)(a * amount * 255));
        }
}

/* the picture's colours (out, or pix of a palette's numbers: a stump's leftover) over its box */
static void plant(const View *v, int gw, int gh, const ushort *out, const uchar *pix, const short *box)
{
    static long sx[4096];
    grs_canvas *cv = grd_canvas;
    long w = (long)(gw * v->kx + 0.5f), h = (long)(gh * v->ky + 0.5f), x0 = (long)floorf(v->x0), y0 = (long)floorf(v->y0),
         xa = x0 > v->xa ? x0 : v->xa, xb = x0 + w < v->xb ? x0 + w : v->xb, ya = y0 > v->ya ? y0 : v->ya,
         yb = y0 + h < v->yb ? y0 + h : v->yb, x, y;
    if (box && w > 0 && h > 0) {        /* only the box of its pixels (the picture's x0, y0, x1, y1) */
        long bxa = x0 + (long)box[0] * w / gw, bxb = x0 + ((long)box[2] * w + gw - 1) / gw + 1,
             bya = y0 + (long)box[1] * h / gh, byb = y0 + ((long)box[3] * h + gh - 1) / gh + 1;
        xa = bxa > xa ? bxa : xa;
        xb = bxb < xb ? bxb : xb;
        ya = bya > ya ? bya : ya;
        yb = byb < yb ? byb : yb;
    }
    if (xa >= xb || ya >= yb || xb - xa > 4096 || w <= 0 || h <= 0)
        return;
    for (x = xa; x < xb; x++)
        sx[x - xa] = (2 * (x - x0) + 1) * gw / (2 * w);
    for (y = ya; y < yb; y++) {
        long row = (2 * (y - y0) + 1) * gh / (2 * h) * gw;
        uchar *d = cv->bm.bits + y * cv->bm.row;
        if (out) {
            const ushort *s = out + row;
            for (x = xa; x < xb; x++) {
                ushort o = s[sx[x - xa]];
                if (o)
                    d[x] = (o & OUT_GLOW ? v->glow : v->lift[o >> OUT_LIFT])[o & 0xFF];
            }
        } else {
            const uchar *s = pix + row;
            for (x = xa; x < xb; x++) {
                uchar o = s[sx[x - xa]];
                if (o)
                    d[x] = v->lit[o];
            }
        }
    }
}

/* x's limits on a row where p x + q >= 0 */
static void span_cut(float p, float q, float *xl, float *xr)
{
    if (p > 1e-6f || p < -1e-6f) {
        float r = -q / p;
        if (p > 0)
            *xl = r > *xl ? r : *xl;
        else
            *xr = r < *xr ? r : *xr;
    } else if (q < 0)
        *xr = *xl - 1;
}

/* each shard's block of the picture turned by its angle and cut by its triangle: on each row
 * only the span inside it is visited, its picture position stepped in 16.16 */
static void shards(const View *v, const Burn *b)
{
    grs_canvas *cv = grd_canvas;
    int i;
    for (i = 0; i < b->ns; i++) {
        const Shard *c = &b->sh[i];
        float hn, cx, cy, cs, sn, r, e, ax, bx, nx, ny;
        long xa, xb, ya, yb, x, y, alpha;
        int q, k;
        if (c->life <= 0)
            continue;
        hn = c->n / 2.0f;
        cx = c->x + hn;
        cy = c->y + hn;
        cs = cosf(c->ang);
        sn = sinf(c->ang);
        r = hn * 1.42f;
        xa = (long)floorf(v->x0 + (cx - r) * v->kx);
        xb = (long)ceilf(v->x0 + (cx + r) * v->kx);
        ya = (long)floorf(v->y0 + (cy - r) * v->ky);
        yb = (long)ceilf(v->y0 + (cy + r) * v->ky);
        xa = xa < v->xa ? v->xa : xa;
        ya = ya < v->ya ? v->ya : ya;
        xb = xb > v->xb ? v->xb : xb;
        yb = yb > v->yb ? v->yb : yb;
        e = clampf(c->life / 30, 0, 1);
        q = (int)(e * 7 + 0.5f);
        alpha = c->life < 5 ? (long)(c->life / 5 * 255) : 255;
        ax = 1 / v->kx;                 /* the picture x across the screen: gx = ax x + bx */
        bx = (0.5f - v->x0) / v->kx - cx;
        nx = (float)(c->n < b->gw - c->bx ? c->n : b->gw - c->bx);
        ny = (float)(c->n < b->gh - c->by ? c->n : b->gh - c->by);
        for (y = ya; y < yb; y++) {
            uchar *d = cv->bm.bits + y * cv->bm.row;
            float gy = (y + 0.5f - v->y0) / v->ky - cy,
                  pa = cs * ax, pb = cs * bx + gy * sn + hn,      /* lx = pa x + pb */
                  qa = -sn * ax, qb = -sn * bx + gy * cs + hn,    /* ly = qa x + qb */
                  xl = (float)xa, xr = (float)(xb - 1);
            long lx, ly, dlx, dly, xs, xe;
            span_cut(pa, pb, &xl, &xr);
            span_cut(-pa, nx - pb, &xl, &xr);
            span_cut(qa, qb, &xl, &xr);
            span_cut(-qa, ny - qb, &xl, &xr);
            for (k = 0; k < 3; k++)
                span_cut(c->e[k][0] * pa + c->e[k][1] * qa, c->e[k][0] * pb + c->e[k][1] * qb + c->e[k][2], &xl, &xr);
            if (xl > xr)
                continue;
            xs = (long)ceilf(xl);
            xe = (long)floorf(xr);
            xs = xs < xa ? xa : xs;
            xe = xe > xb - 1 ? xb - 1 : xe;
            if (xs > xe)
                continue;
            lx = (long)((pa * xs + pb) * 65536);
            ly = (long)((qa * xs + qb) * 65536);
            dlx = (long)(pa * 65536);
            dly = (long)(qa * 65536);
            for (x = xs; x <= xe; x++, lx += dlx, ly += dly) {
                long ix = lx >> 16, iy = ly >> 16;
                uchar s0, col;
                if (ix < 0 || iy < 0 || ix >= nx || iy >= ny)
                    continue;
                s0 = b->pix[(long)(c->by + iy) * b->gw + c->bx + ix];
                if (!s0)
                    continue;
                col = c->burn ? v->glow[mx.shot[q][s0]] : v->lit[mx.schar[s0]];
                d[x] = alpha >= 240 ? col : port_fx_mix(d[x], col, alpha);
            }
        }
    }
}

/* the pieces: each its sheet's cell (24 x 24 at the 3x scale, a picture 480 high's) of its turn,
 * its grey through its colours; fading over its last 6 frames */
static void pieces(const View *v, const Burn *b)
{
    grs_canvas *cv = grd_canvas;
    FxAnim *a = port_fx_loaded() ? port_fx_anim(FX_PIECES) : NULL;
    float cs = 24 * b->gh / 480.0f;
    int i;
    if (!a)
        return;
    for (i = 0; i < b->npc; i++) {
        const Piece *c = &b->pc[i];
        const FxPic *p = &a->pic[c->kind];
        int rot, ramp = c->kind == 2, q = (int)(clampf(c->life / 30, 0, 1) * 7 + 0.5f), nrot = p->w / 24;
        long x0, y0, w, h, xa, xb, ya, yb, x, y, alpha;
        const uchar *table = c->hot ? NULL : b->pc_cold[ramp];
        if (c->life <= 0 || !p->rgba || (c->row + 1) * 24 > p->h || nrot < 1)
            continue;
        rot = ((int)c->a % 8 + 8) % 8 % (nrot < 8 ? nrot : 8);     /* (a sheet of fewer turns: those) */
        x0 = (long)floorf(v->x0 + (c->x - cs / 2) * v->kx);
        y0 = (long)floorf(v->y0 + (c->y - cs / 2) * v->ky);
        w = (long)(cs * v->kx + 0.5f);
        h = (long)(cs * v->ky + 0.5f);
        w = w < 1 ? 1 : w;
        h = h < 1 ? 1 : h;
        xa = x0 > v->xa ? x0 : v->xa;
        xb = x0 + w < v->xb ? x0 + w : v->xb;
        ya = y0 > v->ya ? y0 : v->ya;
        yb = y0 + h < v->yb ? y0 + h : v->yb;
        alpha = c->life < 6 ? (long)(c->life / 6 * 255) : 255;
        for (y = ya; y < yb; y++) {
            const uchar *s = p->rgba + ((long)c->row * 24 + (2 * (y - y0) + 1) * 24 / (2 * h)) * p->w * 4 + (long)rot * 24 * 4;
            uchar *d = cv->bm.bits + y * cv->bm.row;
            for (x = xa; x < xb; x++) {
                const uchar *t = s + (2 * (x - x0) + 1) * 24 / (2 * w) * 4;
                uchar col;
                if (t[3] < 128)
                    continue;
                col = table ? v->lit[table[t[0] >> 4]] : v->glow[b->pc_hot[ramp][t[0] >> 4][q]];
                d[x] = alpha >= 240 ? col : port_fx_mix(d[x], col, alpha);
            }
        }
    }
}

/* the flakes: four shapes in turn as they flutter, a little lighter and darker */
static void flakes(const View *v, const Burn *b)
{
    float s = b->s;
    int i, j;
    for (i = 0; i < b->nfl; i++) {
        const Flake *q = &b->fl[i];
        int st = ((int)(q->ph / 1.5f) + q->st) % 4;
        uchar c = (q->c == 8 ? v->glow : v->lit)[flk_c[q->c][st == 0 ? 1 : st == 2 ? 2 : 0]];
        float x = floorf(q->x / s) * s, y = floorf(q->y / s) * s;
        for (j = 0; j < 3 && FLK[st][j][0] != 99; j++) {
            float px = x + FLK[st][j][1] * s, py = y + FLK[st][j][0] * s;
            fill(v, px, py, px + s, py + s, c, 255);
        }
    }
}

/* the sparks: a streak back along the motion, its head white-hot, cooler down its tail */
static void sparks(const View *v, const Burn *b)
{
    float s = b->s;
    int i, j;
    for (i = 0; i < b->nspk; i++) {
        const Spark *q = &b->spk[i];
        float sp = sqrtf(q->vx * q->vx + q->vy * q->vy), L = sp < 3 * s ? sp : 3 * s, e = q->life / 12, ux, uy;
        int n = (int)(L / s) + 1;
        if (sp < 1e-4f)
            continue;
        n = n < 2 ? 2 : n;
        ux = q->vx / sp;
        uy = q->vy / sp;
        for (j = 0; j < n; j++) {
            float f = (float)j / (n - 1), x = q->x - ux * L * f, y = q->y - uy * L * f;
            uchar c = j ? fire_c[(int)(clampf(0.95f * e + 0.3f - 0.45f * f, 0, 1) * 32 + 0.5f)] : spark_head;
            fill(v, x, y, x + s, y + s, v->glow[c], 255);
        }
    }
}

/* the view of a picture gw x gh standing on the screen point (sx, sy), w x h on the screen */
static void view_at(View *v, long sx, long sy, long w, long h, int gw, int gh, const uchar *lit, const uchar *glow)
{
    long d = (long)((uintptr_t)glow - (uintptr_t)lit);
    int j;
    v->x0 = (float)(sx - w / 2);
    v->y0 = (float)(sy - h + 1);
    v->kx = (float)w / gw;
    v->ky = (float)h / gh;
    v->lit = lit;
    v->glow = glow;
    for (j = 0; j < 16; j++)            /* the screen's light rows between the two (drawobj.c), else the nearer */
        v->lift[j] = d > 0 && d % 256 == 0 && d <= 15 * 256 ? lit + (d / 256 * j + 7) / 15 * 256 : j < 8 ? lit : glow;
}

static int view_clip(View *v)
{
    grs_canvas *cv = grd_canvas;
    v->xa = v->xa > cv->gc.clip.i.left ? v->xa : cv->gc.clip.i.left;
    v->xb = v->xb < cv->gc.clip.i.right ? v->xb : cv->gc.clip.i.right;
    v->ya = v->ya > cv->gc.clip.i.top ? v->ya : cv->gc.clip.i.top;
    v->yb = v->yb < cv->gc.clip.i.bot ? v->yb : cv->gc.clip.i.bot;
    return v->xa < v->xb && v->ya < v->yb;
}

/* the screen point of (x, y, z) and the size of a picture of aspect ah / aw at the world width
 * scale; 0 off the view */
static int project(fix x, fix y, fix z, fix scale, long aw, long ah, long *sx, long *sy, long *w, long *h)
{
    long vx, vy, vz;
    if (!drawobj_29DA60(x, y, z, &vx, &vy, &vz, sx, sy) || vz <= 0)
        return 0;
    if ((vy < 0 ? -vy : vy) > vz * 10 || (vx < 0 ? -vx : vx) > vz * 10)
        return 0;
    *sx >>= 16;
    *sy >>= 16;
    *w = (long)(((long long)ff_cam.zx * scale) / vz) >> 16;      /* as drawobj_29BE78 */
    *h = (long)(((long long)ff_cam.zy * ((ah * (long long)scale) / aw)) / vz) >> 16;
    return *w > 0 && *h > 0 && *w <= 0x4000 && *h <= 0x4000;
}

/* a stump: its leftover, where the plant stood */
static void stump_draw(Stump *st, fix scale, const uchar *lit, const uchar *glow)
{
    grs_canvas *cv = grd_canvas;
    const FxPic *lp = cur_left;
    View v;
    long sx, sy, w, h;
    int gw, gh;
    if (!lp || (lp->boxed && lp->bx1 <= lp->bx0))
        return;
    gw = lp->w;
    gh = lp->h;
    if (!project(st->x, st->y, st->z, scale, gw, gh, &sx, &sy, &w, &h))
        return;
    port_fx_mix_begin();
    colours(port_fx_pal());
    view_at(&v, sx, sy, w, h, gw, gh, lit, glow);
    v.xa = (long)floorf(v.x0);
    v.xb = (long)ceilf(v.x0 + w);
    v.ya = (long)floorf(v.y0);
    v.yb = (long)ceilf(v.y0 + h) + 1;
    if (!view_clip(&v))
        return;
    {
        grs_clip keep = cv->gc.clip;
        short box[4];
        cv->gc.clip.i.left = (short)v.xa;
        cv->gc.clip.i.right = (short)v.xb;
        cv->gc.clip.i.top = (short)v.ya;
        cv->gc.clip.i.bot = (short)v.yb;
        box[0] = lp->bx0;
        box[1] = lp->by0;
        box[2] = lp->bx1;
        box[3] = lp->by1;
        plant(&v, gw, gh, NULL, lp->pix, lp->boxed ? box : NULL);
        cv->gc.clip = keep;
    }
}

/* a broken picture destroyed: it stands a moment in a puff of dust (1170) rolling out from its
 * foot, and three puffs of smoke (1175) rise and fade, by the picture's size (k: its larger side) */
static void rubble_smoke(const View *v, const Burn *b, long ms)
{
    float k = (float)(b->gw > b->gh ? b->gw : b->gh), g = clampf(ms / 1100.0f, 0, 1);
    const FxPic *p;
    int n, i;
    if (ms < 200 && !b->unseen)
        plant(v, b->gw, b->gh, NULL, b->pix, NULL);
    if ((p = port_fx_pic(1175, 0, &n)) != NULL)
        for (i = 0; i < 3; i++) {
            float a = (ms - i * 160) / (float)(SMOKE_MS - 2 * 160), sw, sh, op;
            if (a <= 0 || a >= 1)
                continue;
            sw = k * (0.55f + 0.5f * a);
            sh = sw * 1.7f;
            op = clampf(a * 8, 0, 1) * (1 - a) * (1 - a) * 0.8f;
            p = port_fx_pic(1175, ((int)(ms / 75) + i * 7) % n, NULL);
            pic(v, p, b->gw / 2.0f - sw / 2 + (i - 1) * 0.12f * k + (b->mirror ? -0.1f : 0.1f) * k * a,
                b->gh - sh * 0.75f - (0.15f + 0.6f * a) * k, sw, sh, v->lit, op, (b->seed >> i & 1) != 0);
        }
    if (g < 1 && (p = port_fx_pic(1170, 0, &n)) != NULL) {
        int side;
        for (side = -1; side <= 1; side += 2) {
            float dw = k * (0.6f + 0.6f * g), dh = k * (0.45f + 0.25f * g);
            const FxPic *q = port_fx_pic(1170, (((int)(ms / 60) + side * 5) % n + n) % n, NULL);
            pic(v, q, b->gw / 2.0f + side * (0.12f + 0.45f * g) * k - dw / 2, b->gh - dh + 0.04f * k, dw, dh, v->lit,
                0.7f * (1 - g) * clampf(ms / 80.0f, 0, 1), side < 0);
        }
    }
}

/* drawobj_29AF98: the burn or stump port_burn_set chose, its plant width in the world scale,
 * through the plant's light table lit and an effect's glow (dim: the blast's, a little darker) */
void port_burn_draw(fix scale, const uchar *lit, const uchar *glow, const uchar *dim)
{
    grs_canvas *cv = grd_canvas;
    Burn *b;
    View v;
    long sx, sy, w, h, ms, i;
    float kf, t, T, s, wf, hp, sb;
    int n;
    ulong key;

    if (cur < 0 || !lit || !glow)
        return;
    if (cur >= BURNS) {
        if (stumps[cur - BURNS].used && port_fx_pal())
            stump_draw(&stumps[cur - BURNS], scale, lit, glow);
        return;
    }
    if (!burns[cur].used || !burns[cur].ready)
        return;
    b = &burns[cur];
    key = port_fx_pal();
    if (!key)
        return;
    if (!project(b->x, b->y, b->z, scale, b->gw, b->gh, &sx, &sy, &w, &h))
        return;
    port_fx_mix_begin();
    burn_colours(b, key);
    ms = (long)(game_time - b->t0);
    if (!b->smoke) {
        advance(b, ms > LU_MS ? ms - LU_MS : 0);
        piece_colours(b, key);
    }
    kf = (float)(ms - LU_MS) / 60;     /* the first version's frames: the light-up is -1 .. 2 */
    t = clampf(kf / NF_BLAST, 0, 1);
    T = kf / NF_BLAST;
    s = b->s;
    if (!b->smoke && (b->ev_ms != ms || !b->ev_stage)) {
        evaluate(b, kf);
        b->ev_ms = ms;
    }
    view_at(&v, sx, sy, w, h, b->gw, b->gh, lit, glow);
    /* cut to the box (drawobj_29CCE0's, and below the foot the blast and its dust) and the canvas */
    burn_box(b, &wf, &hp);
    sb = blast_scale(b);
    v.xa = (long)floorf(v.x0 + b->gw * (1 - wf) / 2 * v.kx);
    v.xb = (long)ceilf(v.x0 + b->gw * (1 + wf) / 2 * v.kx);
    v.ya = (long)floorf(v.y0 + (b->gh - hp) * v.ky);
    v.yb = (long)ceilf(v.y0 + (b->gh + burn_below(b)) * v.ky) + 1;
    if (!view_clip(&v))
        return;
    {
        grs_clip keep = cv->gc.clip;
        const FxPic *p;
        cv->gc.clip.i.left = (short)v.xa;
        cv->gc.clip.i.right = (short)v.xb;
        cv->gc.clip.i.top = (short)v.ya;
        cv->gc.clip.i.bot = (short)v.yb;
        if (b->smoke) {
            rubble_smoke(&v, b, ms);
            cv->gc.clip = keep;
            return;
        }
        smoke(&v, b, t, 0.18f, 0.7f, b->mirror ? -14 * t : 0, 110, 230, 3, kf);
        if (!b->lmode)
            pile(&v, b, T, 0.8f);
        /* the plant (what is left of it) behind the blast and the dust, so their first pictures
         * hide it */
        plant(&v, b->gw, b->gh, b->out, NULL, b->lmode && b->ev_stage == 2 ? b->lb : NULL);
        if ((p = port_fx_pic(FX_PLANTBLAST, 0, &n)) != NULL) {    /* the plant's blast, from its picture 3 */
            long j = (n > 3 ? 3 : 0) + (long)((kf - 2) * 60 / 42);
            if (kf >= 2 && j < n) {
                float ax = b->mirror ? 72.0f : 64.0f;
                p = port_fx_pic(FX_PLANTBLAST, j, NULL);
                pic(&v, p, b->gw / 2.0f - ax * sb, b->gh + 4 * sb - 170 * sb, 136 * sb, 176 * sb, dim ? dim : glow, 1, b->mirror);
            }
        } else if (kf >= 2 && kf < 14 && (p = port_fx_pic(1171, 0, &n)) != NULL) {
            float fw = b->gw * 1.8f, fh;    /* else the fireball */
            p = port_fx_pic(1171, (long)((kf - 2) * 3 * n / 30), NULL);
            fh = fw * p->h / p->w;
            pic(&v, p, b->gw / 2.0f - fw / 2, b->gh * 0.6f - fh * 0.8f, fw, fh, dim ? dim : glow, 1, 0);
        }
        if (kf >= 3 && kf < 26) {   /* the dust rolling out */
            float f = (kf - 3) / 23;
            int side;
            p = port_fx_pic(1170, 0, &n);
            for (side = -1; p && side <= 1; side += 2) {
                float dw = (70 + 60 * f) * s, dh = (60 + 30 * f) * s;
                const FxPic *q = port_fx_pic(1170, (((int)kf + side * 5) % n + n) % n, NULL);
                pic(&v, q, b->gw / 2.0f + side * (30 + 70 * f) * s - dw / 2, b->gh - dh + 6 * s, dw, dh, lit, 0.4f * (1 - f), 0);
            }
        }
        shards(&v, b);
        pieces(&v, b);
        for (i = 0; i < b->np; i++) {   /* the spray, sparks, ash */
            const Part *q = &b->part[i];
            fill(&v, q->x, q->y, q->x + q->size, q->y + q->size, (q->glow ? glow : lit)[q->c], 255);
        }
        flakes(&v, b);
        sparks(&v, b);
        cv->gc.clip = keep;
    }
}
