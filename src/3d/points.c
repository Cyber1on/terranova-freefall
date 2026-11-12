/* points.c -- x:\prj\tech\libsrc\3d\points.asm (Terra Nova v1.09): rotating, projecting and
 * offsetting points.  Decompiled from __FF.EXE (hand-written asm in the original).  Names
 * follow System Shock's POINTS.C / 3D.H.
 *
 *   0x2F41D8 g3_rotate_norm         0x2F4240 g3_world_to_object     0x2F42F5 g3_rotate_point
 *   0x2F431E g3_transform_point     0x2F4323 g3_project_point       0x2F4389 g3_transform_list
 *   0x2F43C0 g3_rotate_list         0x2F43F4 g3_project_list        0x2F4422 g3_rotate_light_norm
 *   0x2F444C rotate_norm            0x2F445C do_norm_rotate         0x2F44EB xlate_rotate_point
 *   0x2F450D do_rotate              0x2F459C g3_rotate_delta_x      0x2F45C9 g3_rotate_delta_y
 *   0x2F45F6 g3_rotate_delta_z      0x2F4623 g3_rotate_delta_xz     0x2F4692 g3_rotate_delta_xy
 *   0x2F4701 g3_rotate_delta_yz     0x2F4770 g3_rotate_delta_xyz    0x2F4788 g3_rotate_delta_v
 *   0x2F47A2 g3_copy_add_delta_v    0x2F47D4 g3_add_delta_v         0x2F47ED g3_add_delta_x
 *   0x2F4822 g3_add_delta_y         0x2F4857 g3_add_delta_z         0x2F488C g3_add_delta_xy
 *   0x2F4903 g3_add_delta_xz        0x2F497A g3_add_delta_yz        0x2F49F1 g3_add_delta_xyz
 *   0x2F4A11 g3_copy_add_delta_x    0x2F4A60 g3_copy_add_delta_y    0x2F4AAF g3_copy_add_delta_z
 *   0x2F4AFE g3_replace_add_delta_x 0x2F4B3B g3_replace_add_delta_y 0x2F4B78 g3_replace_add_delta_z
 *   0x2F4BB5 g3_copy_add_delta_xy   0x2F4C4E g3_copy_add_delta_xz   0x2F4CE8 g3_copy_add_delta_yz
 *   0x2F4D82 g3_copy_add_delta_xyz
 *
 * Register conventions of the original entries (C versions are cdecl, argument order of
 * Shock's 3D.H):
 *   vectors in esi, new points returned in edi (with bl = codes from code_point where the
 *   entry ends in code_point), deltas in eax/ebx/ecx (dx, dy, dz in that order of the
 *   letters in the name), points to change in edi, delta vectors in esi (add_delta_v) or ebx
 *   (copy_add_delta_v), rotate_delta destinations in edi.  replace_add_delta_*: esi=src,
 *   edi=dst, eax=delta.  Lists: ecx=n, edi=dest list, esi=src (project_list: esi=list) ->
 *   ax = g_codes (or in al, and in ah).  g3_project_point: edi=point -> eax.
 *   The cores: do_rotate/do_norm_rotate take x, y, z in esi, edi, ebp and return them in
 *   ecx, esi, eax; rotate_norm and xlate_rotate_point take esi=vector.
 *   g3_world_to_object: esi=world vector -> edi=vector (TN, name ours).
 *   The new-point entries take the point with no check: with an empty free list they write
 *   to address 0 (original behaviour, kept).
 */
#include "3d_a.h"

/* first_free pop without check, as every asm entry does it */
static g3s_point *alloc_pt(void)
{
    g3s_point *p = first_free;
    if (p != NULL)
        first_free = p->next;
    return p;
}

/* ---------------- rotate cores ---------------- */

/* (x, y, z) * view_matrix; the x result also goes to g3d_rot_tmp */
void do_rotate(fix x, fix y, fix z, g3s_vector *r)
{
    g3s_matrix *m = &view_matrix;
    g3d_rot_tmp = g3a_dot3(x, m->m1, y, m->m4, z, m->m7);
    r->y = g3a_dot3(x, m->m2, y, m->m5, z, m->m8);
    r->z = g3a_dot3(x, m->m3, y, m->m6, z, m->m9);
    r->x = g3d_rot_tmp;
}

/* same with unscaled_matrix (for normals) */
void do_norm_rotate(fix x, fix y, fix z, g3s_vector *r)
{
    g3s_matrix *m = &unscaled_matrix;
    g3d_rot_tmp = g3a_dot3(x, m->m1, y, m->m4, z, m->m7);
    r->y = g3a_dot3(x, m->m2, y, m->m5, z, m->m8);
    r->z = g3a_dot3(x, m->m3, y, m->m6, z, m->m9);
    r->x = g3d_rot_tmp;
}

void rotate_norm(g3s_vector *v, g3s_vector *r)
{
    do_norm_rotate(v->x, v->y, v->z, r);
}

/* (v - view position) * view_matrix */
void xlate_rotate_point(g3s_vector *v, g3s_vector *r)
{
    fix y = v->y - _view_position.y;
    fix z = v->z - _view_position.z;
    fix x = v->x - _view_position.x;
    do_rotate(x, y, z, r);
}

/* ---------------- points ---------------- */

/* Rotates a normal by unscaled_matrix and undoes the view scale (TN: also y * scrw / scrh,
 * negated).  No clip codes.  Divisions fault in the original on overflow. */
g3s_phandle g3_rotate_norm(g3s_vector *v)
{
    g3s_vector r;
    fix x, y, z;
    g3s_point *p;

    rotate_norm(v, &r);
    z = g3a_div(r.z, _matrix_scale.z);
    x = g3a_div(r.x, _matrix_scale.x);
    y = g3a_div(r.y, _matrix_scale.y);
    y = (fix)(((long long)y * _scrw) / _scrh);
    y = -y;
    p = alloc_pt();
    p->x = x;
    p->y = y;
    p->z = z;
    p->p3_flags = 0;
    return p;
}

/* TN: world vector -> object space through _wtoo_matrix / _wtoo_position (name ours). */
void g3_world_to_object(g3s_vector *dest, g3s_vector *v)
{
    g3s_matrix *m = &_wtoo_matrix;
    fix x = v->x, y = v->y, z = v->z;
    fix ry, rz;
    g3d_rot_tmp = g3a_dot3(x, m->m1, y, m->m4, z, m->m7);
    ry = g3a_dot3(x, m->m2, y, m->m5, z, m->m8);
    rz = g3a_dot3(x, m->m3, y, m->m6, z, m->m9);
    dest->x = g3d_rot_tmp + _wtoo_position.x;
    dest->y = ry + _wtoo_position.y;
    dest->z = rz + _wtoo_position.z;
}

/* New point = v rotated into view space, with clip codes; not projected. */
g3s_phandle g3_rotate_point(g3s_vector *v)
{
    g3s_vector r;
    g3s_point *p;
    xlate_rotate_point(v, &r);
    p = alloc_pt();
    p->x = r.x;
    p->y = r.y;
    p->z = r.z;
    p->p3_flags = 0;
    code_point(p);
    return p;
}

/* rotate + project (the result of the projection is in eax, the point in edi) */
g3s_phandle g3_transform_point(g3s_vector *v)
{
    g3s_point *p = g3_rotate_point(v);
    g3_project_point(p);
    return p;
}

/* Projects a rotated point.  Returns 0 if z <= 0 (point untouched), 1 when projected.  On
 * overflow it sets CC_CLIP_OVERFLOW and returns the overflowed sum (garbage in the original;
 * sy may already be stored when sx overflows).
 * In the original the two idivs run under the division-overflow trap installed by g3_init
 * (0x35F460 temporarily points to 0x2DF9E9, ranges 0x2F4344/0x2F435B..0x2F437D) and an
 * overflowing quotient also ends at the overflow exit.  Here the quotient is checked in
 * software instead, so 0x35F460 is not touched (it is restored on every path anyway). */
#ifdef TN_WATCOM_ABI
#define G3_DIV(n, d) ((n) / (d))
#else
/* port/core/idiv64.c: these two must not saturate like the other 64-bit divisions, the overflow is
 * checked here; the original's trap also counted the fault (port/core/divtrap.c) */
long long tn_ldiv_exact(long long n, long long d);
void port_count_div_fault(void);
#define G3_DIV(n, d) tn_ldiv_exact(n, d)
#endif
int g3_project_point(g3s_phandle p)
{
    long long q;
    fix z = p->z, r;

    if (z <= 0)
        return 0;
    q = G3_DIV((long long)p->y * _scrh, z);
    if (q != (long)q)
        goto overflow;
    if (__builtin_add_overflow(-(long)q, _biasy, &r))
        goto overflow_r;
    p->sy = r;
    q = G3_DIV((long long)p->x * _scrw, z);
    if (q != (long)q)
        goto overflow;
    if (__builtin_add_overflow((long)q, _biasx, &r))
        goto overflow_r;
    p->sx = r;
    p->p3_flags |= PF_PROJECTED;
    return 1;
overflow:
#ifndef TN_WATCOM_ABI
    port_count_div_fault();
#endif
    r = 0;                              /* trap path: eax is whatever the handler leaves */
overflow_r:
    p->codes |= CC_CLIP_OVERFLOW;
    return r;
}

/* Transforms n vectors (12 bytes apart) into new points.  Returns g_codes (al = or of all
 * codes, ah = and).  n is not checked: n = 0 runs 2^32 times. */
int g3_transform_list(int n, g3s_phandle *dest_list, g3s_vector *v)
{
    g_codes.or = 0;
    g_codes.and = 0xFF;
    do {
        g3s_point *p = g3_transform_point(v);
        g_codes.or |= p->codes;
        g_codes.and &= p->codes;
        *dest_list++ = p;
        v++;
    } while (--n != 0);
    return *(ushort *)&g_codes;
}

int g3_rotate_list(int n, g3s_phandle *dest_list, g3s_vector *v)
{
    g_codes.or = 0;
    g_codes.and = 0xFF;
    do {
        g3s_point *p = g3_rotate_point(v);
        g_codes.or |= p->codes;
        g_codes.and &= p->codes;
        *dest_list++ = p;
        v++;
    } while (--n != 0);
    return *(ushort *)&g_codes;
}

/* Projects n points; the codes are accumulated before each projection. */
int g3_project_list(int n, g3s_phandle *point_list)
{
    g_codes.or = 0;
    g_codes.and = 0xFF;
    do {
        g3s_point *p = *point_list++;
        g_codes.or |= p->codes;
        g_codes.and &= p->codes;
        g3_project_point(p);
    } while (--n != 0);
    return *(ushort *)&g_codes;
}

/* New point = v * view_matrix (no translation, no flags or codes set). */
g3s_phandle g3_rotate_light_norm(g3s_vector *v)
{
    g3s_vector r;
    g3s_point *p;
    do_rotate(v->x, v->y, v->z, &r);
    p = alloc_pt();
    p->x = r.x;
    p->y = r.y;
    p->z = r.z;
    return p;
}

/* ---------------- deltas rotated into view space ---------------- */

void g3_rotate_delta_x(g3s_vector *dest, fix dx)
{
    dest->x = g3a_mul(dx, view_matrix.m1);
    dest->y = g3a_mul(dx, view_matrix.m2);
    dest->z = g3a_mul(dx, view_matrix.m3);
}

void g3_rotate_delta_y(g3s_vector *dest, fix dy)
{
    dest->x = g3a_mul(dy, view_matrix.m4);
    dest->y = g3a_mul(dy, view_matrix.m5);
    dest->z = g3a_mul(dy, view_matrix.m6);
}

void g3_rotate_delta_z(g3s_vector *dest, fix dz)
{
    dest->x = g3a_mul(dz, view_matrix.m7);
    dest->y = g3a_mul(dz, view_matrix.m8);
    dest->z = g3a_mul(dz, view_matrix.m9);
}

void g3_rotate_delta_xz(g3s_vector *dest, fix dx, fix dz)
{
    dest->x = g3a_dot2(dx, view_matrix.m1, dz, view_matrix.m7);
    dest->y = g3a_dot2(dx, view_matrix.m2, dz, view_matrix.m8);
    dest->z = g3a_dot2(dx, view_matrix.m3, dz, view_matrix.m9);
}

void g3_rotate_delta_xy(g3s_vector *dest, fix dx, fix dy)
{
    dest->x = g3a_dot2(dx, view_matrix.m1, dy, view_matrix.m4);
    dest->y = g3a_dot2(dx, view_matrix.m2, dy, view_matrix.m5);
    dest->z = g3a_dot2(dx, view_matrix.m3, dy, view_matrix.m6);
}

void g3_rotate_delta_yz(g3s_vector *dest, fix dy, fix dz)
{
    dest->x = g3a_dot2(dy, view_matrix.m4, dz, view_matrix.m7);
    dest->y = g3a_dot2(dy, view_matrix.m5, dz, view_matrix.m8);
    dest->z = g3a_dot2(dy, view_matrix.m6, dz, view_matrix.m9);
}

void g3_rotate_delta_xyz(g3s_vector *dest, fix dx, fix dy, fix dz)
{
    g3s_vector r;
    do_rotate(dx, dy, dz, &r);
    dest->x = r.x;
    dest->y = r.y;
    dest->z = r.z;
}

void g3_rotate_delta_v(g3s_vector *dest, g3s_vector *src)
{
    g3s_vector r;
    do_rotate(src->x, src->y, src->z, &r);
    dest->x = r.x;
    dest->y = r.y;
    dest->z = r.z;
}

/* ---------------- adding deltas to points ---------------- */

/* new point = src + delta (delta already in view space) */
g3s_phandle g3_copy_add_delta_v(g3s_phandle src, g3s_vector *delta)
{
    g3s_point *p = alloc_pt();
    p->x = src->x + delta->x;
    p->y = src->y + delta->y;
    p->z = src->z + delta->z;
    p->p3_flags = 0;
    code_point(p);
    return p;
}

/* The add_delta entries clear PF_PROJECTED and recode the point. */
void g3_add_delta_v(g3s_phandle p, g3s_vector *delta)
{
    p->x += delta->x;
    p->y += delta->y;
    p->z += delta->z;
    p->p3_flags &= ~PF_PROJECTED;
    code_point(p);
}

void g3_add_delta_x(g3s_phandle p, fix dx)
{
    p->x += g3a_mul(dx, view_matrix.m1);
    p->y += g3a_mul(dx, view_matrix.m2);
    p->z += g3a_mul(dx, view_matrix.m3);
    p->p3_flags &= ~PF_PROJECTED;
    code_point(p);
}

void g3_add_delta_y(g3s_phandle p, fix dy)
{
    p->x += g3a_mul(dy, view_matrix.m4);
    p->y += g3a_mul(dy, view_matrix.m5);
    p->z += g3a_mul(dy, view_matrix.m6);
    p->p3_flags &= ~PF_PROJECTED;
    code_point(p);
}

void g3_add_delta_z(g3s_phandle p, fix dz)
{
    p->x += g3a_mul(dz, view_matrix.m7);
    p->y += g3a_mul(dz, view_matrix.m8);
    p->z += g3a_mul(dz, view_matrix.m9);
    p->p3_flags &= ~PF_PROJECTED;
    code_point(p);
}

void g3_add_delta_xy(g3s_phandle p, fix dx, fix dy)
{
    p->x += g3a_dot2(dx, view_matrix.m1, dy, view_matrix.m4);
    p->y += g3a_dot2(dx, view_matrix.m2, dy, view_matrix.m5);
    p->z += g3a_dot2(dx, view_matrix.m3, dy, view_matrix.m6);
    p->p3_flags &= ~PF_PROJECTED;
    code_point(p);
}

void g3_add_delta_xz(g3s_phandle p, fix dx, fix dz)
{
    p->x += g3a_dot2(dx, view_matrix.m1, dz, view_matrix.m7);
    p->y += g3a_dot2(dx, view_matrix.m2, dz, view_matrix.m8);
    p->z += g3a_dot2(dx, view_matrix.m3, dz, view_matrix.m9);
    p->p3_flags &= ~PF_PROJECTED;
    code_point(p);
}

void g3_add_delta_yz(g3s_phandle p, fix dy, fix dz)
{
    p->x += g3a_dot2(dy, view_matrix.m4, dz, view_matrix.m7);
    p->y += g3a_dot2(dy, view_matrix.m5, dz, view_matrix.m8);
    p->z += g3a_dot2(dy, view_matrix.m6, dz, view_matrix.m9);
    p->p3_flags &= ~PF_PROJECTED;
    code_point(p);
}

void g3_add_delta_xyz(g3s_phandle p, fix dx, fix dy, fix dz)
{
    g3s_vector r;
    do_rotate(dx, dy, dz, &r);
    p->x += r.x;
    p->y += r.y;
    p->z += r.z;
    p->p3_flags &= ~PF_PROJECTED;
    code_point(p);
}

/* new point = src + delta along one axis */
g3s_phandle g3_copy_add_delta_x(g3s_phandle src, fix dx)
{
    g3s_point *p = alloc_pt();
    p->x = g3a_mul(dx, view_matrix.m1) + src->x;
    p->y = g3a_mul(dx, view_matrix.m2) + src->y;
    p->z = g3a_mul(dx, view_matrix.m3) + src->z;
    p->p3_flags = 0;
    code_point(p);
    return p;
}

g3s_phandle g3_copy_add_delta_y(g3s_phandle src, fix dy)
{
    g3s_point *p = alloc_pt();
    p->x = g3a_mul(dy, view_matrix.m4) + src->x;
    p->y = g3a_mul(dy, view_matrix.m5) + src->y;
    p->z = g3a_mul(dy, view_matrix.m6) + src->z;
    p->p3_flags = 0;
    code_point(p);
    return p;
}

g3s_phandle g3_copy_add_delta_z(g3s_phandle src, fix dz)
{
    g3s_point *p = alloc_pt();
    p->x = g3a_mul(dz, view_matrix.m7) + src->x;
    p->y = g3a_mul(dz, view_matrix.m8) + src->y;
    p->z = g3a_mul(dz, view_matrix.m9) + src->z;
    p->p3_flags = 0;
    code_point(p);
    return p;
}

/* dst = src + delta (dst is an existing point) */
g3s_phandle g3_replace_add_delta_x(g3s_phandle src, g3s_phandle dst, fix dx)
{
    dst->x = g3a_mul(dx, view_matrix.m1) + src->x;
    dst->y = g3a_mul(dx, view_matrix.m2) + src->y;
    dst->z = g3a_mul(dx, view_matrix.m3) + src->z;
    dst->p3_flags = 0;
    code_point(dst);
    return dst;
}

g3s_phandle g3_replace_add_delta_y(g3s_phandle src, g3s_phandle dst, fix dy)
{
    dst->x = g3a_mul(dy, view_matrix.m4) + src->x;
    dst->y = g3a_mul(dy, view_matrix.m5) + src->y;
    dst->z = g3a_mul(dy, view_matrix.m6) + src->z;
    dst->p3_flags = 0;
    code_point(dst);
    return dst;
}

g3s_phandle g3_replace_add_delta_z(g3s_phandle src, g3s_phandle dst, fix dz)
{
    dst->x = g3a_mul(dz, view_matrix.m7) + src->x;
    dst->y = g3a_mul(dz, view_matrix.m8) + src->y;
    dst->z = g3a_mul(dz, view_matrix.m9) + src->z;
    dst->p3_flags = 0;
    code_point(dst);
    return dst;
}

/* new point = src + delta in two axes (the rotated delta is stored first, then src added) */
g3s_phandle g3_copy_add_delta_xy(g3s_phandle src, fix dx, fix dy)
{
    g3s_point *p = alloc_pt();
    p->x = g3a_dot2(dx, view_matrix.m1, dy, view_matrix.m4);
    p->y = g3a_dot2(dx, view_matrix.m2, dy, view_matrix.m5);
    p->z = g3a_dot2(dx, view_matrix.m3, dy, view_matrix.m6);
    p->x += src->x;
    p->y += src->y;
    p->z += src->z;
    p->p3_flags = 0;
    code_point(p);
    return p;
}

g3s_phandle g3_copy_add_delta_xz(g3s_phandle src, fix dx, fix dz)
{
    g3s_point *p = alloc_pt();
    p->x = g3a_dot2(dx, view_matrix.m1, dz, view_matrix.m7);
    p->y = g3a_dot2(dx, view_matrix.m2, dz, view_matrix.m8);
    p->z = g3a_dot2(dx, view_matrix.m3, dz, view_matrix.m9);
    p->x += src->x;
    p->y += src->y;
    p->z += src->z;
    p->p3_flags = 0;
    code_point(p);
    return p;
}

g3s_phandle g3_copy_add_delta_yz(g3s_phandle src, fix dy, fix dz)
{
    g3s_point *p = alloc_pt();
    p->x = g3a_dot2(dy, view_matrix.m4, dz, view_matrix.m7);
    p->y = g3a_dot2(dy, view_matrix.m5, dz, view_matrix.m8);
    p->z = g3a_dot2(dy, view_matrix.m6, dz, view_matrix.m9);
    p->x += src->x;
    p->y += src->y;
    p->z += src->z;
    p->p3_flags = 0;
    code_point(p);
    return p;
}

g3s_phandle g3_copy_add_delta_xyz(g3s_phandle src, fix dx, fix dy, fix dz)
{
    g3s_vector r;
    g3s_point *p;
    do_rotate(dx, dy, dz, &r);
    p = alloc_pt();
    p->x = r.x + src->x;
    p->y = r.y + src->y;
    p->z = r.z + src->z;
    p->p3_flags = 0;
    code_point(p);
    return p;
}
