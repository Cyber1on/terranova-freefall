/* f_exp.c -- x:\prj\tech\libsrc\fix\f_exp.c (Terra Nova v1.09), 0x2FACB0
 * Decompiled from __FF.EXE; identical to System Shock's F_EXP.C. */
#include "fix.h"

/* Returns e to the x.  No range checking on the high end. */
fix fix_exp(fix x)
{
    int int_part = fix_int(x);
    fix exp_int_part, loy, hiy, exp_frac_part;
    int basex, fracx;

    /* If the exponent goes off the small end of the table, return 0. */
    if (int_part + INTEGER_EXP_OFFSET < 0) return 0;

    exp_int_part = expinttab[int_part + INTEGER_EXP_OFFSET];

    basex = fix_frac(x) >> 12;
    fracx = x & 0x0fff;

    loy = expfractab[basex];
    hiy = expfractab[basex + 1];

    exp_frac_part = loy + (hiy - loy) * fracx / 0x1000;
    return fix_mul(exp_int_part, exp_frac_part);
}
