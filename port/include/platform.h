/* platform.h -- the SDL3 platform layer of the standalone build (Terra Nova: Freefall).
 *
 * Threads, as a stand-in for the DOS machine:
 *   - the main thread runs SDL: the window, events and the display (it shows the frame buffer
 *     the way a VGA card scans video memory, whatever the game is doing);
 *   - the game thread runs the decompiled game (game_main);
 *   - the timer thread calls the Miles timer callbacks (the PIT interrupt), and the audio
 *     callback mixes sound.
 * Keyboard and mouse events reach the game the way their interrupt handlers did.  Everything
 * that runs "at interrupt time" holds the interrupt lock, and the game's cli/sti take it too
 * (port_cli / port_sti), so the game sees the same atomicity as on DOS.
 */
#ifndef TN_PLATFORM_H
#define TN_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

/* ---- interrupts */
void port_cli(void);
void port_sti(void);
/* enter / leave "interrupt time" on the calling thread (nests with port_cli/port_sti) */
void port_irq_enter(void);
void port_irq_leave(void);
/* stop / restart the game thread, for an interrupt handler that must not run alongside it
 * (one that changes the game's globals, as the mouse cursor drawing does with the current
 * canvas): on DOS the program stood still while a handler ran.  Call with the interrupt
 * lock held, from another thread (no effect on the game thread itself). */
void port_game_pause(void);
void port_game_resume(void);
/* pushf / cli and popf: for library code that may be called with interrupts on or off */
int port_irq_save(void);
void port_irq_restore(int was);

/* ---- time */
uint64_t port_now_us(void);             /* microseconds since start */
void port_sleep_us(uint64_t us);
extern int port_frame_us;               /* shortest game frame in microseconds (0 = no limit) */
extern int port_highfps_fix;            /* projectile hit test fixed for high frame rates */
/* port_mouselook: the controls style, 0 Vanilla (the original), 1 Advanced (mouselook) */
extern int port_mouselook, port_mouselook_sens, port_mouselook_invert;
extern int port_auto_targcam;           /* left screen shows the target camera for a new target */
extern int port_map_cone;               /* maps shade the view cone (mapcone.c) */
extern int port_map_enemies;            /* goggles' screens hidden: the map's enemies (mapcone.c) */
extern int port_map_radar;              /* goggles' screens hidden: the map as a round radar (mapcone.c) */
extern int port_aim_box;                /* Auto-aim: the box's pixels each way, 0 Off (options.c) */
#define AIM_CLASSIC 20                  /* ... the game's (Classic controls only) */
#define AIM_SMALL 5                     /* ... the small one (Vanilla+, Freefall) */
int port_aim_box_now(void);             /* ... in force: 20 works as 5 with Advanced controls */
extern int port_aim_sign;               /* Auto-aim highlight: crosshair's middle red on target (options.c) */
void port_options_aim_legacy(int box, int highlight); /* an older tnfree.ini: Auto-aim lines (options.c) */
extern long port_map_cone_wide;         /* main view width / the game's, 16.16 (sharp3d.c) */
extern int port_fullscreen;             /* Fullscreen: the window fills the screen (platform.c) */
extern int port_smooth_fps;             /* 60 frames a second (options.c) */
void port_set_smooth_fps(int on);
extern int port_options_open;           /* the options screen is up (options.c) */
extern int port_options_welcome;        /* the first start: options.c asks for the preset (main.c) */
int port_options_preset(const char *name);  /* --preset: classic, vanilla+ or freefall; 0 unknown */
/* the game's own options the options screen shows (src/game/shell/options.c): sound volumes and
 * stereo; set on the main thread, put in force on the game's (port_frame_limit) */
long port_game_option(int i);
int port_game_option_allowed(int i);
void port_game_option_set(int i, long v);
void port_game_options_apply(void);
extern char port_exe_dir[1024];         /* tnfree.exe's folder (main.c); Previews (options.c) */
int port_renderer_switching(void);      /* the renderer is about to be made again (platform.c) */
extern int port_movies_open;            /* movies the game has open (smacker.c) */
extern uint32_t port_movie_off, port_movie_pitch;   /* the movie's place on the game's screen ... */
extern int port_movie_w, port_movie_h;  /* ... and size (0: none; smacker.c) */
extern int port_game_options_up;        /* the game's own options screen is up */
extern int port_game_dialog_up;         /* the game's yes / no box is up (options.c) */
void port_save_settings(void);          /* writes tnfree.ini (main.c) */
extern int port_lockstep;               /* frame comparison run (lockstep.c) */
void port_lockstep_frame(void);
void port_lockstep_key(int code, int down);    /* a key into the keyboard handler's buffer */
/* the benchmark (tnfree --bench, bench.c): it runs as a lockstep run */
extern int port_bench;
extern int port_help_seen;              /* help.c: the help screen was opened once */
extern char port_bench_path[1100];      /* its results file (main.c) */
extern char port_bench_report[2048];    /* the results, for the message box at the end */
void port_bench_init(long *mission, long *ticks);
void port_bench_frame(int in_mission);  /* every frame after the mission's start */
void port_bench_cut(void);              /* the mission ended before the course did */
const char *port_bench_label(void);     /* the scene now (the FPS meter), "" before the first */
extern int port_window_w, port_window_h;   /* the window's pixels at the last present */
void port_frame_limit(void);            /* called by GameTimeFrame once per frame */
void port_tick_sync(void);              /* the game tick = the real milliseconds (src/timer/timer.c) */

/* ---- video (the virtual VBE card's memory and DAC, read by the display) */
#define PORT_VRAM_KB 4096
typedef struct {
    uint8_t *vram;                      /* PORT_VRAM_KB of video memory (linear frame buffer) */
    volatile int w, h, pitch;           /* current mode (0 = no graphics mode yet) */
    volatile uint32_t start;            /* display start offset in vram */
    uint8_t dac[256 * 3];               /* 6-bit DAC values (0..63) unless dac_bits == 8 */
    volatile int dac_bits;
    volatile unsigned pal_serial;       /* bumped on every DAC change */
} port_video_state;
extern port_video_state port_video;
/* the retrace period of the current mode (us) and when the current frame started */
int port_in_vretrace(void);
int port_in_hretrace(void);
void port_video_set_mode(int w, int h, int pitch);

/* ---- input (main thread -> game "interrupts") */
void port_input_init(void);
void port_kb_scancode(uint8_t sc);       /* one byte from the keyboard controller */
void port_mouse_event(int x, int y, int buttons, int moved, int changed);  /* game pixels */

/* ---- quitting */
extern volatile int port_quit_requested;  /* window closed: ask the game to stop */
void port_request_quit(void);
void port_game_ending(void);              /* port_exit, before the game's exit functions (P113) */

/* ---- game folder and files (files.c) */
extern char port_game_dir[1024];          /* the TNOVA folder (with __FF.EXE) */
/* findgame.c: an installed game (Steam, GOG) found by itself: 1 and its TNOVA folder in out,
 * *where = the shop's name */
int port_find_game(char *out, size_t n, const char **where);
extern char port_cd_dir[1024];            /* the CD's contents (maps, movies), may equal game dir */
/* translate a DOS path of the game ("c:\tnova\data\x.res", "D:\maps\m.res", "data\x.res") to a
 * host path; returns out */
char *port_path(const char *dos, char *out, int size);

/* ---- sound (miles.c) */
void port_audio_init(void);
void port_audio_shutdown(void);
void port_timer_init(void);
/* music output chosen by the player: "auto" (what the game's sound setup chose), "opl3", "opl2",
 * "gm", "soundfont", "off" ("fm" from earlier builds = "opl3") */
extern char port_music_mode[16];
extern char port_music_pack[128];       /* music pack folder in Music/, "" = off (musicpack.c) */
extern char port_music_dir[1024];       /* the Music folder */
int port_music_packs(char out[][64], int max);  /* the pack folders there */
void port_music_pack_changed(void);     /* switches the playing music to the new pack (xmidi.c) */
void port_music_reopen(void);          /* the music moves to port_music_mode now (miles.c) */
extern char port_soundfont[128];        /* SoundFont music's file in Music/, "" = the first (sfsynth.c) */
int port_soundfonts(char out[][64], int max);   /* the .sf2 / .dls / .sbk files there, Windows' gm.dls */
void port_soundfont_name(char *buf, size_t n);  /* the file the options' SoundFont line shows */
const char *port_soundfont_note(void);  /* how much of the game's AWE32 bank plays ("" = none) */

#endif
