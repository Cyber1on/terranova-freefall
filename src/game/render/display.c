/* display.c -- Terra Nova module M088 (0x2AD150-0x2AD9F0; source file name unknown): low
 * resolution 3D view.  The view is drawn into the top-left quarter (or left half) of the
 * canvas and then doubled in place, by plain pixel doubling or with a 64 KB blend table that
 * mixes neighbouring pixels.  Decompiled from __FF.EXE v1.09.
 *
 *   0x2AD150 display_2AD150 blend table ref++   0x2AD184 display_2AD184 ref--
 *   0x2AD19C display_2AD19C halve the canvas    0x2AD1DC display_2AD1DC double it back
 *   0x2AD248 display_2AD248 blended row pair    0x2AD2F0 display_2AD2F0 remapped row (unref.)
 *   0x2AD338 display_2AD338 blended + remapped row pair
 *   0x2AD414 mode 1: double horizontally        0x2AD4B8 mode 2: double both ways
 *   0x2AD5A0 mode 3: blend horizontally         0x2AD6D4 mode 4: blend both ways
 *   0x2AD870 display_2AD870 blend table from grd_bpal / grd_ipal
 *
 * Every routine works from the end of the bitmap backwards, so source and destination share
 * the bitmap.  Quirk kept: the 4-way unrolled copy of modes 1 and 2 enters the unrolled block
 * at the wrong place for odd widths (w % 4 == 1 or 3 copies w + 2 or w - 2 pixels), and a
 * width of 0 runs away (as in the original).
 */
#include "g5_a.h"

void display_2AD150(void)
{
    if (++disp_blend_ref == 1) {
        if (disp_blend == NULL)
            disp_blend = disp_blend_buf;
        display_2AD870(disp_blend);
    }
}

void display_2AD184(void)
{
    if (--disp_blend_ref == 0)
        disp_blend = NULL;
}

void display_2AD19C(void)
{
    if (disp_flags & 1) {
        grd_canvas->bm.w >>= 1;
        if (disp_flags & 2) {
            grd_canvas->bm.h >>= 1;
            return;
        }
        pixel_ratio >>= 1;
        *(long *)grd_cap >>= 1;
    }
}

void display_2AD1DC(void)
{
    if (disp_flags & 1) {
        WATCOM_CALL(disp_double_fns[disp_mode], (long)grd_canvas);
        if (disp_flags & 2) {
            grd_canvas->bm.h += grd_canvas->bm.h;
        } else {
            pixel_ratio += pixel_ratio;
            *(long *)grd_cap += *(long *)grd_cap;
        }
        grd_canvas->bm.w += grd_canvas->bm.w;
    }
}

void display_2AD248(uchar *d, uchar *s, long n, long row, uchar *t)
{
    uchar *e = s + 1 - row;
    while (n-- != 0) {
        uchar a = s[0], t1, t2;
        d[0] = a;
        t1 = t[(s[-1] << 8) | a];
        d[-1] = t1;
        t2 = t[(e[-1] << 8) | a];
        d[-row] = t2;
        d[-1 - row] = t[(t2 << 8) | t1];
        d -= 2;
        e--;
        s--;
    }
}

void display_2AD2F0(uchar *d, uchar *s, long n, uchar *t, uchar *l)
{
    while (n-- != 0) {
        uchar a = s[0];
        d[0] = l[a];
        s--;
        d -= 2;
        d[1] = l[t[(s[0] << 8) | a]];
    }
}

void display_2AD338(uchar *d, uchar *s, long n, long row, uchar *t, uchar *l)
{
    uchar *e = s + 1 - row;
    while (n-- != 0) {
        uchar a = s[0], t1, t2;
        e--;
        d[0] = l[a];
        t1 = t[(s[-1] << 8) | a];
        s--;
        d--;
        d[0] = l[t1];
        t2 = t[(e[0] << 8) | a];
        d--;
        d[2 - row] = l[t2];
        d[1 - row] = l[t[(t2 << 8) | t1]];
    }
}

/* doubles w pixels ending at s into 2w ending at d + 1 (word stores going down) */
static void dup_row(uchar *d, uchar *s, long w)
{
    ulong cnt = (ulong)(w + 3) >> 2;
#define DUP do { uchar c = *s--; d[0] = c; d[1] = c; d -= 2; } while (0)
    switch ((w & 3) ^ 3) {
    case 3:
        do {
            DUP;
    case 2:
            DUP;
    case 1:
            DUP;
    case 0:
            DUP;
        } while (--cnt);
    }
#undef DUP
}

/* blends w pixels ending at s into 2w ending at d: d[0] = s[0], d[-1] = t[s[-1], s[0]] */
static void blend_row(uchar *d, uchar *s, long w, uchar *t)
{
    ulong cnt = (ulong)(w + 1) >> 1;
    d--;
#define BL do { uchar a = *s--; uchar b = *s; d[1] = a; d[0] = t[(b << 8) | a]; d -= 2; } while (0)
    if (w & 1)
        goto odd;
    do {
        BL;
odd:
        BL;
    } while (--cnt);
#undef BL
}

void display_2AD414(grs_bitmap *bm)
{
    long y = bm->h - 1;
    uchar *s = bm->bits + bm->row * y + bm->w - 1;
    uchar *d = bm->bits + bm->row * y + 2 * bm->w - 1;
    for (; y >= 0; y--) {
        dup_row(d - 1, s, bm->w);
        s -= bm->row;
        d -= bm->row;
    }
}

void display_2AD4B8(grs_bitmap *bm)
{
    long y = bm->h - 1;
    uchar *s = bm->bits + bm->row * y + bm->w - 1;
    uchar *d = bm->bits + bm->row * (2 * bm->h - 1) + 2 * bm->w - 1;
    for (; y >= 0; y--) {
        dup_row(d - 1, s, bm->w);
        s -= bm->row;
        d -= bm->row;
        memcpy(d + 1 - 2 * bm->w, d + 1 + bm->row - 2 * bm->w, 2 * bm->w);
        d -= bm->row;
    }
}

void display_2AD5A0(grs_bitmap *bm)
{
    long y = bm->h - 1;
    uchar *base = bm->bits + bm->row * y;
    uchar *s = base + bm->w - 1;
    uchar *d = base + 2 * bm->w - 1;
    for (; y >= 0; y--) {
        if (disp_flags & 4)
            display_2AD2F0(d, s, bm->w, disp_blend, disp_ltab);
        else
            blend_row(d, s, bm->w, disp_blend);
        s -= bm->row;
        d -= bm->row;
    }
}

void display_2AD6D4(grs_bitmap *bm)
{
    long y = bm->h - 1;
    uchar *s = bm->bits + bm->row * y + bm->w - 1;
    uchar *d = bm->bits + bm->row * (2 * bm->h - 1) + 2 * bm->w - 1;
    for (; y >= 1; y--) {
        if (disp_flags & 4)
            display_2AD338(d, s, bm->w, bm->row, disp_blend, disp_ltab);
        else
            display_2AD248(d, s, bm->w, bm->row, disp_blend);
        s -= bm->row;
        d -= 2 * bm->row;
    }
    if (disp_flags & 4)
        display_2AD2F0(d, s, bm->w, disp_blend, disp_ltab);
    else
        blend_row(d, s, bm->w, disp_blend);
    memcpy(d + 1 - 2 * bm->w - bm->row, d + 1 - 2 * bm->w, 2 * bm->w);
}

void display_2AD870(uchar *tab)
{
    ulong *pal = (ulong *)grd_bpal;
    long i, j, k;
    for (i = 0; i < 256; i++) {
        ulong c = pal[i];
        uchar c1[3];
        ushort w1[3];
        uchar flag;
        c1[0] = (c >> 2) & 0xFF;
        c1[1] = (c >> 13) & 0xFF;
        c1[2] = (c >> 24) & 0xFF;
        for (k = 0; k < 3; k++)
            w1[k] = c1[k] << 7;
        flag = i == 0 || c1[0] + c1[1] + c1[2] == 0;
        for (j = 0; j < 256; j++) {
            uchar out;
            if (flag || i == j) {
                out = (uchar)i;
            } else {
                ulong c2 = pal[j];
                uchar cc[3];
                cc[0] = (c2 >> 2) & 0xFF;
                cc[1] = (c2 >> 13) & 0xFF;
                cc[2] = (c2 >> 24) & 0xFF;
                if (j == 0) {
                    out = 0;
                } else {
                    long idx = 0;
                    for (k = 2; k >= 0; k--)
                        idx = (idx << 5) + (((cc[k] << 7) + w1[k]) >> 11);
                    out = grd_ipal[idx];
                }
            }
            *tab++ = out;
        }
    }
}
