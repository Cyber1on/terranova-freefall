/* g9_a.h -- batch G9, worker g9a: M035 nav markers / charges (0x27ACC0-0x27B55F,
 * src/game/mission/simtype.c), M051 sim type tables loader (simload.c), M101 mission statistics
 * and campaign state (0x2C58C0-0x2C8C6F, mstat.c), M102 campaign mission list (camplist.c) and
 * M109 unit table by skill level (ressimld.c).  Read from __FF.EXE v1.09.
 *
 * Nav markers (M035; the map called the module "sim type data"): every radio channel (0..3,
 * = squad member record of game_391E50) has 11 markers of 12 bytes (NavMark) at mapd_nav
 * (0x392170, g8c's name); 10 are used.  colour -1 = free, -2 = used up, 0..2 = a charge or
 * strike group placed through the MFD; an entry with ent != 0 is attached to that entity.
 * simtype_27AEEC fires one: it makes a class-8 (blast) entity at the marker or the entity.
 * nav_sel (0x392140, 12 bytes per channel) is the marker being placed (g8a reads it as
 * mfd_392141 = nav_sel + 1).
 *
 * Mission statistics (M101): ordn_mission_state points at a MissionStats block (0x846 bytes,
 * the default one at 0x440A30); four soldier records, then the mission totals.  Campaign state
 * (camp_*): position in the campaign, mission done flags, soldier records of the squad
 * (0x1EE bytes each) and their medal history.
 *
 * Globals are bound to their original addresses through symbols-g9-a.csv.
 */
#ifndef TN_G9_A_H
#define TN_G9_A_H
#include "game.h"
#include "2d.h"
#include "res.h"
#include "support.h"
#include "g8_a.h"      /* g7_*.h: HudWidget, HudView, hud_views, hud_35A544; mfd_395AD8 .. */
#include "g1_b.h"      /* GameSpeaker game_391E50, game_391E4C, game_359708, rnd_stream_35B48C */
#include "g1_c.h"      /* fileutil_*, game_41DE34, game_35CD9C, remove */
#include "g2_a.h"      /* timers_279E80, skill_cls*, ord_table, voice_soldier, game_442900 */
#include "g2_b.h"      /* ENT, game_cls3/4/8, game_rnd_35B704 */
#include "g2_b2.h"     /* objdmg_get_hp */
#include "g8_b.h"      /* mfddata_2665E8 (the copy in g2_b2.h went at the G9/G10 merge) */
#include "g2_c.h"      /* ordn_flags, ordn_mission_state */
#include "g3_a.h"      /* game_hp_cls*, game_sys_cls* */
#include "g3_b.h"      /* game_395728 */
#include "g3_c.h"      /* instsim_288554, sfx_28DDD8, unit_tab, game_440930 */
#include "g4_a.h"      /* ai_reinforce_max, ai_cheat_flags */
#include "g4_c2.h"     /* game_44127A */
#include "g6_b.h"      /* stat_kills_359718 */
#include "g7_b.h"      /* HUD types */
#include "t1_b.h"      /* kb_flush */
#include "t1_c.h"      /* lg_memset, lg_qsort, tnm_state_ptr */
#include "sup_c.h"     /* Key/Mouse handlers, tnui_full_rect, tnui_back_canvas */

#pragma pack(push, 1)

/* ------------------------------------------------------------------ M035 nav markers */

/* One nav marker / charge (12 bytes). */
typedef struct {
    schar colour;           /* +0 -1 free, -2 (0xFE) used, 0..2 group */
    short ent;              /* +1 entity it is attached to, 0 = position */
    fix x, y;               /* +3, +7 world position */
    uchar count;            /* +B charges (subtracted from simtype_counts) */
} NavMark;
LG_STATIC_ASSERT(sizeof(NavMark) == 12, NavMark_size);

#define NAV(c, k) ((NavMark *)&mapd_nav[(c)][(k) * 12])

extern uchar mapd_nav[4][0x84];         /* 0x392170 10 nav markers of 12 bytes per channel:
                                           schar colour (-1 none), short ent, fix x, fix y */
extern NavMark nav_sel[4];              /* 0x392140 marker being placed, per channel */
extern short simtype_list8[8];          /* 0x392130 entities (-1 = free) */
extern short simtype_list5b[5];         /* 0x392380 installations already fired at once */
extern short simtype_list5a[5];         /* 0x3923D4 installations with 1000-hp charges */
extern uchar cmd_39238A[];              /* 0x39238A entity type -> needed level */

void simtype_27ACC0(void);                                      /* 0x27ACC0 init */
void simtype_27ADAC(HudWidget *w, long ent, long chan);         /* 0x27ADAC */
void simtype_27AE24(long a, long b, long chan);                 /* 0x27AE24 */
void simtype_27AEEC(long chan, long k, long flag);              /* 0x27AEEC fire one marker */
void simtype_27B144(long i);                                    /* 0x27B144 fire a group */
uchar simtype_27B200(void);                                     /* 0x27B200 */
void simtype_27B240(void *w, fix *x, fix *y);                   /* 0x27B240 */
long simtype_27B298(long ent);                                  /* 0x27B298 (arg = channel) */
long simtype_27B300(long ent);                                  /* 0x27B300 */
schar simtype_27B368(long chan);                                /* 0x27B368 */
uchar simtype_27B398(long ent);                                 /* 0x27B398 */
void simtype_27B3C8(long ent);                                  /* 0x27B3C8 */
void simtype_27B3F4(long ent);                                  /* 0x27B3F4 */
uchar simtype_27B420(long ent);                                 /* 0x27B420 */
void simtype_27B450(long ent);                                  /* 0x27B450 */
void simtype_27B4AC(long ent);                                  /* 0x27B4AC */
uchar simtype_27B4D8(long ent);                                 /* 0x27B4D8 */
uchar simtype_27B508(void *p);                                  /* 0x27B508 (arg unused) */

/* ------------------------------------------------------------------ M051 / M109 */
extern uchar simload_39634E[];          /* 0x39634E resource 0x3A6 of ressim.res */

void simload_283270(void);                                      /* 0x283270 */
void ressim_2CDBB0(long skill);                                 /* 0x2CDBB0 script native */

/* ------------------------------------------------------------------ M101 mission statistics */

/* One squad member's statistics (0x128 bytes). */
typedef struct {
    uchar state;            /* +000 0 fit, 1 wounded (< 75 %), 2 dead, 3 (score -30) */
    schar voice;            /* +001 speaker voice (0x20 = none) */
    short f02;              /* +002 hits (mstat_2C6FD8) */
    uchar g04[2];
    short shots;            /* +006 (mstat_2C6FA4) */
    short kills;            /* +008 */
    short assists;          /* +00A */
    short kill_tab[0x7A];   /* +00C kills per kill table index (mstat_2C6110) */
    uchar weapons[0x28];    /* +100 copy of the PBA weapon block, 8 x 5 bytes: +1 max, +3
                               current (ushort) */
} MsSoldier;
LG_STATIC_ASSERT(sizeof(MsSoldier) == 0x128, MsSoldier_size);

/* The statistics of one mission (0x846 bytes; ordn_mission_state, saved per campaign position). */
typedef struct {
    MsSoldier sold[4];      /* +000 */
    short count;            /* +4A0 members */
    ulong time;             /* +4A2 game_time / 1000 at the end */
    uchar pba[4][0xD8];     /* +4A6 copies of the members' PBA + 6: +0x96 weapon block pointer
                               (set to sold[i].weapons), +0x9A short weapon count */
    uchar goal_on[3];       /* +806 */
    uchar goal_state[3];    /* +809 2 open, 4 (failed / not needed) */
    long goal_time[3];      /* +80C time a goal was reached (0 = not) */
    long f818;              /* +818 */
    uchar won;              /* +81C a goal was reached (passed to camplist_2C8FC8) */
    long f81d;              /* +81D game_442418 */
    long f821;              /* +821 game_44241C */
    short medals;           /* +825 the player's medal bits */
    long score;             /* +827 */
    long health[4];         /* +82B per member health % */
    long avg_health;        /* +83B */
    uchar g83f[2];
    uchar music;            /* +841 game_41DE34 */
    long mission;           /* +842 game_359708 */
} MissionStats;
LG_STATIC_ASSERT(sizeof(MissionStats) == 0x846, MissionStats_size);

extern short mstat_cls_counts[5];       /* 0x35D0D0 types per class {1, 0x12, 0xC, 0x11, 0x4A} */
extern ushort mstat_list_res[3];        /* 0x35D0DA {0x6B4, 0x6B5, 0x6B6} */
extern schar mstat_list_len[3];         /* 0x35D0E0 {0xF, 0xB, 3} */
extern uchar mstat_notice;              /* 0x35D0E3 notices (medal bits) still to show */
extern uchar mstat_notice_medals;       /* 0x35D0E4 */
extern long mstat_screen_done;          /* 0x35D0E8 the notice screen is closed */
extern long mstat_35D0CC;               /* 0x35D0CC set by the end of mission 0x1F */
extern long game_359714;                /* 0x359714 campaign kills (stat_kills_359718 summed) */
extern uchar camp_soldiers[][0x1EE];    /* 0x390348 squad soldiers: +3 score, +4 missions,
                                           +8 medal counts [8], +0x18 10-byte history per
                                           campaign position {kills, medals, assists, hits, shots} */
extern uchar mstat_goals[0x30][3];      /* 0x43E370 goal points per mission (res 0x6AE) */
extern MsSoldier mstat_others;          /* 0x43E400 shots / hits / kills of non-members */
extern uchar mstat_43E528[0x30];        /* 0x43E528 per mission: count map points (res 0x6B2) */
extern short mstat_43E558[0x30];        /* 0x43E558 per mission (res 0x6B3) */
extern short mstat_43E5B8, mstat_43E5BA;/* 0x43E5B8 / 0x43E5BA positions of medals 0x20 / 0x10 */
extern schar mstat_43E5BC[];            /* 0x43E5BC health % per campaign position */
extern schar mstat_43E5EB[];            /* 0x43E5EB score per campaign position */
extern schar mstat_43E61C;              /* 0x43E61C */
extern short speaker_score[33];         /* 0x4408C1 per voice */
extern schar speaker_cnt[33];           /* 0x440903 per voice */
extern long game_440924, game_440928, game_44092C; /* 0x440924 / 0x440928 / 0x44092C */
extern char mstat_fname[0x3C];          /* 0x440934 temp file of the statistics */
extern long game_440970, game_440974;   /* 0x440970 medal won, 0x440974 campaign time */
extern uchar mstat_44097C[0x32];        /* 0x44097C res 0x6BE */
extern schar mstat_kill_pts[0x7A];      /* 0x4409AE points per kill table index (0x63 = none) */
extern uchar mstat_440A28[6];           /* 0x440A28 res 0x6B1 */
extern MissionStats mstat_default;      /* 0x440A30 */
extern short game_441278;               /* 0x441278 points on the map (mstat_2C78F4) */
extern long game_442418, game_44241C, game_442804; /* 0x442418 / 0x44241C / 0x442804 */
extern short game_442864;               /* 0x442864 objects with points on the map */

short mstat_2C6110(long cls, long type);                        /* 0x2C6110 (ax) */
void mstat_2C614C(long v, uchar *cls, uchar *type);             /* 0x2C614C */
long mstat_2C618C(long score);                                  /* 0x2C618C */
uchar mstat_2C6244(void);                                       /* 0x2C6244 */
uchar mstat_2C6248(void);                                       /* 0x2C6248 */
long mstat_2C624C(long cls, long type);                         /* 0x2C624C */
void mstat_2C6278(void);                                        /* 0x2C6278 */
uchar mstat_2C62B4(long n);                                     /* 0x2C62B4 */
void mstat_2C6350(void);                                        /* 0x2C6350 */
void mstat_2C64E8(void);                                        /* 0x2C64E8 */
int mstat_2C65A4(void);                                         /* 0x2C65A4 AtExit callback */
long mstat_2C65B0(void);                                        /* 0x2C65B0 native */
void mstat_2C665C(long n);                                      /* 0x2C665C */
void mstat_2C66B8(void);                                        /* 0x2C66B8 */
void mstat_2C6784(long file);                                   /* 0x2C6784 */
void mstat_2C6838(void);                                        /* 0x2C6838 */
void mstat_2C6888(void);                                        /* 0x2C6888 */
void mstat_2C68C8(long speaker, long v);                        /* 0x2C68C8 */
void mstat_2C68E8(void);                                        /* 0x2C68E8 */
void mstat_2C6FA4(long ent);                                    /* 0x2C6FA4 */
void mstat_2C6FD8(long ent);                                    /* 0x2C6FD8 */
void mstat_2C701C(long who, long victim);                       /* 0x2C701C */
long mstat_2C71E0(void);                                        /* 0x2C71E0 */
void mstat_2C7220(long ent, long chan);                         /* 0x2C7220 */
long mstat_2C734C(MissionStats *ms);                            /* 0x2C734C */
long mstat_2C73E8(MissionStats *ms);                            /* 0x2C73E8 */
short mstat_2C7540(MissionStats *ms, long s, long flag);        /* 0x2C7540 (ax) */
long mstat_2C78F4(MissionStats *ms);                            /* 0x2C78F4 */
void mstat_2C79D4(void);                                        /* 0x2C79D4 */
void mstat_2C7A10(void);                                        /* 0x2C7A10 */
void mstat_2C7A4C(void);                                        /* 0x2C7A4C */
void mstat_2C7AC0(void);                                        /* 0x2C7AC0 */
void mstat_2C7AFC(void);                                        /* 0x2C7AFC */
void mstat_2C7C24(void);                                        /* 0x2C7C24 */
long mstat_2C7C54(ulong *ev);                                   /* 0x2C7C54 key / mouse handler */
void mstat_2C7D14(void);                                        /* 0x2C7D14 */
long mstat_2C8124(void);                                        /* 0x2C8124 native */
void mstat_2C81BC(void);                                        /* 0x2C81BC */
long mstat_2C8230(long s, long medal);                          /* 0x2C8230 native */
void mstat_2C8254(void);                                        /* 0x2C8254 native */
void mstat_2C828C(void);                                        /* 0x2C828C */
void mstat_2C84C0(void *font, char *text, long value, long y);  /* 0x2C84C0 */
long mstat_2C8530(short *a, short *b);                          /* 0x2C8530 qsort compare */
void mstat_2C856C(void);                                        /* 0x2C856C native */

/* ------------------------------------------------------------------ M102 campaign list */
extern uchar camp_flags[0x30];          /* 0x441280 per mission: 1 repeatable, 2 repeated */
extern schar camp_sel_b[0xC];           /* 0x4412B0 selection list of res 0x6B5 */
extern schar camp_sel_c[3];             /* 0x4412BC res 0x6B6 */
extern schar camp_sel_a[0xF];           /* 0x4412BF res 0x6B4 */
extern schar camp_level[0x3F];          /* 0x4412CE mission -> campaign position */
extern schar camp_missions[0x3F];       /* 0x44130D campaign position -> mission */
extern uchar camp_repeat;               /* 0x44134C the mission is played again */
extern long camp_pos;                   /* 0x35D0EC campaign position */
extern schar camp_done[3];              /* 0x35D0F0 entries of the selection lists reached */
extern long camp_35D0F4;                /* 0x35D0F4 */
extern long game_359710;                /* 0x359710 */

void camplist_2C8C70(void);                                     /* 0x2C8C70 */
uchar camplist_2C8CC8(char *s, long flag);                      /* 0x2C8CC8 */
void camplist_2C8CE8(schar *src, schar *dst, long n);           /* 0x2C8CE8 */
schar camplist_2C8D30(long id, long n, schar *dst);             /* 0x2C8D30 */
void camplist_2C8E04(void);                                     /* 0x2C8E04 */
void camplist_2C8E54(void);                                     /* 0x2C8E54 */
void camplist_2C8E78(void);                                     /* 0x2C8E78 */
void camplist_2C8FC8(long flag);                                /* 0x2C8FC8 */
#ifndef TN_WATCOM_ABI
extern int port_start_mission;          /* Freefall --mission N (port/core/main.c): New Campaign starts at
                                         * campaign mission N; 0 none */
extern int port_new_campaign;           /* main_24546C (New Campaign) is in camplist_2C8E78 */
extern int port_game_dialog_up;          /* main_246094's yes / no box is up (port/ui/options.c: Esc) */
void port_log(const char *fmt, ...);    /* port/core/log.c (camplist_2C8E78's --mission line) */
extern volatile int port_ms_mission;    /* the Mission Selector's mission for New Campaign, once
                                         * (port/ui/missions.c); 0 none */
void port_ms_progress(long pos);        /* the campaign reached position pos (port/ui/missions.c) */
void port_ms_record(long m, int won, long score, unsigned medals);  /* mission m ended */
#endif

/* Callees of other G9 / G10 workers are declared in their owners' headers (g9_b.h main_245334;
 * g9_c.h equip_278EF0 / 279900, mis108_2CDAEC, saveload_2C3A48; g10_a.h uiwidget_2B5C90;
 * g10_b.h shell_2BE8E8 / 2C066C; g10_c.h options_2CAAC0; moved at the G9/G10 merge).  C files
 * that call them include those headers. */

#pragma pack(pop)
#endif
