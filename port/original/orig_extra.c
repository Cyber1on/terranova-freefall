/* orig_extra.c -- C versions of original routines that the decompilation kept as machine code
 * (small asm helpers of the libraries and the game), written for the standalone build.  The
 * verification build still runs the original code for them.  Each one follows the original
 * instruction by instruction; the address is the original's. */
#include "fix.h"

/* 0x30FC90 fix_pow (FIX library): x^y.  The integer part of y by squaring (16 bits, low bit
 * first), the fraction by repeated square roots of x (bit 15 of the fraction first).
 * Products are truncating fixed-point multiplies (imul / shrd 16). */
static long fmul(long a, long b)
{
    return (long)(((long long)a * b) >> 16);
}

fix fix_pow(fix x, fix y)
{
    long r = 0x10000, cur = x, sq = x;
    unsigned short ip = (unsigned short)(y >> 16), frac = (unsigned short)y;
    int i;

    for (i = 0; i < 16; i++) {
        if (ip & 1)
            r = fmul(r, cur);
        if (ip != 0)
            cur = fmul(cur, cur);
        ip >>= 1;
        if (frac != 0)
            sq = fix_sqrt(sq);
        if (frac & 0x8000)
            r = fmul(r, sq);
        frac <<= 1;
    }
    return r;
}

/* 0x2F2C2C tnui_replay_task (task 5): one poll of every input device, then the player
 * controls from them (a chain of calls ending in a jump). */
void KeyBitmapUpdate(void);
void MouseStateUpdate(void);
void TNJoyPoll(void);
void hmd_poll(void);
void CtrlClearSlots(void);
void CtrlKeyboard(void);
void CtrlJoystick(void);
void CtrlHmd(void);

void tnui_replay_task(void)
{
    KeyBitmapUpdate();
    MouseStateUpdate();
    TNJoyPoll();
    hmd_poll();
    CtrlClearSlots();
    CtrlKeyboard();
    CtrlJoystick();
    CtrlHmd();
}
