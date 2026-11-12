/* pbacreate.c -- Terra Nova PBA / soldier (class 1) record creation (module M044; source
 * name unknown).  Decompiled from __FF.EXE v1.09; names are the map's placeholders.
 *
 *   0x27D720 damage_27D720  create a class 1 entity of a kind from a template
 *   0x27D818 damage_27D818  initialise its PBA record (record + 6)
 *   0x27DBB8 damage_27DBB8  type table byte +0x19 of a kind
 *   0x27DBD4 damage_27DBD4  min(system state, 50) * 2 * (type +0x0D) / 100
 *   0x27DC2C damage_27DC2C  the same with type +0x11
 *
 * Type table game_pba_types (0x46 bytes per kind) fields used: +0 word meter max, +2 word
 * meter rate, +5 dword, +9 dword, +0xD / +0x11 near list radii, +0x15 / +0x17 words,
 * +0x19 byte, +0x1A byte, +0x1B byte (has a unit table record), +0x1D biped type.
 * Template bytes: [0] entity +0x23, [1] .. [0xB] percentages / kinds, [0xC] entity +0x24,
 * [0xD] unit table index.
 * Kinds 0xE..0x11 share one system record (0x441369, 8 systems) and their meter rate is
 * scaled by the skill level table 0x396388.
 */
#include "g3_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
short game_396386[8];
uchar game_441369[0x40];
#endif

#define RW(p, o) (*(short *)((uchar *)(p) + (o)))
#define RUW(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define RL(p, o) (*(long *)((uchar *)(p) + (o)))
#define RP(p, o) (*(uchar **)((uchar *)(p) + (o)))
#define SHARED_KIND(k) ((uchar)(k) >= 0xE && (uchar)(k) <= 0x11)

uchar damage_27D720(short *ent, short *slot, uchar f21, uchar kind, uchar b2, uchar *tmpl)
{
    if (!grid_295588(1, ent, slot))
        return 0;
    if (!damage_27D818((uchar *)&game_pba[*slot] + 6, kind, b2, tmpl, *ent))
        return 0;
    G3B_ENT(*ent).type = kind;
    G3B_ENT(*ent).active = 1;
    G3B_ENT(*ent).f23 = tmpl[0];
    G3B_ENT(*ent).f24 = tmpl[0xC];
    biped_27F150(*ent, PBAT_B(kind, 0x1D));
    G3B_ENT(*ent).f21 = f21;
    return 1;
}

uchar damage_27D818(uchar *rec, uchar kind, uchar b2, uchar *tmpl, long unused)
{
    long pct, a;
    ulong w, v;
    uchar *sys;

    rec[0] = kind;
    rec[1] = PBAT_B(kind, 0x1A);
    rec[2] = b2;
    if (SHARED_KIND(kind)) {
        RP(rec, 0x96) = game_441369;
        RW(rec, 0x9A) = 8;
    } else {
        RW(rec, 0x9A) = 4;
        RP(rec, 0x96) = rec + 0x2B;
    }
    damage_27CCA0(1, kind, rec + 0xF, 4, tmpl[2]);
    DamageInitSystems(1, kind, RP(rec, 0x96), RUW(rec, 0x9A));
    if (PBAT_B(kind, 0x1B))
        damage_27E200((UnitTabRec *)(rec + 3), tmpl[0xD]);
    else
        RL(rec, 7) = -1;
    if (game_359708 == 0x1D && SHARED_KIND(kind))
        RL(rec, 3) = 0;
    if (SHARED_KIND(kind))
        pct = game_396386[1 + game_442900];
    else
        pct = 100;
    a = pct * PBAT_W(kind, 2) / 100;
    objacc_28B914((ObjMeter *)(rec + 0x80), (fix)PBAT_W(kind, 0) << 16,
                  (fix)(tmpl[3] * (long)PBAT_W(kind, 0) / 100) << 16,
                  (fix)(a * tmpl[4] / 100) << 16, (fix)a << 16, 6);
    sys = RP(rec, 0x96);
    w = RUW(sys, 3);
    v = tmpl[1] * w / 100;
    if (w < v)
        v = w;
    RUW(sys, 3) = (ushort)v;
    rec[0x7B] = 4;
    damage_27E0E0((WeapSlot *)(rec + 0x3F), tmpl[8], 2);
    damage_27E0E0((WeapSlot *)(rec + 0x4E), tmpl[9], 2);
    damage_27E0E0((WeapSlot *)(rec + 0x5D), tmpl[0xA], 3);
    damage_27E0E0((WeapSlot *)(rec + 0x6C), tmpl[0xB], 3);
    RL(rec, 0x7C) = -1;
    RW(rec, 0x9C) = 0;
    rec[0x9E] = 0;
    RW(rec, 0xA2) = 0;
    RW(rec, 0xA0) = (short)((long)PBAT_W(rec[0], 0x17) * tmpl[7] / 100);
    RW(rec, 0xA4) = (short)((long)PBAT_W(rec[0], 0x15) * tmpl[6] / 100);
    RW(rec, 0xA6) = PBAT_W(rec[0], 0x15);
    RL(rec, 0xC0) = PBAT_L(rec[0], 9);
    RL(rec, 0xCE) = PBAT_L(rec[0], 5) * tmpl[5] / 100;
    RL(rec, 0xC4) = (game_442435 >> 16) << 16;
    rec[0xC8] = 0;
    RL(rec, 0xD2) = 0;
    rec[0xD7] = 0;
    rec[0xC9] = (rec[0xC9] | 1) & 0xC1;
    if (game_359708 == 0x27)
        rec[0xD6] = 1;
    else if (game_359708 == 0x1D)
        rec[0xD6] = 0;
    else
        rec[0xD6] = 3;
    damage_27D140((NearList *)(rec + 0xAC), PBAT_L(rec[0], 0xD), PBAT_L(rec[0], 0x11), 0x40,
                  (NearEntry *)(rec + 0x3FC));
    return 1;
}

uchar damage_27DBB8(uchar kind)
{
    return PBAT_B(kind, 0x19);
}

/* min(system 1 state, 50) * 2 (as a byte) times type dword / 100 (divided first) */
static long scaled(uchar *rec, short ent, long off)
{
    uchar v = damage_27DF14(ent, 1);
    uchar m = (uchar)((v > 0x32 ? 0x32 : v) * 2);
    return PBAT_L(rec[0], off) / 100 * m;
}

long damage_27DBD4(uchar *rec, short ent)
{
    return scaled(rec, ent, 0xD);
}

long damage_27DC2C(uchar *rec, short ent)
{
    return scaled(rec, ent, 0x11);
}
