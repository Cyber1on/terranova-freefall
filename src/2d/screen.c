/* screen.c -- x:\prj\tech\libsrc\2d\screen.c (Terra Nova v1.09): screens.
 * Decompiled from __FF.EXE.
 *
 *   0x308EA0 gr_alloc_screen     0x309068 gr_free_screen     0x3090B0 gr_set_screen
 *
 * Terra Nova's grs_screen is 0x30 bytes and is allocated in one block with its two canvases
 * (0xB0 bytes).  gr_alloc_screen checks the requested size against the mode: Mode X modes
 * (1..4) against video memory / row, VESA modes (>= 5) by asking the driver for the
 * logical width (vesa_set_logical_width) or the maximum number of lines.  The visible canvas
 * is set up for the full size and then cut down to the mode size (bitmap w, h and clip; its
 * wlog/hlog stay those of the full size).
 */
#include "2d.h"

grs_screen *gr_alloc_screen(short w, short h)
{
    grs_screen *s;
    grs_canvas *c;
    uchar *b;
    long lines;

    if (w < grd_cap->w || h < grd_cap->h)
        return NULL;
    if (grd_mode == 0) {
        if (grd_cap->w < w || h > grd_cap->h)
            return NULL;
    } else if (grd_mode < 5) {
        if (grd_cap->w < w)
            return NULL;
        if (h > (grd_info.memory << 10) / grd_screen_row)
            return NULL;
    } else if (grd_cap->w < w) {
        if (vesa_set_logical_width(w, &lines) < w)
            return NULL;
        if (h > lines)
            return NULL;
    } else {
        lines = vesa_max_lines();
        if (lines < h)
            return NULL;
    }

    if ((s = (grs_screen *)gr_malloc(sizeof(grs_screen) + 2 * sizeof(grs_canvas))) == NULL)
        return NULL;
    if ((b = valloc(w, h)) == (uchar *)-1) {
        gr_free(s);     /* original: frees whatever ecx holds; unreachable (valloc never fails) */
        return NULL;
    }
    gr_init_bm(&s->bm, b, BMT_DEVICE, 0, w, h);
    s->x = 0;
    s->y = 0;
    c = (grs_canvas *)(s + 1);
    s->c = c;
    gr_init_canvas(c, s->bm.bits, BMT_DEVICE, w, h);
    c->bm.w = w;
    gr_init_canvas(c + 1, s->bm.bits, BMT_DEVICE, w, h);
    gr_cset_fix_cliprect(c + 1, 0, 0, fix_make(grd_cap->w, 0), fix_make(grd_cap->h, 0));
    c[1].bm.w = grd_cap->w;
    c[1].bm.h = grd_cap->h;
    return s;
}

void gr_free_screen(grs_screen *s)
{
    vfree(s->bm.bits);
    if (s->c->ytab != NULL)
        gr_free(s->c->ytab);
    if (s->c[1].ytab != NULL && s->c[1].ytab != s->c->ytab)
        gr_free(s->c[1].ytab);
    gr_free(s);
}

/* Makes s the current screen and its first canvas the current canvas. */
void gr_set_screen(grs_screen *s)
{
    grd_screen = s;
    grd_screen_canvas = s->c;
    grd_visible_canvas = s->c + 1;
    gr_set_canvas(s->c);
}
