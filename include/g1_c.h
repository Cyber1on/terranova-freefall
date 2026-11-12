/* g1_c.h -- declarations for worker g1c (batch G1: game foundation code):
 *   src/game/sound/music.c     M001 music.c (game music selection over the T2 music layer)
 *   src/game/replay/replay.c   M075 input recorder / player (tn.rec)
 *   src/game/main/memprobe.c   M100 unreferenced free-memory probes
 *   src/game/main/fileutil.c   M107 paths, numbered file names, bug report check, time strings
 *   src/game/input/control.c   M116 player controls (slots, keyboard, joystick, head tracker)
 *   src/game/main/strutil.c    M119 string helpers
 *   src/game/main/namegen.c    M120 version string
 * Names are the map's placeholders for functions other batches call (they are referenced by those
 * names elsewhere); functions only this batch calls got names from what they do.  Globals are bound
 * to their original addresses through symbols-g1-c.csv.
 */
#ifndef TN_G1_C_H
#define TN_G1_C_H
#include "game.h"
#include "support.h"
#include "res.h"
#include "sup_c.h"
#include "g1_a.h"      /* RndRange, RndStream, snd_* (owner g1a) */
#include "g1_b.h"      /* ffmem_*, config_2AC*, sndlock_* (owner g1b) */

#pragma pack(push, 1)

/* Watcom inlined strcat (repne scasb + copy). */
static inline char *g1c_strcat(char *d, const char *s)
{
    char *p = d;
    while (*p)
        p++;
    while ((*p++ = *s++) != 0)
        ;
    return d;
}
static inline char *g1c_strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}
static inline int g1c_strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}

/* ------------------------------------------------------------------ callees (original) */
/* Watcom C runtime */
int fclose(void *fp);                                          /* 0x2F61B8 */
long filelength(int fd);                                       /* 0x2F682F */
int remove(const char *path);                                  /* 0x2F672E */
char *strrchr(const char *s, int c);                           /* 0x2EF735 */
char *strpbrk(const char *s, const char *set);                 /* 0x2FCF1A */
unsigned fwrite(const void *p, unsigned size, unsigned n, void *fp); /* 0x2FCF60 */
char *getenv(const char *name);                                /* 0x2FD0BA */
int printf(const char *fmt, ...);                              /* 0x2FD18E cdecl */
void *opendir(const char *path);                               /* 0x2FD2C8 (never closed here) */
void *fopen(const char *name, const char *mode);               /* 0x2FB619 */
int _dos_findfirst(const char *path, unsigned attr, void *buf);/* 0x2FD10C */
char *strncpy(char *d, const char *s, unsigned n);             /* 0x2FB18B */
/* LG / DSTRUCT */
bool datapath_add_one(Datapath *pdp, char *path);              /* 0x2EF664 */
ulong *lg_memset32(ulong *dst, ulong val, int unused, ulong n);/* 0x2F0EB7 eax,edx,ecx */
/* game code of other batches */
/* RndRange (g1_a.h), ffmem_2C4F40/2C4FE4, config_2AC9C0/2ACABC/2ACB78/2ACC68 (g1_b.h) */
bool CfgLookup(char *var, char *buf, int size);                /* 0x2E2CB4 (T1b) */
void GfxDrawText(int font, char *s, int x, int y);             /* 0x2E4304 (T2a) */
void weapons_290858(void);                                     /* 0x290858 (G2b) */
/* obj55_288EC0 (G2b): g2_b.h */
void aircraft_284670(long a, long b, long c, long d);          /* 0x284670 (G3c) */
bool biped_27EDA0(short entity);                               /* 0x27EDA0 (G3b) */
void PbaPlayerMove(void *p, fix *dx, fix *dy);                 /* 0x28ABE0 (G3c) */
void hmd_2FDB80(void);                                         /* 0x2FDB80 HMD driver (third party) */
/* sndlock_2491E0 / sndlock_2491FC (music callbacks): g1_b.h; dpmi_lock_data / dpmi_unlock_data: g1_a.h */
void snd_lock(void);                                           /* 0x2EF148 (T2c) */
void snd_unlock(void);                                         /* 0x2EF154 (T2c) */
void fix_sincos(fixang theta, fix *sin, fix *cos);             /* 0x2ECA00 */
fix fix_safe_pyth_dist(fix a, fix b, char *file, int line);    /* 0x2EC968 */
void EDMS_control_marble(int ph, fix X, fix Y, fix Z);         /* 0x2F0C55 */
void EDMS_marble_add_I14(int ph, fix X, fix Y, fix Z);         /* 0x2F0CA5 */

/* ------------------------------------------------------------------ globals of other modules */
extern short game_player_slot;        /* 0x441352 class slot (PBA index) of the player entity */
extern uchar game_41DE33;             /* 0x41DE33 4: music 8 plays with count 8 */
extern uchar game_41DE34;             /* 0x41DE34 music set (8 ids per set) */
extern long game_35CD9C;              /* 0x35CD9C */
/* game_359708 0x359708: g1_b.h */
/* snd_rnd2 0x359840 (RndStream used by music.c): g1_a.h */
extern RndStream game_rnd_text[1];    /* 0x3596E0 RndRange stream (strutil_2DBCF8, objects, units);
                                         an array of one so that game_rnd_text is the RndStream * */
extern char music_c_file[];           /* 0x355390 "r:\prj\tn\v109\tn\src\tn\music.c" */
extern char game_version_tag[4];      /* 0x35ED68 "DABV": [3] is the version letter */
extern char game_version_buf[];       /* 0x446784 lg_sprintf buffer */
extern char game_version[];           /* 0x43D574 version string ("V1.09") */
extern char *fileutil_default_dir;    /* 0x35DAA4 -> "." */
/* hmd_type, hmd_data (HmdData): t2_a.h */
extern uchar hmd_recenter;            /* 0x35F547 */
extern char tn_joy_axes[4];           /* 0x448D80 */
extern uchar tn_joy_buttons;          /* 0x448D84 */

/* ------------------------------------------------------------------ music.c (M001) */
/* Music table: resource 0x8AE (locked, 10-byte entries).  The id for entry i is
 * res[snd_flags bit 4]; next != 0xFF chains entry i - num + next after it. */
typedef struct {
    ushort res[2];       /* +0 resource ids (two variants) */
    uchar num;           /* +4 passed to music_play */
    uchar vol;           /* +5 */
    uchar mode;          /* +6 1: queue behind the current piece without ending it */
    uchar next;          /* +7 */
    short fade;          /* +8 nonzero: start at the current entry's volume, fade to vol */
} MusicEntry;
LG_STATIC_ASSERT(sizeof(MusicEntry) == 10, MusicEntry_size);

extern MusicEntry *music_tab;         /* 0x370600 */
/* music_unlock_ids, music_unlock_count, music_cur, music_cur_seq, music_rnd, music_fade_vol,
 * music_fade_time (0x3598B8..0x3598CC, 0x370CAC): g1_b.h */
extern ushort music_pending;          /* 0x3597BC entry asked for last */
extern ushort music_seen;             /* 0x3597C4 music_periodic's copy of music_cur */
extern long music_level;              /* 0x3597C0 highest kind-1 event level since the change */
extern ulong music_event_time;        /* 0x3597B8 game_time of the last event */

void music_246380(int count);                       /* 0x246380 play entry 1 (title); -1 = 1 */
void music_246428(void);                            /* 0x246428 MusicInit */
void music_24648C(void);                            /* 0x24648C unlock queued resources (map missed it) */
void music_periodic(int offset);                    /* 0x2465A8 periodic: note music changes */
char music_load(ushort id);                         /* 0x2465E4 lock + music_play entry id */
char music_prepare(ushort id);                      /* 0x246698 load, chain, volume */
void music_2467B8(ulong id);                        /* 0x2467B8 MusicPlay (0 stops all) */
void music_queue(ulong id, ulong mode);             /* 0x2468B0 play or queue entry id */
ushort music_get_cur(void);                         /* 0x2469A8 (unreferenced) */
long music_mode(void);                              /* 0x2469B0 (unreferenced) */
void music_2469E4(long level, ulong kind);          /* 0x2469E4 music event */
void music_step(ulong kind);                        /* 0x246AB8 */
void music_246B50(void);                            /* 0x246B50 callback: event 6 in mode 3 */
void music_246B90(void);                            /* 0x246B90 callback: event 7 in mode 3 */
void music_246BD0(short entity);                    /* 0x246BD0 event 3 when entity is near */

/* ------------------------------------------------------------------ replay.c (M075) */
/* tn.rec: 0xF8 version 0xF9, 0xB4-byte header, then per frame 0xFF frame time (or 0xF7 =
 * frame without time) followed by events: 0xFE key (2), 0xFD mouse event (12), 0xFC time base (4),
 * 0xFB key bitmap (0x20), 0xFA mouse position (6), 0xF6 joystick (1 + 4), 0xF5 check (4),
 * 0xF4 file (4 + n), 0xF3 head tracker (0x12), 0xF2 save info (10). */
#define REPLAY_MAX_EVENTS 0x24
extern uchar replay_rec_paused;       /* 0x35C030 */
extern int replay_fd;                 /* 0x35C034 (-1) */
extern long replay_check;             /* 0x35C03C value of the last 0xF5 record */
extern long replay_desync;            /* 0x35C040 */
extern long replay_blob_size;         /* 0x35C044 */
extern void *replay_blob;             /* 0x35C048 */
extern uchar replay_no_time;          /* 0x35C04C last frame ended at 0xF7 */
extern ushort replay_keys[REPLAY_MAX_EVENTS];        /* 0x41DE40 */
extern uchar replay_keybits[0x20];                   /* 0x41DE88 */
extern uchar replay_mouse[REPLAY_MAX_EVENTS][12];    /* 0x41DEA8 */
extern long replay_nkeys;             /* 0x41E058 */
extern long replay_nmouse;            /* 0x41E05C */
extern char replay_filename[];        /* 0x41E060 */
extern long replay_timebase;          /* 0x41E09C */
extern uchar replay_joy[4];           /* 0x41E0A0 */
extern uchar replay_hmd[0x12];        /* 0x41E0A4 */
extern uchar replay_mousepos[6];      /* 0x41E0B6 */
extern uchar replay_saveinfo[10];     /* 0x41E0BC */
extern uchar replay_joybyte;          /* 0x41E0C6 */
extern char replay_play_name[];       /* 0x428BC5 file to play back */

void replay_make_name(void);                        /* 0x299E70 */
void replay_299F20(void *hdr);                      /* 0x299F20 open a new recording */
void replay_299FC0(void);                           /* 0x299FC0 record from the next frame */
void replay_299FDC(void);                           /* 0x299FDC stop recording, copy to tn.rec */
void replay_rec_pause(void);                        /* 0x29A04C (unreferenced) */
void replay_rec_resume(void);                       /* 0x29A068 (unreferenced) */
void replay_29A084(void *hdr);                      /* 0x29A084 open a recording to play */
void replay_read_header(uchar kind, void *hdr);     /* 0x29A2F4 */
void replay_29A3D8(void);                           /* 0x29A3D8 play from the next frame */
void replay_29A40C(void);                           /* 0x29A40C stop playing */
void replay_29A434(long frame, ulong time);         /* 0x29A434 */
void replay_29A484(void);                           /* 0x29A484 */
/* game_replay_record_key / _record_mouse / _get_keys / _get_mouse / _filter_key / _key_ok and
 * tn_replay_state_ptr: sup_c.h */
void game_replay_record_mouse(void *ev);            /* 0x29A4E0 */
void replay_29A518(uchar b, void *joy);             /* 0x29A518 */
void replay_29A564(long v);                         /* 0x29A564 */
void replay_29A59C(void *keybits);                  /* 0x29A59C */
void replay_29A5D4(void *pos);                      /* 0x29A5D4 */
void replay_29A60C(long v);                         /* 0x29A60C */
void replay_29A644(void *hmd);                      /* 0x29A644 */
void replay_29A67C(void *info);                     /* 0x29A67C */
void replay_29A6E8(ulong *frame, ulong *time);       /* 0x29A6E8 read one frame */
void replay_29A940(uchar *b, void *joy);            /* 0x29A940 */
void replay_29A970(ulong *p);                       /* 0x29A970 */
void replay_29A97C(uchar **p);                      /* 0x29A97C */
void replay_29A984(uchar **p);                      /* 0x29A984 */
void replay_29A98C(long v);                         /* 0x29A98C */
void replay_29A9B0(void *hmd);                      /* 0x29A9B0 */
uchar replay_29A9D4(int i);                         /* 0x29A9D4 */
int replay_load_blob(void);                         /* 0x29AAC4 (unreferenced) */
int replay_29AB1C(char *name);                      /* 0x29AB1C record a file */
int replay_29ABE4(char *name);                      /* 0x29ABE4 write the played-back file */

/* ------------------------------------------------------------------ memprobe.c (M100) */
extern ulong mem_info[12];            /* 0x43E340 DPMI 0x500 free memory info */
extern ulong mem_free_estimate;       /* 0x35D0BC */
ulong dead_2C5600(void);                            /* 0x2C5600 malloc probe */
ulong dead_2C567C(ulong *info);                     /* 0x2C567C DPMI free memory info */
long dead_2C5700(long drive);                       /* 0x2C5700 lock/unlock volume */
ulong dead_2C5720(void);                            /* 0x2C5720 */
ulong dead_2C57CC(void);                            /* 0x2C57CC */
ulong dead_2C5804(long want);                       /* 0x2C5804 f_malloc probe */

/* ------------------------------------------------------------------ fileutil.c (M107) */
void *fileutil_2CD3F0(char *dir, char *name, char *mode);   /* 0x2CD3F0 fopen first free name */
char *fileutil_2CD438(Datapath *dp, char *name);    /* 0x2CD438 next free number (octal) */
void fileutil_2CD4EC(char *dir, char *name);        /* 0x2CD4EC (unreferenced) */
bool fileutil_2CD658(void *fp, int fd);             /* 0x2CD658 copy fd to fp */
void fileutil_2CD6CC(char *buf);                    /* 0x2CD6CC "mm:ss" of game_time */
void fileutil_2CD71C(void);                         /* 0x2CD71C bug report check (unreferenced) */
char *fileutil_2CD7B4(void);                        /* 0x2CD7B4 TN_DIR / GEN_DIR / "." */
long fileutil_2CD804(ulong t, char *pday_hour, char *pmin, char *psec); /* 0x2CD804 */

/* ------------------------------------------------------------------ control.c (M116) */
extern long ctl_turn_on;              /* 0x35ED3C */
extern ulong ctl_turn_start;          /* 0x35ED40 */
extern long ctl_look_on;              /* 0x35ED44 */
extern ulong ctl_look_start;          /* 0x35ED48 */
extern fix joy_sens;                  /* 0x35ED4C */
extern long joy_dead;                 /* 0x35ED50 */
extern fix joy_range;                 /* 0x35ED54 */
extern uchar ctl_hmd_mode;            /* 0x35ED58 CtrlJoystick reads the head tracker axes */
extern short ctl_hmd_prev;            /* 0x35ED5A */
extern char ctl_hmd_axes[6];          /* 0x446708 */
extern uchar ctl_hmd_buttons;         /* 0x44670E */
void CtrlClearSlots(void);                          /* 0x2D7910 */
void CtrlApplySlots(void);                          /* 0x2D7960 */
void control_2D7E38(void);                          /* 0x2D7E38 vehicle controls */
void CtrlKeyboard(void);                            /* 0x2D7F2C */
void control_2D8098(fix sens, long dead);           /* 0x2D8098 (unreferenced) */
fix joy_axis(char v);                               /* 0x2D80C4 */
void CtrlJoystick(void);                            /* 0x2D8138 */
void CtrlHmd(void);                                 /* 0x2D828C */
void control_2D8568(void);                          /* 0x2D8568 push the player's marble */

/* ------------------------------------------------------------------ strutil.c, namegen.c */
char *strutil_2DBCE0(char *d, char *s, int n);      /* 0x2DBCE0 strncpy + terminate */
void strutil_2DBCF8(ushort font, Ref ref, int x, int y, int garble);  /* 0x2DBCF8 */
void make_version_string(void);                     /* 0x2DBDF0 */

#pragma pack(pop)
#endif
