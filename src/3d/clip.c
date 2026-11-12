/* clip.c -- x:\prj\tech\libsrc\3d\clip.c (Terra Nova v1.09): polygon and line clipping
 * against the view pyramid ("ripped off from freefall and hacked to use inverted y").
 * Decompiled from __FF.EXE (Watcom C in the original).
 *
 *   0x316510 clip_restore_uv (ours)    0x316554 clip_save_uv (ours)
 *   0x31659C g3_clip_line              0x316A74 g3_clip_polygon
 *   0x317120 intersect                 0x317360 clip_behind (ours, Shock g3_back_intersect)
 *   0x3174C4 clip_left                 0x31760C clip_top
 *   0x317754 clip_right                0x3178A4 clip_bottom
 *   0x3179D4 clip_project_list (ours, Shock project_point)
 * (0x317092 is an interior label of g3_clip_polygon, not a function.)
 *
 * Registers: g3_clip_line is called through `#pragma aux parm [esi] [edi]` and g3_clip_polygon
 * through `parm [ecx] [esi] [edi]` (both return eax); the rest are plain Watcom C.
 *
 * Same algorithm as System Shock's CLIP.C, with these Terra Nova differences:
 *  - the intersect routines first swap s and e so that s is the point with the larger z (and
 *    swap back afterwards), so a segment is cut the same way in both directions;
 *  - g3d_clip_flags (0x362434, set by the polygon / tmap code) selects what intersect
 *    interpolates: 0x10 rgb, else 1 u and v, 4 i;
 *  - TN's PF_UVSAVED (0x80) points carry a pointer to 16.16 u, v (tmap: clip_save_uv); a new
 *    point made from such a point gets the uv slot clip_uv_buf[tnum] (tnum already counted up);
 *  - clip points are 0x1C bytes apart in clip_tbuff (the size of the old point struct; the
 *    fields written all lie below 0x1C).
 * Quirks kept: in g3_clip_polygon, y of a clipped point is projected only when CC_OFF_X is set
 * and CC_OFF_Y clear (Shock's && bug); otherwise sy = (y < 0) ? canvas h : 0.  Not reproducible:
 * when the codes have none of the plane bits (only 0x10/0x20/0x40) g3_clip_polygon returns its
 * caller's ebp; the C returns n then.
 */
#include "3d_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
long clip_uv_buf[32][2];
uchar clip_tbuff[32][28];
g3s_point *clip_e, *clip_s, *clip_tmp, *clip_swapped;
fix clip_den, clip_a, clip_b, clip_c, clip_num;
long clip_tnum;
fix clip_d = 1;
#endif

#define CODES(p) ((p)->codes)
#define MULDIV(a) ((fix)(((long long)(a) * clip_num) / clip_den))

/* Puts back the 8.8 u, v of points whose uv was saved by clip_save_uv. */
void clip_restore_uv(int n, g3s_phandle *list)
{
    int i;

    for (i = 0; i < n; i++, list++) {
        if ((*list)->p3_flags & PF_UVSAVED) {
            long *uv = (long *)(*list)->uvp;
            long u = uv[0] >> 16, v = uv[1] >> 16;
            (*list)->u = u;
            (*list)->v = v;
            (*list)->p3_flags &= ~PF_UVSAVED;
        }
    }
}

/* Saves u, v of each point as 16.16 in uvs[i] and makes the point point there. */
void clip_save_uv(int n, g3s_phandle *list, long (*uvs)[2])
{
    int i;

    for (i = 0; i < n; i++, list++, uvs++) {
        if (!((*list)->p3_flags & PF_UVSAVED)) {
            (*uvs)[0] = (long)(*list)->u << 16;
            (*uvs)[1] = (long)(*list)->v << 16;
            (*list)->uvp = *uvs;
            (*list)->p3_flags |= PF_UVSAVED;
        }
    }
}

/* Fills in flags and the interpolated u, v, i or rgb of the new point clip_tmp at clip_num /
 * clip_den of the way from clip_s to clip_e. */
void intersect(void)
{
    g3s_point *s = clip_s, *e = clip_e, *t = clip_tmp;
    uchar f = g3d_clip_flags;

    t->p3_flags = s->p3_flags | PF_CLIPPNT | PF_PROJECTED;
    if (f & 0x10) {
        ulong c = s->rgb, ce = e->rgb;
        fix rs = (c << 12) & 0x3FF000, gs = (c << 1) & 0x3FF000, bs = (c >> 10) & 0x3FF000;
        fix re = (ce << 12) & 0x3FF000, ge = (ce << 1) & 0x3FF000, be = (ce >> 10) & 0x3FF000;
        rs += MULDIV(re - rs);
        gs += MULDIV(ge - gs);
        bs += MULDIV(be - bs);
        t->rgb = ((rs & 0x3FF000) >> 12) | ((gs & 0x3FF000) >> 1) | ((bs & 0x3FF000) << 10);
    } else if (t->p3_flags & PF_UVSAVED) {
        long *uv = clip_uv_buf[clip_tnum];
        long *se = (long *)e->uvp, *ss = (long *)s->uvp;
        t->uvp = uv;
        if (g3d_clip_flags & 1)
            uv[0] = ss[0] + MULDIV(se[0] - ss[0]);
        if (g3d_clip_flags & 1)
            uv[1] = ss[1] + MULDIV(se[1] - ss[1]);
    } else {
        if (f & 1)
            t->u = s->u + MULDIV(e->u - s->u);
        if (g3d_clip_flags & 1)
            t->v = s->v + MULDIV(e->v - s->v);
    }
    if (g3d_clip_flags & 4)
        t->i = s->i + MULDIV(e->i - s->i);
}

/* common head of the plane clippers: s = the point with the larger z, new point slot */
static void clip_begin(void)
{
    clip_swapped = NULL;
    if (clip_e->z > clip_s->z) {
        g3s_point *t = clip_e;
        clip_swapped = t;
        clip_e = clip_s;
        clip_s = t;
    }
    clip_tmp = (g3s_point *)(clip_tbuff[0] + 28 * clip_tnum);
    clip_tnum++;
}

static void clip_end(void)
{
    intersect();
    if (clip_swapped) {
        clip_s = clip_e;
        clip_e = clip_swapped;
    }
}

/* z = clip_d plane */
void clip_behind(void)
{
    g3s_point *s, *e, *t;
    uchar c;

    clip_begin();
    s = clip_s; e = clip_e; t = clip_tmp;
    clip_a = e->x - s->x;
    clip_num = clip_d - s->z;
    clip_b = e->y - s->y;
    clip_c = e->z - s->z;
    clip_den = clip_c;
    t->x = MULDIV(clip_a) + s->x;
    t->y = MULDIV(clip_b) + s->y;
    t->z = clip_d;
    if (t->y >= t->z)
        c = CC_OFF_TOP;
    else if (-t->z >= t->y)
        c = CC_OFF_BOT;
    else
        c = 0;
    if (t->x >= t->z)
        c |= CC_OFF_RIGHT;
    else if (-t->z >= t->x)
        c |= CC_OFF_LEFT;
    t->codes = c;
    clip_end();
}

/* x = -z */
void clip_left(void)
{
    g3s_point *s, *e, *t;
    uchar c;

    clip_begin();
    s = clip_s; e = clip_e; t = clip_tmp;
    clip_a = e->x - s->x;
    clip_b = e->y - s->y;
    clip_c = e->z - s->z;
    clip_num = -s->z - s->x;
    clip_den = clip_a + clip_c;
    t->y = MULDIV(clip_b) + s->y;
    t->z = MULDIV(clip_c) + s->z;
    t->x = -t->z;
    if (t->z <= t->y)
        c = CC_OFF_TOP;
    else if (-t->z >= t->y)
        c = CC_OFF_BOT;
    else
        c = 0;
    t->codes = c | CC_OFF_X;
    clip_end();
}

/* y = z */
void clip_top(void)
{
    g3s_point *s, *e, *t;

    clip_begin();
    s = clip_s; e = clip_e; t = clip_tmp;
    clip_a = e->x - s->x;
    clip_c = e->z - s->z;
    clip_b = s->y - e->y;
    clip_den = clip_b + clip_c;
    clip_num = s->y - s->z;
    t->x = MULDIV(clip_a) + s->x;
    t->z = MULDIV(clip_c) + s->z;
    t->y = t->z;
    t->codes = (t->x >= t->z ? CC_OFF_RIGHT : 0) | CC_OFF_Y | (CODES(s) & CODES(e) & CC_OFF_X);
    clip_end();
}

/* x = z */
void clip_right(void)
{
    g3s_point *s, *e, *t;

    clip_begin();
    s = clip_s; e = clip_e; t = clip_tmp;
    clip_a = e->x - s->x;
    clip_c = e->z - s->z;
    clip_b = e->y - s->y;
    clip_num = s->z - s->x;
    clip_den = clip_a - clip_c;
    t->y = MULDIV(clip_b) + s->y;
    t->z = MULDIV(clip_c) + s->z;
    t->x = t->z;
    t->codes = (-t->z >= t->y ? CC_OFF_BOT : 0) | CC_OFF_X | (CODES(s) & CODES(e) & CC_OFF_Y);
    clip_end();
}

/* y = -z */
void clip_bottom(void)
{
    g3s_point *s, *e, *t;

    clip_begin();
    s = clip_s; e = clip_e; t = clip_tmp;
    clip_a = e->x - s->x;
    clip_c = e->z - s->z;
    clip_b = s->y - e->y;
    clip_den = clip_b - clip_c;
    clip_num = s->y + s->z;
    t->x = MULDIV(clip_a) + s->x;
    t->z = MULDIV(clip_c) + s->z;
    t->y = -t->z;
    t->codes = CC_OFF_Y | (CODES(s) & CODES(e) & CC_OFF_X);
    clip_end();
}

/* screen coordinates of the clip points among list[0..n-1] (edges are snapped to the canvas) */
static void project_clipped(g3s_phandle *list, int n, int poly)
{
    int i;

    for (i = 0; i < n; i++) {
        g3s_point *p;
        uchar c;

        clip_tmp = p = list[i];
        if (!(p->p3_flags & PF_CLIPPNT))
            continue;
        c = p->codes;
        if (!(c & CC_OFF_X) || (c & CC_OFF_Y))
            p->sx = fix_mul(_scrw, fix_div(p->x, p->z) + 0x10000);
        else
            p->sx = (p->x > 0) ? (long)grd_canvas->bm.w << 16 : 0;
        c = p->codes;
        if (poly ? (!(c & CC_OFF_Y) && (c & CC_OFF_X)) : (!(c & CC_OFF_Y) || (c & CC_OFF_X)))
            p->sy = fix_mul(_scrh, 0x10000 - fix_div(p->y, p->z));
        else
            p->sy = (p->y < 0) ? (long)grd_canvas->bm.h << 16 : 0;
    }
}

/* one plane pass over a 2-point line: returns FALSE when the result is all out (test mask m) */
typedef void (*clipper)(void);

static int line_pass(g3s_phandle *tmps, g3s_phandle *tmpd, uchar bit, clipper f, uchar *cc, uchar m)
{
    int j = 0;

    clip_s = tmps[0];
    clip_e = tmps[1];
    if (!(CODES(clip_s) & bit))
        tmpd[j++] = clip_s;
    if ((CODES(clip_s) ^ CODES(clip_e)) & bit) {
        f();
        tmpd[j++] = clip_tmp;
    }
    if (!(CODES(clip_e) & bit))
        tmpd[j] = clip_e;
    *cc = CODES(tmpd[0]) | CODES(tmpd[1]);
    return !(CODES(tmpd[0]) & CODES(tmpd[1]) & m);
}

int g3_clip_line(g3s_phandle *src, g3s_phandle *dest)
{
    g3s_point *tmp0[32], *tmp1[32];
    g3s_phandle *tmps, *tmpd;
    int b;
    uchar cc, ca;

    cc = CODES(src[0]) | CODES(src[1]);
    ca = CODES(src[0]) & CODES(src[1]);
    if (cc == 0) {
        dest[0] = src[0];
        dest[1] = src[1];
        return CLIP_NONE;
    }
    if (ca)
        return CLIP_ALL;
    clip_tnum = 0;
    b = 0;
    tmps = src;
    tmpd = tmp0;
    if ((signed char)cc < 0) {
        if (!line_pass(tmps, tmpd, CC_BEHIND, clip_behind, &cc, 0xFF))
            return CLIP_ALL;
        tmps = tmpd;
        b = 1 - b;
        tmpd = b == 0 ? tmp0 : tmp1;
    }
    if (cc & CC_OFF_LEFT) {
        if (!line_pass(tmps, tmpd, CC_OFF_LEFT, clip_left, &cc, 0xFF))
            return CLIP_ALL;
        tmps = tmpd;
        b = 1 - b;
        tmpd = b == 0 ? tmp0 : tmp1;
    }
    if (cc & CC_OFF_TOP) {
        if (!line_pass(tmps, tmpd, CC_OFF_TOP, clip_top, &cc, 0xFF))
            return CLIP_ALL;
        tmps = tmpd;
        b = 1 - b;
        tmpd = b == 0 ? tmp0 : tmp1;
    }
    if (cc & CC_OFF_RIGHT) {
        if (!line_pass(tmps, tmpd, CC_OFF_RIGHT, clip_right, &cc, CC_MASK))
            return CLIP_ALL;
        tmps = tmpd;
        b = 1 - b;
        tmpd = b == 0 ? tmp0 : tmp1;
    }
    if (cc & CC_OFF_BOT) {
        if (!line_pass(tmps, tmpd, CC_OFF_BOT, clip_bottom, &cc, CC_MASK))
            return CLIP_ALL;
        tmps = tmpd;
    }
    dest[0] = tmps[0];
    dest[1] = tmps[1];
    project_clipped(dest, 2, 0);
    return CLIP_NONE;
}

/* one plane pass over a polygon: returns the new point count */
static int poly_pass(int n, g3s_phandle *tmps, g3s_phandle *tmpd, uchar bit, clipper f)
{
    int i, j = 0;

    for (i = 0; i < n; i++) {
        clip_s = tmps[i];
        clip_e = tmps[i + 1 == n ? 0 : i + 1];
        if (!(CODES(clip_s) & bit))
            tmpd[j++] = tmps[i];
        if ((CODES(clip_s) ^ CODES(clip_e)) & bit) {
            f();
            tmpd[j++] = clip_tmp;
        }
    }
    return j;
}

static uchar or_codes(int n, g3s_phandle *l)
{
    uchar cc = 0;
    int i;

    for (i = 0; i < n; i++)
        cc |= CODES(l[i]);
    return cc;
}

int g3_clip_polygon(int n, g3s_phandle *src, g3s_phandle *dest)
{
    g3s_point *tmp0[32], *tmp1[32];
    g3s_phandle *tmps, *tmpd;
    int b, i, j = n;
    uchar cc;

    cc = or_codes(n, src);
    if (cc == 0) {
        for (i = 0; i < n; i++)
            dest[i] = src[i];
        return n;
    }
    clip_tnum = 0;
    b = 0;
    tmps = src;
    tmpd = tmp0;
    if ((signed char)cc < 0) {
        n = j = poly_pass(n, tmps, tmpd, CC_BEHIND, clip_behind);
        cc = or_codes(n, tmpd);
        b = 1 - b;
        tmps = tmpd;
        tmpd = b == 0 ? tmp0 : tmp1;
    }
    if (cc & CC_OFF_LEFT) {
        n = j = poly_pass(n, tmps, tmpd, CC_OFF_LEFT, clip_left);
        cc = or_codes(n, tmpd);
        b = 1 - b;
        tmps = tmpd;
        tmpd = b == 0 ? tmp0 : tmp1;
    }
    if (cc & CC_OFF_TOP) {
        n = j = poly_pass(n, tmps, tmpd, CC_OFF_TOP, clip_top);
        cc = or_codes(n, tmpd);
        b = 1 - b;
        tmps = tmpd;
        tmpd = b == 0 ? tmp0 : tmp1;
    }
    if (cc & CC_OFF_RIGHT) {
        n = j = poly_pass(n, tmps, tmpd, CC_OFF_RIGHT, clip_right);
        cc = or_codes(n, tmpd);
        b = 1 - b;
        tmps = tmpd;
        tmpd = b == 0 ? tmp0 : tmp1;
    }
    if (cc & CC_OFF_BOT) {
        n = j = poly_pass(n, tmps, tmpd, CC_OFF_BOT, clip_bottom);
        tmps = tmpd;
    }
    for (i = 0; i < n; i++)
        dest[i] = tmps[i];
    project_clipped(dest, n, 1);
    return j;
}

/* Codes and projection of the points of list that are not projected yet. */
void clip_project_list(g3s_phandle *list, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        g3s_point *p = list[i];
        uchar c = 0;

        if (p->p3_flags & PF_PROJECTED)
            continue;
        if (p->z < 0)
            c = CC_BEHIND;
        if (p->x > p->z)
            c |= CC_OFF_RIGHT;
        else if (-p->z >= p->x)
            c |= CC_OFF_LEFT;
        if (p->y >= p->z)
            c |= CC_OFF_TOP;
        else if (-p->z >= p->y)
            c |= CC_OFF_BOT;
        p->codes = c;
        if (c == 0) {
            p->sx = fix_mul(_scrw, fix_div(p->x, p->z) + 0x10000);
            p->sy = fix_mul(_scrh, 0x10000 - fix_div(p->y, p->z));
            p->p3_flags |= PF_PROJECTED;
        }
    }
}
