/* weapphys.c -- Terra Nova module M117, r:\prj\tn\v109\tn\src\tn\weapphys.c (0x2D8650-0x2DA0C2):
 * aiming and launching projectiles.  Decompiled from __FF.EXE v1.09; names ours except the
 * weapphys_xxxxxx map names kept for the functions other modules call.
 *
 *   0x2D8650 vec_set_length   scale a vector to a length (pre-scaled to keep fix_mul in range)
 *   0x2D8848 aim_lob          launch direction of a lobbed shot hitting a point
 *   0x2D8AA8 death_init       default EDMS death (ballistic body) parameters (unreferenced)
 *   0x2D8AC8 aim_lead         lead a moving target (intercept of a ray and the target's motion)
 *   0x2D8D04 aim_solve        aim_lob / aim_lead / straight at the point
 *   0x2D8D60 aim_scatter      add the shooter's aiming error
 *   0x2D8F84 aim_skill        scale an aiming error by the shooter's skill
 *   0x2D9210 fire_lob         launch a ballistic projectile (EDMS death body)
 *   0x2D9540 weapphys_2D9540  move a ray projectile one step (or trace it in full)
 *   0x2D9A70 dir_close        are two 2D directions within ~3 degrees?
 *   0x2D9AF4 fire_ray         launch a ray projectile
 *   0x2D9DC4 weapphys_2D9DC4  fire ordnance of a type from a point at a target
 *   0x2D9F84 weapphys_2D9F84  aim vector only
 *   (0x2D878E and 0x2D8D82 are labels inside 0x2D8650 / 0x2D8D60; 0x2D8F74 is the switch
 *    table of aim_skill.)
 *
 * Positions handed to EDMS are 6x the game's (EDMS units); fix_mul / fix_div are the inline
 * Watcom sequences (fix.h).  The "(abs(a) >> 16) < abs(b) / 2" tests guard fix_div(a, b).
 * Quirks kept:
 *  - aim_lob: when the first overflow guard fails the original returns sqrt of an uninitialised
 *    stack slot in out[2]; here that slot holds x*x + y*y (the value it gets on the other paths).
 *  - aim_skill: when the shooter has a speaker record it uses the voice of speaker record 1
 *    (the "found" flag is used as the record index), and computes an unused value.
 *  - fire_lob returns the entity id it was given (0 when the aim failed and the shooter is not
 *    the player); weapphys_2D9540 returns the entity id in ax only (the high word is
 *    uninitialised stack in the original); it ignores whether the shot hit for move == 0.
 *  - weapphys_2D9DC4 / 2D9F84 compute fix_div(1.0, distance) and drop it.
 */
#include "g2_a.h"
#include "g1_b.h"
#include "edms.h"
#include "g3_a.h"      /* G3a callees (owner g3a) */
#include "g6_c.h"     /* added at the G5-G8 merge */

/* game_rnd_text: g1_c.h (RndStream[1], via g3_a.h) */

#undef ENT                           /* g2_b.h (via g3_a.h) has a pointer-valued ENT */
#define ENT(e) game_entities[(short)(e)]
#define PROJ(e) ((ProjRec *)game_pools[5].base + ENT(e).slot)
static const char weapphys_file[] = "r:\\prj\\tn\\v109\\tn\\src\\tn\\weapphys.c";

static long iabs(long x) { return x < 0 ? (long)(0u - (ulong)x) : x; }
static long neg(long x) { return (long)(0u - (ulong)x); }

#ifndef TN_WATCOM_ABI
/* G37: the game pushes a soldier back by the whole line from its muzzle to the shot's aim
 * point (st[3..5]) times the weapon's recoil, not by the line's direction.  Aiming at nothing
 * that point is 10 units (60 m) out, but a locked target can be two or three times as far, and in
 * the air nothing slows the push down: shot after shot at a far target sent a flying suit off
 * across the map.  The line is cut to the free aim's 10 units; nearer targets push as before.
 * (A lobbed shot, fire_lob, pushes by its launch speed and is left alone) */
static void recoil_line(const fix *st, fix d, fix *r)
{
    r[0] = st[3];
    r[1] = st[4];
    r[2] = st[5];
    if (d > 0xA0000) {
        fix f = fix_div(0xA0000, d);
        r[0] = fix_mul(r[0], f);
        r[1] = fix_mul(r[1], f);
        r[2] = fix_mul(r[2], f);
    }
}
#endif
static long shl(long x, int n) { return (long)((ulong)x << n); }
/* fix_div(a, b) does not overflow */
static int div_ok(long a, long b) { return (iabs(a) >> 16) < iabs(b) / 2; }
static fix sq3(fix a, fix b, fix c) { return fix_mul(a, a) + fix_mul(b, b) + fix_mul(c, c); }

void vec_set_length(fix *v, fix len)
{
    long m = iabs(v[0]), r, f;
    int i, sh;
    if (iabs(v[1]) >= m) m = iabs(v[1]);
    if (iabs(v[2]) >= m) m = iabs(v[2]);
    if (m > 0x4000000) {
        for (i = 0; i < 3; i++) v[i] >>= 9;
    } else if (m > 0x400000) {
        for (i = 0; i < 3; i++) v[i] >>= 4;
    } else if (m < 0x100) {
        for (i = 0; i < 3; i++) v[i] = shl(v[i], 12);
    } else if (m < 0x10000) {
        for (i = 0; i < 3; i++) v[i] = shl(v[i], 4);
    }
    r = fix_sqrt(sq3(v[0], v[1], v[2]));
    if (div_ok(len, r)) {
        f = fix_div(len, r);
        for (i = 0; i < 3; i++) v[i] = fix_mul(v[i], f);
        return;
    }
    if (len <= 0)
        return;
    for (sh = 0; len > r; sh++)
        len >>= 1;
    f = fix_div(len, r);
    for (i = 0; i < 3; i++) v[i] = shl(fix_mul(v[i], f), sh);
}

bool aim_lob(fix x, fix y, fix z, fix c, fix g, fix *out)
{
    fix g2 = fix_mul(g, g), c2 = fix_mul(c, c);
    fix d = fix_mul(x, x) + fix_mul(y, y);
    fix e, a, b, s, f;
    if (c == 0) {
        out[0] = x;
        out[2] = z;
        out[1] = y;
        return 1;
    }
    if (!div_ok(c2, g2))
        goto fail;
    e = g2 - (fix)((ulong)fix_mul(c, z) * 2);
    e -= fix_mul(fix_div(c2, g2), d);
    if (e < 0)
        goto fail;
    a = fix_mul(g, fix_sqrt(e));
    b = g2 - fix_mul(z, c);
    if (a > b)
        a += b;
    else
        a = b - a;
    if (a < 0)
        goto fail;
    if (!div_ok(a, c2))
        goto fail;
    s = fix_sqrt((fix)((ulong)fix_div(a, c2) * 2));
    if (!div_ok(z, s))
        goto fail;
    out[2] = fix_div(z, s) + fix_mul(c, s) / 2;
    e = g2 - fix_mul(out[2], out[2]);
    if (!div_ok(e, d))
        goto fail;
    f = fix_sqrt(fix_div(e, d));
    out[0] = fix_mul(f, x);
    out[1] = fix_mul(f, y);
    return 1;
fail:
    out[0] = x;
    out[1] = y;
    out[2] = fix_sqrt(d);
    return 0;
}

void death_init(fix *d)
{
    d[0] = 0x10000;
    d[1] = 0x4000;
    d[3] = 0;
    d[2] = weapphys_4463D8;
}

bool aim_lead(fix x, fix y, fix z, fix c, fix *out, short ent)
{
    fix a0, a1, a2, B, D, A, e, r, t, q;
    physics_2D3E74(ent, &a0, &a1, &a2);
    B = sq3(a0, a1, a2) - fix_mul(c, c);
    D = fix_mul(x, a0) + fix_mul(y, a1) + fix_mul(z, a2);
    A = sq3(x, y, z);
    if (iabs(B) > 0x640000 || iabs(D) > 0x640000 || iabs(A) > 0x640000) {
        B >>= 8;
        D >>= 8;
        A >>= 8;
    }
    e = fix_mul(D, D) - fix_mul(B, A);
    if (e < 0)
        return 0;
    r = fix_sqrt(e);
    if (B < 0) {
        D = neg(D);
        B = neg(B);
    }
    t = neg(D) - r;
    if (t < 0) {
        t = r - D;
        if (t <= 0)
            return 0;
    }
    if (B <= 0 && t >= 0)
        return 0;
    if (B >= 0 && t <= 0)
        return 0;
    if (t == 0)
        return 0;
    if (!div_ok(B, t))
        return 0;
    q = fix_div(B, t);
    out[0] = a0 + fix_mul(x, q);
    out[1] = a1 + fix_mul(y, q);
    out[2] = a2 + fix_mul(z, q);
    if (out[2] < 0 && z > 0)
        return 0;
    return 1;
}

bool aim_solve(fix x, fix y, fix z, fix c, fix lob, fix *out, short ent)
{
    if (ent == 0) {
        out[0] = x;
        out[1] = y;
        out[2] = z;
        return 1;
    }
    z += biped_27ED34(ent);
    if (lob == 0)
        return aim_lead(x, y, z, c, out, ent);
    return aim_lob(x, y, z, lob, c, out);
}

void aim_scatter(fix *v, short ent, short other, long d3, uchar keep)
{
    long div, m;
    (void)d3;
    if (ent == game_player_entity)
        return;
    if (other != 0 && ENT(other).cls == 3)
        div = 4;
    else if (other == 0)
        div = 1;
    else
        div = obj55_28927C(other) ? 2 : 1;
    if (!keep) {
        RndStream *rs = (RndStream *)game_rnd_text;
        aim_wobble[0] = fix_mul(0x10000, ((RndRange(rs, 0, 0x7FFF) & 0x7FFF) >> 2) - 0x800);
        aim_wobble[1] = fix_mul(0x10000, ((RndRange(rs, 0, 0x7FFF) & 0x7FFF) >> 2) - 0x800);
        aim_wobble[2] = fix_mul(0x10000, ((RndRange(rs, 0, 0x7FFF) & 0x7FFF) >> 3) - 0x800);
        aim_skill(ent, aim_wobble);
        aim_wobble[0] /= div;
        aim_wobble[1] /= div;
        aim_wobble[2] /= div;
        if (other != 0) {
            fix p0, p1, p2, d;
            physics_2D3E74(other, &p0, &p1, &p2);
            d = fix_fast_pyth_dist(fix_fast_pyth_dist(p0, p1), p2);
            if (d < 0x8000)
                m = 1;
            else if (d < 0x10000)
                m = 2;
            else if (d < 0x18000)
                m = 4;
            else if (d < 0x20000)
                m = 8;
            else
                m = 12;
            aim_wobble[0] *= m;
            aim_wobble[1] *= m;
            m /= 2;
            if (m < 1)
                m = 1;
            aim_wobble[2] *= m;
        }
    }
    v[0] += aim_wobble[0];
    v[1] += aim_wobble[1];
    v[2] += aim_wobble[2];
}

void aim_skill(short ent, fix *v)
{
    uchar found = 0, f21;
    signed char skill;
    short acc, mul;
    long i, m;
    if (ent == game_player_entity) {
        v[2] = 0;
        v[1] = v[2];
        v[0] = v[2];
        return;
    }
    if (game_391E4C > 0) {
        for (i = 0; i < game_391E4C; i++) {
            if (ent == *(short *)((uchar *)&game_391E50[i] + 2)) {
                found = 1;
                break;
            }
        }
    }
    if (found) {
        long k = voice_soldier[(uchar)game_391E50[found].voice];
        skill = soldier_skill[k * 0x1EE];
    } else {
        GameEntity *e = &ENT(ent);
        switch (e->cls) {
        case 1: skill = skill_cls1[e->type]; break;
        case 2: skill = skill_cls2[e->type]; break;
        case 3: skill = skill_cls3[e->type]; break;
        case 4: skill = skill_cls4[e->type]; break;
        default: skill = 5; break;
        }
    }
    for (i = 0; i < 3; i++)
        v[i] = ((10 - skill) * v[i]) / 10;
    f21 = ENT(ent).f21;
    if (f21)
        acc = *(short *)((uchar *)&game_brains[f21] + 0xDB);
    else
        acc = 100;
    if (found) {
        long a = acc * 2;
        if (a > 100)
            a = 100;
        acc = (short)a;
    }
    for (i = 0; i < 3; i++) {
        m = (100 - acc) / 25;
        if (m < 1)
            m = 1;
        v[i] = m * v[i];
    }
    f21 = ENT(ent).f21;
    if (game_brains[f21].f005 == 2)
        mul = aim_mult_a[game_442900];
    else
        mul = aim_mult_b[game_442900];
    for (i = 0; i < 3; i++)
        v[i] = (mul * v[i]) / 100;
}

long fire_lob(fix x, fix y, fix z, fix speed, fix dist, fix *st, long ent, long a4,
              long scatter, long src, long tgt)
{
    fix dp[4];
    fix v[3];
    GridObjDesc desc;
    physics_handle ph;
    GameEntity *e;
    (void)dist; (void)a4;
    dp[0] = 0x10000;
    dp[1] = 0x4000;
    dp[2] = weapphys_4463D8;
    dp[3] = 0;
    if (!aim_solve(st[3], st[4], st[5], speed, weapphys_4463D8 / 6, v, (short)tgt) &&
        (short)src != game_player_entity) {
        grid_295730((short)ent);
        return 0;
    }
    vec_set_length(v, 0x10000);
    if (scatter)
        aim_scatter(v, (short)src, (short)tgt, 0, 0);
    vec_set_length(v, speed);
    st[3] = v[0] * 6;
    st[4] = v[1] * 6;
    st[5] = v[2] * 6;
    ph = EDMS_make_death((Death *)dp, (State *)st);
    e = &ENT(ent);
    phys_ent[(uchar)ph] = (short)ent;
    e->body = (uchar)ph;
    EDMS_set_autodestruct((uchar)ph);
    if (ENT(src).body)
        EDMS_ignore_collisions(e->body, ENT(src).body);
    *(fix *)&desc.pos[0] = x;
    *(fix *)&desc.pos[4] = y;
    *(fix *)&desc.pos[8] = z;
    *(ushort *)&desc.pos[0xC] = fix_atan2(st[4], st[3]);
    *(ushort *)&desc.pos[0xE] = 0;
    *(ushort *)&desc.pos[0x10] = 0;
    desc.ent = (short)ent;
    interact_297400(&desc, e->cls, e->type);
    grid_29582C(&desc);
    if (!scatter)
        return ent;
    if (!ENT(src).body || ENT(src).cls != 1)
        return ent;
    {
        OrdType *o = &ord_table[e->type];
        long k;
        if ((short)src == game_player_entity &&
            *(long *)((uchar *)&game_pba[ENT(game_player_entity).slot] + 0x3C8) > 0x30000)
            return ent;
        k = o->recoil;
        EDMS_marble_add_I14(ENT(src).body, k * neg(st[3]), neg(st[4]) * k, neg(st[5] * k) / 4);
    }
    return ent;
}

short weapphys_2D9540(short ent, long move, long ph)
{
    ProjRec *rec = PROJ(ent);
    OrdType *o = &ord_table[rec->kind];
    fix pos[3], vel[3], dist, nx, ny, nz;
    long g_info, w_info, r;
    uchar hit;
    if (move) {
        long sp, m;
        ulong t;
        if (rec->life == 0)
            return 0;
        if (rec->life < 0) {
            KillObjMark(ent, 1);
            rec->life = 0;
            return 0;
        }
        sp = o->speed;
        if (rec->fast)
            sp = shl(sp, 2);
        t = (game_frame_dt << 16) / 1000;
        m = fix_mul(sp, t) / 6;
        if (m > rec->life)
            m = rec->life;
        rec->life -= m;
        dist = m * 6;
    } else {
        dist = rec->life * 6;
    }
    vel[0] = rec->vel[0];
    vel[1] = rec->vel[1];
    vel[2] = rec->vel[2];
    pos[0] = ENT(ent).x * 6;
    pos[1] = ENT(ent).y * 6;
    pos[2] = ENT(ent).z * 6;
    r = EDMS_cast_projectile(pos, vel, 0x640000, move ? 0x10000 : 0, 0x1000, dist, ph, ph,
                             &g_info, &w_info, (bool *)&hit);
    if (!move) {
        long v;
        grid_295730(ent);
        if (r >= 1) {
            hit_tile = 0;
            hit_who = phys_ent[r];
        } else {
            v = g_info ? g_info : w_info;
            if (v == 0) {
                hit_tile = 0;
                hit_who = 0;
            } else {
                long idx = v & 0xFFF;
                uchar cls = v < 0 ? tile_tmpls[idx].cls : game_entities[idx].cls;
                hit_tile = cls == 0;
                hit_who = (short)idx;
            }
        }
        hit_pos[0] = pos[0] / 6;
        hit_pos[1] = pos[1] / 6;
        hit_pos[2] = pos[2] / 6;
        return 0;
    }
    ordnance_290ABC(ent);
    if (rec->fast == 0)
        sfx_28D730(ent, o, ENT(ent).x, ENT(ent).y, ENT(ent).z, pos[0] / 6, pos[1] / 6, pos[2] / 6);
    nz = pos[2] / 6;
    ny = pos[1] / 6;
    nx = pos[0] / 6;
    if (hit) {
        if (r > 0) {
            short tgt = phys_ent[r];
            ushort dmg;
            long k8;
            memcpy(&ENT(ent).x, &ENT(tgt).x, 0x12);
            dmg = o->dmg;
            if (o->no_falloff == 0) {
                long q = (100 * (short)(rec->life >> 16)) / (long)o->range;
                ushort pct = (ushort)(100 - q);
                if (pct > 0x19)
                    dmg = (ushort)(((long)dmg * (0x74 - ((long)pct * 2) / 3)) / 100);
            }
            physics_2D64E8(r, 0, tgt, ent, dmg, w_info, g_info, 0, pos);
            k8 = (long)o->impact * 8;
            EDMS_marble_add_I14(r, vel[0] * k8, vel[1] * k8, (vel[2] * k8) / 4);
            rec->life = 0;
            return ent;
        }
        ENT(ent).x = nx;
        ENT(ent).y = ny;
        ENT(ent).z = nz;
        physics_2D64E8(0, 0, ent, ent, o->dmg, w_info, g_info, 0, pos);
        rec->life = 0;
        return ent;
    }
    ENT(ent).x = nx;
    ENT(ent).y = ny;
    ENT(ent).z = nz;
    if (rec->life == 0)
        rec->life = -1;
    return ent;
}

bool dir_close(fix a, fix b, fix c, fix d)
{
    fix p = fix_mul(fix_mul(a, a) + fix_mul(b, b), fix_mul(c, c) + fix_mul(d, d));
    fix e = fix_mul(c, a) + fix_mul(b, d);
    return fix_mul(p, 0xFF78) > fix_mul(e, e);
}

short fire_ray(short ent, fix x, fix y, fix z, long kind, fix range, fix *st, long src,
               long move, long keep, long tgt, fix speed)
{
    OrdType *o = &ord_table[kind];
    ProjRec *rec = PROJ(ent);
    fix v[3], len;
    if ((long)((ulong)o->range << 16) < range && move == 0) {
        grid_295730(ent);
        return 0;
    }
    last_shooter = (short)src;
    if (!aim_solve(st[3], st[4], st[5], speed, 0, v, (short)tgt)) {
        grid_295730(ent);
        return 0;
    }
    vec_set_length(v, 0x10000);
    if (move) {
        if (dir_close(v[0], v[1], st[3], st[4]) && keep == 0)
            aim_scatter(v, (short)src, (short)tgt, 0, 0);
        aim_scatter(v, (short)src, (short)tgt, 0, (uchar)keep);
    }
    len = fix_sqrt(sq3(v[0], v[1], v[2]));
    if (len < 0x1000) {
        v[0] = 0;
        v[1] = 0;
        v[2] = 0x10000;
    } else if (len != 0x10000) {
        fix f = fix_div(0x10000, len);
        v[0] = fix_mul(v[0], f);
        v[1] = fix_mul(v[1], f);
        v[2] = fix_mul(v[2], f);
    }
    rec->vel[0] = v[0];
    rec->vel[1] = v[1];
    rec->vel[2] = v[2];
    rec->kind = (short)kind;
    rec->life = (long)((ulong)o->range << 16);
    ENT(ent).x = x;
    ENT(ent).y = y;
    ENT(ent).z = z;
    if (move && ENT(src).body && ENT(src).cls == 1) {
        long k = o->recoil;
#ifndef TN_WATCOM_ABI
        fix r[3];
        recoil_line(st, range, r);
        EDMS_marble_add_I14(ENT(src).body, neg(r[0]) * k, neg(r[1]) * k, neg(r[2] * k) / 4);
#else
        EDMS_marble_add_I14(ENT(src).body, neg(st[3]) * k, neg(st[4]) * k, neg(st[5] * k) / 4);
#endif
    }
    return weapphys_2D9540(ent, move, ENT(src).body);
}

short weapphys_2D9DC4(uchar kind, fix x, fix y, fix z, fix tx, fix ty, fix tz, fix speed,
                      long src, long a6, long a7, long a8, long tgt)
{
    uchar mode;
    short ent, slot;
    short key[2];
    fix st[6], d;
    if (kind == 0)
        return 0;
    mode = ord_table[kind].ray ? 2 : 1;
    if ((uchar)a7 == 0)
        mode = 2;
    key[0] = (short)src;
    key[1] = 0;
    if (!damage_27E14C(&ent, &slot, kind, key))
        return 0;
    st[0] = x * 6;
    st[1] = y * 6;
    st[2] = z * 6;
    st[3] = tx - x;
    st[4] = ty - y;
    st[5] = tz - z;
    d = fix_safe_pyth_dist(st[3], st[4], (char *)weapphys_file, 0x478);
    d = fix_safe_pyth_dist(st[5], d, (char *)weapphys_file, 0x479);
    if (mode == 1)
        return (short)fire_lob(x, y, z, speed, d, st, ent, (uchar)a6, (uchar)a7, (short)src,
                               (short)tgt);
    return fire_ray(ent, x, y, z, kind, d, st, (short)src, (uchar)a7, (uchar)a8, (short)tgt,
                    speed);
}

void weapphys_2D9F84(uchar kind, fix x, fix y, fix z, fix tx, fix ty, fix tz, fix speed,
                     long tgt, fix *out)
{
    uchar mode = ord_table[kind].ray ? 2 : 1;
    fix dx = tx - x, dy = ty - y, dz = tz - z, d;
    d = fix_safe_pyth_dist(dx, dy, (char *)weapphys_file, 0x4B4);
    d = fix_safe_pyth_dist(dz, d, (char *)weapphys_file, 0x4B5);
    (void)d;
    if (aim_solve(dx, dy, dz, speed, mode == 1 ? weapphys_4463D8 / 6 : 0, out, (short)tgt))
        vec_set_length(out, speed);
}
