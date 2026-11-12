/* clpclin.c -- x:\prj\tech\libsrc\2d\clpclin.c (Terra Nova v1.09): clip an RGB-shaded line.
 *   0x346220 gri_cline_clip
 *
 * The vertex u, v, w hold the end colours (only their low bytes are read).  The line is
 * clipped with gr_clip_fix_line4 and each end colour is moved along by the colour slope times
 * the distance the end moved (both in Chebyshev pixels).
 * Quirks kept: the slopes are left unset when the line has length 0 (we use 0; the original
 * reads stack junk, but then no end can move); u, v, w of both vertices are always written
 * back as their low byte zero-extended, even when nothing was clipped.
 */
#include "2d.h"

static fix cheb(fix ax, fix ay, fix bx, fix by)
{
    fix dx = ax - bx, dy = ay - by;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dy > dx ? dy : dx;
}

int gri_cline_clip(grs_vertex *v0, grs_vertex *v1)
{
    fix x0 = v0->x, y0 = v0->y, x1 = v1->x, y1 = v1->y;
    fix ox0 = x0, oy0 = y0, ox1 = x1, oy1 = y1;
    uchar r0 = (uchar)v0->u, g0 = (uchar)v0->v, b0 = (uchar)v0->w;
    uchar r1 = (uchar)v1->u, g1 = (uchar)v1->v, b1 = (uchar)v1->w;
    fix len, dr = 0, dg = 0, db = 0, d0, d1;
    int code;

    len = cheb(x1, y1, x0, y0);
    if (len != 0) {
        dr = fix_div((r1 - r0) << 16, len);
        dg = fix_div((g1 - g0) << 16, len);
        db = fix_div((b1 - b0) << 16, len);
    }
    code = gr_clip_fix_line4(&x0, &y0, &x1, &y1);
    if (code != CLIP_ALL && !(r0 == r1 && g0 == g1 && b0 == b1)
        && !(x0 == ox0 && y0 == oy0 && x1 == ox1 && y1 == oy1)) {
        d0 = cheb(ox0, oy0, x0, y0);
        d1 = cheb(ox1, oy1, x1, y1);
        r0 += fix_mul(dr, d0) >> 16;
        g0 += fix_mul(dg, d0) >> 16;
        b0 += fix_mul(db, d0) >> 16;
        r1 -= fix_mul(dr, d1) >> 16;
        g1 -= fix_mul(dg, d1) >> 16;
        b1 -= fix_mul(db, d1) >> 16;
    }
    v0->x = x0;
    v0->y = y0;
    v0->u = r0;
    v0->v = g0;
    v0->w = b0;
    v1->x = x1;
    v1->y = y1;
    v1->u = r1;
    v1->v = g1;
    v1->w = b1;
    return code;
}
