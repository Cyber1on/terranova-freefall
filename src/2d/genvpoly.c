/* genvpoly.c -- x:\prj\tech\libsrc\2d\genvpoly.c (Terra Nova v1.09): voxel polygon mesh.
 * Decompiled from __FF.EXE; same as System Shock's.
 *
 *   0x328C20 gen_vox_poly
 *
 * Original quirk kept: the triangle test and the vertex fetch read element i+du, which is one
 * past the last column (index col->w, or -1 when scanning backwards).  Those entries were never
 * written for this row (they hold stack garbage, or the neighbouring array's last entry for
 * -1), exactly as in the original frame layout, which the 'vox' array reproduces: xp[2][80],
 * yp[2][80], pp[2][80] contiguous.  Bitmaps wider than 80 overflow the arrays (as in the original).
 */
#include "2d_prim.h"

#define FIX_POLY 26
#define VW 80
#define XP(r, i) vox[(r) * VW + (i)]
#define YP(r, i) vox[2 * VW + (r) * VW + (i)]
#define PP(r, i) vox[4 * VW + (r) * VW + (i)]

void gen_vox_poly(fix x[4], fix y[4], fix dz[3], int near_ver, grs_bitmap *col, grs_bitmap *ht)
{
    int du, dv, initu, endu, initv, endv;
    int i, j;
    int c;
    long z;
    fix fdxdu, fdydu, fdxdv, fdydv;
    long fcurx, fcury;
    fix fdxdz, fdydz;
    long oldcurx, oldcury;
    struct {                       /* original frame order: the poly code reads vertex 3's */
        fix vlist[8];              /* u, v, w, i from what follows vlist */
        fix *vpl[4];
    } f;
    long vox[6 * VW];              /* xp[2][80], yp[2][80], pp[2][80] */
    int currow = 0;

    f.vpl[0] = f.vlist; f.vpl[1] = f.vlist + 2; f.vpl[2] = f.vlist + 4; f.vpl[3] = f.vlist + 6;

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
                XP(currow, i) = fcurx + fdxdz * z;
                YP(currow, i) = fcury + fdydz * z;
            }
            PP(currow, i) = c;
            fcurx += fdxdu;
            fcury += fdydu;
        }
        if (currow == 1) {
            /* plot the quads */
            for (i = initu; i != endu; i += du) {
                /* check for any transparents */
                if ((ulong)PP(0, i) * PP(1, i) * PP(1, i + du) * PP(0, i + du) != 0) {
                    f.vlist[0] = XP(0, i);
                    f.vlist[1] = YP(0, i);
                    f.vlist[4] = XP(1, i + du);
                    f.vlist[5] = YP(1, i + du);
                    if (du * dv < 1) {
                        f.vlist[2] = XP(0, i + du);
                        f.vlist[3] = YP(0, i + du);
                        f.vlist[6] = XP(1, i);
                        f.vlist[7] = YP(1, i);
                    } else {
                        f.vlist[6] = XP(0, i + du);
                        f.vlist[7] = YP(0, i + du);
                        f.vlist[2] = XP(1, i);
                        f.vlist[3] = YP(1, i);
                    }
                    WC3(grd_canvas_table[FIX_POLY], PP(0, i), 4, f.vpl);
                }
            }
            /* copy from 1 to 0 */
            for (i = initu; i != endu; i += du) {
                XP(0, i) = XP(1, i);
                YP(0, i) = YP(1, i);
                PP(0, i) = PP(1, i);
            }
        } else
            ++currow;
        fcurx = (oldcurx += fdxdv);
        fcury = (oldcury += fdydv);
    }
}
