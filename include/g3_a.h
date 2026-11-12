/* g3_a.h -- declarations for worker g3a (batch G3): aircraft creation (M038 aircinit.c),
 * cursor control / damage systems (M040 cursctrl.c + dmgsys.c), class 0 object creation (M043
 * statinit.c), M047 (dmgrec.c), soldier ragdolls (M050 ragdoll.c), vehicles (M062
 * vehsim.c) and the EDMS physics glue (M115 physglue.c: physics init / frame, per-class physics
 * bodies, FF_terrain, Terrain, collision records).
 * Names are ours unless they come from the map, an assert string or an error string.
 * Globals are bound to their original addresses through symbols-g3-a.csv.
 */
#ifndef TN_G3_A_H
#define TN_G3_A_H
#include "lg.h"
#include "fix.h"
#include "game.h"
#include "edms.h"
#include "res.h"
#include "support.h"
#include "2d.h"
#include "g1_a.h"
#include "g1_b.h"
#include "g1_c.h"      /* game_rnd_text, controls, replay */
#include "g2_a.h"      /* includes g2_c.h (objacc, interact) */
#include "g2_b.h"      /* weapons, objmap */
#include "g2_b2.h"     /* obj_damage, objdmg_alert */
#include "g3_c.h"      /* damage_27D1E4/27D140/27E0E0 (owner g3c) */
#include "t1_c.h"
#include "3d_a.h"
#pragma pack(push, 1)

/* fixed-point helpers with the original's exact rounding (imul; shrd 16 / cdq-less idiv) */
#define G3A_FIXMUL(a, b) ((fix)(((long long)(long)(a) * (long)(b)) >> 16))
#define G3A_FIXDIV(a, b) ((fix)((((long long)(long)(a)) << 16) / (long)(b)))
#define G3A_ENT(i) (game_entities[(short)(i)])
/* entity index kept in the low 15 bits of a word (bit 14 sign-extended), as g2c G2C_IDX15 */
#define G3A_IDX15(w) ((short)((short)((ushort)(w) << 1) >> 1))

/* ------------------------------------------------------------------ other batches' declarations
 * g1_c.h, g2_b.h, g2_b2.h and g2_c.h cannot be included together with g2_a.h yet (conflicting
 * types for a few shared symbols; the G2 merge is unifying them), so the symbols used here
 * are declared again, with the owner's types where possible. */
extern long game_359708;                            /* 0x359708 mission kind (g1c/g2b/g2c) */
extern uchar game_cls3[][0x14F];                    /* 0x39E590 class 3 (aircraft) pool records (g2b) */
extern uchar *game_map_35BFB4;                      /* 0x35BFB4 map record (g2b) */
extern ulong game_442439;                           /* 0x442439 (g2b) */
/* g2c: ObjMeter * in g2_c.h */


/* ------------------------------------------------------------------ M038 aircinit.c */
extern short game_35B60C;                           /* 0x35B60C aircraft entity of type 10 */
bool damage_27C120(short *ent, short *slot, uchar f21, uchar type, uchar *data, long v);
bool damage_27C20C(uchar *rec, uchar type, uchar *data, short v);
bool damage_27C360(short ent);                      /* start the aircraft's ambient sound */

/* ------------------------------------------------------------------ M040 cursctrl.c */
/* Sticky cursor control block, 12 bytes */
typedef struct {
    uchar f0, f1;        /* +0 (4), +1 (4) cursor kind (index into cursctrl_refs) */
    short hidden;        /* +2 0 = drawn */
    short x, y;          /* +4 position */
    short w, h;          /* +8 view size */
} CursCtrl;
LG_STATIC_ASSERT(sizeof(CursCtrl) == 12, CursCtrl_size);
extern CursCtrl cursctrl_player;                    /* 0x392460 */
extern CursCtrl cursctrl_aircraft;                  /* 0x39246C */
extern CursCtrl cursctrl_other;                     /* 0x392478 */
extern long game_392484;                            /* 0x392484 */
extern uchar game_392488;                           /* 0x392488 set while damage_27C9C4 runs */
extern long cursctrl_xlim[4];                       /* 0x35B618 */
extern long cursctrl_ylim[5];                       /* 0x35B628 */
extern long cursctrl_refs[];                        /* 0x35B63C resource ids per cursor kind */
extern ushort cursctrl_mask;                        /* 0x35B66C */
void damage_27C840(CursCtrl *c, short w, short h);
CursCtrl *damage_27C880(short ent);
long damage_27C8B8(long pos, long size, short *out);
long damage_27C9C4(long pos, long size, long unused, fix *out);
long damage_27C9F4(long kind);
void CursCtrlDrawStickyCursor(CursCtrl *c, long w, long h);
void damage_27CADC(CursCtrl *out, long w, long h, CursCtrl *in, long ow, long oh);
void damage_27CB58(short *in, fix *out);

/* ------------------------------------------------------------------ M040 dmgsys.c */
extern uchar game_442900;                           /* 0x442900 skill level (g2b name) */
extern uchar game_hp_cls1[][3];                     /* 0x396318 {ushort hp, uchar} per type */
extern uchar game_hp_cls2[][3];                     /* 0x395BB4 */
extern uchar game_hp_cls3[][3];                     /* 0x3963CE */
extern uchar game_hp_cls4[][3];                     /* 0x396178 */
extern ushort game_sys_cls1[][8];                   /* 0x395608 subsystem values per type */
extern ushort game_sys_cls2[][2];                   /* 0x3955D8 */
extern ushort game_sys_cls3[][3];                   /* 0x3962B2 */
extern ushort game_sys_cls4[][1];                   /* 0x395B20 */
extern short game_hp_skill_a[];                     /* 0x396374 percent per skill level */
extern short game_hp_skill_b[];                     /* 0x3963A6 */
extern short game_sys_skill_a[];                    /* 0x39637E */
extern short game_sys_skill_b[];                    /* 0x3963B0 */
void damage_27CCA0(uchar cls, uchar type, uchar *out, long n, long mult);
void DamageInitSystems(uchar cls, uchar type, uchar *out, long n);

/* ------------------------------------------------------------------ M043 statinit.c */
extern uchar game_cls0[][9];                        /* 0x3BA080 class 0 pool records */
bool damage_27D610(short *ent, short *slot, uchar type, uchar *data);
bool damage_27D6E8(uchar *rec, uchar type, uchar *data);

/* ------------------------------------------------------------------ M047 dmgrec.c */
void damage_27E1E0(short *rec, short a, short b, short c, long d);

/* ------------------------------------------------------------------ M050 ragdoll.c */
/* Ragdoll record, 0x3A9 bytes: +0 dword biped state, +4 byte 3, +5 dword frames simulated
 * (age), +0xD skeleton header (0x14 bytes, copied from the biped: +1 segment count, +2 joint
 * count (0x16), +3 normal count (0xB), +8 segment table pointer = rec + 0x21), +0x21 segments
 * (16 bytes: joint a, joint b, normal, +3 flags: 0x10 = frozen), +0xD1 22 joints (fix[3]),
 * +0x1D9 11 normals (fix[3]), +0x25D 11 motions (0x1E bytes: +0 rest length, +4 velocity of
 * joint a, +0x10 of joint b, +0x1C / +0x1D joint on the ground), +0x3A7 short object entity. */
#define RAGDOLL_N 12
#define RAGDOLL_SIZE 0x3A9
extern uchar ragdoll_used[RAGDOLL_N];               /* 0x392500 */
extern uchar ragdoll_recs[RAGDOLL_N][RAGDOLL_SIZE]; /* 0x39250C */
extern fix *ragdoll_cam;                            /* 0x35B6A4 head joint of the player's ragdoll */
extern uchar *ragdoll_cur;                          /* 0x3950F8 record being simulated */
extern fix ragdoll_camdir[3];                       /* 0x3950FC death camera direction (g2c's
                                                       ordn_objtype 0x395100 overlaps [1]) */
extern fix *ragdoll_p0, *ragdoll_p1;                /* 0x395108 0x39510C current segment's joints */
void vec3_2822F0(fix *out);
void vec3_28236C(short *pitch, short *heading);
long vec3_2823DC(void);
void vec3_282450(fix *out, uchar *rec, long seg);
void vec3_2824C8(long seg);
void vec3_2825DC(short slot, fix dt);
void vec3_282CBC(short ent, uchar reason);
void vec3_282CFC(short ent, long unused1, long unused2, long unused3, long unused4);
void vec3_283240(void);
void vec3_283260(void);

/* ------------------------------------------------------------------ M062 vehsim.c */
extern long vehsim_397A40[2];                       /* 0x397A40 result block of vehsim_28ED68 */
/* vehsim_28E850(ulong dt) is declared in g2_a.h */
void vehsim_28E8E4(short vi, long units);
void vehsim_28E9C0(void *ai);
void vehsim_28EAA0(long ent, long mode);            /* mode unused (g2b2's signature) */
bool vehsim_28EC5C(short ent);
void vehsim_28ECC4(void **view, short *heading, short *pitch, short ent, fix *dir);
long *vehsim_28ED68(short ent);
uchar vehsim_28EDC0(short vi, fix x, fix y, fix z, short target, uchar real);
fix vehsim_28EF14(short vi, fix x, fix y, fix z, short target, fix *miss);

/* ------------------------------------------------------------------ M115 physglue.c */
#define G3A_ABS(x) ((x) < 0 ? -(x) : (x))   /* inline cdq / xor / sub */
extern Marble phys_marble_a;                        /* 0x446380 */
extern Marble phys_marble_b;                        /* 0x4463A8 */
extern Marble phys_marble_c;                        /* 0x4463D0 soldier marble (2D4544 / 2D455C) */
extern ulong phys_last_time;                        /* 0x35ED14 time of the last soliton (0 = none) */
extern long ff_bit;                                 /* 0x35ED18 lowest set bit of terrain_bitmask */
extern long phys_checksum;                          /* 0x35ED1C sum of body positions (replay check) */
extern long ff_cache[4];                            /* 0x35ED20 FF_terrain cache: caller, square x, y, mode */
extern uchar ff_fast;                               /* 0x35ED30 */
extern uchar phys_ready;                            /* 0x44659A */
extern long phys_nrec;                              /* 0x446578 */
/* one target of a frame's hits (11 bytes, up to 0x80 in phys_hits): a = hit entity (or bit 31
 * + tile template | group x << 22 | group y << 15, see FF_terrain), b = the one that hit it */
typedef struct {
    long a, b;          /* +0 */
    short dmg;          /* +8 damage summed over the frame */
    uchar dir;          /* +A side it was hit on (physics_2D6644) */
} PhysHit;
LG_STATIC_ASSERT(sizeof(PhysHit) == 11, PhysHit_size);
/* a queued collision (0x26 bytes, 0x20 at 0x442FE0), applied by physics_2D6578 */
typedef struct {
    long ph, a;         /* +0 (passed on, unused by physics_2D672C) */
    short tgt, ent;     /* +8 */
    long dmg, w, g;     /* +0xC */
    short kill;         /* +0x18 entity to kill after the hit (0 = none) */
    uchar pos[12];      /* +0x1A position; physics_2D64E8 copies only its first 4 bytes
                           (memcpy with sizeof of the pointer, original bug) */
} PhysCol;
LG_STATIC_ASSERT(sizeof(PhysCol) == 0x26, PhysCol_size);
extern PhysCol phys_cols[0x20];                     /* 0x442FE0 */
extern long phys_ntiles;                            /* 0x44657C */
extern TileOut *phys_tiles;                         /* 0x446580 frame buffer (0x800): tile objects made real */
extern long phys_nhits;                             /* 0x446584 */
extern PhysHit *phys_hits;                          /* 0x446588 frame buffer (0x580) */
extern long phys_msg[2];                            /* 0x35ED34 message words (+2: the hit entity) */
extern long game_441359;                            /* 0x441359 player ground state (0..2) */
extern long game_36F490;                            /* 0x36F490 */
extern ulong terrain_bitmask;                       /* 0x41DD84 */
void physics_2D3690(void);
void physics_2D3694(void);
void physics_2D37B8(void);
void physics_2D38BC(void);
void PhysicsFrame(void);
int physics_2D398C(void);
void PhysicsSoliton(ulong t);
void physics_2D3B54(void);
void physics_2D3BFC(short ent, StateAccel *s);
void physics_2D3C80(short ent);
fix physics_2D3E74(long ent, fix *x, fix *y, fix *z);
void physics_2D3EE4(short ent, long ph);
bool physics_2D3F20(short ent);
void physics_2D3F78(Marble *dst, Marble *src);
void physics_2D3FB8(long ent);                      /* long as g3_c.h */
void physics_2D4174(short ent);
void physics_2D42E0(short ent);
void physics_2D43DC(short ent);
void physics_2D44C0(short ent);
void physics_2D4544(Marble *out);
void physics_2D455C(Marble *in);
fix physics_2D45CC(short ent);
void physics_2D467C(long ent);                      /* long as g3_c.h */
void physics_2D4794(short ent, fix xd, fix yd);
void physics_2D47D8(long ent, fix x, fix y);        /* long as g3_c.h */
void physics_2D4A70(void);
/* terrain (FF_terrain / Terrain: edms.h) */
extern uchar game_44659C;                           /* 0x44659C terrain type under the last FF_terrain
                                                       (g3b name) */
extern long ff_nobj;                                /* 0x4465A0 objects in ff_objs */
extern short ff_objs[0x15];                         /* 0x4465A2 [1..20] objects of the square */
extern fix ff_h11, ff_h10, ff_h01, ff_h00;          /* 0x4465CC.. corner heights of the square */
extern uchar *ff_p11, *ff_p10, *ff_p01, *ff_p00;    /* 0x4465DC.. its corner map squares */
extern uchar *terrain_cornerbits;                   /* 0x41DD60 [type * 4 + corner] -> bit of terrain_bitmask */
extern uchar terrain_typebits[0x80];                /* 0x41DA30 [ff_bit * 8 + type / 8] bit set of types */
extern uchar game_41DA28[];                         /* 0x41DA28 types with a fixed ground kind (g3b) */
extern uchar *game_41DD58;                          /* 0x41DD58 type -> ground kind (g3b) */
extern fix terrain_kinds[][3];                      /* 0x41DB50 ground kind: traction, friction, +8 sound */
extern uchar terrain_kindtab[][2];                  /* 0x41DAD0 per type: two kinds (nibbles), split mode */
bool physics_2D5C90(fix X, fix Y, fix Z, bool fast, terrain_ff *tff);
ulong physics_2D637C(fix x, fix y, uchar t);       /* g3_b.h: uchar; the original returns a
                                                       zero-extended dword that FF_terrain indexes with */
void physics_2D64E8(long ph, long a, long tgt, long ent, long dmg, long w, long g, long z,
                    fix *pos);              /* as g2_a.h (included) */
void physics_2D6578(void);
void physics_2D65F8(long obj);                      /* as g2_c.h */
uchar physics_2D6644(long ent, long from);          /* as g2_b.h */
void physics_2D672C(long ph, long a, long tgt, long ent, long dmg, long w, long g, uchar *pos);
void physics_2D6A44(long ph1, long ph2, long d1, long d2, long d3, long d4);
void physics_2D6A70(long blast, long unused, long ent);
void physics_2D6C18(long blast, long unused, long tmpl, long gx, long gy);
void physics_2D6D44(long blast, long slot);
void physics_2D70C0(void);
void physics_2D7140(long tile, long x, long y);     /* as g2_b.h */
void physics_2D7178(void);
long physics_2D7718(long ent);
void physics_2D777C(long ph, long ent);
void physics_2D7788(long ent);                      /* as g2_b.h */
void physics_2D77B8(long ph);
void physics_2D77DC(long ph);
void physics_2D7824(long ph, long d1, long d2);
/* callees */
extern uchar game_cls8[][0x0C];                     /* 0x39BB30 (g2b): +6 owner, +8 hit, +0xB */
void biped_2814E4(short ent, fix dt, long now);     /* 0x2814E4 G3b (as g3_b.h) */
void biped_27EE34(short ent);                       /* 0x27EE34 G3b (as g3_b.h) */
bool biped_27EDA0(short entity);                    /* 0x27EDA0 G3b (as g1_c.h) */
extern uchar game_cls5[][0x1E];                     /* 0x39BDF0 (g2b) */
extern uchar ordn_objtype[][0x24];                  /* 0x395100 (g2c) +0x13 steps in physics */
extern short game_396868;                           /* 0x396868 (g2c) */

/* ------------------------------------------------------------------ callees in other batches */
/* functions owned by G1/G2/G3b/G3c come from their owners' headers (included above; G2
 * merge); the rest are callees in later batches */
extern long game_35CD9C;                            /* 0x35CD9C cheats enabled */
extern uchar game_442F1B;                           /* 0x442F1B cheat bits */
extern uchar game_cls6[][0x2C];                     /* 0x39A530 (g2c) +0 entity, +0xA ragdoll index, +0xE resting */
void BipedComputePose(void *biped, long time);      /* 0x280934 G3b (M049) */
fix biped_281750(void *skel, long seg, fix scale);  /* 0x281750 G3b (M049) segment length * scale */

#pragma pack(pop)
#endif
