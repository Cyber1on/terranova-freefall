/* cockpitpack.h -- Freefall: "Cockpit pack" (port/mods/cockpitpack.c): the cockpit's and the goggles'
 * art painted for 16:9, shown in place of the evenly stretched screen of Widescreen.
 * Plain C types only: shared by the SDL side and the game side. */
#ifndef COCKPITPACK_H
#define COCKPITPACK_H

#include <stdint.h>

#define HELM_LAYERS 6                   /* the helmet's parts (for the hit's static), back to front:
                                         * chin console and screens, bezels and hump, brow, wall and
                                         * rim, plates, upper corners and hoses */

#define HELM_PARTS 8                    /* the layered helmet's parts at most */
#define HELM_STILL 0                    /* a part that does not move (it meets the screen's edges) */
#define HELM_SIDE 1                     /* ... that moves sideways (it meets the top edge) */
#define HELM_SIDE_DOWN 2                /* ... that moves sideways and down, not up (the bottom) */
#define HELM_MOVE 3                     /* ... that moves every way (its borders: -border.png) */

extern char port_ck_pack[64];           /* the mod whose Cockpits folder is used, "" = off (screen.c) */
extern volatile int port_ck_on;         /* the display shows a pack's picture (sharp3d.c: the main
                                         * view as wide as the picture, the side screens not wider) */
int port_ck_packs(char out[][64], int max);     /* the mods with a Cockpits folder (fxpack.c) */

/* game thread (helmet.c helm_draw_frame), the art as the game draws it, w x h colour numbers:
 * the cockpit's picture (res 750, the whole screen), and each frame the piece drawn over the
 * main view (part 0: the cockpit's frame 752 at x, y of the screen; part 1: the goggles' frame
 * 751, the whole screen; -1: none, the drone's view), gen changing with its pixels.  letter: the
 * suit's, the last letter of its res file's name (S, N, H, P) */
void port_ck_art(int letter, const uint8_t *px, int w, int h);
void port_ck_piece(int part, int letter, const uint8_t *px, unsigned gen, int w, int h, int x, int y);
/* game thread (mfddata.c): a piece of the suit's damage drawn on the frame at x0..x1, y0..y1 of
 * the screen (pitch bytes a row) */
void port_ck_mark(const uint8_t *scr, long pitch, int x0, int y0, int x1, int y1);
/* ... with a pack's picture shown, the piece (pc, w x h, 0 = none of it) drawn undamaged: the art's
 * pixels (0 if not done) */
int port_ck_whole(uint8_t *scr, long pitch, const uint8_t *pc, int w, int h, int x0, int y0);

/* display: the pack's picture for the screen up now (w x h, the game's screen shown out_h of the
 * window's pixels tall); 0 if there is none */
typedef struct {
    const uint8_t *pix;                 /* (pw * s) x (h * s) colour numbers, 0 = see-through */
    int s;                              /* 1, or 2 for a -2x picture */
    int pw;                             /* its columns at 1x: the screen's w in the middle */
    const uint8_t *win;                 /* pw x h: 1 where any of its s x s pixels is 0 */
    const uint8_t *art;                 /* w x h: the game's art it replaces (0 = the game's window) */
    const uint8_t *marks;               /* w x h: 1 where the game drew a damaged piece of the frame */
    unsigned serial;                    /* changes with the picture or the art */
    unsigned id;                        /* changes with the picture only (a new load too) */
    const uint8_t *lay;                 /* pix's size: each pixel's part 0 .. HELM_LAYERS - 1 (the
                                         * screens' static on parts 0 .. 2), NULL: all */
    /* the layered helmet (<name>-parts.png, cockpitpack.c): its parts move apart, NULL none */
    const uint8_t *parts;               /* pix's size: each pixel's part 1 .. np, back to front
                                         * (0: the window, the 3D) */
    const uint8_t *hidden;              /* np pictures of pix's size one after the other: each
                                         * part's art under the parts in front of it, 0 none */
    int np;
    unsigned char pmove[HELM_PARTS];    /* each part's move: HELM_STILL .. HELM_MOVE */
    float pz[HELM_PARTS];               /* ... and its distance from the eye (m) */
    const uint8_t *border;              /* (pix's width + 2 bb) x (height + 2 bb): the art beyond the
                                         * picture's edges (pix in the middle unused), NULL none */
    int bb;                             /* border's pixels beyond each edge */
} port_ckpic;
int port_ck_get(int w, int h, int out_h, const uint8_t *scr, int pitch, port_ckpic *p);
int port_ck_view_suit(void);            /* game thread: the cockpit or goggles up: its suit (-1 not known), else -2 */
int port_ck_goggles_suit(void);         /* game thread: the goggles' frame over the view: its suit
                                         * (0 scout .. 3 pirate, -1 not known), else -2 */
int port_ck_cols(int w);                /* a pack picture's columns at 1x for a screen w wide */

#endif
