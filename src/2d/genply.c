/* genply.c -- Terra Nova v1.09: generic (device-independent) polygon scanline loops and their
 * inits, used by the 8-bit banked VESA function table (0x3686E0, GRC_POLY).  Decompiled from
 * __FF.EXE (Watcom C); the init names are Shock's (Gen/GENTF.H declares them, no code), the
 * loop names ours.  Spans go through the current uhline fill (solid) or pixel by pixel through
 * grd_pixel_table[GET_UPIXEL8] and grd_function_table[GRC_PIXEL] (translucent, shaded).
 *
 *   0x33E4C0 gri_gen_poly_loop    0x33E5BC gri_gen_poly_init    0x33E5DC gri_gen_tpoly_init
 *   0x33EC60 gri_gen_spoly_loop   0x33EDC4 gri_gen_spoly_init   0x33EDE4 gri_gen_stpoly_init
 *
 * Unlike the flat8 loops these keep no destination pointer.  The shaded opaque loop writes the
 * intensity itself (fix_int(i), no light table), the shaded translucent one
 * tluc8stab[fix_int(i) * 256 + pixel].
 */
#include "2d_tmap.h"

extern void (**grd_uline_fill_vector)();    /* 0x44C5AC */
#define GET_UPIXEL8 2
#define GRL_UHLINE 2

int gri_gen_poly_loop(grs_tmap_loop_info *ti)
{
    uchar c = (uchar)(ulong)ti->bm.bits;
    long parm = (long)grd_gc.fill_parm;

    do {
        int xl = fix_cint(ti->left.x), xr = fix_cint(ti->right.x), x;

        if (xr - xl > 0) {
            if (ti->loop == GRL_OPAQUE)
                WATCOM_CALL((void (*)())grd_uline_fill_vector[GRL_UHLINE], (long)xl, (long)(short)ti->y,
                            (long)(short)(xr - 1), (long)c, parm);
            else if (ti->loop == GRL_TLUC8)
                for (x = xl; x < fix_cint(ti->right.x); x++) {
                    long p = WATCOM_CALL((long (*)())grd_pixel_table[GET_UPIXEL8], (long)(short)x, (long)(short)ti->y);
                    WATCOM_CALL((void (*)())grd_function_table[0], (long)ti->bm.bits[p], (long)(short)x,
                                (long)(short)ti->y);
                }
        } else if (xr - xl < 0)
            return TRUE;
        ti->left.x += ti->left.dx;
        ti->y++;
        ti->right.x += ti->right.dx;
    } while (--ti->n > 0);
    return FALSE;
}

static void poly_common(grs_tmap_loop_info *ti)
{
    ti->loop_func = WATCOM_FN(gri_gen_poly_loop);
    ti->left_edge_func = WATCOM_FN(gri_x_edge);
    ti->right_edge_func = WATCOM_FN(gri_x_edge);
}

void gri_gen_poly_init(grs_tmap_loop_info *ti)
{
    ti->loop = GRL_OPAQUE;
    poly_common(ti);
}

void gri_gen_tpoly_init(grs_tmap_loop_info *ti)
{
    uchar *t = tluc8tab_p[(uchar)(ulong)ti->bm.bits];

    if (t != NULL) {
        ti->loop = GRL_TLUC8;
        ti->bm.bits = t;
    } else
        ti->loop = GRL_OPAQUE;
    poly_common(ti);
}

int gri_gen_spoly_loop(grs_tmap_loop_info *ti)
{
    do {
        fix i = ti->left.i, di;
        int x, d;

        di = tm_fix_div(ti->right.i - ti->left.i, ti->right.x - ti->left.x);
        i += tm_fix_mul(fix_ceil(ti->left.x) - ti->left.x, di);
        x = fix_cint(ti->left.x);
        d = fix_cint(ti->right.x) - x;
        if (d > 0) {
            if (ti->loop == GRL_OPAQUE)
                for (; x < fix_cint(ti->right.x); x++, i += di)
                    WATCOM_CALL((void (*)())grd_function_table[0], (long)(i >> 16), (long)(short)x, (long)(short)ti->y);
            else if (ti->loop == GRL_TLUC8)
                for (; x < fix_cint(ti->right.x); x++, i += di) {
                    long p = WATCOM_CALL((long (*)())grd_pixel_table[GET_UPIXEL8], (long)(short)x, (long)(short)ti->y);
                    WATCOM_CALL((void (*)())grd_function_table[0], (long)tluc8stab[p + ((i >> 16) << 8)],
                                (long)(short)x, (long)(short)ti->y);
                }
        } else if (d < 0)
            return TRUE;
        ti->left.x += ti->left.dx;
        ti->y++;
        ti->right.x += ti->right.dx;
        ti->left.i += ti->left.di;
        ti->right.i += ti->right.di;
    } while (--ti->n > 0);
    return FALSE;
}

static void spoly_common(grs_tmap_loop_info *ti)
{
    ti->loop_func = WATCOM_FN(gri_gen_spoly_loop);
    ti->left_edge_func = WATCOM_FN(gri_ix_edge);
    ti->right_edge_func = WATCOM_FN(gri_ix_edge);
}

void gri_gen_spoly_init(grs_tmap_loop_info *ti)
{
    ti->loop = GRL_OPAQUE;
    spoly_common(ti);
}

void gri_gen_stpoly_init(grs_tmap_loop_info *ti)
{
    ti->loop = tluc8stab != NULL ? GRL_TLUC8 : GRL_OPAQUE;
    spoly_common(ti);
}
