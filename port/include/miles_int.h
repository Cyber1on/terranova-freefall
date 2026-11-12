/* miles_int.h -- the parts of the Miles Sound System stand-in (port/audio/miles.c, port/audio/xmidi.c,
 * port/audio/fmsynth.c, port/audio/gmout.c, port/audio/sfsynth.c) that talk to each other. */
#ifndef TN_MILES_INT_H
#define TN_MILES_INT_H

#include <stdint.h>

#define MIX_RATE 44100                  /* output rate (Hz), stereo 16-bit */
#define XMI_RATE 120                    /* XMIDI ticks per second (the Miles MIDI service rate) */

/* ---- FM synthesis: the Miles OPL2 / OPL3 driver on an emulated OPL3 (fmsynth.c) */
int fm_open(const char *timbre_file, int opl3);  /* 0 = ready */
void fm_close(void);
void fm_msg(int status, int d1, int d2);         /* one MIDI channel message */
void fm_render(int32_t *mix, int frames);        /* adds frames (stereo) to mix */

/* ---- General MIDI: the system's MIDI synthesizer (gmout.c) */
int gm_open(void);                               /* 0 = ready */
void gm_close(void);
void gm_msg(int status, int d1, int d2);

/* ---- SoundFont music: FluidSynth, the game's AWE32 bank (sbk_path) over a SoundFont (sfsynth.c) */
int sf_open(const char *sbk_path);               /* 0 = ready */
void sf_close(void);
void sf_msg(int status, int d1, int d2);
void sf_render(int32_t *mix, int frames);        /* adds frames (stereo) to mix */

/* ---- the XMIDI sequencer (xmidi.c) */
enum { MUSIC_NONE, MUSIC_FM, MUSIC_GM, MUSIC_SF };
extern int xmi_output;                           /* MUSIC_* */
void xmi_tick(void);                             /* one 1/120 s step of every sequence */
void xmi_all_off(void);
void xmi_restore(void);                          /* a new output: channel state, held notes */

void xmi_render_packs(int32_t *mix, int frames);  /* music pack files of the playing sequences */

/* ---- music packs (musicpack.c) */
typedef struct MpTrack MpTrack;
MpTrack *mp_open(int entry, int num, int loop);  /* NULL: no pack or no file for the entry */
void mp_seek_ms(MpTrack *t, unsigned long ms);
void mp_free(MpTrack *t);
void mp_rewind(MpTrack *t);
void mp_render(MpTrack *t, int32_t *mix, int frames, int gain);  /* gain of 0x4000 */

/* ---- miles.c */
void *miles_find_file(const char *dos_name, char *out, int size);  /* host path or NULL */

#endif
