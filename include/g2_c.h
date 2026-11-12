/* Batch G2, worker g2c: kill list (M056 killobj.c), object damage/energy meters (M059 objmeter.c),
 * ordnance / air strike / mission counters (M064 ordnance.c), weapon firing (M065 wpnfire.c),
 * per-class object dispatch (M067 objclass.c), object shapes and collision (M072 objshape.c).
 * Files: src/game/objects/{killobj,objmeter,objclass,objshape}.c, src/game/weapons/{ordnance,wpnfire}.c.
 *
 * Function names: the binary's own (KillObjMark, WeaponFire, ObjectInteraction,
 * ObjectTiledInteraction) and the map's placeholders otherwise (meaning in the file headers).
 * Globals of these modules are named after their use; globals of other modules keep neutral names
 * (address in the name).  Structs are byte-packed like the original.
 */
#ifndef TN_G2_C_H
#define TN_G2_C_H
#include "game.h"
#include "support.h"
#include "res.h"
#include "g1_a.h"
#include "g1_b.h"      /* GameSpeaker game_391E50 */

#pragma pack(push, 1)

typedef signed char schar;

/* fixed-point helpers with the original's exact rounding */
#define G2C_FIXMUL(a, b) ((fix)(((long long)(long)(a) * (long)(b)) >> 16))       /* imul; shrd 16 */
#define G2C_FIXDIV(a, b) ((fix)((((long long)(long)(a)) << 16) / (long)(b)))      /* cdq-less idiv */
#define G2C_ENT(i) (game_entities[(short)(i)])
/* the 15-bit index fields of the object lists (word << 1 >> 1, sign from bit 14) */
#define G2C_IDX15(w) ((short)((short)((ushort)(w) << 1) >> 1))

/* ------------------------------------------------------------------ M056 kill list */

typedef struct {
    short obj;           /* +0 entity */
    uchar reason;        /* +2 1 ordnance, 2 strike over, 3 mission end ... */
} KillEntry;
LG_STATIC_ASSERT(sizeof(KillEntry) == 3, KillEntry_size);

typedef struct {
    llist link;          /* +0x0 */
    short obj;           /* +0x8 per-object callbacks only */
    ulong mask;          /* +0xA bit n = call for reason n */
    void *fn;            /* +0xE Watcom fn(obj, reason) */
} KillCb;
LG_STATIC_ASSERT(sizeof(KillCb) == 0x12, KillCb_size);

#define KILLOBJ_BUF_MAX 0x80
extern KillEntry killobj_buf[KILLOBJ_BUF_MAX];   /* 0x396E30 */
extern LlistHead killobj_class_cbs[9];           /* 0x396FB0 callbacks per entity class */
extern long killobj_count;                       /* 0x3970AC */
extern long killobj_max;                         /* 0x3970B0 */
extern KillEntry *killobj_list;                  /* 0x3970B4 */
extern LlistHead killobj_obj_cbs;                /* 0x3970B8 callbacks per entity */

void killobj_2892D0(void);                       /* KillObjInit */
void killobj_28934C(void);                       /* KillObjProcess */
void KillObjMark(long obj, long reason);
void *killobj_28952C(long cls, void *fn, ulong mask);
void killobj_289554(long ent, void *fn, long arg);
void killobj_289588(long obj, void *fn);

/* ------------------------------------------------------------------ M059 object meters */

/* A meter (hit points, energy, ...): cur, max and rates, 16.16. */
typedef struct {
    fix cur;             /* +0x00 */
    fix max;             /* +0x04 */
    fix f08;             /* +0x08 second maximum (set with max by objacc_28B914) */
    fix f0c;             /* +0x0C regeneration (>> 10 per unit in objacc_28B88C) */
    fix f10;             /* +0x10 */
    uchar f14;           /* +0x14 */
    uchar f15;           /* +0x15 set to 100 */
} ObjMeter;
LG_STATIC_ASSERT(sizeof(ObjMeter) == 0x16, ObjMeter_size);

extern RndStream game_rnd_35B704;                /* 0x35B704 (g2b name; generator pointer at 0x35B708) */

uchar objacc_28B850(ObjMeter *m);
long objacc_28B854(void *m, fix amount, long exact, long take); /* only low bytes of exact/take used */
fix objacc_28B870(ObjMeter *m);
fix objacc_28B874(ObjMeter *m);
void objacc_28B878(ObjMeter *m, fix add);
void objacc_28B88C(ObjMeter *m, long units);
long objacc_28B8B0(ObjMeter *m);
void objacc_28B914(ObjMeter *m, fix max, fix cur, fix f0c, fix f10, long f14);
uchar objacc_28B938(void *m, long damage);

/* ------------------------------------------------------------------ M067 per-class dispatch */

typedef struct { ushort lo, hi, rate; } AngleLimit;     /* lo == hi: no limit */
extern AngleLimit objtype_limits[8];             /* 0x35BF84 .. 0x35BFAE */

long objtype_2933C0(AngleLimit *lim);
long objtype_2933E0(void);
void objtype_2933FC(long obj, long val);
void objtype_293464(long obj);
long objtype_293540(fix *out, long obj, long arg);
uchar objtype_2935E0(long obj, long arg);
uchar objtype_293834(long obj, long a, long b);
void objtype_2938B8(fix *p, fix target);
uchar objtype_2938F0(ushort *p, ulong target, AngleLimit *lim);
uchar objtype_293A5C(ushort *p, ulong a, ulong b, AngleLimit *l1, AngleLimit *l2, long unused);
long objtype_293AA0(long obj);

/* ------------------------------------------------------------------ M072 shapes / collision */

/* Object footprint used by the grid: ObjFoot (= GridObjDesc) and FootCell (= GridKey) are in
 * game.h since the G2 merge (one record, both sets of field names). */

/* Shape resources: one compound resource per class (base id + class), one item per type.
 * item: short type (0 none, 1 polygon, 2 cylinder, 3 dome); short npts; npts points {x, y};
 * then (polygon) 4 heights and the ShapeExt pointer at +0x10, (cylinder) radius,
 * (dome) radius, height. */
typedef struct {
    fix pt[4][2];        /* +0x00 corners pushed out by the collision radius */
    fix n[4][2];         /* +0x20 edge normals */
    fix d[4];            /* +0x40 edge offsets */
    fix z0;              /* +0x50 floor plane z = z0 + zx * x + zy * y */
    fix zx, zy;          /* +0x54, +0x58 */
} ShapeExt;
LG_STATIC_ASSERT(sizeof(ShapeExt) == 0x5C, ShapeExt_size);
typedef struct { short type; short npts; fix pt[][2]; } ShapeItem;
#define SHAPE_TAIL(s) ((fix *)((uchar *)(s) + 4 + (s)->npts * 8))
#define SHAPE_EXT(s) (*(ShapeExt **)(SHAPE_TAIL(s) + 4))

extern ushort shape_base_id;                     /* 0x3C5DF4 0 = not loaded */
extern RefTable *shape_tables[9];                /* 0x3C5DD0 locked compound resource per class */
extern Pool shape_pool;                          /* 0x3C5DF8 ShapeExt pool */
extern fix shape_radius;                         /* 0x35C024 collision padding */
/* tile_tmpls (0x3CC8F4, TileTmpl: class, type ...): g2_a.h */

void interact_2972C0(long base);                 /* load the shape resources */
void interact_297380(void);                      /* unload */
ShapeItem *interact_2973E8(long cls, long type);
uchar interact_297400(ObjFoot *f, long cls, long type);
uchar interact_29775C(long obj, fix x, fix y);
uchar ObjectInteraction(long obj, fix x, fix y, fix z, fix *vec, fix *nrm, uchar *hit, long *edge);
uchar ObjectTiledInteraction(long tile, fix *pos, fix x, fix y, fix z, fix *vec, fix *nrm, uchar *hit, long *edge);
uchar interact_297A40(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix *vec, fix *nrm, uchar *hit, long *edge);
void interact_297AE0(ShapeItem *s);
uchar interact_297DAC(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix *vec, fix *nrm, uchar *hit, long *edge);
uchar interact_2980CC(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix *vec, fix *nrm, uchar *hit);
uchar interact_298218(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix *vec, fix *nrm, uchar *hit);
uchar interact_29837C(long obj, fix x, fix y, fix z, fix *ox, fix *oy, fix *oz);
fix interact_2986C0(ShapeItem *s);
uchar interact_298730(long obj, fix *out, ulong mode);
uchar interact_298854(long obj, fix *out);
uchar interact_298864(long obj, fix *out);
uchar interact_298870(long obj, fix *out);
long interact_298880(fix lx, fix ly, ShapeExt *e);
fix interact_298980(long obj, fix x, fix y, fix z, long *edge);
fix interact_2989E8(long tile, fix *pos, fix x, fix y, fix z, long *edge);
fix interact_298A50(long obj, fix *pos, ShapeItem *s, fix x, fix y, fix z, long *edge);

/* ------------------------------------------------------------------ M064 ordnance / air strike */

typedef struct { short w0; short next; uchar g[0x1A]; } ObjListRec;   /* 0x1E bytes */
/* game_cls5 (0x39BDF0, g2_b.h uchar[][0x1E]) seen as ObjListRec: [0].w0 = first; w0 bit 15 flag */
#define G2C_CLS5(i) ((ObjListRec *)game_cls5[(i)])
extern uchar game_cls6[][0x2C];                 /* 0x39A530 (+0 entity, +0xA next) */
extern uchar ordn_objtype[][0x24];               /* 0x395100 (+0x17 flag) */
extern uchar ordn_counts[0x109];                 /* 0x397B77 kills per entity class / type id */
typedef struct { long id; uchar pct; uchar cnt; } OrdnGoal;
extern OrdnGoal ordn_goal_a[3][6];               /* 0x397A90 */
extern OrdnGoal ordn_goal_b[3][6];               /* 0x397AFC */
extern long ordn_goal_c;                         /* 0x397B68 */
extern uchar ordn_goal_done[3];                  /* 0x397B74 */
extern long ordn_flags[3];                       /* 0x35D0C0 */
extern uchar *ordn_mission_state;                /* 0x440978 (+0x809 states, +0x80C times) */

/* air strike state */
extern short strike_mode;                        /* 0x35BF3C 1 or 2 */
extern short strike_35BF3E;                      /* 0x35BF3E */
extern long strike_35BF40;                       /* 0x35BF40 */
extern uchar strike_warned;                      /* 0x35BF44 */
extern uchar strike_active;                      /* 0x35BF45 */
extern uchar strike_aborted;                     /* 0x35BF46 */
extern uchar strike_called;                      /* 0x35BF47 */
extern uchar game_35BF48;                        /* 0x35BF48 strike over */
extern short strike_35BF4A, strike_35BF4C;       /* 0x35BF4A, 0x35BF4C counters */
extern uchar strike_35BF4E[];                    /* 0x35BF4E */
extern uchar strike_35BF55, strike_35BF56;       /* 0x35BF55, 0x35BF56 */
extern uchar strike_35BF57;                      /* 0x35BF57 bits 1, 2, 4 */
extern ulong strike_35BF58;                      /* 0x35BF58 */
extern long strike_35BF5C;                       /* 0x35BF5C */
extern fix strike_player_x, strike_player_y;     /* 0x397A70, 0x397A74 */
extern fix strike_x, strike_y, strike_z;         /* 0x397A78, 0x397A7C, 0x397A80 */
extern short strike_entity;                      /* 0x397A8A */
extern short ordn_397C80;                        /* 0x397C80 */
extern uchar game_35B6A8;                        /* 0x35B6A8 */
extern long game_35B6D4, game_35B6D8;            /* 0x35B6D4, 0x35B6D8 */
extern uchar game_35B6E0;                        /* 0x35B6E0 */
extern fix game_4414C8, game_4414CC;             /* 0x4414C8, 0x4414CC strike target */
extern uchar game_4413D0[];                      /* 0x4413D0 region centres {x, y} */
extern uchar game_4417D0[];                      /* 0x4417D0 regions {short kind; fix w, h} */
extern uchar game_cls3[][0x14F];                 /* 0x39E590 class-3 (aircraft) records (g2b name) */

void ordnance_2908D0(ulong ms);
void ordnance_2909CC(void *ai);
void ordnance_290ABC(long obj);
void ordnance_290B38(long obj);
uchar ordnance_290B70(void);
long ordnance_291080(void);
void ordnance_291110(long v);
fix ordnance_29111C(void);
fix ordnance_29118C(void);
fix ordnance_2911D0(void);
fix ordnance_291220(void);
fix ordnance_2912B8(void);
void ordnance_291374(long arg);
long ordnance_291450(void);
void ordnance_29150C(void);
void ordnance_2915A8(void);
void ordnance_291660(void);
void ordnance_291700(void);
ulong ordnance_291720(long id);
void ordnance_29173C(void);
void ordnance_291754(ulong id);
void ordnance_29177C(long kind, ulong t, long state);
void ordnance_29188C(long kind);
void ordnance_2918C4(long kind);
void ordnance_2918D8(long kind);
long ordnance_2918EC(long kind);
long ordnance_291934(long id);
long ordnance_2919B4(ulong a, ulong b, long want_min);
long ordnance_291BC4(long e1, long e2);
long ordnance_291C34(long region, long e);
void ordnance_291D14(long a, long b, long c);
void ordnance_291D78(long id, long p1, long p2, long p3);
void ordnance_291E6C(long id, long p1, long p2, long p3);
long ordnance_291F60(long n);
ulong ordnance_292068(void);
long ordnance_29207C(long n);

/* ------------------------------------------------------------------ M065 firing */

/* The weapon block of a PBA record (game_pba[i] + 9). */
typedef struct {
    long count;          /* +0 ammo / charges */
    ulong type;          /* +4 2 reload, 5 reload all, 6 energy, 7/9 mines, 8 timed, 10 air
                            strike call, 11 designator */
    uchar sel;           /* +8 active */
    uchar ready;         /* +9 */
} PbaWeapon;
#define PBA_WEAPON(e) ((PbaWeapon *)((uchar *)&game_pba[G2C_ENT(e).slot] + 9))

extern ulong fire_next_time;                     /* 0x397C98 player refire time */
extern short fire_397C9C;                        /* 0x397C9C */
extern uchar fire_35BF30;                        /* 0x35BF30 */
extern uchar fire_35BF68;                        /* 0x35BF68 */
extern short fire_35BF6A;                        /* 0x35BF6A designated entity */
extern ulong fire_35BF6C;                        /* 0x35BF6C */
extern ulong fire_35BF70, fire_35BF74;           /* 0x35BF70, 0x35BF74 timed weapon: last time, remainder */
extern ulong fire_35BF78, fire_35BF7C;           /* 0x35BF78, 0x35BF7C energy weapon: last time, remainder */
extern uchar game_35ADE0;                        /* 0x35ADE0 */
extern uchar *game_389E4C;                       /* 0x389E4C record (+0x20 |= 0x20) */
extern short game_396868;                        /* 0x396868 */
extern uchar game_3968B5;                        /* 0x3968B5 */
extern uchar game_cls4[][0x79];                  /* 0x39C750 class-4 records (g2b name; w0, next) */
/* grid_hash, grid_squares, grid_hash_cur: g2_a.h */

void fire_292180(void);
void fire_2921A8(void);
void fire_2921F0(long n);
void fire_292200(long n);
void fire_292210(long n);
uchar WeaponFire(long obj);
uchar fire_292970(long obj, long sel);
void fire_292A68(long obj);
void fire_292C2C(long amount, long obj);
uchar fire_292C98(long obj, fix x, fix y);

/* ------------------------------------------------------------------ globals of other modules */

extern long game_359708;                         /* 0x359708 mission kind */
/* game_rnd_text 0x3596E0: g1_c.h */
extern ulong game_frame_dt;                      /* 0x35F3F8 */
extern long game_camera_38FB20;                  /* 0x38FB20 */
/* game_391E50 0x391E50 (GameSpeaker, 0x32 bytes: short kind, short entity): g1_b.h */

/* ------------------------------------------------------------------ callees in other batches */

/* G1 */
long SndPlay(ushort id, long type, long pri, long loops, long a1, long a2, long a3); /* 0x247B60 */
void snd_stop_entity(short ent);                 /* 0x247780 */
void music_246BD0(short entity);                 /* 0x246BD0 */
void RadioMessage(long msg);                     /* 0x2CEA24 */
void RadioAck(long id);                          /* 0x2CEC20 */
/* G2a/G2b */
void static_288040(long obj);                    /* 0x288040 */
void weapons_290858(void);                       /* 0x290858 */
/* grid_295730, grid_29582C (1 argument), grid_295CFC: g2_a.h */
/* G3 */
uchar damage_27CF30(void *a, void *b, long c, long d, void *e, long obj); /* 0x27CF30 */
/* damage_27D610: g3_a.h (owner g3a) */
uchar aircraft_2837B4(long a, long obj);         /* 0x2837B4 */
fix aircraft_284A6C(long obj, fix x, fix y);     /* 0x284A6C ground height */
uchar aircraft_284F2C(long arg);                 /* 0x284F2C */
void aircraft_285024(long arg);                  /* 0x285024 */
long aircraft_286010(long obj);                  /* 0x286010 */
void aircraft_28618C(void);                      /* 0x28618C */
uchar instsim_2888A4(long obj);                  /* 0x2888A4 */
long instsim_288904(long slot, long b, long c, long d, long e, long f); /* 0x288904 */
long instsim_288A4C(long obj);                   /* 0x288A4C */
void instsim_288C64(void *a, void *b, void *c, long d, void *e);  /* 0x288C64 */
long instsim_288D10(long slot, long b, long c, long d, long e, fix *out); /* 0x288D10 */
uchar pbasim_28A534(long obj);                   /* 0x28A534 */
void pbasim_28A5A0(void *a, void *b, void *c, long d, void *e);   /* 0x28A5A0 */
fix *pbasim_28A66C(long obj);                    /* 0x28A66C */
long pbasim_28A698(long obj);                    /* 0x28A698 */
long pbasim_28A79C(long slot, long b, long c, long d, long e, fix *out); /* 0x28A79C */
/* pbasim_28A878: g3_c.h */
/* vehsim_28EC5C/ECC4/ED68/EDC0/EF14, physics_2D3F20/42E0/43DC: g3_a.h (owner g3a; the C files
 * that call them include it) */
void physics_2D65F8(long obj);                   /* 0x2D65F8 */
/* G5 */
/* G6 */
/* G7/G8 */

#pragma pack(pop)
#endif
