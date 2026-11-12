/* partobj.c -- Terra Nova class 4 (part / structure piece) object creation (module M041;
 * source name unknown).  Decompiled from __FF.EXE v1.09; names are the map's placeholders.
 *
 *   0x27CF30 damage_27CF30  create a class 4 entity of a kind from a template
 *   0x27D018 damage_27D018  initialise its class 4 record (record + 6)
 *
 * Template bytes: [0] entity +0x23, [1] damage multiplier, [2] meter start, [3] meter rate 2,
 * [4] weapon kind, [6] entity +0x24.
 * Class 4 record (0x79 bytes, the part after the 6-byte pool header): +0 w0, +2 w2, +4 word
 * (caller's value), +6 shape count (1 cylinder, 2 dome, 5 otherwise), +7 weapon count (1),
 * +8, +9, +0xA damage table, +0x2D weapon slots (15 bytes each), +0x4B systems, +0x50 meter,
 * +0x66, +0x70 random 10-bit word, +0x72.
 */
#include "g3_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
long game_35B670;
RndStream game_rnd_35B674;
ushort game_395728[256][2];
#endif

#define RW(p, o) (*(short *)((uchar *)(p) + (o)))
#define RL(p, o) (*(long *)((uchar *)(p) + (o)))

uchar damage_27CF30(void *a, void *b, long c, long d, void *e, long obj)
{
    short *ent = a, *slot = b;
    uchar *tmpl = e;

    if (!grid_295588(4, ent, slot))
        return 0;
    if (!damage_27D018(&game_cls4[*slot][6], (uchar)d, tmpl, (short)obj))
        return 0;
    G3B_ENT(*ent).type = (uchar)d;
    G3B_ENT(*ent).active = 1;
    G3B_ENT(*ent).f23 = tmpl[0];
    G3B_ENT(*ent).f24 = tmpl[6];
    G3B_ENT(*ent).f21 = (uchar)c;
    if ((uchar)d == 0x3E)
        game_35B670 = *ent;
    return 1;
}

uchar damage_27D018(uchar *rec, uchar kind, uchar *tmpl, short a6)
{
    ShapeItem *s = interact_2973E8(4, kind);
    ulong r;
    long i;

    if (s->type == 2)
        rec[6] = 1;
    else if (s->type == 3)
        rec[6] = 2;
    else
        rec[6] = 5;
    damage_27CCA0(4, kind, rec + 0xA, (schar)rec[6], tmpl[1]);
    DamageInitSystems(4, kind, rec + 0x4B, 1);
    RW(rec, 0) = 0;
    RW(rec, 2) = 0;
    rec[7] = 1;
    rec[8] = 0;
    for (i = 0; i < (schar)rec[7]; i++)
        damage_27E0E0((WeapSlot *)(rec + 0x2D + 15 * i), tmpl[4 + i], 0);
    RW(rec, 4) = a6;
    objacc_28B914((ObjMeter *)(rec + 0x50), (fix)game_395728[kind][0] << 16, (fix)tmpl[2] << 16,
                  (fix)game_395728[kind][1] << 16, (fix)tmpl[3] << 16, 0);
    r = game_rnd_35B674.f_Next(&game_rnd_35B674);
    RL(rec, 0x66) = 0;
    rec[9] = 0;
    rec[0x72] = 0;
    RW(rec, 0x70) = (short)(r >> 22);
    return 1;
}
