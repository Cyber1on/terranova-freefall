/* fl8plyl.c -- x:\prj\tech\libsrc\2d\fl8ply.c (Terra Nova v1.09): the flat8 polygon scanline
 * loop (the init routines are 2d-tmap's src/2d/fl8ply.c).  Decompiled from __FF.EXE.
 *
 *   0x32D2E0 gri_poly_loop
 *
 * For each of ti->n lines it fills [fix_cint(left.x), fix_cint(right.x)) of the line at ti->d:
 *   loop 0x00: solid fill with the low byte of ti->bm.bits (read once at entry);
 *   loop 0x10: d[x] = ti->bm.bits[d[x]]  (bits used as a translucency table);
 *   loop 0x14: d[x] = ti->clut[ti->bm.bits[d[x]]];
 *   any other loop value draws nothing.
 * then steps d by grd_bm.row and both edges by their dx.  A negative span returns 1 at once;
 * otherwise 0 when the lines are done.  The span limits are 16-bit (fix_cint as short).
 */
#include "2d_prim.h"

#define cint16(f) ((short)(((f) + 0xffff) >> 16))

int gri_poly_loop(grs_tmap_loop_info *ti)
{
    uchar color = (uchar)(ulong)ti->bm.bits;
    int x, w;

    do {
        x = cint16(ti->left.x);
        w = cint16(ti->right.x) - x;
        if (w > 0) {
            switch (ti->loop) {
            case 0: {
                uchar *p = ti->d + x;
                ulong pat = color * 0x01010101u;
                unsigned k;
                for (k = (unsigned)w >> 2; k; k--, p += 4) *(ulong *)p = pat;
                for (k = w & 3; k; k--) *p++ = color;
                break;
            }
            case 0x10:
                for (; x < cint16(ti->right.x); x++)
                    ti->d[x] = ti->bm.bits[ti->d[x]];
                break;
            case 0x14:
                for (; x < cint16(ti->right.x); x++)
                    ti->d[x] = ti->clut[ti->bm.bits[ti->d[x]]];
                break;
            }
        } else if (w < 0)
            return 1;
        ti->d += grd_bm.row;
        ti->n--;
        ti->left.x += ti->left.dx;
        ti->right.x += ti->right.dx;
    } while (ti->n > 0);
    return 0;
}
