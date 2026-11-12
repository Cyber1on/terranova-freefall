/* clpslin.c -- x:\prj\tech\libsrc\2d\clpslin.c (Terra Nova v1.09): clip a shaded (intensity)
 * line.  Decompiled from __FF.EXE.
 *
 *   0x325890 gri_sline_clip
 *
 * Same as Shock: di = fix_div(i1-i0, pixels) when pixels != 0 (left unset otherwise; it is
 * only used when the line was clipped, which a zero-length line never is: it is either fully
 * inside (CLIP_NONE, unchanged) or fully outside (CLIP_ALL)).
 */
#include "2d_prim.h"

static inline fix sl_abs(fix a) { return a < 0 ? -a : a; }
static inline fix sl_max(fix a, fix b) { return a > b ? a : b; }

int gri_sline_clip(grs_vertex *v0, grs_vertex *v1)
{
    fix x0 = v0->x, y0 = v0->y, i0 = v0->i;
    fix x1 = v1->x, y1 = v1->y, i1 = v1->i;
    fix xb0 = x0, xb1 = x1, yb0 = y0, yb1 = y1;
    fix di = 0, pixels, pixels_0, pixels_1;
    int r;

    pixels = sl_max(sl_abs(y1 - y0), sl_abs(x1 - x0));
    if (pixels != 0)
        di = fix_div(i1 - i0, pixels);

    r = gr_clip_fix_line4(&x0, &y0, &x1, &y1);
    if (r != CLIP_ALL && i0 != i1 &&
        (x0 != xb0 || y0 != yb0 || x1 != xb1 || y1 != yb1)) {
        pixels_0 = sl_max(sl_abs(yb0 - y0), sl_abs(xb0 - x0));
        pixels_1 = sl_max(sl_abs(yb1 - y1), sl_abs(xb1 - x1));
        i0 += fix_mul(di, pixels_0);
        i1 -= fix_mul(di, pixels_1);
    }
    v0->x = x0; v0->y = y0; v0->i = i0;
    v1->x = x1; v1->y = y1; v1->i = i1;
    return r;
}
