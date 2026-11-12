/* star3d.c -- x:\prj\tech\libsrc\3d\star3d.asm (Terra Nova v1.09; RCS string at 0x365498):
 * transforming a star.  Decompiled from __FF.EXE (hand-written asm in the original).
 *
 *   0x317CEC star_transform (name ours)
 *
 * Original registers: esi=vector -> edi=new point, bl=codes; clobbers eax ebx ecx edx.
 * The tail is a jmp to g3_project_point (eax = its result) when the point is on screen.
 */
#include "3d_a.h"

/* New point = v rotated by view_matrix without translation (stars are at infinity).
 * A star nearer than std_min_z gets codes = CC_BEHIND and nothing else (x, y, z unset);
 * the comparison is the sign of z - std_min_z (js), so it wraps for far-apart values.
 * Otherwise the point is coded, and projected when on screen. */
g3s_phandle star_transform(g3s_vector *v)
{
    g3s_matrix *m = &view_matrix;
    g3s_point *p = first_free;
    fix z;

    if (p != NULL)
        first_free = p->next;
    p->p3_flags = 0;
    z = g3a_dot3(v->x, m->m3, v->y, m->m6, v->z, m->m9);
    if ((long)((ulong)z - (ulong)std_min_z) < 0) {
        p->codes = CC_BEHIND;
        return p;
    }
    p->z = z;
    p->x = g3a_dot3(v->x, m->m1, v->y, m->m4, v->z, m->m7);
    p->y = g3a_dot3(v->x, m->m2, v->y, m->m5, v->z, m->m8);
    if (code_point(p) == 0)
        g3_project_point(p);
    return p;
}
