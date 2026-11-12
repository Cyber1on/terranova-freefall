/* g1_a.h -- declarations for worker g1a (batch G1): sound.c (M002, src/game/sound/sound.c),
 * the LG random streams (M126, src/game/main/rnd.c), the game time module (0x2DE880..,
 * src/game/main/gametime.c) and the key/mouse input snapshot (0x2F2B30, src/game/replay/keybits.c).
 * Names are ours unless they come from the System Shock source (rnd.c) or the map.
 * Globals are bound to their original addresses through symbols-g1-a.csv.
 */
#ifndef TN_G1_A_H
#define TN_G1_A_H
#include "lg.h"
#include "fix.h"
#include "res.h"
#include "support.h"
#include "game.h"
#include "t2_c.h"      /* T2 sound layers: snd_ail_*, snd_read_audio_cfg, SndCard (owner t2c) */
#pragma pack(push, 1)

/* ------------------------------------------------------------------ rnd.c (LG random streams) */
typedef struct RndStream_ {
    ulong curr;                                     /* +0 */
    ulong (WATCOMCALL *f_Next)(struct RndStream_ *prs);           /* +4 */
    void (WATCOMCALL *f_Seed)(struct RndStream_ *prs, ulong seed); /* +8 */
} RndStream;
LG_STATIC_ASSERT(sizeof(RndStream) == 12, RndStream_size);
extern ushort *rnd_gauss_table;                     /* 0x4468D0 RndGauss16Fast table (8192 entries) */
long RndRange(RndStream *prs, long low, long high); /* 0x2DDD70 */
fix RndRangeFix(RndStream *prs, fix low, fix high); /* 0x2DDD88 */
ulong RndLc16(RndStream *prs);                      /* 0x2DDDA4 */
void RndLc16Seed(RndStream *prs, ulong seed);       /* 0x2DDDC4 (also RndGauss16Seed) */
ulong RndGauss16(RndStream *prs);                   /* 0x2DDDD0 */
ulong RndGauss16Fast(RndStream *prs);               /* 0x2DDE8C */
void RndGauss16FastSeed(RndStream *prs, ulong seed);/* 0x2DDEC0 */

/* ------------------------------------------------------------------ gametime.c */
void GameTimeClearPeriodic(void);                   /* 0x2DE880 (jmp periodic_clear) */
/* GameTimeFrame 0x2DE888: game.h */
void GameTimeReset(void);                           /* 0x2DEAE0 (unreferenced) */
void GameTimeSetRate(short rate);                   /* 0x2DEB14 */
void GameTimeResume(void);                          /* 0x2DEB4C */
void GameTimePause(void);                           /* 0x2DEB80 */
void GameTimeResume2(void);                         /* 0x2DEBA0 (copy of GameTimeResume) */
void GameTimeSetMaxStep(short dt);                  /* 0x2DEBD4 */
void periodic_clear(void);                          /* 0x2EEEC0 (T2a) */
void periodic_run(ulong now);                       /* 0x2EEF84 (T2a) */
extern ulong periodic_next_due;                     /* 0x44B9C8 (T2a) */
bool CfgLookup(char *var, char *buf, int size);     /* 0x2E2CB4 (T1b) */
/* replay recorder / player (M075): g1_c.h (owner g1c; gametime.c and keybits.c include it) */

/* ------------------------------------------------------------------ keybits.c */
void KeyBitmapUpdate(void);                         /* 0x2F2B30 */
void MouseStateUpdate(void);                        /* 0x2F2BD4 */
extern void *tn_replay_state_ptr;                   /* 0x44C5D0 */
extern void *tnm_state_ptr;                         /* 0x4470BC */
extern LGPoint tnm_pos;                             /* 0x4470C0 (+ 2 more bytes copied) */
extern uchar tnm_state_copy[6];                     /* 0x4470C6 */
void MouseMoveTo(LGPoint p);                        /* 0x2E4A68 (sup-c) */

/* ------------------------------------------------------------------ sound.c */
#define SND_MAX_CHANS 16
#define SND_FAR 0x29A0000           /* SndChan.dist: out of hearing range */

/* digi_2EBDA0 / snd_get_parm parameters (as t2_b.h) */
#ifndef SND_PARM_PAN
#define SND_PARM_PAN 0
#define SND_PARM_VOL 1
#define SND_PARM_RATE 2
#define SND_PARM_LOOPS 3
#define SND_PARM_PRI 5
#endif

typedef void (WATCOMCALL *SndChanCb)(long id, long arg);   /* SndChan callback */

/* channel types (SndPlay's second argument) */
#define SND_T_PLAIN 0               /* volume (or the sound's own volume when < 0) and pan given */
#define SND_T_POS 1                 /* at a fixed position (a1, a2, a3 = x, y, z) */
#define SND_T_ENTITY 2              /* follows an entity (a1); stopped when the entity dies */
#define SND_T_SPEECH 4              /* speech: speech volume, ducks music and effects */
#define SND_T_ENTITY2 5             /* follows an entity, starts silent */
#define SND_T_LOW 6                 /* plain, first to go when channels run out */

typedef struct {
    signed char hnd;                /* +00 digital layer sample handle, -1 = free */
    long type;                      /* +01 SND_T_*, -1 when free */
    ulong start;                    /* +05 tn_ticks when started */
    ulong last;                     /* +09 tn_ticks of the last doppler update */
    fix dist;                       /* +0D distance to the player, SND_FAR = too far */
    long rate;                      /* +11 playback rate at the start (doppler base) */
    uchar vol;                      /* +15 0..127 before the global volume */
    uchar pan;                      /* +16 */
    uchar loops;                    /* +17 */
    ushort id;                      /* +18 sound resource */
    union {
        short ent;                  /* +1A SND_T_ENTITY(2): entity id */
        fix pos[3];                 /* +1A SND_T_POS: x, y, z */
    } u;
    long ncb;                       /* +26 callbacks run when the sound ends */
    struct {
        SndChanCb fn;               /* Watcom (id, arg) */
        long arg;
    } cb[4];                        /* +2A */
    long *handle_ptr;               /* +4A set to -1 when the sound ends */
    uchar no_doppler;               /* +4E */
} SndChan;
LG_STATIC_ASSERT(sizeof(SndChan) == 0x4F, SndChan_size);
LG_STATIC_ASSERT(offsetof(SndChan, handle_ptr) == 0x4A, SndChan_handle_ptr);

/* scheduled sound (SndSchedule): SndPlay's arguments and a time */
typedef struct SndEvent {
    ulong time;                     /* +00 game_time */
    ushort id;                      /* +04 */
    long type;                      /* +06 */
    long loops;                     /* +0A */
    uchar pri;                      /* +0E */
    long a1, a2, a3;                /* +0F */
    struct SndEvent *next;          /* +1B sorted by time */
} SndEvent;
LG_STATIC_ASSERT(sizeof(SndEvent) == 0x1F, SndEvent_size);

/* looping sound slot (snd_loop_ctl) */
typedef struct {
    ushort id;                      /* +0 */
    uchar flags;                    /* +2 low nibble 3: don't stop; high nibble: don't stop */
    long chan;                      /* +3 channel, -1 none */
} SndLoop;
LG_STATIC_ASSERT(sizeof(SndLoop) == 7, SndLoop_size);

/* sound card description filled by snd_read_audio_cfg: SndCard (t2_c.h; g1a had a partial
 * copy SndCardInfo with idx +0 and drvflags +0x105, removed at the G2 merge) */

/* snd_flags bits */
#define SNDF_MUSIC 0x0F             /* number of music sequences (8 when MIDI is installed) */
#define SNDF_GM 0x10                /* general MIDI driver */
#define SNDF_PAIRS 0x20             /* stereo sample pairs (qsound) */
#define SNDF_PAIRFLAG 0x40          /* passed to snd_pair_play as 0x100 */
#define SNDF_INIT 0x80

extern uchar snd_busy;                  /* 0x35982C SndPlay refuses to play while set */
extern uchar snd_channels;              /* 0x35982D digital channels in use, 0 = no sound */
extern uchar snd_flags;                 /* 0x35982E SNDF_* */
extern uchar snd_sfx_vol;               /* 0x35982F 0..127 */
extern uchar snd_speech_vol;            /* 0x359830 */
extern uchar snd_vol_359831;            /* 0x359831 set to 100 by snd_config_load, else unused */
extern uchar snd_music_vol;             /* 0x359832 */
extern RndStream snd_rnd;               /* 0x359834 */
extern RndStream snd_rnd2;              /* 0x359840 */
extern long snd_finishing;              /* 0x35984C snd_finish_pending running */
extern fix snd_sound_speed;             /* 0x359850 doppler: distance per tick */
extern SndEvent *snd_sched_cur;         /* 0x359854 next event (taken off the list) */
extern short snd_hit_ids[0x22];         /* 0x359858 snd_play_hit sounds */
extern SndLoop snd_loops[4];            /* 0x35989C */
extern long snd_finish_count;           /* 0x3598BC */
extern SndEvent *snd_sched_list;        /* 0x3597C8 */
extern uchar snd_dist_vol[32];          /* 0x3597CC volume by distance (whole units) */
extern uchar snd_pan_vol[64];           /* 0x3597EC volume by min(pan, 127 - pan) */
extern SndEvent *snd_sched_free[8];     /* 0x370610 free stack (see snd_sched_run) */
extern SndEvent snd_sched_pool[8];      /* 0x370630 */
extern long snd_duck_music;             /* 0x370728 music volume while speech plays */
extern long snd_music_cur;              /* 0x37072C */
extern long snd_duck_sfx;               /* 0x370730 effects volume while speech plays */
extern long snd_sfx_base;               /* 0x370734 */
extern SndChan snd_chans[SND_MAX_CHANS];/* 0x370738 */
extern long snd_stereo_flip;            /* 0x370C28 */
extern long snd_sched_free_count;       /* 0x370C2C */
extern long snd_finish_queue[31];       /* 0x370C30 channels whose sample ended (sndlock_2491C0) */
extern uchar snd_stereo_reverse;        /* 0x35FA58 (T2b) */
extern void *music_end_cb;              /* 0x35FA28 (T2a) */
extern void *snd_finish_parms;          /* 0x35FA4C (T2b) */

void snd_config_load(void);                                         /* 0x246D24 */
void snd_config_save(void);                                         /* 0x246EA0 */
void SndInit(bool read_cfg);                                        /* 0x246F24 */
void SndShutdown(void);                                             /* 0x247134 */
void snd_chan_spatialize(SndChan *c);                               /* 0x2471B0 */
long snd_chan_add_callback(long chan, void *fn, long arg);          /* 0x24733C */
void snd_chan_clear_callbacks(long chan);                           /* 0x24739C */
void snd_finish_pending(void);                                      /* 0x2473BC */
void snd_update_channels(void);                                     /* 0x247574 */
bool snd_kill_low_priority(void);                                   /* 0x24765C */
void snd_kill_entity_cb(short ent);                                 /* 0x2476F8 */
void snd_chan_stop(long chan);                                      /* 0x247744 */
void snd_stop_entity(short ent);                                    /* 0x247780 */
void snd_stop_looping(void);                                        /* 0x2477D0 */
signed char snd_chan_start_sample(ushort id, long chan, uchar pri); /* 0x247848 */
long snd_chan_alloc(void);                                          /* 0x247978 */
void snd_chan_update_dist(SndChan *c);                              /* 0x247A08 */
long SndPlay(ushort id, long type, long pri, long loops, long a1, long a2, long a3); /* 0x247B60 */
void snd_chan_set_handle_ptr(long chan, long *p);                   /* 0x247E30 */
long snd_chan_set_vol(long chan, uchar vol);                        /* 0x247E58 */
void snd_chan_set_vol_pan(long chan, long vol, long pan);           /* 0x247EE8 */
long snd_chan_get_vol(long chan);                                   /* 0x247F50 */
uchar snd_res_volume(ushort id);                                    /* 0x247F90 */
void snd_chan_fade(long chan, long vol, long steps, uchar end);     /* 0x247FD0 */
void snd_chan_doppler(SndChan *c);                                  /* 0x248018 */
void snd_chans_init(void);                                          /* 0x2480A4 */
void snd_dig_setup(SndCard *c);                                 /* 0x248124 */
void snd_midi_setup(SndCard *c);                                /* 0x24817C */
bool snd_audible(fix x, fix y, fix z, ushort id);                   /* 0x2481E0 */
void snd_spatial_vol(ushort id, fix x, fix y, fix z, long *vol, long *pan); /* 0x24831C */
long snd_play_2d(ushort id, long loops, long pri);                  /* 0x248530 */
long snd_play_2d_once(ushort id);                                   /* 0x248548 */
void snd_chan_fade_io(long chan, long in, long steps, uchar end);   /* 0x24856C */
void snd_chan_crossfade(long out, long in, long steps, uchar end);  /* 0x2485F4 */
long snd_null_2486A4(void);                                         /* 0x2486A4 */
void SndUpdateHook(void);                                           /* 0x2486A8 frame hook slot 1 */
ushort snd_chan_get_id(long chan);                                  /* 0x2486D0 */
long snd_play_if_idle(ushort id, long vol);                         /* 0x2486FC */
void snd_set_speech_volume(long pct);                               /* 0x24872C */
void snd_preview_sfx_volume(long pct);                              /* 0x248760 */
void snd_set_sfx_volume(long pct);                                  /* 0x2487F4 */
void snd_set_sfx_volume_test(long pct);                             /* 0x24883C */
void snd_set_qsound(long on);                                       /* 0x2488C0 */
void snd_stereo_test_cb(void);                                      /* 0x2488F8 */
void snd_stereo_test(long mode);                                    /* 0x24892C */
void snd_set_stereo_reverse(uchar v);                               /* 0x2489B0 */
void snd_set_channel_level(long level);                             /* 0x2489C4 */
void snd_set_music_volume(long pct);                                /* 0x248A5C */
long snd_chan_active(long chan);                                    /* 0x248AD0 */
void snd_sched_init(void);                                          /* 0x248B04 */
SndEvent *snd_sched_pop(void);                                      /* 0x248B40 */
void snd_sched_run(void);                                           /* 0x248B5C */
ushort *snd_sched_alloc(ulong delay);                               /* 0x248C10 */
bool SndSchedule(ulong delay, ushort id, long type, uchar pri, long loops, long a1, long a2, long a3); /* 0x248CA0 */
long snd_count_playing(ushort id);                                  /* 0x248CE4 */
void snd_speech_duck(bool start);                                   /* 0x248D1C */
void snd_set_type_volume(long type, long vol);                      /* 0x248E00 */
void snd_play_step(long kind, long n, fix speed);                   /* 0x248EBC */
void snd_null_248FA8(void);                                         /* 0x248FA8 */
void snd_play_hit(long idx, long unused, uchar side);               /* 0x248FBC */
void snd_loop_end_cb(ushort id, SndLoop *l);                        /* 0x249080 */
void snd_loop_ctl(long idx, long on);                               /* 0x249094 */
void snd_radio_mode(bool on);                                       /* 0x249120 */

/* T2 sound layers (decompiled, owners t2_b.h / t2_c.h; the rest of what sound.c calls) */
void snd_pair_lock(void);                                           /* 0x2EFF50 */
long snd_pair_play(void *d0, void *d1, long len, long pri, long user, ulong flags); /* 0x2EFF7C */
void snd_pair_unlock(void);                                         /* 0x2F0570 */
void *snd_set_finish(void *fn, long data);                          /* 0x2EB62C */
void digi_2EB67C(void);                                             /* 0x2EB67C */
char digi_2EB87C(void *data, long len, uchar pri, long user, uchar flags); /* 0x2EB87C */
long snd_start_sample(char h);                                      /* 0x2EBBC4 */
long digi_2EBBE0(char h);                                           /* 0x2EBBE0 end sample */
long digi_2EBC40(long pri);                                         /* 0x2EBC40 */
long digi_2EBDA0(long h, uchar parm, long val);                     /* 0x2EBDA0 set parameter */
long snd_get_parm(char h, uchar parm);                              /* 0x2EBDF0 */
long snd_fade(char h, long target, long steps, long flags);         /* 0x2EC0E4 */
bool periodic_add(void *fn, ulong interval, uchar repeat);          /* 0x2EEED0 */
void periodic_remove(void *fn);                                     /* 0x2EEF20 */
void music_check_all(void);                                         /* 0x2EE580 */
bool config_get_single_value(char *var, int type, long *val);       /* 0x2E2C88 */
int config_set_single_value(char *var, int type, long val);         /* 0x2E2A80 */
int DatapathFind(Datapath *pdp, char *fname, char *buf);            /* 0x2EF478 */
char *strrchr(const char *s, int c);                                /* 0x2EF735 */
int dpmi_lock_data(void *p, long size);                             /* 0x2EDC00 cdecl */
int dpmi_unlock_data(void *p, long size);                           /* 0x2EDC18 cdecl (name ours) */
/* other game modules (original) */
/* music.c (M001, G1c): g1_c.h (sound.c includes it) */
void sndlock_249170(void);                                          /* M003 (G1b) */
void sndlock_24919C(long id);                                       /* music end callback */
void sndlock_2491C0(long a, long chan);                             /* sample finish callback */
void sndlock_249230(void);
void noisy_2492C4(void);                                            /* M004 noisy.c (G1b) */
void noisy_249320(void);
void noisy_249488(void);
bool noisy_2495A0(ushort id);
void weapons_29079C(void);                                          /* M063 (G2b) */
void weapons_290858(void);
uchar damage_27DF14(long ent, long a);                              /* M045 (G3c) */
void killobj_289554(long ent, void *fn, long arg);                  /* M056 (G2c) */

#pragma pack(pop)
#endif
