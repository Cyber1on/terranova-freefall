/* statinit.c -- Terra Nova class 0 object creation (module M043; source name unknown).
 * Decompiled from __FF.EXE v1.09.
 *
 *   0x27D610 damage_27D610  create a class 0 entity and its 9-byte pool record
 *   0x27D6E8 damage_27D6E8  initialise a class 0 pool record from its type
 *
 * Class 0 record (after the 6-byte pool header): +0 byte (data[1]), +1 word hit points: 0x19
 * when data[1] != 0, else the type's value (game_cls0_types, game.h).
 */
#include "g3_a.h"

/* data: {+0 -> entity +0x23, +1 record byte, +2 -> entity +0x24}; types >= 0x3C fail */
bool damage_27D610(short *ent, short *slot, uchar type, uchar *data)
{
    uchar *rec;

    if (!grid_295588(0, ent, slot))
        return FALSE;
    rec = game_cls0[*slot] + 6;
    /* damage_27D6E8 inlined */
    if (type >= 0x3C)
        return FALSE;
    rec[0] = data[1];
    if (rec[0])
        *(short *)(rec + 1) = 0x19;
    else
        *(short *)(rec + 1) = *(short *)&game_cls0_types[type][1];
    G3A_ENT(*ent).type = type;
    G3A_ENT(*ent).active = 1;
    G3A_ENT(*ent).f23 = data[0];
    G3A_ENT(*ent).f24 = data[2];
    return TRUE;
}

bool damage_27D6E8(uchar *rec, uchar type, uchar *data)
{
    if (type >= 0x3C)
        return FALSE;
    rec[0] = data[1];
    if (rec[0]) {
        *(short *)(rec + 1) = 0x19;
        return TRUE;
    }
    *(short *)(rec + 1) = *(short *)&game_cls0_types[type][1];
    return TRUE;
}
