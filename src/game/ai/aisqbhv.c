/* aisqbhv.c -- Terra Nova AI squad behaviour: enemy contact, engagement, patrols and orders
 * (second part of module M009, 0x258908-0x25A10F; the first part is g4b's).
 * Decompiled from __FF.EXE v1.09.
 *
 * Every function takes a squad record (GameSquad *, offsets in include/g4_c1.h) except
 * aiperc_259E3C / 259F3C / 25A0B0, which take a squad index.
 *
 *   0x258908 aiperc_258908  look for an enemy (once per AI frame, cached): picks the nearer of
 *                           the two candidates of aiperc_258170 (aircraft last), notes its squad
 *                           and position and tells the brains; with no enemy, a squad on its
 *                           way to the last known position gives up when it got there.
 *                           Returns TRUE when an enemy was found.
 *   0x258C7C aiperc_258C7C  start moving to a point (order kind 0x20 at the queue head) with a
 *                           priority; FALSE when a more important order is queued
 *   0x258CE8 aiperc_258CE8  "search": every 10 s move to a point 30 squares from the enemy
 *   0x258EEC aiperc_258EEC  enemy contact: queue an attack order (kind 8), start combat music
 *   0x258F98 aiperc_258F98  attack the entity of the order at the queue head
 *   0x259120 aiperc_259120  message 0x32 to every brain (unreferenced)
 *   0x2591B4 aiperc_2591B4  plan a path towards the current destination (PathPlan) and follow
 *                           it, or go straight there when it is close or no path is found
 *   0x2593DC aiperc_2593DC  engagement state 1, then aiperc_2593E4 (unreferenced)
 *   0x2593E4 aiperc_2593E4  tell every brain the enemy (message 0x1E with the entity, or 0x1D
 *                           with the squad)
 *   0x25946C aiperc_25946C  (unreferenced) state 4 + aiperc_2593E4, or aiperc_2591B4
 *   0x259498 aiperc_259498  engagement state 2: close in
 *   0x2595C0 aiperc_2595C0  engagement state 4: hold and fire, re-plan after a while
 *   0x2596E4 aiperc_2596E4  engagement step (dispatch on state +0xC3)
 *   0x2598C8 aiperc_2598C8  clear the "moving" flag (unreferenced)
 *   0x2598D0 aiperc_2598D0  enter combat: reset the combat state after 20 s of quiet
 *   0x2599A4 aiperc_2599A4  patrol: next region of the patrol list when there
 *   0x259AA4 aiperc_259AA4  region list: next region whose bit is set, then hold
 *   0x259BE8 aiperc_259BE8  periodic enemy check, asking the AI script first
 *   0x259C70 aiperc_259C70  carry out the order at the queue head
 *   0x259E3C aiperc_259E3C  remove a squad (side list, path, free list)
 *   0x259F3C aiperc_259F3C  an entity died: forget it as the squad's attack target / enemy
 *   0x25A0B0 aiperc_25A0B0  unlink a squad from every next pointer, mark it in use
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_c1.h"
#include "g9_c.h"     /* added at the G9/G10 merge */

#ifndef TN_LINK_ORIGINAL_DATA
uchar aiperc_enemy_checked;
uchar aiperc_enemy_found;
#endif

/* squad offsets */
#define SQ_ACTIVE    0x00
#define SQ_INUSE     0x01
#define SQ_ADDR      0x03
#define SQ_SIDE      0x07
#define SQ_SIDEIDX   0x0D
#define SQ_NEXT      0x0E
#define SQ_BRAIN     0x0F
#define SQ_NBRAINS   0x10
#define SQ_X         0x12
#define SQ_Y         0x14
#define SQ_FORM      0x1E
#define SQ_ORDERS    0x24
#define SQ_MOVE      0x37
#define SQ_MOVING    0x90
#define SQ_DEST      0x91          /* x; y at 0x93 */
#define SQ_PATH      0x95
#define SQ_ATTACK    0xA9
#define SQ_BE        0xBE          /* enemy squad */
#define SQ_BF        0xBF          /* enemy entity (short) */
#define SQ_C1        0xC1
#define SQ_C2        0xC2
#define SQ_STATE     0xC3
#define SQ_EPOS      0xC5          /* enemy position x; y at 0xC7 */
#define SQ_ORDST     0xCB
#define SQ_SKILL     0x156

/* brain offsets */
#define BR_ADDR      0x01
#define BR_SQUAD     0x06
#define BR_NEXT      0x07
#define BR_ENT       0x0B
#define BR_CLASS     0x0D
#define BR_POS       0x0E

#define TIME         game_time

/* the brain of an entity (entity +0x21) */
#define ENT_BRAIN(e) C1_BR(C1_ENT(e).f21)
/* the squad's first brain */
#define FIRST_BRAIN(sq) C1_BR(C1_U8(sq, SQ_BRAIN))

static long iabs(long v) { return v < 0 ? -v : v; }

/* the formation falls back from 2 to 1 when a soldier squad stops (repeated in many places) */
static void form_reset(uchar *sq)
{
    if (C1_U8(FIRST_BRAIN(sq), BR_CLASS) == 1 && C1_U8(sq, SQ_FORM) == 2)
        C1_U8(sq, SQ_FORM) = 1;
}

/* 0x258908 */
uchar aiperc_258908(void *sqv)
{
    uchar *sq = sqv;
    short e1, e2, e3;
    uchar found, *b;

    if (aiperc_enemy_checked)
        return aiperc_enemy_found;
    aiperc_258170(sq, C1_U8(sq, 0xE6) == 0, &e1, &e2, &e3);
    aiperc_enemy_checked = 1;
    found = (e1 != 0 || e2 != 0);
    aiperc_enemy_found = found;
    if (found) {
        if (e1 == 0) {
            C1_S16(sq, SQ_BF) = e2;
        } else if (e2 == 0) {
            C1_S16(sq, SQ_BF) = e1;
        } else {
            /* the nearer one, the distance to the second scaled by (100 + skill) / 100 */
            uchar *s1 = C1_SQ(C1_U8(sq, SQ_BE));
            uchar *s2 = C1_SQ(C1_U8(ENT_BRAIN(e2), BR_SQUAD));
            long d1 = fix_fast_pyth_dist(C1_S16(sq, SQ_X) - C1_S16(s1, SQ_X), C1_S16(sq, SQ_Y) - C1_S16(s1, SQ_Y));
            long d2 = fix_fast_pyth_dist(C1_S16(sq, SQ_X) - C1_S16(s2, SQ_X), C1_S16(sq, SQ_Y) - C1_S16(s2, SQ_Y));
            int a2, a1;
            C1_S16(sq, SQ_BF) = (d2 * (C1_U8(sq, SQ_SKILL) + 100) / 100 < d1) ? e2 : e1;
            /* aircraft (brain class 4) only when the other is one too */
            a2 = C1_U8(ENT_BRAIN(e2), BR_CLASS) == 4;
            a1 = C1_U8(ENT_BRAIN(e1), BR_CLASS) == 4;
            if (a2 && !a1)
                C1_S16(sq, SQ_BF) = e1;
            else if (!a2 && a1)
                C1_S16(sq, SQ_BF) = e2;
        }
        C1_U8(sq, SQ_C1) = 1;
        b = ENT_BRAIN(C1_S16(sq, SQ_BF));
        C1_U8(sq, SQ_BE) = C1_U8(b, BR_SQUAD);
        b = ENT_BRAIN(C1_S16(sq, SQ_BF));
        C1_U32(sq, SQ_EPOS) = C1_U32(b, BR_POS);
        aiperc_2593E4(sq);
        return found;
    }
    if (C1_U8(sq, SQ_MOVING) == 1 && C1_U8(sq, SQ_ORDST) != 9 && C1_U8(sq, SQ_ORDST) != 5) {
        uchar there = C1_U8(C1_SQ(C1_U8(sq, SQ_BE)), SQ_ACTIVE) == 0;
        if (!there) {
            ulong p = C1_U32(sq, SQ_EPOS);
            there = fix_fast_pyth_dist(C1_S16(sq, SQ_X) - C1_PT_X(p), C1_S16(sq, SQ_Y) - C1_PT_Y(p)) <= 1;
        }
        if (there) {
            aiperc_2579E0(sq);
            return found;
        }
        if (C1_U8(sq, SQ_ORDST) != 5 && C1_U8(sq, SQ_ORDST) != 9) {
            C1_U8(sq, SQ_STATE) = 2;
            if (aiperc_257344(sq, C1_U32(sq, SQ_EPOS)))
                aiperc_2573C0(sq, C1_U32(sq, SQ_EPOS));
        }
    }
    return found;
}

/* 0x258C7C: prio arrives in bl (the map's estimate of 2 arguments was short by one) */
uchar aiperc_258C7C(void *sqv, ulong pt, long prio)
{
    uchar *sq = sqv;
    uchar kind;

    if (aiperc_2539F0(sq)) {
        aiperc_2598D0(sq);
        C1_U8(sq, SQ_STATE) = 2;
        if (C1_U8(FIRST_BRAIN(sq), BR_CLASS) == 1)
            C1_U8(sq, SQ_FORM) = 2;
    }
    aiperc_2573C0(sq, pt);
    aimain_249FD0(sq + SQ_ORDERS);
    kind = C1_U8(sq, SQ_ORDERS);
    if (kind == 0 || kind == 1 || (uchar)prio >= C1_U8(sq, SQ_ORDERS + 1)) {
        C1_U8(sq, SQ_ORDERS) = 0x20;
        C1_U32(sq, SQ_ORDERS + 3) = pt;
        C1_U8(sq, SQ_ORDERS + 1) = (uchar)prio;
        return 1;
    }
    return 0;
}

/* 0x258CE8 */
void aiperc_258CE8(void *sqv)
{
    uchar *sq = sqv;
    ulong p;
    ushort ang;
    long dx, dy, x, y;
    short ent;

    aiperc_258908(sq);
    if (C1_U8(sq, 0xB4) == 0 && C1_U32(sq, 0x108) + 8000 < TIME) {
        aiperc_2579E0(sq);
        return;
    }
    if (C1_U32(sq, 0x111) + 10000 > TIME)
        return;
    if (C1_U8(sq, SQ_C1) == 0)
        return;
    C1_U32(sq, 0x111) = TIME;
    if (C1_U8(sq, 0x110) != 2) {
        C1_U8(sq, 0x110) = 2;
        aimain_24A6A0(aiperc_2575C0(sq), 0x39);
    }
    p = C1_U32(sq, SQ_EPOS);
    ent = C1_S16(sq, SQ_BF);
    if (ent != 0 && C1_ENT(ent).f21 != 0)
        p = C1_U32(C1_BR(C1_ENT(ent).f21), BR_POS);
    aiperc_2571D8(sq);
    dx = C1_S16(sq, SQ_X) - C1_PT_X(p);
    dy = C1_S16(sq, SQ_Y) - C1_PT_Y(p);
    if (iabs(dx) > 0x800000 || iabs(dy) > 0x800000)   /* never true for map squares */
        ang = fix_atan2(dy >> 7, dx >> 7);
    else
        ang = fix_atan2(dy, dx);
    /* 30 squares further on, away from the enemy, kept inside the map (10..502) */
    x = (fix)(((long long)fix_cos(ang) * 0x1E0000) >> 16);
    x = (x >> 16) + C1_S16(sq, SQ_X);
    y = (fix)(((long long)fix_sin(ang) * 0x1E0000) >> 16);
    y = (y >> 16) + C1_S16(sq, SQ_Y);
    if ((short)x < 10) x = 10;
    if ((short)y < 10) y = 10;
    if ((short)x >= 0x1F6) x = 0x1F6;
    if ((short)y >= 0x1F6) y = 0x1F6;
    aiperc_2573C0(sq, C1_PT(x, y));
}

/* 0x258EEC */
void aiperc_258EEC(void *sqv)
{
    uchar *sq = sqv;
    long i;

    if (!aiperc_2539F0(sq))
        return;
    if (!aiperc_258908(sq))
        return;
    C1_U8(sq, SQ_C1) = 1;
    C1_U8(sq, SQ_C2) = 0;
    i = aimain_24A050(sq + SQ_ORDERS, 8);
    C1_U8(sq, i * 0x24 + 0x25) = 8;
    C1_U8(sq, i * 0x24 + 0x33) = C1_U8(sq, SQ_BE);
    if (C1_U8(sq, SQ_MOVING) != 1) {
        aiperc_2598D0(sq);
        C1_U8(sq, SQ_STATE) = 0;
        if (C1_U8(FIRST_BRAIN(sq), BR_CLASS) == 1)
            C1_U8(sq, SQ_FORM) = 2;
        C1_U8(sq, 0xE8) = 0;
        music_2469E4(2, 1);
    }
    if (C1_U8(sq, SQ_ORDST) == 9)
        aiperc_258CE8(sq);
}

/* 0x258F98 */
void aiperc_258F98(void *sqv)
{
    uchar *sq = sqv;
    short e;
    uchar b;

    if (C1_U8(sq, SQ_MOVING) == 1) {
        if (C1_U16(sq, 0x33) == 0)
            return;
        if ((long)C1_U16(sq, 0x33) == (long)C1_S16(sq, SQ_BF))
            return;
    }
    if (C1_S16(sq, SQ_BF) == 0 || C1_ENT(C1_S16(sq, SQ_BF)).active == 0) {
        if (C1_U16(sq, 0x33) == 0)
            return;
        if (game_entities[C1_U16(sq, 0x33)].active == 0)
            return;
    }
    C1_U8(sq, 0xB4) = 0;
    C1_U32(sq, 0xB5) = 0;
    C1_U8(sq, 0xB9) = 0;
    C1_U32(sq, 0xBA) = 0;
    e = C1_S16(sq, 0x33);
    if (e == 0)
        return;
    C1_S16(sq, SQ_BF) = e;
    b = C1_ENT(e).f21;
    C1_U8(sq, SQ_BE) = b ? C1_U8(C1_BR(b), BR_SQUAD) : 0;
    C1_U8(sq, SQ_C1) = 1;
    C1_U8(sq, SQ_C2) = 1;
    C1_S16(sq, SQ_EPOS) = (short)(C1_ENT(C1_S16(sq, SQ_BF)).x >> 16);
    C1_S16(sq, SQ_EPOS + 2) = (short)(C1_ENT(C1_S16(sq, SQ_BF)).y >> 16);
    aiperc_2598D0(sq);
    C1_U8(sq, SQ_STATE) = 0;
    C1_U8(sq, 0xE8) = 0;
    if (C1_U8(FIRST_BRAIN(sq), BR_CLASS) == 1)
        C1_U8(sq, SQ_FORM) = 2;
    aiperc_257AB4(sq);
}

/* 0x259120 (unreferenced) */
void aiperc_259120(void *sqv)
{
    uchar *sq = sqv;
    uchar b;
    Msg *m;

    if (!aiperc_2539F0(sq))
        return;
    for (b = C1_U8(sq, SQ_BRAIN); b; b = C1_U8(C1_BR(b), BR_NEXT)) {
        if ((m = MsgAlloc()) == NULL)
            return;
        aiacc_25ADD4(m, C1_U32(sq, SQ_ADDR), C1_U32(C1_BR(b), BR_ADDR));
        m->flag = 7;
        MsgSend(m);
    }
}

/* 0x2591B4 */
void aiperc_2591B4(void *sqv)
{
    uchar *sq = sqv;
    fix spd[3];
    ulong dest, from, next;
    uchar cl, close;
    uchar st = C1_U8(sq, SQ_ORDST), cls;

    if (st == 7 || st == 5 || st == 9)
        return;
    cls = C1_U8(FIRST_BRAIN(sq), BR_CLASS);
    if (cls == 3 || cls == 4)
        return;
    interact_298864(C1_S16(C1_BR(C1_U8(C1_SQ(C1_U8(sq, SQ_BE)), SQ_BRAIN)), BR_ENT), spd);
    aiperc_258908(sq);
    dest = C1_U32(sq, SQ_EPOS);
    cl = aiperc_257820(sq);
    if (C1_U32(sq, 0xB0) + 3000 >= TIME && C1_U8(sq, 0xAF) != 0) {
        close = 1;
    } else {
        long a;
        C1_U32(sq, 0xB0) = TIME;
        if (C1_U8(sq, 0xAF) && C1_S16(sq, SQ_ATTACK) != 0) {
            short e = C1_S16(sq, SQ_ATTACK);
            from = C1_PT(C1_ENT(e).x >> 16, C1_ENT(e).y >> 16);
        } else {
            from = C1_U32(sq, SQ_X);
        }
        a = (short)((spd[2] * 24) >> 16);
        next = PathPlan(C1_U32(sq, SQ_X), from, dest, 0, a, (void *)C1_U32(sq, SQ_PATH), cl,
                        ailow_253580(FIRST_BRAIN(sq)), C1_S16(sq, 0x14F), 2);
        if (C1_PT_X(next) == -1 && C1_PT_Y(next) == -1) {
            close = 1;
        } else {
            uchar *path = (uchar *)C1_U32(sq, SQ_PATH);
            close = fix_fast_pyth_dist(C1_S16(sq, SQ_X) - C1_PT_X(next), C1_S16(sq, SQ_Y) - C1_PT_Y(next)) <= 2;
            if (C1_S16(path, 7) <= 0 || C1_S16(path, 5) == 0)
                C1_U8(path, 0) = 0;
        }
    }
    if (close) {
        aiperc_258C7C(sq, dest, 6);
        return;
    }
    aiperc_2567A4(sq);
    C1_U8(sq, SQ_STATE) = 2;
    if (aiperc_257344(sq, next))
        aiperc_2573C0(sq, next);
}

/* 0x2593DC (unreferenced; falls through into 0x2593E4) */
void aiperc_2593DC(void *sqv)
{
    C1_U8(sqv, SQ_STATE) = 1;
    aiperc_2593E4(sqv);
}

/* 0x2593E4 */
void aiperc_2593E4(void *sqv)
{
    uchar *sq = sqv;
    uchar b;
    Msg *m;

    if (!aiperc_2539F0(sq))
        return;
    for (b = C1_U8(sq, SQ_BRAIN); b; b = C1_U8(C1_BR(b), BR_NEXT)) {
        if ((m = MsgAlloc()) == NULL)
            return;
        if (C1_S16(sq, SQ_BF) != 0 && C1_U8(sq, SQ_C2) != 0)
            aiacc_25B014(m, C1_U32(sq, SQ_ADDR), C1_U32(C1_BR(b), BR_ADDR), C1_U16(sq, SQ_BF), 0);
        else
            aiacc_25AFFC(m, C1_U32(sq, SQ_ADDR), C1_U32(C1_BR(b), BR_ADDR), C1_U8(sq, SQ_BE), 0);
        m->flag = 7;
        MsgSend(m);
    }
}

/* 0x25946C (unreferenced) */
void aiperc_25946C(void *sqv)
{
    uchar *sq = sqv;
    if (C1_U8(sq, 0xB4) != 0) {
        C1_U8(sq, SQ_STATE) = 4;
        C1_U32(sq, 0x151) = TIME;
        aiperc_2593E4(sq);
    } else {
        aiperc_2591B4(sq);
    }
}

/* the common start of 0x259498 / 0x2595C0: TRUE when the step is done */
static int engage_common(uchar *sq)
{
    uchar st = C1_U8(sq, SQ_ORDST);
    if ((st == 4 || st == 6 || st == 8) && C1_U8(sq, 0xB4) == 0
        && C1_U32(sq, 0x108) + 8000 < TIME) {
        aiperc_2579E0(sq);
        return 1;
    }
    st = C1_U8(sq, SQ_ORDST);
    if (st == 5) {
        aiperc_256C90(sq);
        return 1;
    }
    if (st == 9) {
        aiperc_258CE8(sq);
        aiperc_256C90(sq);
        return 1;
    }
    return 0;
}

/* 0x259498 */
void aiperc_259498(void *sqv)
{
    uchar *sq = sqv;
    long d;

    if (engage_common(sq))
        return;
    if (C1_U8(sq, 0xE8) == 0 && C1_U8(sq, 0xB9) != 0) {
        aiperc_2571D8(sq);
        aiperc_2573C0(sq, C1_U32(sq, SQ_X));
        C1_U32(sq, 0x151) = TIME;
        C1_U8(sq, SQ_STATE) = 4;
        aiperc_2593E4(sq);
        return;
    }
    d = iabs((short)(C1_U16(sq, SQ_DEST) - C1_U16(sq, SQ_X)))
      + iabs((short)(C1_U16(sq, SQ_DEST + 2) - C1_U16(sq, SQ_Y)));
    if ((short)d <= 2) {
        aiperc_2571D8(sq);
        C1_U32(sq, 0x151) = TIME;
        C1_U8(sq, SQ_STATE) = 4;
        aiperc_2593E4(sq);
        return;
    }
    if (!aiperc_256C90(sq))
        aiperc_2591B4(sq);
}

/* 0x2595C0 */
void aiperc_2595C0(void *sqv)
{
    uchar *sq = sqv;

    if (engage_common(sq))
        return;
    if (C1_U8(sq, 0xB9) == 0 && C1_U8(sq, 0xB4) == 0)
        return;
    if (C1_U8(sq, 0xB9) != 0)
        C1_U32(sq, 0x151) = TIME;
    if (TIME - C1_U32(sq, 0x151) < (ulong)((100 - C1_U8(sq, SQ_SKILL)) * 80 + 1000)) {
        aiperc_2591B4(sq);
        return;
    }
    if (aiperc_258908(sq) && aiperc_257344(sq, C1_U32(sq, SQ_EPOS)))
        aiperc_258C7C(sq, C1_U32(sq, SQ_EPOS), 6);
}

/* 0x2596E4 */
void aiperc_2596E4(void *sqv)
{
    uchar *sq = sqv;
    uchar st = C1_U8(sq, SQ_ORDST);

    if (st != 5 && st != 9) {
        uchar be = C1_U8(sq, SQ_BE);
        if (C1_U8(sq, SQ_C1) == 0
            || (be != 0 && (C1_U8(C1_SQ(be), SQ_ACTIVE) == 0 || C1_U8(C1_SQ(be), SQ_NBRAINS) == 0))) {
            Msg *m;
            aiperc_2579E0(sq);
            if (aimain_24A0F4(sq + SQ_ORDERS) != 3)
                return;
            if ((m = MsgAlloc()) == NULL)
                return;
            aiacc_25ADE0(m, C1_U32(sq, SQ_ADDR), C1_U32(sq, SQ_ADDR), 0, 0);
            m->flag = 7;
            MsgSend(m);
            C1_U8(sq, SQ_MOVING) = 0;
            form_reset(sq);
            return;
        }
        if (aiperc_258908(sq)) {
            /* quirk: the original's distance test compares a point with itself (|0| + |0| >= 5) */
            long zero = 0;
            if (iabs(zero) + iabs(zero) >= 5) {
                short e = C1_S16(sq, SQ_BF);
                if (e != 0 && C1_ENT(e).f21 != 0)
                    C1_U32(sq, SQ_EPOS) = C1_U32(C1_BR(C1_ENT(e).f21), BR_POS);
                C1_U8(sq, SQ_STATE) = 0;
            }
        }
    }
    C1_U32(sq, 0xEB) = TIME;
    if (C1_U8(sq, SQ_ORDST) != 9) {
        C1_U8(sq, SQ_STATE) = 2;
        C1_U8(sq, SQ_FORM) = 1;
    }
    switch (C1_U8(sq, SQ_STATE)) {
    case 0:
        C1_U8(sq, SQ_STATE) = 1;
        aiperc_2593E4(sq);
        break;
    case 1:
        if (C1_U8(sq, 0xB4) != 0) {
            C1_U32(sq, 0x151) = TIME;
            C1_U8(sq, SQ_STATE) = 4;
            aiperc_2593E4(sq);
        } else {
            aiperc_2591B4(sq);
        }
        break;
    case 2:
        aiperc_259498(sq);
        break;
    case 4:
        aiperc_2595C0(sq);
        break;
    }
}

/* 0x2598C8 (unreferenced) */
void aiperc_2598C8(void *sqv)
{
    C1_U8(sqv, SQ_MOVING) = 0;
}

/* 0x2598D0 */
void aiperc_2598D0(void *sqv)
{
    uchar *sq = sqv;

    C1_U8(sq, SQ_MOVING) = 1;
    aiperc_2539F0(sq);
    if (C1_U32(sq, 0xEB) + 20000 < TIME || (TIME < 20000 && C1_U32(sq, 0xEB) == 0)) {
        C1_U32(sq, 0x100) = 0;
        C1_U32(sq, 0xF3) = 0;
        C1_U32(sq, 0xF7) = 0;
        C1_U32(sq, 0xFC) = 500;
        C1_U8(sq, 0xFB) = 0;
        C1_U8(sq, 0x10E) = 1;
        C1_U8(sq, 0x10F) = 1;
        C1_U8(sq, 0xE9) = 1;
        C1_U8(sq, 0xEA) = 1;
        C1_U32(sq, 0xEF) = TIME;
        C1_U32(sq, 0x104) = TIME;
        C1_U32(sq, 0x108) = TIME;
        C1_U8(sq, 0x10C) = 0;
        if (C1_U8(sq, SQ_SIDE) == 2 && !aimain_24A760())
            aimain_24A73C();
    }
    C1_U32(sq, 0xEB) = TIME;
    aiperc_257AB4(sq);
}

/* 0x2599A4 */
void aiperc_2599A4(void *sqv)
{
    uchar *sq = sqv;
    uchar *mv = sq + SQ_MOVE;
    uchar *b = FIRST_BRAIN(sq);
    ulong r = C1_U8(sq, 0x127 + C1_U8(sq, 0x12B));
    long d = fix_fast_pyth_dist(C1_S16(sq, SQ_DEST) - C1_S16(b, BR_POS),
                                C1_S16(sq, SQ_DEST + 2) - C1_S16(b, BR_POS + 2));

    if (d <= (C1_U8(FIRST_BRAIN(sq), BR_CLASS) == 4 ? 10 : 2)) {
        uchar i = ++C1_U8(sq, 0x12B);
        if (i == 4 || C1_U8(sq, 0x127 + C1_U8(sq, 0x12B)) == 0xFF)
            C1_U8(sq, 0x12B) = 0;
        r = C1_U8(sq, 0x127 + C1_U8(sq, 0x12B));
    }
    mv[0] = 2;
    C1_S16(mv, 1) = (short)(*(fix *)&game_4413D0[r * 8] >> 16);
    C1_S16(mv, 3) = (short)(*(fix *)&game_4413D0[r * 8 + 4] >> 16);
    C1_U8(sq, SQ_MOVING) = 0;
    form_reset(sq);
}

/* 0x259AA4 */
void aiperc_259AA4(void *sqv)
{
    uchar *sq = sqv;
    uchar *mv = sq + SQ_MOVE;
    ulong r = C1_U8(sq, 0x12C + C1_U8(sq, 0x14C));

    if (C1_U32(sq, 0x123) & (1UL << (r & 31))) {
        uchar i;
        misobj_2C9EE8(r);
        i = ++C1_U8(sq, 0x14C);
        if (i == 0x10 || C1_U8(sq, 0x12C + C1_U8(sq, 0x14C)) == 0xFF) {
            /* the list is done: hold here */
            C1_U8(sq, 0x14C) = 0x10;
            mv[0] = 1;
            C1_U32(mv, 1) = C1_U32(sq, SQ_X);
            C1_U8(sq, SQ_MOVING) = 0;
            form_reset(sq);
            aimain_249F6C(sq + SQ_ORDERS, 0x80);
            if (C1_S16(sq, SQ_ATTACK) != 0)
                return;
            if (C1_U8(sq, SQ_NBRAINS) > 1)
                aimain_24A6A0(aiperc_2575C0(sq), 10);
            else
                aimain_24A6A0(aiperc_2575C0(sq), 9);
            return;
        }
        r = C1_U8(sq, 0x12C + C1_U8(sq, 0x14C));
    }
    mv[0] = 2;
    C1_S16(mv, 1) = (short)(*(fix *)&game_4413D0[r * 8] >> 16);
    C1_S16(mv, 3) = (short)(*(fix *)&game_4413D0[r * 8 + 4] >> 16);
    C1_U8(sq, SQ_MOVING) = 0;
    form_reset(sq);
}

/* 0x259BE8 */
void aiperc_259BE8(void *sqv)
{
    uchar *sq = sqv;
    Msg *m;

    if ((ulong)((100 - C1_U8(sq, SQ_SKILL)) * 100 + 2000) + C1_U32(sq, 0xD1) >= TIME)
        return;
    aiperc_258908(sq);
    if (C1_U8(sq, SQ_BE) != 0 && (m = MsgAlloc()) != NULL) {
        m->pad2 = 0x63;
        *(ulong *)&m->from = C1_U32(sq, SQ_ADDR);
        if (aimsg_25A150(m))
            aimsg_25A22C(sq);
        else
            aiperc_258EEC(sq);
    }
    C1_U32(sq, 0xD1) = TIME;
}

/* 0x259C70 */
void aiperc_259C70(void *sqv)
{
    uchar *sq = sqv;
    uchar *mv = sq + SQ_MOVE;
    uchar kind;

    if (C1_U8(sq, 0x169) != 0) {
        ulong old;
        C1_U8(sq, 0x169) = 0;
        old = C1_U32(sq, SQ_DEST);
        C1_S16(sq, SQ_DEST) = -1;
        C1_S16(sq, SQ_DEST + 2) = -1;
        aiperc_2573C0(sq, old);
    }
    kind = C1_U8(sq, SQ_ORDERS);
    switch (kind) {
    case 0:
        mv[0] = 0;
        break;
    case 1:
        mv[0] = 1;
        C1_U32(mv, 1) = C1_U32(sq, SQ_X);
        break;
    case 0x20:
        mv[0] = 2;
        C1_U32(mv, 1) = C1_U32(sq, SQ_ORDERS + 3);
        break;
    case 8:
        mv[0] = 4;
        if (C1_U8(sq, 0x110) == 2)
            aiperc_258EEC(sq);
        else
            aiperc_257B18(sq);
        return;
    case 0x10:
        mv[0] = 3;
        if (C1_U8(sq, 0x110) == 2)
            aiperc_258EEC(sq);
        else
            aiperc_258F98(sq);
        return;
    case 0x40:
        aiperc_2599A4(sq);
        return;
    case 0x80:
        aiperc_259AA4(sq);
        return;
    }
    /* 0, 1, 0x20 and unknown kinds */
    C1_U8(sq, SQ_MOVING) = 0;
    if (C1_U8(sq, SQ_FORM) == 2 && C1_U8(FIRST_BRAIN(sq), BR_CLASS) == 1)
        C1_U8(sq, SQ_FORM) = 1;
}

/* 0x259E3C */
void aiperc_259E3C(long squad)
{
    uchar s = (uchar)squad;
    uchar *sq = C1_SQ(s);
    uchar *side = (uchar *)&game_sides[C1_U8(sq, SQ_SIDEIDX)];
    uchar c;

    C1_U8(sq, SQ_ACTIVE) = 0;
    MsgFlush((MsgAddr *)(sq + SQ_ADDR));
    c = side[6];
    if (c == s) {
        side[6] = C1_U8(sq, SQ_NEXT);
    } else {
        while (C1_U8(C1_SQ(c), SQ_NEXT) != s)
            c = C1_U8(C1_SQ(c), SQ_NEXT);
        C1_U8(C1_SQ(c), SQ_NEXT) = C1_U8(sq, SQ_NEXT);
    }
    aimain_24A158((uchar *)C1_U32(sq, SQ_PATH));
    if (C1_U8(sq, SQ_INUSE) == 0) {
        /* back on the free list headed by squad 0 */
        C1_U8(sq, SQ_NEXT) = C1_U8(C1_SQ(0), SQ_NEXT);
        C1_U8(C1_SQ(0), SQ_NEXT) = s;
    }
    aiperc_253E60(s, 0);
    aiglue_2539BC(s);
    aimsg_25A130(s);
}

/* 0x259F3C */
void aiperc_259F3C(long squad, long ent)
{
    uchar s = (uchar)squad;
    short e = (short)ent;
    uchar *sq, *t;
    Msg *m;

    if (s == 0)
        return;
    sq = C1_SQ(s);
    if (C1_U8(sq, SQ_ACTIVE) == 0)
        return;
    if (e == C1_S16(sq, SQ_ATTACK)) {
        if (C1_S16(sq, SQ_ATTACK) != 0 && C1_U8(sq, 0x157) <= 3) {
            /* the helper squad (+0x163) is asked to take over the target */
            t = C1_SQ(C1_U8(sq, 0x163));
            C1_U8(sq, 0x157) = 0;
            if (C1_U8(t, SQ_ACTIVE) != 0 && s != C1_U8(sq, 0x163) && C1_U8(t, 0x157) == 0
                && (m = MsgAlloc()) != NULL) {
                uchar br;
                C1_U8(sq, 0x157) = 4;
                br = aiperc_2575C0(C1_SQ(C1_U8(sq, 0x163)));
                aiacc_25AEF0(m, C1_U32(sq, SQ_ADDR), C1_U32(sq, SQ_ADDR), C1_S16(C1_BR(br), BR_ENT), 0);
                m->flag = 6;
                MsgSend(m);
            }
        }
        C1_S16(sq, SQ_ATTACK) = 0;
        C1_U8(sq, 0xAF) = 0;
    }
    sq = C1_SQ(s);
    if (e == C1_S16(sq, SQ_BF)) {
        C1_U8(sq, SQ_C1) = 0;
        C1_U8(sq, SQ_C2) = 0;
        C1_U8(sq, SQ_BE) = 0;
        C1_S16(sq, SQ_BF) = 0;
    }
}

/* 0x25A0B0 */
void aiperc_25A0B0(long squad)
{
    uchar s = (uchar)squad;
    uchar i;

    if (s == 0)
        return;
    for (i = 0; i < 0x20; i++)
        if (C1_U8(C1_SQ(i), SQ_NEXT) == s)
            C1_U8(C1_SQ(i), SQ_NEXT) = C1_U8(C1_SQ(s), SQ_NEXT);
    C1_U8(C1_SQ(s), SQ_INUSE) = 1;
}
