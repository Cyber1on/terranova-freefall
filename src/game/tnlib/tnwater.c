/* tnwater.c -- edge scan driver of the water renderer (Terra Nova v1.09; name ours).
 * Decompiled from __FF.EXE.
 *
 *   0x2FB220 water_scan_edges
 *
 * vp is a polygon of n vertex pointers whose first field is the scan coordinate (fix).
 * From the top vertex (smallest ceil) the left chain is walked backwards and the right chain
 * forwards; each edge (from the last vertex at the current scan line to the next vertex
 * below it) goes to the asm edge routines (original, 0x317DF0 / 0x317EA0) that write per
 * scan line entries into buf.  Returns 0 for a flat polygon, 2 when a left edge reports a
 * failure, else the OR of the right edge results.
 */
#include "t2_a.h"

#define CINT(v) ((short)((long)((ulong)(v) + 0xFFFF) >> 16))

int water_scan_edges(int n, fix **vp, long *buf)
{
    fix **end = vp + n;
    fix **top = vp, **p, **e, **last;
    int ymin, ymax, y, yn, ret = 0;

    ymin = ymax = CINT(**vp);
    for (p = vp + 1; p < end; p++) {
        y = CINT(**p);
        if (y < ymin) {
            ymin = y;
            top = p;
        }
        if (y > ymax)
            ymax = y;
    }
    if (ymin == ymax)
        return ret;

    /* left chain (backwards) */
    last = vp + n - 1;
    p = e = top;
    y = ymin;
    do {
        yn = CINT(**p);
        do {
            if (yn == y)
                e = p;
            if (--p < vp)
                p = last;
            yn = CINT(**p);
        } while (yn <= y);
        if (water_left_edge(p, e, buf))
            return 2;
        y = yn;
    } while (yn != ymax);

    /* right chain (forwards) */
    p = e = top;
    y = ymin;
    do {
        yn = CINT(**p);
        do {
            if (yn == y)
                e = p;
            if (++p >= end)
                p = vp;
            yn = CINT(**p);
        } while (yn <= y);
        ret |= water_right_edge(p, e, buf);
        y = yn;
    } while (yn != ymax);
    return ret;
}
