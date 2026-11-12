/* Batch G3, worker g3b: units and physics -- multi-part object groups (M039 partgroup.c),
 * class 4 part objects (M041 partobj.c), PBA / soldier record creation (M044 pbacreate.c),
 * the biped walker (M049 biped.c) and the terrain / object raycast (M118 raycast.c).
 * Files: src/game/units/{partgroup,partobj,pbacreate,biped}.c, src/game/physics/raycast.c.
 *
 * Function names: the binary's own (FF_raycast, FF_raycast_generic_obj), community names
 * (BipedComputePose, BipedIdleTurn, RaycastCylinder) and the map's placeholders otherwise
 * (meaning in the file headers).  Globals of these modules are named after their use; globals
 * of other modules keep neutral names.  Structs are byte-packed like the original.
 *
 * Header use: includes g3_c.h (which includes g2_c.h, g1_a.h); g3_a.h cannot be included
 * together with g3_c.h yet.  Callees declared by headers that cannot be included here are
 * repeated with the owner's exact types.
 */
#ifndef TN_G3_B_H
#define TN_G3_B_H
#include "game.h"
#include "fix.h"
#include "res.h"
#include "support.h"
#include "g1_a.h"
#include "g2_c.h"
#include "g3_c.h"
#include "g3_a.h"      /* physics_2D637C (owner g3a) */
#include "edms.h"
#include "3d.h"

#pragma pack(push, 1)

#define G3B_ENT(i) (game_entities[(short)(i)])
#define G3B_FIXMUL(a, b) ((fix)(((long long)(long)(a) * (long)(b)) >> 16))       /* imul; shrd 16 */
#define G3B_FIXDIV(a, b) ((fix)((((long long)(long)(a)) << 16) / (long)(b)))      /* cdq-less idiv */

/* PBA type table (game.h game_pba_types, 0x46 bytes per type) fields read here */
#define PBAT_W(k, off) (*(ushort *)&game_pba_types[(uchar)(k)][off])
#define PBAT_L(k, off) (*(long *)&game_pba_types[(uchar)(k)][off])
#define PBAT_B(k, off) (game_pba_types[(uchar)(k)][off])

/* ------------------------------------------------------------------ pools */
/* class 7 pool record (10 bytes at 0x39BCB0): a group of class 4 parts */
typedef struct {
    short w0;            /* +0 pool header word */
    short next;          /* +2 */
    short w4;            /* +4 */
    short count;         /* +6 parts */
    short last;          /* +8 last part (class 4 slot); parts are linked through cls4 +8 */
} PartGroup;
LG_STATIC_ASSERT(sizeof(PartGroup) == 10, PartGroup_size);
extern PartGroup game_cls7[];                    /* 0x39BCB0 */
#define CLS4_W(slot, off) (*(short *)&game_cls4[(short)(slot)][off])

/* part layout item of a group resource (0x12 bytes) */
typedef struct {
    uchar kind;          /* +0x0 class 4 type */
    uchar g1[3];
    fix x, y, z;         /* +0x4 offset from the group position */
    ushort heading;      /* +0x10 */
} PartItem;
LG_STATIC_ASSERT(sizeof(PartItem) == 0x12, PartItem_size);

/* ------------------------------------------------------------------ M039 partgroup.c */
short damage_27C3F0(Ref key, fix x, fix y, ushort heading);
void damage_27C5A4(short grp, Ref key, fix x, fix y, ushort unused);
void damage_27C70C(short ent, short slot, uchar kill);
void damage_27C7CC(short ent);

/* ------------------------------------------------------------------ M041 partobj.c */
extern long game_35B670;                         /* 0x35B670 entity of the last type-0x3E part */
extern RndStream game_rnd_35B674;                /* 0x35B674 (generator pointer at 0x35B678) */
extern ushort game_395728[][2];                  /* 0x395728 class 4 meter max / rate (integer parts) */
extern uchar game_35B610[];                      /* 0x35B610 default class 4 template */
/* damage_27CF30: g2_c.h prototype (ent *, slot *, f21, kind, template *, a6) */
uchar damage_27D018(uchar *rec, uchar kind, uchar *tmpl, short a6);

/* ------------------------------------------------------------------ M044 pbacreate.c */
extern short game_396386[];                      /* 0x396386 percent by skill level (read as
                                                    dword >> 16, i.e. the word at +2 + 2 * skill) */
extern uchar game_441369[];                      /* 0x441369 shared system record of kinds 0xE..0x11 */
uchar damage_27D720(short *ent, short *slot, uchar f21, uchar kind, uchar b2, uchar *tmpl);
uchar damage_27D818(uchar *rec, uchar kind, uchar b2, uchar *tmpl, long unused);
uchar damage_27DBB8(uchar kind);
long damage_27DBD4(uchar *rec, short ent);
long damage_27DC2C(uchar *rec, short ent);


/* ------------------------------------------------------------------ M118 raycast.c */
/* module state of FF_raycast (map coordinates: x, y / 6, z * 4; cells of 1.0) */
extern terrain_ff *rc_tff;                       /* 0x446720 */
extern long rc_sy, rc_sx;                        /* 0x446724, 0x446728 direction signs (y, x) */
extern fix rc_fy, rc_fx;                         /* 0x44672C, 0x446730 position in the cell */
extern fix rc_dx, rc_dy, rc_dz;                  /* 0x446734 .. 0x44673C direction (dz rescaled) */
extern fix rc_nx, rc_ny, rc_nz;                  /* 0x446740 .. 0x446748 end of the current piece */
extern fix rc_ex, rc_ey, rc_ez;                  /* 0x44674C .. 0x446754 end point */
extern long rc_ecy, rc_cy;                       /* 0x446758, 0x44675C end cell y, cell y */
extern long rc_tcx, rc_tcy;                      /* 0x446760, 0x446764 4x4 tile cell */
extern long rc_ecx, rc_cx;                       /* 0x446768, 0x44676C end cell x, cell x */
extern fix rc_x, rc_y, rc_z;                     /* 0x446770 .. 0x446778 current point */
/* tile_groups / tile_objs: g2_a.h (the g3b_tile_* aliases were removed at the G3 merge) */

void raycast_2DA0D0(short *hit_ent, short *hit_tile, fix *dist, long *is_tile);
void raycast_2DA3C0(long *tri, long entry, long exit);
long raycast_2DA454(fix fx, fix fy);
void raycast_2DA48C(fix *ox, fix *oy, fix *oz, fix *fx, fix *fy, long *edge);
/* FF_raycast: edms.h prototype */
bool raycast_2DB1A4(short ent, fix x, fix y, fix z, fix nx, fix ny, fix nz, fix size, fix *t);
bool raycast_2DB20C(ushort tmpl, fix *pos, fix x, fix y, fix z, fix nx, fix ny, fix nz, fix size, fix *t);
bool FF_raycast_generic_obj(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix nx, fix ny, fix nz,
                            fix size, fix *t);
fix raycast_2DB310(fix *m);
bool raycast_2DB3B4(fix *p, fix a, fix b, fix c, fix dx, fix dy, fix dz, fix *t);
bool raycast_2DB4B4(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix nx, fix ny, fix nz, fix size, fix *t);
bool raycast_2DB8D8(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix nx, fix ny, fix nz, fix size, fix *t);
bool RaycastCylinder(fix *pos, ShapeItem *s, fix x, fix y, fix z, fix nx, fix ny, fix nz, fix size, fix *t);

/* ------------------------------------------------------------------ M049 biped.c */
/* module statics (biped record = PBA + 0xDE; offsets in biped.c) */
extern fix bp_cs[2];                             /* 0x3924B0 cos, sin of the heading */
extern fix bp_mv[2];                             /* 0x3924B8 unit direction of motion */
extern fix bp_p[2];                              /* 0x3924C0 step reference point */
extern fix bp_fwd;                               /* 0x3924C8 */
extern fix bp_halfw;                             /* 0x3924CC stance width */
extern fix bp_lift;                              /* 0x3924D0 foot lift of the next step */
extern long bp_now;                              /* 0x3924D4 */
extern uchar *bp_cur;                            /* 0x3924D8 biped record */
extern fix bp_stride;                            /* 0x3924DC */
extern fix bp_dt;                                /* 0x3924E0 */
extern fix *bp_joints;                           /* 0x3924E4 bp_cur + 0xD1, 16 joints */
extern fix bp_lead;                              /* 0x3924E8 */
extern fix bp_v[3];                              /* 0x3924EC velocity x, y; 0x3924F4 pelvis z speed */
extern ushort bp_heading;                        /* 0x3924F8 */
extern long game_441361;                         /* 0x441361 footstep counter of the player */
extern uchar game_44659C;                        /* 0x44659C last ground byte (biped_27E250) */
extern uchar game_41DA28[];                      /* 0x41DA28 bit per ground byte: kind from the table */
extern uchar *game_41DD58;                       /* 0x41DD58 ground byte -> kind */
extern uchar game_41DB58[];                      /* 0x41DB58 kinds, 12 bytes each: +0 sound kind */
extern uchar game_35B680[];                      /* 0x35B680 size of the speaker bipeds */

uchar biped_27E250(fix x, fix y, fix z, terrain_ff *tff);
void biped_27E354(fix *out, fix *a, fix *b);
void biped_27E370(fix *out, fix *a, fix *b);
void biped_27E38C(fix *out, fix *a, fix s);
fix biped_27E3B0(fix *a, fix *b);
fix biped_27E3D8(fix *a, fix *b);
void biped_27E400(fix *out, fix *v);
void biped_27E470(long leg);
uchar biped_27E514(uchar *step, long now, long leg);
void biped_27E678(uchar *step, long now);
void biped_27E778(fix speed);
fix biped_27E8C0(fix *a, fix *b, fix *c);
void biped_27E8FC(short ent, ushort ang, fix rate);
void biped_27E99C(short ent, uchar low);
void biped_27EA60(short ent);
uchar biped_27EAB8(short ent);
void biped_27EAF4(short ent, fix *sx, fix *sy);
void biped_27EBD4(short ent, fix *out);
void biped_27EC90(fix *out, short ent);
fix biped_27ED34(long ent);                      /* as g2_a.h, g3_c.h */
bool biped_27EDA0(short entity);                 /* as g1_c.h */
uchar biped_27EDF4(short ent);
void biped_27EE34(short ent);
ushort biped_27EFE4(uchar *biped);
long biped_27F00C(short ent);
uchar biped_27F0D0(long i);
void biped_27F0DC(void);
void biped_27F150(long ent, long kind);
void biped_27F2A4(long sign, long leg);
void biped_27F620(fix *dest, uchar *step);
void biped_27F658(void);
void biped_27FB28(long leg, uchar flag);
void biped_27FE10(fix *out, long leg, ushort turn);
void biped_27FED8(short *ang, fix *cs, fix *xy, fix rate, long left);
long biped_27FFDC(void);
void biped_2800BC(fix *pos, fix *dur, long leg);
uchar biped_280254(long leg);
uchar biped_280364(long leg, fix d, fix t, long kind);
void biped_280544(void);
void biped_28059C(void);
void BipedComputePose(void *biped, long time);   /* as g3_a.h */
void BipedIdleTurn(short ent, fix dt, long now);
void biped_281488(fix *out, fix *a, fix *b, fix t);
void biped_2814E4(short ent, fix dt, long now);
fix biped_281728(void *skel, long seg, fix scale);
fix biped_281750(void *skel, long seg, fix scale);   /* as g3_a.h */
long biped_28176C(void *skel, long seg);
void biped_2817B0(uchar *biped, long k);
long biped_2820A4(short ent);
void biped_28213C(short ent, short *box);

/* ------------------------------------------------------------------ other batches */
extern uchar game_442900;                        /* 0x442900 skill level (g2_a.h) */
extern short last_shooter;                       /* 0x4464B8 (g2_a.h); really a short per physics
                                                    handle: the entity a ray must ignore */
extern uchar *game_map_35BFB4;                   /* 0x35BFB4 (g2_b.h) map: +0x84E row pointers */
extern uchar game_3C5E1C[128][128];              /* 0x3C5E1C (g2_a.h) tile group per 4x4 squares */
/* physics_2D637C: g3_a.h (owner g3a; returns the zero-extended dword) */
extern ulong game_442439;                        /* 0x442439 (g2_b.h) */
extern uchar game_low_mem;                       /* 0x35971E (g1_b.h) */

#pragma pack(pop)
#endif
