/* ailowa.c -- Terra Nova low-level AI, first part of ailow.c (module M007, 0x24ADE0-0x25129C;
 * r:\prj\tn\v109\tn\src\tn\ailow.c, the rest is in G4b's file).  Decompiled from __FF.EXE v1.09.
 *
 *   0x24ADE0 ailow_24ADE0  target ignored by the cheat flags (vehicles / installations)
 *   0x24AE4C ailow_24AE4C  fire the brain's weapon slot (player side soldiers)
 *   0x24AF24 ailow_24AF24  PBA ammunition left, percent
 *   0x24AFB4 ailow_24AFB4  damage levels 1..6: average / worst part / armour meter
 *   0x24B24C ailow_24B24C  every side-1 soldier reports its damage (messages 0x3F..0x41)
 *   0x24B2E0 ailow_24B2E0  copy the speaker records' voice byte to the brains (+9)
 *   0x24B34C ailow_24B34C  count a created brain in its squad (+0x11)
 *   0x24B39C AILowInit     create a brain and its object (classes 1-4)
 *   0x24B8A0 ailow_24B8A0  AILowInit for a soldier (class 1; squad formation 0)
 *   0x24B8DC ailow_24B8DC  ... vehicle (class 2)
 *   0x24B900 ailow_24B900  ... installation (class 4)
 *   0x24B924 ailow_24B924  ... aircraft (class 3)
 *   0x24B948 ailow_24B948  a brain changes side (1 <-> 2)
 *   0x24B9B8 ailow_24B9B8  the player's brain changes side
 *   0x24BA28 ailow_24BA28  create the player's brain in a squad
 *   0x24BB1C ailow_24BB1C  set brain +0x14D
 *   0x24BB30 ailow_24BB30  aircraft brain: may it attack (type 2..6, or +0x14D)
 *   0x24BBA0 ailow_24BBA0  aircraft: fire at the target every 0.25 s (0.05 s types 5/6)
 *   0x24BD00 ailow_24BD00  aircraft: pick / drop a target
 *   0x24BF3C ailow_24BF3C  aircraft: fly around the target / squad point (EDMS teleport)
 */
#include "g4_a.h"
#include "g5_a.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
/* code-segment table at 0x25AB88 ([0] is the tail of the preceding code) */
fix ai_25AB88[25][2] = {
    {0xC35B, 0}, {0, 0}, {-0x8000, 0x8000}, {-0x8000, -0x8000}, {0x8000, 0x8000},
    {0x8000, -0x8000}, {0, -0x8000}, {0, 0}, {-0x4000, 0}, {-0x8000, 0}, {-0xC000, 0},
    {-0x10000, 0}, {-0x14000, 0}, {0, 0}, {0, -0xA0000}, {0, 0xA0000}, {0x50000, -0x140000},
    {0x50000, 0x140000}, {-0x50000, 0}, {0, 0}, {-0x4000, 0x4000}, {-0x4000, -0x4000},
    {0x4000, 0x4000}, {0x4000, -0x4000}, {0, -0x4000},
};
long ai_3598DC, ai_3598E0, ai_3598E4;
ushort ai_371188, ai_turn_step;
uchar ai_37118C;
ulong ai_debug_mask80, ai_debug_mask81;
ushort ai_debug_mask82;
uchar ai_cheat_flags;
ulong ai_fire_mask;
#endif

#define ENTX(e) (G4_ENT(e).x)
#define ENTY(e) (G4_ENT(e).y)
#define ENTZ(e) (G4_ENT(e).z)
/* the brain's path buffer.  Some brains have none (class 4, or when the pool ran out: NULL);
 * where the original read or wrote through it without checking, DOS low memory took it
 * (TN_LOWMEM; docs/BUGS.md G9) */
#define BRPATH(br) ((uchar *)TN_LOWMEM(*(uchar **)((uchar *)(br) + 0xAD)))

/* ------------------------------------------------------------------ 0x24ADE0 */
uchar ailow_24ADE0(uchar *br)
{
    short e = G4_BENT(br);

    if (G4_ENT(e).cls == 2) {
        uchar t = G4_ENT(e).type;
        if (t == 0 || t == 1 || t == 2 || t == 8 || t == 7 || t == 3)
            if (ai_cheat_flags & 1)
                return 1;
    }
    if (br[0xD] == 3 && (ai_cheat_flags & 2))
        return 1;
    return 0;
}

/* ------------------------------------------------------------------ 0x24AE4C */
void ailow_24AE4C(long brain, long weapon, long sel)
{
    uchar *br = G4_BRAIN(brain);
    short e;
    uchar *pba;
    long w;

    if (br[0xD] != 1 || br[5] != 1)
        return;
    if (br[0x2F] == 0)
        return;
    e = G4_BENT(br);
    pba = (uchar *)&game_pba[G4_ENT(e).slot];
    w = G4_S32(pba, 0xD);
    if (weapon != w)
        return;
    if (G4_S32(pba, 9) <= 0)
        return;
    if (w == 10) {
        if (br[0xC3] && br[0xCA])
            aircraft_283A64(e, br[0xCA]);
    } else if ((1u << (weapon & 31)) & ai_fire_mask) {
        WeaponFire(e);
    } else {
        fire_292970(e, (uchar)sel);
    }
    if (br[0x2F] == 1)
        br[0x2F] = 0;
}

/* ------------------------------------------------------------------ 0x24AF24 */
void ailow_24AF24(uchar *br, short *out)
{
    uchar *pba = (uchar *)&game_pba[G4_ENT(G4_BENT(br)).slot];
    uchar *rec = *(uchar **)(pba + 0x9C);
    long cur = 0, max = 0, i;

    for (i = 0; i < G4_U16(pba, 0xA0); i++, rec += 5) {
        cur += G4_U16(rec, 3);
        max += G4_U16(rec, 1);
    }
    *out = (short)(cur * 100 / max);    /* max 0: #DE in the original too */
}

/* ------------------------------------------------------------------ 0x24AFB4 */
static short level5(short v)
{
    if (v == 0) return 1;
    if (v < 5) return 2;
    if (v < 10) return 3;
    if (v < 25) return 4;
    if (v < 70) return 5;
    return 6;
}

void ailow_24AFB4(uchar *br, short *avg, short *worst, short *armour)
{
    short e = G4_BENT(br);
    short slot = G4_ENT(e).slot;
    uchar *recs, *meter;
    uchar n;
    ulong i;
    short v;

    switch (G4_ENT(e).cls) {
    case 1:
        recs = (uchar *)&game_pba[slot] + 0x15;
        meter = *(uchar **)((uchar *)&game_pba[slot] + 0x9C);
        n = 4;
        break;
    case 2:
        recs = (uchar *)&game_vehicles[slot] + 6;
        meter = (uchar *)&game_vehicles[slot] + 0xD;
        n = 1;
        break;
    case 3:
        recs = game_cls3[slot] + 6;
        meter = game_cls3[slot] + 0x1B;
        n = 3;
        break;
    case 4:
        recs = game_cls4[slot] + 0x10;
        meter = game_cls4[slot] + 0x51;
        n = game_cls4[slot][0xC];
        break;
    default:
        return;     /* original: runs on with uninitialised locals */
    }
    *avg = 0;
    *worst = 1000;
    for (i = 0; i < n; i++, recs += 7) {
        long pct = (long)G4_S16(recs, 0) * 100 / G4_S16(recs, 2);
        if ((ulong)pct < (ulong)(long)*worst)
            *worst = (short)pct;
        *avg = (short)(*avg + pct);
    }
    *avg = (short)((long)*avg / (long)n);
    if (G4_U16(meter, 1) == 0)
        *armour = 100;
    else
        *armour = (short)((long)G4_U16(meter, 3) * 100 / (long)G4_U16(meter, 1));
    *avg = level5(*avg);
    *worst = level5(*worst);
    v = *armour;
    if (v == 0) *armour = 1;
    else if (v < 10) *armour = 2;
    else if (v < 20) *armour = 3;
    else if (v < 35) *armour = 4;
    else if (v < 70) *armour = 5;
    else *armour = 6;
}

/* ------------------------------------------------------------------ 0x24B24C */
void ailow_24B24C(void)
{
    uchar i;
    short avg, worst, armour;

    for (i = 0; i < GAME_MAX_BRAINS; i++) {
        uchar *br = G4_BRAIN(i);
        long what;

        if (!br[0] || br[5] != 1)
            continue;
        ailow_24AFB4(br, &avg, &worst, &armour);
        if (armour == 6) what = 1;
        else if (armour == 5) what = 0x3F;
        else if (armour == 4) what = 0x40;
        else if (armour <= 3) what = 0x41;
        else continue;
        aimain_24A6A0(i, what);
    }
}

/* ------------------------------------------------------------------ 0x24B2E0 */
void ailow_24B2E0(void)
{
    long off, n = game_391E4C;

    if (n <= 0)
        return;
    for (off = 0; off < n * 0x32; off += 0x32) {
        uchar *rec = (uchar *)game_391E50 + off;
        short e = (short)((short)(game_pba[G4_S16(rec, 4)].f000 << 1) >> 1);
        G4_BRAIN(G4_ENT(e).f21)[9] = rec[6];
    }
}

/* ------------------------------------------------------------------ 0x24B34C */
void ailow_24B34C(long brain)
{
    game_squads[G4_BRAIN(brain)[6]].f011++;
}

/* ------------------------------------------------------------------ 0x24B39C */
short AILowInit(long cls_arg, long type, long squad_arg, fix x, fix y, uchar *tmpl)
{
    uchar cls = (uchar)cls_arg, squad = (uchar)squad_arg;
    uchar b = game_brains[0].next, n;
    uchar *br, *sq;
    short ent, slot;     /* original: uninitialised when the class is bad */
    uchar *p;
    long t;

    if (!b)
        return 0;
    br = G4_BRAIN(b);
    game_brains[0].next = br[7];
    sq = G4_SQUAD(squad);
    switch (cls) {
    case 1:
        br[0xD] = 1;
        if (!damage_27D720(&ent, &slot, b, (uchar)type, (uchar)(((sq[7] - 1) << 4) + 0x30), tmpl))
            DbgReportError(1, "AILowInit: Couldn't init PBA object data");
        break;
    case 2:
        br[0xD] = 2;
        if (!damage_27DC90(&ent, &slot, b, (uchar)type, tmpl))
            DbgReportError(1, "AIVehInit: Couldn't init Vehicle object data");
        break;
    case 3:
        br[0xD] = 4;
        if (!damage_27C120(&ent, &slot, b, (uchar)type, tmpl, 0))
            DbgReportError(1, "AircraftInit: Couldn't init Air object data");
        break;
    case 4:
        br[0xD] = 3;
        if (!damage_27CF30(&ent, &slot, b, (uchar)type, tmpl, 0))
            DbgReportError(1, "AIInstInit: Couldn't init Inst object data");
        break;
    default:
        DbgReportError(1, "Tried to call AILowInit with class %d!\n", cls);
        ent = slot = 0;
        break;
    }
    G4_U16(br, 1) = 0x80;
    G4_U16(br, 3) = b;
    G4_S16(br, 0xB) = ent;
    br[6] = squad;
    br[0] = 1;
    br[5] = sq[7];
    n = ++sq[0x10];
    br[8] = n;
    ailow_24B34C(b);
    br[7] = sq[0xF];
    sq[0xF] = b;
    G4_S16(br, 0xE) = (short)(x >> 16);
    br[0xA] = 0;
    G4_S16(br, 0x10) = (short)(y >> 16);
    br[9] = 0xD;
    aimain_249F48(br + 0x30);
    br[0x9C] = 0;
    G4_S16(br, 0x9D) = G4_S16(br, 0xE);
    G4_S16(br, 0x9F) = G4_S16(br, 0x10);
    G4_S32(br, 0xA5) = G4_SQCENTRE(G4_S16(br, 0x9D));
    G4_S32(br, 0x16) = 0x100;
    G4_S32(br, 0xA9) = G4_SQCENTRE(G4_S16(br, 0x9F));
    br[0xB1] = 0;
    br[0xB2] = 0;
    br[0xB3] = 0;
    if (G4_ENT(G4_BENT(br)).cls != 4) {
        p = aimain_24A120();
        G4_U32(br, 0xAD) = (ulong)p;
        if (p)
            *p = 0;
    }
    br[0xB4] = 0;
    G4_S16(br, 0xB5) = 0;
    G4_S32(br, 0xBF) = 0;
    br[0xC3] = 0;
    br[0xC9] = 0;
    br[0xCA] = 0;
    br[0xCB] = 0;
    br[0xC4] = 0;
    G4_S32(br, 0xC5) = 0;
    G4_S16(br, 0xCC) = -1;
    G4_S16(br, 0xCE) = -1;
    br[0xD0] = 0;
    G4_S32(br, 0xD1) = 0;
    G4_S16(br, 0xD5) = 0;
    G4_S32(br, 0xE5) = 0;
    G4_S32(br, 0xE9) = 0;
    G4_S32(br, 0xED) = 0;
    G4_S32(br, 0x27) = 0;
    G4_S32(br, 0x2B) = 0;
    br[0x2F] = 2;
    G4_S32(br, 0xF6) = 0;
    G4_S32(br, 0xFA) = 0;
    br[0xF5] = 0;
    G4_S32(br, 0x106) = 0;
    G4_S32(br, 0x10A) = 0;
    G4_S32(br, 0x112) = 0;
    G4_S32(br, 0x10E) = 0;
    G4_S32(br, 0xF1) = 0;
    G4_S32(br, 0x1F) = 6;
    G4_S32(br, 0x23) = 6;
    br[0x116] = 0;
    G4_S32(br, 0x117) = 0;
    G4_S32(br, 0x121) = 0;
    G4_S32(br, 0x125) = 0;
    G4_S32(br, 0x129) = 0;
    br[0x135] = 0;
    G4_S32(br, 0xE1) = 0;
    t = game_time;
    G4_S32(br, 0x140) = 0;
    G4_S32(br, 0x102) = t;
    G4_S32(br, 0xFE) = t;
    G4_S32(br, 0xF6) = t;
    G4_S32(br, 0xDD) = G4_S32(br, 0xE);
    G4_S32(br, 0x144) = 0;
    G4_S16(br, 0x13A) = (short)(RndRange(game_rnd_text, 0, 0x7FFF) * 2);
    t = RndRange(game_rnd_text, 0, 0x7FFF);
    G4_S32(br, 0x13C) = 0x50000;
    G4_S32(br, 0x148) = 0;
    br[0x14D] = 0;
    br[0x14C] = (uchar)(t & 1);
    G4_ENT(ent).x = x;
    G4_ENT(ent).y = y;
    if (br[5] == 1)
        G4_ENT(ent).heading = 0;
    switch (cls) {
    case 1: physics_2D3FB8(ent); break;
    case 2: physics_2D4174(ent); break;
    case 4: physics_2D44C0(ent); break;
    }
    return slot;
}

/* ------------------------------------------------------------------ 0x24B8A0 .. 0x24B924 */
long ailow_24B8A0(long type, long squad, fix x, fix y, uchar *tmpl)    /* the original returns a short */
{
    G4_U8(G4_SQUAD(squad), 0x1E) = 0;
    return AILowInit(1, type, (uchar)squad, x, y, tmpl);
}

short ailow_24B8DC(long type, long squad, fix x, fix y, uchar *tmpl)
{
    return AILowInit(2, type, (uchar)squad, x, y, tmpl);
}

short ailow_24B900(long type, long squad, fix x, fix y, uchar *tmpl)
{
    return AILowInit(4, type, (uchar)squad, x, y, tmpl);
}

short ailow_24B924(long type, long squad, fix x, fix y, void *tmpl)
{
    return AILowInit(3, type, (uchar)squad, x, y, tmpl);
}

/* ------------------------------------------------------------------ 0x24B948 */
void ailow_24B948(long brain_arg)
{
    uchar b = (uchar)brain_arg;
    uchar *br = G4_BRAIN(b);
    uchar side = (uchar)((br[5] == 1) + 1);
    uchar r;

    r = aiperc_253AE8(0, G4_U32(br, 0xE), (signed char)side);
    ailow_253008(b, 0);
    br[5] = side;
    ailow_2531B0(b, r);
}

/* ------------------------------------------------------------------ 0x24B9B8 */
void ailow_24B9B8(long to_enemy)
{
    uchar pb = G4_ENT(game_player_entity).f21;

    if (to_enemy) {
        if (G4_BRAIN(pb)[5] == 1)
            ailow_24B948(pb);
    } else if (G4_BRAIN(pb)[5] == 2) {
        ailow_24B948(pb);
    }
}

/* ------------------------------------------------------------------ 0x24BA28 */
uchar ailow_24BA28(long unused, long squad_arg)
{
    uchar squad = (uchar)squad_arg;
    uchar b = game_brains[0].next;
    uchar *br, *sq;

    if (!b)
        return 0;
    br = G4_BRAIN(b);
    game_brains[0].next = br[7];
    br[6] = squad;
    sq = G4_SQUAD(squad);
    br[5] = 1;
    br[8] = ++sq[0x10];
    br[7] = sq[0xF];
    sq[0xF] = b;
    G4_U16(br, 1) = 0x80;
    br[0xD] = 1;
    br[0] = 0;
    br[0xA] = 1;
    G4_U16(br, 3) = b;
    G4_S16(br, 0xB) = game_player_entity;
    game_sides[1].active = 0;
    game_sides[1].f007 = 1;
    game_441354 = 1;
    if (game_359708 == 0x27)
        ailow_24B948(b);
    return b;
}

/* ------------------------------------------------------------------ 0x24BB1C / 0x24BB30 */
void ailow_24BB1C(long brain, long v)
{
    G4_BRAIN(brain)[0x14D] = (uchar)v;
}

uchar ailow_24BB30(long brain)
{
    uchar *br = G4_BRAIN(brain);
    uchar t = G4_ENT(G4_S16(br, 0xB)).type;

    if (t == 2 || t == 3 || t == 4 || t == 5 || t == 6)
        return 1;
    if ((t == 0 || t == 1) && br[0x14D])
        return 1;
    return 0;
}

/* ------------------------------------------------------------------ 0x24BBA0 */
void ailow_24BBA0(uchar *br)
{
    short e = G4_BENT(br);
    uchar t;

    if (!br[0xCA] || br[5] != 2)
        return;
    t = G4_ENT(e).type;
    if ((t == 5 || t == 6) && G4_S32(br, 0x13C) != 0)
        return;
    if (G4_ENT(e).type == 4 && G4_S32(br, 0x140) == 0)
        return;
    if (G4_U32(br, 0xF6) + 250 >= game_time)
        return;
    if (!G4_ENT(br[0xCA]).f21)
        return;
    t = G4_ENT(e).type;
    G4_U32(br, 0xF6) = game_time + ((t == 5 || t == 6) ? 50 : 250);
    aircraft_284E78(G4_ENT(e).slot, 0, 0, 0, br[0xCA], 0);
    if ((ushort)hit_who == br[0xCA] || hit_who == 0)
        aircraft_284E78(G4_ENT(e).slot, 0, 0, 0, br[0xCA], 1);
}

/* ------------------------------------------------------------------ 0x24BD00 */
uchar ailow_24BD00(uchar *br)
{
    uchar res = 0, i;
    ulong t;

    if (br[0x14D]) {
        if (br[0xCA])
            res = 1;
        br[0xCA] = 0;
        br[0xC9] = 0;
        return res;
    }
    if (br[0xCA] && br[5] == 2 && G4_ENT(br[0xCA]).active) {
        if (G4_U32(br, 0x148) == 0)
            G4_U32(br, 0x148) = game_time + 10000;
        t = game_time;
        if (t > G4_U32(br, 0x148)) {
            G4_U32(br, 0x148) = t + 5000;
            aiperc_257AB4(G4_SQUAD(br[6]));
        }
    }
    if (br[0xCA] && !G4_ENT(br[0xCA]).active) {
        br[0xCA] = 0;
        br[0xC9] = 0;
        res = 1;
    }
    if (game_time > G4_U32(br, 0x144)) {
        long r = RndRange(game_rnd_text, 0, 0x7FFF) % 1000;
        G4_U32(br, 0x144) = game_time + 5000 + r;
        for (i = 0; i < GAME_MAX_BRAINS; i++) {
            uchar *ob = G4_BRAIN(i);
            short e = G4_BENT(br), oe = G4_BENT(ob);

            if (!ob[0] && !ob[0xA])
                continue;
            if (br[5] == ob[5] || ob[0xD] != 1)
                continue;
            if (ailow_24ADE0(ob))
                continue;
            if (fix_fast_pyth_dist(ENTX(e) - ENTX(oe), ENTY(e) - ENTY(oe)) < 0x190000) {
                br[0xCA] = ob[0xB];
                res = 1;
                br[0xC9] = i;
            }
        }
    }
    return res;
}

/* ------------------------------------------------------------------ 0x24BF3C */
void ailow_24BF3C(uchar *br, long force)
{
    short e = G4_BENT(br);
    fix tx, ty, px, py, alt, dx, dy, d, vx, vy, vz, lim, h, gx, gy, newz, step, rate;
    ushort ang, d1, d2, diff, hd;
    State st, st2;
    uchar t;

    if (br[0xCA]) {
        tx = ENTX(br[0xCA]);
        ty = ENTY(br[0xCA]);
    } else {
        uchar *sq = G4_SQUAD(br[6]);
        tx = G4_SQCENTRE(G4_S16(sq, 0x91));
        ty = G4_SQCENTRE(G4_S16(sq, 0x93));
    }
    px = tx + G4_FIXMUL(fix_cos(G4_U16(br, 0x13A)), G4_S32(br, 0x13C));
    py = ty + G4_FIXMUL(fix_sin(G4_U16(br, 0x13A)), G4_S32(br, 0x13C));
    if (fix_fast_pyth_dist(px - ENTX(e), py - ENTY(e)) <= 0x20000 || (uchar)force) {
        switch (G4_ENT(e).type) {
        case 1:
            G4_S32(br, 0x13C) = 0;
            break;
        case 2:
            if (br[0x14C])
                br[0x13B] += 0x20;
            else
                G4_U16(br, 0x13A) -= 0x2000;
            G4_S32(br, 0x13C) = 0x50000;
            break;
        case 3:
        case 4:
            if (G4_U32(br, 0x140) == 0) {
                long r = RndRange(game_rnd_text, 0, 0x7FFF) % 3000;
                G4_U32(br, 0x140) = game_time + 3000 + r;
            }
            if (game_time > G4_U32(br, 0x140)) {
                G4_U32(br, 0x140) = 0;
                G4_S16(br, 0x13A) = (short)(RndRange(game_rnd_text, 0, 0x7FFF) * 2);
            }
            G4_S32(br, 0x13C) = 0x50000;
            break;
        case 5:
        case 6:
            if (G4_U32(br, 0x140) == 0) {
                long r = RndRange(game_rnd_text, 0, 0x7FFF) % 5000;
                G4_U32(br, 0x140) = game_time + 5000 + r;
            }
            if (G4_S32(br, 0x13C) == 0) {
                G4_S32(br, 0x13C) = 0x80000;
            } else if (game_time > G4_U32(br, 0x140)) {
                G4_U32(br, 0x140) = 0;
                G4_S32(br, 0x13C) = 0;
                br[0x13B] += 0x80;
            } else if (br[0x14C]) {
                br[0x13B] += 0x20;
            } else {
                G4_U16(br, 0x13A) -= 0x2000;
            }
            break;
        default:
            G4_S32(br, 0x13C) = 0x50000;
            br[0x13B] += 0x20;
            break;
        }
        px = tx + G4_FIXMUL(fix_cos(G4_U16(br, 0x13A)), G4_S32(br, 0x13C));
        py = ty + G4_FIXMUL(fix_sin(G4_U16(br, 0x13A)), G4_S32(br, 0x13C));
    }
    t = G4_ENT(e).type;
    switch (t) {
    case 0:
    case 1:
        alt = air_alt;
        break;
    case 3:
    case 4:
        alt = 0x35000;
        break;
    case 5:
    case 6:
        d = fix_fast_pyth_dist(ENTX(br[0xCA]) - ENTX(e), ENTY(br[0xCA]) - ENTY(e));
        if (d > G4_S32(br, 0x13C)) {
            alt = 0x60000;
        } else {
            fix q = G4_FIXDIV(d, 0x80000);
            q = G4_FIXMUL(q, q);
            alt = G4_FIXMUL(q, 0x60000) + G4_FIXMUL(0x10000 - q, 0x4000);
        }
        break;
    default:
        alt = 0xB000;
        break;
    }
    dx = px - ENTX(e);
    dy = py - ENTY(e);
    if ((dx < 0 ? -dx : dx) > 0x800000 || (dy < 0 ? -dy : dy) > 0x800000) {
        dx >>= 7;
        dy >>= 7;
    }
    ang = fix_atan2(dy, dx);
    hd = G4_ENT(e).heading;
    d1 = (ushort)(ang - hd);
    d2 = (ushort)(hd - ang);
    diff = d2 <= d1 ? d2 : d1;
    rate = (long)(game_frame_dt << 16) >> 10;
    step = G4_FIXMUL(0x4000, rate);
    ai_turn_step = (ushort)step;
    if (diff <= (ushort)step)
        G4_ENT(e).heading = ang;
    else if (d1 < 0x8000)
        G4_ENT(e).heading += (ushort)step;
    else
        G4_ENT(e).heading -= (ushort)step;
    EDMS_get_state(G4_ENT(e).body, &st2);   /* result unused */
    if (fix_fast_pyth_dist(px - ENTX(e), py - ENTY(e)) > 0x10000) {
        vx = G4_FIXMUL(fix_cos(ang), 0xF0000);
        vy = G4_FIXMUL(fix_sin(ang), 0xF0000);
    } else {
        vx = vy = 0;
    }
    gx = ENTX(e) + G4_FIXMUL(fix_cos(ang), 0x14000);
    gy = ENTY(e) + G4_FIXMUL(fix_sin(ang), 0x14000);
    h = map_2949C4(gx, gy);
    vz = h + alt - ENTZ(e);
    lim = G4_FIXMUL(0x20000, rate);
    if (vz > lim)
        vz = lim;
    else if (vz < -lim)
        vz = -lim;
    newz = ENTZ(e) + vz;
    d = alt + h - ENTZ(e);
    /* original: the clamp's second test reuses the flags of the first, so anything below
     * 2.0 becomes -2.0 */
    d = d >= 0x20000 ? 0x20000 : -0x20000;
    st.X = ENTX(e) * 6;
    st.Y = ENTY(e) * 6;
    st.Z = newz * 6;
    st.X_dot = vx;
    st.Y_dot = vy;
    st.Z_dot = d;
    EDMS_holistic_teleport(G4_ENT(e).body, &st);
    if (RndRange(game_rnd_text, 0, 0x7FFF) % 500 == 100)
        br[0x14C] = br[0x14C] == 0;
}

/* ================================================================== brain think (0x24C618 ..) */

#define SQ(br)  G4_SQUAD((br)[6])
#define ORD(br, i) ((br) + 0x30 + (i) * 0x24)
#define AILOW_C "r:\\prj\\tn\\v109\\tn\\src\\tn\\ailow.c"

static long iabs32(long v) { return v < 0 ? -v : v; }

/* terrain height of map square (x, y): word +1 of the 3-byte cells of the map rows (+0x84E) */
static long map_h(long x, long y)
{
    uchar *row = *(uchar **)(game_map_35BFB4 + 0x84E + y * 4);
    return (short)(G4_S16(row, x * 3 + 1) >> 4);
}

/* fix_atan2(dy, dx), both scaled down by 128 when a component is above 0x800000 */
static fixang atan2_s(long dx, long dy)
{
    if (iabs32(dx) > 0x800000 || iabs32(dy) > 0x800000) {
        dx >>= 7;
        dy >>= 7;
    }
    return fix_atan2(dy, dx);
}

/* the smaller of the two angle differences (16-bit) */
static ushort angdiff(ushort a, ushort b)
{
    ushort d1 = (ushort)(a - b), d2 = (ushort)(b - a);
    return d1 < d2 ? d1 : d2;
}

/* note a heading for this frame's turn (ai_371188) if prio beats the current one */
static void note_heading(long prio, ushort h)
{
    if ((short)ai_37118C < prio) {
        ai_37118C = (uchar)prio;
        ai_371188 = h;
    }
}

static void ai_set_debug(uchar *addr)
{
    ulong bit;

    ai_cur_addr_a = G4_U16(addr, 0);
    ai_cur_addr_b = G4_U16(addr, 2);
    bit = 1u << (ai_cur_addr_b & 31);
    if (ai_cur_addr_a == 0x80)
        bit &= ai_debug_mask80;
    else if (ai_cur_addr_a == 0x81)
        bit &= ai_debug_mask81;
    else
        bit &= ai_debug_mask82;
    ai_debug_on = bit != 0;
}

/* fill an order record with a move to the point from 24E19C (kinds 2 / 4) */
static void order_move(uchar *br, uchar kind, Msg *m)
{
    uchar *o = ORD(br, aimain_24A0B8(br + 0x30));
    fix p[2];

    o[0] = kind;
    ailow_24E19C(br, p);
    G4_S32(o, 7) = p[0];
    G4_S32(o, 0xB) = p[1];
    G4_S16(o, 3) = (short)(G4_S32(o, 7) >> 16);
    G4_S16(o, 5) = (short)(G4_S32(o, 0xB) >> 16);
    o[0xF] = 1;
    o[1] = m->flag;
}

/* ------------------------------------------------------------------ 0x24C618 */
/* the brain's mailbox */
void ailow_24C618(uchar *br)
{
    Msg *m;

    while ((m = MsgReceive((MsgAddr *)(br + 1))) != 0) {
        uchar type = m->pad2;

        if (type == 0) {                                    /* move to a square */
            ushort pos[2];
            uchar b, *o;
            long i;

            if (SQ(br)[0xCB] == 4 || br[0xD] == 3)
                goto done;
            i = aimain_24A0B8(br + 0x30);
            aiacc_25AC98(m, pos, &b);
            o = ORD(br, i);
            o[0] = SQ(br)[0x90] == 1 ? 4 : 2;
            G4_U16(o, 3) = pos[0];
            G4_U16(o, 5) = pos[1];
            G4_S32(o, 7) = G4_SQCENTRE(pos[0]);
            G4_S32(o, 0xB) = G4_SQCENTRE(pos[1]);
            o[0xF] = 0;
            o[1] = m->flag;
        } else if (type == 4) {                             /* take the formation place */
            if (SQ(br)[0xCB] == 4)
                goto done;
            if (SQ(br)[0x90] == 1 && br[0xC3] && SQ(br)[0xCB] != 4)
                goto done;
            if (br[0xD] == 3)
                goto done;
            if (SQ(br)[0x90] == 1) {
                fix p[2];
                ailow_24E19C(br, p);
                if (!ailow_24FC04(br, 0, 0, 0, p[0], p[1], 0xA))
                    goto done;
                order_move(br, 4, m);
            } else {
                order_move(br, 2, m);
            }
        } else if (type == 0x32) {
            uchar res;
            if (br[0xC3] && !ailow_24FD3C(br, 1, 1, 0xA, &res))
                ailow_24FC04(br, 0, 0, 0, G4_SQCENTRE(G4_S16(br, 0xCC)),
                             G4_SQCENTRE(G4_S16(br, 0xCE)), 0xA);
        } else if (type == 0x1D) {
            uchar a, b, *o;
            long i = aimain_24A0B8(br + 0x30);
            aiacc_25ADF8(m, &a, &b);
            o = ORD(br, i);
            o[0] = 8;
            o[0xF] = a;
            o[1] = m->flag;
        } else if (type == 0x1E) {
            short a = 0;
            uchar b, *o;
            long i = aimain_24A0B8(br + 0x30);
            aiacc_25B034(m, &a, &b);
            o = ORD(br, i);
            o[0] = 0x10;
            G4_S16(o, 0xF) = a;
            o[1] = m->flag;
        } else if (type == 0xF) {                           /* hit by someone */
            uchar who, b2;
            uchar pb;
            aiacc_25ADF8(m, &who, &b2);
            pb = G4_ENT(game_player_entity).f21;
            if (br[0xD] == 1 && (G4_BRAIN(who)[0] || who == pb) && SQ(br)[0x90] == 0) {
                uchar *list = (uchar *)damage_27D158(G4_BENT(br));
                uchar i = 0, found = 0;
                while ((long)i < G4_S32(list, 8) && !found) {
                    if (G4_S16(G4_U32(list, 0xC) + i * 6, 0) == G4_S16(G4_BRAIN(who), 0xB))
                        found = 1;
                    i++;
                }
                if (G4_S16(SQ(br), 0xA9) == 0 && !found && SQ(br)[0xCB] != 7 &&
                    ailow_2508D0(G4_BENT(br), G4_S16(G4_BRAIN(who), 0xB))) {
                    Msg *m2 = MsgAlloc();
                    if (m2) {
                        aiacc_25AC6C(m2, G4_U32(br, 1), G4_U32(SQ(br), 3), G4_BRAIN(who) + 0xE, 0);
                        m2->flag = 7;
                        MsgSend(m2);
                    }
                }
            }
            if (G4_BRAIN(who)[0] || who == G4_ENT(game_player_entity).f21) {
                if (SQ(br)[0x10F]) {
                    if (SQ(br)[0x10C])
                        aimain_24A6A0(br[3], 0x23);
                    SQ(br)[0x10F] = 0;
                }
                if (br[0xC3] && br[0xCA])
                    ailow_24DD34(br);
                else
                    ailow_24DAC4(br, 3, 1);
            }
            if (ai_371170 >= game_frame)
                goto done;
            if (br[0xD] == 1 && who == G4_ENT(game_player_entity).f21 && br[5] == br[0xD] &&
                (!br[0x9C] || ai_371184 == 0) && ai_371180 + 1000 < game_time) {
                Msg *m2;
                ai_371180 = game_time;
                if (++ai_371184 >= 3) {
                    game_441365 = 1;
                    aimain_24A184();
                    SQ(br)[0xCB] = 1;
                    SQ(br)[0xC9] = 1;
                    m2 = MsgAlloc();
                    if (m2) {
                        aiacc_25AE48(m2, G4_U32(br, 1), G4_U32(SQ(br), 3),
                                     G4_ENT(game_player_entity).f21, br[3]);
                        MsgSend(m2);
                    }
                } else {
                    m2 = MsgAlloc();
                    if (m2) {
                        ulong to = 0x80 | (ulong)G4_ENT(game_player_entity).f21 << 16;
                        aiacc_25AC50(m2, G4_U32(br, 1), to, (uchar)ai_371184, 0x16);
                        MsgSend(m2);
                    }
                }
            }
            ai_371170 = game_frame;
        } else if (type == 0x10) {                          /* sees an enemy */
            uchar b;
            aiacc_25AC64(m, &b);
            if (b && br[5] == 1 && G4_BRAIN(b)[5] == 2) {
                short e = G4_S16(G4_BRAIN(b), 0xB);
                uchar t = G4_ENT(e).type;
                if (G4_ENT(e).cls == 3 &&
                    (t == 0 || t == 1 || t == 2 || t == 3 || t == 4 || t == 5 || t == 6))
                    aimain_24A6A0(br[3], 0x2A);
                else
                    aimain_24A6A0(br[3], 0x3B);
            }
        } else if (type == 0x17) {                          /* damaged */
            short w68 = 0, w64 = 0, w60;
            short avg, worst, armour, ammo;
            uchar *sq = SQ(br), bb, res;

            aiacc_25AF58(m, &w68, &w64, &w60);
            G4_U32(sq, 0xF3) += (ulong)(ushort)w64 - (ushort)w68;
            if (G4_U32(sq, 0xF3) < G4_U32(sq, 0xF7) && G4_U32(sq, 0xF3) > G4_U32(sq, 0xFC) &&
                (sq[0xFB] == 0 || sq[0xFB] == 1)) {
                if (sq[0x10] == 1)
                    aimain_24A6A0(br[3], 0x38);
                G4_U32(sq, 0xFC) += 100;
                sq[0xFB] = 2;
            }
            {
                Msg *m2 = MsgAlloc();
                if (m2) {
                    m2->pad2 = 0x60;
                    G4_U32(m2, 8) = G4_U32(sq, 3);
                    if (aimsg_25A150(m2))
                        aimsg_25A22C(sq);
                }
            }
            music_2469E4(3, 1);
            bb = sq[0xCB];
            if (bb != 5 && bb != 9) {
                if (br[0xC3] && br[0xCA] && bb != 4) {
                    ailow_24AFB4(br, &avg, &worst, &armour);
                    if (SQ(br)[0xCB] == 7 || (worst <= 4 && br[5] == 1))
                        ailow_24FD3C(br, 0, 0xA, 2, &res);
                    else
                        ailow_24FD3C(br, 1, 0xA, 2, &res);
                } else {
                    ailow_24DAC4(br, 3, 1);
                }
            }
            if (!br[0xCA] && br[0xD] != 2)
                note_heading(3, (ushort)(G4_ENT(G4_BENT(br)).heading + 0x8000));
            if (br[0xD] == 1 && br[5] == 1 && !br[0xA] && G4_U32(br, 0x1F) > 1) {
                ailow_24AFB4(br, &avg, &worst, &armour);
                if (armour < 0x32)
                    ailow_24AE4C(br[3], 2, 1);
                ailow_24AF24(br, &ammo);
                if (ammo < 0x32)
                    ailow_24AE4C(br[3], 5, 1);
                if (armour < 0x4B && br[0xC3] && game_time > G4_U32(br, 0x2B)) {
                    ailow_24AE4C(br[3], 0xA, 1);
                    G4_U32(br, 0x2B) = game_time + 20000;
                }
                if (G4_U32(br, 0x1F) > 4 && worst == 4)
                    aimain_24A6A0(br[3], SQ(br)[0x10] == 1 ? 0x2D : 0x32);
                else if (G4_U32(br, 0x1F) > 3 && worst <= 3)
                    aimain_24A6A0(br[3], 0x33);
                if (G4_U32(br, 0x23) > 2 && armour <= 2)
                    aimain_24A6A0(br[3], 0x34);
                G4_S32(br, 0x1F) = worst;
                G4_S32(br, 0x23) = armour;
            }
        } else if (type == 0x18) {
            uchar a, b, c;
            aiacc_25AF90(m, &a, &b, &c);
            ailow_24DAC4(br, 1, 0);
        } else if (type == 0x1F) {                          /* shot at */
            short ent = 0, amount = 0;
            short avg, worst, armour;
            uchar *sq = SQ(br), b;

            aiacc_25B060(m, &ent, &amount);
            G4_U32(sq, 0xF7) += (ushort)amount;
            ailow_24AFB4(br, &avg, &worst, &armour);
            if (worst > 4 && armour > 5 && G4_U32(sq, 0xF7) > G4_U32(sq, 0xF3) &&
                G4_U32(sq, 0xF7) > G4_U32(sq, 0xFC) && (sq[0xFB] == 0 || sq[0xFB] == 2)) {
                if (game_entities[(ushort)ent].cls != 4)
                    aimain_24A6A0(br[3], sq[0x10] == 1 ? 0x37 : 0x36);
                G4_U32(sq, 0xFC) += 100;
                sq[0xFB] = 1;
            }
            b = game_entities[(ushort)ent].f21;
            if (b && G4_BRAIN(b)[5] == br[5] && br[0x9C] == 1) {
                long r = RndRange(game_rnd_text, 0, 0x7FFF) % 4;
                fix x = G4_S32(br, 0xA5), y = G4_S32(br, 0xA9);
                switch (r) {
                case 0: x += RndRange(game_rnd_text, 0, 0x7FFF) % 0x10000; break;
                case 1: x -= RndRange(game_rnd_text, 0, 0x7FFF) % 0x10000; break;
                case 2: y += RndRange(game_rnd_text, 0, 0x7FFF) % 0x10000; break;
                case 3: y -= RndRange(game_rnd_text, 0, 0x7FFF) % 0x10000; break;
                }
                ailow_24FC04(br, 0, 0, 0, x, y, 2);
            }
        } else if (type == 0x13) {
            aiacc_25AEEC();
            if (G4_BRAIN(SQ(br)[0xF])[7] == 0) {
                aimain_24A6A0(br[3], 0x19);
            } else {
                aimain_24A6A0(br[3], 0x18);
                ailow_253334(br[3]);
            }
        } else if (type == 0x14) {
            uchar b;
            aiacc_25AC64(m, &b);
            aimain_24A6A0(br[3], 0x16);
            ailow_2534A4(br[3], b);
        } else if (type == 0x20) {                          /* stop */
            uchar *o = ORD(br, aimain_24A0B8(br + 0x30));
            o[0] = 0;
            o[1] = 0;
            br[0xC3] = 0;
            br[0xCB] = 0;
            G4_U16(br, 0xCC) = 0xFFFF;
            G4_U16(br, 0xCE) = 0xFFFF;
            br[0xC9] = 0;
            br[0xCA] = 0;
        }
done:
        MsgFree(m);
    }
}

/* ------------------------------------------------------------------ 0x24D5B8 */
/* per-frame bookkeeping: turn step, map square, held-fire weapons, marble control */
void ailow_24D5B8(uchar *br)
{
    short e = G4_BENT(br);
    fix dt;

    ai_37118C = 0;
    dt = G4_FIXDIV(game_frame_dt, 0x100);
    ai_turn_step = G4_ENT(e).cls == 2 ? 0x200 : 0x4000;
    ai_turn_step = (ushort)G4_FIXMUL((ulong)ai_turn_step, dt);
    G4_S16(br, 0xE) = (short)(G4_ENT(e).x >> 16);
    G4_S16(br, 0x10) = (short)(G4_ENT(e).y >> 16);
    if (SQ(br)[0xCB] == 6)
        ailow_24AE4C(br[3], 6, br[0x9C] == 1);
    ailow_24AE4C(br[3], 8, br[0x9C] == 1);
    if (game_frame == 5)
        G4_U32(br, 0x12) = G4_U32(br, 0xE);
    if (ai_371184 > 0 && ai_371180 + 60000 < game_time)
        ai_371184 = 0;
    switch (br[0xD]) {
    case 1:
    case 2:
        EDMS_control_marble(G4_ENT(e).body, 0, 0, 0);
        break;
    case 4:
        EDMS_marble_set_I11(G4_ENT(e).body, 0, 0, 0);
        break;
    }
}

/* ------------------------------------------------------------------ 0x24D750 */
/* stuck check: every 2 s a soldier that has not got nearer its target is pushed or its path
 * dropped */
void ailow_24D750(uchar *br)
{
    uchar flag = 0;
    uchar *sq = SQ(br), *path;
    short e = G4_BENT(br);
    fix d1, d2;

    if (br[0xD] != 1 || br[0x116] == 2 || br[0x30] == 1 || br[0x30] == 0 ||
        G4_U32(br, 0x16) == 0 || sq[0x157] == 3 || (br[0x9C] == 1 && br[0xD0] == 0) ||
        sq[0xE7] != 0)
        goto record;
    if (game_time < 10000) {
        flag = 1;
    } else if (G4_U32(br, 0x129)) {
        if (G4_U32(br, 0x129) + 750 < game_time) {
            G4_U32(br, 0x129) = 0;
            flag = 1;
        }
    } else {
        path = *(uchar **)(br + 0xAD);
        if ((!path || !*path) && br[0x116] != 1) {
            flag = 1;
        } else {
            d1 = fix_fast_pyth_dist(G4_S32(br, 0x12D) - G4_S32(br, 0xA5),
                                    G4_S32(br, 0x131) - G4_S32(br, 0xA9)) - 0x800;
            d2 = fix_fast_pyth_dist(ENTX(e) - G4_S32(br, 0xA5), ENTY(e) - G4_S32(br, 0xA9));
            if (d2 < d1) {
                flag = 1;
            } else if (game_time - G4_U32(br, 0x125) > 2000) {
                G4_U32(br, 0x129) = game_time;
                if (d2 < 0x10000 && sq[0x157] != 1 && sq[0x157] != 2) {
                    *BRPATH(br) = 0;
                    br[0xB1] = 0;
                    br[0xD0] = 0;
                } else {
                    physics_2D467C(G4_BENT(br));
                }
                flag = 1;
            }
        }
    }
    if (!flag)
        return;
record:
    G4_U32(br, 0x125) = game_time;
    G4_S32(br, 0x12D) = ENTX(G4_BENT(br));
    G4_S32(br, 0x131) = ENTY(G4_BENT(br));
}

/* ------------------------------------------------------------------ 0x24D98C */
/* side 2 idlers wander a square around the point where they stopped */
void ailow_24D98C(uchar *br)
{
    ushort p[2];
    uchar *o;

    if (br[0x9C] || br[0xD] == 3 || br[5] != 2 || G4_U32(br, 0x9D) != G4_U32(br, 0xE))
        return;
    if (iabs32(G4_S16(br, 0xDD) - G4_S16(br, 0xE)) > 2 ||
        iabs32(G4_S16(br, 0xDF) - G4_S16(br, 0x10)) > 2)
        G4_U32(br, 0xDD) = G4_U32(br, 0xE);
    if (game_time <= G4_U32(br, 0xE1))
        return;
    *(ulong *)p = G4_U32(br, 0xDD);
    switch (RndRange(game_rnd_text, 0, 0x7FFF) % 4) {
    case 0: p[0] += 2; break;
    case 1: p[0] -= 2; break;
    case 2: p[1] += 2; break;
    case 3: p[1] -= 2; break;
    }
    o = ORD(br, aimain_24A0B8(br + 0x30));
    G4_U32(o, 3) = *(ulong *)p;
    G4_S32(o, 7) = G4_SQCENTRE(p[0]);
    o[0] = 2;
    o[1] = 6;
    o[0xF] = 0;
    G4_S32(o, 0xB) = G4_SQCENTRE(p[1]);
}

/* ------------------------------------------------------------------ 0x24DAC4 */
/* raise the brain's alarm level (+0xB4); with rnd a reaction delay 0 / 0.1 / 0.2 / 1 s */
void ailow_24DAC4(uchar *br, long level, long rnd)
{
    uchar bb = SQ(br)[0xCB];

    if (bb == 5 || bb == 9)
        return;
    if ((short)br[0xB4] >= (short)level)
        return;
    br[0xB4] = (uchar)level;
    if (!(uchar)rnd) {
        G4_U32(br, 0xE9) = 0;
        return;
    }
    if (G4_U32(br, 0xE9) != 0)
        return;
    switch (RndRange(game_rnd_text, 0, 0x7FFF) % 4) {
    case 0: G4_U32(br, 0xE9) = game_time; break;
    case 1: G4_U32(br, 0xE9) = game_time + 100; break;
    case 2: G4_U32(br, 0xE9) = game_time + 200; break;
    case 3: G4_U32(br, 0xE9) = game_time + 1000; break;
    }
}

/* ------------------------------------------------------------------ 0x24DBC4 */
/* alarmed soldier: jump aside by +0xB4 squares once the delay is over */
void ailow_24DBC4(uchar *br)
{
    long r;
    short prio;
    fix x, y;

    if (br[0xD] != 1 || br[0xB4] == 0 || game_time <= G4_U32(br, 0xE9))
        return;
    r = RndRange(game_rnd_text, 0, 0x7FFF) % 4;
    prio = (br[0xB4] > 1) + 1;
    x = G4_S32(br, 0xA5);
    y = G4_S32(br, 0xA9);
    switch (r) {
    case 0: x += (long)br[0xB4] << 16; break;
    case 1: x -= (long)br[0xB4] << 16; break;
    case 2: y += (long)br[0xB4] << 16; break;
    case 3: y -= (long)br[0xB4] << 16; break;
    }
    ailow_24FC04(br, 0, 0, 0, x, y, prio);
    br[0xB4] = 0;
}

/* ------------------------------------------------------------------ 0x24DCBC */
void ailow_24DCBC(uchar *br)
{
    if (game_time < G4_U32(br, 0x136)) {
        if (br[0xD] == 1 && G4_ENT(G4_BENT(br)).type == 6)
            pbasim_28A878(G4_ENT(G4_BENT(br)).slot, 0, 0, 0, 0, 1, 1);
    } else {
        br[0x135] = 0;
    }
}

/* ------------------------------------------------------------------ 0x24DD18 (no callers) */
void ailow_24DD18(uchar *br)
{
    aimain_249FD0(br + 0x30);
    ailow_252E60(br);
    ailow_252F78(br);
}

/* ------------------------------------------------------------------ 0x24DD34 */
void ailow_24DD34(uchar *br)
{
    uchar bb = SQ(br)[0xCB];

    if (bb == 9 || bb == 5 || br[0xD] != 1 || G4_U32(br, 0xE5) != 0)
        return;
    switch (RndRange(game_rnd_text, 0, 0x7FFF) % 3) {
    case 0:     /* the original falls through every case: always game_time + 500 */
        G4_U32(br, 0xE5) = game_time;
    case 1:
        G4_U32(br, 0xE5) = game_time + 200;
    case 2:
        G4_U32(br, 0xE5) = game_time + 500;
    }
}

/* ------------------------------------------------------------------ 0x24DDD8 */
void ailow_24DDD8(uchar *br)
{
    short avg, worst, armour;
    uchar res;

    if (!br[0xC3]) {
        G4_U32(br, 0xE5) = 0;
        return;
    }
    if (G4_U32(br, 0xE5) == 0 || G4_U32(br, 0xE5) >= game_time)
        return;
    ailow_24AFB4(br, &avg, &worst, &armour);
    G4_U32(br, 0xE5) = 0;
    if (SQ(br)[0xCB] == 7 || (worst <= 4 && br[5] == 1))
        ailow_24FD3C(br, 0, 0xA, 2, &res);
    else
        ailow_24FD3C(br, 1, 0xA, 2, &res);
}

/* ------------------------------------------------------------------ 0x24DEA0 */
void ailow_24DEA0(uchar *br)
{
    if (SQ(br)[0x167] > 0 && br[0xD] == 1 && br[0x116] == 0 &&
        G4_U16(&game_pba[G4_ENT(G4_BENT(br)).slot], 0xAA) != 0)
        ailow_24F778(br);
}

/* ------------------------------------------------------------------ 0x24DF10 */
void AISoldierThink(uchar *br)
{
    ai_set_debug(br + 1);
    ailow_24D5B8(br);
    if (SQ(br)[2]) {
        MsgFlush((MsgAddr *)(br + 1));
        return;
    }
    if (br[0xD] == 4) {
        if (ailow_24BB30(br[3]))  {
            ailow_24BF3C(br, ailow_24BD00(br));
            ailow_24BBA0(br);
        }
        MsgFlush((MsgAddr *)(br + 1));
        return;
    }
    ailow_24C618(br);
    if (br[0x135]) {
        ailow_24DCBC(br);
        return;
    }
    ailow_24DDD8(br);
    ailow_24DEA0(br);
    aimain_249FD0(br + 0x30);
    ailow_252E60(br);
    ailow_252F78(br);
    ailow_24E774(br);
    ailow_24DBC4(br);
    ailow_24D750(br);
    ailow_24D98C(br);
}

/* ------------------------------------------------------------------ 0x24E040 */
/* entered a new map square: tell every squad of the side (message 4) */
void ailow_24E040(uchar *br)
{
    short e = G4_BENT(br);
    long x = ENTX(e) >> 16, y = ENTY(e) >> 16;
    uchar sq;

    if (G4_S16(br, 0xE) == (short)x && G4_S16(br, 0x10) == (short)y)
        return;
    for (sq = G4_SIDE(br[5])[6]; sq; sq = G4_SQUAD(sq)[0xE]) {
        Msg *m = MsgAlloc();
        if (!m)
            break;
        aiacc_25AD68(m, G4_U32(br, 1), G4_U32(G4_SQUAD(sq), 3));
        m->flag = 7;
        MsgSend(m);
    }
    G4_S16(br, 0xE) = (short)x;
    G4_S16(br, 0x10) = (short)y;
}

/* ------------------------------------------------------------------ 0x24E100 */
/* the point dist squares from the player at angle ang relative to his heading */
void ailow_24E100(long ang, long dist, fix *out)
{
    short p = game_player_entity;
    fix r = (fix)((ulong)(long)(short)dist << 16);
    fix c = G4_FIXMUL(fix_cos((ushort)(G4_ENT(p).heading + ang)), r);
    fix s = G4_FIXMUL(fix_sin((ushort)(ang + G4_ENT(p).heading)), r);

    out[0] = ENTX(p) + c;
    out[1] = ENTY(p) + s;
}

/* ------------------------------------------------------------------ 0x24E19C */
/* the brain's place in the squad formation.  The original returns the 8-byte point through a
 * buffer in esi (Watcom struct return); W_ailow_24E19C below is that entry. */
#define FORM(i) ai_25AB88[i]        /* [type * 6 + rank] (rank 1-based); [18 + rank] = type 3 */

void ailow_24E19C(uchar *br, fix *out)
{
    uchar *sq = SQ(br);
    fix x0 = G4_S32(sq, 0x16), y0 = G4_S32(sq, 0x1A), x, y;
    ushort ang;
    uchar *p;
    ushort xy[2];
    short d;
    long h1, h2;

    if (br[8] == 1 && sq[0x157] == 2 && G4_S16(sq, 0xA9) != 0 &&
        iabs32(G4_S16(sq, 0x91) - G4_S16(sq, 0x12)) + iabs32(G4_S16(sq, 0x93) - G4_S16(sq, 0x14)) < 2) {
        short t = G4_S16(sq, 0xA9);
        out[0] = ENTX(t);
        out[1] = ENTY(t);
        return;
    }
    p = *(uchar **)(sq + 0x95);
    if (*p)
        ang = (ushort)((p[0xB + G4_S16(p, 7) * 3] >> 3) << 11);
    else
        ang = 0;
    if (sq[0x1E] == 2 && sq[0xAE]) {
        uchar *o = G4_SQUAD(sq[0xAE]);
        ang = atan2_s(G4_S32(o, 0x16) - G4_S32(sq, 0x16), G4_S32(o, 0x1A) - G4_S32(sq, 0x1A));
    }
    if (br[0xD] == 2)
        path_25BE1C(G4_FIXMUL(0xC0000, FORM(sq[0x1E] * 6 + br[8])[0]), FORM(sq[0x1E] * 6 + br[8])[1],
                    ang, x0, y0, &x, &y);
    else
        path_25BE1C(FORM(sq[0x1E] * 6 + br[8])[0], FORM(sq[0x1E] * 6 + br[8])[1], ang, x0, y0, &x, &y);
    xy[0] = (ushort)(x >> 16);
    xy[1] = (ushort)(y >> 16);
    d = path_25CA34(G4_U32(sq, 0x12), *(ulong *)xy, sq[0xA6], ailow_253580(br));
    h1 = map_h(G4_S16(sq, 0x12), G4_S16(sq, 0x14));
    h2 = map_h((short)xy[0], (short)xy[1]);
    if (d > 100 || iabs32(h1 - h2) > 0x14)
        path_25BE1C(FORM(18 + br[8])[0], FORM(18 + br[8])[1], ang, x0, y0, &x, &y);
    out[0] = x;
    out[1] = y;
}

/* Watcom entry: eax = brain, esi = result buffer; returns eax = esi; esi is not preserved
 * (the original leaves it pointing into its frame), every other register is. */
#ifdef TN_WATCOM_ABI
__asm__(".text\n.globl W_ailow_24E19C\n"
        "W_ailow_24E19C:\n"
        "\tpushl %ecx\n\tpushl %edx\n\tpushl %esi\n"
        "\tpushl %esi\n\tpushl %eax\n"
        "\tcall ailow_24E19C\n"
        "\taddl $8, %esp\n"
        "\tpopl %eax\n\tpopl %edx\n\tpopl %ecx\n"
        "\tret\n");
#else
#endif

/* ------------------------------------------------------------------ 0x24E5B4 */
/* the next point of the brain's path (or its target when the path is used up or the brain is
 * within 2 squares of (dx, dy)), nudged half a square away from steep terrain */
void ailow_24E5B4(uchar *br, long dx, long dy, fix *ox, fix *oy)
{
    uchar *path, *rec;

    if (iabs32((short)dx) + iabs32((short)dy) <= 2) {
        *ox = G4_S32(br, 0xA5);
        *oy = G4_S32(br, 0xA9);
        return;
    }
    path = BRPATH(br);
    if (G4_S16(path, 7) <= 0) {
        *ox = G4_S32(br, 0xA5);
        *oy = G4_S32(br, 0xA9);
    } else {
        rec = path + G4_S16(path, 7) * 3;
        *ox = (fix)((ulong)(G4_U16(rec, 9) & 0x1FF) << 16);
        rec = path + G4_S16(path, 7) * 3;
        *oy = (fix)((ulong)(((rec[0xB] & 3) << 7) + (G4_U16(rec, 9) >> 9)) << 16);
    }
    if (iabs32(map_h(*ox >> 16, *oy >> 16) - map_h((*ox + 0x10000) >> 16, *oy >> 16)) < 0x14)
        *ox += 0x8000;
    else
        *ox -= 0x8000;
    if (iabs32(map_h(*ox >> 16, *oy >> 16) - map_h(*ox >> 16, (*oy + 0x10000) >> 16)) < 0x14)
        *oy += 0x8000;
    else
        *oy -= 0x8000;
}

/* ------------------------------------------------------------------ 0x24E750 (no callers) */
uchar ailow_24E750(long heading, long prio)
{
    if ((short)prio > (short)(ushort)ai_37118C) {
        ai_37118C = (uchar)prio;
        ai_371188 = (ushort)heading;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ 0x24E774 */
/* turn towards the target / the squad's target / the noted heading */
void ailow_24E774(uchar *br)
{
    short e;
    uchar *sq;

    if (br[0xD] != 2 && br[0xCA]) {
        uchar t = br[0xCA];
        e = G4_BENT(br);
        note_heading(2, atan2_s(ENTX(t) - ENTX(e), ENTY(t) - ENTY(e)));
    }
    sq = SQ(br);
    if (sq[0x157] == 3 && G4_S16(sq, 0xA9) != 0) {
        short t = G4_S16(sq, 0xA9);
        e = G4_BENT(br);
        note_heading(3, atan2_s(ENTX(t) - ENTX(e), ENTY(t) - ENTY(e)));
    }
    if (ai_37118C > 0 && br[0xD] != 3) {
        ushort h, d;
        e = G4_BENT(br);
        h = G4_ENT(e).heading;
        d = (ushort)(ai_371188 - h);
        if (angdiff(ai_371188, h) <= ai_turn_step)
            G4_ENT(e).heading = ai_371188;
        else if (d < 0x8000)
            G4_ENT(e).heading += ai_turn_step;
        else
            G4_ENT(e).heading -= ai_turn_step;
    }
}

/* ------------------------------------------------------------------ 0x24E9B8 */
/* the brain of the squad with the given rank (0 if none) */
uchar ailow_24E9B8(long rank, long squad)
{
    uchar b = G4_SQUAD((uchar)squad)[0xF];

    while (b && (ushort)G4_BRAIN(b)[8] != (ushort)rank)
        b = G4_BRAIN(b)[7];
    return b;
}

/* ------------------------------------------------------------------ 0x24EA04 (no callers) */
/* above the ground by more than 8 height units */
uchar ailow_24EA04(uchar *br)
{
    short e = G4_BENT(br);
    short z = (short)((G4_ENT(e).z * 24) >> 16);

    return z > map_h(ENTX(e) >> 16, ENTY(e) >> 16) + 8;
}

/* ------------------------------------------------------------------ 0x24EA7C */
/* move the brain's marble towards its next path point (soldiers and vehicles).  (dx, dy) is
 * the distance to the target in squares (ailow_2500EC). */
void ailow_24EA7C(uchar *br, long dx_arg, long dy_arg)
{
    short e = G4_BENT(br);
    uchar *pba = br[0xD] == 1 ? (uchar *)&game_pba[G4_ENT(e).slot] + 0xA2 : 0;
    uchar low = 0, slow = 0;
    State st;
    fix vx, vy, tx, ty, ddx, ddy, x6, y6, dist, wx, wy, fx, fy, dvx, dvy, dv, k;
    long pd = 0;
    ushort hd;
    uchar *sq = SQ(br);

    EDMS_get_state(G4_ENT(e).body, &st);
    vx = st.X_dot;
    vy = st.Y_dot;
    if (sq[0xE7])
        return;
    if (br[0x116]) {                                        /* climbing: the stored point */
        tx = (fix)((ulong)(long)G4_S16(br, 0x11B) << 16);
        ty = (fix)((ulong)(long)G4_S16(br, 0x11D) << 16);
    } else {
        if (G4_S16(br, 0xB5)) {
            uchar b = G4_ENT(G4_S16(br, 0xB5)).f21;
            if (b && br[5] != G4_BRAIN(b)[5])
                G4_S16(br, 0xB5) = 0;
        }
        if (game_time - G4_U32(br, 0xBF) < 600) {
            /* the original computes a point 10 squares off (unused) then jitters by a square */
            ushort a = (ushort)(atan2_s(G4_S32(br, 0xB7) - ENTX(e), G4_S32(br, 0xBB) - ENTY(e)) + 0x6000);
            tx = ENTX(e) + G4_FIXMUL(fix_cos(a), 0xA0000);
            ty = ENTY(e) + G4_FIXMUL(fix_sin(a), 0xA0000);
            tx = ENTX(e) + ((RndRange(game_rnd_text, 0, 0x7FFF) & 3) - 1) * 0x10000;
            ty = ENTY(e) + ((RndRange(game_rnd_text, 0, 0x7FFF) & 3) - 1) * 0x10000;
        } else {
            ailow_24E5B4(br, (short)dx_arg, (short)dy_arg, &tx, &ty);
            G4_S16(br, 0xB5) = 0;
        }
    }
    ddx = tx - ENTX(e);
    ddy = ty - ENTY(e);
    x6 = ddx * 6;
    y6 = ddy * 6;
    hd = atan2_s(x6, y6);
    dist = fix_safe_pyth_dist(x6, y6, AILOW_C, 0xF88);
    if (br[0xCA] && br[0xD] != 2) {
        uchar t = br[0xCA];
        ushort a = atan2_s(ENTX(t) - ENTX(G4_BENT(br)), ENTY(t) - ENTY(G4_BENT(br)));
        note_heading(2, angdiff(a, hd) < 0x2000 ? hd : a);
    } else {
        note_heading(1, hd);
    }
    if (br[0x116] == 1) {
        if (fix_safe_pyth_dist(ddx, ddy, AILOW_C, 0xFAA) < 0x10000) {
            G4_U32(br, 0x117) = game_time;
            br[0x116] = 2;
            if (--sq[0x167] == 0)
                sq[0x169] = 1;
        }
        if (br[0x116] == 1) {
            if (st.Z_dot > 0x4000) {
                low = 0;
            } else {
                short z = (short)((ENTZ(e) * 24) >> 16);
                if (z < G4_S16(br, 0x11F)) {
                    low = 1;
                } else {
                    low = z < map_h(ENTX(e) >> 16, ENTY(e) >> 16) + 0x12;
                    if (!low) {
                        short x = (hd > 0 && hd <= 0x8000) ? G4_S16(br, 0xE) + 1 : G4_S16(br, 0xE) - 1;
                        short y = (hd > 0x4000 && hd <= 0xC000) ? G4_S16(br, 0x10) + 1 : G4_S16(br, 0x10) - 1;
                        low = z < map_h(x, y) + 0x12;
                    }
                }
            }
        }
    }
    if (pba)
        obj55_288EC0(pba, low);
    if (br[0x116] == 2) {
        if (!br[0x9C]) {
            uchar *o = ORD(br, aimain_24A0B8(br + 0x30));
            o[0] = 0;
            o[1] = 0;
        }
        dist = 0;
        if (sq[0x167] == 0) {
            sq[0x167] = 0;
            br[0x116] = 0;
        } else if (game_time - G4_U32(br, 0x117) > 5000 ||
                   fix_safe_pyth_dist(ddx, ddy, AILOW_C, 0x1023) < 0x40000) {
            br[0x116] = 0;
        }
    }
    if (dist > 0x1000) {
        if (br[0x116] == 1) {
            slow = 1;
            wx = G4_FIXMUL(fix_cos(hd), 0xA0000);
            wy = G4_FIXMUL(fix_sin(hd), 0xA0000);
        } else {
            wx = G4_FIXMUL(fix_cos(hd), 0x1E0000);
            wy = G4_FIXMUL(fix_sin(hd), 0x1E0000);
        }
    } else {
        wx = wy = 0;
    }
    if (br[0xD] == 1 && br[0x116] == 0 && !biped_27EDF4(e))
        return;
    dvx = wx - vx;
    dvy = wy - vy;
    dv = fix_safe_pyth_dist(dvx, dvy, AILOW_C, 0x1053);
    if (dv > 0) {
        if (br[0xD] == 1) {
            if (G4_S16(sq, 0xA9)) {
                short t = G4_S16(sq, 0xA9);
                pd = fix_fast_pyth_dist(G4_S16(br, 0xE) - (ENTX(t) >> 16),
                                        G4_S16(br, 0x10) - (ENTY(t) >> 16));
            }
            if (!br[0x9C] && game_player_entity == G4_S16(sq, 0xA9) && pd >= 2)
                k = 0xA00000;
            else
                k = 0x640000;
            fx = (fix)((long long)dvx * k / dv);
            fy = (fix)((long long)dvy * k / dv);
            if (br[5] == 2 && br[0xC3] && br[0xC4] && G4_U32(br, 0x16) > 0xBE)
                G4_U32(br, 0x16) = 0xB4;
            fx = (fix)((long long)fx * (long)(G4_U32(br, 0x16) << 16) / 0x1000000);
            fy = (fix)((long long)fy * (long)(G4_U32(br, 0x16) << 16) / 0x1000000);
        } else if (br[0xD] == 2) {
            fx = (fix)((long long)dvx * 0x320000 / dv);
            fy = (fix)((long long)dvy * 0x320000 / dv);
            G4_U32(br, 0x16) = 0x100;
            if (br[8] > 1) {
                uchar *ob = G4_BRAIN(ailow_24E9B8((short)(br[8] - 1), br[6]));
                long d = fix_fast_pyth_dist(G4_S16(br, 0xE) - G4_S16(ob, 0xE),
                                            G4_S16(br, 0x10) - G4_S16(ob, 0x10));
                if (d <= 3)
                    G4_U32(br, 0x16) = d <= 2 ? 0 : 0x80;
                if (d <= 8 && br[0xB3]) {
                    Msg *m = MsgAlloc();
                    if (m) {
                        aiacc_25AC50(m, G4_U32(br, 1), G4_U32(sq, 3), br[3], 1);
                        m->flag = 6;
                        MsgSend(m);
                        br[0xB3] = 0;
                    }
                }
            }
            fx = (fix)((long long)fx * (long)(G4_U32(br, 0x16) << 16) / 0x1000000);
            fy = (fix)((long long)fy * (long)(G4_U32(br, 0x16) << 16) / 0x1000000);
        } else {
            fx = (fix)((long long)dvx * 0x640000 / dv);
            fy = (fix)((long long)dvy * 0x640000 / dv);
        }
    } else {
        fx = fy = 0;
    }
    if (G4_U32(br, 0x129) && G4_U32(br, 0x129) + 300 > game_time) {
        slow = 1;
        wx = G4_FIXDIV(wx, 0x30000);
        wy = G4_FIXDIV(wy, 0x30000);
    }
    if (br[0xD] == 2 || (br[0xD] == 1 && G4_ENT(e).type == 9)) {
        ushort a = atan2_s(fx, fy);
        if (angdiff(a, G4_ENT(e).heading) > 0x1000) {
            fx = G4_FIXDIV(fx, 0x30000);
            fy = G4_FIXDIV(fy, 0x30000);
        }
    }
    if (br[0xD] == 1) {
        if (br[5] == 2 && !br[0x9C] && G4_S16(sq, 0xA9) == 0)
            PbaPlayerMove((uchar *)&game_pba[G4_ENT(e).slot] + 6, &fx, &fy);
        EDMS_control_marble(G4_ENT(e).body, fx, fy, G4_S32(pba, 0xC));
        if (slow)
            physics_2D4794(e, wx, wy);
        G4_S32(pba, 0xC) = 0;
        return;
    }
    EDMS_control_marble(G4_ENT(e).body, fx, fy, 0);
}

/* ------------------------------------------------------------------ path helpers */
/* a path record (ai_paths): +0 used, +1 goal (packed square), +5 count?, +7 current step
 * (word), +9 3-byte steps {word: x (9 bits) | y low 7 bits << 9; byte +2: y high 2 bits,
 * bit 2 = upper level / bridge} */
#define PSTEP(P, i) ((P) + (i) * 3)
#define PCUR(P) PSTEP(P, G4_S16(P, 7))
#define STEPX(r) (G4_U16(r, 9) & 0x1FF)
#define STEPY(r) ((((r)[0xB] & 3) << 7) + (G4_U16(r, 9) >> 9))

/* map cell of level lvl (0 ground, 1 upper: the rows at map +0x4A + lvl * 0x804) */
static uchar *map_cell(long lvl, long x, long y)
{
    return *(uchar **)(game_map_35BFB4 + 0x4A + lvl * 0x804 + y * 4) + x * 3;
}

static long cell_h(uchar *c)
{
    return (short)(G4_S16(c, 1) >> 4);
}

/* ------------------------------------------------------------------ 0x24F704 */
/* (re)plan the path's current leg */
void ailow_24F704(uchar *br)
{
    uchar *P = BRPATH(br);

    if (G4_S16(P, 7) == 0) {
        uchar r = ailow_253580(br);
        P = *(uchar **)(br + 0xAD);
        path_25BF04(G4_U32(br, 0xE), G4_U32(BRPATH(br), 1), br[0xB2], P, 1, r);
    } else {
        uchar r = ailow_253580(br);
        if (!path_25D35C(G4_U32(br, 0xE), br[0xB2], *(uchar **)(br + 0xAD), r))
            *BRPATH(br) = 0;
    }
}

/* ------------------------------------------------------------------ 0x24F778 */
/* a soldier whose next step is a climb (terrain kind in terrain_bitmask, or a step up more
 * than 0x14 / down more than 0x28) starts climbing (+0x116 = 1) towards the first step of the
 * slope; squads climb together (squad +0x167 climbers, +0x16A point) */
void ailow_24F778(uchar *br)
{
    uchar *P = BRPATH(br), *rec, *cur, *nxt, *prev;
    uchar *sq = SQ(br);
    short idx;
    ushort px, py;
    uchar first = 1;
    long n;

    if (!*P || G4_S16(P, 7) == -1 || br[0xD] == 2)
        return;
    if (br[0xD] == 1 && G4_ENT(G4_BENT(br)).type == 9)
        return;
    idx = G4_S16(P, 7);
    cur = map_cell(1, G4_S16(br, 0xE), G4_S16(br, 0x10));
    rec = PSTEP(P, idx);
    px = STEPX(rec);
    py = STEPY(rec);
    nxt = map_cell((rec[0xB] >> 2) & 1, (short)px, (short)py);
    if (!(terrain_bitmask & (1u << (game_41DD58[nxt[0] & 0x3F] & 31))) &&
        cell_h(nxt) - cell_h(cur) <= 0x14 && cell_h(cur) - cell_h(nxt) <= 0x28)
        return;
    if (sq[0x167] == 0) {
        n = 0;
        for (;;) {
            idx--;
            if (idx < 0) {
                if (br[0x9C] == 1 && (sq[0xCB] == 9 || sq[0xCB] == 5 || sq[0xCB] == 8)) {
                    idx = 0;
                    break;
                }
                if (!first) {
                    idx = 0;
                    break;
                }
                if (**(uchar **)(sq + 0x95) == 0) {
                    idx = 0;
                    break;
                }
                P = *(uchar **)(sq + 0x95);
                first = 0;
                idx = G4_S16(P, 7);
            }
            rec = PSTEP(P, idx);
            px = STEPX(rec);
            prev = nxt;
            py = STEPY(rec);
            nxt = map_cell((rec[0xB] >> 2) & 1, (short)px, (short)py);
            if ((terrain_bitmask & (1u << (game_41DD58[nxt[0] & 0x3F] & 31))) ||
                (short)iabs32(cell_h(nxt) - cell_h(prev)) > 0x14) {
                n = 0;
                continue;
            }
            n += (rec[0xB] & 4) ? 1 : 4;
            if (n >= 6)
                break;
        }
        if (PSTEP(P, idx)[0xB] & 4) {
            G4_U16(br, 0x11B) = px;
            G4_U16(br, 0x11D) = py;
        } else {
            G4_U16(br, 0x11B) = (ushort)((px - G4_U16(game_map_35BFB4, 8)) << 2);
            G4_U16(br, 0x11D) = (ushort)((py - G4_U16(game_map_35BFB4, 0xA)) << 2);
        }
        G4_S16(br, 0x11F) = (short)(cell_h(nxt) + 0x12);
        G4_U16(sq, 0x16A) = G4_U16(br, 0x11B);
        G4_U16(sq, 0x16C) = G4_U16(br, 0x11D);
        G4_U16(sq, 0x16E) = G4_U16(br, 0x11F);
    } else {
        G4_U16(br, 0x11B) = G4_U16(sq, 0x16A);
        G4_U16(br, 0x11D) = G4_U16(sq, 0x16C);
        G4_U16(br, 0x11F) = G4_U16(sq, 0x16E);
    }
    G4_U32(br, 0x117) = game_time;
    br[0x116] = 1;
    sq[0x167]++;
    *BRPATH(br) = 0;
}

/* ------------------------------------------------------------------ 0x24FC04 */
/* plan a path to (x, y) with priority prio (10 always replans; others wait 2 s unless higher) */
uchar ailow_24FC04(uchar *br, long u1, long u2, long u3, fix x, fix y, long prio)
{
    short e;
    fix d1, d2;

    if (br[0xD] == 3)
        return 0;
    if (!(game_time > G4_U32(br, 0xD1) || (short)prio > G4_S16(br, 0xD5) || (short)prio == 0xA))
        return 0;
    G4_S16(br, 0xD5) = (short)prio;
    G4_U32(br, 0xD1) = game_time + 2000;
    e = G4_BENT(br);
    d1 = fix_fast_pyth_dist(ENTX(e) - x, ENTY(e) - y);
    d2 = fix_fast_pyth_dist(G4_S32(br, 0xA5) - x, G4_S32(br, 0xA9) - y);
    if (!(d1 < 0x20000 && d1 > 0) && d2 <= G4_FIXDIV(d1, 0x60000))
        return 0;
    if (!ailow_2512F8(br, 0, 0, 0, x, y))
        return 0;
    br[0xD0] = 1;
    *BRPATH(br) = 0;
    br[0xB1] = 0;
    return 1;
}

/* ------------------------------------------------------------------ 0x24FCFC (no callers) */
uchar ailow_24FCFC(uchar *br, ulong sq_xy, long prio)
{
    return ailow_24FC04(br, 0, 0, 0, G4_SQCENTRE(sq_xy), G4_SQCENTRE(sq_xy >> 16), (short)prio);
}

/* ------------------------------------------------------------------ 0x24FD3C */
/* plan a route to a firing position on the target (+0xCA) once a second; *res = 1 when
 * PathPlan finds none */
uchar ailow_24FD3C(uchar *br, long flag, long mode, long prio, uchar *res)
{
    fix pos[3];
    long r25, hz;
    uchar f, r;
    ulong p;

    *res = 0;
    if (!(game_time > G4_U32(br, 0xD1) || (short)prio > G4_S16(br, 0xD5) || (short)prio == 0xA))
        return 0;
    if (G4_U32(br, 0x112) + 1000 >= game_time)
        return 0;
    if (!br[0xC9] || !br[0xCA])
        return 0;
    f = (uchar)flag ? 1 : 0;
    r25 = ailow_25129C(br);
    if ((uchar)flag)
        interact_298870(br[0xCA], pos);
    else
        interact_298854(br[0xCA], pos);
    hz = (short)((pos[2] * 24) >> 16);
    G4_U32(br, 0x112) = game_time;
    r = ailow_253580(br);
    p = PathPlan(G4_U32(br, 0xE), G4_U32(br, 0xE), G4_U32(G4_BRAIN(br[0xC9]), 0xE), 6, hz,
                 *(uchar **)(br + 0xAD), f, r, (short)mode, r25);
    if ((short)p == -1 && (short)(p >> 16) == -1)
        *res = 1;
    if ((short)p != -1 || (short)(p >> 16) != -1) {
        uchar *P = BRPATH(br);
        if (!(PCUR(P)[0xB] & 4) && *P)
            ailow_24F704(br);
        return ailow_24FC04(br, 0, 0, 0, G4_SQCENTRE(p), G4_SQCENTRE(p >> 16), (short)prio);
    }
    {
        /* no path buffer when PathPlan failed: DOS read (and wrote) low memory there */
        uchar *P = TN_LOWMEM(*(uchar **)(br + 0xAD));
        if (G4_S16(P, 5) == 0 || G4_S16(P, 7) <= 0)
            *P = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ 0x24FF24 */
/* the squad's centre: the leader for vehicle squads, else the mean of the members */
void ailow_24FF24(uchar *br, fix *out)
{
    uchar *sq = SQ(br);
    uchar b = sq[0xF];
    long i, n = 0;
    fix sx = 0, sy = 0;

    out[0] = ENTX(G4_BENT(br));
    out[1] = ENTY(G4_BENT(br));
    if (G4_BRAIN(b)[0xD] == 2) {
        for (i = 0; i < sq[0x10]; i++) {
            uchar *ob = G4_BRAIN(b);
            if (ob[0] && !ob[0xA] && ob[8] == 1) {
                out[0] = ENTX(G4_BENT(ob));
                out[1] = ENTY(G4_BENT(ob));
                return;
            }
            b = ob[7];
        }
        return;
    }
    for (i = 0; i < sq[0x10]; i++) {
        uchar *ob = G4_BRAIN(b);
        if (ob[0] && !ob[0xA]) {
            n++;
            sx += ENTX(G4_BENT(ob));
            sy += ENTY(G4_BENT(ob));
        }
        b = ob[7];
    }
    out[0] = G4_FIXDIV(sx, n << 16);     /* n 0: #DE in the original too */
    out[1] = G4_FIXDIV(sy, n << 16);
}

/* ------------------------------------------------------------------ 0x2500EC */
/* follow the path to the target (+0xA5); 1 = arrived */
uchar ailow_2500EC(uchar *br)
{
    short e;
    fix dist;
    short dxs, dys;
    uchar *P, done = 0, arrive;
    uchar *sq = SQ(br);

    if (br[0xD] == 3)
        return 0;
    G4_U32(br, 0x16) = 0x100;
    e = G4_BENT(br);
    dist = fix_fast_pyth_dist(G4_S32(br, 0xA5) - ENTX(e), G4_S32(br, 0xA9) - ENTY(e));
    dxs = (short)((G4_S32(br, 0xA5) - ENTX(e)) >> 16);
    dys = (short)((G4_S32(br, 0xA9) - ENTY(e)) >> 16);
    P = *(uchar **)(br + 0xAD);
    if (!(P && *P && G4_S16(P, 7) < 7)) {
        if (iabs32(dxs) + iabs32(dys) > 2 || br[0x116])
            goto follow;
    }
    arrive = 0;
    if (br[0xB3]) {
        Msg *m = MsgAlloc();
        if (m) {
            aiacc_25AC50(m, G4_U32(br, 1), G4_U32(sq, 3), br[3], 1);
            m->flag = 6;
            MsgSend(m);
            br[0xB3] = 0;
        }
    }
    if (br[0xD] == 1 && !br[0x9C]) {
        long sum = 0, n = 0, avg;
        uchar b;
        for (b = sq[0xF]; b; b = G4_BRAIN(b)[7]) {
            uchar *p = *(uchar **)(G4_BRAIN(b) + 0xAD);
            if (p && *p) {
                n++;
                sum += G4_S16(p, 7) * 12;
            }
        }
        avg = n > 0 ? sum / n : -1000;
        P = *(uchar **)(br + 0xAD);
        if (P && *P && G4_S16(P, 7) * 12 + 1 <= avg)
            G4_U32(br, 0x16) = 0x40;
    }
    if (dist < 0x18000)
        G4_U32(br, 0x16) = 0x80;
    if (dist < 0x8000)
        G4_U32(br, 0x16) = 0x40;
    if (br[0xD] == 2 && dist < 0x10000)
        arrive = 1;
    if (dist < 0x1000)
        arrive = 1;
    if (sq[0x157] == 2) {
        arrive = 0;
        if (G4_U32(br, 0x16) < 0x80)
            G4_U32(br, 0x16) = 0x80;
    }
    if (arrive) {
        *BRPATH(br) = 0;
        if (br[0xD] == 1) {
            fix c[2];
            ailow_24FF24(br, c);
            e = G4_BENT(br);
            note_heading(1, atan2_s(ENTX(e) - c[0], ENTY(e) - c[1]));
        }
        return 1;
    }
    P = *(uchar **)(br + 0xAD);
    if (P && G4_S16(P, 7) == -1)
        done = 1;
    else if (G4_U16(br, 0x9D) == G4_U16(br, 0xE) && G4_U16(br, 0x9F) == G4_U16(br, 0x10))
        done = 1;
follow:
    if (!br[0x116] && !done) {
        if (br[0xB1] && RndRange(game_rnd_text, 0, 0x7FFF) % 64 == 0)
            br[0xB1] = 0;
        P = *(uchar **)(br + 0xAD);
        if (P && !*P && !br[0xB1]) {
            uchar r = ailow_253580(br);
            if (path_25BF04(G4_U32(br, 0xE), G4_U32(br, 0x9D), br[0xB2], *(uchar **)(br + 0xAD), 0, r)) {
                G4_U32(*(uchar **)(br + 0xAD), 1) = G4_U32(br, 0x9D);
                **(uchar **)(br + 0xAD) = 1;
                br[0xB1] = 0;
                if (!(PCUR(*(uchar **)(br + 0xAD))[0xB] & 4))
                    ailow_24F704(br);
            } else {
                br[0xB1] = 1;
                **(uchar **)(br + 0xAD) = 0;
            }
        }
        if (br[0xB1])
            return 0;
        if (!(PCUR(BRPATH(br))[0xB] & 4))
            ailow_24F704(br);
        P = BRPATH(br);
        if (iabs32(G4_S16(br, 0xE) - (long)STEPX(PCUR(P))) <= 1) {
            P = BRPATH(br);
            if (iabs32(G4_S16(br, 0x10) - (long)STEPY(PCUR(P))) <= 1) {
                G4_S16(BRPATH(br), 7)--;
                P = BRPATH(br);
                if (G4_S16(P, 7) >= 0 && !(PCUR(P)[0xB] & 4))
                    ailow_24F704(br);
                if (G4_S16(BRPATH(br), 7) >= 0 && br[0xD] == 1 && br[0x116] == 0)
                    ailow_24F778(br);
            }
        }
    }
    if (br[0xD] == 1 || br[0xD] == 2)
        ailow_24EA7C(br, dxs, dys);
    return 0;
}

/* ------------------------------------------------------------------ 0x250668 */
uchar ailow_250668(uchar *p)
{
    G4_U32(p, 0x1B) = G4_U32(p, 0x23);
    return 1;
}

/* the sighting list of an entity (damage_27D158): +8 count, +0xC 6-byte records {id, b, ent} */
static uchar sees(uchar *list, short ent)
{
    uchar i = 0;
    short id;

    if (!list)
        return 0;
    id = G4_S16(G4_U32(list, 0xC), 0);
    while ((long)i < G4_S32(list, 8) && id) {
        long byte = id >> 3;
        if (!(ai_seen_bits[byte] & (1 << (id & 7)))) {
            uchar b = G4_ENT(id).f21;
            ai_seen_bits[byte] |= (uchar)(1 << (id & 7));
            if (b && ent == G4_S16(G4_BRAIN(b), 0xB))
                return 1;
        }
        i++;
        id = G4_S16(G4_U32(list, 0xC), i * 6);
    }
    return 0;
}

/* ------------------------------------------------------------------ 0x250674 */
/* can the brain or a squad mate see entity ent (else the line-of-sight test 2508D0) */
uchar ailow_250674(uchar *br, long ent_arg)
{
    short ent = (short)ent_arg;
    uchar b;

    if (!G4_ENT(ent).active)
        return 0;
    memset(ai_seen_bits, 0, 0x26);
    if (sees((uchar *)damage_27D158(G4_BENT(br)), ent))
        return 1;
    memset(ai_seen_bits, 0, 0x26);
    for (b = SQ(br)[0xF]; b; b = G4_BRAIN(b)[7]) {
        if (b == G4_U16(br, 3))
            continue;
        if (sees((uchar *)damage_27D158(G4_S16(G4_BRAIN(b), 0xB)), ent))
            return 1;
    }
    return ailow_2508D0(G4_BENT(br), ent) ? 1 : 0;
}

/* ------------------------------------------------------------------ 0x2508D0 */
/* line of sight from e1 to e2 within range (25 squares, 45 for installation type 0x45, any for
 * vehicle type 8) */
uchar ailow_2508D0(long e1_arg, long e2_arg)
{
    short e1 = (short)e1_arg, e2 = (short)e2_arg;
    fix p1[3], p2[3];
    long adx, ady;

    p1[0] = G4_ENT(e1).x; p1[1] = G4_ENT(e1).y; p1[2] = G4_ENT(e1).z;
    p2[0] = G4_ENT(e2).x; p2[1] = G4_ENT(e2).y; p2[2] = G4_ENT(e2).z;
    if (!e1 || !e2)
        return 0;
    adx = iabs32(p2[0] - p1[0]);
    ady = iabs32(p2[1] - p1[1]);
    if (ady + adx > 0x1F40000)
        return 0;
    if (!(G4_ENT(e1).cls == 2 && G4_ENT(e1).type == 8)) {
        fix lim = (G4_ENT(e1).cls == 4 && G4_ENT(e1).type == 0x45) ? 0x2D0000 : 0x190000;
        if (fix_fast_pyth_dist(adx, ady) > lim)
            return 0;
    }
    return aimain_24A3E0(e1, e2, p1[0], p1[1], p1[2], p2[0], p2[1], p2[2]);
}

/* ------------------------------------------------------------------ 0x250A1C */
/* pick the target (+0xC9 brain, +0xCA entity, +0xCB squad, +0xCC/+0xCE square) among the
 * enemies the brain sees: nearest first, then by class (soldier, vehicle / big installation,
 * aircraft, installation) */
uchar ailow_250A1C(uchar *br)
{
    uchar *sq = SQ(br), *list;
    uchar found = 0, i = 0;
    ushort bestd = 0xFFFF;
    short bestp = 5, p = 0, bestid = 0, id;

    memset(ai_seen_bits, 0, 0x26);
    list = (uchar *)damage_27D158(G4_BENT(br));
    if (list) {
        id = G4_S16(G4_U32(list, 0xC), 0);
        while ((long)i < G4_S32(list, 8) && id) {
            long byte = id >> 3;
            if (!(ai_seen_bits[byte] & (1 << (id & 7)))) {
                uchar b = G4_ENT(id).f21;
                ai_seen_bits[byte] |= (uchar)(1 << (id & 7));
                if (b && (G4_BRAIN(b)[0] || G4_BRAIN(b)[0xA]) && br[5] != G4_BRAIN(b)[5] &&
                    !ailow_24ADE0(G4_BRAIN(b)) && id != G4_S16(sq, 0xA9)) {
                    ushort d = G4_U16(G4_U32(list, 0xC), i * 6 + 2);
                    uchar t;
                    switch (G4_BRAIN(b)[0xD]) {
                    case 1: p = 1; break;
                    case 2: p = 2; break;
                    case 3:
                        t = G4_ENT(G4_S16(G4_BRAIN(b), 0xB)).type;
                        p = (t == 0x45 || t == 0x46 || t == 0x47) ? 2 : 4;
                        break;
                    case 4: p = 3; break;
                    }
                    if (d < bestd || p < bestp) {
                        bestp = p;
                        bestid = id;
                        bestd = d;
                        found = 1;
                    }
                }
            }
            i++;
            id = G4_S16(G4_U32(list, 0xC), i * 6);
        }
    }
    if (!found)
        return 0;
    if ((ushort)br[0xCA] != (ushort)bestid) {
        br[0xC9] = G4_ENT(bestid).f21;
        br[0xCA] = (uchar)bestid;
        br[0xCB] = G4_BRAIN(br[0xC9])[6];
    }
    G4_S16(br, 0xCC) = (short)(ENTX(br[0xCA]) >> 16);
    G4_S16(br, 0xCE) = (short)(ENTY(br[0xCA]) >> 16);
    return 1;
}

/* ------------------------------------------------------------------ 0x250CE0 */
/* distances of the nearest friend (or ignored enemy) to the brain (*near) and to the target
 * square (*tgt); 1000.0 when none */
void ailow_250CE0(uchar *br, fix *near, fix *tgt)
{
    ulong pt;
    uchar i;

    *near = 0x3E80000;
    *tgt = 0x3E80000;
    if (!br[0xC3])
        return;
    pt = br[0xC9] ? G4_U32(G4_BRAIN(br[0xC9]), 0xE) : G4_U32(br, 0xCC);
    for (i = 0; i < 0x3C; i++) {
        uchar *ob = G4_BRAIN(i);
        short oe, me;
        fix d;

        if (!ob[0] && !ob[0xA])
            continue;
        if (br[5] != ob[5] && !ailow_24ADE0(ob))
            continue;
        oe = G4_BENT(ob);
        d = fix_fast_pyth_dist(ENTX(oe) - G4_SQCENTRE(pt), ENTY(oe) - G4_SQCENTRE(pt >> 16));
        if (d < *tgt)
            *tgt = d;
        me = G4_BENT(br);
        d = fix_fast_pyth_dist(ENTX(oe) - ENTX(me), ENTY(oe) - ENTY(me));
        if (d < *near && G4_U16(br, 3) != i)
            *near = d;
    }
}

/* ------------------------------------------------------------------ 0x250E58 */
/* choose a weapon: flag 0 -> range of the current one (0 when it is not ready); flag 1 ->
 * select (record +cur) the last ready weapon that scores above 0 and suits the target, and
 * return its range */
short ailow_250E58(uchar *br, long flag)
{
    short slot = G4_ENT(G4_BENT(br)).slot;
    uchar *rec, *weap, *cur, *ready, *w;
    short n;
    long res = 0;
    fix d_near = 0, d_tgt = 0;
    uchar computed = 0, ok = 0;
    signed char i;

    switch (br[0xD]) {
    case 1:
        rec = (uchar *)&game_pba[slot];
        n = (signed char)rec[0x81];
        weap = rec + 0x45; cur = rec + 0xCE; ready = rec + 0x86;
        break;
    case 2:
        rec = (uchar *)&game_vehicles[slot];
        n = (signed char)rec[0x35];
        weap = rec + 0x17; cur = rec + 0x36; ready = rec + 0x37;
        break;
    case 3:
        rec = game_cls4[slot];
        n = (signed char)rec[0xD];
        weap = rec + 0x33; cur = rec + 0xE; ready = rec + 0x56;
        break;
    case 4:
        rec = game_cls3[slot];
        n = (signed char)rec[0x4A];
        weap = rec + 0x2A; cur = rec + 0x4B; ready = rec + 0x4C;
        break;
    default:
        return 0;
    }
    if (!(uchar)flag) {
        w = weap + (signed char)*cur * 15;
        if (weapon_ready(w, ready))
            return 0;
        return (short)ord_table[w[0]].range;
    }
    for (i = 0; i < n; i++) {
        OrdType *o;
        long v10, v16, score;

        w = weap + i * 15;
        if (w[0] == 8 || w[0] == 0x1C)
            continue;
        if (weapon_ready(w, ready))
            continue;
        o = &ord_table[w[0]];
        v10 = ((uchar *)o)[7] ? G4_U16(game_cls4_types[((uchar *)o)[7]], 0) : G4_U16(o, 5);
        v16 = ((uchar *)o)[0] ? 1000 / (long)G4_U16(o, 0x16) : G4_U16(o, 0x16);
        score = ai_3598DC * (long)o->range + ai_3598E0 * v10 + v16 * ai_3598E4;
        if (score <= 0)             /* the original compares with a best score it never updates */
            continue;
        if (w[0] == 7 && br[0xC3] && br[0xC9] && G4_BRAIN(br[0xC9])[0xD] == 3)
            ok = 0;                 /* overwritten below (dead in the original too) */
        if (w[0] == 8) {
            ok = 0;
        } else if (w[0] == 2 && SQ(br)[0xCB] != 7 && SQ(br)[0xCB] != 9) {
            ok = 0;
        } else if (w[0] == 9) {
            uchar k = G4_BRAIN(br[0xC9])[0xD];
            if (k == 4 || k == 3 || G4_U32(SQ(br), 0xEF) + 10000 < game_time)
                ok = 0;
            /* else: ok keeps its value (uninitialised stack byte in the original) */
        } else if (w[0] == 0xC) {
            if (G4_BRAIN(br[0xC9])[0xD] == 4) {
                ok = 0;
            } else {
                if (!computed) {
                    computed = 1;
                    ailow_250CE0(br, &d_near, &d_tgt);
                }
                ok = d_tgt >= 0x20000 && d_near >= 0x20000;
            }
        } else if (((uchar *)o)[4] == 1 && G4_BRAIN(br[0xC9])[0xD] == 4) {
            ok = 0;
        } else if (((uchar *)o)[4] == 1) {
            if (!computed) {
                computed = 1;
                ailow_250CE0(br, &d_near, &d_tgt);
            }
            ok = d_tgt > 0x60000 && d_near >= 0x10000;
        } else if (w[0] == 4 && br[5] == 2) {
            ok = 0;
        } else {
            ok = 1;
        }
        if (ok) {
            res = o->range;
            *cur = (uchar)i;
        }
    }
    return (short)res;
}
