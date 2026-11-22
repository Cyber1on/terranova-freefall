/* aimain.c -- Terra Nova AI main loop and helpers (module M005; part of the AI code whose
 * low-level half is r:\prj\tn\v109\tn\src\tn\ailow.c).  Decompiled from __FF.EXE v1.09.
 *
 *   0x249A80 aimain_249A80  AI init: reset, install frame hook 3, kill callbacks
 *   0x249B1C aimain_249B1C  AI shutdown: reset, remove frame hook 3
 *   0x249B2C aimain_249B2C  flush every mailbox, clear sides / squads / brains / paths
 *   0x249D18 aimain_249D18  kill callback (classes 1-4): forget a dead entity
 *   0x249DCC aimain_249DCC  clear brain +0x1A before a frame
 *   0x249E24 AISquadLoop    frame hook 3: squads, sides, every brain's think
 *   0x249F48 aimain_249F48  init a brain's 3 order records
 *   0x249F6C aimain_249F6C  remove the orders whose kind matches a mask
 *   0x249FD0 aimain_249FD0  sort the orders by priority (+1), highest first
 *   0x24A050 aimain_24A050  find / add the order of a kind
 *   0x24A088 aimain_24A088  find the order of a kind (3 = none)
 *   0x24A0B8 aimain_24A0B8  find / add the "new" order (+2 == 1)
 *   0x24A0F4 aimain_24A0F4  find the "new" order (3 = none)
 *   0x24A120 aimain_24A120  allocate a path record
 *   0x24A158 aimain_24A158  free a path record
 *   0x24A184 aimain_24A184  player side defects: squads of side 1 move to side 3
 *   0x24A26C aimain_24A26C  a brain was hit by a shell / blast: tell both squads
 *   0x24A3E0 aimain_24A3E0  line of fire between two entities / points (EDMS ray)
 *   0x24A65C aimain_24A65C  debug dump of a path (no refs; result discarded)
 *   0x24A6A0 aimain_24A6A0  a squad mate of the player reports something (message 0x12)
 *   0x24A73C aimain_24A73C  the player is dead (once)
 *   0x24A760 aimain_24A760  ... was it
 *   0x24A768 aimain_24A768  kill callback: an entity is gone, tell every brain and squad
 *   0x24A7B8 aimain_24A7B8  can a squad act (count < 6, or ask aiperc_253AE8)
 *
 * Order records (brain +0x30, 3 x 0x24 bytes): +0 kind (bit mask), +1 priority, +2 1 = new,
 * +3 .. data (+3 short x / y squares, +7 / +0xB fix x / y, +0xF byte, +0x12 byte).
 * Quirks kept: aimain_24A050 / 24A088 never match the third record; aimain_24A184 uses the
 * caller's bl when no player brain is found (0 here); aimain_249DCC clears +0x1A of brain i for
 * every active squad i; aimain_24A3E0 only handles class 5 / 8 attackers in 24A26C (other
 * attacker classes are ignored).
 */
#include "g4_a.h"
#include "g6_b.h"     /* added at the G5-G8 merge */
#include "g8_b.h"     /* added at the G5-G8 merge */
#include "g9_c.h"     /* added at the G9/G10 merge */

#ifndef TN_LINK_ORIGINAL_DATA
uchar ai_path_used[0x50];
uchar ai_paths[0x50][0x309];
ulong ai_371170;
uchar ai_player_dead;
ulong ai_371180;
long ai_371184;
#endif

#define ORD(p, i) ((uchar *)(p) + (i) * 0x24)

void aimain_249A80(void)
{
    aimain_249B2C();
    game_hooks[3] = (GameHook)WATCOM_FN(AISquadLoop);
    *(uchar *)&game_hook_mask |= 8;
    killobj_28952C(1, WATCOM_FN(aimain_249D18), 0xFFFFFFFF);
    killobj_28952C(2, WATCOM_FN(aimain_249D18), 0xFFFFFFFF);
    killobj_28952C(4, WATCOM_FN(aimain_249D18), 0xFFFFFFFF);
    killobj_28952C(3, WATCOM_FN(aimain_249D18), 0xFFFFFFFF);
    aiglue_253810();
    aimsg_25A110();
    ai_player_dead = 0;
    ai_371170 = 0;
    ai_371184 = 0;
    ai_371180 = 0;
}

void aimain_249B1C(void)
{
    aimain_249B2C();
    *(uchar *)&game_hook_mask &= 0xF7;
}

void aimain_249B2C(void)
{
    int i;

    for (i = 0; i < GAME_MAX_SIDES; i++)
        MsgFlush((MsgAddr *)(G4_SIDE(i) + 1));
    for (i = 0; i < GAME_MAX_SQUADS; i++)
        MsgFlush((MsgAddr *)(G4_SQUAD(i) + 3));
    for (i = 0; i < GAME_MAX_BRAINS; i++)
        MsgFlush((MsgAddr *)(G4_BRAIN(i) + 1));
    for (i = 0; i < GAME_MAX_SIDES; i++)
        game_sides[i].active = 0;
    for (i = 0; i < GAME_MAX_SQUADS; i++) {
        game_squads[i].active = 0;
        game_squads[i].f001 = 0;
    }
    for (i = 0; i < GAME_MAX_BRAINS; i++)
        game_brains[i].active = 0;
    /* free lists: squad 0 / brain 0 head them */
    for (i = 0; i < GAME_MAX_SQUADS; i++)
        game_squads[i].next = (uchar)(i + 1);
    game_squads[GAME_MAX_SQUADS - 1].next = 0;
    for (i = 0; i < GAME_MAX_BRAINS; i++)
        game_brains[i].next = (uchar)(i + 1);
    game_brains[GAME_MAX_BRAINS - 1].next = 0;
    memset(ai_path_used, 0, 0x50);
    for (i = 0; i < 0x50; i++)
        ai_paths[i][0] = 0;
}

void aimain_249D18(long ent_arg, long reason)
{
    short ent = (short)ent_arg;
    uchar b = G4_ENT(ent).f21;
    int i;

    if (b && game_brains[b].active)
        ailow_2534FC(b);
    for (i = 0; i < GAME_MAX_SQUADS; i++)
        if (game_squads[i].active && G4_S16(G4_SQUAD(i), 0xBF) == ent)
            G4_S16(G4_SQUAD(i), 0xBF) = 0;
    for (i = 0; i < GAME_MAX_BRAINS; i++) {
        uchar *br = G4_BRAIN(i);
        if (br[0] && (ushort)br[0xCA] == (ushort)ent) {
            br[0xCA] = 0;
            br[0xC3] = 0;
            br[0xC9] = 0;
            br[0xCB] = 0;
            G4_S16(br, 0xCC) = -1;
            G4_S16(br, 0xCE) = -1;
        }
    }
}

void aimain_249DCC(void)
{
    int i;

    /* indexes the brains with the squad index (original) */
    for (i = 0; i < GAME_MAX_SQUADS; i++)
        if (game_squads[i].active)
            G4_U8(G4_BRAIN(i), 0x1A) = 0;
    for (i = 0; i < GAME_MAX_BRAINS; i++)
        if (game_brains[i].active || game_brains[i].alt_think)
            G4_U8(G4_BRAIN(i), 0x1A) = 0;
}

#ifndef TN_WATCOM_ABI
extern int port_ai_tick25;              /* port/mods/ai.c */
void port_ai_frame(void);
#endif

void AISquadLoop(void)
{
    int i, s;

#ifndef TN_WATCOM_ABI
    {   /* "AI: Fixed" (G60): this frame begins a new 40 ms of game time (one frame of the game's 25) */
        static ulong step;
        ulong now = game_time / 40;
        port_ai_tick25 = now != step;
        step = now;
        port_ai_frame();
    }
#endif
    aiglue_253814();
    aimain_249DCC();
    for (i = 0; i < GAME_MAX_SQUADS; i++)
        if (game_squads[i].active && game_squads[i].f010 == 0)
            aiperc_259E3C(i);
    for (s = 0; s < GAME_MAX_SIDES; s++) {
        GameSide *side = &game_sides[s];
        ulong sq;

        if (side->active || side->f007)
            aisquad_24A9B8((uchar *)side, side->f007);
        if (!side->active && !side->f007)
            continue;
        for (sq = side->first_squad; sq; sq = game_squads[sq].next) {
            uchar b;

            if (!game_squads[sq].active)
                continue;
            b = game_squads[sq].first_brain;
            aiperc_2563D0(&game_squads[sq]);
            while (b) {
                if (game_brains[b].alt_think)
                    ailow_24E040((uchar *)&game_brains[b]);
                else if (game_brains[b].active)
                    AISoldierThink((uchar *)&game_brains[b]);
                b = game_brains[b].next;
            }
        }
    }
}

void aimain_249F48(uchar *ords)
{
    int i;

    ords[0] = 1;
    ords[1] = 1;
    for (i = 1; i < 3; i++) {
        ORD(ords, i)[0] = 0;
        ORD(ords, i)[1] = 0;
    }
}

void aimain_249F6C(uchar *ords, long mask)
{
    long i, j;

    for (i = 0; i < 3; i++) {
        if ((long)(ORD(ords, i)[0] & (uchar)mask) > 0) {
            for (j = i; j < 2; j++)
                memcpy(ORD(ords, j), ORD(ords, j + 1), 0x24);
            ords[0x48] = 0;
            ords[0x49] = 0;
            i--;
        }
    }
}

void aimain_249FD0(uchar *ords)
{
    long i, c, best, bestp;
    uchar tmp[0x24];

    for (i = 0; i < 2; i++) {
        best = i;
        bestp = ORD(ords, i)[1];
        for (c = i + 1; c < 3; c++) {
            if ((long)ORD(ords, c)[1] > bestp) {
                bestp = ORD(ords, c)[1];
                best = c;
            }
        }
        if (best != i) {
            memcpy(tmp, ORD(ords, i), 0x24);
            memcpy(ORD(ords, i), ORD(ords, best), 0x24);
            memcpy(ORD(ords, best), tmp, 0x24);
        }
    }
}

long aimain_24A050(uchar *ords, long kind_arg)
{
    uchar kind = (uchar)kind_arg;
    long i = 0;

    if (kind == ords[0])
        return 0;
    for (;;) {
        if (ORD(ords, i)[0] == 0)
            break;
        i++;
        if (i >= 2)
            break;
        if (kind == ORD(ords, i)[0])
            return i;
    }
    ORD(ords, i)[2] = 0;
    ORD(ords, i)[0] = kind;
    return i;
}

long aimain_24A088(uchar *ords, long kind_arg)
{
    uchar kind = (uchar)kind_arg;
    long i = 0;

    if (kind == ords[0])
        return 0;
    for (;;) {
        if (ORD(ords, i)[0] == 0)
            return 3;
        i++;
        if (i >= 2)
            return 3;
        if (kind == ORD(ords, i)[0])
            return i;
    }
}

long aimain_24A0B8(uchar *ords)
{
    long i = 0;

    if (ords[0]) {
        for (;;) {
            if (ORD(ords, i)[2] == 1)
                return i;
            i++;
            if (i >= 2)
                break;
            if (ORD(ords, i)[0] == 0)
                break;
        }
    }
    ORD(ords, i)[2] = 1;
    ORD(ords, i)[0x12] = 0;
    return i;
}

long aimain_24A0F4(uchar *ords)
{
    long i = 0;

    for (;;) {
        if (ORD(ords, i)[2] == 1)
            return i;
        i++;
        if (i >= 2)
            return 3;
    }
}

uchar *aimain_24A120(void)
{
    signed char i;

    for (i = 0; i < 0x50; i++) {
        if (!ai_path_used[i]) {
            ai_path_used[i] = 1;
            ai_paths[i][0] = 0;
            return ai_paths[i];
        }
    }
    return 0;
}

void aimain_24A158(uchar *path)
{
    if (path) {
        long i = (long)(path - &ai_paths[0][0]) / 0x309;
        ai_path_used[i] = 0;
        *path = 0;
    }
}

void aimain_24A184(void)
{
    uchar sq, b, alt = 0;   /* original: the caller's bl when no player brain is found */
    ulong xy;

    aisquad_24A820(3);
    sq = game_sides[1].first_squad;
    game_sides[3].first_squad = sq;
    game_sides[1].first_squad = 0;
    while (sq) {
        G4_U8(G4_SQUAD(sq), 0xD) = 3;
        game_squads[sq].f007 = 3;
        for (b = game_squads[sq].first_brain; b; b = game_brains[b].next) {
            if (game_brains[b].alt_think) {
                alt = b;
            } else {
                game_brains[b].f005 = 3;
                game_brains[b].f006 = sq;
            }
        }
        sq = game_squads[sq].next;
    }
    xy = G4_U32(G4_BRAIN(alt), 0xE);
    ailow_2534A4(alt, aiperc_253AE8(0, xy, 1));
    strike_aborted = 1;
    game_35BF48 = 1;
    strike_active = 1;
    game_35B6E0 = 0;
    mfddata_266A4C();
}

void aimain_24A26C(long brain_arg, long attacker_arg)
{
    uchar brain = (uchar)brain_arg;
    short att = (short)attacker_arg;
    uchar cls, ab;
    uchar *me, *him;
    Msg *m;

    if (!att)
        return;
    cls = G4_ENT(att).cls;
    if (cls == 5)
        ab = G4_ENT(*(short *)&game_cls5[G4_ENT(att).slot][6]).f21;
    else if (cls == 8)
        ab = G4_ENT(*(short *)&game_cls8[G4_ENT(att).slot][6]).f21;
    else
        return;
    if (!ab)
        return;
    him = G4_BRAIN(ab);
    me = G4_BRAIN(brain);
    if (him[5] == me[5])
        return;
    m = MsgAlloc();
    if (m) {
        aiacc_25AE48(m, G4_U32(him, 1), G4_U32(G4_SQUAD(me[6]), 3), him[3], me[3]);
        MsgSend(m);
    }
    m = MsgAlloc();
    if (m) {
        aiacc_25AE48(m, G4_U32(him, 1), G4_U32(me, 1), him[3], me[3]);
        MsgSend(m);
    }
}

uchar aimain_24A3E0(long e1_arg, long e2_arg, fix x1, fix y1, fix z1, fix x2, fix y2, fix z2)
{
    short e1 = (short)e1_arg, e2 = (short)e2_arg;
    fix p1[3], p2[3], dir[3];
    fix dx, dy, dz, len, inv, range;
    long ginfo, winfo, v;
    bool hit;
    physics_handle ph;

    if (e1)
        interact_298854(e1, p1);
    else {
        p1[0] = x1;
        p1[1] = y1;
        p1[2] = z1;
    }
    if (e2)
        interact_298854(e2, p2);
    else {
        p2[0] = x2;
        p2[1] = y2;
        p2[2] = z2;
    }
    dx = p2[0] - p1[0];
    dy = p2[1] - p1[1];
    dz = p2[2] - p1[2];
    if ((dx < 0 ? -dx : dx) >= 0x640000 || (dy < 0 ? -dy : dy) >= 0x640000 ||
        (dz < 0 ? -dz : dz) >= 0x640000)
        return 0;
    p1[0] *= 6;              /* EDMS units */
    p1[1] *= 6;
    p1[2] *= 6;
    len = fix_sqrt(G4_FIXMUL(dz, dz) + (G4_FIXMUL(dx, dx) + G4_FIXMUL(dy, dy)));
    if (len < 0x1000)
        return 1;
    inv = G4_FIXDIV(0x10000, len);
    dir[0] = G4_FIXMUL(dx, inv);
    dir[1] = G4_FIXMUL(dy, inv);
    dir[2] = G4_FIXMUL(dz, inv);
    range = len * 6;
    if (e1)
        physics_2D777C(0, e1);
    /* the shooter's own body is excluded */
    ph = EDMS_cast_projectile(p1, dir, 0x10000, 0, 0x4000, range,
                              G4_ENT(e1).body, G4_ENT(e1).body, &ginfo, &winfo, &hit);
    if (e1)
        physics_2D777C(0, 0);
    if (ph < 1 && !hit)
        return 1;
    if (e2 && ph == G4_ENT(e2).body)
        return 1;
    v = ginfo ? ginfo : winfo;
    if ((ulong)v >> 31)
        return 0;
    if ((v & 0xFFF) == e2)
        return 1;
    return 0;
}

void aimain_24A65C(uchar *path)
{
    /* builds an "H"/"L" string of the path's nodes on the stack and drops it (debug) */
    char buf[0x104];
    long i, n;

    if (!path[0])
        return;
    n = G4_S16(path, 7);
    for (i = 0; i <= n; i++)
        if (i < (long)sizeof(buf))
            buf[i] = (path[3 * i + 0xB] & 4) ? 'H' : 'L';
    if (i < (long)sizeof(buf))
        buf[i] = 0;
    (void)buf;
}

void aimain_24A6A0(long brain_arg, long what)
{
    uchar brain = (uchar)brain_arg;
    uchar *br;
    Msg *m;
    MsgAddr from, to;

    if (!brain)
        return;
    br = G4_BRAIN(brain);
    if (br[0xD] != 1 || br[5] != 1 || br[0xA])
        return;
    m = MsgAlloc();
    if (!m)
        return;
    from.a = 0x80;
    from.b = brain;
    to.a = 0x80;
    to.b = G4_ENT(game_player_entity).f21;      /* the player's brain */
    aiacc_25AEC8(m, *(ulong *)&from, *(ulong *)&to, brain, (uchar)what);
    MsgSend(m);
}

void aimain_24A73C(void)
{
    if (ai_player_dead == 1)
        return;
    ai_player_dead = 1;
    misobj_2C9FD4();
    if (game_41DE33 < 3)
        return;
    flash_28E630();
}

uchar aimain_24A760(void)
{
    return ai_player_dead;
}

void aimain_24A768(long ent)
{
    uchar i;

    for (i = 0; i < GAME_MAX_BRAINS; i++)
        if (game_brains[i].active)
            ailow_2537AC(i, (short)ent);
    for (i = 0; i < GAME_MAX_SQUADS; i++)
        if (game_squads[i].active)
            aiperc_259F3C(i, (short)ent);
}

uchar aimain_24A7B8(long squad_arg, ulong xy, long c)
{
    uchar sq = (uchar)squad_arg;

    if (!game_squads[sq].f001 || !sq)
        return 0;
    if (game_squads[sq].active)
        return game_squads[sq].f010 < 6;
    return aiperc_253AE8(sq, xy, (signed char)c) != 0;
}
