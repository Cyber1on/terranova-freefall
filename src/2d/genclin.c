/* genclin.c -- x:\prj\tech\libsrc\2d\genclin.c, genwclin.c (Terra Nova v1.09): clipped
 * colour-shaded and wire-polygon line wrappers.  Decompiled from __FF.EXE.
 *
 *   0x324E50 gen_fix_cline(x0,y0,c0,x1,y1,c1)  6 args (x1,y1... on the stack, ret 8)
 *   0x324EE0 gri_cline_clip_fill
 *   0x324F40 gri_wire_poly_cline_clip_fill
 *   0x324F98 gri_wire_poly_line_clip_fill
 *
 * gen_fix_cline: gr_split_rgb writes only the LOW BYTE of v.u, v.v, v.w (as in Shock, through
 * uchar pointers); the other bytes of the stack vertices and v.i stay uninitialised.  The
 * callee (grd_line_clip_fill_vector[5]) gets the low bytes only, so we zero the rest.
 * The clip-fill wrappers copy the vertices, clip the copies and call the unclipped line
 * drawer of grd_uline_fill_vector unless the clipper returned CLIP_ALL:
 *   cline: gri_cline_clip (0x346220) + vector[5]
 *   wire poly cline: gri_cline_clip + vector[8]
 *   wire poly line: gr_clip_fix_line (0x313D20, the x,y pair) + vector[6]
 */
#include "2d_prim.h"

int gen_fix_cline(fix x0, fix y0, long c0, fix x1, fix y1, long c1)
{
    grs_vertex v0, v1;

    v0.x = x0; v0.y = y0;
    v1.x = x1; v1.y = y1;
    v0.u = 0; v0.v = 0; v0.w = 0; v0.i = 0;   /* only low bytes are set by the original */
    v1.u = 0; v1.v = 0; v1.w = 0; v1.i = 0;
    *(uchar *)&v0.u = (uchar)(c0 >> 2);
    *(uchar *)&v0.v = (uchar)(c0 >> 13);
    *(uchar *)&v0.w = (uchar)(c0 >> 24);
    *(uchar *)&v1.u = (uchar)(c1 >> 2);
    *(uchar *)&v1.v = (uchar)(c1 >> 13);
    *(uchar *)&v1.w = (uchar)(c1 >> 24);
    return WC4(grd_line_clip_fill_vector[5], grd_gc.fcolor, grd_gc.fill_parm, &v0, &v1);
}

int gri_cline_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    grs_vertex u0 = *v0, u1 = *v1;
    int r = gri_cline_clip(&u0, &u1);

    if (r != CLIP_ALL)
        WC4(grd_uline_fill_vector[5], c, parm, &u0, &u1);
    return r;
}

int gri_wire_poly_cline_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    grs_vertex u0 = *v0, u1 = *v1;
    int r = gri_cline_clip(&u0, &u1);

    if (r != CLIP_ALL)
        WC4(grd_uline_fill_vector[8], c, parm, &u0, &u1);
    return r;
}

int gri_wire_poly_line_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    grs_vertex u0 = *v0, u1 = *v1;
    int r = gr_clip_fix_line(&u0.x, &u1.x);

    if (r != CLIP_ALL)
        WC4(grd_uline_fill_vector[6], c, parm, &u0, &u1);
    return r;
}
