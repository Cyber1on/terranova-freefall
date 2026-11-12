/* Batch G1, worker g1b: sound callback queues (M003, sndlock.c), noisy.c ambient sounds (M004),
 * ff config file + camera zoom init (M087, ffconfig.c), ff memory system (M099, ffmem.c),
 * radio speech (M110, radio.c).  Files: src/game/sound/{sndlock,noisy,radio}.c,
 * src/game/input/ffconfig.c, src/game/main/ffmem.c.
 * Names are ours unless the binary carries them (FfConfigLookupName, ffFree, noisy.c); the
 * functions other G1 workers call keep their map names (sndlock_*, noisy_2492C4/249320/249488/
 * 2495A0, ffmem_2C4F40/2C4FE4, config_2AC9C0/2ACABC/2ACB78/2ACC68; meaning in the comments).
 * Sound names and types (SndChan, snd_chans, snd_rnd, snd_chan_* ...) come from g1_a.h.
 * Globals of other modules are declared here with neutral names (address in the name).
 */
#ifndef TN_G1_B_H
#define TN_G1_B_H
#include "g1_a.h"
#include "resmem.h"

#pragma pack(push, 1)

typedef signed char schar;

/* ------------------------------------------------------------------ callees in other batches */

/* TN support (T1/T2) */
int music_set_volume(char i, long vol, long ms);   /* 0x2EE91C */
int dpmi_lock_region(void *start, void *end);      /* 0x2EDB28 cdecl */
int dpmi_unlock_region(void *start, void *end);    /* 0x2EDB94 cdecl */
typedef ulong (WATCOMCALL *ConfigReadFilter)(char *var);
typedef bool (WATCOMCALL *ConfigSwitchFilter)(int c);
int config_init(void);                                          /* 0x2E2084 */
int config_read_file(char *fname, ConfigReadFilter filter);     /* 0x2E22FC */
void config_set_cmdline_seps(char *seps);                       /* 0x2E2D0C */
int config_parse_cmdline(int argc, char **argv, ConfigSwitchFilter filter); /* 0x2E2D14 */

/* ------------------------------------------------------------------ shared game globals */

extern RndStream rnd_stream_35B48C;    /* 0x35B48C RndRange stream (radio, units) */
extern long game_359708;               /* 0x359708 mission number / kind (5, 0x27 tested by radio.c;
                                          0x2E, 0x2F use music set 4); g1c name, one name since G2 merge */
extern long game_35B77C;               /* 0x35B77C nonzero: no radio */
extern uchar game_35BF48;              /* 0x35BF48 nonzero: only message 0x47 */
extern uchar *game_389DF8;             /* 0x389DF8 record whose +0x20 gets bit 0x20 */
extern uchar game_low_mem;             /* 0x35971E low-memory mode (fewer pages) */
extern uchar game_35F3B8;              /* 0x35F3B8 big buffer lent whole / no OOM error */
extern uchar *game_big_buf;            /* 0x39A3E0 0xEF420-byte buffer (3 x 0x4B000 slots) */
extern uchar game_big_free;            /* 0x35972F big buffer may be lent */
extern void *game_438C20;              /* 0x438C20 record whose +0x2E word is set to 1 */
extern long game_442432;               /* 0x442432 (word at +2 = zoom scale, 16.16) */
extern ushort cam_zoom_mask[2];        /* 0x35C95C, 0x35C95E bit per zoom level */
extern long cam_view_tab[];            /* 0x35C94C */
extern uchar ff_options[0xB4];         /* 0x428B30 game options (DETAIL +0x52, VS_DETAIL +0x53,
                                          music +0x6F, digital +0x70, record +0x91, play +0x92,
                                          play file +0x95) */
extern char *ffcfg_detail_names[];     /* 0x35C960 */
extern char *ffcfg_sound_names[];      /* 0x35C9B0 */
extern char *ffcfg_helmet_names[];     /* 0x35C99C */
extern char *ffcfg_asf_names[];        /* 0x35C9F8 */

/* "speaker" records (0x32 bytes) at 0x391E50, count 0x391E4C; radio reads three fields */
typedef struct {
    short active;        /* +0x00 */
    uchar g_02[4];
    schar voice;         /* +0x06 voice type (0x10.. use the shared break files) */
    schar chan;          /* +0x07 radio channel 1..3 */
    uchar g_08[0x28];
    uchar f30;           /* +0x30 */
    uchar g_31;
} GameSpeaker;
LG_STATIC_ASSERT(sizeof(GameSpeaker) == 0x32, GameSpeaker_size);
extern long game_391E4C;               /* 0x391E4C number of speaker records */
extern GameSpeaker game_391E50[];      /* 0x391E50 */

/* ------------------------------------------------------------------ M003 sndq (sndq.c) */

/* Music queue state shared by sndq.c (the callbacks write it) and music.c (G1c, reads it).
 * g1b had called these sndq_*; one name per address since the G2 merge (g1c's names). */
extern long music_unlock_ids[];        /* 0x370CAC entries whose resources music_24648C unlocks,
                                          1-based ([music_unlock_count] last); 31 used of 32 */
extern long music_unlock_count;        /* 0x3598B8 */
extern ushort music_cur;               /* 0x3598C0 entry playing (sndlock_2491FC stores w) */
extern char music_cur_seq;             /* 0x3598C2 its sequence index (-1 none) */
extern long music_rnd;                 /* 0x3598C4 random value for the prefix callback */
extern long music_fade_vol;            /* 0x3598C8 pending volume change: volume */
extern long music_fade_time;           /* 0x3598CC ... and time (0 = none) */

void sndlock_249170(void);                             /* 0x249170 lock code and queues */
void sndlock_24919C(long v);                           /* 0x24919C sequence ended: queue v */
void sndlock_2491C0(long a, long chan);                /* 0x2491C0 sample finished: queue chan */
long sndlock_2491E0(long a, long b, long c);           /* 0x2491E0 music prefix callback */
long sndlock_2491FC(long seq, long w);                 /* 0x2491FC music play callback */
void sndlock_249230(void);                             /* 0x249230 unlock */

/* ------------------------------------------------------------------ M004 noisy.c */

typedef struct {
    short obj;           /* +0 entity */
    ushort snd;          /* +2 sound id, 0 = free */
    schar chan;          /* +4 sound channel playing it, -1 none */
    uchar audible;       /* +5 */
    uchar fade;          /* +6 fade-in steps left (0x20 after a stop) */
    uchar vol;           /* +7 */
    uchar pan;           /* +8 */
    ulong time;          /* +9 */
} NoisyRec;
LG_STATIC_ASSERT(sizeof(NoisyRec) == 13, NoisyRec_size);
#define NOISY_MAX 64
extern long noisy_count;               /* 0x3598D0 */
extern long noisy_order[NOISY_MAX];    /* 0x370D30 indexes sorted by sound id */
extern NoisyRec noisy_tab[NOISY_MAX];  /* 0x370E30 */

void noisy_kill_cb(long obj);                          /* 0x249260 */
void noisy_2492C4(void);                               /* 0x2492C4 init */
void noisy_249320(void);                               /* 0x249320 reset (remove all) */
void noisy_remove(long i);                             /* 0x249340 */
long noisy_add(long obj, long snd);                    /* 0x249388 */
ulong noisy_snd_done_cb(long h, NoisyRec *r);          /* 0x249474 */
void noisy_249488(void);                               /* 0x249488 per-frame update */
bool noisy_2495A0(ushort snd);                         /* 0x2495A0 stop one channel of the loudest */
ushort noisy_loudest(void);                            /* 0x2495F8 */
void noisy_sort(void);                                 /* 0x249680 */
void noisy_sort_vol(void);                             /* 0x2496FC */
void noisy_calc_vol(void);                             /* 0x249784 */
void noisy_select(void);                               /* 0x249874 */
void noisy_oneshot(NoisyRec *r);                       /* 0x249928 */

/* ------------------------------------------------------------------ M087 ffconfig.c */

void CameraInitZoom(uchar *cam, long zoom, long view);         /* 0x2AC7A0 */
bool ffcfg_switch_cb(int c);                                   /* 0x2AC960 */
ulong ffcfg_read_cb(char *var);                                /* 0x2AC964 */
void FfConfigInit(char *fname, int argc, char **argv);         /* 0x2AC96C */
void config_2AC9C0(void);                                      /* 0x2AC9C0 option defaults */
void config_2ACABC(uchar *src, uchar *dst);                    /* 0x2ACABC convert old layout */
void config_2ACB78(uchar *src, uchar *dst);                    /* 0x2ACB78 convert middle layout */
void config_2ACC68(uchar *src, uchar *dst);                    /* 0x2ACC68 copy */
void FfConfigApply(void);                                      /* 0x2ACD70 */
int FfConfigLookupName(char *name, char **names);              /* 0x2ACF5C */
bool ffcfg_weapon_slot(uchar *out, long slot, char *name);     /* 0x2ACF94 */
bool ffcfg_suit_slot(long i, char *name);                      /* 0x2ACFF0 */
bool ffcfg_read_loadout(uchar *out, char *prefix);             /* 0x2AD070 */

/* ------------------------------------------------------------------ M099 ffmem.c */

typedef struct {
    void *ptr;           /* +0 */
    ulong min;           /* +4 smallest size served */
    ulong max;           /* +8 block size */
    uchar used;          /* +C */
} FfBlock;
LG_STATIC_ASSERT(sizeof(FfBlock) == 13, FfBlock_size);
typedef struct { ulong min, size; long count; } FfPoolSpec;
extern uchar ffmem_stack_on;           /* 0x35D018 */
extern long ffmem_stack_peak;          /* 0x35D01C */
extern FfPoolSpec ffmem_spec[13];      /* 0x35D020 */
extern FfBlock ffmem_blocks[256];      /* 0x43D580 */
extern long ffmem_big_used[3];         /* 0x43E280 */
extern uchar *ffmem_pool_start;        /* 0x43E28C */
extern uchar *ffmem_pool_end;          /* 0x43E290 */
extern long ffmem_nblocks;             /* 0x43E294 */
extern long ffmem_npages;              /* 0x43E298 */
extern uchar *ffmem_pages_end;         /* 0x43E29C */
extern uchar *ffmem_pages;             /* 0x43E2A0 */
extern MemStack ffmem_stack;           /* 0x43E2A4 */
extern void *ffmem_stack_buf;          /* 0x43E2B0 */
extern uchar ffmem_page_map[];         /* 0x43E2B4 run length at a run's first page, 0xFF
                                          for the rest of the run, 0 free; 0xFF after the end */

void ffmem_stack_init(void);                           /* 0x2C4EE0 */
long ffmem_stack_peak_update(void);                    /* 0x2C4F14 */
void *ffmem_2C4F40(size_t size);                       /* 0x2C4F40 stack alloc */
void *ffmem_stack_realloc(void *p, size_t size);       /* 0x2C4F90 */
bool ffmem_2C4FE4(void *p);                            /* 0x2C4FE4 stack free */
void ffmem_pages_init(void);                           /* 0x2C5038 */
void *ffmem_page_alloc(size_t size);                   /* 0x2C5084 */
void *ffmem_page_alloc_res(size_t size);               /* 0x2C5130 */
long ffmem_page_free(void *p);                         /* 0x2C5150 */
void ffmem_out_of_memory(size_t size);                 /* 0x2C5188 */
void *ffMalloc(size_t size);                           /* 0x2C51A4 */
void ffFree(void *p);                                  /* 0x2C5300 */
void *ffRealloc(void *p, size_t size);                 /* 0x2C5414 */
void ffmem_init(void);                                 /* 0x2C5468 */
void *ffmem_page_purge(size_t size);                   /* 0x2C559C */

/* ------------------------------------------------------------------ M110 radio.c */

typedef struct {
    ushort snd;          /* +0 queued speech line (sound id), 0 = empty */
    schar prio;          /* +2 */
    ulong expire;        /* +3 */
    short line;          /* +7 line index (usage counters) */
    uchar wait;          /* +9 only when nobody important talks */
} RadioQ;
typedef struct {
    schar voice;         /* +0x00 voice type of the speaker on this channel */
    long handle;         /* +0x01 sound channel, -1 idle */
    schar prio;          /* +0x05 */
    ulong start;         /* +0x06 game time the current line started */
    RadioQ q[3];         /* +0x0A */
} RadioChan;
LG_STATIC_ASSERT(sizeof(RadioChan) == 0x28, RadioChan_size);
typedef struct { short id; ulong start; ulong expire; } RadioMsgQ;   /* 10 bytes */
LG_STATIC_ASSERT(sizeof(RadioMsgQ) == 10, RadioMsgQ_size);
#define RADIO_LINES 0x109
extern int radio_files[4];             /* 0x442910 resbrk files */
extern RadioChan radio_chans[4];       /* 0x442920 */
extern uchar radio_line_used[4][RADIO_LINES];  /* 0x4429C0 per channel */
extern uchar radio_voice_used[][RADIO_LINES];  /* 0x43E698 per voice */
extern uchar radio_talking[4];         /* 0x442DE4 per speaker */
extern RadioMsgQ radio_ackq[6];        /* 0x442DE8 (start = time queued, expire unused) */
extern RadioMsgQ radio_msgq[6];        /* 0x442E24 */
extern schar radio_nchans;             /* 0x442E60 */
extern schar radio_class_vol[];        /* 0x35DAB0 volume per voice class */
extern char radio_file_names[][14];    /* 0x35DAC3 "resbrk01.res" ... */
extern uchar radio_text_flags;         /* 0x35DA84 ack: 1 speech, 2 text */
extern uchar radio_msg_flags;          /* 0x35DA85 messages: 1 speech, 2 text */
extern long radio_msg_handle;          /* 0x35DBD0 */
extern long radio_msg_cur;             /* 0x35DBD4 */
extern long radio_msg_last;            /* 0x35DBD8 */
extern uchar radio_msg_47_said;        /* 0x35DBDC */
extern long radio_ack_handle;          /* 0x35DBE0 */
extern short radio_ack_cur;            /* 0x35DBE4 */

bool radio_chance(long pct);                           /* 0x2CDC10 */
ulong radio_voice_class(ulong v);                      /* 0x2CDC34 */
ulong radio_line_id(ulong v, long base, long n);       /* 0x2CDC4C */
ushort radio_line_span(long a, long b);                /* 0x2CDC78 */
void radio_files_reset(void);                          /* 0x2CDCBC */
void radio_stop_voice(long v);                         /* 0x2CDCD8 */
void radio_init(void);                                 /* 0x2CDD20 */
void radio_close(void);                                /* 0x2CDE9C */
schar radio_pick_line(long ch, long base, long n, long mode); /* 0x2CDECC */
long radio_snd_done_cb(long h, long ch);               /* 0x2CDFD8 */
uchar radio_busy_prio(void);                           /* 0x2CE040 */
void radio_duck_below(long prio);                      /* 0x2CE098 */
void radio_stop_below(long prio);                      /* 0x2CE0FC */
ulong radio_last_start(long ch);                       /* 0x2CE144 */
bool radio_play(long ch, long snd, long prio, long line); /* 0x2CE188 */
void radio_update(void);                               /* 0x2CE32C */
void radio_say(long ch, long snd, long prio, long delay, long line, long wait, long unused); /* 0x2CE450 */
void RadioSpeak(long spk, long msg);                   /* 0x2CE620 */
void radio_flush(void);                                /* 0x2CE7B8 */
uchar radio_msg_group(long g, uchar *n);               /* 0x2CE7F8 */
long radio_msg_done_cb(void);                          /* 0x2CE860 */
void radio_msg_play(long msg);                         /* 0x2CE87C */
void radio_msg_update(void);                           /* 0x2CE974 */
void RadioMessage(long msg);                           /* 0x2CEA24 */
void radio_ack_done_cb(void);                          /* 0x2CEB0C */
void radio_ack_play(long id);                          /* 0x2CEB20 */
void radio_ack_update(void);                           /* 0x2CEBA4 */
void RadioAck(long id);                                /* 0x2CEC20 */
void RadioMessageHi(long msg);                         /* 0x2CECE4 */
void radio_stop_all(void);                             /* 0x2CED40 */

#pragma pack(pop)
#endif
