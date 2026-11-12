/* reticle.c -- Terra Nova module M106 (source name unknown): the player's reticle target and
 * the map edge warning.  Decompiled from __FF.EXE v1.09; names ours.
 *
 *   0x2CCD30 reticle_reset        0x2CCD44 reticle_edge_check   0x2CCDEC reticle_edge_dist
 *   0x2CCE70 reticle_target       0x2CD388 reticle_edge_msg
 *
 * reticle_target (called by the HUD views with a screen point packed x | y << 16, the view's
 * camera and view record) projects the point into the world (viewdist_2A28D0).  Unless the
 * player's current weapon is of type 8, at most once a second it tells every AI brain and
 * squad whose bearing from the player is within 0x400 of the target's bearing (and that is
 * not too far: dx + dy < 20.0) that the player is aiming its way (aiacc_25AE48 messages from
 * {0x80, player}).  When pbasim_28A878 says the player's line of fire hits object e (a soldier
 * with a brain), the brain and its squad get messages too.
 * reticle_edge_check sends message 0x85 to the AI every 10 s while the player is within 16
 * of the map edge (512 x 512), and sets the "out of the area" state within 6.
 */
#include "g2_b.h"
#include "g3_c.h"      /* pbasim_28A878, damage_27D540, instsim_288554 (owner g3c) */
#include "g4_a.h"      /* AI callees (owners g4a/g4b/g4c1/g4c2; G4 merge) */
#include "g5_c.h"     /* added at the G5-G8 merge */

#ifndef TN_WATCOM_ABI
void port_arms_free_aim(long kind, long type, const void *pview, fix *x, fix *y, fix *z); /* port/video/sharp3d.c */
#endif

#ifndef TN_LINK_ORIGINAL_DATA
ulong reticle_edge_time;
ulong reticle_edge_count;
long game_397B70;
uchar game_3970E0;
ushort game_36F49C;
#endif

void reticle_reset(void)
{
    reticle_edge_count = 0;
    reticle_edge_time = 0;
}

void reticle_edge_check(void)
{
    GameEntity *p = ENT(game_player_entity);
    long x = p->x >> 16, y = p->y >> 16;
    if (x > 0x1FA || x < 6 || y > 0x1FA || y < 6) {
        game_397B70 = 3;
        game_3970E0 = 1;
        return;
    }
    if (x > 0x1F0 || x < 0x10 || y > 0x1F0 || y < 0x10) {
        if (game_time - reticle_edge_time > 10000) {
            reticle_edge_count++;
            reticle_edge_msg((uchar)reticle_edge_count);
            reticle_edge_time = game_time;
        }
    }
}

/* unreferenced: distance from the player to the nearest map edge (only the low word of x
 * takes part in the comparisons; the whole register is returned) */
long reticle_edge_dist(void)
{
    GameEntity *p = ENT(game_player_entity);
    long x = p->x >> 16, y = p->y >> 16;
    if (x > 0x100)
        x = 0x200 - x;
    if (y > 0x100) {
        if ((long)(ushort)x > 0x200 - y)
            x = 0x200 - y;
    } else if ((long)(ushort)x > y)
        x = y;
    return x;
}

/* |a| > 8.0 or |b| > 8.0: scale both down for fix_atan2 */
static void atan_scale(fix *a, fix *b)
{
    fix t = *a < 0 ? -*a : *a;
    fix u = *b < 0 ? -*b : *b;
    if (t > 0x800000 || u > 0x800000) {
        *a >>= 7;
        *b >>= 7;
    }
}

static fixang bearing(fix dx, fix dy)
{
    atan_scale(&dx, &dy);
    return fix_atan2(dy, dx);
}

void reticle_target(long e, ulong pt, uchar *cam, void *view)
{
    GameEntity *pl;
    uchar *pba, *brain, *sq;
    short scr[2];
    fix tz, ty, tx;
    ulong from;
    uchar no_alert, i, b;
    Msg *m;

    brain = (uchar *)&game_brains[ENT(game_player_entity)->f21];
    scr[0] = (short)((short)pt >> (U16(cam, 0x2E) & 1));
    scr[1] = (short)((short)(pt >> 16) >> ((U16(cam, 0x2E) >> 1) & 1));
    viewdist_2A28D0((ushort)scr[0] | (ulong)(ushort)scr[1] << 16, &tx, &ty, &tz, cam, view);
    pba = (uchar *)&game_pba[game_player_slot];
    no_alert = B8(pba, 0x45 + S8(pba, 0xCE) * 15) == 8;
    from = 0x80 | ((ulong)(ushort)game_player_entity << 16);
    if (!no_alert && S32(brain, 0xFA) + 1000 < game_time) {
        S32(brain, 0xFA) = game_time;
        for (i = 0; i < GAME_MAX_BRAINS; i++) {
            uchar *br = (uchar *)&game_brains[i];
            GameEntity *t;
            fix dx, dy;
            fixang a1, a2, d1, d2;
            if (br[0] == 0 || br[0xA] != 0 || br[5] == 1)
                continue;
            t = ENT(S16(br, 0xB));
            pl = ENT(game_player_entity);
            dx = t->x - pl->x;
            dy = t->y - pl->y;
            if (dx + dy >= 0x140000)
                continue;
            a1 = bearing(dx, dy);
            pl = ENT(game_player_entity);
            a2 = bearing(tx - pl->x, ty - pl->y);
            d1 = a2 - a1;
            d2 = a1 - a2;
            if (d1 < d2)
                d2 = d1;
            if (d2 >= 0x400)
                continue;
            m = MsgAlloc();
            if (m == NULL)
                continue;
            aiacc_25AE48(m, from, U32(br, 1), ENT(game_player_entity)->f21, i);
            MsgSend(m);
        }
        for (i = 0; i < GAME_MAX_SQUADS; i++) {
            fix dx, dy;
            fixang a1, a2, d1, d2;
            sq = (uchar *)&game_squads[i];
            if (sq[0] == 0 || sq[7] == 1)
                continue;
            pl = ENT(game_player_entity);
            dx = S32(sq, 0x16) - pl->x;
            dy = S32(sq, 0x1A) - pl->y;
            if (dx + dy >= 0x140000)
                continue;
            a1 = bearing(dx, dy);
            pl = ENT(game_player_entity);
            a2 = bearing(tx - pl->x, ty - pl->y);
            d1 = a2 - a1;
            d2 = a1 - a2;
            if (d1 < d2)
                d2 = d1;
            if (d2 >= 0x400)
                continue;
            m = MsgAlloc();
            if (m == NULL)
                continue;
            aiacc_25AE48(m, from, U32(sq, 3), ENT(game_player_entity)->f21, i);
            MsgSend(m);
        }
    }
#ifndef TN_WATCOM_ABI
    if ((short)e == 0)                  /* P127: a free shot from the arm aims under the pointer */
        port_arms_free_aim(B8(pba, 0x46 + S8(pba, 0xCE) * 15), B8(pba, 0x45 + S8(pba, 0xCE) * 15), view,
                           &tx, &ty, &tz);
#endif
    if (!(uchar)pbasim_28A878(game_player_slot, tx, ty, tz, (short)e, 1, 0))
        return;
    if ((short)e == 0)
        return;
    b = ENT(e)->f21;
    if (b == 0 || no_alert)
        return;
    if (ENT(e)->cls != 1)
        return;
    m = MsgAlloc();
    if (m) {
        aiacc_25AE48(m, from, 0x80 | ((ulong)b << 16), ENT(game_player_entity)->f21, b);
        MsgSend(m);
    }
    if (game_brains[b].f005 == 1)
        return;
    m = MsgAlloc();
    if (m == NULL)
        return;
    aiacc_25AE48(m, from, 0x81 | ((ulong)game_brains[b].f006 << 16), ENT(game_player_entity)->f21, b);
    MsgSend(m);
}

void reticle_edge_msg(long n)
{
    ushort w = game_36F49C;
    Msg *m = MsgAlloc();
    if (m == NULL)
        return;
    aiacc_25B364(m, 0x85, 2 | ((ulong)w << 16), (uchar)n);
    MsgSend(m);
}
