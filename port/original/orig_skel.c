/* orig_skel.c -- C versions of the skeleton renderer's helpers that the decompilation kept as
 * machine code (Terra Nova's own C, 0x315E60..0x3164FF), written for the standalone build (the
 * verification build still runs the original code for them).  Each follows the original
 * instruction by instruction; the address is the original's.
 *
 *   0x315E60 crt_315E60   depth sort of the segments, farthest first
 *   0x315F88 crt_315F88   identity order
 *   0x315FB0 crt_315FB0   draws a segment as an eight-sided prism
 */
#include "g6_b.h"
#include "3d_a.h"
#include "3d_b.h"

void crt_315F88(Skel *sk, uchar *order)
{
    int i;

    for (i = 0; i < sk->nseg; i++)
        *order++ = (uchar)i;
}

/* Sum of the two joints' depths per segment, then an insertion sort that keeps segments of
 * equal depth in their order.  (The original's depth array holds 32 segments.) */
void crt_315E60(Skel *sk, uchar *order, g3s_phandle *pts)
{
    long depth[256];
    int i, j, k;

    for (i = 0; i < sk->nseg; i++)
        depth[i] = pts[sk->seg[i].j0]->z + pts[sk->seg[i].j1]->z;
    for (i = 0; i < sk->nseg; i++) {
        for (j = 0; j < i; j++)
            if (depth[i] > depth[order[j]]) {
                for (k = i; k > j; k--)
                    order[k] = order[k - 1];
                break;
            }
        order[j] = (uchar)i;
    }
}

static fix fmul(fix a, fix b)
{
    return (fix)(((long long)a * b) >> 16);
}

/* edx:eax idiv d; a quotient that does not fit traps like the CPU does (the port's
 * divide-error handler then gives the original handler's result) */
static long idiv64(long long n, long d)
{
    long q, hi = (long)(n >> 32);
    __asm__ volatile("idivl %3" : "=a"(q), "+d"(hi) : "0"((long)n), "rm"(d) : "cc");
    return q;
}

/* From (x0,y0,z0) to (x1,y1,z1) in view space, radius r: a vector u across the segment (from
 * the two smaller components of d, the third solving u.d = 0), v = d x u / |d|, and the eight
 * directions u, (u+v)/sqrt2, v, (v-u)/sqrt2 and their negatives; the 16 points around both ends
 * are projected and the eight sides drawn as quads in colour c. */
void crt_315FB0(fix x0, fix y0, fix z0, fix x1, fix y1, fix z1, uchar **model, long c)
{
    fix r = (fix)(long)model;           /* the segment's radius (seg->model) */
    g3s_phandle p[16], q[4];
    fix dx = x1 - x0, dy = y1 - y0, dz = z1 - z0, adx, ady, adz, s, n, inv, k;
    g3s_point *u, *v, *a, *b;
    int i;

    g3_alloc_list(16, p);
    adx = dx < 0 ? -dx : dx;
    ady = dy < 0 ? -dy : dy;
    adz = dz < 0 ? -dz : dz;
    n = fix_sqrt(fmul(dz, dz) + (fmul(dx, dx) + fmul(dy, dy)));
    inv = idiv64(1LL << 32, n);
    n = fix_sqrt(0x20000);
    k = idiv64(1LL << 32, n);
    u = p[0];
    if (ady > adz && ady > adx) {
        s = dx + dz;
        n = fix_sqrt(fmul(s, s) + 2 * fmul(dy, dy));
        u->z = u->x = idiv64((long long)dy * r, n);
        u->y = -idiv64((long long)u->z * s, dy);
    } else if (adz > adx) {
        s = dx + dy;
        n = fix_sqrt(fmul(s, s) + 2 * fmul(dz, dz));
        u->x = u->y = idiv64((long long)dz * r, n);
        u->z = -idiv64((long long)u->x * s, dz);
    } else {
        s = dy + dz;
        n = fix_sqrt(fmul(s, s) + 2 * fmul(dx, dx));
        u->y = u->z = idiv64((long long)dx * r, n);
        u->x = -idiv64((long long)u->y * s, dx);
    }
    v = p[2];
    v->x = fmul(fmul(dy, u->z) - fmul(dz, u->y), inv);
    v->y = fmul(fmul(dz, u->x) - fmul(dx, u->z), inv);
    v->z = fmul(fmul(dx, u->y) - fmul(dy, u->x), inv);
    a = p[1];
    a->x = fmul(u->x + v->x, k);
    a->y = fmul(u->y + v->y, k);
    a->z = fmul(u->z + v->z, k);
    b = p[3];
    b->x = fmul(v->x - u->x, k);
    b->y = fmul(v->y - u->y, k);
    b->z = fmul(v->z - u->z, k);
    for (i = 0; i < 4; i++) {
        p[i + 4]->x = -p[i]->x;
        p[i + 4]->y = -p[i]->y;
        p[i + 4]->z = -p[i]->z;
    }
    for (i = 0; i < 8; i++) {
        p[i + 8]->p3_flags = p[i + 8]->codes = 0;
        p[i + 8]->x = p[i]->x + x1;
        p[i + 8]->y = p[i]->y + y1;
        p[i + 8]->z = p[i]->z + z1;
        p[i]->p3_flags = p[i]->codes = 0;
        p[i]->x += x0;
        p[i]->y += y0;
        p[i]->z += z0;
    }
    g3_project_list(16, p);
    grd_canvas->gc.fcolor = (uchar)c;
    for (i = 0; i < 8; i++) {
        q[0] = p[i];
        q[1] = p[(i + 1) % 8];
        q[2] = p[8 + (i + 1) % 8];
        q[3] = p[8 + i];
        g3_draw_poly((uchar)c, 4, q);
    }
    g3_free_list(16, p);
}
