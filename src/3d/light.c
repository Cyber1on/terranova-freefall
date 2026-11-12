/* light.c -- x:\prj\tech\libsrc\3d\light.asm (Terra Nova v1.09; RCS string at 0x3621DC):
 * lighting of points and normals.  Decompiled from __FF.EXE (hand-written asm in the original).
 *
 *   0x2F88CC g3_set_light_src        0x2F88D8 g3_rotate_light_src (name ours)
 *   0x2F88EA g3_eval_vec_light       0x2F8927 g3_trans_loc_light
 *   0x2F893E g3_eval_loc_light       0x2F89A2 g3_eval_view
 *   0x2F8A11 g3_eval_ldotv           0x2F8A28 g3_eval_light_obj_cen
 *   0x2F8A66 g3_eval_view_ahead      0x2F8A91 check_for_near
 *   0x2F8AC8 g3_light_diff           0x2F8B04 g3_light_spec
 *   0x2F8B7D g3_light_dands          0x2F8C09 g3_light
 *
 * Registers (original): g3_set_light_src eax = vector; g3_rotate_light_src edi = matrix;
 * g3_eval_loc_light / g3_eval_view eax = position; g3_eval_light_obj_cen takes the point in edi
 * (an accident of the Shock code it came from, see there); g3_light* eax = norm, edx = pos,
 * result in eax.  All of them may change every register.
 *
 * Differences from System Shock's light.asm (as transcribed in LIGHT.C):
 *  - g3_eval_vec_light does not rotate: light_vec = light_src * diff_light.
 *  - The lighting routines return the value in eax and do not store it into the point; the
 *    result is clamped (unsigned compare) to g3d_light_max (0x36243B).  Only the norm (tmp1)
 *    is used; LT_GOUR is ignored except by g3_light_dands's stack quirk below.
 *  - g3_light dispatches on (light_type >> 1) & 3: 1 diff, 2 spec, otherwise dands.
 * Quirks kept:
 *  - g3_eval_loc_light computes and normalizes the local light vector, then jumps to
 *    g3_eval_vec_light, which overwrites it with light_src * diff_light.
 *  - check_for_near calls g3_eval_ldotv on the zero flag left by the code before it: only after
 *    g3_eval_view, and only when the last product of g3_eval_view (view_vec.z) is non-zero.
 *  - g3_trans_loc_light gets a point from g3_rotate_point and never frees it.
 * Quirk not reproducible in C: g3_light_dands pushes eax (edx with LT_GOUR) on entry and never
 * pops it, so its final `ret` jumps to the norm (pos) point as if it were code.  The C does what
 * was meant and returns.  (Verified by placing a `ret` opcode in the first byte of that point.)
 */
#include "3d_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
g3s_point *g3d_light_tmp1, *g3d_light_tmp2;
#endif

/* Sets the light source (a vector, or a point with LT_LOC_LIGHT). */
void g3_set_light_src(g3s_vector *l)
{
    _g3d_light_src = *l;
}

/* Rotates the light source in place by matrix m. */
void g3_rotate_light_src(g3s_matrix *m)
{
    g3b_vec_rotate(&_g3d_light_src, &_g3d_light_src, m);
}

/* light_vec = light_src scaled by the diffuse intensity (call once per frame). */
void g3_eval_vec_light(void)
{
    _g3d_light_vec.x = fix_mul(_g3d_light_src.x, _g3d_diff_light);
    _g3d_light_vec.y = fix_mul(_g3d_light_src.y, _g3d_diff_light);
    _g3d_light_vec.z = fix_mul(_g3d_light_src.z, _g3d_diff_light);
}

/* Transforms a local light source into viewer coordinates (light_trans). */
void g3_trans_loc_light(void)
{
    g3s_point *p = g3b_rotate_point(&_g3d_light_src);   /* never freed (as in Shock) */

    _g3d_light_trans = *(g3s_vector *)p;
}

/* Light vector from a local light to pos (viewer coordinates), unscaled and normalized ...
 * and then thrown away: the asm ends with a jump to g3_eval_vec_light. */
void g3_eval_loc_light(g3s_vector *pos)
{
    _g3d_light_vec.x = fix_div(-(pos->x - _g3d_light_trans.x), _matrix_scale.x);
    _g3d_light_vec.y = fix_div(-(pos->y - _g3d_light_trans.y), _matrix_scale.y);
    _g3d_light_vec.z = fix_div(-(pos->z - _g3d_light_trans.z), _matrix_scale.z);
    g3b_vec_normalize(&_g3d_light_vec);
    g3_eval_vec_light();
}

/* View vector at pos: normalized (pos - view_position) times -spec_light. */
void g3_eval_view(g3s_vector *pos)
{
    fix s;

    _g3d_view_vec.x = pos->x - _view_position.x;
    _g3d_view_vec.y = pos->y - _view_position.y;
    _g3d_view_vec.z = pos->z - _view_position.z;
    g3b_vec_normalize(&_g3d_view_vec);
    s = -_g3d_spec_light;
    _g3d_view_vec.x = fix_mul(_g3d_view_vec.x, s);
    _g3d_view_vec.y = fix_mul(_g3d_view_vec.y, s);
    _g3d_view_vec.z = fix_mul(_g3d_view_vec.z, s);
}

/* ldotv = light_vec . view_vec */
void g3_eval_ldotv(void)
{
    _g3d_ldotv = g3b_vec_dotprod(&_g3d_light_vec, &_g3d_view_vec);
}

/* Evaluates the light (and view, with LT_SPEC) at the object centre.  pnt is whatever the
 * caller left in edi (Shock's version had the g3_rotate_point that set it commented out). */
void g3_eval_light_obj_cen(g3s_point *pnt)
{
    if (!(_g3d_light_type & (LT_LOC_LIGHT | LT_SPEC))) {
        g3_eval_vec_light();
        return;
    }
    if (_g3d_light_type & LT_LOC_LIGHT)
        g3_eval_loc_light((g3s_vector *)pnt);
    else
        g3_eval_vec_light();
    if (_g3d_light_type & LT_SPEC) {
        g3_eval_view(&g3d_obj_cen);
        g3_eval_ldotv();
    }
}

/* Cheap view vector straight ahead (0, 0, -1/16). */
void g3_eval_view_ahead(void)
{
    _g3d_view_vec.x = 0;
    _g3d_view_vec.y = 0;
    _g3d_view_vec.z = -0x1000;
    _g3d_ldotv = -_g3d_light_vec.z;
}

/* Re-evaluates the near light / near view at tmp2. */
void check_for_near(void)
{
    if (!(_g3d_light_type & (LT_NEAR_LIGHT | LT_NEAR_VIEW)))
        return;
    if (_g3d_light_type & LT_NEAR_LIGHT)
        g3_eval_loc_light((g3s_vector *)g3d_light_tmp2);
    if (_g3d_light_type & LT_NEAR_VIEW) {
        g3_eval_view((g3s_vector *)g3d_light_tmp2);
        /* the asm tests the zero flag of g3_eval_view's last multiply */
        if (_g3d_view_vec.z != 0)
            g3_eval_ldotv();
    }
}

/* clamp: unsigned compare against the table maximum */
static fix light_clamp(fix l)
{
    if ((ulong)l >= (ulong)g3d_light_max)
        l = g3d_light_max;
    return l;
}

/* diffuse: max(light_vec . norm, 0) + ambient */
static fix light_diff_raw(g3s_point *norm)
{
    fix l = g3b_vec_dotprod(&_g3d_light_vec, (g3s_vector *)norm);

    if (l < 0)
        l = 0;
    return light_clamp(l + _g3d_amb_light);
}

/* specular: 2 (s.v)(s.l) - l.v, zero under the flash threshold, + ambient */
static fix light_spec_raw(g3s_point *norm)
{
    fix l = g3b_vec_dotprod(&_g3d_light_vec, (g3s_vector *)norm);

    if (l < 0)
        return _g3d_amb_light;
    _g3d_sdotl = l;
    _g3d_sdotv = g3b_vec_dotprod(&_g3d_view_vec, (g3s_vector *)g3d_light_tmp1);
    l = fix_mul(_g3d_sdotv << 1, _g3d_sdotl) - _g3d_ldotv;
    if (l < _g3d_flash)
        l = 0;
    return light_clamp(l + _g3d_amb_light);
}

/* diffuse and specular */
static fix light_dands_raw(g3s_point *norm)
{
    fix l = g3b_vec_dotprod(&_g3d_light_vec, (g3s_vector *)norm);

    if (l < 0)
        return _g3d_amb_light;
    _g3d_sdotl = l;
    _g3d_sdotv = g3b_vec_dotprod(&_g3d_view_vec, (g3s_vector *)g3d_light_tmp1);
    l = (fix_mul(_g3d_sdotv, _g3d_sdotl) << 1) - _g3d_ldotv;
    if (l < _g3d_flash)
        l = 0;
    return light_clamp(l + _g3d_sdotl + _g3d_amb_light);
}

fix g3_light_diff(g3s_point *norm, g3s_point *pos)
{
    g3d_light_tmp1 = norm;
    g3d_light_tmp2 = pos;
    check_for_near();
    return light_diff_raw(g3d_light_tmp1);
}

fix g3_light_spec(g3s_point *norm, g3s_point *pos)
{
    g3d_light_tmp1 = norm;
    g3d_light_tmp2 = pos;
    check_for_near();
    return light_spec_raw(g3d_light_tmp1);
}

/* See the header comment: the original returns into the point (norm, or pos with LT_GOUR). */
fix g3_light_dands(g3s_point *norm, g3s_point *pos)
{
    g3d_light_tmp1 = norm;
    g3d_light_tmp2 = pos;
    check_for_near();
    return light_dands_raw(g3d_light_tmp1);
}

/* Lights norm (at pos) according to _g3d_light_type. */
fix g3_light(g3s_point *norm, g3s_point *pos)
{
    g3d_light_tmp1 = norm;
    g3d_light_tmp2 = pos;
    if (_g3d_light_type & (LT_NEAR_LIGHT | LT_NEAR_VIEW))
        check_for_near();
    switch ((_g3d_light_type >> 1) & 3) {
    case 1:
        return light_diff_raw(g3d_light_tmp1);
    case 2:
        return light_spec_raw(g3d_light_tmp1);
    default:
        return light_dands_raw(g3d_light_tmp1);
    }
}
