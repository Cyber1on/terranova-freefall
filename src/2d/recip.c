/* recip.c -- (Terra Nova v1.09): fixed-point reciprocal table.  Decompiled from __FF.EXE.
 *
 *   0x325A70 init_inverse_table (name from 2d-core's 2d.h; called from 0x31AACB)
 *
 * Fills grd_recip_tab (0x45F71C, the 2d-tmap name): tab[0] = 0x7FFFFFFF ("1/0"),
 * tab[n] = fix_div(1, n) = 0x10000 / n for n = 1..1023 (1024 entries in all).
 */
#include "2d_prim.h"

void init_inverse_table(void)
{
    int n;

    grd_recip_tab[0] = 0x7fffffff;
    for (n = 1; n < 1024; n++)
        grd_recip_tab[n] = 0x10000 / n;
}
