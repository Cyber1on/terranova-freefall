/* strwid.c -- x:\prj\tech\libsrc\2d\strwid.c (2D library code linked outside the 2D range;
 * Terra Nova v1.09).  Decompiled from __FF.EXE.
 *
 *   0x2F2AA0 gr_font_string_width   0x2F2B00 gr_font_string_height (name ours)
 *
 * '\n' and CHAR_SOFTCR (1) end a line, CHAR_SOFTSP (2) is skipped.  Widths are summed in 16
 * bits like Shock's shorts.
 */
#include "t1_c.h"

#define CHAR_SOFTCR 1
#define CHAR_SOFTSP 2

/* Width in pixels of the widest line of s. */
short gr_font_string_width(grs_font *f, char *s)
{
    short *offset_tab = f->off_tab;
    short w_lin = 0, w = 0;
    uchar c;

    while ((c = (uchar)*s++) != '\0') {
        if (c == CHAR_SOFTSP)
            continue;
        if (c == '\n' || c == CHAR_SOFTCR) {
            if (w_lin > w)
                w = w_lin;
            w_lin = 0;
            continue;
        }
        w_lin += offset_tab[c - f->min + 1] - offset_tab[c - f->min];
    }
    return w_lin > w ? w_lin : w;
}

/* Height in pixels of s: one font height per line. */
short gr_font_string_height(grs_font *f, char *s)
{
    short h = f->h;
    char c;

    while ((c = *s++) != '\0')
        if (c == '\n' || c == CHAR_SOFTCR)
            h += f->h;
    return h;
}
