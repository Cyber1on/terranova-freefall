/* g2_b.h -- declarations for worker g2b (batch G2): object damage and alerts (M053
 * src/game/objects/objdamage.c), M055 (src/game/objects/objheat.c), weapons.c (M063,
 * src/game/weapons/weapons.c), the object class descriptor (M070, src/game/objects/classdesc.c),
 * the object marker map (M080, src/game/objects/objmap.c) and the fire alert (M106,
 * src/game/weapons/firealert.c).
 * Names are ours; functions other batches already call keep their map names.  Globals are
 * bound to their original addresses through symbols-g2-b.csv.
 */
#ifndef TN_G2_B_H
#define TN_G2_B_H
#include "lg.h"
#include "fix.h"
#include "res.h"
#include "game.h"
#include "t1_c.h"
#include "g1_a.h"
#include "g1_b.h"
#include "g1_c.h"      /* game_rnd_text */
#include "g2_a.h"      /* grid, weapphys (g2a); g2_a.h includes g2_c.h (killobj, interact, objacc) */

extern short game_player_slot;       /* 0x441352 (g1_c.h): PBA slot of the player */
#pragma pack(push, 1)

/* raw field access into the big records (layouts in game.h are partial) */
#define B8(p, o)  (*(uchar *)((uchar *)(p) + (o)))
#define S8(p, o)  (*(signed char *)((uchar *)(p) + (o)))
#define U16(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define S16(p, o) (*(short *)((uchar *)(p) + (o)))
#define U32(p, o) (*(ulong *)((uchar *)(p) + (o)))
#define S32(p, o) (*(long *)((uchar *)(p) + (o)))
#define PTR(p, o) (*(uchar **)((uchar *)(p) + (o)))

#define ENT(e) (&game_entities[(short)(e)])
#define ENT_PBA(e) ((uchar *)&game_pba[ENT(e)->slot])

/* ------------------------------------------------------------------ other modules' tables */
/* class record tables (see game_pools, game.h): class 3 0x14F bytes at 0x39E590, class 4 0x79 at
 * 0x39C750, class 5 0x1E at 0x39BDF0, class 6 0x2C at 0x39A530, class 7 10 at 0x39BCB0,
 * class 8 12 at 0x39BB30, class 0 9 at 0x3BA080 */
extern uchar game_cls3[][0x14F];        /* 0x39E590 */
extern uchar game_cls4[][0x79];         /* 0x39C750 */
extern uchar game_cls5[][0x1E];         /* 0x39BDF0 */
extern uchar game_cls8[][0x0C];         /* 0x39BB30 */
extern RndStream game_rnd_35B704;          /* 0x35B704 RndRange stream (objects) */
extern long game_359708;                /* 0x359708 mission number (g1c name) */
extern ulong game_frame_dt;             /* 0x35F3F8 */

/* ------------------------------------------------------------------ M055 (objheat.c) */
/* 4-byte state handled by obj55_288EF0: +0 ushort timer, +2 enabled, +3 state 0..3 */
extern ulong game_442439;               /* 0x442439 mission parameter used as a divisor base */
extern uchar game_35B750;               /* 0x35B750 */
extern uchar *game_389E34;              /* 0x389E34 */
extern uchar *game_389EC4;              /* 0x389EC4 */

void obj55_288EC0(uchar *p, long v);                           /* 0x288EC0 */
ushort obj55_288EF0(long ent, uchar *p, long v);               /* 0x288EF0 */
void obj55_288F78(long ent, uchar *p, long v, void *x);        /* 0x288F78 (ecx is a 4th arg) */
uchar obj55_28927C(long ent);                                  /* 0x28927C off the ground */

/* ------------------------------------------------------------------ M070 (classdesc.c) */
/* game_pools[9] (GamePool, game.h): the descriptor table at 0x295320 in the code segment
 * (g2b called it obj_class_desc; one name since the G2 merge), defined in classdesc.c */
extern long objid_index, objid_page;           /* 0x3BA980, 0x3BA984 */
void objid_reset(void);                        /* 0x295368 */
uchar objid_next(short *id);                   /* 0x29537C */
void pool_2953DC(uchar *id);                   /* 0x2953DC */

/* ------------------------------------------------------------------ M080 (objmap.c) */
#define OBJMAP_MAX 20
typedef struct {
    short ent;           /* +0x00 */
    short gx, gy;        /* +0x02 grid position (rounded 16.16 >> 16) */
    long type;           /* +0x06 objmap_stamps index */
    uchar cells[64];     /* +0x0A what the stamp added, column major (x * 8 + y) */
} ObjMapEnt;
LG_STATIC_ASSERT(sizeof(ObjMapEnt) == 0x4A, ObjMapEnt_size);
typedef struct { short x, y, count, next; } ObjMapNode;
typedef struct {
    short cx, cy;        /* +0 stamp centre */
    signed char v[64];   /* +4 column major */
} ObjMapStamp;
LG_STATIC_ASSERT(sizeof(ObjMapStamp) == 0x44, ObjMapStamp_size);
extern ObjMapEnt objmap_tab[OBJMAP_MAX];   /* 0x421360 */
extern long objmap_count;                  /* 0x42192C */
extern long objmap_free;                   /* 0x421928 node free list head */
extern short objmap_hash[256];             /* 0x420160 chain heads, -1 = empty */
extern ObjMapNode objmap_nodes[512];       /* 0x420360 */
extern ObjMapStamp objmap_stamps[];        /* 0x35C11C */
extern uchar *game_map_35BFB4;             /* 0x35BFB4 map record (+0x84E: cell row pointers) */

uchar objmap_add(long ent, long type);                         /* 0x2A3230 */
uchar objmap_find(long ent);                                   /* 0x2A32E4 */
uchar objmap_kill_cb(long ent);                                /* 0x2A3320 */
uchar objmap_remove(long ent);                                 /* 0x2A3324 */
void objmap_update(void);                                      /* 0x2A3390 */
void objmap_clear(void);                                       /* 0x2A33B8 */
void objmap_update_one(long i);                                /* 0x2A33E8 */
void objmap_hash_init(void);                                   /* 0x2A347C */
long objmap_node_pop(void);                                    /* 0x2A34E0 */
void objmap_node_push(long n);                                 /* 0x2A34FC */
void objmap_node_free(long n);                                 /* 0x2A3514 */
long objmap_node_get(long x, long y);                          /* 0x2A359C */
long objmap_node_find(long x, long y);                         /* 0x2A364C */
long objmap_node_move(long x, long y, long from, long to);     /* 0x2A36AC */
long objmap_node_sub(long x, long y, long d);                  /* 0x2A36C8 */
void objmap_cell_add(long x, long y, uchar *out, long val);    /* 0x2A3704 */
void objmap_cell_sub(long x, long y, long val);                /* 0x2A37A8 */
void objmap_stamp(long i);                                     /* 0x2A381C */
void objmap_unstamp(long i);                                   /* 0x2A39BC */
uchar objmap_query(ulong pt, long *out);                       /* 0x2A3B1C */

/* ------------------------------------------------------------------ M106 (reticle.c) */
extern ulong reticle_edge_time;         /* 0x442850 */
extern ulong reticle_edge_count;        /* 0x442854 */
extern long game_397B70;                /* 0x397B70 set to 3 when the player leaves the area */
extern uchar game_3970E0;               /* 0x3970E0 set when the player leaves the area */
extern ushort game_36F49C;              /* 0x36F49C */

void reticle_reset(void);                                       /* 0x2CCD30 */
void reticle_edge_check(void);                                  /* 0x2CCD44 */
long reticle_edge_dist(void);                                   /* 0x2CCDEC */
void reticle_target(long e, ulong pt, uchar *cam, void *view);  /* 0x2CCE70 */
void reticle_edge_msg(long n);                                  /* 0x2CD388 */


/* ------------------------------------------------------------------ M063 (weapons.c) */
/* weapon type records, 0x24 bytes at 0x395114 (layout in weapons.c) */
extern uchar game_weapon_types[][0x24];  /* 0x395114 */
#define WT(t) (game_weapon_types[(uchar)(t)])
extern short game_skill_tab[][5];        /* 0x396356 per-skill-level factors (percent) */
extern long game_35CD9C;                 /* 0x35CD9C cheats enabled (g1c name) */
extern uchar game_442900;                /* 0x442900 skill level 0..4 */
extern uchar game_442F1B;                /* 0x442F1B cheat bits: 2 endless clips, 4 no reload */
extern uchar game_428BC4;                /* 0x428BC4 set: clips are not used up */
extern uchar game_35BF38;                /* 0x35BF38 set during weapon_blast */
extern uchar game_3C5E1C[128][128];      /* 0x3C5E1C tile objects per 4 x 4 squares */
typedef struct {
    short ent;           /* +0 firer */
    short hit;           /* +2 what it hit */
    short snd;           /* +4 sound (0 = free) */
    short noise;         /* +6 noisy_add sound */
    long dist;           /* +8 distance from the player */
} WpnSnd;
extern WpnSnd wsnd_slots[2];             /* 0x397A50 */
extern long debris_count;                /* 0x397A68 */

/* the spatial grid of M071 (grid_hash, grid_squares, grid_hash_cur, grid_295CFC): g2_a.h */

void weapons_28F020(void);                                     /* 0x28F020 */
uchar weapon_ready(uchar *w, void *store);                     /* 0x28F024 */
void weapon_nop(void);                                         /* 0x28F0A0 */
void weapon_radio(long a, long b);                             /* 0x28F0A4 */
short weapon_fire(uchar *w, long ent, fix x, fix y, fix z, fix tx, fix ty, fix tz, long target,
                  void *store, long real);                     /* 0x28F0B4 */
fix weapon_aim_error(uchar *w, long ent, fix x, fix y, fix z, fix tx, fix ty, fix tz, long target,
                     fix *miss);                               /* 0x28F7FC */
uchar weapon_use_ammo(long ent, uchar *w, long n);             /* 0x28F950 */
short weapon_find_target(long ent, fix range, long alive, long any); /* 0x28FAAC */
void turret_fire(long i);                                      /* 0x28FD54 */
void turret_alert(long i);                                     /* 0x28FE20 */
void debris_reset(void);                                       /* 0x28FE68 */
void debris_burst(long unused, fix *pos, fix speed);           /* 0x28FE74 */
void debris_one(long ent, long unused, fix *from);             /* 0x28FFC4 */
uchar weapon_facing(long ent, fix x, fix y);                   /* 0x28FFF8 */
void weapon_blast(long ent, fix radius, fix x, fix y, fix z);  /* 0x290044 */
long weapon_damage5(long ent);                                 /* 0x2903A0 */
long weapon_damage8(long ent, long val);                       /* 0x290448 */
void wsnd_kill_cb(long ent);                                   /* 0x2904F8 */
void wsnd_fire(long ent, uchar *w, long hit);                  /* 0x290524 */
void weapons_29079C(void);                                     /* 0x29079C */
void weapons_290858(void);                                     /* 0x290858 */

/* ------------------------------------------------------------------ callees in other batches */
/* pbasim_28A878, damage_27D540, instsim_288554: g3_c.h (G3c) */
void RadioAck(long id);                                        /* 0x2CEC20 (G1b) */
void RadioMessage(long msg);                                   /* 0x2CEA24 (G1b) */
uchar damage_27DF14(long ent, long a);                         /* 0x27DF14 (G3c) */
void physics_2D7788(long ent);                                 /* 0x2D7788 (G3a) */
fix aircraft_284A6C(long ent, fix x, fix y);                   /* 0x284A6C (G3c) */

uchar static_286794(long ent, void *parts, void *hp, void *np, void *a5, void *a6, void *a7,
                    void *a8);                                 /* 0x286794 (M053) */
void static_287168(long ent, long killer, long a, long b, long c);  /* 0x287168 (M053) */
/* physics_2D45CC: g3_a.h (owner g3a) */
uchar physics_2D6644(long ent, long from);                     /* 0x2D6644 (G3a) */
void physics_2D7140(long tile, long x, long y);                /* 0x2D7140 (G3a) */
/* weapphys_2D9DC4 (returns short), weapphys_2D9F84 (void): g2_a.h */

#pragma pack(pop)
#endif
