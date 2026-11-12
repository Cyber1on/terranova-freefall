/* g4_c2.h -- declarations for worker g4c2 (batch G4, AI): the second half of the AI message
 * packers M011 (0x25AF34-0x25B372, src/game/ai/aimsgput.c), the path planner M012 / M013
 * (0x25B380-0x25EB5F, pathplan.c / pathfind.c), its priority queue M014 (pathheap.c) and the AI
 * helpers M112-M114 (0x2D2BE0-0x2D367F, aihelp*.c).
 *
 * Names: the map's placeholders are kept (other batches call them); meanings are in the file
 * headers.  Globals of these modules are named after their use and bound to their original
 * addresses through symbols-g4-c2.csv.  Structs are byte-packed.
 *
 * Points ("pt") are one ulong: x in the low word, y in the high word (map squares).  The planner
 * works on two grids of the map record (game_map_35BFB4): the coarse grid (sel 0, row pointers at
 * map + 0x4A) and the fine one (sel 1, 4 x 4 per coarse square, row pointers at map + 0x84E);
 * coarse = fine / 4 + (short at map + 8, short at map + 0xA).  A cell is 3 bytes: terrain type in
 * bits 0-5 of byte 0 (bit 6 = water/bridge flag used by path_25DDEC), height << 4 in the word at +1.
 */
#ifndef TN_G4_C2_H
#define TN_G4_C2_H
#include "lg.h"
#include "fix.h"
#include "game.h"
#include "t1_c.h"
#include "g2_c.h"
#include "g2_a.h"      /* grid, tile objects, game_3C5E1C */
#pragma pack(push, 1)

#define G4C2_PX(p)     ((short)(ulong)(p))
#define G4C2_PY(p)     ((short)((ulong)(p) >> 16))
#define G4C2_PT(x, y)  ((ulong)(ushort)(x) | ((ulong)(ushort)(y) << 16))

/* ------------------------------------------------------------------ M011 (second half) */
void aiacc_25AF34(Msg *m, ulong from, ulong to, long a, long b, long c);
void aiacc_25AF58(Msg *m, short *a, short *b, short *c);
void aiacc_25AF70(Msg *m, ulong from, ulong to, long a, long b, long c);
void aiacc_25AF90(Msg *m, uchar *a, uchar *b, uchar *c);
void aiacc_25AFA4(Msg *m, ulong from, ulong to, long a, long b, long c);
void aiacc_25AFC4(Msg *m, ulong from, ulong to, long a);
void aiacc_25AFD4(Msg *m, ulong from, ulong to, long a, long b);
void aiacc_25AFEC(Msg *m, ulong from, ulong to, long a);
void aiacc_25AFFC(Msg *m, ulong from, ulong to, long a, long b);
void aiacc_25B014(Msg *m, ulong from, ulong to, long a, long b);
void aiacc_25B034(Msg *m, short *a, uchar *b);
void aiacc_25B044(Msg *m, ulong from, ulong to, long a, long b);
void aiacc_25B060(Msg *m, short *a, short *b);
void aiacc_25B074(Msg *m, ulong from, ulong to);
void aiacc_25B084(Msg *m, ulong from, ulong to, long a, long b, long c);
void aiacc_25B0A8(Msg *m, ulong from, ulong to, long a, long b, long c, long d);
void aiacc_25B0D0(Msg *m, uchar *a, uchar *b, uchar *c, uchar *d);
void aiacc_25B0F0(Msg *m, ulong from, ulong to, const void *buf);
void aiacc_25B11C(Msg *m, void *buf);
void aiacc_25B140(Msg *m, ulong from, ulong to, long a);
void aiacc_25B150(Msg *m, ulong from, ulong to, long a);
void aiacc_25B160(Msg *m, ulong from, ulong to, long a);
void aiacc_25B170(Msg *m, ulong from, ulong to, long a);
void aiacc_25B180(Msg *m, ulong from, ulong to, long a);
void aiacc_25B190(Msg *m, ulong from, ulong to);
void aiacc_25B19C(Msg *m, ulong from, ulong to, long a);
void aiacc_25B1AC(Msg *m, ulong from, ulong to, long a);
void aiacc_25B1BC(Msg *m, ulong from, ulong to, long a);
void aiacc_25B1CC(Msg *m, short *a);
void aiacc_25B1D4(Msg *m, ulong from, ulong to);
void aiacc_25B1E0(Msg *m, ulong from, ulong to);
void aiacc_25B1EC(Msg *m, ulong from, long a, long b);
void aiacc_25B208(Msg *m, long unused, ulong to, long id, const void *buf);
void aiacc_25B244(Msg *m, uchar *id, void *buf);
void aiacc_25B26C(Msg *m, ulong from, ulong to, long a);
void aiacc_25B27C(Msg *m, long *a);
void aiacc_25B284(Msg *m, ulong from, ulong to, long a);
void aiacc_25B294(Msg *m, ulong from, ulong to, long a);
void aiacc_25B2A4(Msg *m, ulong from, ulong to);
void aiacc_25B2B0(Msg *m, ulong from, ulong to);
void aiacc_25B2BC(Msg *m, ulong from, ulong to);
void aiacc_25B2C8(Msg *m, ulong from, long a);
void aiacc_25B2E0(Msg *m, ulong from, long a);
void aiacc_25B2F8(Msg *m, ulong from, long a, long b, long c, long d);
void aiacc_25B324(Msg *m, ulong from, ulong to);
void aiacc_25B330(Msg *m, ulong from, ulong to, long a, long b);
void aiacc_25B348(Msg *m, long *a, long *b);
void aiacc_25B358(Msg *m, ulong from, ulong to);
void aiacc_25B364(Msg *m, ulong from, ulong to, long a);

/* ------------------------------------------------------------------ M012-M014 path planner */

/* A search node, 16 bytes.  The planners keep one node per square of the search window in
 * path_pool (index (y - path_oy) * path_w + (x - path_ox)). */
typedef struct {
    ulong pt;            /* +0x0 square */
    ulong parent;        /* +0x4 square of the parent (0xFFFFFFFF = none) */
    ushort bits;         /* +0x8 bits 0-9 heap position, 10-12 step direction / kind,
                            13 side (1 = grown from the start), 14-15 entry offset (path_25E36C) */
    short g;             /* +0xA cost from the start */
    short h;             /* +0xC heuristic (PathPlan: cached line-of-sight slope) */
    short z;             /* +0xE height */
} PathNode;
LG_STATIC_ASSERT(sizeof(PathNode) == 16, PathNode_size);

typedef struct { short key; PathNode *node; } PathHeapEnt;   /* 6 bytes */
typedef struct { short count; short max; PathHeapEnt *ents; } PathHeap;
LG_STATIC_ASSERT(sizeof(PathHeapEnt) == 6, PathHeapEnt_size);

/* The path record (0x309 bytes, ai_paths in g4_a.h): +1 dword end point, +5 short last entry,
 * +7 short current entry, +9 up to 256 entries of 3 bytes: bits 0-8 x, 9-17 y, 18 fine grid,
 * 19-23 heading / 2048 (path_25CFB4). */
#define PATH_W(r, o)   (*(short *)((uchar *)(r) + (o)))
#define PATH_E(r, i)   ((uchar *)(r) + 9 + (i) * 3)

/* globals (symbols-g4-c2.csv) */
extern uchar path_trace_lines;      /* 0x3598F8 debug: mark line-of-sight squares (main_245468) */
extern ulong path_none;             /* 0x3598FC 0xFFFFFFFF: "no point" */
extern long path_map_h;             /* 0x359900 0x200: map size in squares (path_25BE1C clamps) */
extern long path_map_w;             /* 0x359904 0x200 */
extern long path_ox;                /* 0x359908 search window: first x */
extern long path_oy;                /* 0x35990C first y */
extern short path_obj_a;            /* 0x359910 object at the start square (not an obstacle) */
extern short path_obj_b;            /* 0x359912 object at the goal square */
extern PathHeap *path_heap;         /* 0x359918 open list, 0x200 entries */
extern PathHeap *path_heap_local;   /* 0x35991C open list of path_25E36C, 0x40 entries */
extern uchar path_trace;            /* 0x359920 debug: mark expanded squares */
extern fix path_cost_tab[][6];      /* 0x359924 per mode: step factor plain / slow / bad terrain,
                                       heuristic factor plain / slow / bad */
extern short path_slope_cost[40];   /* 0x359984 step cost by height difference */
extern uchar *path_los_seen;        /* 0x389370 PathPlan: squares whose slope is cached (bits) */
extern uchar path_seen[0x800];      /* 0x389380 squares that have a node (bits) */
extern long path_w;                 /* 0x389B88 window width */
extern PathNode *path_pool;         /* 0x389B8C node per window square (ffmem, 0x40000 bytes) */
extern long path_x1;                /* 0x389B90 window: last x (inclusive) */
extern long path_y1;                /* 0x389B94 window: y limit (exclusive) */
extern long path_h;                 /* 0x389B98 window height */
extern uchar path_sel;              /* 0x389B9C 0 coarse grid, 1 fine grid */
extern ulong path_41DD80;           /* 0x41DD80 terrain kinds that slow (bit per kind) */

/* other modules' data (same types as their owners) */

/* M012 */
ulong PathPlan(ulong from, ulong to, ulong dest, long a4, long a5, void *path, long a7,
               long a8, long a9, long a10);                     /* 0x25B380 */
/* M013 */
ulong path_25BD30(ulong pt, fixang a);
void path_25BDBC(fix x, fix y, fixang a, fix *ox, fix *oy);
void path_25BE1C(fix dist, long b, long heading, fix x, fix y, fix *ox, fix *oy); /* 0x25BE1C */
void path_25BED0(void);
uchar path_25BF04(ulong a, ulong b, long dir, void *rec, long near, long mode);
uchar path_25C52C(ulong pt, ulong unused, long side, long far, long dir, long unused2);
long path_25C6A8(ulong a, ulong b, short k);
uchar path_25C704(ulong from, ulong to, PathNode *fn, PathNode *tn, long dir, short *cost,
                  short *mult, short *z, long mode);
short path_25CA34(ulong a, ulong b, long dir, long mode);
long path_25CA70(long side, ulong a, ulong b, long mode);
uchar path_25CC1C(void *rec, ulong a, ulong b);
long path_25CEDC(ulong pt);
void path_25CFB4(void *rec);
void path_25D114(const void *src, void *dst);
uchar path_25D13C(ulong a, ulong b);
uchar path_25D35C(ulong start, long dir, void *rec, long mode);
uchar path_25D9A0(ulong a, ulong b, long r, long dir, void *rec, long mode);
uchar path_25DDEC(ulong a, ulong b, long dir, void *rec, long near, long mode);
uchar path_25E36C(ulong a, ulong b, PathNode *fn, PathNode *tn, long dir, short *cost,
                  short *mult, short *flag, short *z, long mode);
/* M014 */
PathHeap *pathheap_25EB60(short max);
void pathheap_25EBA0(PathHeap *h);
void pathheap_25EBB8(PathHeap *h);
long pathheap_25EBC0(PathHeap *h, PathNode *node, short key, uchar evict);
long pathheap_25ECCC(PathHeap *h, long pos, short key);
PathNode *pathheap_25EDAC(PathHeap *h);

/* ------------------------------------------------------------------ M112-M114 AI helpers */
/* an entity iterator (first = 0 restarts and returns -1; then the next id, 0 at the end) */
typedef long (WATCOMCALL *AihelpIter)(long first);

extern uchar *game_442F44;          /* 0x442F44 spawn table: dword bit mask, then 0x32-byte
                                       records from +4 (+0 kind, +6 bit in game_442420, +7 squad,
                                       +8/+0xC fix position, +0x1B) */
extern ulong game_442420;           /* 0x442420 bit mask tested with spawn record +6 */
extern ushort game_38FAC4;          /* 0x38FAC4 view heading used for the spawn view test */
extern uchar game_44127A;           /* 0x44127A copied to spawn record +0x1B for kind 1 */
extern uchar game_39A3F4;           /* 0x39A3F4 cleared by aihelp_2D3010 */
extern uchar game_442F90[0x40];     /* 0x442F90 map object record buffer (aihelp_2D301C) */
extern uchar game_3885D1[0xC0 * 6]; /* 0x3885D1 0xC0 x 6 bytes, word +0 entity */
extern short aihelp_cur_2D313C;     /* 0x442FD0 iterator cursors */
extern short aihelp_cur_2D31D8;     /* 0x442FD2 */
extern short aihelp_cur_2D3298;     /* 0x442FD4 */
extern short aihelp_cur_2D3388;     /* 0x442FD6 */
extern short aihelp_cur_2D3410;     /* 0x442FD8 */
extern short aihelp_cur_2D34B8;     /* 0x442FDA */
extern uchar aihelp_cur_2D3540;     /* 0x442FDC */
extern uchar aihelp_cur_2D35A0;     /* 0x442FDD */
extern uchar aihelp_cur_2D35FC;     /* 0x442FDE */

/* M112 */
uchar aihelp_2D2BE0(long i);
uchar aihelp_2D2E9C(long i);
void aihelp_2D2F5C(ulong dt);
/* M113 */
void aihelp_2D3010(void);
void aihelp_2D301C(void);
long aihelp_2D30D8(void);
/* M114 */
short aihelp_2D30E0(fix x, fix y, AihelpIter it);
long aihelp_2D313C(long first);
long aihelp_2D31D8(long first);
long aihelp_2D3298(long start);
long aihelp_2D3388(long first);
long aihelp_2D3410(long first);
long aihelp_2D34B8(long first);
long aihelp_2D3540(long first);
long aihelp_2D35A0(long first);
long aihelp_2D35FC(long first);

/* main_245468 (G9b): g9_b.h; misobj_2C9898 (G9c): g9_c.h (moved there at the G9/G10 merge) */

#pragma pack(pop)
#endif
