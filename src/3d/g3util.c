/* g3util.c -- Terra Nova's own 3D helpers, Watcom C linked after points.asm (no source path
 * string; file name and function names ours).  Decompiled from __FF.EXE.
 *
 *   0x2F4DC0 g3_screen_size       0x2F4E34 g3_screen_size_x     0x2F4E7C g3_screen_size_y
 *   0x2F4EC4 g3_set_light_shift   0x2F4EF0 g3_set_light_max     0x2F4F3C g3_cross_product
 *
 * All use the standard Watcom register convention.
 */
#include "3d_a.h"

/* Projected size in pixels of a radius r at depth z (larger of the x and y scales).
 * Returns -1 when the object is entirely behind the viewer (z < -r * zoom), twice the larger
 * half-window size when z < 0.  The final division faults in the original on overflow or
 * z == 0.  Unreferenced. */
long g3_screen_size(fix r, fix z)
{
    fix s, t;
    if (z < -g3a_mul(r, _view_zoom))
        return -1;
    if (z < 0) {
        s = _scrw;
        if (s <= _scrh)
            s = _scrh;
        return s + s;
    }
    s = g3a_mul(_matrix_scale.x, _scrw);
    t = g3a_mul(_matrix_scale.y, _scrh);
    if (s > t)
        t = s;
    return (long)(((long long)t * r) / z);
}

/* the same in x only (game 0x2821EC) */
long g3_screen_size_x(fix r, fix z)
{
    if (z < -g3a_mul(r, _view_zoom))
        return -1;
    if (z < 0)
        return _scrw + _scrw;
    return (long)(((long long)g3a_mul(_matrix_scale.x, _scrw) * r) / z);
}

/* the same in y only (game 0x282111) */
long g3_screen_size_y(fix r, fix z)
{
    if (z < -g3a_mul(r, _view_zoom))
        return -1;
    if (z < 0)
        return _scrh + _scrh;
    return (long)(((long long)g3a_mul(_matrix_scale.y, _scrh) * r) / z);
}

/* g3d_light_shift = 8 - log2(n) (n = number of light levels).  Returns what is left in eax
 * (n halved down to 1, or n itself when n <= 1).  Unreferenced. */
long g3_set_light_shift(long n)
{
    int bits = 0;
    while (n > 1) {
        n = (n - (n >> 31)) >> 1;       /* signed n / 2 */
        bits++;
    }
    g3d_light_shift = (uchar)(8 - bits);
    return n;
}

/* g3d_light_max = (n << 16) / pow(2.0, 8 - g3d_light_shift) - 1.  The original computes the
 * power with the x87 (CRT pow 0x314F28 on 2.0 at 0x357204, chopped by __CHP 0x315080); for
 * an integer exponent that is exactly 1 << k for k = 0..8 and chops to 0 for a negative k
 * (light shift > 8), where the division then faults.  Unreferenced. */
long g3_set_light_max(long n)
{
    long k = 8 - g3d_light_shift;
    volatile long p = (k >= 0) ? (1L << k) : 0;
    g3d_light_max = (n << 16) / p - 1;
    return g3d_light_max;
}

/* dest = a x b with fix_mul per product (the components are written in order, so dest may
 * alias a or b with the same result as the original).  Called by skel and the game. */
void g3_cross_product(g3s_vector *dest, g3s_vector *a, g3s_vector *b)
{
    dest->x = g3a_mul(a->y, b->z) - g3a_mul(a->z, b->y);
    dest->y = g3a_mul(a->z, b->x) - g3a_mul(a->x, b->z);
    dest->z = g3a_mul(a->x, b->y) - g3a_mul(a->y, b->x);
}
