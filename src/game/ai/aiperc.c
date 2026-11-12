/* aiperc.c -- Terra Nova squad AI: perception, messages and behaviour, first part (module M009
 * 0x2539F0-0x258908; the rest, 0x258908-0x25A110, is g4c1's aisqbhv.c).  The original source
 * file name is not known (no assert string in the module).  Decompiled from __FF.EXE v1.09.
 *
 * Squads (0x170-byte records, game_squads) and brains (0x14E, game_brains) are reached through
 * byte offsets (include/g4_b.h, g4_a.h, g4_c1.h).  A "point" is a map square {short x, y} kept
 * in one dword.
 *
 *   0x2539F0 aiperc_2539F0  can the squad react to a message (ends a finished hold order first)
 *   0x253AE8 aiperc_253AE8  create a squad (from the free list or a given index)
 *   0x253E60 aiperc_253E60  move a squad into another squad's ring (+0x165 / +0x166)
 *   0x253F70 aiperc_253F70  squad message handler, messages from the mission script
 *   0x25486C aiperc_25486C  squad message handler, messages from its brains and the player
 *                           (0x254848 = its jump tables; 0x2555DC, 0x2555F5, 0x255624,
 *                           0x255663 = case labels of its message 0x21 switch)
 *   0x255798 aiperc_255798  a moving squad complains when its brains' average drops
 *   0x255858 aiperc_255858  a helping squad (+0x157 1 or 2) starts waiting (state 3)
 *   0x2558C4 aiperc_2558C4  a squad sent to a point (+0x159) arrives
 *   0x25597C aiperc_25597C  a brain sent to help another squad reports
 *   0x255CE4 aiperc_255CE4  first frame of a mission: the player's squad greets
 *   0x255E88 aiperc_255E88  combat chatter: casualties, enemy down, fight over
 *   0x25607C aiperc_25607C  brains of the squads around (near / coming / enemy)
 *   0x2561B4 aiperc_2561B4  the nearest other squad of the same side within 80
 *   0x25626C aiperc_25626C  every 4 s the speaker brain shouts (pbasim_28B778)
 *   0x2562B4 aiperc_2562B4  side 2 squads on the move: hold, retreat or attack
 *   0x2563D0 aiperc_2563D0  one squad's think: messages, chatter, then the current order
 *   0x2566CC aiperc_2566CC  message 0x61 (from each squad) to the AI script
 *   0x256710 aiperc_256710  reset the path step counters (+0x9A..+0xA3)
 *   0x256734 aiperc_256734  rebuild the mask of the squad's AI brains
 *   0x2567A4 aiperc_2567A4  "new orders" message to every brain of the squad
 *   0x256814 aiperc_256814  forget the attack target and the region list
 *   0x256888 aiperc_256888  region list: skip to the best region
 *   0x2569E4 aiperc_2569E4  follow the attack target (+0xA9)
 *   0x256C90 aiperc_256C90  move the squad one path step towards its destination
 *   0x2571D8 aiperc_2571D8  squad position from its brains
 *   0x257344 aiperc_257344  is pt a useful new destination
 *   0x2573C0 aiperc_2573C0  set the squad's destination, kept inside its region
 *   0x257518 aiperc_257518  message (aiacc_25B074) to every brain of the squad
 *   0x257588 aiperc_257588  the first live AI brain from brain c on
 *   0x2575C0 aiperc_2575C0  a random live AI brain of the squad (the speaker)
 *   0x257628 aiperc_257628  sum of the brains' strength (ailow_250668)
 *   0x257664 aiperc_257664  side 1: soldiers with weapon kind 8 fire
 *   0x2576D4 aiperc_2576D4  side 1: call an air strike on the target
 *   0x257820 aiperc_257820  engage the known enemy squad (+0xBE)?
 *   0x2579E0 aiperc_2579E0  forget the enemy squad, drop the attack orders
 *   0x257AB4 aiperc_257AB4  tell the side where the squad is
 *   0x257B18 aiperc_257B18  attack the squad of order +0x33, or go where an enemy was seen
 *   0x257C20 aiperc_257C20  brain is a vehicle / installation the cheat flags hide
 *   0x257CB8 aiperc_257CB8  can the squad take a brain as an enemy
 *   0x257D18 aiperc_257D18  qsort comparison (aiperc_257D38's list)
 *   0x257D38 aiperc_257D38  enemies in range that a random check lets the squad see
 *   0x25804C aiperc_25804C  scale the near-list radii of the squad's units
 *   0x2580CC aiperc_2580CC  distance from pt to the squad
 *   0x258170 aiperc_258170  pick the enemies the squad sees, count the vehicles seen
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_b.h"
#include "g8_a.h"     /* added at the G5-G8 merge */
#include "g8_b.h"     /* added at the G5-G8 merge */
#include "g9_c.h"     /* added at the G9/G10 merge */

#define U8(p, o)  (*(uchar *)((uchar *)(p) + (o)))
#define S8(p, o)  (*(signed char *)((uchar *)(p) + (o)))
#define U16(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define S16(p, o) (*(short *)((uchar *)(p) + (o)))
#define U32(p, o) (*(ulong *)((uchar *)(p) + (o)))
#define S32(p, o) (*(long *)((uchar *)(p) + (o)))
#define BRAIN(i)  ((uchar *)&game_brains[(uchar)(i)])
#define SQUAD(i)  ((uchar *)&game_squads[(uchar)(i)])
#define SIDE(i)   ((uchar *)&game_sides[(signed char)(i)])
#define EN(i)     (game_entities[(short)(i)])
#define ENU(i)    (game_entities[(ushort)(i)])
#define BENT(b)   S16(b, 0xB)
#define SQC(w)    ((long)(((ulong)(long)(short)(w) << 16) | 0x8000))
#define FIXINT(f) ((long)(f) >> 16)
#define RND()     RndRange(game_rnd_text, 0, 0x7FFF)
#define ORD(sq, k) ((uchar *)(sq) + 0x24 + (k) * 0x24)
#define LEADER_ENT(sq) BENT(BRAIN(U8(sq, 0xF)))

static long iabs(long v) { return v < 0 ? -v : v; }

/* fix_atan2 of a vector, scaled down by 128 when a component is above 0x800000 */
static fixang atan2_scaled(long dx, long dy)
{
    if (iabs(dx) > 0x800000 || iabs(dy) > 0x800000) {
        dx >>= 7;
        dy >>= 7;
    }
    return fix_atan2(dy, dx);
}

/* TRUE if the fine map cell (x, y) is passable (terrain kind not in terrain_bitmask) */
static uchar passable(short x, short y)
{
    uchar *row = *(uchar **)(game_map_35BFB4 + 0x84E + (long)y * 4);
    uchar kind = game_41DD58[row[(long)x * 3] & 0x3F];

    return ((1UL << (kind & 31)) & terrain_bitmask) == 0;
}

/* tell the squad's leader brain to say something */
static void say(uchar *sq, long what)
{
    aimain_24A6A0(aiperc_2575C0(sq), what);
}

/* ------------------------------------------------------------------ 0x2539F0 */
uchar aiperc_2539F0(void *squad)
{
    uchar *sq = squad;
    short e;
    uchar c, t;

    if (sq[0xCB] == 6 && *(uchar *)U32(sq, 0x95)) {
        long k;

        aimain_249F6C(sq + 0x24, 0xE0);
        U16(sq, 0xA9) = 0;
        if (sq[0x157] == 2)
            sq[0x157] = 0;
        *(uchar *)U32(sq, 0x95) = 0;
        sq[0xCB] = 4;
        k = aimain_24A0B8(sq + 0x24);
        ORD(sq, k)[0] = 1;
        ORD(sq, k)[1] = 0;
        ORD(sq, k)[2] = 0;
        U32(sq, 0x104) = game_time - 4000;
    }
    e = LEADER_ENT(sq);
    c = EN(e).cls;
    if (c == 2) {
        t = EN(e).type;
        if (t == 0 || t == 1 || t == 2 || t == 8 || t == 7 || t == 3)
            return 0;
    }
    if (sq[0xC9] == 2)
        return 0;
    if (sq[0x157])
        return 0;
    return 1;
}

/* ------------------------------------------------------------------ 0x253AE8 */
uchar aiperc_253AE8(long a, ulong xy, long side)
{
    ulong idx = (uchar)a ? (uchar)a : SQUAD(0)[0xE];
    uchar c = (uchar)side;
    uchar *sq, *sd;
    uchar *p;

    if (idx == 0)
        return 0;
    sq = SQUAD(idx);
    if (sq[1] == 0)
        SQUAD(0)[0xE] = sq[0xE];
    U16(sq, 3) = 0x81;
    U16(sq, 5) = (ushort)idx;
    sq[0] = 1;
    sq[2] = 0;
    sq[7] = c;
    sq[0xD] = c;
    sd = SIDE(c);
    sq[0xE] = sd[6];
    sd[6] = (uchar)idx;
    U16(sq, 0x12) = (ushort)xy;
    U16(sq, 0x14) = (ushort)(xy >> 16);
    sq[0xF] = 0;
    sq[0x10] = 0;
    sq[0x11] = 0;
    S32(sq, 0x16) = SQC(xy);
    sq[0xC] = 0;
    sq[0x1E] = 1;
    S32(sq, 0x1A) = SQC(xy >> 16);
    sq[0x166] = (uchar)idx;
    sq[0x165] = (uchar)idx;
    aimain_249F48(sq + 0x24);
    U16(sq, 0x91) = U16(sq, 0x12);
    U16(sq, 0x93) = U16(sq, 0x14);
    sq[0x90] = 0;
    U32(sq, 0xEB) = 0;
    sq[0x99] = 0;
    sq[0x9A] = 0;
    U32(sq, 0x9C) = 0;
    sq[0x9B] = 0;
    U32(sq, 0xA0) = 0;
    U16(sq, 0xA4) = 0;
    sq[0xA6] = 0;
    p = aimain_24A120();
    U32(sq, 0x95) = (ulong)p;
    *p = 0;
    aiperc_256814(sq, 0);
    sq[0xAF] = 0;
    U32(sq, 0xB0) = 0;
    sq[0xB4] = 0;
    U32(sq, 0xB5) = 0;
    sq[0xB9] = 0;
    U32(sq, 0xBA) = 0;
    sq[0xC1] = 0;
    sq[0xC2] = 0;
    sq[0xBE] = 0;
    U16(sq, 0xBF) = 0;
    U16(sq, 0xC5) = 0xFFFF;
    U16(sq, 0xC7) = 0xFFFF;
    U32(sq, 0x116) = 5;
    U32(sq, 0xD1) = 0;
    if (c == 1) {
        sq[0xCB] = 8;
        sq[0xC9] = 1;
    } else {
        sq[0xCB] = 1;
        sq[0xC9] = 1;
    }
    sq[0xCC] = 0;
    sq[0xCA] = 0;
    U32(sq, 0xCD) = 0;
    sq[0xD9] = 0x32;
    sq[0xE8] = 0;
    sq[0xE9] = 0;
    sq[0xEA] = 0;
    U32(sq, 0xF3) = 0;
    U32(sq, 0xF7) = 0;
    U32(sq, 0x104) = 0;
    U32(sq, 0x108) = 0;
    U32(sq, 0x100) = 0;
    U32(sq, 0xD5) = 0;
    sq[0x10D] = 0;
    sq[0x10E] = 0;
    sq[0x10F] = 0;
    sq[0x10C] = 0;
    sq[0x110] = 0;
    sq[0x115] = 0;
    U32(sq, 0x111) = 0;
    sq[0x11A] = 0;
    sq[0x11B] = 0;
    U32(sq, 0xDE) = 0;
    sq[0xE2] = 0;
    U16(sq, 0xE4) = 10;
    sq[0xE6] = 1;
    sq[0x122] = 0;
    U32(sq, 0x123) = 0;
    sq[0x157] = 0;
    sq[0x120] = 0;
    sq[0x121] = 0;
    sq[0x155] = 1;
    sq[0xE7] = 0;
    sq[0x156] = 100;
    sq[0x164] = 0;
    U16(sq, 0x14F) = 0;
    sq[0x167] = 0;
    sq[0x168] = 0;
    sq[0x169] = 0;
    lg_memset(sq + 0x127, 0xFFFFFFFF, 0, 4);
    sq[0x12B] = 0xFF;
    sq[0x14C] = 0xFF;
    return (uchar)idx;
}

/* ------------------------------------------------------------------ 0x253E60 */
void aiperc_253E60(long squad, long other)
{
    uchar a = (uchar)squad, b = (uchar)other;
    uchar *A, *B;
    uchar n;

    if (a == 0 || a == b)
        return;
    A = SQUAD(a);
    n = A[0x166];
    if (a != n) {                       /* unlink from the old ring */
        SQUAD(A[0x165])[0x166] = n;
        SQUAD(n)[0x165] = A[0x165];
    }
    if (b == 0) {
        A[0x166] = a;
        A[0x165] = a;
    } else {
        B = SQUAD(b);
        n = B[0x166];
        A[0x166] = n;
        A[0x165] = b;
        B[0x166] = a;
        SQUAD(n)[0x165] = a;
    }
}

/* put an order of kind 8 (attack squad s) / 0x10 (attack entity e) into the squad's queue */
static uchar *new_order(uchar *sq)
{
    return ORD(sq, aimain_24A0B8(sq + 0x24));
}

/* ------------------------------------------------------------------ 0x253F70 */
uchar aiperc_253F70(void *squad, Msg *m)
{
    uchar *sq = squad;
    uchar *mb = (uchar *)m;

    switch (mb[2]) {
    case 0x0B: {                                /* attack a squad (script) */
        uchar a, b;
        long k;
        uchar *o;
        Msg *m2;

        if (!aiperc_2539F0(sq) || !sq[0x155])
            return 1;
        sq[0xE2] = 0;
        music_2469E4(2, 1);
        m2 = MsgAlloc();
        if (m2) {
            m2->pad2 = 0x62;
            U32(m2, 8) = U32(sq, 3);
            if (aimsg_25A150(m2)) {
                aimsg_25A22C(sq);
                return 1;
            }
        }
        k = aimain_24A0B8(sq + 0x24);
        aiacc_25ADF8(m, &a, &b);
        o = ORD(sq, k);
        o[0] = 8;
        o[1] = mb[3];
        o[0xF] = a;
        if (b)
            o[0x12] = b;
        return 1;
    }
    case 0x1D: {                                /* attack a squad */
        uchar s, v;
        uchar *o;

        sq[0xCB] = 1;
        sq[0xC9] = 1;
        sq[0x157] = 0;
        if (!aiperc_2539F0(sq) || !sq[0x155])
            return 1;
        sq[0xE2] = 0;
        aiacc_25ADF8(m, &s, &v);
        if (sq[7] == SQUAD(s)[7]) {
            if (v)
                say(sq, 0x5C);
            return 1;
        }
        music_2469E4(2, 1);
        if (v)
            say(sq, 0xE);
        o = new_order(sq);
        o[0] = 8;
        o[1] = mb[3];
        o[0xF] = s;
        sq[0x115] = 0;
        if (v)
            o[0x12] = v;
        return 1;
    }
    case 0x1E: {                                /* attack an entity */
        short e;
        uchar v, t;
        uchar *lead, *o;

        sq[0xCB] = 1;
        sq[0xC9] = 1;
        sq[0x157] = 0;
        aiacc_25B034(m, &e, &v);
        if (EN(LEADER_ENT(sq)).cls == 2 && EN(LEADER_ENT(sq)).type == 8) {
            lead = BRAIN(sq[0xF]);
            ailow_252758(lead, e);
        }
        if (!aiperc_2539F0(sq) || !sq[0x155])
            return 1;
        sq[0xE2] = 0;
        t = ENU(e).f21;
        if (!t) {
            if (v)
                say(sq, 0x5C);
            return 1;
        }
        if (sq[7] == BRAIN(t)[5]) {
            if (v)
                say(sq, 0xF);
            return 1;
        }
        music_2469E4(2, 1);
        if (e == 0) {
            sq[0xC2] = 0;
            if (v)
                say(sq, 0xC);
            return 1;
        }
        if (v)
            say(sq, 0xE);
        o = new_order(sq);
        o[0] = 0x10;
        o[1] = 9;
        U16(o, 0xF) = e;
        sq[0x115] = 0;
        if (v)
            o[0x12] = v;
        return 1;
    }
    case 0x22: {
        short a, b, c;

        aiacc_25AF58(m, &a, &b, &c);
        sq[0xE2] = 1;
        U32(sq, 0xDE) = game_time + (ushort)b;
        sq[0xE3] = (uchar)a;
        return 1;
    }
    case 0x25: {                                /* keep inside a region */
        uchar a;

        aiacc_25AC64(m, &a);
        if ((signed char)a > 0) {
            if (!misobj_2C9D50((signed char)a, S32(sq, 0x16), S32(sq, 0x1A)))
                return 1;
            sq[0x120] = 1;
            sq[0x121] = a;
        } else
            sq[0x120] = 0;
        return 1;
    }
    case 0x26: {
        uchar a;

        aiacc_25AC64(m, &a);
        sq[0x155] = a;
        if (a)
            sq[0xE2] = 0;
        return 1;
    }
    case 0x27:
        sq[0xE2] = 0;
        say(sq, 0x39);
        aiperc_258CE8(sq);
        return 1;
    case 0x28: {                                /* skill */
        uchar a;

        aiacc_25AC64(m, &a);
        sq[0x156] = a > 100 ? 100 : a;
        return 1;
    }
    case 0x29: {                                /* a move order reached its point */
        ulong pos;
        long k;

        aiacc_25ACCC(m, &pos);
        aimain_249FD0(sq + 0x24);
        k = aimain_24A088(sq + 0x24, 0x20);
        if (k >= 3)
            return 1;
        if (pos != U32(ORD(sq, k), 3))
            return 1;
        ORD(sq, k)[0] = 1;
        ORD(sq, k)[1] = 0;
        ORD(sq, k)[2] = 0;
        if (k == 0)
            *(uchar *)U32(sq, 0x95) = 0;
        return 1;
    }
    case 0x2A: {                                /* attack entity e */
        short e;
        Msg *m2;

        aiacc_25B1CC(m, &e);
        if (e == 0)
            return 1;
        sq[0xAF] = 1;
        m2 = MsgAlloc();
        if (!m2)
            return 1;
        aiacc_25AEF0(m2, U32(sq, 3), U32(sq, 3), e, 0);
        m2->flag = 6;
        MsgSend(m2);
        return 1;
    }
    case 0x2B:
        sq[0xAF] = 0;
        U16(sq, 0xA9) = 0;
        if (sq[0x157] == 2)
            sq[0x157] = 0;
        return 1;
    case 0x2C: {
        uchar a;

        aiacc_25AC64(m, &a);
        sq[0x164] = a;
        return 1;
    }
    case 0x2D: {
        uchar a;

        aiacc_25AC64(m, &a);
        aiperc_25804C(sq, a);
        return 1;
    }
    case 0x2E:
        return 1;
    case 0x2F: {
        uchar a;

        aiacc_25AC64(m, &a);
        aiperc_253E60(sq[5], a);
        return 1;
    }
    case 0x30: {                                /* aggression */
        uchar a;

        aiacc_25AC64(m, &a);
        if (a > 0x32) {
            sq[0xCB] = 1;
            sq[0xC9] = 1;
        } else {
            sq[0xCB] = 2;
            sq[0xC9] = 2;
        }
        sq[0xD9] = a;
        return 1;
    }
    case 0x65:
        sq[0xE2] = 0;
        aimsg_25A7C8(sq, m);
        return 1;
    case 0x67: {
        long v;
        Msg *m2;

        sq[0xE2] = 0;
        aiacc_25B27C(m, &v);
        m2 = MsgAlloc();
        if (!m2)
            return 1;
        aiacc_25B294(m2, U32(sq, 3), U32(sq, 3), v);
        if (aimsg_25A150(m2))
            aimsg_25A22C(sq);
        return 1;
    }
    case 0x69: {                                /* destroy the squad's units */
        uchar c = sq[0xF];

        sq[0xE2] = 0;
        while (c) {
            if (!BRAIN(c)[0xA])
                obj_destroy(BENT(BRAIN(c)), 1, 0);
            c = BRAIN(c)[7];
        }
        return 1;
    }
    case 0x6A: {
        Msg *m2;

        sq[0xE2] = 0;
        m2 = MsgAlloc();
        if (!m2)
            return 1;
        aiacc_25B2BC(m2, U32(sq, 3), U32(sq, 3));
        if (aimsg_25A150(m2))
            aimsg_25A22C(sq);
        return 1;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ 0x25486C
 * squad message handler, messages from the squad's own brains and the player.  The map's
 * aiperc_254848 (two jump tables) and aiperc_2555DC / 2555F5 / 255624 / 255663 (case labels of
 * the message 0x21 switch) are parts of this function. */
uchar aiperc_25486C(void *squad, Msg *m)
{
    uchar *sq = squad;
    uchar *mb = (uchar *)m;

    switch (mb[2]) {
    case 0x00: {                                /* move to a point */
        ulong pos;
        uchar v, cls, mode, r;
        long k;
        uchar *o, *rec;

        aiacc_25AC98(m, &pos, &v);
        if ((short)pos < 10 || (short)pos > 0x1F6 || (short)(pos >> 16) < 10 || (short)(pos >> 16) > 0x1F6) {
            if (v)
                say(sq, 0xB);
            return 1;
        }
        if (sq[7] == 2 && RND() % 256 <= 0x40) {
            Msg *m2 = MsgAlloc();
            if (m2) {
                aiacc_25AC6C(m2, U32(sq, 3), U32(sq, 3), &pos, v);
                m2->flag = mb[3];
                MsgSend(m2);
                return 1;
            }
        }
        if (sq[0xCB] == 4)
            sq[0xCB] = 1;
        if (sq[0xCB] == 6)
            sq[0xCB] = 1;
        if (v)
            sq[0x157] = 0;
        if (sq[0x90] == 1) {
            if (sq[0xCC] == 0 && sq[0xCB] != 8)
                sq[0xCC] = sq[0xCB];
            sq[0xCB] = 5;
        }
        sq[0x120] = 0;
        if (U32(sq, 0x12) == pos) {
            if (v)
                say(sq, 1);
            return 1;
        }
        aiperc_2573C0(sq, pos);
        cls = BRAIN(sq[0xF])[0xD];
        if (cls == 4 || cls == 3)
            return 1;
        mode = ailow_253580(BRAIN(sq[0xF]));
        r = path_25DDEC(U32(sq, 0x12), U32(sq, 0x91), sq[0xA6], (void *)U32(sq, 0x95), 0, mode);
        if (r) {
            if (BRAIN(sq[0xF])[0xD] == 1)
                sq[0x1E] = sq[0x90] == 0 ? 1 : 2;
            rec = (uchar *)U32(sq, 0x95);
            U32(rec, 1) = U32(sq, 0x91);
            *(uchar *)U32(sq, 0x95) = 1;
            sq[0x99] = 0;
            if (sq[0xCB] == 8)
                sq[0xCB] = 1;
        } else {
            sq[0x99] = 1;
            U32(sq, 0x91) = U32(sq, 0x12);
        }
        if (v)
            aimain_249F6C(sq + 0x24, 0xFF);
        if (sq[0x99]) {
            if (v)
                say(sq, 0xB);
            return 1;
        }
        aiperc_256814(sq, 1);
        k = aimain_24A0B8(sq + 0x24);
        o = ORD(sq, k);
        o[0] = 0x20;
        U32(o, 3) = pos;
        o[1] = sq[0x90] == 1 ? 6 : mb[3];
        if (v && sq[0x90] != 1)
            ORD(sq, k)[0x12] = v;
        else
            ORD(sq, k)[0x12] = 0;
        if (v)
            say(sq, 6);
        return 1;
    }
    case 0x01: {
        uchar a;

        aiacc_25AC64(m, &a);
        if (U32(sq, 0x9C) & (1UL << (a & 31)))
            return 1;
        sq[0x9A]++;
        U32(sq, 0x9C) |= 1UL << (a & 31);
        return 1;
    }
    case 0x02: {
        uchar a;

        aiacc_25AC64(m, &a);
        a &= 0x1F;
        if (U32(sq, 0xA0) & (1UL << a))
            return 1;
        U32(sq, 0xA0) |= 1UL << a;
        sq[0x9B]++;
        return 1;
    }
    case 0x04:
        if (U16(sq, 0xA9))
            aiperc_2569E4(sq);
        return 1;
    case 0x05: {                                /* hold */
        uchar v;
        long k;

        aiacc_25AC64(m, &v);
        if (v)
            say(sq, 0xD);
        if (v)
            aimain_249F6C(sq + 0x24, 0xFF);
        k = aimain_24A0B8(sq + 0x24);
        ORD(sq, k)[0] = 1;
        ORD(sq, k)[1] = mb[3];
        if (sq[0x90] == 1) {
            if (sq[0xCC] == 0)
                sq[0xCC] = sq[0xCB];
            sq[0xCB] = 4;
            U32(sq, 0xDA) = U32(sq, 0x12);
        }
        if (sq[0xCB] == 6)
            sq[0xCB] = 1;
        U16(sq, 0xA9) = 0;
        if (sq[0x157] == 2)
            sq[0x157] = 0;
        if (v)
            ORD(sq, k)[0x12] = v;
        return 1;
    }
    case 0x06: {
        uchar a;

        aiacc_25AC64(m, &a);
        sq[0xE8] = 1;
        return 1;
    }
    case 0x0D: {
        uchar a, b;
        ulong bit;

        aiacc_25ADF8(m, &a, &b);
        a &= 0x1F;
        bit = 1UL << a;
        if (b) {
            if (U32(sq, 0xB5) & bit)
                return 1;
            sq[0xB4]++;
            U32(sq, 0xB5) |= bit;
        } else {
            if (!(U32(sq, 0xB5) & bit))
                return 1;
            U32(sq, 0xB5) &= ~bit;
            sq[0xB4]--;
        }
        return 1;
    }
    case 0x0E: {
        uchar a, b;
        ulong bit;

        aiacc_25ADF8(m, &a, &b);
        a &= 0x1F;
        bit = 1UL << a;
        if (b) {
            if (U32(sq, 0xBA) & bit)
                return 1;
            sq[0xB9]++;
            U32(sq, 0xBA) |= bit;
        } else {
            if (!(U32(sq, 0xBA) & bit))
                return 1;
            sq[0xB9]--;
            U32(sq, 0xBA) &= ~bit;
        }
        return 1;
    }
    case 0x0F: {                                /* an enemy brain was seen */
        uchar br, x, f;
        uchar *b;
        long k;

        if (sq[0xC9] == 2) {
            sq[0xC9] = 1;
            sq[0xCA] = 2;
        }
        if (!aiperc_2539F0(sq) || !sq[0x155])
            return 1;
        aiacc_25ADF8(m, &br, &x);
        b = BRAIN(br);
        if (b[0] == 0 && b[0xA] == 0)
            return 1;
        if (sq[7] == b[5])
            return 1;
        if (sq[0x90] == 1)
            return 1;
        if (U16(b, 0xB) != 0)
            f = 1;
        else
            f = RND() % 256 <= 0x10;
        if (!ailow_2508D0(LEADER_ENT(sq), f))
            return 1;
        k = aimain_24A050(sq + 0x24, 8);
        ORD(sq, k)[1] = 8;
        ORD(sq, k)[0x12] = 0;
        ORD(sq, k)[0xF] = BRAIN(br)[6];
        aiperc_2598D0(sq);
        sq[0xC3] = 0;
        sq[0xE8] = 0;
        if (BRAIN(sq[0xF])[0xD] == 1)
            sq[0x1E] = 2;
        ai_371195 = 1;
        if (sq[0xCB] == 8)
            ai_371190 = U32(BRAIN(EN(game_player_entity).f21), 0xE);
        else
            ai_371190 = U32(BRAIN(br), 0xE);
        return 1;
    }
    case 0x15: {                                /* attack an entity */
        short e;
        uchar v;

        aiacc_25AF0C(m, &e, &v);
        if (v && e == S16(BRAIN(v), 0xB)) {
            aimain_24A6A0(v, 0x5C);
            return 1;
        }
        if (sq[0xCB] == 6)
            sq[0xCB] = 1;
        if (game_frame > 0x14 && sq[0x157] == 0 && v)
            say(sq, e == game_player_entity ? 0x12 : 1);
        U16(sq, 0xA9) = e;
        U32(sq, 0xAB) = game_time + 1000;
        if (e) {
            short t = S16(sq, 0xA9);
            long x = FIXINT(EN(t).x), y = FIXINT(EN(t).y);
            Msg *m2 = MsgAlloc();

            if (m2) {
                aiacc_25AD68(m2, U32(sq, 3), U32(sq, 3));
                m2->flag = 6;
                MsgSend(m2);
            }
            sq[0x14C] = 0;
            sq[0x14D] = 1;
            sq[0x12C] = misobj_2C9E60(x, y);
            if (game_player_entity == S16(sq, 0xA9))
                sq[0xCB] = 8;
            else if (sq[0xCB] == 8)
                sq[0xCB] = 1;
            if (v)
                aimain_249F6C(sq + 0x24, 0xFF);
        }
        if (sq[0x157] == 2)
            sq[0x157] = 0;
        return 1;
    }
    case 0x21: {                                /* orders from the player */
        uchar a, v;

        aiacc_25ADF8(m, &a, &v);
        switch (a) {
        case 1:
        case 2:
        case 3:
            sq[0xCB] = a == 1 ? 3 : a == 2 ? 1 : 2;
            if (sq[0xC9] == 3)
                sq[0xC9] = 1;
            if (v)
                say(sq, 0x14);
            break;
        case 4: case 5: case 6: case 7:
            sq[0xCB] = a == 4 ? 7 : a == 5 ? 9 : a == 6 ? 8 : 4;
            if (v)
                say(sq, 1);
            break;
        case 8: case 9: case 10:
            sq[0xC9] = a - 7;
            if (v)
                say(sq, 1);
            break;
        case 11:
            sq[0xCB] = 6;
            sq[0xC9] = 2;
            U32(sq, 0xD5) = game_time;
            if (v)
                say(sq, 1);
            break;
        default:
            break;
        }
        switch (sq[0xCB]) {
        case 3:
            sq[0xC3] = 2;
            aiperc_2573C0(sq, U32(sq, 0xC5));
            break;
        case 4:
            U32(sq, 0xDA) = U32(sq, 0x12);
            U16(sq, 0xA9) = 0;
            if (sq[0x90] == 0 && v)
                aimain_249F6C(sq + 0x24, 0xFF);
            break;
        case 8: {
            Msg *m2;

            U16(sq, 0xA9) = game_player_entity;
            if (v)
                aimain_249F6C(sq + 0x24, 0xFF);
            m2 = MsgAlloc();
            if (m2) {
                aiacc_25AD68(m2, U32(sq, 3), U32(sq, 3));
                m2->flag = 6;
                MsgSend(m2);
            }
            break;
        }
        default:
            break;
        }
        sq[0xC2] = 0;
        sq[0xCC] = 0;
        sq[0xCA] = 0;
        return 1;
    }
    case 0x23: {                                /* patrol regions */
        long k = aimain_24A0B8(sq + 0x24);

        aiacc_25B0D0(m, sq + 0x127, sq + 0x128, sq + 0x129, sq + 0x12A);
        sq[0x12B] = 0;
        ORD(sq, k)[0] = 0x40;
        ORD(sq, k)[0x12] = 0;
        ORD(sq, k)[1] = mb[3];
        if (sq[0xCB] == 6)
            sq[0xCB] = 1;
        return 1;
    }
    case 0x24: {                                /* region list */
        long k = aimain_24A0B8(sq + 0x24);

        U16(sq, 0xA9) = 0;
        if (sq[0x157] == 2)
            sq[0x157] = 0;
        if (sq[0xCB] == 4)
            sq[0xCB] = 1;
        aiacc_25B11C(m, sq + 0x12C);
        sq[0x14C] = 0;
        ORD(sq, k)[0] = 0x80;
        ORD(sq, k)[1] = mb[3];
        ORD(sq, k)[0x12] = sq[0xF];
        say(sq, 6);
        if (sq[0xCB] == 6)
            sq[0xCB] = 1;
        return 1;
    }
    case 0x31: {                                /* split a brain off to help another squad */
        ulong pos;
        short a;
        uchar b, c, d, ns;
        uchar *S;

        aiacc_25AD34(m, &pos, &a, &b, &c, &d);
        if (!b || !c)
            return 1;
        aimain_249F6C(sq + 0x24, 0xFF);
        if (sq[0x10] > 1)
            ns = ailow_253334(b);
        else
            ns = sq[5];
        S = SQUAD(ns);
        if (sq[0xCB] == 6)
            sq[0xCB] = 1;
        S[0x162] = c;
        S[0x161] = d;
        S[0x163] = sq[5];
        S[0x158] = S[0xF];
        if (a) {
            S[0x157] = 2;
            U16(S, 0xA9) = a;
            S[0xCB] = 5;
            *(uchar *)U32(S, 0x95) = 0;
            return 1;
        }
        S[0x157] = 1;
        S[0xCB] = 5;
        U32(S, 0x159) = pos;
        U16(S, 0xA9) = 0;
        aiperc_2573C0(S, pos);
        aiperc_258C7C(S, pos, 9);
        return 1;
    }
    case 0x33:
        sq[0xE7] = 1;
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ 0x255798
 * a moving squad complains when its brains' average (+0x1F) drops */
void aiperc_255798(void *squad)
{
    uchar *sq = squad;
    ulong sum = 0, avg;
    uchar c;

    if (sq[0x90] != 1 || 1 >= sq[0x10])
        return;
    for (c = sq[0xF]; c; c = BRAIN(c)[7])
        if (BRAIN(c)[0])
            sum += U32(BRAIN(c), 0x1F);
    avg = sum / sq[0x10];
    if (avg > U32(sq, 0x116))
        return;
    if (avg <= 3) {
        say(sq, 0x31);
        U32(sq, 0x116) = 0;
        return;
    }
    if (avg <= 4) {
        say(sq, 0x30);
        U32(sq, 0x116) = 3;
    }
}

/* ------------------------------------------------------------------ 0x255858 */
void aiperc_255858(long squad)
{
    uchar *S = SQUAD((uchar)squad);

    if (S[0x157] == 1 || S[0x157] == 2) {
        if (S[0x157] != 1) {
            long k;

            aiperc_257518(S);
            k = aimain_24A0B8(S + 0x24);
            ORD(S, k)[0] = 1;
            ORD(S, k)[1] = 0;
        }
        S[0x157] = 3;
        U32(S, 0x15D) = game_time;
    }
}

/* ------------------------------------------------------------------ 0x2558C4
 * a squad sent to a point (+0x159) arrives */
void aiperc_2558C4(void *squad)
{
    uchar *sq = squad;
    short e;
    long d;

    if (sq[0x157] != 1)
        return;
    e = LEADER_ENT(sq);
    d = iabs(S16(sq, 0x159) - FIXINT(EN(e).x));
    e = LEADER_ENT(sq);
    d += iabs(S16(sq, 0x15B) - FIXINT(EN(e).y));
    if (d <= (U32(BRAIN(sq[0xF]), 0x129) != 0) + 2)
        aiperc_255858(sq[5]);
}

/* ------------------------------------------------------------------ 0x25597C
 * a brain sent to help another squad (+0x163) reports, after +0x161 seconds */
void aiperc_25597C(void *squad)
{
    uchar *sq = squad;
    uchar *H;
    Msg *m2;

    if (sq[0x157] != 3)
        return;
    biped_27E99C(LEADER_ENT(sq), 0);
    if (game_time - U32(sq, 0x15D) <= (ulong)sq[0x161] * 1000)
        return;
    biped_27EA60(LEADER_ENT(sq));
    if (U16(sq, 0xA9) == 0 || EN(S16(sq, 0xA9)).active) {
        uchar *b = BRAIN(sq[0x158]);
        if (b[0])
            mfd_27065C(BENT(b), sq[0x162]);
    }
    H = SQUAD(sq[0x163]);
    if (H[0] == 0 || U16(sq, 5) == sq[0x163] || H[0x157] != 0) {
        sq[0x157] = 0;
        m2 = MsgAlloc();
        if (!m2)
            return;
        aiacc_25AEF0(m2, U32(sq, 3), U32(sq, 3), game_player_entity, 0);
        m2->flag = 6;
        MsgSend(m2);
        return;
    }
    m2 = MsgAlloc();
    if (m2) {
        sq[0x157] = 4;
        aiacc_25AEF0(m2, U32(sq, 3), U32(sq, 3), BENT(BRAIN(aiperc_2575C0(H))), 0);
        m2->flag = 6;
        MsgSend(m2);
    } else
        sq[0x157] = 0;
    if (sq[0x162] != 0x19)
        return;
    {
        long dx = S32(H, 0x16) - S32(sq, 0x16);
        long dy = S32(H, 0x1A) - S32(sq, 0x1A);
        ushort a;
        short pt[2];

        if (fix_fast_pyth_dist(dx, dy) >= 0x60000)
            return;
        a = atan2_scaled(dx, dy);
        pt[0] = (short)(((long)(((long long)fix_cos(a) * 0x60000) >> 16) >> 16) + S16(H, 0x12));
        pt[1] = (short)(((long)(((long long)fix_sin(a) * 0x60000) >> 16) >> 16) + S16(H, 0x14));
        if (!passable(pt[0], pt[1]))
            return;
        m2 = MsgAlloc();
        if (!m2)
            return;
        aiacc_25AC6C(m2, U32(sq, 3), U32(H, 3), pt, 0);
        m2->flag = 7;
        MsgSend(m2);
    }
}

/* ------------------------------------------------------------------ 0x255CE4
 * first frame of a mission: the player's squad (if the biggest of side 1) greets */
void aiperc_255CE4(void *squad)
{
    uchar *sq = squad;
    uchar l = sq[0xF];
    uchar big = 1;
    ulong n, i;
    uchar c, br;
    long r;

    if (game_359708 == 0x1D || game_frame != 0x14 || !l || sq[0x90] || BRAIN(l)[0xA])
        return;
    n = game_time & 0xFF;
    for (i = 0; i < n; i++)
        RND();
    for (c = 0; c < 32; c++) {
        uchar *S = SQUAD(c);
        if (S[7] == 1 && S[0] != 0 && S[0x10] > sq[0x10])
            big = 0;
    }
    if (!big)
        return;
    r = RND() % 100;
    br = aiperc_2575C0(sq);
    if ((short)r > 0x32) {
        switch (game_35CADA) {
        case 0:
            aimain_24A6A0(br, 0x1F);
            break;
        case 1:
            aimain_24A6A0(br, 0x20);
            break;
        case 0xFF:
            if (game_41DE33 == 4 || game_41DE34 == 3)
                aimain_24A6A0(br, 0x21);
            else
                aimain_24A6A0(br, 0x1E);
            break;
        default:
            break;
        }
    } else if (game_41DE34 <= 3)
        aimain_24A6A0(br, 0x1A + game_41DE34);
}

/* ------------------------------------------------------------------ 0x255E88
 * combat chatter: casualties, enemy down, fight over */
void aiperc_255E88(void *squad)
{
    uchar *sq = squad;
    uchar c;

    if (sq[0x10E] && sq[0x10F] && U32(sq, 0x100) != 0) {
        say(sq, sq[0x10C] ? 0x22 : 0x29);
        sq[0x10E] = 0;
    }
    if (sq[0x10D] && U32(sq, 0x100) != 0 && U32(sq, 0x104) + 5000 < game_time) {
        uchar br = aiperc_2575C0(sq);
        if (sq[0x10C] && U32(sq, 0x100) - 1 <= 4)
            aimain_24A6A0(br, 0x24 + U32(sq, 0x100) - 1);
        sq[0x10D] = 0;
    }
    if (sq[0xE9] && U32(sq, 0x100) != 0 && U32(sq, 0x108) + 6000 < game_time) {
        if (sq[0x90] != 1) {
            if (sq[0xCA]) {
                c = sq[0xCA];
                sq[0xCA] = 0;
                sq[0xC9] = c;
            }
            if (sq[0xCC]) {
                c = sq[0xCC];
                sq[0xCC] = 0;
                sq[0xCB] = c;
            }
        }
        sq[0xE9] = 0;
        sq[0x110] = 0;
        sq[0x115] = 0;
        say(sq, sq[0x10C] ? 0x3E : 0x2B);
        for (c = sq[0xF]; c; c = BRAIN(c)[7]) {
            ulong v = U32(BRAIN(c), 0x23);
            if (v == 5)
                aimain_24A6A0(c, 0x3F);
            else if (v == 4)
                aimain_24A6A0(c, 0x40);
            else if (v <= 3)
                aimain_24A6A0(c, 0x41);
        }
    }
}

/* ------------------------------------------------------------------ 0x25607C
 * brains of squads around: *near = own side's moving squads within 20, *coming = those
 * plus own squads heading to within 3, *enemy = other sides' squads within 30 */
void aiperc_25607C(void *squad, short *near, short *coming, short *enemy)
{
    uchar *sq = squad;
    uchar i;

    *near = 0;
    *coming = 0;
    *enemy = 0;
    for (i = 0; i < 32; i++) {
        uchar *S = SQUAD(i);
        long d;

        if (!S[0])
            continue;
        d = fix_fast_pyth_dist(S16(S, 0x12) - S16(sq, 0x12), S16(S, 0x14) - S16(sq, 0x14));
        if (sq[7] == S[7]) {
            if (d < 20 && S[0x90] == 1) {
                *near += S[0x10];
                *coming += S[0x10];
                continue;
            }
            if (U32(S, 0x91) == U32(S, 0x12))
                continue;
            if (fix_fast_pyth_dist(S16(S, 0x91) - S16(sq, 0x12), S16(S, 0x93) - S16(sq, 0x14)) < 3)
                *coming += S[0x10];
        } else if (d < 30)
            *enemy += S[0x10];
    }
}

/* ------------------------------------------------------------------ 0x2561B4
 * the nearest other squad of the same side within 80: its position */
uchar aiperc_2561B4(void *squad, ulong *pt)
{
    uchar *sq = squad;
    long dmin = 0x50;
    uchar best = 0, i;

    for (i = 0; i < 32; i++) {
        uchar *S;
        long d;

        if (i == U16(sq, 5))
            continue;
        S = SQUAD(i);
        if (!S[0] || S[7] != sq[7])
            continue;
        d = fix_fast_pyth_dist(S16(S, 0x12) - S16(sq, 0x12), S16(S, 0x14) - S16(sq, 0x14));
        if (d < dmin) {
            dmin = d;
            best = i;
        }
    }
    if (!best)
        return 0;
    *pt = U32(SQUAD(best), 0x12);
    return 1;
}

/* ------------------------------------------------------------------ 0x25626C */
void aiperc_25626C(void *squad)
{
    uchar *sq = squad;

    if (U32(sq, 0xD5) + 4000 < game_time) {
        pbasim_28B778(S16(BRAIN(aiperc_2575C0(sq)), 0xB));
        U32(sq, 0xD5) = game_time;
    }
}

/* ------------------------------------------------------------------ 0x2562B4
 * side 2 squads on the move: hold, retreat or attack depending on the forces around */
void aiperc_2562B4(void *squad)
{
    uchar *sq = squad;

    if (sq[7] != 2 || game_time <= U32(sq, 0xCD))
        return;
    U32(sq, 0xCD) = game_time + 1000;
    if (sq[0x90] == 1) {
        short near, coming, enemy;
        uchar attack;

        aiperc_25607C(sq, &near, &coming, &enemy);
        if (near >= enemy) {
            sq[0xCB] = 1;
            return;
        }
        if (enemy < coming) {
            sq[0xCB] = 7;
            return;
        }
        if (sq[0xCB] != 1)
            return;
        attack = 1;
        if ((RND() & 1) && sq[0x120] == 0) {
            ulong pt;
            if (aiperc_2561B4(sq, &pt) && aiperc_258C7C(sq, pt, 0x10)) {
                sq[0xCB] = 5;
                sq[0xCC] = 1;
                attack = 0;
            }
        }
        if (attack)
            sq[0xCB] = 3;
    } else if (sq[0xCB] != 1) {
        sq[0xCB] = 1;
        sq[0xCC] = 0;
        U32(sq, 0xCD) = 0;
    }
}

/* ------------------------------------------------------------------ 0x2563D0
 * one squad's think: messages, chatter, then the current order
 * Quirk kept: a message neither handler takes is not freed and ends the message loop. */
void aiperc_2563D0(void *squad)
{
    uchar *sq = squad;
    ulong bit;
    Msg *m;
    uchar k;

    if (sq[0x10] == 0) {
        MsgFlush((MsgAddr *)(sq + 3));
        return;
    }
    ai_cur_addr_a = U16(sq, 3);
    ai_cur_addr_b = U16(sq, 5);
    bit = 1UL << ((uchar)ai_cur_addr_b & 31);
    if (ai_cur_addr_a == 0x80)
        bit &= ai_debug_mask80;
    else if (ai_cur_addr_a == 0x81)
        bit &= ai_debug_mask81;
    else
        bit &= ai_debug_mask82;
    ai_debug_on = bit != 0;
    if (sq[2]) {
        MsgFlush((MsgAddr *)(sq + 3));
        return;
    }
    if (game_frame == 10 && sq[7] == 1 && sq[0xF] && BRAIN(sq[0xF])[0xD] == 1) {
        Msg *m2 = MsgAlloc();
        if (m2) {
            aiacc_25AEF0(m2, U32(sq, 3), U32(sq, 3), game_player_entity, 0);
            MsgSend(m2);
        }
    }
    aiperc_255CE4(sq);
    aiperc_2562B4(sq);
    aiperc_255E88(sq);
    aiperc_enemy_checked = 0;
    ai_371195 = 0;
    if (sq[0x11] == 0 && BRAIN(sq[0xF])[0xA]) {
        ulong p = U32(BRAIN(sq[0xF]), 0xE);
        U32(sq, 0x12) = p;
        U32(sq, 0x91) = p;
        MsgFlush((MsgAddr *)(sq + 3));
        return;
    }
    sq[0xC4] = 0;
    sq[0xB4] = 0;
    U32(sq, 0xB5) = 0;
    sq[0xB9] = 0;
    U32(sq, 0xBA) = 0;
    if (sq[0xE2] && game_time > U32(sq, 0xDE)) {
        Msg *m2 = MsgAlloc();
        if (m2) {
            aiacc_25B26C(m2, U32(sq, 3), U32(sq, 3), sq[0xE3]);
            if (aimsg_25A150(m2))
                aimsg_25A22C(sq);
        }
        sq[0xE2] = 0;
    }
    while ((m = MsgReceive((MsgAddr *)(sq + 3))) != 0) {
        if (!aiperc_253F70(sq, m) && !sq[0xE2] && !aiperc_25486C(sq, m))
            break;
        MsgFree(m);
    }
    if (sq[0xE2])
        return;
    aiperc_255798(sq);
    if (sq[0x157] == 1)
        aiperc_2558C4(sq);
    if (sq[0x157] == 3)
        aiperc_25597C(sq);
    if (sq[0xCB] == 6 && U32(sq, 0xD5) + 4000 < game_time) {
        pbasim_28B778(S16(BRAIN(aiperc_2575C0(sq)), 0xB));
        U32(sq, 0xD5) = game_time;
    }
    if (U16(sq, 0xA9) && game_time > U32(sq, 0xAB)) {
        aiperc_2569E4(sq);
        U32(sq, 0xAB) = game_time + 1000;
    }
    aimain_249FD0(sq + 0x24);
    aiperc_259C70(sq);
    k = sq[0x37];
    if (k < 2) {
        aiperc_2571D8(sq);
        aiperc_259BE8(sq);
    } else if (k == 2) {
        aiperc_2573C0(sq, U32(sq, 0x38));
        aiperc_256C90(sq);
        aiperc_259BE8(sq);
    } else if (k <= 4)
        aiperc_2596E4(sq);
}

/* ------------------------------------------------------------------ 0x2566CC
 * message 0x61 (from each squad) to the AI script */
void aiperc_2566CC(void)
{
    uchar *S;

    for (S = (uchar *)game_squads; S != (uchar *)game_squads + 32 * 0x170; S += 0x170) {
        Msg *m;

        if (!S[0])
            continue;
        m = MsgAlloc();
        if (!m)
            continue;
        m->pad2 = 0x61;
        U32(m, 8) = U32(S, 3);
        if (aimsg_25A150(m))
            aimsg_25A22C(S);
    }
}

/* ------------------------------------------------------------------ 0x256710 */
void aiperc_256710(void *squad)
{
    uchar *sq = squad;

    sq[0x9A] = 0;
    U32(sq, 0x9C) = 0;
    sq[0x9B] = 0;
    U32(sq, 0xA0) = 0;
}

/* ------------------------------------------------------------------ 0x256734
 * rebuild the mask of the squad's AI brains (+0x9A count, +0x9C bits) */
void aiperc_256734(void *squad)
{
    uchar *sq = squad;
    uchar c;

    sq[0x9A] = 0;
    U32(sq, 0x9C) = 0;
    for (c = sq[0xF]; c; c = BRAIN(c)[7]) {
        if (!BRAIN(c)[0xA]) {
            sq[0x9A]++;
            U32(sq, 0x9C) |= 1UL << (c & 31);
        }
    }
    sq[0x9B] = 0;
    U32(sq, 0xA0) = 0;
}

/* ------------------------------------------------------------------ 0x2567A4
 * message 6-flagged "new orders" to every brain of the squad */
void aiperc_2567A4(void *squad)
{
    uchar *sq = squad;
    uchar c;

    sq[0x9A] = 0;
    U32(sq, 0x9C) = 0;
    sq[0x9B] = 0;
    U32(sq, 0xA0) = 0;
    for (c = sq[0xF]; c;) {
        uchar *b = BRAIN(c);
        Msg *m = MsgAlloc();

        if (!m)
            return;
        aiacc_25AD68(m, U32(sq, 3), U32(b, 1));
        m->flag = 6;
        MsgSend(m);
        c = b[7];
    }
}

/* ------------------------------------------------------------------ 0x256814
 * forget the attack target and the region list (flag: also clear the regions' centres) */
void aiperc_256814(void *squad, long flag)
{
    uchar *sq = squad;
    uchar f = (uchar)flag;
    long i;

    U16(sq, 0xA9) = 0;
    if (sq[0x157] == 2)
        sq[0x157] = 0;
    for (i = 0; i < 32; i++) {
        if (f && sq[0x12C + i] != 0xFF)
            misobj_2C9EE8(sq[0x12C + i]);
        sq[0x12C + i] = 0xFF;
    }
    sq[0x14D] = 0;
    U32(sq, 0xAB) = 0;
    sq[0x14C] = sq[0x14D];
}

#define REGION_X(r) ((long)*(ulong *)(game_4413D0 + (r) * 8) >> 16)
#define REGION_Y(r) ((long)*(ulong *)(game_4413D0 + (r) * 8 + 4) >> 16)

/* ------------------------------------------------------------------ 0x256888
 * region list (+0x12C ring, +0x14C current, +0x14D end): skip to the best region, put its
 * centre into order k */
void aiperc_256888(void *squad, long k)
{
    uchar *sq = squad;
    ulong cur = sq[0x14C], i, best, n;
    ulong bestd;
    uchar *o;

    if (sq[0x14D] == sq[0x14C])
        return;
    best = cur;
    bestd = 0xFFFFFFFF;
    n = 0;
    for (i = cur; i != sq[0x14D];) {
        uchar r = sq[0x12C + i];
        long dx = REGION_X(r) - S16(sq, 0x12);
        long dy = REGION_Y(r) - S16(sq, 0x14);
        long d = dx * dx + dy * dy;

        if (d <= 10) {
            best = i + 1;
            if ((long)best >= 32)
                best = 0;
            if (best == sq[0x14D])
                best = i;
            bestd = 0xFFFFFFFF;
        } else if ((ulong)(d - n) < bestd) {
            best = i;
            bestd = d - n;
        }
        i++;
        n++;
        if ((long)i >= 32)
            i = 0;
    }
    if (best != cur) {
        ulong c = cur;
        while (c != best) {
            misobj_2C9EE8(sq[0x12C + c]);
            c++;
            if ((long)c >= 32)
                c = 0;
        }
        sq[0x14C] = (uchar)best;
    }
    o = ORD(sq, k);
    U16(o, 3) = (ushort)REGION_X(sq[0x12C + sq[0x14C]]);
    o[1] = 6;
    U16(o, 5) = (ushort)REGION_Y(sq[0x12C + sq[0x14C]]);
}

/* ------------------------------------------------------------------ 0x2569E4
 * follow the attack target (+0xA9): new orders, joining the helped squad, arrival */
void aiperc_2569E4(void *squad)
{
    uchar *sq = squad;
    long k = aimain_24A0B8(sq + 0x24);
    short t;
    long tx, ty, d1, d2;
    uchar o0;
    uchar *o;

    if (sq[0x157] == 3)
        return;
    t = S16(sq, 0xA9);
    if (!EN(t).active) {
        uchar c = sq[0x157];
        U16(sq, 0xA9) = 0;
        if (c == 2)
            sq[0x157] = 0;
        sq[0xAF] = 0;
    }
    if (U16(sq, 0xA9) == 0)
        return;
    if (sq[0x90] && sq[0x157] == 0)
        return;
    t = S16(sq, 0xA9);
    tx = FIXINT(EN(t).x);
    ty = FIXINT(EN(t).y);
    {
        short le = LEADER_ENT(sq);
        d1 = fix_fast_pyth_dist(EN(le).x - EN(t).x, EN(le).y - EN(t).y);
    }
    d2 = fix_fast_pyth_dist(S16(sq, 0x91) - tx, S16(sq, 0x93) - ty);
    if (sq[0x157] == 4 && sq[0x163] == BRAIN(EN(S16(sq, 0xA9)).f21)[6] && d1 <= 0x40000)
        ailow_2534A4(sq[0xF], sq[0x163]);
    aiperc_2567A4(sq);
    if (sq[0x157] == 2) {
        long lim = game_359708 == 0x1E ? 0x30000 : 0x8000;
        uchar *lb = BRAIN(sq[0xF]);

        if (d1 <= lim || (d1 < 0x40000 && U32(lb, 0x129) != 0)
            || (d1 < 0x10000 && *(uchar *)TN_LOWMEM(*(uchar **)(BRAIN(sq[0xF]) + 0xAD)) == 0))   /* G9 */
            aiperc_255858(sq[5]);
    }
    o0 = sq[0x24];
    if (o0 == 0x20) {
        if ((d2 << 17) <= d1)
            return;
        o = ORD(sq, k);
        U16(o, 3) = (ushort)tx;
        U16(o, 5) = (ushort)ty;
        o[1] = sq[0x157] == 2 ? 9 : 6;
        return;
    }
    if (d2 <= 1 && sq[0x157] == 0 && o0 != 1 && o0 != 0)
        return;
    o = ORD(sq, k);
    o[0] = 0x20;
    t = S16(sq, 0xA9);
    U16(o, 3) = (ushort)FIXINT(EN(t).x);
    o[1] = 6;
    U16(o, 5) = (ushort)FIXINT(EN(t).y);
}

/* ------------------------------------------------------------------ 0x256C90
 * move the squad one path step towards its destination (+0x91); TRUE while on the way.
 * Path record (+0x95): +0 valid, +1 goal point, +7 short steps left, +9 3-byte steps
 * {x: 9 bits, y: 9 bits, bit 2 of byte 2: waypoint, byte 2 >> 3: speed}. */
uchar aiperc_256C90(void *squad)
{
    uchar *sq = squad;
    uchar cls = BRAIN(sq[0xF])[0xD];
    short sum, thr;
    uchar *p;
    uchar st[3];

    if (cls == 3 || cls == 4)
        return 0;
    sum = (short)iabs((short)(U16(sq, 0x91) - U16(sq, 0x12)))
        + (short)iabs((short)(U16(sq, 0x93) - U16(sq, 0x14)));
    if (sq[0x157] == 2)
        thr = 0;
    else if (sq[0x157] == 0 && U16(sq, 0xA9) != 0)
        thr = S16(sq, 0xA9) == game_player_entity ? 1 : 2;
    else {
        p = (uchar *)U32(sq, 0x95);
        thr = (p && *p && S16(p, 7) <= 1) ? 0 : 2;
    }
    if (sum <= thr) {                           /* arrived */
        p = (uchar *)U32(sq, 0x95);
        if (p && S16(p, 7) <= 1 && BRAIN(sq[0xF])[0xD] == 1 && sq[0x1E] == 1) {
            sq[0x1E] = 0;
            aiperc_2567A4(sq);
        }
        if (sq[0x24] == 0x20 && sq[0x36] && U16(sq, 0xA9) == 0 && sq[0xCB] != 9)
            say(sq, sq[0x10] > 1 ? 0xA : 9);
        if (sq[0xCB] == 9)
            aiperc_2579E0(sq);
        if (sq[0xCB] == 6)
            sq[0xCB] = 4;
        if (sq[0xCB] == 5) {
            uchar c;

            aiperc_2579E0(sq);
            c = sq[0xCC];
            if (c) {
                sq[0xCC] = 0;
                sq[0xCB] = c;
            } else
                sq[0xCB] = 1;
        }
        *(uchar *)U32(sq, 0x95) = 0;
        if (U32(sq, 0x27) == U32(sq, 0x91)) {
            sq[0x24] = 1;
            sq[0x25] = 0;
            sq[0x26] = 0;
        }
        return 1;
    }
    if (sq[0x99] && RND() % 64 == 0)
        sq[0x99] = 0;
    if (*(uchar *)U32(sq, 0x95) == 0 && sq[0x99] == 0) {
        uchar mode = ailow_253580(BRAIN(sq[0xF]));

        if (path_25DDEC(U32(sq, 0x12), U32(sq, 0x91), sq[0xA6], (void *)U32(sq, 0x95), 0, mode)) {
            if (BRAIN(sq[0xF])[0xD] == 1)
                sq[0x1E] = sq[0x90] == 0 ? 1 : 2;
            U32((uchar *)U32(sq, 0x95), 1) = U32(sq, 0x91);
            *(uchar *)U32(sq, 0x95) = 1;
            sq[0x99] = 0;
        } else
            sq[0x99] = 1;
    }
    p = (uchar *)U32(sq, 0x95);
    if (*p && (long)(sq[0x9A] + sq[0x9B]) >= (long)sq[0x11]) {
        short n = S16(p, 7);
        ushort w, x, y;
        ulong pt;
        long fx, fy;

        memcpy(st, p + n * 3 + 9, 3);
        if (S16(p, 7) > 0 && !(p[(n - 1) * 3 + 0xB] & 4)) {
            S16(p, 7)--;
            p = (uchar *)U32(sq, 0x95);
            if (S16(p, 7) == 0) {
                uchar mode = ailow_253580(BRAIN(sq[0xF]));
                path_25BF04(U32(sq, 0x12), U32(p, 1), sq[0xA6], p, 1, mode);
            } else {
                uchar mode = ailow_253580(BRAIN(sq[0xF]));
                if (!path_25D35C(U32(sq, 0x12), sq[0xA6], p, mode)) {
                    *(uchar *)U32(sq, 0x95) = 0;
                    return 0;
                }
            }
            p = (uchar *)U32(sq, 0x95);
            memcpy(st, p + S16(p, 7) * 3 + 9, 3);
        }
        w = (ushort)(st[0] | st[1] << 8);
        x = w & 0x1FF;
        y = (ushort)((w >> 9) + ((st[2] & 3) << 7));
        pt = x | (ulong)y << 16;
        sq[0xA6] = ai_dir3x3[((short)x - S16(sq, 0x12)) * 3 + ((short)y - S16(sq, 0x14))];
        {
            uchar mode = ailow_253580(BRAIN(sq[0xF]));
            U16(sq, 0xA7) = path_25CA34(U32(sq, 0x12), pt, sq[0xA6], mode);
        }
        U16(sq, 0xA4) = (ushort)((st[2] >> 3) << 11);
        U32(sq, 0x12) = pt;
        fx = SQC(U16(sq, 0x12));
        fy = SQC(U16(sq, 0x14));
        if (FIXINT(S32(sq, 0x16)) < S16(sq, 0x12))
            fx -= 0x7000;
        else if (FIXINT(S32(sq, 0x16)) > S16(sq, 0x12))
            fx += 0x7000;
        if (FIXINT(S32(sq, 0x1A)) < S16(sq, 0x14))
            fy -= 0x7000;
        else if (FIXINT(S32(sq, 0x1A)) > S16(sq, 0x14))
            fy += 0x7000;
        S32(sq, 0x16) = fx - 0x8000;
        S32(sq, 0x1A) = fy - 0x8000;
        p = (uchar *)U32(sq, 0x95);
        if (S16(p, 7) == 0)
            *p = 0;
        else
            S16(p, 7)--;
        sq[0x9A] = 0;
        U32(sq, 0x9C) = 0;
        sq[0x9B] = 0;
        U32(sq, 0xA0) = 0;
        aiperc_2567A4(sq);
    }
    return sq[0x99] == 0 && *(uchar *)U32(sq, 0x95) ? 1 : 0;
}

/* ------------------------------------------------------------------ 0x2571D8
 * squad position: the leading vehicle's (rank 1), else the average of its AI brains
 * (no AI brain alive divides by zero, as the original) */
void aiperc_2571D8(void *squad)
{
    uchar *sq = squad;
    uchar c;
    long i;

    if (sq[0x11] == 0)
        return;
    c = sq[0xF];
    if (BRAIN(c)[0xD] == 2) {
        for (i = 0; i < sq[0x10]; i++) {
            uchar *b = BRAIN(c);
            if (b[0] && !b[0xA] && b[8] == 1) {
                S32(sq, 0x16) = EN(BENT(b)).x;
                S32(sq, 0x1A) = EN(BENT(b)).y;
                break;
            }
            c = b[7];
        }
    } else {
        long sx = 0, sy = 0, n = 0;

        for (i = 0; i < sq[0x10]; i++) {
            uchar *b = BRAIN(c);
            if (b[0] && !b[0xA]) {
                sx += EN(BENT(b)).x;
                sy += EN(BENT(b)).y;
                n++;
            }
            c = b[7];
        }
        S32(sq, 0x16) = (long)(((long long)sx << 16) / (long)(n << 16));
        S32(sq, 0x1A) = (long)(((long long)sy << 16) / (long)(n << 16));
    }
    U16(sq, 0x12) = (ushort)FIXINT(S32(sq, 0x16));
    U16(sq, 0x14) = (ushort)FIXINT(S32(sq, 0x1A));
}

/* ------------------------------------------------------------------ 0x257344
 * FALSE if pt is more than 2 away and the destination is within a sixth of that from it */
uchar aiperc_257344(void *squad, ulong pt)
{
    uchar *sq = squad;
    short px = (short)pt, py = (short)(pt >> 16);
    long d1 = fix_fast_pyth_dist(S16(sq, 0x12) - px, S16(sq, 0x14) - py);
    long d2 = fix_fast_pyth_dist(S16(sq, 0x91) - px, S16(sq, 0x93) - py);

    if (d1 > 2 && d2 <= d1 / 6)
        return 0;
    return 1;
}

/* ------------------------------------------------------------------ 0x2573C0
 * set the squad's destination (+0x91), kept inside its region (+0x120 / +0x121) */
void aiperc_2573C0(void *squad, ulong pt)
{
    uchar *sq = squad;
    uchar r;

    if (sq[0xAF] && U16(sq, 0xA9) && sq[0x90] == 1) {
        short t = S16(sq, 0xA9);
        short ex = (short)FIXINT(EN(t).x), ey = (short)FIXINT(EN(t).y);

        if (fix_fast_pyth_dist(ex - (short)pt, ey - (short)(pt >> 16)) >= 10)
            pt = (ushort)ex | (ulong)(ushort)ey << 16;
    }
    if (pt == U32(sq, 0x91))
        return;
    if (sq[0x120])
        r = misobj_2C9D50(sq[0x121], (long)(short)pt << 16, (long)(short)(pt >> 16) << 16);
    else
        r = 1;
    if (r)
        U32(sq, 0x91) = pt;
    else {
        U16(sq, 0x91) = (ushort)REGION_X(sq[0x121]);
        U16(sq, 0x93) = (ushort)REGION_Y(sq[0x121]);
    }
    *(uchar *)U32(sq, 0x95) = 0;
    sq[0x99] = 0;
    sq[0x9A] = 0;
    U32(sq, 0x9C) = 0;
    sq[0x9B] = 0;
    U32(sq, 0xA0) = 0;
    aiperc_2571D8(sq);
    aiperc_256734(sq);
}

/* ------------------------------------------------------------------ 0x257518
 * message 0x?? (aiacc_25B074) to every brain of the squad */
void aiperc_257518(void *squad)
{
    uchar *sq = squad;
    uchar c;

    sq[0x9A] = 0;
    U32(sq, 0x9C) = 0;
    sq[0x9B] = 0;
    U32(sq, 0xA0) = 0;
    for (c = sq[0xF]; c;) {
        uchar *b = BRAIN(c);
        Msg *m = MsgAlloc();

        if (!m)
            return;
        aiacc_25B074(m, U32(sq, 3), U32(b, 1));
        m->flag = 6;
        MsgSend(m);
        c = b[7];
    }
}

/* ------------------------------------------------------------------ 0x257588
 * the first live AI brain from brain c on along the squad list (brain 0: the free list) */
uchar aiperc_257588(long brain)
{
    uchar c = (uchar)brain;

    do {
        uchar *b = BRAIN(c);
        if (b[0] && !b[0xA])
            return c;
        c = b[7];
    } while (c);
    return 0;
}

/* ------------------------------------------------------------------ 0x2575C0
 * a random live AI brain of the squad (the speaker of its messages) */
uchar aiperc_2575C0(void *squad)
{
    uchar *sq = squad;
    uchar n, i, c;

    if (sq[0x11] == 0)
        return 0;
    n = (uchar)(RND() % (long)sq[0x11]);
    c = aiperc_257588(sq[0xF]);
    for (i = 0; i < n; i++)
        c = aiperc_257588(BRAIN(c)[7]);
    return c;
}

/* ------------------------------------------------------------------ 0x257628
 * sum of the brains' +0x1B after ailow_250668 */
long aiperc_257628(void *squad)
{
    uchar *sq = squad;
    long sum = 0;
    uchar c;

    for (c = sq[0xF]; c; c = BRAIN(c)[7]) {
        uchar *b = BRAIN(c);
        ailow_250668(b);
        sum += U32(b, 0x1B);
    }
    return sum;
}

/* ------------------------------------------------------------------ 0x257664
 * side 1: every soldier whose weapon block +4 is 8 fires (fire_292970 sel) */
void aiperc_257664(void *squad, long sel)
{
    uchar *sq = squad;
    uchar c;

    if (sq[7] != 1)
        return;
    for (c = sq[0xF]; c; c = BRAIN(c)[7]) {
        uchar *b = BRAIN(c);
        if (b[0xD] == 1) {
            short e = BENT(b);
            uchar *pba = (uchar *)&game_pba[EN(e).slot];
            if (U32(pba, 0xD) == 8)
                fire_292970(e, (uchar)sel);
        }
    }
}

/* ------------------------------------------------------------------ 0x2576D4
 * side 1: one soldier with an air strike (weapon kind 10, > 2) calls it on the target,
 * at most every 30 s per brain */
void aiperc_2576D4(void *squad)
{
    uchar *sq = squad;
    uchar first, c, i;

    if (sq[7] != 1)
        return;
    first = aiperc_2575C0(sq);
    c = first;
    i = 0;
    if (sq[0x11] == 0)
        return;
    do {
        uchar *b = BRAIN(c);
        short e = BENT(b);
        uchar *pba = (uchar *)&game_pba[EN(e).slot];

        if (S32(pba, 9) > 2 && U32(pba, 0xD) == 10) {
            ulong now = game_time;
            if (now - U32(b, 0x102) >= 30000 && WeaponFire(e)) {
                signed char s = (signed char)mfddata_2665E8(e);
                if (s == -1)
                    return;
                air_target[s] = b[0xC3] ? (short)b[0xCA] : S16(sq, 0xBF);
                U32(BRAIN(c), 0x102) = now;
                return;
            }
        }
        c = aiperc_257588(BRAIN(c)[7]);
        if (!c)
            c = aiperc_257588(sq[0xF]);
        if (c == first)
            return;
        i++;
    } while (i < sq[0x11]);
}

/* ------------------------------------------------------------------ 0x257820
 * engage the known enemy squad (+0xBE)?  TRUE if the own strength is enough; on bad odds
 * the side-1 soldiers fire their specials and call air strikes */
uchar aiperc_257820(void *squad)
{
    uchar *sq = squad;
    uchar keep = 1, ret;
    ulong own, enemy, a, e, ratio, thr;
    long f, q;

    if (!aiperc_2539F0(sq))
        return 0;
    if (sq[0xE8])
        return 0;
    own = aiperc_257628(sq);
    if (!sq[0xC1])
        return own > (ulong)sq[0x11] * 10;
    enemy = aiperc_257628(SQUAD(sq[0xBE]));
    if (sq[0xD9] == 0x32)
        f = sq[0x156] * 5;
    else
        f = (100 - sq[0xD9]) * 10;
    q = (long)(((long long)(long)((ulong)(f * 15) << 16) << 16) / 0x27100000);
    thr = (ulong)(long)(((long long)q * (long)enemy) >> 16);
    ret = own > thr;
    a = own < 1 ? 1 : own;
    e = enemy < 1 ? 1 : enemy;
    ratio = a * 100 / e;
    if (ratio <= 100)
        aiperc_257664(sq, 1);
    if (ratio < 40)
        aiperc_2576D4(sq);
    if (sq[0x11B] != sq[0xBE])
        sq[0x11B] = sq[0xBE];
    else if (ratio < U32(sq, 0x11C) * 2 && ratio * 2 > U32(sq, 0x11C))
        keep = 0;
    if (keep)
        U32(sq, 0x11C) = ratio;
    return ret;
}

/* ------------------------------------------------------------------ 0x2579E0
 * forget the enemy squad, drop the attack orders */
void aiperc_2579E0(void *squad)
{
    uchar *sq = squad;

    sq[0xC1] = 0;
    sq[0xC2] = 0;
    sq[0xBE] = 0;
    U16(sq, 0xBF) = 0;
    aimain_249F6C(sq + 0x24, 0x1C);
    if (sq[0xCB] == 9 || game_player_entity == S16(sq, 0xA9)) {
        aimain_249F6C(sq + 0x24, 0xE0);
        *(uchar *)U32(sq, 0x95) = 0;
        if (sq[0xCB] == 9) {
            uchar c = sq[0x157];
            U16(sq, 0xA9) = 0;
            if (c == 2)
                sq[0x157] = 0;
        }
    }
    aimain_249FD0(sq + 0x24);
    if (sq[0x24] == 0x20 && sq[0x36] && U16(sq, 0xA9) == 0)
        say(sq, 8);
    if (sq[0x157] == 0)
        aiperc_257518(sq);
    aiperc_257664(sq, 0);
}

/* ------------------------------------------------------------------ 0x257AB4
 * tell the side (message flag 7) where the squad is, unless side 1 or +0x164 */
void aiperc_257AB4(void *squad)
{
    uchar *sq = squad;
    uchar f = sq[7] == 1 ? 0 : sq[0x164] == 0;

    if (f) {
        Msg *m = MsgAlloc();
        if (m) {
            aiacc_25AC6C(m, U32(sq, 3), U32((uchar *)&game_sides[sq[0xD]], 1), sq + 0x12, 0);
            m->flag = 7;
            MsgSend(m);
        }
    }
}

/* ------------------------------------------------------------------ 0x257B18
 * attack the squad of order +0x33 (not moving), or go where an enemy was seen */
void aiperc_257B18(void *squad)
{
    uchar *sq = squad;

    if (sq[0x90] != 1) {
        uchar c;

        aiperc_2598D0(sq);
        sq[0xB4] = 0;
        U32(sq, 0xB5) = 0;
        sq[0xB9] = 0;
        U32(sq, 0xBA) = 0;
        c = sq[0x33];
        U16(sq, 0xBF) = 0;
        if (c)
            sq[0xBE] = c;
        aiperc_258908(sq);
        if (sq[0xC1])
            aiperc_258EEC(sq);
        return;
    }
    if (aiperc_258908(sq)) {
        aiperc_258EEC(sq);
        return;
    }
    if (!ai_371195 || sq[0xCB] == 9 || sq[0xCB] == 5)
        return;
    if (fix_fast_pyth_dist((short)ai_371190 - S16(sq, 0x91), (short)(ai_371190 >> 16) - S16(sq, 0x93)) <= 5)
        return;
    sq[0xC3] = 2;
    aiperc_2567A4(sq);
    if (aiperc_257344(sq, ai_371190))
        aiperc_258C7C(sq, ai_371190, 8);
}

/* ------------------------------------------------------------------ 0x257C20
 * brain b is a vehicle / installation the cheat flags make invisible */
uchar aiperc_257C20(void *brain)
{
    uchar *b = brain;
    uchar *sq = SQUAD(b[6]);
    short le = LEADER_ENT(sq);

    if (EN(le).cls == 2) {
        uchar t = EN(le).type;
        if ((t == 0 || t == 1 || t == 2 || t == 8 || t == 7 || t == 3) && (ai_cheat_flags & 1))
            return 1;
    }
    if (b[0xD] == 3 && (ai_cheat_flags & 2))
        return 1;
    return 0;
}

/* ------------------------------------------------------------------ 0x257CB8
 * can squad sq take brain b as an enemy */
uchar aiperc_257CB8(void *squad, void *brain)
{
    uchar *sq = squad, *b = brain;

    if (b[0] == 0 && b[0xA] == 0)
        return 0;
    if (sq[0xC2] && U16(sq, 0xBF) != U16(b, 0xB))
        return 0;
    if (!sq[0xC2] && sq[7] == b[5])
        return 0;
    if (!sq[0xC2] && aiperc_257C20(b))
        return 0;
    return 1;
}

/* ------------------------------------------------------------------ 0x257D18
 * qsort comparison of two longs */
long aiperc_257D18(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;

    if (x < y)
        return -1;
    return x != y;
}

/* ------------------------------------------------------------------ 0x257D38
 * enemies in range of the squad (25 squares, 45 for installation type 0x45) that a random
 * speaker brain would hit (pbasim_28ADBC chance against a d100 roll), nearest first:
 * *own = the nearest visible one of the known enemy squad (+0xBE), *other = of any other
 * squad (aircraft are replaced by a later find) */
#pragma pack(push, 1)
typedef struct {
    long d;
    uchar brain;
} PercCand;
#pragma pack(pop)

void aiperc_257D38(void *squad, short *own, short *other)
{
    uchar *sq = squad;
    ulong pos = U32(sq, 0x12);
    uchar f_own = 0, f_other = 0;
    uchar spk = aiperc_2575C0(sq);
    uchar *lb = BRAIN(sq[0xF]);
    short range;
    PercCand list[60];
    long n = 0, j;
    uchar i;

    if (lb[0xD] == 3 && EN(BENT(lb)).type == 0x45)
        range = 0x2D;
    else
        range = 0x19;
    for (i = 0; i < 60; i++) {
        uchar *b = BRAIN(i);
        long d, r;

        if (!aiperc_257CB8(sq, b))
            continue;
        d = fix_fast_pyth_dist((short)pos - S16(b, 0xE), (short)(pos >> 16) - S16(b, 0x10));
        if (d >= range)
            continue;
        r = RND() % 100;
        if ((signed char)pbasim_28ADBC(BENT(BRAIN(spk)), BENT(b), d << 16) <= r)
            continue;
        list[n].d = d;
        list[n].brain = i;
        n++;
    }
    *other = 0;
    *own = *other;
    if (n <= 0)
        return;
    lg_qsort(list, n, 5, WATCOM_FN(aiperc_257D18));
    for (j = 0; j < n; j++) {
        uchar *t = BRAIN(list[j].brain);

        if (!ailow_2508D0(BENT(BRAIN(spk)), BENT(t)))
            continue;
        if (t[6] == sq[0xBE]) {
            if (*own == 0 || f_own) {
                *own = S16(t, 0xB);
                f_own = t[0xD] == 4;
            }
        } else {
            if (*other == 0 || f_other) {
                *other = S16(t, 0xB);
                f_other = t[0xD] == 4;
            }
        }
        if (*own && *other && (!f_own || !f_other))
            return;
    }
}

/* ------------------------------------------------------------------ 0x25804C
 * scale the near-list radii (damage_27D158 +0 / +4) of the squad's units by pct / 100 */
void aiperc_25804C(void *squad, long pct)
{
    uchar *sq = squad;
    long f = (long)(((ulong)(uchar)pct << 16)) / 100;
    uchar c;

    for (c = sq[0xF]; c; c = BRAIN(c)[7]) {
        long *p = (long *)damage_27D158(BENT(BRAIN(c)));
        if (p) {
            p[0] = (long)(((long long)f * p[0]) >> 16);
            p[1] = (long)(((long long)f * p[1]) >> 16);
        }
    }
}

/* ------------------------------------------------------------------ 0x2580CC
 * distance from pt to the squad (its position or the nearest live brain) */
long aiperc_2580CC(void *squad, ulong pt)
{
    uchar *sq = squad;
    short px = (short)pt, py = (short)(pt >> 16);
    long d = fix_fast_pyth_dist(S16(sq, 0x12) - px, S16(sq, 0x14) - py);
    uchar c;

    for (c = sq[0xF]; c; c = BRAIN(c)[7]) {
        uchar *b = BRAIN(c);
        if (b[0]) {
            long d2 = fix_fast_pyth_dist(S16(b, 0xE) - px, S16(b, 0x10) - py);
            if (d2 < d)
                d = d2;
        }
    }
    return d;
}

/* TRUE for a "vehicle" entity class/type pair as tested by 0x258170 */
#define ISVEH(t) ((t) == 0x45 || (t) == 0x46 || (t) == 0x47)
/* TRUE for entity class 3 types 0..6 (infantry) */
#define ISINF(e) (EN(e).cls == 3 && EN(e).type <= 6)

/* ------------------------------------------------------------------ 0x258170
 * pick the enemies the squad sees: *own (near the squad's target side +0xBE) and *other;
 * *e3 gets the last spotter seen (non-player sides).  Counts the vehicles seen (+0x100,
 * +0x10C / +0x10D flags) and stamps +0x108 when something was found. */
void aiperc_258170(void *squad, long flag, short *own, short *other, short *e3)
{
    uchar *sq = squad;
    ulong counter = 0;
    ushort best_own, best_other;
    uchar f2c = 0, f34 = 0, f38 = 0, f3c = 0, f40 = 0, f44 = 0, f4c = 0;

    *other = 0;
    *own = 0;
    *e3 = 0;
    best_own = 0xFFFF;
    best_other = (uchar)flag ? (ushort)(S32(sq, 0xE2) >> 16) : 0xFFFF;
    if (sq[7] == 2) {
        uchar c;

        memset(ai_seen_bits, 0, 0x26);
        for (c = sq[0xF]; c; c = BRAIN(c)[7]) {
            uchar *nl = (uchar *)damage_27D158(BENT(BRAIN(c)));
            uchar idx;
            short ent;

            if (!nl)
                continue;
            idx = 0;
            for (ent = **(short **)(nl + 0xC); (long)idx < S32(nl, 8) && ent;
                 idx++, ent = *(short *)(*(uchar **)(nl + 0xC) + idx * 6)) {
                uchar *t;
                uchar tb;
                ushort d;

                if (ai_seen_bits[ent >> 3] & (1 << (ent & 7)))
                    continue;
                ai_seen_bits[ent >> 3] |= (uchar)(1 << (ent & 7));
                tb = EN(ent).f21;
                if (!tb)
                    continue;
                t = BRAIN(tb);
                if (!aiperc_257CB8(sq, t))
                    continue;
                d = *(ushort *)(*(uchar **)(nl + 0xC) + idx * 6 + 2);
                if (sq[0xBE] == t[6]) {
                    if (!(d < best_own) && (!f3c || t[0xD] == 4))
                        continue;
                    if (t[0xD] != 1 && (f44 || f38))
                        continue;
                    *own = ent;
                    best_own = d;
                    if (t[0xD] == 1)
                        f44 = 1;
                    f3c = t[0xD] == 4;
                } else {
                    if (!(d < best_other) && (!f40 || t[0xD] == 4))
                        continue;
                    if (t[0xD] != 1 && (f38 || f44))
                        continue;
                    *other = ent;
                    best_other = d;
                    if (t[0xD] == 1)
                        f38 = 1;
                    f40 = t[0xD] == 4;
                }
            }
        }
    } else {
        uchar *sd = SIDE(sq[0xD]);
        uchar k = 0;

        do {
            uchar *rec = sd + k * 6;
            short ent = S16(rec, 8);
            uchar *t;
            ushort d;
            uchar cl;

            if (!ent)
                break;
            if (!EN(ent).f21)
                continue;
            t = BRAIN(EN(ent).f21);
            if (!aiperc_257CB8(sq, t))
                continue;
            d = U16(rec, 0xA);
            *e3 = S16(rec, 0xC);
            cl = (ulong)BRAIN(EN(*e3).f21)[6] == (ulong)U16(sq, 5);
            if (cl && (t[0xD] != 3 || ISVEH(EN(BENT(t)).type))) {
                counter++;
                if (!ISINF(BENT(t)))
                    f2c = 1;
            }
            if (t[6] == sq[0xBE]) {
                if (!(cl && !f34) && d >= best_own)
                    continue;
                best_own = d;
                f34 = cl;
                *own = ent;
            } else {
                if (!(cl && !f4c) && d >= best_other)
                    continue;
                best_other = d;
                f4c = cl;
                *other = ent;
            }
        } while (++k < 0xC0);
    }

    if (*own == 0 && *other == 0) {
        aiperc_257D38(sq, own, other);
        if (*own != 0 || *other != 0) {
            U32(sq, 0x108) = game_time;
            if (*own && (EN(*own).cls != 4 || ISVEH(EN(*own).type))) {
                if ((ushort)counter == 0)
                    counter = 1;
                if (!ISINF(*own))
                    f2c = 1;
            }
            if (*other && (EN(*other).cls != 4 || ISVEH(EN(*other).type))) {
                if ((ushort)counter == 0)
                    counter = 1;
                if (!ISINF(*other))
                    f2c = 1;
            }
        }
    } else {
        if (*own && !f34) {
            ulong pt = (ushort)FIXINT(EN(*own).x) | ((ulong)FIXINT(EN(*own).y) << 16);
            if (aiperc_2580CC(sq, pt) > 0x2D)
                *own = 0;
        }
        if (*other && !f4c) {
            ulong pt = (ushort)FIXINT(EN(*other).x) | ((ulong)FIXINT(EN(*other).y) << 16);
            if (aiperc_2580CC(sq, pt) > 0x2D)
                *other = 0;
        }
    }
    if ((ushort)counter > 0) {
        if (f2c)
            sq[0x10C] = 1;
        if ((ulong)(ushort)counter > U32(sq, 0x100)) {
            sq[0x10D] = 1;
            U32(sq, 0x100) = (ushort)counter;
            U32(sq, 0x104) = game_time;
        }
    }
    if (*own || *other)
        U32(sq, 0x108) = game_time;
}
