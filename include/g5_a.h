/* g5_a.h -- declarations for worker g5a (batch G5, terrain and render pipeline):
 *   map.c      (M068 0x293AF0-0x295180, src/game/terrain/map.c): map load (MapLoad), the map
 *              record (GameMap), terrain heights, building table
 *   display.c  (M088 0x2AD150-0x2AD9F0, src/game/render/display.c): low-res pixel doubling,
 *              blend table
 *   tcache.c   (M089 0x2AD9F0-0x2ADEB0 + M090 0x2ADEB0-0x2ADF00, src/game/terrain/tcache.c):
 *              terrain cache memstack, texture-strip pattern table, render record alloc
 *   terrrast.c (M091 0x2ADF00-0x2AF4E0, src/game/terrain/terrrast.c): terrain raster
 *   haze.c     (M095 0x2B3980-0x2B5C90, src/game/terrain/haze.c): object visibility list, haze
 * Names are ours unless they come from the map or an assert string.  Globals are bound to their
 * original addresses through symbols-g5-a.csv.
 */
#ifndef TN_G5_A_H
#define TN_G5_A_H
#include "lg.h"
#include "fix.h"
#include "game.h"
#include "res.h"
#include "support.h"
#include "2d.h"
#include "g1_a.h"      /* RndStream, RndRange */
#include "g1_b.h"      /* game_big_buf, ffmem_2C4F40 / 2C4FE4 */
#include "g2_a.h"      /* grid squares, tile templates */
#include "g2_b.h"      /* game_map_35BFB4, game_cls4 */
#include "g3_a.h"      /* terrain_cornerbits, terrain_bitmask, game_41DD58, damage_27D6E8 */
#include "t2_b.h"      /* fn_split / fn_merge */
#include "g5_b.h"      /* FfView ff_view, FfCamera ff_cam */
#pragma pack(push, 1)

/* ------------------------------------------------------------------ map.c (M068) */
/* A map cell: 3 bytes.  word0 = b0 | b1 << 8: bits 0-5 terrain type, bit 6 "mixed" flag (the
 * square's fine cells differ in kind or height), bits 7-9 shade (0..7); b1 bits 2-3 texture
 * variant (0..3); word1 = b1 | b2 << 8, (short)word1 >> 4 = height (1/24 world unit). */
#define MAP_CELL_TYPE(c)   ((c)[0] & 0x3F)
#define MAP_CELL_H(c)      ((short)(*(ushort *)((c) + 1)) >> 4)

/* The map record (game_map_35BFB4 points at it; map_cur 0x39A3E8 holds the same pointer).
 * +0x00..+0x33 come from map resource 0x50.  Two grids: the coarse grid (w x h cells, rows1)
 * and the fine grid (0x201 x 0x201 cells, 4 per coarse square, rows2) for the 512 x 512
 * square play area that starts at coarse square (x0, y0). */
typedef struct {
    uchar flags;        /* +0x000 bit 0: grids come from the planet generator; bit 1 loaded */
    uchar f01[3];
    short w;            /* +0x004 coarse grid width (cells per row) */
    short h;            /* +0x006 coarse grid rows */
    short x0;           /* +0x008 fine area origin in coarse cells */
    short y0;           /* +0x00A */
    short x1;           /* +0x00C */
    short y1;           /* +0x00E */
    short f10, f12;     /* +0x010 both nonzero: the map has a fine grid */
    uchar planet[0x20]; /* +0x014 PlanetUse() record (G5c) */
    uchar f34;          /* +0x034 bit 0: copy the fine terrain kinds into the coarse grid */
    uchar f35;
    uchar *base;        /* +0x036 buffer (game_big_buf): resource 0x51 (0x88D bytes) */
    uchar *res52;       /* +0x03A base + 0x88D: resource 0x52 (14 bytes) */
    void *f3e;          /* +0x03E = &map_35C00C */
    uchar *grid1;       /* +0x042 coarse grid (base + 0x89B) */
    uchar *grid2;       /* +0x046 fine grid (after the coarse grid) */
    uchar *rows1[0x201];/* +0x04A coarse row pointers (h used) */
    uchar *rows2[0x201];/* +0x84E fine row pointers */
    ulong f1052;        /* +0x1052 resource 0x5F size / 8 */
    void *f1056;        /* +0x1056 resource 0x5F (f_malloc) */
    char path[0x50];    /* +0x105A map file name without drive / directory */
    uchar f10aa;        /* +0x10AA resource 0x57 (one byte) */
} GameMap;
LG_STATIC_ASSERT(sizeof(GameMap) == 0x10AB, GameMap_size);
#define GMAP ((GameMap *)game_map_35BFB4)

/* building table: open addressing on the map square, 0x3F1 entries */
typedef struct {
    short x, y;         /* +0 square (x == y == -1: free) */
    short height;       /* +4 ground height + building height */
    short ent;          /* +6 entity */
} MapBldg;
#define MAP_BLDGS 0x3F1

extern long map_state;              /* 0x35BFB8 0 opened, 2 loading in blocks, 3 loaded */
extern uchar *map_load_ptr;         /* 0x35BFBC destination of the block loader */
extern char *map_terrain_names[];   /* 0x35BFC8 WATER, SAND ... MUD, NULL */
extern long map_35C00C;             /* 0x35C00C (GameMap.f3e points here) */
extern RndStream map_rnd;           /* 0x35C010 texture variant stream */
extern long map_bldg_count;         /* 0x35C01C */
extern long game_35CADC;            /* 0x35CADC >= 0x10: flat terrain (no shading / heights) */
extern MapBldg map_bldgs[MAP_BLDGS];/* 0x397F30 */
extern long map_shade_max;          /* 0x39A3E4 largest shade of the fine grid */
extern GameMap *map_cur;            /* 0x39A3E8 */
extern void *map_39A3EC;            /* 0x39A3EC buffer freed after the load (if game_39A3F4) */
extern int map_file;                /* 0x39A3F0 map resource file, -1 none */
extern char map_name[];             /* 0x39A3F5 */
extern short map_res_id;            /* 0x39A402 resource being loaded in blocks */
extern ulong map_vary_mask;         /* 0x41DD78 kinds that get random texture variants */
extern short map_pair_tab[24][4];   /* 0x293AF0 (code segment) fine-cell pairs compared by map_29462C */

/* copies of other workers' declarations (identical) */
void ResExtractInBlocks(Id id, void *buff, long blockSize, void *f_ProcBlock); /* 0x2F5C40 */
/* G5c callees (orig rows here until G5c lands) */

void MapTaskHook(void);                         /* 0x293BB0 frame hook slot 22 */
void map_293C00(void);                          /* 0x293C00 map_file = -1 */
void map_293C0C(void);                          /* 0x293C0C install the map hook */
void map_293C2C(void);                          /* 0x293C2C disable the map hook */
uchar *map_293C34(GameMap *m);                  /* 0x293C34 lay the map out in game_big_buf */
void map_293D5C(void);                          /* 0x293D5C load resource 0x5F */
void map_293DCC(GameMap *m);                    /* 0x293DCC release the map buffers */
long map_293E20(char *fname, GameMap *m);       /* 0x293E20 open a map file */
long map_293EC8(void *buf, long n);             /* 0x293EC8 block loader callback */
void *map_293EFC(Id id, void *buf, long block); /* 0x293EFC extract (in blocks when loading) */
long MapLoad(char *fname, GameMap *m, long state); /* 0x293F50 */
void map_294148(void);                          /* 0x294148 close the map file */
long map_294190(void);                          /* 0x294190 PlanetUse(gMap + 0x14) */
long map_2941A4(char *name);                    /* 0x2941A4 terrain name -> kind, -1 */
void map_2941E8(void *rec);                     /* 0x2941E8 add a map object record */
void map_29431C(short sel, short row);          /* 0x29431C texture variants of a grid row */
void map_294468(void);                          /* 0x294468 fine kinds -> coarse grid */
void map_29462C(void);                          /* 0x29462C mark mixed coarse squares */
void map_294900(void);                          /* 0x294900 map_shade_max */
void map_294960(GameMap *m);                    /* 0x294960 coarse row pointers */
void map_2949A0(GameMap *m);                    /* 0x2949A0 fine row pointers */
fix map_2949C4(fix x, fix y);                   /* 0x2949C4 terrain height */
fix map_294B18(fix x, fix y);                   /* 0x294B18 terrain shade */
void map_294C78(void);                          /* 0x294C78 clear the building table */
void map_294CFC(void);                          /* 0x294CFC buildings from the class-4 objects */
void map_294ED4(ulong pt, short ent);           /* 0x294ED4 add a building */
short map_295000(ulong pt);                     /* 0x295000 object on a square (G5a) */
short map_2950A0(ulong pt);                     /* 0x2950A0 height of a square, objects included (G5a) */

/* ------------------------------------------------------------------ tcache.c (M089, M090) */
typedef struct {
    short id;           /* +0 pattern number / circle radius */
    ushort size;        /* +2 bytes at data */
    uchar *data;        /* +4 */
} TcRec;
/* game_438C20 (g1_b.h, void *): 0x30-byte record made by ralloc_2ADEB0 */
typedef struct {
    uchar f00[0x24];
    TcRec *first;       /* +0x24 first dynamic record (tcache_recs + 50) */
    TcRec *end;         /* +0x28 end of the used records */
    uchar f2c[4];
} TcList;
extern MemStack tcache_ms;          /* 0x438C00 0x4240-byte stack */
extern uchar **tcache_tab;          /* 0x438C0C pattern pairs, index 1..0x31 */
extern TcRec *tcache_recs;          /* 0x438C10 records, index 1.. */
extern uchar tcache_dir9[9];        /* 0x2AD9F0 (code segment) step direction codes [dx+1][dy+1] */
void tcache_2AD9FC(void *buf);      /* 0x2AD9FC */
void memstack_2ADA2C(void);         /* 0x2ADA2C */
void memstack_2ADB80(void);         /* 0x2ADB80 */
void memstack_2ADB8C(void *obj);    /* 0x2ADB8C */
long memstack_2ADBD8(long err, short x, short y, short *sx, short *sy); /* 0x2ADBD8 */
long memstack_2ADC84(long err, short *px, short *py); /* 0x2ADC84 */
void *memstack_2ADD14(long rad);    /* 0x2ADD14 */
void ralloc_2ADEB0(void);           /* 0x2ADEB0 */
void ralloc_2ADEEC(void);           /* 0x2ADEEC */

/* ------------------------------------------------------------------ display.c (M088) */
extern uchar *disp_ltab;            /* 0x41EBE8 colour remap applied after blending (flag 4) */
extern uchar disp_mode;             /* 0x41EBF0 doubling routine 1..4 */
extern uchar disp_flags;            /* 0x41EBF2 1 low res (double), 2 double rows, 4 remap */
extern void *disp_double_fns[5];    /* 0x35CA74 doubling routines by disp_mode (0: none) */
extern uchar disp_blend_buf[0x10000]; /* 0x428BF0 */
extern uchar *disp_blend;           /* 0x438BF0 blend table [a << 8 | b] */
extern long disp_blend_ref;         /* 0x438BF4 */
void display_2AD150(void);                         /* 0x2AD150 (G5a) */
void display_2AD184(void);                         /* 0x2AD184 (G5a) */
void display_2AD19C(void);          /* 0x2AD19C halve the canvas for low res */
void display_2AD1DC(void);          /* 0x2AD1DC double the canvas back */
void display_2AD248(uchar *d, uchar *s, long n, long row, uchar *t);
void display_2AD2F0(uchar *d, uchar *s, long n, uchar *t, uchar *l);
void display_2AD338(uchar *d, uchar *s, long n, long row, uchar *t, uchar *l);
void display_2AD414(grs_bitmap *bm);
void display_2AD4B8(grs_bitmap *bm);
void display_2AD5A0(grs_bitmap *bm);
void display_2AD6D4(grs_bitmap *bm);
void display_2AD870(uchar *tab);    /* 0x2AD870 build the blend table */


/* ------------------------------------------------------------------ terrrast.c (M091) */
/* A terrain vertex as terrrast_2AE004 emits it (0x27 bytes; weather_2B1218 projects it). */
typedef struct {
    uchar f00[8];
    fix r, g, b;        /* +0x08 colour components (colour mode, ff_cam.g_5c == 3) */
    fix shade;          /* +0x14 (15 - 2 * shade bits) . 0x8000 */
    fix x, y, z;        /* +0x18 view space */
    uchar f24;
    uchar kind;         /* +0x25 5, or 0x10 with a colour */
    uchar variant;      /* +0x26 cell variant bits */
} TrVert;
LG_STATIC_ASSERT(sizeof(TrVert) == 0x27, TrVert_size);
/* Strip extents passed to terrrast_2AE004: arrays of 8-byte {first, last} pairs. */
typedef struct { long first, last; } TrSpan;
/* The strip record (TerrainStripBuild's): only the fields terrrast_2AE004 reads. */
typedef struct {
    uchar f00[0x2E];
    ushort octant;      /* +0x2E one bit per 45 degree sector of the heading, plus the number */
    uchar f30[4];
    ulong n34;          /* +0x34 spans in a / b */
    ulong n38;          /* +0x38 spans in c */
    long rad;           /* +0x3C circle radius: tcache_tab[rad - 1] */
} TrStrip;
extern long *tr_idx;                /* 0x438C34 64 x 64 vertex numbers ([y & 63][x & 63]) */
extern TrVert *tr_verts;            /* 0x438C38 vertex buffer */
extern void *tr_buf30;              /* 0x438C30 0x7D00 bytes */
void terrrast_2ADF00(void);         /* 0x2ADF00 allocate the buffers */
void terrrast_2ADF34(void);         /* 0x2ADF34 free them */
void terrrast_2ADF58(uchar *cell, TrVert *v); /* 0x2ADF58 vertex colour */
fix terrrast_2ADFD4(uchar *cell);   /* 0x2ADFD4 vertex shade (unreferenced) */
void terrrast_2AE004(TrStrip *s, TrSpan *a, TrSpan *b, TrSpan *c); /* 0x2AE004 emit vertices */

/* ------------------------------------------------------------------ haze.c (M095) */
/* The terrain ray caster ("RAY_SITE"): walks the map outward from the viewer in rings, finds
 * the visible stretches of terrain per ring (bisection against the view frustum), hands them to
 * the strip renderer (strip_2AC19C) and collects the objects on the visible squares into the
 * strips' object lists (viewdist_2A1160 / 2A13B8).  Objects farther than the ring are parked in
 * per-column buckets (hz_head) until the ring reaches them. */
typedef struct { short ent; short next; } HzNode;
extern HzNode hz_nodes[0x40];       /* 0x43BF60 parked objects (node 0: free list head) */
extern short hz_head[0x80];         /* 0x43C060 per bucket (ring distance) first node */
extern uchar hz_init;               /* 0x35CCF4 1: hz_nodes still to be set up */
extern long hz_strides[3];          /* 0x35CCEC cell row stride: fine, coarse, (1) */
extern FfView hz_view;              /* 0x43CADC copy of the view */
extern FfCamera hz_cam;             /* 0x43CB50 copy of the camera */
extern fix hz_mat[9];               /* 0x4287E8 view rotation (unscaled; the ffpipe's) */
extern uchar hz_log2[0x100];        /* 0x43C960 copy of the camera's log2 table */
extern long hz_divx[0x100];         /* 0x43C560 copy of div_x */
extern long hz_divy[0x100];         /* 0x43C160 copy of div_y */
extern uchar hz_rad1[0x400];        /* 0x43BB60 haze radius -> light level */
extern uchar hz_rad2[0x400];        /* 0x43B760 its copy (memory trash check) */
extern long hz_bands[8];            /* 0x43B730 ring distances where the step doubles */
extern long hz_ends[4];             /* 0x43B750 first / last span of the ring (x, y) */
extern long hz_nbands;              /* 0x43CA64 */
extern long hz_wy;                  /* 0x43CA68 walk step (y) */
extern long hz_ca6c;                /* 0x43CA6C cell step of the walk */
extern uchar *hz_cell;              /* 0x43CA70 current cell of the walk */
extern long hz_mask;                /* 0x43CA74 coordinate mask of the current step */
extern long hz_ca78, hz_ca7c;       /* 0x43CA78, 0x43CA7C view x / z step of the walk */
extern long hz_wx;                  /* 0x43CA80 walk step (x) */
extern long hz_px, hz_py;           /* 0x43CA84, 0x43CA88 walk position */
extern long hz_ca8c;                /* 0x43CA8C view y step of the walk */
extern fix hz_ca90, hz_ca94, hz_ca98; /* 0x43CA90 view translation of the map origin */
extern long hz_c9c, hz_ca0, hz_ca4, hz_ca8; /* 0x43CA9C x / y limits of the coarse map */
extern long hz_stride;              /* 0x43CAAC cell row stride */
extern fix hz_lasty;                /* 0x43CAB0 last screen y */
extern long hz_shade;               /* 0x43CAB4 */
extern long hz_step;                /* 0x43CAB8 ring step (1, 2, 4 ...) */
extern long hz_col;                 /* 0x43CABC last screen column */
extern long hz_nheads;              /* 0x43CAC0 */
extern fix hz_vx, hz_vz;            /* 0x43CAC4, 0x43CAC8 view x / z of the walk */
extern long hz_ncols;               /* 0x43CACC canvas width */
extern fix hz_sx, hz_sy;            /* 0x43CAD0, 0x43CAD4 projection (thunder_2B2930) */
extern long hz_ring;                /* 0x43CAD8 */
extern long hz_dist6, hz_dist5, hz_dist4; /* 0x43CB38, 0x43CB3C, 0x43CB40 */
extern fix hz_vy;                   /* 0x43CB44 view y of the walk */
extern fix hz_cy, hz_cx;            /* 0x43CB48, 0x43CB4C screen centre */
extern uchar hz_first;              /* 0x43CC0A first span of the ring */
extern uchar hz_near;               /* 0x43CC0B ring still within dist[4] */
extern uchar hz_vis;                /* 0x43CC0C last point visible */
extern uchar hz_fine;               /* 0x43CC0D step 1: shade from one cell */
extern uchar hz_pos;                /* 0x43CC0E main axis positive */
extern uchar hz_xmajor;             /* 0x43CC0F the view looks along x */
extern uchar hz_coarse;             /* 0x43CC10 step >= 4: coarse map */
extern GridXY hz_key;               /* 0x43CC12 */
extern fix hz_play[3];              /* 0x43CC20 camera playback position */
extern short hz_play_h, hz_play_p;  /* 0x43CC2C, 0x43CC2E */
extern long hz_play_cam;            /* 0x43CC30 */
extern long hz_playing;             /* 0x43CC34 */
extern long *strip_dst;             /* 0x427E18 strip_2AC19C column pointer */
extern fix strip_y0, strip_dy;      /* 0x427E20, 0x427E24 */
extern long strip_shade;            /* 0x35C93C */
extern long strip_neg;              /* 0x35C940 */
extern long hz_clut;                /* 0x427F48 canvas clut (fill type 1) */
extern uchar *hz_shade_row;         /* 0x42875C planet_shade row of the ring */
extern uchar ff_cams_390010[3][0xBA]; /* 0x390010 cameras (FfCamera) */
extern long hz_level;               /* 0x43CA60 light level of the ring (g5_c.h: short game_43CA60) */
extern long vd_center_col;          /* 0x427F40 (g5_c.h) */

uchar haze_2B3980(short ent);
void haze_2B399C(short ent);
void haze_2B3A54(void);
void haze_2B3A7C(void);
void haze_2B3A94(short ent, long bucket);
void haze_2B3B20(long bucket);
void haze_2B3BA4(long x, long y, long d);
void haze_2B3D44(long x, long y);
long haze_2B3D7C(long x, long y);
void haze_2B3E9C(long col, long b, long shade);
uchar *haze_2B3FD0(long x, long y);
void haze_2B402C(void);
uchar haze_2B40C8(long x, long y);
uchar haze_2B4198(long x, long y, fix *sx, fix *sy);
void haze_2B42C0(long x, long y, long dx, long dy);
uchar haze_2B4428(void);
void haze_2B44FC(long c0, long c1, long dx, long dy);
long haze_2B458C(long a, long b, long y);
long haze_2B45D8(long a, long b, long y);
long haze_2B4630(long a, long b, long x);
long haze_2B4674(long a, long b, long x);
long haze_2B46D0(uchar *cell);
long haze_2B4724(uchar *cell);
long haze_2B4748(uchar *cell);
uchar haze_2B4780(long col, fix y);
long haze_2B47C0(long a, long b, long y);
long haze_2B486C(long a, long b, long y);
long haze_2B4918(long a, long b, long x);
long haze_2B49C4(long a, long b, long x);
void haze_2B4A70(long a, long b, long y);
void haze_2B4B6C(long a, long b, long y);
void haze_2B4C78(long a, long b, long x);
void haze_2B4D88(long a, long b, long x);
void haze_2B4E8C(void);
void haze_2B4ED0(void);
void haze_2B4EFC(long a, long b, long c, uchar up);
void haze_2B4F68(long a, long b, long c, uchar up);
long haze_2B4FD0(long a, long b, long c, uchar lo, long hi, long y, uchar up);
long haze_2B50DC(long a, long b, long y, uchar up);
long haze_2B51AC(long a, long b, long c, uchar lo, long hi, long x, uchar up);
long haze_2B52D0(long a, long b, long x, uchar up);
void haze_2B53B0(void);
void haze_2B5448(long x, long y);
void haze_2B5484(FfCamera *cam, FfView *view, void *strips); /* 0x2B5484 (G5a) */
void haze_2B5B70(FfView *v);
long haze_2B5BA4(char *name);

/* callees in other batches (orig rows in symbols-g5-a.csv until they are decompiled) */
int crt_2FB71F(void *fp, const char *fmt, ...);            /* 0x2FB71F fscanf (cdecl) */

#pragma pack(pop)
#endif
