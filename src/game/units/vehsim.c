/* vehsim.c -- Terra Nova vehicle simulation glue (module M062; the source name is in the binary:
 * r:\prj\tn\v109\tn\src\tn\vehsim.c).  Decompiled from __FF.EXE v1.09.
 *
 *   0x28E850 vehsim_28E850  per-frame update of every live vehicle
 *   0x28E8E4 vehsim_28E8E4  update one vehicle: weapons, armour meter, neighbours, energy
 *   0x28E9C0 vehsim_28E9C0  AI damage callback: apply an AI-reported hit to a vehicle
 *   0x28EAA0 vehsim_28EAA0  vehicle destroyed (mission 0x24 type 7 leaves two wrecks)
 *   0x28EC5C vehsim_28EC5C  can the vehicle's current weapon fire now
 *   0x28ECC4 vehsim_28ECC4  view block pointer and heading / pitch of a direction
 *   0x28ED68 vehsim_28ED68  the vehicle's +0x124 / +0x126 words as two longs (static)
 *   0x28EDC0 vehsim_28EDC0  fire the current weapon of a vehicle at a point or an entity
 *   0x28EF14 vehsim_28EF14  aim error of the current weapon at a point or an entity
 *
 * Vehicle record (GameVehicle, 0x128 bytes, game.h): +0 entity (low 15 bits; bit 15 = inactive),
 * +2 next vehicle, +4 / +8 energy (high words: current +6, max +0xA), +6 damage part record,
 * +0xD two subsystems, +0x17 two weapons of 15 bytes, +0x36 current weapon (signed byte),
 * +0x37 armour meter (ObjMeter), +0x4D neighbour list (NearList), +0x124 / +0x126 two words.
 * vehsim_28EDC0 / 28EF14 take the vehicle index, the others an entity.
 */
#include "g3_a.h"
#include "g4_a.h"      /* AI callees (owners g4a/g4b/g4c1/g4c2; G4 merge) */
#include "g6_c.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
long vehsim_397A40[2];
#endif

#define VEH(i)       ((uchar *)&game_vehicles[(short)(i)])
#define VEH_ENT(v)   G3A_IDX15(*(short *)(v))
#define VEH_WEAPON(v) ((v) + 0x17 + (signed char)(v)[0x36] * 0xF)

void vehsim_28E850(ulong units)
{
    short i = G3A_IDX15(*(short *)VEH(0));
    ushort off = *(ushort *)VEH(i) >> 15;

    while (i) {
        if (!off && G3A_ENT(VEH_ENT(VEH(i))).active)
            vehsim_28E8E4(i, units);
        i = *(short *)(VEH(i) + 2);
        off = *(ushort *)VEH(i) >> 15;
    }
}

void vehsim_28E8E4(short vi, long units)
{
    uchar *v = VEH(vi);
    uchar *p;
    short ent = VEH_ENT(v);
    long cur, max;

    for (p = v + 0x17; p < v + 0x35; p += 0xF)
        weapon_nop();
    objacc_28B88C((ObjMeter *)(v + 0x37), units);
    damage_27D1E4(VEH_ENT(v), (NearList *)(v + 0x4D));
    if (game_35CD9C && (game_442F1B & 0x20))
        return;
    cur = *(short *)(v + 6) +
          (long)((ulong)(game_veh_types[G3A_ENT(ent).type][0xD] * units) / 1000);
    max = *(short *)(v + 0xA);
    *(short *)(v + 6) = (short)(cur < max ? cur : max);
}

void vehsim_28E9C0(void *ai)
{
    long target, amount, mode, dtype, ent;
    uchar *v;
    short e;

    aiacc_25AE98(ai, &ent, &target, &dtype, &mode, &amount);
    if (target == 0xFFF)
        return;
    e = (short)ent;
    v = VEH(G3A_ENT(e).slot);
    obj_damage(e, (short)(target & 0xFFF), (uchar)dtype, v + 6, 1, v + 0xD, 2, v + 0x37, v + 0x17, 2,
               (ushort)mode, (ushort)amount);
    if (G3A_ENT(e).f21)
        aimain_24A26C(G3A_ENT(e).f21, (short)(target & 0xFFF));
}

void vehsim_28EAA0(long ent_arg, long mode)
{
    short ent = (short)ent_arg;
    GridObjDesc d1, d2;
    uchar data1[3], data2[3];
    short ent1, slot1, ent2, slot2;
    GameEntity *e;

    e = &G3A_ENT(ent);
    if (game_359708 == 0x24 && e->type == 7) {
        data1[0] = data1[1] = data1[2] = 0;
        damage_27D610(&ent1, &slot1, 0x3A, data1);
        d1.ent = ent1;
        memcpy(d1.pos, &e->x, 0x12);
        interact_297400(&d1, G3A_ENT(ent1).cls, G3A_ENT(ent1).type);
        grid_29582C(&d1);
        data2[0] = data2[1] = data2[2] = 0;
        damage_27D610(&ent2, &slot2, 0x3B, data2);
        d2.ent = ent2;
        memcpy(d2.pos, &e->x, 0x12);
        *(fix *)&d2.pos[0] -= 0x288F5;
        *(fix *)&d2.pos[4] -= 0x57CED;
        interact_297400(&d2, G3A_ENT(ent2).cls, G3A_ENT(ent2).type);
        grid_29582C(&d2);
    }
    KillObjMark(ent, 1);
    sfx_28CB24(sfx_28C300(ent), ent);
}

bool vehsim_28EC5C(short ent)
{
    uchar *v = VEH(G3A_ENT(ent).slot);
    uchar *w = VEH_WEAPON(v);

    if (!w[0])
        return FALSE;
    if (weapon_ready(w, v + 0x37))
        return FALSE;
    return TRUE;
}

void vehsim_28ECC4(void **view, short *heading, short *pitch, short ent, fix *dir)
{
    GameEntity *e = &G3A_ENT(ent);

    *view = VEH(e->slot) + 0x124;
    if (dir) {
        *heading = (short)fix_atan2(dir[1], dir[0]);
        *pitch = (short)(fix_atan2(dir[2], fix_safe_pyth_dist(dir[0], dir[1], "vehsim.c", 0x160)) - e->f1d);
        *heading = (short)(e->heading - *heading);
    }
}

long *vehsim_28ED68(short ent)
{
    uchar *v = VEH(G3A_ENT(ent).slot);

    vehsim_397A40[0] = *(ushort *)(v + 0x124);
    vehsim_397A40[1] = *(ushort *)(v + 0x126);
    return vehsim_397A40;
}

/* returns 0 when the weapon fired, 6 when it could not, or weapon_ready's answer */
uchar vehsim_28EDC0(short vi, fix x, fix y, fix z, short target, uchar real)
{
    uchar *v = VEH(vi);
    uchar *w = VEH_WEAPON(v);
    short vent = VEH_ENT(v);
    fix ox, oy, oz;
    uchar r;

    if (!w[0])
        return 6;
    if (real && (r = weapon_ready(w, v + 0x37)) != 0)
        return r;
    if (target) {
        x = G3A_ENT(target).x;
        y = G3A_ENT(target).y;
        z = G3A_ENT(target).z;
    }
    if (!interact_29837C(vent, x, y, z, &ox, &oy, &oz))
        return 6;
    if (weapon_fire(w, VEH_ENT(v), ox, oy, oz, x, y, z, target, v + 0x37, real))
        return 0;
    return 6;
}

/* returns weapon_aim_error's result; the original leaves a scratch value in eax when the
 * vehicle has no weapon or no line from its muzzle (0 here) */
fix vehsim_28EF14(short vi, fix x, fix y, fix z, short target, fix *miss)
{
    uchar *v = VEH(vi);
    uchar *w = VEH_WEAPON(v);
    short vent = VEH_ENT(v);
    fix ox, oy, oz;

    if (!w[0])
        return 0;
    if (target) {
        x = G3A_ENT(target).x;
        y = G3A_ENT(target).y;
        z = G3A_ENT(target).z;
    }
    if (!interact_29837C(vent, x, y, z, &ox, &oy, &oz))
        return 0;
    return weapon_aim_error(w, VEH_ENT(v), ox, oy, oz, x, y, z, target, miss);
}
