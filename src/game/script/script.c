/* script.c -- TN script ("babble") threads: start, per-frame scheduling, waits, key and mouse
 * input, text and choice boxes.  Decompiled from __FF.EXE (object file 0x2E7E10-0x2E8414;
 * no assert path, all names ours).
 *
 *   0x2E7E10 ScriptInit          0x2E7E38 ScriptShutdown      0x2E7E48 ScriptLoadGlobals
 *   0x2E7E8C ScriptKillAll       0x2E7EDC ScriptSetClocks     0x2E7EE8 ScriptFrame
 *   0x2E7F08 ScriptThreadFrame   0x2E7FF4 ScriptStart         0x2E809C ScriptWake
 *   0x2E8148 ScriptSetWait       0x2E8154 ScriptKill          0x2E8168 ScriptFind
 *   0x2E8198 ScriptIdentity      0x2E819C ScriptSendKey       0x2E81D0 ScriptThreadKey
 *   0x2E8278 ScriptFreeThread    0x2E82E0 ScriptPushResult    0x2E8300 ScriptSetTextBox
 *   0x2E8308 ScriptSetChoiceBox  0x2E8310 ScriptTextCallback  0x2E831C ScriptChoiceCallback
 *   0x2E836C ScriptAbortAnims    0x2E83A0 ScriptKeyHandler    0x2E83FC ScriptMouseHandler
 *
 * main() calls ScriptInit, installs ScriptFrame as frame hook 2, ScriptSetClocks(&game_time,
 * &game_time), sets script_text_cb / script_choice_cb to ScriptTextCallback /
 * ScriptChoiceCallback, registers the opcode tables, installs ScriptKeyHandler and
 * ScriptMouseHandler as tnui handlers, then ScriptLoadGlobals(1000, ...) and ScriptStart(0x3E90004).
 * ScriptFind, ScriptKill and ScriptIdentity are also engine functions of the scripts (functab
 * 90, 89, 86).
 */
#include "t1_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar script_header[8];
ulong *script_wait_clock;
ulong *script_frame_clock;
ScriptFunc script_text_cb;
uchar *script_globals;
ScriptFunc script_choice_cb;
LlistHead script_threads;
short script_next_id;
ushort script_globals_res;
void *script_textbox;
void *script_choicebox;
uchar script_choice_keys[8];
#endif

/* a value pushed on a suspended thread's stack (resumes with it on top) */
static inline void push_saved(ScriptThread *t, long v)
{
    t->saved_val[t->saved_sp] = v;
    t->saved_tag[t->saved_sp] = 0;
}

void ScriptInit(void)
{
    LlistInit(&script_threads, sizeof(ScriptThread), 4);
    ScriptOpsInit();
    AtExit(WATCOM_FN(ScriptShutdown));
}

void ScriptShutdown(void)
{
    ScriptKillAll();
    LlistDestroy(&script_threads);
}

/* Reads the script header resource and allocates the global variables. */
void ScriptLoadGlobals(ushort id, long **vartab, ScriptFunc *functab)
{
    script_globals_res = id;
    ResExtract(id, script_header);
    if (*(ushort *)(script_header + 6))
        script_globals = Calloc(*(ushort *)(script_header + 6));
    script_vartab = vartab;
    script_functab = functab;
}

/* Frees every thread and the globals (nothing if no header is loaded). */
void ScriptKillAll(void)
{
    ScriptThread *t, *next;

    if (script_globals_res == 0)
        return;
    for (t = (ScriptThread *)script_threads.head.pnext; t != (ScriptThread *)&script_threads.tail; t = next) {
        next = t->next;
        ScriptFreeThread(t);
    }
    if (script_globals) {
        (*f_free)(script_globals);
        script_globals = NULL;
    }
    script_globals_res = 0;
}

void ScriptSetClocks(ulong *frame_clock, ulong *wait_clock)
{
    script_frame_clock = frame_clock;
    script_wait_clock = wait_clock;
}

/* Frame hook: one step of every thread. */
void ScriptFrame(void)
{
    ScriptThread *t, *next;

    for (t = (ScriptThread *)script_threads.head.pnext; t != (ScriptThread *)&script_threads.tail; t = next) {
        next = t->next;
        ScriptThreadFrame(t);
    }
}

/* Counts down the thread's timer, checks its wait condition, runs it when it is runnable and
 * frees it when it has ended. */
void ScriptThreadFrame(ScriptThread *t)
{
    ScriptMsgKey key;
    ulong now = *script_frame_clock;
    ulong dt = now - t->last_clock;

    t->last_clock = now;
    if (t->wait != SCRIPT_RUN) {
        if (t->wait_timer != 0) {
            t->wait_timer = (short)(t->wait_timer - (short)dt);
            if (t->wait_timer <= 0) {
                if (t->wait == SCRIPT_KEY) {
                    /* key wait timed out: resumes with 0x88 */
                    t->saved_val[t->saved_sp] = 0x88;
                    t->saved_tag[t->saved_sp] = 0;
                    t->saved_sp++;
                }
                t->wait = SCRIPT_RUN;
                t->wait_timer = 0;
            }
        }
        if (t->wait == SCRIPT_UNTIL && *script_wait_clock >= (ulong)t->wait_arg)
            t->wait = SCRIPT_RUN;
        if (t->wait == SCRIPT_MSG) {
            key.type = 2;
            key.id = t->id;
            t->msg_in = (uchar *)MsgReceive((MsgAddr *)&key);
            if (t->msg_in)
                t->wait = SCRIPT_RUN;
        }
    }
    if (t->wait == SCRIPT_RUN)
        ScriptRun(t);
    if (t->wait == SCRIPT_DEAD) {
        short id = t->id;

        ScriptFreeThread(t);
        ScriptWake(NULL, SCRIPT_WAIT_END, id);
    }
}

/* Starts a thread at function REFINDEX(ref) of script REFID(ref). */
ScriptThread *ScriptStart(Ref ref)
{
    ScriptThread *t;

    script_next_id++;
    if (script_next_id <= 0)
        script_next_id = 1;
    t = LlistAddTail(&script_threads);
    t->id = script_next_id;
    t->res = (ushort)(ref >> 16);
    t->code = ResLock(t->res);
    t->pc = *(ushort *)(t->code + (ref & 0xFFFF) * 4 + 2);
    t->lcap = 0x40;
    t->lstack = Calloc(t->lcap);
    t->lsp = 0;
    t->lfp = 0;
    t->msg_out = NULL;
    t->msg_in = NULL;
    t->wait = SCRIPT_RUN;
    t->saved_sp = 0;
    t->last_clock = *script_frame_clock;
    /* wait_arg and wait_timer keep what the recycled node held */
    return t;
}

static inline bool wake_matches(ScriptThread *t, short wait, long arg)
{
    return wait == t->wait && (arg == 0 || t->wait_arg == 0 || arg == t->wait_arg);
}

/* Wakes thread t (or, with t NULL, every thread) waiting in state `wait` for `arg` (0 on either
 * side matches anything); the thread resumes with arg on its stack. */
void ScriptWake(ScriptThread *t, short wait, long arg)
{
    if (t) {
        if (wake_matches(t, wait, arg)) {
            push_saved(t, arg);
            t->wait = SCRIPT_RUN;
            t->saved_sp++;
        }
        return;
    }
    for (t = (ScriptThread *)script_threads.head.pnext; t != (ScriptThread *)&script_threads.tail; t = t->next) {
        if (wake_matches(t, wait, arg)) {
            push_saved(t, arg);
            t->wait = SCRIPT_RUN;
            t->saved_sp++;
        }
    }
}

void ScriptSetWait(ScriptThread *t, short wait, long arg, short timer)
{
    t->wait = wait;
    t->wait_arg = arg;
    t->wait_timer = timer;
}

/* Engine function 89 (its result, eax = t, is pushed by the script). */
ScriptThread *ScriptKill(ScriptThread *t)
{
    t->wait = SCRIPT_DEAD;
    t->wait_arg = 0;
    t->wait_timer = 0;
    return t;
}

/* Quirk: the id is compared sign-extended against the zero-extended argument, so ids >= 0x8000
 * never match (ids stay below 0x8000 anyway). */
ScriptThread *ScriptFind(ushort id)
{
    ScriptThread *t;

    for (t = (ScriptThread *)script_threads.head.pnext; t != (ScriptThread *)&script_threads.tail; t = t->next)
        if ((long)t->id == (long)id)
            return t;
    return NULL;
}

/* Engine function 86: a bare `ret`, so it returns its first argument. */
long ScriptIdentity(long x)
{
    return x;
}

/* Offers a key to the threads in order; returns the first ScriptThreadKey result != 0. */
int ScriptSendKey(ushort key)
{
    ScriptThread *t;
    int r;

    for (t = (ScriptThread *)script_threads.head.pnext; t != (ScriptThread *)&script_threads.tail; t = t->next) {
        r = ScriptThreadKey(t, key);
        if (r)
            return r;
    }
    return 0;
}

/* Key for one thread: 1 = it was waiting for a key (resumes with it), 2 = it was in a choice
 * menu and the key selects an entry (jumps to the entry's target), 0 = not taken. */
int ScriptThreadKey(ScriptThread *t, ushort key)
{
    uchar *tab, *e, *end;

    if (t->wait == SCRIPT_KEY) {
        t->wait = SCRIPT_RUN;
        t->saved_val[t->saved_sp] = key;
        t->saved_tag[t->saved_sp] = 0;
        t->saved_sp++;
        return 1;
    }
    if (t->wait == SCRIPT_CHOICE) {
        /* choice table at pc: short n, then n entries of 10 bytes */
        tab = t->code + t->pc;
        end = tab + 2 + *(short *)tab * 10;
        for (e = tab + 2; (ulong)e < (ulong)end; e += 10) {
            if (key == *(ushort *)(e + 4)) {
                t->wait = SCRIPT_RUN;
                t->pc = (ushort)(*(ushort *)(e + 8) + (*(ushort *)t->code + 1) * 4 + 2);
                return 2;
            }
        }
    }
    return 0;
}

void ScriptFreeThread(ScriptThread *t)
{
    ScriptMsgKey key;

    ResUnlock(t->res);
    if (t->lstack)
        (*f_free)(t->lstack);
    if (t->msg_out)
        MsgFree((Msg *)t->msg_out);
    if (t->msg_in)
        MsgFree((Msg *)t->msg_in);
    key.type = 2;
    key.id = t->id;
    MsgFlush((MsgAddr *)&key);
    LlistFree(&script_threads, t);
}

/* (unreferenced) */
void ScriptPushResult(ScriptThread *t, long v)
{
    t->saved_val[t->saved_sp] = v;
    t->saved_tag[t->saved_sp] = 0;
    t->saved_sp++;
}

void ScriptSetTextBox(void *box)
{
    script_textbox = box;
}

void ScriptSetChoiceBox(void *box)
{
    script_choicebox = box;
}

/* script_text_cb: shows the text in the text box (the id is not used). */
void ScriptTextCallback(ushort id, char *text)
{
#ifndef TN_WATCOM_ABI
    if (!script_textbox)                /* no text box yet: the original wrote through 0 (G11) */
        return;
#endif
    tbox_set_text((TBox *)script_textbox, text);
}

/* script_choice_cb: shows a choice menu (n strings, keys = low bytes of ids) in the choice box. */
void ScriptChoiceCallback(short n, char **strs, short *ids)
{
    struct {
        short n;
        ulong mask;
        uchar *keys;
        char **strs;
    } __attribute__((packed)) c;
    ulong mask = 0;
    int i = 0;

    while (i < n) {
        i++;
        mask = mask * 2 | 1;
        script_choice_keys[i - 1] = *(uchar *)ids;
        ids++;
    }
    c.mask = mask;
    c.n = n;
    c.keys = script_choice_keys;
    c.strs = strs;
    tmenu_set_items((TMenu *)script_choicebox, (TMenuItems *)&c);
}

/* Stops the animations threads wait for (they resume); TRUE if there was one. */
bool ScriptAbortAnims(void)
{
    ScriptThread *t;
    bool r = FALSE;

    for (t = (ScriptThread *)script_threads.head.pnext; t != (ScriptThread *)&script_threads.tail; t = t->next) {
        if (t->wait == SCRIPT_ANIM) {
            anim_2E6A34((void *)t->wait_arg);
            r = TRUE;
            t->wait = SCRIPT_RUN;
        }
    }
    return r;
}

/* tnui key handler (event: +4 key code).  Escape skips animations; other keys go to the
 * threads, and a key nobody takes also skips animations.  A choice closes the choice box. */
bool ScriptKeyHandler(uchar *ev)
{
    ushort key = *(ushort *)(ev + 4);
    int r;

    if (key == 0x1B)
        return ScriptAbortAnims() ? TRUE : FALSE;
    r = ScriptSendKey(key);
    if (r == 0)
        return ScriptAbortAnims() ? TRUE : FALSE;
    if (r == 1)
        return TRUE;
    if (r == 2) {
        tmenu_set_items((TMenu *)script_choicebox, NULL);
        return TRUE;
    }
    return FALSE;
}

/* tnui mouse handler: a click (event type 1) is key 0x81. */
bool ScriptMouseHandler(uchar *ev)
{
    if (*(long *)ev == 1)
        return ScriptSendKey(0x81) != 0;
    return FALSE;
}
