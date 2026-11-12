/* clplin.c -- x:\prj\tech\libsrc\2d\clplin.c (Terra Nova v1.09): fixed-point Cohen-Sutherland
 * line clipper.  Decompiled from __FF.EXE.
 *
 *   0x313D20 gr_clip_fix_line
 *
 * Terra Nova passes the two points as fix[2] arrays (x, y).  gr_clip_fix_code (0x325470) is
 * in the 2d-prim range.  fix_mul_div is a 64-bit multiply and 32-bit idiv (faults on a zero
 * divisor or an overflowing quotient, like the original).
 */
#include "2d.h"

static inline fix clp_mul_div(fix a, fix b, fix c)
{
    return (fix)(((long long)a * b) / c);
}

int gr_clip_fix_line(fix *p0, fix *p1)
{
    fix dx = p1[0] - p0[0];
    fix dy = p1[1] - p0[1];
    int code0, code1, code;
    fix *px, *py;

    for (;;) {
        code0 = gr_clip_fix_code(p0[0], p0[1]);
        code1 = gr_clip_fix_code(p1[0], p1[1]);
        if (code0 == 0 && code1 == 0)
            return CLIP_NONE;
        if (code0 & code1)
            return CLIP_ALL;
        if (code0 != 0) {
            px = &p0[0]; py = &p0[1]; code = code0;
        } else {
            px = &p1[0]; py = &p1[1]; code = code1;
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
