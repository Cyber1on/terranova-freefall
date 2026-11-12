/* str.c -- x:\prj\tech\libsrc\2d\strwrap.c + strsiz.c (Terra Nova v1.09): string layout.
 * Decompiled from __FF.EXE.
 *
 *   0x2F3050 gr_font_string_wrap     0x2F3144 gr_font_string_unwrap
 *   0x2F31F0 gr_font_string_size     data: 0x44C69C pCharPixOff (strwrap.c static)
 *
 * Soft carriage return = 1, soft space = 2.  Terra Nova's wrap differs from System Shock's:
 * when the break point is a space that ends the string, the space becomes a soft space and
 * the next line starts on it (so it counts one more line).
 */
#include "2d.h"

#define CHAR_SOFTCR 1
#define CHAR_SOFTSP 2

#ifndef TN_LINK_ORIGINAL_DATA
short *pCharPixOff;                 /* 0x44C69C char offset table with min subtracted */
#endif


/* Inserts soft carriage returns into ps so that no line is wider than width; returns the
 * number of lines. */
int gr_font_string_wrap(grs_font *pfont, char *ps, short width)
{
    short *offs = pfont->off_tab - pfont->min;   /* kept in a register; stored at the end */
    uchar *p;
    char *pmark;
    short numLines = 0;
    short currWidth;

    while (*ps) {
        pmark = NULL;
        currWidth = 0;
        p = (uchar *)ps;
        while (*p) {
            while (*p != 0 && *p != '\n' && *p != ' ') {
                currWidth += (short)(offs[*p + 1] - offs[*p]);
                p++;
            }
            if (currWidth > width) {
                if (pmark == NULL && *p != 0 && *p != '\n')
                    pmark = (char *)p;
                break;
            }
            if (*p == 0 || *p == '\n') {
                pmark = NULL;
                break;
            }
            pmark = (char *)p;
            currWidth += (short)(offs[' ' + 1] - offs[' ']);
            p++;
        }
        if (pmark) {
            if (pmark == (char *)p - 1 && *p == 0) {
                *pmark = CHAR_SOFTSP;      /* TN: trailing space at the end of the string */
                ps = pmark;
            } else {
                *pmark = CHAR_SOFTCR;
                ps = pmark + 1;
                if (*ps == ' ')
                    *ps++ = CHAR_SOFTSP;
            }
        } else {
            if (*p)
                ++p;
            ps = (char *)p;
        }
        ++numLines;
    }
    pCharPixOff = offs;
    return numLines;
}

/* Turns soft carriage returns and soft spaces back into spaces. */
void gr_font_string_unwrap(char *s)
{
    int c;

    while ((c = *s) != 0) {
        if (c == CHAR_SOFTCR || c == CHAR_SOFTSP)
            *s = ' ';
        s++;
    }
}

/* Width (of the widest line) and height of s in font f. */
void gr_font_string_size(grs_font *f, char *s, short *w, short *h)
{
    short *offset_tab = f->off_tab;
    short w_lin = 0, w_str = 0;
    short h_str = f->h;
    uchar c;

    while ((c = (uchar)*s++) != '\0') {
        if (c == CHAR_SOFTSP)
            continue;
        if (c == '\n' || c == CHAR_SOFTCR) {
            if (w_lin > w_str)
                w_str = w_lin;
            w_lin = 0;
            h_str += f->h;
            continue;
        }
        w_lin += offset_tab[c - f->min + 1] - offset_tab[c - f->min];
    }
    *w = (w_lin > w_str) ? w_lin : w_str;
    *h = h_str;
}
