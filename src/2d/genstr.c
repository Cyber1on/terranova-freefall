/* genstr.c -- x:\prj\tech\libsrc\2d\genustr.c, genstr.c, struscl.c, strscl.c (Terra Nova v1.09):
 * font strings drawn character by character through the canvas table bitmap entries.
 * Decompiled from __FF.EXE.  gen_font_ustring and the scaled versions are the same as System
 * Shock's; gen_font_string differs (see there).
 *
 *   0x32C760 gen_font_ustring         0x32C8A0 gen_font_string
 *   0x32CCB0 gen_font_scale_ustring   0x32CEB0 gen_font_scale_string
 */
#include "2d_prim.h"

#define CHAR_SOFTCR 1
#define CHAR_SOFTSP 2

#define font_bm_init(bm, f) \
    gr_init_bitmap(&(bm), NULL, ((f)->id == 0xcccc) ? BMT_FLAT8 : BMT_MONO, BMF_TRANS, 0, (f)->h)

/* set the character bitmap's bits (and alignment for mono fonts) */
#define font_bm_bits(bm, char_buf, offset) \
    ((bm).type == BMT_MONO ? ((bm).bits = (char_buf) + ((offset) >> 3), (bm).align = (offset) & 7) \
                           : ((bm).bits = (char_buf) + (offset), 0))

#define DRAW_UBM(bm, x, y) WC3(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * (bm).type], &(bm), x, y)
#define DRAW_BM(bm, x, y) WC3(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * (bm).type], &(bm), x, y)

/* draw a string s in the specified font at (x0,y0).  does not perform any clipping. */
void gen_font_ustring(grs_font *f, char *s, short x0, short y0)
{
    grs_bitmap bm;
    short *offset_tab;
    uchar *char_buf;
    short offset;
    short x, y;
    uchar c;

    char_buf = (uchar *)f + f->buf;
    offset_tab = f->off_tab;
    font_bm_init(bm, f);
    bm.row = f->w;
    x = x0; y = y0;
    while ((c = (uchar)(*s++)) != '\0') {
        if (c == '\n' || c == CHAR_SOFTCR) {
            x = x0;
            y += f->h;
            continue;
        }
        if (c > f->max || c < f->min || c == CHAR_SOFTSP)
            continue;
        offset = offset_tab[c - f->min];
        bm.w = offset_tab[c - f->min + 1] - offset;
        font_bm_bits(bm, char_buf, offset);
        if (bm.type == BMT_MONO)
            WC3(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * BMT_MONO], &bm, x, y);
        else
            WC3(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * BMT_FLAT8], &bm, x, y);
        x += bm.w;
    }
}

/* Clipped string.  Terra Nova's version: lines entirely below the clip rectangle end the
 * string; a line entirely above it is skipped, but the line after it is then drawn without
 * checking it again (quirk); returns CLIP_ALL only for the initial trivial reject, otherwise
 * CLIP_NONE. */
int gen_font_string(grs_font *f, char *s, short x0, short y0)
{
    grs_bitmap bm;
    short *offset_tab;
    uchar *char_buf;
    short offset;
    short x, y;
    uchar c;
    short yok;

    if (x0 > grd_clip.right || y0 > grd_clip.bot)
        return CLIP_ALL;

    char_buf = (uchar *)f + f->buf;
    offset_tab = f->off_tab;
    font_bm_init(bm, f);
    bm.row = f->w;
    x = x0; y = y0;
    for (;;) {
        if (y >= grd_clip.bot)
            return CLIP_NONE;
        if (y + f->h < grd_clip.top) {
            /* not yet in y-range: skip the line */
            while ((c = *s++) != CHAR_SOFTCR && c != '\n')
                if (c == '\0') return CLIP_NONE;
            x = x0; y += f->h;
        }
        yok = (y >= grd_clip.top && y + f->h <= grd_clip.bot);

        /* line coming into range */
        while ((c = (uchar)(*s++)) != CHAR_SOFTCR && c != '\n') {
            if (c == '\0')
                return CLIP_NONE;
            if (c > f->max || c < f->min || c == CHAR_SOFTSP)
                continue;
            offset = offset_tab[c - f->min];
            bm.w = offset_tab[c - f->min + 1] - offset;
            if (x + bm.w >= grd_clip.left)
                break;
            x += bm.w;
        }
        if (c == '\n' || c == CHAR_SOFTCR) {
            x = x0; y += f->h;
            continue;
        }
        /* clip boundary character */
        font_bm_bits(bm, char_buf, offset);
        DRAW_BM(bm, x, y);
        x += bm.w;

        /* line in range */
        while ((c = *s++) != CHAR_SOFTCR && c != '\n') {
            if (c == '\0')
                return CLIP_NONE;
            if (c > f->max || c < f->min || c == CHAR_SOFTSP)
                continue;
            offset = offset_tab[c - f->min];
            bm.w = offset_tab[c - f->min + 1] - offset;
            if (x + bm.w > grd_clip.right)
                break;
            font_bm_bits(bm, char_buf, offset);
            if (yok)
                DRAW_UBM(bm, x, y);
            else
                DRAW_BM(bm, x, y);
            x += bm.w;
        }
        if (c == '\n' || c == CHAR_SOFTCR) {
            x = x0; y += f->h;
            continue;
        }
        /* clip boundary character */
        font_bm_bits(bm, char_buf, offset);
        DRAW_BM(bm, x, y);
        /* end of line */
        while ((c = *s++) != CHAR_SOFTCR && c != '\n')
            if (c == '\0') return CLIP_NONE;
        x = x0; y += f->h;
    }
}

/* shared body of the scaled strings; clipped selects the canvas table entry */
static void font_scale_string(grs_font *f, char *s, short x0, short y0, short w, short h, int base)
{
    grs_bitmap bm;
    short *offset_tab;
    uchar *char_buf;
    short offset;
    short str_w, str_h;
    fix x, y;
    fix x_scale, y_scale;
    fix next_x, next_y, del_y;
    int i;
    uchar c;

    char_buf = (uchar *)f + f->buf;
    offset_tab = f->off_tab;
    font_bm_init(bm, f);
    bm.row = f->w;
    gr_font_string_size(f, s, &str_w, &str_h);
    x_scale = (w << 16) / str_w;
    y_scale = (h << 16) / str_h;
    x = x0 << 16; y = y0 << 16;
    for (i = 0, del_y = 0; i < f->h; del_y += y_scale, i++);
    next_y = y + del_y;
    while ((c = (uchar)(*s++)) != '\0') {
        if (c == '\n' || c == CHAR_SOFTCR) {
            x = x0 << 16;
            y = next_y;
            next_y = y + del_y;
            continue;
        }
        if (c > f->max || c < f->min || c == CHAR_SOFTSP)
            continue;
        offset = offset_tab[c - f->min];
        bm.w = offset_tab[c - f->min + 1] - offset;
        for (i = 0, next_x = x; i < bm.w; next_x += x_scale, i++);
        font_bm_bits(bm, char_buf, offset);
        WC5(grd_canvas_table[base + 2 * bm.type], &bm, (short)fix_int(x), (short)fix_int(y),
            (short)(fix_int(next_x) - fix_int(x)), (short)(fix_int(next_y) - fix_int(y)));
        x = next_x;
    }
}

void gen_font_scale_ustring(grs_font *f, char *s, short x0, short y0, short w, short h)
{
    font_scale_string(f, s, x0, y0, w, h, SCALE_DEVICE_UBITMAP);
}

int gen_font_scale_string(grs_font *f, char *s, short x0, short y0, short w, short h)
{
    font_scale_string(f, s, x0, y0, w, h, SCALE_DEVICE_BITMAP);
    return CLIP_NONE;
}
