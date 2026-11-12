/* objclass.c -- Terra Nova per-class object dispatch (module M067; source name unknown):
 * aiming / turret angle limits and calls into the class simulators (PBA 1, vehicle 2,
 * aircraft 3, installation 4).  Decompiled from __FF.EXE v1.09; names are the map's.
 *
 *   0x2933C0 objtype_2933C0  angle step of a limit for this frame: rate * dt / 256
 *   0x2933E0 objtype_2933E0  0x8000 * dt / 256
 *   0x2933FC objtype_2933FC  PBA: clear the aim words, set the two aim values
 *   0x293464 objtype_293464  return the aim to rest by one frame's step
 *   0x293540 objtype_293540  aim direction of an object (class simulator) into out[3]
 *   0x2935E0 objtype_2935E0  turn the weapon toward its target; TRUE when aligned and ready
 *   0x293834 objtype_293834  class simulator query (slot, a, b)
 *   0x2938B8 objtype_2938B8  move a fix toward a target by at most 0x8000 * dt / 256
 *   0x2938F0 objtype_2938F0  move an angle toward a target within [lo, hi] at the limit's rate
 *   0x293A5C objtype_293A5C  two angles at once (6 args)
 *   0x293AA0 objtype_293AA0  class simulator query
 *
 * Angle limits (objtype_limits, 0x35BF84): {lo, hi, rate}; lo == hi means a full circle.
 * Quirks kept: objtype_2938F0 moves the angle by at most one step and returns FALSE when it
 * had to clamp the step or the target to the limits; both angles are always moved even when
 * the first did not reach its target.
 */
#include "g2_c.h"
#include "g3_c.h"      /* pbasim_28A878 (owner g3c) */
#include "g3_a.h"      /* G3a callees (owner g3a) */

#ifndef TN_LINK_ORIGINAL_DATA
AngleLimit objtype_limits[8];
#endif

#define FRAME_STEP(r) ((long)((long long)(long)(r) * (long)game_frame_dt / 0x100))

long objtype_2933C0(AngleLimit *lim)
{
    return FRAME_STEP(lim->rate);
}

long objtype_2933E0(void)
{
    return FRAME_STEP(0x8000);
}

void objtype_2933FC(long obj, long val)
{
    GameEntity *e = &G2C_ENT(obj);

    if (e->cls == 1) {
        uchar *p = (uchar *)&game_pba[e->slot];
        *(short *)(p + 0x3F2) = 0;
        *(short *)(p + 0x3F4) = 0;
        *(short *)(p + 0x3F6) = 0;
        *(short *)(p + 0x3F8) = 0;
        *(long *)(p + 0x3FA) = val;
        *(long *)(p + 0x3FE) = val;
    }
}

/* clamp v to [-step, step] */
static long clamp_step(long v, long step)
{
    if (v > step)
        return step;
    if (v < -step)
        return -step;
    return v;
}

void objtype_293464(long obj)
{
    short o = (short)obj;
    void *loc = NULL;

    switch (G2C_ENT(o).cls) {
    case 1: {
        fix *p = pbasim_28A66C(o);
        p[0] += clamp_step(-p[0], objtype_2933E0());
        p[1] += clamp_step(-p[1], objtype_2933E0());
        break;
    }
    case 2:
        vehsim_28ECC4(&loc, NULL, NULL, o, NULL);
        *(short *)loc += (short)objtype_2933C0(&objtype_limits[6]);    /* 0x35BFA8 */
        break;
    case 4:
        instsim_288C64(&loc, NULL, NULL, o, NULL);
        if (G2C_ENT(o).type != 0x47) {
            short d = (short)objtype_2933C0(&objtype_limits[7]);       /* 0x35BFAE */
            *(short *)loc += d;
        }
        break;
    }
}

long objtype_293540(fix *out, long obj, long arg)
{
    GameEntity *e = &G2C_ENT(obj);

    out[2] = 0;
    out[1] = out[2];
    out[0] = out[2];
    switch (e->cls) {
    case 2:
        return vehsim_28EF14(e->slot, 0, 0, 0, (short)arg, out);
    case 1:
        return pbasim_28A79C(e->slot, 0, 0, 0, (short)arg, out);
    case 4:
        return instsim_288D10(e->slot, 0, 0, 0, (short)arg, out);
    }
    out[0] = 0x10000;
    return (long)out;
}

uchar objtype_2935E0(long obj, long arg)
{
    short o = (short)obj, a = (short)arg;
    fix dir[3] = {0, 0, 0};
    ushort *ang = NULL;
    long t1 = 0, t2 = 0;
    uchar res = 0, r1, r2;
    AngleLimit *l1, *l2;

    switch (G2C_ENT(o).cls) {
    case 4:
        res = instsim_2888A4(o);
        objtype_293540(dir, o, a);
        instsim_288C64(&ang, &t1, &t2, o, dir);
        l1 = &objtype_limits[4];          /* 0x35BF9C */
        l2 = &objtype_limits[5];          /* 0x35BFA2 */
        break;
    case 1: {
        fix *p = pbasim_28A66C(o);
        long q = pbasim_28A698(o);
        fix *pq = &p[q], *pn;
        *pq += clamp_step(0x10000 - *pq, objtype_2933E0());
        pn = &p[q == 0];
        *pn += clamp_step(-*pn, objtype_2933E0());
        res = pbasim_28A534(o);
        objtype_293540(dir, o, a);
        pbasim_28A5A0(&ang, &t1, &t2, o, dir);
        *pbasim_28A66C(o) = 0x10000;
        l1 = &objtype_limits[0];          /* 0x35BF84 */
        l2 = &objtype_limits[1];          /* 0x35BF8A */
        break;
    }
    case 2:
        res = vehsim_28EC5C(o);
        objtype_293540(dir, o, a);
        vehsim_28ECC4((void **)&ang, (short *)&t1, (short *)&t2, o, dir);
        l1 = &objtype_limits[2];          /* 0x35BF90 */
        l2 = &objtype_limits[3];          /* 0x35BF96 */
        break;
    default:
        return res;
    }
    r1 = objtype_2938F0(ang, (ushort)t1, l1);
    r2 = objtype_2938F0(ang + 1, (ushort)t2, l2);
    return (r1 && r2) ? res : 0;
}

uchar objtype_293834(long obj, long a, long b)
{
    GameEntity *e = &G2C_ENT(obj);
    short slot = e->slot;

    switch (e->cls) {
    case 1:
        return (uchar)pbasim_28A878(slot, 0, 0, 0, (short)a, (uchar)b, 0);
    case 2:
        return (uchar)vehsim_28EDC0(slot, 0, 0, 0, (short)a, (uchar)b);
    case 4:
        return (uchar)instsim_288904(slot, 0, 0, 0, (short)a, (uchar)b);
    }
    return 0;
}

void objtype_2938B8(fix *p, fix target)
{
    long step = FRAME_STEP(0x8000);
    *p += clamp_step(target - *p, step);
}

uchar objtype_2938F0(ushort *p, ulong target, AngleLimit *lim)
{
    uchar res = 1;
    long step, lo, hi, cur, t, d;

    if ((ushort)target == *p)
        return 1;
    step = FRAME_STEP(lim->rate);
    if (lim->lo == lim->hi) {
        /* no limits: the shorter way round */
        d = ((ushort)target - *p) & 0xFFFF;
        if (d >= 0x8000) {
            d = (*p - (ushort)target) & 0xFFFF;
            if (d > step) {
                res = 0;
                d = step;
            }
            *p = (ushort)(*p - d);
        } else {
            if (d > step) {
                res = 0;
                d = step;
            }
            *p = (ushort)(*p + d);
        }
        return res;
    }
    lo = lim->lo;
    hi = lim->hi;
    cur = *p;
    t = (ushort)target;
    if (hi < lo) {                       /* range through 0: unwrap */
        hi += 0x10000;
        if (cur < lo)
            cur += 0x10000;
        if (t < lo)
            t += 0x10000;
    }
    if (t < lo) {                        /* outside: go to the nearer limit */
        t = (lo - t > t + 0x10000 - hi) ? hi : lo;
        res = 0;
    } else if (t > hi) {
        t = (t - hi > lo - (t - 0x10000)) ? lo : hi;
        res = 0;
    }
    d = t - cur;
    if (d > step) {
        res = 0;
        d = step;
    } else if (-d > step) {
        d = -step;
        res = 0;
    }
    *p = (ushort)(cur + d);
    return res;
}

uchar objtype_293A5C(ushort *p, ulong a, ulong b, AngleLimit *l1, AngleLimit *l2, long unused)
{
    uchar r1 = objtype_2938F0(p, a & 0xFFFF, l1);
    uchar r2 = objtype_2938F0(p + 1, (ushort)b, l2);
    (void)unused;
    return r1 && r2;
}

long objtype_293AA0(long obj)
{
    short o = (short)obj;

    switch (G2C_ENT(o).cls) {
    case 2:
        return (long)vehsim_28ED68(o);
    case 3:
        return aircraft_286010(o);
    case 4:
        return instsim_288A4C(o);
    }
    return 0;
}
