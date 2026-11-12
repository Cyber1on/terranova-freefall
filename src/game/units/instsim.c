/* instsim.c -- Terra Nova installation simulator (module M054, r:\prj\tn\v109\tn\src\tn\instsim.c).
 * Decompiled from __FF.EXE v1.09.  Installations are the class-4 objects (turrets, sirens,
 * generators ...; game_cls4 records, layout in g3_c.h).
 *
 *   0x288260 instsim_288260  TRUE for installation types 0x2E, 0x2F, 0x45..0x47 (turrets)
 *   0x288278 instsim_288278  per-frame update of every installation (sim hook)
 *   0x2883BC instsim_2883BC  timed update of one installation (messages, meter, weapons)
 *   0x28846C instsim_28846C  damage message handler (AI callback)
 *   0x288554 instsim_288554  destroy an installation (smoke / debris objects, shape, sounds)
 *   0x2888A4 instsim_2888A4  TRUE if the current weapon can fire
 *   0x288904 instsim_288904  fire the current weapon at a point or an entity (0 fired, 6 not)
 *   0x288A4C instsim_288A4C  aim angles of an installation (static result buffer)
 *   0x288A98 instsim_288A98  v * game_frame_dt / 256
 *   0x288AAC instsim_288AAC  siren / alarm sweep (type 0x31 installations)
 *   0x288C64 instsim_288C64  aim block pointer and relative angles of a direction vector
 *   0x288D10 instsim_288D10  aim error of the current weapon
 *   0x288E04 instsim_288E04  lower the aim rate by 0x1000 (0x8000 when below) (unused)
 *   0x288E40 instsim_288E40  alert the installation's squad (AI)
 *
 * Quirks kept:
 *  - instsim_288554 does not check damage_27CF30's result: on failure the new shape's object id
 *    is an uninitialised local (0 here);
 *  - instsim_288904 / instsim_288D10 return what is left in eax on some paths: 6 (al) with the
 *    weapon offset in the upper bits, the weapon_ready result, or (288D10, no weapon) the weapon
 *    offset idx * 15; the C returns the same values;
 *  - instsim_288E04 returns the aim block pointer (eax).
 */
#include "g3_c.h"
#include "g2_a.h"      /* grid_29582C (owner g2a) */
#include "g4_a.h"      /* AI callees (owners g4a/g4b/g4c1/g4c2; G4 merge) */
#include "g6_c.h"     /* added at the G5-G8 merge */
#include "g7_a.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
uchar *game_41E218;
uchar game_35ECF2;
fix inst_aim_out[2];
short inst_siren_ent;
long inst_siren_level;
uchar inst_siren_flags;
uchar inst_siren_count;
ulong inst_siren_time;
long inst_siren_35B74C;
char instsim_file[] = "r:\\prj\\tn\\v109\\tn\\src\\tn\\instsim.c";
#endif

#define IDX15(w) ((short)((short)((ushort)(w) << 1) >> 1))
#define W0(r) (*(ushort *)(r))

uchar instsim_288260(long type)
{
    uchar t = (uchar)type;
    return (t >= 0x2E && t <= 0x2F) || (t >= 0x45 && t <= 0x47);
}

void instsim_288278(ulong dt)
{
    short i = IDX15(W0(CLS4(0)));
    ushort off = W0(CLS4(i)) >> 15;

    while (i != 0) {
        uchar *r = CLS4(i);
        if (!off && G3C_ENT(IDX15(W0(r))).active) {
            GameEntity *e = &G3C_ENT(IDX15(W0(CLS4(i))));
            uchar type = e->type;
            ushort t;

            if (type == 0x2E)
                ((uchar *)e)[0x1C] += 4;
            if (type == 0x31) {
                sfx_28DD7C();
                instsim_288AAC(IDX15(W0(CLS4(i))));
            }
            t = *(ushort *)(r + 0x76) + (ushort)dt;
            *(ushort *)(r + 0x76) = t;
            if (t >= 0x400) {
                instsim_2883BC(i, *(ushort *)(r + 0x76));
                *(ushort *)(r + 0x76) = 0;
                if (type == 0x2E)
                    turret_fire(i);
                else if (type == 0x2F)
                    turret_alert(i);
                else if (type == 0x49)
                    ((uchar *)&G3C_ENT(IDX15(W0(CLS4(i)))))[0x1C] += 4;
            }
        }
        i = *(short *)(CLS4(i) + 2);
        off = W0(CLS4(i)) >> 15;
    }
}

void instsim_2883BC(long idx, long dt)
{
    GameEntity *e = &G3C_ENT(IDX15(W0(CLS4(idx))));
    uchar *r;
    long i;

    if (e->f21) {
        MsgAddr key;
        Msg *m;
        key.a = 0x80;
        key.b = e->f21;
        while ((m = MsgReceive(&key)) != NULL)
            MsgFree(m);
    }
    r = CLS4(idx);
    objacc_28B88C((ObjMeter *)(r + 0x56), dt);
    for (i = 0; i < (schar)r[0xD]; i++)
        weapon_nop();
}

void instsim_28846C(void *ai)
{
    long target, e5, d, c, ent;
    uchar *r;

    aiacc_25AE98(ai, &ent, &target, &c, &d, &e5);
    if (target == 0xFFF)
        return;
    r = CLS4(G3C_ENT((short)ent).slot);
    obj_damage((short)ent, (short)(target & 0xFFFF0FFF), (uchar)c, r + 0x10, (schar)r[0xC],
               r + 0x51, 1, r + 0x56, r + 0x33, (schar)r[0xD], (ushort)d, (ushort)e5);
    if (G3C_ENT((short)ent).f21)
        aimain_24A26C(G3C_ENT((short)ent).f21, (short)(target & 0xFFFF0FFF));
}

void instsim_288554(long ent, long unused)   /* edx: a second argument (not saved, never read) */
{
    short me = (short)ent;
    GameEntity *e = &G3C_ENT(me);
    uchar *r = CLS4(e->slot);
    short nid, nslot;
    uchar ok;

    if (r[0x78] == 0 && *(long *)(game_41E218 + e->type * 8 + 4) != 0) {
        r[0x78] = 1;
        *(short *)(r + 0x54) = (short)((long)*(ushort *)(r + 0x52) / 2);
        ok = 1;
    } else
        ok = 0;
    if (!ok)
        KillObjMark(me, 1);

    if (G3C_ENT(me).type == 0x2F && timers_279E80(&nid, &nslot, 4)) {
        G3C_ENT(nid).x = G3C_ENT(me).x;
        G3C_ENT(nid).y = G3C_ENT(me).y;
        G3C_ENT(nid).z = G3C_ENT(me).z;
        G3C_ENT(nid).type = 4;
        *(short *)(game_cls8[nslot] + 6) = *(short *)(CLS4(G3C_ENT(me).slot) + 0xA);
        *(short *)(game_cls8[nslot] + 8) = me;
        game_cls8[nslot][0xB] = 1;
        game_cls8[nslot][0xA] = 1;
        sfx_28DDD8(nid, 1);
    }
    if (G3C_ENT(me).type == 0x33 && timers_279E80(&nid, &nslot, 0x10)) {
        G3C_ENT(nid).x = G3C_ENT(me).x;
        G3C_ENT(nid).y = G3C_ENT(me).y;
        G3C_ENT(nid).z = G3C_ENT(me).z;
        G3C_ENT(nid).type = 0x10;
        *(short *)(game_cls8[nslot] + 6) = *(short *)(CLS4(G3C_ENT(me).slot) + 0xA);
        *(short *)(game_cls8[nslot] + 8) = me;
        game_cls8[nslot][0xB] = 1;
        game_cls8[nslot][0xA] = 1;
        sfx_28DDD8(nid, 3);
    }
    if (G3C_ENT(me).type == 0x35 || G3C_ENT(me).type == 0x34) {
        uchar parm[8];
        long shape = 0, b;
        ObjFoot foot;

        parm[0] = 0;
        parm[4] = parm[5] = parm[6] = 0;
        parm[1] = parm[2] = parm[3] = 0x64;
        damage_27CF30(&shape, &b, 0, (uchar)((G3C_ENT(me).type == 0x34) + 0x36), parm, 0);
        foot.obj = (short)shape;
        memcpy(&foot.x, &G3C_ENT(me).x, 18);
        interact_297400(&foot, G3C_ENT((short)shape).cls, G3C_ENT((short)shape).type);
        grid_29582C(&foot);
    }
    if (G3C_ENT(me).type == 0x3F)
        game_35ECF2 |= 1;
    sfx_28CB24(sfx_28C300(me), me);
}

uchar instsim_2888A4(long obj)
{
    uchar *r = CLS4(G3C_ENT(obj).slot);
    uchar *w = r + 0x33 + (schar)r[0xE] * 15;

    if (*w == 0)
        return 0;
    if (weapon_ready(w, r + 0x56))
        return 0;
    return 1;
}

long instsim_288904(long slot, long x, long y, long z, long target, long real)
{
    uchar *r = CLS4(slot);
    long wo = (schar)r[0xE] * 15;
    uchar *w = r + 0x33 + wo;
    short me = IDX15(W0(r));
    fix ox, oy, oz;
    uchar ok;

    if (*w == 0)
        return (wo & ~0xFF) | 6;
    if ((uchar)real) {
        ok = weapon_ready(w, r + 0x56);
        if (ok)
            return ok;
    }
    if ((short)target != 0) {
        x = G3C_ENT(target).x;
        y = G3C_ENT(target).y;
        z = G3C_ENT(target).z;
    }
    if (!interact_29837C(me, x, y, z, &ox, &oy, &oz))
        return 6;
    if (weapon_fire(w, IDX15(W0(CLS4(slot))), ox, oy, oz, x, y, z, (short)target, r + 0x56,
                    (uchar)real))
        return 0;
    return 6;
}

long instsim_288A4C(long obj)
{
    uchar *a = CLS4(G3C_ENT(obj).slot) + 0x70;

    inst_aim_out[0] = *(ushort *)a;
    inst_aim_out[1] = *(ushort *)(a + 2);
    return (long)inst_aim_out;
}

long instsim_288A98(long v)
{
    return (long)((long long)v * (long)game_frame_dt / 0x100);
}

void instsim_288AAC(long ent)
{
    ushort *a = (ushort *)(CLS4(G3C_ENT(ent).slot) + 0x70);
    ushort h;

    inst_siren_ent = (short)ent;
    if (!(inst_siren_flags & 2) || (inst_siren_flags & 8))
        return;
    if (inst_siren_time > game_time)
        return;
    inst_siren_time = game_time + 0x4B;
    h = a[0];
    if (h < 0x7F49) {
        a[0] = h + 0xB6;
        return;
    }
    if (inst_siren_count >= 3) {
        inst_siren_flags |= 8;
        return;
    }
    if (inst_siren_flags & 1) {
        long lvl = inst_siren_level + (long)((long long)0x15000 * (long)game_frame_dt / 0x100);
        fix d;

        inst_siren_flags &= 0xFA;
        inst_siren_flags |= 4;
        inst_siren_level = lvl;
        inst_siren_35B74C = 1;
        d = fix_fast_pyth_dist(G3C_ENT(game_player_entity).x - G3C_ENT(inst_siren_ent).x,
                               G3C_ENT(game_player_entity).y - G3C_ENT(inst_siren_ent).y);
        hud_268FD0(d >> 16, 0x32);
        a[1] = (ushort)inst_siren_level;
        return;
    }
    inst_siren_level -= (long)((long long)0x1000 * (long)game_frame_dt / 0x100);
    if (inst_siren_level <= 0x1000) {
        inst_siren_flags |= 1;
        inst_siren_count++;
    }
    a[1] = (ushort)inst_siren_level;
}

void instsim_288C64(void *a, void *b, void *c, long d, void *e)
{
    GameEntity *ent = &G3C_ENT(d);
    ushort *hdg = b, *pitch = c;
    fix *v = e;

    *(uchar **)a = CLS4(ent->slot) + 0x70;
    if (v) {
        *hdg = fix_atan2(v[1], v[0]);
        *pitch = (ushort)fix_atan2(v[2], fix_safe_pyth_dist(v[0], v[1], instsim_file, 0x274))
                 - ent->f1d;
        *hdg = *hdg - ent->heading;
        *pitch = -*pitch;
    }
}

long instsim_288D10(long slot, long x, long y, long z, long target, fix *miss)
{
    uchar *r = CLS4(slot);
    long wo = (schar)r[0xE] * 15;
    uchar *w = r + 0x33 + wo;
    short me = IDX15(W0(r));
    fix ox, oy, oz;
    uchar ok;

    if (*w == 0)
        return wo;
    if ((short)target != 0) {
        x = G3C_ENT(target).x;
        y = G3C_ENT(target).y;
        z = G3C_ENT(target).z;
    }
    ok = interact_29837C(me, x, y, z, &ox, &oy, &oz);
    if (!ok)
        return ok;
    return weapon_aim_error(w, IDX15(W0(CLS4(slot))), ox, oy, oz, x, y, z, (short)target, miss);
}

uchar *instsim_288E04(long slot)
{
    uchar *a = CLS4(slot) + 0x70;

    if (*(ushort *)(a + 4) < 0x1000)
        *(ushort *)(a + 4) = 0x8000;
    else
        *(ushort *)(a + 4) -= 0x1000;
    return a;
}

void instsim_288E40(long ent)
{
    uchar b = G3C_ENT(ent).f21;
    uchar *brain = (uchar *)&game_brains[b];
    uchar sq = aiperc_253AE8(0, *(ulong *)(brain + 0xE), 1);

    game_squads[sq].f0cb = 1;
    ((uchar *)&game_squads[sq])[0xC9] = 1;
    ailow_253008(b, 0);
    brain[5] = 1;
    brain[0xC3] = 0;
    ailow_2531B0(b, sq);
}
