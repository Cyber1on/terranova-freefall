/* canvas.c -- x:\prj\tech\libsrc\2d\canvas.c (Terra Nova v1.09): canvas handling.
 * Decompiled from __FF.EXE.
 *
 *   0x2EAB30 gr_set_canvas        0x2EABF8 gr_push_canvas       0x2EAC2C gr_pop_canvas
 *   0x2EAC5C gr_init_canvas       0x2EACF4 gr_init_sub_canvas   0x2EAD88 gr_make_canvas
 *   0x2EAE00 gr_alloc_canvas      0x2EAE78 gr_free_canvas       0x2EAE9C gr_alloc_sub_canvas
 *   0x2EAEEC gr_free_sub_canvas
 *
 * Differences from System Shock's canvas.c:
 *  - gr_make_canvas is its own code (bitmap copied, gc initialised) and leaves c->ytab as it
 *    was (Shock calls gr_init_canvas, which clears it).
 *  - gr_alloc_canvas allocates 2 bytes per pixel for BMT_FLAT16 (8).
 *  - gr_init_sub_canvas initialises the gc of dc from dc's (new) bitmap, as in Shock.
 */
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
grs_canvas *grd_canvas_stack[16];   /* 0x44B244 */
int grd_canvas_stackp = 0;          /* 0x35F8D8 */
#endif

#define CANVAS_STACKSIZE 16

/* Makes c the current canvas and selects the function tables for its bitmap type. */
void gr_set_canvas(grs_canvas *c)
{
    int i;

    if (c == NULL)
        return;
    grd_canvas = c;
    if (chn_flags & CHN_GEN)
        i = BMT_GEN;
    else
        i = c->bm.type;
    grd_pixel_index = c->bm.type;
    grd_pixel_table = grd_canvas_table_list[grd_pixel_index];
    grd_canvas_index = i;
    grd_canvas_table = grd_canvas_table_list[i];
    grd_uline_fill_table = grd_uline_fill_table_list[c->bm.type];
    grd_uline_fill_vector = grd_uline_fill_table + c->gc.fill_type * GRD_LINE_TYPES;
    grd_function_fill_table = grd_function_table_list[i];
    grd_function_table = grd_function_fill_table + c->gc.fill_type * (GRD_FUNCS_TN * REAL_BMT_TYPES);
}

/* Pushes the current canvas and makes c current.  Returns 0, or -1 if the stack is full. */
int gr_push_canvas(grs_canvas *c)
{
    if (grd_canvas_stackp >= CANVAS_STACKSIZE)
        return -1;
    grd_canvas_stack[grd_canvas_stackp++] = grd_canvas;
    gr_set_canvas(c);
    return 0;
}

/* Pops the last pushed canvas and makes it current.  Returns it, or NULL on underflow. */
grs_canvas *gr_pop_canvas(void)
{
    grs_canvas *c;

    if (grd_canvas_stackp <= 0)
        return NULL;
    c = grd_canvas_stack[--grd_canvas_stackp];
    gr_set_canvas(c);
    return c;
}

/* Initialises a canvas for a w x h bitmap of the given type at p. */
void gr_init_canvas(grs_canvas *c, uchar *p, uchar type, short w, short h)
{
    gr_init_bm(&c->bm, p, type, 0, w, h);
    gr_init_gc(c);
    gr_cset_fix_cliprect(c, 0, 0, fix_make(w, 0), fix_make(h, 0));
    c->ytab = NULL;
}

/* Initialises dc as the w x h part of sc at (x, y). */
void gr_init_sub_canvas(grs_canvas *sc, grs_canvas *dc, short x, short y, short w, short h)
{
    gr_init_sub_bm(&sc->bm, &dc->bm, x, y, w, h);
    gr_init_gc(dc);
    gr_cset_fix_cliprect(dc, 0, 0, fix_make(w, 0), fix_make(h, 0));
    dc->ytab = NULL;
}

/* Initialises c as a canvas on bitmap bm.  Leaves c->ytab unchanged (see above). */
void gr_make_canvas(grs_bitmap *bm, grs_canvas *c)
{
    c->bm = *bm;
    gr_init_gc(c);
    gr_cset_fix_cliprect(c, 0, 0, fix_make(bm->w, 0), fix_make(bm->h, 0));
}

/* Allocates a canvas and its bitmap memory (video memory for BMT_DEVICE). */
grs_canvas *gr_alloc_canvas(uchar type, short w, short h)
{
    grs_canvas *c;
    uchar *p;

    if ((c = (grs_canvas *)gr_malloc(sizeof(*c))) == NULL)
        return NULL;
    switch (type) {
    case BMT_DEVICE:
        p = valloc(w, h);
        break;
    case BMT_FLAT16:
        p = (uchar *)gr_malloc(2 * w * h);
        break;
    default:
        p = (uchar *)gr_malloc(w * h);
        break;
    }
    gr_init_canvas(c, p, type, w, h);
    return c;
}

/* Frees a canvas from gr_alloc_canvas and its bitmap memory. */
void gr_free_canvas(grs_canvas *c)
{
    if (c->bm.type == BMT_DEVICE)
        vfree(c->bm.bits);
    else
        gr_free(c->bm.bits);
    gr_free(c);
}

/* Allocates a canvas for the w x h part of c at (x, y). */
grs_canvas *gr_alloc_sub_canvas(grs_canvas *c, short x, short y, short w, short h)
{
    grs_canvas *c_new;

    c_new = (grs_canvas *)gr_malloc(sizeof(*c_new));
    if (c_new != NULL)
        gr_init_sub_canvas(c, c_new, x, y, w, h);
    return c_new;
}

void gr_free_sub_canvas(grs_canvas *c)
{
    gr_free(c);
}
