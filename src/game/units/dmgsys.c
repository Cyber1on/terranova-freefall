/* dmgsys.c -- Terra Nova damage systems set-up (second file of module M040; source name
 * unknown, the error string names DamageInitSystems).  Decompiled from __FF.EXE v1.09.
 *
 *   0x27CCA0 damage_27CCA0      fill n hit-point records (7 bytes) of an object from its type
 *   0x27CDE4 DamageInitSystems  fill n subsystem records (5 bytes) of an object from its type
 *
 * Per-class source tables (class 1..4: PBA, vehicle, aircraft, installation), indexed by type:
 *   hit points  0x396318 / 0x395BB4 / 0x3963CE / 0x396178 (3 bytes: ushort value, byte)
 *   systems     0x395608 (16 bytes) / 0x3955D8 (4) / 0x3962B2 (6) / 0x395B20 (2): ushort[]
 * Some types (class 1 types 3..12, class 2 types 4..6, class 3 types 2..6) are scaled by a
 * per-skill-level percentage, class 1 types 0..2 by another one.
 * Quirks kept:
 *  - damage_27CCA0 does not advance the source: all n records get the first entry;
 *  - DamageInitSystems scales only the first record; its "wrong number of systems" report can
 *    only fire for a negative count.
 */
#include "g3_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar game_hp_cls1[64][3], game_hp_cls2[64][3], game_hp_cls3[64][3], game_hp_cls4[64][3];
ushort game_sys_cls1[64][8], game_sys_cls2[64][2], game_sys_cls3[64][3], game_sys_cls4[64][1];
short game_hp_skill_a[8], game_hp_skill_b[8], game_sys_skill_a[8], game_sys_skill_b[8];
#endif

static int scaled_type(uchar cls, uchar type)
{
    return (cls == 1 && type >= 3 && type <= 12) || (cls == 2 && type >= 4 && type <= 6)
        || (cls == 3 && type >= 2 && type <= 6);
}

void damage_27CCA0(uchar cls, uchar type, uchar *out, long n, long mult)
{
    uchar *src;
    long m, i;
    ushort w;

    switch (cls) {
    case 1: src = game_hp_cls1[type]; break;
    case 2: src = game_hp_cls2[type]; break;
    case 3: src = game_hp_cls3[type]; break;
    case 4: src = game_hp_cls4[type]; break;
    default: return;
    }
    m = 100;
    if (scaled_type(cls, type))
        m = game_hp_skill_a[game_442900];
    else if (cls == 1 && type <= 2)
        m = game_hp_skill_b[game_442900];
    for (i = 0; i < n; i++) {
        w = (ushort)((ulong)(*(ushort *)src * m) / 100);
        *(ushort *)(out + 4) = w;
        *(ushort *)(out + 2) = w;
        *(short *)out = (short)((long)*(short *)(out + 2) * mult / 100);
        out += 7;
        out[-1] = src[2];
    }
}

void DamageInitSystems(uchar cls, uchar type, uchar *out, long n)
{
    ushort *src;
    long m, i;

    switch (cls) {
    case 1: src = game_sys_cls1[type]; break;
    case 2: src = game_sys_cls2[type]; break;
    case 3: src = game_sys_cls3[type]; break;
    case 4: src = game_sys_cls4[type]; break;
    default: return;
    }
    m = 100;
    if (scaled_type(cls, type))
        m = game_sys_skill_a[game_442900];
    else if (cls == 1 && type <= 2)
        m = game_sys_skill_b[game_442900];
    for (i = 0; i < n; i++) {
        *(ushort *)(out + 1) = *src;
        if (i == 0)
            *(ushort *)(out + 1) = (ushort)((ulong)(*(ushort *)(out + 1) * m) / 100);
        out += 5;
        *(ushort *)(out - 2) = *(ushort *)(out - 4);
        src++;
    }
    if (i != n)
        DbgReportError(1, "DamageInitSystems: wrong number of systems for class: %d subclass: %d (found %d, expect %d\n",
                       cls, type, i, n);
}
