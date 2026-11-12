/* wirpoly.c -- x:\prj\tech\libsrc\2d\wirpoly.c (Terra Nova v1.09): wire-frame polygons.
 * Decompiled from __FF.EXE.
 *
 *   0x2F3AA0 gr_wire_upoly    0x2F3B20 gr_wire_poly    0x2F3C08 gr_wire_ucpoly
 *   0x2F3C80 gr_wire_cpoly
 *
 * The colour of gr_wire_upoly/gr_wire_poly is a byte.  Quirk kept: gr_wire_poly's vertex
 * copies only have x and y set.
 */
#include "2d.h"

#define GR_WIRE_POLY_LINE 6
#define GR_WIRE_POLY_CLINE 8

void gr_wire_upoly(uchar c, int n, grs_vertex **vpl)
{
    int i;

    for (i = 1; i < n; i++)
        WATCOM_CALL(grd_uline_fill_vector[GR_WIRE_POLY_LINE], c, grd_canvas->gc.fill_parm,
                    (long)vpl[i - 1], (long)vpl[i]);
    WATCOM_CALL(grd_uline_fill_vector[GR_WIRE_POLY_LINE], c, grd_canvas->gc.fill_parm,
                (long)vpl[n - 1], (long)vpl[0]);
}

void gr_wire_poly(uchar c, int n, grs_vertex **vpl)
{
    int i;
    grs_vertex v0, v1;

    for (i = 1; i < n; i++) {
        v0.x = vpl[i - 1]->x; v0.y = vpl[i - 1]->y;
        v1.x = vpl[i]->x; v1.y = vpl[i]->y;
        if (gr_clip_fix_line(&v0.x, &v1.x) != CLIP_ALL)
            WATCOM_CALL(grd_uline_fill_vector[GR_WIRE_POLY_LINE], c, grd_canvas->gc.fill_parm,
                        (long)&v0, (long)&v1);
    }
    v0.x = vpl[n - 1]->x; v0.y = vpl[n - 1]->y;
    v1.x = vpl[0]->x; v1.y = vpl[0]->y;
    if (gr_clip_fix_line(&v0.x, &v1.x) != CLIP_ALL)
        WATCOM_CALL(grd_uline_fill_vector[GR_WIRE_POLY_LINE], c, grd_canvas->gc.fill_parm,
                    (long)&v0, (long)&v1);
}

void gr_wire_ucpoly(int n, grs_vertex **vpl)
{
    int i;

    for (i = 1; i < n; i++)
        WATCOM_CALL(grd_uline_fill_vector[GR_WIRE_POLY_CLINE], grd_canvas->gc.fcolor,
                    grd_canvas->gc.fill_parm, (long)vpl[i - 1], (long)vpl[i]);
    WATCOM_CALL(grd_uline_fill_vector[GR_WIRE_POLY_CLINE], grd_canvas->gc.fcolor,
                grd_canvas->gc.fill_parm, (long)vpl[n - 1], (long)vpl[0]);
}

void gr_wire_cpoly(int n, grs_vertex **vpl)
{
    int i;

    for (i = 1; i < n; i++)
        WATCOM_CALL(grd_line_clip_fill_vector[GR_WIRE_POLY_CLINE], grd_canvas->gc.fcolor,
                    grd_canvas->gc.fill_parm, (long)vpl[i - 1], (long)vpl[i]);
    WATCOM_CALL(grd_line_clip_fill_vector[GR_WIRE_POLY_CLINE], grd_canvas->gc.fcolor,
                grd_canvas->gc.fill_parm, (long)vpl[n - 1], (long)vpl[0]);
}
