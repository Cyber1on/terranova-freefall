/* fix.c -- x:\prj\tech\libsrc\fix\fix.c (Terra Nova v1.09)
 *
 * Decompiled from __FF.EXE.  Same algorithms as System Shock's FIX.C (Rex Bradford / Dan
 * Schmidt, LG 1993-94); differences from that file are noted inline.
 *
 *   0x2EC8E0 fix_mul_asm_safe   0x2EC908 fix_pyth_dist      0x2EC930 fix_fast_pyth_dist
 *   0x2EC968 fix_safe_pyth_dist 0x2EC9EC long_safe_pyth_dist 0x2ECA00 fix_sincos
 *   0x2ECA98 fix_sin            0x2ECAE0 fix_cos            0x2ECB28 fix_fastsincos
 *   0x2ECB54 fix_fastsin        0x2ECB6C fix_fastcos        0x2ECB84 fix_asin
 *   0x2ECBD0 fix_acos           0x2ECC24 fix_atan2
 */
#include "fix.h"

/* 0x44B7E0: set by fix_mul_asm_safe when the product does not fit in 16.16. */
#ifndef TN_LINK_ORIGINAL_DATA
uchar gOVResult;
#endif

/* fix_mul that flags overflow.  Not called anywhere in Terra Nova.  The original is a small
 * inline-asm routine:  imul edx; shr eax,16; ror edx,15; inc dx; cmp dx,1; seta gOVResult;
 * dec dx; ror edx,1; mov dx,0; or eax,edx.
 * The result is the ordinary fix_mul; gOVResult is set when bits 47..63 of the 64-bit product
 * are not all equal (the product does not fit in 16.16). */
fix fix_mul_asm_safe(fix a, fix b)
{
    long long p = (long long)a * b;
    ushort top = (ushort)((ulong)(p >> 32) >> 15);   /* bits 47..62 */
    gOVResult = 0;
    gOVResult = (ushort)(top + 1) > 1;
    return (fix)(p >> 16);
}

/* Returns the distance from (0,0) to (a,b).  Can overflow (no check). */
fix fix_pyth_dist(fix a, fix b)
{
    return fix_sqrt(fix_mul(a, a) + fix_mul(b, b));
}

/* Returns an approximation to the distance from (0,0) to (a,b). */
fix fix_fast_pyth_dist(fix a, fix b)
{
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    if (a > b)
        return a + b / 2;
    else
        return b + a / 2;
}

/* Overflow-safe distance (Moler & Morrison, via METAFONT): reflect (a,b) so a^2+b^2 stays the
 * same while b shrinks; when b reaches 0, a is the distance.
 * Terra Nova's version takes the caller's __FILE__/__LINE__ (for a Warning() that the release
 * build compiled out), so it has two unused extra arguments. */
fix fix_safe_pyth_dist(fix a, fix b, char *file, int line)
{
    fix tmp;

    a = a < 0 ? -a : a;                 /* abs(): cdq / xor / sub */
    b = b < 0 ? -b : b;
    if (a < b) {
        tmp = a; a = b; b = tmp;
    }                               /* now 0 <= b <= a */
    if (a > 0) {
        if (a > 0x2fffffff) {
            /* Warning(("Overflow in fix_safe_pyth_dist\n")) -- compiled out */
            return 0;
        }
        for (;;) {
            tmp = fix_div(b, a);
            tmp = fix_mul(tmp, tmp);
            if (tmp == 0) break;
            tmp = fix_div(tmp, tmp + fix_make(4, 0));
            a += fix_mul(2 * a, tmp);
            b = fix_mul(b, tmp);
        }
    }
    return a;
}

/* The fix function works because the difference in scale doesn't matter. */
long long_safe_pyth_dist(long a, long b)
{
    return fix_safe_pyth_dist(a, b, "x:\\prj\\tech\\libsrc\\fix\\fix.c", 197);
}

/* Table lookup with linear interpolation on the low byte. */
#define INTERP(lo, hi, frac) ((short)((lo) + (short)((((short)(hi) - (short)(lo)) * (uchar)(frac)) >> 8)))

/* Computes sin and cos of theta (cos x = sin(x + PI/2), 64 table entries on). */
void fix_sincos(fixang theta, fix *sin, fix *cos)
{
    uchar baseth = theta >> 8, fracth = theta & 0xff;
    ushort lowsin = sintab[baseth], hisin = sintab[baseth + 1];
    ushort lowcos = sintab[baseth + 64], hicos = sintab[baseth + 65];

    *sin = INTERP(lowsin, hisin, fracth) << 2;
    *cos = INTERP(lowcos, hicos, fracth) << 2;
}

fix fix_sin(fixang theta)
{
    uchar baseth = theta >> 8, fracth = theta & 0xff;
    return INTERP(sintab[baseth], sintab[baseth + 1], fracth) << 2;
}

fix fix_cos(fixang theta)
{
    uchar baseth = theta >> 8, fracth = theta & 0xff;
    return INTERP(sintab[baseth + 64], sintab[baseth + 65], fracth) << 2;
}

/* Faster than fix_sincos() but not as accurate (no interpolation). */
void fix_fastsincos(fixang theta, fix *sin, fix *cos)
{
    *sin = ((short)sintab[theta >> 8]) << 2;
    *cos = ((short)sintab[(theta >> 8) + 64]) << 2;
}

fix fix_fastsin(fixang theta)
{
    return ((short)sintab[theta >> 8]) << 2;
}

fix fix_fastcos(fixang theta)
{
    return ((short)sintab[(theta >> 8) + 64]) << 2;
}

/* arcsin of x, -1 <= x <= 1, returns 0xc000..0x4000 (-PI/2..PI/2).
 * basex is shifted by 0x40 so the table is continuous (see trigtab). */
fixang fix_asin(fix x)
{
    uchar basex = ((x >> 2) >> 8) + 0x40;
    uchar fracx = (x >> 2) & 0xff;
    fixang lowy = asintab[basex], hiy = asintab[basex + 1];

    return lowy + (short)((((short)hiy - (short)lowy) * (uchar)fracx) >> 8);
}

/* arccos of x, returns 0x0000..0x8000 (0..PI): acos(x) = PI/2 - asin(x). */
fixang fix_acos(fix x)
{
    uchar basex = ((x >> 2) >> 8) + 0x40;
    uchar fracx = (x >> 2) & 0xff;
    ushort lowy = asintab[basex], hiy = asintab[basex + 1];
    fixang asin_answer = lowy + (short)((((short)hiy - (short)lowy) * (uchar)fracx) >> 8);

    return 0x4000 - asin_answer;
}

/* atan of y/x, in the correct quadrant.  Terra Nova uses fix_safe_pyth_dist here
 * (System Shock's FIX.C uses fix_pyth_dist). */
fixang fix_atan2(fix y, fix x)
{
    fix hyp, s, c;
    fixang th;

    /* Special cases first; this also catches y == x == 0. */
    if (y == 0) {
        if (x >= 0) return 0x0000;
        else return 0x8000;
    } else if (x == 0) {
        if (y >= 0) return 0x4000;
        else return 0xc000;
    }

    if ((hyp = fix_safe_pyth_dist(x, y, "x:\\prj\\tech\\libsrc\\fix\\fix.c", 371)) == 0)
        return 0;

    /* Use asin unless the sine is close to 1 or -1. */
    s = fix_div(y, hyp);
    if ((ulong)s < 0x00004000 || (ulong)s > 0xffffc000) {
        th = fix_asin(s);
        if (x < 0) {
            if (th < 0x4000) th = 0x8000 - th;
            else th = ~th + 0x8000;
        }
    } else {
        c = fix_div(x, hyp);
        th = fix_acos(c);
        if (y < 0)
            th = ~th;
    }
    return th;
}
