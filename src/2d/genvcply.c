/* genvcply.c -- x:\prj\tech\libsrc\2d\genvcply.c (Terra Nova v1.09): voxel mesh drawn as
 * Gouraud (rgb) polygons.  Decompiled from __FF.EXE.
 *
 *   0x3291B0 gen_vox_cpoly
 *
 * Original bugs kept (same as System Shock's source): the v deltas divide by col->w - 1, the
 * height map is addressed with col->row, the vertex colours read grd_pal[c+k] instead of
 * grd_pal[3*c+k], and with du*dv > 0 vertex 3 duplicates vertex 2.  Like gen_vox_poly it
 * reads element i+du one past the last column (never written for this row); the arrays keep
 * the original frame order (pp, yp, xp).
 */
#include "2d_prim.h"

#define FIX_UCPOLY 29
#define VW 80
#define PP(r, i) vox[(r) * VW + (i)]
#define YP(r, i) vox[2 * VW + (r) * VW + (i)]
#define XP(r, i) vox[4 * VW + (r) * VW + (i)]

static void set_vertex(grs_vertex *v, fix x, fix y, ulong c)
{
    v->x = x;
    v->y = y;
    v->u = fix_make(grd_pal[c + 0], 0x8000);
    v->v = fix_make(grd_pal[c + 1], 0x8000);
    v->w = fix_make(grd_pal[c + 2], 0x8000);
}

void gen_vox_cpoly(fix x[4], fix y[4], fix dz[3], int near_ver, grs_bitmap *col, grs_bitmap *ht)
{
    int du, dv, initu, endu, initv, endv;
    int i, j;
    int c;
    long z;
    fix fdxdu, fdydu, fdxdv, fdydv;
    fix fcurx, fcury;
    fix fdxdz, fdydz;
    fix oldcurx, oldcury;
    fix vlist[20];
    grs_vertex *vpl[4];
    long vox[6 * VW];              /* pp[2][80], yp[2][80], xp[2][80] */
    int currow = 0;

    vpl[0] = (grs_vertex *)vlist; vpl[1] = (grs_vertex *)(vlist + 5);
    vpl[2] = (grs_vertex *)(vlist + 10); vpl[3] = (grs_vertex *)(vlist + 15);

    /* How to scan given near_ver */
    if (near_ver == 0 || near_ver == 3) { initu = col->w - 1; du = -1; endu = -1; }
    else { initu = 0; du = 1; endu = col->w; }
    if (near_ver < 2) { initv = col->h - 1; dv = -1; endv = -1; }
    else { initv = 0; dv = 1; endv = col->h; }

    fdxdu = (x[1] - x[0]) / (col->w - 1);
    fdydu = (y[1] - y[0]) / (col->w - 1);
    fdxdv = (x[2] - x[1]) / (col->w - 1);
    fdydv = (y[2] - y[1]) / (col->w - 1);

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
                z = *(ht->bits + i + j * col->row);
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
                    set_vertex(vpl[0], XP(0, i), YP(0, i), PP(0, i));
                    set_vertex(vpl[2], XP(1, i + du), YP(1, i + du), PP(1, i + du));
                    if (du * dv > 0) {
                        set_vertex(vpl[1], XP(0, i + du), YP(0, i + du), PP(0, i + du));
                        set_vertex(vpl[3], XP(1, i + du), YP(1, i + du), PP(1, i + du));
                    } else {
                        set_vertex(vpl[3], XP(0, i + du), YP(0, i + du), PP(0, i + du));
                        set_vertex(vpl[1], XP(1, i), YP(1, i), PP(1, i));
                    }
                    WC3(grd_canvas_table[FIX_UCPOLY], 0, 4, vpl);
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
