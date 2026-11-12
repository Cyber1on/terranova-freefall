/* g4_b.h -- declarations for worker g4b (batch G4, AI): the second part of ailow.c (M007
 * 0x25129C-0x253810, src/game/ai/ailowb.c: per-brain targeting, firing and squad membership),
 * the AI glue module M008 (0x253810-0x2539F0, aiglue.c) and the first part of the squad
 * perception / behaviour module M009 (0x2539F0-0x258908, aiperc.c).
 * Names: the map's placeholders are kept (other batches call them); what each function does
 * is in the file headers.  Globals are bound to their original addresses through
 * symbols-g4-b.csv.
 *
 * Records are reached through byte offsets (game.h has the sizes only; include/g4_a.h and
 * include/g4_c1.h describe more offsets).  Offsets used here:
 * Brain (0x14E x 60 at 0x3832F0): +0 active, +1 MsgAddr, +3 index, +5 side, +6 squad, +7 next,
 *   +8 rank, +0xA player-controlled, +0xB short entity, +0xD class (1 soldier, 2 vehicle,
 *   3 installation, 4 aircraft), +0xE/+0x10 map square x/y, +0x12 point, +0x27 timer,
 *   +0x30 3 order records of 0x24 bytes, +0x43 move state {kind, point +1, fix x/y +5/+9},
 *   +0x9C "moving", +0x9D point last moved to, +0xA5 fix x/y of it, +0xAD path record,
 *   +0xB1, +0xB3 "order changed", +0xB5 short attacker, +0xB7 fix x/y where hit, +0xBF hit
 *   time, +0xC3 has target, +0xC4 target visible (line of fire), +0xC5 target time,
 *   +0xC9 target brain, +0xCA target entity (byte), +0xCB target squad, +0xCC/+0xCE target
 *   square x/y, +0xD0 "dodging", +0xD5, +0xD7 time the target was taken, +0xDB word 0..100
 *   (how long the target has been held), +0xED fire timer, +0xF1 reaction time, +0xF5 "can
 *   fire" sent, +0xF6 last shot time, +0xFA alert timer, +0xFE, +0x106/+0x10A/+0x10E timers,
 *   +0x116 kind, +0x135/+0x136 timed flag.
 * Squad (0x170 x 32 at 0x3804F0): see g4_c1.h; +0x16/+0x1A fix centre, +0xC9 "fire at will" (1), +0xDA point to hold (mode 4),
 *   +0xEA "tell the squad it was seen", +0x120/+0x121 region test, +0x122/+0x123 region mask,
 *   +0x165/+0x166 previous / next squad in a ring.
 */
#ifndef TN_G4_B_H
#define TN_G4_B_H
#include "lg.h"
#include "fix.h"
#include "game.h"
#include "t1_c.h"
#include "g1_c.h"      /* game_rnd_text, music_2469E4 */
#include "g3_c.h"      /* misobj_2C9D50, damage_27D158, pbasim_*; includes g2_c.h, g2_a.h */
#include "g2_b2.h"     /* obj_destroy, game_396E14 / game_396E18 */
#include "g4_c1.h"     /* aisqbhv.c, aicmd.c, aimkmsg.c (G4c1): callees and callers */
#include "g4_c2.h"     /* aimsgput.c, path planner (G4c2) */
#pragma pack(push, 1)

/* ------------------------------------------------------------------ globals (orig data rows) */
extern ulong aiglue_from;                 /* 0x3598E8 MsgAddr {0x86, 0}: sender of the AI glue messages */
extern ulong ai_371190;                   /* 0x371190 point where the player's side was last seen */
extern uchar ai_371195;                   /* 0x371195 ai_371190 is set (cleared each AI frame) */
extern uchar *ai_dir3x3;                  /* 0x359914 -> centre of a 3x3 table: direction of a step */

/* ------------------------------------------------------------------ ailowb.c (M007 part 2) */
long ailow_25129C(uchar *b);                                    /* 0x25129C */
uchar ailow_2512F8(uchar *b, long u1, long u2, long u3, fix x, fix y); /* 0x2512F8 ret 8 */
uchar ailow_25142C(uchar *b, long squad);                       /* 0x25142C */
uchar ailow_251594(ulong pt, long r, ushort *out);              /* 0x251594 */
void ailow_251668(uchar *b, long flag, long ent);               /* 0x251668 */
void ailow_251C98(uchar *b);                                    /* 0x251C98 */
uchar ailow_252758(uchar *b, long tgt);                         /* 0x252758 */
void ailow_252C5C(uchar *b, long tgt, fix *pos, short *who, uchar *fa, uchar *fb, uchar *fc);
uchar ailow_252E4C(uchar *b, long ent, long f);                 /* 0x252E4C */
void ailow_252E60(void *brain);                                 /* 0x252E60 */
void ailow_252F78(void *brain);                                 /* 0x252F78 */
void ailow_253008(long brain, long b);                          /* 0x253008 (as g3_c.h) */
void ailow_2531B0(long brain, long squad);                      /* 0x2531B0 (as g3_c.h) */
uchar ailow_253334(long brain);                                 /* 0x253334 */
uchar ailow_25344C(void);                                       /* 0x25344C */
uchar ailow_2534A4(long brain, long squad);                     /* 0x2534A4 */
void ailow_2534FC(long brain);                                  /* 0x2534FC */
uchar ailow_253580(void *brain);                                /* 0x253580 */
void ailow_253608(long brain, long secs);                       /* 0x253608 */
void ailow_253640(uchar brain, long ent, fix *pos);             /* 0x253640 (as g3_a.h) */
void ailow_2537AC(long brain, long ent);                        /* 0x2537AC */

/* ------------------------------------------------------------------ aiglue.c (M008) */
void aiglue_253810(void);                                       /* 0x253810 */
void aiglue_253814(void);                                       /* 0x253814 */
void aiglue_253994(long id);                                    /* 0x253994 (as g2_c.h) */
void aiglue_2539BC(long squad);                                 /* 0x2539BC */

/* ------------------------------------------------------------------ aiperc.c (M009 part 1) */
/* aiperc.c functions g4c1 calls (were declared in g4_c1.h until the G4 merge) */
uchar aiperc_2539F0(void *sq);                                  /* 0x2539F0 */
void aiperc_253E60(long squad, long b);                         /* 0x253E60 */
void aiperc_2567A4(void *sq);                                   /* 0x2567A4 */
uchar aiperc_256C90(void *sq);                                  /* 0x256C90 */
void aiperc_2571D8(void *sq);                                   /* 0x2571D8 */
uchar aiperc_257344(void *sq, ulong pt);                        /* 0x257344 */
void aiperc_2573C0(void *sq, ulong pt);                         /* 0x2573C0 */
uchar aiperc_2575C0(void *sq);                                  /* 0x2575C0 brain index */
uchar aiperc_257820(void *sq);                                  /* 0x257820 */
void aiperc_2579E0(void *sq);                                   /* 0x2579E0 */
void aiperc_257AB4(void *squad);                                /* 0x257AB4 */
void aiperc_257B18(void *sq);                                   /* 0x257B18 */
void aiperc_258170(void *sq, long flag, short *e1, short *e2, short *e3); /* 0x258170 */
uchar aiperc_253AE8(long a, ulong xy, long c);                  /* 0x253AE8 (as g2_c.h) */
uchar aiperc_253F70(void *squad, Msg *m);                       /* 0x253F70 */
uchar aiperc_25486C(void *squad, Msg *m);                       /* 0x25486C */
void aiperc_255798(void *squad);                                /* 0x255798 */
void aiperc_255858(long squad);                                 /* 0x255858 */
void aiperc_2558C4(void *squad);                                /* 0x2558C4 */
void aiperc_25597C(void *squad);                                /* 0x25597C */
void aiperc_255CE4(void *squad);                                /* 0x255CE4 */
void aiperc_255E88(void *squad);                                /* 0x255E88 */
void aiperc_25607C(void *squad, short *near, short *coming, short *enemy); /* 0x25607C */
uchar aiperc_2561B4(void *squad, ulong *pt);                    /* 0x2561B4 */
void aiperc_25626C(void *squad);                                /* 0x25626C */
void aiperc_2562B4(void *squad);                                /* 0x2562B4 */
void aiperc_2563D0(void *squad);                                /* 0x2563D0 (as g4_a.h) */
void aiperc_2566CC(void);                                       /* 0x2566CC */
void aiperc_256710(void *squad);                                /* 0x256710 */
void aiperc_256734(void *squad);                                /* 0x256734 */
void aiperc_256814(void *squad, long flag);                     /* 0x256814 */
void aiperc_256888(void *squad, long k);                        /* 0x256888 */
void aiperc_2569E4(void *squad);                                /* 0x2569E4 */
void aiperc_257518(void *squad);                                /* 0x257518 */
uchar aiperc_257588(long brain);                                /* 0x257588 */
long aiperc_257628(void *squad);                                /* 0x257628 */
void aiperc_257664(void *squad, long sel);                      /* 0x257664 */
void aiperc_2576D4(void *squad);                                /* 0x2576D4 */
uchar aiperc_257C20(void *brain);                               /* 0x257C20 */
uchar aiperc_257CB8(void *squad, void *brain);                  /* 0x257CB8 */
long aiperc_257D18(const void *a, const void *b);               /* 0x257D18 qsort compare */
void aiperc_257D38(void *squad, short *own, short *other);      /* 0x257D38 */
void aiperc_25804C(void *squad, long pct);                      /* 0x25804C */
long aiperc_2580CC(void *squad, ulong pt);                      /* 0x2580CC */

/* ------------------------------------------------------------------ callees of other workers */
/* g4a (M005 aimain.c, M007 part 1), types as in include/g4_a.h where it declares them */
/* ailow_24E19C (0x24E19C) returns a fix x/y pair through esi (Watcom struct return): called
 * through a stub in ailowb.c. */

/* g4c1 / g4c2 (M009 part 2, M010, M011), types as in include/g4_c1.h */

/* misobj_2C9E60 / misobj_2C9DF8 (G9c): g9_c.h (moved there at the G9/G10 merge) */

#pragma pack(pop)
#endif
