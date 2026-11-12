/* projinit.c -- Terra Nova: weapon slot and class-5 record creation (module M046; source name
 * unknown).  Decompiled from __FF.EXE v1.09.
 *
 *   0x27E0E0 damage_27E0E0  initialise a weapon slot (0xF bytes) for ordnance type `kind`
 *   0x27E134 damage_27E134  set the target position (+6, +8) of a class-5 record (unused)
 *   0x27E14C damage_27E14C  create a class-5 entity (grid_295588) and set its target position
 */
#include "g3_c.h"
#include "g2_b.h"      /* game_cls5 (owner g2b) */

void damage_27E0E0(WeapSlot *w, long kind, long b1)
{
    uchar k = (uchar)kind;
    uchar *t = ordn_objtype[k];

    w->b1 = (uchar)b1;
    w->kind = k;
    w->f2 = *(short *)(t + 0x1C);
    w->fa = 0;
    w->f6 = (game_442435 >> 16) << 16;
    w->f4 = *(short *)(t + 0x1A);
    if (w->f4 == 1)
        w->f4 = 0;
    w->fe = 0;
}

uchar damage_27E134(uchar *rec, long unused, short *pos)
{
    *(short *)(rec + 6) = pos[0];
    *(short *)(rec + 0x1C) = 0;
    *(short *)(rec + 8) = pos[1];
    return 1;
}

bool damage_27E14C(short *ent, short *slot, long kind, void *key)
{
    uchar *rec;
    short *pos = key;

    if (!grid_295588(5, ent, slot))
        return 0;
    rec = (uchar *)&game_cls5[*slot];
    *(short *)(rec + 6) = pos[0];
    *(short *)(rec + 0x1C) = 0;
    *(short *)(rec + 8) = pos[1];
    G3C_ENT(*ent).type = (uchar)kind;
    G3C_ENT(*ent).active = 1;
    G3C_ENT(*ent).f23 = 0;
    return 1;
}
