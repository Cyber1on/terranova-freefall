/* t2_b.h -- declarations for worker t2b (batch T2): TN support code in src/game/tnlib/:
 * the delta animation player (tnanim.c), the PCX screenshot writer (pcxshot.c), the digital
 * sample layer over Miles AIL (tndigi.c), file name split/merge (fnsplit.c), the sound card
 * configuration reader (sndcfg.c).  Names are ours unless noted.  Globals are bound to their original addresses
 * through symbols-t2-b.csv.
 */
#ifndef TN_T2_B_H
#define TN_T2_B_H
#include "support.h"
#include "res.h"
#include "2d.h"
#include "sup_c.h"      /* AIL timer API (AIL_register_timer ..., owner sup-c) */

#pragma pack(push, 1)

/* ------------------------------------------------------------------ calling original cdecl code */
/* Miles AIL API wrappers and the DPMI lock helper are cdecl but clobber ebx (and ecx, edx), which
 * gcc expects to be preserved.  t2b_cdecl(f, a, b, c) calls f(a, b, c) through a stub that saves
 * ebx/esi/edi/ebp (extra args are harmless for cdecl).  tndigi.c defines the stub.
 * Redundant since the T2 merge: gen_thunks.py now routes every call to an orig cdecl ('v') row
 * through a register-saving thunk, so the name already is a safe entry (A_name = raw address).
 * Kept so tndigi.c / sndcfg.c stay unchanged; new code calls the AIL functions directly. */
long t2b_cdecl(void *f, long a, long b, long c);
#define T2B_CDECL(f, a, b, c) t2b_cdecl((void *)(f), (long)(a), (long)(b), (long)(c))

/* Miles AIL sample API (original code, cdecl, symbols-t2-b.csv orig rows; HSAMPLE = void *).
 * Prototypes since the G2 merge (they were char[] here; the timer functions are in sup_c.h). */
long AIL_set_preference(ulong number, long value);          /* 0x309F1C */
void AIL_lock(void);                                        /* 0x30A02C */
void AIL_unlock(void);                                      /* 0x30A034 */
void AIL_init_sample(void *s);                              /* 0x30B720 */
long AIL_set_sample_file(void *s, void *image, long block); /* 0x30B79C */
void AIL_set_sample_address(void *s, void *start, ulong len); /* 0x30B8B4 */
void AIL_set_sample_type(void *s, long format, ulong flags); /* 0x30B948 */
void AIL_start_sample(void *s);                             /* 0x30B9DC */
void AIL_stop_sample(void *s);                              /* 0x30BA58 */
void AIL_resume_sample(void *s);                            /* 0x30BAD4 */
void AIL_end_sample(void *s);                               /* 0x30BB50 */
void AIL_set_sample_playback_rate(void *s, long rate);      /* 0x30BBCC */
void AIL_set_sample_volume(void *s, long vol);              /* 0x30BC54 */
void AIL_set_sample_pan(void *s, long pan);                 /* 0x30BCDC */
void AIL_set_sample_loop_count(void *s, long n);            /* 0x30BD64 */
ulong AIL_sample_status(void *s);                           /* 0x30BDEC */
long AIL_sample_playback_rate(void *s);                     /* 0x30BEEC */
void *AIL_register_EOS_callback(void *s, void *cb);         /* 0x30CBE8 */
long AIL_sample_user_data(void *s, ulong index);            /* 0x30CE9C */
long AIL_active_sample_count(void *dig);                    /* 0x30CFAC */
int dpmi_lock_region(void *start, void *end);               /* 0x2EDB28 (sup-c row), cdecl */
#define SMP_DONE 2
#define SMP_PLAYING 4

/* inlined string helpers (Watcom expands these inline; the copies run byte by byte) */
static inline char *t2b_strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}
static inline char *t2b_strcat(char *d, const char *s)
{
    t2b_strcpy(d + strlen(d), s);
    return d;
}
static inline char *t2b_strchr(const char *s, char c)
{
    for (; *s != c; s++)
        if (*s == 0)
            return NULL;
    return (char *)s;
}

/* ------------------------------------------------------------------ tnanim.c */
/* Animation resource header (locked compound resource item). */
typedef struct {
    LGPoint size;       /* +0  reported to the notify callback (AN_SIZE) when it changes */
    Id frames;          /* +4  compound resource holding the FrameDescs */
    LGPoint offset;     /* +6  reported (as a delta) when it changes */
    short depth;        /* +A  nonzero: dz moves / depth changes are reported (AN_DEPTH) */
    ushort flags;       /* +C  bit 1: frame times accumulate (ActAnim flag AF_ACCUM) */
    uchar codes[1];     /* +E  animation op codes */
} AnimHead;

/* Frame (compound resource item): a bitmap header, the update rectangle, then the bits. */
typedef struct {
    grs_bitmap bm;      /* +0  BMT_RSD8 (4) frames are drawn, others ignored */
    LGRect update;      /* +10 */
    long pal_off;       /* +18 offset of a palette block in the frames resource, 0 = none */
    uchar bits[1];      /* +1C */
} AnimFrame;

/* Data handed to the callbacks (copied out of the anim, then cleared). */
typedef struct {
    short dx, dy, dz;   /* +0  accumulated motion (AN_MOVE) */
    LGPoint doff;       /* +6  offset change (AN_OFFSET) */
    LGPoint size;       /* +A  new size (AN_SIZE) */
    LGRect update;      /* +E  area drawn since the last call (AN_FRAME) */
    short user;         /* +16 op 11 argument (AN_USER) */
} AnimCodeData;
LG_STATIC_ASSERT(sizeof(AnimCodeData) == 0x18, AnimCodeData_size);

typedef struct ActAnim {
    llist link;         /* +00 node in anim_list */
    grs_canvas *cnv;    /* +08 target canvas, NULL = don't draw */
    void *code_cb;      /* +0C (anim, pending, data) for any pending bit in 0xEB */
    void *user;         /* +10 */
    void *notify_cb;    /* +14 (anim, pending, data) for pending bits in notify_mask */
    void *notify_data;  /* +18 */
    uchar notify_mask;  /* +1C */
    Ref ref;            /* +1D the AnimHead */
    AnimHead *head;     /* +21 locked */
    uchar *pc;          /* +25 next op code */
    uchar *loop_pc;     /* +29 */
    short loop_count;   /* +2D */
    Ref frame_ref;      /* +2F current frame: id of the frames resource, item index */
    short frame;        /* +33 frame-run counter, -1 = not in a frame run */
    short last_frame;   /* +35 */
    short frame_time;   /* +37 ticks per frame of the run */
    fix time_scale;     /* +39 0 = 1.0 (set by the script layer) */
    ulong next_time;    /* +3D */
    fix pos[3];         /* +41 fractional motion */
    fix vel[3];         /* +4D per tick (op 8 / anim_2E6AAC divide by 1000) */
    ulong last_time;    /* +59 */
    uchar pending;      /* +5D AN_xxx bits for the callbacks */
    AnimCodeData data;  /* +5E */
    ushort flags;       /* +76 AF_xxx */
    uchar *buf;         /* +78 frames extracted here (anim_2E6B98), NULL = lock each frame */
    ulong bufsize;      /* +7C */
} ActAnim;
LG_STATIC_ASSERT(sizeof(ActAnim) == 0x80, ActAnim_size);

/* ActAnim.flags */
#define AF_ACCUM 0x01   /* next_time += frame time (else now + frame time) */
#define AF_DEAD 0x02    /* free at the end of the next anim_2E66F0 */
#define AF_PAUSED 0x04  /* no codes run */
#define AF_MOVING 0x08  /* has a velocity */
#define AF_INBUF 0x20   /* frames resource extracted into buf */
/* ActAnim.pending */
#define AN_FRAME 0x01
#define AN_MOVE 0x02
#define AN_END 0x04
#define AN_KILL 0x08
#define AN_USER 0x10
#define AN_DEPTH 0x20
#define AN_OFFSET 0x40
#define AN_SIZE 0x80

extern LlistHead anim_list;                 /* 0x447250 */
extern short anim_kill_count;               /* 0x44726C */
extern ulong *anim_time_ptr;                /* 0x35F568 */
extern uchar anim_use_time_ptr;             /* 0x35F56C */
extern uchar anim_op_len[];                 /* 0x35F56D (indexed by any op byte) */

void AnimInit(void);                                        /* 0x2E66B0 */
void AnimSetTimePtr(ulong *p);                              /* 0x2E66D4 */
void AnimShutdown(void);                                    /* 0x2E66E4 (AtExit) */
void anim_2E66F0(void);                                     /* 0x2E66F0 AnimRecur */
ActAnim *anim_2E68A0(Ref ref, void *user, grs_canvas *cnv, void *code_cb); /* 0x2E68A0 */
void anim_2E695C(void *anim, long ref);                     /* 0x2E695C AnimChangeRef */
void anim_2E6A34(void *anim);                               /* 0x2E6A34 AnimKill */
void AnimKillAll(void);                                     /* 0x2E6A64 */
void anim_2E6AAC(void *anim, long vx, long vy, long vz);    /* 0x2E6AAC AnimSetVelocity */
void AnimMoveBy(ActAnim *pa, short dx, short dy, short dz); /* 0x2E6AF8 (unreferenced) */
void anim_2E6B3C(void *anim, void *data, uchar mask, void *cb); /* 0x2E6B3C AnimSetNotify */
void anim_2E6B48(Ref ref);                                  /* 0x2E6B48 AnimSetPalette */
bool anim_2E6B98(void *anim, Ref ref);                      /* 0x2E6B98 AnimLoadFrames */
long AnimGetSize(Ref ref);                                  /* 0x2E6C08 LGPoint as a long */
void AnimRestart(ActAnim *pa);                              /* 0x2E6C10 */
Ref AnimFirstFrameRef(ActAnim *pa);                         /* 0x2E6CC0 */
void AnimDoCodes(ActAnim *pa, ulong now);                   /* 0x2E6D54 */
void AnimMove(ActAnim *pa, ulong dt);                       /* 0x2E7044 */
void AnimShowFrame(ActAnim *pa);                            /* 0x2E7124 */

/* ------------------------------------------------------------------ pcxshot.c */
int pcx_open_next(char *namebuf, char *base, char *ext);    /* 0x2EAF00 */
int pcx_screenshot(char *namebuf, char *base);              /* 0x2EB070 */
int pcx_write_canvas(int fd);                               /* 0x2EB090 */
int pcx_write_line(int y, int fd, int w);                   /* 0x2EB184 */

/* ------------------------------------------------------------------ tndigi.c */
#define SND_MAX_SAMPLES 16
typedef struct {
    uchar pan;          /* +0  0..0x7F, default 0x40 */
    uchar vol;          /* +1  default 0x7F */
    uchar pri;          /* +2  default 0x3F */
    uchar flags;        /* +3  SND_FLG_xxx */
    uchar pad4;         /* +4 */
    uchar type;         /* +5  AIL sample type for raw data */
    long rate;          /* +6  playback rate << 8 (default 22050 << 8) */
    long pitch;         /* +A  0 = none, else rate scale (8.8 after >> 8) */
    long snd_ref;       /* +E */
    long loops;         /* +12 default 1 */
    void *data;         /* +16 */
    long user;          /* +1A handed to snd_sample_done_hook */
    long len;           /* +1E */
    void *hnd;          /* +22 HSAMPLE */
} snd_digi_parms;
LG_STATIC_ASSERT(sizeof(snd_digi_parms) == 0x26, snd_digi_parms_size);
#define SND_FLG_NODATA 0x01     /* no sample data yet (snd_sample_play with data NULL) */
#define SND_FLG_INUSE 0x02
#define SND_FLG_RAWDATA 0x80    /* not a file AIL recognises: set type + address */

typedef struct {
    void *start, *end, *stop, *resume;      /* Watcom (handle) */
    void *eos;                              /* Watcom (handle) */
} snd_chan_vtbl;

/* Per-sample callbacks (snd_sample_cbs).  T2c's audiocfg.c (stereo pairs) uses the same record
 * as SndSampleCb with the names set / get / fns / done / done_user; both kept (G2 merge). */
typedef struct {
    union { void *set_parm; void *set; };   /* +0  Watcom (handle, parm, value) */
    union { void *get_parm; void *get; };   /* +4  Watcom (handle, parm) */
    union { snd_chan_vtbl *vt; void *fns; };/* +8 */
    void *done;         /* +C  Watcom (chan, user), called once by snd_pair_done (T2c) */
    long done_user;     /* +10 */
} snd_chan_ops;
LG_STATIC_ASSERT(sizeof(snd_chan_ops) == 0x14, snd_chan_ops_size);

typedef struct {
    long cur;           /* +0 */
    uchar pad4;         /* +4 */
    long target;        /* +5 */
    long steps;         /* +9 remaining time, 256 per timer tick */
    long flags;         /* +D bit 0: end the sample when done; 0x1000: active */
} snd_fade_info;
LG_STATIC_ASSERT(sizeof(snd_fade_info) == 0x11, snd_fade_info_size);

/* parameter ids of digi_2EBDA0 / snd_get_parm */
#define SND_PARM_PAN 0
#define SND_PARM_VOL 1
#define SND_PARM_RATE 2
#define SND_PARM_LOOPS 3
#define SND_PARM_POS 4
#define SND_PARM_PRI 5
#define SND_PARM_USER 6

extern void *snd_sample_done_hook;                    /* 0x35F8F8 Watcom (handle, user) */
extern long snd_finish_data;                /* 0x35F8FC */
extern void *snd_ail_getters[7];            /* 0x35F900 cdecl (HSAMPLE) */
extern void *snd_ail_setters[7];            /* 0x35F91C cdecl (HSAMPLE, value) */
extern snd_chan_vtbl snd_ail_vtbl;          /* 0x35F938 */
extern long snd_sample_count;                   /* 0x35F950 */
extern uchar snd_dig_stereo;                    /* 0x35F954 pan changes allowed */
extern long snd_fade_timer;                 /* 0x35F958 HTIMER, -1 = none */
extern void *snd_dig_driver;                      /* 0x35FA3C HDIGDRIVER */
extern void *snd_finish_parms;              /* 0x35FA4C Watcom (snd_digi_parms *) */
extern long snd_last_error;                      /* 0x35FA54 */
extern uchar snd_stereo_reverse;            /* 0x35FA58 */
extern snd_fade_info snd_fade_tab[SND_MAX_SAMPLES];   /* 0x44B290 */
extern snd_digi_parms snd_sample_info[SND_MAX_SAMPLES];  /* 0x44B3A0 */
extern void *snd_sample_handles[SND_MAX_SAMPLES];           /* 0x44B600 */
extern snd_chan_ops snd_sample_cbs[SND_MAX_SAMPLES];        /* 0x44B640 */

void digi_2EB5E0(void);                                   /* 0x2EB5E0 */
void *snd_sample_handle(int i);                             /* 0x2EB60C */
snd_digi_parms *snd_sample_parms(int i);                    /* 0x2EB614 */
void *snd_set_finish(void *fn, long data);                  /* 0x2EB62C returns the old one */
void snd_eos_callback(void *s);                             /* 0x2EB644 cdecl (AIL) */
void digi_2EB67C(void);                            /* 0x2EB67C */
char snd_find_free_handle(uchar pri, bool check_only);      /* 0x2EB6DC */
void snd_sample_reload_parms(snd_digi_parms *p);            /* 0x2EB7EC */
char digi_2EB87C(void *data, long len, uchar pri, long user, uchar flags); /* 0x2EB87C */
char snd_sample_play(long snd_ref, long len, void *data, snd_digi_parms *dprm); /* 0x2EB9F0 */
long snd_start_sample(char h);                              /* 0x2EBBC4 */
long digi_2EBBE0(char h);                                /* 0x2EBBE0 */
long snd_stop_sample(char h);                               /* 0x2EBC00 */
long snd_resume_sample(char h);                             /* 0x2EBC20 */
long digi_2EBC40(long pri);                              /* 0x2EBC40 */
void snd_foreach_playing(void *fn, long arg);               /* 0x2EBCD8 */
void snd_default_parms(snd_digi_parms *p);                  /* 0x2EBD34 */
void snd_copy_parms(snd_digi_parms *d, snd_digi_parms *s);  /* 0x2EBD60 */
long digi_2EBDA0(long h_, uchar parm, long val);          /* 0x2EBDA0 */
long snd_get_parm(char h, uchar parm);                      /* 0x2EBDF0 */
long digi_2EBE24(char h, uchar parm, long val);        /* 0x2EBE24 */
long digi_2EBE44(char h, uchar parm);                  /* 0x2EBE44 */
long snd_ail_set_pri(void *s, long pri);                     /* 0x2EBE6C cdecl */
long snd_ail_get_pri(void *s);                              /* 0x2EBE98 cdecl */
long snd_ail_set_user(void *s, long user);                  /* 0x2EBEC4 cdecl */
long snd_ail_get_user(void *s);                             /* 0x2EBEF0 cdecl */
uchar snd_sample_flags(char h);                             /* 0x2EBF18 */
void snd_fade_tick(void);                                   /* 0x2EBF34 cdecl (AIL timer) */
long snd_fade(char h, long target, long steps, long flags); /* 0x2EC0E4 */
long snd_ail_start(char h);                                 /* 0x2EC1A0 */
long snd_ail_end(char h);                                   /* 0x2EC1E0 */
long snd_ail_stop(char h);                                  /* 0x2EC1FC */
long snd_ail_resume(char h);                                /* 0x2EC218 */
long snd_ail_eos(char h);                                   /* 0x2EC234 */

/* ------------------------------------------------------------------ fnsplit.c */
typedef struct {
    char drive;         /* +0  0 = none */
    char dir[0x72];     /* +1 */
    char name[9];       /* +73 */
    char ext[4];        /* +7C */
} FnParts;
LG_STATIC_ASSERT(sizeof(FnParts) == 0x80, FnParts_size);
int fn_split(FnParts *parts, char *path);                   /* 0x2F5A30 0, -1 if truncated / no ext */
char *fn_merge(char *dst, FnParts *parts);                  /* 0x2F5B94 */
char *strncpy0(char *dst, char *src, size_t n);             /* 0x2F5C24 (unreferenced) */

/* ------------------------------------------------------------------ sndcfg.c */
/* A sound card description (0x110 bytes; one digital, one MIDI).  T2c (audiocfg.c, SndCard) and
 * G1a (sound.c) use the same record with their own field names; both kept (G2 merge). */
typedef struct {
    union { uchar num; signed char idx; };  /* +0  digi_num / midi_num (or _type); T2c: driver
                                               index 0..19, -1 none */
    union { char *name; char *drv; };       /* +1  -> name_buf, or NULL (T2c: driver file name) */
    union {
        char name_buf[0x100];               /* +5 */
        struct { uchar unk5; uchar pad6[0xFF]; };
    };
    union { uchar flags; uchar drvflags; }; /* +105 1 digi, 2 midi, 4 general midi */
    union {
        short io[5];                        /* +106 io, irq, dma16, dma8, extra */
        struct { short io_w[4]; short io10e; }; /* +10E (T2c) 1: load the .sbk sound bank */
    };
} SndCardCfg;
LG_STATIC_ASSERT(sizeof(SndCardCfg) == 0x110, SndCardCfg_size);

extern char *snd_datapath;                  /* 0x35FA38 (Datapath *; T2c's name, set by its AIL startup) */
extern char sndcfg_found_name[];            /* 0x44E018 */
extern short ail_io_parms[4];               /* 0x45F2FC current driver io (AIL data) */

/* map names kept: T2c's audio code calls these */
void sndcfg_30FF90(SndCardCfg *c, short *io);               /* 0x30FF90 get_io */
short *sndcfg_30FFC0(SndCardCfg *c, short *io);             /* 0x30FFC0 check_blaster */
uchar sndcfg_310100(short *io);                             /* 0x310100 io_matches */
char *sndcfg_310170(char *name);                            /* 0x310170 find_file */
ulong sndcfg_read_filter(char *var);                        /* 0x310258 */
long sndcfg_310260(char *fname, SndCardCfg *digi, SndCardCfg *midi); /* 0x310260 read audio.cfg */

/* Watcom CRT (kept original): __Init_Argv 0x30FE10, _SplitParms 0x30FEB6 */

#pragma pack(pop)
#endif
