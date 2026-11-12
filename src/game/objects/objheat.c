/* objheat.c -- Terra Nova module M055 (source name unknown): a per-object 4-byte state machine
 * and the drain of a suit resource (fuel / heat style counters at p+4..p+0xF) while the object
 * is off the ground.  Decompiled from __FF.EXE v1.09; names from the map (meaning uncertain).
 *
 *   0x288EC0 obj55_288EC0     0x288EF0 obj55_288EF0     0x288F78 obj55_288F78
 *   0x28927C obj55_28927C
 *
 * State p: +0 ushort counter, +2 enabled, +3 state (1 grounded, 2 rising, 3 airborne).
 * Record p (obj55_288F78): +4 ushort rate, +6 ushort drain, +8 ushort level, +0xA ushort max,
 * +0xC long accumulated drain (16.16).
 * Quirks kept:
 *  - obj55_288EF0 returns the caller's value (all 32 bits) or the counter minus 200 computed in
 *    32 bits from the caller's edi high word; callers use the low word (C returns ushort);
 *  - obj55_288F78 takes a 4th argument in ecx (passed on to objacc_28B854), and its inlined
 *    copy of obj55_288EC0 compares the mission number but clears the flag either way.
 */
#include "g2_b.h"
#include "g3_c.h"      /* damage_27D540 (owner g3c) */
#include "g3_a.h"      /* G3a callees (owner g3a) */
#include "g6_c.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
ulong game_442439;
uchar game_35B750;
uchar *game_389E34, *game_389EC4;
#endif

void obj55_288EC0(uchar *p, long v)
{
    if (p == NULL)
        return;
    if (game_359708 == 0x2E)
        p[2] = 0;
    else
        p[2] = (uchar)v;
}

ushort obj55_288EF0(long ent, uchar *p, long v)
{
    ushort t;
    if (p[2] == 0)
        return (ushort)v;
    if (obj55_28927C((short)ent)) {
        switch (p[3]) {
        case 1:
            p[3] = 2;
#ifndef TN_WATCOM_ABI
            U16(p, 0) = U16(p, 0) != 0;     /* (P97: frames on the ground, as before) */
#endif
            /* fall through */
        case 2:
            t = (ushort)(U16(p, 0) + v);
            U16(p, 0) = t;
            if (t <= 200)
                return 0;
            p[3] = 3;
            return (ushort)(t - 200);
        default:
            p[3] = 3;
            return (ushort)v;
        }
    }
    if (p[3] == 1) {
#ifndef TN_WATCOM_ABI
        /* P97: on the ground the jets push every second frame, so at 60 fps they pushed twice
         * before the soldier left the ground, a jump about a quarter higher holding Space
         * (10.6 squares up against 8.5 at 25 fps): a push 40 ms after the first frame on the
         * ground, as on the second frame at 25 fps, never on the first frame (the same pushes as
         * before at 25 fps and slower; 8.1 squares up at 60 fps).  The counter holds 1 + the
         * time since that first frame. */
        ushort was = U16(p, 0);
        t = (ushort)(was ? was + (v > 0 ? v : 1) : 1);
        U16(p, 0) = t;
        if (t < 41)
            return 0;
#else
        t = (ushort)(U16(p, 0) + 1);
        U16(p, 0) = t;
        if (t < 2)
            return 0;
#endif
    }
    physics_2D7788((short)ent);
    U16(p, 0) = 0;
    p[3] = 1;
    return 0;
}

static long fdiv64(long a, long b)
{
    return (long)(((long long)a << 16) / b);
}

void obj55_288F78(long ent, uchar *p, long v, void *x)
{
    uchar pct, flag, amt;
    uchar *pba;
    ushort r, old_level, old_drain;
    long lim;

    pct = damage_27DF14((short)ent, 3);
    flag = 0;
    pba = ENT_PBA(ent) + 6;
    if ((short)v == 0)
        return;
    r = obj55_288EF0((short)ent, p, (ushort)v);
    if (r == 0)
        return;
    if (U16(p, 8) == 0 && game_35B750 == 0 && (short)ent == game_player_entity) {
        if (p != NULL)
            p[2] = 0;               /* inlined obj55_288EC0(p, 0) */
        return;
    }
    if (pct == 0)
        flag = 1;
    else if (pct < 0x3C) {
        long t = (0x3C - pct) / 2;
        if (t > 15)
            t = 15;
        flag = RndRange(&game_rnd_35B704, 0, 100) < t;
    }
    if (!flag) {
        long d = (long)(game_442439 + 0x20000) / 3;
        if (!objacc_28B854(x, fdiv64((long)((ulong)r << 16), d), 1, 1))
            return;
    }
    if (flag || p[2] == 0) {
        U16(p, 6) = 0;
        return;
    }
    sfx_28CBD0((short)ent, fdiv64((long)game_frame_dt, 1000), 15);
    old_level = U16(p, 8);
    old_drain = U16(p, 6);
    U16(p, 6) = (ushort)((long)U16(p, 4) / 15);
    amt = (uchar)(((long long)(long)((ulong)U16(p, 6) << 9) * (long)r) >> 16);
    if (S32(pba, 7) == 4)
        amt = amt / 2;
    if (game_359708 != 0x2F) {
        long l = U16(p, 8);
        U16(p, 8) = (ushort)(l < amt ? 0 : l - amt);
    }
    if ((short)ent == game_player_entity) {
        lim = (long)U16(p, 0xA) / 5;
        if ((long)U16(p, 8) < lim) {
            if (U16(p, 8) == 0)
                RadioAck(0x51);
            else if ((long)old_level >= lim)
                RadioAck(0x50);
        }
    }
    if (amt > 0)
        (game_camera[GAME_CAM_DA] ? game_389EC4 : game_389E34)[0x20] |= 0x20;
    if (pct < 0x3C) {
        long m = pct + 15;
        if (m < 0x1C)
            m = 0x1C;
        U16(p, 6) = (ushort)((long)U16(p, 6) * m / 75);
    }
    if (U16(p, 6) < 1)
        U16(p, 6) = 0;
    else {
        ulong *t = damage_27D540((short)ent);
        if (t)
            *t = game_time + 10000;
        S32(p, 0xC) += (long)U16(p, 6) << 16;
    }
    if ((short)ent == game_player_entity && old_drain == 0 && U16(p, 6) != 0)
        SndPlay(0x794, 0, 0x28, 1, -1, 0x40, 0);
}

/* TRUE when the object is more than 0.25 above or below the ground height at its position */
uchar obj55_28927C(long ent)
{
    GameEntity *o = ENT(ent);
    long d = o->z - aircraft_284A6C((short)ent, o->x, o->y);
    if (d < 0)
        d = -d;
    return d > 0x4000;
}
