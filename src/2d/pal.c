/* pal.c -- x:\prj\tech\libsrc\2d\pal.c (+ gamma correction) (Terra Nova v1.09): palette.
 * Decompiled from __FF.EXE.
 *
 *   0x2E7C30 gri_set_gamma_screen_pal (name ours)   0x2E7CC4 gr_set_pal
 *   0x2E7DA8 gr_get_pal                              0x2ED500 gr_set_gamma_pal
 *   data: 0x35F594 grd_gamma_on, 0x35F595 grd_gamma_tab[256], 0x44B7F8 grd_gamma (names ours)
 *
 * Terra Nova keeps a 256-entry gamma table.  While grd_gamma_on is set (by the game),
 * gr_set_pal sends the gamma-corrected colours to the hardware; grd_pal always keeps the
 * uncorrected ones.  Unlike System Shock, gr_set_gamma_pal only sends entries start..start+n-1.
 */
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar grd_gamma_on = 0;                       /* 0x35F594 */
uchar grd_gamma_tab[256] = {                  /* 0x35F595 identity */
#define R16(b) b, b + 1, b + 2, b + 3, b + 4, b + 5, b + 6, b + 7, b + 8, b + 9, b + 10, b + 11, b + 12, b + 13, b + 14, b + 15
    R16(0), R16(16), R16(32), R16(48), R16(64), R16(80), R16(96), R16(112),
    R16(128), R16(144), R16(160), R16(176), R16(192), R16(208), R16(224), R16(240)
#undef R16
};
fix grd_gamma = 0;                            /* 0x44B7F8 gamma the table was built for */
#endif

/* Sends entries start..start+n-1 of grd_pal, gamma corrected, to the hardware. */
void gri_set_gamma_screen_pal(int start, int n)
{
    uchar *tmp;
    int i;

    if ((tmp = temp_malloc(3 * 256)) == NULL)
        return;
    for (i = 3 * start; i < 3 * (start + n); i++)
        tmp[i] = grd_gamma_tab[grd_pal[i]];
    WATCOM_CALL(grd_device_table[GRT_SET_PAL], start, n, (long)(tmp + 3 * start));
    temp_free(tmp);
}

void gr_set_pal(int start, int n, uchar *pal_data)
{
    int i;
    uchar *p;

    if (n <= 0)
        return;
    memcpy(grd_pal + 3 * start, pal_data, 3 * n);
    for (i = start, p = grd_pal + 3 * start; i < start + n; i++, p += 3)
        grd_bpal[i] = gr_bind_rgb(p[0], p[1], p[2]);
    if (grd_gamma_on)
        gri_set_gamma_screen_pal(start, n);
    else
        WATCOM_CALL(grd_device_table[GRT_SET_PAL], start, n, (long)(grd_pal + 3 * start));
}

void gr_get_pal(int start, int n, uchar *pal_data)
{
    if (n <= 0)
        return;
    memcpy(pal_data, grd_pal + 3 * start, 3 * n);
}

/* Rebuilds the gamma table if gamma changed, then sends entries start..start+n-1. */
void gr_set_gamma_pal(int start, int n, fix gamma)
{
    int i;

    if (gamma != grd_gamma) {
        for (i = 0; i < 256; i++)
            grd_gamma_tab[i] = (uchar)(((ulong)fix_pow(fix_make(i, 0) / 255, gamma) * 255 + 0x8000) >> 16); /* fix_rint(x*255) */
        grd_gamma = gamma;
    }
    gri_set_gamma_screen_pal(start, n);
}
