/* invcmap.c -- inverse palette (grd_ipal: 15-bit rgb -> palette index) for the 2D library
 * (Terra Nova v1.09).  The core is Spencer W. Thomas' incremental inverse colormap algorithm
 * (inv_cmap.c, Graphics Gems II), compiled with the statics as globals; names from that file,
 * the wrappers' names ours.  Decompiled from __FF.EXE.
 *
 *   0x2F1130 ipal_alloc      0x2F1198 ipal_free       0x2F11C4 ipal_update
 *   0x2F11E4 ipal_build      0x2F12C4 inv_cmap        0x2F14E0 inv_cmap_redloop
 *   0x2F1664 inv_cmap_greenloop  0x2F1954 inv_cmap_blueloop  0x2F1B3C inv_cmap_maxfill
 *
 * grs_rgb holds r at bits 2..9, g at 13..20, b at 24..31.  The colormap arrays are passed
 * as {b, g, r}, so the "red" axis of inv_cmap is blue and the ipal index is b<<10 | g<<5 | r.
 * Differences from the Graphics Gems text: maxfill uses the global colormax (one argument).
 */
#include "t2_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
int icm_rcenter, icm_gcenter, icm_cindex, icm_colormax;
long icm_crinc, icm_cginc, icm_cbinc;
uchar *icm_crgbp, *icm_rrgbp, *icm_grgbp;
ulong *icm_rdp;
long icm_x;
ulong *icm_gdp;
long icm_xsqr;
ulong *icm_cdp;
long icm_cdist, icm_rdist, icm_gdist;
int icm_rstride, icm_gstride, icm_bcenter;
long icm_rxx;
uchar *icm_gcrgbp;
ulong *icm_gcdp;
long icm_gcdist, icm_gxx, icm_ginc;
int icm_g_prevmin, icm_g_prevmax, icm_g_max, icm_g_min, icm_g_here;
long icm_binc;
int icm_b_prevmax, icm_b_prevmin, icm_b_max, icm_b_min, icm_b_here;
#endif
extern int icm_rcenter, icm_gcenter, icm_cindex, icm_colormax;   /* 0x44BCE8 0x44BCEC 0x44BCF0 0x44BCF4 */
extern long icm_crinc, icm_cginc, icm_cbinc;                     /* 0x44BCF8 0x44BCFC 0x44BD00 */
extern uchar *icm_crgbp, *icm_rrgbp, *icm_grgbp;                 /* 0x44BD04 0x44BD08 0x44BD0C */
extern ulong *icm_rdp;                                           /* 0x44BD10 */
extern long icm_x;                                               /* 0x44BD14 */
extern ulong *icm_gdp;                                           /* 0x44BD18 */
extern long icm_xsqr;                                            /* 0x44BD1C */
extern ulong *icm_cdp;                                           /* 0x44BD20 */
extern long icm_cdist, icm_rdist, icm_gdist;                     /* 0x44BD24 0x44BD28 0x44BD2C */
extern int icm_rstride, icm_gstride, icm_bcenter;                /* 0x44BD30 0x44BD34 0x44BD38 */
extern long icm_rxx;                                             /* 0x44BD3C redloop */
extern uchar *icm_gcrgbp;                                        /* 0x44BD40 greenloop */
extern ulong *icm_gcdp;                                          /* 0x44BD44 */
extern long icm_gcdist, icm_gxx, icm_ginc;                       /* 0x44BD48 0x44BD4C 0x44BD50 */
extern int icm_g_prevmin, icm_g_prevmax, icm_g_max, icm_g_min, icm_g_here; /* 0x44BD54..0x44BD64 */
extern long icm_binc;                                            /* 0x44BD68 blueloop */
extern int icm_b_prevmax, icm_b_prevmin, icm_b_max, icm_b_min, icm_b_here; /* 0x44BD6C..0x44BD7C */

/* Allocates grd_ipal (32 KB, freeing an old one) and builds it from grd_bpal.
 * 0 ok, -1 out of memory. */
int ipal_alloc(void)
{
    if (grd_ipal) {
        gr_free(grd_ipal);
        grd_ipal = 0;
    }
    grd_ipal = gr_malloc(0x8000);
    if (grd_ipal == 0)
        return -1;
    {
        int r = ipal_build(grd_bpal, grd_ipal);
        if (r < 0)
            return r;
    }
    return 0;
}

/* 0, or -2 when there is none. */
int ipal_free(void)
{
    if (grd_ipal == 0)
        return -2;
    gr_free(grd_ipal);
    grd_ipal = 0;
    return 0;
}

/* Rebuilds grd_ipal from grd_bpal; -3 when there is no ipal. */
int ipal_update(void)
{
    if (grd_ipal == 0)
        return -3;
    return ipal_build(grd_bpal, grd_ipal);
}

/* 0, or -1 when out of memory. */
int ipal_build(grs_rgb *bpal, uchar *ipal)
{
    uchar *cmap[3];
    uchar *buf;
    ulong *dist;
    int i;
    buf = gr_malloc(0x300);
    if (buf == 0)
        return -1;
    cmap[0] = buf;
    cmap[1] = buf + 0x100;
    cmap[2] = buf + 0x200;
    for (i = 0; i < 256; i++) {
        cmap[2][i] = bpal[i] >> 2;
        cmap[1][i] = bpal[i] >> 13;
        cmap[0][i] = bpal[i] >> 24;
    }
    dist = gr_malloc(0x20000);
    if (dist == 0)
        return -1;           /* leaks buf (original) */
    inv_cmap(256, cmap, 5, dist, ipal);
    gr_free(dist);
    gr_free(buf);
    return 0;
}

void inv_cmap(int colors, uchar **colormap, int bits, ulong *dist_buf, uchar *rgbmap)
{
    int nbits = 8 - bits;
    long x;
    ulong *p;
    long n;
    int rcolor, gcolor, bcolor;

    icm_colormax = 1 << bits;
    x = 1 << nbits;
    icm_xsqr = 1 << (2 * nbits);
    icm_gstride = icm_colormax;
    icm_rstride = icm_colormax * icm_colormax;

    /* maxfill (inlined) */
    for (p = dist_buf, n = icm_rstride * icm_colormax; n > 0; n--)
        *p++ = 0xFFFFFFFF;

    for (icm_cindex = 0; icm_cindex < colors; icm_cindex++) {
        rcolor = colormap[0][icm_cindex];
        gcolor = colormap[1][icm_cindex];
        bcolor = colormap[2][icm_cindex];
        icm_rcenter = rcolor >> nbits;
        icm_gcenter = gcolor >> nbits;
        icm_bcenter = bcolor >> nbits;

        icm_rdist = rcolor - (icm_rcenter * x + x / 2);
        icm_gdist = gcolor - (icm_gcenter * x + x / 2);
        icm_cdist = bcolor - (icm_bcenter * x + x / 2);
        icm_cdist = icm_rdist * icm_rdist + icm_gdist * icm_gdist + icm_cdist * icm_cdist;

        icm_crinc = 2 * ((icm_rcenter + 1) * icm_xsqr - (rcolor * x));
        icm_cginc = 2 * ((icm_gcenter + 1) * icm_xsqr - (gcolor * x));
        icm_cbinc = 2 * ((icm_bcenter + 1) * icm_xsqr - (bcolor * x));

        icm_cdp = dist_buf + icm_rcenter * icm_rstride + icm_gcenter * icm_gstride + icm_bcenter;
        icm_crgbp = rgbmap + icm_rcenter * icm_rstride + icm_gcenter * icm_gstride + icm_bcenter;
        icm_x = x;

        inv_cmap_redloop();
    }
    icm_x = x;
}

int inv_cmap_redloop(void)
{
    int detect = 0;
    int r;
    int first;
    long txsqr = icm_xsqr + icm_xsqr;

    /* Basic loop up. */
    icm_rdist = icm_cdist;
    icm_rdp = icm_cdp;
    icm_rrgbp = icm_crgbp;
    for (r = icm_rcenter, icm_rxx = icm_crinc, first = 1; r < icm_colormax; ) {
        if (inv_cmap_greenloop(first))
            detect = 1;
        else if (detect)
            break;
        r++;
        icm_rdp += icm_rstride;
        icm_rrgbp += icm_rstride;
        icm_rdist += icm_rxx;
        icm_rxx += txsqr;
        first = 0;
    }

    /* Basic loop down. */
    icm_rxx = icm_crinc - txsqr;
    icm_rdist = icm_cdist - icm_rxx;
    icm_rdp = icm_cdp - icm_rstride;
    icm_rrgbp = icm_crgbp - icm_rstride;
    for (r = icm_rcenter - 1, first = 1; r >= 0; ) {
        if (inv_cmap_greenloop(first))
            detect = 1;
        else if (detect)
            break;
        r--;
        icm_rdp -= icm_rstride;
        icm_rrgbp -= icm_rstride;
        icm_rxx -= txsqr;
        icm_rdist -= icm_rxx;
        first = 0;
    }
    return detect;
}

int inv_cmap_greenloop(int restart)
{
    int detect;
    int g;
    int first;
    long txsqr = icm_xsqr + icm_xsqr;
    int thismax, thismin;

    if (restart) {
        icm_g_here = icm_gcenter;
        icm_g_min = 0;
        icm_g_max = icm_colormax - 1;
        icm_ginc = icm_cginc;
        icm_g_prevmax = 0;
        icm_g_prevmin = icm_colormax;
    }

    thismin = icm_g_min;
    thismax = icm_g_max;
    detect = 0;

    /* Basic loop up. */
    icm_gcdist = icm_gdist = icm_rdist;
    icm_gcdp = icm_gdp = icm_rdp;
    icm_gcrgbp = icm_grgbp = icm_rrgbp;
    for (g = icm_g_here, icm_gxx = icm_ginc, first = 1; g <= icm_g_max; ) {
        if (inv_cmap_blueloop(first)) {
            if (!detect) {
                /* Remember here and associated data! */
                if (g > icm_g_here) {
                    icm_g_here = g;
                    icm_rdp = icm_gcdp;
                    icm_rrgbp = icm_gcrgbp;
                    icm_rdist = icm_gcdist;
                    icm_ginc = icm_gxx;
                    thismin = g;
                }
                detect = 1;
            }
        } else if (detect) {
            thismax = g - 1;
            break;
        }
        g++;
        icm_gdp += icm_gstride;
        icm_gcdp += icm_gstride;
        icm_grgbp += icm_gstride;
        icm_gcrgbp += icm_gstride;
        icm_gdist += icm_gxx;
        icm_gcdist += icm_gxx;
        icm_gxx += txsqr;
        first = 0;
    }

    /* Basic loop down. */
    icm_gxx = icm_ginc - txsqr;
    icm_gcdist = icm_gdist = icm_rdist - icm_gxx;
    icm_gcdp = icm_gdp = icm_rdp - icm_gstride;
    icm_gcrgbp = icm_grgbp = icm_rrgbp - icm_gstride;
    for (g = icm_g_here - 1, first = 1; g >= icm_g_min; ) {
        if (inv_cmap_blueloop(first)) {
            if (!detect) {
                /* Remember here! */
                icm_g_here = g;
                icm_rdp = icm_gcdp;
                icm_rrgbp = icm_gcrgbp;
                icm_rdist = icm_gcdist;
                icm_ginc = icm_gxx;
                thismax = g;
                detect = 1;
            }
        } else if (detect) {
            thismin = g + 1;
            break;
        }
        g--;
        icm_gdp -= icm_gstride;
        icm_gcdp -= icm_gstride;
        icm_grgbp -= icm_gstride;
        icm_gcrgbp -= icm_gstride;
        icm_gxx -= txsqr;
        icm_gdist -= icm_gxx;
        icm_gcdist -= icm_gxx;
        first = 0;
    }

    /* If we saw something, update the edge trackers.  For now, only tracks edges that are
     * "shrinking" (min increasing, max decreasing). */
    if (detect) {
        if (thismax < icm_g_prevmax)
            icm_g_max = thismax;
        icm_g_prevmax = thismax;
        if (thismin > icm_g_prevmin)
            icm_g_min = thismin;
        icm_g_prevmin = thismin;
    }
    return detect;
}

int inv_cmap_blueloop(int restart)
{
    int detect;
    ulong *dp;
    uchar *rgbp;
    ulong bdist;
    long bxx;
    int b, i = icm_cindex;
    long txsqr = icm_xsqr + icm_xsqr;
    int lim;
    int thismin, thismax;

    if (restart) {
        icm_b_here = icm_bcenter;
        icm_b_min = 0;
        icm_b_max = icm_colormax - 1;
        icm_binc = icm_cbinc;
        icm_b_prevmin = icm_colormax;
        icm_b_prevmax = 0;
    }

    detect = 0;
    thismin = icm_b_min;
    thismax = icm_b_max;

    /* Basic loop up.  First loop just finds first applicable cell. */
    for (b = icm_b_here, bdist = icm_gdist, bxx = icm_binc, dp = icm_gdp, rgbp = icm_grgbp,
         lim = icm_b_max;
         b <= lim;
         b++, dp++, rgbp++, bdist += bxx, bxx += txsqr) {
        if (*dp > bdist) {
            /* Remember new 'here' and associated data! */
            if (b > icm_b_here) {
                icm_b_here = b;
                icm_gdp = dp;
                icm_grgbp = rgbp;
                icm_gdist = bdist;
                icm_binc = bxx;
                thismin = b;
            }
            detect = 1;
            break;
        }
    }
    /* Second loop fills in a run of closer cells. */
    for (; b <= lim; b++, dp++, rgbp++, bdist += bxx, bxx += txsqr) {
        if (*dp > bdist) {
            *dp = bdist;
            *rgbp = i;
        } else {
            thismax = b - 1;
            break;
        }
    }

    /* Basic loop down.  Do initializations here, since the 'find' loop might not get
     * executed. */
    lim = icm_b_min;
    b = icm_b_here - 1;
    bxx = icm_binc - txsqr;
    bdist = icm_gdist - bxx;
    dp = icm_gdp - 1;
    rgbp = icm_grgbp - 1;
    /* The 'find' loop is executed only if we didn't already find something. */
    if (!detect)
        for (; b >= lim; b--, dp--, rgbp--, bxx -= txsqr, bdist -= bxx) {
            if (*dp > bdist) {
                /* Remember here!  No test for b against here necessary because b < here
                 * by definition. */
                icm_b_here = b;
                icm_gdp = dp;
                icm_grgbp = rgbp;
                icm_gdist = bdist;
                icm_binc = bxx;
                thismax = b;
                detect = 1;
                break;
            }
        }
    /* The 'update' loop. */
    for (; b >= lim; b--, dp--, rgbp--, bxx -= txsqr, bdist -= bxx) {
        if (*dp > bdist) {
            *dp = bdist;
            *rgbp = i;
        } else {
            thismin = b + 1;
            break;
        }
    }

    /* If we saw something, update the edge trackers. */
    if (detect) {
        if (thismax < icm_b_prevmax)
            icm_b_max = thismax;
        if (thismin > icm_b_prevmin)
            icm_b_min = thismin;
        icm_b_prevmax = thismax;
        icm_b_prevmin = thismin;
    }
    return detect;
}

void inv_cmap_maxfill(ulong *buffer)
{
    long n = icm_colormax * icm_colormax * icm_colormax;
    for (; n > 0; n--)
        *buffer++ = 0xFFFFFFFF;
}
