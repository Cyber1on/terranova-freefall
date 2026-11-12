/* g6_a.h -- declarations for worker g6a (batch G6, render):
 *   M076 drawobj.c object drawing          (0x29AC50-0x29E11F, src/game/render/drawobj.c)
 *   M094 thunder.c lightning bolts         (0x2B2BF0-0x2B397F, src/game/render/thunder.c)
 *   M094 column rasteriser asm helpers     (0x2B2930-0x2B2BDF, src/game/render/colras.c)
 *
 * Names: the map's placeholders are kept for functions other modules call (drawobj_*,
 * thunder_2B2930 / 2B29B0 / 2B2AD8 / 2B2BF0 / 2B2DBC / 2B32BC / 2B33E4 / 2B3404 / 2B3898);
 * their prototypes are the ones the callers' headers already declare (g5_a.h, g5_b.h, g5_c.h,
 * g3_c.h), included here rather than repeated.  Internal functions got names after what they do
 * (meanings in the file headers).  Globals are bound to their original addresses through
 * symbols-g6-a.csv.  Structs are byte-packed.
 */
#ifndef TN_G6_A_H
#define TN_G6_A_H
#include "lg.h"
#include "fix.h"
#include "game.h"
#include "2d.h"
#include "3d.h"
#include "g1_a.h"      /* RndRange */
#include "g1_c.h"      /* game_rnd_text */
#include "g5_c.h"      /* StripRas, thunder_2B29B0 / 2B2AD8, drawobj_29CCE0 / 29ACA8 / 29B170 /
                          29DA60 / 29AF98; g5_a.h thunder_2B2930, hz_*; g5_b.h thunder_2B3898,
                          drawobj_29BA3C, ff_view, ff_cam, ff_res_tab */
#include "g3_c.h"      /* drawobj_29B2C0 */
#pragma pack(push, 1)

/* ------------------------------------------------------------------ M094 column rasteriser */
/* thunder_2B29B0 / thunder_2B2AD8 are self-modifying asm: the state is patched into the
 * immediates of two copies of the column loop (textured light: fade_step != 0, and flat).
 * Our C keeps the patched values in the same code bytes, so original and C halves of the pair
 * can be mixed.  Offsets are relative to the loop labels below. */
extern uchar colras_fade_loop[];         /* 0x2B2A1F column loop with a light fade (code) */
extern uchar colras_flat_loop[];         /* 0x2B2B4F column loop with one light row (code) */
void thunder_2B2930(long a, long b, fix depth, long h);   /* 0x2B2930 (G6a) project: depth in ebx, height in ecx -> hz_sx, hz_sy */
void thunder_2B29B0(long a, long b, StripRas *r, long n);  /* 0x2B29B0 */
void thunder_2B2AD8(long a, long b, StripRas *r);          /* 0x2B2AD8 set rows / row length */

/* ------------------------------------------------------------------ M094 thunder.c */
/* Lightning bolts: up to 7 bolts (1..7), each a list of segments (bolt_head[b] -> seg.next);
 * a segment is a chain of points from its tip back to its base (pt.link).  Segments 1..0xFE
 * and points 0..0x1FE come from pools with free lists. */
typedef struct {
    short tip;               /* +0 newest point */
    short base;              /* +2 first point (the parent's tip for a branch) */
    uchar next;              /* +4 next segment of the bolt (0 = end) / free list */
    uchar gen;               /* +5 branch generation (0 = main channel) */
    uchar cnt;               /* +6 steps since the last branch */
    uchar life;              /* +7 length (points) the segment grows to */
} BoltSeg;
LG_STATIC_ASSERT(sizeof(BoltSeg) == 8, BoltSeg_size);

typedef struct {
    fix x, y, z;             /* +0 world position */
    short link;              /* +0xC previous point of the segment (-1 none) / free list */
    uchar seg;               /* +0xE segment */
    uchar count;             /* +0xF points from the segment's base */
} BoltPt;
LG_STATIC_ASSERT(sizeof(BoltPt) == 0x10, BoltPt_size);

extern uchar bolt_head[8];               /* 0x438EE0 first segment of bolt b (0 = free slot) */
extern BoltSeg bolt_seg[0x100];          /* 0x438EE8 (entry 0 unused) */
extern BoltPt bolt_pt[0x200];            /* 0x4396E8 */
extern short bolt_root[8];               /* 0x43B6E8 first point of bolt b (-1 none) */
extern long *bolt_ybuf;                  /* 0x43B6F8 per column: terrain top (drawing) */
extern fix bolt_width_branch;            /* 0x43B6FC line width of branches (near) */
extern fix bolt_width_main;              /* 0x43B700 line width of the main channel (near) */
extern long bolt_len;                    /* 0x43B704 main channel length (points) */
extern fix bolt_dist;                    /* 0x43B708 length of bolt_dir before normalising */
extern fix bolt_dir[3];                  /* 0x43B70C direction (x, y, z) */
extern long bolt_seg_nfree;              /* 0x43B720 segments on the free list */
extern long bolt_pt_nfree;               /* 0x43B724 points on the free list */
extern uchar bolt_col_branch;            /* 0x43B728 colour of the branches (gen != 0) */
extern uchar bolt_col_main;              /* 0x43B729 colour of the main channel (gen 0) */
extern long bolt_pt_next;                /* 0x35CC90 next never-used point (< 0x200) */
extern long bolt_seg_next;               /* 0x35CC94 next never-used segment (< 0x100) */
extern long bolt_seg_free;               /* 0x35CC98 segment free list (0 = empty) */
extern long bolt_pt_free;                /* 0x35CC9C point free list (-1 = empty) */
extern fix bolt_scale;                   /* 0x35CCA0 branch / first step length */
extern fix bolt_step;                    /* 0x35CCA4 growth step length */
extern uchar bolt_fade_main[0x21];       /* 0x35CCA8 colour by age (main channel) */
extern uchar bolt_fade_branch[0x21];     /* 0x35CCC9 colour by age (branches) */

void thunder_2B2BF0(void);                               /* 0x2B2BF0 reset the pools */
long bolt_find_free(void);                               /* 0x2B2C38 (no callers) */
long bolt_seg_alloc(void);                               /* 0x2B2C60 0 = none */
long bolt_pt_alloc(void);                                /* 0x2B2CB8 -1 = none */
void bolt_pt_release(long p);                            /* 0x2B2D10 (no callers) */
void bolt_seg_release(long s);                           /* 0x2B2D3C */
void thunder_2B2DBC(long b);                             /* 0x2B2DBC free a bolt */
void bolt_seg_unlink(long b, long s);                    /* 0x2B2E34 */
long bolt_seg_extend(long s);                            /* 0x2B2E80 new tip point, -1 = none */
long bolt_seg_new(long b);                               /* 0x2B2ED4 */
long bolt_seg_branch(long b, long parent);               /* 0x2B2F04 */
void bolt_dir_set(fix *a, fix *b);                       /* 0x2B2F44 (no callers) */
void bolt_dir_normalize(void);                           /* 0x2B2F78 */
void bolt_dir_jitter(void);                              /* 0x2B300C */
long bolt_seg_step(long s, long from, long to, fix len, uchar jitter); /* 0x2B3098 */
long bolt_seg_life(long s);                              /* 0x2B3170 */
long bolt_branch(long b, long parent, fix len);          /* 0x2B31DC */
uchar bolt_pt_underground(long p);                       /* 0x2B3244 */
void thunder_2B32BC(fix scale, fix step);                /* 0x2B32BC */
uchar bolt_grow(long b);                                 /* 0x2B32C8 1 = finished */
uchar thunder_2B33E4(long b, long n);                    /* 0x2B33E4 grow n steps */
long thunder_2B3404(fix *from, fix *to);                 /* 0x2B3404 new bolt (0 = none) */
void bolt_draw_line(BoltPt *a, BoltPt *b, uchar gen);    /* 0x2B3628 */
void bolt_draw_wide(BoltPt *a, BoltPt *b, uchar gen);    /* 0x2B365C */
typedef void (WATCOMCALL *BoltDrawFn)(BoltPt *a, BoltPt *b, uchar gen);
void bolt_walk(long b, BoltDrawFn fn);                   /* 0x2B36A4 */
void bolt_draw_simple(long b, long close, long age);     /* 0x2B372C */
void bolt_draw_far(BoltPt *a, BoltPt *b, uchar gen);     /* 0x2B3788 */
void bolt_draw_near(BoltPt *a, BoltPt *b, uchar gen);    /* 0x2B3824 */
void thunder_2B3898(long bolt, long close, ulong age, long *ybuf); /* 0x2B3898 (G6a) */

/* ------------------------------------------------------------------ M076 drawobj.c */
/* What drawobj_29ACA8 fills (callers keep 12 bytes on their stack and pass them as uchar *). */
typedef struct {
    long ref;                /* +0 Ref of the model / sprite bitmap (class 1: 1) */
    fix scale;               /* +4 sprite width */
    uchar draw;              /* +8 1 = draw it */
    uchar pad[3];
} DrawInfo;
LG_STATIC_ASSERT(sizeof(DrawInfo) == 12, DrawInfo_size);

extern uchar *drawobj_cls3_models;       /* 0x41E21C RefLock(0x55D0000): Ref per class 3 type */
extern uchar *drawobj_cls2_models;       /* 0x41E220 RefLock(0x55C0000): Ref per class 2 type */
/* drawobj_cls0_models 0x41E224: g6_b.h; game_41E218 (RefLock(0x55E0000)): g3_c.h */
extern uchar drawobj_flat;               /* 0x35C050 draw without terrain shade / lighting */
extern g3s_vector drawobj_plate[4];      /* 0x35C054 corners of the textured plate */
extern g3s_vector drawobj_nav_shape[5];  /* 0x35C084 nav marker pyramid (4 corners + top) */
extern grs_vertex *wl_vlist[8];          /* 0x35C0C0 -> wl_verts */
extern grs_vertex wl_verts[8];           /* 0x41E0D0 polygon of drawobj_29D2B0 */
extern long drawobj_nav_sx;              /* 0x41E210 nav marker on screen (-1 = not drawn) */
extern long drawobj_nav_sy;              /* 0x41E214 */
extern ushort drawobj_nav_turn;          /* 0x41E228 nav marker heading (+0x100 per frame) */
extern long drawobj_pose[3];             /* 0x41E22C PBA pose angles */
extern long drawobj_35B1A0;              /* 0x35B1A0 nonzero: class 3 type 0xA uses the normal clut */
extern short drawobj_38FB12;             /* 0x38FB12 entity whose ragdoll carries the camera */
extern long ffpipe_38FFF4;               /* 0x38FFF4 nav point y (x: ffpipe_38FFF0, g5_b.h) */

/* G6b's skelrend.c (declared in g6_b.h with Skel *): skel_2F6A64 lock, skel_2F6AD0 unlock,
 * skel_2F74F0 render, skel_2F7B1C positions only. */

void drawobj_29AC50(void);                                           /* 0x29AC50 unlock model tables */
uchar drawobj_29ACA8(GameEntity *e, uchar *buf);                   /* 0x29ACA8 draw setup */
void drawobj_29AF98(GameEntity *e, uchar *buf);                    /* 0x29AF98 draw */
uchar drawobj_29B170(long ent, uchar *buf, fix *a, fix *b, fix *c);/* 0x29B170 */
uchar drawobj_29CCE0(GameEntity *e, short *rect);                  /* 0x29CCE0 screen rectangle */
uchar drawobj_29DA60(fix x, fix y, fix h, fix *vx, fix *vy, fix *vz, long *sx, long *sy); /* 0x29DA60 project */
uchar drawobj_29AED0(long ent);                                      /* 0x29AED0 (no refs) */
fix ff_image_model_width(GameEntity *e);                             /* 0x29AF44 */
void drawobj_29B234(GameEntity *e);                                  /* 0x29B234 pose skeleton */
void drawobj_29B2C0(long slot, long kind, fix *x, fix *y, fix *z); /* 0x29B2C0 G6a */
void drawobj_29B3AC(GameEntity *e, uchar *bb);                       /* 0x29B3AC start object */
void drawobj_29B43C(void);                                           /* 0x29B43C (no refs) end object */
void drawobj_29B448(long idx, long res, uchar lock);                 /* 0x29B448 lock a texture */
void drawobj_29B488(uchar *obj, uchar lock);                         /* 0x29B488 model textures */
uchar drawobj_29B4D0(GameEntity *e);                                 /* 0x29B4D0 highlight */
void drawobj_29B54C(GameEntity *e, uchar *obj);                      /* 0x29B54C draw a model */
long drawobj_29B694(GameEntity *e);                       /* 0x29B694 debris images -> count */
void drawobj_29BA3C(void);                                  /* 0x29BA3C (G6a) */
uchar drawobj_29BE28(GameEntity *e, fix *lo, fix *hi);    /* 0x29BE28 model box */
void drawobj_29B730(GameEntity *e, long tex);                        /* 0x29B730 textured plate */
void drawobj_29BC60(g3s_point *p, short *r);                         /* 0x29BC60 grow rect */
void drawobj_29BCF0(GameEntity *e, uchar *buf, short *r);            /* 0x29BCF0 model rect */
void drawobj_29BE78(GameEntity *e, uchar *bm, fix scale, long shade);/* 0x29BE78 sprite */
void drawobj_29C114(fix *out, fix *a, fix *b, long c, long d, long e); /* 0x29C114 */
void drawobj_29C1DC(fix *v, fix *a, fix *b, fix t);                  /* 0x29C1DC */
void drawobj_29C348(fix *a, fix *b);                                 /* 0x29C348 (no refs) */
long drawobj_29C36C(long a);                                         /* 0x29C36C */
long drawobj_29C39C(long a);                                         /* 0x29C39C */
void drawobj_29C3AC(uchar *bp, uchar *sk);                           /* 0x29C3AC */
void drawobj_29C448(uchar *bp, uchar *sk, long k);                   /* 0x29C448 */
void drawobj_29C508(long slot, long t);                              /* 0x29C508 pose a PBA */
void drawobj_29C864(GameEntity *e);                                  /* 0x29C864 draw a PBA */
void drawobj_29CA48(GameEntity *e);                                  /* 0x29CA48 draw a ragdoll */
void drawobj_29CBA4(long ent, short *r);                             /* 0x29CBA4 */
void drawobj_29CF2C(short *r, fix x, fix y);                         /* 0x29CF2C (no refs) */
void drawobj_29CF68(short *r);                                       /* 0x29CF68 (no refs) */
void drawobj_29CFC4(fix ax, fix ay, fix az, fix bx, fix by, fix bz, uchar color);       /* 0x29CFC4 3D line */
void drawobj_29D238(long a, fix *p, fix *q, long b, fix d, fix e);   /* 0x29D238 */
long drawobj_29D284(fix x, fix y, fix w);                            /* 0x29D284 (no refs) */
void drawobj_29D2B0(fix ax, fix ay, fix az, fix bx, fix by, fix bz, fix width, uchar color); /* 0x29D2B0 wide 3D line */
fixang drawobj_29DC20(long ent);                                     /* 0x29DC20 vehicle pitch */
fixang drawobj_29DD64(GameEntity *e, uchar *bb);                     /* 0x29DD64 model pitch */
void drawobj_29DE8C(GameEntity *e);                                  /* 0x29DE8C beam */
void drawobj_29DF38(GameEntity *e);                                  /* 0x29DF38 particles */
void drawobj_29E0DC(GameEntity *e);                                  /* 0x29E0DC (no refs) bolt */

#pragma pack(pop)

#endif
