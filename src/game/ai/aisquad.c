/* aisquad.c -- Terra Nova AI side helpers (module M006).  Decompiled from __FF.EXE v1.09.
 *
 *   0x24A820 aisquad_24A820  init a side record
 *   0x24A88C aisquad_24A88C  side 2: call reinforcements to the player's square every 5 s
 *   0x24A9B8 aisquad_24A9B8  side frame: debug flag, sighting list, mailbox (types 0 / 0xB)
 *
 * The sighting list (side +8, up to 0xC0 records of {short id, short b, short ent}) is
 * rebuilt every frame from the PBA sighting tables (PBA +0xBE: 6-byte records, 0-terminated,
 * at most 0x40) of the entities aihelp_2D3298 lists, and the 0x20 records of the class 3
 * carrier (game_396868; record +0x6E); ai_seen_bits keeps each id once.
 */
#include "g4_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
ulong ai_cur_addr_b, ai_cur_addr_a;
uchar ai_debug_on;
uchar ai_seen_bits[0x26];
ulong ai_reinforce_time;
short ai_reinforce_max;
#endif

void aisquad_24A820(long side_arg)
{
    signed char s = (signed char)side_arg;
    uchar *side = G4_SIDE(s);

    G4_U16(side, 1) = 0x82;
    side[5] = (uchar)s;
    G4_S16(side, 3) = s;
    side[6] = 0;
    side[7] = 0;
    side[0] = 1;
    memset(side + 8, 0, 0x480);
    side[0x488] = 0;
}

void aisquad_24A88C(uchar *side)
{
    ulong pos;
    short i, n = 0;

    if (side[5] != 2)
        return;
    if (ai_reinforce_time + 5000 >= game_time)
        return;
    pos = G4_U32(G4_BRAIN(G4_ENT(game_player_entity).f21), 0xE);
    ai_reinforce_time = game_time;
    for (i = 0; i < GAME_MAX_BRAINS; i++) {
        uchar *b = G4_BRAIN(i);
        if (b[0] && b[5] == 2 && b[0xD] != 3)
            n++;
    }
    if (n > ai_reinforce_max)
        return;
    for (i = 0; i < GAME_MAX_BRAINS; i++) {
        uchar *b = G4_BRAIN(i);
        Msg *m;

        if (!b[0] || b[5] != 2)
            continue;
        m = MsgAlloc();
        if (!m)
            continue;
        aiacc_25AC6C(m, G4_U32(side, 1), G4_U32(G4_SQUAD(b[6]), 3), &pos, (uchar)i);
        m->flag = 7;
        MsgSend(m);
    }
}

/* debug mask bit of the object whose MsgAddr is at p (also in AISoldierThink) */
static void ai_set_debug(uchar *addr)
{
    ulong bit;

    ai_cur_addr_a = G4_U16(addr, 0);
    ai_cur_addr_b = G4_U16(addr, 2);
    bit = 1u << (ai_cur_addr_b & 31);
    if (ai_cur_addr_a == 0x80)
        bit &= ai_debug_mask80;
    else if (ai_cur_addr_a == 0x81)
        bit &= ai_debug_mask81;
    else
        bit &= ai_debug_mask82;
    ai_debug_on = bit != 0;
}

static uchar add_sighting(uchar *side, uchar cnt, short *rec, short ent)
{
    short id = rec[0];
    long byte = id >> 3;
    ulong bits = ai_seen_bits[byte];

    if (!(bits & (1u << (ushort)(id & 7)))) {
        ai_seen_bits[byte] |= (uchar)(1 << (id & 7));
        G4_S16(side, 8 + cnt * 6) = rec[0];
        G4_S16(side, 0xA + cnt * 6) = rec[1];
        G4_S16(side, 0xC + cnt * 6) = ent;
        cnt++;
        if (cnt >= 0xC0)
            cnt--;
    }
    return cnt;
}

void aisquad_24A9B8(uchar *side, long quiet)
{
    uchar cnt = 0, j;
    long ent;
    Msg *m;
    short pos[2];
    uchar b;

    ai_set_debug(side + 1);
    memset(ai_seen_bits, 0, 0x26);
    aihelp_2D3298(0);
    while ((ent = aihelp_2D3298(1)), (short)ent != 0) {
        short slot = G4_ENT((short)ent).slot;
        for (j = 0; j != 0x40; j++) {
            short *rec = (short *)(G4_U32(&game_pba[slot], 0xBE) + j * 6);
            if (!rec[0])
                break;
            cnt = add_sighting(side, cnt, rec, (short)ent);
        }
    }
    if (game_396868) {
        for (j = 0; j != 0x20; j++) {
            short slot = G4_ENT(game_396868).slot;
            short *rec = (short *)(G4_U32(game_cls3[slot], 0x6E) + j * 6);
            if (!rec[0])
                break;
            cnt = add_sighting(side, cnt, rec, game_396868);
        }
    }
    if (cnt < 0xC0)
        G4_S16(side, 8 + cnt * 6) = 0;
    if (quiet)
        return;
    aisquad_24A88C(side);
    while ((m = MsgReceive((MsgAddr *)(side + 1))) != 0) {
        uchar type = m->pad2;
        uchar start, sq;

        if (type == 0) {
            aiacc_25AC98(m, pos, &b);
            start = (uchar)m->from.b;
            for (sq = G4_U8(G4_SQUAD(start), 0x166); sq != start; sq = G4_U8(G4_SQUAD(sq), 0x166)) {
                if (G4_U8(G4_SQUAD(sq), 0x90) == 0) {
                    Msg *m2 = MsgAlloc();
                    if (m2) {
                        aiacc_25AC6C(m2, G4_U32(side, 1), G4_U32(G4_SQUAD(sq), 3), pos, b);
                        m2->flag = 7;
                        MsgSend(m2);
                    }
                }
            }
        } else if (type == 0xB) {
            side[0x488] = 1;
            start = (uchar)m->from.b;
            for (sq = G4_U8(G4_SQUAD(start), 0x166); sq != start; sq = G4_U8(G4_SQUAD(sq), 0x166)) {
                Msg *m2 = MsgAlloc();
                if (m2) {
                    aiacc_25ADE0(m2, G4_U32(side, 1), G4_U32(G4_SQUAD(sq), 3), 0, 0);
                    m2->flag = 7;
                    MsgSend(m2);
                }
            }
        }
        MsgFree(m);
    }
}
