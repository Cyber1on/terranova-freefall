/* rnd.c -- LG random number streams (x:\prj\tech\libsrc\rnd\rnd.c, Rex E. Bradford), linked with
 * the game code as module M126 (Terra Nova v1.09).  Decompiled from __FF.EXE; names and
 * structure from the System Shock source (Libraries/RND/Source/RND.C).
 *
 *   0x2DDD70 RndRange          0x2DDD88 RndRangeFix       0x2DDDA4 RndLc16
 *   0x2DDDC4 RndLc16Seed       0x2DDDD0 RndGauss16        0x2DDE8C RndGauss16Fast
 *   0x2DDEC0 RndGauss16FastSeed
 *
 * RndGauss16Seed is the same code as RndLc16Seed (only one copy in the exe; the stream
 * declarations point at 0x2DDDC4).  A stream's generators are called through its function
 * pointers (Watcom convention; the game's streams use RndLc16 / RndLc16Seed).
 */
#include "g1_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
ushort *rnd_gauss_table;
#endif

#define LC16_MULT 2053
#define LC16_ADD 13849
#define NUM_GAUSSBITS 13
#define SIZE_GAUSSTABLE (1 << NUM_GAUSSBITS)
#define MASK_GAUSSTABLE (SIZE_GAUSSTABLE - 1)

#define Rnd(prs) ((prs)->f_Next(prs))     /* WATCOMCALL (regparm) pointer */

/* low + the high 32 bits of Rnd * (high - low + 1) */
long RndRange(RndStream *prs, long low, long high)
{
    ulong r = Rnd(prs);
    return low + (long)(((unsigned long long)r * (ulong)(high - low + 1)) >> 32);
}

fix RndRangeFix(RndStream *prs, fix low, fix high)
{
    long r = (long)(Rnd(prs) >> 16);
    return low + (fix)(((long long)r * (long)(high - low)) >> 16);
}

ulong RndLc16(RndStream *prs)
{
    prs->curr = prs->curr * LC16_MULT + LC16_ADD;   /* only the low 16 bits matter */
    return prs->curr << 16;
}

void RndLc16Seed(RndStream *prs, ulong seed)
{
    prs->curr = seed ^ (seed >> 16);
}

/* 6 lc rnums added and 6 subtracted, / 12: roughly gaussian.  The state becomes the result. */
ulong RndGauss16(RndStream *prs)
{
    long gauss = 0;
    ushort rnum = (ushort)prs->curr;
    int i;

    for (i = 0; i < 6; i++) {
        rnum = rnum * LC16_MULT + LC16_ADD;
        gauss += rnum;
        rnum = rnum * LC16_MULT + LC16_ADD;
        gauss -= rnum;
    }
    gauss /= 12;
    if (gauss < -32768)
        gauss = -32768;
    else if (gauss > 32767)
        gauss = 32767;
    prs->curr = gauss + 32768;
    return prs->curr << 16;
}

ulong RndGauss16Fast(RndStream *prs)
{
    prs->curr = prs->curr * LC16_MULT + LC16_ADD;
    return (ulong)rnd_gauss_table[prs->curr & MASK_GAUSSTABLE] << 16;
}

/* Builds the table (Malloc, not checked) on the first call. */
void RndGauss16FastSeed(RndStream *prs, ulong seed)
{
    int i;
    ushort *pg;

    if (rnd_gauss_table == NULL) {
        pg = rnd_gauss_table = (ushort *)Malloc(SIZE_GAUSSTABLE * sizeof(short));
        for (i = 0; i < SIZE_GAUSSTABLE; i++) {
            prs->curr = i;
            *pg++ = RndGauss16(prs) >> 16;
        }
    }
    prs->curr = seed ^ (seed >> 16);
}
