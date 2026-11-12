/* kblayout.c -- keyboard layouts (Terra Nova v1.09; no Shock counterpart, names ours).
 * Decompiled from __FF.EXE.
 *
 *   0x31AC40 kb_set_layout    0x31AD68 kb_get_layout    0x31AD70 kb_layout_init
 *
 * kb_cnv_table (0x365708) holds three cooked codes per scan code: unshifted, shifted and
 * AltGr.  Layouts 1-6 are lists of patches {scan code, three codes} at kb_layout_tables[n]
 * (the list ends where the next one starts).  kb_layout_init (called once by kb_startup)
 * turns the patches into differences from the base (US) table; kb_set_layout subtracts the
 * current layout's differences and adds the new one's.
 */
#include "support.h"
#include "sup_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar kb_layout = 0;
#endif

/* number of patches of layout l */
#define LAYOUT_COUNT(l) ((int)((char *)kb_layout_tables[(l) + 1] - (char *)kb_layout_tables[l]) / 8)

int kb_set_layout(int layout)
{
    uchar l = layout, cur = kb_layout;
    short *p;
    int i, n;

    if (l == cur)
        return 1;
    if (l >= 7)
        return 0;
    if (cur != 0) {
        p = kb_layout_tables[cur];
        n = LAYOUT_COUNT(cur);
        for (i = 0; i < n; i++, p += 4) {
            kb_cnv_table[p[0]][0] -= p[1];
            kb_cnv_table[p[0]][1] -= p[2];
            kb_cnv_table[p[0]][2] -= p[3];
        }
    }
    kb_layout = l;
    if (l != 0) {
        p = kb_layout_tables[l];
        n = LAYOUT_COUNT(l);
        for (i = 0; i < n; i++, p += 4) {
            kb_cnv_table[p[0]][0] += p[1];
            kb_cnv_table[p[0]][1] += p[2];
            kb_cnv_table[p[0]][2] += p[3];
        }
    }
    return 1;
}

int kb_get_layout(void)
{
    return kb_layout;
}

void kb_layout_init(void)
{
    uchar l;
    short *p;
    int i, n;

    for (l = 1; l < 7; l++) {
        p = kb_layout_tables[l];
        n = LAYOUT_COUNT(l);
        for (i = 0; i < n; i++, p += 4) {
            p[1] -= kb_cnv_table[p[0]][0];
            p[2] -= kb_cnv_table[p[0]][1];
            p[3] -= kb_cnv_table[p[0]][2];
        }
    }
}
