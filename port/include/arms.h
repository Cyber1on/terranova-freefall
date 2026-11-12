/* arms.h -- Freefall: "Arms pack" (port/mods/arms.c): the suit's arm holding the selected weapon,
 * drawn over the main 3D view in the goggles, aimed at the pointer.
 * Plain C types only: shared by the SDL side and the game side. */
#ifndef ARMS_H
#define ARMS_H

#include <stdint.h>

extern char port_arms_pack[64];         /* the mod whose Arms folder is used, "" = off (screen.c) */
int port_arms_packs(char out[][64], int max);   /* the mods with an Arms folder (fxpack.c) */

/* game thread, the end of the main view's sharp drawing (sharp3d.c), goggles up: the arm into
 * the picture high (W x H colour numbers; tc / tc_rows its "True colour" pixels or NULL; rows
 * down to Hd are there too: drawn below the view for Helmet motion, where the frame moving up
 * shows them, so the arm goes on there and is lowered below them), the
 * pointer at u, v of the view (-1 .. 1 from its middle to its edges, v up), mount the selected
 * weapon's (pbasim.c: 2 the left arm, 3 the right, else none), suit 0 scout .. 3 pirate (-1 not known) */
void port_arms_draw(uint8_t *high, uint32_t *tc, const uint8_t *tc_rows, int W, int H, int Hd, double u,
                    double v, int mount, int suit);
void port_arms_preload(int suit);              /* the suit's poses start loading (P78) */
int port_arms_muzzle(float *fx, float *fy);    /* the arm drawn last: its muzzle in the picture, its mount */
int port_player_weapon_mount(void);     /* pbasim.c: the selected weapon's mount */
int port_player_motion(double *fwd, double *side, double *secs);  /* sharp3d.c: the suit's move on the ground (m, s) */
void port_arms_fired(void);             /* pbasim.c: the player fired a weapon */
extern int port_ml_frame_turn, port_ml_frame_look;  /* platform.c: mouselook's turn this frame */

#endif
