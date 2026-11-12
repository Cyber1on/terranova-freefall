/* aicmd.c -- Terra Nova AI squad commands from the mission script (module M010,
 * 0x25A110-0x25AB8B).  Decompiled from __FF.EXE v1.09.
 *
 * The mission's AI script thread (id ai_script_id, set by MissionSetup) can steer squads.  A
 * squad that consults the script (aimsg_25A150) sends it a message {from 0x81.squad, to 2.id}
 * and runs the thread at once; the script answers through the engine functions below, which
 * (with squad 0 = "the squad that is asking") store a command and up to four arguments in
 * aicmd_cmd / aicmd_arg.  aimsg_25A22C turns that command into an AI message to the squad.
 * A command for another squad goes to it as a message of type 0x65 (aimsg_25A754), unless that
 * squad is already script-controlled (+0x14E) and in use.
 *
 *   0x25A110 aimsg_25A110  clear every squad's "script-controlled" flag (+0x14E), squads 1..31
 *   0x25A130 aimsg_25A130  set it for one squad (called when a squad is removed)
 *   0x25A150 aimsg_25A150  hand a squad's message to the AI script (or drop it): returns the
 *                          command the script chose (0 = none)
 *   0x25A22C aimsg_25A22C  send the chosen command to the squad as an AI message
 *   0x25A754 aimsg_25A754  send a command to another squad (message type 0x65)
 *   0x25A7C8 aimsg_25A7C8  take a command from a type 0x65 message and carry it out
 *   0x25A7E8 aimsg_25A7E8  store or forward a command (the common part of the engine functions)
 *   0x25A868 .. 0x25AB6C   script engine functions: commands 1..0x18 (0x25AB04: mission timer)
 *
 * Return values: the engine functions return what the original leaves in eax, because the
 * script interpreter pushes it (T1a): the last stack argument when the command is stored, the
 * squad's record offset (squad * 0x170) when the command is dropped, else aimsg_25A754's value
 * (the message pointer, which MsgSend leaves in eax, or 0 when the pool is empty).
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_c1.h"

#ifndef TN_LINK_ORIGINAL_DATA
long aicmd_arg[4];
uchar aicmd_cmd;
ushort ai_script_id;
long *game_442F40;
#endif

#define SQ_INUSE     0x01
#define SQ_ADDR      0x03
#define SQ_SCRIPTED  0x14E

/* 0x25A110 */
void aimsg_25A110(void)
{
    uchar i;
    for (i = 1; i < 0x20; i++)
        C1_U8(C1_SQ(i), SQ_SCRIPTED) = 0;
}

/* 0x25A130 */
void aimsg_25A130(long squad)
{
    C1_U8(C1_SQ(squad), SQ_SCRIPTED) = 1;
}

/* 0x25A150: a message from a squad ({0x81, squad}) that is script-controlled and in use is
 * dropped; anything else goes to the AI script thread, which runs at once. */
uchar aimsg_25A150(Msg *m)
{
    uchar *sq = (uchar *)&game_squads[0] + (ulong)m->from.b * sizeof(GameSquad);

    aicmd_cmd = 0;
    if (m->from.a == 0x81 && C1_U8(sq, SQ_SCRIPTED) != 0 && C1_U8(sq, SQ_INUSE) == 0) {
        MsgFree(m);
    } else {
        m->to.b = ai_script_id;
        m->to.a = 2;
        MsgSend(m);
        ScriptThreadFrame(ScriptFind(ai_script_id));
    }
    return aicmd_cmd;
}

/* the waypoint (region centre) of a command argument */
static void region_point(short *p, long i)
{
    p[0] = (short)(*(fix *)&game_4413D0[i * 8] >> 16);
    p[1] = (short)(*(fix *)&game_4413D0[i * 8 + 4] >> 16);
}

/* 0x25A22C */
void aimsg_25A22C(void *sq)
{
    Msg *m;
    short pt[2];
    ulong to, a;
    uchar flag = 6;

    switch (aicmd_cmd) {
    case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12:
    case 13: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
    case 24:
        break;
    default:            /* 1, 14 and anything else: nothing */
        return;
    }
    if ((m = MsgAlloc()) == NULL)
        return;
    to = C1_U32(sq, SQ_ADDR);
    switch (aicmd_cmd) {
    case 2:
        region_point(pt, aicmd_arg[0]);
        aiacc_25AC6C(m, to, to, pt, 0);
        break;
    case 3:
        aiacc_25B084(m, to, to, (ushort)aicmd_arg[0], (ushort)aicmd_arg[1], (ushort)aicmd_arg[2]);
        break;
    case 4:
        aiacc_25B284(m, to, C1_PT(0x81, aicmd_arg[1]), aicmd_arg[0]);
        break;
    case 5:
        aiacc_25B2A4(m, to, to);
        flag = 0xF;
        break;
    case 6:
        aiacc_25B2B0(m, to, C1_PT(0x81, aicmd_arg[0]));
        break;
    case 7:
        aiacc_25AFFC(m, to, to, (uchar)aicmd_arg[0], 0);
        break;
    case 8:
        a = (ushort)ordnance_291934(aicmd_arg[0]);
        aiacc_25B014(m, to, to, a, 0);
        break;
    case 9:
        aiacc_25B0A8(m, to, to, (signed char)aicmd_arg[0], (signed char)aicmd_arg[1],
                     (signed char)aicmd_arg[2], (signed char)aicmd_arg[3]);
        break;
    case 10:
        aiacc_25B170(m, to, to, (uchar)aicmd_arg[0]);
        break;
    case 11:
        a = (ushort)ordnance_291934(aicmd_arg[0]);
        aiacc_25B1BC(m, to, to, a);
        break;
    case 12:
        aiacc_25B1E0(m, to, to);
        break;
    case 13:
        aiacc_25B140(m, to, to, (signed char)aicmd_arg[0]);
        break;
    case 15:
        aiacc_25B150(m, to, to, (uchar)aicmd_arg[0]);
        break;
    case 16:
        aiacc_25B160(m, to, to, (uchar)aicmd_arg[0]);
        break;
    case 17:
        region_point(pt, aicmd_arg[0]);
        aiacc_25ACB0(m, to, to, pt);
        break;
    case 18:
        aiacc_25B1D4(m, to, to);
        break;
    case 19:
        aiacc_25B180(m, to, to, (uchar)aicmd_arg[0]);
        break;
    case 20:
        aiacc_25B190(m, to, to);
        break;
    case 21:
        aiacc_25B19C(m, to, to, (uchar)aicmd_arg[0]);
        break;
    case 22:
        aiacc_25B1AC(m, to, to, (uchar)aicmd_arg[0]);
        break;
    case 23:
        aiacc_25AF1C(m, to, to, (uchar)aicmd_arg[0], 0);
        break;
    case 24:
        aiacc_25ADC8(m, to, to);
        break;
    }
    m->flag = flag;
    MsgSend(m);
}

/* 0x25A754: command for another squad, as a type 0x65 message to {0x81, squad} carrying the
 * command and the four arguments (aiacc_25B208 sets the sender itself and ignores the script
 * address it is given) */
long aimsg_25A754(long squad, long cmd, long a, long b, long c, long d)
{
    long args[4];
    MsgAddr from, to;
    Msg *m;

    args[0] = a;
    args[1] = b;
    args[2] = c;
    args[3] = d;
    from.a = 0x81;
    from.b = (uchar)squad;
    to.a = 2;
    to.b = ai_script_id;
    if ((m = MsgAlloc()) == NULL)
        return 0;
    aiacc_25B208(m, *(long *)&to, *(ulong *)&from, (uchar)cmd, args);
    MsgSend(m);
    return (long)m;        /* MsgSend leaves the message in eax */
}

/* 0x25A7C8 */
void aimsg_25A7C8(void *sq, Msg *m)
{
    aiacc_25B244(m, &aicmd_cmd, aicmd_arg);
    aimsg_25A22C(sq);
}

/* 0x25A7E8 */
long aimsg_25A7E8(long squad, long cmd, long a, long b, long c, long d)
{
    uchar s = (uchar)squad;
    uchar *sq;

    if (s == 0) {
        aicmd_cmd = (uchar)cmd;
        aicmd_arg[0] = a;
        aicmd_arg[2] = c;
        aicmd_arg[1] = b;
        aicmd_arg[3] = d;
        return d;
    }
    sq = C1_SQ(s);
    if (C1_U8(sq, SQ_SCRIPTED) != 0 && C1_U8(sq, SQ_INUSE) == 0)
        return (long)s * 0x170;
    return aimsg_25A754(s, (uchar)cmd, a, b, c, d);
}

/* 0x25A868 .. 0x25AB6C: script engine functions, one per command */
long aimsg_25A868(long s)                         { return aimsg_25A7E8((uchar)s, 1, 0, 0, 0, 0); }
long aimsg_25A888(long s, long a)                 { return aimsg_25A7E8((uchar)s, 2, a, 0, 0, 0); }
long aimsg_25A8A4(long s, long a, long b, long c) { return aimsg_25A7E8((uchar)s, 3, a, b, c, 0); }
long aimsg_25A8C4(long s, long a, long b)         { return aimsg_25A7E8((uchar)s, 4, a, b, 0, 0); }
/* declared void by g3_c.h (G3c calls it); the eax the original leaves is not returned */
void aimsg_25A8E4(long s)                         { aimsg_25A7E8((uchar)s, 5, 0, 0, 0, 0); }
long aimsg_25A904(long s, long a)                 { return aimsg_25A7E8((uchar)s, 6, (uchar)a, 0, 0, 0); }
long aimsg_25A924(long s, long a)                 { return aimsg_25A7E8((uchar)s, 7, (uchar)a, 0, 0, 0); }
long aimsg_25A944(long s, long a)                 { return aimsg_25A7E8((uchar)s, 8, a, 0, 0, 0); }
long aimsg_25A960(long s, long a, long b, long c) { return aimsg_25A7E8((uchar)s, 9, a, b, c, 0xFF); }
long aimsg_25A984(long s, long a)                 { return aimsg_25A7E8((uchar)s, 0xA, a, 0, 0, 0); }
long aimsg_25A9A0(long s, long a)                 { return aimsg_25A7E8((uchar)s, 0xB, a, 0, 0, 0); }
long aimsg_25A9BC(long s)                         { return aimsg_25A7E8((uchar)s, 0xC, 0, 0, 0, 0); }
long aimsg_25A9DC(long s, long a)                 { return aimsg_25A7E8((uchar)s, 0xD, a, 0, 0, 0); }
long aimsg_25A9F8(long s)                         { return aimsg_25A7E8((uchar)s, 0xE, 0, 0, 0, 0); }
long aimsg_25AA18(long s, long a)                 { return aimsg_25A7E8((uchar)s, 0xF, a, 0, 0, 0); }
long aimsg_25AA34(long s, long a)                 { return aimsg_25A7E8((uchar)s, 0x10, a, 0, 0, 0); }
long aimsg_25AA50(long s, long a)                 { return aimsg_25A7E8((uchar)s, 0x11, a, 0, 0, 0); }
long aimsg_25AA6C(long s)                         { return aimsg_25A7E8((uchar)s, 0x12, 0, 0, 0, 0); }
long aimsg_25AA8C(long s, long a)                 { return aimsg_25A7E8((uchar)s, 0x13, a, 0, 0, 0); }
long aimsg_25AAA8(long s)                         { return aimsg_25A7E8((uchar)s, 0x14, 0, 0, 0, 0); }
long aimsg_25AAC8(long s, long a)                 { return aimsg_25A7E8((uchar)s, 0x15, (uchar)a, 0, 0, 0); }
long aimsg_25AAE8(long s, long a)                 { return aimsg_25A7E8((uchar)s, 0x16, a, 0, 0, 0); }
long aimsg_25AB50(long s, long a)                 { return aimsg_25A7E8((uchar)s, 0x17, a, 0, 0, 0); }
long aimsg_25AB6C(long s)                         { return aimsg_25A7E8((uchar)s, 0x18, 0, 0, 0, 0); }

/* 0x25AB04: mission timer `timer` runs for `secs` seconds (both copies of the time in ms), or
 * is switched off when secs <= 0.  Returns what the original leaves in eax. */
long aimsg_25AB04(long timer, long secs)
{
    long *t = game_442F40;
    ulong mask = 1UL << (timer & 31);

    if (secs > 0) {
        long ms = secs * 1000;
        t[0] |= mask;
        t[timer + 0x21] = ms;
        t[timer + 1] = ms;
        return (secs * 32 - secs) * 4;   /* eax = secs * 124 (an intermediate of secs * 1000) */
    }
    t[0] &= ~mask;
    return (long)t;
}
