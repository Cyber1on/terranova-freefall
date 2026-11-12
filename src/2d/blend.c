/* blend.c -- x:\prj\tech\libsrc\2d\blend.c (Terra Nova v1.09): blend tables.
 * Decompiled from __FF.EXE.
 *
 *   0x34B160 gri_build_blend   0x34B2CC gr_free_blend   0x34B2FC gr_init_blend
 *   data: 0x36EF34 grd_blend, 0x36EF38 grd_half_blend, 0x36EF3C grd_log_blend_levels
 *
 * gr_init_blend(n) builds (1<<n)-1 tables of 64K; table k (0-based) mixes at
 * frac = (k+1) * (256>>n) / 256.  Entry [i<<8 | j] = palette colour nearest to
 * (1-frac)*rgb(i) + frac*rgb(j) through grd_ipal, except that colour 0, i == j and black
 * colours map to themselves (i if i is 0, i == j or black; else j if j is 0 or black).
 * Return values are TN's (inverted from System Shock): gr_init_blend returns 0 on success,
 * 1 on an allocation failure; with n <= 0 it frees the tables and returns 1 if there were
 * none.  gr_free_blend returns 1 if there were no tables, else 0.  Neither clears
 * grd_half_blend.
 */
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar *grd_blend = NULL;
uchar *grd_half_blend = NULL;
int grd_log_blend_levels = 0;
#endif

#define RGB_R(c) (((c) >> 2) & 0xFF)
#define RGB_G(c) (((c) >> 13) & 0xFF)
#define RGB_B(c) (((c) >> 24) & 0xFF)

void gri_build_blend(uchar *dst, int frac)
{
    uchar *ipal = grd_ipal;
    int i, j, k, idx;
    uchar c1[3], c2[3];

    for (i = 0; i < 256; i++) {
        c1[0] = RGB_R(grd_bpal[i]);
        c1[1] = RGB_G(grd_bpal[i]);
        c1[2] = RGB_B(grd_bpal[i]);
        for (j = 0; j < 256; j++) {
            if (i == 0 || i == j || c1[0] + c1[1] + c1[2] == 0) {
                *dst++ = i;
                continue;
            }
            c2[0] = RGB_R(grd_bpal[j]);
            c2[1] = RGB_G(grd_bpal[j]);
            c2[2] = RGB_B(grd_bpal[j]);
            if (j == 0 || c2[0] + c2[1] + c2[2] == 0) {
                *dst++ = j;
                continue;
            }
            idx = 0;
            for (k = 2; k >= 0; k--)
                idx = (idx << 5) + ((c2[k] * frac + c1[k] * (256 - frac)) >> 11);
            *dst++ = ipal[idx];
        }
    }
}

int gr_free_blend(void)
{
    if (grd_blend == NULL)
        return 1;
    gr_free(grd_blend);
    grd_blend = NULL;
    grd_log_blend_levels = 0;
    return 0;
}

uchar gr_init_blend(int log_blend_levels)
{
    int tab_cnt, step, i, frac;

    if (log_blend_levels <= 0)
        return gr_free_blend();
    tab_cnt = (1 << log_blend_levels) - 1;
    step = 256 >> log_blend_levels;
    if (grd_blend != NULL && gr_free_blend())
        return 1;
    grd_blend = gr_malloc(tab_cnt << 16);
    if (grd_blend == NULL)
        return 1;
    for (i = 0, frac = step; i < tab_cnt; i++, frac += step)
        gri_build_blend(grd_blend + (i << 16), frac);
    grd_log_blend_levels = log_blend_levels;
    grd_half_blend = grd_blend + ((tab_cnt >> 1) << 16);
    return 0;
}
