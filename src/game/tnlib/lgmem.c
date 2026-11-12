/* lgmem.c -- hand-written asm memory helpers between DSTRUCT and the CRT (Terra Nova v1.09;
 * names ours).  Rewritten in C from __FF.EXE.
 *
 *   0x2F0E80 lg_memset    0x2F0EB7 lg_memset32    0x2F0F23 lg_memswap    0x2F0F49 lg_qsort
 *
 * Register use of the asm: lg_memset / lg_memset32 take eax = dest, edx = pattern, ecx =
 * count (ebx is not used: the C prototypes have an unused third argument so the standard
 * Watcom thunk passes ecx as the count); both return eax = dest advanced past the dword part.
 * lg_memswap takes esi, edi, ecx (custom, verified directly and through lg_qsort).  lg_qsort is
 * Watcom-convention (base, num, width, cmp) and calls cmp(a, b) with eax, edx.
 * The asm also does dummy reads ahead of the destination (cache warm-up); not reproduced.
 */
#include "t1_c.h"

#define NOLOOPCALL __attribute__((optimize("no-tree-loop-distribute-patterns")))

/* Fills n dwords; first aligns dest to 32 bytes one dword at a time (no effect on the result). */
NOLOOPCALL ulong *lg_memset32(ulong *dst, ulong val, int unused, ulong n)
{
    while (n != 0) {
        *dst++ = val;
        n--;
    }
    return dst;
}

/* Fills n bytes with a dword pattern kept aligned to the address: leading bytes take the
 * pattern's low byte and rotate it, the middle is filled with lg_memset32, and the 1-3 trailing
 * bytes get pattern bytes 0, 1, 0 (quirk: the third byte repeats byte 0).  Returns dest
 * advanced past the leading and dword parts (not the trailing bytes). */
NOLOOPCALL void *lg_memset(void *dst, ulong pattern, int unused, ulong n)
{
    uchar *p = dst;

    if (n == 0)
        return p;
    while ((ulong)p & 3) {
        *p++ = (uchar)pattern;
        pattern = (pattern >> 8) | (pattern << 24);
        if (--n == 0)
            return p;
    }
    p = (uchar *)lg_memset32((ulong *)p, pattern, 0, n >> 2);
    n &= 3;
    if (n != 0) {
        p[0] = (uchar)pattern;
        if (--n != 0) {
            p[1] = (uchar)(pattern >> 8);
            if (--n != 0)
                p[2] = (uchar)pattern;
        }
    }
    return p;
}

/* Swaps two n-byte areas (dwords first, then the remaining bytes). */
NOLOOPCALL void lg_memswap(void *a, void *b, ulong n)
{
    ulong *pa = a, *pb = b, t, k;
    uchar *ca, *cb, c;

    for (k = n >> 2; k != 0; k--) {
        t = *pb; *pb++ = *pa; *pa++ = t;
    }
    ca = (uchar *)pa; cb = (uchar *)pb;
    for (k = n & 3; k != 0; k--) {
        c = *cb; *cb++ = *ca; *ca++ = c;
    }
}

/* Quicksort with an explicit stack (32 entries; the smaller part is sorted first).  The middle
 * element is swapped to the front as the pivot; elements <= pivot are gathered after it. */
void lg_qsort(void *base_, ulong num, ulong width, void *cmp)
{
    uchar *base = base_;
    uchar *stk_base[32];
    ulong stk_num[32];
    long sp = 0;
    uchar *p, *last, *right;
    ulong lt, eq, i;
    long r;

    for (;;) {
        while (num > 1) {
            p = base + width;
            if (num == 2) {
                if (WATCOM_CALL(cmp, (long)base, (long)p) > 0)
                    lg_memswap(base, p, width);
                break;
            }
            lg_memswap(base, base + (num >> 1) * width, width);
            last = base;
            lt = 0;
            eq = 0;
            for (i = 1; i < num; i++, p += width) {
                r = WATCOM_CALL(cmp, (long)p, (long)base);
                if (r <= 0) {
                    last += width;
                    lt++;
                    if (i != lt)
                        lg_memswap(p, last, width);
                }
                if (r != 0)
                    eq = lt;
            }
            if (num - 1 == lt && eq == 0)
                break;                      /* everything equal to the pivot */
            if (lt != 0)
                lg_memswap(base, last, width);
            num = num - lt - 1;             /* elements after the pivot */
            if (lt != eq)
                lt = eq + 1;
            right = last + width;
            if (lt < num) {
                stk_base[sp] = right;
                stk_num[sp] = num;
                num = lt;
            } else {
                stk_base[sp] = base;
                stk_num[sp] = lt;
                base = right;
            }
            sp++;
        }
        if (sp == 0)
            return;
        sp--;
        base = stk_base[sp];
        num = stk_num[sp];
    }
}
