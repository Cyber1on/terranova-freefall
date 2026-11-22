/* extra.c -- the extra missions (Terra Nova: Freefall): missions that are not part of the game's
 * campaign, played from the Mission Selector with the player's own files.
 *
 * The demo missions.  The two playable demos of 1996 (the folders TNDEMO1 and TNDEMO2 that the
 * Steam installation keeps beside the game's) hold four missions the game itself does
 * not have: Operation Madison (demo 1) and Operations Brickdragger, Flashlight and Citadel
 * (demo 2).  The demos are the same engine, a week older than v1.09, with their own resource
 * files in place of the game's:
 *   MISSnD.RES + its map   the mission, in the game's own format (version 4): used as it is;
 *   DEMOMISn.RES           the missions' tables (RESMISS.RES: squad, kit, time limits, names);
 *   DEMOPTRM.RES 1455      the briefings (items 1..4) and the words after a mission won (5..8);
 *   DEMOGAMn.RES           the goals' texts (0x175), the objects' names (0x177), "Objective: ";
 *   DEMOBABn.RES 1007      the missions' script ("demomiss": functions 1..4 the events of
 *                          missions 1..4, function 5 the goals of all four), compiled for the
 *                          demo's own numbering of natives, engine variables and globals.
 * A demo mission N runs as the game's mission N: the game's rules for missions 1 and 2 (the
 * pickup point, the dropship) are in the demos' code too, with the same numbers.  While it is
 * played these resources take the place of the game's own, in memory (extrares.c; nothing is
 * written and none of the player's files is changed):
 *   - every table of DEMOMISn.RES but the campaign's order (0x6AD: the mission keeps its place
 *     in the game's campaign, so the squad and the kit are the game's at that point);
 *   - RESPTERM.RES 1455 with the mission's briefing, and no note from Mac in the outfitting;
 *   - RESGAME.RES 0x175 with the mission's three goals, 0x177 and 0x172 (the demo's);
 *   - RESBABL2.RES: the script module the game calls for mission N (1011, 1033, 1055, 1077)
 *     made of the demo's functions translated to the game's numbering (the tables below, found
 *     by matching the two programs' native and variable tables), with a debriefing function
 *     (the demo's own, in its "3D" module, said "Mission complete!" or "Mission failure!"; here
 *     the words of DEMOPTRM.RES after a mission won), and the module's strings after it.
 * The mission file and the map are opened under the game's names for them (files.c).
 * The radio messages are the game's: the demo's are fewer but grouped the same way (0x8B0).
 *
 * The lost missions.  The game's CD has mission files in DATA that the installer never copied
 * (with a CD image they are among the files copied once into Freefall's CD folder):
 *   MISS38C.RES   an earlier Operation Panama (mission 38), on the same map: other forces;
 *   MISS40.RES    "Operation Bricklayer": the game's tables, texts and script still have the
 *                 mission number 40 (a squad of four, a kit, the briefing "This is the mission
 *                 to Drag the Brick.", the goals and events of mission 1).  Its file is a
 *                 smaller Operation Eagle Eye on mission 1's map (the file names a map40.res
 *                 that is nowhere; the pirate base's buildings stand where mission 1's do).
 * They are in the mission format's version 3: four tables have other sizes (the game reads
 * version 4 only), which are cut or filled with zeros here, in memory; MISS40.RES, older
 * still, has no pickup point where the game looks for it (lost_pickup).  A lost mission runs as
 * the game's mission of its number, with everything the game has for that number; mission 40,
 * which is in no campaign, takes mission 1's place in the campaign's order.
 * (MISS41.RES is MISS40.RES again with one more building, for a number the game's kit table
 * does not reach; ROADMISS.RES is version 1 and MISS69.RES has no map: not here.)
 *
 * The fan missions.  Operation Stone Henge by Joerg Fischer (1999; here with his permission) is
 * a mission for the first demo, made with the mission editors of the time: demo mission 1 with
 * other forces, labels, names and words, on the game's map of Operation Sherwood (MAP2.RES)
 * with a changed land.  Freefall has what he changed and nothing else: a patch file
 * (tools/mission-patch.py made it; port/res/stone-henge.ffmp, built into tnfree) with, for each
 * resource he changed, the difference between the file he worked from and his own.  The mission
 * is built as demo mission 1 is, from the player's TNDEMO1 and the CD's MAP2.RES, with the
 * patch applied to those resources in memory (patch_apply; each is checked to be the one he
 * worked from, and to come out as his); the changed resources of the mission file and of the
 * map go with the others that take the place of the game's.
 */
#include <SDL3/SDL.h>
#include <string.h>
#include "platform.h"
#include "port.h"
#include "gameart.h"
#include "extra.h"
#include "stb/stb_image.h"              /* its inflate (fxpack.c has the code) */

volatile int port_extra_pick;
int port_extra_on;

#define MAXX 12
#define MAXRES 48
typedef struct {
    ExtraInfo info;
    int demo;                           /* the demo's mission 1..4, 0: not a demo's */
    int lost;                           /* lost[lost - 1], 0: not a lost mission */
    int fan;                            /* fan[fan - 1] (a demo mission with a patch), 0: not one */
    char dir[1100];                     /* its demo's folder */
    ExtraRes res[MAXRES];
    int nres, built;                    /* built: 0 not yet, 1, -1 its files could not be used */
} Extra;
static Extra xs[MAXX];
static int nx = -1;
static SDL_SpinLock init_lock;
static SDL_Mutex *lock;

static void take(void)
{
    SDL_LockSpinlock(&init_lock);
    if (!lock)
        lock = SDL_CreateMutex();
    SDL_UnlockSpinlock(&init_lock);
    SDL_LockMutex(lock);
}

/* ------------------------------------------------------------------ the demos' folders */
static int is_file(const char *p)
{
    SDL_PathInfo inf;
    return SDL_GetPathInfo(p, &inf) && inf.type == SDL_PATHTYPE_FILE;
}

/* TNDEMO1 / TNDEMO2: beside the game's folder (Steam, GOG), in it, or beside tnfree.exe */
static int demo_dir(const char *name, char *out, int size)
{
    char up[1100];
    const char *bases[3];
    SDL_PathInfo inf;
    int i;
    SDL_snprintf(up, sizeof up, "%s/..", port_game_dir);
    bases[0] = up;
    bases[1] = port_game_dir;
    bases[2] = port_exe_dir;
    for (i = 0; i < 3; i++)
        if (bases[i][0] && port_host_join(bases[i], name, out, size) && SDL_GetPathInfo(out, &inf) &&
            inf.type == SDL_PATHTYPE_DIRECTORY)
            return 1;
    return 0;
}

/* a file of a demo: in its DATA or MAPS folder, or in the demo's folder itself */
static int demo_file(const char *dir, const char *name, char *out, int size)
{
    static const char *const sub[3] = { "DATA/", "MAPS/", "" };
    char rel[80];
    int i;
    for (i = 0; i < 3; i++) {
        SDL_snprintf(rel, sizeof rel, "%s%s", sub[i], name);
        if (port_host_join(dir, rel, out, size) && is_file(out))
            return 1;
    }
    return 0;
}

static void lower(char *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z')
            *s = (char)(*s - 'A' + 'a');
}

/* ------------------------------------------------------------------ the lost missions */
typedef struct {
    const char *key, *file;
    int m;                              /* the game's mission it runs as */
    const char *map;                    /* its map, when the file names one that is nowhere */
    int place;                          /* the campaign mission whose place it takes (0: its own) */
    const char *suffix;                 /* after the name the game's tables have for it */
    const char *source;
} Lost;
static const Lost lost[] = {
    { "lost38c", "MISS38C.RES", 38, NULL, 0, " (EARLY VERSION)",
      "An earlier version of the mission, left on the game's CD" },
    { "lost40", "MISS40.RES", 40, "MAP1.RES", 1, NULL,
      "A test mission the developers left on the game's CD" },
};

static void scan_lost(void)             /* (lock held) */
{
    unsigned i;
    for (i = 0; i < SDL_arraysize(lost) && nx < MAXX; i++) {
        const Lost *l = &lost[i];
        Extra *x = &xs[nx];
        ExtraInfo *in = &x->info;
        GaRes f = { 0 };
        uint8_t *b;
        uint32_t bn, sz;
        const uint8_t *t;
        char dos[64];
        int c;

        memset(x, 0, sizeof *x);
        SDL_snprintf(dos, sizeof dos, "D:\\DATA\\%s", l->file);
        port_path(dos, in->miss_host, sizeof in->miss_host);
        if (!is_file(in->miss_host))
            continue;
        if (l->map)
            SDL_snprintf(in->map_name, sizeof in->map_name, "%s", l->map);
        else {
            if (!ga_res_open_host(&f, in->miss_host))
                continue;
            b = ga_res_read(&f, 170, &bn, &c);
            ga_res_close(&f);
            if (!b || !bn) {
                SDL_free(b);
                continue;
            }
            SDL_snprintf(in->map_name, sizeof in->map_name, "%.*s", (int)(bn < 15 ? bn : 15), (const char *)b);
            SDL_free(b);
        }
        SDL_snprintf(dos, sizeof dos, "D:\\MAPS\\%s", in->map_name);
        port_path(dos, in->map_host, sizeof in->map_host);
        if (!is_file(in->map_host)) {
            port_log("Extra missions: %s: its map %s is missing\n", l->file, in->map_name);
            continue;
        }
        lower(in->map_name);
        SDL_snprintf(in->miss_name, sizeof in->miss_name, "miss%d.res", l->m);
        /* its name (the game's table of mission names), its briefing (the game's) */
        SDL_snprintf(in->name, sizeof in->name, "MISSION %d", l->m);
        if (ga_res_open(&f, "C:\\TNOVA\\DATA\\RESMISS.RES")) {
            b = ga_res_read(&f, 0x6AA, &bn, &c);
            t = b && c ? ga_item(b, bn, (unsigned)(l->m - 1), &sz) : NULL;
            if (t && sz > 2)
                SDL_snprintf(in->name, sizeof in->name, "%.*s%s", (int)(sz < 30 ? sz : 30), (const char *)t,
                             l->suffix ? l->suffix : "");
            SDL_free(b);
            ga_res_close(&f);
        }
        port_path("C:\\TNOVA\\DATA\\RESPTERM.RES", in->text_host, sizeof in->text_host);
        SDL_snprintf(in->key, sizeof in->key, "%s", l->key);
        in->group = "Lost mission";
        in->source = l->source;
        in->m = l->m;
        in->text_item = l->m;
        x->lost = (int)i + 1;
        nx++;
    }
}

static void scan_fan(void);

static void scan(void)                  /* (lock held) */
{
    char dir[2][1100], path[1100];
    int have[2], n, f;

    if (nx >= 0)
        return;
    nx = 0;
    have[0] = demo_dir("TNDEMO1", dir[0], sizeof dir[0]);
    have[1] = demo_dir("TNDEMO2", dir[1], sizeof dir[1]);
    for (n = 1; n <= 4; n++) {
        int d = n > 1;
        Extra *x = &xs[nx];
        ExtraInfo *in = &x->info;
        GaRes f = { 0 };
        uint8_t *b;
        uint32_t bn, sz;
        const uint8_t *s;
        int c;
        char name[32];

        if (!have[d])
            continue;
        memset(x, 0, sizeof *x);
        SDL_snprintf(name, sizeof name, "MISS%dD.RES", n);
        if (!demo_file(dir[d], name, in->miss_host, sizeof in->miss_host) ||
            !demo_file(dir[d], "DEMOPTRM.RES", in->text_host, sizeof in->text_host) ||
            !demo_file(dir[d], d ? "DEMOMIS2.RES" : "DEMOMIS1.RES", path, sizeof path) ||
            !demo_file(dir[d], d ? "DEMOGAM2.RES" : "DEMOGAM1.RES", path, sizeof path) ||
            !demo_file(dir[d], d ? "DEMOBAB2.RES" : "DEMOBAB1.RES", path, sizeof path)) {
            port_log("Extra missions: demo mission %d: a file of %s is missing (%s)\n", n,
                     d ? "TNDEMO2" : "TNDEMO1", dir[d]);
            continue;
        }
        /* the mission's map (resource 170 of the mission file names it) */
        if (!ga_res_open_host(&f, in->miss_host))
            continue;
        b = ga_res_read(&f, 170, &bn, &c);
        ga_res_close(&f);
        if (!b || !bn) {
            SDL_free(b);
            continue;
        }
        SDL_snprintf(in->map_name, sizeof in->map_name, "%.*s", (int)(bn < 15 ? bn : 15), (const char *)b);
        SDL_free(b);
        if (!demo_file(dir[d], in->map_name, in->map_host, sizeof in->map_host)) {
            port_log("Extra missions: demo mission %d: its map %s is missing (%s)\n", n, in->map_name, dir[d]);
            continue;
        }
        lower(in->map_name);
        SDL_snprintf(in->miss_name, sizeof in->miss_name, "miss%d.res", n);
        /* its name (the demo's table of mission names) */
        demo_file(dir[d], d ? "DEMOMIS2.RES" : "DEMOMIS1.RES", path, sizeof path);
        SDL_snprintf(in->name, sizeof in->name, "Demo Mission %d", n);
        if (ga_res_open_host(&f, path)) {
            b = ga_res_read(&f, 0x6AA, &bn, &c);
            s = b && c ? ga_item(b, bn, (unsigned)(n - 1), &sz) : NULL;
            if (s && sz > 1)
                SDL_snprintf(in->name, sizeof in->name, "%.*s", (int)(sz < 47 ? sz : 47), (const char *)s);
            SDL_free(b);
            ga_res_close(&f);
        }
        SDL_snprintf(in->key, sizeof in->key, "demo%d", n);
        in->group = "Demo mission";
        in->source = d ? "From the second Terra Nova demo (1996)" : "From the first Terra Nova demo (1996)";
        in->m = n;
        in->hint = n == 1;
        in->text_item = n;
        x->demo = n;
        SDL_snprintf(x->dir, sizeof x->dir, "%s", dir[d]);
        nx++;
    }
    n = nx;
    scan_fan();
    f = nx;
    scan_lost();
    port_log("Extra missions: %d found: %d of the demos (TNDEMO1 %s, TNDEMO2 %s), %d fan-made, %d of the CD's lost "
             "missions\n", nx, n, have[0] ? dir[0] : "not found", have[1] ? dir[1] : "not found", f - n, nx - f);
}

int port_extra_count(void)
{
    take();
    scan();
    SDL_UnlockMutex(lock);
    return nx;
}

const ExtraInfo *port_extra_info(int k)
{
    return k >= 1 && k <= port_extra_count() ? &xs[k - 1].info : NULL;
}

/* files.c: the game's own name of the extra mission's file or map -> where it is */
int port_extra_file(const char *rel, char *out, int size)
{
    const ExtraInfo *in;
    const char *b = rel, *p;
    int k = port_extra_on;
    if (k < 1 || k > nx)
        return 0;
    in = &xs[k - 1].info;
    for (p = rel; *p; p++)
        if (*p == '/' || *p == '\\')
            b = p + 1;
    if (!SDL_strcasecmp(b, in->miss_name))
        SDL_snprintf(out, (size_t)size, "%s", in->miss_host);
    else if (!SDL_strcasecmp(b, in->map_name))
        SDL_snprintf(out, (size_t)size, "%s", in->map_host);
    else
        return 0;
    return 1;
}

/* ------------------------------------------------------------------ resources */
typedef struct {
    const uint8_t *p;
    uint32_t n;
} Item;

static void wr16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v & 0xFFFF); wr16(p + 2, v >> 16); }

/* a compound resource of n items */
static uint8_t *compound_make(const Item *it, unsigned n, uint32_t *size)
{
    uint32_t total = 2 + 4 * (n + 1), o;
    unsigned i;
    uint8_t *b;
    for (i = 0; i < n; i++)
        total += it[i].n;
    b = SDL_calloc(1, total + 16);
    if (!b)
        return NULL;
    wr16(b, n);
    o = 2 + 4 * (n + 1);
    for (i = 0; i < n; i++) {
        wr32(b + 2 + 4 * i, o);
        if (it[i].n)
            memcpy(b + o, it[i].p, it[i].n);
        o += it[i].n;
    }
    wr32(b + 2 + 4 * n, o);
    *size = total;
    return b;
}

/* a compound resource's items (SDL_free the list), 0 when it is not a sound one */
static Item *compound_items(const uint8_t *b, uint32_t n, unsigned *count)
{
    unsigned k, cnt = b && n >= 6 ? ga_rd16(b) : 0;
    Item *it;
    if (!cnt || 2 + 4 * (cnt + 1) > n)
        return NULL;
    it = SDL_calloc(cnt, sizeof *it);
    for (k = 0; it && k < cnt; k++) {
        it[k].p = ga_item(b, n, k, &it[k].n);
        if (!it[k].p) {
            SDL_free(it);
            return NULL;
        }
    }
    *count = cnt;
    return it;
}

/* (the data is the list's from here on; on 0 it is freed) */
static int add(Extra *x, int id, int flags, int type, uint8_t *data, uint32_t size)
{
    ExtraRes *r;
    if (!data || x->nres >= MAXRES) {
        SDL_free(data);
        return 0;
    }
    r = &x->res[x->nres++];
    r->id = (unsigned short)id;
    r->flags = (unsigned char)(flags & 2);
    r->type = (unsigned char)type;
    r->size = size;
    r->data = data;
    return 1;
}

/* the game's compound resource `id` with some of its items taken from elsewhere: put[i] goes in
 * place of item at[i] */
static int add_merged(Extra *x, const GaRes *game, int id, const int *at, const Item *put, int n)
{
    uint32_t bn, size;
    int c = 0, type = 0, i, ok = 0;
    unsigned cnt = 0;
    uint8_t *b = ga_res_read(game, id, &bn, &c), *out;
    Item *it = b && c ? compound_items(b, bn, &cnt) : NULL;

    ga_res_info(game, id, NULL, NULL, &type);
    if (it) {
        for (i = 0; i < n; i++)
            if (at[i] >= 0 && (unsigned)at[i] < cnt && put[i].p)
                it[at[i]] = put[i];
            else
                break;
        if (i == n && (out = compound_make(it, cnt, &size)) != NULL)
            ok = add(x, id, 2, type, out, size);
    }
    SDL_free(it);
    SDL_free(b);
    return ok;
}

/* ------------------------------------------------------------------ the demo's script
 * The demos' script natives and engine variables by number -> the game's (-1: not in the game;
 * the missions' script uses none of those).  The demo's globals from 0x2C on are 0x64 further
 * in the game, and its script resources are: 1001 "3D" -> 1005, 1002 -> 1006, 1005 "missgen"
 * -> 1009, 1006 -> 1010, 1007 "demomiss" (itself) and 1008 (its strings). */
static const short demo_native[138] = {
    83, 84, 85, 0, 86, 2, 3, 4, 5, 6, 7, 8, 87, 88, 41, 89,
    90, 21, 22, 91, 92, 93, 94, 141, 107, 108, 109, 82, 1, 9, 10, 11,
    12, 13, 14, 15, 16, 17, 18, 19, 20, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 42, 43, 44, 45, 46,
    47, 48, 49, 50, 51, 52, 53, 54, -1, -1, 55, 56, 57, 58, 59, 60,
    61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76,
    77, 78, 79, 80, 81, 101, 102, 103, 104, 105, 106, 110, 111, 112, 113, 114,
    115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
    131, 132, 133, 134, 135, 136, 138, 162, 146, 163,
};
static const short demo_var[42] = {
    29, 30, 7, 31, 32, 33, 34, 35, 36, 37, 24, 25, 26, 27, 28, 0,
    1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, -1, 54, 55, 56,
};
#define NATIVE_GOAL_STATE 0x8D          /* the game's ordnance_2918EC: a goal's state (1 met) */

/* the bytes of a script instruction (src/game/script/scrinterp.c), 0 for one not known */
static int op_size(int op)
{
    switch (op) {
    case 0x01: case 0x0C: case 0x37: case 0x39: case 0x3A: case 0x3B: return 2;
    case 0x02: case 0x24: case 0x26: case 0x27: case 0x2B: case 0x2C: case 0x2D: case 0x35: case 0x7F: return 3;
    case 0x03: case 0x0D: case 0x25: case 0x2E: return 5;
    case 0x2A: case 0x3C: return 4;
    case 0x0E: case 0x29: case 0x36: case 0x38: return 1;
    }
    if (op >= 0x04 && op <= 0x0B)
        return 3;
    if ((op >= 0x0F && op <= 0x23) || (op >= 0x2F && op <= 0x34) || (op >= 0x3D && op <= 0x3F))
        return 1;
    return 0;
}

/* A script function's code moved by `delta` (its jumps count from the start of the module's
 * data), and, for the demo's, its numbers made the game's: self the demo module's functions by
 * number -> the new module's, target the new module's resource.  0: an instruction not known,
 * or a number with no match in the game. */
static int script_fix(uint8_t *code, uint32_t n, long delta, int demo, const int *self, int target)
{
    uint32_t pc = 0;
    while (pc < n) {
        int op = code[pc], sz = op_size(op);
        unsigned v;
        long t;
        if (!sz || pc + (uint32_t)sz > n)
            return 0;
        if (demo)
            switch (op) {
            case 0x04: case 0x05: case 0x08: case 0x09:     /* a global */
                v = ga_rd16(code + pc + 1);
                if (v >= 0x2C)
                    wr16(code + pc + 1, v + 0x64);
                break;
            case 0x07: case 0x0B:                           /* an engine variable */
                v = ga_rd16(code + pc + 1);
                if (v >= SDL_arraysize(demo_var) || demo_var[v] < 0)
                    return 0;
                wr16(code + pc + 1, (unsigned)demo_var[v]);
                break;
            case 0x2A:                                      /* a native */
                v = ga_rd16(code + pc + 1);
                if (v >= SDL_arraysize(demo_native) || demo_native[v] < 0)
                    return 0;
                wr16(code + pc + 1, (unsigned)demo_native[v]);
                break;
            case 0x0D: case 0x25: case 0x2E:                /* a string, a call, a new thread */
                v = ga_rd16(code + pc + 3);
                switch (v) {
                case 0x3E9: v = 0x3ED; break;
                case 0x3EA: v = 0x3EE; break;
                case 0x3ED: v = 0x3F1; break;
                case 0x3EE: v = 0x3F2; break;
                case 0x3EF:
                    if (op != 0x0D) {
                        unsigned fn = ga_rd16(code + pc + 1);
                        if (fn > 5 || self[fn] < 0)
                            return 0;
                        wr16(code + pc + 1, (unsigned)self[fn]);
                    }
                    v = (unsigned)target;
                    break;
                case 0x3F0: v = (unsigned)target + 1; break;
                default: return 0;
                }
                wr16(code + pc + 3, v);
                break;
            }
        switch (op) {
        case 0x24: case 0x2B: case 0x2C: case 0x2D:
            t = (long)ga_rd16(code + pc + 1) + delta;
            if (t < 0 || t > 0xFFFF)
                return 0;
            wr16(code + pc + 1, (unsigned)t);
            break;
        case 0x3C:
            t = (long)ga_rd16(code + pc + 2) + delta;
            if (t < 0 || t > 0xFFFF)
                return 0;
            wr16(code + pc + 2, (unsigned)t);
            break;
        case 0x35:                      /* a choice's table of jumps: not in a mission's script */
            if (delta)
                return 0;
            break;
        }
        pc += (uint32_t)sz;
    }
    return 1;
}

/* a script module's function k: its code and where it sits (counted like the jumps) */
static int module_fn(const uint8_t *m, uint32_t mn, unsigned k, Item *code, long *at)
{
    unsigned n = m && mn >= 6 ? ga_rd16(m) : 0;
    uint32_t o0, o1;
    if (k >= n || 6 + 4 * n > mn)
        return 0;
    o0 = ga_rd32(m + 2 + 4 * k);
    o1 = ga_rd32(m + 6 + 4 * k);
    if (o0 < 6 + 4 * n || o0 >= o1 || o1 > mn)
        return 0;
    code->p = m + o0;
    code->n = o1 - o0;
    *at = (long)o0 - (long)(6 + 4 * n);
    return 1;
}

/* The script module the game calls for mission n, from the demo's.  For missions 2..4 (the
 * game's resources 1033, 1055, 1077): functions 1 goals, 2 debriefing, 3 events.  For mission 1
 * (1011): 1 the game's own (a stub), 2 debriefing, 3 goals, 4 the game's own (the thread every
 * mission's messages go through: the "3D" module starts it from here), 5 events.
 * The debriefing: "print string 0 of the module's strings when goal 1 is met, else string 1". */
static uint8_t *module_make(int n, int target, const uint8_t *dm, uint32_t dn, const uint8_t *gm, uint32_t gn,
                            uint32_t *size)
{
    enum { DEMO, GAME, DEBRIEF };
    struct { int from, fn; } plan[6];
    int self[6] = { -1, -1, -1, -1, -1, -1 };
    int count, k;
    uint32_t base, cur, cap;
    uint8_t *out;
    Item name, code;
    long at;

    if (n == 1) {
        count = 6;
        plan[1].from = GAME, plan[1].fn = 1;
        plan[2].from = DEBRIEF, plan[2].fn = 0;
        plan[3].from = DEMO, plan[3].fn = 5;
        plan[4].from = GAME, plan[4].fn = 4;
        plan[5].from = DEMO, plan[5].fn = 1;
    } else {
        count = 4;
        plan[1].from = DEMO, plan[1].fn = 5;
        plan[2].from = DEBRIEF, plan[2].fn = 0;
        plan[3].from = DEMO, plan[3].fn = n;
    }
    for (k = 1; k < count; k++)
        if (plan[k].from == DEMO)
            self[plan[k].fn] = k;
    if (!dm || dn < 6 || ga_rd16(dm) != 6 || 6 + 4 * 6 > dn)
        return NULL;
    name.p = ga_item(dm, dn, 0, &name.n);
    if (!name.p)
        return NULL;
    base = 6 + 4 * (uint32_t)count;
    cap = base + dn + gn + 256;
    out = SDL_calloc(1, cap);
    if (!out)
        return NULL;
    wr16(out, (unsigned)count);
    wr32(out + 2, base);
    memcpy(out + base, name.p, name.n);
    cur = name.n;                       /* (counted from base, like the jumps) */
    for (k = 1; k < count; k++) {
        uint8_t *d = out + base + cur;
        wr32(out + 2 + 4 * k, base + cur);
        if (plan[k].from == DEBRIEF) {
            static const uint8_t tmpl[40] = {
                0x27, 0, 0,                         /* enter */
                0x01, 1, 0x29,                      /* goal 1 */
                0x2A, NATIVE_GOAL_STATE, 0, 1,      /* its state */
                0x01, 1, 0x19,                      /* == 1 */
                0x24, 0, 0,                         /* if not: to 27 */
                0x01, 0, 0x0D, 0, 0, 0, 0, 0x36,    /* print string 0 */
                0x2D, 0, 0,                         /* to 35 */
                0x01, 0, 0x0D, 1, 0, 0, 0, 0x36,    /* 27: print string 1 */
                0x01, 0, 0x26, 0, 0,                /* 35: return 0 */
            };
            if (cur + 35 > 0xFFFF)
                goto bad;
            memcpy(d, tmpl, sizeof tmpl);
            wr16(d + 14, cur + 27);
            wr16(d + 21, (unsigned)target + 1);
            wr16(d + 25, cur + 35);
            wr16(d + 32, (unsigned)target + 1);
            cur += sizeof tmpl;
            continue;
        }
        if (plan[k].from == DEMO ? !module_fn(dm, dn, (unsigned)plan[k].fn, &code, &at) :
                                   !module_fn(gm, gn, (unsigned)plan[k].fn, &code, &at))
            goto bad;
        if (base + cur + code.n > cap)
            goto bad;
        memcpy(d, code.p, code.n);
        if (!script_fix(d, code.n, (long)cur - at, plan[k].from == DEMO, self, target))
            goto bad;
        cur += code.n;
    }
    wr32(out + 2 + 4 * (uint32_t)count, base + cur);
    *size = base + cur;
    return out;
bad:
    SDL_free(out);
    return NULL;
}

/* ------------------------------------------------------------------ a fan mission's patch
 * What its author changed in the files his mission was made from (tools/mission-patch.py):
 * "FFMP", the number of resources, then for each: the file (PF_...), the resource, its size, the
 * CRC-32 of the original and of the author's, and the two XORed with each other, packed (zlib). */
enum { PF_MISSION, PF_MAP, PF_TABLES, PF_TEXT, PF_NAMES };     /* MISSnD, the map, DEMOMISn, DEMOPTRM, DEMOGAMn */
typedef struct {
    const char *key, *name;
    int demo;                           /* the demo mission it is a changed copy of */
    const char *map;                    /* the game's map its own was made from */
    const unsigned char *patch;
    const unsigned *patch_len;
    const char *group, *source;
} Fan;
extern const unsigned char port_patch_stone_henge[];    /* (port/res/stone-henge.ffmp) */
extern const unsigned port_patch_stone_henge_len;
static const Fan fan[] = {
    { "stonehenge", "OPERATION STONE HENGE", 1, "MAP2.RES", port_patch_stone_henge, &port_patch_stone_henge_len,
      "Fan mission by Joerg Fischer", "A mission for the first demo by Joerg Fischer (1999)" },
};
typedef struct {
    int file, id;
    uint32_t size, crc0, crc1, zn;
    const uint8_t *z;
} PatchRec;

/* the patch's record k (0: past the last, or the patch is damaged) */
static int patch_rec(const Fan *f, unsigned k, PatchRec *r)
{
    const uint8_t *p = f->patch, *end = p + *f->patch_len;
    unsigned i;
    if (*f->patch_len < 6 || memcmp(p, "FFMP", 4) || k >= ga_rd16(p + 4))
        return 0;
    p += 6;
    for (i = 0;; i++) {
        if (end - p < 19)
            return 0;
        r->file = p[0];
        r->id = (int)ga_rd16(p + 1);
        r->size = ga_rd32(p + 3);
        r->crc0 = ga_rd32(p + 7);
        r->crc1 = ga_rd32(p + 11);
        r->zn = ga_rd32(p + 15);
        r->z = p + 19;
        if ((uint32_t)(end - r->z) < r->zn)
            return 0;
        if (i == k)
            return 1;
        p = r->z + r->zn;
    }
}

/* Resource id of the fan mission's file `file`, the player's own (b, n bytes), made the
 * author's.  1: done, or he did not change it; 0 (*why says it): it is not the one he worked
 * from. */
static int patch_apply(const Extra *x, int file, int id, uint8_t *b, uint32_t n, const char **why)
{
    const Fan *f = x->fan ? &fan[x->fan - 1] : NULL;
    PatchRec r;
    unsigned k;
    uint8_t *d;
    uint32_t i;
    int ok;

    for (k = 0; f && patch_rec(f, k, &r); k++) {
        if (r.file != file || r.id != id)
            continue;
        if (n != r.size || SDL_crc32(0, b, n) != r.crc0) {
            static const char *const which[5] = {
                "the demo's mission file (it is not the one the mission's author worked from)",
                "the game's map (it is not the one the mission's author worked from)",
                "the demo's mission tables (they are not the ones the mission's author worked from)",
                "the demo's briefings (they are not the ones the mission's author worked from)",
                "the demo's names of objects (they are not the ones the mission's author worked from)",
            };
            *why = which[file];
            return 0;
        }
        if (!(d = SDL_malloc(n)))
            return 0;
        ok = stbi_zlib_decode_buffer((char *)d, (int)n, (const char *)r.z, (int)r.zn) == (int)n;
        for (i = 0; ok && i < n; i++)
            b[i] ^= d[i];
        SDL_free(d);
        if (ok && SDL_crc32(0, b, n) == r.crc1)
            return 1;
        *why = "the mission's patch (damaged)";
        return 0;
    }
    return 1;
}

/* the resources the author changed in the mission file or the map (host: the player's), made
 * his: among the resources that take the place of the game's */
static int add_patched(Extra *x, int file, const char *host, const char **why)
{
    const Fan *f = &fan[x->fan - 1];
    GaRes g = { 0 };
    PatchRec r;
    unsigned k;
    uint8_t *b;
    uint32_t bn;
    int comp, flags, type, ok = 1;

    if (!ga_res_open_host(&g, host)) {
        *why = file == PF_MAP ? "the game's map (the CD's MAPS)" : "the demo's mission file";
        return 0;
    }
    for (k = 0; ok && patch_rec(f, k, &r); k++) {
        if (r.file != file)
            continue;
        ok = 0;
        *why = file == PF_MAP ? "the game's map (the CD's MAPS)" : "the demo's mission file";
        if (!ga_res_info(&g, r.id, NULL, &flags, &type) || !(b = ga_res_read(&g, r.id, &bn, &comp)))
            break;
        if (!patch_apply(x, file, r.id, b, bn, why)) {
            SDL_free(b);
            break;
        }
        if (file == PF_MISSION && r.id == 170 && bn > strlen(f->map)) {
            /* the map's name: the author's own map is the game's with his changes */
            memset(b, 0, bn);
            memcpy(b, f->map, strlen(f->map));
        }
        ok = add(x, r.id, flags, type, b, bn);
    }
    ga_res_close(&g);
    return ok;
}

/* ------------------------------------------------------------------ a demo mission's resources */
static int build_demo(Extra *x)
{
    const int n = x->demo, d = n > 1, target = n == 1 ? 1011 : 1033 + 22 * (n - 2);
    char path[1100];
    GaRes game = { 0 }, dem = { 0 }, txt = { 0 };
    uint8_t *a = NULL, *b = NULL, *c = NULL, *mod;
    uint32_t an = 0, bn = 0, cn = 0, size;
    int comp, flags, type, ok = 0, i, id;
    const char *why = "the demo's mission tables";
    Item put[3];
    int at[3];

    /* the mission tables */
    if (!demo_file(x->dir, d ? "DEMOMIS2.RES" : "DEMOMIS1.RES", path, sizeof path) || !ga_res_open_host(&dem, path))
        goto out;
    for (i = 0; (id = ga_res_id(&dem, (unsigned)i)) >= 0; i++) {
        if (id == 0x6AD)
            continue;                   /* the campaign's order stays the game's */
        uint8_t *t;
        if (!ga_res_info(&dem, id, NULL, &flags, &type) || !(t = ga_res_read(&dem, id, &an, &comp)))
            goto out;
        if (!patch_apply(x, PF_TABLES, id, t, an, &why)) {
            SDL_free(t);
            goto out;
        }
        if (!add(x, id, flags, type, t, an))
            goto out;
    }
    ga_res_close(&dem);
    if (x->nres < 20)
        goto out;

    /* the briefing; no note from Mac in the outfitting; (kept open: the words after the mission) */
    why = "the briefing (DEMOPTRM.RES, RESPTERM.RES)";
    if (!ga_res_open_host(&txt, x->info.text_host) || !(c = ga_res_read(&txt, 1455, &cn, &comp)) || !comp ||
        !ga_res_open(&game, "C:\\TNOVA\\DATA\\RESPTERM.RES") || !patch_apply(x, PF_TEXT, 1455, c, cn, &why))
        goto out;
    at[0] = n;
    put[0].p = ga_item(c, cn, (unsigned)n, &put[0].n);
    at[1] = 0x30 + n;
    put[1].p = (const uint8_t *)" ";
    put[1].n = 2;
    if (!add_merged(x, &game, 1455, at, put, 2))
        goto out;
    ga_res_close(&game);

    /* the goals, the objects' names, "Objective: " */
    why = "the goals' texts (DEMOGAMn.RES, RESGAME.RES)";
    if (!demo_file(x->dir, d ? "DEMOGAM2.RES" : "DEMOGAM1.RES", path, sizeof path) || !ga_res_open_host(&dem, path) ||
        !ga_res_open(&game, "C:\\TNOVA\\DATA\\RESGAME.RES"))
        goto out;
    if (!(a = ga_res_read(&dem, 0x175, &an, &comp)) || !comp)
        goto out;
    for (i = 0; i < 3; i++) {
        at[i] = n * 3 + i;
        put[i].p = ga_item(a, an, (unsigned)at[i], &put[i].n);
    }
    if (!add_merged(x, &game, 0x175, at, put, 3))
        goto out;
    SDL_free(a);
    if (!(a = ga_res_read(&dem, 0x172, &an, &comp)) || !comp)
        goto out;
    at[0] = 10;
    put[0].p = ga_item(a, an, 10, &put[0].n);
    if (!add_merged(x, &game, 0x172, at, put, 1))
        goto out;
    SDL_free(a);
    a = NULL;
    type = 0;
    ga_res_info(&game, 0x177, NULL, NULL, &type);
    {
        uint8_t *t = ga_res_read(&dem, 0x177, &an, &comp);
        if (t && !patch_apply(x, PF_NAMES, 0x177, t, an, &why)) {
            SDL_free(t);
            goto out;
        }
        if (!add(x, 0x177, 2, type, t, an))
            goto out;
    }
    ga_res_close(&dem);
    ga_res_close(&game);

    /* the script, and its strings: the words after the mission won (DEMOPTRM.RES 1455 items
     * 5..8) and the demo's "Mission failure!" */
    why = "the mission's script (DEMOBABn.RES, RESBABL2.RES)";
    if (!demo_file(x->dir, d ? "DEMOBAB2.RES" : "DEMOBAB1.RES", path, sizeof path) || !ga_res_open_host(&dem, path) ||
        !ga_res_open(&game, "C:\\TNOVA\\DATA\\RESBABL2.RES"))
        goto out;
    a = ga_res_read(&dem, 1007, &an, &comp);
    b = ga_res_read(&game, 1011, &bn, &comp);
    if (!a || !b)
        goto out;
    mod = module_make(n, target, a, an, b, bn, &size);
    type = 12;
    ga_res_info(&game, target, NULL, NULL, &type);
    if (!mod) {
        why = "the mission's script: not the demo's script this build knows";
        goto out;
    }
    if (!add(x, target, 2, type, mod, size))
        goto out;
    SDL_free(a);
    if (!(a = ga_res_read(&dem, 1002, &an, &comp)) || !comp)
        goto out;
    put[0].p = ga_item(c, cn, (unsigned)(4 + n), &put[0].n);
    put[1].p = ga_item(a, an, 1, &put[1].n);
    if (!put[0].p || !put[1].p)
        goto out;
    type = 1;
    ga_res_info(&game, target + 1, NULL, NULL, &type);
    mod = compound_make(put, 2, &size);     /* (first: its size is read in the call below) */
    if (!add(x, target + 1, 2, type, mod, size))
        goto out;
    /* a fan mission: what its author changed in the mission file and in the map */
    if (x->fan && (!add_patched(x, PF_MISSION, x->info.miss_host, &why) || !add_patched(x, PF_MAP, x->info.map_host, &why)))
        goto out;
    ok = 1;
out:
    if (!ok)
        port_log("Extra missions: %s cannot be played: %s could not be used\n", x->info.name, why);
    SDL_free(a);
    SDL_free(b);
    SDL_free(c);
    ga_res_close(&dem);
    ga_res_close(&game);
    ga_res_close(&txt);
    return ok;
}

/* The fan missions: each needs its demo mission (found above) and the game's map.  They are built
 * here, so that one whose files are not the ones its author worked from is not in the list. */
static void scan_fan(void)              /* (lock held) */
{
    unsigned i;
    int k, base = nx;
    for (i = 0; i < SDL_arraysize(fan) && nx < MAXX; i++) {
        const Fan *f = &fan[i];
        Extra *x = &xs[nx];
        ExtraInfo *in = &x->info;
        char dos[64];

        for (k = 0; k < base && xs[k].demo != f->demo; k++)
            ;
        if (k == base)
            continue;                   /* (its demo is not here: nothing to say) */
        *x = xs[k];
        x->nres = x->built = 0;
        SDL_snprintf(in->key, sizeof in->key, "%s", f->key);
        SDL_snprintf(in->name, sizeof in->name, "%s", f->name);
        in->group = f->group;
        in->source = f->source;
        in->hint = 0;
        SDL_snprintf(in->map_name, sizeof in->map_name, "%s", f->map);
        lower(in->map_name);
        SDL_snprintf(dos, sizeof dos, "D:\\MAPS\\%s", f->map);
        port_path(dos, in->map_host, sizeof in->map_host);
        if (!is_file(in->map_host)) {
            port_log("Extra missions: %s is left out: the game's map %s is missing (the CD's MAPS)\n", f->name, f->map);
            continue;
        }
        x->fan = (int)i + 1;
        x->built = build_demo(x) ? 1 : -1;
        if (x->built < 0) {
            for (k = 0; k < x->nres; k++)
                SDL_free(x->res[k].data);
            continue;
        }
        nx++;
    }
}

/* The Mission Selector's preview: a fan mission's own version of a resource of its mission
 * file, its map or its briefing (a copy: SDL_free it), NULL when it has none. */
unsigned char *port_extra_own(int k, int id, unsigned *size, int *compound)
{
    const Extra *x;
    unsigned char *b;
    int i;
    if (k < 1 || k > port_extra_count() || !xs[k - 1].fan)
        return NULL;
    x = &xs[k - 1];
    for (i = 0; i < x->nres; i++)
        if (x->res[i].id == id && (b = SDL_malloc(x->res[i].size + 16)) != NULL) {
            memcpy(b, x->res[i].data, x->res[i].size);
            *size = (unsigned)x->res[i].size;
            if (compound)
                *compound = x->res[i].flags & 2;
            return b;
        }
    return NULL;
}

/* ------------------------------------------------------------------ a lost mission's resources */
/* The dropship picks the squad up at the mission's point 31 (table 176: 8 bytes a point, x and
 * y; ordnance.c's game_4414C8).  A file from before that rule has no point 31: it gets the place
 * of the map label that says PICKUP POINT (table 175: x and y at byte 5, the text at byte 25). */
static void lost_pickup(const GaRes *f, uint8_t *points)
{
    static const uint8_t none[8] = { 0 };
    uint32_t bn, sz;
    int comp;
    unsigned k;
    uint8_t *b;

    if (memcmp(points + 31 * 8, none, 8))
        return;
    b = ga_res_read(f, 175, &bn, &comp);
    for (k = 0; b && comp && k < ga_rd16(b); k++) {
        const uint8_t *it = ga_item(b, bn, k, &sz);
        const char *t = (const char *)it + 25;
        uint32_t n = 25;
        if (!it || sz < 32)
            continue;
        while (n < sz && (*t == '-' || *t == ' '))
            t++, n++;
        if (n + 6 <= sz && !SDL_strncasecmp(t, "PICKUP", 6)) {
            memcpy(points + 31 * 8, it + 5, 8);
            break;
        }
    }
    SDL_free(b);
}

static int build_lost(Extra *x)
{
    /* the tables whose size changed with the mission format's version 4, and that size */
    static const struct { int id; uint32_t size; } v4[4] = { { 176, 1024 }, { 177, 2048 }, { 181, 1604 }, { 182, 1280 } };
    const Lost *l = &lost[x->lost - 1];
    GaRes f = { 0 };
    uint8_t *b, *t;
    uint32_t bn, sz;
    int comp, flags, type, ok = 0, i, ver;
    const char *why = "its mission file";

    if (!ga_res_open_host(&f, x->info.miss_host))
        goto out;
    b = ga_res_read(&f, 180, &bn, &comp);
    ver = b && bn >= 2 ? (int)ga_rd16(b) : 0;
    SDL_free(b);
    if (ver != 3 && ver != 4) {
        why = "its mission file (not version 3 or 4 of the format)";
        goto out;
    }
    if (ver == 3) {
        for (i = 0; i < 4; i++) {
            if (!ga_res_info(&f, v4[i].id, NULL, &flags, &type) || !(b = ga_res_read(&f, v4[i].id, &bn, &comp)))
                goto out;
            t = SDL_calloc(1, v4[i].size);
            if (t)
                memcpy(t, b, bn < v4[i].size ? bn : v4[i].size);
            SDL_free(b);
            if (t && v4[i].id == 176)
                lost_pickup(&f, t);
            if (!add(x, v4[i].id, flags, type, t, v4[i].size))
                goto out;
        }
        type = 0;
        ga_res_info(&f, 180, NULL, NULL, &type);
        t = SDL_calloc(1, 2);
        if (t)
            t[0] = 4;
        if (!add(x, 180, 0, type, t, 2))
            goto out;
    }
    if (l->map) {                       /* the map's name */
        type = 0;
        if (!ga_res_info(&f, 170, NULL, NULL, &type) || !(b = ga_res_read(&f, 170, &bn, &comp)) ||
            bn <= strlen(l->map)) {
            SDL_free(b);
            goto out;
        }
        memset(b, 0, bn);
        memcpy(b, l->map, strlen(l->map));
        if (!add(x, 170, 0, type, b, bn))
            goto out;
    }
    ga_res_close(&f);
    if (l->place) {                     /* the campaign's order with it in a mission's place */
        const uint8_t *o;
        why = "the campaign's order (RESMISS.RES)";
        type = 0;
        if (!ga_res_open(&f, "C:\\TNOVA\\DATA\\RESMISS.RES") || !ga_res_info(&f, 0x6AD, NULL, NULL, &type) ||
            !(b = ga_res_read(&f, 0x6AD, &bn, &comp)))
            goto out;
        o = comp ? ga_item(b, bn, 0, &sz) : NULL;
        for (i = 0; o && i < (int)sz; i++)
            if (o[i] == l->place) {
                b[(o - b) + i] = (uint8_t)l->m;
                break;
            }
        if (!o || i == (int)sz) {
            SDL_free(b);
            goto out;
        }
        if (!add(x, 0x6AD, 2, type, b, bn))
            goto out;
    }
    ok = 1;
out:
    if (!ok)
        port_log("Extra missions: %s cannot be played: %s could not be used\n", x->info.name, why);
    ga_res_close(&f);
    return ok;
}

const ExtraRes *port_extra_pack(int k, int *n)
{
    Extra *x;
    *n = 0;
    if (k < 1 || k > port_extra_count())
        return NULL;
    x = &xs[k - 1];
    take();
    if (!x->built) {
        x->built = (x->lost ? build_lost(x) : build_demo(x)) ? 1 : -1;
        if (x->built < 0) {
            int i;
            for (i = 0; i < x->nres; i++)
                SDL_free(x->res[i].data);
            x->nres = 0;
        }
    }
    SDL_UnlockMutex(lock);
    if (x->built < 0)
        return NULL;
    *n = x->nres;
    return x->res;
}
