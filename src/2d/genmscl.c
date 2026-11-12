/* genmscl.c -- Terra Nova v1.09: monochrome bitmap scaler for the 8-bit banked VESA canvas
 * (function table 0x3686E0, BMT_MONO GRC_SCALE..GRC_TRANS_CLUT_SCALE).  Decompiled from
 * __FF.EXE (Watcom C); the init names are Shock's (Gen/GENTF.H), the loop name ours.
 *
 *   0x33E658 gri_gen_mono_scale_umap_loop
 *   0x33EBB8 gri_gen_mono_trans_scale_umap_init        (loop 1)
 *   0x33EBD8 gri_gen_mono_opaque_scale_umap_init       (loop 0)
 *   0x33EBF8 gri_gen_mono_trans_clut_scale_umap_init   (loop 5)
 *   0x33EC18 gri_gen_mono_opaque_clut_scale_umap_init  (loop 4)
 *   0x33EC38 gri_gen_mono_trans_solid_scale_umap_init  (loop 9)
 *
 * Set bits draw grd_gc.fcolor (opaque: clear bits draw bcolor), through ti->clut for the clut
 * loops; the solid loop draws the low byte of ti->clut.  Rows are written straight into the
 * bank window.  Quirks kept: the loop returns 1 (abort) for an empty first span; every row
 * starts at the same u; when a row crosses the end of the window the rest of it restarts at
 * that u (so it repeats the start of the source row); unknown loop types draw nothing.
 */
#include "2d_tmap.h"

extern uchar bitmask[8];                   /* 0x36EF28 {0x80, 0x40, .., 1} (2d-prim csv) */

#define SET_BANK() WATCOM_CALL((void (*)())grd_device_table[GRT_SET_BANK], (long)(short)grd_bank8_bank)

int gri_gen_mono_scale_umap_loop(grs_tmap_loop_info *ti)
{
    uchar *win = grd_cap->vbase, *src, *p;
    int xl = fix_cint(ti->left.x);
    int cnt = fix_cint(ti->right.x) - xl, i, n;
    fix u, du, uu;
    long addr, room;

    if (cnt <= 0)
        return 1;
    du = tm_fix_div(ti->right.u - ti->left.u, ti->right.x - ti->left.x);
    u = ti->left.u + tm_fix_mul(du, fix_ceil(ti->left.x) - ti->left.x);
    u += ti->bm_align << 16;
    addr = (long)((ushort)grd_bm.row * ti->y + (long)grd_bm.bits + xl);
    {
        int l = grd_bank8_lock;
        long bank = addr >> 16;
        if (l != 0 || grd_bank8_bank != bank) {
            grd_bank8_bank = (ushort)bank;
            grd_bank8_lock = l + 1;
            SET_BANK();
            grd_bank8_lock--;
        }
    }
    do {
        room = 0x10000 - (addr & 0xffff);
        src = ti->bm.bits + (ti->left.v >> 16) * (ulong)(ushort)ti->bm_row;
        n = room < cnt ? room : cnt;
        i = 0;
        p = win + (addr & 0xffff);
        for (;;) {
            uchar *q = p + i;
            int set;

#define BIT() (set = src[uu >> 19] & bitmask[(uu >> 16) & 7])
            switch (ti->loop) {
            case 0:
                for (uu = u; i < n; i++, uu += du)
                    *q++ = (uchar)(BIT() ? grd_gc.fcolor : grd_gc.bcolor);
                break;
            case 1:
                for (uu = u; i < n; i++, uu += du, q++)
                    if (BIT())
                        *q = (uchar)grd_gc.fcolor;
                break;
            case 4:
                for (uu = u; i < n; i++, uu += du)
                    *q++ = ti->clut[(uchar)(BIT() ? grd_gc.fcolor : grd_gc.bcolor)];
                break;
            case 5:
                for (uu = u; i < n; i++, uu += du, q++)
                    if (BIT())
                        *q = ti->clut[(uchar)grd_gc.fcolor];
                break;
            case 9:
                for (uu = u; i < n; i++, uu += du, q++)
                    if (BIT())
                        *q = (uchar)(ulong)ti->clut;
                break;
            }
            if (room >= cnt) {
                if ((ushort)grd_bm.row >= room) {
                    grd_bank8_bank++;
                    grd_bank8_lock++;
                    SET_BANK();
                    grd_bank8_lock--;
                }
                break;
            }
            if (n != room)
                break;
            grd_bank8_bank++;
            grd_bank8_lock++;
            SET_BANK();
            grd_bank8_lock--;
            p = win - i;
            n = cnt;
        }
        ti->left.v += ti->left.dv;
        ti->y++;
        addr += (ushort)grd_bm.row;
    } while (--ti->n > 0);
    return 0;
}

static void mono_scale_init(grs_tmap_loop_info *ti, int loop)
{
    ti->loop = (uchar)loop;
    ti->loop_func = WATCOM_FN(gri_gen_mono_scale_umap_loop);
    ti->right_edge_func = WATCOM_FN(gr_null);
    ti->left_edge_func = WATCOM_FN(gri_scale_edge);
}

void gri_gen_mono_trans_scale_umap_init(grs_tmap_loop_info *ti) { mono_scale_init(ti, 1); }
void gri_gen_mono_opaque_scale_umap_init(grs_tmap_loop_info *ti) { mono_scale_init(ti, 0); }
void gri_gen_mono_trans_clut_scale_umap_init(grs_tmap_loop_info *ti) { mono_scale_init(ti, 5); }
void gri_gen_mono_opaque_clut_scale_umap_init(grs_tmap_loop_info *ti) { mono_scale_init(ti, 4); }
void gri_gen_mono_trans_solid_scale_umap_init(grs_tmap_loop_info *ti) { mono_scale_init(ti, 9); }

/* The same monochrome scaler drawing through grd_function_table[GRC_PIXEL] (used by the ModeX
 * function table 0x36AB1C, BMT_MONO GRC_SCALE.. in FILL_NORM and FILL_CLUT); names ours.
 *
 *   0x340C78 gri_pix_mono_scale_umap_loop
 *   0x340FE4 gri_pix_mono_trans_scale_umap_init        (loop 1)
 *   0x341004 gri_pix_mono_opaque_scale_umap_init       (loop 0)
 *   0x341024 gri_pix_mono_trans_clut_scale_umap_init   (loop 5)
 *   0x341044 gri_pix_mono_opaque_clut_scale_umap_init  (loop 4)
 *   0x341064 gri_pix_mono_trans_solid_scale_umap_init  (loop 9, not in any table)
 *
 * The x span and starting u come from the first row only (as in genscl.c); an empty span
 * returns 1.
 */
#define PIX(c, x, y) WATCOM_CALL((void (*)())grd_function_table[0], (long)(c), (long)(short)(x), (long)(short)(y))

int gri_pix_mono_scale_umap_loop(grs_tmap_loop_info *ti)
{
    int xl = fix_cint(ti->left.x), xr = fix_cint(ti->right.x), x;
    fix u, du, uu;
    uchar *src;

    if (xr <= xl)
        return 1;
    du = tm_fix_div(ti->right.u - ti->left.u, ti->right.x - ti->left.x);
    u = ti->left.u + tm_fix_mul(du, fix_ceil(ti->left.x) - ti->left.x);
    u += ti->bm_align << 16;
    do {
        src = ti->bm.bits + (ti->left.v >> 16) * (long)(ushort)ti->bm_row;
#define SET(uu) (src[(uu) >> 19] & bitmask[((uu) >> 16) & 7])
        switch (ti->loop) {
        case 0:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                PIX((uchar)(SET(uu) ? grd_gc.fcolor : grd_gc.bcolor), x, ti->y);
            break;
        case 1:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                if (SET(uu))
                    PIX((uchar)grd_gc.fcolor, x, ti->y);
            break;
        case 4:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                PIX(ti->clut[(uchar)(SET(uu) ? grd_gc.fcolor : grd_gc.bcolor)], x, ti->y);
            break;
        case 5:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                if (SET(uu))
                    PIX(ti->clut[(uchar)grd_gc.fcolor], x, ti->y);
            break;
        case 9:
            for (x = xl, uu = u; x < xr; x++, uu += du)
                if (SET(uu))
                    PIX((uchar)(ulong)ti->clut, x, ti->y);
            break;
        }
#undef SET
        ti->left.v += ti->left.dv;
        ti->y++;
    } while (--ti->n > 0);
    return 0;
}

static void pix_mono_scale_init(grs_tmap_loop_info *ti, int loop)
{
    ti->loop = (uchar)loop;
    ti->loop_func = WATCOM_FN(gri_pix_mono_scale_umap_loop);
    ti->right_edge_func = WATCOM_FN(gr_null);
    ti->left_edge_func = WATCOM_FN(gri_scale_edge);
}

void gri_pix_mono_trans_scale_umap_init(grs_tmap_loop_info *ti) { pix_mono_scale_init(ti, 1); }
void gri_pix_mono_opaque_scale_umap_init(grs_tmap_loop_info *ti) { pix_mono_scale_init(ti, 0); }
void gri_pix_mono_trans_clut_scale_umap_init(grs_tmap_loop_info *ti) { pix_mono_scale_init(ti, 5); }
void gri_pix_mono_opaque_clut_scale_umap_init(grs_tmap_loop_info *ti) { pix_mono_scale_init(ti, 4); }
void gri_pix_mono_trans_solid_scale_umap_init(grs_tmap_loop_info *ti) { pix_mono_scale_init(ti, 9); }
