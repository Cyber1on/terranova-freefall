/* vector.c -- x:\prj\tech\libsrc\3d\vector.asm (Terra Nova v1.09): vector arithmetic.
 * Decompiled from __FF.EXE (hand-written asm in the original).  Names follow System Shock's
 * VECTOR.C / 3D.H.
 *
 *   0x2F3FC4 g3_vec_add           0x2F3FDD g3_vec_sub           0x2F3FF6 g3_vec_scale
 *   0x2F4019 g3_vec_mag           0x2F403B g3_vec_dotprod       0x2F405B g3_compute_normal
 *   0x2F406A g3_vec_normalize     0x2F40BB g3_compute_normal_quick
 *
 * Register conventions of the original entries (C versions are cdecl):
 *   g3_vec_add/sub      edi=dest esi=src1 ebx=src2; clobber eax
 *   g3_vec_scale        edi=dest esi=src ebx=scale (not eax); clobbers eax edx
 *   g3_vec_mag          esi=v -> eax; clobbers ebx ecx edx (quad_sqrt)
 *   g3_vec_dotprod      esi=v0 edi=v1 -> eax; clobbers ebx ecx edx, esi += 12
 *   g3_vec_normalize    esi=v in place
 *   g3_compute_normal(_quick)  edi=dest eax=v0 edx=v1 ebx=v2; clobber esi
 */
#include "3d_a.h"

void g3_vec_add(g3s_vector *dest, g3s_vector *src1, g3s_vector *src2)
{
    dest->x = src1->x + src2->x;
    dest->y = src1->y + src2->y;
    dest->z = src1->z + src2->z;
}

void g3_vec_sub(g3s_vector *dest, g3s_vector *src1, g3s_vector *src2)
{
    dest->x = src1->x - src2->x;
    dest->y = src1->y - src2->y;
    dest->z = src1->z - src2->z;
}

void g3_vec_scale(g3s_vector *dest, g3s_vector *src, fix s)
{
    dest->x = g3a_mul(src->x, s);
    dest->y = g3a_mul(src->y, s);
    dest->z = g3a_mul(src->z, s);
}

/* sqrt of the 64-bit x*x + y*y + z*z */
fix g3_vec_mag(g3s_vector *v)
{
    long long s = (long long)v->x * v->x + (long long)v->y * v->y + (long long)v->z * v->z;
    return (fix)quad_sqrt((long)s, (long)(s >> 32));
}

fix g3_vec_dotprod(g3s_vector *v0, g3s_vector *v1)
{
    return g3a_dot3(v0->x, v1->x, v0->y, v1->y, v0->z, v1->z);
}

/* Normal of the triangle v0 v1 v2, normalized. */
void g3_compute_normal(g3s_vector *norm, g3s_vector *v0, g3s_vector *v1, g3s_vector *v2)
{
    g3_compute_normal_quick(norm, v0, v1, v2);
    g3_vec_normalize(norm);
}

/* Scales v to unit length; a zero vector becomes (1, 0, 0).  The divisions fault in the
 * original when a component does not fit (only possible through quad_sqrt rounding). */
void g3_vec_normalize(g3s_vector *v)
{
    fix m = g3_vec_mag(v);
    if (m == 0) {
        v->x = FIX_UNIT;
        v->y = 0;
        v->z = 0;
        return;
    }
    v->x = g3a_div(v->x, m);
    v->y = g3a_div(v->y, m);
    v->z = g3a_div(v->z, m);
}

/* x86 shrd mem, reg, cl (count masked to 5 bits) */
static long shrd(long lo, long hi, int c)
{
    c &= 31;
    if (c == 0)
        return lo;
    return (long)(((ulong)lo >> c) | ((ulong)hi << (32 - c)));
}

/* bits 31..62 of |v| (the asm negates the 64-bit value, then shl/rcl by one and keeps the
 * high dword) */
static ulong abs_hi(long long v)
{
    if (v < 0)
        v = -v;
    return (ulong)((unsigned long long)v >> 31);
}

/* Unnormalized normal (v1 - v0) x (v2 - v1), computed in 64 bits and shifted right until
 * it fits in 31 bits.  The differences go to g3d_norm_v0/v1 and the high dwords of the
 * products to g3d_norm_hi (module temporaries, as in the original).
 * Quirk: if a product reaches 2^62 the shift-count loop never ends (sar keeps the sign). */
void g3_compute_normal_quick(g3s_vector *norm, g3s_vector *v0, g3s_vector *v1, g3s_vector *v2)
{
    g3s_vector *a = &g3d_norm_v0, *b = &g3d_norm_v1;
    long long px, py, pz;
    long h;
    int c;

    g3_vec_sub(&g3d_norm_v0, v1, v0);
    g3_vec_sub(&g3d_norm_v1, v2, v1);
    px = (long long)b->z * a->y - (long long)b->y * a->z;
    norm->x = (long)px;
    g3d_norm_hi[0] = (long)(px >> 32);
    py = (long long)b->x * a->z - (long long)b->z * a->x;
    norm->y = (long)py;
    g3d_norm_hi[1] = (long)(py >> 32);
    pz = (long long)b->y * a->x - (long long)b->x * a->y;
    norm->z = (long)pz;
    g3d_norm_hi[2] = (long)(pz >> 32);

    h = (long)(abs_hi(((long long)g3d_norm_hi[0] << 32) | (ulong)norm->x) |
               abs_hi(((long long)g3d_norm_hi[1] << 32) | (ulong)norm->y) |
               abs_hi(((long long)g3d_norm_hi[2] << 32) | (ulong)norm->z));
    if (h == 0)
        return;
    c = 0;
    while ((ulong)h >= 0x100) {
        c += 8;
        h >>= 8;                        /* sar */
    }
    c = (uchar)(c + shift_table[h]);
    norm->x = shrd(norm->x, g3d_norm_hi[0], c);
    norm->y = shrd(norm->y, g3d_norm_hi[1], c);
    norm->z = shrd(norm->z, g3d_norm_hi[2], c);
}
