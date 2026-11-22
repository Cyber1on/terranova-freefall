/* play.c -- test actions that play a mission's goals without a player (the game's thread).
 *
 * The test script (FREEFALL_SCRIPT, platform.c) asks with T<n>; the request is carried out at the
 * next frame's end (port_frame_limit), in a mission:
 *   T1   the player destroys everything the mission's kill goals count: every entity of the
 *        groups and classes in the goals' "at least so many killed" tables (ordnance.c), through
 *        the game's own damage (objdmg_drain: all their hit points, and they are destroyed at
 *        once), so that the mission's script sees its goals met the way it does in play;
 *   T2   the pickup point is where the player stands, and the player gives the pickup command
 *        (ordnance_291660: it calls the dropship, and once the dropship has landed it boards);
 *   T3   the player dies;
 *   T4   only the report: the goals' states and kill counts;
 *   T5   the mission's groups of entities;
 *   T6   the player targets the first entity of group 1 (the goal of the game's mission 1);
 *   T7   the player is put 50 m from that entity, on the side the player came from;
 *   T9   the player is put 12 m from the pickup point.
 * The log says what was done. */
#include "g2_c.h"
#include "g2_b.h"
#include <math.h>
#include "port.h"
#include "sharp3d.h"

extern OrdnGoal ordn_goal_b[3][6];
extern long ordn_flags[3];
void objdmg_drain(long ent, long killer, long amount);
ushort objdmg_get_hp(long ent);
void obj_destroy(long ent, long mode, long owner);
void ordnance_291660(void);
int port_keys_in_mission(void);
long helm_262E34(void);
void helm_262D4C(long ent);
ulong ordnance_291720(long id);
long ordnance_291080(void);
fix ordnance_2911D0(void);
extern short strike_entity;
extern long strike_35BF40;

volatile int port_test_req;

void port_test_run(void)
{
    int req = port_test_req;
    short player = (short)game_player_entity, e;
    int g, k, n = 0, left = 0;

    static long again_x, again_y;

    port_test_req = 0;
    if (!port_keys_in_mission())        /* (not in a movie, a menu or the debriefing) */
        return;
    if (req == 8) {                     /* (T7's second frame: see port_player_place) */
        port_player_place(again_x, again_y, G2C_ENT(player).heading);
        return;
    }
    port_log("test: T%d at game time %lu s, player %d, goals' states %d %d %d\n", req, (unsigned long)(game_time / 1000),
             player, (schar)ordn_mission_state[0x809], (schar)ordn_mission_state[0x80A],
             (schar)ordn_mission_state[0x80B]);
    port_log("test: the pickup: status %ld, aircraft %d, %ld from the pickup point, the mission's end flag %ld\n",
             (long)ordnance_291080(), strike_entity, (long)(ordnance_2911D0() >> 16), strike_35BF40);
    if (!player)
        return;
    switch (req) {
    case 1:
        for (g = 0; g < 3; g++)
            for (k = 0; k < 5 && ordn_flags[g] && ordn_goal_b[g][k].id != -1; k++) {
                long id = ordn_goal_b[g][k].id;
                for (e = (short)game_entities[0].f05; e; e = (short)G2C_ENT(e).f07) {
                    if (e == player || !(G2C_ENT(e).f23 == id || G2C_ENT(e).cls + 0x100 == id))
                        continue;
                    port_log("test:   target %d: class %d type %d, %d hit points\n", e, G2C_ENT(e).cls,
                             G2C_ENT(e).type, objdmg_get_hp(e));
                    if (objdmg_get_hp(e)) {
                        objdmg_drain(e, 0, 0x7FFF);
                        n++;
                    } else if (G2C_ENT(e).active && G2C_ENT(e).cls == 0) {
                        obj_destroy(e, 1, 0);   /* (a map object: it has no damage record) */
                        left++;
                    }
                }
                port_log("test: goal %d counts %d of group %ld\n", g + 1, ordn_goal_b[g][k].cnt, id);
            }
        port_log("test: the player destroyed %d of the goals' targets and %d map objects among them\n", n, left);
        break;
    case 2:
        game_4414C8 = G2C_ENT(player).x;
        game_4414CC = G2C_ENT(player).y;
        k = (int)ordnance_291080();
        if (k != 0 && k != 2)           /* (on its way, or landing) */
            break;
        port_log("test: the pickup point is at the player (%ld, %ld); the pickup command is given (%s)\n",
                 (long)(game_4414C8 >> 16), (long)(game_4414CC >> 16), k ? "boarding" : "the call");
        ordnance_291660();
        break;
    case 4:                             /* (a report: the goals' kill counts) */
        for (g = 0; g < 3; g++)
            for (k = 0; k < 5 && ordn_flags[g] && ordn_goal_b[g][k].id != -1; k++)
                port_log("test: goal %d: %lu of %d of group %ld killed\n", g + 1,
                         (unsigned long)ordnance_291720(ordn_goal_b[g][k].id), ordn_goal_b[g][k].cnt,
                         ordn_goal_b[g][k].id);
        break;
    case 5: {                           /* (a report: the mission's groups) */
        int cnt[256] = { 0 };
        for (e = (short)game_entities[0].f05; e; e = (short)G2C_ENT(e).f07)
            cnt[G2C_ENT(e).f23]++;
        for (k = 1; k < 256; k++)
            if (cnt[k])
                port_log("test: group %d: %d entities\n", k, cnt[k]);
        port_log("test: the target's group and side word: %ld\n", (long)helm_262E34());
        break;
    }
    case 6:                             /* the player targets the first entity of group 1 */
        for (e = (short)game_entities[0].f05; e; e = (short)G2C_ENT(e).f07)
            if (G2C_ENT(e).f23 == 1 && e != player) {
                helm_262D4C(e);
                if (!targ_current)      /* (out of the sensors' range: as if the player had come near) */
                    targ_current = e;
                port_log("test: the player targets entity %d (class %d type %d)\n", e, G2C_ENT(e).cls, G2C_ENT(e).type);
                break;
            }
        break;
    case 7:
        for (e = (short)game_entities[0].f05; e; e = (short)G2C_ENT(e).f07)
            if (G2C_ENT(e).f23 == 1 && e != player) {
                double dx = (double)G2C_ENT(player).x - G2C_ENT(e).x, dy = (double)G2C_ENT(player).y - G2C_ENT(e).y;
                double d = sqrt(dx * dx + dy * dy), r = 50.0 / 6 * 65536;   /* (a map unit is 6 m) */
                if (d <= r)
                    break;
                again_x = G2C_ENT(e).x + (long)(dx / d * r);
                again_y = G2C_ENT(e).y + (long)(dy / d * r);
                port_log("test: the player is put at (%ld, %ld), 50 m from entity %d at (%ld, %ld)\n", again_x >> 16,
                         again_y >> 16, e, (long)(G2C_ENT(e).x >> 16), (long)(G2C_ENT(e).y >> 16));
                port_player_place(again_x, again_y, G2C_ENT(player).heading);
                port_test_req = 8;
                break;
            }
        break;
    case 9:
        again_x = game_4414C8 + 0x20000;
        again_y = game_4414CC;
        port_log("test: the player is put at (%ld, %ld), by the pickup point\n", again_x >> 16, again_y >> 16);
        port_player_place(again_x, again_y, G2C_ENT(player).heading);
        port_test_req = 8;
        break;
    case 3:
        objdmg_drain(player, 0, 0x7FFF);
        port_log("test: the player dies\n");
        break;
    }
}
