/* genrsdbm.c -- x:\prj\tech\libsrc\2d\genrsdbm.c (Terra Nova v1.09): rsd8 bitmaps drawn by
 * unpacking them into grd_unpack_buf (gr_rsd8_convert) and calling the canvas table entry of the
 * unpacked bitmap's type.  Decompiled from __FF.EXE; same as System Shock's.
 *
 *   0x32AD00 unpack_rsd8_ubitmap             0x32AD40 unpack_rsd8_bitmap
 *   0x32AD88 unpack_rsd8_scale_ubitmap       0x32ADDC unpack_rsd8_scale_bitmap
 *   0x32AE34 unpack_rsd8_clut_scale_ubitmap  0x32AE88 unpack_rsd8_clut_scale_bitmap
 *   0x32AEE4 unpack_rsd8_clut_ubitmap        0x32AF24 unpack_rsd8_clut_bitmap
 */
#include "2d_prim.h"

#define CNV(base, bm) grd_canvas_table[(base) + 2 * (bm).type]

void unpack_rsd8_ubitmap(grs_bitmap *bm, short x, short y)
{
    if (grd_unpack_buf != NULL) {
        grs_bitmap tbm;
        if (gr_rsd8_convert(bm, &tbm) == GR_UNPACK_RSD8_OK)
            WC3(CNV(DRAW_DEVICE_UBITMAP, tbm), &tbm, x, y);
    }
}

int unpack_rsd8_bitmap(grs_bitmap *bm, short x, short y)
{
    if (grd_unpack_buf != NULL) {
        grs_bitmap tbm;
        if (gr_rsd8_convert(bm, &tbm) == GR_UNPACK_RSD8_OK)
            return WC3(CNV(DRAW_DEVICE_BITMAP, tbm), &tbm, x, y);
    }
    return CLIP_ALL;
}

void unpack_rsd8_scale_ubitmap(grs_bitmap *bm, short x, short y, short w, short h)
{
    if (grd_unpack_buf != NULL) {
        grs_bitmap tbm;
        if (gr_rsd8_convert(bm, &tbm) == GR_UNPACK_RSD8_OK)
            WC5(CNV(SCALE_DEVICE_UBITMAP, tbm), &tbm, x, y, w, h);
    }
}

int unpack_rsd8_scale_bitmap(grs_bitmap *bm, short x, short y, short w, short h)
{
    if (grd_unpack_buf != NULL) {
        grs_bitmap tbm;
        if (gr_rsd8_convert(bm, &tbm) == GR_UNPACK_RSD8_OK)
            return WC5(CNV(SCALE_DEVICE_BITMAP, tbm), &tbm, x, y, w, h);
    }
    return CLIP_ALL;
}

void unpack_rsd8_clut_scale_ubitmap(grs_bitmap *bm, short x, short y, short w, short h, uchar *cl)
{
    if (grd_unpack_buf != NULL) {
        grs_bitmap tbm;
        if (gr_rsd8_convert(bm, &tbm) == GR_UNPACK_RSD8_OK)
            WC6(CNV(CLUT_SCALE_DEVICE_UBITMAP, tbm), &tbm, x, y, w, h, cl);
    }
}

int unpack_rsd8_clut_scale_bitmap(grs_bitmap *bm, short x, short y, short w, short h, uchar *cl)
{
    if (grd_unpack_buf != NULL) {
        grs_bitmap tbm;
        if (gr_rsd8_convert(bm, &tbm) == GR_UNPACK_RSD8_OK)
            return WC6(CNV(CLUT_SCALE_DEVICE_BITMAP, tbm), &tbm, x, y, w, h, cl);
    }
    return CLIP_ALL;
}

void unpack_rsd8_clut_ubitmap(grs_bitmap *bm, short x, short y, uchar *cl)
{
    if (grd_unpack_buf != NULL) {
        grs_bitmap tbm;
        if (gr_rsd8_convert(bm, &tbm) == GR_UNPACK_RSD8_OK)
            WC4(CNV(CLUT_DRAW_DEVICE_UBITMAP, tbm), &tbm, x, y, cl);
    }
}

int unpack_rsd8_clut_bitmap(grs_bitmap *bm, short x, short y, uchar *cl)
{
    if (grd_unpack_buf != NULL) {
        grs_bitmap tbm;
        if (gr_rsd8_convert(bm, &tbm) == GR_UNPACK_RSD8_OK)
            return WC4(CNV(CLUT_DRAW_DEVICE_BITMAP, tbm), &tbm, x, y, cl);
    }
    return CLIP_ALL;
}
