/* misobjrg.c -- mission regions and time limit (M104, 0x2C9D50-0x2C9FD3; names are the map's),
 * Terra Nova v1.09.  Decompiled from __FF.EXE.  (Split from misobj.c while g4_b.h declared
 * misobj_2C9E60 as uchar; the original returns a full long: -1 = no free region, and the callers
 * keep eax.  Since the G9/G10 merge every declaration is in g9_c.h, with the long return.)
 *
 *   0x2C9D50 misobj_2C9D50  point (x, y) in region a: kind 0 circle of diameter w, kind 1
 *                           rectangle w x h around the centre, other kinds never
 *   0x2C9DF8 misobj_2C9DF8  *mask = bits of the regions 0..31 enabled in game_442420 that hold
 *                           (x, y); 1 if any
 *   0x2C9E60 misobj_2C9E60  new circle region (diameter 3) at square (x, y): index, -1 when full
 *   0x2C9EE8 misobj_2C9EE8  free a region (centre 0, 0)
 *   0x2C9EFC misobj_2C9EFC  mission time limits from resmiss.res: game_442418 (res 0x6A4) and
 *                           game_44241C = h * 3600 + m * 60 + s (res 0x6A5)
 *
 * Region centre i: game_4413D0 + 8 i {fix x, y}; region shape i: game_4417D0 + 10 i
 * {ushort kind; fix w, h}.
 * Quirks kept: misobj_2C9E60 treats a slot as free when either coordinate is 0; the differences
 * are compared with 32-bit wrap (sub / test), not with a signed compare; misobj_2C9EFC uses
 * mission 1 for mission numbers outside 1..0x2F.
 */
#include "g9_c.h"       /* misobj_2C9*; g9_a.h game_442418 / 44241C; g2_c.h game_4413D0,
                           game_4417D0; g4_c2.h game_442420; g1_b.h game_359708 */

#define RG_X(i)    (*(fix *)(game_4413D0 + (i) * 8))
#define RG_Y(i)    (*(fix *)(game_4413D0 + (i) * 8 + 4))
#define RG_KIND(i) (*(ushort *)(game_4417D0 + (i) * 10))
#define RG_W(i)    (*(fix *)(game_4417D0 + (i) * 10 + 2))
#define RG_H(i)    (*(fix *)(game_4417D0 + (i) * 10 + 6))

uchar misobj_2C9D50(long a, fix x, fix y)
{
    fix dx = x - RG_X(a), dy = y - RG_Y(a);

    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    if (RG_KIND(a) == 0)
        return (long)((ulong)fix_fast_pyth_dist(dx, dy) - (ulong)(RG_W(a) / 2)) <= 0;
    if (RG_KIND(a) == 1)
        return (long)((ulong)dx - (ulong)(RG_W(a) / 2)) <= 0
            && (long)((ulong)dy - (ulong)(RG_H(a) / 2)) <= 0;
    return 0;
}

uchar misobj_2C9DF8(ulong *mask, fix x, fix y)
{
    uchar found = 0;
    long i;

    *mask = 0;
    for (i = 0; i < 0x20; i++)
        if (((ulong)1 << i) & game_442420 && misobj_2C9D50(i, x, y)) {
            *mask |= (ulong)1 << i;
            found = 1;
        }
    return found;
}

long misobj_2C9E60(long x, long y)
{
    long i;

    for (i = 0; i < 0x80; i++)
        if (!RG_X(i) || !RG_Y(i)) {
            RG_X(i) = (x << 16) | 0x8000;
            RG_Y(i) = (y << 16) | 0x8000;
            RG_KIND(i) = 0;
            RG_W(i) = 0x30000;
            RG_H(i) = 0x30000;
            return i;
        }
    return -1;
}

void misobj_2C9EE8(long region)
{
    RG_X(region) = 0;
    RG_Y(region) = 0;
}

void misobj_2C9EFC(void)
{
    schar m;
    int f;
    uchar *p;
    schar *t;

    if (game_359708 > 0x2F || game_359708 < 1)
        m = 1;
    else
        m = (schar)game_359708;
    f = ResOpenResFile("resmiss.res", 0, 0);
    p = ResLock(0x6A4);
    game_442418 = *(long *)(p + *(long *)(p + 2) + m * 4);
    ResUnlock(0x6A4);
    p = ResLock(0x6A5);
    t = (schar *)(p + *(long *)(p + 2) + m * 3);
    game_44241C = t[0] * 3600;
    game_44241C += t[1] * 60;
    game_44241C += t[2];
    ResUnlock(0x6A5);
    ResCloseFile(f);
}
