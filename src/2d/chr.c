/* chr.c -- x:\prj\tech\libsrc\2d\chrwid.c + chrsiz.c (Terra Nova v1.09): character sizes.
 * Decompiled from __FF.EXE.
 *
 *   0x2FCCE0 gr_font_char_width     0x2FCD20 gr_font_char_size
 */
#include "2d.h"

/* Returns the width in pixels of c in font f (0 if f has no such character). */
short gr_font_char_width(grs_font *f, char c)
{
    short *off_tab;

    if ((uchar)c < f->min || (uchar)c > f->max)
        return 0;
    off_tab = f->off_tab;
    return off_tab[(uchar)c - f->min + 1] - off_tab[(uchar)c - f->min];
}

/* Width and height of c in font f; leaves *w and *h alone if f has no such character. */
void gr_font_char_size(grs_font *f, char c, short *w, short *h)
{
    short *off_tab;
    short offset;

    if ((uchar)c < f->min || (uchar)c > f->max)
        return;
    off_tab = f->off_tab;
    offset = off_tab[(uchar)c - f->min];
    if ((uchar)c < f->min || (uchar)c > f->max)
        *w = 0;
    else
        *w = off_tab[(uchar)c - f->min + 1] - offset;
    *h = f->h;
}
