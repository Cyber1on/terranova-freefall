/* g4_a.h -- declarations for worker g4a (batch G4, AI): the AI main / squad loop (M005,
 * src/game/ai/aimain.c), side helpers (M006, aisquad.c) and the first part of ailow.c (M007
 * 0x24ADE0-0x25129C, ailowa.c: per-brain low-level AI).
 * Names are ours unless they come from the map, an assert string or an error string.
 * Globals are bound to their original addresses through symbols-g4-a.csv.
 *
 * AI records (game.h has the sizes; most fields are only known by offset, so the code uses the
 * byte accessors below with the offset in the name of the access):
 *
 * Side (GameSide, 0x489 x 4 at 0x388140): +0 active, +1 MsgAddr (a = 0x80 / 0x81 / 0x82...,
 *   b = side index), +5 side number, +6 first squad, +7 "run the side loop even when inactive",
 *   +8 0xC0 x 6-byte sighting records {short a, b, ent} (aisquad_24A9B8), +0x488 flag (message 0xB).
 * Squad (GameSquad, 0x170 x 32 at 0x3804F0; 0 heads the free list through +0xE):
 *   +0 active, +1 used, +2 "frozen" (brains only flush their mailbox), +3 MsgAddr, +7 side,
 *   +0xD side copy, +0xE next squad of the side / free list, +0xF first brain, +0x10 brain
 *   count, +0x11 count of brains created, +0x1E formation (0..2), +0x90 order kind,
 *   +0x166 next squad in a ring (message fan-out), +0xBF word target entity, +0xCB order state.
 * Brain (GameBrain, 0x14E x 60 at 0x3832F0; 0 heads the free list through +7):
 *   +0 active, +1 MsgAddr (a = 0x80, b = brain index; +3 = the index byte), +5 side,
 *   +6 squad, +7 next brain of the squad / free list, +8 rank in the squad (1-based),
 *   +9 dword whose high word (+0xB) is the entity, +0xA "alt think" (player-controlled brain),
 *   +0xD class (1 soldier, 2 vehicle, 3 installation, 4 aircraft), +0xE/+0x10 map square x/y,
 *   +0x30 three 0x24-byte order records (aimain_249F48 ...), +0x9C .. +0x14D state.
 */
#ifndef TN_G4_A_H
#define TN_G4_A_H
#include "lg.h"
#include "fix.h"
#include "game.h"
#include "t1_c.h"
#include "edms.h"
#include "g1_a.h"
#include "g2_a.h"
#include "g1_b.h"      /* game_359708, game_35BF48, game_391E4C / 391E50 */
#include "g1_c.h"      /* game_rnd_text, game_41DE33 */
#include "g2_b.h"      /* game_cls3/4/5/8, game_map_35BFB4 */
#include "g2_c.h"      /* strike_*, game_35B6E0, game_396868 */
#include "g3_a.h"      /* terrain_bitmask, game_41DD58 */
#include "g3_c.h"      /* air_alt, damage_27D158 */
#include "g3_b.h"      /* biped_*, damage_27D720 / 27CF30 callers */
#pragma pack(push, 1)

/* byte-offset accessors */
#define G4_U8(p, o)  (*(uchar *)((uchar *)(p) + (o)))
#define G4_S8(p, o)  (*(signed char *)((uchar *)(p) + (o)))
#define G4_U16(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define G4_S16(p, o) (*(short *)((uchar *)(p) + (o)))
#define G4_U32(p, o) (*(ulong *)((uchar *)(p) + (o)))
#define G4_S32(p, o) (*(long *)((uchar *)(p) + (o)))

#define G4_BRAIN(i)  ((uchar *)&game_brains[(uchar)(i)])
#define G4_SQUAD(i)  ((uchar *)&game_squads[(uchar)(i)])
#define G4_SIDE(i)   ((uchar *)&game_sides[(signed char)(i)])
#define G4_ENT(i)    (game_entities[(short)(i)])
/* a brain's entity: the high word of the dword at +9 */
#define G4_BENT(b)   G4_S16(b, 0xB)
/* (v >> 16 << 16) | 0x8000: the centre of map square v (v = 16.16 or a square index << 16) */
#define G4_SQCENTRE(w) ((long)(((ulong)(long)(short)(w) << 16) | 0x8000))

/* fixed-point helpers with the original's exact rounding (imul; shrd 16 / 64-bit idiv) */
#define G4_FIXMUL(a, b) ((fix)(((long long)(long)(a) * (long)(b)) >> 16))
#define G4_FIXDIV(a, b) ((fix)((((long long)(long)(a)) << 16) / (long)(b)))

/* ------------------------------------------------------------------ AI globals (shared ones in game.h) */
extern ulong ai_371170;                   /* 0x371170 game_frame of the last "squad mate hit" */
extern uchar ai_player_dead;              /* 0x371174 set once by aimain_24A73C */
extern ulong ai_371180;                   /* 0x371180 game_time of the last friendly-fire warning */
extern long ai_371184;                    /* 0x371184 friendly-fire warnings given */
extern ulong ai_reinforce_time;           /* 0x3598D4 game_time of the last reinforcement call */
extern ulong ai_debug_mask80;             /* 0x3598EC debug mask for MsgAddr.a 0x80 (brains) */
extern ulong ai_debug_mask81;             /* 0x3598F0 debug mask for 0x81 (squads) */
extern ushort ai_debug_mask82;            /* 0x3598F4 debug mask for other kinds (sides) */
extern short ai_reinforce_max;            /* 0x441276 */
extern uchar ai_cheat_flags;              /* 0x44127B bit 0 vehicles, bit 1 installations ignored */
extern ulong ai_fire_mask;                /* 0x35BF60 weapon slots fired with WeaponFire */
extern fix ai_25AB88[25][2];             /* 0x25AB88 squad formation offsets in the code segment:
                                            [type * 6 + rank] (rank 1-based, type 0..3);
                                            0x25AB88 / 0x25AB8C are map rows aimsg_25AB88 /
                                            aiacc_25AB8C, 0x25AC18 is [18] (type 3) */
extern long ai_3598DC, ai_3598E0, ai_3598E4; /* 0x3598DC.. weapon score weights: range, damage
                                            (or installation type word), rate (ailow_250E58) */

/* ------------------------------------------------------------------ other batches (orig rows) */
extern uchar game_441354;                 /* 0x441354 */
extern uchar game_441365;                 /* 0x441365 */


/* ------------------------------------------------------------------ aimain.c (M005) */
void aimain_249A80(void);                                       /* 0x249A80 AI init */
void aimain_249B1C(void);                                       /* 0x249B1C AI shutdown */
void aimain_249B2C(void);                                       /* 0x249B2C reset tables */
void aimain_249D18(long ent, long reason);                      /* 0x249D18 kill callback */
void aimain_249DCC(void);                                       /* 0x249DCC */
void AISquadLoop(void);                                         /* 0x249E24 frame hook 3 */
void aimain_249F48(uchar *ords);                                /* 0x249F48 */
void aimain_249F6C(uchar *ords, long mask);                     /* 0x249F6C */
void aimain_249FD0(uchar *ords);                                /* 0x249FD0 */
long aimain_24A050(uchar *ords, long kind);                     /* 0x24A050 */
long aimain_24A088(uchar *ords, long kind);                     /* 0x24A088 */
long aimain_24A0B8(uchar *ords);                                /* 0x24A0B8 */
long aimain_24A0F4(uchar *ords);                                /* 0x24A0F4 */
uchar *aimain_24A120(void);                                     /* 0x24A120 */
void aimain_24A158(uchar *path);                                /* 0x24A158 */
void aimain_24A184(void);                                       /* 0x24A184 */
void aimain_24A26C(long brain, long attacker);                  /* 0x24A26C */
uchar aimain_24A3E0(long e1, long e2, fix x1, fix y1, fix z1, fix x2, fix y2, fix z2); /* 0x24A3E0 */
void aimain_24A65C(uchar *path);                                /* 0x24A65C */
void aimain_24A6A0(long brain, long what);                      /* 0x24A6A0 */
void aimain_24A73C(void);                                       /* 0x24A73C */
uchar aimain_24A760(void);                                      /* 0x24A760 */
void aimain_24A768(long ent);                                   /* 0x24A768 */
uchar aimain_24A7B8(long squad, ulong xy, long c);              /* 0x24A7B8 */

/* ------------------------------------------------------------------ aisquad.c (M006) */
void aisquad_24A820(long side);                                 /* 0x24A820 */
void aisquad_24A88C(uchar *side);                               /* 0x24A88C */
void aisquad_24A9B8(uchar *side, long quiet);                   /* 0x24A9B8 */

/* ------------------------------------------------------------------ ailowa.c (M007 part 1) */
uchar ailow_24ADE0(uchar *br);                                  /* 0x24ADE0 */
void ailow_24AE4C(long brain, long weapon, long sel);           /* 0x24AE4C */
void ailow_24AF24(uchar *br, short *out);                       /* 0x24AF24 */
void ailow_24AFB4(uchar *br, short *avg, short *worst, short *armour); /* 0x24AFB4 */
void ailow_24B24C(void);                                        /* 0x24B24C */
void ailow_24B2E0(void);                                        /* 0x24B2E0 */
void ailow_24B34C(long brain);                                  /* 0x24B34C */
short AILowInit(long cls, long type, long squad, fix x, fix y, uchar *tmpl); /* 0x24B39C */
long ailow_24B8A0(long type, long squad, fix x, fix y, uchar *tmpl);         /* 0x24B8A0 (original: short; long as in g3_c.h) */
short ailow_24B8DC(long type, long squad, fix x, fix y, uchar *tmpl);        /* 0x24B8DC */
short ailow_24B900(long type, long squad, fix x, fix y, uchar *tmpl);        /* 0x24B900 */
short ailow_24B924(long type, long squad, fix x, fix y, void *tmpl);        /* 0x24B924 */
void ailow_24B948(long brain);                                  /* 0x24B948 */
void ailow_24B9B8(long to_enemy);                               /* 0x24B9B8 */
uchar ailow_24BA28(long unused, long squad);                    /* 0x24BA28 */
void ailow_24BB1C(long brain, long v);                          /* 0x24BB1C */
uchar ailow_24BB30(long brain);                                 /* 0x24BB30 */
void ailow_24BBA0(uchar *br);                                   /* 0x24BBA0 */
uchar ailow_24BD00(uchar *br);                                  /* 0x24BD00 */
void ailow_24BF3C(uchar *br, long force);                       /* 0x24BF3C */
void ailow_24C618(uchar *br);                                   /* 0x24C618 mailbox */
void ailow_24D5B8(uchar *br);                                   /* 0x24D5B8 */
void ailow_24D750(uchar *br);                                   /* 0x24D750 */
void ailow_24D98C(uchar *br);                                   /* 0x24D98C */
void ailow_24DAC4(uchar *br, long level, long rnd);             /* 0x24DAC4 */
void ailow_24DBC4(uchar *br);                                   /* 0x24DBC4 */
void ailow_24DCBC(uchar *br);                                   /* 0x24DCBC */
void ailow_24DD18(uchar *br);                                   /* 0x24DD18 */
void ailow_24DD34(uchar *br);                                   /* 0x24DD34 */
void ailow_24DDD8(uchar *br);                                   /* 0x24DDD8 */
void ailow_24DEA0(uchar *br);                                   /* 0x24DEA0 */
void AISoldierThink(uchar *br);                                 /* 0x24DF10 */
void ailow_24E040(uchar *br);                                   /* 0x24E040 */
void ailow_24E100(long ang, long dist, fix *out);               /* 0x24E100 */
void ailow_24E19C(uchar *br, fix *out);   /* 0x24E19C: Watcom entry W_ailow_24E19C (eax, esi) */
void ailow_24E5B4(uchar *br, long dx, long dy, fix *ox, fix *oy); /* 0x24E5B4 ret 4 */
uchar ailow_24E750(long heading, long prio);                    /* 0x24E750 */
void ailow_24E774(uchar *br);                                   /* 0x24E774 */
uchar ailow_24E9B8(long rank, long squad);                      /* 0x24E9B8 */
uchar ailow_24EA04(uchar *br);                                  /* 0x24EA04 */
void ailow_24EA7C(uchar *br, long dx, long dy);                 /* 0x24EA7C (3 args: ebx = dy) */
void ailow_24F704(uchar *br);                                   /* 0x24F704 */
void ailow_24F778(uchar *br);                                   /* 0x24F778 */
uchar ailow_24FC04(uchar *br, long u1, long u2, long u3, fix x, fix y, long prio); /* ret 0xC */
uchar ailow_24FCFC(uchar *br, ulong sq_xy, long prio);          /* 0x24FCFC */
uchar ailow_24FD3C(uchar *br, long flag, long mode, long prio, uchar *res); /* 0x24FD3C ret 4 */
void ailow_24FF24(uchar *br, fix *out);                         /* 0x24FF24 */
uchar ailow_2500EC(uchar *br);                                  /* 0x2500EC */
uchar ailow_250668(uchar *p);                                   /* 0x250668 */
uchar ailow_250674(uchar *br, long ent);                        /* 0x250674 */
uchar ailow_2508D0(long e1, long e2);                           /* 0x2508D0 */
uchar ailow_250A1C(uchar *br);                                  /* 0x250A1C */
void ailow_250CE0(uchar *br, fix *near, fix *tgt);              /* 0x250CE0 */
short ailow_250E58(uchar *br, long flag);                       /* 0x250E58 */

#pragma pack(pop)

/* The rest of the AI (G4b, G4c1, G4c2; merged at the G4 merge): one declaration per symbol, in its
 * owner's header; g4_b.h includes g4_c1.h, which includes g4_c2.h.  Included after g4a's own
 * declarations, so every AI C file includes this header first and sees all of them. */
#include "g4_b.h"
#endif
