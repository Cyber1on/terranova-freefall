/* mxstate.c -- Terra Nova v1.09 ModeX canvas: push/pop of the plane state (canvas table
 * PUSH_STATE / POP_STATE of table 0x36808C).  Decompiled from __FF.EXE (Watcom C); names ours.
 *
 *   0x33B0F0 modex_push_state   0x33B12C modex_pop_state
 */
#include "2d_tmap.h"

extern int grd_mx_state_depth;            /* 0x36EF4C */
extern uchar grd_mx_state_stack[10][2];   /* 0x4608E8 {map mask, read map} */

int modex_push_state(void)
{
    int d = grd_mx_state_depth, r;

    if (d >= 10)
        r = -1;
    else {
        d++;
        grd_mx_state_stack[d - 1][0] = grd_mx_wmask;
        grd_mx_state_stack[d - 1][1] = grd_mx_rplane;
        r = 0;
    }
    grd_mx_state_depth = d;
    return r;
}

int modex_pop_state(void)
{
    uchar m;

    if (grd_mx_state_depth <= 0)
        return -1;
    grd_mx_state_depth--;
    m = grd_mx_state_stack[grd_mx_state_depth][0];
    if (grd_mx_wmask != m) {
        grd_mx_wmask = m;
        tm_outw(0x3C4, m << 8 | 2);
    }
    m = grd_mx_state_stack[grd_mx_state_depth][1];
    if (grd_mx_rplane != m) {
        grd_mx_rplane = m;
        tm_outw(0x3CE, m << 8 | 4);
    }
    return 0;
}
