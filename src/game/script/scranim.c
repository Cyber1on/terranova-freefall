/* scranim.c -- TN script animation opcodes (delta animation player).  Decompiled from
 * __FF.EXE (object file 0x2E8800-0x2E8AB1; names ours).
 *
 *   0x2E8800 ScriptOpAnimPlay    0x2E88FC ScriptOpAnimSet     0x2E8968 ScriptOpAnimWait
 *   0x2E89A8 ScriptOpAnimStop    0x2E89CC ScriptOpAnimColor   0x2E8A18 ScriptOpAnimMove
 *   0x2E8A78 ScriptOpAnimRef     0x2E8AA0 ScriptAnimDone
 */
#include "t1_a.h"

/* opcode table (0x35F6B8 in the original; registered by main) */
const ScriptOpReg script_anim_ops[] = {
    { 0x40, WATCOM_FN(ScriptOpAnimPlay) },
    { 0x41, WATCOM_FN(ScriptOpAnimSet) },
    { 0x42, WATCOM_FN(ScriptOpAnimWait) },
    { 0x43, WATCOM_FN(ScriptOpAnimStop) },
    { 0x44, WATCOM_FN(ScriptOpAnimColor) },
    { 0x45, WATCOM_FN(ScriptOpAnimMove) },
    { 0x46, WATCOM_FN(ScriptOpAnimRef) },
    { 0, NULL }
};

/* op 0x40 flags: (ref x y z -- [anim]) starts an animation; flags 0x80 push it, 1 wait for it */
uchar *ScriptOpAnimPlay(uchar *pc)
{
    short pos[4];
    void *anim;

    script_sp--;
    pos[2] = (short)script_stack[script_sp];
    script_sp--;
    pos[1] = (short)script_stack[script_sp];
    script_sp--;
    pos[0] = (short)script_stack[script_sp];
    script_sp--;
    anim = tnui2_2FD344(script_stack[script_sp], (AnimPos *)pos);
    anim_2E6B3C(anim, script_cur, 0x0C, WATCOM_FN(ScriptAnimDone));
    if (pc[1] & 0x80) {
        script_stack[script_sp] = (long)anim;
        script_tags[script_sp] = 0;
        script_sp++;
    }
    if (pc[1] & 1)
        ScriptSetWait(script_cur, SCRIPT_ANIM, (long)anim, 0);
    return pc + 2;
}

/* op 0x41 wait: (anim arg --) */
uchar *ScriptOpAnimSet(uchar *pc)
{
    long arg, anim;

    script_sp--;
    arg = script_stack[script_sp];
    script_sp--;
    anim = script_stack[script_sp];
    anim_2E695C((void *)anim, arg);
    if (pc[1]) {
        ScriptSetWait(script_cur, SCRIPT_ANIM, anim, 0);
        script_done = 1;
    }
    return pc + 2;
}

/* op 0x42: (anim --) waits for the animation */
uchar *ScriptOpAnimWait(uchar *pc)
{
    script_sp--;
    ScriptSetWait(script_cur, SCRIPT_ANIM, script_stack[script_sp], 0);
    script_done = 1;
    return pc + 1;
}

/* op 0x43: (anim --) */
uchar *ScriptOpAnimStop(uchar *pc)
{
    script_sp--;
    anim_2E6A34((void *)script_stack[script_sp]);
    return pc + 1;
}

/* op 0x44: (anim color --) +0x39 = (high byte (signed) << 16) | (low byte << 8) */
uchar *ScriptOpAnimColor(uchar *pc)
{
    short c = (short)script_stack[script_sp - 1];
    uchar *anim = (uchar *)script_stack[script_sp - 2];

    script_sp -= 2;
    *(long *)(anim + 0x39) = ((long)(c >> 8) << 16) | ((long)(c & 0xFF) << 8);
    return pc + 1;
}

/* op 0x45: (anim a b c --) the three values become 16.16 */
uchar *ScriptOpAnimMove(uchar *pc)
{
    long c = script_stack[script_sp - 1] << 16;
    long b = script_stack[script_sp - 2] << 16;
    long a = script_stack[script_sp - 3] << 16;
    long anim = script_stack[script_sp - 4];

    script_sp -= 4;
    anim_2E6AAC((void *)anim, a, b, c);
    return pc + 1;
}

/* op 0x46: (ref --) */
uchar *ScriptOpAnimRef(uchar *pc)
{
    script_sp--;
    anim_2E6B98(NULL, script_stack[script_sp]);
    return pc + 1;
}

/* Animation end callback (anim +0x18 = the thread that started it). */
void ScriptAnimDone(uchar *anim)
{
    ScriptThread *t = *(ScriptThread **)(anim + 0x18);

    if (t->wait == SCRIPT_ANIM)
        t->wait = SCRIPT_RUN;
}
