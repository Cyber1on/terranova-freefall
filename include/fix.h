/* FIX: Looking Glass fixed-point math library (x:\prj\tech\libsrc\fix\), as linked into
 * Terra Nova v1.09.  Decompiled from __FF.EXE; names and structure follow System Shock's
 * FIX.H / FIX.C (Night Dive Mac release) where Terra Nova's code is the same.
 *
 *   fix    16.16 signed fixed point
 *   fix24  24.8 signed fixed point
 *   fixang 0x0000..0xFFFF == 0..2*PI
 */
#ifndef FIX_H
#define FIX_H
#include "lg_types.h"

typedef long fix;
typedef long fix24;
typedef ushort fixang;

#define FIX_UNIT 0x10000L
#define fix_make(a, b) ((((long)(a)) << 16) | (b))
#define fix_int(n) ((n) >> 16)
#define fix_frac(n) ((n) & 0xffff)

/* In the DOS build fix_mul and fix_div are #pragma aux inline sequences:
 *   fix_mul: imul edx / shrd eax,edx,16      fix_div: (a<<16 as 64 bits) idiv b
 * These C versions give bit-identical results (fix_div faults on overflow in the original). */
static inline fix fix_mul(fix a, fix b) { return (fix)(((long long)a * b) >> 16); }
#ifdef TN_WATCOM_ABI
static inline fix fix_div(fix a, fix b) { return (fix)(((long long)a << 16) / b); }
#else
/* The port uses the original's idiv: on overflow (or b == 0) the CPU exception reaches
 * port/core/divtrap.c, which gives the original handler's saturated result, where 64-bit C division
 * would wrap or crash in the C library. */
static inline fix fix_idiv64(long hi, unsigned long lo, long b)  /* edx:eax = hi:lo; idiv b */
{
    long q, r;
    __asm__ volatile ("idivl %4" : "=a"(q), "=d"(r) : "0"(lo), "1"(hi), "rm"(b) : "cc");
    (void)r;
    return q;
}
static inline fix fix_div(fix a, fix b) { return fix_idiv64(a >> 16, (unsigned long)a << 16, b); }
#endif

/* trigtab.c (fix.c data in the original) */
extern ushort sintab[256 + 64 + 1];
extern fixang asintab[128 + 1];
extern ulong expinttab[23];
extern ulong expfractab[17];
#define INTEGER_EXP_OFFSET 11

/* fix_asm.asm (Watcom asm module; register conventions given by #pragma aux) */
ulong long_sqrt(long x);                 /* sqrt of a long, rounded; 0 if x <= 0 */
fix fix_sqrt(fix x);
fix fix_sloppy_sqrt(fix x);              /* first guess only */
fix24 fix24_sqrt(fix24 x);
ulong quad_sqrt(long lo, long hi);       /* sqrt of the 64-bit value hi:lo */
void fix_sincos_asm(fixang theta, fix *sin, fix *cos); /* asm-callable: bx in, eax/ebx out */

/* fix.c */
fix fix_mul_asm_safe(fix a, fix b);      /* fix_mul that records overflow in gOVResult */
extern uchar gOVResult;
fix fix_pyth_dist(fix a, fix b);
fix fix_fast_pyth_dist(fix a, fix b);
fix fix_safe_pyth_dist(fix a, fix b, char *file, int line);
long long_safe_pyth_dist(long a, long b);
void fix_sincos(fixang theta, fix *sin, fix *cos);
fix fix_sin(fixang theta);
fix fix_cos(fixang theta);
void fix_fastsincos(fixang theta, fix *sin, fix *cos);
fix fix_fastsin(fixang theta);
fix fix_fastcos(fixang theta);
fixang fix_asin(fix x);
fixang fix_acos(fix x);
fixang fix_atan2(fix y, fix x);

/* f_exp.c */
fix fix_exp(fix x);

#endif
