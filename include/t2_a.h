/* t2_a.h -- declarations for worker t2a (batch T2: TN support code, src/game/tnlib/):
 * graphics mode glue / text draw / blit (tngfx.c), head tracker selection (tnhmd.c), the
 * music layer over the Miles AIL sequence API plus a periodic call list (tnmusic.c), the
 * inverse colormap builder (invcmap.c, Spencer Thomas' inv_cmap from Graphics Gems II) and
 * the water mapper's edge scanner (tnwater.c).  Names are ours unless noted (GfxInit,
 * GfxSetModeAndStuff, GfxSaveAndSetMode, GfxRestoreMode come from the game's error strings).
 * Globals are bound to their original addresses through symbols-t2-a.csv.
 */
#ifndef TN_T2_A_H
#define TN_T2_A_H
#include "support.h"
#include "sup_c.h"
#include "res.h"
#include "resmem.h"
#include "2d.h"

#pragma pack(push, 1)

/* ------------------------------------------------------------------ callees (original) */
bool CfgLookup(char *var, char *buf, int size);              /* 0x2E2CB4 (T1b) */
void replay_29A9B0(void *data);                              /* 0x29A9B0 play back tracker data (G1c) */
void replay_29A644(void *data);                              /* 0x29A644 record tracker data (G1c) */

/* ------------------------------------------------------------------ tngfx.c */
extern grs_bitmap gfx_blit_bm;              /* 0x35F548 bitmap GfxBlit draws (a window of the back canvas) */
extern grs_screen *gfx_screen;              /* 0x35F558 */
extern uchar gfx_alt_lowres;                /* 0x35F55C use mode 12 (VESA 320x200) for mode 0 */
extern uchar gfx_alt_medres;                /* 0x35F55D use mode 14 (VESA 320x400) for mode 2 */
extern uchar gfx_linear_ok;                 /* 0x35F55E linear frame buffer allowed */
extern uchar gfx_inited;                    /* 0x35F55F gfx_init_once done */
extern grs_canvas gfx_saved_canvas;         /* 0x446F20 GfxSaveAndSetMode copies */
extern grs_bitmap gfx_saved_blit_bm;        /* 0x446F60 */
extern LGRect gfx_saved_rect;               /* 0x446F70 */
extern void *gfx_canvas_buf;                /* 0x446FC0 buffer GfxSetMode uses for the back canvas */
extern int gfx_saved;                       /* 0x446FC4 1 between GfxSaveAndSetMode and GfxRestoreMode */
extern short gfx_saved_mode;                /* 0x446FC8 */
extern short gfx_mode;                      /* 0x446FCA mode actually set */
extern uchar gfx_mode_set;                  /* 0x446FCC */

void GfxInit2(int mode);                    /* 0x2E3660 copy of GfxInit without cursor_lock_a */
void gfx_check_config(void);                /* 0x2E3DF0 badpaldac/badlowres/badmedres/badlinmodes */
void gfx_init_once(void);                   /* 0x2E3E78 */
void GfxInit(int mode);                     /* 0x2E3EB0 */
void GfxSetModeAndStuff(short mode);        /* 0x2E3EE0 */
void GfxSetMode(short mode);                /* 0x2E3F90 mode + back canvas */
void GfxSaveAndSetMode(short mode, void *buf);  /* 0x2E40C0 */
void GfxRestoreMode(void *keep);            /* 0x2E4144 */
void GfxShutdown(void);                     /* 0x2E41EC AtExit */
void gfx_2E421C(void);                    /* 0x2E421C */
void gfx_2E4238(Ref ref, int x, int y);   /* 0x2E4238 */
void GfxDrawText(int font, char *s, int x, int y);   /* 0x2E4304 */
void GfxRectVerts(grs_vertex *v, grs_vertex **vp, long x0, long y0, long x1, long y1); /* 0x2E4324 */
void GfxTlucRect(uchar *tab, long x0, long y0, long x1, long y1);   /* 0x2E4370 */
void GfxBlit(LGRect *r);                    /* 0x2E43D0 */
void GfxBlitHFlip(LGRect *r);               /* 0x2E444C (unreferenced) */
void GfxBlitMask(LGRect *r, long x);        /* 0x2E44C8 (unreferenced; x is the caller's edx) */

/* ------------------------------------------------------------------ tnhmd.c */
/* Head tracker drivers (third party / LG Spaceball code, original).  Slot names follow the
 * pointer each address is stored in: init (called once, 0 = ok), shutdown, poll (returns a
 * 0x12-byte record or NULL), set (called with 1, 0 after init). */
typedef struct {
    uchar data[0x10];
    ushort flags;                           /* +0x10 */
} HmdData;
extern HmdData hmd_data;                    /* 0x35F534 (0x12 bytes) */
extern uchar hmd_data_46;                   /* 0x35F546 byte after hmd_data (a quirk reads it) */
extern uchar hmd_busy;                      /* 0x36453C */
extern int (*hmd_init_fn)(void);            /* 0x364544 */
extern void (*hmd_shutdown_fn)(void);       /* 0x364548 */
extern void (*hmd_set_fn)(void);            /* 0x36454C called (1, 0) */
extern void *(*hmd_poll_fn)(void);          /* 0x364550 */
extern char *hmd_name;                      /* 0x3645A0 */
extern int hmd_type;                        /* 0x44D0F0 0 none, 1 Spaceball, 3 Vfx1, 4 Swift,
                                             * 5 CyberMaxx, 6 7th Sense, 7 Virtual IO, 8 Old CyberMx */
extern uchar hmd_null_data[];               /* 0x44D0F4 */
extern char hmd_str_none[], hmd_str_vfx1[], hmd_str_swift[], hmd_str_7th[], hmd_str_vio[],
            hmd_str_oldcmx[], hmd_str_cmx[];

int spaceball_detect(void);   void spaceball_init(void); void spaceball_shutdown(void);
void spaceball_poll(void);    void spaceball_set(void);
int vfx1_detect(void);        void vfx1_init(void);      void vfx1_shutdown(void);
void vfx1_poll(void);         void vfx1_set(void);
int swift_detect(void);       void swift_init(void);     void swift_shutdown(void);
void swift_poll(void);
int sense7_detect(void);      void sense7_init(void);    void sense7_shutdown(void);
void sense7_poll(void);       void sense7_set(void);
int vio_detect(void);         void vio_init(void);       void vio_shutdown(void);
void vio_poll(void);          void vio_set(void);
int oldcmx_detect(void);      void oldcmx_init(void);    void oldcmx_shutdown(void);
void oldcmx_poll(void);       void oldcmx_set(void);
int cmx_detect(void);         void cmx_init(void);       void cmx_shutdown(void);
void cmx_poll(void);          void cmx_set(void);
void hmd_null_init(void);     void hmd_null_func(void);
void te6_2FDC60(int on);                    /* 0x2FDC60 TE6 driver */

bool hmd_detect(void);                      /* 0x2E39D8 */
bool hmd_select(int type);                  /* 0x2E3BE4 (unreferenced) */
void *hmd_null_poll(void);                  /* 0x2E3D0C */
int hmd_init(void);                         /* 0x2E3D20 */
void hmd_shutdown(void);                    /* 0x2E3D64 AtExit */
void hmd_poll(void);                        /* 0x2E3D74 */

/* ------------------------------------------------------------------ tnmusic.c */
/* Miles AIL 3 sequence API (original, cdecl). */
typedef void *HSEQUENCE;
long AIL_init_sequence(HSEQUENCE S, void *start, long num);      /* 0x30D4B4 */
void AIL_start_sequence(HSEQUENCE S);                            /* 0x30D5CC */
void AIL_stop_sequence(HSEQUENCE S);                             /* 0x30D648 */
void AIL_resume_sequence(HSEQUENCE S);                           /* 0x30D6C4 */
void AIL_end_sequence(HSEQUENCE S);                              /* 0x30D740 */
void AIL_set_sequence_tempo(HSEQUENCE S, long tempo, long ms);   /* 0x30D7BC */
void AIL_set_sequence_volume(HSEQUENCE S, long vol, long ms);    /* 0x30D850 */
long AIL_sequence_status(HSEQUENCE S);                           /* 0x30D96C */
void AIL_branch_index(HSEQUENCE S, ulong marker);                /* 0x30E918 */
void *AIL_register_prefix_callback(HSEQUENCE S, void *cb);       /* 0x30E9A0 */
void *AIL_register_trigger_callback(HSEQUENCE S, void *cb);      /* 0x30EAB0 */
void *AIL_register_sequence_callback(HSEQUENCE S, void *cb);     /* 0x30EBC0 */
void *AIL_register_beat_callback(HSEQUENCE S, void *cb);         /* 0x30ECD0 */
void AIL_set_sequence_user_data(HSEQUENCE S, ulong index, long value); /* 0x30F000 */
long AIL_sequence_user_data(HSEQUENCE S, ulong index);           /* 0x30F094 */
#define SEQ_DONE 2
#define SEQ_PLAYING 4
#define SEQ_STOPPED 8

#define MUSIC_SEQS 8
typedef struct {
    uchar vol;                              /* +0x00 0x7F */
    uchar prio;                             /* +0x01 0x3F (passed to music_find_free, unused) */
    uchar f02;
    long tempo;                             /* +0x03 100 */
    long id;                                /* +0x07 */
    void *data;                             /* +0x0B */
    long user;                              /* +0x0F */
    long num;                               /* +0x13 sequence number in the file */
    uchar playing;                          /* +0x17 */
    uchar f18[4];
    signed char next;                       /* +0x1C sequence started when this one ends, -1 */
} MusicSeq;
LG_STATIC_ASSERT(sizeof(MusicSeq) == 0x1D, MusicSeq_size);
typedef struct { uchar f0; long a; long b; } MusicMark;     /* 9 bytes */
typedef struct { uchar f0; long beat; long measure; } MusicBeat;
typedef struct {
    void (*fn)(void);                       /* Watcom, called with eax = byte offset of the entry */
    ulong interval;
    ulong next;
    uchar repeat;                           /* keep the period (next += interval) */
} Periodic;                                 /* 13 bytes */
LG_STATIC_ASSERT(sizeof(Periodic) == 13, Periodic_size);

extern MusicSeq music_seq[MUSIC_SEQS];      /* 0x44B800 */
extern HSEQUENCE music_handle[MUSIC_SEQS];  /* 0x44B8E8 */
extern MusicMark music_mark[MUSIC_SEQS];    /* 0x44B908 */
extern int music_seq_count;                 /* 0x44B950 */
extern MusicBeat music_beat;                /* 0x44B954 */
extern Periodic periodic_tab[8];            /* 0x44B960 */
extern ulong periodic_next_due;             /* 0x44B9C8 */
extern void *music_beat_cb;                 /* 0x35FA10 Watcom (idx, MusicBeat *) */
extern void *music_trigger_cb;              /* 0x35FA14 Watcom (idx, value) */
extern void *music_prefix_cb;               /* 0x35FA18 Watcom (idx, a, b) */
extern void *music_1c;                      /* 0x35FA1C */
extern long music_20;                       /* 0x35FA20 */
extern void *music_play_cb;                 /* 0x35FA24 Watcom (idx, id) */
extern void *music_end_cb;                  /* 0x35FA28 Watcom (id) */
extern void *music_start_cb;                /* 0x35FA2C Watcom (idx, MusicSeq *) */
extern signed char music_current;           /* 0x35FA30 */
extern int periodic_count;                  /* 0x35FA34 */
extern long snd_last_error;                    /* 0x35FA54 */

void music_lock(void);                      /* 0x2EE510 */
HSEQUENCE music_get_handle(char i);         /* 0x2EE53C */
MusicSeq *music_get_seq(char i);            /* 0x2EE548 */
void music_end(char i);                     /* 0x2EE564 */
void music_check_all(void);                 /* 0x2EE580 */
int music_count_playing(void);              /* 0x2EE60C */
char music_find_free(void);                 /* 0x2EE65C */
char music_play(void *data, long id, long num, uchar prio);    /* 0x2EE6B0 */
int music_start(char i);                    /* 0x2EE784 */
int music_start_exclusive(char i);          /* 0x2EE82C */
void music_pause(char i);                   /* 0x2EE8B4 */
void music_resume(char i);                  /* 0x2EE8E8 */
int music_set_volume(char i, long vol, long ms);   /* 0x2EE91C */
int music_get_volume(char i);               /* 0x2EE978 */
void music_set_user(char i, long v);        /* 0x2EE994 */
long music_get_user(char i);                /* 0x2EE9AC */
long music_set_tempo(char i, long tempo, long ms); /* 0x2EE9C4 */
long music_get_tempo(char i);               /* 0x2EEA14 */
void music_branch(char i, ulong marker);    /* 0x2EEA2C */
void *music_set_trigger_cb(void *f);        /* 0x2EEA48 */
void music_trigger_hook(HSEQUENCE S, long channel, long value);           /* 0x2EEA58 cdecl */
void *music_set_beat_cb(void *f);           /* 0x2EEA80 */
void music_beat_hook(void *mdi, HSEQUENCE S, long beat, long measure);    /* 0x2EEA90 cdecl */
void *music_set_prefix_cb(void *f);         /* 0x2EEACC */
void music_prefix_hook(HSEQUENCE S, long a, long b);                      /* 0x2EEADC cdecl */
void *music_set_1c(void *f, long v);        /* 0x2EEB00 */
long music_set_mark(char i, long a, long b);       /* 0x2EEB18 */
void music_seq_hook(HSEQUENCE S);                                          /* 0x2EEB40 cdecl */
void music_register_hooks(char i);          /* 0x2EEBE0 */
char music_play_ex(long id, void *data, long num, MusicSeq *settings);    /* 0x2EEC48 */
void music_default_settings(MusicSeq *s);   /* 0x2EED58 */
void music_set_start_cb(void *f);           /* 0x2EED6C */
char music_current_playing(void);           /* 0x2EED74 */
void music_set_play_cb(void *f);            /* 0x2EEDA4 */
void music_set_next(char i, char next);     /* 0x2EEDAC */
void music_end_beat_hook(void *mdi, HSEQUENCE S, long beat, long measure); /* 0x2EEE20 cdecl */
void music_end_trigger_hook(HSEQUENCE S, long channel, long value);       /* 0x2EEE34 cdecl */
void music_end_at_beat(char i);             /* 0x2EEE4C */
void music_end_at_trigger(char i);          /* 0x2EEE6C */
void music_unlock(void);                    /* 0x2EEE8C */
void periodic_clear(void);                  /* 0x2EEEC0 */
bool periodic_add(void *fn, ulong interval, uchar repeat);   /* 0x2EEED0 */
void periodic_remove(void *fn);             /* 0x2EEF20 */
void periodic_run(ulong now);               /* 0x2EEF84 */
void periodic_update(void);                 /* 0x2EEFF4 */

/* ------------------------------------------------------------------ invcmap.c */
int ipal_alloc(void);                       /* 0x2F1130 */
int ipal_free(void);                        /* 0x2F1198 (unreferenced) */
int ipal_update(void);                      /* 0x2F11C4 */
int ipal_build(grs_rgb *bpal, uchar *ipal); /* 0x2F11E4 */
void inv_cmap(int colors, uchar **colormap, int bits, ulong *dist_buf, uchar *rgbmap); /* 0x2F12C4 */
int inv_cmap_redloop(void);                 /* 0x2F14E0 */
int inv_cmap_greenloop(int restart);        /* 0x2F1664 */
int inv_cmap_blueloop(int restart);         /* 0x2F1954 */
void inv_cmap_maxfill(ulong *buffer);       /* 0x2F1B3C (unreferenced) */

/* ------------------------------------------------------------------ tnwater.c */
int water_scan_edges(int n, fix **vp, long *buf);   /* 0x2FB220 */
int water_left_edge(fix **v1, fix **v0, long *buf);  /* 0x317DF0 asm (original) */
int water_right_edge(fix **v1, fix **v0, long *buf); /* 0x317EA0 asm (original) */

#pragma pack(pop)
#endif
