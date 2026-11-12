/* t2_c.h -- declarations for worker t2c (batch T2: TN support code, src/game/tnlib/):
 *   viewport.c   tnui regions ("viewports"): creation, show/hide, move, dirty-rect lists,
 *                redraw and blit tasks (0x2E55D8-0x2E66B0)
 *   ailstart.c   Miles AIL startup glue and master volumes (0x2EF050-0x2EF200)
 *   audiocfg.c   audio.cfg reading, MIDI/digital driver install, sequence/sample handle pools,
 *                stereo sample pairs (0x2EF750-0x2F059C)
 *   animrgn.c    animations played in their own tnui region (0x2FD330-0x2FD8E0)
 * Names are ours unless noted (no assert paths here); the viewport_* / tnui2_* / tnui_task_*
 * names were given by earlier batches and kept so their code keeps linking.
 * Globals are bound to their original addresses through symbols-t2-c.csv.
 */
#ifndef TN_T2_C_H
#define TN_T2_C_H
#include "support.h"
#include "sup_c.h"
#include "res.h"
#include "2d.h"
#include "t2_a.h"      /* music layer: music_seq, music_seq_count (owner t2a) */
#include "t2_b.h"      /* digital layer, sndcfg_*, AIL sample API, anim_* (owner t2b) */

#pragma pack(push, 1)

/* ================================================================== viewport.c */
/* A tnui region: a node of the priority list tnui_list (LlistInit(&tnui_list, 0xBC, 16));
 * the last node is the topmost.  Types:
 *   0 drawn straight to the screen (sub canvas of grd_screen_canvas)
 *   1 drawn into the back buffer (sub canvas of tnui_back_canvas); shown by blitting
 *   2 own off-screen bitmap, copied into the back buffer when a dirty rect is redrawn
 *   3 drawn by its draw method into the back buffer (clipped) when a dirty rect is redrawn
 *     (create makes it a sub canvas of the back buffer, like type 1) */
typedef struct TnRegion {
    struct TnRegion *next;  /* +0  llist */
    struct TnRegion *prev;  /* +4 */
    short prio;             /* +8  queue priority */
    uchar type;             /* +A  TNR_xxx */
    uchar vflags;           /* +B  TNRF_xxx */
    LGRect rect;            /* +C  screen rectangle */
    grs_canvas cnv;         /* +14 */
    long fill_type;         /* +54 used when a type 2 bitmap is copied to the back buffer */
    long fill_parm;         /* +58 */
    ulong mask;             /* +5C events the region wants (TNE_xxx | mouse/key bits) */
    uchar *save;            /* +60 screen pixels under the region (create flag 2) */
    void *event;            /* +64 uchar (*)(TnRegion *, TnEvent *) (Watcom) */
    void *draw;             /* +68 void (*)(TnRegion *, LGRect *) (Watcom) */
    void *user;             /* +6C style (text boxes), animation (animrgn.c) */
    uchar extra[0x4C];      /* +70 owner data (animrgn.c: canvas +70, own-bitmap flag +B0) */
} TnRegion;
LG_STATIC_ASSERT(sizeof(TnRegion) == 0xBC, TnRegion_size);
#define TNR_SCREEN 0
#define TNR_BACK   1
#define TNR_BITMAP 2
#define TNR_DRAWN  3
#define TNRF_HIDDEN 1
#define TNRF_TRANSP 2           /* create flag 1: not opaque (bm.flags BMF_TRANS) */
#define TNRF_DEAD   4           /* destroyed, freed by tnui_task_60f4 */

/* Region events (12 bytes; the same layout as tnmouse_event / tnkey_event). */
typedef struct { ulong type; ulong pad; LGPoint pt; } TnEvent;
#define TNE_DESTROY 0x100000
#define TNE_MOVE    0x200000
#define TNE_ENTER   0x400000
#define TNE_LEAVE   0x800000

typedef struct { ushort id; ulong mask; void *event; void *draw; } TnRegionClass;
/* creation parameters (packed, 0x11 bytes) */
typedef struct {
    TnRegionClass *cls;     /* +0 */
    uchar type;             /* +4 */
    LGRect rect;            /* +5 */
    short prio;             /* +D */
    ushort flags;           /* +F bit 0 transparent, bit 1 save the screen under it */
} TnRegionParams;
LG_STATIC_ASSERT(sizeof(TnRegionParams) == 0x11, TnRegionParams_size);

#define TNUI_MAX_RECTS 16
extern TnRegion *tnui_hover;                /* 0x447220 region under the mouse */
extern TnRegion *viewport_key_focus;        /* 0x447240 (T1 name) mouse capture: gets all mouse events */
extern short tnui_blit_count;               /* 0x447244 */
extern short tnui_dirty_count;              /* 0x447246 */
extern short tnui_dead_count;               /* 0x447248 */
extern LGRect tnui_blit_rects[TNUI_MAX_RECTS];  /* 0x447120 back buffer -> screen */
extern LGRect tnui_dirty_rects[TNUI_MAX_RECTS]; /* 0x4471A0 regions to redraw */
extern char viewport_check_msg[];           /* 0x35713C */

void GfxBlit(LGRect *r);                    /* 0x2E43D0 (T2a) back buffer -> screen */

TnRegion *viewport_2E55D8(TnRegionParams *p);            /* create */
void viewport_2E575C(TnRegion *r);                       /* destroy */
void viewport_2E57F0(TnRegion *r);                       /* show */
void viewport_2E582C(TnRegion *r);                       /* hide */
void viewport_2E5848(TnRegion *r, LGRect *rect);         /* draw now */
void viewport_2E5954(TnRegion *r, long dx, long dy, long dprio, long w, long h); /* move/resize */
TnRegion *tnui_region_at(LGPoint pt);                    /* 0x2E5A64 */
/* bool tnui_key_handler(void *e, void *node);  0x2E5B6C, declared in sup_c.h */
uchar tnui_region_event(TnRegion *r, TnEvent *e);        /* 0x2E5BB0 */
void viewport_2E5BC8(TnRegion *r, LGRect *rect);         /* update (rect relative) */
void viewport_2E5C94(TnRegion *r, LGRect *rect);         /* update (rect absolute) */
void tnui_add_dirty_rect(LGRect *r);                     /* 0x2E5CC8 */
void tnui_add_blit_rect(LGRect *r);                      /* 0x2E5DB0 */
void tnui_redraw_rect(LGRect *r);                        /* 0x2E5E98 */
void tnui_redraw_rect_before(TnRegion *end, LGRect *r);  /* 0x2E5FAC */
void tnui_redraw_rect_after(TnRegion *start, LGRect *r); /* 0x2E6020 */
void tnui_clear_back(void);                              /* 0x2E6094 */
void tnui_blit_all(void);                                /* 0x2E60E0 */
void tnui_task_60f4(void);                               /* 0x2E60F4 (sup-c name) */
void tnui_task_6174(void);                               /* 0x2E6174 (sup-c name) */
/* bool tnui_mouse_handler(void *e, void *node); 0x2E62D0, declared in sup_c.h */
void tnui_save_under(TnRegion *r);                       /* 0x2E6304 */
void tnui_restore_under(TnRegion *r);                    /* 0x2E6398 */
void tnui_draw_region_rect(TnRegion *r, LGRect *rect);   /* 0x2E6410 */
void tnui_copy_region_rect(TnRegion *r, LGRect *rect);   /* 0x2E64BC */
void ViewportCheckPtr(TnRegion *r);                      /* 0x2E6674 (name from its message) */

/* ================================================================== ailstart.c */
/* Miles AIL (original, cdecl) */
long AIL_startup(void);                                     /* 0x309CE4 */
void AIL_shutdown(void);                                    /* 0x309E88 */
/* AIL_set_preference, AIL_lock, AIL_unlock: t2_b.h */
void *AIL_install_DIG_driver_file(char *name, void *io);    /* 0x30B300 */
void AIL_uninstall_DIG_driver(void *dig);                   /* 0x30B410 */
void *AIL_allocate_sample_handle(void *dig);                /* 0x30B48C */
void AIL_release_sample_handle(void *s);                    /* 0x30B6A4 */
/* AIL_init_sample .. AIL_sample_status: t2_b.h */
void AIL_set_digital_master_volume(void *dig, long v);      /* 0x30C2EC */
long AIL_digital_master_volume(void *dig);                  /* 0x30C374 */
void AIL_set_sample_user_data(void *s, ulong index, long v);/* 0x30CE08 */
void *AIL_install_MDI_driver_file(char *name, void *io);    /* 0x30D1AC */
void AIL_uninstall_MDI_driver(void *mdi);                   /* 0x30D2BC */
void *AIL_allocate_sequence_handle(void *mdi);              /* 0x30D338 */
void AIL_release_sequence_handle(void *s);                  /* 0x30D438 */
long AIL_sequence_status(void *s);                          /* 0x30D96C (same as t2_a.h: HSEQUENCE = void *) */
void AIL_set_XMIDI_master_volume(void *mdi, long v);        /* 0x30DD6C */
long AIL_XMIDI_master_volume(void *mdi);                    /* 0x30DDF4 */
void AIL_set_GTL_filename_prefix(char *prefix);             /* 0x30E10C */
void AIL_set_sequence_user_data(void *s, ulong index, long v); /* 0x30F000 */

extern char *snd_datapath;                  /* 0x35FA38 (&gDatapath; read by sndcfg_310170) */
extern void *snd_dig_driver;                /* 0x35FA3C DIG_DRIVER * */
extern void *snd_mdi_driver;                /* 0x35FA40 MDI_DRIVER * */
extern char snd_gtl_prefix[];               /* 0x35FA59 */

void snd_ail_startup(void);                 /* 0x2EF050 */
void snd_ail_setup(char *datapath, char *gtl_prefix); /* 0x2EF0A8 */
void snd_ail_shutdown(void);                /* 0x2EF134 */
void snd_lock(void);                        /* 0x2EF148 */
void snd_unlock(void);                      /* 0x2EF154 */
void snd_set_midi_volume(long v);           /* 0x2EF160 */
long snd_get_midi_volume(void);             /* 0x2EF190 */
void snd_set_digital_volume(long v);        /* 0x2EF1B0 */
long snd_get_digital_volume(void);          /* 0x2EF1E0 */

/* ================================================================== audiocfg.c */
/* A sound card description (0x110 bytes on the game's stack: one digital, one MIDI). */
/* Fields used here: idx +0 (driver index 0..19 from audio.cfg, -1 none), drv +1 (driver file
 * name), unk5 +5, drvflags +105 (per-driver byte, snd_drv_flags; 1 after digital install), io +106
 * (port/irq/dma..., -1 = unknown), io10e +10E (1: load the AWE32 .sbk sound bank).  Same record as
 * T2b's SndCardCfg (t2_b.h, which has both sets of names) since the G2 merge. */
typedef SndCardCfg SndCard;
#define SND_DRV_AWE32 13

/* stereo sample pair: a sound played on two AIL samples (left/right) */
typedef struct {
    signed char partner;    /* +0 the other sample of the pair */
    ulong flags;            /* +1 caller flags; 0x200 wait for the partner, 0x400 = second half */
    long pan;               /* +5 0..127 */
    long vol;               /* +9 0..127 */
} SndPair;
LG_STATIC_ASSERT(sizeof(SndPair) == 13, SndPair_size);
#define SND_MAX_SAMPLES 16
#define SND_MAX_SEQS 8
/* per-sample callbacks of the digital layer: snd_chan_ops (t2_b.h; set / get / fns / done /
 * done_user are this file's names for its fields) */
typedef snd_chan_ops SndSampleCb;
/* panning table entry: left/right volume factor and pan of each of the two samples */
typedef struct { long lvol, lpan, rvol, rpan; } SndPanEntry;

extern char *snd_drv_names[40];             /* 0x3651E8 [base + idx]: 0..19 MIDI, 20..39 digital */
extern uchar snd_drv_flags[40];             /* 0x365288 */
extern char snd_cfg_name[];                 /* 0x3571DC "audio.cfg" */
extern char snd_sbk_ext[];                  /* 0x3571E8 ".sbk" */
extern const char snd_sbk_template[32];     /* 0x2EF890 (zeros, in the code segment) */
extern void *snd_mdi_lock;                  /* 0x35FA80 (copy of snd_mdi_driver; DPMI-locked) */
extern uchar snd_sbk_wanted;                /* 0x35FA84 */
extern long snd_mdi_type;                   /* 0x35FA88 driver index of the installed MIDI driver */
extern SndPair snd_pairs[SND_MAX_SAMPLES];  /* 0x35FA8C */
extern uchar snd_lock_data_end[];           /* 0x36036C end of the locked data range */
extern void *snd_pair_fns[];                /* 0x35FB5C function table set for paired samples */
extern SndPanEntry snd_pan_table[128];      /* 0x35FB70 */
/* music_seq (MusicSeq[8]) 0x44B800, music_seq_count 0x44B950: t2_a.h */
extern uchar snd_music_slots2[9 * 9];       /* 0x44B900 9-byte records 1..8 (+0, +4 cleared) */
extern void *music_handle[SND_MAX_SEQS]; /* 0x44B8E8 */
/* digital layer (T2b) data */
extern void *snd_dig_lock;                  /* 0x35F94C (copy of snd_dig_driver) */
extern long snd_sample_count;               /* 0x35F950 */
extern uchar snd_dig_stereo;                /* 0x35F954 */
extern void *snd_sample_handles[SND_MAX_SAMPLES]; /* 0x44B600 */
/* snd_sample_cbs (snd_chan_ops) 0x44B640, snd_sample_info (snd_digi_parms) 0x44B3A0: t2_b.h */
extern uchar snd_sample_info2[17 * 17];     /* 0x44B288 17-byte records 1..16 (+0 cleared) */
/* snd_ail_vtbl (snd_chan_vtbl) 0x35F938 default function table: t2_b.h */
extern void *snd_ail_getters[];          /* 0x35F900 cdecl long (*)(sample) by attribute */
extern void *snd_ail_setters[];          /* 0x35F91C cdecl void (*)(sample, v): [0] pan, [1] volume */
extern void *snd_sample_done_hook;          /* 0x35F8F8 Watcom (chan, user) */
extern long snd_last_error;                 /* 0x35FA54 */

/* sound card config sndcfg_30FF90 .. sndcfg_310260: t2_b.h */
void sbk_load(void *mdi, char *path);       /* 0x310D34 AWE32 sound bank */
void sbk_unload(void *mdi);                 /* 0x31116C */
/* digital / music layers (T2b / T2a, original) */
/* digital layer digi_* (start a sample, end, free count, set attribute, default callbacks): t2_b.h */
void music_lock(void);                      /* 0x2EE510 */
void music_check_all(void);                   /* 0x2EE580 */
void music_unlock(void);                    /* 0x2EEE8C */

char *snd_cfg_driver_name(SndCard *c, long base);       /* 0x2EF750 */
ulong snd_read_audio_cfg(SndCard *dig, SndCard *midi);  /* 0x2EF798 */
void snd_cfg_clear(SndCard *c);                         /* 0x2EF858 */
void snd_music_slots_init(void);                        /* 0x2EF8B0 */
long snd_midi_install(SndCard *c);                      /* 0x2EF8EC */
long snd_set_sequence_count(long n);                    /* 0x2EFA0C */
long snd_midi_uninstall(void);                          /* 0x2EFB2C */
void snd_load_sbk(long type);                           /* 0x2EFB8C */
void snd_unload_sbk(void);                              /* 0x2EFC1C */
long snd_dig_install(SndCard *c);                       /* 0x2EFC30 */
long snd_set_sample_count(long n);                      /* 0x2EFD0C */
void snd_samples_reset_user(void);                      /* 0x2EFE30 */
uchar snd_dig_is_stereo(void);                          /* 0x2EFE6C */
long snd_dig_uninstall(void);                           /* 0x2EFE74 */
void snd_sample_tables_init(void);                      /* 0x2EFEA0 */
void snd_pair_lock(void);                               /* 0x2EFF50 */
long snd_pair_play(void *snd0, void *snd1, long a, long chan_req, long c, ulong flags); /* 0x2EFF7C */
void snd_pair_set(long chan, long what, long v);        /* 0x2F00E8 */
long snd_pair_get(long chan, long what);                /* 0x2F02A8 */
long snd_pair_start(long chan);                         /* 0x2F0304 */
long snd_pair_end(long chan);                           /* 0x2F03DC */
long snd_pair_stop(long chan);                          /* 0x2F0444 */
long snd_pair_resume(long chan);                        /* 0x2F0488 */
long snd_pair_done(long chan);                          /* 0x2F04CC */
void snd_pair_unlock(void);                             /* 0x2F0570 */

/* ================================================================== animrgn.c */
/* animation header (resource; T2b's delta animation player) */
typedef struct {
    short w, h;             /* +0 */
    short unk4;             /* +4 */
    short hotx, hoty;       /* +6 */
    short base_z;           /* +A 0: not scaled, else size scales by base_z / z */
    uchar flags;            /* +C 1 transparent, 4 frames are bitmaps (RefGet of +2F) */
    uchar pad;
} AnimHdr;
LG_STATIC_ASSERT(sizeof(AnimHdr) == 14, AnimHdr_size);
/* the fields of the animation player's object used here (T2b) */
#define ANIM_BM(a)     (*(grs_bitmap **)((uchar *)(a) + 0x08))
#define ANIM_REGION(a) (*(TnRegion **)((uchar *)(a) + 0x10))
#define ANIM_HDR(a)    (*(AnimHdr **)((uchar *)(a) + 0x21))
#define ANIM_FRAME(a)  (*(Ref *)((uchar *)(a) + 0x2F))
/* per-frame info handed to the callback */
typedef struct {
    short x, y;             /* +0  move */
    short dprio;            /* +4 */
    short x2, y2;           /* +6  more move (flags 0x42) */
    LGPoint size;           /* +A  new size, 0: header size */
    LGRect upd;             /* +E  rectangle to update (flag 1) */
} AnimFrameInfo;
/* region data after the TnRegion header */
#define ANIMRGN_CNV(r)  ((grs_canvas *)((r)->extra))          /* +70 */
#define ANIMRGN_OWN(r)  ((r)->extra[0x40])                    /* +B0 own bitmap */
typedef struct { LGPoint pt; short z; } AnimPos;

extern TnRegionClass animrgn_class;         /* 0x364518 */
extern TnRegionParams animrgn_params;       /* 0x364528 */
extern uchar animrgn_use_back;              /* 0x364539 */
/* anim_2E68A0 (start the delta animation player): t2_b.h */

void animrgn_back_on(void);                                 /* 0x2FD330 */
void animrgn_back_off(void);                                /* 0x2FD338 */
void *tnui2_2FD344(Ref ref, AnimPos *pos);                  /* 0x2FD344 (T1 name) start */
void animrgn_callback(void *anim, uchar flags, AnimFrameInfo *fi); /* 0x2FD4AC */
void animrgn_draw(TnRegion *r);                             /* 0x2FD64C */
void animrgn_rect(TnRegion *r, short base_z, LGPoint size, LGPoint hot, LGRect *out); /* 0x2FD710 */
void animrgn_start_rect(Ref ref, AnimPos *pos, LGRect *out);  /* 0x2FD7C4 */
long animrgn_scale(short z, short base_z, LGPoint pt, LGPoint *out); /* 0x2FD88C */

#pragma pack(pop)
#endif
