/* ailowb.c -- Terra Nova low-level AI, second part (module M007 0x25129C-0x253810,
 * r:\prj\tn\v109\tn\src\tn\ailow.c).  Decompiled from __FF.EXE v1.09.
 * The first part of the module (0x24ADE0-0x25129C) is g4a's ailowa.c.
 *
 * Brains (0x14E-byte records, game_brains) and squads (0x170, game_squads) are reached through
 * byte offsets (layout notes in include/g4_b.h).  A "point" is a map square {short x, y} kept
 * in one dword; SQC(v) is the fix centre of square v ((v << 16) | 0x8000).
 *
 *   0x25129C ailow_25129C  sample ailow_250E58 (fresh every 500 ms) -> value / 2, or 4
 *   0x2512F8 ailow_2512F8  set the brain's move goal (squad leader distance, region checks)
 *   0x25142C ailow_25142C  pick the nearest brain of a squad as the target
 *   0x251594 ailow_251594  random square within r of a point; TRUE if its terrain is passable
 *   0x251668 ailow_251668  targeting: keep / drop / pick a target, report it to the squad,
 *                          choose a move when the target is lost
 *   0x251C98 ailow_251C98  fight: line of fire, firing, closing in / dodging per squad order
 *   0x252758 ailow_252758  fire at a target; tell enemies in front of the shooter
 *   0x252C5C ailow_252C5C  line-of-fire test from a brain to a target entity
 *   0x252E4C ailow_252E4C  objtype_293834 for the brain's entity (unused)
 *   0x252E60 ailow_252E60  turn the current order into a move state
 *   0x252F78 ailow_252F78  run the move state
 *   0x253008 ailow_253008  take a brain out of its squad
 *   0x2531B0 ailow_2531B0  put a brain into a squad
 *   0x253334 ailow_253334  give a brain a squad of its own (copying the old squad's orders)
 *   0x25344C ailow_25344C  ailow_253334 for the player's brain (unused)
 *   0x2534A4 ailow_2534A4  move a brain to another squad
 *   0x2534FC ailow_2534FC  free a brain
 *   0x253580 ailow_253580  movement kind of a brain (1 walk, 2 drive, 3 ...)
 *   0x253608 ailow_253608  set the +0x135 flag for secs seconds (unused)
 *   0x253640 ailow_253640  note that the brain was hit (by whom, where)
 *   0x2537AC ailow_2537AC  forget a target entity
 *
 * Quirks kept:
 *  - ailow_251594 divides by 2 * r (r = 0 faults) and shifts 1 by the terrain kind (x86: mod 32);
 *  - the target entity is kept in one byte (+0xCA): entity ids above 255 wrap;
 *  - ailow_253008 loops forever when the brain is not on its squad's list;
 *  - ailow_2512F8 overwrites its own stack arguments (x, y) with the leader's position.
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_b.h"
#include "g9_c.h"     /* added at the G9/G10 merge */

#define U8(p, o)  (*(uchar *)((uchar *)(p) + (o)))
#define S8(p, o)  (*(signed char *)((uchar *)(p) + (o)))
#define U16(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define S16(p, o) (*(short *)((uchar *)(p) + (o)))
#define U32(p, o) (*(ulong *)((uchar *)(p) + (o)))
#define S32(p, o) (*(long *)((uchar *)(p) + (o)))
#define BRAIN(i)  ((uchar *)&game_brains[(uchar)(i)])
/* the brain's path buffer.  Some brains have none (class 4, or when the pool ran out: NULL);
 * where the original read or wrote through it without checking, DOS low memory took it
 * (TN_LOWMEM; docs/BUGS.md G9) */
#define BRPATH(br) ((uchar *)TN_LOWMEM(*(uchar **)((uchar *)(br) + 0xAD)))
#define SQUAD(i)  ((uchar *)&game_squads[(uchar)(i)])
#define EN(i)    (game_entities[(short)(i)])
#define BENT(b)   S16(b, 0xB)
#define SQC(w)    ((long)(((ulong)(long)(short)(w) << 16) | 0x8000))
#define FIXINT(f) ((long)(f) >> 16)
#define RND()     RndRange(game_rnd_text, 0, 0x7FFF)


/* ailow_24E19C (g4a) returns a fix x/y pair through a buffer passed in esi (Watcom struct
 * return; esi is not preserved). */
static void ailow_24E19C_call(fix *out, uchar *b)
{
#ifdef TN_WATCOM_ABI
    void *s = out;
    __asm__ volatile("call *%[f]"
                     : "+a"(b), "+S"(s)
                     : [f] "r"(0x24E19C)
                     : "memory", "cc", "ecx", "edx");
#else
    ailow_24E19C(b, out);
#endif
}

static long iabs(long v) { return v < 0 ? -v : v; }

/* forward dword copy (rep movsd) */
static void copy_dw(void *dst, const void *src, int n)
{
    ulong *d = dst;
    const ulong *s = src;
    while (n--) *d++ = *s++;
}

/* the atan2 of a fix (or integer) vector, scaled down by 128 when a component is above
 * 0x800000 (as the callers do before fix_atan2) */
static fixang atan2_scaled(long dx, long dy)
{
    if (iabs(dx) > 0x800000 || iabs(dy) > 0x800000) {
        dx >>= 7;
        dy >>= 7;
    }
    return fix_atan2(dy, dx);
}

long ailow_25129C(uchar *b)
{
    ulong t = game_time;
    short r;

    if ((long)(t - U32(b, 0xFE)) < 500)
        r = ailow_250E58(b, 0);
    else {
        r = ailow_250E58(b, 1);
        U32(b, 0xFE) = t;
    }
    if (r == 0)
        return 4;
    return (long)(((long long)(ushort)r * 0x8000) >> 16);
}

uchar ailow_2512F8(uchar *b, long u1, long u2, long u3, fix x, fix y)
{
    uchar *sq = SQUAD(b[6]);
    uchar r;

    if (sq[0xAF] && U16(sq, 0xA9) && b[0x9C] == 1) {
        short e = S16(sq, 0xA9);
        long dx = (short)FIXINT(EN(e).x) - FIXINT(x);
        long dy = (short)FIXINT(EN(e).y) - FIXINT(y);

        if (fix_fast_pyth_dist(dx, dy) >= 10) {
            x = EN(e).x;
            y = EN(e).y;
        }
    }
    sq = SQUAD(b[6]);
    if (sq[0x120])
        r = misobj_2C9D50(sq[0x121], x, y);
    else
        r = 1;
    if (r) {
        U16(b, 0x9D) = (ushort)FIXINT(x);
        U16(b, 0x9F) = (ushort)FIXINT(y);
        S32(b, 0xA5) = x;
        S32(b, 0xA9) = y;
        return 1;
    }
    return r;
}

uchar ailow_25142C(uchar *b, long squad)
{
    uchar first = SQUAD((uchar)squad)[0xF];
    uchar best, c;
    long bx, by, d;
    short ent;
    uchar *t;

    if (!first)
        return 0;
    best = first;
    bx = FIXINT(S32(b, 0xC));
    by = FIXINT(S32(b, 0xE));
    {
        long ex = FIXINT(S32(BRAIN(first), 0xC)) - bx;
        long ey = FIXINT(S32(BRAIN(first), 0xE)) - by;
        d = ex * ex + ey * ey;
    }
    for (c = BRAIN(first)[7]; c; c = BRAIN(c)[7]) {
        long ex = FIXINT(S32(BRAIN(c), 0xC)) - bx;
        long ey = FIXINT(S32(BRAIN(c), 0xE)) - by;
        long d2 = ey * ey + ex * ex;
        if (d2 < d) {
            d = d2;
            best = c;
        }
    }
    t = BRAIN(best);
    ent = BENT(t);
    if (!ailow_250674(b, ent))
        return 0;
    b[0xC3] = 1;
    b[0xC9] = best;
    b[0xCA] = t[0xB];
    b[0xCB] = BRAIN(b[0xC9])[6];
    U16(b, 0xCC) = (ushort)FIXINT(EN(b[0xCA]).x);
    U16(b, 0xCE) = (ushort)FIXINT(EN(b[0xCA]).y);
    return 1;
}

uchar ailow_251594(ulong pt, long r, ushort *out)
{
    long rr = (short)r;
    long m = rr * 2;
    long v;
    uchar kind;
    uchar *row;

    v = RND();
    out[0] = (ushort)(v % m + (short)pt - rr);
    v = RND();
    out[1] = (ushort)(v % m + FIXINT(pt) - rr);
    row = *(uchar **)(game_map_35BFB4 + 0x84E + (long)(short)out[1] * 4);
    kind = game_41DD58[row[(long)(short)out[0] * 3] & 0x3F];
    return ((1UL << (kind & 31)) & terrain_bitmask) == 0;
}

void ailow_251668(uchar *b, long flag, long ent)
{
    ushort old = b[0xCA];
    uchar *sq;

    if (b[0x9C] == 0 && b[5] == 2) {
        long r = RND();
        S32(b, 0xF1) = game_time + 500 + (r * 2) % 1000;
    }
    if (b[0xC3] && b[0xC9]) {
        uchar *t = BRAIN(b[0xC9]);
        if (t[0] == 0 && t[0xA] == 0) {
            U16(b, 0xCC) = 0xFFFF;
            U16(b, 0xCE) = 0xFFFF;
            b[0xC3] = 0;
            b[0xC9] = 0;
            b[0xCA] = 0;
            b[0xCB] = 0;
        }
    }
    sq = SQUAD(b[6]);
    if (sq[0xC2] && sq[0xC1]) {
        if (U16(sq, 0xBF) != 0 && (ushort)b[0xCA] != U16(sq, 0xBF)) {
            b[0xC9] = EN(S16(sq, 0xBF)).f21;
            b[0xCA] = SQUAD(b[6])[0xBF];
            b[0xC3] = 1;
            b[0xCB] = BRAIN(b[0xC9])[6];
            U32(b, 0xCC) = U32(b, 0xE);
        }
    } else if ((short)ent != 0) {
        if (!b[0xC3]) {
            b[0xC9] = EN((short)ent).f21;
            b[0xCA] = (uchar)ent;
            b[0xCB] = BRAIN(b[0xC9])[6];
            b[0xC3] = 1;
            U32(b, 0xCC) = U32(b, 0xE);
        }
    } else if (b[0xC3] == 0 && (uchar)flag) {
        if (game_time - U32(b, 0x106) > 1500) {
            U32(b, 0x106) = game_time;
            if (ailow_25142C(b, (uchar)flag))
                U32(b, 0xC5) = game_time;
        }
    } else if (b[0xC3]) {
        if (game_time - U32(b, 0x10E) > 1000) {
            U32(b, 0x10E) = game_time;
            if (ailow_250A1C(b)) {
                U16(b, 0xCC) = (ushort)FIXINT(EN(b[0xCA]).x);
                U16(b, 0xCE) = (ushort)FIXINT(EN(b[0xCA]).y);
                U32(b, 0xC5) = game_time;
            }
        }
    }

    if (b[0xC3] && (ushort)b[0xCA] != old)
        U32(b, 0xD7) = game_time;
    if (game_time - U32(b, 0x106) > 1500 && RND() % 256 <= 0x80) {
        if (b[0xC3] && b[0xCA]) {
            b[0xC4] = ailow_2508D0(BENT(b), b[0xCA]);
            U32(b, 0x106) = game_time;
        } else
            b[0xC4] = 0;
        if (b[0xC4])
            U32(b, 0xC5) = game_time;
    }
    {
        Msg *m = MsgAlloc();
        if (m) {
            aiacc_25AE18(m, U32(b, 1), U32(SQUAD(b[6]), 3), b[3], b[0xC4]);
            MsgSend(m);
        }
    }

    if (game_time - U32(b, 0xC5) > 15000 && SQUAD(b[6])[0xCB] != 7) {
        uchar lost = 0;

        if (b[0xC3]) {
            switch (RND() % 7) {
            case 0:
                if (b[5] == 1) {
                    fix p[2];
                    ailow_24E19C_call(p, b);
                    if (ailow_24FC04(b, 0, 0, 0, p[0], p[1], 3))
                        lost = 1;
                } else {
                    ulong pt = U32(b, 0x12);
                    if (ailow_24FC04(b, 0, 0, 0, SQC(pt), SQC(pt >> 16), 3))
                        lost = 1;
                }
                break;
            case 1:
            case 2:
                U16(b, 0xCC) = (ushort)FIXINT(EN(b[0xCA]).x);
                U16(b, 0xCE) = (ushort)FIXINT(EN(b[0xCA]).y);
                U32(b, 0xC5) = game_time;
                break;
            case 3:
                physics_2D467C(BENT(b));
                break;
            default: {
                ushort np[2];
                if (ailow_251594(U32(b, 0xCC), 10, np)) {
                    U32(b, 0xCC) = *(ulong *)np;
                    U32(b, 0xC5) = game_time;
                }
                break;
            }
            }
        }
        if (b[0xC3] && lost) {
            b[0xC3] = 0;
            b[0xC9] = 0;
            b[0xCA] = 0;
            b[0xCB] = 0;
            U16(b, 0xCC) = 0xFFFF;
            U16(b, 0xCE) = 0xFFFF;
        }
    }

    if (b[0xC3] == 0 && b[0xD0] == 0) {
        fix p1[2], p2[2];
        long k = aimain_24A0B8(b + 0x30);
        uchar *o;

        ailow_24E19C_call(p1, b);
        o = b + 0x30 + k * 0x24;
        o[0] = 2;
        ailow_24E19C_call(p2, b);
        S32(o, 7) = p2[0];
        S32(o, 0xB) = p2[1];
        o[1] = 6;
        U16(o, 3) = (ushort)FIXINT(S32(o, 7));
        o[0xF] = 1;
        U16(o, 5) = (ushort)FIXINT(S32(o, 0xB));
        b[0x43] = 2;
        U16(b, 0x44) = (ushort)FIXINT(p1[0]);
        U16(b, 0x46) = (ushort)FIXINT(p1[1]);
        S32(b, 0x48) = p1[0];
        b[0x9C] = 0;
        S32(b, 0x4C) = p1[1];
    } else {
        b[0x43] = 3;
        b[0x9C] = 1;
    }
}

/* fire at the move goal / attack point pt (a packed point) */
static uchar move_to(uchar *b, ulong pt, long mode)
{
    return ailow_24FC04(b, 0, 0, 0, SQC(pt), SQC(pt >> 16), mode);
}

/* the 3000 ms "under fire" call of ailow_251C98 */
static void alert(uchar *b)
{
    if (game_time > U32(b, 0x27)) {
        U32(b, 0x27) = game_time + 3000;
        ailow_24AE4C(b[3], 7, 1);
        ailow_24AE4C(b[3], 9, 1);
    }
}

void ailow_251C98(uchar *b)
{
    uchar fa = 0, fb = 0, fc = 0, vis = 0;
    uchar canhit = 0;
    uchar ok;
    ushort mode;
    uchar res_a0, res_a4, res_ac;
    short cnt;
    fix pos[3];
    short who;
    fix p3[2];
    uchar u84[4];
    uchar u8c[4];

    ok = (b[0xC3] && b[0xCA]) ? 1 : 0;
    if (game_396E18 && b[0xD] == 3) {
        if (game_396E18 != 2)
            ok = 0;
        else if (!((1UL << (EN(BENT(b)).f23 & 31)) & game_396E14))
            ok = 0;
    }
    if (ok) {
        canhit = objtype_2935E0(BENT(b), (short)(ushort)b[0xCA]);
        if (SQUAD(b[6])[0xC9] == 1 && game_time - U32(b, 0x10A) > 250
            && game_time > U32(b, 0xF1)) {
            U32(b, 0x10A) = game_time;
            ailow_25129C(b);
            ailow_252C5C(b, (short)(ushort)b[0xCA], pos, &who, &fa, &fb, &fc);
            if (((who == 0 && fb == 0) || fc != 0) && fa == 0)
                vis = 1;
            else if (who != 0 && EN(who).f21 == 0 && !fa && !fb)
                vis = 1;
            else {
                uchar t;
                vis = 0;
                if (who != 0 && (t = EN(who).f21) != 0 && !fa && !fb && b[5] != BRAIN(t)[5]) {
                    if (!ailow_24ADE0(BRAIN(t)) || SQUAD(b[6])[0xC2])
                        vis = 1;
                }
            }
            if (vis && canhit)
                ailow_252758(b, (short)(ushort)b[0xCA]);
            if (vis != b[0xF5]) {
                Msg *m = MsgAlloc();
                if (m) {
                    aiacc_25AE30(m, U32(b, 1), U32(SQUAD(b[6]), 3), b[3], vis);
                    MsgSend(m);
                }
            }
            b[0xF5] = vis;
        }
    } else
        objtype_293464(BENT(b));

    if (b[0xD] == 3)
        goto done;
    mode = SQUAD(b[6])[0xCB];
    if (b[0xC3] && mode != 8 && mode != 5 && mode != 9 && mode != 4) {
        if (BRAIN(b[0xC9])[0xD] == 4 && b[0xD] == 1 && (short)mode != -1) {
            ushort q[2];
            q[0] = (ushort)(FIXINT(S32(b, 0xC)) + RND() % 3 - 1);
            q[1] = (ushort)(RND() % 3 + FIXINT(S32(b, 0xE)) - 1);
            mode = 0xFFFF;
            ailow_24FC04(b, 0, 0, 0, SQC(q[0]), SQC(q[1]), 2);
        }
        if (b[0xC4] && (short)mode != -1) {
            long d = fix_fast_pyth_dist(FIXINT(S32(b, 0xC)) - FIXINT(S32(b, 0xCA)),
                                        FIXINT(S32(b, 0xE)) - FIXINT(S32(b, 0xCC)));
            uchar m = SQUAD(b[6])[0xCB];
            short r = (m == 2) ? 5 : (m == 3) ? 2 : 3;
            if (d < r) {
                long dx = FIXINT(S32(b, 0xC)) - FIXINT(S32(b, 0xCA));
                long dy = FIXINT(S32(b, 0xE)) - FIXINT(S32(b, 0xCC));
                ushort a = atan2_scaled(dx, dy);
                long rr = (long)r << 16;
                ushort q[2];
                q[0] = (ushort)((((long)(((long long)fix_cos(a) * rr) >> 16)) >> 16) + FIXINT(S32(b, 0xC)));
                q[1] = (ushort)((((long)(((long long)fix_sin(a) * rr) >> 16)) >> 16) + FIXINT(S32(b, 0xE)));
                if (ailow_24FC04(b, 0, 0, 0, SQC(q[0]), SQC(q[1]), 3))
                    mode = 0xFFFF;
            }
        }
    }
    if (b[0xC3]) {
        uchar m = SQUAD(b[6])[0xCB];
        if (m != 5 && m != 9 && m != 4) {
            ailow_24AFB4(b, (short *)u8c, &cnt, (short *)u84);
            if (cnt <= 4 && b[5] == 1)
                mode = 7;
        }
    }

    switch ((ushort)(mode + 1)) {
    case 3:                                     /* mode 2 */
        if (!b[0xC3])
            break;
        if (b[5] == 1) {
            if (!b[0xC4] || !b[0xF5]) {
                ailow_24FD3C(b, 1, 1, 1, &res_a0);
                if (res_a0)
                    move_to(b, U32(b, 0xCC), 1);
            } else if (b[0xD0] && vis)
                move_to(b, U32(b, 0xE), 1);
            break;
        }
        /* fall through */
    case 2:                                     /* mode 1 */
        if (!b[0xC3])
            break;
        {
            long d = fix_fast_pyth_dist(FIXINT(S32(b, 0xC)) - FIXINT(S32(b, 0xCA)),
                                        FIXINT(S32(b, 0xE)) - FIXINT(S32(b, 0xCC)));
            if (!fa && d <= 10 && (!b[0xC4] || !b[0xF5])) {
                ailow_24FD3C(b, 1, 1, 1, &res_ac);
                break;
            }
        }
        /* fall through */
    case 4:                                     /* mode 3 */
        if (b[0xC3])
            move_to(b, U32(b, 0xCC), 1);
        break;
    case 8:                                     /* mode 7 */
        if (!b[0xC3])
            break;
        alert(b);
        if (b[0xC4] || b[0xF5])
            ailow_24FD3C(b, 0, 2, 3, &res_a4);
        break;
    case 6:                                     /* modes 5 and 9 */
    case 10:
        if (SQUAD(b[6])[0xCB] == 9)
            alert(b);
        b[0xB3] = 1;
        move_to(b, U32(SQUAD(b[6]), 0x12), 3);
        break;
    case 9:                                     /* mode 8 */
        if (game_time > U32(b, 0xED)) {
            long ang, n;
            switch (b[8]) {
            case 1: ang = 0x4000; n = 4; break;
            case 2: ang = 0xC000; n = 4; break;
            case 3: ang = 0x8000; n = 2; break;
            default: ang = 0; n = 2; break;
            }
            ailow_24E100(ang, n, p3);
            fix_fast_pyth_dist(p3[0] - EN(BENT(b)).x, p3[1] - EN(BENT(b)).y);
            ailow_24FC04(b, 0, 0, 0, p3[0], p3[1], 3);
            U32(b, 0xED) = game_time + 1000;
        }
        break;
    case 5: {                                   /* mode 4 */
        uchar *sq = SQUAD(b[6]);
        long d = fix_fast_pyth_dist(FIXINT(S32(b, 0xC)) - FIXINT(S32(sq, 0xD8)),
                                    FIXINT(S32(b, 0xE)) - FIXINT(S32(sq, 0xDA)));
        if (d > 2)
            move_to(b, U32(sq, 0xDA), 3);
        else if (b[0xD0])
            move_to(b, U32(b, 0xE), 1);
        break;
    }
    default:
        break;
    }
done:
    if (b[0xD0] && ailow_2500EC(b)) {
        b[0xD0] = 0;
        U16(b, 0xD5) = 0;
    }
}

/* the angle between the shooter (self) -> tgt and self -> (x, y), as the smaller of the two
 * 16-bit differences */
static ushort angle_off(short self, short tgt, fix x, fix y)
{
    ushort a1 = atan2_scaled(x - EN(self).x, y - EN(self).y);
    ushort a2 = atan2_scaled(EN(tgt).x - EN(self).x, EN(tgt).y - EN(self).y);
    ushort d1 = (ushort)(a2 - a1), d2 = (ushort)(a1 - a2);
    return d1 < d2 ? d1 : d2;
}

uchar ailow_252758(uchar *b, long tgt)
{
    short t = (short)tgt;
    uchar r = objtype_293834(BENT(b), t, 1);
    ulong now;
    uchar c;

    if (r) {
        short self = BENT(b);
        if (EN(self).cls != 4) {
            ushort a = atan2_scaled(EN(t).x - EN(self).x, EN(t).y - EN(self).y);
            if (EN(BENT(b)).cls == 1 && (short)(ushort)ai_37118C < 2) {
                ai_37118C = 2;
                ai_371188 = a;
            }
        }
    }
    if (r)
        return r;

    {
        uchar *sq = SQUAD(b[6]);
        if (sq[0xEA] && EN(t).cls != 4 && sq[0xCB] != 9) {
            uchar n = sq[0x10];
            if (n > 1)
                aimain_24A6A0(aiperc_2575C0(sq), 0x2E);
            else if (n == 1)
                aimain_24A6A0(aiperc_2575C0(sq), 0x2C);
            SQUAD(b[6])[0xEA] = 0;
        }
    }
    now = game_time;
    if (U32(b, 0xD7) + 5000 < now)
        U16(b, 0xDB) = 100;
    else
        U16(b, 0xDB) = (ushort)(((now - U32(b, 0xD7)) * 100) / 5000);
    music_2469E4(2, 1);
    U32(b, 0xF6) = game_time;
    c = EN(b[0xCA]).cls;
    if ((c == 1 || c == 2 || c == 4) && U32(b, 0xFA) + 1000 < game_time) {
        uchar i;

        U32(b, 0xFA) = game_time;
        for (i = 0; i < 60; i++) {
            uchar *o = BRAIN(i);
            if (o[0] && !o[0xA] && o[5] != b[5]) {
                short self = BENT(b);
                short e = BENT(o);
                long dx = EN(e).x - EN(self).x;
                long dy = EN(e).y - EN(self).y;
                if (dx + dy < 0x140000 && angle_off(self, t, EN(e).x, EN(e).y) < 0x400) {
                    Msg *m = MsgAlloc();
                    if (m) {
                        aiacc_25AE48(m, U32(b, 1), U32(o, 1), b[3], i);
                        MsgSend(m);
                    }
                }
            }
        }
        for (i = 0; i < 32; i++) {
            uchar *s = SQUAD(i);
            if (s[0] && s[7] != b[5]) {
                short self = BENT(b);
                long dx = S32(s, 0x16) - EN(self).x;
                long dy = S32(s, 0x1A) - EN(self).y;
                if (dx + dy < 0x140000 && angle_off(self, t, S32(s, 0x16), S32(s, 0x1A)) < 0x400) {
                    Msg *m = MsgAlloc();
                    if (m) {
                        aiacc_25AE48(m, U32(b, 1), U32(s, 3), b[3], i);
                        MsgSend(m);
                    }
                }
            }
        }
    }
    return 0;
}

void ailow_252C5C(uchar *b, long tgt, fix *pos, short *who, uchar *fa, uchar *fb, uchar *fc)
{
    short t = (short)tgt;
    long d, d2;

    pos[2] = 0;
    pos[1] = pos[2];
    pos[0] = pos[1];
    *who = 0;
    *fa = 0;
    *fb = 0;
    d = fix_fast_pyth_dist(FIXINT(S32(b, 0xC)) - FIXINT(EN(t).x),
                           FIXINT(S32(b, 0xE)) - FIXINT(EN(t).y));
    if (b[0xD] == 1) {
        uchar *pb = (uchar *)&game_pba[EN(BENT(b)).slot] + 6;
        long k = S32(pb, 0xC5) >> 24;
        uchar w = pb[k * 15 + 0x3F];
        if ((long)ord_table[w].range < d) {
            *fa = 1;
            *who = 0;
            return;
        }
    }
    objtype_293834(BENT(b), t, 0);
    pos[0] = hit_pos[0];
    pos[1] = hit_pos[1];
    pos[2] = hit_pos[2];
    *who = hit_who;
    *fc = hit_tile;
    d2 = fix_fast_pyth_dist(FIXINT(S32(b, 0xC)) - FIXINT(pos[0]),
                            FIXINT(S32(b, 0xE)) - FIXINT(pos[1]));
    if (d2 < d - 1 && t != *who)
        *fb = 1;
    if (b[0xD] == 1) {
        uchar *pba = (uchar *)&game_pba[EN(BENT(b)).slot];
        long k = S32(pba, 0xCB) >> 24;
        uchar w = pba[0x45 + k * 15];
        uchar ray = ((uchar *)&ord_table[w])[4];
        if (ray == 1) {
            if (d2 <= 5) {
                *fb = 1;
                *fc = 0;
            } else
                *fb = 0;
        }
    }
}

uchar ailow_252E4C(uchar *b, long ent, long f)
{
    return objtype_293834(BENT(b), (short)ent, (uchar)f);
}

void ailow_252E60(void *brain)
{
    uchar *b = brain;
    uchar *o = b + 0x30;
    uchar *st = b + 0x43;

    switch (o[0]) {
    case 0:
        st[0] = 0;
        b[0x9C] = 0;
        return;
    case 1:
        st[0] = 2;
        U32(st, 1) = U32(b, 0xE);
        b[0xB3] = 1;
        S32(st, 5) = EN(BENT(b)).x;
        S32(st, 9) = EN(BENT(b)).y;
        b[0x9C] = 0;
        return;
    case 2:
    case 4:
        if (b[0xD] != 3) {
            st[0] = 2;
            U32(st, 1) = U32(o, 3);
            copy_dw(st + 5, o + 7, 2);
            b[0xB3] = 1;
            o[0xF] = 0;
            U32(o, 3) = U32(st, 1);
            copy_dw(o + 7, st + 5, 2);
        }
        if (o[0] == 2) {
            b[0x9C] = 0;
            return;
        }
        if (b[0xC3] && b[0xCA])
            st[0] = 3;
        return;
    case 8:
        ailow_251668(b, o[0xF], 0);
        return;
    case 0x10:
        ailow_251668(b, 0, S16(o, 0xF));
        return;
    default:
        return;
    }
}

void ailow_252F78(void *brain)
{
    uchar *b = brain;
    uchar *st = b + 0x43;

    switch (st[0]) {
    case 0:
        objtype_293464(BENT(b));
        return;
    case 2:
        if (U32(b, 0x9D) != U32(st, 1) && game_time > U32(b, 0xD1)) {
            if (ailow_2512F8(b, 0, 0, 0, S32(st, 5), S32(st, 9))) {
                *BRPATH(b) = 0;
                b[0xB1] = 0;
            }
        }
        ailow_2500EC(b);
        objtype_293464(BENT(b));
        return;
    case 3:
    case 4:
        ailow_251C98(b);
        return;
    default:
        return;
    }
}

void ailow_253008(long brain, long b)
{
    uchar idx = (uchar)brain;
    uchar *bb = BRAIN(idx);
    uchar *sq = SQUAD(bb[6]);
    uchar i;
    uchar *p;

    sq[0x10]--;
    if (bb[0x116] == 1)
        sq[0x167] -= 1;
    if (BRAIN(idx)[0xA] == 0)
        sq[0x11]--;
    aiperc_256710(sq);
    sq[0xB4] = 0;
    U32(sq, 0xB5) = 0;
    for (i = 0; i < 60; i++) {
        uchar *o = BRAIN(i);
        if (o[0] && !o[0xA] && o[6] == bb[6] && o[8] > bb[8])
            o[8]--;
    }
    p = BRAIN(sq[0xF]);
    if (p == bb)
        sq[0xF] = bb[7];
    else {
        while (idx != p[7]) {
            p[0xB3] = 1;
            p = BRAIN(p[7]);
        }
        p[7] = bb[7];
    }
    if (sq[0x10] == 0) {
        sq[0] = 0;
        aiperc_259E3C(bb[6]);
    }
    bb[6] = 0;
    if ((uchar)b == 0) {
        long k = aimain_24A0B8(bb + 0x30);
        bb[0x30 + k * 0x24] = 0;
        bb[0x31 + k * 0x24] = 0;
    }
}

void ailow_2531B0(long brain, long squad)
{
    uchar idx = (uchar)brain, s = (uchar)squad;
    uchar *bb = BRAIN(idx);
    uchar *sq = SQUAD(s);
    uchar c;

    bb[6] = s;
    bb[5] = sq[7];
    sq[0x10]++;
    bb[8] = sq[0x10];
    if (bb[0xA] == 0)
        ailow_24B34C(idx);
    bb[7] = sq[0xF];
    sq[0xF] = idx;
    aiperc_256710(sq);
    U32(sq, 0xB5) = 0;
    sq[0xB4] = 0;
    if (sq[0x157] == 4)
        sq[0x157] = 0;
    if (bb[0x116] == 1)
        sq[0x167] += 1;
    c = bb[7];
    if (c) {
        long k = aimain_24A0F4(BRAIN(c) + 0x30);
        if (k != 3) {
            long j = aimain_24A0B8(bb + 0x30);
            copy_dw(bb + 0x30 + j * 0x24, BRAIN(bb[7]) + 0x30 + k * 0x24, 9);
        }
    }
}

uchar ailow_253334(long brain)
{
    uchar idx = (uchar)brain;
    uchar *bb = BRAIN(idx);
    uchar old, ns;
    uchar *n, *o;

    if (SQUAD(bb[6])[0x10] == 1)
        return bb[6];
    old = bb[6];
    ailow_253008(idx, 1);
    ns = aiperc_253AE8(0, U32(bb, 0xE), S8(bb, 5));
    ailow_2531B0(idx, ns);
    n = SQUAD(ns);
    o = SQUAD(old);
    n[0x90] = o[0x90];
    n[0xCB] = o[0xCB];
    n[0xC9] = o[0xC9];
    U16(n, 0xA9) = U16(o, 0xA9);
    copy_dw(n + 0x24, o + 0x24, 9);
    copy_dw(n + 0x48, o + 0x48, 9);
    copy_dw(n + 0x6C, o + 0x6C, 9);
    return ns;
}

uchar ailow_25344C(void)
{
    uchar br = EN(game_player_entity).f21;

    if (SQUAD(BRAIN(br)[6])[0x11] > 0)
        return ailow_253334(br);
    return 0;
}

uchar ailow_2534A4(long brain, long squad)
{
    uchar idx = (uchar)brain, s = (uchar)squad;

    if (SQUAD(s)[0] == 0)
        return 0;
    if (BRAIN(idx)[6] != s) {
        ailow_253008(idx, 0);
        ailow_2531B0(idx, s);
    }
    return 1;
}

void ailow_2534FC(long brain)
{
    uchar idx = (uchar)brain;
    uchar *bb = BRAIN(idx);

    MsgFlush((MsgAddr *)(bb + 1));
    ailow_253008(idx, 0);
    bb[0] = 0;
    aimain_24A158(*(uchar **)(bb + 0xAD));
    bb[7] = BRAIN(0)[7];
    BRAIN(0)[7] = idx;
}

uchar ailow_253580(void *brain)
{
    uchar *b = brain;

    switch (b[0xD]) {
    case 1:
        return U16(&game_pba[EN(BENT(b)).slot], 0xAA) == 0 ? 1 : 0;
    case 2:
        switch (EN(BENT(b)).type) {
        case 4: case 5: case 6: case 8:
            return 3;
        default:
            return 2;
        }
    default:
        return 1;
    }
}

void ailow_253608(long brain, long secs)
{
    uchar *bb = BRAIN(brain);

    bb[0x135] = 1;
    U32(bb, 0x136) = (ushort)secs * 1000 + game_time;
}

void ailow_253640(uchar brain, long ent, fix *pos)
{
    uchar *bb;
    uchar *sq;
    uchar *path;

    if (!brain)
        return;
    bb = BRAIN(brain);
    if (bb[0] == 0 && bb[0xA] == 0)
        return;
    U16(bb, 0xB5) = (ushort)ent;
    S32(bb, 0xB7) = pos[0];
    S32(bb, 0xBB) = pos[1];
    U32(bb, 0xBF) = game_time;
    sq = SQUAD(bb[6]);
    if (sq[0x157] == 2 && (short)ent != 0 && (short)ent == S16(sq, 0xA9))
        aiperc_255858(bb[6]);
    path = *(uchar **)(bb + 0xAD);
    if (path && *path) {
        short e = BENT(bb);
        long d = fix_fast_pyth_dist(S32(bb, 0xA5) - EN(e).x, S32(bb, 0xA9) - EN(e).y);
        if ((d < 0x20000 && (short)ent != 0) || d < 0x10000) {
            S32(bb, 0xA5) = EN(BENT(bb)).x;
            S32(bb, 0xA9) = EN(BENT(bb)).y;
            **(uchar **)(bb + 0xAD) = 0;
        }
    }
}

void ailow_2537AC(long brain, long ent)
{
    uchar *bb;

    if (!(uchar)brain)
        return;
    bb = BRAIN(brain);
    if (!bb[0])
        return;
    if ((ushort)ent != (ushort)bb[0xCA])
        return;
    bb[0xC3] = 0;
    bb[0xCA] = 0;
    bb[0xC9] = 0;
    bb[0xCB] = 0;
    U16(bb, 0xCC) = 0xFFFF;
    U16(bb, 0xCE) = 0xFFFF;
}
