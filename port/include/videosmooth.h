/* videosmooth.h -- "Smooth videos": in-between pictures for the game's 15 fps movies, made by
 * RIFE on the graphics card (tnfree-rife.dll, loaded only when this is used).  videosmooth.c. */
#ifndef TN_VIDEOSMOOTH_H
#define TN_VIDEOSMOOTH_H

#include <stdint.h>

extern int port_smooth_videos;          /* 1 = make in-between pictures for the movies */

/* Loads tnfree-rife.dll and the model and starts the worker thread, once.  With wait = 0 it
 * returns at once (the model loads in the background); 1 when ready, 0 while loading, -1 if it
 * can't work here (no Vulkan, no DLL, ...; vsmooth_status() tells why). */
int vsmooth_start(int wait, int gpu);   /* gpu: card number, -1 default, -2 integrated, -3 discrete */
void vsmooth_set_model(const char *name);   /* a folder in RIFE/ (before vsmooth_start) */
const char *vsmooth_status(void);
const char *vsmooth_cards(void);        /* the Vulkan cards, one "  card N: name (kind)" line each */       /* "RIFE 4.6 on <card>" or the reason it is off */
void vsmooth_stop(void);                /* at exit */

/* one movie: pictures w x h, 24-bit RGB */
typedef struct vsmooth vsmooth;
vsmooth *vsmooth_begin(int w, int h);   /* NULL when not ready */
/* Picture number `frame` (8-bit pixels and the 6-bit palette, as in the movie), in order.
 * Queues the in-between picture of frame-1 and frame, unless the two are a scene cut. */
void vsmooth_push(vsmooth *v, uint32_t frame, const uint8_t *pixels, const uint8_t *pal6);
/* The in-between picture of frame-1 and frame into rgb (w * h * 3 bytes): 1 = there, 0 = not
 * done yet (after waiting up to wait_ms), -1 = none (scene cut, first picture, worker too slow). */
int vsmooth_mid(vsmooth *v, uint32_t frame, uint8_t *rgb, int wait_ms);
/* frame's own picture in RGB (the last two pushed are kept): 1 = there */
int vsmooth_rgb(vsmooth *v, uint32_t frame, uint8_t *rgb);
void vsmooth_end(vsmooth *v);
/* puts an in-between picture up over the movie (at vram_off in the game's video memory, rows
 * pitch bytes apart) until vsmooth_hide (the movie's next picture) or a palette change */
void vsmooth_show(vsmooth *v, const uint8_t *rgb, uint32_t vram_off, int pitch);
void vsmooth_hide(void);
/* the display (platform.c): draws the picture that is up into the game's w x h screen as XRGB
 * (screen at video memory `start`, `pitch`); grade turns n RGB pixels into the display's colours.
 * 1 when drawn. */
int vsmooth_overlay(uint32_t *pixels, int tpitch, uint32_t start, int pitch, int w, int h,
                    void (*grade)(const uint8_t *rgb, uint32_t *out, int n));
int vsmooth_state(void);                /* 0 not started, 1 loading, 2 ready, -1 not available */

typedef struct {
    uint32_t made, cuts, dropped, late, shown;
    double total_ms, max_ms;            /* time the card took for the in-between pictures */
} vsmooth_stats;
void vsmooth_get_stats(const vsmooth *v, vsmooth_stats *st);
void vsmooth_count_late(vsmooth *v);    /* an in-between picture missed its moment */

/* "tnfree --smooth-test": smooths movies into AVI files to look at (smoothtest.c) */
int vsmooth_test_main(int argc, char **argv);

/* smacker.c, for the test: a movie at a host path, video only */
void *port_smk_open(const char *host, int *w, int *h, uint32_t *frames, double *ms_per_frame);
int port_smk_next(void *m, const uint8_t **pixels, const uint8_t **pal6);
void port_smk_close(void *m);

#endif
