/* aircinit.c -- Terra Nova aircraft creation (module M038; source name unknown).
 * Decompiled from __FF.EXE v1.09.
 *
 *   0x27C120 damage_27C120  create an aircraft entity (class 3) and its pool record
 *   0x27C20C damage_27C20C  initialise an aircraft pool record from its type
 *   0x27C360 damage_27C360  start the aircraft's ambient engine sound
 *
 * The aircraft type table is game_cls3_types (game.h, 0x3964B0, 0x37 bytes per type).
 */
#include "g3_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
short game_35B60C;
#endif

/* data: {+0 byte -> entity +0x23, +1 -> damage_27CCA0 multiplier, +2 / +3 meter values,
 * +4 / +5 two damage_27E0E0 values, +6 -> entity +0x24} */
bool damage_27C120(short *ent, short *slot, uchar f21, uchar type, uchar *data, long v)
{
    GameEntity *e;

    if (!grid_295588(3, ent, slot))
        return FALSE;
    if (!damage_27C20C(game_cls3[*slot] + 6, type, data, (short)v))
        return FALSE;
    G3A_ENT(*ent).type = type;
    G3A_ENT(*ent).active = 1;
    G3A_ENT(*ent).f23 = data[0];
    G3A_ENT(*ent).f24 = data[6];
    e = &G3A_ENT(*ent);
    e->f21 = f21;
    damage_27C360(*ent);
    if (type == 10)
        game_35B60C = *ent;
    return TRUE;
}

bool damage_27C20C(uchar *rec, uchar type, uchar *data, short v)
{
    int i;

    damage_27CCA0(3, type, rec, 3, data[1]);
    DamageInitSystems(3, type, rec + 0x15, 3);
    objacc_28B914((ObjMeter *)(rec + 0x46), *(long *)&game_cls3_types[type][0] << 16,
                  (long)data[2] << 16, (long)*(ushort *)&game_cls3_types[type][4] << 16,
                  (long)data[3] << 16, 0);
    rec[0x44] = 1;
    rec[0x45] = 0;
    for (i = 0; i < 2; i++)
        damage_27E0E0((WeapSlot *)(rec + 0x24 + i * 0xF), data[4 + i], 0);
    damage_27D140((NearList *)(rec + 0x5C), *(long *)&game_cls3_types[type][6], *(long *)&game_cls3_types[type][0xA],
                  0x20, (NearEntry *)(rec + 0x6C));
    *(long *)(rec + 0x12C) = 0;
    *(short *)(rec + 0x130) = 0;
    *(short *)(rec + 0x132) = 0;
    *(long *)(rec + 0x13C) = 0;
    *(long *)(rec + 0x140) = 0;
    rec[0x144] = 0;
    *(short *)(rec + 0x42) = v;
    return TRUE;
}

bool damage_27C360(short ent)
{
    ushort snd;

    switch (G3A_ENT(ent).type) {
    case 0: case 1: snd = 0x84B; break;
    case 2: snd = 0x849; break;
    case 3: snd = 0x847; break;
    case 4: snd = 0x846; break;
    case 5: snd = 0x845; break;
    case 6: snd = 0x848; break;
    case 7: snd = 0x844; break;
    case 14: snd = game_359708 == 0xE ? 0x844 : 0; break;
    default: snd = 0; break;
    }
    if (snd && noisy_add(ent, snd) >= 0)
        return TRUE;
    return FALSE;
}
