/* g9_c.h -- batch G9, worker g9c: M031 squad / equipment data (resmiss.res; src/game/mission/
 * equip.c), M098 save / load game and its shell screen (saveload.c), M104 mission object
 * creation, mission regions and sky (misobj.c, misobjrg.c), M108 campaign training of the
 * soldiers who sat a mission out (mstathlp.c), M121 movies (Smacker player glue;
 * src/game/main/movie.c) and M122 file copy jobs (filecopy.c).  Read from __FF.EXE v1.09.
 *
 * Every G9c function is declared here (the copies that g3_c, g4_a, g4_b, g4_c1, g4_c2, g9_a and
 * g9_b.h held were moved here at the G9/G10 merge; C files that call them include g9_c.h).
 * Globals are bound to their original addresses through symbols-g9-c.csv.
 */
#ifndef TN_G9_C_H
#define TN_G9_C_H
#include "g9_b.h"      /* g9_a.h, g1..g8 headers, res.h, 2d.h, sup_c.h, t1_*.h, InstallJob */
#include "resbuild.h"  /* ResMake, ResWrite, ResUnmake, ResPack */

#pragma pack(push, 1)

/* ------------------------------------------------------------------ M122 file copy jobs */

/* One copy job (0x20 bytes; g9_b.h's InstallJob names only +0x18). */
typedef struct {
    char *src;              /* +0x00 malloc'd copy of the source name */
    char *dst;              /* +0x04 malloc'd copy of the target name */
    int fd_in;              /* +0x08 -1 = closed */
    int fd_out;             /* +0x0C */
    long chunk;             /* +0x10 bytes per step */
    long size;              /* +0x14 length of the source */
    long left;              /* +0x18 bytes still to copy */
    long flags;             /* +0x1C 1 = the job itself is malloc'd (never stays set, see C) */
} CopyJob;
LG_STATIC_ASSERT(sizeof(CopyJob) == 0x20, CopyJob_size);

extern void *copy_buf;                  /* 0x35F3BC copy buffer */
extern long copy_buf_size;              /* 0x35F3C0 its size */

uchar install_2DC5BC(char **dst, char *s);                 /* 0x2DC5BC strdup into *dst */
void install_2DC610(CopyJob *job, long del);               /* 0x2DC610 close (remove target) */
long install_2DC674(CopyJob *job, char *src, char *dst);   /* 0x2DC674 open; 0 or -1/-2/-3 */
long install_2DC85C(CopyJob *job);                         /* 0x2DC85C copy everything */
long install_2DC89C(char *src, char *dst);                 /* 0x2DC89C copy a file */
/* the installer's view of a job (g9_b.h InstallJob = the first 0x1C bytes of CopyJob) */
void install_2DC5B0(void *buf, long size);               /* 0x2DC5B0 copy buffer */
InstallJob *install_2DC760(char *src, char *dst, long n); /* 0x2DC760 start a copy */
long install_2DC7C8(InstallJob *job);                    /* 0x2DC7C8 copy a block, <0 at error */
long install_2DC88C(InstallJob *job, long how);          /* 0x2DC88C finish a copy, returns 0 */

/* ------------------------------------------------------------------ M121 movies */

/* movie_tab entry (16 bytes): file name, 1 = 640x480 movie */
typedef struct {
    char name[15];
    uchar hires;            /* +0x0F */
} MovieEntry;
LG_STATIC_ASSERT(sizeof(MovieEntry) == 16, MovieEntry_size);

extern MovieEntry movie_tab[0x5A];      /* 0x35ED78 */
extern schar mission_movie[][2];        /* 0x35F318 per mission {movie, when} */
extern schar mission_movie_end[];       /* 0x35F388 per mission */
extern uchar movie_played[0x28];        /* 0x43E638 per mission: its movie was shown */

void * __attribute__((stdcall)) intro_2DBE50(long size);  /* 0x2DBE50 Smacker malloc callback */
void __attribute__((stdcall)) intro_2DBE6C(void *p);      /* 0x2DBE6C Smacker free callback */
void intro_2DBE7C(void);                                  /* 0x2DBE7C opening movies */
void intro_2DBED8(void);                                  /* 0x2DBED8 ending movies */
void intro_2DBF14(long flags);                            /* 0x2DBF14 mouse range for a mode */
void intro_2DBF94(void);                                  /* 0x2DBF94 briefing movie */
long intro_2DC014(long when);                             /* 0x2DC014 mission movie due now */
long intro_2DC06C(void);                                  /* 0x2DC06C */
void intro_2DC0FC(void *smk);                             /* 0x2DC0FC palette + backdrop */
long intro_2DC254(long movie, long input);                /* 0x2DC254 play a movie */
void intro_2DC07C(void);                 /* 0x2DC07C */
uchar intro_2DC0B0(void);                /* 0x2DC0B0 movies present */

/* Smacker (third party, stays original): stdcall / cdecl, they clobber ebx, so every call goes
 * through g9c_call (saves ebx/esi/edi/ebp, works for both conventions). */
long g9c_call(void *fn, long n, ...);
extern char smacker_2FDDA0[], smacker_2FDDB0[], smacker_2FDDC0[], smacker_2FDDD0[],
    smacker_2FDE80[], smacker_2FDF50[], smacker_2FE1C0[], smacker_2FE610[], smacker_2FED30[],
    smacker_2FEDF0[], smacker_2FEF20[], smacker_2FF820[], smacker_2FFD70[], smacker_300040[],
    smacker_300420[], smacker_300560[], smacker_3007A0[];
void smacker_2FDD94(long ms);                             /* 0x2FDD94 wait ms (Watcom) */

/* ------------------------------------------------------------------ M108 */
extern uchar mstathlp_35DAA8[];         /* 0x35DAA8 RndRange span per soldier kind */
extern long game_442860;                /* 0x442860 */

long mis108_2CD870(long s, MissionStats *ms);              /* 0x2CD870 train one soldier */
long mis108_2CDB98(long s, MissionStats *ms);              /* 0x2CDB98 (unreferenced) */
void mis108_2CDAEC(long a);                                     /* 0x2CDAEC */

/* ------------------------------------------------------------------ M104 mission objects */

/* object list record (res 0xAD of the mission file, 0x34 bytes) */
typedef struct {
    uchar cls;              /* +0x00 0 static, 1 soldier, 2 vehicle, 3 aircraft, 4 installation,
                               5 weapon, 6 nothing, 7 compound group */
    uchar type;             /* +0x01 */
    uchar g02[4];
    uchar bit;              /* +0x06 bit in game_442420 (object set of the mission variant) */
    uchar squad;            /* +0x07 */
    fix x, y, z;            /* +0x08 z above the ground */
    ushort heading;         /* +0x14 */
    ushort f16, f18;        /* +0x16 */
    uchar tmpl[0x1A];       /* +0x1A AI template to the end (passed by address); tmpl[4] / tmpl[5]
                               (+0x1E / +0x1F) nonzero make a class-4 object an AI installation */
} MisObj;
LG_STATIC_ASSERT(sizeof(MisObj) == 0x34, MisObj_size);

extern uchar misobj_35D0FC;             /* 0x35D0FC map annotations: init (1) or reset */
extern uchar misobj_squad_kind[0x20];   /* 0x4413B0 res 0xAE: AI kind per squad */
extern long misobj_timers[0x41];        /* 0x441CD0 game_442F40 points here */
extern uchar misobj_spawns[0x644];      /* 0x441DD4 res 0xB5 (mask + 0x20 x 0x32); game_442F44 points here */

long misobj_2C93C0(void);                                  /* 0x2C93C0 returns 0 (unreferenced) */
void MissionReserveMedAIs(RefTable *rt);                   /* 0x2C95BC */
uchar misobj_2C97E4(MisObj *o);                            /* 0x2C97E4 squad of an object */
void misobj_2C93C4(long arg);            /* 0x2C93C4 */
long misobj_2C93CC(void);                /* 0x2C93CC nonzero: map missing */
void misobj_2C940C(void);                /* 0x2C940C */
void misobj_2C9BBC(void);                /* 0x2C9BBC */
void misobj_2C9C6C(void);                /* 0x2C9C6C */
void misobj_2C9D10(void);                /* 0x2C9D10 (not in the function map) */
void misobj_2C9EFC(void);                /* 0x2C9EFC */
void misobj_2C9898(void *rec, long squad);  /* 0x2C9898 create a spawn's objects */
uchar misobj_2C9D50(long a, fix x, fix y);       /* 0x2C9D50 */
uchar misobj_2C9DF8(ulong *mask, fix x, fix y);                 /* 0x2C9DF8 region bits of a point */
long misobj_2C9E60(long x, long y);                              /* 0x2C9E60 region index at a point */
void misobj_2C9EE8(long region);                                /* 0x2C9EE8 clears a region centre */
void misobj_2C9FD4(void);                                       /* 0x2C9FD4 */

/* ------------------------------------------------------------------ M031 squad / equipment */
/* Soldier record (camp_soldiers[s], 0x1EE bytes, g9_a.h): +0 voice, +1 (2), +2, +3, +4 history
 * count, +5 kit kind, +6, +7 campaign position, +8 name (8), +0x10 skills (5), +0x15 short,
 * +0x18 history (10-byte entries). */
extern schar squad_soldier[3];          /* 0x391F4A squad member 1..3 -> voice */
extern uchar squad_390320[4];           /* 0x390320 member has a fixed place (kind 5); [0] = 1 */
extern uchar squad_kit[6][6];           /* 0x390324 kit per kind: types[4], +4 d, +5 b (0x6A8/0x6A9) */
extern uchar camp_43E61A;               /* 0x43E61A voice of soldier 0 */
extern schar camp_43E61B;               /* 0x43E61B alternates 0 / 1 for replacement voices */
extern schar camp_status[0xD];          /* 0x43E61D per soldier: 0 free, 1 resting, 2 out */
extern schar camp_43E62A[0xD];          /* 0x43E62A per soldier: 1 wounded, 2 out */
extern schar camp_43E637;               /* 0x43E637 mission the squad was chosen for */
extern uchar camp_43E660[8], camp_43E668[8];  /* 0x43E660 / 0x43E668 */
extern uchar camp_391C6E[3];            /* 0x391C6E {3, 4, 2} */
extern uchar game_442F32;               /* 0x442F32 cheat squad size (res 0x6BD) */

void equip_278CE4(void);                                  /* 0x278CE4 voice -> soldier table */
uchar equip_27915C(schar voice, schar *st, schar *ids, long n); /* 0x27915C replacement soldier */
void equip_279254(uchar *s, uchar *src);                  /* 0x279254 new soldier record */
void equip_2792D4(void);                                  /* 0x2792D4 choose the squad */
void equip_27998C(void);                                  /* 0x27998C mission squad / kits */
void equip_279B50(long n, char *buf);                     /* 0x279B50 string 0x6AA:n-1 */
void equip_279BA4(long i, long voice);                    /* 0x279BA4 MFD entry of a member */
void equip_279C00(void);                                  /* 0x279C00 (data ref) */
void equip_279C50(void);                                  /* 0x279C50 (data ref) */
void equip_278CC0(void);                 /* 0x278CC0 */
void equip_278D18(void);                 /* 0x278D18 */
void equip_279D4C(void);                 /* 0x279D4C */
void equip_278EF0(long voice);                                  /* 0x278EF0 */
void equip_279900(void *ms);                                    /* 0x279900 */

/* ------------------------------------------------------------------ M098 save / load */
extern uchar save_loading;              /* 0x35CFF0 1 while saveload_2C38A8 loads */
extern long save_slot;                  /* 0x35CFF4 */
extern uchar save_cursor;               /* 0x35CFF8 edit cursor */
extern long *save_vars[6];              /* 0x35D000 dwords saved as res 0x6F0 ([0] = 0x6EF0018) */
extern char save_name[0x40];            /* 0x43D490 description being edited */
extern TnRegion *save_region;           /* 0x43D4D0 */
extern char save_dir[0x3C];             /* 0x43D4D4 */
extern uchar save_43D510[0x6E];         /* 0x43D510 res 0x6F4 */
extern uchar save_insert;               /* 0x43D57E next key replaces the whole text */
extern short save_boxes[2][4];         /* 0x43CC48 title boxes {x0, y0, x1, y1} (y1 unused) */

uchar saveload_2C3548(char *buf, long n);                  /* 0x2C3548 description of a file */
long saveload_2C35BC(void);                                /* 0x2C35BC save */
void saveload_2C37F8(void);                                /* 0x2C37F8 rewrite the description */
void saveload_2C38A8(long a, long rewrite);                /* 0x2C38A8 load */
uchar saveload_2C3A8C(Id id, void *buf, long size);        /* 0x2C3A8C 0 ok, 1 size differs, 2 none */
void saveload_2C3530(void);              /* 0x2C3530 */
void saveload_2C3A48(long id, void *data, long size, long file, long flag); /* 0x2C3A48 */
long saveload_2C3ADC(long slot);                           /* 0x2C3ADC slot used (-1: any) */
void saveload_2C3B90(long saving);                         /* 0x2C3B90 draw the slot list */
uchar saveload_2C40C0(void *ev);                           /* 0x2C40C0 mouse handler */
long saveload_2C4188(char *s, long n);                     /* 0x2C4188 cursor position: x | y << 16 */
void saveload_2C4200(char *s, long lineh);                 /* 0x2C4200 marks after ": " */
void saveload_2C42C8(void);                                /* 0x2C42C8 draw the edit line */
uchar saveload_2C4520(void *ev);                           /* 0x2C4520 key handler */
void saveload_2C4C1C(long slot, TnRegion *r);              /* 0x2C4C1C open the save dialog */
void saveload_2C4EA4(long result);                         /* 0x2C4EA4 close it (unreferenced) */

#pragma pack(pop)
#endif
