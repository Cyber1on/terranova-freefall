/* genntrp2.c -- Terra Nova v1.09: generic 2x interpolating / 2x2 filtering blits through the
 * canvas pixel routines (canvas tables 0x36808C and 0x36A558, INTERP2_UBITMAP and
 * FILTER2_UBITMAP; Shock lists gen_interp2_ubitmap / gen_filter2_ubitmap as not implemented and
 * has only the flat8 ones).  Decompiled from __FF.EXE (Watcom C).  Pixels go through
 * grd_pixel_table[SET_UPIXEL8] and are read back with grd_pixel_table[GET_UPIXEL8].
 *
 *   0x33B1B0 gen_interp2_ubitmap   0x33B3B0 gen_filter2_ubitmap
 *
 * gen_interp2_ubitmap writes the source pixels to the even pixels of the even rows and the
 * average of each pair to the odd ones; then every odd row is the average of the rows above
 * and below it (read back from the canvas) and the last row is cleared to 0.  The averages
 * are (a >> 1) + (b >> 1) of the grd_bpal entries, looked up in grd_ipal.
 */
#include "2d_tmap.h"

#define SET_UPIXEL8 0
#define GET_UPIXEL8 2
#define PIXEL(c, x, y) WATCOM_CALL((void (*)())grd_pixel_table[SET_UPIXEL8], (long)(c), (long)(short)(x), (long)(short)(y))
#define IPAL_ADD(a) (grd_ipal + ((a) >> 5 & 0x1f) + ((a) >> 11 & 0x3e0) + ((a) >> 17 & 0x7c00))

void gen_interp2_ubitmap(grs_bitmap *bm)
{
    int i, j;
    ulong a, b;

    for (j = 0; j < bm->h; j++) {
        short y2 = (short)(2 * j), x2 = 1;

        for (i = 0; i < bm->w; i++, x2 += 2) {
            uchar *s = bm->bits + j * (ulong)(ushort)bm->row + i;
            ulong ca = s[0], cb = s[1];

            PIXEL(ca, 2 * i, y2);
            a = grd_bpal[ca];
            b = grd_bpal[cb];
            a = (a >> 1) + (b >> 1);
            PIXEL(*IPAL_ADD(a), x2, y2);
        }
    }
    for (i = 0; i < bm->w * 2; i++) {
        short y = 1;

        for (j = 0; j < bm->h * 2 - 2; j += 2, y += 2) {
            long ca = WATCOM_CALL((long (*)())grd_pixel_table[GET_UPIXEL8], (long)(short)i, (long)(short)j);
            long cb = WATCOM_CALL((long (*)())grd_pixel_table[GET_UPIXEL8], (long)(short)i, (long)(short)(j + 2));

            a = grd_bpal[ca];
            b = grd_bpal[cb];
            a = (a >> 1) + (b >> 1);
            PIXEL(*IPAL_ADD(a), i, y);
        }
        PIXEL(0, i, y);
    }
}

#define Q(c) ((grd_bpal[c] >> 2) & 0x3fc7f8ff)

void gen_filter2_ubitmap(grs_bitmap *bm)
{
    int i, j;

    for (j = 0; j < bm->h; j += 2) {
        short y = (short)(j / 2);

        for (i = 0; i < bm->w; i += 2) {
            uchar *s = bm->bits + i + j * (ulong)(ushort)bm->row;
            ulong a = Q(s[0]) + Q(s[1]);

            s += 1 + (ushort)bm->row;
            a += Q(s[0]);
            a += Q(s[-1]);
            PIXEL(*IPAL_ADD(a), i / 2, y);
        }
    }
}
