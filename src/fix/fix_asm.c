/* fix_asm.c -- C equivalent of x:\prj\tech\libsrc\fix\fix_asm.asm (Terra Nova v1.09)
 *
 * The original module is hand-written 386 assembly; System Shock's FIX_ASM.ASM is the same
 * source (the exe matches it instruction for instruction).  These are exact C models of it,
 * including its quirks, so they can replace the asm in a portable build.
 *
 *   0x2F059C fix_sincos (asm)   0x2F05F5 long_sqrt   0x2F0676 fix_sqrt
 *   0x2F0682 fix_sloppy_sqrt    0x2F06C2 fix24_sqrt  0x2F06CE quad_sqrt
 *   0x360370 byteguess[256]
 */
#include "fix.h"

/* Best 4-bit guess for the square root of a byte (FIX_ASM.ASM data). */
#ifdef TN_LINK_ORIGINAL_DATA
extern uchar byteguess[256];
#else
uchar byteguess[256] = {
    1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15
};
#endif

/* asm-callable sincos: theta in bx, returns sin in eax and cos in ebx.  Unlike fix_sincos()
 * in fix.c, the table difference is taken in 16 bits (sub ax,si). */
void fix_sincos_asm(fixang theta, fix *sin, fix *cos)
{
    uint frac = theta & 0xff;
    uint base = theta >> 8;
    short s = (short)(sintab[base] + (ushort)((int)(short)(sintab[base + 1] - sintab[base]) * (int)frac >> 8));
    short c = (short)(sintab[base + 64] + (ushort)((int)(short)(sintab[base + 65] - sintab[base + 64]) * (int)frac >> 8));
    *sin = (fix)s << 2;
    *cos = (fix)c << 2;
}

/* First guess: byteguess[] of the highest non-zero byte, shifted into place.  Returns the
 * guess, or 0 when x <= 0 (the asm jumps to return_zero). */
static ulong sqrt_guess(long x, int extra_shift)
{
    ulong ux = (ulong)x;
    uchar b3 = ux >> 24, b2 = ux >> 16, b1 = ux >> 8, b0 = ux;
    if (b3 & 0x80) return 0;
    if (b3) return (ulong)byteguess[b3] << (12 + extra_shift);
    if (b2) return (ulong)byteguess[b2] << (8 + extra_shift);
    if (b1) return (ulong)byteguess[b1] << (4 + extra_shift);
    if (b0) return (ulong)byteguess[b0] << extra_shift;
    return 0;
}

/* Newton's method with 16-bit divides (div bx), two blind steps then checked steps. */
ulong long_sqrt(long x)
{
    ulong ux = (ulong)x;
    ulong g = sqrt_guess(x, 0), q, r, old;
    if (g == 0) return 0;
    g = (g + ux / g) >> 1;      /* add bx,ax / rcr bx,1: 17-bit average */
    g = (g + ux / g) >> 1;
    for (;;) {
        q = ux / g;
        r = ux % g;
        if (q == g) return g;
        old = g;
        g = (g + q) >> 1;
        if (q == g || old == g) break;
    }
    /* x = g*(g+1) + r: closer to (g+1)^2 unless r == 0 */
    return r ? g + 1 : g;
}

fix fix_sqrt(fix x)
{
    return (fix)((ulong)(ushort)long_sqrt(x) << 8);
}

/* "Beware, bro', this ONLY does the table lookup" */
fix fix_sloppy_sqrt(fix x)
{
    return (fix)sqrt_guess(x, 8);
}

fix24 fix24_sqrt(fix24 x)
{
    return (fix24)((ulong)(ushort)long_sqrt(x) << 4);
}

/* Square root of the 64-bit value hi:lo (edx:eax), 32-bit Newton steps.
 * Not C-callable in the original (takes edx:eax).  Keeps the original's guess quirk: when the
 * top two bytes of hi are zero it takes the guess byte from lo (byte 1 or byte 0) instead of hi. */
ulong quad_sqrt(long lo, long hi)
{
    unsigned long long x = ((unsigned long long)(ulong)hi << 32) | (ulong)lo;
    ulong uhi = (ulong)hi, ulo = (ulong)lo;
    ulong g, q, r, old;
    uchar b;
    int shift;

    if (uhi == 0 && !(ulo & 0x80000000)) return long_sqrt(lo);
    if (uhi & 0x80000000) return 0;
    if (uhi >> 24) { b = uhi >> 24; shift = 28; }
    else if ((uhi >> 16) & 0xff) { b = uhi >> 16; shift = 24; }
    else if ((uhi >> 8) & 0xff) { b = ulo >> 8; shift = 20; }     /* sic: mov al,ah */
    else { b = ulo; shift = 16; }                                  /* sic: al of lo */
    g = (ulong)byteguess[b] << shift;

    g = (ulong)(((unsigned long long)g + (ulong)(x / g)) >> 1);   /* add ebx,eax / rcr ebx,1 */
    g = (ulong)(((unsigned long long)g + (ulong)(x / g)) >> 1);
    for (;;) {
        q = (ulong)(x / g);
        r = (ulong)(x % g);
        if (q == g) return g;
        old = g;
        g = (ulong)(((unsigned long long)g + q) >> 1);
        if (q == g || old == g) break;
    }
    return r ? g + 1 : g;
}
