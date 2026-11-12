/* mxsub.c -- Terra Nova v1.09 ModeX canvas (canvas table 0x36808C): bitmap row size and
 * sub-bitmaps.  Decompiled from __FF.EXE (Watcom C); names ours.  A ModeX row holds w / 4
 * bytes per plane; a ModeX bitmap's align is the plane of its first pixel.
 *
 *   0x33D230 modex_calc_row   0x33D240 modex_sub_bitmap
 */
#include "2d_tmap.h"

long modex_calc_row(short w)
{
    return (w + 3) >> 2;
}

grs_bitmap *modex_sub_bitmap(grs_bitmap *bm, short x, short y)
{
    bm->bits += (ushort)bm->row * y + ((x + bm->align) >> 2);
    bm->align = (uchar)((bm->align + x) & 3);
    return bm;
}
