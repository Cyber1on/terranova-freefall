/* helmetfx.h -- Freefall: the helmet's moves (port/mods/helmetfx.c).
 * Plain C types only: shared by the SDL side and the game side. */
#ifndef HELMETFX_H
#define HELMETFX_H

extern int port_helmet_motion;          /* "Helmet motion": 0 Off, 1 On (screen.c) */

/* game thread */
void port_helmet_eye(void);             /* hud_eye_player: the eye of this frame is set */
void port_helmet_fired(void);           /* arms.c port_arms_fired: the player fired */
void port_helmet_hit(int amount);       /* hudmain.c damage_shake: the player took damage */

/* display: the helmet's move against the head */
typedef struct {
    double yaw, pitch;                  /* degrees: + moves the helmet right, down on the screen */
    double ex, ey;                      /* the eye's offset in the helmet (m): + right, up */
    double hit;                         /* the screens' static after a hit, 0 .. 1 */
} port_helmet_state;
void port_helmet_get(port_helmet_state *out);
extern volatile int port_helmet_sway;   /* display: "Helmet motion" with a cockpit pack's picture
                                         * that has parts (its -parts.png): 1 the parts move apart
                                         * (platform.c) and the 3D stays, drawn a few rows taller
                                         * (sharp3d.c); 0 off */

#endif
