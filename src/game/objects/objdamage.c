/* objdamage.c -- Terra Nova module M053 (source name unknown): damage to objects (body parts,
 * subsystems, armour meters, weapons), kill credit and statistics, the delayed destruction list
 * and the AI messages about hits.  Decompiled from __FF.EXE v1.09; names ours except the map
 * names other modules call.
 *
 *   0x2863B0 objdmg_msg_tmpl (data in the code segment, 2 dwords)
 *   0x2863B8 static_2863B8 (init)    0x286488 static_286488 (empty) 0x28648C objdmg_rand_amount
 *   0x2864D8 objdmg_systems          0x286794 static_286794 (records) 0x286994 objdmg_status_str
 *   0x286B10 objdmg_apply            0x286B94 objdmg_get_hp         0x286BE0 objdmg_drain
 *   0x286C7C objdmg_mark             0x286D38 obj_owner             0x286D9C objdmg_snd_done
 *   0x286DC8 objdmg_credit           0x287110 objdmg_hit_sound      0x287168 static_287168
 *   0x287218 obj_damage              0x287E5C obj_destroy           0x287F3C objdmg_death_time
 *   0x287F9C objdmg_death_add        0x288040 static_288040 (cancel) 0x28808C static_28808C (run)
 *   0x2880EC objdmg_alert            0x288130 objdmg_repair_all
 *
 * static_286794 finds the damage records of an entity (class 1 PBA, 2 vehicle, 3 aircraft,
 * 4 installation; layouts in g2_b2.h); obj_damage spreads an amount over the body parts (a
 * per-class random table from RESSIM.RES picks the parts), the armour meter, the mounted
 * weapons and the subsystems, then credits the kill and destroys the object or puts it on the
 * delayed death list (static_28808C destroys it when its time comes).
 *
 * Quirks kept:
 *  - obj_damage tests "d > 100 && d < 50" (never true) twice, and lowers the damage of a badly
 *    hurt part by d >> 2 (arithmetic shift) but of a lightly hurt part by d / 4;
 *  - objdmg_status_str gives 'D' to a part exactly at 73 % (the 'N' test is strict);
 *  - objdmg_death_add writes slot -1 (0x396A08) when the count is below 128 but no slot is free;
 *  - static_288040 with entity 0 frees nothing but still decrements the count;
 *  - objdmg_credit calls mstat_2C701C for a kill even when nobody gets the credit (who = 0);
 *  - objdmg_systems divides by the subsystem maximum (and maximum / 8) without a zero test,
 *    static_286794 gives NULL records for other classes and objdmg_status_str reads them;
 *  - objdmg_systems keeps its weights in short w[8]: more subsystems overwrite its other
 *    locals (modelled with a raw frame, up to 20 subsystems);
 *  - obj_damage keeps the part shares in uchar pct[4]: parts 4..7 read the next local (the
 *    dword i * 4, so part 4 gets 16 % and parts 5..7 nothing); writes for parts 4..11 land in
 *    locals that are set again before use (not modelled), 12 and up would overwrite the
 *    subsystem pointer (not reproduced; class 4 objects take the part count from their record);
 *  - obj_damage's class 0 branch (mask 1, 100 % to part 0) is dead: the original tests the
 *    flags of the table address computation instead of the class;
 *  - obj_damage reads the armour byte +0x14 through a NULL pointer for class 3 (aircraft).
 */
#include "g2_b2.h"
#include "g3_c.h"      /* instsim_288554, damage_27D540 (owner g3c) */
#include "g3_a.h"      /* G3a callees (owner g3a) */
#include "g4_a.h"      /* AI callees (owners g4a/g4b/g4c1/g4c2; G4 merge) */
#include "g6_b.h"     /* added at the G5-G8 merge */
#include "g6_c.h"     /* added at the G5-G8 merge */
#include "g7_b.h"     /* added at the G5-G8 merge */
#include "g7_c.h"     /* added at the G5-G8 merge */
#include "g8_b.h"     /* added at the G5-G8 merge */
#include "g9_a.h"     /* added at the G9/G10 merge */

#ifndef TN_LINK_ORIGINAL_DATA
char objdmg_ressim_name[] = "ressim.res";
uchar objdmg_res_3A4[0x20];
uchar objdmg_cls_tab[9][0x20];
ObjDeath objdmg_deaths[OBJDEATH_MAX];
short objdmg_death_count;
signed char objdmg_radio_level;
uchar objdmg_hit_flag;
long game_396E10;
ulong game_396E14;
long game_396E18;
ulong objdmg_msg_from = 0x83;
ulong objdmg_msg_to = 0x80;
ushort objdmg_res_ids[9] = { 0, 0x3A0, 0x3A1, 0x3A2, 0x3A3 };
uchar game_35B72E;
uchar objdmg_cls_parts[9] = { 1, 4, 1, 3, 5, 1, 1, 1, 1 };
long objdmg_snd_chan = -1;
long objdmg_snd_count;
long game_35B7EC;
uchar game_39640C[0x40][9];
uchar game_441366;
ushort game_441367;
ulong game_441391[8];
#endif

/* message addresses {0x80, id}: the low word is the address kind, the high word the brain */
#if !defined(TN_LINK_ORIGINAL_DATA) || defined(TN_WATCOM_ABI)
/* (the port takes it from the original's code segment like the other data rows) */
const ulong objdmg_msg_tmpl[2] = { 0x80, 0x80 };
#endif

#define MSG_TO_ID (((ushort *)&objdmg_msg_to)[1])

void static_2863B8(void)
{
    int fd;
    long i;

    fd = ResOpenResFile(objdmg_ressim_name, 0, 0);
    game_rnd_35B704.f_Seed(&game_rnd_35B704, 0);
    RefExtract(ResLock(0x3A4), 0x3A40000, objdmg_res_3A4);
    ResUnlock(0x3A4);
    memset(objdmg_cls_tab, 0, sizeof(objdmg_cls_tab));
    for (i = 0; i < 9; i++) {
        if (objdmg_res_ids[i] != 0) {
            void *t = ResLock(objdmg_res_ids[i]);
            RefExtract(t, (ulong)objdmg_res_ids[i] << 16, objdmg_cls_tab[i]);
            ResUnlock(objdmg_res_ids[i]);
        }
    }
    objdmg_death_count = 0;
    memset(objdmg_deaths, 0, sizeof(objdmg_deaths));
    objdmg_radio_level = 0;
    ResCloseFile(fd);
}

void static_286488(void)
{
}

/* v / 2 + v / 3 + (number of v / 3 random draws with the top bit clear) */
long objdmg_rand_amount(long v)
{
    long n = v / 3, r = v / 2 + n, i;

    for (i = 0; i < n; i++)
        r += (game_rnd_35B704.f_Next(&game_rnd_35B704) & 0x80000000) == 0;
    return r;
}

/* spread amount (scaled by the skill level) over subsystems 1..n-1 of sys, weighted by a
 * random draw that favours subsystems near 60 %.  The weights live in an 8-entry array of the
 * original's frame; with n > 8 they overwrite the locals after it (n, the loop counter, the sum,
 * ...), so the frame is kept as raw bytes: f+0 short w[8], +0x10 n, +0x14 i, +0x18 sum,
 * +0x1C amount, +0x20 short ent, +0x24 signed char count (n > 20 reaches the saved registers:
 * not reproduced). */
void objdmg_systems(long ent, uchar *sys, long n, long amount)
{
    uchar f[0x40];
    long k, pct, t, d, old, cur;
    uchar *p;

    S16(f, 0x20) = (short)ent;
    S32(f, 0x10) = n;
    S32(f, 0x1C) = amount;
    S32(f, 0x18) = 0;
    S8(f, 0x24) = 0;
    if (amount == 0)
        return;
    S32(f, 0x1C) = game_skill_tab[7][game_442900] * amount / 100;
    memset(f, 0, 16);
    S32(f, 0x14) = 0;
    if (S32(f, 0x10) > 0) {
        for (k = 0, p = sys; ; k++, p += 5) {
            if (S32(f, 0x14) != 0) {
                pct = (long)U16(p, 3) * 100 / (long)U16(p, 1);
                t = 0x3C - pct;
                if (t < 0)
                    t = -t;
                t = 0x3E - t;
                S16(f, 2 * k) = (short)RndRange(&game_rnd_35B704, 1, t * t);
                S32(f, 0x18) += S16(f, 2 * k);
                S8(f, 0x24)++;
            }
            if (++S32(f, 0x14) >= S32(f, 0x10))
                break;
        }
    }
    if (S8(f, 0x24) == 0 || S32(f, 0x18) == 0)
        return;
    S32(f, 0x14) = 0;
    if (S32(f, 0x10) <= 0)
        return;
    for (k = 0, p = sys; ; k++, p += 5) {
        if (S16(f, 2 * k) == 0 || U16(p, 3) == 0)
            goto next;
        d = S16(f, 2 * k) * S32(f, 0x1C) * S8(f, 0x24) / S32(f, 0x18);
        if (d == 0 && S8(f, 0x24) > S32(f, 0x1C))
            d = 1;
        if (d == 0)
            goto next;
        if (d > 0x41)
            d = 0x41;
        old = ((long)U16(p, 3) - 7) / ((long)U16(p, 1) >> 3);
        cur = (long)U16(p, 3) - d;
        if (cur < 0)
            cur = 0;
        U16(p, 3) = (ushort)cur;
        if (S16(f, 0x20) == game_player_entity) {
            ushort q;
            damage_27E064((uchar)S32(f, 0x14));
            if (objdmg_radio_level >= 2)
                goto next;
            q = (ushort)((long)U16(p, 3) * 100 / (long)U16(p, 1));
            if (q <= 0x28) {
                objdmg_radio_level = 2;
                RadioAck(0x34);
            } else if (q <= 0x4B && objdmg_radio_level == 0) {
                objdmg_radio_level = 1;
                RadioAck(0x33);
            }
        } else {
            long now = ((long)U16(p, 3) - 7) / ((long)U16(p, 1) >> 3);
            uchar side;
            Msg *m;
            if (now == old)
                goto next;
            side = ENT(S16(f, 0x20))->f21;
            if (side == 0)
                goto next;
            m = MsgAlloc();
            if (m == NULL)
                goto next;
            MSG_TO_ID = side;
            aiacc_25AF70(m, objdmg_msg_from, objdmg_msg_to, (uchar)S32(f, 0x14), (uchar)now, (uchar)old);
            MsgSend(m);
        }
    next:
        if (++S32(f, 0x14) >= S32(f, 0x10))
            break;
    }
}

/* the damage records of an entity; FALSE (all zero) for classes other than 1..4 */
uchar static_286794(long ent, void *parts_, void *hp_, void *np_, void *a5_, void *a6_, void *a7_,
                    void *a8_)
{
    uchar **parts = parts_, **sys = hp_, **armor = a6_, **wl = a7_;
    long *np = np_, *nsys = a5_, *nw = a8_;
    GameEntity *e = ENT(ent);
    short slot = e->slot;
    uchar *p;

    switch (e->cls) {
    case 1:
        p = (uchar *)&game_pba[slot];
        *parts = p + 0x15;
        *sys = PTR(p, 0x9C);
        *np = 4;
        *nsys = U16(p, 0xA0);
        *armor = p + 0x86;
        *wl = p + 0x45;
        *nw = S8(p, 0x81);
        return 1;
    case 2:
        p = (uchar *)&game_vehicles[slot];
        *parts = p + 6;
        *sys = p + 0xD;
        *np = 1;
        *nsys = 2;
        *armor = p + 0x37;
        *wl = p + 0x17;
        *nw = S8(p, 0x35);
        return 1;
    case 3:
        p = game_cls3[slot];
        *parts = p + 6;
        *sys = p + 0x1B;
        *np = 3;
        *nsys = 3;
        *armor = NULL;
        *wl = p + 0x2A;
        *nw = S8(p, 0x4A);
        return 1;
    case 4:
        p = game_cls4[slot];
        *parts = p + 0x10;
        *sys = p + 0x51;
        *np = S8(p, 0xC);
        *nsys = 1;
        *armor = p + 0x56;
        *wl = p + 0x33;
        *nw = S8(p, 0xD);
        return 1;
    }
    *parts = NULL;
    *sys = NULL;
    *np = 0;
    *nsys = 0;
    *armor = NULL;
    *wl = NULL;
    *nw = 0;
    return 0;
}

/* one letter per part: 0x84 healthy, 'N' damaged, 'D' badly damaged */
static uchar part_letter(uchar *part)
{
    long max = S16(part, 2), cur = S16(part, 0), hi = max * 0x49 / 100;

    if (cur > hi)
        return 0x84;
    if (cur > max * 0x25 / 100 && cur < hi)
        return 'N';
    return 'D';
}

void objdmg_status_str(uchar *out, long ent)
{
    uchar *parts, *sys, *armor, *wl;
    long np, nsys, nw, i;

    if (ENT(ent)->cls == 0) {
        for (i = 0; i < 4; i++)
            out[i] = 0x84;
        return;
    }
    static_286794((short)ent, &parts, &sys, &np, &nsys, &armor, &wl, &nw);
    out[0] = part_letter(parts);
    if (ENT(ent)->cls == 1) {
        for (i = 1; i < np; i++)
            out[i] = part_letter(parts + 7 * i);
    } else {
        for (i = 1; i < 4; i++)
            out[i] = out[0];
    }
}

/* unreferenced: damage any entity of class 1..4 */
void objdmg_apply(long ent, long attacker, long dtype, long mode, long amount)
{
    uchar *parts, *sys, *armor, *wl;
    long np, nsys, nw;

    if (static_286794((short)ent, &parts, &sys, &np, &nsys, &armor, &wl, &nw))
        obj_damage((short)ent, (short)attacker, (uchar)dtype, parts, np, sys, nsys, armor, wl, nw,
                   mode, amount);
}

ushort objdmg_get_hp(long ent)
{
    uchar *parts, *sys, *armor, *wl;
    long np, nsys, nw;

    if (static_286794((short)ent, &parts, &sys, &np, &nsys, &armor, &wl, &nw))
        return U16(sys, 3);
    return 0;
}

/* take amount hit points without part damage; at 0 the object dies (delayed when a killer is
 * given) */
void objdmg_drain(long ent, long killer, long amount)
{
    uchar *parts, *sys, *armor, *wl;
    long np, nsys, nw, v;

    if (!static_286794((short)ent, &parts, &sys, &np, &nsys, &armor, &wl, &nw))
        return;
    if (U16(sys, 3) == 0)
        return;
    v = (long)U16(sys, 3) - (short)amount;
    if (v < 0)
        v = 0;
    U16(sys, 3) = (ushort)v;
    if (U16(sys, 3) != 0)
        return;
    if ((short)killer != 0)
        objdmg_death_add((short)ent, (short)killer);
    else
        obj_destroy((short)ent, 1, 0);
}

/* set the bit of who's MFD index in ent's record */
void objdmg_mark(long ent, long who)
{
    signed char bit = (signed char)mfddata_2665E8((short)who);
    GameEntity *e;
    uchar b;

    if (bit == -1)
        return;
    e = ENT(ent);
    b = (uchar)(1u << ((uchar)bit & 31));
    switch (e->cls) {
    case 1: game_pba[e->slot].g_0d0[0xDD - 0xD0] |= b; break;
    case 2: game_vehicles[e->slot].f123 |= b; break;
    case 3: game_cls3[e->slot][0x14A] |= b; break;
    case 4: game_cls4[e->slot][0xF] |= b; break;
    }
}

/* the entity that controls a class 5 / 8 object (0 for the others) */
ushort obj_owner(long ent)
{
    GameEntity *e = ENT(ent);

    if (e->cls == 5)
        return U16(game_cls5[e->slot], 6);
    if (e->cls == 8)
        return U16(game_cls8[e->slot], 6);
    return 0;
}

void objdmg_snd_done(void)
{
    objdmg_snd_chan = -1;
}

/* kill / hit credit for attacker: statistics, kill comments and the AI message */
void objdmg_credit(long victim, long attacker, long dmg, long lost, long killed)
{
    GameEntity *a, *v;
    short who;

    if ((short)attacker == 0)
        return;
    a = ENT(attacker);
    who = a->slot;
    switch (a->cls) {
    case 5:
        who = U16(game_cls5[who], 6);
        mstat_2C6FD8(who);
        objdmg_mark((short)victim, who);
        break;
    case 8: {
        uchar *r = game_cls8[who];
        uchar t = a->type;
        who = S16(r, 6);
        if (t == 4 || t == 0xC || t == 0x10 || t == 0xE || t == 0xF || r[0xA] != 0)
            break;
        mstat_2C6FD8(who);
        objdmg_mark((short)victim, who);
        r[0xA] = 1;
        break;
    }
    case 1: case 2: case 3: case 4:
        who = (short)attacker;
        if (game_35BF38 < 2)
            mstat_2C6FD8((short)attacker);
        if (game_35BF38 == 1)
            game_35BF38 = 2;
        objdmg_mark((short)victim, who);
        break;
    default:
        who = 0;
        break;
    }
    if (who != 0) {
        if ((short)victim == strike_entity && who == game_player_entity) {
            if (++strike_35BF4A == 1)
                RadioMessage(0x3B);
        } else if ((short)victim == strike_entity && who != game_player_entity) {
            strike_35BF4C++;
        } else if (who == game_player_entity) {
            v = ENT(victim);
            if (v->cls == 1 && (v->type <= 2 || v->type == 0xD)) {
                short *q = (short *)PTR(&game_pba[game_player_slot], 0xBE);
                uchar found = 0;
                for (; *q != 0; q += 3) {
                    uchar b = ENT(*q)->f21;
                    if (b != 0 && game_brains[b].f005 == 2) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    if (v->type == 0xD) {
                        if (objdmg_snd_count < 3) {
                            if (objdmg_snd_chan < 0) {
                                long ch = SndPlay((ushort)(0x1E41 + (ushort)objdmg_snd_count), 4, 0xFF,
                                                  1, 0x40, 0xBE, 0);
                                objdmg_snd_chan = ch;
                                if (ch >= 0)
                                    snd_chan_add_callback(ch, WATCOM_FN(objdmg_snd_done), 0);
                            }
                            objdmg_snd_count += 2;
                        }
                    } else {
                        ulong from = objdmg_msg_tmpl[0], to = objdmg_msg_tmpl[1];
                        Msg *m;
                        from = (from & 0xFFFF) | (ulong)ENT(game_player_entity)->f21 << 16;
                        to = (to & 0xFFFF) | (ulong)v->f21 << 16;
                        m = MsgAlloc();
                        if (m) {
                            aiacc_25AE48(m, from, to, ENT(game_player_entity)->f21, v->f21);
                            MsgSend(m);
                        }
                    }
                }
            }
        }
    }
    if ((uchar)killed)
        mstat_2C701C(who, (short)victim);
}

/* hit sound of the attacking weapon (class 5 objects by type, class 8 by the type table) */
void objdmg_hit_sound(long ent, long amount, long dtype)
{
    GameEntity *e;
    uchar idx;

    if ((short)ent == 0)
        return;
    e = ENT(ent);
    if (e->cls == 5)
        idx = e->type;
    else if (e->cls == 8)
        idx = game_39640C[e->type][0];
    else
        return;
    snd_play_hit(idx, amount, (uchar)dtype);
}

void static_287168(long ent, long killer, long a, long b, long c)
{
    uchar *parts, *sys, *armor, *wl;
    long np, nsys, nw;
    uchar cls = ENT(ent)->cls;

    if (cls == 0) {
        flash_28E040((short)ent, (short)killer, b, c);
        return;
    }
    if (cls > 4)
        return;
    if (static_286794((short)ent, &parts, &sys, &np, &nsys, &armor, &wl, &nw))
        obj_damage((short)ent, (short)killer, (uchar)a, parts, np, sys, nsys, armor, wl, nw, b, c);
}

void obj_damage(long victim_, long attacker_, long dtype_, uchar *parts, long np, uchar *sys,
                long nsys, uchar *armor, uchar *wlist, long nw, long mode, long amount)
{
    short victim = (short)victim_, attacker = (short)attacker_;
    uchar dtype = (uchar)dtype_;
    GameEntity *ve = ENT(victim), *ae = ENT(attacker);
    uchar side = ve->f21;
    long total = 0, r, i;
    ushort before = 0, after = 0;   /* sums of the part values before / after (low words) */
    uchar pct[4];
    uchar mask, m;

    if (game_359708 == 0x24 && ve->cls == 2 && ve->type == 7 && ae->cls == 8 && ae->type == 0xD) {
        obj_destroy(victim, 1, 0);
        return;
    }
    objdmg_hit_flag = 1;
    if (mode == 0 || amount == 0 || sys == NULL)
        return;
    /* class 3 units have no armour record (static_286794 gives NULL): the test of its byte 0x14
     * below and objacc_28B938 went to DOS low memory for them (TN_LOWMEM; docs/BUGS.md G62) */
    armor = TN_LOWMEM(armor);
    if (victim == game_player_entity && game_441366 != 0)
        return;
    if (game_396E18 != 0 && game_396E18 != 2 && victim == game_player_entity)
        return;
    if (game_396E18 != 0 && victim != game_player_entity) {
        uchar t = ve->f23;
        if (t != game_396E18 && (game_396E18 != 2 || t < 4))
            return;
        if (game_396E18 == 3) {
            if (ae->cls == 5 && game_player_entity == S16(game_cls5[ae->slot], 6))
                return;
            if (ae->cls == 8 && game_player_entity == S16(game_cls8[ae->slot], 6))
                return;
        }
    }
    if (victim == game_player_entity && game_396E10 != 0)
        return;

    if (U16(sys, 3) == 0)
        goto credit;
    if (mode == 4)
        return;
    if (obj55_28927C(victim))
        amount *= 2;
    if (victim == game_player_entity)
        game_441367 += (ushort)amount;
    r = objdmg_rand_amount(amount);
    {
        uchar fac = objdmg_cls_parts[ve->cls];
        uchar *tab = objdmg_cls_tab[ve->cls];

        memset(pct, 0, 4);
        if (tab == NULL) {          /* never: the original tests the flags of tab's address
                                       computation, meant as "class 0" */
            mask = 1;
            pct[0] = 100;
        } else if (mode == 5) {
            /* the weakest part takes everything */
            short min = 0x7FFF;
            signed char sel = 0;
            for (i = 0; i < np; i++) {
                short v = S16(parts + 7 * i, 0);
                if (min > v) {
                    sel = (signed char)i;
                    min = v;
                }
            }
            mask = (uchar)(1u << ((uchar)sel & 31));
            if (sel < 4)
                pct[(long)sel] = 100;   /* (beyond 4: a local that is rewritten before use) */
        } else {
            signed char x = (signed char)(uchar)(dtype * fac * 2);
            uchar rem = 100, last = 0;
            mask = 0;
            for (i = 0; i < np; i++) {
                long idx = x + i;
                uchar bit = (uchar)(1u << (i & 31));
                uchar v = tab[idx];
                if (v == 0xFF) {
                    mask |= bit;
                    last = (uchar)i;
                } else if (v != 0) {
                    uchar p;
                    mask |= bit;
                    p = (uchar)RndRange(&game_rnd_35B704, v, tab[idx + fac]);
                    rem -= p;
                    if (i < 4)
                        pct[i] = p;
                }
            }
            if (last < 4)
                pct[last] = rem;
        }
    }

    m = mask;
    if (victim != game_player_entity && (long)U16(sys, 3) / 3 < r
        && game_rnd_35B704.f_Next(&game_rnd_35B704) % 100 < parts[6]) {
        total = 30000;      /* dodged: no part damage */
        goto credit;
    }

    if (m != 0) {
        uchar *part = parts;
        uchar *wend = wlist + nw * 15;
        i = 0;
        do {
            long d, dd, cp, mx;
            before += U16(part, 0);
            if (!(m & 1)) {
                after += U16(part, 0);
                goto next;
            }
            /* pct[] is 4 bytes; parts 4..7 read the next local, the dword i * 4 */
            d = (i < 4 ? pct[i] : i == 4 ? 16 : 0) * r;
            if (d > 100 && d < 50)
                d = 100;
            d /= 100;
            if (d <= 0) {
                after += U16(part, 0);
                goto next;
            }
            if (ve->cls == 1) {
                uchar *pb = (uchar *)&game_pba[ve->slot];
                if (S32(pb, 0xD) == 8 && B8(pb, 0x11) != 0) {
                    short k = (short)RndRange(&game_rnd_35B704, 50, 100);
                    short kk = (short)(k * d / 100);
                    d -= kk;
                    fire_292C2C(kk, victim);
                    if (victim == game_player_entity)
                        game_441391[i] = game_time + 300;
                }
            }
            d *= 10;
            cp = U16(part, 0);
            if (cp == 0) {
                dd = d;
                goto apply;
            }
            {
                long sub, v;
                if (mode == 5)
                    sub = 0;
                else {
                    sub = d / 2;
                    if (cp < sub)
                        sub = cp;
                }
                v = S16(part, 0) - (long)(ushort)sub;
                if (v < 0)
                    v = 0;
                U16(part, 0) = (ushort)v;
                if (ae->cls == 5 && (ae->type == 7 || ae->type == 0x17)) {
                    v = S16(part, 4) - (long)(ushort)sub;
                    if (v < 0)
                        v = 0;
                    U16(part, 4) = (ushort)v;
                }
            }
            after += U16(part, 0);
            mx = S16(part, 2);
            if (cp > mx * 0x49 / 100) {
                long q = d / 4;
                if (mode == 5)
                    dd = q;
                else if (d > cp * 2)
                    dd = d / 2;
                else if (d > cp / 2)
                    dd = q;
                else
                    dd = 0;
            } else {
                long t = (ushort)((mx - cp) * 100 / mx);
                dd = d * t;
                if (dd < 100 && dd > 50)
                    dd = 100;
                dd /= 100;
                if (cp > mx * 0x25 / 100 && cp < mx * 0x49 / 100)
                    dd /= 2;
                else
                    dd -= dd >> 2;
            }
        apply:
            dd /= 10;
            total += dd;
            if (((1u << (armor[0x14] & 31)) & mask) && game_396E18 == 0) {
                uchar ok = objacc_28B938(armor, dd);
                if (ok && ve->f21 != 0) {
                    uchar s = ve->f21;
                    Msg *msg = MsgAlloc();
                    if (msg) {
                        MSG_TO_ID = s;
                        aiacc_25AFC4(msg, objdmg_msg_from, objdmg_msg_to, ok);
                        MsgSend(msg);
                    }
                }
            }
            if (wlist && game_396E18 == 0) {
                uchar *w;
                long k;
                for (w = wlist, k = 0; w < wend; w += 15, k += 15) {
                    long n;
                    uchar ok;
                    if (w[1] != i)
                        continue;
                    n = RndRange(&game_rnd_35B704, 0, dd) / 3;
                    ok = weapon_use_ammo(victim, w, n);
                    if (ok && ve->f21 != 0) {
                        uchar s = ve->f21;
                        Msg *msg = MsgAlloc();
                        if (msg) {
                            MSG_TO_ID = s;
                            aiacc_25AFD4(msg, objdmg_msg_from, objdmg_msg_to, (uchar)(k / 15), ok);
                            MsgSend(msg);
                        }
                    }
                }
            }
        next:
            m >>= 1;
            i++;
            part += 7;
        } while (m != 0);
    }
    if (sys && game_396E18 == 0 && mode != 5) {
        objdmg_systems(victim, sys, nsys, r / 16);
        if (victim == game_player_entity && game_pba[game_player_slot].f00d == 5)
            hudview_26C5F4(0xC);
    }

credit:
    if (side != 0) {
        Msg *msg = MsgAlloc();
        if (msg) {
            MSG_TO_ID = side;
            aiacc_25AF34(msg, objdmg_msg_from, objdmg_msg_to, after, before, dtype);
            MsgSend(msg);
        }
    }
    if (U16(sys, 3) == 0) {
        objdmg_credit(victim, attacker, total, (long)before - (long)after, 0);
    } else {
        long v = (long)U16(sys, 3) - total;
        if (v < 0)
            v = 0;
        U16(sys, 3) = (ushort)v;
        if (total != 0 && victim == game_player_entity && game_pba[game_player_slot].f00d == 2)
            hudview_26C5F4(0xC);
        objdmg_credit(victim, attacker, total, (long)before - (long)after, U16(sys, 3) == 0);
        if (U16(sys, 3) == 0) {
            if (game_396E18 != 0 && victim == game_player_entity) {
                game_396E10 = 1;
                U16(sys, 3) = U16(sys, 1);
            } else if (game_396E18 == 2) {
                game_396E14 &= ~(1u << (ve->f23 & 31));
                helm_262A88(victim);
                sfx_28CB24(sfx_28C300(victim), victim);
                U16(sys, 3) = U16(sys, 1);
            } else {
                simtype_27B450(victim);
                if (mode == 1)
                    objdmg_death_add(victim, attacker);
                else
                    obj_destroy(victim, mode, (short)obj_owner(attacker));
            }
        }
    }
    if (victim == game_player_entity)
        objdmg_hit_sound(attacker, amount, dtype);
    if (game_359708 == 0x1D && victim == game_35B7EC && victim != 0) {
        if (game_35B72E++ > 2) {
            uchar *pb;
            long k;
            objdmg_drain((short)game_35B7EC, attacker, 9000);
            for (k = 5; k <= 0x14; k += 5) {
                short v = (short)RndRange(&game_rnd_35B704, 3, 0x5A);
                pb = (uchar *)&game_pba[game_entities[game_35B7EC].slot];
                S16(pb, 0x2F + k) = v;
            }
            for (k = 7; k <= 0x1C; k += 7) {
                short v = (short)RndRange(&game_rnd_35B704, 0x3C, 0x1F4);
                pb = (uchar *)&game_pba[game_entities[game_35B7EC].slot];
                S16(pb, 0xE + k) = v;
            }
        }
    }
}

/* destroy an entity now (class handlers) and tell the AI */
void obj_destroy(long ent, long mode, long owner)
{
    GameEntity *e = ENT(ent);
    uchar told = 0;

    switch (e->cls) {
    case 0: flash_28E350((short)ent, mode); break;
    case 1: pbasim_28A140((short)ent, mode); told = 1; break;
    case 2: vehsim_28EAA0((short)ent, mode); told = 1; break;
    case 3: aircraft_2844F4((short)ent); told = 1; break;
    case 4: instsim_288554((short)ent, mode); break;
    }
    if (told) {
        Msg *msg = MsgAlloc();      /* (the original also tests the brain byte against -1) */
        if (msg) {
            MSG_TO_ID = ENT(owner)->f21;
            aiacc_25AE60(msg, objdmg_msg_from, objdmg_msg_to, ENT(ent)->f21);
            MsgSend(msg);
        }
    }
}

/* game time at which ent dies when killed by killer: later the farther they are apart */
ulong objdmg_death_time(long ent, long killer)
{
    GameEntity *a = ENT(ent), *k = ENT(killer);

    return (ulong)fix_fast_pyth_dist(k->x - a->x, k->y - a->y) / 0xA5 + game_time;
}

void objdmg_death_add(long ent, long killer)
{
    long i, free = -1;
    ObjDeath *d;

    if (objdmg_death_count >= OBJDEATH_MAX)
        return;
    for (i = 0; i < OBJDEATH_MAX; i++) {
        short s = objdmg_deaths[i].ent;
        if ((short)ent == s)
            return;
        if (free == -1 && s == 0)
            free = i;
    }
    d = (ObjDeath *)((uchar *)objdmg_deaths + free * 8);
    d->ent = (short)ent;
    objdmg_death_count++;
    d->time = objdmg_death_time((short)ent, (short)killer);
    d->owner = obj_owner((short)killer);
}

void static_288040(long obj)
{
    long i;

    for (i = 0; i < OBJDEATH_MAX; i++) {
        if ((short)obj == objdmg_deaths[i].ent) {
            objdmg_deaths[i].ent = 0;
            objdmg_death_count--;
            return;
        }
    }
}

void static_28808C(ulong now)
{
    long i;

    if (objdmg_death_count == 0)
        return;
    for (i = 0; i < OBJDEATH_MAX; i++) {
        ObjDeath *d = &objdmg_deaths[i];
        if (d->ent != 0 && now > d->time) {
            obj_destroy(d->ent, 1, d->owner);
            static_288040(d->ent);
        }
    }
}

void objdmg_alert(long ent)
{
    ulong *t = (ulong *)damage_27D540((short)ent);

    if (t == NULL)
        return;
    if (*t < game_time)
        *t = game_time + 15000;
    else
        *t += 15000;
}

/* every entity: parts and hit points back to their maximum (cheat key) */
void objdmg_repair_all(void)
{
    short e;
    long k;

    for (e = game_entities[0].f05; e != 0; e = game_entities[e].f07) {
        GameEntity *o = &game_entities[e];
        uchar *q;
        switch (o->cls) {
        case 1:
            q = (uchar *)&game_pba[o->slot] + 6;
            U16(q, 0x2E) = U16(q, 0x2C);
            for (k = 1; k <= 4; k++)
                U16(q + 7 * k, 8) = U16(q + 7 * k, 0xA);
            break;
        case 2:
            q = (uchar *)&game_vehicles[o->slot] + 6;
            U16(q, 0xA) = U16(q, 8);
            U16(q, 0) = U16(q, 2);
            break;
        case 3:
            q = game_cls3[o->slot] + 6;
            U16(q, 0x18) = U16(q, 0x16);
            for (k = 1; k <= 3; k++)
                U16(q + 7 * k, -7) = U16(q + 7 * k, -5);
            break;
        case 4:
            q = game_cls4[o->slot] + 6;
            U16(q, 0x4E) = U16(q, 0x4C);
            for (k = 1; k <= 5; k++)
                U16(q + 7 * k, 3) = U16(q + 7 * k, 5);
            break;
        }
    }
}
