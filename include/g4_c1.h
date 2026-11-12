/* g4_c1.h -- declarations for worker g4c1 (batch G4, AI): the second part of the squad
 * perception / behaviour module M009 (0x258908-0x25A110, src/game/ai/aisqbhv.c), the squad
 * command / script message module M010 (0x25A110-0x25AB8C, aicmd.c) and the first half of the
 * AI message packers M011 (0x25AC50-0x25AF33, aimkmsg.c; the rest is g4c2's).
 * Names: the map's placeholders are kept (other batches call them); meanings are in the file
 * headers.  Globals are bound to their original addresses through symbols-g4-c1.csv.
 *
 * Records are reached through byte offsets (game.h has the sizes only).  Squad offsets used
 * here (GameSquad, 0x170 x 32 at 0x3804F0; see also include/g4_a.h):
 *   +0x00 active   +0x01 "in use" (0 = on the free list)   +0x03 MsgAddr (dword)
 *   +0x07 side     +0x0D side index   +0x0E next squad   +0x0F first brain   +0x10 brain count
 *   +0x12/+0x14 map square x / y (shorts; the dword at +0x12 is the packed position)
 *   +0x1E formation   +0x24 order queue (0x24-byte records: +0 kind, +1 priority, +3 point,
 *         +0xF entity; aimain_249FD0 ...)   +0x37 current move {kind, point}
 *   +0x90 "moving" flag   +0x91/+0x93 destination x / y   +0x95 path record (pointer)
 *   +0xA9 entity to attack   +0xAF   +0xB0 path timer   +0xB4 / +0xB5 / +0xB9 / +0xBA
 *   +0xBE enemy squad   +0xBF enemy entity (short)   +0xC1 enemy known   +0xC2 enemy entity valid
 *   +0xC3 engagement state 0..4   +0xC5/+0xC7 enemy position x / y   +0xCB order state
 *   +0xD1 enemy check timer   +0xE6   +0xE8 combat music sent   +0xE9 .. +0x10F combat state
 *   (reset by aiperc_2598D0)   +0x110 alert level   +0x111 alert timer   +0x123 region mask
 *   +0x127 patrol region list [4] (0xFF ends)  +0x12B its index  +0x12C region list [16]
 *   +0x14C its index   +0x14E "commanded by the mission script"   +0x14F word   +0x151 timer
 *   +0x156 skill (0..100)   +0x157 call-for-help state   +0x163 squad to help   +0x169
 * Brain offsets: +1 MsgAddr, +6 squad, +7 next, +9 dword (high word +0xB = entity), +0xD class
 * (1 soldier, 2 vehicle, 3 installation, 4 aircraft), +0xE/+0x10 map square x/y (dword +0xE).
 * Positions ("points") are passed as one ulong: x in the low word, y in the high word.
 */
#ifndef TN_G4_C1_H
#define TN_G4_C1_H
#include "lg.h"
#include "fix.h"
#include "game.h"
#include "t1_a.h"
#include "t1_c.h"
#include "g1_c.h"
#include "g2_c.h"
#include "g4_c2.h"     /* aiacc_25AF* .. 25B2B0, PathPlan (owner g4c2) */
#pragma pack(push, 1)

/* byte-offset accessors */
#define C1_U8(p, o)  (*(uchar *)((uchar *)(p) + (o)))
#define C1_S8(p, o)  (*(signed char *)((uchar *)(p) + (o)))
#define C1_U16(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define C1_S16(p, o) (*(short *)((uchar *)(p) + (o)))
#define C1_U32(p, o) (*(ulong *)((uchar *)(p) + (o)))
#define C1_S32(p, o) (*(long *)((uchar *)(p) + (o)))
#define C1_SQ(i)     ((uchar *)&game_squads[(uchar)(i)])
#define C1_BR(i)     ((uchar *)&game_brains[(uchar)(i)])
#define C1_ENT(i)    (game_entities[(short)(i)])
#define C1_PT_X(p)   ((short)(ulong)(p))
#define C1_PT_Y(p)   ((short)((ulong)(p) >> 16))
#define C1_PT(x, y)  ((ulong)(ushort)(x) | ((ulong)(ushort)(y) << 16))

/* ------------------------------------------------------------------ globals (symbols-g4-c1.csv) */
extern uchar aiperc_enemy_checked;   /* 0x371194 aiperc_258908 ran (cleared by aiperc_2564C4 area, G4b) */
extern uchar aiperc_enemy_found;     /* 0x371196 its cached result */
extern long aicmd_arg[4];            /* 0x3711A0 arguments of the script's squad command */
extern uchar aicmd_cmd;              /* 0x3711B0 squad command set by the AI script (0 = none) */
extern ushort ai_script_id;          /* 0x36F4C0 id of the AI script thread (set by MissionSetup) */
extern long *game_442F40;            /* 0x442F40 mission timers: [0] bit mask, [1+i] / [0x21+i] ms */

/* ------------------------------------------------------------------ aisqbhv.c (M009 part 2) */
uchar aiperc_258908(void *sq);                                  /* 0x258908 */
uchar aiperc_258C7C(void *sq, ulong pt, long prio);             /* 0x258C7C */
void aiperc_258CE8(void *sq);                                   /* 0x258CE8 */
void aiperc_258EEC(void *sq);                                   /* 0x258EEC */
void aiperc_258F98(void *sq);                                   /* 0x258F98 */
void aiperc_259120(void *sq);                                   /* 0x259120 */
void aiperc_2591B4(void *sq);                                   /* 0x2591B4 */
void aiperc_2593DC(void *sq);                                   /* 0x2593DC */
void aiperc_2593E4(void *sq);                                   /* 0x2593E4 */
void aiperc_25946C(void *sq);                                   /* 0x25946C */
void aiperc_259498(void *sq);                                   /* 0x259498 */
void aiperc_2595C0(void *sq);                                   /* 0x2595C0 */
void aiperc_2596E4(void *sq);                                   /* 0x2596E4 */
void aiperc_2598C8(void *sq);                                   /* 0x2598C8 */
void aiperc_2598D0(void *sq);                                   /* 0x2598D0 */
void aiperc_2599A4(void *sq);                                   /* 0x2599A4 */
void aiperc_259AA4(void *sq);                                   /* 0x259AA4 */
void aiperc_259BE8(void *sq);                                   /* 0x259BE8 */
void aiperc_259C70(void *sq);                                   /* 0x259C70 */
void aiperc_259E3C(long squad);                                 /* 0x259E3C */
void aiperc_259F3C(long squad, long ent);                       /* 0x259F3C */
void aiperc_25A0B0(long squad);                                 /* 0x25A0B0 */

/* ------------------------------------------------------------------ aicmd.c (M010) */
void aimsg_25A110(void);                                        /* 0x25A110 */
void aimsg_25A130(long squad);                                  /* 0x25A130 */
uchar aimsg_25A150(Msg *m);                                     /* 0x25A150 */
void aimsg_25A22C(void *squad);                                 /* 0x25A22C */
long aimsg_25A754(long squad, long cmd, long a, long b, long c, long d); /* 0x25A754 */
void aimsg_25A7C8(void *sq, Msg *m);                            /* 0x25A7C8 */
long aimsg_25A7E8(long squad, long cmd, long a, long b, long c, long d); /* 0x25A7E8 */
/* script engine functions (tables 0x35DE8C / 0x35E1B8 / 0x35E488): squad commands 1..0x18 */
long aimsg_25A868(long s);
long aimsg_25A888(long s, long a);
long aimsg_25A8A4(long s, long a, long b, long c);
long aimsg_25A8C4(long s, long a, long b);
void aimsg_25A8E4(long n);                       /* void as in g3_c.h */
long aimsg_25A904(long s, long a);
long aimsg_25A924(long s, long a);
long aimsg_25A944(long s, long a);
long aimsg_25A960(long s, long a, long b, long c);
long aimsg_25A984(long s, long a);
long aimsg_25A9A0(long s, long a);
long aimsg_25A9BC(long s);
long aimsg_25A9DC(long s, long a);
long aimsg_25A9F8(long s);
long aimsg_25AA18(long s, long a);
long aimsg_25AA34(long s, long a);
long aimsg_25AA50(long s, long a);
long aimsg_25AA6C(long s);
long aimsg_25AA8C(long s, long a);
long aimsg_25AAA8(long s);
long aimsg_25AAC8(long s, long a);
long aimsg_25AAE8(long s, long a);
long aimsg_25AB04(long timer, long secs);
long aimsg_25AB50(long s, long a);
long aimsg_25AB6C(long s);

/* ------------------------------------------------------------------ aimkmsg.c (M011 part 1) */
void aiacc_25AC50(Msg *m, ulong from, ulong to, long a, long type);
void aiacc_25AC64(Msg *m, uchar *a);
void aiacc_25AC6C(Msg *m, ulong from, ulong to, void *pos, long b);
void aiacc_25AC98(Msg *m, void *pos, uchar *b);
void aiacc_25ACB0(Msg *m, ulong from, ulong to, void *pos);
void aiacc_25ACCC(Msg *m, void *pos);
void aiacc_25ACE0(Msg *m, ulong from, ulong to, void *pos, long a, long b, long c, long d);
void aiacc_25AD34(Msg *m, void *pos, short *a, uchar *b, uchar *c, uchar *d);
void aiacc_25AD68(Msg *m, ulong from, ulong to);
void aiacc_25AD78(Msg *m, ulong from, ulong to, long a);
void aiacc_25AD88(Msg *m, ulong from, ulong to, long a);
void aiacc_25AD98(Msg *m, ulong from, ulong to);
void aiacc_25ADA8(Msg *m, ulong from, ulong to);
void aiacc_25ADB8(Msg *m, ulong from, ulong to, long a);
void aiacc_25ADC8(Msg *m, ulong from, ulong to);
void aiacc_25ADD4(Msg *m, ulong from, ulong to);
void aiacc_25ADE0(Msg *m, ulong from, ulong to, long a, long b);
void aiacc_25ADF8(Msg *m, uchar *a, uchar *b);
void aiacc_25AE08(Msg *m, ulong from, ulong to, long a);
void aiacc_25AE18(Msg *m, ulong from, ulong to, long a, long b);
void aiacc_25AE30(Msg *m, ulong from, ulong to, long a, long b);
void aiacc_25AE48(Msg *m, ulong from, ulong to, long a, long b);
void aiacc_25AE60(Msg *m, ulong from, ulong to, long a);
void aiacc_25AE70(Msg *m, ulong from, ulong to, long a, long b, long c, long d);
void aiacc_25AE98(void *ai, long *ent, long *target, long *c, long *d, long *e); /* as g2_c.h */
void aiacc_25AEC8(Msg *m, ulong from, ulong to, long a, long b);
void aiacc_25AEE0(Msg *m, ulong from, ulong to);
void aiacc_25AEEC(void);
void aiacc_25AEF0(Msg *m, ulong from, ulong to, long a, long b);
void aiacc_25AF0C(Msg *m, short *a, uchar *b);
void aiacc_25AF1C(Msg *m, ulong from, ulong to, long a, long b);

/* ------------------------------------------------------------------ other workers (orig rows) */
/* misobj_2C9EE8 (G9c): g9_c.h (moved there at the G9/G10 merge) */

#pragma pack(pop)
#endif
