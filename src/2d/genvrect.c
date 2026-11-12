/* genvrect.c -- x:\prj\tech\libsrc\2d\genvrect.c (Terra Nova v1.09): voxel rectangle.
 * Decompiled from __FF.EXE; same as System Shock's.
 *
 *   0x3289A0 gen_vox_rect
 */
#include "2d_prim.h"

#define DRAW_RECT 16
#define gr_set_fcolor(c) (grd_gc.fcolor = (c))
#define gr_rect(l, t, r, b) WC4(grd_canvas_table[DRAW_RECT], (short)(l), (short)(t), (short)(r), (short)(b))

/* x and y are the screen x and y's in fixed point of the vertices of this voxel piece
   dz[0] is the difference in x for a delta one in z
   dz[1] is the difference in y for a delta one in z
   near_ver is which vertex is closest to the viewer, so it knows which way to traverse
   col is the color map
   ht is the height map, 0 means at the surface of the bounding box, positive goes in
   dotw and doth are the height and width of the rectangles to use at each point */
void gen_vox_rect(fix x[4], fix y[4], fix dz[3], int near_ver, grs_bitmap *col, grs_bitmap *ht, int dotw, int doth)
{
    int du, dv, initu, endu, initv, endv;
    int i, j;
    int c;
    long z;
    fix fdxdu, fdydu, fdxdv, fdydv;
    fix fcurx, fcury;
    long fxp, fyp;
    long fdxdz, fdydz;
    long oldcurx, oldcury;

    /* How to scan given near_ver */
    if (near_ver == 0 || near_ver == 3) { initu = col->w - 1; du = -1; endu = -1; }
    else { initu = 0; du = 1; endu = col->w; }
    if (near_ver < 2) { initv = col->h - 1; dv = -1; endv = -1; }
    else { initv = 0; dv = 1; endv = col->h; }

    fdxdu = (x[1] - x[0]) / (col->w - 1);
    fdydu = (y[1] - y[0]) / (col->w - 1);
    fdxdv = (x[2] - x[1]) / (col->h - 1);
    fdydv = (y[2] - y[1]) / (col->h - 1);

    fdxdz = dz[0];
    fdydz = dz[1];

    oldcurx = fcurx = x[0] + fdxdu * initu + fdxdv * initv;
    oldcury = fcury = y[0] + fdydu * initu + fdydv * initv;

    fdxdu *= du;
    fdydu *= du;
    fdxdv *= dv;
    fdydv *= dv;

    for (j = initv; j != endv; j += dv) {
        for (i = initu; i != endu; i += du) {
            c = *(col->bits + i + j * col->row);
            if (c != 0) {
                z = *(ht->bits + i + j * ht->row);
                fxp = (fcurx + fdxdz * z) >> 16;
                fyp = (fcury + fdydz * z) >> 16;
                gr_set_fcolor(c);
                gr_rect(fxp, fyp, fxp + dotw, fyp + doth);
            }
            fcurx += fdxdu;
            fcury += fdydu;
        }
        fcurx = (oldcurx += fdxdv);
        fcury = (oldcury += fdydv);
    }
}
