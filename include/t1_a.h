/* t1_a.h -- declarations for worker t1a (batch T1): the TN script ("babble") interpreter,
 * 0x2E7E10-0x2EAB30, src/game/script/ (*.c).
 *
 * Scripts are compiled byte code in RESBABL2.RES (demos: DEMOBAB1/2.RES): resource 1000 (type 13)
 * is a header whose last word is the size of the global variable block; every odd id >= 1001
 * (type 12) is a compound resource holding the functions of one script file (item 0 is the
 * file name, items 1.. are functions), and the next even id is its string table (type 1).
 * A thread runs one function of one script; byte code is executed by handlers registered per
 * opcode (script_ops[256]).  Names are ours (no assert path, no Shock counterpart).
 * Globals are bound to their original addresses through symbols-t1-a.csv.
 */
#ifndef TN_T1_A_H
#define TN_T1_A_H
#include "lg.h"
#include "res.h"
#include "support.h"
#include "2d.h"
#include "t1_c.h"
#include "t2_b.h"      /* delta animation player anim_* (owner t2b) */

#pragma pack(push, 1)

/* ------------------------------------------------------------------ threads */
/* wait states (ScriptThread.wait) */
#define SCRIPT_RUN      0   /* runnable */
#define SCRIPT_DEAD     1   /* ends: freed by ScriptThreadFrame, waiters of state 2 woken */
#define SCRIPT_WAIT_END 2   /* until a thread ends (ScriptWake(NULL, 2, id)) */
#define SCRIPT_SLEEP    3   /* wait_timer clock units */
#define SCRIPT_KEY      4   /* key (pushed); 0x88 on timeout */
#define SCRIPT_UNTIL    5   /* *script_wait_clock >= wait_arg */
#define SCRIPT_CHOICE   6   /* choice menu key (ScriptThreadKey) */
#define SCRIPT_ANIM     7   /* animation wait_arg ends (ScriptAnimDone) */
#define SCRIPT_MSG      8   /* a message for this thread */

/* Thread record, 0x82 bytes, node of the llist script_threads. */
typedef struct ScriptThread {
    struct ScriptThread *next;  /* +0x00 llist link */
    struct ScriptThread *prev;  /* +0x04 */
    short id;                   /* +0x08 1..0x7FFF */
    ushort res;                 /* +0x0A script resource (locked) */
    uchar *code;                /* +0x0C ResLock(res) */
    ushort pc;                  /* +0x10 offset of the next instruction from code */
    ushort lsp;                 /* +0x12 local stack top (bytes) */
    ushort lfp;                 /* +0x14 frame pointer (bytes) */
    ushort lcap;                /* +0x16 local stack capacity (grows by 0x40) */
    uchar *lstack;              /* +0x18 local stack (Calloc, f_realloc) */
    uchar *msg_out;             /* +0x1C message being built (msgpool) */
    uchar *msg_in;              /* +0x20 message received */
    short wait;                 /* +0x24 SCRIPT_* */
    long wait_arg;              /* +0x26 */
    short wait_timer;           /* +0x2A clock units, counts down while waiting */
    ulong last_clock;           /* +0x2C *script_frame_clock at the last frame */
    long saved_val[16];         /* +0x30 evaluation stack while the thread is suspended */
    uchar saved_tag[16];        /* +0x70 */
    short saved_sp;             /* +0x80 */
} ScriptThread;
LG_STATIC_ASSERT(sizeof(ScriptThread) == 0x82, ScriptThread_size);

/* message (msgpool) fields used by the scripts */
#define SMSG_KIND(m)   (*(uchar *)((m) + 0x02))
#define SMSG_TYPE(m)   (*(ushort *)((m) + 0x08))
#define SMSG_FROM(m)   (*(ushort *)((m) + 0x0A))
#define SMSG_TO(m)     (*(short *)((m) + 0x0C))
#define SMSG_ARG(m)    (*(short *)((m) + 0x0E))
#define SMSG_DATA(m)   ((m) + 0x10)
/* msgpool lookup key: {2, thread id} selects the messages of a thread */
typedef struct { short type; short id; } ScriptMsgKey;

typedef uchar *(WATCOMCALL *ScriptOp)(uchar *pc);
/* opcode registration table entry (packed, a zero opcode ends the table) */
typedef struct { uchar op; void *fn; } ScriptOpReg;

extern uchar script_header[8];              /* 0x448D90 resource 1000; +6 globals size */
extern ulong *script_wait_clock;            /* 0x448D98 clock of SCRIPT_UNTIL (game_time) */
extern ulong *script_frame_clock;           /* 0x448D9C clock of the timers (game_time) */
typedef long (*ScriptFunc)();                /* engine function / callback (Watcom code) */
extern ScriptFunc script_text_cb;            /* 0x448DA0 (ushort id, char *text) */
extern uchar *script_globals;               /* 0x448DA4 global variables (Calloc) */
extern ScriptFunc script_choice_cb;          /* 0x448DA8 (short n, char **strs, short *ids) */
extern LlistHead script_threads;            /* 0x448DAC */
extern short script_next_id;                /* 0x448DC8 */
extern ushort script_globals_res;           /* 0x448DCA header resource, 0 = not loaded */
extern void *script_textbox;                /* 0x448DD0 */
extern void *script_choicebox;              /* 0x448DD4 */
extern uchar script_choice_keys[8];         /* 0x448DD8 */
extern uchar script_pal[0x300];             /* 0x448E00 palette held while faded */
extern void *script_ops[256];               /* 0x449100 */
extern uchar script_tags[64];               /* 0x449500 type tag per stack entry */
extern long script_stack[64];               /* 0x449540 evaluation stack */
extern long **script_vartab;                /* 0x449640 engine variables (pointers) */
extern ScriptFunc *script_functab;           /* 0x449644 engine functions */
extern ScriptThread *script_cur;            /* 0x449648 */
extern short script_sp;                     /* 0x44964C */
extern uchar script_done;                   /* 0x44964E ends the dispatch loop */
extern short script_choice_ids[8];          /* 0x449650 */
extern char *script_choice_strs[8];         /* 0x449660 */
extern char script_strbuf[500];             /* 0x449680 choice strings */
extern uchar *script_vp;                    /* 0x449E80 current viewport (canvas at +0x14) */
extern ushort script_font;                  /* 0x449E84 */
extern uchar script_pal_held;               /* 0x35F698 1: palette changes go to script_pal */
extern char *script_strbuf_end;             /* 0x35F820 = script_strbuf + 500 */
/* viewport description of op 0x50 (passed to viewport_2E55D8) */
typedef struct {
    uchar *style;               /* +0x00 = script_vp_style */
    uchar kind;                 /* +0x04 2 */
    short rect[4];              /* +0x05 x0 y0 x1 y1 */
    short z;                    /* +0x0D */
    short flag;                 /* +0x0F op operand != 0 */
    uchar pad[3];               /* +0x11 */
} ScriptVpDesc;
LG_STATIC_ASSERT(sizeof(ScriptVpDesc) == 0x14, ScriptVpDesc_size);
extern ScriptVpDesc script_vp_template;     /* 0x35F898 */
extern uchar script_vp_style[0x10];         /* 0x35F888 (contents not interpreted here) */

/* opcode tables (C copies of 0x35F6E0, 0x35F6B8, 0x35F69C, 0x35F824, 0x35F8AC) */
extern const ScriptOpReg script_core_ops[];
extern const ScriptOpReg script_anim_ops[];
extern const ScriptOpReg script_pal_ops[];
extern const ScriptOpReg script_gfx_ops[];
extern const ScriptOpReg script_snd_ops[];

/* script.c */
void ScriptInit(void);                                      /* 0x2E7E10 */
void ScriptShutdown(void);                                  /* 0x2E7E38 */
void ScriptLoadGlobals(ushort id, long **vartab, ScriptFunc *functab); /* 0x2E7E48 */
void ScriptKillAll(void);                                   /* 0x2E7E8C */
void ScriptSetClocks(ulong *frame_clock, ulong *wait_clock);/* 0x2E7EDC */
void ScriptFrame(void);                                     /* 0x2E7EE8 hook slot 2 */
void ScriptThreadFrame(ScriptThread *t);                    /* 0x2E7F08 */
ScriptThread *ScriptStart(Ref ref);                         /* 0x2E7FF4 */
void ScriptWake(ScriptThread *t, short wait, long arg);     /* 0x2E809C */
void ScriptSetWait(ScriptThread *t, short wait, long arg, short timer); /* 0x2E8148 */
ScriptThread *ScriptKill(ScriptThread *t);                       /* 0x2E8154 */
ScriptThread *ScriptFind(ushort id);                        /* 0x2E8168 */
long ScriptIdentity(long x);                                /* 0x2E8198 */
int ScriptSendKey(ushort key);                              /* 0x2E819C */
int ScriptThreadKey(ScriptThread *t, ushort key);           /* 0x2E81D0 */
void ScriptFreeThread(ScriptThread *t);                     /* 0x2E8278 */
void ScriptPushResult(ScriptThread *t, long v);             /* 0x2E82E0 */
void ScriptSetTextBox(void *box);                           /* 0x2E8300 */
void ScriptSetChoiceBox(void *box);                         /* 0x2E8308 */
void ScriptTextCallback(ushort id, char *text);             /* 0x2E8310 */
void ScriptChoiceCallback(short n, char **strs, short *ids);/* 0x2E831C */
bool ScriptAbortAnims(void);                                /* 0x2E836C */
bool ScriptKeyHandler(uchar *ev);                           /* 0x2E83A0 */
bool ScriptMouseHandler(uchar *ev);                         /* 0x2E83FC */

/* scrpal.c */
uchar *ScriptOpFadeOut(uchar *pc);                          /* 0x2E8420 op 0x48 */
uchar *ScriptOpFadeIn(uchar *pc);                           /* 0x2E84CC op 0x49 */
uchar *ScriptOpPalRes(uchar *pc);                           /* 0x2E856C op 0x4A */
uchar *ScriptOpPalRef(uchar *pc);                           /* 0x2E85F4 op 0x4B */
void ScriptSavePal(void);                                   /* 0x2E8690 */
long ScriptPalFromRef(Ref ref);                             /* 0x2E86A8 */
long ScriptPalBlack(void);                                  /* 0x2E8740 */
long ScriptPalRestore(void);                                /* 0x2E8788 */
void ScriptSetPal(int start, int n, uchar *data);           /* 0x2E87A8 */

/* scranim.c */
uchar *ScriptOpAnimPlay(uchar *pc);                         /* 0x2E8800 op 0x40 */
uchar *ScriptOpAnimSet(uchar *pc);                          /* 0x2E88FC op 0x41 */
uchar *ScriptOpAnimWait(uchar *pc);                         /* 0x2E8968 op 0x42 */
uchar *ScriptOpAnimStop(uchar *pc);                         /* 0x2E89A8 op 0x43 */
uchar *ScriptOpAnimColor(uchar *pc);                        /* 0x2E89CC op 0x44 */
uchar *ScriptOpAnimMove(uchar *pc);                         /* 0x2E8A18 op 0x45 */
uchar *ScriptOpAnimRef(uchar *pc);                          /* 0x2E8A78 op 0x46 */
void ScriptAnimDone(uchar *anim);                           /* 0x2E8AA0 */

/* scrinterp.c */
void ScriptOpsInit(void);                                   /* 0x2E8AC0 */
void ScriptRegisterOps(const ScriptOpReg *tab);             /* 0x2E8B04 */
void ScriptRun(ScriptThread *t);                            /* 0x2E8B2C */
uchar *ScriptOpPushByte(uchar *pc);                         /* 0x2E8C60 op 0x01 */
uchar *ScriptOpPushShort(uchar *pc);                        /* 0x2E8C98 op 0x02 */
uchar *ScriptOpPushLong(uchar *pc);                         /* 0x2E8CD0 op 0x03 */
uchar *ScriptOpPushVar(uchar *pc);                          /* 0x2E8D2C ops 0x04-0x0C */
uchar *ScriptOpPushString(uchar *pc);                       /* 0x2E9000 op 0x0D */
uchar *ScriptOpDrop(uchar *pc);                             /* 0x2E9038 op 0x0E */
uchar *ScriptOpBinary(uchar *pc);                           /* 0x2E908C ops 0x0F-0x1E */
uchar *ScriptOpUnary(uchar *pc);                            /* 0x2E91B8 ops 0x1F-0x20 */
uchar *ScriptOpIndex(uchar *pc);                            /* 0x2E9200 op 0x21 */
uchar *ScriptOpLoadIndexed(uchar *pc);                      /* 0x2E9274 op 0x22 */
uchar *ScriptOpStore(uchar *pc);                            /* 0x2E9318 op 0x23 */
uchar *ScriptOpJumpFalse(uchar *pc);                        /* 0x2E9394 op 0x24 */
uchar *ScriptOpCall(uchar *pc);                             /* 0x2E93E0 op 0x25 */
uchar *ScriptOpReturn(uchar *pc);                           /* 0x2E9494 op 0x26 */
uchar *ScriptOpEnter(uchar *pc);                            /* 0x2E9548 op 0x27 */
uchar *ScriptOpPushArg(uchar *pc);                          /* 0x2E95E8 op 0x29 */
uchar *ScriptOpNative(uchar *pc);                           /* 0x2E9670 op 0x2A */
uchar *ScriptOpCase(uchar *pc);                             /* 0x2E98B4 op 0x2B */
uchar *ScriptOpCaseString(uchar *pc);                       /* 0x2E9904 op 0x2C */
uchar *ScriptOpJump(uchar *pc);                             /* 0x2E99A0 op 0x2D */
uchar *ScriptOpSpawn(uchar *pc);                            /* 0x2E99C8 op 0x2E */
uchar *ScriptOpWake(uchar *pc);                             /* 0x2E9A0C op 0x2F */
uchar *ScriptOpWait(uchar *pc);                             /* 0x2E9A78 op 0x30 */
uchar *ScriptOpYield(uchar *pc);                            /* 0x2E9ACC op 0x31 */
uchar *ScriptOpWaitUntil(uchar *pc);                        /* 0x2E9AD8 op 0x32 */
uchar *ScriptOpWaitFor(uchar *pc);                          /* 0x2E9B18 op 0x33 */
uchar *ScriptOpSleep(uchar *pc);                            /* 0x2E9B60 op 0x34 */
uchar *ScriptOpChoice(uchar *pc);                           /* 0x2E9BA0 op 0x35 */
uchar *ScriptOpPrint(uchar *pc);                            /* 0x2E9D30 op 0x36 */
uchar *ScriptOpWaitKey(uchar *pc);                          /* 0x2E9DB4 op 0x37 */
uchar *ScriptOpMsgNew(uchar *pc);                           /* 0x2E9E00 op 0x38 */
uchar *ScriptOpMsgSet(uchar *pc);                           /* 0x2E9E18 op 0x39 */
uchar *ScriptOpMsgSend(uchar *pc);                          /* 0x2E9E7C op 0x3A */
uchar *ScriptOpMsgWait(uchar *pc);                          /* 0x2E9F0C op 0x3B */
uchar *ScriptOpMsgIs(uchar *pc);                            /* 0x2E9F64 op 0x3C */
uchar *ScriptOpMsgFree(uchar *pc);                          /* 0x2E9FA8 op 0x3D */
uchar *ScriptOpLine(uchar *pc);                             /* 0x2E9FD4 op 0x7F */
uchar *ScriptOpNull(uchar *pc);                             /* 0x2E9FD8 op 0x3E */
uchar *ScriptOpEnd(uchar *pc);                              /* 0x2E9FDC op 0x3F, default */
char *ScriptFormat(Ref ref, char *dst, char *end);          /* 0x2EA000 */
char *ScriptFormatArg(char *dst, uchar **psrc, char *end);  /* 0x2EA060 */
long ScriptReadLong(uchar **pp);                            /* 0x2EA368 */
short ScriptReadShort(uchar **pp);                          /* 0x2EA37C */
long ScriptPop(void);                                       /* 0x2EA390 */
void ScriptPush(long v, uchar tag);                         /* 0x2EA3B8 */
void ScriptPopDiscard(void);                                /* 0x2EA3E8 */
void ScriptLocalPush(long v);                               /* 0x2EA404 */
long ScriptLocalPop(void);                                  /* 0x2EA458 */
void ScriptLocalAlloc(ushort n);                            /* 0x2EA474 */

/* scrgfx.c */
uchar *ScriptOpVpOpen(uchar *pc);                           /* 0x2EA4C0 op 0x50 */
uchar *ScriptOpVpClose(uchar *pc);                          /* 0x2EA540 op 0x51 */
uchar *ScriptOpVpMove(uchar *pc);                           /* 0x2EA564 op 0x52 */
uchar *ScriptOpVpMoveRel(uchar *pc);                        /* 0x2EA5C0 op 0x53 */
uchar *ScriptOpVpHide(uchar *pc);                           /* 0x2EA654 op 0x54 */
uchar *ScriptOpVpShow(uchar *pc);                           /* 0x2EA678 op 0x55 */
uchar *ScriptOpVpBegin(uchar *pc);                          /* 0x2EA69C op 0x56 */
uchar *ScriptOpVpEnd(uchar *pc);                            /* 0x2EA6C8 op 0x57 */
uchar *ScriptOpFont(uchar *pc);                             /* 0x2EA708 op 0x58 */
uchar *ScriptOpColor(uchar *pc);                            /* 0x2EA72C op 0x59 */
uchar *ScriptOpBackColor(uchar *pc);                        /* 0x2EA754 op 0x5A */
uchar *ScriptOpClip(uchar *pc);                             /* 0x2EA77C op 0x5B */
uchar *ScriptOpClear(uchar *pc);                            /* 0x2EA7C4 op 0x5C */
uchar *ScriptOpLine2(uchar *pc);                            /* 0x2EA7F0 op 0x5D */
uchar *ScriptOpRect(uchar *pc);                             /* 0x2EA82C op 0x5E */
uchar *ScriptOpBox(uchar *pc);                              /* 0x2EA870 op 0x5F */
uchar *ScriptOpBitmap(uchar *pc);                           /* 0x2EA8B4 op 0x60 */
uchar *ScriptOpText(uchar *pc);                             /* 0x2EA940 op 0x61 */
uchar *ScriptOpBitmap2(uchar *pc);                          /* 0x2EA9D4 op 0x62 */
short *ScriptPopRect(short *r);                             /* 0x2EAA90 */

/* scrsnd.c */
uchar *ScriptOpSound(uchar *pc);                            /* 0x2EAAF0 op 0x68 */
uchar *ScriptOpMusic(uchar *pc);                            /* 0x2EAB08 op 0x69 */

/* ------------------------------------------------------------------ original code called */
/* T1c (include/t1_c.h): MsgAlloc, MsgFree, MsgSend, MsgReceive, MsgFlush, tmenu_set_items,
 * tbox_set_text */
/* T2b delta animation player anim_*: t2_b.h */
/* T2a / T2c */
void gfx_2E421C(void);                                      /* 0x2E421C wait for retrace */
/* viewport_2E582C / 2E5954 / 2E5C94, tnui2_2FD344 (start an animation in a region): t2_c.h */
/* 2D library internals outside the 2D map (lib-internal rows) */
void gr_int_309264(void *bm, short *rect);                  /* 0x309264 */
void gr_int_3091F0(void *bm, long x, long y);               /* 0x3091F0 */
/* G1c music.c: music_2467B8 is in g1_c.h */

#pragma pack(pop)
#endif
