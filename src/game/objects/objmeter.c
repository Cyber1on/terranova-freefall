/* objmeter.c -- Terra Nova object meters (module M059; source name unknown): hit points /
 * energy of an object as {cur, max, second max, regeneration, ...}.  Decompiled from
 * __FF.EXE v1.09; names are the map's placeholders.
 *
 *   0x28B850 objacc_28B850  byte at +0xC (low byte of the regeneration rate)
 *   0x28B854 objacc_28B854  take up to `amount` (or exactly, or nothing)
 *   0x28B870 objacc_28B870  cur
 *   0x28B874 objacc_28B874  max
 *   0x28B878 objacc_28B878  cur += n, capped at max
 *   0x28B88C objacc_28B88C  cur += units * (rate >> 10), capped at max
 *   0x28B8B0 objacc_28B8B0  condition in percent: mean of max / max0 and rate / rate0
 *   0x28B914 objacc_28B914  initialise (6 args)
 *   0x28B938 objacc_28B938  random damage effect: lose cur, max or rate (1, 2, 3; 0 none)
 */
#include "g2_c.h"

uchar objacc_28B850(ObjMeter *m)
{
    return (uchar)m->f0c;
}

/* signature as in g2_b.h; only the low bytes of exact / take are tested */
long objacc_28B854(void *x, fix amount, long exact, long take)
{
    ObjMeter *m = x;

    if (amount > m->cur) {
        if ((uchar)exact)
            return 0;
        amount = m->cur;
    }
    if ((uchar)take)
        m->cur -= amount;
    return amount;
}

fix objacc_28B870(ObjMeter *m)
{
    return m->cur;
}

fix objacc_28B874(ObjMeter *m)
{
    return m->max;
}

void objacc_28B878(ObjMeter *m, fix add)
{
    fix v = add + m->cur;
    if (v >= m->max)
        v = m->max;
    m->cur = v;
}

void objacc_28B88C(ObjMeter *m, long units)
{
    fix v = units * (m->f0c >> 10) + m->cur;
    if (v >= m->max)
        v = m->max;
    m->cur = v;
}

long objacc_28B8B0(ObjMeter *m)
{
    short a = (short)((m->max >> 16) * 100 / (m->f08 >> 16));
    short b = (short)((m->f0c >> 16) * 100 / (m->f10 >> 16));
    return ((long)a + b) / 2;
}

void objacc_28B914(ObjMeter *m, fix max, fix cur, fix f0c, fix f10, long f14)
{
    m->f15 = 100;
    m->cur = cur;
    m->f08 = max;
    m->max = max;
    m->f0c = f0c;
    m->f10 = f10;
    m->f14 = (uchar)f14;
}

uchar objacc_28B938(void *x, long damage)   /* signature as in g2_b2.h */
{
    ObjMeter *m = x;
    ulong r = game_rnd_35B704.f_Next(&game_rnd_35B704);
    fix d, f, v;

    if (damage < 10)
        return 0;
    d = damage << 16;
    if (r & 0x80000000) {
        v = G2C_FIXMUL(m->cur, 0x10000 - (d >> 6));
        m->cur = v > 0 ? v : 0;
        return 1;
    }
    f = 0x10000 - (d >> 9);
    if (r & 0x40000000) {
        v = G2C_FIXMUL(m->max, f);
        m->max = v > 0 ? v : 0;
        return 2;
    }
    if (r & 0x20000000) {
        v = G2C_FIXMUL(m->f0c, f);
        m->f0c = v > 0 ? v : 0;
        return 3;
    }
    return 0;
}
