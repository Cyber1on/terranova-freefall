/* matrix.c -- x:\prj\tech\libsrc\3d\matrix.asm (Terra Nova v1.09): view setup, angles to
 * matrix, matrix helpers, clip codes and the view pyramid.  Decompiled from __FF.EXE
 * (hand-written asm in the original; RCS string at 0x364472).  Names follow System Shock's
 * MATRIX.C / 3D.H.
 *
 *   0x2FA0F4 g3_set_view_angles   0x2FA12B g3_set_view_matrix   0x2FA148 process_view_matrix
 *   0x2FA285 scale_view_matrix    0x2FA42E compute_invalid      0x2FA42F angles_2_matrix
 *   0x2FA478 compute_ZXY          0x2FA496 compute_XZY          0x2FA4B4 compute_ZYX
 *   0x2FA4D2 compute_YXZ          0x2FA5AA compute_YZX          0x2FA686 compute_XYZ
 *   0x2FA762 code_point           0x2FA793 g3_vec_rotate        0x2FA820 g3_transpose
 *   0x2FA83C g3_copy_transpose    0x2FA871 g3_matrix_x_matrix   0x2FA9E1 get_pyr_vector
 *   0x2FABB7 g3_get_view_pyramid
 *
 * Register conventions of the original entries (C versions are cdecl):
 *   g3_set_view_angles  esi=pos ebx=angvec ecx=order eax=zoom
 *   g3_set_view_matrix  esi=pos ebx=matrix eax=zoom
 *   angles_2_matrix     esi=angvec edi=matrix ecx=order (jmp cs:[order*4+0x2FA40E])
 *   compute_*           edi=matrix, angles in the sin/cos temporaries
 *   code_point          edi=point -> bl = codes (also stored at +0x14)
 *   g3_vec_rotate       ebx=dest esi=src edi=matrix
 *   g3_transpose        esi=matrix          g3_copy_transpose  edi=dest esi=src
 *   g3_matrix_x_matrix  ebx=dest esi=src1 edi=src2 (dest = src1 * src2)
 *   get_pyr_vector      edi=dest, plane terms in g3d_pyr_t
 *   g3_get_view_pyramid edi=4 vectors
 */
#include "3d_a.h"

#define VM ((fix *)&view_matrix)

/* ---------------- view setup ---------------- */

/* Note the angvec order: tx goes to view_pitch, ty to view_heading. */
void g3_set_view_angles(g3s_vector *pos, g3s_angvec *angles, int rotation_order, fix zoom)
{
    _view_zoom = zoom;
    _view_position = *pos;
    view_pitch = angles->tx;
    view_heading = angles->ty;
    view_bank = angles->tz;
    angles_2_matrix(angles, &view_matrix, rotation_order);
    process_view_matrix();
}

void g3_set_view_matrix(g3s_vector *pos, g3s_matrix *m, fix zoom)
{
    _view_zoom = zoom;
    _view_position = *pos;
    view_matrix = *m;
    process_view_matrix();
}

static void swap_fix(fix *a, fix *b)
{
    fix t = *a;
    *a = *b;
    *b = t;
}

/* Applies the g3_init axis mapping to view_matrix, takes the horizon vector, copies the
 * result to unscaled_matrix, scales it and resets the instance stack depth.
 * Quirk kept: the horizon vector is read at view_matrix[up_axis], and up_axis is negative
 * for a downward y axis (Terra Nova: -9, i.e. unscaled_matrix m1..m3 of the previous view). */
void process_view_matrix(void)
{
    fix *m = VM;
    uchar f = axis_swap_flag;
    int i;

    if (f & 1)
        for (i = 0; i < 9; i += 3)
            swap_fix(&m[i], &m[i + 1]);
    if (f & 2)
        for (i = 0; i < 9; i += 3)
            swap_fix(&m[i], &m[i + 2]);
    if (f & 4)
        for (i = 0; i < 9; i += 3)
            swap_fix(&m[i + 1], &m[i + 2]);
    horizon_vector.x = m[up_axis];
    horizon_vector.y = m[up_axis + 1];
    horizon_vector.z = m[up_axis + 2];
    f = axis_neg_flag;
    for (i = 0; i < 3; i++)
        if (f & (1 << i)) {
            m[i] = -m[i];
            m[i + 3] = -m[i + 3];
            m[i + 6] = -m[i + 6];
        }
    unscaled_matrix = view_matrix;
    scale_view_matrix();
    g3d_stack_depth = 0;
}

/* Computes _matrix_scale from the zoom and the window aspect, scales the columns of
 * view_matrix and the horizon vector. */
void scale_view_matrix(void)
{
    fix *m = VM;
    fix t;
    int i;

    _matrix_scale.x = FIX_UNIT;
    _matrix_scale.y = FIX_UNIT;
    _matrix_scale.z = FIX_UNIT;
    if (_view_zoom <= FIX_UNIT)
        _matrix_scale.z = _view_zoom;
    else {
        t = g3a_div(FIX_UNIT, _view_zoom);
        _matrix_scale.x = t;
        _matrix_scale.y = t;
    }
    t = (fix)(((long long)window_height * pixel_ratio) / window_width);
    if (t > FIX_UNIT)
        _matrix_scale.y = g3a_div(_matrix_scale.y, t);
    else
        _matrix_scale.x = g3a_mul(t, _matrix_scale.x);
    for (i = 0; i < 9; i += 3) {
        m[i] = g3a_mul(m[i], _matrix_scale.x);
    }
    for (i = 1; i < 9; i += 3)
        m[i] = g3a_mul(m[i], _matrix_scale.y);
    for (i = 2; i < 9; i += 3)
        m[i] = g3a_mul(m[i], _matrix_scale.z);
    horizon_vector.x = g3a_mul(g3a_mul(_matrix_scale.y, _matrix_scale.z), horizon_vector.x);
    horizon_vector.y = g3a_mul(g3a_mul(_matrix_scale.x, _matrix_scale.z), horizon_vector.y);
    horizon_vector.z = g3a_mul(g3a_mul(_matrix_scale.x, _matrix_scale.y), horizon_vector.z);
}

/* ---------------- angles -> matrix ---------------- */

/* table slots 2 and 5: just ret */
void compute_invalid(g3s_matrix *m)
{
}

/* The jump table at 0x2FA40E (in the code object).  TN quirk kept: slot 7 (ZYX) points to
 * compute_XYZ, so compute_ZYX is never used. */
static void (*const compute_tab[8])(g3s_matrix *) = {
    compute_XYZ, compute_YXZ, compute_invalid, compute_YZX,
    compute_XZY, compute_invalid, compute_ZXY, compute_XYZ,
};

/* Builds a rotation matrix from three angles.  The sin/cos of tx, ty, tz go to the module
 * temporaries.  Orders above 7 jump through garbage in the original; here they do nothing. */
void angles_2_matrix(g3s_angvec *angles, g3s_matrix *m, int rotation_order)
{
    fix_sincos_asm(angles->tx, &g3d_sin_x, &g3d_cos_x);
    fix_sincos_asm(angles->ty, &g3d_sin_y, &g3d_cos_y);
    fix_sincos_asm(angles->tz, &g3d_sin_z, &g3d_cos_z);
    if ((uint)rotation_order < 8)
        compute_tab[rotation_order](m);
}

/* The "reversed" orders: negate the sines, build the opposite order, transpose. */
static void negate_sines(void)
{
    g3d_sin_x = -g3d_sin_x;
    g3d_sin_z = -g3d_sin_z;
    g3d_sin_y = -g3d_sin_y;
}

void compute_ZXY(g3s_matrix *m)
{
    negate_sines();
    compute_YXZ(m);
    g3_transpose(m);
}

void compute_XZY(g3s_matrix *m)
{
    negate_sines();
    compute_YZX(m);
    g3_transpose(m);
}

/* unused (see compute_tab) */
void compute_ZYX(g3s_matrix *m)
{
    negate_sines();
    compute_XYZ(m);
    g3_transpose(m);
}

/* sx/cx = sin/cos of tx (pitch), sy/cy = ty (heading), sz/cz = tz (bank) */
void compute_YXZ(g3s_matrix *m)
{
    fix a, b;
    a = g3a_mul(g3d_cos_z, g3d_cos_y);
    b = g3a_mul(g3d_sin_z, g3d_sin_y);
    m->m1 = g3a_mul(b, g3d_sin_x) + a;
    m->m8 = g3a_mul(a, g3d_sin_x) + b;
    a = g3a_mul(g3d_sin_z, g3d_cos_y);
    b = g3a_mul(g3d_cos_z, g3d_sin_y);
    m->m2 = g3a_mul(b, g3d_sin_x) - a;
    m->m7 = g3a_mul(a, g3d_sin_x) - b;
    m->m3 = g3a_mul(g3d_sin_y, g3d_cos_x);
    m->m4 = g3a_mul(g3d_sin_z, g3d_cos_x);
    m->m5 = g3a_mul(g3d_cos_z, g3d_cos_x);
    m->m6 = -g3d_sin_x;
    m->m9 = g3a_mul(g3d_cos_y, g3d_cos_x);
}

void compute_YZX(g3s_matrix *m)
{
    fix a, b;
    m->m1 = g3a_mul(g3d_cos_y, g3d_cos_z);
    a = g3a_mul(g3d_cos_y, g3d_cos_x);
    b = g3a_mul(g3d_sin_y, g3d_sin_x);
    m->m2 = b - g3a_mul(a, g3d_sin_z);
    m->m9 = a - g3a_mul(b, g3d_sin_z);
    a = g3a_mul(g3d_cos_y, g3d_sin_x);
    b = g3a_mul(g3d_sin_y, g3d_cos_x);
    m->m3 = b + g3a_mul(a, g3d_sin_z);
    m->m8 = g3a_mul(b, g3d_sin_z) + a;
    m->m4 = g3d_sin_z;
    m->m5 = g3a_mul(g3d_cos_z, g3d_cos_x);
    m->m6 = -g3a_mul(g3d_cos_z, g3d_sin_x);
    m->m7 = -g3a_mul(g3d_sin_y, g3d_cos_z);
}

void compute_XYZ(g3s_matrix *m)
{
    fix a, b;
    m->m1 = g3a_mul(g3d_cos_y, g3d_cos_z);
    m->m2 = -g3a_mul(g3d_cos_y, g3d_sin_z);
    m->m3 = g3d_sin_y;
    a = g3a_mul(g3d_sin_x, g3d_cos_z);
    b = g3a_mul(g3d_cos_x, g3d_sin_z);
    m->m4 = b + g3a_mul(a, g3d_sin_y);
    m->m8 = g3a_mul(b, g3d_sin_y) + a;
    a = g3a_mul(g3d_sin_x, g3d_sin_z);
    b = g3a_mul(g3d_cos_x, g3d_cos_z);
    m->m5 = b - g3a_mul(a, g3d_sin_y);
    m->m7 = a - g3a_mul(b, g3d_sin_y);
    m->m6 = -g3a_mul(g3d_sin_x, g3d_cos_y);
    m->m9 = g3a_mul(g3d_cos_x, g3d_cos_y);
}

/* ---------------- points and matrices ---------------- */

/* Clip codes of a rotated point (x, y against +-z; CC_BEHIND when z <= 0). */
uchar code_point(g3s_phandle p)
{
    fix z = p->z, nz;
    uchar c = 0;
    if (p->x > z)
        c |= CC_OFF_RIGHT;
    if (p->y > z)
        c |= CC_OFF_TOP;
    nz = -z;
    if (nz > -1)
        c |= CC_BEHIND;
    if (p->x < nz)
        c |= CC_OFF_LEFT;
    if (p->y < nz)
        c |= CC_OFF_BOT;
    p->codes = c;
    return c;
}

/* dest = src * m (row vector times matrix); x and y go through module temporaries. */
void g3_vec_rotate(g3s_vector *dest, g3s_vector *src, g3s_matrix *m)
{
    fix z;
    g3d_vrot_x = g3a_dot3(src->x, m->m1, src->y, m->m4, src->z, m->m7);
    g3d_vrot_y = g3a_dot3(src->x, m->m2, src->y, m->m5, src->z, m->m8);
    z = g3a_dot3(src->x, m->m3, src->y, m->m6, src->z, m->m9);
    dest->z = z;
    dest->x = g3d_vrot_x;
    dest->y = g3d_vrot_y;
}

void g3_transpose(g3s_matrix *m)
{
    swap_fix(&m->m2, &m->m4);
    swap_fix(&m->m3, &m->m7);
    swap_fix(&m->m6, &m->m8);
}

/* The copy order of the original is kept (it matters when dest == src). */
void g3_copy_transpose(g3s_matrix *dest, g3s_matrix *src)
{
    dest->m1 = src->m1;
    dest->m5 = src->m5;
    dest->m9 = src->m9;
    dest->m2 = src->m4;
    dest->m4 = src->m2;
    dest->m3 = src->m7;
    dest->m7 = src->m3;
    dest->m6 = src->m8;
    dest->m8 = src->m6;
}

/* dest = src1 * src2, written element by element (aliasing behaves as in the original). */
void g3_matrix_x_matrix(g3s_matrix *dest, g3s_matrix *src1, g3s_matrix *src2)
{
    fix *d = (fix *)dest, *a = (fix *)src1, *b = (fix *)src2;
    int i, j;
    for (i = 0; i < 9; i += 3)
        for (j = 0; j < 3; j++)
            d[i + j] = g3a_dot3(a[i], b[j], a[i + 1], b[j + 3], a[i + 2], b[j + 6]);
}

/* ---------------- view pyramid ---------------- */

#define T g3d_pyr_t
/* a*b - c*d in 64 bits (the asm computes c*d first, then a*b and subtracts) */
static long long xmul(fix a, fix b, fix c, fix d)
{
    return (long long)a * b - (long long)c * d;
}

static int big(fix v)
{
    v = v < 0 ? -v : v;                 /* cdq/xor/sub: abs(INT_MIN) stays negative */
    return v >= 0x8000;
}

/* Corner vector of the view pyramid from the two plane equations in g3d_pyr_t: solves for
 * the largest usable component (set to 1.0), normalizes and points it forward (positive
 * dot with the view z column).  The divisions fault in the original on overflow. */
void get_pyr_vector(g3s_vector *corner)
{
    g3d_pyr_den = (fix)(xmul(T[0], T[3], T[1], T[2]) >> 16);
    if (big(g3d_pyr_den)) {
        corner->z = FIX_UNIT;
        corner->x = (fix)(xmul(T[5], T[2], T[4], T[3]) / g3d_pyr_den);
        corner->y = (fix)(xmul(T[4], T[1], T[0], T[5]) / g3d_pyr_den);
    } else {
        g3d_pyr_den = (fix)(xmul(T[2], T[5], T[3], T[4]) >> 16);
        if (big(g3d_pyr_den)) {
            corner->x = FIX_UNIT;
            corner->y = (fix)(xmul(T[1], T[4], T[0], T[5]) / g3d_pyr_den);
            corner->z = (fix)(xmul(T[0], T[3], T[1], T[2]) / g3d_pyr_den);
        } else {
            g3d_pyr_den = (fix)(xmul(T[0], T[5], T[1], T[4]) >> 16);
            corner->y = FIX_UNIT;
            corner->x = (fix)(xmul(T[3], T[4], T[2], T[5]) / g3d_pyr_den);
            corner->z = (fix)(xmul(T[2], T[1], T[3], T[0]) / g3d_pyr_den);
        }
    }
    g3_vec_normalize(corner);
    if ((long long)corner->x * view_matrix.m3 + (long long)corner->y * view_matrix.m6 +
            (long long)corner->z * view_matrix.m9 < 0) {
        corner->x = -corner->x;
        corner->y = -corner->y;
        corner->z = -corner->z;
    }
}

/* The four corner vectors of the view pyramid (in view space before scaling). */
void g3_get_view_pyramid(g3s_vector *corners)
{
    fix *m = VM;
    fix s1, s3, s5;

    T[0] = m[0] - m[2];
    T[1] = m[1] - m[2];
    T[2] = m[3] - m[5];
    T[3] = m[4] - m[5];
    T[4] = m[6] - m[8];
    T[5] = m[7] - m[8];
    get_pyr_vector(corners);
    s1 = T[1];
    s3 = T[3];
    s5 = T[5];
    T[1] -= m[1] << 1;
    T[3] -= m[4] << 1;
    T[5] -= m[7] << 1;
    get_pyr_vector(corners + 1);
    T[0] -= m[0] << 1;
    T[2] -= m[3] << 1;
    T[4] -= m[6] << 1;
    get_pyr_vector(corners + 2);
    T[5] = s5;
    T[3] = s3;
    T[1] = s1;
    get_pyr_vector(corners + 3);
}
#undef T
