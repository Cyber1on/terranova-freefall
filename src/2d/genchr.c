/* genchr.c -- x:\prj\tech\libsrc\2d\genuchr.c, genchr.c (Terra Nova v1.09): one font character,
 * unclipped and clipped.  Decompiled from __FF.EXE; same as System Shock's.
 *
 *   0x32D0B0 gen_font_uchar   0x32D1B0 gen_font_char
 */
#include "2d_prim.h"

void gen_font_uchar(grs_font *f, char c, short x, short y)
{
    grs_bitmap bm;
    short *off_tab;
    uchar *data_buf;
    short offset;

    if ((uchar)c > f->max || (uchar)c < f->min)
        return;
    data_buf = (uchar *)f + f->buf;
    off_tab = f->off_tab;
    offset = off_tab[(uchar)c - f->min];
    gr_init_bitmap(&bm, NULL, (f->id == 0xcccc) ? BMT_FLAT8 : BMT_MONO,
                   BMF_TRANS, off_tab[(uchar)c - f->min + 1] - offset, f->h);
    bm.row = f->w;
    if (bm.type == BMT_MONO) {
        bm.bits = data_buf + (offset >> 3);
        bm.align = offset & 7;
        WC3(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * BMT_MONO], &bm, x, y);
    } else {
        bm.bits = data_buf + offset;
        WC3(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * BMT_FLAT8], &bm, x, y);
    }
}

int gen_font_char(grs_font *f, char c, short x, short y)
{
    grs_bitmap bm;
    short *off_tab;
    uchar *data_buf;
    short offset;

    if ((uchar)c > f->max || (uchar)c < f->min)
        return CLIP_ALL;
    data_buf = (uchar *)f + f->buf;
    off_tab = f->off_tab;
    offset = off_tab[(uchar)c - f->min];
    gr_init_bitmap(&bm, NULL, (f->id == 0xcccc) ? BMT_FLAT8 : BMT_MONO,
                   BMF_TRANS, off_tab[(uchar)c - f->min + 1] - offset, f->h);
    bm.row = f->w;
    if (bm.type == BMT_MONO) {
        bm.bits = data_buf + (offset >> 3);
        bm.align = offset & 7;
        return WC3(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * BMT_MONO], &bm, x, y);
    } else {
        bm.bits = data_buf + offset;
        return WC3(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * BMT_FLAT8], &bm, x, y);
    }
}
