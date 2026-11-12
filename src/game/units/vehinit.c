/* vehinit.c -- Terra Nova: vehicle record creation and unit system levels (module M045; source
 * name unknown).  Decompiled from __FF.EXE v1.09.
 *
 *   0x27DC90 damage_27DC90  create a vehicle entity (grid_295588 class 2) and its record
 *   0x27DD64 damage_27DD64  initialise a vehicle record (+6) from its type and parameters
 *   0x27DE84 damage_27DE84  start the vehicle's ambient engine sound (by vehicle type)
 *   0x27DF14 damage_27DF14  level of system `sys` of a unit in percent (100 if it has no maximum)
 *   0x27E064 damage_27E064  update the player's stored level of one system
 *   0x27E0B4 damage_27E0B4  update the player's stored levels of all 8 systems
 *
 * A unit's systems are 5-byte records {uchar, ushort max, ushort cur}: PBA through the pointer
 * at +0x9C (8 for the player, 4 otherwise), vehicle +0xD (2), aircraft +0x1B (3), class 4
 * +0x51 (1).
 * Quirks kept:
 *  - damage_27DD64 sets the weapon count to 1 just before the loop that initialises the weapon
 *    slots, so exactly one slot is initialised;
 *  - damage_27E064 runs a loop over 4 thresholds (objdmg_res_3A4) that has no effect (its body
 *    was compiled out);
 *  - damage_27DF14 returns the quotient in eax (callers use al).
 */
#include "g3_c.h"
#include "g3_a.h"      /* G3a callees (owner g3a) */

uchar damage_27DC90(short *id, short *slot, long f21, long type, uchar *params)
{
    if (!grid_295588(2, id, slot))
        return 0;
    if (!damage_27DD64((uchar *)&game_vehicles[*slot] + 6, (uchar)type, params))
        return 0;
    G3C_ENT(*id).type = (uchar)type;
    G3C_ENT(*id).active = 1;
    G3C_ENT(*id).f23 = params[0];
    G3C_ENT(*id).f24 = params[6];
    damage_27DE84(*id);
    G3C_ENT(*id).f21 = (uchar)f21;
    return 1;
}

uchar damage_27DD64(uchar *rec, long type, uchar *params)
{
    uchar t = (uchar)type;
    ushort i;

    damage_27CCA0(2, t, rec, 1, params[1]);
    DamageInitSystems(2, t, rec + 7, 2);
    objacc_28B914((ObjMeter *)(rec + 0x31), (fix)*(ushort *)&game_veh_types[t][0] << 16,
                  (fix)params[2] << 16, (fix)*(ushort *)&game_veh_types[t][2] << 16,
                  (fix)params[3] << 16, 0);
    rec[0x2F] = 1;
    rec[0x30] = 0;
    for (i = 0; i < (schar)rec[0x2F]; i++)
        damage_27E0E0((WeapSlot *)(rec + 0x11 + i * 0xF), params[4 + i], 0);
    damage_27D140((NearList *)(rec + 0x47), *(fix *)&game_veh_types[t][4],
                  *(fix *)&game_veh_types[t][8], 0x20, (NearEntry *)(rec + 0x57));
    *(long *)(rec + 0x119) = 0;
    rec[0x11D] = 0;
    return 1;
}

uchar damage_27DE84(long ent)
{
    short snd;

    switch (G3C_ENT(ent).type) {
    case 0: case 1: case 2: snd = 0x851; break;
    case 3: snd = 0x850; break;
    case 4: snd = 0x84C; break;
    case 5: snd = 0x84D; break;
    case 7: snd = 0x84F; break;
    default: snd = 0x84E; break;
    }
    if (snd != 0 && noisy_add((short)ent, (ushort)snd) >= 0)
        return 1;
    return 0;
}

uchar damage_27DF14(long ent, long sys)
{
    GameEntity *e = &G3C_ENT(ent);
    uchar s = (uchar)sys;
    short slot = e->slot;
    uchar *p;
    ushort max;

    switch (e->cls) {
    case 1:
        if ((short)ent == game_player_entity) {
            if (s >= 8)
                return 0;
        } else if (s >= 4)
            return 0;
        p = *(uchar **)((uchar *)&game_pba[slot] + 0x9C);
        break;
    case 2:
        if (s >= 2)
            return 0;
        p = (uchar *)&game_vehicles[slot] + 0xD;
        break;
    case 3:
        if (s >= 3)
            return 0;
        p = game_cls3[slot] + 0x1B;
        break;
    case 4:
        if (s >= 1)
            return 0;
        p = game_cls4[slot] + 0x51;
        break;
    default:
        return 0;
    }
    if (p == NULL)
        return 0;
    p += s * 5;
    max = *(ushort *)(p + 1);
    if (max == 0)
        return 100;
    return (uchar)((long)(*(ushort *)(p + 3) * 100) / (long)max);
}

void damage_27E064(long sys)
{
    uchar s = (uchar)sys;
    schar lvl = (schar)damage_27DF14(game_player_entity, s);
    schar old = player_sys_pct[s];
    long i;

    for (i = 0; i < 4; i++) {      /* no effect: the body was compiled out */
        schar th = (schar)objdmg_res_3A4[s * 4 + i];
        if (lvl < th && old >= th)
            break;
    }
    player_sys_pct[s] = lvl;
}

void damage_27E0B4(void)
{
    long i;

    for (i = 0; i < 8; i++)
        player_sys_pct[i] = (schar)damage_27DF14(game_player_entity, (uchar)i);
}
