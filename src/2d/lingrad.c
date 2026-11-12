/* lingrad.c -- linear mapper gradient setup (Terra Nova v1.09).  Decompiled from __FF.EXE;
 * no System Shock counterpart (Shock computes these inline in the lin mappers).
 *
 *   0x34C270 gri_lin_gradients (name ours; called by the 2d-tmap lin mappers)
 *
 * From the first three vertices of vpl it solves the plane of u and v over screen x, y:
 *   info->left.du = du/dx, info->left.dv = dv/dx, info->right.du = du/dy, info->right.dv = dv/dy,
 * eliminating with the edge whose |dy| (for d/dx) or |dx| (for d/dy) is larger.  The
 * products are 64-bit with a 32-bit quotient ("imul; idiv"), the final divisions fix_div;
 * all fault in the original on a zero divisor or overflow.  Returns the last quotient
 * (eax is left with it).
 */
#include "2d.h"

static long mul_div(long a, long b, long c) { return (long)(((long long)a * b) / c); }

long gri_lin_gradients(grs_tmap_loop_info *info, grs_vertex **vpl)
{
    grs_vertex *v0 = vpl[0], *v1 = vpl[1], *v2 = vpl[2];
    fix dx1 = v1->x - v0->x, dy1 = v1->y - v0->y;
    fix dx2 = v2->x - v0->x, dy2 = v2->y - v0->y;
    fix du1 = v1->u - v0->u, dv1 = v1->v - v0->v;
    fix du2 = v2->u - v0->u, dv2 = v2->v - v0->v;
    fix den;

    if ((dy1 < 0 ? -dy1 : dy1) > (dy2 < 0 ? -dy2 : dy2)) {
        den = dx2 - mul_div(dy2, dx1, dy1);
        info->left.du = fix_div(du2 - mul_div(dy2, du1, dy1), den);
        info->left.dv = fix_div(dv2 - mul_div(dy2, dv1, dy1), den);
    } else {
        den = dx1 - mul_div(dy1, dx2, dy2);
        info->left.du = fix_div(du1 - mul_div(dy1, du2, dy2), den);
        info->left.dv = fix_div(dv1 - mul_div(dy1, dv2, dy2), den);
    }
    if ((dx1 < 0 ? -dx1 : dx1) > (dx2 < 0 ? -dx2 : dx2)) {
        den = dy2 - mul_div(dx2, dy1, dx1);
        info->right.du = fix_div(du2 - mul_div(dx2, du1, dx1), den);
        info->right.dv = fix_div(dv2 - mul_div(dx2, dv1, dx1), den);
    } else {
        den = dy1 - mul_div(dx1, dy2, dx2);
        info->right.du = fix_div(du1 - mul_div(dx1, du2, dx2), den);
        info->right.dv = fix_div(dv1 - mul_div(dx1, dv2, dx2), den);
    }
    return info->right.dv;
}
