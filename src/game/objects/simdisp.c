/* simdisp.c -- Terra Nova module M057 (0x2895C0-0x2898FC; source file name unknown): start /
 * stop of the object simulations, the per-frame sim hook that runs every simulation and
 * dispatches the sim messages, and a vector rotation helper.  Decompiled from __FF.EXE v1.09.
 *
 *   0x2895C0 sim_flush_key (data: MsgAddr {0x83, 0}; the map lists it as a function)
 *   0x2895C4 simdisp_2895C4 sim start      0x289660 simdisp_289660 sim stop
 *   0x28968C sim_nop (empty handler in sim_handlers[6..8])
 *   0x289690 SimUpdateAll (frame hook slot 10)
 *   0x289730 simdisp_289730 rotate a vector by heading / pitch / bank
 *
 * Quirks kept:
 *  - SimUpdateAll reads the entity class of every message whose pad2 byte is 0x11 (also for
 *    messages that are not from 0x84) and calls sim_handlers[class] without a range check.
 *  - simdisp_289730: the y result uses fix_mul(sin h, fix_mul(sin p, cos b)) - fix_mul(cos h,
 *    cos b) where a rotation would have sin b in the second product; the formula is kept.
 */
#include "g2_a.h"
#include "g4_a.h"      /* AI callees (owners g4a/g4b/g4c1/g4c2; G4 merge) */
#include "g6_b.h"     /* added at the G5-G8 merge */
#include "g6_c.h"     /* added at the G5-G8 merge */

extern MsgAddr sim_flush_key;        /* 0x2895C0 */

void simdisp_2895C4(void)
{
    *((uchar *)&game_hook_mask + 1) |= 4;
    game_3970E0 = 0;
    game_hooks[10] = (GameHook)WATCOM_FN(SimUpdateAll);
    killobj_2892D0();
    killobj_28952C(5, WATCOM_FN(ordnance_290B38), 2);
    weapons_28F020();
    timers_27A0C0();
    static_2863B8();
    killobj_28952C(1, WATCOM_FN(aimain_24A768), 2);
    killobj_28952C(3, WATCOM_FN(aimain_24A768), 2);
    killobj_28952C(4, WATCOM_FN(aimain_24A768), 2);
    killobj_28952C(2, WATCOM_FN(aimain_24A768), 2);
}

void simdisp_289660(void)
{
    MsgAddr key = sim_flush_key;
    static_286488();
    weapons_28F020();
    timers_27A0C0();
    *((uchar *)&game_hook_mask + 1) &= 0xFB;
    MsgFlush(&key);
}

void sim_nop(Msg *msg)
{
    (void)msg;
}

void SimUpdateAll(void)
{
    ulong dt = game_frame_dt;
    Msg *m;
    sfx_28CC68(dt);
    ordnance_2908D0(dt);
    pbasim_289B94(dt);
    vehsim_28E850(dt);
    aircraft_283B88(dt);
    instsim_288278(dt);
    flash_28DE40(dt);
    aihelp_2D2F5C(dt);
    static_28808C(game_time);
    while ((m = MsgReceive(&sim_msg_key)) != NULL) {
        if (m->pad2 == 0x11) {
            uchar cls = game_entities[m->from.b].cls;
            if (m->from.a == 0x84)
                WATCOM_CALL(sim_handlers[cls], (long)m, (long)m, (long)m, (long)cls);
        }
        MsgFree(m);
    }
    killobj_28934C();
}

void simdisp_289730(fix *px, fix *py, fix *pz, ushort h, ushort p, ushort b)
{
    fix x = *px, y = *py, z = *pz;
    fix sh, ch, sp, cp, sb, cb;
    fix nx, ny;
    fix_sincos(h, &sh, &ch);
    fix_sincos((fixang)-p, &sp, &cp);
    fix_sincos(b, &sb, &cb);
    nx = fix_mul(x, fix_mul(ch, cp));
    nx -= fix_mul(y, fix_mul(ch, fix_mul(sp, sb)) + fix_mul(sh, cb));
    nx -= fix_mul(z, fix_mul(ch, fix_mul(sp, cb)) - fix_mul(sh, sb));
    *px = nx;
    ny = fix_mul(x, fix_mul(sh, cp));
    ny -= fix_mul(y, fix_mul(sh, fix_mul(sp, cb)) - fix_mul(ch, cb));
    ny -= fix_mul(z, fix_mul(sh, fix_mul(sp, cb)) + fix_mul(ch, sb));
    *py = ny;
    *pz = fix_mul(x, sp) + fix_mul(y, fix_mul(cp, sb)) + fix_mul(z, fix_mul(cp, cb));
}
