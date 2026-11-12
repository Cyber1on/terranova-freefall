/* instance.c -- x:\prj\tech\libsrc\3d\instance.asm (Terra Nova v1.09, RCS string 0x361C5A):
 * object instancing (start/end object, matrix and context stacks).  Decompiled from __FF.EXE
 * (hand-written asm in the original; System Shock's INSTANCE.C is the C transcription).
 *
 *   0x2F7D4C g3_start_object             0x2F7DA2 g3_start_object_angles_xyz
 *   0x2F7DC0 g3_start_object_angles_v    0x2F7E79 g3_start_object_angles_x
 *   0x2F7E9A instance_x                  0x2F8022 g3_start_object_angles_y
 *   0x2F8074 instance_y                  0x2F836E g3_start_object_angles_z
 *   0x2F8393 instance_z                  0x2F851B g3_start_object_angles_xy
 *   0x2F8562 g3_start_object_angles_yz   0x2F85A9 g3_start_object_angles_xz
 *   0x2F85F0 g3_start_object_angles_zx (name ours)
 *   0x2F8621 g3_start_object_matrix      0x2F86BA save_context
 *   0x2F8705 g3_scale_object             0x2F8864 g3_end_object
 *   0x2F88AF instance_matrix
 *
 * Register conventions (the C arguments in this order): start_object esi=pos; angles_xyz
 * esi, ax, bx, dx, ecx=order; angles_v esi, edi=angvec, ecx; angles_x/y/z esi, bx; the two-angle
 * forms esi, bx, dx, ecx; matrix esi, edi=matrix; scale_object eax; instance_x/y/z bx.
 * The start routines return eax = -1 (0 when save_context fails); save_context returns CF.
 *
 * TN differences from Shock (kept): the context stack holds the view matrix + position (0x30)
 * and 27 dwords of light state from _g3d_light_src (0x6C), depth limit 8.  The world-to-object
 * transform (_wtoo_*) is updated by start_object, angles_v/xyz, angles_y and matrix, and
 * instance_y also rotates rows 2..3 of _wtoo_matrix and light_src y,z; instance_x/z and the
 * two-angle forms leave _wtoo_* alone.  TN's axis naming differs from Shock's: the routine at
 * Shock's instance_x position rotates x/z (Shock's instance_y), and the one at the instance_y
 * position rotates y/z (Shock's instance_x); the names here follow the TN positions.
 * The two-angle forms take (bx, dx) as:
 *   xy: order & 1 clear: instance_y(bx), instance_x(dx); set: instance_x(dx), instance_y(bx)
 *   yz: order & 2 clear: instance_y(bx), instance_z(dx); set: instance_z(dx), instance_y(bx)
 *   xz: order & 4 clear: instance_x(bx), instance_z(dx); set: instance_z(dx), instance_x(bx)
 *   zx: instance_z(bx), instance_x(dx)
 * g3_end_object at depth 0 does nothing.  g3_scale_object divides with idiv (overflow traps).
 * The temporaries live at 0x362168..0x3621D9 as in the original.
 */
#include "3d_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
g3s_mstack_entry g3d_matrix_stack[G3_MAX_DEPTH];
g3s_mstack_entry *g3d_matrix_stackp = g3d_matrix_stack;
ulong g3d_context_stack[G3_MAX_DEPTH][27];
void *g3d_context_stackp = g3d_context_stack;
long g3d_stack_depth;
g3s_matrix inst_temp_matrix, inst_temp_matrix2;
g3s_angvec inst_temp_angles;
g3s_vector inst_temp_vec;
fix inst_sin_y, inst_cos_y, inst_sin_x, inst_cos_x, inst_sin_z, inst_cos_z;
#endif

/* (a*b - c*d) >> 16 and (a*b + c*d) >> 16 on 64-bit products, low 32 bits kept */
#define MSUB(a, b, c, d) ((fix)((((long long)(a) * (b)) - ((long long)(c) * (d))) >> 16))
#define MADD(a, b, c, d) ((fix)((((long long)(a) * (b)) + ((long long)(c) * (d))) >> 16))

static void vec_rotate(void *dest, void *src, void *m)
{
    tn3c_regs r = { 0 };
    r.ebx = (ulong)dest; r.esi = (ulong)src; r.edi = (ulong)m;
    tn3c_call(A_g3_vec_rotate, &r);
}

/* save_context: pushes view matrix + position and the light state; TRUE when full (CF) */
bool save_context(void)
{
    long *s, *d;
    int i;

    if (g3d_stack_depth == G3_MAX_DEPTH)
        return TRUE;
    g3d_stack_depth++;
    d = (long *)g3d_matrix_stackp;
    s = (long *)&view_matrix;                  /* view_matrix and _view_position are adjacent */
    for (i = 0; i < 12; i++) *d++ = *s++;
    g3d_matrix_stackp = (g3s_mstack_entry *)d;
    d = (long *)g3d_context_stackp;
    s = (long *)&_g3d_light_src;
    for (i = 0; i < 27; i++) *d++ = *s++;
    g3d_context_stackp = d;
    return FALSE;
}

void g3_end_object(void)
{
    long *s, *d;
    int i;

    if (g3d_stack_depth == 0)
        return;
    g3d_stack_depth--;
    g3d_matrix_stackp = (g3s_mstack_entry *)((uchar *)g3d_matrix_stackp - 0x30);
    s = (long *)g3d_matrix_stackp;
    d = (long *)&view_matrix;
    for (i = 0; i < 12; i++) *d++ = *s++;
    g3d_context_stackp = (uchar *)g3d_context_stackp - 0x6C;
    s = (long *)g3d_context_stackp;
    d = (long *)&_g3d_light_src;
    for (i = 0; i < 27; i++) *d++ = *s++;
}

static void sub_view_position(g3s_vector *p)
{
    _view_position.x -= p->x;
    _view_position.y -= p->y;
    _view_position.z -= p->z;
}

/* _wtoo_position += p rotated by _wtoo_matrix */
static void add_wtoo_position(g3s_vector *p)
{
    vec_rotate(&inst_temp_vec, p, &_wtoo_matrix);
    _wtoo_position.x += inst_temp_vec.x;
    _wtoo_position.y += inst_temp_vec.y;
    _wtoo_position.z += inst_temp_vec.z;
}

long g3_start_object(g3s_vector *p)
{
    if (save_context())
        return 0;
    sub_view_position(p);
    add_wtoo_position(p);
    return -1;
}

/* dest = src x dest (via inst_temp_matrix2) */
void instance_matrix(g3s_matrix *dest, g3s_matrix *src)
{
    tn3c_regs r = { 0 };
    r.edi = (ulong)dest; r.esi = (ulong)src; r.ebx = (ulong)&inst_temp_matrix2;
    tn3c_call(A_g3_matrix_x_matrix, &r);
    *dest = inst_temp_matrix2;
}

static long start_angles_common(g3s_vector *p, g3s_angvec *o, int order)
{
    tn3c_regs r = { 0 };

    sub_view_position(p);
    add_wtoo_position(p);
    r.esi = (ulong)o; r.edi = (ulong)&inst_temp_matrix; r.ecx = order;
    tn3c_call(A_angles_2_matrix, &r);
    vec_rotate(&_view_position, &_view_position, &inst_temp_matrix);
    vec_rotate(&_g3d_light_src, &_g3d_light_src, &inst_temp_matrix);
    r.esi = (ulong)&inst_temp_matrix;
    tn3c_call(A_g3_transpose, &r);
    instance_matrix(&_wtoo_matrix, &inst_temp_matrix);
    instance_matrix(&view_matrix, &inst_temp_matrix);
    return -1;
}

long g3_start_object_angles_xyz(g3s_vector *p, fixang tx, fixang ty, fixang tz, int order)
{
    if (save_context())
        return 0;
    inst_temp_angles.tx = tx;
    inst_temp_angles.ty = ty;
    inst_temp_angles.tz = tz;
    return start_angles_common(p, &inst_temp_angles, order);
}

long g3_start_object_angles_v(g3s_vector *p, g3s_angvec *o, int order)
{
    if (save_context())
        return 0;
    return start_angles_common(p, o, order);
}

/* 0x2F7E9A: rotation in the x/z plane (Shock's instance_y): view position x,z and view
 * matrix rows m1..m3 / m7..m9 */
long instance_x(fixang a)
{
    fix s, c, t;
    fix_sincos_asm(a, &s, &c);
    inst_sin_x = s;
    inst_cos_x = c;
    t = _view_position.x;
    _view_position.x = MSUB(_view_position.x, c, _view_position.z, s);
    _view_position.z = MADD(_view_position.z, c, t, s);
    inst_temp_vec.x = MSUB(c, view_matrix.m1, s, view_matrix.m7);
    inst_temp_vec.y = MSUB(c, view_matrix.m2, s, view_matrix.m8);
    inst_temp_vec.z = MSUB(c, view_matrix.m3, s, view_matrix.m9);
    view_matrix.m7 = MADD(s, view_matrix.m1, c, view_matrix.m7);
    view_matrix.m8 = MADD(s, view_matrix.m2, c, view_matrix.m8);
    view_matrix.m9 = MADD(s, view_matrix.m3, c, view_matrix.m9);
    view_matrix.m1 = inst_temp_vec.x;
    view_matrix.m2 = inst_temp_vec.y;
    view_matrix.m3 = inst_temp_vec.z;
    return -1;
}

long g3_start_object_angles_x(g3s_vector *p, fixang tx)
{
    if (save_context())
        return 0;
    sub_view_position(p);
    return instance_x(tx);
}

/* 0x2F8074: rotation in the y/z plane (Shock's instance_x): view position y,z, light_src y,z,
 * view matrix rows m4..m6 / m7..m9 and the same rows of _wtoo_matrix */
long instance_y(fixang a)
{
    fix s, c, t;
    fix_sincos_asm(a, &s, &c);
    inst_sin_y = s;
    inst_cos_y = c;
    t = _view_position.y;
    _view_position.y = MADD(_view_position.z, s, _view_position.y, c);
    _view_position.z = MSUB(_view_position.z, c, t, s);
    t = _g3d_light_src.y;
    _g3d_light_src.y = MADD(_g3d_light_src.z, s, _g3d_light_src.y, c);
    _g3d_light_src.z = MSUB(_g3d_light_src.z, c, t, s);
    inst_temp_vec.x = MADD(s, view_matrix.m7, c, view_matrix.m4);
    inst_temp_vec.y = MADD(s, view_matrix.m8, c, view_matrix.m5);
    inst_temp_vec.z = MADD(s, view_matrix.m9, c, view_matrix.m6);
    view_matrix.m7 = MSUB(c, view_matrix.m7, s, view_matrix.m4);
    view_matrix.m8 = MSUB(c, view_matrix.m8, s, view_matrix.m5);
    view_matrix.m9 = MSUB(c, view_matrix.m9, s, view_matrix.m6);
    view_matrix.m4 = inst_temp_vec.x;
    view_matrix.m5 = inst_temp_vec.y;
    view_matrix.m6 = inst_temp_vec.z;
    inst_temp_vec.x = MADD(s, _wtoo_matrix.m7, c, _wtoo_matrix.m4);
    inst_temp_vec.y = MADD(s, _wtoo_matrix.m8, c, _wtoo_matrix.m5);
    inst_temp_vec.z = MADD(s, _wtoo_matrix.m9, c, _wtoo_matrix.m6);
    _wtoo_matrix.m7 = MSUB(c, _wtoo_matrix.m7, s, _wtoo_matrix.m4);
    _wtoo_matrix.m8 = MSUB(c, _wtoo_matrix.m8, s, _wtoo_matrix.m5);
    _wtoo_matrix.m9 = MSUB(c, _wtoo_matrix.m9, s, _wtoo_matrix.m6);
    _wtoo_matrix.m4 = inst_temp_vec.x;
    _wtoo_matrix.m5 = inst_temp_vec.y;
    _wtoo_matrix.m6 = inst_temp_vec.z;
    return -1;
}

long g3_start_object_angles_y(g3s_vector *p, fixang ty)
{
    if (save_context())
        return 0;
    sub_view_position(p);
    add_wtoo_position(p);
    return instance_y(ty);
}

/* 0x2F8393: rotation in the x/y plane: view position x,y and view matrix rows m1..m3 / m4..m6 */
long instance_z(fixang a)
{
    fix s, c, t;
    fix_sincos_asm(a, &s, &c);
    inst_sin_z = s;
    inst_cos_z = c;
    t = _view_position.x;
    _view_position.x = MADD(_view_position.x, c, _view_position.y, s);
    _view_position.y = MSUB(_view_position.y, c, t, s);
    inst_temp_vec.x = MADD(s, view_matrix.m4, c, view_matrix.m1);
    inst_temp_vec.y = MADD(s, view_matrix.m5, c, view_matrix.m2);
    inst_temp_vec.z = MADD(s, view_matrix.m6, c, view_matrix.m3);
    view_matrix.m4 = MSUB(c, view_matrix.m4, s, view_matrix.m1);
    view_matrix.m5 = MSUB(c, view_matrix.m5, s, view_matrix.m2);
    view_matrix.m6 = MSUB(c, view_matrix.m6, s, view_matrix.m3);
    view_matrix.m1 = inst_temp_vec.x;
    view_matrix.m2 = inst_temp_vec.y;
    view_matrix.m3 = inst_temp_vec.z;
    return -1;
}

long g3_start_object_angles_z(g3s_vector *p, fixang tz)
{
    if (save_context())
        return 0;
    sub_view_position(p);
    return instance_z(tz);
}

long g3_start_object_angles_xy(g3s_vector *p, fixang a_ebx, fixang a_edx, int order)
{
    if (save_context())
        return 0;
    sub_view_position(p);
    if (!(order & 1)) {
        instance_y(a_ebx);
        return instance_x(a_edx);
    }
    instance_x(a_edx);
    return instance_y(a_ebx);
}

long g3_start_object_angles_yz(g3s_vector *p, fixang a_ebx, fixang a_edx, int order)
{
    if (save_context())
        return 0;
    sub_view_position(p);
    if (!(order & 2)) {
        instance_y(a_ebx);
        return instance_z(a_edx);
    }
    instance_z(a_edx);
    return instance_y(a_ebx);
}

long g3_start_object_angles_xz(g3s_vector *p, fixang a_ebx, fixang a_edx, int order)
{
    if (save_context())
        return 0;
    sub_view_position(p);
    if (!(order & 4)) {
        instance_x(a_ebx);
        return instance_z(a_edx);
    }
    instance_z(a_edx);
    return instance_x(a_ebx);
}

long g3_start_object_angles_zx(g3s_vector *p, fixang a_ebx, fixang a_edx, int order)
{
    if (save_context())
        return 0;
    sub_view_position(p);
    instance_z(a_ebx);
    return instance_x(a_edx);
}

long g3_start_object_matrix(g3s_vector *p, g3s_matrix *m)
{
    tn3c_regs r = { 0 };

    if (save_context())
        return 0;
    sub_view_position(p);
    add_wtoo_position(p);
    vec_rotate(&_view_position, &_view_position, m);
    vec_rotate(&_g3d_light_src, &_g3d_light_src, m);
    r.esi = (ulong)m; r.edi = (ulong)&inst_temp_matrix;
    tn3c_call(A_g3_copy_transpose, &r);
    instance_matrix(&view_matrix, &inst_temp_matrix);
    instance_matrix(&_wtoo_matrix, &inst_temp_matrix);
    return -1;
}

/* view position /= s (fix_div by idiv), view matrix and _wtoo_matrix *= s */
void g3_scale_object(fix s)
{
    fix *m;
    int i;

    _view_position.x = (fix)(((long long)_view_position.x << 16) / s);
    _view_position.y = (fix)(((long long)_view_position.y << 16) / s);
    _view_position.z = (fix)(((long long)_view_position.z << 16) / s);
    m = &view_matrix.m1;
    for (i = 0; i < 9; i++) m[i] = (fix)(((long long)m[i] * s) >> 16);
    m = &_wtoo_matrix.m1;
    for (i = 0; i < 9; i++) m[i] = (fix)(((long long)m[i] * s) >> 16);
}
