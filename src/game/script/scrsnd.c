/* scrsnd.c -- TN script sound opcodes.  Decompiled from __FF.EXE (object file
 * 0x2EAAF0-0x2EAB2B; names ours).
 *
 *   0x2EAAF0 ScriptOpSound       0x2EAB08 ScriptOpMusic
 */
#include "t1_a.h"
#include "g1_c.h"      /* music_2467B8 (owner g1c) */

/* opcode table (0x35F8AC in the original; registered by main) */
const ScriptOpReg script_snd_ops[] = {
    { 0x68, WATCOM_FN(ScriptOpSound) },
    { 0x69, WATCOM_FN(ScriptOpMusic) },
    { 0, NULL }
};

/* op 0x68 b: (v --) does nothing with its operands (sound effects removed?) */
uchar *ScriptOpSound(uchar *pc)
{
    script_sp--;
    return pc + 2;
}

/* op 0x69 b: (v --) music (music.c 0x2467B8) */
uchar *ScriptOpMusic(uchar *pc)
{
    script_sp--;
    music_2467B8(script_stack[script_sp]);
    return pc + 2;
}
