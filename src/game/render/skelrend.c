/* skelrend.c -- LG skeleton renderer (x:\prj\tech\libsrc\skel\render.c), 0x2F68B0-0x2F7D4C.
 * Decompiled from __FF.EXE v1.09.  Names are ours (skel_ + address).
 *
 * A skeleton (Skel, g6_b.h) is a set of joints (g3s_vector) joined by segments; each segment has
 * a resource (locked into seg->model by skel_2F69E0 / skel_2F6A64) and a parameter.
 * skel_2F74F0 transforms the joints, orders the segments (mode 0 as stored, 1 the skeleton's own
 * callback, 2 / 3 by depth) and draws them by skeleton type:
 *   0 lines in colour seg->parm, 1 crt_315FB0 segments, 2 sprite segments (skel_2F6C50: one of
 *   16 directional bitmaps of the model stretched between the two joints), 3 3D sub-objects
 *   placed by skel_2F737C.
 * Per-segment hooks (indexed by segment number, set by skel_2F6B50.. which nothing calls):
 *   skel_cb_a: called before / after drawing with the nearer / farther joint;
 *   skel_cb_b: called with a point offset from the segment (skel_cb_parm) when it faces the viewer.
 * skel_2F7B1C uses the same tables as 1-based indices into skel_pos instead (attachment
 * positions) and draws only the sub-objects of a type 3 skeleton.
 *
 * Quirks kept:
 *  - skel_2F6C50 tests |x / z| of the second point twice and never its y.
 *  - skel_2F6C50 rewrites the chosen bitmap's bits pointer (bm + 0x20) on every draw.
 *  - skel_2F74F0 type 3 passes its edi register to g3_interpret_object (read by the light code
 *    when _g3d_light_type & 6): the point list end for the first segment, then the position in
 *    the order buffer.
 * Deviations (values the original leaves to chance):
 *  - skel_2F6C50 leaves the vertices' w and i uninitialised (unused by the bitmap drawers); 0.
 *  - skel_2F7480 passes its caller's edi to g3_interpret_object; we pass 0 (nothing calls it).
 */
#include "g6_b.h"
#include "3d_a.h"
#include "3d_b.h"
#include "3d_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
long skel_cb_a[32];
long skel_cb_b[32];
uchar skel_cb_flag[32];
fix skel_cb_parm[32][3];
g3s_vector skel_pos[32];
uchar skel_tab_6F0[256];
uchar *skel_clut_a;
uchar *skel_clut_b;
uchar skel_clut[256];
#endif

typedef void (*skel_fn)();

/* ------------------------------------------------------------------ allocation */

long skel_2F6920(long n1, long n2, long n3)
{
    return 0x14 + (uchar)n1 * 16 + (uchar)n3 * 12 + (uchar)n2 * 12;
}

Skel *skel_2F68B0(long n1, long n2, long n3)
{
    Skel *sk = f_malloc(skel_2F6920((uchar)n1, (uchar)n2, (uchar)n3));

    sk->seg = (SkSeg *)((uchar *)sk + 0x14);
    sk->joint = (g3s_vector *)((uchar *)sk->seg + (uchar)n1 * 16);
    sk->up = (g3s_vector *)((uchar *)sk->joint + (uchar)n2 * 12);
    sk->nseg = n1;
    sk->njoint = n2;
    sk->n3 = n3;
    return sk;
}

void skel_2F6918(Skel *sk)
{
    f_free(sk);
}

void skel_2F695C(Skel *sk, long n1, long n2, long n3)
{
    sk->seg = (SkSeg *)((uchar *)sk + 0x14);
    sk->joint = (g3s_vector *)((uchar *)sk->seg + (uchar)n1 * 16);
    sk->n3 = n3;
    sk->up = (g3s_vector *)((uchar *)sk->joint + (uchar)n2 * 12);
    sk->nseg = n1;
    sk->njoint = n2;
}

void skel_2F69AC(Skel *sk, ulong parm)
{
    long i;

    for (i = 0; i < sk->nseg; i++)
        sk->seg[i].parm = parm;
}

void skel_2F69DC(Skel *sk, void *fn)
{
    sk->sort_fn = fn;
}

/* ------------------------------------------------------------------ resources */

void skel_2F69E0(Skel *sk, long hi)
{
    long i;

    if (sk->type != 2 && sk->type != 3)
        return;
    for (i = 0; i < sk->nseg; i++)
        sk->seg[i].model = RefLock(((ulong)(ushort)hi << 16) | sk->seg[i].ref);
}

void skel_2F6A64(Skel *sk)
{
    long i;

    if (sk->type != 2 && sk->type != 3)
        return;
    for (i = 0; i < sk->nseg; i++)
        sk->seg[i].model = RefLock(sk->seg[i].ref);
}

void skel_2F6AD0(Skel *sk)
{
    long i;

    if (sk->type < 2)
        return;
    for (i = 0; i < sk->nseg; i++)
        ResUnlock((sk->seg[i].ref >> 16) & 0xffff);
}

void skel_2F6B08(Skel *sk, long hi)
{
    long i;

    if (sk->type < 2)
        return;
    for (i = 0; i < sk->nseg; i++)
        ResUnlock(((((ulong)(ushort)hi << 16) | sk->seg[i].ref) >> 16) & 0xffff);
}

/* ------------------------------------------------------------------ hooks */

void skel_2F6B50(long i, long v, long flag)
{
    if (i < 0x20) {
        skel_cb_a[i] = v;
        skel_cb_flag[i] = flag;
    }
}

void skel_2F6B64(long i, long v, fix a, fix b, fix c)
{
    if (i < 0x20) {
        skel_cb_b[i] = v;
        skel_cb_parm[i][0] = a;
        skel_cb_parm[i][1] = b;
        skel_cb_parm[i][2] = c;
    }
}

void skel_2F6B94(long i, long v, long flag)
{
    if (i < 0x20) {
        skel_cb_flag[i] = flag;
        skel_cb_a[i] = v + 1;
    }
}

void skel_2F6BA8(long i, long v, fix a, fix b, fix c)
{
    if (i < 0x20) {
        skel_cb_b[i] = v + 1;
        skel_cb_parm[i][0] = a;
        skel_cb_parm[i][1] = b;
        skel_cb_parm[i][2] = c;
    }
}

void skel_2F6BD8(void)
{
    long i;

    for (i = 0; i < 32; i++) {
        skel_cb_a[i] = 0;
        skel_cb_b[i] = 0;
    }
    for (i = 0; i < 256; i++)
        skel_tab_6F0[i] = i;
}

/* ------------------------------------------------------------------ sprite segments */

static void set_fill(long t)
{
    grd_canvas->gc.fill_type = t;
    grd_function_table = (void (**)())((uchar *)grd_function_fill_table + t * 0x618);
    grd_uline_fill_vector = (void (**)())((uchar *)grd_uline_fill_table + t * 0x24);
}

static ulong rd32(const uchar *p)
{
    ulong v;

    __builtin_memcpy(&v, p, 4);
    return v;
}

#define FIXL(p, o) (*(fix *)((uchar *)(p) + (o)))

uchar *skel_2F7AAC(uchar *a, uchar *b)
{
    long i;

    if (a != skel_clut_a || b != skel_clut_b) {
        skel_clut_a = a;
        skel_clut_b = b;
        for (i = 0; i < 256; i++)
            skel_clut[i] = b[a[i]];
    }
    return skel_clut;
}

static void draw_bm(grs_bitmap *bm, grs_vertex **vp)
{
    WATCOM_CALL(grd_canvas_table[55 + 2 * bm->type], (long)bm, 4, (long)vp);
}

void skel_2F6C50(SkSeg *s, g3s_vector *a, g3s_vector *b, g3s_vector *up,
                 g3s_point *pa, g3s_point *pb)
{
    grs_vertex v[4];
    grs_vertex *vp[4];
    g3s_vector d70, d94, n88, d7c, na0;
    ulong parm = s->parm;
    uchar *model;
    grs_bitmap *bm;
    long q, t, k, old_ft;
    fix scale, dot, dx, dy, c0, c4, c8, cbc, dist, kk, A, r_ebx, r_esi, r_edi, r_ecx, W, H;
    uchar sym, flip;

    if (s->flags & 0x10)
        return;
    q = pa->x / pa->z;
    if (q < 0) q = -q;
    if (q > 1) return;
    q = pa->y / pa->z;
    if (q < 0) q = -q;
    if (q > 1) return;
    q = pb->x / pb->z;
    if (q < 0) q = -q;
    if (q > 1) return;
    q = pb->x / pb->z;                 /* sic: x again, y never tested */
    if (q < 0) q = -q;
    if (q > 1) return;
    if (pa->z <= 0 || pb->z <= 0)
        return;

    model = s->model;
    vp[0] = &v[0];
    vp[1] = &v[1];
    vp[2] = &v[2];
    vp[3] = &v[3];
    scale = fix_div(grd_cap->aspect * (long)grd_canvas->bm.h,
                    FIXL(model, 4) * (pa->z + pb->z));
    g3_vec_sub(&d70, a, &_view_position);
    g3_vec_sub(&d94, b, a);
    g3_cross_product(&n88, &d70, &d94);
    g3_vec_normalize(&n88);
    dot = g3_vec_dotprod(&n88, up);
    t = dot;
    if (dot > 0x10000)
        t = 0x10000;
    else if (dot < -0x10000)
        t = -0x10000;
    k = (long)(short)asintab[64 + (t >> 10)] << 2;
    if (g3_vec_dotprod(&d70, up) > 0)
        k = 0x10000 - k;
    else
        k += 0x30000;
    k = ((k + 0x2000) >> 14) & 15;
    if (s->flags & 1)
        k = (16 - k) & 15;
    if (model[0xC] == 1 && k > 8) {
        k = (16 - k) & 15;
        sym = 1;
    } else
        sym = 0;
    bm = (grs_bitmap *)(model + rd32(model + 0xD + k * 4));
    bm->bits = (uchar *)bm + 0x20;

    dx = pb->sx - pa->sx;
    dy = pb->sy - pa->sy;
    c4 = fix_mul(dx, FIXL(bm, 0x14));
    c0 = fix_mul(dy, FIXL(bm, 0x14));
    cbc = fix_mul(dx, FIXL(bm, 0x18));
    c8 = fix_mul(dy, FIXL(bm, 0x18));
    dist = fix_safe_pyth_dist(dx, dy, "x:\\prj\\tech\\libsrc\\skel\\render.c", 0x86);
    kk = fix_div((long)((signed char *)bm)[0x10] * scale, dist);
    flip = ((s->flags & 1) != 0) ^ sym;
    A = -(fix)(((long long)dx * kk) / grd_cap->aspect);
    if (flip) {
        r_ebx = A;
        r_esi = fix_mul(A, FIXL(bm, 0x1C));
        r_edi = fix_mul(dy, kk);
        r_ecx = fix_mul(r_edi, FIXL(bm, 0x1C));
    } else {
        r_esi = A;
        r_ebx = fix_mul(A, FIXL(bm, 0x1C));
        r_ecx = fix_mul(dy, kk);
        r_edi = fix_mul(r_ecx, FIXL(bm, 0x1C));
    }
    v[0].x = pa->sx - c4 - r_ecx;
    v[0].y = pa->sy - c0 - r_esi;
    v[1].x = pa->sx - c4 + r_edi;
    v[1].y = pa->sy - c0 + r_ebx;
    v[2].x = pb->sx + cbc + r_edi;
    v[2].y = pb->sy + c8 + r_ebx;
    v[3].x = pb->sx + cbc - r_ecx;
    v[3].y = pb->sy + c8 - r_esi;
    v[0].v = 0;
    v[1].v = 0;
    H = (fix)((ulong)(long)bm->h << 16) - 1;
    v[2].v = H;
    v[3].v = H;
    W = (fix)((ulong)(long)bm->w << 16) - 1;
    if (flip) {
        v[1].u = 0;
        v[2].u = 0;
        v[0].u = W;
        v[3].u = W;
    } else {
        v[0].u = 0;
        v[1].u = W;
        v[2].u = W;
        v[3].u = 0;
    }
    for (q = 0; q < 4; q++)
        v[q].w = v[q].i = 0;

    old_ft = grd_canvas->gc.fill_type;
    if (_g3d_light_type) {
        fix lt;

        g3_vec_sub(&d7c, a, b);
        g3_vec_normalize(&d7c);
        g3_cross_product(&na0, &d7c, &n88);
        lt = g3_light((g3s_point *)&na0, (g3s_point *)a);
        set_fill(FILL_CLUT);
        grd_canvas->gc.fill_parm = (long)((((ulong)(lt >> g3d_light_shift)) & ~0xFFul) +
                                          (ulong)g3d_light_tab);
    }
    if (parm == 0)
        draw_bm(bm, vp);
    else if (grd_canvas->gc.fill_type == 0) {
        set_fill(FILL_CLUT);
        grd_canvas->gc.fill_parm = parm;
        draw_bm(bm, vp);
    } else {
        long save = grd_canvas->gc.fill_parm;

        grd_canvas->gc.fill_parm = (long)skel_2F7AAC((uchar *)parm, (uchar *)save);
        draw_bm(bm, vp);
        grd_canvas->gc.fill_parm = save;
    }
    set_fill(old_ft);
}

/* ------------------------------------------------------------------ 3D sub-objects */

void skel_2F737C(g3s_vector *a, g3s_vector *b, g3s_vector *up)
{
    g3s_vector u = *up, d, c;
    g3s_matrix m;
    fix dot;

    g3_vec_sub(&d, b, a);
    g3_vec_normalize(&d);
    dot = g3_vec_dotprod(&d, &u);
    if (dot) {
        u.x -= fix_mul(dot, d.x);
        u.y -= fix_mul(dot, d.y);
        u.z -= fix_mul(dot, d.z);
        g3_vec_normalize(&u);
    }
    g3_cross_product(&c, &d, &u);
    m.m1 = -u.x;
    m.m2 = d.x;
    m.m3 = c.x;
    m.m4 = -u.y;
    m.m5 = d.y;
    m.m6 = c.y;
    m.m7 = -u.z;
    m.m8 = d.z;
    m.m9 = c.z;
    g3_start_object_matrix(a, &m);
}

void skel_2F7480(SkSeg *s, g3s_vector *a, g3s_vector *b, g3s_vector *up)
{
    skel_2F737C(a, b, up);
    g3_interpret_object(s->model, (void *)s->parm, 0);
    g3_end_object();
}

void skel_2F74A4(SkSeg *s, g3s_vector *a, g3s_vector *b, g3s_vector *up)
{
    uchar *obj = s->model;

    if (obj[0x47]) {
        skel_2F737C(a, b, up);
        g3_interpret_sub_object(obj, (void *)s->parm);
        g3_end_object();
    }
}

/* ------------------------------------------------------------------ render */

void skel_2F74F0(Skel *sk, long mode)
{
    g3s_phandle *pts = temp_malloc(sk->njoint * 4);
    uchar *order = temp_malloc(sk->nseg);
    g3s_vector d00, d0c, d18, d24, d30, d3c;
    g3s_phandle pt = 0;
    long i, k;

    g3_alloc_list(sk->njoint, pts);
    g3_transform_list(sk->njoint, pts, sk->joint);
#ifndef TN_WATCOM_ABI
    for (i = 0; i < sk->njoint; i++)
        if (sk->joint[i].y > port_water_low)
            port_water_low = sk->joint[i].y;    /* its feet, for "Water reflection: High" (rndsky.c) */
#endif
    switch ((uchar)mode) {
    case 0: crt_315F88(sk, order); break;
    case 1: WATCOM_CALL((skel_fn)sk->sort_fn, (long)sk, (long)order, (long)pts); break;
    case 2:
    case 3: crt_315E60(sk, order, pts); break;
    }
    switch (sk->type) {
    case 0:
        for (i = 0; i < sk->nseg; i++) {
            SkSeg *s = &sk->seg[order[i]];

            if (s->flags & 0x10)
                continue;
            grd_canvas->gc.fcolor = s->parm;
            g3_draw_line(pts[s->j0], pts[s->j1]);
        }
        break;
    case 1:
        for (i = 0; i < sk->nseg; i++) {
            SkSeg *s = &sk->seg[order[i]];
            g3s_point *pa, *pb;

            if (s->flags & 0x10)
                continue;
            pa = pts[s->j0];
            pb = pts[s->j1];
            crt_315FB0(pa->x, pa->y, pa->z, pb->x, pb->y, pb->z, (uchar **)s->model,
                       (uchar)s->parm);
        }
        break;
    case 3:
        for (i = 0; i < sk->nseg; i++) {
            SkSeg *s = &sk->seg[order[i]];

            if (s->flags & 0x10)
                continue;
            skel_2F737C(&sk->joint[s->j0], &sk->joint[s->j1], &sk->up[s->up]);
            g3_interpret_object(s->model, (void *)s->parm,
                                i ? (ulong)(order + i) : (ulong)(pts + sk->njoint));
            g3_end_object();
        }
        break;
    case 2:
        for (i = 0; i < sk->nseg; i++) {
            SkSeg *s;
            g3s_vector *a, *b, *up;
            g3s_point *pa, *pb;

            k = order[i];
            s = &sk->seg[k];
            a = &sk->joint[s->j0];
            b = &sk->joint[s->j1];
            up = &sk->up[s->up];
            pa = pts[s->j0];
            pb = pts[s->j1];
            if (skel_cb_a[k] && ((pa->z > pb->z) ^ (skel_cb_flag[k] == 0))) {
                if (pa->z > pb->z)
                    WATCOM_CALL((skel_fn)skel_cb_a[k], (long)pa, (long)a, k * 4);
                else
                    WATCOM_CALL((skel_fn)skel_cb_a[k], (long)pb, (long)b, k * 4);
                skel_cb_a[k] = 0;
            }
            if (skel_cb_b[k]) {
                g3_vec_sub(&d24, b, a);
                g3_vec_normalize(&d24);
                g3_cross_product(&d30, &d24, up);
                g3_vec_scale(&d18, up, skel_cb_parm[k][1]);
                g3_vec_scale(&d0c, &d30, skel_cb_parm[k][2]);
                g3_vec_add(&d18, &d0c, &d18);
                g3_vec_scale(&d0c, &d24, skel_cb_parm[k][0]);
                g3_vec_add(&d0c, &d0c, a);
                g3_vec_sub(&d00, &d0c, &_view_position);
                g3_vec_add(&d3c, &d0c, &d18);
                pt = g3_transform_point(&d3c);
                if (g3_vec_dotprod(&d18, &d00) > 0) {
                    WATCOM_CALL((skel_fn)skel_cb_b[k], (long)pt, (long)&d3c, k * 4);
                    g3_free_point(pt);
                    skel_cb_b[k] = 0;
                }
            }
            skel_2F6C50(s, a, b, up, pa, pb);
            if (skel_cb_a[k]) {
                if (pa->z > pb->z)
                    WATCOM_CALL((skel_fn)skel_cb_a[k], (long)pb, (long)b, k * 4);
                else
                    WATCOM_CALL((skel_fn)skel_cb_a[k], (long)pa, (long)a, k * 4);
                skel_cb_a[k] = 0;
            }
            if (skel_cb_b[k]) {
                WATCOM_CALL((skel_fn)skel_cb_b[k], (long)pt, (long)&d3c, k * 4);
                g3_free_point(pt);
                skel_cb_b[k] = 0;
            }
        }
        break;
    }
    g3_free_list(sk->njoint, pts);
    temp_free(order);
    temp_free(pts);
}

/* ------------------------------------------------------------------ positions only */

void skel_2F7B1C(Skel *sk)
{
    g3s_vector d0, d0c, d18, d24;
    long i;

    for (i = 0; i < sk->nseg; i++) {
        SkSeg *s = &sk->seg[i];

        if (skel_cb_a[i]) {
            g3s_vector *src = skel_cb_flag[i] == 0 ? &sk->joint[s->j0] : &sk->joint[s->j1];

            skel_pos[skel_cb_a[i] - 1] = *src;
            skel_cb_a[i] = 0;
        }
        if (sk->type == 3) {
            g3s_vector *a = &sk->joint[s->j0], *b = &sk->joint[s->j1], *up = &sk->up[s->up];

            if (s->model[0x47]) {
                skel_2F737C(a, b, up);
                g3_interpret_sub_object(s->model, (void *)s->parm);
                g3_end_object();
            }
        }
        if (skel_cb_b[i]) {
            g3s_vector *up = &sk->up[s->up];

            g3_vec_sub(&d0, &sk->joint[s->j1], &sk->joint[s->j0]);
            g3_vec_normalize(&d0);
            g3_cross_product(&d24, &d0, up);
            g3_vec_scale(&d18, up, skel_cb_parm[i][1]);
            g3_vec_scale(&d0c, &d24, skel_cb_parm[i][2]);
            g3_vec_add(&d18, &d0c, &d18);
            g3_vec_scale(&d0c, &d0, skel_cb_parm[i][0]);
            g3_vec_add(&d0c, &d0c, &sk->joint[s->j0]);
            g3_vec_add(&skel_pos[skel_cb_b[i] - 1], &d0c, &d18);
            skel_cb_b[i] = 0;
        }
    }
}
