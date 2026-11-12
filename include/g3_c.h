/* Batch G3, worker g3c: units -- nearby-object lists (M042), vehicle and projectile record
 * creation (M045, M046), the 12-byte unit table (M048), the aircraft simulator (M052), the
 * installation simulator (M054 instsim.c) and the PBA / soldier simulator (M058 pbasim.c).
 * Files: src/game/units/{unitnear,vehinit,projinit,unittab,aircraft,instsim,pbasim}.c.
 *
 * Function names: the map's placeholders (the meaning is in the file headers); names other
 * batches already call are kept.  Globals of these modules are named after their use; globals
 * of other modules keep neutral names (address in the name).  Structs are byte-packed.
 *
 * Header use: includes g2_c.h, which already declares several of these functions (types kept
 * compatible); prototypes of callees whose owners' headers cannot be included together with
 * g2_c.h yet (g1_b.h, g2_a.h, g2_b.h, g2_b2.h) are repeated here with the owner's types.
 */
#ifndef TN_G3_C_H
#define TN_G3_C_H
#include "game.h"
#include "support.h"
#include "res.h"
#include "g1_a.h"
#include "g2_c.h"
#include "g2_a.h"      /* grid, tile objects, voice_soldier (owner g2a) */
#include "t1_c.h"

#pragma pack(push, 1)

#define G3C_ENT(i) (game_entities[(short)(i)])
#define G3C_FIXMUL(a, b) ((fix)(((long long)(long)(a) * (long)(b)) >> 16))

/* ------------------------------------------------------------------ M042 nearby objects */

/* A list of objects near a unit, kept per unit (PBA +0xB2, vehicle +0x4D, aircraft +0x62). */
typedef struct {
    short id;            /* +0 entity, 0 ends the list */
    short dist;          /* +2 distance * 6 >> 16 */
    short f4;            /* +4 */
} NearEntry;
typedef struct {
    fix near_r;          /* +0x0 (6 x) radius inside which everything is listed */
    fix far_r;           /* +0x4 (6 x) radius beyond which nothing is listed */
    long max;            /* +0x8 entries in buf (one is kept for the terminator) */
    NearEntry *buf;      /* +0xC */
} NearList;
LG_STATIC_ASSERT(sizeof(NearList) == 0x10, NearList_size);

void damage_27D140(NearList *l, fix near_r, fix far_r, long max, NearEntry *buf);
NearList *damage_27D158(long ent);
long damage_27D1E4(long ent, NearList *l);
long damage_27D368(NearList *out, NearList **lists, long n);
long damage_27D458(NearList *l, long ent);
ulong *damage_27D540(long ent);

extern uchar game_42015A;                        /* 0x42015A */

/* ------------------------------------------------------------------ M045 vehicle records */

/* Vehicle type table game_veh_types (game.h, 0x36-byte records at 0x395850): +0x0 short meter
 * maximum, +0x2 short meter rate (integer parts), +0x4 / +0x8 near list radii, +0xC hit base. */
uchar damage_27DC90(short *id, short *slot, long side, long type, uchar *params);
uchar damage_27DD64(uchar *rec, long type, uchar *params);
uchar damage_27DE84(long ent);
uchar damage_27DF14(long ent, long sys);
void damage_27E064(long sys);
void damage_27E0B4(void);

extern schar player_sys_pct[8];                  /* 0x3924A0 player system levels, % */
extern uchar objdmg_res_3A4[0x20];               /* 0x3968D0 (g2b2) thresholds per system */

/* ------------------------------------------------------------------ M046 */

/* Ordnance type table (g2c ordn_objtype 0x395100, 0x24-byte records): +0x1A, +0x1C words. */
typedef struct {
    uchar kind;          /* +0x0 */
    uchar b1;            /* +0x1 */
    short f2;            /* +0x2 */
    short f4;            /* +0x4 */
    fix f6;              /* +0x6 */
    long fa;             /* +0xA */
    uchar fe;            /* +0xE */
} WeapSlot;
LG_STATIC_ASSERT(sizeof(WeapSlot) == 0xF, WeapSlot_size);
void damage_27E0E0(WeapSlot *w, long kind, long b1);
uchar damage_27E134(uchar *rec, long unused, short *pos);
bool damage_27E14C(short *ent, short *slot, long kind, void *key);
extern long game_442435;                         /* 0x442435 (integer part used) */

/* ------------------------------------------------------------------ M048 */

typedef struct { long f0, f4, f8; } UnitTabRec;
extern UnitTabRec unit_tab[];                    /* 0x442870 */
void damage_27E200(UnitTabRec *dst, long idx);
long damage_27E234(long idx);

/* ------------------------------------------------------------------ M052 aircraft.c */

/* Aircraft (class 3) record, 0x14F bytes (game_cls3 0x39E590): +0x00 w0 entity (15 bits; bit
 * 15 = inactive), +0x02 next, +0x06 parts (3), +0x1B systems (3), +0x2A weapons (2 x 0xF),
 * +0x48 weapon kind word, +0x4B current weapon (schar), +0x4C ObjMeter, +0x62 NearList,
 * +0x132 flight block {long speed; short turn (heading += 2 x turn per frame); short bank},
 * +0x13A / +0x13E long velocity x / y (fix per 1024 ms), +0x142 long climb rate,
 * +0x14B {ushort a, b} (strike plane state; a = approach angle). */
#define AIR_ENT(i) ((&game_396868)[i])           /* 0x396868 aircraft entity per side (4) */

void aircraft_2836E0(void);
long aircraft_28375C(void);
long aircraft_283770(long a);
uchar aircraft_2837B4(long a, long obj);
uchar aircraft_283A64(long ent, long target);
uchar aircraft_283A94(void);
void aircraft_283AFC(void);
void aircraft_283B88(ulong dt);
void aircraft_283F00(long ent);
void aircraft_283F2C(uchar *rec, long dt);
void aircraft_284444(void *ai);
void aircraft_2844F4(long ent);
void aircraft_284670(long a, long b, long c, long d);
void aircraft_284888(uchar *rec, fix dx, fix dy, long dt);
fix aircraft_284A6C(long obj, fix x, fix y);
uchar aircraft_284AB0(uchar *rec, long target);
uchar aircraft_284CEC(uchar *rec);
void aircraft_284D00(uchar *rec, fix dx, fix dy);
long aircraft_284E78(long slot, fix x, fix y, fix z, long target, long real);
uchar aircraft_284F2C(long arg);
void aircraft_285024(long arg);
void aircraft_285348(uchar *rec);
void aircraft_285460(long ent);
void aircraft_28559C(long ent);
void aircraft_285608(void);
void aircraft_28565C(uchar *rec);
uchar aircraft_2857E8(uchar *rec, uchar *flag, fix *pos);
void aircraft_2859DC(long a, long b, long c, long ecx_in);
uchar aircraft_285AFC(uchar *rec);
void aircraft_285DC4(void);
long aircraft_286010(long obj);
void aircraft_28605C(ulong dt);
void aircraft_28618C(void);
long aircraft_2862B0(void);
void aircraft_286324(void);
uchar aircraft_28635C(uchar *rec);

extern uchar air_dead[4];                        /* 0x396870 deletion in progress per side */
extern short air_target[4];                      /* 0x397C90 entity an aircraft attacks, per side */
extern uchar air_3968B2;                         /* 0x3968B2 landed */
extern uchar air_3968B3;                         /* 0x3968B3 player controls the aircraft */
extern uchar air_3968B4;                         /* 0x3968B4 returning to the player */
extern fix air_alt;                              /* 0x396874 flying height (0x18000) */
extern ulong air_ret[2];                         /* 0x396860 aircraft_286010 result */
extern fix air_drop_pos[3];                      /* 0x396878 */
extern short air_drop_ang[3];                    /* 0x396884 */
extern fix air_strike_vel[3];                    /* 0x3968B8 */
extern fix air_strike_prev[3];                   /* 0x3968C4 */
extern uchar air_35B6C2;                         /* 0x35B6C2 */
extern uchar air_parm_35B6C3[];                  /* 0x35B6C3 squad template (aircraft call) */
extern uchar air_parm_35B6E6[];                  /* 0x35B6E6 squad template (mission 0xE) */
extern short air_35B6AA;                         /* 0x35B6AA mission 0xE transport entity */
extern fix air_35B6B0[3];                        /* 0x35B6B0 landing position */
extern ulong air_35B6CC;                         /* 0x35B6CC end-of-mission timer */
extern uchar air_35B6D0;                         /* 0x35B6D0 */
extern ulong air_35B6DC;                         /* 0x35B6DC */
extern uchar air_mission_flags[5];               /* 0x35B6E1 missions 2, 6, 0x16, 0x1A, 0x1E */
extern uchar air_35B6ED;                         /* 0x35B6ED mission 0xE transport state bits */
extern ulong air_35B6F0;                         /* 0x35B6F0 */
extern ulong air_35B6F4;                         /* 0x35B6F4 next drop time */
extern long air_35B6F8;                          /* 0x35B6F8 */
extern uchar air_35B6FC;                         /* 0x35B6FC drops done */
extern ulong air_35B700;                         /* 0x35B700 */
extern uchar game_38FC12[][0x20];                /* 0x38FC12 camera views (game_camera +0xEE): +0 mode,
                                                    +6 flags */
/* 0x39246C: cursctrl_aircraft (g3_a.h) */

/* ------------------------------------------------------------------ M054 instsim.c */

/* Installation (class 4) record, 0x79 bytes (game_cls4, 64 at 0x39C750):
 *  +0x00 w0 entity (15 bits; bit 15 = inactive), +0x02 next, +0x0A short (smoke tag),
 *  +0x0C parts count, +0x0D weapon count, +0x0E current weapon, +0x10 parts,
 *  +0x33 weapons (0xF each), +0x51 systems (1), +0x56 ObjMeter, +0x6C listed-until time,
 *  +0x70 aim {ushort heading, pitch, f74, rate}, +0x76 timer, +0x78 destroyed flag. */
#define CLS4(i) (game_cls4[(short)(i)])

uchar instsim_288260(long type);
void instsim_288278(ulong dt);
void instsim_2883BC(long idx, long dt);
void instsim_28846C(void *ai);
void instsim_288554(long ent, long unused); /* 2 args (G2 merge): edx is an unused 2nd arg */
uchar instsim_2888A4(long obj);
long instsim_288904(long slot, long b, long c, long d, long e, long f);
long instsim_288A4C(long obj);
long instsim_288A98(long v);
void instsim_288AAC(long ent);
void instsim_288C64(void *a, void *b, void *c, long d, void *e);
long instsim_288D10(long slot, long b, long c, long d, long e, fix *out);
uchar *instsim_288E04(long slot);
void instsim_288E40(long ent);

extern uchar *game_41E218;                       /* 0x41E218 per entity type 8-byte records */
extern uchar game_35ECF2;                        /* 0x35ECF2 bit 0 set when a type 0x3F dies */
extern fix inst_aim_out[2];                      /* 0x396E20 instsim_288A4C result */
extern short inst_siren_ent;                     /* 0x396E2C */
extern long inst_siren_level;                    /* 0x35B740 */
extern uchar inst_siren_flags;                   /* 0x35B744 1 falling, 2 on, 4, 8 done */
extern uchar inst_siren_count;                   /* 0x35B745 */
extern ulong inst_siren_time;                    /* 0x35B748 next step */
extern long inst_siren_35B74C;                   /* 0x35B74C */
extern char instsim_file[];                      /* 0x355A00 "r:\prj\tn\v109\tn\src\tn\instsim.c" */

/* ------------------------------------------------------------------ M058 pbasim.c */

/* PBA record offsets used here (game_pba, 0x582 bytes; "p6" = record + 6 is the per-unit data
 * block that damage_27D720 / damage_27D818 fill):
 *  +0x00 w0 entity (15 bits), +0x02 next, +0x0D long state (-1 none, 3 = running),
 *  +0x15 four 7-byte parts {short cur, short max, ...}, +0x34 four 5-byte armour words,
 *  +0x45 four weapons (WeapSlot, 0xF each), +0x82 long sound channel, +0x86 ObjMeter,
 *  +0x9C uchar * systems (5 bytes each: kind, ushort max, short cur), +0xA0 ushort systems,
 *  +0xA2 / +0xA4 / +0xA8 obj55 block, +0xB2 NearList (+0xB6 far radius, +0xBE buffer),
 *  +0xC6 / +0xCA movement speed / load, +0xCE current weapon (schar), +0xCF flags
 *  (0x10 no meter regeneration, 0x20 hit this frame), +0x3F2 aim blocks, +0x3FA fix[]. */
#define PBA_REC(s) ((uchar *)&game_pba[(short)(s)])

void pbasim_289910(long ent, long b);
void pbasim_289930(void);
void pbasim_289948(void);
void pbasim_289A64(long kind, long f21, uchar *tmpl);
void pbasim_289B3C(void);
void pbasim_289B7C(void);
void pbasim_289B94(ulong dt);
void pbasim_289D20(long slot, ulong dt);
void pbasim_28A05C(void *ai);
void pbasim_28A140(long ent, long mode);
uchar *pbasim_28A344(uchar *buf, long kind);
void pbasim_28A500(long slot, uchar *buf);
uchar pbasim_28A534(long obj);
void pbasim_28A5A0(void *a, void *b, void *c, long d, void *e);
fix *pbasim_28A66C(long obj);
long pbasim_28A698(long obj);
long pbasim_28A69C(long obj);
void pbasim_28A6F0(long ent, long kind, fix *x, fix *y, fix *z);
long pbasim_28A79C(long slot, long b, long c, long d, long e, fix *out);
long pbasim_28A878(long slot, long b, long c, long d, long e, long f, long g);
uchar pbasim_28A9B8(uchar *p6);
void pbasim_28A9C8(long chan, long user);
void pbasim_28AA50(void);
void pbasim_28AB1C(uchar *p6, long idx);
void pbasim_28AB74(uchar *p6, long dir);
void PbaPlayerMove(void *p, fix *dx, fix *dy);
void pbasim_28AC5C(uchar *p6, fix *a, fix *b, long flag);
fix pbasim_28ACD4(fix v);
void pbasim_28AD10(long a, long b, long c, long ecx_in);
uchar pbasim_28ADBC(long me, long target, fix dist);
void pbasim_28B498(void);
void pbasim_28B5B0(long slot, fix margin);
uchar pbasim_28B654(long slot);
void pbasim_28B778(long ent);

extern short pba_dying[4];                       /* 0x3970F0 soldiers falling (killed in the air) */
extern short pba_end_timer;                      /* 0x3970F8 ms until the mission ends (player dead) */
extern ulong pba_saved_mask;                     /* 0x35B798 game_hook_mask before the player died */
extern uchar pba_edge_flags;                     /* 0x35B794 1 / 2: map edge warnings given */
extern ulong pba_targ_lost_time;                 /* 0x35A940 */
extern long game_35A95C;                         /* 0x35A95C set when the player leaves the map */
extern uchar game_392490;                        /* 0x392490 bit 1 cleared every frame */
extern long pba_enemy_count;                     /* 0x35B790 enemies near the player last frame */
extern long game_440930;                         /* 0x440930 speaker deaths */
extern long pba_snd_count;                       /* 0x35B784 weapon-change sound callbacks */
extern long pba_snd_chan;                        /* 0x35B788 weapon-change sound channel */
extern long pba_snd_chan2;                       /* 0x35B78C weapon ready sound channel */
extern long pba_speed_tab[][2];                  /* 0x35B79C {load limit, speed %} */
extern uchar game_35CADA;                        /* 0x35CADA difficulty (0, 1, 0xFF) */
/* voice_soldier 0x391F4D (speaker kind -> record; g3c called it game_391F4D): g2_a.h */
extern uchar game_39034D[];                      /* 0x39034D 0x1EE-byte records, +0 flag */
extern char pbasim_file[];                       /* 0x355A7C "r:\prj\tn\v109\tn\src\tn\pbasim.c" */

/* ------------------------------------------------------------------ callees in other batches */
/* G1 */
long noisy_add(long obj, long snd);                    /* 0x249388 */
/* G2 */
bool timers_279E80(short *ent, short *slot, uchar type);              /* 0x279E80 class-8 entity */
uchar weapon_ready(uchar *w, void *store);                     /* 0x28F024 */
void weapon_nop(void);                                         /* 0x28F0A0 */
short weapon_fire(uchar *w, long ent, fix x, fix y, fix z, fix tx, fix ty, fix tz, long target,
                  void *store, long real);                     /* 0x28F0B4 */
fix weapon_aim_error(uchar *w, long ent, fix x, fix y, fix z, fix tx, fix ty, fix tz, long target,
                     fix *miss);                               /* 0x28F7FC */
void turret_fire(long i);                                      /* 0x28FD54 */
void turret_alert(long i);                                     /* 0x28FE20 */
void obj_damage(long victim, long attacker, long dtype, uchar *parts, long np, uchar *sys,
                long nsys, uchar *armor, uchar *wlist, long nw, long mode, long amount); /* 0x287218 */
extern uchar game_cls8[][0x0C];                  /* 0x39BB30 */
bool grid_295588(uchar cls, short *ent, short *slot); /* 0x295588 create entity + pool record */
/* G3a */
void damage_27CCA0(uchar cls, uchar type, uchar *out, long n, long mult); /* 0x27CCA0 (g3_a.h) */
void DamageInitSystems(uchar cls, uchar type, uchar *out, long n);        /* 0x27CDE4 (g3_a.h) */
/* G4 */
/* G6 */
/* G7/G8 */

/* more callees (owners' types where they have them; repeated from headers that cannot be
 * included here) */
extern short targ_current;                       /* 0x35A93C (g2_a.h) */
extern uchar game_3970E0;                        /* 0x3970E0 (g2_a.h) */
extern long game_397B70;                         /* 0x397B70 (g2_b.h) */
extern long game_35CD9C;                         /* 0x35CD9C cheats enabled (g1_c.h) */
extern uchar game_442F1B;                        /* 0x442F1B cheat bits (g2_b.h) */
extern uchar game_41DE34;                        /* 0x41DE34 (g1_c.h) */
extern uchar *game_map_35BFB4;                   /* 0x35BFB4 map record (g2_b.h) */
extern long game_391E4C;                         /* 0x391E4C number of speaker records (g1_b.h) */
extern long game_35B77C;                         /* 0x35B77C nonzero: no radio (g1_b.h) */
extern ulong game_441391[8];                     /* 0x441391 (g2_b2.h) */
extern long game_35B7EC;                         /* 0x35B7EC (g2_b2.h) */
extern short game_player_slot;                   /* 0x441352 (g1_c.h) */
void radio_stop_all(void);                       /* 0x2CED40 (g1_b.h) */
void radio_stop_voice(long v);                   /* 0x2CDCD8 (g1_b.h) */
void RadioSpeak(long spk, long msg);             /* 0x2CE620 (g1_b.h) */
void objdmg_drain(long ent, long killer, long amount);           /* 0x286BE0 (g2_b2.h) */
void obj55_288F78(long ent, uchar *p, long v, void *x);          /* 0x288F78 (g2_b.h) */
void simdisp_289730(fix *x, fix *y, fix *z, ushort h, ushort p, ushort b); /* 0x289730 */
fix physics_2D3E74(long ent, fix *x, fix *y, fix *z);            /* 0x2D3E74 (g2_a.h) */
fix biped_27ED34(long ent);                                      /* 0x27ED34 (g2_a.h) */
void vec3_282CFC(short ent, long unused1, long unused2, long unused3, long unused4); /* g3_a.h */
void physics_2D3FB8(long ent);                   /* 0x2D3FB8 G3a */
void physics_2D467C(long ent);                   /* 0x2D467C G3a */
void physics_2D47D8(long ent, fix x, fix y);     /* 0x2D47D8 G3a */
short weapon_find_target(long ent, fix range, long alive, long any); /* 0x28FAAC (g2_b.h) */
/* damage_27C840: g3_a.h (owner g3a) */

#pragma pack(pop)
#endif
