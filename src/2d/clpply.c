/* clpply.c -- x:\prj\tech\libsrc\2d\clpply.c (Terra Nova v1.09): Sutherland-Hodgman polygon
 * clipper.  Decompiled from __FF.EXE.
 *
 *   0x325AC8 gr_clip_poly(n, l, vpl, pcplist)
 *   0x325AB0 gri_clip_order_x (name ours; unreferenced): swaps *a and *b when (*a)[0] < (*b)[0].
 *
 * Terra Nova changes against Shock:
 *  - *pcplist is stored right after the buffer is chosen (temp_malloc'd when *pcplist is NULL),
 *    before anything else.
 *  - Trivial reject of huge coordinates: if any vertex has x or y outside
 *    (0xE0000000, 0x20000000) exclusive, it returns 0 with no output (checked x then y, vertex
 *    by vertex).
 *  - Every intersection is interpolated from the end point with the larger x (the two points
 *    are swapped when v0[0] < v1[0], for all four edges, including top and bottom), so a
 *    shared edge gives the same intersection whichever way it is walked.
 * The rest is Shock: left (vpl->tplist), top (->cplist), right (->tplist), bottom (->cplist),
 * entering edges whose end point lies exactly on the clip line output only the end point,
 * extra parameters k = 2..l-1 are interpolated, and vpl[n-1] / list[j-1] are read even when
 * the list is empty.  fix_mul_div is imul/idiv (faults like the original on overflow).
 */
#include "2d_prim.h"

static inline fix cp_mul_div(fix a, fix b, fix c)
{
    return (fix)(((long long)a * b) / c);
}

void gri_clip_order_x(fix **a, fix **b)
{
    fix *pa = *a, *pb = *b;

    if (pa[0] < pb[0]) {
        *a = pb;
        *b = pa;
    }
}

/* one Sutherland-Hodgman pass.  c = clipped coordinate (0 = x, 1 = y), hi = clip against an
 * upper bound (inside is v[c] <= bound) instead of a lower one (inside is v[c] >= bound). */
static int clip_pass(fix **src, int n, fix **dst, fix *tlist, int *pnew_v, int l,
                     int c, int hi, fix bound)
{
    fix *v0, *v1, *a, *b;
    int i, j = 0, k, new_v = *pnew_v;
    fix num, den;

    v0 = src[n - 1];
    for (i = 0; i < n; i++) {
        v1 = src[i];
        int in0 = hi ? v0[c] <= bound : v0[c] >= bound;
        int in1 = hi ? v1[c] <= bound : v1[c] >= bound;
        int isect;

        if (in0) {
            if (in1) {
                dst[j++] = v1;
                v0 = v1;
                continue;
            }
            isect = 1;
        } else {
            if (!in1) {
                v0 = v1;
                continue;
            }
            isect = (v1[c] != bound);
        }
        if (isect) {
            a = v0; b = v1;
            if (a[0] < b[0]) { fix *t = a; a = b; b = t; }
            num = bound - a[c];
            den = b[c] - a[c];
            tlist[new_v + c] = bound;
            tlist[new_v + (c ^ 1)] = a[c ^ 1] + cp_mul_div(b[c ^ 1] - a[c ^ 1], num, den);
            for (k = 2; k < l; k++)
                tlist[new_v + k] = a[k] + cp_mul_div(b[k] - a[k], num, den);
            dst[j++] = &tlist[new_v];
            new_v += k;
        }
        if (!in0)
            dst[j++] = v1;
        v0 = v1;
    }
    *pnew_v = new_v;
    return j;
}

int gr_clip_poly(int n, int l, grs_vertex **vpl, grs_vertex ***pcplist)
{
    fix **cplist, **tplist, *tlist;
    fix **vl = (fix **)vpl;
    int i, j, new_v = 0;

    if (*pcplist)
        cplist = (fix **)*pcplist;
    else
        cplist = (fix **)temp_malloc(2 * n * (l + 2) * sizeof(fix *));
    *pcplist = (grs_vertex **)cplist;

    for (i = 0; i < n; i++) {
        fix x = vl[i][0], y;
        if (x >= 0x20000000 || x <= (fix)0xe0000000)
            return 0;
        y = vl[i][1];
        if (y >= 0x20000000 || y <= (fix)0xe0000000)
            return 0;
    }
    tplist = cplist + 2 * n;
    tlist = (fix *)(tplist + 2 * n);

    j = clip_pass(vl, n, tplist, tlist, &new_v, l, 0, 0, grd_fix_clip.left);
    j = clip_pass(tplist, j, cplist, tlist, &new_v, l, 1, 0, grd_fix_clip.top);
    j = clip_pass(cplist, j, tplist, tlist, &new_v, l, 0, 1, grd_fix_clip.right);
    if (j <= 0)
        return 0;
    j = clip_pass(tplist, j, cplist, tlist, &new_v, l, 1, 1, grd_fix_clip.bot);
    return j;
}
