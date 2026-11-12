/* flashfx.c -- Terra Nova module M061 (0x28DE20-0x28E84F; source file name unknown): damage to
 * class 0 objects (trees, rocks, small structures) and the designator strike flash.
 * Decompiled from __FF.EXE v1.09.  Names are ours (the map called the module "flash /
 * palette effects" after flash_28E350).
 *
 *   0x28DE20 flash_28DE20  qsort comparator: descending by the first dword
 *   0x28DE40 flash_28DE40  more than 100 live class 0 objects: kill up to 10 far ones
 *   0x28E040 flash_28E040  damage a class 0 object (ent, attacker, mode, amount)
 *   0x28E2FC flash_28E2FC  message handler: unpacks a type 0x11 message into flash_28E040
 *   0x28E350 flash_28E350  designator strike: white palette flash, then a blast object
 *   0x28E5F0 flash_28E5F0  would `amount' destroy an object of this type? (unused)
 *   0x28E630 flash_28E630  objmap entries for every live type 0x39 object
 *   0x28E6C0 flash_28E6C0  damage rules: hit (object destroyed) / kill (destroyed model)
 *
 * Quirks kept:
 *  - flash_28DE40 collects every live object into a 0x800-byte buffer (256 entries of 8
 *    bytes) without a bound, sorts the 256 entries (zeros past the live ones) and then walks
 *    every second entry (stride 16 bytes) while the walk index is below the live count.
 *  - flash_28E6C0 (team rules) toggles flash_team_type between the attacker's type and 0.
 *  - flash_28E350 ignores its second argument; the descriptor handed to sfx_28D0F4 has its
 *    entity word unset (sfx_28D0F4 writes it).
 */
#include "g6_b.h"
#include "g6_c.h"     /* added at the G5-G8 merge */
#include "g7_a.h"     /* added at the G5-G8 merge */
#ifndef TN_WATCOM_ABI
extern int port_burn;   /* "Tree explosion" (port/mods/burn.c): 0 Original, 1 Realistic */
#endif

#ifndef TN_LINK_ORIGINAL_DATA
long stat_kills_359718;
ulong flash_end_time;
uchar flash_saved_pal[0x300], flash_from_pal[0x300], flash_white_pal[0x300];
uchar flash_team_type, flash_pal_effect;
uchar *drawobj_cls0_models;
#endif

static long iabs(long v) { long s = v >> 31; return (v ^ s) - s; }

long flash_28DE20(long *a, long *b)
{
    if (*a == *b) return 0;
    if (*a > *b) return -1;
    return 1;
}

void flash_28DE40(ulong dt)
{
    long live = 0, killed = 0, n = 0, k;
    short i;
    ulong flag;
    long *buf, *p;

    i = G6B_IDX15(G6B_C0_W0(0));
    flag = G6B_C0_W0(i) >> 15;
    while (i) {
        if (!(uchar)flag) live++;
        i = G6B_C0_NEXT(i);
        flag = G6B_C0_W0(i) >> 15;
    }
    if (live <= 100)
        return;
    buf = ffmem_2C4F40(0x800);
    memset(buf, 0, 0x800);
    i = G6B_IDX15(G6B_C0_W0(0));
    flag = G6B_C0_W0(i) >> 15;
    p = buf;
    while (i) {
        if (!(uchar)flag) {
            short e = G6B_IDX15(G6B_C0_W0(i));
            long dx = iabs(G6B_ENT(game_player_entity).x - G6B_ENT(e).x);
            long dy = iabs(G6B_ENT(game_player_entity).y - G6B_ENT(e).y);
            p[0] = dy + dx;
            p[1] = i;
            p += 2;
            n++;
        }
        i = G6B_C0_NEXT(i);
        flag = G6B_C0_W0(i) >> 15;
    }
    lg_qsort(buf, 0x100, 8, WATCOM_FN(flash_28DE20));
    p = buf;
    for (k = 0; killed < 10 && k < live; k += 2, p += 4) {
        short e = G6B_IDX15(G6B_C0_W0(p[1]));
        uchar t = G6B_ENT(e).type;
        if (t <= 0x19) {        /* the original also tests t >= 0 (a local that stays 0) */
            KillObjMark(e, 3);
            killed++;
        }
    }
    ffmem_2C4FE4(buf);
}

/* types whose kills count for the player: 0..2, 9..11 and 0x34 */
static int counted_type(uchar t)
{
    return t <= 2 || (t >= 9 && t <= 0xB) || t == 0x34;
}

void flash_28E040(long ent, long attacker, long mode, long amount)
{
    uchar hit, kill;
    short e = (short)ent, a = (short)attacker;

    flash_28E6C0(G6B_ENT(e).type, e, a, mode, amount, &hit, &kill);
    if (hit || (kill && G6B_C0_DEAD(G6B_ENT(e).slot))) {
        if (mode == 1) {
            objdmg_death_add(e, a);
            if (G6B_ENT(a).cls == 8 && counted_type(G6B_ENT(e).type)
                && game_player_entity == *(short *)&game_cls8[G6B_ENT(a).slot][6])
                stat_kills_359718++;
            return;
        }
        if (counted_type(G6B_ENT(e).type)
            && a == game_player_entity && G6B_ENT(a).cls == 1 && hit)
            stat_kills_359718++;
        flash_28E350(e, mode);
        return;
    }
    if (!kill)
        return;
    G6B_C0_DEAD(G6B_ENT(e).slot) = 1;
    sfx_28CB24(sfx_28C300(e), e);
    if (G6B_ENT(a).cls == 5 && counted_type(G6B_ENT(e).type)
        && game_player_entity == *(short *)&game_cls5[G6B_ENT(a).slot][6])
        stat_kills_359718++;
#ifndef TN_WATCOM_ABI
    /* P109: with "Tree explosion" the plant bursts whole (its death effect above), so its broken
     * picture is never drawn; it goes now as a second hit would take it (flash_28E350), so no
     * unseen obstacle as wide as the plant stays and no second hit is needed.  What is left is
     * the pack's stump, a picture only.  With Tree explosion Original, the game's rule */
    if (port_burn)
        KillObjMark(e, 1);
#endif
}

void flash_28E2FC(void *msg)
{
    long ent, target, c, d, e;
    aiacc_25AE98(msg, &ent, &target, &c, &d, &e);
    if (target == 0xFFF)
        return;
    flash_28E040((short)ent, target & 0xFFF, (ushort)d, (ushort)e);
}

uchar flash_28E350(long ent, long b)
{
    GridObjDesc d;
    short slot, obj, s1, s2;
    short e = (short)ent;

    if (e != fire_35BF6A) {
        KillObjMark(e, 1);
        sfx_28CB24(sfx_28C300(e), e);
        return 1;
    }
    if (game_time >= fire_35BF6C && !fire_35BF30) {
        fire_35BF30 = 1;
        lg_memset(flash_white_pal, 0xFFFFFFFF, 0, 0x300);
        gr_get_pal(0, 0x100, flash_from_pal);
        gr_get_pal(0, 0x100, flash_saved_pal);
        flash_pal_effect = palette_install_effect(0, 0, 0, 0xFF, 10, 0x1E, flash_from_pal,
                                                  flash_white_pal);
        flash_end_time = game_time + 500;
        return 0;
    }
    if (!fire_35BF30 || game_time < flash_end_time)
        return 0;
    palette_remove_effect(flash_pal_effect);
    gr_set_pal(0, 0x100, flash_saved_pal);
    if (timers_279E80(&obj, &slot, 0xD)) {
        G6B_ENT(obj).x = G6B_ENT(e).x;
        G6B_ENT(obj).y = G6B_ENT(e).y;
        G6B_ENT(obj).z = G6B_ENT(e).z;
        G6B_ENT(obj).type = 0xD;
        *(short *)&game_cls8[slot][8] = e;
        game_cls8[slot][0xB] = 1;
        game_cls8[slot][0xA] = 1;
        *(short *)&game_cls8[slot][6] = fire_397C9C;
    }
    d.x = G6B_ENT(e).x;
    d.y = G6B_ENT(e).y;
    d.z = G6B_ENT(e).z;
    d.heading = 0;
    d.f10 = 0;
    d.f12 = 0;
    sfx_28D0F4(&s2, &s1, 0x1E, 0xFF, &d);
    SndPlay(0x76C, 0, 0x28, 1, 0x40, 0x40, 0);
    hud_268FD0(fix_fast_pyth_dist(G6B_ENT(game_player_entity).x - G6B_ENT(e).x,
                                  G6B_ENT(game_player_entity).y - G6B_ENT(e).y) >> 16, 100);
    KillObjMark(e, 1);
    fire_35BF6A = 0;
    return 1;
}

uchar flash_28E5F0(uchar type, long mode, long amount)
{
    uchar hit, kill;
    flash_28E6C0(type, 0, 0, mode, amount, &hit, &kill);
    return hit || kill;
}

void flash_28E630(void)
{
    short i = G6B_IDX15(G6B_C0_W0(0));
    ulong flag = G6B_C0_W0(i) >> 15;
    while (i) {
        if (!(uchar)flag) {
            short e = G6B_IDX15(G6B_C0_W0(i));
            if (G6B_ENT(e).type == 0x39) {
                objmap_add(e, 4);
                killobj_289554(e, WATCOM_FN(objmap_kill_cb), -1);
            }
        }
        i = G6B_C0_NEXT(i);
        flag = G6B_C0_W0(i) >> 15;
    }
}

void flash_28E6C0(uchar type, long ent, long attacker, long mode, long amount,
                  uchar *hit, uchar *kill)
{
    short e = (short)ent, a = (short)attacker;
    *kill = 0;
    *hit = *kill;
    if (!amount)
        return;
    if (game_396E18) {
        if (!e || G6B_ENT(e).f23 != (ulong)game_396E18)
            return;
        if (game_396E18 == 1 && a) {
            uchar t = G6B_ENT(a).type;
            if (flash_team_type == t)
                return;
            flash_team_type = flash_team_type ? 0 : t;
        }
        *hit = 1;
        return;
    }
    if ((ulong)mode < 1 || (ulong)mode > 3)
        return;
    if (!e) {
        ushort hp = *(ushort *)&game_cls0_types[type][1];
        if (amount < (long)hp || !hp)
            return;
        *hit = 1;
        return;
    }
    {
        short s = G6B_ENT(e).slot;
        ushort hp = G6B_C0_HP(s);
        if (amount < (long)hp) {
            G6B_C0_HP(s) = hp - amount;
            return;
        }
        if (amount > (long)*(ushort *)&game_cls0_types[type][1] * 2
            || !*(long *)(drawobj_cls0_models + type * 8 + 4)) {
            *hit = 1;
            return;
        }
        *kill = 1;
    }
}
