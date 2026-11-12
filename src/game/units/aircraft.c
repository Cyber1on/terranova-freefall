/* Terra Nova: aircraft simulator (module M052; the original file name is not in the binary).
 * Class 3 objects: the player's support aircraft (side 0 of AIR_ENT), the air strike plane
 * (strike_entity, driven by ordnance.c) and the scripted transports of some missions.
 *
 *   0x2836E0 aircraft_2836E0  reset the aircraft state
 *   0x28375C aircraft_28375C  the player has an aircraft
 *   0x283770 aircraft_283770  mission object test at the aircraft's position
 *   0x2837B4 aircraft_2837B4  call an aircraft for a unit's side (1 = created)
 *   0x283A64 aircraft_283A64  call an aircraft against a target
 *   0x283A94 aircraft_283A94  the player calls the aircraft (uses one call)
 *   0x283AFC aircraft_283AFC  send the player's aircraft home
 *   0x283B88 aircraft_283B88  per-frame update: mission checks and every aircraft
 *   0x283F00 aircraft_283F00  delete an aircraft when it has a target (unused)
 *   0x283F2C aircraft_283F2C  per-frame update of one aircraft
 *   0x284444 aircraft_284444  AI hit callback: apply damage to an aircraft
 *   0x2844F4 aircraft_2844F4  delete an aircraft
 *   0x284670 aircraft_284670  move the player's aircraft (remote control)
 *   0x284888 aircraft_284888  climb rate that keeps the flying height
 *   0x284A6C aircraft_284A6C  ground height under a point
 *   0x284AB0 aircraft_284AB0  steer towards an entity (1 = landed by it)
 *   0x284CEC aircraft_284CEC  steer towards the player (unused)
 *   0x284D00 aircraft_284D00  climb rate of the strike plane
 *   0x284E78 aircraft_284E78  fire the current weapon (0 fired, 6 not)
 *   0x284F2C aircraft_284F2C  strike plane: steer to the target (0 = arrived)
 *   0x285024 aircraft_285024  strike plane: approach and attack run
 *   0x285348 aircraft_285348  follow the player
 *   0x285460 aircraft_285460  killobj callback: an aircraft is gone
 *   0x28559C aircraft_28559C  stop the aircraft of a unit's side
 *   0x285608 aircraft_285608  arm the scripted transport of the mission
 *   0x28565C aircraft_28565C  scripted transport: fly to the landing point, then leave
 *   0x2857E8 aircraft_2857E8  scripted transport: steering (1 = arrived)
 *   0x2859DC aircraft_2859DC  mission 0xE: create the transport squad
 *   0x285AFC aircraft_285AFC  mission 0xE transport (0 = done)
 *   0x285DC4 aircraft_285DC4  mission 0xE: drop the next squad
 *   0x286010 aircraft_286010  the +0x14B words of an aircraft (static result)
 *   0x28605C aircraft_28605C  strike plane velocity and approach angle
 *   0x28618C aircraft_28618C  strike plane approach sound and angle
 *   0x2862B0 aircraft_2862B0  state of the mission's scripted transport (0, 1 armed, 2 done)
 *   0x286324 aircraft_286324  squad 14 message
 *   0x28635C aircraft_28635C  near the map edge
 *
 * Quirks kept:
 *  - aircraft_283F2C's terrain loop compares the entity's slot with the strike plane's slot,
 *    not the entities;
 *  - aircraft_28605C / aircraft_28618C test the address of ordnance_291080 (always nonzero)
 *    instead of calling it;
 *  - aircraft_285348 halves the turn with an unsigned shift (-0x800 becomes 0x7C00);
 *  - aircraft_2859DC passes the integer parts of the player's position to aircraft_284A6C and
 *    uses an uninitialised slot (ecx, taken as a fourth argument) when no squad was found;
 *  - aircraft_284E78 returns 6 with the weapon offset in the upper bits, or the weapon_ready
 *    result, as the original leaves them in eax;
 *  - the terrain_ff blocks passed to FF_terrain have only the caller field set.
 */
#include "g3_c.h"
#include "g2_a.h"      /* grid_29582C (owner g2a) */
#include "edms.h"
#include "3d_a.h"
#include "g3_a.h"      /* G3a callees (owner g3a) */
#include "g4_a.h"      /* AI callees (owners g4a/g4b/g4c1/g4c2; G4 merge) */
#include "g6_c.h"     /* added at the G5-G8 merge */

#ifndef TN_WATCOM_ABI
extern int port_smooth_fps;             /* port/ui/options.c: 60 frames a second */
#endif
#include "g7_a.h"     /* added at the G5-G8 merge */
#include "g7_c.h"     /* added at the G5-G8 merge */
#include "g8_b.h"     /* added at the G5-G8 merge */
#include "g9_c.h"     /* added at the G9/G10 merge (owner header of G9/G10 callees) */

#ifndef TN_LINK_ORIGINAL_DATA
uchar air_dead[4];
short air_target[4];
uchar air_3968B2, air_3968B3, air_3968B4;
fix air_alt;
ulong air_ret[2];
fix air_drop_pos[3];
short air_drop_ang[3];
fix air_strike_vel[3];
fix air_strike_prev[3];
uchar air_35B6C2;
uchar air_parm_35B6C3[0x23];
uchar air_parm_35B6E6[7];
short air_35B6AA;
fix air_35B6B0[3];
ulong air_35B6CC;
uchar air_35B6D0;
ulong air_35B6DC;
uchar air_mission_flags[5];
uchar air_35B6ED;
ulong air_35B6F0, air_35B6F4;
long air_35B6F8;
uchar air_35B6FC;
ulong air_35B700;
uchar game_38FC12[4][0x20];
#endif

#define IDX15(w) ((short)((short)((ushort)(w) << 1) >> 1))
#define W(p, o) (*(short *)((uchar *)(p) + (o)))
#define UW(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define L(p, o) (*(long *)((uchar *)(p) + (o)))
#define REC_ENT(r) IDX15(UW(r, 0))
#define CLS3(s) (game_cls3[(short)(s)])
#define FIXDIV(a, b) ((fix)((((long long)(long)(a)) << 16) / (long)(b)))
#define CAM(o) ((uchar *)game_camera + (o))
#define REGION(o) (*(fix *)&game_4413D0[(o) - 0x4413D0])

static long abs32(long v) { return v < 0 ? -v : v; }

/* ground height under (x, y) as FF_terrain reports it for `caller` */
static fix ground(long caller, fix x, fix y)
{
    terrain_ff t;
    t.caller = caller;
    FF_terrain(x * 6, y * 6, 0, 0, &t);
    return t.g_height / 6;
}

/* re-insert an aircraft into the object grid at its position and stop its physics control */
static void place(short ent)
{
    ObjFoot foot;
    GameEntity *e = &G3C_ENT(ent);

    foot.obj = ent;
    memcpy(&foot.x, &e->x, 18);
    interact_297400(&foot, e->cls, e->type);
    grid_29582C(&foot);
    physics_2D47D8(ent, foot.x, foot.y);
    EDMS_control_marble(G3C_ENT(foot.obj).body, 0, 0, 0);
}

void aircraft_2836E0(void)
{
    long i;
    for (i = 0; i < 4; i++) {
        AIR_ENT(i) = 0;
        air_dead[i] = 0;
        air_target[i] = 0;
    }
    game_3968B5 = 0;
    air_3968B4 = 0;
    air_alt = 0x18000;
}

long aircraft_28375C(void)
{
    return AIR_ENT(0) != 0;
}

long aircraft_283770(long a)
{
    short e = AIR_ENT(0);
    if (e == 0)
        return 0;
    return misobj_2C9D50(a, G3C_ENT(e).x, G3C_ENT(e).y);
}

uchar aircraft_2837B4(long a, long obj)
{
    short ent = (short)obj;
    GameEntity *e = &G3C_ENT(ent);
    schar side = (schar)mfddata_2665E8(ent);
    short acft = 0, slot = 0;
    GameEntity *f;
    uchar *r;

    if (side == -1)
        return 0;
    if (AIR_ENT(side) != 0)
        return 0;
    if (e->f21 != 0) {
        ulong xy = (ushort)(e->x >> 16) | ((ulong)(ushort)(e->y >> 16) << 16);
        uchar sq = aiperc_253AE8(0, xy, (schar)game_brains[e->f21].f005);
        if (sq) {
            slot = ailow_24B924(side, sq, e->x, e->y, air_parm_35B6C3);
            if (slot != 0)
                acft = IDX15(UW(CLS3(slot), 0));
        }
    }
    if (acft == 0) {
        AIR_ENT(side) = 0;
        return 0;
    }
    AIR_ENT(side) = acft;
    if ((uchar)a == 1)
        air_target[side] = 0;
    {
        ObjFoot foot;
        foot.obj = acft;
        memcpy(&foot.x, &e->x, 18);
        foot.z += 0x8000;
        f = &G3C_ENT(acft);
        interact_297400(&foot, f->cls, f->type);
        grid_29582C(&foot);
    }
    physics_2D42E0(acft);
    EDMS_ignore_collisions(f->body, G3C_ENT(game_player_entity).body);
    EDMS_control_marble(f->body, 0, 0, 0);
    r = CLS3(slot);
    L(r, 0x13A) = 0;
    L(r, 0x13E) = 0;
    L(r, 0x142) = 0x8000;
    f->f1d = 0x800;
    killobj_289554(acft, WATCOM_FN(aircraft_285460), -1);
    damage_27C840(&cursctrl_aircraft, 100, 100);
    SndPlay(0x7E6, 0, 0x28, 1, 0x40, 0x40, 0);
    air_3968B4 = 0;
    air_3968B3 = 0;
    air_35B6C2 = 1;
    return 1;
}

uchar aircraft_283A64(long ent, long target)
{
    uchar r = aircraft_2837B4(1, (short)ent);
    if (r) {
        air_target[(schar)mfddata_2665E8((short)ent)] = (short)target;
        return 1;
    }
    return r;
}

uchar aircraft_283A94(void)
{
    uchar r;

    air_alt = 0x18000;
    if (((uchar *)&game_pba[game_player_slot])[0xDC] == 0 || AIR_ENT(0) != 0)
        return 0;
    r = aircraft_2837B4(0, game_player_entity);
    if (r) {
        ((uchar *)&game_pba[game_player_slot])[0xDC]--;
        RadioAck(0x4B);
    }
    return r;
}

void aircraft_283AFC(void)
{
    short e = AIR_ENT(0);
    if (e == 0 || G3C_ENT(e).type != 0)
        return;
    air_3968B2 = 0;
    air_3968B4 = 1;
    SndPlay(0xAE5, 4, 0xFF, 1, 0x4C, 0x40, 0);
    RadioAck(0x4C);
    ailow_24BB1C(G3C_ENT(AIR_ENT(0)).f21, 0);
}

/* hud_26843C(mission, n) && state == v, else the fallback (a second hud_26843C call) */
static uchar goal_state(long n, uchar v, long alt)
{
    if (hud_26843C(game_359708, n) && ordn_mission_state[0x808 + n] == v)
        return 1;
    if (alt < 0)
        return !hud_26843C(game_359708, n);
    return ordn_flags[n - 1] == 0;
}

void aircraft_283B88(ulong dt)
{
    uchar a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0, i = 0;
    short s;

    if (game_35B6A8)
        aircraft_28618C();
    if (game_time > 5000) {
        if (!ordn_flags[0] || ordn_mission_state[0x809] == 1)
            a = 1;
        if (!ordn_flags[1] || ordn_mission_state[0x80A] == 1)
            b = 1;
        if (!ordn_flags[2] || ordn_mission_state[0x80B] == 1)
            c = 1;
        d = goal_state(1, 3, -1);
        e = goal_state(2, 3, -1);
        f = goal_state(3, 3, -1);
        g = goal_state(1, 4, 0);
        h = goal_state(2, 4, 0);
        i = goal_state(3, 4, 0);
    }
    if (g && h && i && air_35B6CC < game_time && air_35B6D0 == 0) {
        void *res = RefGet(0x16F00B9);
        if (air_35B6CC == 0)
            air_35B6CC = game_time + 5000;
        else {
            air_35B6D0 = 1;
            if (main_246094(res)) {
                game_35A95C = 1;
                game_35B77C = 1;
                ordn_goal_c = 1;
            }
        }
    }
    if (!strike_called && !game_35BF48) {
        if (((a && b && c) || (d && e && f) || (game_359708 == 1 && a)) && game_35CD9C == 0)
            ordnance_290B70();
    }
    s = IDX15(UW(CLS3(0), 0));
    while (s != 0) {
        uchar *r = CLS3(s);
        if (!(UW(r, 0) >> 15) && G3C_ENT(REC_ENT(r)).active) {
            aircraft_283F2C(r, dt);
            damage_27D1E4(REC_ENT(r), (NearList *)(r + 0x62));
            if (game_35CD9C)
                objacc_28B88C((ObjMeter *)(r + 0x4C), dt);
        }
        s = W(r, 2);
    }
    aircraft_28605C(dt);
}

void aircraft_283F00(long ent)
{
    if (weapon_find_target((short)ent, 0x10000, 1, 1))
        aircraft_2844F4((short)ent);
}

void aircraft_283F2C(uchar *rec, long dt)
{
    short me = REC_ENT(rec);
    GameEntity *e;
    uchar t, flag = 0;
    fix s, c, dx = 0, dy = 0;
    long i;

    if (G3C_ENT(me).f21 && ailow_24BB30(G3C_ENT(me).f21))
        return;
    t = G3C_ENT(me).type;
    if ((t == 0 || t == 1) && aircraft_28635C(rec))
        aircraft_2844F4(REC_ENT(rec));
    me = REC_ENT(rec);
    t = G3C_ENT(me).type;
    if (t == 0) {
        if (air_3968B4 && aircraft_284AB0(rec, game_player_entity)) {
            KillObjMark(AIR_ENT(0), 1);
            ((uchar *)&game_pba[game_player_slot])[0xDC]++;
            return;
        }
    } else if (t == 1) {
        for (i = 0; i < 4; i++) {
            if (REC_ENT(rec) == AIR_ENT(i)) {
                if (air_target[i] != 0) {
                    short m;
                    aircraft_284AB0(rec, air_target[i]);
                    m = REC_ENT(rec);
                    if (weapon_find_target(m, 0x10000, 1, 1))
                        aircraft_2844F4(m);
                    flag = 1;
                }
                break;
            }
        }
    } else if (me == air_35B6AA) {
        if (!aircraft_285AFC(rec)) {
            aircraft_2844F4(REC_ENT(rec));
            air_35B6AA = 0;
        }
    } else if (t >= 8 && t <= 0xD)
        aircraft_28565C(rec);
    else if (me == strike_entity)
        ordnance_291374((long)rec);
    else
        aircraft_285348(rec);

    me = REC_ENT(rec);
    e = &G3C_ENT(me);
#ifndef TN_WATCOM_ABI
    /* G47: the game turns an aircraft by its turn rate once a frame, whatever the frame's length,
     * so at 60 frames a second the drone (and every aircraft) turned 2.4 times as fast as at the
     * game's 25, while it flew at the right speed.  At 60 frames a second: the turn for the
     * frame's time (40 ms = the game's frame), the rest kept for the next frame */
    if (port_smooth_fps && dt > 0 && dt < 40) {
        static short rest[64];
        long a = (short)UW(rec, 0x136) * 2 * dt + rest[e->slot & 63], q = a / 40;
        rest[e->slot & 63] = (short)(a - q * 40);
        e->heading = (ushort)(e->heading + q);
    } else
#endif
    e->heading = (ushort)(e->heading + UW(rec, 0x136) * 2);
    fix_sincos((ushort)(e->heading + UW(rec, 0x138)), &s, &c);
    c = G3C_FIXMUL(c, L(rec, 0x132) << 8);
    s = G3C_FIXMUL(s, L(rec, 0x132) << 8);
    L(rec, 0x13A) = (L(rec, 0x13A) + c) / 2;
    L(rec, 0x13E) = (L(rec, 0x13E) + s) / 2;
    dx = (L(rec, 0x13A) * dt) >> 10;
    dy = (L(rec, 0x13E) * dt) >> 10;
    e->x += dx;
    e->y += dy;
    if (e->x < 0x80000 || e->x > 0x1F80000 || e->y < 0x80000 || e->y > 0x1F80000) {
        KillObjMark(REC_ENT(rec), 0);
        return;
    }
    me = REC_ENT(rec);
    if (!(me == AIR_ENT(0) && (air_3968B3 || flag))) {
        uchar t2 = G3C_ENT(me).type;
        if (t2 == 9 || t2 == 10)
            ;
        else if (me == strike_entity && game_35B6D4 == 0)
            aircraft_284D00(rec, dx, dy);
        else if (!(me == AIR_ENT(0) && air_3968B4 && air_3968B2))
            aircraft_284888(rec, dx, dy, dt);
    }
    me = REC_ENT(rec);
    if ((me == strike_entity && game_35B6D4 == 0) || me == AIR_ENT(0))
        e->z += (L(rec, 0x142) * dt) >> 10;
    {
        fix h = ground(REC_ENT(rec), e->x, e->y);
        while (e->z - h < 0x20 && e->slot != G3C_ENT(strike_entity).slot)
            e->z += 0x10;
    }
    place(REC_ENT(rec));
}

void aircraft_284444(void *ai)
{
    long ent, target, c, d, e;
    short me;
    uchar *r;

    aiacc_25AE98(ai, &ent, &target, &c, &d, &e);
    me = (short)ent;
    if (G3C_ENT(me).type == 1) {
        aircraft_2844F4(me);
        return;
    }
    if (target == 0xFFF)
        return;
    r = CLS3(G3C_ENT(me).slot);
    obj_damage(me, (short)(target & 0x0FFF), (uchar)c, r + 6, 3, r + 0x1B, 3, r + 0x4C, r + 0x2A, 2,
               (ushort)d, (ushort)e);
}

void aircraft_2844F4(long ent)
{
    short me = (short)ent;
    GameEntity *e = &G3C_ENT(me);
    schar k = -1;
    long i;

    if (e->type == 1 || e->type == 0) {
        for (i = 0; i < 4; i++)
            if (me == AIR_ENT(i)) {
                k = (schar)i;
                break;
            }
        if (k != -1) {
            if (air_dead[k])
                return;
            air_dead[k] = 1;
        }
    }
    if (e->type == 1) {
        short n, s8;
        if (timers_279E80(&n, &s8, 0xC)) {
            GameEntity *f = &G3C_ENT(n);
            f->x = e->x;
            f->y = e->y;
            f->z = e->z;
            f->type = 0xC;
            W(game_cls8[s8], 8) = me;
            W(game_cls8[s8], 6) = W(CLS3(e->slot), 0x48);
            game_cls8[s8][0xB] = 1;
            game_cls8[s8][0xA] = 1;
            sfx_28DDD8(n, 2);
            if (me == AIR_ENT(0))
                RadioAck(0x68);
        }
    } else if (e->type == 0)
        RadioAck(0x4E);
    KillObjMark(me, 1);
    sfx_28CB24(sfx_28C300(me), me);
}

void aircraft_284670(long a, long b, long c, long d)
{
    GameEntity *e;
    fix lim, step, h;

    if (AIR_ENT(0) == 0)
        return;
    if (((ushort)a | b | c | (ushort)d) == 0) {
        air_3968B3 = 0;
        return;
    }
    e = &G3C_ENT(AIR_ENT(0));
    ailow_24BB1C(e->f21, 0);
    e->heading = (ushort)(e->heading + a);
    if ((ushort)d == 0x8000)
        e->f1d = 0x800;
    else {
        ushort v = (ushort)(e->f1d + d);
        e->f1d = v;
        if (v > 0x2000 && v < 0xE000)
            e->f1d = v < 0x8000 ? 0x2000 : 0xE000;
    }
    if (b == 0 && c == 0)
        return;
    if (b != 0) {
        fix s, co;
        fix_sincos(e->heading, &s, &co);
        e->x += G3C_FIXMUL(co, b);
        e->y += G3C_FIXMUL(s, b);
    }
    e->z += c;
    if (e->type == 1) {
        lim = 0x400;
        step = 0x100;
    } else {
        lim = 0x8000;
        step = 0x2000;
    }
    h = ground(AIR_ENT(0), e->x, e->y);
    while (e->z - h < lim)
        e->z += step;
    place(AIR_ENT(0));
}

/* doubles a short step until it reaches one unit (at most 9 times) */
static void stretch(fix *dx, fix *dy)
{
    long n = 10;
    while (--n != 0 && abs32(*dx) < 0x10000 && abs32(*dy) < 0x10000) {
        *dx *= 2;
        *dy *= 2;
    }
}

/* the higher of the ground under the aircraft and under the point (dx, dy) ahead */
static fix ground_ahead(uchar *rec, fix dx, fix dy)
{
    short me = REC_ENT(rec);
    GameEntity *e = &G3C_ENT(me);
    fix h = ground(me, e->x, e->y), h2;

    stretch(&dx, &dy);
    h2 = ground(REC_ENT(rec), e->x + dx, e->y + dy);
    return h2 > h ? h2 : h;
}

void aircraft_284888(uchar *rec, fix dx, fix dy, long dt)
{
    GameEntity *e = &G3C_ENT(REC_ENT(rec));
    fix h = ground_ahead(rec, dx, dy);
    long d = e->z - (h + air_alt);

    if (d < -0x8000) {
        if (((long)((ulong)dt << 18) >> 10) + d > 0)
            L(rec, 0x142) = (-d / dt) << 10;
        else
            L(rec, 0x142) = 0x40000;
    } else {
        long t = (long)((ulong)dt << 16);
        if (d < 0) {
            if ((t >> 10) + d > 0)
                L(rec, 0x142) = (-d / dt) << 10;
            else
                L(rec, 0x142) = 0x10000;
        } else if (d > 0) {
            if ((-t >> 10) + d < 0)
                L(rec, 0x142) = (-d / dt) << 10;
            else
                L(rec, 0x142) = -0x10000;
        } else
            L(rec, 0x142) = 0;
    }
}

fix aircraft_284A6C(long obj, fix x, fix y)
{
    return ground((short)obj, x, y);
}

uchar aircraft_284AB0(uchar *rec, long target)
{
    short me = REC_ENT(rec);
    GameEntity *e = &G3C_ENT(me), *t = &G3C_ENT((short)target);
    fix dx = t->x - e->x, dy = t->y - e->y;
    fix d = fix_fast_pyth_dist(dx, dy);
    long dz;
    short diff;
    fix lim, spd;

    W(rec, 0x138) = 0;
    if (e->type != 1) {
        if (d < 0x10000) {
            dz = e->z - t->z;
            W(rec, 0x136) = 0;
            air_3968B2 = 1;
            L(rec, 0x132) = 0;
            if (abs32(dz) > 0x2000) {
                L(rec, 0x142) = dz > 0 ? -0x10000 : 0x10000;
                return 0;
            }
            return 1;
        }
    } else {
        dz = e->z - t->z;
        if (abs32(dz) > 0x4000)
            L(rec, 0x142) = dz > 0 ? -0x10000 : 0x10000;
        else if (dz == 0)
            L(rec, 0x142) = 0;
        else
            L(rec, 0x142) = dz > 0 ? -0x800 : 0x800;
    }
    diff = (short)(fix_atan2(dy >> 2, dx >> 2) - e->heading);
    if (abs32(diff) >= 0x1000) {
        L(rec, 0x132) = 0;
        W(rec, 0x136) = diff < 0 ? -0x800 : 0x800;
        return 0;
    }
    W(rec, 0x136) = diff / 2;
    if (e->type == 1) {
        lim = 0x50000;
        spd = 0x9C4;
    } else {
        lim = 0xA0000;
        spd = 0x960;
    }
    if (lim > d) {
        long q = spd / 4;
        if (q > L(rec, 0x132))
            L(rec, 0x132) = q;
        else
            L(rec, 0x132) = L(rec, 0x132) >> 1;
    } else
        L(rec, 0x132) = spd;
    return 0;
}

uchar aircraft_284CEC(uchar *rec)
{
    return aircraft_284AB0(rec, game_player_entity);
}

void aircraft_284D00(uchar *rec, fix dx, fix dy)
{
    GameEntity *e = &G3C_ENT(REC_ENT(rec));
    fix h = ground_ahead(rec, dx, dy);
    long d = e->z - h;

    if (d < 0x1E000)
        L(rec, 0x142) = 0x10000;
    else if (d > 0x26000)
        L(rec, 0x142) = -0x10000;
    else if (d < 0x20000)
        L(rec, 0x142) = 0x8000;
    else if (d > 0x28000)
        L(rec, 0x142) = -0x8000;
    else
        L(rec, 0x142) = 0;
}

long aircraft_284E78(long slot, fix x, fix y, fix z, long target, long real)
{
    uchar *r = CLS3(slot);
    long wo = (schar)r[0x4B] * 15;
    uchar *w = r + 0x2A + wo;
    uchar ok;

    if (*w == 0)
        return (wo & ~0xFF) | 6;
    if ((uchar)real) {
        ok = weapon_ready(w, r + 0x4C);
        if (ok)
            return ok;
    }
    if (weapon_fire(w, REC_ENT(r), 0, 0, -0x2000, x, y, z, (short)target, r + 0x4C, (uchar)real))
        return 0;
    return 6;
}

/* turn towards a heading difference: half of it below 0x1000, else a full turn and stop */
static uchar steer(uchar *rec, short diff)
{
    if (abs32(diff) >= 0x1000) {
        L(rec, 0x132) = 0;
        W(rec, 0x136) = diff < 0 ? -0x800 : 0x800;
        return 0;
    }
    W(rec, 0x136) = diff / 2;
    return 1;
}

/* approach speed by distance (0x285024, 0x285AFC) */
static long approach_speed(fix d)
{
    if (d < 0x50000)
        return G3C_FIXMUL(FIXDIV(d, 0x50000), 0x1C20000) >> 16;
    if (d < 0x280000)
        return G3C_FIXMUL(FIXDIV(d, 0x230000), 0x7D00000) >> 16;
    return 0x9C4;
}

uchar aircraft_284F2C(long arg)
{
    uchar *rec = (uchar *)arg;
    GameEntity *e = &G3C_ENT(REC_ENT(rec));
    fix dx = strike_x - e->x, dy = strike_y - e->y;
    fix d = fix_fast_pyth_dist(dx, dy);
    short q;

    W(rec, 0x138) = 0;
    q = (short)(fix_atan2(dy >> 2, dx >> 2) - e->heading);
    q = (short)(q / 4);
    if (d < 0x280000) {
        ordnance_29150C();
        return 0;
    }
    L(rec, 0x132) = 0x9C4;
    if ((d >> 13) > 0) {
        if (abs32(q) < 0x1000)
            W(rec, 0x136) = q / 2;
        else {
            L(rec, 0x132) = 0;
            W(rec, 0x136) = q < 0 ? -0x800 : 0x800;
        }
    }
    return 1;
}

void aircraft_285024(long arg)
{
    uchar *rec = (uchar *)arg;
    short me = REC_ENT(rec);
    GameEntity *e = &G3C_ENT(me);
    fix dx, dy, d, h;

    W(rec, 0x138) = 0;
    ground(strike_entity, e->x, e->y);
    dx = game_4414C8 - e->x;
    dy = game_4414CC - e->y;
    d = fix_fast_pyth_dist(dx, dy);
    if (game_35B6D4 == 0) {
        if (!steer(rec, (short)(fix_atan2(dy >> 2, dx >> 2) - e->heading)))
            return;
    }
    if (d < 0x18000) {
        if (game_35B6D4 == 0)
            air_35B6DC = game_time + 5000;
        game_35B6D4 = 1;
        h = ground(REC_ENT(rec), e->x, e->y);
        L(rec, 0x132) = 0;
        if (game_time < air_35B6DC && h + 0x10000 < e->z)
            W(rec, 0x136) = 0x4B;
        else
            W(rec, 0x136) = 0;
        if (h + 0x8000 < e->z && game_time > game_35B6D8) {
            game_35B6D8 = game_time + 0x4B;
            if (h + 0x8000 < e->z && h + 0x20000 > e->z)
                e->z -= G3C_FIXMUL(0x2000, FIXDIV(e->z - (h + 0x8000), 0x18000));
            else
                e->z -= 0x2000;
        }
        if (h + 0x10000 > e->z)
            game_35B6E0 = 1;
    } else
        L(rec, 0x132) = approach_speed(d);
}

void aircraft_285348(uchar *rec)
{
    GameEntity *e = &G3C_ENT(REC_ENT(rec)), *p = &G3C_ENT(game_player_entity);
    fix dx = p->x - e->x, dy = p->y - e->y;
    fix d = fix_fast_pyth_dist(dx, dy);
    short diff;

    W(rec, 0x138) = 0;
    if (d < 0x50000) {
        W(rec, 0x136) = 0;
        return;
    }
    diff = (short)(fix_atan2(dy >> 2, dx >> 2) - e->heading);
    if (abs32(diff) < 0x1000) {
        W(rec, 0x136) = diff / 2;
        L(rec, 0x132) = d > 0x320000 ? 0x3E8 : 0x1F4;
    } else {
        ushort v = diff < 0 ? 0xF800 : 0x800;
        L(rec, 0x132) = 0xFA;
        UW(rec, 0x136) = v >> 1;
    }
}

void aircraft_285460(long ent)
{
    short me = (short)ent;
    long i;
    const long k = 2;                          /* the aircraft's view */

    for (i = 0; i < 4; i++)
        if (me == AIR_ENT(i))
            break;
    if (i == 4)
        return;
    if (i == 0) {
        uchar m;
        *(ulong *)&game_38FC12[k][6] |= 0x11C00;
        if (me == *(short *)CAM(0xBC)) {
            hud_26BA18();
            hudview_26C318(k, 8);
            game_38FC12[k][7] &= 0xFE;
        } else
            hudview_26C318(k, G3C_ENT(me).type == 1 ? 0x13 : 0x11);
        m = game_38FC12[k][0];
        if (m == 0x14 || m == 0x12 || m == 0x11 || m == 0x13)
            hudview_26C318(k, m);
        if (G3C_ENT(me).type == 1)
            game_3968B5 = 0;
    }
    if (G3C_ENT(me).type == 1)
        air_target[i] = 0;
    AIR_ENT(i) = 0;
    air_dead[i] = 0;
}

void aircraft_28559C(long ent)
{
    schar side = (schar)mfddata_2665E8((short)ent);
    uchar *r;

    if (side == -1)
        return;
    air_target[side] = 0;
    r = CLS3(G3C_ENT(AIR_ENT(side)).slot) + 0x132;
    L(r, 0x10) = 0;
    L(r, 0xC) = 0;
    L(r, 8) = 0;
    W(r, 6) = 0;
    W(r, 4) = 0;
    L(r, 0) = 0;
}

void aircraft_285608(void)
{
    long m = game_359708;
    if (m == 2)
        air_mission_flags[0] |= 1;
    else if (m == 6)
        air_mission_flags[1] |= 1;
    else if (m == 0x16)
        air_mission_flags[2] |= 1;
    else if (m == 0x1A)
        air_mission_flags[3] |= 1;
    game_359708 = m;
}

void aircraft_28565C(uchar *rec)
{
    static const uchar order[5] = {4, 0, 1, 2, 3};
    static const long mission[5] = {2, 6, 0x16, 0x1A, 0x1E};
    long j;

    for (j = 0; j < 5; j++) {
        uchar *fl = &air_mission_flags[order[j]];
        if ((*fl & 1) && game_359708 == mission[order[j]]) {
            if (!aircraft_2857E8(rec, fl, air_35B6B0))
                return;
            *fl &= 0xFE;
            aircraft_2844F4(REC_ENT(rec));
            *fl |= 0x10;
            return;
        }
    }
}

uchar aircraft_2857E8(uchar *rec, uchar *flag, fix *pos)
{
    short me = REC_ENT(rec);
    GameEntity *e = &G3C_ENT(me);
    fix dx = pos[0] - e->x, dy = pos[1] - e->y;
    fix d = fix_fast_pyth_dist(dx, dy);
    fix dz = pos[2] - e->z;
    short diff;

    W(rec, 0x138) = 0;
    if (aircraft_284A6C(me, e->x, e->y) + 0x48000 >= e->z) {
        e->z += 0x2000;
        return 0;
    }
    *flag |= 8;
    if (e->z < 0x48000)
        L(rec, 0x142) = dz > 0 ? -0x800 : 0x800;
    else
        L(rec, 0x142) = 0;
    if (game_359708 != 0xE && d < 0xA0000)
        return 1;
    if (d < 0x40000)
        return 1;
    diff = (short)(fix_atan2(dy >> 2, dx >> 2) - e->heading);
    L(rec, 0x132) = (*flag & 8) ? 0x3E8 : 0x64;
    if (abs32(diff) < 0x1000)
        W(rec, 0x136) = diff / 8;
    else {
        W(rec, 0x136) = diff < 0 ? -0x200 : 0x200;
        L(rec, 0x132) = 0;
    }
    return 0;
}

void aircraft_2859DC(long a, long b, long c, long ecx_in)
{
    GameEntity *p;
    short px, py, s = (short)ecx_in, ent;
    uchar sq;

    if (game_359708 != 0xE)
        return;
    p = &G3C_ENT(game_player_entity);
    px = (short)(p->x >> 16);
    py = (short)(p->y >> 16);
    sq = aiperc_253AE8(0, (ushort)px | ((ulong)(ushort)py << 16), (schar)game_brains[p->f21].f005);
    if (sq) {
        s = ailow_24B924(0xE, sq, REGION(0x4413F0), REGION(0x4413F4), air_parm_35B6E6);
        if (s != 0)
            air_35B6AA = IDX15(UW(CLS3(s), 0));
    }
    if (!sq || s == 0)
        air_35B6AA = 0;
    ent = IDX15(UW(CLS3(s), 0));
    G3C_ENT(ent).z = aircraft_284A6C(ent, px, py);
    G3C_ENT(ent).f23 = 1;
}

uchar aircraft_285AFC(uchar *rec)
{
    short me = REC_ENT(rec);
    GameEntity *e = &G3C_ENT(me);
    uchar f;
    fix tx, ty, dx, dy, d;
    short diff;

    W(rec, 0x138) = 0;
    e->f1d = 0;
    f = air_35B6ED;
    if (!(f & 1)) {
        tx = REGION(0x4413E0);
        ty = REGION(0x4413E4);
    } else if (!(f & 8)) {
        if (!(f & 0x10)) {
            air_35B6F0 = game_time + 7000;
            air_35B6ED |= 0x10;
        }
        if (game_time > air_35B6F0)
            air_35B6ED |= 8;
        return 1;
    } else if (!(f & 2)) {
        e->z += 0x1200;
        aircraft_285DC4();
        return 1;
    } else if (f & 4)
        return 0;
    else {
        tx = REGION(0x4413F8);
        ty = REGION(0x4413FC);
    }
    ground(REC_ENT(rec), tx, ty);
    dx = tx - e->x;
    dy = ty - e->y;
    d = fix_fast_pyth_dist(dx, dy);
    diff = (short)(fix_atan2(dy >> 2, dx >> 2) - e->heading);
    f = air_35B6ED;
    if (!(f & 1) || (f & 2)) {
        if (!steer(rec, diff))
            return 1;
    }
    if (d < 0x18000) {
        if (!(air_35B6ED & 1))
            air_35B6ED |= 1;
        f = air_35B6ED;
        if (!(f & 4) && (f & 2)) {
            air_35B6ED = f | 4;
            return 0;
        }
        L(rec, 0x132) = 0;
        return 1;
    }
    L(rec, 0x132) = approach_speed(d);
    return 1;
}

/* entity of the leader brain of a squad */
static short squad_leader(long sq)
{
    return *(short *)((uchar *)&game_brains[((uchar *)&game_squads[sq])[0xF]] + 0xB);
}

void aircraft_285DC4(void)
{
    long k;

    if (game_time <= air_35B6F4)
        return;
    k = (air_35B6FC >= 2) + 1;
    air_drop_pos[0] = G3C_ENT(squad_leader(k)).x;
    air_drop_pos[1] = G3C_ENT(squad_leader(k)).y;
    air_drop_pos[2] = ground(air_35B6AA, air_drop_pos[0], air_drop_pos[1]);
    air_drop_ang[0] = air_drop_ang[1] = air_drop_ang[2] = 0;
    sfx_28DD90(air_35B6AA, air_drop_pos, 0x7D0);
    air_35B6F4 = game_time + RndRange(&game_rnd_35B704, 0xFA, 0x7D0);
    SndPlay(0x815, 0, 0x28, 1, 0x40, 0x40, 0);
    air_35B6F8 = 1;
    air_35B6FC++;
    if (air_35B6FC == 2 && G3C_ENT(squad_leader(1)).cls == 1)
        aimsg_25A8E4(1);
    if (air_35B6FC == 4 && G3C_ENT(squad_leader(2)).cls == 1)
        aimsg_25A8E4(2);
    if (air_35B6FC >= 5) {
        if (G3C_ENT(squad_leader(4)).cls == 1)
            aimsg_25A8E4(4);
        air_35B6ED |= 2;
    }
}

long aircraft_286010(long obj)
{
    uchar *r = CLS3(G3C_ENT(obj).slot) + 0x14B;
    air_ret[0] = UW(r, 0);
    air_ret[1] = UW(r, 2);
    return (long)air_ret;
}

void aircraft_28605C(ulong dt)
{
    GameEntity *se;
    uchar *r;
    fix v[3], s, c, p;
    fix k;

    if (strike_entity == 0 || dt == 0)
        return;
    k = (fix)(0x3E80000ul / dt);
    se = &G3C_ENT(strike_entity);
    r = CLS3(se->slot) + 0x14B;
    fix_sincos(se->heading, &s, &c);
    g3_vec_sub((g3s_vector *)v, (g3s_vector *)&se->x, (g3s_vector *)air_strike_prev);
    g3_vec_scale((g3s_vector *)v, (g3s_vector *)v, k);
    p = G3C_FIXMUL(v[0], c) + G3C_FIXMUL(v[1], s);
    if (p < 0)
        p = 0;
    W(r, 2) = (short)FIXDIV(0x8000, p + 0x20000);
    memcpy(air_strike_prev, &se->x, 12);
    memcpy(air_strike_vel, v, 12);
}

void aircraft_28618C(void)
{
    ushort *a = (ushort *)(CLS3(G3C_ENT(strike_entity).slot) + 0x14B);
    ulong t;

    if (strike_entity == 0 || !game_35B6E0)
        return;
    if (!game_35B6A8) {
        game_35B6A8 = 1;
        RadioMessage(0x4A);
        air_35B700 = game_time;
        SndPlay(0x7F9, 2, 0x40, 0, strike_entity, 0, 0);
        return;
    }
    t = game_time - air_35B700;
    if (t <= 0x32)
        return;
    if (*a < 0x38E3 && !strike_aborted)
        *a = (ushort)(*a + (t > 0x64 ? 0x2D8 : 0x16C));
    else if (strike_aborted)
        *a -= 0x2D8;
    else {
        strike_35BF40 = 1;
        snd_stop_entity(strike_entity);
    }
    air_35B700 = game_time;
}

long aircraft_2862B0(void)
{
    long i;
    uchar f;

    switch (game_359708) {
    case 2: i = 0; break;
    case 6: i = 1; break;
    case 0x16: i = 2; break;
    case 0x1A: i = 3; break;
    case 0x1E: i = 4; break;
    default: return 0;
    }
    f = air_mission_flags[i];
    if (f & 0x10)
        return 2;
    if (f & 1)
        return 1;
    return 0;
}

void aircraft_286324(void)
{
    if (G3C_ENT(squad_leader(14)).cls == 4)
        aimsg_25A8E4(0xE);
}

uchar aircraft_28635C(uchar *rec)
{
    GameEntity *e = &G3C_ENT(REC_ENT(rec));
    return e->x < 0x190000 || e->y < 0x190000 || e->x > 0x1E70000 || e->y > 0x1E70000;
}
