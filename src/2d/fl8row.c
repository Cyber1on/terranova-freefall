/* fl8row.c -- x:\prj\tech\libsrc\2d\fl8row.c, fl8sub.c (Terra Nova v1.09): flat8 device bitmap
 * row and sub bitmap.  Decompiled from __FF.EXE.
 *
 *   0x32D2B0 flat8_calc_row: returns its argument (a bare ret).
 *   0x32D2C0 flat8_sub_bitmap: Terra Nova's only moves bm->bits to (x,y); unlike Shock it does
 *            not set w and h (it takes no more than three arguments) and returns bm.
 */
#include "2d_prim.h"

long flat8_calc_row(long w)    /* bare ret: eax comes back as passed, all 32 bits */
{
    return w;
}

grs_bitmap *flat8_sub_bitmap(grs_bitmap *bm, short x, short y)
{
    bm->bits += y * bm->row + x;
    return bm;
}
