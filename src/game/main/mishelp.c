/* mishelp.c -- mission view helpers, module M103 (Terra Nova v1.09, __FF.EXE).
 *
 *   0x2C9130 mis103_2C9130   reset the player view pitch / ground state
 *   0x2C9180 mis103_2C9180   move the view pitch or heading toward a target (7 kinds)
 *
 * The jump table of mis103_2C9180 sits at 0x2C9164 (between the two functions).
 */
#include "g9_b.h"

void mis103_2C9130(void)
{
    hud_441357 = 0;
    game_441365 = 0;
    game_44135D = 0;
    game_441361 = 0;
    game_441359 = 2;
}

/* Step a 16-bit angle toward target: equal within one step, +step when a little ahead,
 * +2*step when further ahead; behind: -step when within 4 steps, else -2*step. */
static ushort step_toward(ushort cur, ushort target, ushort step)
{
    ushort d = (ushort)(target - cur);
    ulong s = step;
    if (d < step || (ulong)d > 0xffff - s)
        return target;
    if ((ulong)d < s * 4)
        return (ushort)(cur + step);
    if (d < 0x8000)
        return (ushort)(cur + 2 * step);
    if ((ulong)d > 0xffff - s * 4)
        return (ushort)(cur - step);
    return (ushort)(cur - 2 * step);
}

int mis103_2C9180(int kind, long val, uchar *changed)
{
    ushort v, old, step;

    *changed = 0;
    switch (kind) {
    case 5:                                  /* set pitch */
        old = (ushort)hud_441357;
        hud_441357 = (short)val;
        *changed = (ushort)((ushort)val - old) != 0;
        return -1;
    case 0:                                  /* pitch toward val */
        step = (ushort)(game_frame_dt * 5);
        if (step > 0x2000)
            step = 0x2000;
        old = (ushort)hud_441357;
        hud_441357 = (short)step_toward(old, (ushort)val, step);
        *changed = (ushort)((ushort)val - old) != 0;
        return 2;
    case 1:                                  /* pitch += val, clamped to +-0x2000 */
        v = (ushort)(hud_441357 + val);
        hud_441357 = (short)v;
        if (v < 0xe000 && v > 0x2000) {
            hud_441357 = (short)(v > 0x8000 ? 0xe000 : 0x2000);
            *changed = 0;
        } else
            *changed = 1;
        return 1;
    case 6:                                  /* set heading */
        old = (ushort)hud_441355;
        hud_441355 = (short)val;
        *changed = (ushort)((ushort)val - old) != 0;
        return -1;
    case 2:                                  /* heading toward val */
        step = (ushort)(game_frame_dt * 10);
        if (step > 0x2000)
            step = 0x2000;
        old = (ushort)hud_441355;
        hud_441355 = (short)step_toward(old, (ushort)val, step);
        *changed = (ushort)((ushort)val - old) != 0;
        return (short)val != 0 ? 0 : 3;
    case 3:                                  /* heading += val, clamped to +-0x4000 */
        v = (ushort)(hud_441355 + val);
        hud_441355 = (short)v;
        if (v > 0x4000 && v < 0xc000) {
            hud_441355 = (short)(v < 0x8000 ? 0x4000 : 0xc000);
            *changed = 0;
        } else if (val != 0)
            *changed = 1;
        return 0;
    default:
        return -1;
    }
}
