/* aiglue.c -- Terra Nova AI glue (module M008 0x253810-0x2539F0, no assert path; between
 * ailow.c and the squad perception module).  Decompiled from __FF.EXE v1.09.
 * Messages from here go to the mission's AI script (aimsg_25A150) with the sender address
 * aiglue_from {0x86, 0}.
 *
 *   0x253810 aiglue_253810  empty
 *   0x253814 aiglue_253814  per squad: mission regions (misobj_2C9DF8) the squad (or, for a
 *                           squad of player brains only, the player) is in; one message 0x6E per
 *                           region entered or left
 *   0x253994 aiglue_253994  message 0x6C (id)
 *   0x2539BC aiglue_2539BC  message 0x6D (squad)
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_b.h"
#include "g9_c.h"     /* added at the G9/G10 merge */

#define U8(p, o)  (*(uchar *)((uchar *)(p) + (o)))
#define U16(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define S32(p, o) (*(long *)((uchar *)(p) + (o)))
#define U32(p, o) (*(ulong *)((uchar *)(p) + (o)))
#define BRAIN(i)  ((uchar *)&game_brains[(uchar)(i)])
#define SQUAD(i)  ((uchar *)&game_squads[(uchar)(i)])
#define EN(i)    (game_entities[(short)(i)])

#ifndef TN_LINK_ORIGINAL_DATA
ulong aiglue_from = 0x86;
#endif

void aiglue_253810(void)
{
}

void aiglue_253814(void)
{
    uchar s;

    for (s = 1; s < 32; s++) {
        uchar *sq = SQUAD(s);
        ulong old, mask;

        if (!sq[0])
            continue;
        old = U32(sq, 0x123);
        if (U16(sq, 0x12) == 0 && U16(sq, 0x14) == 0)
            continue;
        if (sq[0x11] == 0 && BRAIN(sq[0xF])[0xA] != 0)
            sq[0x122] = misobj_2C9DF8(&mask, EN(game_player_entity).x, EN(game_player_entity).y);
        else
            sq[0x122] = misobj_2C9DF8(&mask, (long)((ulong)U16(sq, 0x12) << 16),
                                      (long)((ulong)U16(sq, 0x14) << 16));
        if (old != mask) {
            ulong added = ~old & mask;
            ulong diff = added | (~mask & old);
            long i;

            for (i = 0; i < 32; i++) {
                ulong bit = 1UL << i;
                if (bit & diff) {
                    Msg *m = MsgAlloc();
                    if (m) {
                        aiacc_25B2F8(m, aiglue_from, s, i, (bit & added) != 0, SQUAD(s)[7]);
                        aimsg_25A150(m);
                    }
                }
            }
            U32(SQUAD(s), 0x123) = mask;
        }
    }
}

void aiglue_253994(long id)
{
    Msg *m = MsgAlloc();

    if (m) {
        aiacc_25B2C8(m, aiglue_from, id);
        aimsg_25A150(m);
    }
}

void aiglue_2539BC(long squad)
{
    Msg *m = MsgAlloc();

    if (m) {
        aiacc_25B2E0(m, aiglue_from, squad);
        aimsg_25A150(m);
    }
}
