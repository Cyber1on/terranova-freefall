/* genrect.c -- x:\prj\tech\libsrc\2d\genrect.c, genbox.c (Terra Nova v1.09): generic filled
 * and outlined rectangles built from horizontal / vertical lines.  Decompiled from __FF.EXE.
 *
 *   0x3275C0 gen_urect   0x327610 gen_rect   0x327670 gen_ubox   0x327774 gen_box
 */
#include "2d_prim.h"

/* gr_uhline / gr_uvline: the current fill type's unclipped line fillers */
#define gr_uhline(x0, y, x1) \
    WC5(grd_uline_fill_vector[GRL_UHLINE], x0, y, x1, grd_gc.fcolor, grd_gc.fill_parm)
#define gr_uvline(x, y0, y1) \
    WC5(grd_uline_fill_vector[GRL_UVLINE], x, y0, y1, grd_gc.fcolor, grd_gc.fill_parm)

/* draw an unclipped, filled rectangle with edges as given.  do this
   by making repeated calls to the installed unclipped hline drawer. */
void gen_urect(short left, short top, short right, short bot)
{
    while (top < bot) {
        gr_uhline(left, top, (short)(right - 1));
        top++;
    }
}

/* draw a clipped, filled rectangle.  clip, then chain to the installed
   unclipped rectangle drawer.  returns clip code. */
int gen_rect(short left, short top, short right, short bot)
{
    int r = gr_clip_rect(&left, &top, &right, &bot);
    if (r != CLIP_ALL)
        WC4(grd_canvas_table[DRAW_URECT], left, top, right, bot);
    return r;
}

/* draw an unclipped, unfilled rectangle.  does 2 hlines & 2 vlines. */
void gen_ubox(short left, short top, short right, short bot)
{
    if (left <= right - 2)
        gr_uhline(left, top, (short)(right - 2));
    if (top <= bot - 2)
        gr_uvline((short)(right - 1), top, (short)(bot - 2));
    if (left + 1 <= right - 1)
        gr_uhline((short)(left + 1), (short)(bot - 1), (short)(right - 1));
    if (top + 1 <= bot - 1)
        gr_uvline(left, (short)(top + 1), (short)(bot - 1));
}

/* draw a clipped, unfilled rectangle.  does 2 clipped hlines and 2 clipped
   vlines.  returns clip code. */
int gen_box(short left, short top, short right, short bot)
{
    int code = CLIP_NONE;

    if (left <= right - 2)
        code |= gen_hline(left, top, right - 2);
    if (top <= bot - 2)
        code |= gen_vline(right - 1, top, bot - 2);
    if (left + 1 <= right - 1)
        code |= gen_hline(left + 1, bot - 1, right - 1);
    if (top + 1 <= bot - 1)
        code |= gen_vline(left, top + 1, bot - 1);
    return code;
}
