/* line.c -- line drawing entry points and their clipping fill functions (Terra Nova v1.09).
 * Decompiled from __FF.EXE.  System Shock files: gen/genhlin.c, gen/genvlin.c, clip/clpltab.c,
 * clip/clplin2.c, clip/clpslin.c (the gr_hline/... macros of 2D.H are functions here).
 *
 *   0x2ED5A0 gen_hline               0x2ED5C8 gri_hline_clip_fill
 *   0x2ED650 gen_vline               0x2ED678 gri_vline_clip_fill
 *   0x2F2100 gr_iline (name ours)    0x2F213C gri_iline_clip_fill
 *   0x2F3D00 gr_fix_line (name ours) 0x2F3D34 gri_line_clip_fill
 *   0x317C50 gr_fix_sline (name ours) 0x317C94 gri_sline_clip_fill
 *   data: 0x3651C0 grd_line_clip_fill_table[9], 0x3651E4 grd_line_clip_fill_vector
 *
 * The clip fill functions take (colour, fill parm, ...) and call the unclipped line routine of
 * the current fill type (grd_uline_fill_vector).  The entry points pass the canvas's fcolor
 * and fill_parm.  Quirks kept: the vertex copies made by gr_iline/gri_line_clip_fill only
 * have x and y set (the other fields are whatever is on the stack).
 */
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
void (*grd_line_clip_fill_table[GRD_LINE_TYPES])() = {
    (void (*)())gri_line_clip_fill, (void (*)())gri_iline_clip_fill,
    (void (*)())gri_hline_clip_fill, (void (*)())gri_vline_clip_fill,
    (void (*)())gri_sline_clip_fill, (void (*)())gri_cline_clip_fill,
    (void (*)())gri_wire_poly_line_clip_fill, (void (*)())gr_null,
    (void (*)())gri_wire_poly_cline_clip_fill
};
void (**grd_line_clip_fill_vector)() = grd_line_clip_fill_table;
#endif

#define GR_LINE 0
#define GR_ILINE 1
#define GR_HLINE 2
#define GR_VLINE 3
#define GR_SLINE 4

int gen_hline(short x0, short y0, short x1)
{
    return WATCOM_CALL(GR_IFN(grd_line_clip_fill_vector[GR_HLINE]), x0, y0, x1,
                       grd_canvas->gc.fcolor, grd_canvas->gc.fill_parm);
}

int gri_hline_clip_fill(short x0, short y0, short x1, long c, long parm)
{
    int r = CLIP_NONE;
    short t;

    if (x0 > x1) {
        t = x0; x0 = x1; x1 = t;
    }
    if (y0 < grd_canvas->gc.clip.i.top || y0 >= grd_canvas->gc.clip.i.bot ||
        x1 < grd_canvas->gc.clip.i.left || x0 >= grd_canvas->gc.clip.i.right)
        return CLIP_ALL;
    if (x0 < grd_canvas->gc.clip.i.left) {
        r |= CLIP_LEFT;
        x0 = grd_canvas->gc.clip.i.left;
    }
    if (x1 >= grd_canvas->gc.clip.i.right) {
        r |= CLIP_RIGHT;
        x1 = grd_canvas->gc.clip.i.right - 1;
    }
    if (r != CLIP_ALL)
        WATCOM_CALL(grd_uline_fill_vector[GR_HLINE], x0, y0, x1, c, parm);
    return r;
}

int gen_vline(short x0, short y0, short y1)
{
    return WATCOM_CALL(GR_IFN(grd_line_clip_fill_vector[GR_VLINE]), x0, y0, y1,
                       grd_canvas->gc.fcolor, grd_canvas->gc.fill_parm);
}

int gri_vline_clip_fill(short x0, short y0, short y1, long c, long parm)
{
    int r = CLIP_NONE;
    short t;

    if (y0 > y1) {
        t = y0; y0 = y1; y1 = t;
    }
    if (x0 < grd_canvas->gc.clip.i.left || x0 >= grd_canvas->gc.clip.i.right ||
        y1 < grd_canvas->gc.clip.i.top || y0 >= grd_canvas->gc.clip.i.bot)
        return CLIP_ALL;
    if (y0 < grd_canvas->gc.clip.i.top) {
        r |= CLIP_TOP;
        y0 = grd_canvas->gc.clip.i.top;
    }
    if (y1 >= grd_canvas->gc.clip.i.bot) {
        r |= CLIP_BOT;
        y1 = grd_canvas->gc.clip.i.bot - 1;
    }
    WATCOM_CALL(grd_uline_fill_vector[GR_VLINE], x0, y0, y1, c, parm);
    return r;
}

/* Line between integer points (x and y of the vertices are ints). */
int gr_iline(short x0, short y0, short x1, short y1)
{
    grs_vertex v0, v1;

    v0.x = x0; v0.y = y0;
    v1.x = x1; v1.y = y1;
    return WATCOM_CALL(GR_IFN(grd_line_clip_fill_vector[GR_ILINE]), grd_canvas->gc.fcolor,
                       grd_canvas->gc.fill_parm, (long)&v0, (long)&v1);
}

/* Converts integer vertices to fix (pixel centres), clips and draws. */
int gri_iline_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    grs_vertex u0, u1;
    int r;

    u0.x = (v0->x << 16) | 0x8000;
    u0.y = (v0->y << 16) | 0x8000;
    u1.x = (v1->x << 16) | 0x8000;
    u1.y = (v1->y << 16) | 0x8000;
    r = gr_clip_fix_line(&u0.x, &u1.x);
    if (r != CLIP_ALL)
        WATCOM_CALL(grd_uline_fill_vector[GR_LINE], c, parm, (long)&u0, (long)&u1);
    return r;
}

int gr_fix_line(fix x0, fix y0, fix x1, fix y1)
{
    grs_vertex v0, v1;

    v0.x = x0; v0.y = y0;
    v1.x = x1; v1.y = y1;
    return WATCOM_CALL(GR_IFN(grd_line_clip_fill_vector[GR_LINE]), grd_canvas->gc.fcolor,
                       grd_canvas->gc.fill_parm, (long)&v0, (long)&v1);
}

int gri_line_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    grs_vertex u0, u1;
    int r;

    u0.x = v0->x; u0.y = v0->y;
    u1.x = v1->x; u1.y = v1->y;
    r = gr_clip_fix_line(&u0.x, &u1.x);
    if (r != CLIP_ALL)
        WATCOM_CALL(grd_uline_fill_vector[GR_LINE], c, parm, (long)&u0, (long)&u1);
    return r;
}

/* Shaded line: intensities i0, i1. */
int gr_fix_sline(fix x0, fix y0, fix i0, fix x1, fix y1, fix i1)
{
    grs_vertex v0, v1;

    v0.x = x0; v0.y = y0; v0.i = i0;
    v1.x = x1; v1.y = y1; v1.i = i1;
    return WATCOM_CALL(GR_IFN(grd_line_clip_fill_vector[GR_SLINE]), grd_canvas->gc.fcolor,
                       grd_canvas->gc.fill_parm, (long)&v0, (long)&v1);
}

int gri_sline_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    grs_vertex u0 = *v0, u1 = *v1;
    int r;

    r = gri_sline_clip(&u0, &u1);
    if (r != CLIP_ALL)
        WATCOM_CALL(grd_uline_fill_vector[GR_SLINE], c, parm, (long)&u0, (long)&u1);
    return r;
}
