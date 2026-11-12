/* g6_b.h -- declarations for worker g6b (batch G6): class 0 object damage and the designator
 * strike flash (M061 flashfx.c), weather (M092 weather.c) and the LG skeleton renderer
 * (skel/render.c, skelrend.c).
 * Names are ours unless they come from the map, an assert string or an error string.
 * Globals are bound to their original addresses through symbols-g6-b.csv.
 */
#ifndef TN_G6_B_H
#define TN_G6_B_H
#include "lg.h"
#include "fix.h"
#include "game.h"
#include "2d.h"
#include "3d.h"
#include "res.h"
#include "support.h"
#include "g3_a.h"      /* g1_*, g2_* (objects, kill, damage), g3_c.h */
#include "t1_c.h"      /* lg_memset, lg_qsort */
#include "t2_a.h"      /* water_scan_edges */
#include "g4_c1.h"     /* aiacc_25AE98 */
#include "g6_a.h"      /* thunder_*; g5_c.h TerrBin, viewdist_*, planet_tex; g5_b.h ff_view, ff_cam,
                          hview_cols, strip_427DE0, ff_bolt*; g5_a.h TrVert, tr_*, GMAP */
#pragma pack(push, 1)

/* ------------------------------------------------------------------ M061 flashfx.c */
/* Class 0 pool records (game_cls0, 0x3BA080, 9 bytes; record 0 is the list header):
 * +0 word: entity in bits 0..14 (sign-extended from bit 14), bit 15 = free/dead flag
 * +2 short next record, +6 byte "destroyed" flag, +7 ushort hit points left. */
#define G6B_C0_W0(i)   (*(ushort *)&game_cls0[(short)(i)][0])
#define G6B_C0_NEXT(i) (*(short *)&game_cls0[(short)(i)][2])
#define G6B_C0_DEAD(i) (game_cls0[(short)(i)][6])
#define G6B_C0_HP(i)   (*(ushort *)&game_cls0[(short)(i)][7])
#define G6B_IDX15(w)   ((short)((short)((ushort)(w) << 1) >> 1))
#define G6B_ENT(i)     (game_entities[(short)(i)])

extern long stat_kills_359718;        /* 0x359718 player kills of class 0 objects (stats) */
extern ulong flash_end_time;          /* 0x35BF34 game time the designator flash ends */
extern uchar flash_saved_pal[0x300];  /* 0x397130 palette before the flash */
extern uchar flash_from_pal[0x300];   /* 0x397430 palette effect start */
extern uchar flash_white_pal[0x300];  /* 0x397730 all 0xFF */
extern uchar flash_team_type;         /* 0x397A30 team rules (game_396E18 == 1): type of the
                                         last friendly attacker */
extern uchar flash_pal_effect;        /* 0x397A31 palette effect id */
extern uchar *drawobj_cls0_models;    /* 0x41E224 RefLock(0x55B0000): 8 bytes per class 0 type,
                                         +4 = destroyed model (0: none) (G6a drawobj) */

long flash_28DE20(long *a, long *b);                                  /* 0x28DE20 qsort cmp */
void flash_28DE40(ulong dt);                                          /* 0x28DE40 */
void flash_28E040(long ent, long attacker, long mode, long amount);   /* 0x28E040 */
void flash_28E2FC(void *msg);                                         /* 0x28E2FC */
uchar flash_28E350(long ent, long b);                                 /* 0x28E350 */
uchar flash_28E5F0(uchar type, long mode, long amount);               /* 0x28E5F0 */
void flash_28E630(void);                                              /* 0x28E630 */
void flash_28E6C0(uchar type, long ent, long attacker, long mode, long amount,
                  uchar *hit, uchar *kill);                           /* 0x28E6C0 */

/* ------------------------------------------------------------------ M092 weather.c */
/* Rain / snow drops drawn over a camera view (wx_views[], one per camera), lightning storms,
 * the terrain triangle clipper (near plane 0x80 and the four frustum sides 1, 8, 2, 4) and
 * the terrain bin drawing (bins sorted into 9 groups around the building mesh rectangle). */
typedef struct {
    uchar *bits_a;       /* +0x00 drop bitmap (state 0) */
    uchar *bits_b;       /* +0x04 drop bitmap (state 1) */
    uchar size;          /* +0x08 bitmap width = height */
    uchar color;         /* +0x09 pixel colour (state 2, or no bitmap) */
    signed char dx0, dx1;     /* +0x0A drift per step: x range (scaled by the view width / 300) */
    signed char dy0, dy1;     /* +0x0C y range */
    ulong snd;           /* +0x0E looping sound id (0: none) */
} WxType;
LG_STATIC_ASSERT(sizeof(WxType) == 0x12, WxType_size);

typedef struct {
    short x, y;          /* +0 screen position << 4 (y << 3 with a doubled sky) */
    signed char depth;      /* +4 16..32 (drawn in band ((3 - b) * 4 + 16 .. + 3)) */
    uchar state;         /* +5 0 / 1 bitmap a / b, 2 single pixel */
} WxDrop;

typedef struct {
    WxType t;            /* +0x00 copy of wx_types[kind] (dx / dy scaled) */
    WxDrop *drops;       /* +0x12 0 free, -1 reserved (no drops yet) */
    short count;         /* +0x16 */
    ushort heading;      /* +0x18 last view heading */
    fix lastx, lasty;    /* +0x1A last view position (0xFFFF0000: none yet) */
    long turn;           /* +0x22 camera zx: drift per heading change */
    ulong time;          /* +0x26 game time of the last update */
    short x0, y0, x1, y1;/* +0x2A drop area (screen units) */
    uchar f32;           /* +0x32 */
} WxView;
LG_STATIC_ASSERT(sizeof(WxView) == 0x33, WxView_size);

/* The two triangles of a terrain bin (tr_buf30 + bin * 0x40: A at +0, B at +0x20). */
typedef struct { long n; TrVert *v[7]; } WxTri;
/* TerrBin (g5_c.h) fields used here: +4 triangle A, +8 triangle B, +0xC next bin of the group,
 * byte +0x1C flags: 1 A visible, 2 B visible, 4 the diagonal goes the other way. */
#define G6B_BIN_A(b)    (*(WxTri **)((uchar *)(b) + 4))
#define G6B_BIN_B(b)    (*(WxTri **)((uchar *)(b) + 8))
#define G6B_BIN_NEXT(b) (*(TerrBin **)((uchar *)(b) + 0xC))
#define G6B_BIN_FL(b)   (*((uchar *)(b) + 0x1C))
#define G6B_BINS        ((TerrBin *)strip_427DE0)
/* camera band mode byte (ff_cam + 0x5C + band * 12): index of terr_poly_fns */
#define G6B_BAND_MODE(k) (((uchar *)&ff_cam)[0x5C + (k) * 12])
typedef uchar (*TerrPolyFn)(TrVert **pts, long n, long x, long y);

extern long wx_snd_chan;              /* 0x35CA88 rain / wind loop channel (-1 none) */
extern ulong wx_thunder_wait;         /* 0x35CA8C time to the next thunder storm phase */
extern WxType wx_types[2];            /* 0x35CAA4 by game_35CADA (0 rain, 1 snow) */
extern RndStream wx_rnd[1];           /* 0x35CAE0 */
extern fix wx_snd_speed;              /* 0x35CAEC distance per thunder delay unit */
extern long wx_tex_acc;               /* 0x35CAF0 water texture animation time */
extern ulong wx_last_time;            /* 0x35CAF4 */
extern fix wx_near_z;                 /* 0x35CAF8 near clipping plane */
extern long wx_band_dist[3];          /* 0x35CAFC drop bands: draw band b before bins nearer */
extern long wx_draw_order[9][9];      /* 0x35CB08 bin group order per viewer region */
extern long wx_rows[17];              /* 0x35CC4C shade rows (weather_2B2598) */
extern void *terr_poly_fns[8];        /* 0x35C0E0 terrain polygon routines by band mode
                                         (entry 1 = ffpipe_35C0E4, g5_b.h) */
extern grs_bitmap terr_tex_bm;        /* 0x41EB40 terrain texture bitmap */
extern grs_bitmap wx_bm[2];           /* 0x438C40 drop bitmaps */
extern ulong wx_bolt_tick;            /* 0x438C74 */
extern fix wx_cos[32];                /* 0x438C7C all = cos(pitch) */
extern long wx_h16;                   /* 0x438CFC view height << 4 */
extern fix wx_sin[32];                /* 0x438D04 all = sin(pitch) */
extern long wx_cx16;                  /* 0x438D84 view centre x << 4 */
extern long wx_cy3;                   /* 0x438D88 wx_h16 / 3 */
extern WxView wx_views[4];            /* 0x438D8C */
extern ushort wx_heading;             /* 0x438E58 pitch of wx_sin / wx_cos */
extern uchar wx_bolt_done;            /* 0x438E5A lightning bolt fully grown */
extern uchar wx_thunder_vol;          /* 0x438E5B */
extern TrVert *wx_clip_next, *wx_clip_prev; /* 0x438E60, 0x438E64 edge being clipped */
extern TrVert *wx_clip_pool;             /* 0x438E68 0x320 temporary vertices */
extern TrVert *wx_clip_new;              /* 0x438E6C last new vertex */
extern fix wx_clip_d[3];                 /* 0x438E70 edge delta x, y, z */
extern fix wx_clip_t;                    /* 0x438E7C intersection parameter */
extern long wx_clip_count;               /* 0x438E80 temporary vertices used */
extern uchar wx_region;               /* 0x438E94 viewer position against the mesh rectangle */
extern TerrBin *wx_bins[9];           /* 0x438E95 bin groups (wx_bins[4]: inside the rectangle) */
extern uchar wx_grid_mode;            /* 0x438EB9 draw the inside group flat (weather_2B2690) */
extern uchar wx_in_center;            /* 0x438EBA drawing the inside group */
extern uchar game_39A404;             /* 0x39A404 show the targeted unit's squares */

void weather_2AF4E0(FfView *pos, long idx);                  /* 0x2AF4E0 move the drops */
void weather_2AF958(void);                                   /* 0x2AF958 sound done callback */
void weather_2AF964(long h);                                 /* 0x2AF964 */
void weather_2AF9B8(long *ox, long *oy, WxDrop *p);          /* 0x2AF9B8 drop -> screen */
void weather_2AFA5C(long a, long b);                        /* 0x2AFA5C (G6b) */
void weather_2AFBD8(WxView *w, long scale);                  /* 0x2AFBD8 make the drops */
void weather_2AFD4C(long kind, long scale);                  /* 0x2AFD4C set the weather */
void weather_2AFE38(long idx, FfCamera *cam);                /* 0x2AFE38 resize a view */
long weather_2AFF54(FfCamera *cam, long b);                  /* 0x2AFF54 new view */
void weather_2AFFFC(long idx);                               /* 0x2AFFFC free a view */
void weather_2B003C(void);                                   /* 0x2B003C start a storm */
void weather_2B0090(fix x, fix y);                           /* 0x2B0090 thunder sound */
void weather_2B0158(ulong dt);                               /* 0x2B0158 (no callers) */
void weather_2B0178(void);                                   /* 0x2B0178 lightning strike */
void weather_2B0238(void);                                   /* 0x2B0238 */
void weather_2B0244(void);                                   /* 0x2B0244 storm step */
long weather_2B033C(void);                                   /* 0x2B033C */
void weather_2B0358(long dt);                                /* 0x2B0358 water animation */
void weather_2B0434(void);                                   /* 0x2B0434 per frame */
void weather_2B0520(void);          /* 0x2B0520 (G6b; the map lists it inside weather_2B0434) */
void weather_2B053C(void);          /* 0x2B053C (G6b) */
void weather_2B1218(TrVert *v);     /* 0x2B1218 (G6b) project a vertex */
uchar weather_2B0548(TrVert **in, long n, TrVert **out, long *pn); /* 0x2B0548 clip a polygon */
void weather_2B0B18(void);                                   /* 0x2B0B18 interpolate attributes */
void back_intersect(void);                                   /* 0x2B0C60 near plane */
void weather_2B0DA4(void);                                   /* 0x2B0DA4 plane x = -z */
void weather_2B0EC8(void);                                   /* 0x2B0EC8 plane y = -z */
void weather_2B0FF4(void);                                   /* 0x2B0FF4 plane x = z */
void weather_2B1118(void);                                   /* 0x2B1118 plane y = z */
void weather_2B12D0(TerrBin *b);                             /* 0x2B12D0 texture corners */
void weather_2B1694(void);                                  /* 0x2B1694 (G6b) */
void weather_2B16AC(void);                                  /* 0x2B16AC (G6b) */
void weather_2B16B0(long mesh, fix x, fix y);               /* 0x2B16B0 (G6b) */
void weather_2B1814(void);                                  /* 0x2B1814 (G6b) */
void weather_2B1DCC(void);                                  /* 0x2B1DCC (G6b) */
void weather_2B1F24(void);                                  /* 0x2B1F24 (G6b) */
void weather_2B1BC0(TerrBin *b, void *fn);                   /* 0x2B1BC0 draw a bin */
uchar weather_2B219C(TrVert **pts, long n, long x, long y);  /* 0x2B219C sky reflection */
uchar weather_2B2340(TrVert **pts, long n, long x, long y);  /* 0x2B2340 textured, perspective */
uchar weather_2B2418(TrVert **pts, long n, long x, long y);  /* 0x2B2418 textured, linear */
uchar weather_2B24C0(TrVert **pts, long n, long x, long y);  /* 0x2B24C0 flat colour */
uchar weather_2B250C(TrVert **pts, long n);                  /* 0x2B250C wireframe */
uchar weather_2B2584(TrVert **pts, long n);                  /* 0x2B2584 black (n: edx passed on) */
uchar weather_2B2598(TrVert **pts, long n, long x, long y);  /* 0x2B2598 shaded rows */
long weather_2B2610(long x, long y);                         /* 0x2B2610 target square colour */
uchar weather_2B2690(TrVert **pts, long n, long x, long y);  /* 0x2B2690 grid */
void crt_2FB3A0(grs_bitmap *bm, long n, TrVert **pts, void *refl); /* 0x2FB3A0 water mapper */

/* ------------------------------------------------------------------ skel/render.c (skelrend.c) */
/* LG skeleton (x:\prj\tech\libsrc\skel\render.c).  0x14-byte header followed by the arrays:
 * +0 type (0 lines, 1 crt_315FB0 segments, 2 sprite segments, 3 sub-objects), +1 segment count,
 * +2 joint count, +3 extra count (n3), +4 sort callback (mode 1), +8 segments (16 bytes each),
 * +0xC joints (g3s_vector), +0x10 per-segment up vectors (g3s_vector, indexed by seg->up). */
typedef struct {
    uchar j0, j1, up;              /* +0 +1 +2 joint indices, up vector index */
    uchar flags;                   /* +3 0x10 hidden, 1 mirrored */
    ulong ref;                     /* +4 resource ref (low word; high word from the caller) */
    uchar *model;                  /* +8 locked resource (sprite set or 3D object) */
    ulong parm;                    /* +0xC colour / clut / object parms */
} SkSeg;
typedef struct {
    uchar type, nseg, njoint, n3;  /* +0 +1 +2 +3 */
    void *sort_fn;                 /* +4 */
    SkSeg *seg;                    /* +8 */
    g3s_vector *joint;             /* +0xC */
    g3s_vector *up;                /* +0x10 */
} Skel;

extern long skel_cb_a[32];         /* 0x44CB90 per segment: callback (2F74F0) or 1-based skel_pos index (2F7B1C) */
extern long skel_cb_b[32];         /* 0x44CB10 */
extern uchar skel_cb_flag[32];     /* 0x44C970 */
extern fix skel_cb_parm[32][3];    /* 0x44C7F0 */
extern g3s_vector skel_pos[32];    /* 0x44C990 */
extern uchar skel_tab_6F0[256];    /* 0x44C6F0 identity table set by skel_2F6BD8, not read here */
extern uchar *skel_clut_a;         /* 0x44CC14 skel_2F7AAC cache keys */
extern uchar *skel_clut_b;         /* 0x44CC10 */
extern uchar skel_clut[256];       /* 0x44CC18 */

Skel *skel_2F68B0(long n1, long n2, long n3);               /* 0x2F68B0 allocate */
void skel_2F6918(Skel *sk);                                 /* 0x2F6918 free */
long skel_2F6920(long n1, long n2, long n3);                /* 0x2F6920 size */
void skel_2F695C(Skel *sk, long n1, long n2, long n3);      /* 0x2F695C init in place */
void skel_2F69AC(Skel *sk, ulong parm);                     /* 0x2F69AC set every seg parm */
void skel_2F69DC(Skel *sk, void *fn);                       /* 0x2F69DC set sort callback */
void skel_2F69E0(Skel *sk, long hi);                        /* 0x2F69E0 lock (ref | hi << 16) */
void skel_2F6A64(Skel *sk);                                 /* 0x2F6A64 lock */
void skel_2F6AD0(Skel *sk);                                 /* 0x2F6AD0 unlock */
void skel_2F6B08(Skel *sk, long hi);                        /* 0x2F6B08 unlock */
void skel_2F6B50(long i, long v, long flag);                /* 0x2F6B50 */
void skel_2F6B64(long i, long v, fix a, fix b, fix c);      /* 0x2F6B64 */
void skel_2F6B94(long i, long v, long flag);                /* 0x2F6B94 index v (stored v+1) */
void skel_2F6BA8(long i, long v, fix a, fix b, fix c);      /* 0x2F6BA8 index v (stored v+1) */
void skel_2F6BD8(void);                                     /* 0x2F6BD8 clear */
void skel_2F6C50(SkSeg *s, g3s_vector *a, g3s_vector *b, g3s_vector *up,
                 g3s_point *pa, g3s_point *pb);             /* 0x2F6C50 draw a sprite segment */
void skel_2F737C(g3s_vector *a, g3s_vector *b, g3s_vector *up); /* 0x2F737C start object frame */
void skel_2F7480(SkSeg *s, g3s_vector *a, g3s_vector *b, g3s_vector *up); /* 0x2F7480 */
void skel_2F74A4(SkSeg *s, g3s_vector *a, g3s_vector *b, g3s_vector *up); /* 0x2F74A4 */
void skel_2F74F0(Skel *sk, long mode);                      /* 0x2F74F0 render */
uchar *skel_2F7AAC(uchar *a, uchar *b);                     /* 0x2F7AAC combined clut */
void skel_2F7B1C(Skel *sk);                                 /* 0x2F7B1C sub-objects / positions only */
void crt_315F88(Skel *sk, uchar *order);                    /* 0x315F88 identity order */
void crt_315E60(Skel *sk, uchar *order, g3s_phandle *pts);  /* 0x315E60 depth sort */
void crt_315FB0(fix x0, fix y0, fix z0, fix x1, fix y1, fix z1, uchar **model, long c); /* 0x315FB0 */

#pragma pack(pop)

#endif
