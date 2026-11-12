/* Looking Glass support libraries in Terra Nova v1.09: LG (memall, memstack, tmpalloc, dbg,
 * lgsprntf, exit list), DSTRUCT (datapath, hash, rect, llist, pool), INPUT (kb, kbcook, mouse),
 * PALETTE, and the timer layer (TN glue over the Miles AIL timer services).
 * TN's own key/mouse/cursor layer ("tnui") is declared here too.
 *
 * Map: docs/decompilation/support-map.md, entry list: docs/decompilation/support-functions.csv.
 * Structs are byte-packed like the original (Watcom -zp1); enums used in structs are one byte.
 * Addresses are offsets in __FF.EXE (code object 0x244000, data object 0x355000).
 * Names marked "(name ours)" have no Shock counterpart.
 */
#ifndef TN_SUPPORT_H
#define TN_SUPPORT_H
#include "lg.h"

#pragma pack(push, 1)

/* ------------------------------------------------------------------ LG: memall */

/* One allocator-stack slot (MemPushAllocator), 12 bytes. */
typedef struct {
    void *(WATCOMCALL *malloc_f)(size_t size);
    void *(WATCOMCALL *realloc_f)(void *p, size_t size);
    void (WATCOMCALL *free_f)(void *p);
} MemAllocSet;
LG_STATIC_ASSERT(sizeof(MemAllocSet) == 12, MemAllocSet_size);

extern MemAllocSet mem_alloc_stack[4];      /* 0x35F470 */
extern int mem_alloc_index;                 /* 0x35F4A0 current slot */
extern MemAllocSet mem_check_saved;         /* 0x35F4B0 allocators saved by MemCheckOn */
extern bool mem_check_hard;                 /* 0x446B10 MemCheckOn(hard) flag */

/* DOS conventional memory block (MallocConvMemBlock, DPMI 0x100), 10 bytes. */
typedef struct {
    ushort realSeg;      /* +0 real-mode segment */
    ushort selector;     /* +2 protected-mode selector */
    void *ptr;           /* +4 linear address (realSeg << 4) */
    ushort pad;          /* +8 ds selector: ptr is stored as a far pointer (selector:offset) */
} ConvMemBlock;
LG_STATIC_ASSERT(sizeof(ConvMemBlock) == 10, ConvMemBlock_size);

void MemSetAllocator(void *f_malloc, void *f_realloc, void *f_free); /* 0x2DFC30 */
void MemCheckOn(bool hard);                                         /* 0x2DFD34 */
void MemCheckOff(void);                                             /* 0x2DFD8C */
void *MallocConvMemBlock(ushort size, ConvMemBlock *pcmb);          /* 0x2DFDCC */
void *ReallocConvMemBlock(ConvMemBlock *pcmb, ushort size);         /* 0x2DFE2C */
int FreeConvMemBlock(ConvMemBlock *pcmb);                           /* 0x2DFEC0 */
void *MallocChecked(size_t size);                                   /* 0x2DFEF8 */
void *ReallocChecked(void *p, size_t size);                         /* 0x2DFF28 */

/* ------------------------------------------------------------------ LG: memstack, tmpalloc */

typedef struct {
    void *baseptr;       /* +0 bottom of stack */
    long sz;             /* +4 size in bytes */
    void *topptr;        /* +8 current top */
} MemStack;
LG_STATIC_ASSERT(sizeof(MemStack) == 12, MemStack_size);

void MemStackInit(MemStack *ms);                                    /* 0x2FB1C0 */
void *MemStackAlloc(MemStack *ms, long size);                       /* 0x2FB1C8 */
void *MemStackRealloc(MemStack *ms, void *p, long newsize);         /* 0x2FB1EC */
bool MemStackFree(MemStack *ms, void *p);                           /* 0x2FB208 */

extern MemStack *temp_mem_stack;            /* 0x36518C */
extern bool temp_mem_own;                   /* 0x365190 stack was allocated by temp_mem_init (check width) */
void *temp_mem_get_stack(void);                                     /* 0x309110 (also in 2d.h) */
int temp_mem_init(void *stack);                                     /* 0x309118 (also in 2d.h) */
int temp_mem_uninit(void);                                          /* 0x309164 */
void *temp_malloc(size_t n);                                        /* 0x30918C (also in 2d.h) */
void *temp_realloc(void *p, size_t n);                              /* 0x3091B4 */
int temp_free(void *p);                                             /* 0x3091CC: returns 1 if freed out of order (also in 2d.h) */

/* ------------------------------------------------------------------ LG: exit list, dbg, sprintf */

extern int atexit_count;                    /* 0x4469DC (name ours) */
extern void *atexit_funcs[32];              /* 0x4469E0 (name ours) */

typedef void (WATCOMCALL *DbgReportFunc)(int type, char *str);
extern DbgReportFunc dbg_report_routine;    /* 0x35F4C8 */
extern int (*dbg_getch)(void);              /* 0x35F4CC = getch 0x2E7C0B */
extern bool dbg_exit_on_error;              /* 0x35F4D0 (check width) */
extern char *dbg_prefix[4];                 /* 0x35F4D4 "", "WARNING: ", "WARN USER: ", "ERROR: " */
extern int dbg_last_error;                  /* 0x446B40 code passed to DbgReportError */

void DbgReportError(int code, char *fmt, ...);                      /* 0x2E00D0, cdecl */
void DbgReportWarning(char *fmt, ...);                               /* 0x2E0108, cdecl (no code argument) */
void DbgSetReportRoutine(DbgReportFunc f);                          /* 0x2E0188 */
void DbgHandle(int type, int unused, char *str);                    /* 0x2E0190 (edx unused?) */
void DbgHandleC(int type, char *src, char *fmt, void *va);          /* 0x2E01C8 */

typedef char *(WATCOMCALL *LgsStringFunc)(ulong id);
extern LgsStringFunc lgs_string_func;       /* 0x35F420 */
int lg_sprintf(char *buf, char *fmt, ...);                          /* 0x2DEBE0, cdecl */
int lg_vsprintf(char *buf, char *fmt, void *args, int ecx_in);     /* 0x2DEC08 (ecx_in: see lgsprntf.c) */
void lg_sprintf_install_stringfunc(LgsStringFunc f);                /* 0x2DF418 */

/* LG's exit list is installed into this Watcom CRT exit hook (called by exit 0x2DF6AA). */
extern void *crt_exit_hook;                 /* 0x35F454 (name ours) */

/* DOS handlers (name ours) */
extern uchar lg_divtrap_gate[8];            /* 0x35F4C0 IDT gate template: offset lo, sel, attr 0xEE00, offset hi */
extern ushort lg_ctrlbreak_count;           /* 0x446AF2 incremented by the int 0x23 stub 0x30404C */
extern ulong lg_old_int23_off;              /* 0x446B2C */
extern ushort lg_old_int23_sel;             /* 0x446B32 */
extern uchar lg_idt_save[8];                /* 0x446B20 saved IDT gate 0 */
extern uchar lg_idtr[6];                    /* 0x364A28 sidt result (base at 0x364A2A) */
void lg_ctrlbreak_install(void);                                    /* 0x2DFF60 */
void lg_ctrlbreak_restore(void);                                    /* 0x2DFFE4 */
void lg_divtrap_install(void);                                      /* 0x2E0024 */
void lg_divtrap_restore(void);                                      /* 0x2E0084 */

/* ------------------------------------------------------------------ DSTRUCT: datapath */

#define DP_NUM_PATHS 16
struct Datapath {
    uchar numDatapaths;      /* +0 */
    uchar last;              /* +1 1 + index of the path that last succeeded (0 = none/current dir) */
    uchar noCurrent;         /* +2 don't try the current directory first */
    char *datapath[DP_NUM_PATHS]; /* +3 */
};
LG_STATIC_ASSERT(sizeof(struct Datapath) == 0x43, Datapath_size);

void DatapathFree(Datapath *pdp);                                   /* 0x2EF200 */
void *DatapathFOpen(Datapath *pdp, char *fname, char *mode);        /* 0x2EF23C fopen along the path (FILE *) */
int DatapathFind(Datapath *pdp, char *fname, char *buf);           /* 0x2EF478 _dos_findfirst along the path; buf = full name */
/* DatapathAdd (0x2EF580, declared in lg.h) returns a bool (TRUE if any path was added). */
extern char *datapath_empty;                /* 0x35FA7C -> "": DatapathLastPath when no path was used */


/* ------------------------------------------------------------------ DSTRUCT: hash */

typedef int (WATCOMCALL *Equfunc)(void *e1, void *e2);
typedef int (WATCOMCALL *Hashfunc)(void *e);
typedef bool (*HashIterFunc)(void *elem, void *data);   /* called with Watcom regs (eax, edx) */
typedef struct _hashtable {
    int size;            /* +0x00 */
    int sizelog2;        /* +0x04 */
    int elemsize;        /* +0x08 */
    int fullness;        /* +0x0C */
    Equfunc efunc;       /* +0x10 */
    Hashfunc hfunc;      /* +0x14 */
    char *statvec;       /* +0x18 */
    char *vec;           /* +0x1C */
} Hashtable;
LG_STATIC_ASSERT(sizeof(Hashtable) == 0x20, Hashtable_size);

int hash_init(Hashtable *h, int elemsize, int vecsize, Hashfunc hfunc, Equfunc efunc); /* 0x3046A0 */
int hash_copy(Hashtable *t, Hashtable *s);                          /* 0x30473C */
int hash_set(Hashtable *h, void *elem);                             /* 0x3049B4 */
int hash_insert(Hashtable *h, void *elem);                          /* 0x304A44 */
int hash_delete(Hashtable *h, void *elem);                          /* 0x304AB4 */
int hash_lookup(Hashtable *h, void *elem, void **result);           /* 0x304AE0 */
int hash_iter(Hashtable *h, HashIterFunc f, void *data);            /* 0x304B14 */
int hash_step(Hashtable *h, void **result, int *index);             /* 0x304B54 */
int hash_destroy(Hashtable *h);                                     /* 0x304B94 */

/* ------------------------------------------------------------------ DSTRUCT: rect */

typedef struct { short x, y; } LGPoint;
typedef struct { LGPoint ul, lr; } LGRect;
LG_STATIC_ASSERT(sizeof(LGRect) == 8, LGRect_size);

int RectTestSect(LGRect *pr1, LGRect *pr2);                        /* 0x2F2850 */
int RectSect(LGRect *pr1, LGRect *pr2, LGRect *prsect);             /* 0x2F2884 */
void RectUnion(LGRect *pr1, LGRect *pr2, LGRect *prunion);          /* 0x2F290C */
int RectEncloses(LGRect *pr1, LGRect *pr2);                         /* 0x2F2960 */
int RectTestPt(LGRect *prect, LGPoint pt);                          /* 0x2F2994, pt in edx */
void RectMove(LGRect *pr, LGPoint delta);                           /* 0x2F29C8 */
void RectOffsettedRect(LGRect *pr, LGPoint delta, LGRect *proff);   /* 0x2F2A10 */
int RectClipCode(LGRect *pr, LGPoint pt);                           /* 0x2F2A4C */

/* ------------------------------------------------------------------ DSTRUCT: llist, lllist */

typedef struct _llist { struct _llist *pnext, *pprev; } llist;
typedef struct _queue { struct _queue *pnext, *pprev; short priority; } queue;
typedef struct { llist head; llist tail; } llist_head;   /* two sentinels: first = head.pnext, last = tail.pprev */
LG_STATIC_ASSERT(sizeof(llist_head) == 16, llist_head_size);
typedef struct {
    llist head;          /* +0x00 sentinel head */
    llist tail;          /* +0x08 sentinel tail */
    llist *pfree;        /* +0x10 free-node list */
    llist *pNodeStore;   /* +0x14 node blocks */
    ushort nodeSize;     /* +0x18 */
    short numNodesPerBlock; /* +0x1A */
} LlistHead;
LG_STATIC_ASSERT(sizeof(LlistHead) == 0x1C, LlistHead_size);

void LlistInit(LlistHead *plh, ushort nodeSize, short numNodesPerBlock); /* 0x2F2C60 */
void *LlistAddHead(LlistHead *plh);                                 /* 0x2F2CAC */
void *LlistAddTail(LlistHead *plh);                                 /* 0x2F2D00 */
void *LlistAddQueue(LlistHead *plh, short prior);                   /* 0x2F2D58 */
bool LlistMoveQueue(LlistHead *plh, void *pnode, short newprior);   /* 0x2F2DB0 */
void LlistFree(LlistHead *plh, void *pnode);                        /* 0x2F2DBC */
void LlistFreeAll(LlistHead *plh);                                  /* 0x2F2DD8 */
void LlistDestroy(LlistHead *plh);                                  /* 0x2F2E50 */
void LlistGrowList(LlistHead *plh);                                 /* 0x2F2E7C */
void LlistInitNodeBlock(LlistHead *plh, llist *pNodeBlock, ushort blockSize); /* 0x2F2EDC */
void llist_insert_queue(llist_head *plh, queue *pq);                /* 0x313EA0 */
bool llist_move_queue(llist_head *plh, queue *pq);                  /* 0x313ED0 */
int llist_num_nodes(llist_head *plh);                               /* 0x313F50 */

/* ------------------------------------------------------------------ DSTRUCT: pool (name ours) */

typedef struct {
    llist link;          /* +0 */
    ushort used;         /* +8 elements in use */
    /* +0xA elements */
} PoolBlock;
typedef struct _pool_free { struct _pool_free *next; PoolBlock *block; } PoolFreeElem;
typedef struct {
    llist blocks;        /* +0x00 block list head (with +0x08 tail: llist_head pair) */
    llist blocks_tail;   /* +0x08 */
    ushort elemsize;     /* +0x10 min 8 */
    ushort perblock;     /* +0x12 */
    bool freeEmpty;      /* +0x14 free a block when it becomes empty */
    long blocksize;      /* +0x15 */
    long count;          /* +0x19 */
    PoolFreeElem *freelist; /* +0x1D */
} Pool;
LG_STATIC_ASSERT(sizeof(Pool) == 0x21, Pool_size);

void PoolInit(Pool *p, ushort elemsize, ushort perblock, bool freeEmpty); /* 0x2F5FA0 */
void PoolFreeAll(Pool *p);                                          /* 0x2F5FEC */
void *PoolAlloc(Pool *p);                                           /* 0x2F6020 */
void *PoolCalloc(Pool *p);                                          /* 0x2F605C */
void PoolFree(Pool *p, void *elem);                                 /* 0x2F60B0 */

/* ------------------------------------------------------------------ INPUT: kb */

typedef struct { uchar code; uchar state; } kbs_event;  /* returned in ax; code 0xFF = none */
#define KBC_NONE 0xFF
#define KBA_STATE 1
#define KBA_REPEAT 2
#define KBA_SIGNAL 4

/* Low-memory keyboard buffer shared with the ISR (offsets from kb_startup / the ISR; check). */
typedef struct {
    ulong head;          /* +0x000 ISR write index */
    ulong last;          /* +0x004 last scancodes */
    ulong unk8;          /* +0x008 */
    ulong flags;         /* +0x00C */
    kbs_event ring[0x200]; /* +0x010 .. +0x410 */
    uchar state[256];    /* +0x410 per-scancode KBA_* bits */
} kb_lowmem;
LG_STATIC_ASSERT(sizeof(kb_lowmem) == 0x510, kb_lowmem_size);

extern kb_lowmem *kbd_lowmem_start;         /* 0x45D62C */
extern ulong kb_read_index;                 /* 0x45DAF4 */
extern ulong kb_stub_size;                  /* 0x36C998 real-mode stub size (0xD7) */
extern uchar kb_layout;                     /* 0x36C9A0 (name ours) */

int kb_startup(void *init_buf);                                     /* 0x304C80 */
void kb_set_state(int code, uchar bits);                            /* 0x304EC0 (code: full register used as index) */
int kb_shutdown(void);                                              /* 0x304ED0 */
void kb_flush_bios(void);                                           /* 0x305000 */
ushort kb_look_next(void);                                          /* 0x305020 kbs_event in ax: code in al (0xFF = none), state in ah */
ushort kb_next(void);                                               /* 0x30504C kbs_event in ax: code in al (0xFF = none), state in ah */
int kb_set_layout(int layout);                                      /* 0x31AC40 (name ours) */
int kb_get_layout(void);                                            /* 0x31AD68 (name ours) */
void kb_layout_init(void);                                          /* 0x31AD70 (name ours) */
bool kb_cook(kbs_event ev, ushort *cooked, bool *results, bool raw); /* 0x301CE0 */
bool kb_get_cooked(ushort *key);                                    /* 0x301F80 */

/* ------------------------------------------------------------------ INPUT: mouse */

typedef struct _mouse_event {
    short x;             /* +0 */
    short y;             /* +2 */
    uchar type;          /* +4 MOUSE_* event bits */
    ulong timestamp;     /* +5 */
    uchar buttons;       /* +9 */
    char pad[6];         /* +A pad to 16 */
} mouse_event;
LG_STATIC_ASSERT(sizeof(mouse_event) == 16, mouse_event_size);
typedef void (*mouse_callfunc)(mouse_event *e, void *data);   /* Watcom regs (eax, edx) */

extern short mouse_x, mouse_y;              /* 0x44D0CA, 0x44D0CC */
extern bool mouse_inited;                   /* 0x3644D6 (check width) */
extern ulong *mouse_ticks_ptr;              /* 0x3644DC */
extern long mouse_xvel, mouse_yvel;         /* 0x3644E0, 0x3644E4 */
extern long mouse_vel_xl, mouse_vel_yl, mouse_vel_xh, mouse_vel_yh; /* 0x3644F0 0x3644F4 0x3644E8 0x3644EC (Xmin Ymin Xmax Ymax) */
extern ulong mouse_last_vel_time;           /* 0x3644F8 */
extern mouse_event mouse_queue[];           /* 0x44CEC0 */
extern short mouse_qhead, mouse_qtail;      /* 0x44D0C0, 0x44D0C4 (check width) */

int mouse_shutdown(void);                                           /* 0x2FB750 */
int mouse_init(short xsize, short ysize);                           /* 0x2FB7C8 */
int mouse_set_screensize(short x, short y);                         /* 0x2FB8EC */
int mouse_get_xy(short *x, short *y);                               /* 0x2FBAB0 */
int mouse_put_xy(short x, short y);                                 /* 0x2FBAE0 */
int mouse_check_btn(short button, bool *res);                       /* 0x2FBBD4 */
int mouse_look_next(mouse_event *res);                              /* 0x2FBC00 */
int mouse_next(mouse_event *res);                                   /* 0x2FBC48 */
int mouse_flush(void);                                              /* 0x2FBCB8 */
int mouse_generate(mouse_event e);                                  /* 0x2FBCD4, by value, ret 0x10 */
int mouse_set_callback(mouse_callfunc f, void *data, int *id);      /* 0x2FBDB8 */
int mouse_unset_callback(int id);                                   /* 0x2FBE1C */
int mouse_constrain_xy(short xl, short yl, short xh, short yh);     /* 0x2FBE64 */
int mouse_set_rate(short xr, short yr, short thold);                /* 0x2FBF44 */
int mouse_get_rate(short *xr, short *yr, short *thold);             /* 0x2FBFD0 */
int mouse_set_timestamp_register(ulong *tstamp);                    /* 0x2FC010 */
ulong mouse_get_time(void);                                         /* 0x2FC018 */
int mouse_extremes(short *xmin, short *ymin, short *xmax, short *ymax); /* 0x2FC05C */
int mouse_set_lefty(bool lefty);                                    /* 0x2FC0EC */
int mouse_set_velocity_range(long xl, long yl, long xh, long yh);   /* 0x2FC128 */
int mouse_set_velocity(long x, long y);                             /* 0x2FC144 */
int mouse_add_velocity(long x, long y);                             /* 0x2FC1D0 */
int mouse_get_velocity(long *x, long *y);                           /* 0x2FC1EC */

/* ------------------------------------------------------------------ PALETTE */

typedef enum { SHIFT, CYCLE, CBANK } PAL_TYPE;
typedef enum { EMPTY, ACTIVE, DELAYED, FROZEN } PAL_STATUS;
typedef enum { REAL_TIME, STEADY } PAL_MODE;
typedef struct {
    uchar status;        /* +0x00 PAL_STATUS */
    uchar effect;        /* +0x01 PAL_TYPE */
    uchar mode;          /* +0x02 PAL_MODE */
    short dsteps;        /* +0x03 */
    short stages;        /* +0x05 */
    short curr_dstep;    /* +0x07 */
    short curr_stage;    /* +0x09 */
    short entry_1;       /* +0x0B */
    short entry_n;       /* +0x0D */
    short range;         /* +0x0F */
    uchar *from_pal;     /* +0x11 */
    uchar *to_pal;       /* +0x15 */
} PAL_TABLE_ENTRY;
LG_STATIC_ASSERT(sizeof(PAL_TABLE_ENTRY) == 0x19, PAL_TABLE_ENTRY_size);

extern PAL_TABLE_ENTRY *pal_fx_table;       /* 0x35F57C */
extern short pal_table_size;                /* 0x35F580 */
extern long pal_timestamp;                  /* 0x35F584 */
extern short pal_shift_count;               /* 0x35F588 number of active effects */
extern uchar pal_shift_count2;              /* 0x35F58A number of installed shifts (byte) */
extern long pal_remainder;                  /* 0x35F58C */
extern short pal_rate;                      /* 0x448A70 timestamps per step */
extern long pal_smap_delta[];               /* 0x447270 fix Delta_Cmap[768] */
extern long pal_smap_curr[];                /* 0x447E70 fix Shadow_Fixed_Cmap[768] */

void palette_advance_all_fx(long timestamp);                        /* 0x2E7220 */
void palette_advance_effect(uchar id, int steps);                   /* 0x2E72C4 */
uchar palette_install_effect(PAL_TYPE type, PAL_MODE mode, short b1, short b2,
                             short b3, short b4, uchar *ptr1, uchar *ptr2); /* 0x2E7720, ret 0x10 */
int palette_remove_effect(uchar id);                                /* 0x2E78BC */
int palette_freeze_effect(uchar id);                                /* 0x2E7900 */
int palette_unfreeze_effect(uchar id);                              /* 0x2E794C */
PAL_STATUS palette_query_effect(uchar id);                          /* 0x2E7998 */
void palette_change_delay(uchar id, short delay);                   /* 0x2E79B8 */
void palette_set_rate(short ts);                                    /* 0x2E79E0 */
void palette_initialize(short tbl_size);                            /* 0x2E79E8 */
void palette_shutdown(void);                                        /* 0x2E7A50 */
void palette_init_smap(short first, short last, uchar *from, uchar *to, short num_steps); /* 0x2E7A5C, ret 4 */

/* ------------------------------------------------------------------ TIMER: TN layer (names ours) */

/* Timer driver table (7 entries at 0x364A3C, pointer in 0x44DFD0). */
typedef struct {
    long (*reg)(void *cb, long user, long divisor);
    long (*release)(long h);
    long (*start)(long h);
    long (*stop)(long h);
    long (*set_divisor)(long h, long d);
    long (*set_frequency)(long h, long hz);
    long (*set_period)(long h, long us);
} TnTimerDriver;
LG_STATIC_ASSERT(sizeof(TnTimerDriver) == 28, TnTimerDriver_size);

extern ulong tn_ticks;                      /* 0x35F8DC 250 Hz game tick (mouse timestamp register) */
extern ulong tn_ticks2;                     /* 0x35F8E0 += tn_ticks2_step each tick */
extern ulong tn_ticks2_step;                /* 0x35F8E4 */
extern void (*tn_timer_user_cb)(void);      /* 0x35F8E8 */
extern long tn_timer_hz;                    /* 0x35F8EC */
extern ushort tn_tick_frac;                 /* 0x35F8F0 low word of a 32-bit fix accumulator (tn_timer_tick adds a dword, then zeroes the word at 0x35F8F2) */
extern long tn_timer_handle;                /* 0x35F8F4 */
extern long tn_tick_step;                   /* 0x44B284 */
extern TnTimerDriver *tn_timer_driver;      /* 0x44DFD0 */
extern TnTimerDriver tn_timer_ail_driver;   /* 0x364A3C */

ulong tn_get_ticks(void);                                           /* 0x2E0098 */
void tn_timer_startup(void);                                        /* 0x2E00A0 */
int tn_timer_set_rate(long hz, long hz2, void (*cb)(void));         /* 0x2EB264 */
long tn_timer_get_rate(void);                                       /* 0x2EB378 */

/* ------------------------------------------------------------------ TIMER: Miles AIL core (third-party) */

#define AIL_MAX_TIMERS 16
extern void *ail_timer_callback[AIL_MAX_TIMERS]; /* 0x36D164 */
extern long ail_timer_status[AIL_MAX_TIMERS];   /* 0x36D1A4 0 free, 1 stopped, 2 running (check) */
extern long ail_timer_accum[AIL_MAX_TIMERS];    /* 0x36D1E4 */
extern long ail_timer_period[AIL_MAX_TIMERS];   /* 0x36D224 */
extern long ail_timer_pending[AIL_MAX_TIMERS];  /* 0x36D264 */
extern long ail_timer_user[AIL_MAX_TIMERS];     /* 0x36D2A4 */
extern ulong ail_old_int8_off;                  /* 0x36D2E4 */
extern ushort ail_old_int8_sel;                 /* 0x36D2E8 */
extern ushort ail_pit_divisor;                  /* 0x36D2EE */
extern long ail_in_isr;                         /* 0x36D2F6 AIL_background result */
extern long ail_lock_count;                     /* 0x36D2FA */

/* ------------------------------------------------------------------ tnui: TN key/mouse layer (names ours) */

extern LlistHead tnkey_handlers;            /* 0x446EF0 */
extern LlistHead tnmouse_handlers;          /* 0x447080 */
extern short tnmouse_hide_count;            /* 0x4470D2 */
/* tnui functions are declared in include/sup_c.h */


#pragma pack(pop)

#endif
