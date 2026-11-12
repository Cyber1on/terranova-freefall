/* clpfix.c -- x:\prj\tech\libsrc\2d\clplin.c (Terra Nova v1.09): Cohen-Sutherland clip code and
 * four-pointer fixed-point line clipper.  Decompiled from __FF.EXE.
 *
 *   0x325470 gr_clip_fix_code
 *   0x3254C4 gr_clip_fix_line4 (name ours: Shock's gr_clip_fix_line(fix *x0, fix *y0, fix *x1,
 *            fix *y1); Terra Nova also has a two-pointer copy at 0x313D20, which 2d-core
 *            decompiled as gr_clip_fix_line).  Called by gri_sline_clip and 0x346346.
 *
 * As in Shock, dx and dy are taken once, before the loop, and fix_mul_div is a 64-bit
 * multiply and 32-bit idiv (faults on a zero divisor or an overflowing quotient, like the
 * original).
 */
#include "2d_prim.h"

int gr_clip_fix_code(fix x, fix y)
{
    int code = 0;

    if (x < grd_fix_clip.left)
        code = CLIP_LEFT;
    else if (x > grd_fix_clip.right - fix_make(1, 0))
        code = CLIP_RIGHT;
    if (y < grd_fix_clip.top)
        code |= CLIP_TOP;
    else if (y > grd_fix_clip.bot - fix_make(1, 0))
        code |= CLIP_BOT;
    return code;
}

static inline fix clp_mul_div(fix a, fix b, fix c)
{
    return (fix)(((long long)a * b) / c);
}

int gr_clip_fix_line4(fix *x0, fix *y0, fix *x1, fix *y1)
{
    fix dx = *x1 - *x0;
    fix dy = *y1 - *y0;
    int code0, code1, code;
    fix *px, *py;

    for (;;) {
        code0 = gr_clip_fix_code(*x0, *y0);
        code1 = gr_clip_fix_code(*x1, *y1);
        if (code0 == 0 && code1 == 0)
            return CLIP_NONE;
        if (code0 & code1)
            return CLIP_ALL;
        if (code0 != 0) {
            px = x0; py = y0; code = code0;
        } else {
            px = x1; py = y1; code = code1;
        }
        if (code & CLIP_LEFT) {
            *py += clp_mul_div(dy, grd_fix_clip.left - *px, dx);
            *px = grd_fix_clip.left;
        } else if (code & CLIP_RIGHT) {
            *py += clp_mul_div(dy, grd_fix_clip.right - fix_make(1, 0) - *px, dx);
            *px = grd_fix_clip.right - fix_make(1, 0);
        }
        if (code & CLIP_TOP) {
            *px += clp_mul_div(dx, grd_fix_clip.top - *py, dy);
            *py = grd_fix_clip.top;
        } else if (code & CLIP_BOT) {
            *px += clp_mul_div(dx, grd_fix_clip.bot - fix_make(1, 0) - *py, dy);
            *py = grd_fix_clip.bot - fix_make(1, 0);
        }
    }
}
