/* g5_c.h -- declarations for worker g5c (batch G5, terrain and render pipeline):
 *   M074 planet memory / PlanetUse   (0x299590-0x299E6C, src/game/terrain/planet.c)
 *   M079 view distance / visibility  (0x2A0D70-0x2A322F, src/game/render/viewdist.c)
 *   M083 terrain raster strip builder (0x2A8880-0x2AB0FF, src/game/terrain/terrstrp.c)
 *   M085 terrain strips              (0x2AB800-0x2AC52F, src/game/terrain/strip.c)
 *   M086 BlastFindTmap               (0x2AC530-0x2AC79F, src/game/render/blasttm.c)
 *
 * Names: the map's placeholders are kept for functions other modules call; meanings are in the
 * file headers.  Globals are named after their use and bound to their original addresses through
 * symbols-g5-c.csv.  Structs are byte-packed.
 */
#ifndef TN_G5_C_H
#define TN_G5_C_H
#include "lg.h"
#include "fix.h"
#include "game.h"
#include "g5_a.h"      /* map record, map_293EFC, map_vary_mask; g3_a.h terrain tables; g1_c.h */
#include "g5_b.h"      /* FfView, FfCamera, ff_view, ff_cam, hview_cols, strip_427DE0 */
#pragma pack(push, 1)

/* ------------------------------------------------------------------ M074 planet (planet.c) */
/* The planet file (named by the map record) holds the terrain textures, palettes and shading
 * tables of a world.  PlanetUse loads it: the variable-sized parts go into one static heap
 * (planet_heap, at most 0x45948 bytes), the fixed ones into their own buffers. */
extern ulong planet_mem_left;            /* 0x41DD40 bytes left in the planet heap */
extern uchar *planet_mem_ptr;            /* 0x41DD44 next free byte */
extern uchar *planet_mem_base;           /* 0x41DD48 start of the allocation */
extern uchar *planet_shade;              /* 0x41DD4C shading table (planet_tab37) */
extern uchar **planet_tex_rows;          /* 0x41DD50 per texture: res 0x31 data + 0x100 * idx */
extern uchar *planet_res2c;              /* 0x41DD54 res 0x2C */
extern uchar *planet_res47;              /* 0x41DD5C res 0x47 */
extern uchar *planet_res46;              /* 0x41DD64 res 0x46 */
extern uchar *planet_res31;              /* 0x41DD68 res 0x31 (0x100 bytes per entry) */
extern uchar *planet_res30;              /* 0x41DD6C res 0x30 (0x1000 bytes per entry) */
extern uchar *planet_res2f;              /* 0x41DD70 res 0x2F */
extern uchar *planet_res2e;              /* 0x41DD74 res 0x2E */
extern short planet_pal_count;           /* 0x41DD8C palette entries in res 0x32 */
extern uchar planet_pal_part[0xA5];      /* 0x41DD8E palette entries 0xB0..0xE6 (rgb) */
extern uchar planet_tex_count;           /* 0x41DE35 res 0x29: textures (<= 0x40) */
extern uchar planet_res2a;               /* 0x41DE36 res 0x2A */
extern uchar *planet_tex[0x40];          /* 0x41DC40 per texture: res 0x30 data + 0x1000 * idx */
extern uchar planet_pal[0x300];          /* 0x417028 res 0x32 palette */
extern uchar planet_tab3b[0x100];        /* 0x417328 */
extern uchar planet_tab3c[0x100];        /* 0x417428 */
extern uchar planet_tab3d[0x100];        /* 0x417528 */
extern uchar planet_tab3e[0x100];        /* 0x417628 */
extern uchar planet_tab40[0x100];        /* 0x417728 */
extern uchar planet_tab3f[0x100];        /* 0x417828 */
extern uchar planet_tab41[0x100];        /* 0x417928 */
extern uchar planet_shaded[3][0x1800];   /* 0x417A28 planet_light through planet_shade + 0x200 * (k + 1) */
extern uchar planet_light[0x1800];       /* 0x41C228 res 0x35, 24 rows of 0x100 */
extern uchar planet_tab38[0x800];        /* 0x3CE5E0 */
extern uchar planet_tab3a[0x1100];       /* 0x3CEDE0 */
extern uchar planet_tab37[0x1000];       /* 0x3CFEE0 */
extern uchar planet_tab36[0x800];        /* 0x3D0EE0 */
extern uchar planet_heap[0x45948];       /* 0x3D16E0 */
extern int planet_res_file;              /* 0x35C028 open planet file, -1 none */
/* Resources stored under other batches' names: 0x2B game_41DD58, 0x34 terrain_cornerbits,
 * 0x43 game_41DA28, 0x44 terrain_typebits, 0x45 terrain_kindtab, 0x49 terrain_kinds (g3_a.h),
 * 0x48 at map_vary_mask (g5_a.h; 0x12 bytes: also path_41DD80, terrain_bitmask), res 0x28
 * bytes 0 / 1 game_41DE34 / game_41DE33 (g1_c.h, music set). */

void planet_299590(long size);          /* 0x299590 start an allocation of size bytes */
void planet_2995DC(void);               /* 0x2995DC forget the allocation */
uchar *planet_2995E8(ulong size);        /* 0x2995E8 take size bytes */
long PlanetUse(void *planet);       /* 0x299624 */
void planet_299C34(void);               /* 0x299C34 build planet_shaded */
void planet_299CC4(void);               /* 0x299CC4 (unused) brightened colour rows of planet_light */
int planet_299E04(void);                /* 0x299E04 set the planet palette */
void planet_299E4C(void);               /* 0x299E4C forget the planet file */

/* callees in other G5 shares (original code for now; map_293EFC in g5_a.h) */

/* ------------------------------------------------------------------ M079 viewdist.c */
/* A node of a strip's circular object list (pool vd_nodes, 0x190 nodes; 0x1000 in the port's
 * sharp view).  ent != 0: an entity; ent == 0: a map tile object (template, placement, global
 * object id). */
typedef struct VdNode {
    short ent;               /* +0 */
    ushort tmpl;             /* +2 tile_tmpls index */
    ushort place;            /* +4 tile_places index */
    ushort id;               /* +6 object id (tile_3C9E1C bit, vd_tile_info index) */
    struct VdNode *next;     /* +8 */
} VdNode;
LG_STATIC_ASSERT(sizeof(VdNode) == 12, VdNode_size);

typedef struct { short a, b; } VdInfo;   /* a = span (>= 0 a y, < 0 -index in vd_spans), b = light level */

/* A terrain strip (bin) of the strip builder, 0x20 bytes; the array ends at strip_427DE0. */
typedef struct {
    VdNode *list;            /* +0x00 tail of the object list */
    uchar f04[0xC];
    short x, y;              /* +0x10 map square */
    ushort key[4];           /* +0x14 corner vertex numbers (y & 63) << 6 | (x & 63): (x, y),
                                (x + 1, y), (x + 1, y + 1), (x, y + 1) */
    short col;               /* +0x1C canvas column (or span) */
    uchar count;             /* +0x1E nodes in list */
    uchar band;              /* +0x1F camera band (terrstrip_2AA2B4) */
} TerrBin;
LG_STATIC_ASSERT(sizeof(TerrBin) == 0x20, TerrBin_size);

extern uchar vd_dist_tab[9][0x80];       /* 0x41ECC0 res 0x560 + class: view distance per type */
extern grs_canvas vd_canvas;             /* 0x41EC80 save canvas for the viewer's strip */
extern ushort vd_spans[0x800];           /* 0x41F140 terrain silhouette spans (lo, n, y[n]) */
extern VdNode *vd_nodes;                 /* 0x420140 */
extern long vd_420144, vd_420148;        /* 0x420144, 0x420148 zeroed per frame */
extern VdInfo *vd_tile_info;             /* 0x42014C per tile object id */
extern VdInfo *vd_ent_info;              /* 0x420150 per entity */
extern short vd_draw_arg;                /* 0x420154 */
extern ushort vd_node_count;             /* 0x420156 */
extern ushort vd_spans_used;             /* 0x420158 */
/* vd_center_col (0x427F40) and hz_level (0x43CA60, light level of the span; the viewdist code
 * reads its low word) are declared in g5_a.h */
extern uchar vd_pick_cls[];              /* 0x35C110 per class: pickable by the probe */

void viewdist_2A0D70(void);
uchar viewdist_2A0E14(uchar *p, long d);
uchar viewdist_2A0E54(GameEntity *e);
void viewdist_2A0EC8(void);
void viewdist_2A0F48(void);
long viewdist_2A0F68(long a, GameEntity *e);
void viewdist_2A10E8(TerrBin *b, short ent);
void viewdist_2A1160(TerrBin *b, short ent, long col, long a);
void viewdist_2A11B0(TerrBin *b, short ent, long x, long y, long unused);
void viewdist_2A13B8(TerrBin *b, uchar flag);
fix viewdist_2A1840(VdNode *n);
long viewdist_2A18F8(VdNode **a, VdNode **b);
void viewdist_2A197C(TerrBin *b);
void viewdist_2A1CC8(void);
void viewdist_2A1D80(short *r, long lim);
void viewdist_2A1E84(short *r, long lim);
ulong viewdist_2A2088(ushort *p);
uchar viewdist_2A20C4(fix *p);
uchar viewdist_2A213C(fix x, fix y, fix z);
uchar viewdist_2A2204(long obj, fix *a, fix *b, fix *c);
uchar viewdist_2A2284(GameEntity *e, uchar fl, long span, long lvl, short arg);
void viewdist_2A250C(void *p, long n);
long viewdist_2A2864(uchar i);
long viewdist_2A28D0(ulong pt, fix *x, fix *y, fix *z, uchar *cam, void *view);
uchar viewdist_2A2CA8(fix *t, fix px, fix py, fix pz);
long viewdist_2A2D88(short a, short b);
long viewdist_2A3158(short a);
long viewdist_2A3190(VdNode **arr, long n, long i);

/* callees in G6a / G7 (original code; orig rows in symbols-g5-c.csv) */

/* ------------------------------------------------------------------ M085 strip.c */
/* The message of strip_2ABB10 (on the caller's stack, 0x28 bytes). */
typedef struct {
    uchar code;              /* +0x00 0 set up (view), 1 free, 2..5 start at a square (scale 1/2/4/8),
                                6 step, 7 project */
    FfView *view;            /* +0x01 */
    uchar *log2;             /* +0x05 floor(log2) table (FfCamera.log2) */
    long *div_x, *div_y;     /* +0x09, +0x0D (FfCamera.div_x / div_y) */
    long cx, cy;             /* +0x11, +0x15 screen centre */
    uchar near;              /* +0x19 step through the 256-entry tables */
    uchar g_1a;
    uchar dir;               /* +0x1B neighbour 0..8 */
    long x, y;               /* +0x1C, +0x20 map square */
    long n;                  /* +0x24 height (index into the tables when near) */
} StripMsg;
LG_STATIC_ASSERT(sizeof(StripMsg) == 0x28, StripMsg_size);

/* The column rasteriser state read by thunder_2B29B0 / 2B2AD8 (asm, G6a), 0x30 bytes. */
typedef struct {
    uchar *tex;              /* +0x00 texture row (0x100 bytes) */
    long u;                  /* +0x04 start texel */
    long *ybuf;              /* +0x08 per column: top y */
    uchar *dst;              /* +0x0C */
    fix v, dv;               /* +0x10, +0x14 */
    uchar *light;            /* +0x18 */
    uchar *shade;            /* +0x1C */
    long *rows;              /* +0x20 row offsets */
    long row;                /* +0x24 bytes per row */
    long fade;               /* +0x28 light level << 8 + 0x80 while fading */
    long fade_step;          /* +0x2C */
} StripRas;
LG_STATIC_ASSERT(sizeof(StripRas) == 0x30, StripRas_size);

extern StripRas strip_ras;               /* 0x427E10 */
extern uchar strip_row[0x100];           /* 0x427E40 texture row with blast marks */
extern long *strip_rows;                 /* 0x427F44 camera row offsets */
extern long strip_row_len;               /* 0x427F4C canvas bytes per row */
extern uchar *strip_bits;                /* 0x427F50 canvas bits */
extern long strip_recip[0x101];          /* 0x427F54 0x10000 / i */
extern long strip_recip2[0x101];         /* 0x428358 the same (entry 0 unset) */
extern fix strip_cur_x, strip_cur_z;     /* 0x428760, 0x428764 view-space point */
extern fix strip_stepz[9];               /* 0x428768 per neighbour direction */
extern fix strip_stepy[9];               /* 0x42878C */
extern fix strip_stepx[9];               /* 0x4287B0 */
extern fix strip_cur_y;                  /* 0x4287D4 */
extern fix *strip_tz, *strip_ty, *strip_tx; /* 0x4287D8, 0x4287DC, 0x4287E0 per height: i * m[7|4|1] * 16 */
/* the copy of the view matrix at 0x4287E8 is g5_a.h's hz_mat[9] */
extern uchar strip_mark;                 /* 0x42880C mark the columns (debug) */
extern uchar strip_shift;                /* 0x42880D scale of strip_stepx/y/z (1, 2, 4, 8) */
/* g5_a.h names used here: strip_shade (0x35C93C last light level, -1 none), strip_neg (0x35C940),
 * hz_clut (0x427F48 shade table override), hz_shade_row (0x42875C shade row); its strip_dst,
 * strip_y0, strip_dy (0x427E18, 0x427E20, 0x427E24) are strip_ras.ybuf, .v, .dv. */
extern long strip_recip_sum;             /* 0x35C944 */
extern long strip_recip_ok;              /* 0x35C948 */

void strip_2AB800(fix *t0, fix *t1, fix *t2, fix s0, fix s1, fix s2); /* 0x2AB800 */
void strip_2AB8BC(StripMsg *m, long *sx, long *sy);   /* 0x2AB8BC project */
void strip_2ABA1C(long cur, long want);               /* 0x2ABA1C rescale the steps */
void strip_2ABB10(void *msg, long *sx, long *sy);         /* 0x2ABB10 (G5c); sx, sy (edx, ebx) are passed through to strip_2AB8BC for message 7 */
void strip_2AC124(void);                                    /* 0x2AC124 (G5c) */
void strip_2AC4E4(FfCamera *cam);                           /* 0x2AC4E4 (G5c) */
uchar strip_2AC098(short *p);                         /* 0x2AC098 (no callers) */
void strip_2AC0C0(void);                              /* 0x2AC0C0 reciprocal tables */
uchar *strip_2AC128(long tex);                        /* 0x2AC128 row with blast marks */
void strip_2AC15C(uchar *p, long n, fix y, fix dy);   /* 0x2AC15C mark columns */
void strip_2AC19C(long a, long x, long tex, long lvl); /* 0x2AC19C draw a column run */
void strip_2AC33C(void);                              /* 0x2AC33C shade table */
void strip_2AC378(long n);                            /* 0x2AC378 run with fading */
void strip_2AC4B8(void);                              /* 0x2AC4B8 (no callers) */

/* G6a asm column rasterisers (original; they take the state in ebx and the count in ecx and
 * keep only ebp: called through these prototypes, eax / edx are don't-cares) */

/* ------------------------------------------------------------------ M086 blasttm.c */
typedef struct {
    uchar *buf;              /* +0 0x1000-byte texture copy */
    ulong stamp;             /* +4 blast_clock when last used */
    uchar owner;             /* +8 planet texture (0xFF none) */
    uchar g_9[7];
} BlastTex;
LG_STATIC_ASSERT(sizeof(BlastTex) == 0x10, BlastTex_size);

extern uchar blast_map[0x40];            /* 0x428810 slot of each planet texture (0 none) */
extern BlastTex blast_cache[5];          /* 0x428850 slots 1..4 (0 unused) */
extern uchar blast_noise[0x100];         /* 0x4288A0 */
extern uchar blast_noise_tmp[0x100];     /* 0x4289A0 */
extern ulong blast_clock;                /* 0x428AA0 */

void blast_2AC530(void);                 /* 0x2AC530 set up the cache */
void blast_2AC5B0(void);                 /* 0x2AC5B0 free it */
uchar *BlastFindTmap(long tex);          /* 0x2AC5BC */
void blast_2AC6D0(void);                 /* 0x2AC6D0 random pixels of frame 0x48C0002 */

/* ------------------------------------------------------------------ M083 terrstrp.c */
/* The strip record TerrainStripBuild fills (0x40 bytes; g5_a.h's TrStrip is its tail). */
typedef struct {
    ushort fov;              /* +0x00 camera fov_x */
    fix px, py;              /* +0x02 viewer position inside its square (low words) */
    fix d[2][2];             /* +0x0A near edge vectors (side 0 / 1: dx, dy) */
    fix e[2][2];             /* +0x1A edge directions (side 0 / 1: dx, dy) */
    fix f2a;                 /* +0x2A far distance (-1.0 when the circle misses) */
    ushort oct[3];           /* +0x2E octant of the heading and the two next to it:
                                bit (8 + n) | n */
    ulong n34;               /* +0x34 rings in table B */
    ulong n38;               /* +0x38 rings in table C */
    long rad;                /* +0x3C rings (camera radius) */
} TsRec;
LG_STATIC_ASSERT(sizeof(TsRec) == 0x40, TsRec_size);
typedef TrSpan TsSpan;                   /* {first, last} per ring */
typedef struct { TerrBin *bin; long x, y; uchar band; } TsCtx;   /* terrstrip_2AA2B4 */
typedef struct { uchar g[2]; ushort count; uchar *data; } TsPath; /* 3-bit direction pairs */
typedef struct { TsPath *path; uchar *cur; short x, y; ushort k; uchar back; } TsIter;

extern long ts_nbr[8];                   /* 0x35C85C cell offsets of the 8 neighbours */
extern long ts_dirx[8], ts_diry[8];      /* 0x35C87C, 0x35C89C direction steps */
extern long ts_ring_dx[8], ts_ring_dy[8]; /* 0x35C8BC, 0x35C8DC next ring per octant */
extern long ts_step_dx[8], ts_step_dy[8]; /* 0x35C8FC, 0x35C91C along a ring per octant */
extern short strip_count;                /* 0x427DE4 bins built */
/* strip_427DE0 (pool) is in g5_b.h */

long terrstrip_2A8880(long x, long y);   /* 0x2A8880 lowest height around a cell */
long terrstrip_2A88FC(long x, long y);   /* 0x2A88FC highest */
void terrstrip_2A8978(FfCamera *cam, FfView *view, TsRec *r);   /* 0x2A8978 */
uchar terrstrip_2A8DCC(TsIter *it);      /* 0x2A8DCC (no callers) */
ushort terrstrip_2A8FA0(long *out, TsRec *r, uchar dir, uchar side, uchar flag); /* 0x2A8FA0 */
void terrstrip_2A98AC(TsRec *r, TsSpan *a, TsSpan *b, TsSpan *c); /* 0x2A98AC */
uchar terrstrip_2AA2B4(TsCtx *t);        /* 0x2AA2B4 add a bin */
long terrstrip_2AB0C0(long n);           /* 0x2AB0C0 (no callers) */
void TerrainStripBuild(FfCamera *cam, FfView *view, void *strips); /* 0x2AA398 (G5c) */
void terrstrip_2AB0D4(void);                                /* 0x2AB0D4 (G5c) */
void terrstrip_2AB0E8(void);                                /* 0x2AB0E8 (G5c) */

#ifndef TN_WATCOM_ABI
/* port/video/sharp3d.c: a bigger span table while the sharp 3D view is drawn */
extern ushort *port_vd_spans;
extern long port_vd_spans_max;
#define vd_spans port_vd_spans
#endif

#pragma pack(pop)

#endif
