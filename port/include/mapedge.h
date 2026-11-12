/* mapedge.h -- Freefall: the land around the map's 512 x 512 squares for the 3D view (G34, G35;
 * port/video/mapedge.c).  The terrain's polygons (terrrast.c, terrstrp.c, weather.c) and the caster's
 * near rings (haze.c) read the map's fine grid through these, a copy of it with PF_M squares of
 * land around it made from the coarse grid. */
#ifndef TN_MAPEDGE_H
#define TN_MAPEDGE_H

#define PF_M 256                        /* squares of land on each side */
#define PF_W (0x201 + 2 * PF_M)         /* cells a row (and rows) */

extern unsigned char *port_pf;          /* the cell of square (0, 0) */
void port_pf_sync(void);                /* before a 3D view: the copy as the map is now */
void port_pf_frame(void);               /* a game frame went by (port_frame_limit) */

#define PF_CELL(x, y) (port_pf + ((long)(y) * PF_W + (long)(x)) * 3)
/* a cell in the copy, and one with the cells right and below it too */
#define PF_IN(x, y)  ((unsigned long)((x) + PF_M) < PF_W && (unsigned long)((y) + PF_M) < PF_W)
#define PF_IN1(x, y) ((unsigned long)((x) + PF_M) < PF_W - 1 && (unsigned long)((y) + PF_M) < PF_W - 1)

#endif
