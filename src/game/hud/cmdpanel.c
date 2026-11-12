/* cmdpanel.c -- Terra Nova module M015 (0x25EF10-0x26147F): the squad order panel of the HUD
 * (the MFD order menus, the command record they fill in and the dispatch of a command to the
 * squad members' AI).  Decompiled from __FF.EXE v1.09.
 *
 * Command record (16 bytes; the current one at cmd_389BC0, copies on the stack):
 *   +0 ushort command, +2 ushort flags (0x200 whole team, 0x400 one member, high byte: 2 all
 *   squads, 4 / 8 own squad, 0x10 per member, 0x20 script key), +4 short member entity,
 *   +6 short squad, +8 short target entity or x, +0xA short y, +0xC long.
 * Menus are MfdMenu (g8_a.h: back link, MfdItem list); mfd_38FC87 is the menu shown.
 *
 *   0x25EF10 data (4 zero bytes): default packed position of cmd_25F9E4
 *   0x25EF14 open a menu for a member          0x25F044 close the panel
 *   0x25F0AC set the close timer               0x25F0C0 member button: open its menu
 *   0x25F138 call a strike                     0x25F2B8 timer: close the panel
 *   0x25F33C build a menu of names             0x25F3B8 menu key
 *   0x25F7E8 run a menu line                   0x25F9AC packed entity position (unused)
 *   0x25F9E4 target position of a command      0x25FB30 order to the nearest member's squad
 *   0x25FD80 mission objective entity          0x25FFC8 mission ordnance / objective by kind
 *            (0x25FF6C, 0x25FFA4, 0x25FFB4 jump tables; 0x25FFFA .. 0x260150 its case labels)
 *   0x26016C / 26018C "line disabled" tests     0x2601A8 apply the flag table (unused)
 *   0x2601D8 issue the current command         0x2603C4 dispatch a command record
 *   0x260BFC menu for a member's state (0x260BE4 jump table)
 *   0x260D10 text of a menu line (0x260C68 jump table)
 *   0x261320 player near the strike target     0x261370 run a line for the selected member
 *   0x2613E0 member shortcut keys
 */
#include "g8_c.h"

#define MEMBER(i) ((uchar *)game_391E50 + (long)(i) * 0x32)
#define M_ENT(i) (*(short *)(MEMBER(i) + 2))
#define M_SLOT(i) (*(short *)(MEMBER(i) + 4))
#define M_VOICE(i) (MEMBER(i)[6])
#define M_CHAN(i) (*(schar *)(MEMBER(i) + 7))
#define CENT(i) (game_entities[(short)(i)])
#define BRAIN_SQUAD(b) (((uchar *)&game_brains[0])[(long)(b) * 0x14E + 6])
#define SQUAD_FIRST(s) (((uchar *)&game_squads[0])[(long)(s) * 0x170 + 0xF])
#define PBA_ENT(slot) ((short)(*(short *)&game_pba[(short)(slot)] << 1) >> 1)
#define REC (cmd_389BC0)
#define R16(p, o) (*(short *)((uchar *)(p) + (o)))
#define RU16(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define R32(p, o) (*(long *)((uchar *)(p) + (o)))

/* the "close the panel" sequence the original inlines in many places */
static void close_panel(void)
{
    hud_38FC80 = 0;
    RU16(REC, 2) = 0;
    hud_38FFFC = game_player_entity;
    if (game_38FC12[0][0] == 2)
        hudview_26C224(0, cmd_3599D8);
    if (game_38FC12[0][0] == 6)
        mfd_26DF10();
    hudview_26CB14();
    hudview_26C5B4(7);
    mapd_390308 = 0;
}

/* member index of the record's entity, -1 if none (schar loop like the original) */
static schar member_of(short ent)
{
    schar i;

    for (i = 0; i < game_391E4C; i++)
        if (ent == M_ENT(i))
            break;
    if (i == game_391E4C)
        i = -1;
    return i;
}

void cmd_25EF14(long member, MfdMenu *menu)
{
    short m = (short)member;

    hud_38FC80 = 1;
    RU16(REC, 2) = 0;
    if (m < game_391E4C) {
        mfd_38FC81 = M_ENT(m);
        if (m >= 0)
            cmd_38FC83 = BRAIN_SQUAD(CENT(PBA_ENT(M_SLOT(m))).f21);
        REC[3] |= 4;
    } else {
        mfd_38FC81 = M_ENT(mfd_3599D4);
        cmd_38FC83 = BRAIN_SQUAD(CENT(M_ENT(mfd_3599D4)).f21);
        RU16(REC, 2) = 0x200;
    }
    mfd_38FC87 = menu;
    cmd_38FC85 = 0;
    cmd_389BD8 = 0;
    hudview_26C224(1, 7);
    mapd_390308 = 0;
}

void cmd_25F044(void)
{
    close_panel();
}

void cmd_25F0AC(long a)
{
    cmd_389BD8 = game_time + a;
}

void cmd_25F0C0(long member, long on)
{
    MfdMenu *menu = &cmd_menus[5];

    SndPlay(0x7E3, 0, 0x40, 1, -1, 0x40, 0);
    if ((short)member < game_391E4C)
        menu = cmd_260BFC((short)member);
    cmd_25EF14((short)member, menu);
    if ((short)member < game_391E4C)
        mfd_3599D4 = (short)member;
    if ((uchar)on)
        hudview_26C224(0, 2);
}

/* strike request: refused while cheats are on, during an air strike in progress, after the
 * strike or while an ordnance strike is pending; else radio and an AI message */
void cmd_25F138(void)
{
    uchar a = 0, b = 0, c = 0;
    Msg *m;

    if (game_35CD9C)
        return;
    if (air_35B6D0 && !game_35B6D4)
        return;
    if (game_35BF48) {
        RadioMessage(0x47);
        return;
    }
    if ((game_359708 == 0x10 || game_359708 == 0x1E) && cmd_261320())
        strike_35BF55 = 0;
    if ((ordn_flags[0] && ordn_mission_state[0x809] == 1) || ordn_mission_state[0x809] == 3)
        a = 1;
    if ((ordn_flags[1] && ordn_mission_state[0x80A] == 1) || ordn_mission_state[0x80A] == 3)
        b = 1;
    if ((ordn_flags[2] && ordn_mission_state[0x80B] == 1) || ordn_mission_state[0x80B] == 3)
        c = 1;
    if (a || b || c || game_35B6D4)
        strike_35BF55 = 0;
    else if (game_359708 == 0x27) {
        RadioMessage(0x3A);
        strike_35BF55 = 1;
    } else {
        RadioMessage(0x54);
        strike_35BF55 = 1;
    }
    if (strike_35BF55)
        return;
    m = MsgAlloc();
    if (!m)
        return;
    aiacc_25B358(m, cmd_3599DC, 2 | ((ulong)game_36F49C << 16));
    MsgSend(m);
    hudcb_264680(1);
}

void cmd_25F2B8(void)
{
    if (hud_38FC80 && cmd_389BD8 && game_time >= cmd_389BD8)
        close_panel();
}

/* menu of n names (lines call cmd_260D10 with arg 0x23 + i; null names are skipped) */
void cmd_25F33C(long n, char **names, ushort *keys)
{
    MfdItem *it = cmd_items_389BE0;
    long i;

    for (i = 0; i < (short)n; i++, names++, keys++)
        if (*names) {
            cmd_389B14[0x23 + i] = *names;
            it->fn = (void *)WATCOM_FN(cmd_260D10);
            it->arg = i + 0x23;
            it->key = *keys;
            *(ushort *)&it->f0a = 0xA000;
            it++;
            *(long *)&it[-1].f0e[0] = 0;
            it[-1].cmd = (short)*keys;
        }
    it->fn = NULL;
    cmd_25EF14(-1, &cmd_menu_3599E0);
}

/* a key while the order menu is up: Escape goes back / closes; else the line with that key
 * (letters case-insensitive unless bit 0x800) runs, with some lines refused per mission */
uchar cmd_25F3B8(long key)
{
    schar mi;
    MfdItem *it;
    long k, idx = 0;
    short s;
    uchar r;

    for (mi = 0; mi < game_391E4C; mi++)
        if (R16(REC, 4) == M_ENT(mi))
            break;
    if (mi == game_391E4C)
        mi = -1;
    if (key == 0x1B) {
        MfdMenu *prev;

        if (RU16(REC, 2) == 0 && (prev = (MfdMenu *)mfd_38FC87->f00) != NULL) {
            mfd_38FC87 = prev;
            if (prev == &cmd_menus[4])
                mfd_38FC87 = cmd_260BFC(mi);
            hudview_26C5F4(7);
        } else {
            if (mfd_38FC87->items->flags & 0x20)
                ScriptSendKey(0x60);
            close_panel();
        }
        SndPlay(0x7E2, 0, 0x28, 1, 0x40, *(schar *)RefGet(0x89C0076), 0);
        return 1;
    }
    it = mfd_38FC87->items;
    if (key & 0x800)
        k = key;
    else
        k = (key & 0xFF00) | toupper(key & 0xFF);
    if (!it->fn)
        return 0;
    for (;; it++, idx++) {
        if (M_VOICE(mi) == 1 && game_359708 == 0x1F && (it->cmd == 0x19 || it->cmd == 0x1A) &&
            (game_35B150 & 0x10))
            return 0;
        if (M_VOICE(mi) == 1 && game_359708 == 0x1F && it->cmd == 0x19 && !(game_35B150 & 0x10)) {
            if (key != 0x83B)
                return 0;
            s = (short)cmd_25FFC8(it->arg);
            if (!s) {
                RadioSpeak(mi, 0xC);
                return 0;
            }
            *(short *)(mfd_392141 + M_CHAN(mi) * 12) = s;
            mfd_26FB38(1, 3, *(schar *)(MEMBER(mi) + 2), 0x42);
            simtype_27B3C8(s);
            close_panel();
            return 0;
        }
        if (!simtype_27B368(M_CHAN(mi)) && it->cmd == 0x19 && key == 0x83B)
            return 0;
        if (!simtype_27B200() && it->cmd == 0x1A && key == 0x83C)
            return 0;
        if ((ushort)it->key == k) {
            if (it->cmd == 0x42) {
                s = (short)cmd_25FFC8((uchar)it->arg);
                r = simtype_27B420(s);
                if (r || !s)
                    goto next;
            } else if (it->cmd == 0x3D) {
                s = cmd_25FD80();
                r = simtype_27B4D8(s);
                if (r || !s)
                    goto next;
            } else if (mfd_38FC87->items == mfd_35A090 && idx >= 2) {
                if (!(mfd_38FC87->items == mfd_35A090 && game_359708 == 0x1E && M_VOICE(mi) == 1))
                    goto next;
            }
            mfd_38FCFC = idx;
            cmd_25F7E8(it);
            return 1;
        }
next:
        if (!it[1].fn)
            return 0;
    }
}

void cmd_25F7E8(MfdItem *it)
{
    uchar f;
    long i;

    SndPlay(0x7E2, 0, 0x40, 1, -1, 0x40, 0);
    if (*(MfdMenu **)&it->f0e[0]) {
        mfd_38FC87 = *(MfdMenu **)&it->f0e[0];
        hudview_26C5F4(7);
    }
    if (it->flags & 0x40) {
        cmd_25F3B8(0x60);
        return;
    }
    RU16(REC, 2) |= *(ushort *)&it->f0a;
    RU16(REC, 0) = it->cmd;
    R16(REC, 4) = mfd_38FC81;
    R16(REC, 6) = cmd_38FC83;
    R32(REC, 0xC) = *(long *)&it->f0e[4];
    f = it->f0a;
    if (f & 0x3F) {
        hud_38FFFC = R16(REC, 4);
        if (f & 8)
            hudview_26C224(1, 0x16);
        else if (f & 0x10)
            hudview_26C224(1, 0x18);
        else if (f & 0x20)
            hudview_26C224(1, 0x19);
        else
            hudview_26C224(1, 9);
    } else if (it->cmd) {
        if (game_359708 == 5 && it->arg == 9)
            for (i = 0; i < 60; i++) {
                short e = PBA_ENT(i);

                if (CENT(e).f23 == 1) {
                    game_3599E8 |= 8;
                    hud_389BDC = e;
                }
            }
        cmd_3599E9 = (uchar)it->arg;
        cmd_2601D8();
    }
    if (it->flags & 0x80)
        close_panel();
}

/* packed position (x >> 16 low word, y >> 16 high word) */
static ulong packed_pos(short ent)
{
    return ((ulong)(CENT(ent).y >> 16) << 16) + ((CENT(ent).x >> 16) & 0xFFFF);
}

ulong cmd_25F9AC(long ent)
{
    return packed_pos((short)ent);
}

ulong cmd_25F9E4(uchar *cmd)
{
    switch (RU16(cmd, 0)) {
    case 2:
    case 0x11:
    case 0x3C:
        return packed_pos(R16(cmd, 8));
    case 9:
    case 0x44:
        return R32(cmd, 8);
    case 0xD:
        return ((ulong)(ushort)(ffpipe_38FFF4 >> 16) << 16) | (ushort)(ffpipe_38FFF0 >> 16);
    case 0xE:
        return ((ulong)(ushort)(game_4414CC >> 16) << 16) | (ushort)(game_4414C8 >> 16);
    case 0x13:
        return packed_pos(targ_current);
    default:
        return cmd_25EF10;
    }
}

/* (g8_c.h declares an argument; the original takes none.)
 * Whole-team order: the member nearest the target gets the command for its squad; the leaders
 * of the other members' squads get it too if they are more than sqrt(0x4B) cells from him,
 * otherwise they join his squad.
 * Quirk kept: the nearest member is a local the original never sets when there is no member
 * besides the player (game_391E4C <= 1, mission 1); it then indexes the member records with
 * the word its frame holds at [esp + 0x2C] (28 bytes below the return address).  Called through
 * the Watcom thunk (W_cmd_25FB30: 3 saved registers, no argument, the C return address) that
 * slot is 12 bytes below our entry esp, so the entry stub saves it before our own frame
 * overwrites it (found at the G5-G8 merge by test_g7_b's menu set-up; see frame.py
 * STALE_SLOTS).  nx / ny are uninitialised too but unused in that case. */
short cmd_stale_near;                /* (ours, not an original variable) */
void cmd_25FB30_c(void) __attribute__((used));
#ifdef TN_WATCOM_ABI
__asm__("\t.text\n\t.globl cmd_25FB30\ncmd_25FB30:\n"
        "\tmovw -12(%esp), %ax\n\tmovw %ax, cmd_stale_near\n\tjmp cmd_25FB30_c\n");
#else
void cmd_25FB30(long unused) { (void)unused; cmd_25FB30_c(); }
#endif

void cmd_25FB30_c(void)
{
    uchar loc[16];
    ulong t = cmd_25F9E4(REC), best = 0xFFFFFFFF, d;
    short near = cmd_stale_near, nx = 0, ny = 0, ex, ey;
    long i, nb, bi;

    for (i = 1; i < game_391E4C; i++) {
        ex = (short)(CENT(M_ENT(i)).x >> 16);
        ey = (short)(CENT(M_ENT(i)).y >> 16);
        d = (ulong)((ey - (short)(t >> 16)) * (ey - (short)(t >> 16))) +
            (ulong)((ex - (short)t) * (ex - (short)t));
        if (d <= best) {
            best = d;
            near = (short)i;
            nx = ex;
            ny = ey;
        }
    }
    nb = CENT(M_ENT(near)).f21;
    for (i = 1; i < game_391E4C; i++) {
        bi = CENT(M_ENT(i)).f21;
        if (BRAIN_SQUAD(nb) == BRAIN_SQUAD(bi))
            continue;
        if (SQUAD_FIRST(BRAIN_SQUAD(bi)) != bi)
            continue;
        ex = (short)(CENT(M_ENT(i)).x >> 16);
        ey = (short)(CENT(M_ENT(i)).y >> 16);
        d = (ulong)((ey - ny) * (ey - ny)) + (ulong)((ex - nx) * (ex - nx));
        if (d > 0x4B) {
            memcpy(loc, REC, 16);
            R16(loc, 4) = M_ENT(i);
            R16(loc, 6) = BRAIN_SQUAD(bi);
            cmd_2603C4(loc);
        } else
            mfddata_266748(bi, BRAIN_SQUAD(nb));
    }
    R16(REC, 4) = M_ENT(near);
    R16(REC, 6) = BRAIN_SQUAD(nb);
    cmd_2603C4(REC);
}

/* walk a class list (15-bit index fields, word 0 head, word 2 next) for the last entity of a
 * type */
#define LIST_FIND(tab, stride, ty, r)                                                       \
    do {                                                                                    \
        short i_ = (short)(*(short *)(tab) << 1) >> 1, e_;                                  \
        while (i_) {                                                                        \
            e_ = (short)(*(short *)((uchar *)(tab) + i_ * (stride)) << 1) >> 1;              \
            if (CENT(e_).type == (ty))                                                      \
                r = e_;                                                                     \
            i_ = *(short *)((uchar *)(tab) + i_ * (stride) + 2);                            \
        }                                                                                   \
    } while (0)

short cmd_25FD80(void)
{
    short r = 0;

    switch (game_359708) {
    case 5: {
        short i = (short)(*(short *)&game_pba[0] << 1) >> 1, e;

        while (i) {
            e = PBA_ENT(i);
            if (CENT(e).f23 == 1)
                r = e;
            i = *(short *)((uchar *)&game_pba[i] + 2);
        }
        break;
    }
    case 0x15:
    case 0x18:
    case 0x19:
        LIST_FIND(game_cls4, 0x79, 0x23, r);
        break;
    case 0x1E:
        LIST_FIND(game_cls3, 0x14F, 0xA, r);
        break;
    case 0x20:
        LIST_FIND(game_cls4, 0x79, 0x34, r);
        break;
    case 0x21:
        LIST_FIND(game_cls4, 0x79, 0x37, r);
        break;
    default:
        r = 0;
    }
    if (r && !mfd_270E80(r))
        r = 0;
    return r;
}

/* ordnance / objective entity for an order kind in the missions 0x15..0x22 */
long cmd_25FFC8(long kind)
{
    long r = 0;

    switch (game_359708) {
    case 0x15:
        if (kind == 0x13)
            r = ordnance_291934(1);
        else if (kind == 0x20)
            r = ordnance_291934(2);
        break;
    case 0x17:
        LIST_FIND(game_cls4, 0x79, 0x3A, r);
        break;
    case 0x18:
        if (kind == 0x15)
            r = ordnance_291934(1);
        else if (kind == 0x1F)
            r = ordnance_291934(2);
        break;
    case 0x19:
        switch (kind) {
        case 0x1A: r = ordnance_291934(3); break;
        case 0x1B: r = ordnance_291934(1); break;
        case 0x1C: r = ordnance_291934(4); break;
        case 0x1E: r = ordnance_291934(2); break;
        }
        break;
    case 0x1B:
        switch (kind) {
        case 0x16: r = ordnance_291934(1); break;
        case 0x17: r = ordnance_291934(2); break;
        case 0x18: r = ordnance_291934(3); break;
        case 0x19: r = ordnance_291934(4); break;
        }
        break;
    case 0x1F:
        LIST_FIND(game_cls4, 0x79, 0x2C, r);
        break;
    case 0x22:
        LIST_FIND(game_cls4, 0x79, 0x35, r);
        break;
    }
    if ((short)r && !mfd_270E80((short)r))
        r = 0;
    return r;
}

uchar cmd_26016C(long kind)
{
    short s = (short)cmd_25FFC8((uchar)kind);

    if (simtype_27B420(s) || !s)
        return 1;
    return 0;
}

uchar cmd_26018C(void)
{
    short s = cmd_25FD80();

    if (simtype_27B4D8(s) || !s)
        return 1;
    return 0;
}

/* or the flags of the command's entry of the table into the record */
static void apply_flags(uchar *cmd)
{
    long i;

    for (i = 0; i < 5; i++)
        if ((short)cmd_flagtab[i * 2] == (long)RU16(cmd, 0))
            RU16(cmd, 2) |= cmd_flagtab[i * 2 + 1];
}

void cmd_2601A8(uchar *cmd)
{
    apply_flags(cmd);
}

void cmd_2601D8(void)
{
    uchar loc[16], f, b;
    schar mi = member_of(R16(REC, 4));
    long i, bi;

    apply_flags(REC);
    f = REC[3];
    if (f & 0x20) {
        ScriptSendKey(RU16(REC, 0));
        return;
    }
    b = CENT(M_ENT(mi)).f21;
    if (f & 0x10) {
        if (f & 2) {
            for (i = 1; i < game_391E4C; i++) {
                bi = CENT(M_ENT(i)).f21;
                if ((REC[3] & 8) && SQUAD_FIRST(BRAIN_SQUAD(bi)) != bi)
                    continue;
                memcpy(loc, REC, 16);
                R16(loc, 4) = M_ENT(i);
                R16(loc, 6) = BRAIN_SQUAD(bi);
                cmd_2603C4(loc);
            }
            return;
        }
        if (f & 8) {
            ailow_253334(b);
            R16(REC, 6) = BRAIN_SQUAD(b);
        }
    } else {
        if (f & 2) {
            cmd_25FB30(0);
            return;
        }
        if (!(f & 4))
            return;
        ailow_253334(b);
        R16(REC, 6) = BRAIN_SQUAD(b);
    }
    cmd_2603C4(REC);
}

static void move_order(uchar *cmd, uchar b)
{
    mfddata_266618(R16(cmd, 6), R32(cmd, 8), b);
    mapd_390308 = RU16(cmd, 6);
}

/* Dispatch a command record to the AI of the member (record +4) / squad (+6).  The original
 * re-stores game_391E4C on some paths (no effect). */
void cmd_2603C4(uchar *cmd)
{
    schar mi = member_of(R16(REC, 4));
    uchar b = CENT(M_ENT(mi)).f21, f;
    short s, t;

    switch (RU16(cmd, 0)) {
    case 2:
        t = R16(cmd, 8);
        if (!CENT(t).active)
            move_order(cmd, b);
        else if (CENT(t).cls == 1)
            mfddata_266680(R16(cmd, 6), BRAIN_SQUAD(CENT(t).f21), b);
        else
            mfddata_2666E4(R16(cmd, 6), t, b);
        return;
    case 4:
        mfddata_266748(b, R16(cmd, 8));
        return;
    case 9:
        move_order(cmd, b);
        return;
    case 0xD:
        if (ffpipe_38FFF0 && ffpipe_38FFF4) {
            R16(cmd, 8) = (short)(ffpipe_38FFF0 >> 16);
            R16(cmd, 0xA) = (short)(ffpipe_38FFF4 >> 16);
            move_order(cmd, b);
        } else
            aimain_24A6A0(b, 0xB);
        return;
    case 0xE:
        if ((game_359708 == 0x10 && !game_389BD4) || (game_359708 == 0x1E && !game_389BD0) ||
            (game_359708 > 0x29 && game_359708 < 0x2E)) {
            aimain_24A6A0(b, 0xB);
            return;
        }
        R16(cmd, 8) = (short)(game_4414C8 >> 16);
        R16(cmd, 0xA) = (short)((game_4414CC >> 16) + 2);
        move_order(cmd, b);
        return;
    case 0x11:
        if (CENT(R16(cmd, 8)).active)
            mfddata_266860((short *)cmd, b);
        else
            move_order(cmd, b);
        mfd_270090(R16(cmd, 4), R16(cmd, 0));
        return;
    case 0x13:
        R16(cmd, 8) = targ_current;
        mfddata_2666E4(R16(cmd, 6), R16(cmd, 8), b);
        return;
    case 0x28:
    case 0x29:
    case 0x2A:
        mfd_270090(R16(cmd, 4), R16(cmd, 0));
        return;
    case 0x34:
        if ((game_3599E8 & 8) && game_359708 == 5 && mfd_38FCFC >= game_391E4C)
            mfd_26FDA8(R16(cmd, 4), hud_389BDC, 0x40);
        else
            mfd_26FDA8(R16(cmd, 4), M_ENT(mfd_38FCFC), 0x34);
        return;
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x3E:
    case 0x3F:
        return;
    case 0x3C:
        t = R16(cmd, 8);
        if (!mfd_270E80(t)) {
            RadioSpeak(mi, 0x53);
            return;
        }
        RadioSpeak(mi, 1);
        mfd_270CE0(R16(cmd, 4), t, 0x3C);
        return;
    case 0x3D:
        s = cmd_25FD80();
        if (!s || !mfd_270E80(s)) {
            RadioSpeak(mi, 0xC);
            return;
        }
        RadioSpeak(mi, 1);
        if (s)
            mfd_270CE0(R16(cmd, 4), s, 0x3D);
        return;
    case 0x41:
        if (game_359708 != 0x1E)
            return;
        if (!game_35B19C) {
            RadioSpeak(mi, 0x5C);
            return;
        }
        RadioSpeak(mi, 2);
        mfd_271470(R16(cmd, 4), game_35B60C);
        close_panel();
        return;
    case 0x42:
        s = (short)cmd_25FFC8(cmd_3599E9);
        if (!s)
            RadioSpeak(mi, 0xC);
        if (!s) {
            close_panel();
            return;
        }
        *(short *)(mfd_392141 + M_CHAN(member_of(R16(cmd, 4))) * 12) = s;
        mfd_38FD01 = cmd_39238A[CENT(s).type];
        mfd_38FD00 = simtype_27B368(M_CHAN(member_of(R16(cmd, 4))));
        if (mfd_38FD00 < mfd_38FD01) {
            RadioSpeak(mi, 0x5C);
            close_panel();
            return;
        }
        hudview_26C224(1, 0x1B);
        simtype_27B3C8(s);
        return;
    case 0x44:
        mfddata_266618(R16(cmd, 6), R32(cmd, 8), b);
        RU16(cmd, 0) = 0xA;
        mfddata_266860((short *)cmd, b);
        return;
    default:
        f = cmd[3];
        if (!(f & 0x10) || (f & 8))
            mfddata_266860((short *)cmd, b);
        else
            mfddata_2667AC(b, RU16(cmd, 0));
        return;
    }
}

/* order menu for a member, by the state of its channel's record */
MfdMenu *cmd_260BFC(long member)
{
    schar k = cmd_391F4D[M_VOICE((short)member)];

    switch (game_39034D[k * 0x1EE]) {
    case 1: return &cmd_menus[0];
    case 3: return &cmd_menus[1];
    case 4: return &cmd_menus[2];
    case 5: return &cmd_menus[3];
    default: return &cmd_menus[4];
    }
}

/* text of a menu line (arg: line kind; empty when the line does not apply).  Quirk: the default
 * case (args 0x21, 0x22 and above 0x29) formats a text and then clears it. */
char *cmd_260D10(long arg, long member, char *out)
{
    short m = (short)member;
    schar k;
    long n, j;
    static const struct { long ref; long mission; schar voice; } fixed[] = {
        /* 0x12 .. 0x20 */
        { 0x2430016, 0x22, 0 }, { 0x2430017, 0x15, 0 }, { 0x2430018, 0x17, 0 },
        { 0x2430019, 0x18, 0 }, { 0x243001A, 0x1B, 0 }, { 0x243001B, 0x1B, 0 },
        { 0x243001C, 0x1B, 0 }, { 0x243001D, 0x1B, 0 }, { 0x243001E, 0x19, 0 },
        { 0x243001F, 0x19, 0 }, { 0x2430020, 0x19, 0 }, { 0x2430022, 0x1F, 1 },
        { 0x2430021, 0x19, 0 }, { 0x2430021, 0x18, 0 }, { 0x2430021, 0x15, 0 },
    };

    switch (arg) {
    case 0:
    case 1:
    case 2: {
        char *w = RefGet(0x16F0074 + *(schar *)(MEMBER(m) + 0xD));

        lg_sprintf(out, RefGet(0x16F000A + arg), w);
        return out;
    }
    case 3:
        if (game_359708 != 5)
            break;
        lg_sprintf(out, RefGet(0x16F0082));
        return out;
    case 4:
        if (game_359708 != 0x15)
            break;
        lg_sprintf(out, RefGet(0x16F0083));
        return out;
    case 5:
        if (game_359708 != 0x18)
            break;
        lg_sprintf(out, RefGet(0x16F0084));
        return out;
    case 6:
    case 7:
    case 8:
    case 9:
        n = (schar)(uchar)game_391E4C;
        if (game_359708 == 5)
            n = (schar)(n + 1);
        j = arg - 5;
        if (n < j)
            break;
        if (j > game_391E4C)
            lg_sprintf(out, RefGet(0x16F00C7));
        else {
            char *v = RefGet(0x16C0000 + M_VOICE(arg - 6));

            lg_sprintf(out, RefGet(0x16F0080), v);
        }
        return out;
    case 0xA:
    case 0xB:
        break;
    case 0xC:
        if (game_359708 != 0x19)
            break;
        lg_sprintf(out, RefGet(0x16F0087));
        return out;
    case 0xD:
        if (game_359708 != 0x1E)
            break;
        lg_sprintf(out, RefGet(0x16F0086));
        return out;
    case 0xE:
        if (game_359708 != 0x20 || M_VOICE(m) != 2)
            break;
        lg_sprintf(out, RefGet(0x16F0085));
        return out;
    case 0xF:
        if (game_359708 != 0x21)
            break;
        lg_sprintf(out, RefGet(0x16F0085));
        return out;
    case 0x10:
        if (game_359708 != 0x1E || M_VOICE(m) != 1)
            break;
        lg_sprintf(out, RefGet(0x16F009C));
        return out;
    case 0x11: {
        char *s, *d = out;

        k = *(schar *)(MEMBER(m) + 0xD);
        if (k == -1 || (mfdp_35BF64 & (1L << (k & 31))))
            break;
        s = RefGet(0x16F0006);
        while ((*d++ = *s++) != 0)
            ;
        return out;
    }
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
        lg_sprintf(out, cmd_389B14[arg]);
        return out;
    default:
        if ((ulong)arg > 0x29 || arg == 0x21 || arg == 0x22) {
            lg_sprintf(out, RefGet(0x16E00C0), arg, (long)m);
            break;
        }
        j = arg - 0x12;
        if (game_359708 != fixed[j].mission)
            break;
        if (fixed[j].voice && M_VOICE(m) != fixed[j].voice)
            break;
        lg_sprintf(out, RefGet(fixed[j].ref));
        return out;
    }
    *out = 0;
    return out;
}

uchar cmd_261320(void)
{
    GameEntity *e = &CENT(game_player_entity);

    return fix_fast_pyth_dist(e->x - game_4414C8, e->y - game_4414CC) < 0x80000;
}

void cmd_261370(uchar *item)
{
    RU16(REC, 2) = 0;
    mfd_38FC81 = M_ENT(mfd_3599D4);
    cmd_38FC83 = BRAIN_SQUAD(CENT(M_ENT(mfd_3599D4)).f21);
    cmd_25F7E8((MfdItem *)item);
}

/* the member order shortcut keys (list 0x35A44C, 11 lines) when there is a team */
uchar cmd_2613E0(long key)
{
    long i;

    if (game_391E4C <= 1)
        return 0;
    for (i = 0; i < 11; i++)
        if ((short)key == (short)cmd_items_35A44C[i].key) {
            mfd_38FC81 = M_ENT(mfd_3599D4);
            RU16(REC, 2) = 0;
            cmd_38FC83 = BRAIN_SQUAD(CENT(M_ENT(mfd_3599D4)).f21);
            cmd_25F7E8(&cmd_items_35A44C[i]);
            return 1;
        }
    return 0;
}
