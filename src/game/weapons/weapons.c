/* weapons.c -- Terra Nova module M063 (r:\prj\tn\v109\tn\src\tn\weapons.c): firing a weapon,
 * ammunition, the automatic turrets, blast damage and the firing sounds.
 * Decompiled from __FF.EXE v1.09; names ours except where other modules already call the
 * map names.
 *
 *   0x28F020 weapons_28F020 (empty)  0x28F024 weapon_ready        0x28F0A0 weapon_nop
 *   0x28F0A4 weapon_radio            0x28F0B4 weapon_fire         0x28F7FC weapon_aim_error
 *   0x28F950 weapon_use_ammo         0x28FAAC weapon_find_target  0x28FD54 turret_fire
 *   0x28FE20 turret_alert            0x28FE68 debris_reset        0x28FE74 debris_burst
 *   0x28FFC4 debris_one              0x28FFF8 weapon_facing       0x290044 weapon_blast
 *   0x2903A0 weapon_damage5          0x290448 weapon_damage8      0x2904F8 wsnd_kill_cb
 *   0x290524 wsnd_fire               0x29079C weapons_29079C      0x290858 weapons_290858
 *
 * A weapon record is 15 bytes: +0 type, +1 byte (the player's sound balance), +2 short
 * rounds/health, +4 ushort clip count, +0xA ulong time it can fire again, +0xE burst count.
 * Weapon types are 0x24-byte records at 0x395114 (WT(t)): +1 ushort damage, +4 ushort ammo
 * drawn from a store per shot (0: uses the clip count +4), +6 ushort (> 1: clip weapon),
 * +8 short rounds per "quarter", +0xC long spread * 6, +0x12 ushort refire delay, +0x16
 * ushort sound, +0x18 ushort noise sound, +0x1D burst length, +0x1E ushort burst delay.
 *
 * Quirks kept:
 *  - weapon_fire computes |d3| * 0x600 for the spread pattern and drops it;
 *  - weapon_blast finds squares through the inlined grid lookup (also in weapon_find_target):
 *    an empty bucket reads the chain of grid_hash[0];
 *  - weapon_blast's hit list holds 32 entries without a bound check;
 *  - wsnd_fire's distance takes the firer's absolute z, not the difference;
 *  - weapons_29079C uses the slot of any entity class as a PBA index.
 */
#include "g2_b.h"
#include "g3_c.h"      /* pbasim_28A878, damage_27D540, instsim_288554 (owner g3c) */
#include "g3_a.h"      /* G3a callees (owner g3a) */
#include "g6_c.h"     /* added at the G5-G8 merge */
#include "g9_a.h"     /* added at the G9/G10 merge */

#ifndef TN_LINK_ORIGINAL_DATA
uchar game_weapon_types[0x30][0x24];
short game_skill_tab[8][5];
uchar game_442900;
uchar game_442F1B;
uchar game_428BC4;
uchar game_35BF38;
uchar game_3C5E1C[128][128];
WpnSnd wsnd_slots[2];
long debris_count;
#endif

static char weapons_c[] = "r:\\prj\\tn\\v109\\tn\\src\\tn\\weapons.c";

#define SKILL(row) (game_skill_tab[row][game_442900])

void weapons_28F020(void)
{
}

/* 0 ready, 1 no rounds, 2 clip empty, 3 not enough in the store, 5 still reloading */
uchar weapon_ready(uchar *w, void *store)
{
    uchar *t;
    if (S16(w, 2) <= 0)
        return 1;
    t = WT(w[0]);
    if (U16(t, 6) > 1) {
        if (U16(w, 4) == 0)
            return 2;
    } else {
        long amt = (long)U16(t, 4) << 16;
        if (amt > objacc_28B854(store, amt, 1, 0))
            return 3;
    }
    if (game_time < U32(w, 0xA))
        return 5;
    return 0;
}

void weapon_nop(void)
{
}

void weapon_radio(long a, long b)
{
    RadioAck((short)((uchar)a * 3 + (uchar)b));
}

/* fire weapon w of entity ent from the offset (x, y, z) (the entity's own position for a
 * soldier) at (tx, ty, tz) or at object target; real = 0 only traces.  Returns what the shot
 * hit (> 0), as weapphys_2D9DC4 reports it */
short weapon_fire(uchar *w, long ent, fix x, fix y, fix z, fix tx, fix ty, fix tz, long target,
                  void *store, long real)
{
    GameEntity *o = ENT(ent);
    fix t[3], dxy, d3, spread, ex, ey, ez;
    long type, r, tgt = (short)target;
    short hit = 0;
    uchar fired = 0, nozero = (short)target == 0;
    ushort delay;
    uchar *wt;

    real = (uchar)real;
    if (o->cls != 1) {
        simdisp_289730(&x, &y, &z, U16(o, 0x1B), U16(o, 0x1D), 0);
        x += ENT(ent)->x;
        y += ENT(ent)->y;
        z += ENT(ent)->z;
        if (ENT(ent)->body)
            z -= physics_2D45CC((short)ent);
    }
    spread = S32(WT(w[0]), 0xC) / 6;
    if ((short)target)
        interact_298864((short)target, t);
    else {
        t[0] = tx;
        t[1] = ty;
        t[2] = tz;
    }
    dxy = fix_safe_pyth_dist(t[0] - x, t[1] - y, weapons_c, 0x261);
    d3 = fix_safe_pyth_dist(t[2] - z, dxy, weapons_c, 0x262);
    (void)d3;
    type = w[0];
    if (type == 12) {
        if (real)
            weapon_blast((short)ent, (long)U16(WT(12), 0xA) << 16, x, y, z);
        hit = -1;
        fired = 1;
        if (ENT(ent)->cls != 1 || !real)
            goto post;
        mstat_2C6FA4((short)ent);
        goto post;
    }
    if (type == 5 || type == 0x1B) {
        r = weapphys_2D9DC4(type, x, y, z, t[0], t[1], t[2], spread, (short)ent, nozero, real, 0, tgt);
        if (!real || (short)r <= 0)
            return (short)r;
        ex = fix_mul(t[1] - y, 0x600);
        ey = fix_mul(x - t[0], 0x600);
        ez = fix_mul(dxy, 0x600);
        if (w[0] == 5) {
            r = weapphys_2D9DC4(w[0], x, y, z, t[0] + ex, t[1] + ey, t[2] + ez, spread, (short)ent,
                                nozero, real, 1, tgt);
            if ((short)r <= 0)
                return (short)r;
            r = weapphys_2D9DC4(w[0], x, y, z, t[0] + ex, t[1] + ey, t[2] - ez, spread, (short)ent,
                                nozero, real, 1, tgt);
            if ((short)r <= 0)
                return (short)r;
            r = weapphys_2D9DC4(w[0], x, y, z, t[0] - ex, t[1] - ey, t[2] + ez, spread, (short)ent,
                                nozero, real, 1, tgt);
            if ((short)r <= 0)
                return (short)r;
            hit = (short)weapphys_2D9DC4(w[0], x, y, z, t[0] - ex, t[1] - ey, t[2] - ez, spread,
                                         (short)ent, nozero, real, 1, tgt);
            if (ENT(ent)->cls != 1 || !real)
                goto post;
            mstat_2C6FA4((short)ent);
            mstat_2C6FA4((short)ent);
            mstat_2C6FA4((short)ent);
            mstat_2C6FA4((short)ent);
            mstat_2C6FA4((short)ent);
            goto post;
        }
        r = weapphys_2D9DC4(w[0], x, y, z, t[0] + ex, t[1] + ey, t[2], spread, (short)ent,
                            nozero, real, 1, tgt);
        if ((short)r <= 0)
            return (short)r;
        hit = (short)weapphys_2D9DC4(w[0], x, y, z, t[0] - ex, t[1] - ey, t[2], spread, (short)ent,
                                     nozero, real, 1, tgt);
        if (hit <= 0)
            return hit;
        goto post;
    }
    hit = (short)weapphys_2D9DC4(type, x, y, z, t[0], t[1], t[2], spread, (short)ent, nozero, real,
                                 0, tgt);
    if (ENT(ent)->cls == 1 && real && w[0] != 8)
        mstat_2C6FA4((short)ent);

post:
    if (!real)
        return hit;
    if (hit <= 0 && !fired)
        return hit;
    if (snd_channels)
        wsnd_fire((short)ent, w, hit);
    wt = WT(w[0]);
    delay = U16(wt, 0x12);
    if (B8(wt, 0x1D)) {
        w[0xE]++;
        if (U32(w, 0xA) - delay + U16(wt, 0x1E) < game_time)
            w[0xE] = 1;
        else if (w[0xE] >= B8(wt, 0x1D)) {
            delay = U16(wt, 0x1E);
            w[0xE] = 0;
        }
    }
    if ((short)ent == game_player_entity) {
        if (game_35CD9C && (game_442F1B & 4))
            delay = 0;
        else
            delay = (ushort)((ulong)((long)delay * SKILL(1)) / 100);
    } else {
        GameEntity *e = ENT(ent);
        if (e->cls == 1 && (e->type == 0 || e->type == 1 || e->type == 2 || e->type == 0xD))
            ;
        else
            delay = (ushort)((ulong)((long)delay * SKILL(2)) / 100);
    }
    U32(w, 0xA) = game_time + delay;
    if (U16(WT(w[0]), 4)) {
        if (!(game_35CD9C && (game_442F1B & 4) && (short)ent == game_player_entity))
            objacc_28B854(store, (long)U16(WT(w[0]), 4) << 16, 0, 1);
    } else {
        if (game_428BC4 == 0)
            U16(w, 4)--;
        if (game_35CD9C && (game_442F1B & 2) && (short)ent == game_player_entity)
            U16(w, 4)++;
        if (U16(w, 4) == 0 && (short)ent == game_player_entity) {
            RadioAck((short)(w[0] * 3 + 2));
            weapons_290858();
        }
    }
    {
        ulong *p = damage_27D540((short)ent);
        if (p)
            *p = game_time + 10000;
    }
    return hit;
}

/* how far a shot from weapon w would miss: |spread^2 - |miss|^2| (miss from weapphys_2D9F84) */
fix weapon_aim_error(uchar *w, long ent, fix x, fix y, fix z, fix tx, fix ty, fix tz, long target,
                     fix *miss)
{
    GameEntity *o = ENT(ent);
    fix t[3], spread, s, d;
    if (o->cls != 1) {
        simdisp_289730(&x, &y, &z, U16(o, 0x1B), U16(o, 0x1D), 0);
        x += o->x;
        y += o->y;
        z += o->z;
    }
    spread = S32(WT(w[0]), 0xC) / 6;
    if ((short)target)
        interact_298864((short)target, t);
    else {
        t[0] = tx;
        t[1] = ty;
        t[2] = tz;
    }
    weapphys_2D9F84(w[0], x, y, z, t[0], t[1], t[2], spread, (short)target, miss);
    s = fix_mul(miss[0], miss[0]);
    s += fix_mul(miss[1], miss[1]);
    s += fix_mul(miss[2], miss[2]);
    d = fix_mul(spread, spread) - s;
    return d < 0 ? -d : d;
}

/* take n rounds off weapon w of ent (n >= 5 and some other weapon of the object still able
 * to fire, or the object has none).  0 nothing to say, 1 one quarter left, 2 down to the last
 * quarter, 3 empty */
uchar weapon_use_ammo(long ent, uchar *w, long n)
{
    void *parts, *hp;
    long np;
    uchar *wl;
    void *store;
    long nw;
    uchar old, now;
#ifdef TN_WATCOM_ABI
    short v;
#else
    long v;                             /* static_286794 writes a long here (docs/BUGS.md P11) */
#endif

    if (S16(w, 2) <= 0 || n < 5)
        return 0;
    if (static_286794((short)ent, &parts, &hp, &np, &v, &store, &wl, &nw)) {
        long i, cnt = 0;
        for (i = 0; i < nw; i++) {
            uchar r;
            if (wl + i * 15 == w)
                continue;
            r = weapon_ready(wl + i * 15, store);
            if (r != 1 && r != 2)
                cnt++;
        }
        if (cnt == 0)
            return 0;
    }
    old = (uchar)(S16(w, 2) * 4 / S16(WT(w[0]), 8));
    nw = S16(w, 2) - n;
    if (nw < 0)
        nw = 0;
    S16(w, 2) = (short)nw;
    if ((short)nw == 0) {
        if ((short)ent == game_player_entity)
            RadioAck((short)(w[0] * 3 + 1));
        return 3;
    }
    now = (uchar)(S16(w, 2) * 4 / S16(WT(w[0]), 8));
    if (now == old)
        return 0;
    if (now == 0)
        return 2;
    if (now != 1)
        return 0;
    if ((short)ent == game_player_entity)
        RadioAck((short)(w[0] * 3));
    return 1;
}

/* the head of the grid chain of square (x, y) (inlined grid lookup) */
static short grid_head(short x, short y)
{
    GridXY key;
    long e;
    key.x = x;
    key.y = y;
    grid_hash_cur = (short)(((x * 4 + y) & 0x1FF) + 0x200);
    if (grid_hash[grid_hash_cur].sq == 0)
        e = 0;
    else if (grid_squares[grid_hash[grid_hash_cur].sq].x == x
             && grid_squares[grid_hash[grid_hash_cur].sq].y == y)
        e = grid_hash_cur;
    else
        e = (short)grid_295CFC(key, 0, grid_hash_cur);
    return grid_hash[e].sq;
}

/* the nearest object within range of ent that a turret would shoot at (any = 1: the first
 * one found) */
short weapon_find_target(long ent, fix range, long alive, long any)
{
    long gx, gy, best = 0, bestd = 0x7FFFFFFF;
    alive = (uchar)alive;
    any = (uchar)any;
    for (gx = (ENT(ent)->x - range) >> 16; (ENT(ent)->x + range) >> 16 >= gx; gx++) {
        for (gy = (ENT(ent)->y - range) >> 16; (ENT(ent)->y + range) >> 16 >= gy; gy++) {
            short sq;
            for (sq = grid_head((short)gx, (short)gy); sq; sq = grid_squares[sq].next) {
                short other = grid_squares[sq].ent;
                GameEntity *o, *me;
                fix d;
                if (other == (short)ent)
                    continue;
                o = ENT(other);
                if (o->cls == 0)
                    continue;
                if (o->cls == 4) {
                    if (o->type != 0x45 && o->type != 0x46 && o->type != 0x47) {
                        if (game_359708 != 0x2F)
                            continue;
                        if (o->type == 0x2E)
                            continue;
                    }
                } else if (o->f21 == 0)
                    continue;
                if (alive) {
                    if (o->cls != 4 && game_brains[o->f21].f005 == 1)
                        continue;
                    if (o->cls == 1 && o->type <= 2)
                        continue;
                }
                if (any)
                    return other;
                me = ENT(ent);
                d = fix_fast_pyth_dist(me->x - o->x, me->y - o->y);
                d = fix_fast_pyth_dist(d, (me->z - o->z) / 2);
                if (d >= range)
                    continue;
                if ((short)best == 0 || d < bestd) {
                    best = other;
                    bestd = d;
                }
            }
        }
    }
    return (short)best;
}

/* class 4 object i: fire its weapon (record +0x33) at the nearest target within 8 */
void turret_fire(long i)
{
    uchar *rec = game_cls4[(short)i];
    long me = (short)(S16(rec, 0) << 1) >> 1;
    long tg = weapon_find_target(me, 0x80000, 1, 0);
    fix tx, ty, tz, a[3];
    if ((short)tg == 0)
        return;
    tg = (short)tg;
    tx = ENT(tg)->x;
    ty = ENT(tg)->y;
    tz = ENT(tg)->z;
    interact_29837C(me, tx, ty, tz, &a[2], &a[1], &a[0]);
    weapon_fire(rec + 0x33, me, a[2], a[1], a[0], tx, ty, tz, tg, rec + 0x56, 1);
}

/* class 4 object i: wake its sim when something is within 2 */
void turret_alert(long i)
{
    long me = (short)(S16(game_cls4[(short)i], 0) << 1) >> 1;
    if (weapon_find_target(me, 0x20000, 1, 1))
        instsim_288554(me, 1);
}

void debris_reset(void)
{
    debris_count = 6;
}

/* the debris left of the count, thrown from pos (x, y, z, +0xC heading, +0xE pitch) */
void debris_burst(long unused, fix *pos, fix speed)
{
    fix v[3], s1, c1, s2, c2;
    long a1, a2, r;
    (void)unused;
    if (debris_count == 0)
        return;
    do {
        ushort h = U16(pos, 0xC), p = U16(pos, 0xE);
        r = RndRange(game_rnd_text, 0, 0x7FFF) % 256;
        r += RndRange(game_rnd_text, 0, 0x7FFF) % 256;
        a1 = (r << 5) + h - 0x2000;
        a2 = ((RndRange(game_rnd_text, 0, 0x7FFF) % 256) << 4) + p - 0x800;
        fix_sincos((fixang)a1, &s1, &c1);
        fix_sincos((fixang)a2, &s2, &c2);
        v[0] = pos[0] + fix_mul(c1, fix_mul(c2, speed));
        v[1] = pos[1] + fix_mul(s1, fix_mul(c2, speed));
        v[2] = pos[2] + fix_mul(s2, speed);
        sfx_28D448(pos, v);
    } while (--debris_count);
}

void debris_one(long ent, long unused, fix *from)
{
    (void)unused;
    if (debris_count == 0)
        return;
    sfx_28D448(from, &ENT(ent)->x);
    debris_count--;
}

/* (x, y) within 45 degrees of ent's heading */
uchar weapon_facing(long ent, fix x, fix y)
{
    GameEntity *o = ENT(ent);
    ushort a = (ushort)(fix_atan2(y - o->y, x - o->x) - U16(o, 0x1B));
    return a <= 0x2000 || a >= 0xE000;
}

/* blast of the given radius around (x, y, z) set off by ent (weapon type 12): damages every
 * object (once) on the map squares in front of ent within the radius */
void weapon_blast(long ent, fix radius, fix x, fix y, fix z)
{
    short *list = ffmem_2C4F40(0x40);
    fix pos[5];
    long n = 0, gx, gy, cx;
    short e = (short)ent;

    {
        long k;
        for (k = 0; k < 0x12; k++)
            ((uchar *)pos)[k] = ((uchar *)&ENT(e)->x)[k];
    }
    pos[0] = x;
    pos[1] = y;
    pos[2] = z;
    debris_count = 6;
    game_35BF38 = 1;
    for (gx = (ENT(e)->x - radius) >> 16; (ENT(e)->x + radius) >> 16 >= gx; gx++) {
        cx = (gx << 16) | 0x8000;
        for (gy = (ENT(e)->y - radius) >> 16; (ENT(e)->y + radius) >> 16 >= gy; gy++) {
            short sq;
            uchar tile;
            if (!weapon_facing(e, cx, (gy << 16) | 0x8000))
                continue;
            tile = game_3C5E1C[gx >> 2][gy >> 2];
            if (tile)
                physics_2D7140(tile, gx >> 2, gy >> 2);
            for (sq = grid_head((short)gx, (short)gy); sq; sq = grid_squares[sq].next) {
                short other = grid_squares[sq].ent;
                long i;
                fix d;
                uchar r;
                if (other == e)
                    continue;
                if (ENT(other)->cls == 6 || ENT(other)->cls == 5)
                    continue;
                for (i = 0; i < n && other != list[i]; i++)
                    ;
                if (i != n)
                    continue;
                list[n] = other;
                d = fix_fast_pyth_dist(ENT(e)->x - ENT(other)->x, ENT(e)->y - ENT(other)->y);
                d = fix_fast_pyth_dist(d, ENT(e)->z - ENT(other)->z);
                n++;
                if (d >= radius)
                    continue;
                r = physics_2D6644(other, e);
                static_287168(other, e, r, B8(WT(11), 0x22), U16(WT(12), 1));
                if (debris_count) {
                    sfx_28D448(pos, &ENT(other)->x);
                    debris_count--;
                }
            }
        }
    }
    debris_burst(e, pos, radius);
    game_35BF38 = 0;
    ffmem_2C4FE4(list);
}

/* damage of a class 5 object (the owner's skill scales the player's side) */
long weapon_damage5(long ent)
{
    GameEntity *o = ENT(ent);
    long owner = S16(game_cls5[o->slot], 6);
    long sc = game_brains[ENT(owner)->f21].f005 == 2 ? SKILL(0) : 100;
    return (long)U16(WT(ENT(ent)->type), 1) * sc / 100;
}

/* damage val of a class 8 object, times its count byte +0xB, scaled like weapon_damage5 */
long weapon_damage8(long ent, long val)
{
    GameEntity *o = ENT(ent);
    long owner = S16(game_cls8[o->slot], 6);
    long sc = game_brains[ENT(owner)->f21].f005 == 2 ? SKILL(0) : 100;
    return (short)(val * B8(game_cls8[ENT(ent)->slot], 0xB)) * sc / 100;
}

/* killobj callback: forget the pending sounds of a dead object */
void wsnd_kill_cb(long ent)
{
    long i;
    for (i = 0; i < 2; i++)
        if (wsnd_slots[i].ent == (short)ent || wsnd_slots[i].hit == (short)ent)
            wsnd_slots[i].snd = 0;
}

#ifndef TN_WATCOM_ABI
static ulong gun_last_shot;             /* G38: game_time of the player's last shot */
#endif

/* the sound of ent firing weapon w (hit: what it hit).  The player's own gun plays at once;
 * others go to the two nearest-shot slots played by weapons_29079C */
void wsnd_fire(long ent, uchar *w, long hit)
{
    ulong noise = U16(WT(w[0]), 0x18);
    ulong snd = U16(WT(w[0]), 0x16);
    uchar type = w[0];
    GameEntity *o, *p;
    fix d;
    long i;

    hit = (short)hit;
    if ((short)ent == game_player_entity) {
        uchar *pba = (uchar *)&game_pba[game_player_slot];
        long ch = S32(pba, 0x82), r;
        long bal = (4 - (long)w[1]) * 42;
        if (ch >= 0)
            snd_chan_stop(ch);
#ifndef TN_WATCOM_ABI
        gun_last_shot = game_time;
#endif
        r = SndPlay((ushort)snd, 0, 0xFF, type != 4, -1, bal, 0);
        pba = (uchar *)&game_pba[game_player_slot];
        S32(pba, 0x82) = r;
        if (r >= 0)
            snd_chan_set_handle_ptr(S32(pba, 0x82), (long *)(pba + 0x82));
        goto noise;
    }
    if (type >= 0x19 && type <= 0x1B) {
        if (ENT(ent)->cls == 2)
            snd = 0x830;
        else if (ENT(ent)->cls == 3)
            snd = 0x82F;
    }
    if (ENT(ent)->f21 && game_brains[ENT(ent)->f21].f005 != 2)
        snd += 0xD;
    o = ENT(ent);
    p = ENT(game_player_entity);
    d = fix_safe_pyth_dist(o->x - p->x, o->y - p->y, weapons_c, 0x55E);
    d = fix_safe_pyth_dist(d, o->z, weapons_c, 0x55E);
    for (i = 0; i < 2; i++) {
        if (d < wsnd_slots[i].dist || wsnd_slots[i].snd == 0) {
            wsnd_slots[i].ent = (short)ent;
            wsnd_slots[i].dist = d;
            wsnd_slots[i].hit = (short)hit;
            wsnd_slots[i].snd = (short)snd;
            wsnd_slots[i].noise = (short)noise;
            if ((short)hit > 0)
                killobj_289554((short)hit, WATCOM_FN(wsnd_kill_cb), -1);
            killobj_289554((short)ent, WATCOM_FN(wsnd_kill_cb), -1);
            break;
        }
    }
noise:
    if ((short)ent == game_player_entity && (short)noise == 0x770 && (short)hit >= 0)
        noisy_add((short)hit, (ushort)noise);
}

/* play the two queued firing sounds */
void weapons_29079C(void)
{
    WpnSnd *s;
    for (s = wsnd_slots; s != wsnd_slots + 2; s++) {
        uchar *pba;
        if (s->snd == 0)
            continue;
        pba = (uchar *)&game_pba[ENT(s->ent)->slot];
        if (S32(pba, 0x82) < 0) {
            long r = SndPlay((ushort)s->snd, 2, 0xB4, 1, s->ent, 0, 0);
            if (r >= 0) {
                S32(pba, 0x82) = r;
                snd_chan_set_handle_ptr(r, (long *)(pba + 0x82));
            }
        }
        if (s->noise && s->hit > 0)
            noisy_add(s->hit, (ushort)s->noise);
        s->snd = 0;
        s->dist = 0;
    }
}

/* stop the player's gun sound (unless it is a looping one) */
void weapons_290858(void)
{
    uchar *pba = (uchar *)&game_pba[game_player_slot];
    long ch;
    B8(pba, 0xCF) &= ~2;
    ch = S32(pba, 0x82);
    if (ch < 0 || snd_chans[ch].loops == 1)
        return;
    snd_chan_stop(S32(pba, 0x82));
    S32((uchar *)&game_pba[game_player_slot], 0x82) = -1;
}

#ifndef TN_WATCOM_ABI
/* G38: the player's laser sound loops (wsnd_fire plays it with no end) from a shot until the
 * trigger is let go (weapons_290858).  With the trigger held and the suit's store too low for a
 * shot (the jump jets draw from it too) the laser fires no more, but its sound went on: a laser
 * heard with no beam.  Called each frame the trigger is held (the HUD's tick, and
 * hview3d_274C30 for the cockpit's other views): with the trigger held (0xCF bit 2), the
 * weapon unable to fire for want of energy or rounds (not merely between shots) and no shot for
 * 150 ms (the laser fires every 100 to 120 ms), its looping sound stops; the next shot plays it
 * again.  The trigger stays held, so the laser fires again as the store refills, as before. */
void port_gun_quiet(void)
{
    uchar *pba = (uchar *)&game_pba[game_player_slot];
    uchar *ws = pba + 0x45 + 15 * (schar)pba[0xCE];
    long ch = S32(pba, 0x82);
    uchar r;

    if (!(pba[0xCF] & 2) || ch < 0 || snd_chans[ch].loops == 1)
        return;
    r = weapon_ready(ws, pba + 0x86);
    if (r == 0 || r == 5 || game_time - gun_last_shot < 150)
        return;
    snd_chan_stop(ch);
    S32(pba, 0x82) = -1;
}
#endif
