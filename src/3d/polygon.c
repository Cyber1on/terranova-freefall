/* polygon.c -- x:\prj\tech\libsrc\3d\polygon.asm (Terra Nova v1.09; RCS string at 0x3635CE):
 * polygons, lines, points and facing checks.  Decompiled from __FF.EXE (hand-written asm).
 *
 *   0x2F953C g3_check_codes              0x2F9550 g3_check_poly_facing
 *   0x2F9587 g3_check_and_draw_cpoly     0x2F9590 g3_check_and_draw_tluc_spoly
 *   0x2F9599 g3_check_and_draw_spoly     0x2F95A2 g3_check_and_draw_tluc_poly
 *   0x2F95AB g3_check_and_draw_poly      0x2F95CA g3_draw_cpoly
 *   0x2F95D3 g3_draw_tluc_spoly          0x2F95DC g3_draw_spoly
 *   0x2F95E5 g3_draw_tluc_poly           0x2F95EE g3_draw_poly
 *   0x2F95F5 draw_poly_common            0x2F974D g3_draw_point
 *   0x2F9781 g3_draw_cline               0x2F97C8 g3_draw_sline
 *   0x2F97D8 g3_draw_line                0x2F99AD g3_check_normal_facing
 *
 * Registers (original): g3_check_codes ecx = n, esi = list -> bx = codes (bl or, bh and);
 * g3_check_poly_facing eax, edx, ebx = points -> al (and SF) = 1 when the polygon faces the
 * viewer; the poly routines eax = color, ecx = n, esi = point list -> eax (0 drawn, 0x10 not);
 * g3_draw_point esi; the line routines esi, edi; g3_check_normal_facing esi = point on the
 * plane, edi = normal -> al.  All may change every register.
 *
 * The polygon types (g3d_poly_type) select the 2D canvas function (index table 0x364428 plus
 * fill.asm's offset 0x365348) and the clipper's interpolation flags (0x36443C):
 *   0 flat (0x19), 1 translucent (0x1F), 2 gouraud spoly (0x1B, i), 3 tluc spoly (0x22, i),
 *   4 rgb cpoly (0x1D, rgb in u/v/w).
 * Quirks kept:
 *  - The g3_check_and_draw_* routines return eax = color with al cleared when the polygon faces
 *    away; the facing routines return the low dword of the 64-bit dot product with al = sign.
 *  - g3_draw_cline with two equal colors goes to the line code without resetting the clip
 *    flags (g3d_clip_flags keeps its old value).
 *  - g3_draw_sline uses gr_fix_sline (which clips) only when the second point came out of the
 *    clipper with non-zero codes; otherwise the unclipped uline function.
 *  - After drawing a line, the routine pushes g3d_line_free_n (0x364420) entries of
 *    g3d_line_free_list (0x364410) back on the point free list; nothing in TN ever writes them
 *    (left over from an older clipper that allocated points), so with the initial 0 it does
 *    nothing.  The plain-line vertices are only {x, y} on the stack.
 */
#include "3d_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
grs_vertex g3d_poly_vbuf[100];
grs_vertex *g3d_poly_vlist[100];
long g3d_poly_n;
g3s_phandle g3d_poly_src[100];
g3s_phandle g3d_poly_clipped[100];
g3s_vector g3d_poly_normal;
g3s_phandle g3d_line_free_list[4];
long g3d_line_free_n;
long g3d_line_color;
long g3d_poly_func_index[5] = { 0x19, 0x1F, 0x1B, 0x22, 0x1D };
uchar g3d_poly_clip_flags[6] = { 0, 0, 4, 4, 0x10, 0 };
#endif

/* or/and of the codes of n points: returns or | and << 8 (bx) */
ushort g3_check_codes(int n, g3s_phandle *list)
{
    uchar or = 0, and = 0xFF;

    do {
        uchar c = (*list++)->codes;
        and &= c;
        or |= c;
    } while (--n);
    return or | (and << 8);
}

/* sign of (p0 . (p1 - p0) x (p2 - p1)) as a 64-bit sum: the low dword with al = 1 when negative */
static long facing_result(long long s)
{
    return (long)(((ulong)s & ~0xFFul) | (s < 0));
}

long g3_check_poly_facing(g3s_point *p0, g3s_point *p1, g3s_point *p2)
{
    long long s;

    g3b_compute_normal_quick(&g3d_poly_normal, (g3s_vector *)p0, (g3s_vector *)p1, (g3s_vector *)p2);
    s = (long long)p0->x * g3d_poly_normal.x + (long long)p0->y * g3d_poly_normal.y
        + (long long)p0->z * g3d_poly_normal.z;
    return facing_result(s);
}

static long check_and_draw(uchar type, long c, int n, g3s_phandle *list)
{
    g3d_poly_type = type;
    if (g3_check_poly_facing(list[0], list[1], list[2]) & 1)
        return draw_poly_common(c, n, list);
    return c & ~0xFF;
}

long g3_check_and_draw_cpoly(long c, int n, g3s_phandle *list) { return check_and_draw(G3_CPOLY, c, n, list); }
long g3_check_and_draw_tluc_spoly(long c, int n, g3s_phandle *list) { return check_and_draw(G3_TLUC_SPOLY, c, n, list); }
long g3_check_and_draw_spoly(long c, int n, g3s_phandle *list) { return check_and_draw(G3_SPOLY, c, n, list); }
long g3_check_and_draw_tluc_poly(long c, int n, g3s_phandle *list) { return check_and_draw(G3_TLUC_POLY, c, n, list); }
long g3_check_and_draw_poly(long c, int n, g3s_phandle *list) { return check_and_draw(G3_POLY, c, n, list); }

long g3_draw_cpoly(long c, int n, g3s_phandle *list) { g3d_poly_type = G3_CPOLY; return draw_poly_common(c, n, list); }
long g3_draw_tluc_spoly(long c, int n, g3s_phandle *list) { g3d_poly_type = G3_TLUC_SPOLY; return draw_poly_common(c, n, list); }
long g3_draw_spoly(long c, int n, g3s_phandle *list) { g3d_poly_type = G3_SPOLY; return draw_poly_common(c, n, list); }
long g3_draw_tluc_poly(long c, int n, g3s_phandle *list) { g3d_poly_type = G3_TLUC_POLY; return draw_poly_common(c, n, list); }
long g3_draw_poly(long c, int n, g3s_phandle *list) { g3d_poly_type = G3_POLY; return draw_poly_common(c, n, list); }

/* Clips, projects and draws a polygon of type g3d_poly_type.  Returns 0, or CLIP_ALL (0x10)
 * when it is off screen. */
long draw_poly_common(long c, int n, g3s_phandle *list)
{
    int i, m;
    ushort codes;

    g3d_poly_color = c;
    g3d_clip_flags = g3d_poly_clip_flags[g3d_poly_type];
    codes = g3_check_codes(n, list);
    if (codes >> 8)
        return CLIP_ALL;
    for (i = 0; i < n; i++)
        g3d_poly_src[i] = list[i];
    m = g3_clip_polygon(n, g3d_poly_src, g3d_poly_clipped);
    if (m == 0)
        return CLIP_ALL;
    g3d_poly_n = m;
    for (i = 0; i < m; i++) {
        g3s_point *p = g3d_poly_clipped[i];
        if (!(p->p3_flags & PF_PROJECTED))
            g3b_project_point(p);
        g3d_poly_vbuf[i].x = p->sx;
        g3d_poly_vbuf[i].y = p->sy;
        g3d_poly_vlist[i] = &g3d_poly_vbuf[i];
    }
    if ((signed char)g3d_poly_type >= G3_SPOLY) {
        if ((signed char)g3d_poly_type >= G3_CPOLY) {
            for (i = 0; i < m; i++) {           /* 10:11:11 rgb -> u, v, w */
                ulong rgb = g3d_poly_clipped[i]->rgb;
                g3d_poly_vbuf[i].u = (rgb & 0x3FF) << 14;
                g3d_poly_vbuf[i].v = (rgb & 0x1FFC00) << 3;
                g3d_poly_vbuf[i].w = (rgb & 0xFFE00000) >> 8;
            }
        } else {
            for (i = 0; i < m; i++)
                g3d_poly_vbuf[i].i = (ushort)g3d_poly_clipped[i]->i << 8;
        }
    }
    WATCOM_CALL(grd_canvas_table[g3d_poly_func_index[g3d_poly_type] + g3d_fill_func_ofs],
                g3d_poly_color, m, (long)g3d_poly_vlist);
    return 0;
}

/* Draws one point (2D canvas function 10 at the rounded screen position). */
long g3_draw_point(g3s_point *p)
{
    if (p->codes)
        return CLIP_ALL;
    if (!(p->p3_flags & PF_PROJECTED))
        g3b_project_point(p);
    return WATCOM_CALL(grd_canvas_table[10], (p->sx + 0x8000) >> 16, (p->sy + 0x8000) >> 16);
}

static long draw_line_common(g3s_point *p0, g3s_point *p1);

/* Line between two rgb points: gouraud rgb line if the colors differ, else a plain line in the
 * nearest palette color. */
long g3_draw_cline(g3s_point *p0, g3s_point *p1)
{
    ulong rgb = p0->rgb;

    if (rgb != p1->rgb) {
        g3d_poly_type = 1;
        g3d_clip_flags = 0x10;
    } else {
        ulong idx = ((rgb >> 5) & 0x1F) | ((rgb >> 11) & 0x3E0) | ((rgb >> 17) & 0x7C00);
        g3d_line_color = grd_ipal[idx];
        g3d_poly_type = 0;              /* g3d_clip_flags left alone (quirk) */
    }
    return draw_line_common(p0, p1);
}

/* Gouraud (intensity) line. */
long g3_draw_sline(g3s_point *p0, g3s_point *p1)
{
    g3d_poly_type = G3_SLINE;
    g3d_clip_flags = 4;
    return draw_line_common(p0, p1);
}

/* Line in the canvas foreground color. */
long g3_draw_line(g3s_point *p0, g3s_point *p1)
{
    g3d_clip_flags = 0;
    g3d_line_color = grd_canvas->gc.fcolor;
    g3d_poly_type = 0;
    return draw_line_common(p0, p1);
}

static long draw_line_common(g3s_point *p0, g3s_point *p1)
{
    long r;
    int k;
    uchar codes1;

    g3d_poly_src[0] = p0;
    g3d_poly_src[1] = p1;
    if ((uchar)g3_clip_line(g3d_poly_src, g3d_poly_clipped) == CLIP_ALL)
        return CLIP_ALL;
    p0 = g3d_poly_clipped[0];
    p1 = g3d_poly_clipped[1];
    codes1 = p1->codes;
    if (!(p0->p3_flags & PF_PROJECTED))
        g3b_project_point(p0);
    if (!(p1->p3_flags & PF_PROJECTED))
        g3b_project_point(p1);

    if (g3d_poly_type == 0) {
#ifdef TN_WATCOM_ABI
        fix v0[2], v1[2];               /* only x, y */
#else
        fix v0[6] = { 0 }, v1[6] = { 0 };   /* x, y; the line routine copies a whole grs_vertex (G14) */
#endif
        v0[0] = p0->sx; v0[1] = p0->sy;
        v1[0] = p1->sx; v1[1] = p1->sy;
        WATCOM_CALL(grd_line_clip_fill_vector[6], g3d_line_color, grd_canvas->gc.fill_parm, (long)v0, (long)v1);
        r = 0;
    } else if ((signed char)g3d_poly_type > 0) {
        fix v0[5], v1[5];               /* x, y, r, g, b */
        v0[0] = p0->sx; v0[1] = p0->sy;
        v0[2] = (p0->rgb >> 2) & 0xFF; v0[3] = (p0->rgb >> 13) & 0xFF; v0[4] = (p0->rgb >> 24) & 0xFF;
        v1[0] = p1->sx; v1[1] = p1->sy;
        v1[2] = (p1->rgb >> 2) & 0xFF; v1[3] = (p1->rgb >> 13) & 0xFF; v1[4] = (p1->rgb >> 24) & 0xFF;
        WATCOM_CALL(grd_line_clip_fill_vector[8], grd_canvas->gc.fcolor, grd_canvas->gc.fill_parm, (long)v0, (long)v1);
        r = 0;
    } else {
        fix i0 = (ushort)p0->i << 8, i1 = (ushort)p1->i << 8;
        if (codes1) {
            r = gr_fix_sline(p0->sx, p0->sy, i0, p1->sx, p1->sy, i1);
        } else {
            grs_vertex v0 = { p0->sx, p0->sy, 0, 0, 0, i0 };
            grs_vertex v1 = { p1->sx, p1->sy, 0, 0, 0, i1 };
            WATCOM_CALL(grd_uline_fill_vector[4], grd_canvas->gc.fcolor, grd_canvas->gc.fill_parm, (long)&v0, (long)&v1);
            r = 0;
        }
    }
    for (k = g3d_line_free_n - 1; k >= 0; k--) {
        g3d_line_free_list[k]->next = first_free;
        first_free = g3d_line_free_list[k];
    }
    return r;
}

/* Does the plane through v with this normal face the viewer?  (v - view_position) . normal < 0
 * as a 64-bit sum: low dword with al = 1 when negative. */
long g3_check_normal_facing(g3s_vector *v, g3s_vector *normal)
{
    long long s = (long long)(v->x - _view_position.x) * normal->x
        + (long long)(v->y - _view_position.y) * normal->y
        + (long long)(v->z - _view_position.z) * normal->z;
    return facing_result(s);
}
