/* gencirc.c -- x:\prj\tech\libsrc\2d\gencirc.c, gendisk.c (Terra Nova v1.09): circles and
 * disks as ellipses / ovals corrected for the screen aspect.  Decompiled from __FF.EXE.
 *
 *   0x329C50 flat8_roll_null   0x329C60 gen_fix_circle   0x329CB8 gen_fix_ucircle
 *   0x329D10 gen_int_circle    0x329D38 gen_int_ucircle  0x329D60 gen_fix_disk
 *   0x329DB8 gen_fix_udisk     0x329E10 gen_int_udisk    0x329E38 gen_int_disk
 *   0x329E60 gen_rod_null
 *
 * Terra Nova's versions take fix coordinates (the int versions convert to the pixel centre,
 * x - 1/2) and call the fix ellipse / oval drawers; clipped and unclipped versions are the
 * same code (both call the clipping drawer).  The ROLL and ROD canvas entries point at the two
 * empty functions (names ours).
 */
#include "2d_prim.h"

/* scale from 320x200 x-pixels; b = a corrected for the aspect */
#define CIRCLE_AB(r, a, b)                          \
    do {                                            \
        fix ratio = fix_div(grd_cap->w, 320);       \
        a = fix_mul(r, ratio);                      \
        b = fix_div(a, grd_cap->aspect);            \
    } while (0)

#define PIX_CENTER(v) ((fix_make((v) - 1, 0)) | 0x8000)

void flat8_roll_null(void)
{
}

void gen_rod_null(void)
{
}

int gen_fix_circle(fix x0, fix y0, fix r)
{
    fix a, b;
    CIRCLE_AB(r, a, b);
    return gen_fix_elipse(x0, y0, a, b);
}

int gen_fix_ucircle(fix x0, fix y0, fix r)
{
    fix a, b;
    CIRCLE_AB(r, a, b);
    return gen_fix_elipse(x0, y0, a, b);
}

int gen_int_circle(short x0, short y0, short r)
{
    return gen_fix_circle(PIX_CENTER(x0), PIX_CENTER(y0), fix_make(r, 0));
}

int gen_int_ucircle(short x0, short y0, short r)
{
    return gen_fix_ucircle(PIX_CENTER(x0), PIX_CENTER(y0), fix_make(r, 0));
}

int gen_fix_disk(fix x0, fix y0, fix r)
{
    fix a, b;
    CIRCLE_AB(r, a, b);
    return gen_fix_oval(x0, y0, a, b);
}

int gen_fix_udisk(fix x0, fix y0, fix r)
{
    fix a, b;
    CIRCLE_AB(r, a, b);
    return gen_fix_oval(x0, y0, a, b);
}

int gen_int_udisk(short x0, short y0, short r)
{
    return gen_fix_udisk(PIX_CENTER(x0), PIX_CENTER(y0), fix_make(r, 0));
}

int gen_int_disk(short x0, short y0, short r)
{
    return gen_fix_disk(PIX_CENTER(x0), PIX_CENTER(y0), fix_make(r, 0));
}
