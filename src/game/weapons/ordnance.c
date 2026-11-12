/* ordnance.c -- Terra Nova ordnance hits, the air strike and mission kill counters (module
 * M064; source name unknown).  Decompiled from __FF.EXE v1.09; names are the map's
 * placeholders (meaning below).
 *
 *   0x2908D0 ordnance_2908D0  class-5 objects of a few types: sound tick (ms)
 *   0x2909CC ordnance_2909CC  ordnance hit: impact sound, mark the ordnance for killing
 *   0x290ABC ordnance_290ABC  remove the trail objects of a class-5 object
 *   0x290B38 ordnance_290B38  ... unless its type keeps them
 *   0x290B70 ordnance_290B70  call the air strike: pick an entry point, spawn the aircraft
 *   0x291080 ordnance_291080  air strike status (0 none .. 5 active, -666 over)
 *   0x291110 ordnance_291110  set flag 0x35BF55
 *   0x29111C ordnance_29111C  distance player - strike aircraft
 *   0x29118C ordnance_29118C  distance player - strike target
 *   0x2911D0 ordnance_2911D0  distance strike target - strike aircraft
 *   0x291220 ordnance_291220  largest distance of a squad speaker to the strike target
 *   0x2912B8 ordnance_2912B8  smallest distance of a flagged PBA to the strike target
 *   0x291374 ordnance_291374  air strike update (abort, warnings)
 *   0x291450 ordnance_291450  mission 0x14 timed strike script
 *   0x29150C ordnance_29150C  end the air strike
 *   0x2915A8 ordnance_2915A8  strike request answer (map: not a separate entry; tail of 291660)
 *   0x291660 ordnance_291660  "request air strike" command
 *   0x291700 ordnance_291700  reset the kill counters and the tracked target
 *   0x291720 ordnance_291720  kills of an id (1..0x108: entity type, 0x100 + class)
 *   0x29173C ordnance_29173C  clear the kill counters
 *   0x291754 ordnance_291754  count a kill
 *   0x29177C ordnance_29177C  set a mission objective state (kind 1..3), HUD note on change
 *   0x29188C ordnance_29188C  objective started (state 1) if no start time yet
 *   0x2918C4 ordnance_2918C4  objective state 4
 *   0x2918D8 ordnance_2918D8  objective state 3
 *   0x2918EC ordnance_2918EC  objective state (3 for other kinds)
 *   0x291934 ordnance_291934  first entity with group id (f23) == id, cached
 *   0x2919B4 ordnance_2919B4  distance between two packed references (kind << 16 | id)
 *   0x291BC4 ordnance_291BC4  3D distance between two entities, integer units
 *   0x291C34 ordnance_291C34  distance of an entity outside a map region (circle or box)
 *   0x291D14 ordnance_291D14  reset the kill goals
 *   0x291D78 ordnance_291D78  add a kill goal (table B: "at least pct% killed")
 *   0x291E6C ordnance_291E6C  add a kill goal (table A: "at most pct% killed")
 *   0x291F60 ordnance_291F60  check goal table A of objective n
 *   0x292068 ordnance_292068  game time in seconds
 *   0x29207C ordnance_29207C  check goal table B of objective n (latches done = 2)
 * Data: 0x2916D0, 0x2916DC, 0x2916E8, 0x2916F4: four copies of {&0x35D0C0, &0x35D0C4,
 * &0x35D0C8} (the goal flags), copied to the stack by the goal functions (here: ordn_flags).
 *
 * Packed references (0x2919B4): high word 0 = the player, 1 = entity, 2 = group (entities with
 * f23 == id), 3 = map region id.
 * Quirks kept:
 *  - 2919B4 swaps its arguments when the second is a region: two regions loop forever;
 *    "best" is replaced when (d < best) == want_min, comparing the whole argument (only 0 / 1
 *    work); the group/group case compares groups by f23 against the second id;
 *  - 290B70 mode 2 checks the second candidate point's x against the bounds but tests the
 *    first point's y (ebp) instead of its own y's lower bound; the mode-1 table writes for
 *    rolls 1 and 2 are overwritten by the roll != 3 case;
 *  - 2912B8 keeps the last distance for entities it skips (inactive or not flagged);
 *  - 29177C with a kind outside 1..3 compares the caller's edi with the new state (here: no
 *    HUD call); 291F60 / 29207C with n outside 1..3 read a pointer from below the stack
 *    (here: 0);
 *  - the goal tables do not restart the slot scan for each objective (k carries over).
 */
#include "g2_c.h"
#include "g2_a.h"      /* grid (owner g2a) */
#include "g2_b.h"      /* game_cls5, game_cls4 (owner g2b) */
#include "g1_c.h"      /* game_rnd_text (owner g1c) */
#include "g3_a.h"      /* G3a callees (owner g3a) */
#include "g4_a.h"      /* AI callees (owners g4a/g4b/g4c1/g4c2; G4 merge) */
#include "g6_c.h"     /* added at the G5-G8 merge */
#include "g7_a.h"     /* added at the G5-G8 merge */
#include "g8_a.h"     /* added at the G5-G8 merge */
#include "g8_b.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
uchar ordn_counts[0x109];
OrdnGoal ordn_goal_a[3][6];
OrdnGoal ordn_goal_b[3][6];
long ordn_goal_c;
uchar ordn_goal_done[3];
long ordn_flags[3];
short strike_mode;
short strike_35BF3E;
long strike_35BF40;
uchar strike_warned, strike_active, strike_aborted, strike_called;
short strike_35BF4A, strike_35BF4C;
uchar strike_35BF4E[7];
uchar strike_35BF55, strike_35BF56, strike_35BF57;
ulong strike_35BF58;
long strike_35BF5C;
fix strike_player_x, strike_player_y;
fix strike_x, strike_y, strike_z;
short strike_entity;
short ordn_397C80;
#endif

#define PLAYER ((short)game_player_entity)

static int ordn_hit_type(uchar t)
{
    return t == 0xA || t == 0x1E || t == 0x21 || t == 0xD || t == 0xE || t == 0x20;
}

void ordnance_2908D0(ulong ms)
{
    fix t = (fix)(((long long)(long)ms << 16) / 1000);
    short i = G2C_IDX15(G2C_CLS5(0)->w0);
    ushort flag = (ushort)G2C_CLS5(i)->w0 >> 15;

    while (i) {
        if (!flag) {
            short e = G2C_IDX15(G2C_CLS5(i)->w0);
            if (G2C_ENT(e).active && ordn_hit_type(G2C_ENT(e).type)) {
                if (ms > 0x28 || (ulong)(RndRange((RndStream *)game_rnd_text, 0, 0x7FFF) & 0x3F) < ms)
                    sfx_28CB58(G2C_IDX15(G2C_CLS5(i)->w0), t);
            }
        }
        i = G2C_CLS5(i)->next;
        flag = (ushort)G2C_CLS5(i)->w0 >> 15;
    }
}

void ordnance_2909CC(void *ai)
{
    long ent = 0, target = 0, l8 = 0, l4 = 0, l10 = 0;

    aiacc_25AE98(ai, &ent, &target, &l8, &l4, &l10);
    if (target != 0xFFF) {
        short e = (short)ent;
        uchar ty = G2C_ENT(e).type;
        long h;
        if (target) {
            ulong i = target & 0xFFF;
            uchar cls, typ;
            if ((ulong)target >> 31) {
                cls = tile_tmpls[i].cls;
                typ = tile_tmpls[i].type;
            } else {
                cls = game_entities[i].cls;
                typ = game_entities[i].type;
            }
            h = sfx_28BAF8(ty, cls, typ);
        } else {
            h = sfx_28BA34(ty, e, G2C_ENT(e).x, G2C_ENT(e).y);
#ifndef TN_WATCOM_ABI
            port_water_hit(G2C_ENT(e).x, G2C_ENT(e).y, G2C_ENT(e).z, h ? ((SfxDesc *)h)->type : 0);
#endif
        }
        sfx_28CB24(h, e);
    }
    KillObjMark((short)ent, 1);
}

void ordnance_290ABC(long obj)
{
    ObjListRec *r = G2C_CLS5(G2C_ENT(obj).slot);
    short i = *(short *)&r->g[4];        /* +8 first trail record */

    while (i) {
        short next = *(short *)&game_cls6[i][0xA];
        grid_295730(G2C_IDX15(*(short *)&game_cls6[i][0]));
        i = next;
    }
    *(short *)&G2C_CLS5(G2C_ENT(obj).slot)->g[4] = 0;
}

void ordnance_290B38(long obj)
{
    if (ordn_objtype[G2C_ENT(obj).type][0x17] == 0)
        ordnance_290ABC((short)obj);
}

#define XMIN 0x280000
#define XMAX 0x1D80000

uchar ordnance_290B70(void)
{
    ObjFoot fp;
    long roll;
    short ai;
    uchar perc;
    fix px, py, tx, ty, z;

    if (strike_called)
        return 0;
    memset(&fp, 0, sizeof(fp));
    strike_player_x = G2C_ENT(PLAYER).x;
    strike_player_y = G2C_ENT(PLAYER).y;
    strike_mode = (short)RndRange(&game_rnd_35B704, 1, 2);
    roll = RndRange(&game_rnd_35B704, 1, 4);
    if (strike_mode == 2) {
        /* come in along the line target -> player */
        long dy, dx;
        px = G2C_ENT(PLAYER).x;
        py = G2C_ENT(PLAYER).y;
        tx = game_4414C8;
        ty = game_4414CC;
        dy = py - ty;
        if (dy < 0)
            dy = -dy;
        dx = px - tx;
        if (dx < 0)
            dx = -dx;
        if (dy >> 16 >= dx / 2)
            strike_mode = 1;
        else {
            fix slope = G2C_FIXDIV(py - ty, px - tx);
            fix ye = (ty > py) ? XMIN : XMAX, xe, xa, ya;
            long d = ye - ty, s = slope;
            if (d < 0)
                d = -d;
            if (s < 0)
                s = -s;
            if (d >> 16 >= s / 2)
                strike_mode = 1;
            else {
                xa = G2C_FIXDIV(ye - ty, slope) + tx;
                xe = (px < tx) ? XMIN : XMAX;
                ya = G2C_FIXMUL(slope, xe - tx) + ty;
                if (xa >= XMIN && ye >= XMIN && xa <= XMAX && ye <= XMAX) {
                    fp.x = xa;
                    fp.y = ye;
                } else if (xe >= XMIN && ye >= XMIN && xe <= XMAX && ya <= XMAX) {
                    fp.x = xe;
                    fp.y = ya;
                }
            }
        }
    }
    if (strike_mode == 1) {
        if ((short)roll == 1) {
            fp.x = XMIN;
            fp.y = XMIN;
        }
        if ((short)roll == 2) {
            fp.x = XMAX;
            fp.y = XMIN;
        }
        if ((short)roll == 3) {
            fp.x = XMAX;
            fp.y = XMAX;
        } else {
            fp.x = XMIN;
            fp.y = XMAX;
        }
    }
    if (game_359708 == 0x2F) {
        fp.x = 0x1900000;
        fp.y = 0x1000000;
    }
    if (fp.x < XMIN || fp.x > XMAX || fp.y < XMIN || fp.y > XMAX) {
        fp.x = XMIN;
        fp.y = XMIN;
    }
    if (game_359708 == 1) {
        fp.x = XMAX;
        fp.y = G2C_FIXDIV(XMAX - 0x1E0000, 0x20000);
    }
    {
        GameEntity *pe = &G2C_ENT(PLAYER);
        ulong xy = ((ulong)(ushort)(pe->y >> 16) << 16) | (ushort)(pe->x >> 16);
        perc = aiperc_253AE8(0, xy, (schar)((uchar *)&game_brains[pe->f21])[5]);
    }
    ai = 0;
    if (perc) {
        ai = ailow_24B924(7, perc, fp.x, fp.y, strike_35BF4E);
        if (ai)
            strike_entity = G2C_IDX15(*(short *)game_cls3[ai]);
    }
    if (!perc || !ai) {
        strike_entity = 0;
        return 0;
    }
    fp.obj = strike_entity;
    strike_warned = 0;
    strike_active = 0;
    strike_called = 1;
    z = aircraft_284A6C(fp.obj, fp.x, fp.y);
    fp.z = z + 0x50000;
    strike_x = fp.x;
    strike_y = fp.y;
    strike_z = fp.z;
    fp.f10 = 0;
    fp.f12 = 0;
    fp.heading = 0;
    interact_297400(&fp, G2C_ENT(fp.obj).cls, G2C_ENT(fp.obj).type);
    grid_29582C(&fp);
    physics_2D42E0(strike_entity);
    *(long *)&game_cls3[ai][0x13A] = 0;
    *(long *)&game_cls3[ai][0x13E] = 0;
    *(long *)&game_cls3[ai][0x142] = 0;
    G2C_ENT(strike_entity).f1d = 0;
    return 1;
}

long ordnance_291080(void)
{
    if (game_35BF48)
        return -666;
    if (strike_active)
        return 5;
    if (strike_called && game_35B6D4 && game_35B6E0)
        return 2;
    if (strike_called && game_35B6D4 && !game_35B6E0)
        return 3;
    if (strike_called && ordnance_2911D0() < 0x80000)
        return 4;
    if (strike_called)
        return 1;
    return 0;
}

void ordnance_291110(long v)
{
    strike_35BF55 = (v == 1);
}

fix ordnance_29111C(void)
{
    if (!strike_entity)
        return 0x270F0000;
    return fix_fast_pyth_dist(G2C_ENT(PLAYER).x - G2C_ENT(strike_entity).x,
                              G2C_ENT(PLAYER).y - G2C_ENT(strike_entity).y);
}

fix ordnance_29118C(void)
{
    return fix_fast_pyth_dist(G2C_ENT(PLAYER).x - game_4414C8, G2C_ENT(PLAYER).y - game_4414CC);
}

fix ordnance_2911D0(void)
{
    if (!strike_entity)
        return 0x7FFFFFFF;
    return fix_fast_pyth_dist(game_4414C8 - G2C_ENT(strike_entity).x,
                              game_4414CC - G2C_ENT(strike_entity).y);
}

fix ordnance_291220(void)
{
    fix best = 0, d;
    int i;

    if (!strike_entity || !strike_called || !game_35B6D4)
        return 0x7FFFFFFF;
    for (i = 0; i < 4; i++) {
        uchar *s = (uchar *)&game_391E50[i];
        if (*(short *)s == 1) {
            short e = *(short *)(s + 2);
            d = fix_fast_pyth_dist(G2C_ENT(e).x - game_4414C8, G2C_ENT(e).y - game_4414CC);
            if (d > best)
                best = d;
        }
    }
    if (game_35BF48)
        return 0x7FFFFFFF;
    return best;
}

fix ordnance_2912B8(void)
{
    fix best = 0x7FFFFFFF, d = 0x7FFFFFFF;
    short i;

    if (!strike_entity)
        return 0x270F0000;
    for (i = G2C_IDX15(game_pba[0].f000); i; i = game_pba[i].f002) {
        short e = G2C_IDX15(game_pba[i].f000);
        if (G2C_ENT(e).active && (schar)mfddata_2665E8(e) < 0) {
            e = G2C_IDX15(game_pba[i].f000);
            d = fix_fast_pyth_dist(G2C_ENT(e).x - game_4414C8, G2C_ENT(e).y - game_4414CC);
        }
        if (d < best)
            best = d;
    }
    return best;
}

void ordnance_291374(long arg)
{
    if (!strike_aborted && strike_35BF4A >= 5) {
        if (!strike_active)
            RadioMessage(0x3C);
        strike_active = 1;
        strike_aborted = 1;
        game_35BF48 = 1;
        game_35B6E0 = 0;
        aimain_24A184();
    }
    if (strike_35BF4C >= 5 && !strike_35BF56) {
        RadioMessage(0x3D);
        strike_35BF56 = 1;
        strike_active = 1;
        game_35B6E0 = 0;
    }
    if (strike_active) {
        if (!aircraft_284F2C(arg)) {
            if (!game_35BF48)
                strike_called = 0;
            strike_active = 0;
        }
    } else
        aircraft_285024(arg);
    if (!strike_warned && ordnance_2911D0() < 0x3C0000)
        RadioMessage(0x46);
    strike_warned = 1;
}

long ordnance_291450(void)
{
    uchar f = strike_35BF57;
    ulong now = game_time;

    if (now < 0x927C0 || game_359708 != 0x14) {
        strike_35BF57 = f;
        return 0;
    }
    if (game_35B6D4 && !(f & 4)) {
        f |= 4;
        strike_35BF58 = now + 0x1D4C0;
        strike_35BF57 = f;
        return 1;
    }
    if (game_time >= 0x927C0 && !strike_called && !(f & 1)) {
        f |= 1;
        strike_35BF57 = f;
        ordnance_290B70();
        f = strike_35BF57;
    }
    if (game_time >= strike_35BF58 && !(f & 2) && (f & 4)) {
        f |= 0xA;
        strike_35BF55 = 1;
        strike_active = 1;
        strike_35BF57 = f;
        return 2;
    }
    strike_35BF57 = f;
    return 0;
}

void ordnance_29150C(void)
{
    if (!strike_entity)
        return;
    game_35B6A8 = 0;
    strike_35BF3E = 0;
    strike_mode = 0;
    strike_active = 0;
    strike_warned = 0;
    strike_called = 0;
    game_35B6D8 = 0;
    strike_35BF4C = 0;
    strike_35BF4A = 0;
    strike_35BF40 = 0;
    game_35B6D4 = 0;
    strike_35BF56 = 0;
    KillObjMark(strike_entity, 2);
    mfd_26DF10();
    strike_entity = 0;
    snd_stop_entity(0);
}

void ordnance_2915A8(void)
{
    if (!strike_called) {
        RadioMessage(0x42);
        strike_35BF40 = 0;
        return;
    }
    if (!game_35B6D4) {
        RadioMessage(0x41);
        strike_35BF40 = 0;
        return;
    }
    if (ordnance_291220() > 0x100000 && ordnance_29118C() >= 0x70000 && game_35B6D4) {
        RadioMessage(0x43);
        strike_35BF40 = 0;
        return;
    }
    if (ordnance_291220() > 0x100000 && ordnance_29118C() < 0x70000 && game_35B6D4) {
        RadioMessage(0x40);
        strike_35BF40 = 0;
        return;
    }
    aircraft_28618C();
}

void ordnance_291660(void)
{
    switch (ordnance_291080()) {
    case 0:
        RadioMessage(0x49);
        ordnance_290B70();
        break;
    case 1:
        RadioMessage(0x4B);
        break;
    case 2:
        ordnance_2915A8();
        break;
    case 3:
        RadioMessage(0x55);
        break;
    case 4:
        RadioMessage(0x4C);
        break;
    case 5:
        break;
    case -666:
        RadioMessage(0x47);
        break;
    default:
        RadioMessage(0x4A);
        break;
    }
}

void ordnance_291700(void)
{
    memset(ordn_counts, 0, 0x109);
    ordnance_291934(0);
}

ulong ordnance_291720(long id)
{
    if (id <= 0 || (ulong)id >= 0x109)
        return 0;
    return ordn_counts[id];
}

void ordnance_29173C(void)
{
    memset(ordn_counts, 0, 0x109);
}

void ordnance_291754(ulong id)
{
    ushort i = (ushort)id;

    if (i != 0 && i < 0x109) {
        ordn_counts[i]++;
        if (i <= 0xFF)
            aiglue_253994(i);
    }
}

void ordnance_29177C(long kind, ulong t, long state)
{
    uchar *p = ordn_mission_state;
    ulong now = game_time / 1000;
    long old = state;

    if (kind >= 1 && kind <= 3) {
        ulong *start = (ulong *)(p + 0x808 + kind * 4);
        if (state == 1 && *start == 0)
            *start = t ? t : now;
        old = (schar)p[0x808 + kind];
        p[0x808 + kind] = (uchar)state;
    }
    ordn_mission_state = p;
    if (old != state && hud_26843C(game_359708, kind))
        hud_2683F8(1);
}

void ordnance_29188C(long kind)
{
    if (*(ulong *)(ordn_mission_state + 0x808 + kind * 4) == 0)
        ordnance_29177C(kind, game_time / 1000, 1);
}

void ordnance_2918C4(long kind)
{
    ordnance_29177C(kind, 0, 4);
}

void ordnance_2918D8(long kind)
{
    ordnance_29177C(kind, 0, 3);
}

long ordnance_2918EC(long kind)
{
    if ((ulong)kind - 1 < 3)
        return (schar)ordn_mission_state[0x808 + kind];
    return 3;
}

long ordnance_291934(long id)
{
    long cur = strike_35BF5C;
    short e;

    if (id == 0) {
        strike_35BF5C = 0;
        return 0;
    }
    if (id == cur) {
        if (G2C_ENT(ordn_397C80).active) {
            strike_35BF5C = cur;
            return ordn_397C80;
        }
        cur = 0;
    }
    for (e = (short)game_entities[0].f05; e; e = (short)G2C_ENT(e).f07)
        if (G2C_ENT(e).f23 == id) {
            strike_35BF5C = id;
            ordn_397C80 = e;
            return e;
        }
    strike_35BF5C = cur;
    return 0;
}

long ordnance_2919B4(ulong a, ulong b, long want_min)
{
    ulong ak, bk, t;
    long aid, bid, best, d;
    short w, e;

restart:
    bk = b & 0xFFFF0000;
    ak = a & 0xFFFF0000;
    aid = a & 0xFFFF;
    bid = b & 0xFFFF;
    if (bk == 0x30000) {
        t = a;
        a = b;
        b = t;
        goto restart;
    }
    if (ak == 0) {
        aid = PLAYER;
        ak = 0x10000;
    }
    if (bk == 0) {
        bk = 0x10000;
        bid = PLAYER;
    }
    if (ak == 0x30000) {
        if (bk == 0x10000)
            return ordnance_291C34(aid, (short)bid);
        best = want_min ? 0x7FFFFFFF : 0;
        for (e = (short)game_entities[0].f05; e; e = (short)G2C_ENT(e).f07)
            if (G2C_ENT(e).f23 == bid) {
                d = ordnance_291C34(aid, e);
                if ((ulong)want_min == (ulong)(d < best))
                    best = d;
            }
        return best;
    }
    if (ak == 0x20000) {
        best = want_min ? 0x7FFFFFFF : 0;
        for (w = (short)game_entities[0].f05; w; w = (short)G2C_ENT(w).f07) {
            if (G2C_ENT(w).f23 != aid)
                continue;
            if (bk == 0x20000) {
                for (e = (short)game_entities[0].f05; e; e = (short)G2C_ENT(e).f07)
                    if (G2C_ENT(e).f23 == bid) {
                        d = ordnance_291BC4(w, e);
                        if ((ulong)want_min == (ulong)(d < best))
                            best = d;
                    }
            } else {
                d = ordnance_291BC4(w, (short)bid);
                if ((ulong)want_min == (ulong)(d < best))
                    best = d;
            }
        }
        return best;
    }
    if (bk == 0x20000) {
        t = a;
        a = b;
        b = t;
        goto restart;
    }
    return ordnance_291BC4((short)aid, (short)bid);
}

long ordnance_291BC4(long e1, long e2)
{
    GameEntity *a = &G2C_ENT(e1), *b = &G2C_ENT(e2);
    fix d = fix_fast_pyth_dist(b->x - a->x, b->y - a->y);
    return fix_fast_pyth_dist(d, b->z - a->z) >> 16;
}

long ordnance_291C34(long region, long e)
{
    GameEntity *p = &G2C_ENT(e);
    fix dx = p->x - *(fix *)&game_4413D0[region * 8];
    fix dy = p->y - *(fix *)&game_4413D0[region * 8 + 4];
    uchar *r = &game_4417D0[region * 10];
    ushort kind = *(ushort *)r;
    fix d;

    if (kind == 0)
        d = fix_fast_pyth_dist(dx, dy) - *(fix *)(r + 2);
    else if (kind == 1) {
        fix ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        fix hw = *(fix *)(r + 2) / 2, hh = *(fix *)(r + 6) / 2;
        if (ax < hw)
            d = ay - hh;
        else if (ay < hh)
            d = ax - hw;
        else
            d = fix_fast_pyth_dist(ax - hw, ay - hh);
    } else
        d = 0;
    if (d < 0)
        d = 0;
    return (long)((ulong)d + 0xFFFF) >> 16;
}

void ordnance_291D14(long a, long b, long c)
{
    ordn_flags[0] = a;
    ordn_flags[1] = b;
    ordn_flags[2] = c;
    ordn_goal_b[0][0].id = -1;
    ordn_goal_a[0][0].id = -1;
    ordn_goal_a[1][0].id = -1;
    ordn_goal_b[2][0].id = -1;
    ordn_goal_a[2][0].id = -1;
    ordn_goal_done[0] = 1;
    ordn_goal_b[1][0].id = -1;
    ordn_goal_done[1] = 1;
    ordn_goal_done[2] = 1;
    ordn_goal_c = 0;
}

static void ordn_goal_add(OrdnGoal (*tab)[6], long id, long p1, long p2, long p3)
{
    long p[3];
    long k = 0;
    int g;

    p[0] = p1;
    p[1] = p2;
    p[2] = p3;
    for (g = 0; g < 3; g++) {
        uchar cnt = 0;
        short e;
        if (p[g] == 0)
            continue;
        while (tab[g][k].id != -1 && k < 5)
            k++;
        if (k == 5)
            return;
        tab[g][k].id = id;
        tab[g][k].pct = (uchar)p[g];
        for (e = (short)game_entities[0].f05; e; e = (short)G2C_ENT(e).f07)
            if (G2C_ENT(e).f23 == id || G2C_ENT(e).cls + 0x100 == id)
                cnt++;
        tab[g][k].cnt = cnt;
        tab[g][k + 1].id = -1;
        ordn_flags[g] = 1;
    }
}

void ordnance_291D78(long id, long p1, long p2, long p3)
{
    ordn_goal_add(ordn_goal_b, id, p1, p2, p3);
}

void ordnance_291E6C(long id, long p1, long p2, long p3)
{
    ordn_goal_add(ordn_goal_a, id, p1, p2, p3);
}

static long ordn_kills(long id)
{
    return (id > 0 && (ulong)id < 0x109) ? ordn_counts[id] : 0;
}

long ordnance_291F60(long n)
{
    long idx = (n > 0 && n < 4) ? n - 1 : -1;
    long k;

    if (idx == -1)
        return 0;                       /* original: reads a pointer below the stack */
    if (ordn_flags[idx] && ordn_goal_a[idx][0].id == -1)
        return 1;
    if (!ordn_goal_c || !ordn_flags[idx])
        return 0;
    for (k = 0; ordn_goal_a[idx][k].id != -1; ) {
        OrdnGoal *g = &ordn_goal_a[idx][k];
        long q;
        if (g->cnt == 0)
            return 0;
        q = ordn_kills(g->id) * 100 / g->cnt;
        if (q > 100 - g->pct)
            return 0;
        if (++k >= 5)
            break;
    }
    return 1;
}

ulong ordnance_292068(void)
{
    return game_time / 1000;
}

long ordnance_29207C(long n)
{
    long idx = (n > 0 && n < 4) ? n - 1 : -1;
    long k;

    if (idx == -1 || !ordn_flags[idx])
        return 0;
    if (ordn_goal_done[idx] == 2)
        return 1;
    for (k = 0; ordn_goal_b[idx][k].id != -1; ) {
        OrdnGoal *g = &ordn_goal_b[idx][k];
        long q;
        if (g->cnt == 0)
            return 0;
        q = ordn_kills(g->id) * 100 / g->cnt;
        if (q < g->pct)
            return 0;
        if (++k >= 5)
            break;
    }
    ordn_goal_done[idx] = 2;
    return 1;
}
