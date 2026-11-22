/* visor.h -- "Visor damage" (port/mods/visor.c): the suit's damage shown on the visor's glass
 * with a cockpit mod's picture. */
#ifndef TN_VISOR_H
#define TN_VISOR_H
#include <stdint.h>

extern int port_visor;                  /* "Visor damage": 0 Off, 1 On (screen.c) */
/* mfddata.c (game thread): a mission's suit is set up: no damage yet, the mission's picks */
void port_visor_begin(void);
/* sharp3d.c (game thread): the main view's picture is drawn (high, its full colours tc / tc_rows
 * as port_s3d_frame's; W x H the view's pixels, ey more rows above and below it; the game's
 * screen scr_w x scr_h at kx x ky): the damage of the step the armour is at over it.  goggles:
 * the goggles' view, else the cockpit's; suit as port_ck_view_suit (the Pirate suit, 3: none) */
void port_visor_draw(uint8_t *high, uint32_t *tc, const uint8_t *tc_rows, int W, int H, int ey, int scr_w,
                     int scr_h, int kx, int ky, int goggles, int suit);
#endif
