/* sfx.c -- Terra Nova module M060 (0x28B9D0-0x28DE1F; "r:\prj\tn\v109\tn\src\tn\sfx.c"): visual
 * effects -- explosions, impacts, debris, burning, smoke, flickering fires, tracers, lightning.
 * Decompiled from __FF.EXE v1.09.
 *
 *   0x28B9D0 sfx_28B9D0 (data) template of sfx_28DC50
 *   0x28B9E0 sfx_28B9E0 particles block a ray?      0x28BA34 sfx_28BA34 impact effect by ground
 *   0x28BAF8 sfx_28BAF8 hit effect by class         0x28BB80 sfx_28BB80 debris piece
 *   0x28BD84 sfx_28BD84 flying object               0x28BDF0 sfx_28BDF0 debris from an entity
 *   0x28BEDC sfx_28BEDC random jitter (no callers)  0x28BEFC sfx_28BEFC debris ring
 *   0x28BFF0 sfx_28BFF0 debris through a box        0x28C228 sfx_28C228 debris of an L shape
 *   0x28C2F4 sfx_28C2F4 (no callers)                0x28C300 sfx_28C300 explosion of an entity
 *   0x28C5D8 sfx_28C5D8 scorch a map cell           0x28C714 sfx_28C714 run an effect
 *   0x28CB24 / 0x28CB58 / 0x28CBD0 / 0x28CC0C / 0x28DC50 / 0x28DD90 / 0x28DDD8 effect wrappers
 *   0x28CC68 sfx_28CC68 update the effect objects   0x28D0F4 sfx_28D0F4 new effect object
 *   0x28D2DC / 0x28D318 (no callers) set fly / flicker
 *   0x28D368 sfx_28D368 attach particles            0x28D448 sfx_28D448 lightning bolt
 *   0x28D4E8 sfx_28D4E8 translucency tables         0x28D5B8 sfx_28D5B8 animation end callback
 *   0x28D5C8 sfx_28D5C8 flicker spawn               0x28D6D0 sfx_28D6D0 flicker interval
 *   0x28D730 sfx_28D730 tracer (the map's sfx_28D710, 28D7FB, 28D99A, 28D9E1, 28DAD4, 28DB1B and
 *            28DB5F are its jump table and switch cases; 0x28D6F0 is another, no-op, table)
 *   0x28DC9C sfx_28DC9C 3D interpreter callback     0x28DD7C sfx_28DD7C install it
 *   0x28DDA4 sfx_28DDA4 sound (no callers)
 *
 * Effect objects are class 6 entities (game_cls6 records, SfxObj): types 0x1C tracer segment,
 * 0x1D particle group, 0x1F lightning bolt, 0x20 debris piece, 0x21 ragdoll part (vec3.c), other
 * types are animations (resource 0x4AB0000 lists one per type) that remove themselves when the
 * animation ends.  An SfxDesc says what an effect creates: an object, a sound, a scorch mark,
 * smoke, a flickering fire, particles, a debris ring.
 *
 * Quirks kept:
 *  - sfx_28D448 and sfx_28D730 leave heading / +10 / +12 of the object descriptor unset (stack
 *    garbage) -- the C sets them to 0 (the tests zero the original's stack the same way).
 *  - sfx_28CC68 hands FF_terrain a terrain_ff whose caller (+0x30) it never sets (FF_terrain keeps
 *    it in ff_cache 0x35ED20) -- the C sets it to 0.
 *  - sfx_28CC68 reads the debris spin speeds as unsigned words (a negative spin turns fast).
 *  - sfx_28C714 marks object slot 0 (flags |= 0x40) when its object could not be created, and
 *    scorches around entity ent even when ent is 0.
 *  - sfx_28D0F4 calls map_2949C4 twice when the height above ground is not negative.
 */
#include "g6_c.h"
#include "g6_b.h"     /* added at the G5-G8 merge */

#define MAP_ROW(y) (((uchar **)(game_map_35BFB4 + 0x84E))[y])

uchar sfx_28B9E0(long ent)
{
    GameEntity *E = ENT(ent);
    SfxObj *r = SFX_OBJ(E->slot);
    if (E->type == 0x1D && (r->flags & 8)) return particle_2A5914(r->fx.handle);
    return 0;
}

long sfx_28BA34(long type, long ent, fix x, fix y)
{
    uchar cell = MAP_ROW(y >> 16)[(x >> 16) * 3] & 0x3F;
    ulong kind;
    game_44659C = cell;
    if (game_41DA28[cell >> 3] & (1 << (game_44659C & 7)))
        kind = game_41DD58[cell];
    else
        kind = physics_2D637C(x, y, cell);
    if ((1u << (kind & 31)) & terrain_bitmask) return (long)&sfx_hit_soft[(uchar)type];
    return (long)&sfx_hit_hard[(uchar)type];
}

long sfx_28BAF8(long a, long cls, long type)
{
    uchar t = (uchar)a, c = (uchar)cls;
    if (c > 4) {
        sfx_tmp = sfx_hit_obj[t];
        sfx_tmp.snd = 0;
        return (long)&sfx_tmp;
    }
    if (sfx_hit_obj[t].snd == 0xFFFF) {
        sfx_tmp = sfx_hit_obj[t];
        sfx_tmp.snd = sfx_snd_by_cls[c];
        return (long)&sfx_tmp;
    }
    return (long)&sfx_hit_obj[t];
}

/* A piece of debris at pos; with a velocity, 3 in 4 are spinning pieces (type 0x20) with one of
 * the exploding model's images. */
void sfx_28BB80(long ent, fix *pos, fix *vel)
{
    GridObjDesc d;
    short e, s;
    uchar type = sfx_cur_desc->type, vol = sfx_cur_desc->vol;
    SfxObj *r;
    long n;
    d.x = pos[0];
    d.y = pos[1];
    d.z = pos[2];
    if (vel && RndRange(&sfx_rnd, 0, 3) >= 1) {
        type = 0x20;
        d.heading = (ushort)RndRange(&sfx_rnd, 0, 0x8000);
        d.f10 = (ushort)RndRange(&sfx_rnd, 0, 0x8000);
        vol = 0;
        d.f12 = (ushort)RndRange(&sfx_rnd, 0, 0x8000);
    } else {
        d.heading = d.f10 = d.f12 = 0;
    }
    if (!sfx_28D0F4(&e, &s, type, vol, &d)) return;
    r = SFX_OBJ(s);
    if (vel) {
        r->fly.grav = 0x40000;
        r->fly.vx = vel[0];
        r->fly.vy = vel[1];
        r->fly.vz = vel[2];
        r->flags |= 1;
        if (type == 0x20) {
            r->fly.spin_h = (ushort)RndRange(&sfx_rnd, -0x4000, 0x4000);
            r->fly.spin_p = (ushort)RndRange(&sfx_rnd, -0x4000, 0x4000);
            r->fly.spin_b = 0;
        }
    }
    if (type == 0x20) {
        n = sfx_ndebris;
        r->fly.image = n ? sfx_debris_img[RndRange(&sfx_rnd, 0, n - 1)] : -1;
    }
}

void sfx_28BD84(long type, GridObjDesc *d, fix vx, fix vy, fix vz, fix grav)
{
    short e, s;
    SfxObj *r;
    if (!sfx_28D0F4(&e, &s, (uchar)type, 0, d)) return;
    r = SFX_OBJ(s);
    r->flags |= 0x40;
    r->fly.vx = vx;
    r->fly.vz = vz;
    r->fly.grav = grav;
    r->fly.vy = vy;
    r->flags |= 1;
}

/* A debris piece at (dx, dy, dz) from the entity, thrown outwards (faster when nearer). */
void sfx_28BDF0(long ent, fix dx, fix dy, fix dz)
{
    GameEntity *E = ENT(ent);
    fix p[3], v[3], d, k;
    p[0] = E->x + dx;
    p[1] = E->y + dy;
    p[2] = E->z + dz;
    d = fix_safe_pyth_dist(dx, dy, sfx_file_name, 0x2F1);
    d = fix_safe_pyth_dist(d, dz, sfx_file_name, 0x2F1);
    k = fix_mul(0x8000, 0x10000 - fix_div(0x10000, d + 0x10000)) + 0x10000;
    v[0] = (fix)(((long long)dx * k) / d);
    v[1] = (fix)(((long long)dy * k) / d);
    v[2] = (fix)(((long long)(p[2] - sfx_ground + 0x4000) * k) / d);
    sfx_28BB80((short)ent, p, v);
}

long sfx_28BEDC(void)
{
    return (short)(RndRange(game_rnd_text, 0, 0x7FFF) << 3) >> 2;
}

/* ring: {count, done, object type, fix speed}: count objects flung out in a circle. */
void sfx_28BEFC(uchar *ring, GridObjDesc *d, long ent)
{
    long a, step, n;
    fix s, c, speed;
    if (ring[1] != 0) return;
    a = RndRange(game_rnd_text, 0, 0x7FFF) << 3;
    n = ring[0];
    step = 0x10000 / n;
    speed = *(fix *)(ring + 3);
    while (n--) {
        fix_sincos((fixang)a, &s, &c);
        s += (short)(RndRange(game_rnd_text, 0, 0x7FFF) << 3) >> 2;
        c += (short)(RndRange(game_rnd_text, 0, 0x7FFF) << 3) >> 2;
        sfx_28BD84(ring[2], d, fix_mul(s, speed), fix_mul(c, speed), 0, 0);
        a += step;
    }
}

/* Debris pieces on a grid through the box a..b (model space, rotated by the entity's heading
 * and pitch), at most about 32 of them. */
void sfx_28BFF0(long ent, fix *a, fix *b)
{
    GameEntity *E = ENT(ent);
    fix sh, ch, sp, cp, save[3], step, x, y, z, t1, t2;
    fix_sincos(E->heading, &sh, &ch);
    fix_sincos(E->f1d, &sp, &cp);
    memcpy(save, a, 12);
    a[0] += 0x1555;
    a[1] += 0x1555;
    a[2] += 0x1555;
    step = 0x5555;
    while (((b[0] - a[0]) / step + 1) * ((b[1] - a[1]) / step + 1) * ((b[2] - a[2]) / step + 1) >= 0x20)
        step = step * 9 / 8;
    for (x = a[0]; x < b[0]; x += step)
        for (y = a[1]; y < b[1]; y += step)
            for (z = a[2]; z < b[2]; z += step) {
                t1 = fix_mul(y, cp) + fix_mul(z, sp);
                t2 = fix_mul(z, cp) - fix_mul(y, sp);
                sfx_28BDF0((short)ent, fix_mul(x, ch) - fix_mul(t1, sh),
                           fix_mul(x, sh) + fix_mul(t1, ch), t2);
            }
    memcpy(a, save, 12);
}

void sfx_28C228(long ent, fix *p1, fix *p2, fix *p3, fix *p4)
{
    fix A[3], B[3];
    A[0] = p3[0]; A[1] = p3[1]; A[2] = p2[2];
    B[0] = p4[0]; B[1] = p4[1]; B[2] = p4[2];
    sfx_28BFF0((short)ent, A, B);
    A[2] = p1[0]; B[2] = p2[2]; A[0] = p2[0];
    sfx_28BFF0((short)ent, A, B);
    A[0] = p3[0]; B[0] = p1[0];
    sfx_28BFF0((short)ent, A, B);
    A[0] = p1[0]; B[0] = p2[0]; A[1] = p2[1];
    sfx_28BFF0((short)ent, A, B);
    B[1] = p1[1]; A[1] = p3[1];
    sfx_28BFF0((short)ent, A, B);
}

long sfx_28C2F4(GameEntity *e)
{
    return sfx_ndebris = drawobj_29B694(e);
}

/* The effect of destroying ent (classes 0..4): built in sfx_dyn.  Installations (class 4) that
 * are not simulated buildings break into debris here and return 0. */
long sfx_28C300(long ent)
{
    GameEntity *E = ENT(ent);
    uchar cls = E->cls, type;
    long r;
    fix lo[3], lo2[3], hi[3], hi2[3];
    if (cls > 4) return 0;
    r = RndRange(game_rnd_text, 0, 0x7FFF) % sfx_snd_range[cls];
    type = E->type;
    sfx_dyn.type = 0x17;
    sfx_dyn.flags = (sfx_dyn.flags & ~3) | (sfx_smoke_by_cls[cls] & 1) | ((sfx_flick_by_cls[cls] & 1) << 1);
    sfx_dyn.kind = sfx_kind_by_cls[cls];
    sfx_dyn.vol = sfx_vol_by_cls[cls];
    if (RndRange(game_rnd_text, 0, 0x7FFF) % 256 < sfx_ptc_chance[cls])
        sfx_dyn.ptype = sfx_ptype_by_cls[cls];
    else
        sfx_dyn.ptype = 0;
    sfx_dyn.debris = NULL;
    sfx_dyn.snd = sfx_snd_base[cls] + r;
    if (cls != 4) return (long)&sfx_dyn;
    if (type == 0x3D) sfx_dyn.snd = 0;
    if (instsim_288260(type)) {
        sfx_dyn.flags &= ~3;
        sfx_dyn.ptype = 0;
        sfx_dyn.snd = sfx_snd_base[0] + r;
        return (long)&sfx_dyn;
    }
    if (type == 0x3D) return (long)&sfx_dyn;
    sfx_cur_desc = &sfx_dyn;
    sfx_dyn.ptype = 0;
    sfx_dyn.flags &= ~3;
    sfx_dyn.vol = 0x20;
    if (!drawobj_29BE28(E, lo, hi)) return (long)&sfx_dyn;
#ifndef TN_WATCOM_ABI
    port_sfx_kill = 1;                  /* G19: the debris of a destroyed building */
#endif
    sfx_ground = E->z + lo[2];
    if (game_cls4[E->slot][0x78]) {
        game_cls4[E->slot][0x78] = 0;
        sfx_ndebris = drawobj_29B694(E);
        drawobj_29BE28(E, lo2, hi2);
        game_cls4[E->slot][0x78] = 1;
        sfx_28C228((short)ent, lo, hi, lo2, hi2);
    } else {
        sfx_ndebris = drawobj_29B694(E);
        sfx_28BFF0((short)ent, lo, hi);
    }
#ifndef TN_WATCOM_ABI
    port_sfx_kill = 0;
#endif
    if (snd_count_playing(sfx_dyn.snd) < 2) SndPlay(sfx_dyn.snd, 1, 0, 1, E->x, E->y, E->z);
    return 0;
}

/* Scorches a flat enough map cell (bit 0x40), more likely next to scorched cells. */
void sfx_28C5D8(uchar *c)
{
    long h0 = *(short *)(c + 1) >> 4, h1 = *(short *)(c + 4) >> 4;
    long h2 = *(short *)(c + 0x604) >> 4, h3 = *(short *)(c + 0x607) >> 4;
    long lo, hi, d, n, k, m;
    if (h0 < h1) { lo = h0; hi = h1; } else { lo = h1; hi = h0; }
    if (h2 < h3) {
        if (h2 < lo) lo = h2;
        if (h3 > hi) hi = h3;
    } else {
        if (h3 < lo) lo = h3;
        if (h2 > hi) hi = h2;
    }
    if (lo + 0x28 < hi) return;
    d = h2 + h1 - h3 - h0;
    if (d < 0) d = -d;
    if (d > 10) return;
    n = 0;
    for (k = 0; k < 4; k++) {
        n += (c[sfx_scorch_nb1[k] * 3] & 0x40) != 0;
        m = ((c[sfx_scorch_nb2[k] * 3] & 0x40) != 0) * 2;
        if (m) m += ((c[sfx_scorch_nb2[k] * 6] & 0x40) != 0) * 3;
        n += m;
    }
    if (RndRange(game_rnd_text, 0, 0x7FFF) % 100 < sfx_scorch_chance[n]) c[0] |= 0x40;
}

#ifndef TN_WATCOM_ABI
int port_killobj_marked(long obj);      /* killobj.c */

/* "Effects pack" (port/mods/effects.c): what made the death's effect object slot, for its pictures: a
 * class 0 object's model and width, an installation's gun.  A plant with a broken picture (the
 * type's second model, flash_28E6C0) that broke now (broken 1: it stays, as that picture) or
 * whose broken picture went (broken 2: the model given is that picture).  Nothing changes */
static void fx_death(short slot, GameEntity *E)
{
    long ref = 0, ent = (long)(E - game_entities);
    fix w = 0;
    int broken = 0;
    if (E->cls == 0 && drawobj_cls0_models) {
        ref = *(long *)(drawobj_cls0_models + E->type * 8);
        if (G6B_C0_DEAD(E->slot) && *(long *)(drawobj_cls0_models + E->type * 8 + 4)) {
            broken = port_killobj_marked(ent) ? 2 : 1;  /* it goes (KillObjMark) or stays */
            if (broken == 2)
                ref = *(long *)(drawobj_cls0_models + E->type * 8 + 4);
        }
        if (ref && gResDesc2[(ulong)ref >> 16].type == 2)
            w = ff_image_model_width(E);
    }
    port_fx_death(slot, SFX_OBJ(slot)->anim, E->cls, E->type, ref, w, E->x, E->y, E->z,
                  E->cls == 4 && game_cls4[E->slot][0x33], ent, broken);
}
#endif

void sfx_28C714(SfxDesc *D, long ent, fix *pos)
{
    GridObjDesc d;
    short e1 = 0, s1 = 0, e3 = 0, s3 = 0, e4 = 0, s4 = 0, e5 = 0, s5 = 0;
    GameEntity *E;
    SfxFlicker *F;
    SfxObj *r;
    uchar *cell;
    long cx, cy, kind;
    if (!D) return;
#ifndef TN_WATCOM_ABI
    port_sfx_kill = D == &sfx_dyn;      /* G19: something destroyed (sfx_28C300) */
#endif
    E = ENT(ent);
    d.x = pos[0];
    d.y = pos[1];
    d.z = pos[2];
    if (!D->nojitter) {
        d.x += RndRangeFix(&sfx_rnd, -0x800, 0x800);
        d.y += RndRangeFix(&sfx_rnd, -0x800, 0x800);
        d.z += RndRangeFix(&sfx_rnd, -0x800, 0x800);
    }
    d.heading = d.f10 = d.f12 = 0;
    if (D->type && !sfx_28D0F4(&e1, &s1, D->type, D->vol, &d)) e1 = 0;
#ifndef TN_WATCOM_ABI
    if (e1 && D == &sfx_dyn)
        fx_death(s1, E);                /* "Effects pack": the fireball's pictures by what died */
#endif
    if (D->flags & 4) SFX_OBJ(s1)->flags |= 0x40;
    if (D->snd && snd_count_playing(D->snd) < D->snd_max)
        SndPlay(D->snd, 1, 0, 1, d.x, d.y, d.z);
    if (D->scorch) {
        cy = E->y >> 16;
        cx = E->x >> 16;
        cell = MAP_ROW(cy) + cx * 3;
        kind = game_41DD58[cell[0] & 0x3F];
        if (!(terrain_bitmask & (1u << (kind & 31))) && cx > 1 && cy > 1 && cx < 0x1FE && cy < 0x1FE
            && (E->z - map_2949C4(E->x, E->y)) * 24 < ((long)D->scorch << 16))
            sfx_28C5D8(cell);
    }
    if (D->flags & 1) {
        if (!sfx_28D0F4(&e3, &s3, 0x12, 0xFF, &d)) e3 = 0;
        else SFX_OBJ(s3)->flags |= 0x40;
    }
    if (D->flags & 2) {
        F = &sfx_flick[D->kind];
        if (!sfx_28D0F4(&e4, &s4, F->type, 0, &d)) {
            e4 = 0;
        } else {
            r = SFX_OBJ(s4);
            r->fl.desc = F;
            r->flags |= 2;
            r->fl.value = F->value;
            r->fl.timer = (short)sfx_28D6D0(r);
        }
    }
    if (D->ptype) {
        if (D->type == 0x1D) {
            if (e1 && !sfx_28D368(s1, D->ptype, &d, (short)ent)) grid_295730(e1);
        } else if (sfx_28D0F4(&e5, &s5, 0x1D, 0, &d)) {
            if (!sfx_28D368(s5, D->ptype, &d, 0)) grid_295730(e5);
        }
    }
    if (D->debris) sfx_28BEFC(D->debris, &d, (short)ent);
#ifndef TN_WATCOM_ABI
    port_sfx_kill = 0;
#endif
}

void sfx_28CB24(long h, long ent)
{
    if (h) sfx_28C714((SfxDesc *)h, (short)ent, &ENT(ent)->x);
}

void sfx_28CB58(long ent, fix t)
{
    GameEntity *E = ENT(ent);
    sfx_ptc_scale = t;
    if (E->type == 0x1E || E->type == 0x20)
        sfx_28C714(&sfx_burn2_fx, (short)ent, &E->x);
    else
        sfx_28C714(&sfx_burn_fx, (short)ent, &E->x);
}

void sfx_28CBD0(long ent, fix dt, long n)
{
    sfx_ptc_scale = dt;
    sfx_28C714(&sfx_heat_fx, (short)ent, &ENT(ent)->x);
}

void sfx_28CC0C(SfxDesc *D, fix x1, fix y1, fix z1, fix x0, fix y0, fix z0)
{
    fix p[3];
    p[0] = x0;
    p[1] = y0;
    p[2] = z0;
    sfx_dir_y = y1 - y0;
    sfx_dir_x = x1 - x0;
    sfx_dir_flag = 1;
    sfx_dir_z = z1 - z0;
    sfx_28C714(D, 0, p);
}

static void sfx_place(GridObjDesc *d, long e)
{
    GameEntity *E = ENT(e);
    d->ent = (short)e;
    memcpy(d->pos, &E->x, 0x12);
    interact_297400(d, E->cls, E->type);
    grid_29582C(d);
}

void sfx_28CC68(ulong dt)
{
    long t = (long)dt, dt16, dt2, cur, type;
    SfxObj *r;
    GameEntity *E;
    SfxFlicker *F;
    GridObjDesc d;
    terrain_ff tff;
    weather_2B0434();
    particle_2A4978(t);
    dt16 = (t << 16) >> 10;
    dt2 = fix_mul(dt16, dt16);
    for (cur = SFX_ENT(SFX_OBJ(0)); cur; cur = SFX_OBJ(cur)->next) {
        r = SFX_OBJ(cur);
        E = ENT(SFX_ENT(r));
        if (!E->active) continue;
        type = E->type;
        if (type == 0x1D) {
            if (particle_2A41C0(r->fx.handle)) {
                if (r->fx.handle >= 0) particle_2A4114(r->fx.handle);
                r->fx.handle = -1;
                r->flags &= ~8;
                KillObjMark(SFX_ENT(SFX_OBJ(cur)), 1);
                continue;
            }
        } else if (type == 0x1F) {
            if (r->fx.handle) {
                if (r->fx.count) {
                    if (++r->fx.count == 5) {
                        thunder_2B2DBC(r->fx.handle);
                        r->fx.handle = 0;
                        KillObjMark(SFX_ENT(SFX_OBJ(cur)), 1);
                        continue;
                    }
                } else if (thunder_2B33E4(r->fx.handle, 4)) {
                    r->fx.count = 1;
                }
            }
        } else if (type == 0x21) {
            vec3_2825DC((short)cur, dt16);
            sfx_place(&d, SFX_ENT(SFX_OBJ(cur)));
        }
        if (r->flags & 1) {
            E = ENT(SFX_ENT(SFX_OBJ(cur)));
            E->x += fix_mul(r->fly.vx, dt16);
            E->y += fix_mul(r->fly.vy, dt16);
            E->z += fix_mul(r->fly.vz, dt16);
            if (type == 0x20) {
                E->heading += fix_mul(r->fly.spin_h, dt16);
                E->f1d += fix_mul(r->fly.spin_p, dt16);
                E->f1f += fix_mul(r->fly.spin_b, dt16);
            }
            if (r->fly.grav) {
                r->fly.vz -= fix_mul(r->fly.grav, dt16);
                E->z -= fix_mul(r->fly.grav, dt2) / 2;
            }
            sfx_place(&d, SFX_ENT(SFX_OBJ(cur)));
            E = ENT(SFX_ENT(SFX_OBJ(cur)));
            tff.caller = 0;                     /* unset in the original (FF_terrain caches it) */
            FF_terrain(E->x * 6, E->y * 6, E->z * 6, 0, &tff);
            if (tff.g_height / 6 > E->z + 0x2000) {
                type = E->type;
                if (type == 0x1C || type == 0x1D || type == 0x1F || type == 0x20 || type == 0x21)
                    KillObjMark(SFX_ENT(SFX_OBJ(cur)), 1);
                else
                    anim_2E6A34(r->anim);
            }
        } else if ((r->flags & 2) && r->fl.value >= 0) {
            r->fl.value -= t;
            r->fl.timer -= (short)t;
            if (r->fl.timer <= 0) {
                sfx_28D5C8(r);
                F = r->fl.desc;
                r->fl.timer = (short)RndRange(&sfx_rnd, F->tmin, F->tmax);
            }
        }
    }
}

bool sfx_28D0F4(short *ent, short *slot, uchar type, long vol, GridObjDesc *d)
{
    long ref = 0, h, v;
    GameEntity *E;
    SfxObj *r;
    bool special = type == 0x1C || type == 0x1D || type == 0x1F || type == 0x20 || type == 0x21;
    if (!special) {
        ref = ((long *)RefGet(0x4AB0000))[type];
        if (!ref) return 0;
    }
    if (!grid_295588(6, ent, slot)) return 0;
    E = ENT(*ent);
    E->type = type;
    E->active = 1;
    E->f23 = 0;
    d->ent = *ent;
    interact_297400(d, ENT(*ent)->cls, ENT(*ent)->type);
    grid_29582C(d);
    r = SFX_OBJ(*slot);
    if (!special) {
        r->anim = anim_2E68A0(ref, NULL, NULL, NULL);
        anim_2E6B3C(r->anim, (void *)(long)*ent, 8, WATCOM_FN(sfx_28D5B8));
    }
#ifndef TN_WATCOM_ABI
    port_fx_death(*slot, NULL, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0);    /* a new effect: no death of its own yet */
#endif
    r->flags &= 0x80;
    if (vol) {
        h = ((d->z - map_2949C4(d->x, d->y)) * 24) >> 16;
        if (h < 0) h = 0;
        else h = ((d->z - map_2949C4(d->x, d->y)) * 24) >> 16;
        if (h >= 0x200) h = 0x200;
        v = (vol * (0x200 - h)) >> 13;
        if (v) {
            objmap_add(*ent, sfx_vol_type[v]);
            killobj_289554(*ent, WATCOM_FN(objmap_kill_cb), -1);
        }
    }
    return 1;
}

void sfx_28D2DC(long slot, fix vx, fix vy, fix vz, fix grav)
{
    SfxObj *r = SFX_OBJ((short)slot);
    r->fly.vx = vx;
    r->fly.vy = vy;
    r->fly.grav = grav;
    r->fly.vz = vz;
    r->flags |= 1;
}

void sfx_28D318(long slot, SfxFlicker *f)
{
    SfxObj *r = SFX_OBJ((short)slot);
    r->fl.desc = f;
    r->flags |= 2;
    r->fl.value = f->value;
    r->fl.timer = (short)RndRange(&sfx_rnd, r->fl.desc->tmin, r->fl.desc->tmax);
}

/* Particles of descriptor ptype at the object; spread along the entity's motion (sfx_ptc_scale)
 * or along sfx_dir_* when set. */
long sfx_28D368(long slot, long ptype, GridObjDesc *d, long ent)
{
    long h = particle_2A6134(ptype, d->x, d->y, d->z);
    SfxObj *r = SFX_OBJ((short)slot);
    fix x, y, z;
    r->fx.handle = h;
    r->flags |= 8;
    if (h < 0) return 0;
    if ((short)ent && sfx_ptc_scale) {
        physics_2D3E74((short)ent, &x, &y, &z);
        particle_2A6000(h, sfx_ptc_scale, -x, -y, -z);
        sfx_ptc_scale = 0;
    } else if (sfx_dir_flag) {
        particle_2A6000(h, 0x10000, sfx_dir_x, sfx_dir_y, sfx_dir_z);
        sfx_dir_flag = 0;
    }
    return 1;
}

void sfx_28D448(fix *from, fix *to)
{
    GridObjDesc d;
    short e, s;
    long b;
    d.x = (from[0] + to[0]) >> 1;
    d.y = (from[1] + to[1]) >> 1;
    d.z = (from[2] + to[2]) >> 1;
    d.heading = d.f10 = d.f12 = 0;              /* unset in the original */
    if (!sfx_28D0F4(&e, &s, 0x1F, 0x40, &d)) return;
    thunder_2B32BC(0x4000, 0x4000);
    b = thunder_2B3404(from, to);
    SFX_OBJ(s)->fx.handle = b;
    SFX_OBJ(s)->fx.count = 0;
}

void sfx_28D4E8(void)
{
    rndsky_2A6B80(0xFF, 0x2000, 0x4000, 0x20040080);
    rndsky_2A6B80(0xFE, 0x3000, 0x4000, 0x40080100);
    rndsky_2A6B80(0xFD, 0x7000, 0x4000, 0x20040080);
    rndsky_2A6B80(0xFC, 0x5000, 0x4000, (long)0x80100200);
    rndsky_2A6B80(0xFB, 0x4800, 0x4000, (long)0xA0140280);
    rndsky_2A6B80(0xFA, 0x6000, 0x4000, (long)0xC0180300);
    rndsky_2A6B80(0xF9, 0x4800, 0x4000, (long)0xE01C0380);
    rndsky_2A6B80(0xF8, 0x3000, 0x4000, (long)0xF01E03C0);
}

/* Animation notify callback (mask 8, the end): the entity is the notify data. */
void sfx_28D5B8(void *anim)
{
    KillObjMark(*(short *)((uchar *)anim + 0x18), 1);
}

void sfx_28D5C8(SfxObj *r)
{
    GridObjDesc d;
    short e, s;
    GameEntity *E = ENT(SFX_ENT(r));
    SfxObj *q;
    fix vx, vy;
    memcpy(d.pos, &E->x, 0x12);
    d.z = E->z + r->fl.desc->zoff;
    if (!sfx_28D0F4(&e, &s, r->fl.desc->sub, 0, &d)) return;
    vx = fix_mul(RndRangeFix(&sfx_rnd, -0x10000, 0x10000), r->fl.desc->spread);
    vy = fix_mul(RndRangeFix(&sfx_rnd, -0x10000, 0x10000), r->fl.desc->spread);
    q = SFX_OBJ(s);
    q->fly.vz = 0x10000;
    q->fly.grav = 0;
    q->fly.vx = vx;
    q->fly.vy = vy;
    q->flags |= 1;
}

long sfx_28D6D0(SfxObj *r)
{
    return RndRange(&sfx_rnd, r->fl.desc->tmin, r->fl.desc->tmax);
}

/* A shot from (x, y, z) towards (nx, ny, nz): four segments covering 12/13 of the way, each a
 * tracer object (type 0x1C) chained from the shooter's class 5 record, or for some ordnance
 * kinds (o[0x1E]) an effect along the segment instead (kind 7: both). */
void sfx_28D730(long ent, OrdType *o, fix x, fix y, fix z, fix nx, fix ny, fix nz)
{
    short *chain = (short *)(game_cls5[ENT(ent)->slot] + 8);
    uchar color = ((uchar *)o)[3], kind;
    fix dx = nx - x, dy = ny - y, dz = nz - z, sx, sy, sz, ex, ey, ez, pos[3];
    long i;
    GridObjDesc d;
    short e, s;
    GameEntity *E;
    SfxObj *r;
    SfxDesc *fx;
    ptc_color7 = color + 8;
    for (i = 0; i < 4; i++) {
        sx = x + 3 * dx * i / 13;
        ex = x + 3 * dx * (i + 1) / 13;
        sy = y + 3 * dy * i / 13;
        ey = y + 3 * dy * (i + 1) / 13;
        sz = z + 3 * dz * i / 13;
        ez = z + 3 * dz * (i + 1) / 13;
        kind = ((uchar *)o)[0x1E];
        switch (kind) {
        case 2: case 7: fx = &sfx_shot_fx_a; break;
        case 8: fx = &sfx_shot_fx_b; break;
        case 5: fx = &sfx_shot_fx_c; break;
        default: fx = NULL; break;
        }
        if (fx) {
            pos[0] = ex;
            pos[1] = ey;
            pos[2] = ez;
            sfx_dir_x = sx - ex;
            sfx_dir_y = sy - ey;
            sfx_dir_z = sz - ez;
            sfx_dir_flag = 1;
            sfx_28C714(fx, 0, pos);
            if (kind != 7) continue;
        }
        if (!grid_295588(6, &e, &s)) return;
        E = ENT(e);
        E->active = 1;
        E->f23 = 0;
        E->type = 0x1C;
        d.x = sx;
        d.y = sy;
        d.z = sz;
        d.heading = d.f10 = d.f12 = 0;          /* unset in the original */
        d.ent = e;
        interact_297400(&d, E->cls, E->type);
        grid_29582C(&d);
        r = SFX_OBJ(s);
        r->flags = (r->flags | 4) & 0xC4;
        *chain = s;
        r->seg.next = 0;
        r->seg.x = ex;
        r->seg.y = ey;
        r->seg.z = ez;
        chain = &r->seg.next;
        r->seg.color = color;
    }
}

void sfx_28DC50(fix *pos, uchar random)
{
    SfxDesc t = sfx_28B9D0;
    t.snd = random ? 0x7A4 + RndRange(&sfx_rnd, 0, 3) : 0;
    sfx_28C714(&t, 0, pos);
}

/* 3D model callback (the siren of an installation): smoke at the model point v. */
void sfx_28DC9C(g3s_vector *v)
{
    g3s_vector o;
    GridObjDesc d;
    short e, s;
    GameEntity *E;
    if (!(inst_siren_flags & 4)) return;
    g3_world_to_object(&o, v);
    d.x = o.z;
    d.y = o.x;
    d.z = -o.y;
    d.heading = d.f10 = d.f12 = 0;
    sfx_28D0F4(&e, &s, 0x1E, 0xFF, &d);
    E = ENT(inst_siren_ent);
    if (snd_count_playing(0x832) < 2) SndPlay(0x832, 1, 0, 1, E->x, E->y, E->z);
    inst_siren_flags &= ~4;
}

void sfx_28DD7C(void)
{
    g3d_interp_callbacks[0] = WATCOM_FN(sfx_28DC9C);
}

void sfx_28DD90(long ent, fix *pos, long ms)
{
    sfx_28C714(&sfx_drop_fx, (short)ent, pos);
}

void sfx_28DDA4(long snd, long max, fix *pos)
{
    if (snd_count_playing((ushort)snd) < max) SndPlay((ushort)snd, 1, 0, 1, pos[0], pos[1], pos[2]);
}

void sfx_28DDD8(long ent, long kind)
{
    sfx_28C714(&sfx_kind_fx[(schar)kind], (short)ent, &ENT(ent)->x);
}
