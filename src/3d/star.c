/* star.c -- x:\prj\tech\libsrc\3d\star.c (Terra Nova v1.09): the starfield.  Decompiled from
 * __FF.EXE (Watcom C in the original; not in the System Shock sources).  g3_star_set,
 * g3_star_alloc, g3_star_free, g3_star_render and g3_star_random are LG names (the game's
 * calls); the rest are ours.
 *
 *   0x2F99E0 g3_star_set          0x2F99F4 g3_star_alloc        0x2F9A3C g3_star_free
 *   0x2F9A48 star_dist            0x2F9AAC g3_star_poly         0x2F9B10 g3_star_clip_poly
 *   0x2F9B6C star_reset_bounds    0x2F9B84 star_pixel           0x2F9BC8 star_color
 *   0x2F9C90 star_build_table     0x2F9D38 g3_star_render       0x2F9F10 g3_star_random
 *
 * All Watcom register convention; star_pixel has a fifth argument on the stack (ret 4).
 *
 * Stars are unit vectors stored as 3 shorts (fix / 2).  g3_star_render draws only when a
 * sky polygon was seen this frame: g3_star_poly / g3_star_clip_poly record the nearest z
 * (std_min_z, 0x7FFFFFFF = none) and the farthest scaled distance (std_max_z) of the sky
 * polygon's points; star_reset_bounds forces drawing.  The object scale is set to
 * sqrt(std_max_z) so the stars lie just beyond the polygon, and stars nearer than std_min_z
 * are dropped by star_transform.  Stars land only on pixels that are still 0 (empty sky).
 */
#include "3d_a.h"

#ifndef TN_WATCOM_ABI
/* port/video/sharp3d.c: while the sharp 3D view is drawn, a pixel of the game's view is a kx x ky
 * block there; the stars keep the size they have in the game's view */
extern int port_s3d_on, port_s3d_kx, port_s3d_ky, port_s3d_low_w;
void port_s3d_block(long c, long x, long y, int only0);
#endif

/* Sets the star arrays directly (unreferenced). */
void g3_star_set(long n, g3s_star *vec, uchar *col)
{
    std_num = n;
    std_vec = vec;
    std_col = col;
}

/* Allocates n stars (6 bytes of vector + 1 colour byte each).  Returns n, or -1. */
long g3_star_alloc(long n)
{
    uchar *p = malloc(n * 7);
    std_vec = (g3s_star *)p;
    if (p == NULL)
        return -1;
    std_num = n;
    std_col = p + n * 6;
    return n;
}

void g3_star_free(void)
{
    free(std_vec);
}

/* x^2 + y^2 + z^2 of a rotated point with the view scale undone (fix_div then fix_mul). */
fix star_dist(g3s_vector *v)
{
    fix x = g3a_div(v->x, _matrix_scale.x);
    fix y = g3a_div(v->y, _matrix_scale.y);
    fix z = g3a_div(v->z, _matrix_scale.z);
    fix s = g3a_mul(x, x);
    s += g3a_mul(y, y);
    return g3a_mul(z, z) + s;
}

/* bounds of a sky polygon's points */
static void star_bounds(g3s_point *p)
{
    fix d;
    if (p->z < std_min_z)
        std_min_z = p->z;
    d = star_dist((g3s_vector *)p);
    if (d > std_max_z)
        std_max_z = d;
}

/* Draws a sky polygon in colour 0 and records the bounds of its clipped points
 * (g3_draw_poly leaves them in the polygon module's clipped list).  Unreferenced. */
void g3_star_poly(long n, g3s_phandle *list)
{
    long i;
    if (g3a_asm_call(G3_ORIG_DRAW_POLY, 0, 0, n, 0, (long)list, 0) == 0x10)
        return;
    for (i = 0; i < g3d_poly_n; i++)
        star_bounds(g3d_poly_clipped[i]);
}

/* Clips a sky polygon (into a 10-entry buffer) and records the bounds of the result.
 * The clip points are not freed (as in the original).  Unreferenced. */
void g3_star_clip_poly(long n, g3s_phandle *list)
{
    g3s_phandle buf[10];
    long i, m;
    m = g3a_asm_call(G3_ORIG_CLIP_POLYGON, 0, 0, n, 0, (long)list, (long)buf);
    for (i = 0; i < m; i++)
        star_bounds(buf[i]);
}

/* Draw the stars this frame, whatever the sky polygons. */
void star_reset_bounds(void)
{
    std_min_z = 0;
    std_max_z = FIX_UNIT;
}

/* Puts colour std_shade[(a * b * c) >> 16] at (x, y) if the pixel there is 0. */
void star_pixel(short x, short y, long a, long b, long c)
{
#ifndef TN_WATCOM_ABI
    if (port_s3d_on) {                  /* x, y: a pixel of the game's view */
        port_s3d_block(std_shade[(long)((ulong)a * b * c) >> 16], x * port_s3d_kx + port_s3d_kx / 2,
                       y * port_s3d_ky + port_s3d_ky / 2, 1);
        return;
    }
#endif
    if (WATCOM_CALL(grd_pixel_table[3], x, y) != 0)
        return;
    gen_fill_pixel(std_shade[(long)((ulong)a * b * c) >> 16], x, y);
}

/* Antialiased star: spreads the brightness over the 2x2 pixels around (sx, sy) by the
 * fractional position.  c = the star's colour (std_color_base .. base + range - 1). */
void star_color(fix sx, fix sy, long c)
{
    long fx = (sx & 0xFFFF) >> 8;
    long fy = (sy & 0xFFFF) >> 8;
    long t = std_color_base + std_color_range - 1 - c;
    long k = (t * 255) / (std_color_range + 1);
    short x, y;
#ifndef TN_WATCOM_ABI
    if (port_s3d_on) {                  /* where it is in the game's view */
        sx /= port_s3d_kx;
        sy /= port_s3d_ky;
        fx = (sx & 0xFFFF) >> 8;
        fy = (sy & 0xFFFF) >> 8;
    }
#endif
    x = (short)(sx >> 16);
    y = (short)(sy >> 16);
    star_pixel(x, y, 256 - fx, 256 - fy, k);
    star_pixel(x + 1, y, fx, 256 - fy, k);
    star_pixel(x, y + 1, 256 - fx, fy, k);
    star_pixel(x + 1, y + 1, fx, fy, k);
}

/* std_shade[i] = base + range - 1 - (range - 1) * (i / 255)^0.4578 (fix_pow, 30000/65536) */
void star_build_table(void)
{
    long top = std_color_base + std_color_range - 1;
    fix neg = -(std_color_range - 1) << 16;
    long i;
    for (i = 0; i < 256; i++)
        std_shade[i] = (uchar)((g3a_mul(neg, fix_pow((i << 16) / 255, 0x7530)) >> 16) + top);
}

/* Draws the stars of the current frame (see the header comment), then marks the bounds as
 * empty.  Large stars (g3d_star_size > 1) are squares; otherwise the star is antialiased
 * when the canvas is at least g3d_star_minw wide, else one pixel. */
void g3_star_render(void)
{
    long aa = ((short)grd_canvas->bm.w >= g3d_star_minw);
#ifndef TN_WATCOM_ABI
    if (port_s3d_on)
        aa = ((short)port_s3d_low_w >= g3d_star_minw);
#endif
    long i;

    if (std_min_z == 0x7FFFFFFF)
        return;
    if (std_min_z < 0)
        std_min_z = 0;
    g3a_asm_call(G3_ORIG_SCALE_OBJECT, fix_sqrt(std_max_z), 0, 0, 0, 0, 0);
    for (i = 0; i < std_num; i++) {
        g3s_star *s = &std_vec[i];
        g3s_vector v;
        g3s_point *p;
        v.x = s->x * 2;
        v.y = s->y * 2;
        v.z = s->z * 2;
        p = star_transform(&v);
        if (p->codes == 0) {
            long px = (p->sx + 0x8000) >> 16;
            long py = (p->sy + 0x8000) >> 16;
#ifndef TN_WATCOM_ABI
            if (port_s3d_on && (!aa || g3d_star_size > 1)) {   /* a pixel / square of the game's view */
                long n = g3d_star_size <= 1 ? 1 : g3d_star_size, xx, yy;
                px /= port_s3d_kx;
                py /= port_s3d_ky;
                for (yy = py; yy < py + n; yy++)
                    for (xx = px; xx < px + n; xx++)
                        port_s3d_block(std_col[i], xx * port_s3d_kx + port_s3d_kx / 2,
                                       yy * port_s3d_ky + port_s3d_ky / 2, 1);
            } else
#endif
            if (g3d_star_size <= 1) {
                if (aa)
                    star_color(p->sx, p->sy, std_col[i]);
                else if (WATCOM_CALL(grd_pixel_table[3], (short)px, (short)py) == 0)
                    gen_fill_pixel(std_col[i], (short)px, (short)py);
            } else {
                long xx, yy;
                for (xx = px; px + g3d_star_size > xx; xx++)
                    for (yy = py; yy < py + g3d_star_size; yy++)
                        if (WATCOM_CALL(grd_pixel_table[3], (short)xx, (short)yy) == 0)
                            gen_fill_pixel(std_col[i], (short)xx, (short)yy);
            }
        }
        g3_free_point(p);
    }
    std_min_z = 0x7FFFFFFF;
    std_max_z = 0;
}

/* Makes std_num random unit vectors (rejecting ones too near the origin) with random
 * colours color .. color + range - 1, and builds the shade table for colours 0xD0..0xDF.
 * Uses the CRT rand(). */
void g3_star_random(uchar color, uchar range)
{
    long i;
    std_color_base = 0xD0;
    std_color_range = 0x10;
    star_build_table();
    for (i = 0; i < std_num; i++) {
        g3s_star *s = &std_vec[i];
        fix x, y, z, d;
        x = ((rand() % 4000) << 8) - 0x7D000;
        y = ((rand() % 4000) << 8) - 0x7D000;
        z = ((rand() % 4000) << 8) - 0x7D000;
        d = g3a_mul(x, x);
        d += g3a_mul(y, y);
        d = g3a_mul(z, z) + d;
        if (d < 0x28F) {
            i--;
            continue;
        }
        d = fix_sqrt(d);
        x = g3a_div(x, d);
        y = g3a_div(y, d);
        z = g3a_div(z, d);
        s->x = (short)(x >> 1);
        s->y = (short)(y >> 1);
        s->z = (short)(z >> 1);
        std_col[i] = (uchar)(rand() % range + color);
    }
}
