/* scrinterp.c -- TN script interpreter: opcode table, dispatch loop, core opcodes, string
 * formatting.  Decompiled from __FF.EXE (object file 0x2E8AC0-0x2EA4B6; names ours).
 *
 *   0x2E8AC0 ScriptOpsInit       0x2E8B04 ScriptRegisterOps   0x2E8B2C ScriptRun
 *   0x2E8C60 ScriptOpPushByte    0x2E8C98 ScriptOpPushShort   0x2E8CD0 ScriptOpPushLong
 *   0x2E8D2C ScriptOpPushVar     0x2E9000 ScriptOpPushString  0x2E9038 ScriptOpDrop
 *   0x2E908C ScriptOpBinary      0x2E91B8 ScriptOpUnary       0x2E9200 ScriptOpIndex
 *   0x2E9274 ScriptOpLoadIndexed 0x2E9318 ScriptOpStore       0x2E9394 ScriptOpJumpFalse
 *   0x2E93E0 ScriptOpCall        0x2E9494 ScriptOpReturn      0x2E9548 ScriptOpEnter
 *   0x2E95E8 ScriptOpPushArg     0x2E9670 ScriptOpNative      0x2E98B4 ScriptOpCase
 *   0x2E9904 ScriptOpCaseString  0x2E99A0 ScriptOpJump        0x2E99C8 ScriptOpSpawn
 *   0x2E9A0C ScriptOpWake        0x2E9A78 ScriptOpWait        0x2E9ACC ScriptOpYield
 *   0x2E9AD8 ScriptOpWaitUntil   0x2E9B18 ScriptOpWaitFor     0x2E9B60 ScriptOpSleep
 *   0x2E9BA0 ScriptOpChoice      0x2E9D30 ScriptOpPrint       0x2E9DB4 ScriptOpWaitKey
 *   0x2E9E00 ScriptOpMsgNew      0x2E9E18 ScriptOpMsgSet      0x2E9E7C ScriptOpMsgSend
 *   0x2E9F0C ScriptOpMsgWait     0x2E9F64 ScriptOpMsgIs       0x2E9FA8 ScriptOpMsgFree
 *   0x2E9FD4 ScriptOpLine        0x2E9FD8 ScriptOpNull        0x2E9FDC ScriptOpEnd
 *   0x2EA000 ScriptFormat        0x2EA060 ScriptFormatArg     0x2EA368 ScriptReadLong
 *   0x2EA37C ScriptReadShort     0x2EA390 ScriptPop           0x2EA3B8 ScriptPush
 *   0x2EA3E8 ScriptPopDiscard    0x2EA404 ScriptLocalPush     0x2EA458 ScriptLocalPop
 *   0x2EA474 ScriptLocalAlloc
 *
 * Machine: an evaluation stack of 64 longs with a type tag byte each (script_stack /
 * script_tags / script_sp), saved in the thread (up to 15 entries) while it is suspended; a
 * per-thread local stack of bytes (lstack, lsp, frame pointer lfp) for call frames, arguments
 * and locals.  Every handler takes the address of its opcode and returns the next one; a
 * handler that suspends the thread sets script_done.  Jump targets are offsets from the data
 * of the script resource (code + 6 + 4 * number of items).
 * Tags: 0 value, 1/2 address of a global, 3 address of a local, 4 index of an engine variable,
 * 5 string ref, 6 "no message".
 *
 * Quirks kept: unary minus and logical not (ops 0x1F/0x20) switch on the operand value instead
 * of the opcode, so they only change the values 0x1F and 0x20; modulo by 0 faults (division
 * returns 0); op 0x22 loads a short, and with an address tag 0 or 4 pushes whatever the
 * dispatch loop left in esi (here: see ScriptRun); op 0x39 stores only a word for type 4;
 * op 0x3E returns a NULL pc without ending the loop; op 0x35 keeps the condition of the
 * previous entry for entry types 5..0x7FFF (uninitialised for the first: here 0); the index
 * suffix of format codes (0xC9) is compared sign-extended and never matches.
 */
#include "t1_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
void *script_ops[256];
uchar script_tags[64];
long script_stack[64];
long **script_vartab;
ScriptFunc *script_functab;
ScriptThread *script_cur;
short script_sp;
uchar script_done;
short script_choice_ids[8];
char *script_choice_strs[8];
char script_strbuf[500];
char *script_strbuf_end = script_strbuf + 500;
#endif

#define PACKED __attribute__((packed))
#define OPS(o, f) { o, WATCOM_FN(f) }
#define DATA(t) (t->code + *(ushort *)t->code * 4 + 6)   /* jump base of a script */

const ScriptOpReg script_core_ops[] = {
    OPS(0x01, ScriptOpPushByte), OPS(0x02, ScriptOpPushShort), OPS(0x03, ScriptOpPushLong),
    OPS(0x04, ScriptOpPushVar), OPS(0x05, ScriptOpPushVar), OPS(0x06, ScriptOpPushVar),
    OPS(0x07, ScriptOpPushVar), OPS(0x08, ScriptOpPushVar), OPS(0x09, ScriptOpPushVar),
    OPS(0x0A, ScriptOpPushVar), OPS(0x0B, ScriptOpPushVar), OPS(0x0C, ScriptOpPushVar),
    OPS(0x0D, ScriptOpPushString), OPS(0x0E, ScriptOpDrop),
    OPS(0x0F, ScriptOpBinary), OPS(0x10, ScriptOpBinary), OPS(0x11, ScriptOpBinary),
    OPS(0x12, ScriptOpBinary), OPS(0x13, ScriptOpBinary), OPS(0x14, ScriptOpBinary),
    OPS(0x15, ScriptOpBinary), OPS(0x16, ScriptOpBinary), OPS(0x17, ScriptOpBinary),
    OPS(0x18, ScriptOpBinary), OPS(0x19, ScriptOpBinary), OPS(0x1A, ScriptOpBinary),
    OPS(0x1B, ScriptOpBinary), OPS(0x1C, ScriptOpBinary), OPS(0x1D, ScriptOpBinary),
    OPS(0x1E, ScriptOpBinary), OPS(0x1F, ScriptOpUnary), OPS(0x20, ScriptOpUnary),
    OPS(0x21, ScriptOpIndex), OPS(0x22, ScriptOpLoadIndexed), OPS(0x23, ScriptOpStore),
    OPS(0x24, ScriptOpJumpFalse), OPS(0x25, ScriptOpCall), OPS(0x26, ScriptOpReturn),
    OPS(0x27, ScriptOpEnter), OPS(0x29, ScriptOpPushArg), OPS(0x2A, ScriptOpNative),
    OPS(0x2B, ScriptOpCase), OPS(0x2C, ScriptOpCaseString), OPS(0x2D, ScriptOpJump),
    OPS(0x2E, ScriptOpSpawn), OPS(0x2F, ScriptOpWake), OPS(0x30, ScriptOpWait),
    OPS(0x31, ScriptOpYield), OPS(0x32, ScriptOpWaitUntil), OPS(0x33, ScriptOpWaitFor),
    OPS(0x34, ScriptOpSleep), OPS(0x35, ScriptOpChoice), OPS(0x36, ScriptOpPrint),
    OPS(0x37, ScriptOpWaitKey), OPS(0x38, ScriptOpMsgNew), OPS(0x39, ScriptOpMsgSet),
    OPS(0x3A, ScriptOpMsgSend), OPS(0x3B, ScriptOpMsgWait), OPS(0x3C, ScriptOpMsgIs),
    OPS(0x3D, ScriptOpMsgFree), OPS(0x3E, ScriptOpNull), OPS(0x3F, ScriptOpEnd),
    OPS(0x7F, ScriptOpLine), { 0, NULL }
};

/* esi of the original dispatch loop (see ScriptOpLoadIndexed) */
static long run_esi;

static inline void push(long v, uchar tag)
{
    script_stack[script_sp] = v;
    script_tags[script_sp] = tag;
    script_sp++;
}

/* local stack of the current thread: grows by 0x40 when full */
static inline void lgrow(ScriptThread *t)
{
    if (t->lsp >= t->lcap) {
        t->lcap += 0x40;
        script_cur->lstack = (*f_realloc)(t->lstack, t->lcap);
    }
}

void ScriptOpsInit(void)
{
    int i;

    for (i = 0; i < 256; i++)
        script_ops[i] = WATCOM_FN(ScriptOpEnd);
    ScriptRegisterOps(script_core_ops);
}

void ScriptRegisterOps(const ScriptOpReg *tab)
{
    for (; tab->op; tab++)
        script_ops[tab->op] = tab->fn;
}

/* Runs thread t until a handler sets script_done; keeps its evaluation stack (a thread with
 * 16 or more entries left is killed). */
void ScriptRun(ScriptThread *t)
{
    uchar *pc;
    short sp;

    script_cur = t;
    run_esi = 0;    /* original: the caller's esi */
    if (t->saved_sp) {
        memcpy(script_stack, t->saved_val, t->saved_sp * 4);
        memcpy(script_tags, script_cur->saved_tag, script_cur->saved_sp);
        run_esi = (long)(script_cur->saved_tag + script_cur->saved_sp);
    }
    script_sp = script_cur->saved_sp;
    pc = script_cur->code + script_cur->pc;
    script_done = 0;
    do
        pc = ((ScriptOp)script_ops[*pc])(pc);
    while (!script_done);
    t = script_cur;
    t->pc = (ushort)((ushort)(ulong)pc - (ushort)(ulong)t->code);
    sp = script_sp;
    if (sp) {
        if (sp >= 16) {
            ScriptSetWait(t, SCRIPT_DEAD, 0, 0);
            script_done = 1;
            return;
        }
        memcpy(t->saved_val, script_stack, script_sp * 4);
        memcpy(script_cur->saved_tag, script_tags, script_sp);
    }
    script_cur->saved_sp = script_sp;
}

/* op 0x01 b: push signed byte */
uchar *ScriptOpPushByte(uchar *pc)
{
    push((signed char)pc[1], 0);
    return pc + 2;
}

/* op 0x02 w */
uchar *ScriptOpPushShort(uchar *pc)
{
    push(*(short *)(pc + 1), 0);
    return pc + 3;
}

/* op 0x03 l */
uchar *ScriptOpPushLong(uchar *pc)
{
    push(*(long *)(pc + 1), 0);
    return pc + 5;
}

/* ops 0x04-0x0C: variables.  4/5 global, 6 local, 7 engine variable (values); 8/9 global,
 * 0x0A local, 0x0B engine variable (addresses, tags 1, 2, 3, 4); 0x0C field of the received
 * message (byte: low 5 bits offset in the data, 0x1E type, 0x1F sender; high 3 bits size). */
uchar *ScriptOpPushVar(uchar *pc)
{
    short sp = script_sp;
    uchar *m;
    uchar off, size;
    long v;

    switch (pc[0]) {
    case 0x04: case 0x05:
        script_stack[sp] = *(long *)(script_globals + *(ushort *)(pc + 1));
        script_tags[sp] = 0;
        sp++;
        break;
    case 0x06:
        script_stack[sp] = *(long *)(script_cur->lstack + script_cur->lfp + *(short *)(pc + 1));
        script_tags[sp] = 0;
        sp++;
        break;
    case 0x07:
        script_stack[sp] = *script_vartab[*(ushort *)(pc + 1)];
        script_tags[sp] = 0;
        sp++;
        break;
    case 0x08:
        script_stack[sp] = *(ushort *)(pc + 1);
        script_tags[sp] = 1;
        sp++;
        break;
    case 0x09:
        script_stack[sp] = *(ushort *)(pc + 1);
        script_tags[sp] = 2;
        sp++;
        break;
    case 0x0A:
        script_stack[sp] = *(short *)(pc + 1) & ~1;
        script_tags[sp] = 3;
        sp++;
        break;
    case 0x0B:
        script_stack[sp] = *(ushort *)(pc + 1);
        script_tags[sp] = 4;
        sp++;
        break;
    case 0x0C:
        off = pc[1] & 0x1F;
        size = pc[1] >> 5;
        m = script_cur->msg_in;
        if (m == NULL) {
            script_stack[sp] = 0;
            script_tags[sp] = 6;
            sp++;
        } else if (off == 0x1E) {
            script_stack[sp] = SMSG_TYPE(m);
            script_tags[sp] = 0;
            sp++;
        } else if (off == 0x1F) {
            script_stack[sp] = SMSG_FROM(m);
            script_tags[sp] = 0;
            sp++;
        } else {
            m = SMSG_DATA(m) + off;
            if (size == 1)
                v = *(signed char *)m;
            else if (size == 2)
                v = *(short *)m;
            else if (size == 4)
                v = *(long *)m;
            else
                v = 0;
            script_stack[sp] = v;
            script_tags[sp] = 0;
            sp++;
        }
        script_sp = sp;
        return pc + 2;
    }
    script_sp = sp;
    return pc + 3;
}

/* op 0x0D ref: push a string ref (tag 5) */
uchar *ScriptOpPushString(uchar *pc)
{
    push(*(long *)(pc + 1), 5);
    return pc + 5;
}

/* op 0x0E */
uchar *ScriptOpDrop(uchar *pc)
{
    script_sp--;
    return pc + 1;
}

/* ops 0x0F-0x1E: (a b -- a op b); the tag of a stays */
uchar *ScriptOpBinary(uchar *pc)
{
    long *a, b;

    script_sp--;
    a = &script_stack[script_sp - 1];
    b = script_stack[script_sp];
    switch (pc[0]) {
    case 0x0F: *a += b; break;
    case 0x10: *a -= b; break;
    case 0x11: *a *= b; break;
    case 0x12: *a = b ? *a / b : 0; break;
    case 0x13: *a = *a % b; break;          /* faults when b == 0 */
    case 0x14: *a &= b; break;
    case 0x15: *a |= b; break;
    case 0x16: *a ^= b; break;
    case 0x17: *a = (*a != 0 && b != 0); break;
    case 0x18: *a = (*a != 0 || b != 0); break;
    case 0x19: *a = (b == *a); break;
    case 0x1A: *a = (b != *a); break;
    case 0x1B: *a = (b < *a); break;
    case 0x1C: *a = (b <= *a); break;
    case 0x1D: *a = (b > *a); break;
    case 0x1E: *a = (b >= *a); break;
    }
    return pc + 1;
}

/* ops 0x1F (negate) and 0x20 (not).  Bug: the switch is on the value, not the opcode. */
uchar *ScriptOpUnary(uchar *pc)
{
    long *v = &script_stack[script_sp - 1];

    if ((ulong)*v == 0x1F)
        *v = -*v;
    else if ((ulong)*v == 0x20)
        *v = (*v == 0);
    return pc + 1;
}

/* op 0x21: (index base -- base + (index - 1) * 4) with the tag of base */
uchar *ScriptOpIndex(uchar *pc)
{
    short sp = script_sp;
    uchar tag = script_tags[sp - 1];
    long base = script_stack[sp - 1];

    script_stack[sp - 2] = (script_stack[sp - 2] - 1) * 4 + base;
    script_tags[sp - 2] = tag;
    script_sp = sp - 1;
    return pc + 1;
}

/* op 0x22: (index base -- short element) with the tag of base */
uchar *ScriptOpLoadIndexed(uchar *pc)
{
    short sp = script_sp;
    uchar tag = script_tags[sp - 1];
    long base = script_stack[sp - 1];
    long i = script_stack[sp - 2] - 1;
    long v = run_esi;   /* tags 0 and 4: the original pushes its caller's esi */

    switch (tag) {
    case 1: case 2:
        v = *(short *)(script_globals + base + i * 4);
        break;
    case 3:
        v = *(short *)(script_cur->lstack + script_cur->lfp + base + i * 4);
        break;
    }
    script_stack[sp - 2] = v;
    script_tags[sp - 2] = tag;
    script_sp = sp - 1;
    return pc + 1;
}

#ifndef TN_WATCOM_ABI
extern int port_extra_on;               /* an extra mission is played (port/mods/extrares.c) */
long port_extra_state(long v);
#endif

/* op 0x23: (address value --) */
uchar *ScriptOpStore(uchar *pc)
{
    short sp = script_sp;
    long v = script_stack[sp - 1];
    uchar tag = script_tags[sp - 2];
    long addr = script_stack[sp - 2];

    switch (tag) {
    case 1: case 2:
#ifndef TN_WATCOM_ABI
        /* Freefall: the shell's screen number (global 4) while an extra mission is played: it
         * ends at the main menu, not in the campaign (port/mods/extrares.c) */
        if (addr == 4 && port_extra_on)
            v = port_extra_state(v);
#endif
        *(long *)(script_globals + addr) = v;
        break;
    case 3:
        *(long *)(script_cur->lstack + script_cur->lfp + addr) = v;
        break;
    case 4:
        *script_vartab[addr] = v;
        break;
    }
    script_sp = sp - 2;
    return pc + 1;
}

/* op 0x24 w: (cond --) jumps when 0 */
uchar *ScriptOpJumpFalse(uchar *pc)
{
    script_sp--;
    if (script_stack[script_sp])
        return pc + 3;
    return DATA(script_cur) + *(ushort *)(pc + 1);
}

/* op 0x25 ref: calls function REFINDEX(ref) of script REFID(ref); the return address
 * (resource << 16 | offset) goes on the local stack. */
uchar *ScriptOpCall(uchar *pc)
{
    ScriptThread *t = script_cur;
    Ref ref = *(long *)(pc + 1);
    ulong ret = ((ulong)t->res << 16) | (ushort)((ushort)(ulong)(pc + 5) - (ushort)(ulong)t->code);

    lgrow(t);
    t = script_cur;
    *(ulong *)(t->lstack + t->lsp) = ret;
    t->lsp += 4;
    ResUnlock(t->res);
    script_cur->res = (ushort)(ref >> 16);
    script_cur->code = ResLock(script_cur->res);
    return script_cur->code + *(long *)(script_cur->code + (ref & 0xFFFF) * 4 + 2);
}

/* op 0x26 w: returns (w = bytes of arguments to drop); returning from the outermost function
 * ends the thread. */
uchar *ScriptOpReturn(uchar *pc)
{
    ScriptThread *t = script_cur;
    ulong ret;

    t->lsp = t->lfp;
    t->lsp -= 4;
    t->lfp = *(ushort *)(t->lstack + t->lsp);
    if (t->lsp == 0) {
        ScriptSetWait(t, SCRIPT_DEAD, 0, 0);
        script_done = 1;
        return NULL;
    }
    ResUnlock(t->res);
    t = script_cur;
    t->lsp -= 4;
    ret = *(ulong *)(t->lstack + t->lsp);
    t->res = (ushort)(ret >> 16);
    t->code = ResLock((ushort)(ret >> 16));
    script_cur->lsp -= *(ushort *)(pc + 1);
    return script_cur->code + (ret & 0xFFFF);
}

/* op 0x27 w: function entry: saves the frame pointer, reserves w bytes of locals */
uchar *ScriptOpEnter(uchar *pc)
{
    ScriptThread *t = script_cur;
    ushort fp = t->lfp;

    lgrow(t);
    t = script_cur;
    *(ulong *)(t->lstack + t->lsp) = fp;
    t->lsp += 4;
    t->lfp = t->lsp;
    t->lsp += *(ushort *)(pc + 1);
    if (t->lsp >= t->lcap) {
        t->lcap = t->lsp + 0x40;
        script_cur->lstack = (*f_realloc)(t->lstack, t->lcap);
    }
    return pc + 3;
}

/* op 0x29: (v --) pushes an argument on the local stack */
uchar *ScriptOpPushArg(uchar *pc)
{
    ScriptThread *t = script_cur;
    long v;

    script_sp--;
    v = script_stack[script_sp];
    lgrow(t);
    t = script_cur;
    *(long *)(t->lstack + t->lsp) = v;
    t->lsp += 4;
    return pc + 1;
}

/* pops a long of the local stack of thread t */
static inline long lpop(ScriptThread *t)
{
    t->lsp -= 4;
    return *(long *)(t->lstack + t->lsp);
}

/* op 0x2A w n: (-- result) calls engine function w with n (0-5) arguments from the local
 * stack (the last pushed is the first argument). */
uchar *ScriptOpNative(uchar *pc)
{
    ScriptThread *t = script_cur;
    short i = *(short *)(pc + 1);
    long a, b, c, d, e, r;

    switch (pc[3]) {
    case 0:
        r = WATCOM_CALL(script_functab[i], (long)script_functab, i * 4);
        break;
    case 1:
        a = lpop(t);
        r = WATCOM_CALL(script_functab[i], a);
        break;
    case 2:
        a = lpop(t);
        b = lpop(t);
        r = WATCOM_CALL(script_functab[i], a, b);
        break;
    case 3:
        a = lpop(t);
        b = lpop(t);
        c = lpop(t);
        r = WATCOM_CALL(script_functab[i], a, b, c, (long)&script_functab[i]);
        break;
    case 4:
        a = lpop(t);
        b = lpop(t);
        c = lpop(t);
        d = lpop(t);
        r = WATCOM_CALL(script_functab[i], a, b, c, d);
        break;
    case 5:
        a = lpop(t);
        b = lpop(t);
        c = lpop(t);
        d = lpop(t);
        e = lpop(t);
        r = WATCOM_CALL(script_functab[i], a, b, c, d, e);
        break;
    default:
        r = 0;
        break;
    }
    script_cur = t;
    push(r, 0);
    return pc + 4;
}

/* op 0x2B w: (x y -- x) continues when x == y, else jumps */
uchar *ScriptOpCase(uchar *pc)
{
    script_sp--;
    if (script_stack[script_sp - 1] == script_stack[script_sp])
        return pc + 3;
    return DATA(script_cur) + *(ushort *)(pc + 1);
}

/* op 0x2C w: (s1 s2 -- s1) string refs, compared without case */
uchar *ScriptOpCaseString(uchar *pc)
{
    Ref a, b;
    char *sa, *sb;

    script_sp--;
    b = script_stack[script_sp];
    a = script_stack[script_sp - 1];
    if (a == b)
        return pc + 3;
    sa = RefLock(a);
    sb = RefLock(b);
    ResUnlock((ushort)(a >> 16));
    ResUnlock((ushort)(b >> 16));
    if (strcmpi(sa, sb) == 0)
        return pc + 3;
    return DATA(script_cur) + *(ushort *)(pc + 1);
}

/* op 0x2D w */
uchar *ScriptOpJump(uchar *pc)
{
    return DATA(script_cur) + *(ushort *)(pc + 1);
}

/* op 0x2E ref: (-- id) starts a thread */
uchar *ScriptOpSpawn(uchar *pc)
{
    ScriptThread *t = ScriptStart(*(long *)(pc + 1));

    push(t->id, 0);
    return pc + 5;
}

/* op 0x2F: (id wait arg --) wakes thread id (0: all threads) */
uchar *ScriptOpWake(uchar *pc)
{
    short sp = script_sp;
    long arg = script_stack[sp - 1];
    short wait = (short)script_stack[sp - 2];
    ushort id = (ushort)script_stack[sp - 3];
    ScriptThread *t;

    script_sp = sp - 3;
    if (id) {
        t = ScriptFind(id);
        if (t)
            ScriptWake(t, wait, arg);
    } else
        ScriptWake(NULL, wait, arg);
    return pc + 1;
}

/* op 0x30: (wait arg --) */
uchar *ScriptOpWait(uchar *pc)
{
    short sp = script_sp;
    long arg = script_stack[sp - 1];
    short wait = (short)script_stack[sp - 2];

    script_sp = sp - 2;
    ScriptSetWait(script_cur, wait, arg, 0);
    script_done = 1;
    return pc + 1;
}

/* op 0x31 */
uchar *ScriptOpYield(uchar *pc)
{
    script_done = 1;
    return pc + 1;
}

/* op 0x32: (time --) waits until the wait clock reaches time */
uchar *ScriptOpWaitUntil(uchar *pc)
{
    script_sp--;
    ScriptSetWait(script_cur, SCRIPT_UNTIL, script_stack[script_sp], 0);
    script_done = 1;
    return pc + 1;
}

/* op 0x33: (dt --) */
uchar *ScriptOpWaitFor(uchar *pc)
{
    script_sp--;
    ScriptSetWait(script_cur, SCRIPT_UNTIL, script_stack[script_sp] + *script_wait_clock, 0);
    script_done = 1;
    return pc + 1;
}

/* op 0x34: (dt --) */
uchar *ScriptOpSleep(uchar *pc)
{
    script_sp--;
    ScriptSetWait(script_cur, SCRIPT_SLEEP, 0, (short)script_stack[script_sp]);
    script_done = 1;
    return pc + 1;
}

/* op 0x35 w: choice menu.  The table at the jump target w: short n, then n entries of 10
 * bytes {+2 condition type, +4 value / key, +6 id, +8 string index}; type 0 constant, 1/2
 * global, 3 local, 4 engine variable (byte read), 0x8000 no string.  Entries whose condition
 * is 0 get id 0.  The thread waits in SCRIPT_CHOICE with pc = the table. */
uchar *ScriptOpChoice(uchar *pc)
{
    uchar *tab = DATA(script_cur) + *(ushort *)(pc + 1);
    uchar *e = tab;
    char *buf = script_strbuf;
    uchar cond = 0;     /* original: uninitialised stack byte */
    bool nostr;
    int i;
    ushort type, val;

    for (i = 0; i < *(short *)tab; i++, e += 10) {
        val = *(ushort *)(e + 4);
        nostr = FALSE;
        type = *(ushort *)(e + 2);
        if (type == 0)
            cond = (uchar)val;
        else if (type == 1 || type == 2)
            cond = script_globals[val];
        else if (type == 3)
            cond = *(script_cur->lstack + script_cur->lfp + (short)val);
        else if (type == 4)
            cond = *(uchar *)script_vartab[val];
        else if (type == 0x8000)
            nostr = TRUE;
        if (nostr) {
            script_choice_strs[i] = NULL;
            script_choice_ids[i] = *(short *)(e + 6);
        } else {
            script_choice_strs[i] = buf;
            buf = ScriptFormat(((ulong)(script_cur->res + 1) << 16) | *(ushort *)(e + 8), buf, script_strbuf_end) + 1;
            script_choice_ids[i] = cond ? *(short *)(e + 6) : 0;
        }
    }
    if (script_choice_cb) {
        WATCOM_CALL(script_choice_cb, *(short *)tab, (long)script_choice_strs, (long)script_choice_ids);
        ScriptSetWait(script_cur, SCRIPT_CHOICE, 0, 0);
        script_done = 1;
    }
    return tab;
}

/* op 0x36: (id ref --) formats string ref and shows it (id 0x7FFF: not shown) */
uchar *ScriptOpPrint(uchar *pc)
{
    char buf[0x800];
    short sp = script_sp;
    Ref ref = script_stack[sp - 1];
    ushort id = (ushort)script_stack[sp - 2];

    script_sp = sp - 2;
    buf[0] = 0;
    ScriptFormat(ref, buf, buf + 0x800);
    if (id != 0x7FFF && script_text_cb)
        WATCOM_CALL(script_text_cb, (long)id, (long)buf);
    return pc + 1;
}

/* op 0x37 b: waits for a key; b != 0: (timeout --) */
uchar *ScriptOpWaitKey(uchar *pc)
{
    long timeout = 0;

    if (pc[1]) {
        script_sp--;
        timeout = script_stack[script_sp];
    }
    ScriptSetWait(script_cur, SCRIPT_KEY, 0, (short)timeout);
    script_done = 1;
    return pc + 2;
}

/* op 0x38: new outgoing message */
uchar *ScriptOpMsgNew(uchar *pc)
{
    script_cur->msg_out = (uchar *)MsgAlloc();
    return pc + 1;
}

/* op 0x39 b: (v --) message field (b as op 0x0C); size 4 stores only a word */
uchar *ScriptOpMsgSet(uchar *pc)
{
    uchar *p;
    uchar size;
    long v;

    if (script_cur->msg_out) {
        size = pc[1] >> 5;
        p = SMSG_DATA(script_cur->msg_out) + (pc[1] & 0x1F);
        script_sp--;
        v = script_stack[script_sp];
        if (size == 1)
            *p = (uchar)v;
        else if (size == 2 || size == 4)
            *(short *)p = (short)v;
    }
    return pc + 2;
}

/* op 0x3A kind: (to arg --) sends the message */
uchar *ScriptOpMsgSend(uchar *pc)
{
    ScriptThread *t = script_cur;
    short sp = script_sp;
    uchar *m = t->msg_out;

    if (m) {
        SMSG_TYPE(m) = 2;
        SMSG_FROM(t->msg_out) = t->id;
        sp--;
        SMSG_ARG(t->msg_out) = (short)script_stack[sp];
        sp--;
        SMSG_TO(t->msg_out) = (short)script_stack[sp];
        SMSG_KIND(t->msg_out) = pc[1];
        script_sp = sp;
        MsgSend((Msg *)t->msg_out);
        sp = script_sp;
        script_cur->msg_out = NULL;
    }
    script_sp = sp;
    return pc + 2;
}

/* op 0x3B b: waits for a message; b != 0: (timeout --) */
uchar *ScriptOpMsgWait(uchar *pc)
{
    if (pc[1]) {
        script_sp--;
        ScriptSetWait(script_cur, SCRIPT_MSG, 0, (short)script_stack[script_sp]);
    } else
        ScriptSetWait(script_cur, SCRIPT_MSG, 0, 0);
    script_done = 1;
    return pc + 2;
}

/* op 0x3C kind w: continues when the received message is of this kind, else jumps */
uchar *ScriptOpMsgIs(uchar *pc)
{
    uchar *m = script_cur->msg_in;

    if (m && pc[1] == SMSG_KIND(m))
        return pc + 4;
    return DATA(script_cur) + *(ushort *)(pc + 2);
}

/* op 0x3D: frees the received message */
uchar *ScriptOpMsgFree(uchar *pc)
{
    if (script_cur->msg_in) {
        MsgFree((Msg *)script_cur->msg_in);
        script_cur->msg_in = NULL;
    }
    return pc + 1;
}

/* op 0x7F w: line number */
uchar *ScriptOpLine(uchar *pc)
{
    return pc + 3;
}

/* op 0x3E (end of function marker after the return; never reached): returns NULL without
 * ending the loop */
uchar *ScriptOpNull(uchar *pc)
{
    return NULL;
}

/* op 0x3F and every unknown opcode: ends the thread */
uchar *ScriptOpEnd(uchar *pc)
{
    ScriptSetWait(script_cur, SCRIPT_DEAD, 0, 0);
    script_done = 1;
    return NULL;
}

/* Copies string ref to dst (until end), expanding the format codes (bytes with bit 7 set);
 * returns the address of the terminating 0. */
char *ScriptFormat(Ref ref, char *dst, char *end)
{
    uchar *src = RefLock(ref);

    while (*src) {
        if (*src & 0x80)
            dst = ScriptFormatArg(dst, &src, end);
        else
            *dst++ = *src++;
        if ((ulong)dst >= (ulong)end)
            break;
    }
    *dst = 0;
    ResUnlock((ushort)(ref >> 16));
    return dst;
}

/* One format code: 'C' long constant; 'G'/'S' global, 'L' local, 'E' engine variable (as
 * numbers); 'g'/'s' global, 'l' local holding a string ref; 'M' field of the received message.
 * Numbers are written with "%d" without checking end. */
char *ScriptFormatArg(char *dst, uchar **psrc, char *end)
{
    uchar *p = *psrc;
    uchar *q;
    int code = (signed char)p[0] & 0x7F;
    ulong kind = (ulong)end;   /* the original's ecx for unknown codes */
    long num = 0;              /* the original's edi when not set */
    long idx = 0;
    uchar *var = NULL;
    uchar *m;
    char tmp[0x28];
    int len;

    p++;
    switch (code) {
    case 'C':
        num = *(long *)p;
        p += 4;
        kind = 0;
        break;
    case 'G': case 'S':
        var = script_globals + *(ushort *)p;
        p += 2;
        kind = 1;
        break;
    case 'L':
        var = script_cur->lstack + script_cur->lfp + *(short *)p;
        p += 2;
        kind = 1;
        break;
    case 'E':
        var = (uchar *)script_vartab[*(ushort *)p];
        p += 2;
        kind = 1;
        break;
    case 'g': case 's':
        var = script_globals + *(ushort *)p;
        p += 2;
        kind = 2;
        break;
    case 'l':
        var = script_cur->lstack + script_cur->lfp + *(short *)p;
        p += 2;
        kind = 2;
        break;
    case 'M':
        m = script_cur->msg_in;
        if (m) {
            int off = *p & 0x1F;
            if (off == 0x1E)
                num = SMSG_TYPE(m);
            else if (off == 0x1F)
                num = SMSG_FROM(m);
            else {
                q = SMSG_DATA(m) + (signed char)off;
                switch ((signed char)*p >> 5) {
                case 1: num = *(signed char *)q; break;
                case 2: num = *(short *)q; break;
                case 4: num = *(long *)q; break;
                }
            }
        }
        p++;
        kind = 0;
        break;
    }
    /* index suffix: dead code (a sign-extended byte is compared with 0xC9) */
    if ((signed char)*p == 0xC9)
        idx = 0;
    if (kind == 0 || kind == 1) {
        if (kind == 1)
            num = *(long *)(var + idx * 4);
        lg_sprintf(tmp, "%d", num);
        len = strlen(tmp);
        memcpy(dst, tmp, len);
        dst += strlen(tmp);
    } else if (kind == 2)
        dst = ScriptFormat(*(long *)(var + idx * 4), dst, end);
    *psrc = p;
    return dst;
}

/* (unreferenced helpers) */
long ScriptReadLong(uchar **pp)
{
    long v = *(long *)*pp;

    *pp += 4;
    return v;
}

short ScriptReadShort(uchar **pp)
{
    short v = *(short *)*pp;

    *pp += 2;
    return v;
}

long ScriptPop(void)
{
    short sp = script_sp;
    long v;

    if (sp > 0) {
        sp--;
        v = script_stack[sp];
    } else
        v = 0;
    script_sp = sp;
    return v;
}

void ScriptPush(long v, uchar tag)
{
    short sp = script_sp;

    if (sp < 0x40) {
        script_stack[sp] = v;
        script_tags[sp] = tag;
        sp++;
    }
    script_sp = sp;
}

void ScriptPopDiscard(void)
{
    if (script_sp > 0)
        script_sp--;
}

void ScriptLocalPush(long v)
{
    ScriptThread *t = script_cur;

    lgrow(t);
    t = script_cur;
    *(long *)(t->lstack + t->lsp) = v;
    t->lsp += 4;
}

long ScriptLocalPop(void)
{
    ScriptThread *t = script_cur;

    t->lsp -= 4;
    return *(long *)(t->lstack + t->lsp);
}

void ScriptLocalAlloc(ushort n)
{
    ScriptThread *t = script_cur;

    t->lsp += n;
    if (t->lsp >= t->lcap) {
        t->lcap = t->lsp + 0x40;
        script_cur->lstack = (*f_realloc)(t->lstack, t->lcap);
    }
}
