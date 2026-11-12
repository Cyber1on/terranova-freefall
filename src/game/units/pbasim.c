/* Terra Nova: r:\prj\tn\v109\tn\src\tn\pbasim.c (module M058) -- the PBA / soldier simulator.
 *
 *   0x289900 (data)  14-byte squad template copied by pbasim_28AD10 (not a function)
 *   0x289910 pbasim_289910  AI and helm notification for an entity (unused)
 *   0x289930 pbasim_289930  clear the falling-soldier list
 *   0x289948 pbasim_289948  apply the mission's starting damage and heading to the player
 *   0x289A64 pbasim_289A64  create the player's PBA
 *   0x289B3C pbasim_289B3C  freeze the game hooks when the player dies
 *   0x289B7C pbasim_289B7C  restore the game hooks
 *   0x289B94 pbasim_289B94  per-frame update of every PBA and the player's mission checks
 *   0x289D20 pbasim_289D20  per-frame update of one PBA
 *   0x28A05C pbasim_28A05C  AI hit callback: apply damage to a PBA
 *   0x28A140 pbasim_28A140  a PBA dies
 *   0x28A344 pbasim_28A344  16 status-display bytes for a damage level (helper of 0x28A500)
 *   0x28A500 pbasim_28A500  status-display bytes of the four parts (unused)
 *   0x28A534 pbasim_28A534  can the current weapon fire now
 *   0x28A5A0 pbasim_28A5A0  aim block and relative angles of a direction vector
 *   0x28A66C pbasim_28A66C  address of the +0x3FA block
 *   0x28A698 pbasim_28A698  cwde, then 0x28A69C
 *   0x28A69C pbasim_28A69C  arm of the current weapon (0 left, 1 right, -1 none)
 *   0x28A6F0 pbasim_28A6F0  muzzle position of the player's weapon
 *   0x28A79C pbasim_28A79C  aim error of the current weapon
 *   0x28A878 pbasim_28A878  fire the current weapon (0 fired, 6 not)
 *   0x28A9B8 pbasim_28A9B8  first system has no charge
 *   0x28A9C8 pbasim_28A9C8  sound callback: weapon ready sound
 *   0x28AA50 pbasim_28AA50  weapon change sound
 *   0x28AB1C pbasim_28AB1C  select a weapon
 *   0x28AB74 pbasim_28AB74  select the next / previous weapon
 *   0x28ABE0 PbaPlayerMove  limit a movement step to the unit's speed
 *   0x28AC5C pbasim_28AC5C  limit two speeds (unused)
 *   0x28ACD4 pbasim_28ACD4  speed factor of a load
 *   0x28AD10 pbasim_28AD10  create a strike squad (unused)
 *   0x28ADBC pbasim_28ADBC  hit chance of a shot
 *   0x28B498 pbasim_28B498  mission 0x1D: damage the speaker near the strike target
 *   0x28B5B0 pbasim_28B5B0  keep a PBA inside the map
 *   0x28B654 pbasim_28B654  map edge warnings (1 = left the map)
 *   0x28B778 pbasim_28B778  fire the first weapon of kind 8 straight ahead
 *
 * Quirks kept:
 *  - pbasim_289D20 advances falling soldiers with game_frame_dt, not its dt argument;
 *  - pbasim_28A5A0 does not check the vector pointer (instsim_288C64 does);
 *  - pbasim_28A79C returns the slot, pbasim_28A878 6 with the weapon address in the upper
 *    bits, or the weapon_ready result, as the original leaves them in eax;
 *  - pbasim_28AD10 reads ecx uninitialised when aiperc_253AE8 returns 0 (the C takes it as a
 *    fourth argument);
 *  - pbasim_28ADBC writes game_391E4C back with the value it read (no effect).
 */
#include "g3_c.h"
#include "g3_b.h"
#include "g4_a.h"      /* AI callees (owners g4a/g4b/g4c1/g4c2; G4 merge) */
#include "g6_a.h"     /* added at the G5-G8 merge */
#include "g6_c.h"     /* added at the G5-G8 merge */
#include "g7_b.h"     /* added at the G5-G8 merge */
#include "g8_a.h"     /* added at the G5-G8 merge */
#include "g8_b.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
short pba_dying[4];
short pba_end_timer;
ulong pba_saved_mask;
uchar pba_edge_flags;
ulong pba_targ_lost_time;
long game_35A95C;
uchar game_392490;
long pba_enemy_count;
long game_440930;
long pba_snd_count;
long pba_snd_chan;
long pba_snd_chan2;
long pba_speed_tab[][2] = {{0, 0x64}, {0x14D, 0x5A}, {0x166, 0x50}, {0x175, 0x46}, {0x1AE, 0x3C},
                           {0x1EA, 0x2D}, {0x226, 0x1E}, {0x262, 0xF}, {0x7FFFFFFF, 0xF}};
uchar game_35CADA;
schar voice_soldier[0x100];
uchar game_39034D[0x1EE * 0x10];
char pbasim_file[] = "r:\\prj\\tn\\v109\\tn\\src\\tn\\pbasim.c";
#endif

#define IDX15(w) ((short)((short)((ushort)(w) << 1) >> 1))
#define W(p, o) (*(short *)((uchar *)(p) + (o)))
#define UW(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define L(p, o) (*(long *)((uchar *)(p) + (o)))
#define PBA_ENT(s) IDX15(game_pba[(short)(s)].f000)

#ifndef TN_WATCOM_ABI
void port_arms_fired(void);             /* port/mods/arms.c: the player fired (muzzle flash, recoil) */
int port_arms_shot(long kind, fix *x, fix *y, fix *z);  /* port/video/sharp3d.c: the arm's muzzle, or 0 */

/* port/mods/arms.c: where the player's selected weapon sits (its record's byte 1, as pbasim_28A6F0
 * places the shot: 2 the left arm, 3 the right), 0 with none */
int port_player_weapon_mount(void)
{
    uchar *p, *w;
    schar sel;

    if (game_player_slot < 0)
        return 0;
    p = PBA_REC(game_player_slot);
    sel = (schar)p[0xCE];
    if (sel < 0)
        return 0;
    w = p + 0x45 + sel * 15;
    return *w ? w[1] : 0;
}
#endif
#define FIXDIV(a, b) ((fix)((((long long)(long)(a)) << 16) / (long)(b)))
#define PCT(c, n) ((c) = (short)(((long)(c) * (n)) / 100))
#define SPK(k) ((uchar *)game_391E50 + (k) * 0x32)

static const uchar parm_289900[14] = {0x00, 0xFF, 0x64, 0x64, 0x64, 0x64, 0x64,
                                      0x64, 0x1A, 0x16, 0x1A, 0x16, 0x00, 0x00};

/* speaker record of an entity, -1 if none */
static schar spk_find(short ent)
{
    schar k;
    for (k = 0; k < game_391E4C; k++)
        if (W(SPK(k), 2) == ent)
            break;
    if (k == game_391E4C)
        k = -1;
    return k;
}

static uchar spk_flag(schar k)
{
    schar s = voice_soldier[SPK(k)[6]];
    return game_39034D[s * 0x1EE];
}

void pbasim_289910(long ent, long b)
{
    aimain_249D18((short)ent, (uchar)b);
    helm_2627F8((short)ent, (uchar)b);
}

void pbasim_289930(void)
{
    memset(pba_dying, 0, 8);
}

void pbasim_289948(void)
{
    long f, i;
    uchar *p, *b;

    if (game_359708 >= 0x2F)
        return;
    f = ResOpenResFile("resmiss.res", 0, 0);
    p = ResLock(0x6B0);
    b = p + *(long *)(p + 2 + game_359708 * 4);
    if (*b != 0) {
        for (i = 0; i < 8; i++, b++) {
            uchar *s = *(uchar **)(PBA_REC(game_player_slot) + 0x9C) + i * 5;
            W(s, 3) = (short)(((long)(schar)*b * UW(s, 1)) / 100);
        }
        for (i = 0; i < 4; i++, b++) {
            uchar *q = PBA_REC(game_player_slot) + 0x15 + i * 7;
            W(q, 0) = (short)(((long)W(q, 2) * (schar)*b) / 100);
        }
    }
    ResUnlock(0x6B0);
    p = ResLock(0x6BB);
    G3C_ENT(game_player_entity).heading = (ushort)((long)(schar)p[*(long *)(p + 2) + game_359708] << 12);
    ResUnlock(0x6BB);
    ResCloseFile(f);
}

void pbasim_289A64(long kind, long f21, uchar *tmpl)
{
    GameEntity *e;

    if (!damage_27D720(&game_player_entity, &game_player_slot, (uchar)f21, (uchar)kind, 0x30, tmpl))
        DbgReportError(1, "Couldn't init Player PBA object data!\n");
    e = &G3C_ENT(game_player_entity);
    e->x = *(fix *)&game_4413D0[0];
    e->y = *(fix *)&game_4413D0[4];
    if (e->x == 0 && e->y == 0) {
        e->x = 0x1000000;
        e->y = 0x1000000;
    }
    physics_2D3FB8(game_player_entity);
    pba_end_timer = 0;
    pba_saved_mask = 0;
    pba_edge_flags = 0;
    game_35B77C = 0;
    fire_next_time = 0;
    damage_27E0B4();
    pbasim_289948();
    objtype_2933FC(game_player_entity, 0x8000);
}

static void freeze_hooks(void)
{
    if (pba_saved_mask == 0) {
        game_35B77C = 1;
        weapons_290858();
        radio_stop_all();
        pba_saved_mask = game_hook_mask;
        game_hook_mask &= 0xFFFFFE2Ful; /* the original ANDs the low word */
    }
}

void pbasim_289B3C(void)
{
    freeze_hooks();
}

void pbasim_289B7C(void)
{
    ordn_goal_c = 1;
    game_hook_mask = pba_saved_mask;
}

void pbasim_289B94(ulong dt)
{
    short s = IDX15(game_pba[0].f000);

    game_392490 &= 0xFD;
    while (s != 0) {
        if (G3C_ENT(PBA_ENT(s)).active)
            pbasim_289D20(s, dt);
        s = game_pba[s].f002;
    }
    if (pba_end_timer != 0) {
        pba_end_timer = (short)(pba_end_timer - dt);
        if (pba_end_timer <= 0) {
            game_3970E0 = 1;
            ordn_goal_c = 1;
            game_hook_mask = pba_saved_mask;
            game_397B70 = 2;
        }
    }
    if (targ_current != 0) {
        uchar lvl = damage_27DF14(game_player_entity, 5);
        if (lvl < 0x28) {
            if (pba_targ_lost_time != 0) {
                if ((ulong)((lvl / 10 + 4) * 0x2EE) + pba_targ_lost_time < game_time)
                    targ_current = 0;
            } else
                pba_targ_lost_time = game_time;
        }
    }
    if (*(long *)&game_camera[0x24] != 0 && damage_27DF14(game_player_entity, 4) == 0)
        fire_292180();
    if (game_359708 == 0x1D)
        pbasim_28B498();
    if (game_35CD9C != 0)
        pbasim_28B5B0(game_player_slot, 0x200000);
    else if (pbasim_28B654(game_player_slot)) {
        game_35A95C = 1;
        ordn_goal_c = 1;
        game_35B77C = 1;
    }
}

void pbasim_289D20(long slot, ulong dt)
{
    uchar *p = PBA_REC(slot);
    short me = IDX15(W(p, 0));
    schar k = spk_find(me);
    long i, n;

    for (i = 0; i < 4; i++) {
        if (pba_dying[i] == me) {
            if (G3C_ENT(me).z >= 0xA0000) {
                KillObjMark(me, 1);
                pba_dying[i] = 0;
            } else {
                physics_2D467C(me);
                sfx_28CBD0(me, (fix)((((long long)(long)game_frame_dt) << 16) / 1000), 1000);
            }
            return;
        }
    }
    p[0xCF] &= 0xDF;
    if (L(p, 0xD) != -1)
        fire_292A68(me);
    if (!(p[0xCF] & 0x10))
        objacc_28B88C((ObjMeter *)(p + 0x86), L(p, 0xD) == 3 ? (long)(dt + (dt >> 1)) : (long)dt);
    weapon_nop();
    weapon_nop();
    weapon_nop();
    weapon_nop();
    if (UW(p, 0xA8) != 0 || p[0xA4] != 0)
        obj55_288F78(me, p + 0xA2, (ushort)dt, p + 0x86);
    if (k != -1) {
        L(p, 0xB6) = damage_27DC2C(p + 6, me);
        if (spk_flag(k) == 1)
            L(p, 0xB2) = L(p, 0xB6) - 0xC0000;
        else
            L(p, 0xB2) = damage_27DBD4(p + 6, me);
    }
    damage_27D1E4(me, (NearList *)(p + 0xB2));
    if ((short)slot == game_player_slot) {
        NearEntry *q = *(NearEntry **)(p + 0xBE);
        n = 0;
        for (; q->id != 0; q++) {
            GameEntity *e = &G3C_ENT(q->id);
            if (e->f21 != 0 && game_brains[e->f21].f005 == 2 && e->cls != 4)
                n++;
        }
        if (n != 0 && pba_enemy_count == 0) {
            if (L(ordn_mission_state, 0x818) == 0)
                L(ordn_mission_state, 0x818) = game_time / 1000;
            SndPlay(0x7E4, 0, 0x28, 1, -1, 0x40, 0);
        }
        pba_enemy_count = n;
    }
    if (game_35CD9C == 0 || !(game_442F1B & 0x20)) {
        for (i = 0; i < 4; i++) {
            uchar *q = p + 0x15 + i * 7;
            long v = W(q, 0) + (long)((ulong)game_pba_types[G3C_ENT(me).type][4] * dt / 1000);
            long m = W(q, 2);
            W(q, 0) = (short)(v < m ? v : m);
        }
    }
}

void pbasim_28A05C(void *ai)
{
    long ent, target, c, d, e;
    uchar *p;
    short me, att;

    aiacc_25AE98(ai, &ent, &target, &c, &d, &e);
    if (target == 0xFFF)
        return;
    me = (short)ent;
    p = PBA_REC(G3C_ENT(me).slot);
    att = (short)(target & 0x0FFF);
    obj_damage(me, att, (uchar)c, p + 0x15, 4, *(uchar **)(p + 0x9C), UW(p, 0xA0), p + 0x86,
               p + 0x45, 4, (ushort)d, (ushort)e);
    p[0xCF] |= 0x20;
    aimain_24A26C(G3C_ENT(me).f21, att);
}

void pbasim_28A140(long ent, long mode)
{
    short me = (short)ent;
    GameEntity *e = &G3C_ENT(me);
    uchar *p = PBA_REC(e->slot);
    uchar dying = 0;
    schar k;
    long i;

    if (me == game_player_entity) {
        vec3_282CFC(me, 0, 0, 0, -1);
        freeze_hooks();
        if (pba_end_timer == 0)
            pba_end_timer = 0x1388;
        sfx_28CB24(sfx_28C300(me), me);
        return;
    }
    if (L(p, 0x82) >= 0)
        snd_chan_stop(L(p, 0x82));
    if (e->type <= 2 || (e->type == 0xD && mfd_270E70() != 0)) {
        for (i = 0; i < 4; i++)
            if (pba_dying[i] == 0) {
                pba_dying[i] = me;
                dying = 1;
                break;
            }
    }
    if (!dying) {
        vec3_282CFC(me, 0, 0, 0, -1);
        KillObjMark(me, 1);
        sfx_28CB24(sfx_28C300(me), me);
        return;
    }
    if (game_359708 == 0x1E && mode == 0) {
        KillObjMark(me, 1);
        return;
    }
    k = spk_find(me);
    if (k != -1) {
        mfddata_26645C(me);
        radio_stop_voice((schar)SPK(k)[6]);
        RadioSpeak(k, 0x35);
        game_440930++;
    }
    aimain_249D18(me, 0);
}

uchar *pbasim_28A344(uchar *buf, long kind)
{
    static const uchar base[5] = {0x82, 0x4D, 0x41, 0x25, 0x33};
    long i;
    uchar k = (uchar)kind;

    if (k > 4)
        return buf;
    for (i = 0; i < 16; i++)
        buf[i] = (uchar)(base[k] + (k == 0 ? i : i / 2));
    return buf + 16;
}

void pbasim_28A500(long slot, uchar *buf)
{
    uchar *q = PBA_REC(slot) + 0x15;
    long i;

    for (i = 0; i < 4; i++, q += 7) {
        uchar lvl;
        if (game_441391[i] != 0)
            lvl = 4;
        else {
            long max = W(q, 2), cur = W(q, 0);
            if (cur > max * 0x49 / 100)
                lvl = 0;
            else if (cur < max * 0x25 / 100 && cur > 0)
                lvl = 2;
            else if (cur == 0)
                lvl = 3;
            else
                lvl = 1;
        }
        pbasim_28A344(buf + 0x20 + i * 0x20, lvl);
    }
}

uchar pbasim_28A534(long obj)
{
    uchar *p = PBA_REC(G3C_ENT(obj).slot);
    uchar *w = p + 0x45 + (schar)p[0xCE] * 15;

    if (*w == 0)
        return 0;
    if (weapon_ready(w, p + 0x86))
        return 0;
    return 1;
}

void pbasim_28A5A0(void *a, void *b, void *c, long d, void *e)
{
    uchar **out = a;
    ushort *hdg = b, *pitch = c;
    fix *v = e;
    short ent = (short)d;
    long n = pbasim_28A69C(ent);
    GameEntity *g = &G3C_ENT(ent);

    *out = PBA_REC(g->slot) + 0x3F2 + (n ? 4 : 0);
    *hdg = fix_atan2(v[1], v[0]);
    *pitch = (ushort)(fix_atan2(v[2], fix_safe_pyth_dist(v[0], v[1], pbasim_file, 0x35D)) - g->f1d);
    *hdg = (ushort)(g->heading - *hdg);
}

fix *pbasim_28A66C(long obj)
{
    return (fix *)(PBA_REC(G3C_ENT(obj).slot) + 0x3FA);
}

long pbasim_28A69C(long obj)
{
    uchar *p = PBA_REC(G3C_ENT(obj).slot);
    uchar t = p[0x46 + (schar)p[0xCE] * 15];

    if (t == 2)
        return 0;
    if (t == 3)
        return 1;
    return -1;
}

long pbasim_28A698(long obj)
{
    return pbasim_28A69C((short)obj);
}

void pbasim_28A6F0(long ent, long kind, fix *x, fix *y, fix *z)
{
    GameEntity *e;

    if (kind == 2) {
        *x = 0x1000;
        *y = -0x2000;
        *z = 0x2C00;
    } else if (kind == 3) {
        *x = 0x1000;
        *y = 0x2000;
        *z = 0x2C00;
    }
    e = &G3C_ENT(ent);
    simdisp_289730(x, y, z, e->heading, e->f1d, 0);
    *x += e->x;
    *y += e->y;
    *z += biped_27ED34((short)ent) + e->z;
}

/* muzzle position of the current weapon */
static void muzzle(short slot, uchar *w, fix *ox, fix *oy, fix *oz)
{
    if (slot == game_player_slot) {
#ifndef TN_WATCOM_ABI
        if (port_arms_shot(w[1], ox, oy, oz))     /* "Arms pack": from the arm's muzzle */
            return;
#endif
        pbasim_28A6F0(PBA_ENT(slot), w[1], ox, oy, oz);
    } else
        drawobj_29B2C0(slot, w[1], ox, oy, oz);
}

long pbasim_28A79C(long slot, long x, long y, long z, long target, fix *miss)
{
    uchar *p = PBA_REC(slot);
    uchar *w = p + 0x45 + (schar)p[0xCE] * 15;
    fix ox, oy, oz;

    if (*w == 0)
        return (short)slot;
    muzzle((short)slot, w, &ox, &oy, &oz);
    return weapon_aim_error(w, PBA_ENT(slot), ox, oy, oz, x, y, z, (short)target, miss);
}

long pbasim_28A878(long slot, long x, long y, long z, long target, long real, long flag)
{
    uchar *p = PBA_REC(slot);
    uchar *w = p + 0x45 + (schar)p[0xCE] * 15;
    fix ox, oy, oz;
    uchar ok;

    if (*w == 0)
        return ((ulong)(p + 0x45) & ~0xFFul) | 6;
    if ((uchar)real) {
        ok = weapon_ready(w, p + 0x86);
        if (ok)
            return ok;
    }
    if ((uchar)flag)
        z += 0x140000;
    muzzle((short)slot, w, &ox, &oy, &oz);
    if (weapon_fire(w, PBA_ENT(slot), ox, oy, oz, x, y, z, (short)target, p + 0x86, (uchar)real)) {
#ifndef TN_WATCOM_ABI
        if ((uchar)real && (short)slot == game_player_slot)
            port_arms_fired();
#endif
        return 0;
    }
    return 6;
}

uchar pbasim_28A9B8(uchar *p6)
{
    return W(*(uchar **)(p6 + 0x96), 3) == 0;
}

void pbasim_28A9C8(long chan, long user)
{
    uchar *p;
    uchar t;

    if (--pba_snd_count > 0)
        return;
    p = PBA_REC(game_player_slot);
    t = p[0x45 + (schar)p[0xCE] * 15];
    pba_snd_chan2 = SndPlay(UW(ordn_objtype[t], 0x28), 0, 0x28, 1, -1, user, 0);
    if (pba_snd_chan2 >= 0)
        snd_chan_set_handle_ptr(pba_snd_chan2, &pba_snd_chan2);
}

void pbasim_28AA50(void)
{
    uchar *p = PBA_REC(game_player_slot);
    long v = 4 - p[0x46 + (schar)p[0xCE] * 15];
    long pri = v * 42;

    if (pba_snd_chan >= 0) {
        snd_chan_clear_callbacks(pba_snd_chan);
        snd_chan_stop(pba_snd_chan);
        pba_snd_count--;
    }
    if (pba_snd_chan2 >= 0)
        snd_chan_stop(pba_snd_chan2);
    pba_snd_chan = SndPlay(0x7E7, 0, 0x28, 1, -1, pri, 0);
    if (pba_snd_chan >= 0) {
        snd_chan_set_handle_ptr(pba_snd_chan, &pba_snd_chan);
        pba_snd_count++;
        snd_chan_add_callback(pba_snd_chan, WATCOM_FN(pbasim_28A9C8), pri);
    }
}

void pbasim_28AB1C(uchar *p6, long idx)
{
    uchar i = (uchar)idx;
    uchar *w = p6 + i * 15;
    uchar t = w[0x3F];
    ulong v;

    if (t == 0)
        return;
    v = UW(ordn_objtype[t], 0x24) + game_time;
    p6[0xC8] = i;
    if (v > *(ulong *)(w + 0x49))
        *(ulong *)(w + 0x49) = v;
}

void pbasim_28AB74(uchar *p6, long dir)
{
    long b = (schar)p6[0xC8];

    do {
        b += dir;
        if (b == 4)
            b = 0;
        else if (b < 0)
            b = 3;
        if (p6[b * 15 + 0x3F] != 0)
            pbasim_28AB1C(p6, (uchar)b);
    } while (b != (schar)p6[0xC8]);
    if (p6 == PBA_REC(game_player_slot) + 6)
        pbasim_28AA50();
}

/* the unit's speed limit (0x28ABE0, 0x28AC5C) */
static fix speed_limit(uchar *p6)
{
    fix s = pbasim_28ACD4(L(p6, 0xC4));
    fix m = G3C_FIXMUL(L(p6, 0xC0), s);
    return G3C_FIXMUL(m, 0x20000);
}

void PbaPlayerMove(void *p, fix *dx, fix *dy)
{
    fix lim = FIXDIV(speed_limit(p), 0x20000);
    fix d = fix_pyth_dist(*dx, *dy);

    if (lim < d) {
        fix f = FIXDIV(lim, d);
        *dx = G3C_FIXMUL(*dx, f);
        *dy = G3C_FIXMUL(*dy, f);
    }
}

void pbasim_28AC5C(uchar *p6, fix *a, fix *b, long flag)
{
    fix m1 = FIXDIV(speed_limit(p6), 0x20000);
    fix m2 = FIXDIV(m1, 0x20000);

    *a = m1 < *a ? m1 : *a;
    if (flag)
        *b = m2 < *b ? m2 : *b;
}

fix pbasim_28ACD4(fix v)
{
    long k = v >> 16;
    long i = 0;

    while (k <= pba_speed_tab[i][0] || k > pba_speed_tab[i + 1][0])
        i++;
    return FIXDIV(pba_speed_tab[i][1] << 16, 0x640000);
}

void pbasim_28AD10(long a, long b, long c, long ecx_in)
{
    uchar parm[14];
    ulong xy = (ushort)game_4414C8 | ((ulong)(ushort)game_4414CC << 16);
    short s = (short)ecx_in;
    uchar r, v;

    memcpy(parm, parm_289900, 14);
    r = aiperc_253AE8(0, xy, 2);
    if (r)
        s = (short)ailow_24B8A0(0xC, r, game_4414C8, game_4414CC, parm);
    v = damage_27DBB8(0xC);
    game_brains[G3C_ENT(PBA_ENT(s)).f21].f009 = v;
}

/* terrain class under an entity (map cell word >> 6 & 0xE) */
static uchar pba_terrain(GameEntity *e)
{
    uchar *row = *(uchar **)(game_map_35BFB4 + (e->y >> 16) * 4 + 0x84E);
    return (uchar)((*(ushort *)(row + (e->x >> 16) * 3) >> 6) & 0xE);
}

static fix speed3(long ent)
{
    fix a, b, c;
    physics_2D3E74(ent, &a, &b, &c);
    return fix_fast_pyth_dist(fix_fast_pyth_dist(a, b), c);
}

uchar pbasim_28ADBC(long me, long target, fix dist)
{
    GameEntity *e = &G3C_ENT(me), *t = &G3C_ENT(target);
    short c;
    schar v, k;
    fix s;

    if (e->cls == 4 && e->type == 0x45)
        return 0x4B;
    switch (t->cls) {
    case 1: c = game_pba_types[t->type][0x1C]; break;
    case 2: c = game_veh_types[t->type][0xC]; break;
    case 3: c = game_cls3_types[t->type][0xE]; break;
    default: c = 50; break;
    }
    if (game_41DE34 == 4)
        PCT(c, 75);
    else if (game_35CADA == 0)
        PCT(c, 125);
    else if (game_35CADA == 1)
        PCT(c, 50);
    else if (game_35CADA == 0xFF)
        PCT(c, 150);

    v = pba_terrain(e);
    if (v < 4)
        PCT(c, 50);
    else if (v < 8)
        PCT(c, 100);
    else if (v > 12)
        PCT(c, 150);
    v = pba_terrain(t);
    if (v < 4)
        PCT(c, 150);
    else if (v > 12)
        PCT(c, 25);
    else if (v > 8)
        PCT(c, 50);

    s = speed3((short)target);
    if (s < 0x1000)
        PCT(c, 25);
    else if (s < 0x4000)
        PCT(c, 50);
    else if (s < 0x9000)
        PCT(c, 100);
    else
        PCT(c, 150);
    s = speed3((short)me);
    if (s < 0x1000)
        PCT(c, 100);
    else if (s < 0x4000)
        PCT(c, 75);
    else if (s < 0x9000)
        PCT(c, 50);
    else
        PCT(c, 25);

    if (dist > 0x200000)
        PCT(c, 25);
    else if (dist > 0x180000)
        PCT(c, 50);
    else if (dist > 0x100000)
        PCT(c, 70);

    k = spk_find((short)me);
    if (k != -1) {
        if (spk_flag(k) == 1)
            PCT(c, 250);
    } else if (aimain_24A760())
        PCT(c, 200);
    k = spk_find((short)target);
    if (k != -1 && spk_flag(k) == 1)
        PCT(c, 35);
    if (game_time < *damage_27D540((short)target))
        PCT(c, 150);
    return (uchar)c;
}

void pbasim_28B498(void)
{
    long i;

    game_35B7EC = 0;
    for (i = 0; i < 4; i++)
        if (SPK(i)[6] == 5 && W(SPK(i), 0) != 0)
            game_35B7EC = W(SPK(i), 2);
    if (game_35B7EC == 0)
        return;
    if (fix_fast_pyth_dist(game_4414C8 - G3C_ENT(game_35B7EC).x,
                           game_4414CC - G3C_ENT(game_35B7EC).y) >= 0x1E0000)
        return;
    objdmg_drain((short)game_35B7EC, (short)game_35B7EC, 0x2328);
    for (i = 0; i < 4; i++) {
        long r = RndRange(&game_rnd_35B704, 3, 0x5A);
        W(PBA_REC(G3C_ENT(game_35B7EC).slot), 0x34 + i * 5) = (short)r;
    }
    for (i = 0; i < 4; i++) {
        long r = RndRange(&game_rnd_35B704, 0x3C, 0x1F4);
        W(PBA_REC(G3C_ENT(game_35B7EC).slot), 0x15 + i * 7) = (short)r;
    }
}

void pbasim_28B5B0(long slot, fix margin)
{
    short ent = PBA_ENT(slot);
    fix x = G3C_ENT(ent).x, y = G3C_ENT(ent).y;
    fix nx = x, ny = y;

    if (x < margin)
        nx = margin;
    if (y < margin)
        ny = margin;
    if (x > 0x2000000 - margin)
        nx = 0x2000000 - margin;
    if (y > 0x2000000 - margin)
        ny = 0x2000000 - margin;
    if (nx != x || ny != y)
        physics_2D47D8(ent, nx, ny);
}

static uchar outside(fix x, fix y, fix m)
{
    return x < m || y < m || x > 0x2000000 - m || y > 0x2000000 - m;
}

uchar pbasim_28B654(long slot)
{
    GameEntity *e = &G3C_ENT(PBA_ENT(slot));
    fix x = e->x, y = e->y;
    uchar warn = x < 0x190000 || y < 0x190000 || x > 0x1E70000 || y > 0x1E70000;
    uchar near = outside(x, y, 0x160000);
    uchar out = outside(x, y, 0xF0000);

    if (out)
        return 1;
    if (near) {
        if (!(pba_edge_flags & 2)) {
            RadioMessage(0x45);
            pba_edge_flags |= 2;
        }
        return 0;
    }
    if (warn) {
        if (!(pba_edge_flags & 1)) {
            RadioMessage(0x44);
            pba_edge_flags |= 1;
        }
        return 0;
    }
    pba_edge_flags = 0;
    return 0;
}

void pbasim_28B778(long ent)
{
    GameEntity *e = &G3C_ENT(ent);
    uchar *p6 = PBA_REC(e->slot) + 6;
    long i;
    fix x, y;

    for (i = 0; i < 4; i++)
        if (p6[i * 15 + 0x3F] == 8) {
            p6[0xC8] = (uchar)i;
            break;
        }
    if (i == 4)
        return;
    x = e->x + G3C_FIXMUL(0xC80000, fix_cos(e->heading));
    y = e->y + G3C_FIXMUL(0xC80000, fix_sin(e->heading));
    pbasim_28A878(e->slot, x, y, 0x50000, 0, 1, 0);
}
