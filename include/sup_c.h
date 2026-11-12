/* sup_c.h -- declarations for worker sup-c: TN timer layer (src/timer), TN key/mouse/cursor
 * layer "tnui" (src/tnui) and DSTRUCT hash (src/dstruct/hash.c; types in support.h).
 * Names marked (ours) have no Shock counterpart.  Globals are bound to their original
 * addresses through symbols-sup-c.csv.
 */
#ifndef TN_SUP_C_H
#define TN_SUP_C_H
#include "support.h"
#include "2d.h"

#pragma pack(push, 1)

/* ------------------------------------------------------------------ CRT / misc used here */
int rand(void);                                             /* 0x317DB5 Watcom CRT */
/* DPMI lock helpers (cdecl, TN/LG "dos" helpers, not decompiled yet; names ours) */
int dpmi_lock_region(void *start, void *end);               /* 0x2EDB28 DPMI 0x600 */
int dpmi_unlock_region(void *start, void *end);             /* 0x2EDB94 DPMI 0x601 */
int dpmi_lock_data(void *p, long size);                     /* 0x2EDC00 */

/* ------------------------------------------------------------------ TIMER: TN layer */
extern long tn_tick_acc;                    /* 0x35F8F0 fix accumulator of tn_tick_step; the
                                             * integer word 0x35F8F2 is moved to tn_ticks */
/* Miles AIL public API wrappers (cdecl, third-party, original code) */
long AIL_register_timer(void *callback);                    /* 0x30A824 */
long AIL_set_timer_divisor(long h, long divisor);           /* 0x30AB44 */
long AIL_release_timer_handle(long h);                      /* 0x30AE90 */
long AIL_start_timer(long h);                               /* 0x30ACC8 */
long AIL_stop_timer(long h);                                /* 0x30ADAC */
long AIL_set_timer_frequency(long h, long hz);              /* 0x30AABC */
long AIL_set_timer_period(long h, long us);                 /* 0x30AA34 */

void tn_timer_tick(long user);                              /* 0x2EB220 cdecl (Miles callback) */
long tn_timer_drv_open(void *cb, long user, long divisor);  /* 0x309B00 */
long tnt_register(void *cb, long user, long divisor);       /* 0x304200 */
long tnt_release(long h);                                   /* 0x304228 */
long tnt_start(long h);                                     /* 0x30423C */
long tnt_stop(long h);                                      /* 0x304250 */
long tnt_set_divisor(long h, long d);                       /* 0x304264 */
long tnt_set_frequency(long h, long hz);                    /* 0x304274 */
long tnt_set_period(long h, long us);                       /* 0x304284 */
long tn_timer_install_ail(void);                            /* 0x304294 */
long tn_timer_shutdown(void);                               /* 0x3042A4 */

/* ------------------------------------------------------------------ tnui: tasks */
/* Per-frame task table of the game (ours): bit n of tn_task_mask enables tn_task_func[n]. */
extern void *tn_task_func[32];              /* 0x446A60 */
extern ulong tn_task_mask;                  /* 0x446AEC */
#define TN_TASK_UI      0x00001             /* 0x2E6174 (gfx glue) */
#define TN_TASK_REPLAY  0x00020             /* 0x2F2C2C (game record/replay) */
#define TN_TASK_MOUSE   0x00080             /* tnmouse_poll */
#define TN_TASK_KEY     0x00100             /* tnkey_poll */
#define TN_TASK_UI2     0x40000             /* 0x2E60F4 (gfx glue) */
extern uchar tn_replay_playing;             /* 0x35C02D game: input comes from a recording */
extern uchar tn_replay_recording;           /* 0x35C02C game: input is being recorded */

/* ------------------------------------------------------------------ tnui: keys */
/* Key event handed to key handlers (12 bytes on the caller's stack, +8 unused). */
typedef struct {
    ulong type;                             /* +0 TNKEY_DOWN / TNKEY_UP */
    short code;                             /* +4 cooked key (bit 8 = KB_FLAG_DOWN flipped) */
    short pad;                              /* +6 0 */
#ifndef TN_WATCOM_ABI
    LGPoint pt;                             /* +8 unset (see dispatch, tnkey.c) */
#endif
} tnkey_event;
#define TNKEY_DOWN 0x100
#define TNKEY_UP   0x200
typedef bool (WATCOMCALL *tnkey_handler)(tnkey_event *e, void *node);

/* Handler list node (0x11 bytes) */
typedef struct {
    llist link;                             /* +0 */
    void *data;                             /* +8 */
    uchar all;                              /* +C also gets TNKEY_UP events */
    tnkey_handler handler;                  /* +D */
} tnkey_node;
LG_STATIC_ASSERT(sizeof(tnkey_node) == 0x11, tnkey_node_size);

extern uchar tnkey_init_buf[];              /* 0x446CF0 kb_startup buffer */
extern ushort tnkey_removed;                /* 0x446F0C handlers marked dead */
extern void *tnkey_hook;                    /* 0x35F530 */
extern uchar tn_replay_state[0x20];         /* 0x44C5B0 cleared by tnkey_init */
extern void *tn_replay_state_ptr;           /* 0x44C5D0 */

/* game code (original) */
void game_replay_get_keys(short **keys, int *n);            /* 0x29A920 */
bool game_replay_key_ok(short code);                        /* 0x29AA84 */
bool game_replay_filter_key(short code);                    /* 0x29A9E0 */
void game_replay_record_key(short code);                    /* 0x29A4A8 */
void game_replay_get_mouse(void **evs, int *n);             /* 0x29A930 */
void game_replay_record_mouse(void *ev);                    /* 0x29A4E0 */
void tnui_replay_task(void);                                 /* 0x2F2C2C game record/replay task */
/* gfx glue (original, next module) */
extern LlistHead tnui_list;                 /* 0x447224 */
extern LGRect tnui_full_rect;               /* 0x446FB8 */
void tnui_task_60f4(void);                                  /* 0x2E60F4 */
void tnui_task_6174(void);                                  /* 0x2E6174 */
bool tnui_mouse_handler(void *e, void *node);               /* 0x2E62D0 */
bool tnui_key_handler(void *e, void *node);                 /* 0x2E5B6C */

void tnkey_init(void);                                      /* 0x2E3690 */
void tnkey_shutdown(void);                                  /* 0x2E3730 */
void tnkey_poll(void);                                      /* 0x2E3748 */
uint tnkey_wait(void);                                      /* 0x2E38AC */
void KeyAddHandler(void *handler, void *data, bool all, bool tail); /* 0x2E38D8 */
void KeyRemoveHandler(void *handler, void *data);           /* 0x2E3900 */
void tnkey_dispatch_code(short code);                       /* 0x2E3950 */
void tnkey_dispatch(tnkey_event *e);                        /* 0x2E3984 */
bool tnkey_null_handler(void);                              /* 0x2E39C4 */
void tnkey_set_hook(void *f);                               /* 0x2E39D0 */

/* ------------------------------------------------------------------ tnui: mouse/cursor */
/* A cursor: bitmap + hotspot (0x14 bytes; a FrameDesc's bm and anchor) */
typedef struct {
    grs_bitmap bm;                          /* +0 */
    LGPoint hot;                            /* +10 */
} TnCursor;
/* Save-under of a drawn cursor (0x1D bytes) */
typedef struct {
    grs_canvas *canvas;                     /* +0 */
    LGPoint pos;                            /* +4 top left */
    TnCursor cur;                           /* +8 saved pixels */
    uchar drawn;                            /* +1C */
} TnSaveUnder;
LG_STATIC_ASSERT(sizeof(TnSaveUnder) == 0x1D, TnSaveUnder_size);

/* Mouse event handed to mouse handlers (12 bytes) */
typedef struct {
    ulong type;                             /* +0 one MOUSE_xxx >> 1 bit, 0x40 motion, 0x80 idle */
    ulong unused;                           /* +4 */
    short x, y;                             /* +8 */
} tnmouse_event;
#define TNM_MOTION 0x40
#define TNM_IDLE   0x80
#define TNM_ANYWHERE 0x400
typedef bool (WATCOMCALL *tnmouse_handler)(tnmouse_event *e, void *node);
typedef struct {
    llist link;                             /* +0 */
    LGRect r;                               /* +8 */
    ulong mask;                             /* +10 */
    tnmouse_handler handler;                /* +14 */
} tnmouse_node;
LG_STATIC_ASSERT(sizeof(tnmouse_node) == 0x18, tnmouse_node_size);
/* draw function: cmd 0 prepare, 1 draw, 2 undraw */
typedef void (*TnCursorDrawFunc)(TnCursor *c, LGPoint pos, uchar cmd);

extern grs_canvas tnui_back_canvas;         /* 0x446F78 (game's back buffer canvas) */
extern grs_canvas tnm_back_canvas;          /* 0x446FD0 cursor sub canvas of the back canvas */
extern grs_canvas tnm_screen_canvas;        /* 0x447010 cursor sub canvas of the screen */
extern LGPoint tnm_last_pos;                /* 0x447060 last position reported */
extern LGPoint tnm_screen_size;             /* 0x447064 */
extern LGPoint tnm_cursor_max;              /* 0x447068 largest cursor (save-under size) */
extern ulong tnm_poll_time;                 /* 0x44706C */
extern ulong tnm_click_time[2];             /* 0x447070 */
extern TnCursorDrawFunc tnm_draw_func;      /* 0x447078 */
extern long tnm_cursor_ref;                 /* 0x44707C */
extern grs_stencil *tnm_stencil;            /* 0x44709C */
extern LGPoint tnm_sten_origin;             /* 0x4470A0 */
extern LGPoint tnm_draw_pos;                /* 0x4470A4 */
extern TnCursor tnm_cursor;                 /* 0x4470A8 */
extern void *tnm_state_ptr;                 /* 0x4470BC (game replay) */
extern LGPoint tnm_pos;                     /* 0x4470C0 current position (6-byte record with +4) */
extern uchar tnm_state_copy[6];             /* 0x4470C6 */
extern ushort tnm_removed;                  /* 0x4470CC */
extern short tnm_sten_h;                    /* 0x4470CE */
extern short tnm_4470d0;                    /* 0x4470D0 */
/* 0x4470D2 hide count: tnmouse_hide_count (support.h) */
extern TnSaveUnder tnm_su2;                 /* 0x4470D4 */
extern TnSaveUnder tnm_su;                  /* 0x4470F4 */
extern uchar tnm_lefty;                     /* 0x447111 */
extern uchar tnm_frozen;                    /* 0x447112 */
extern uchar tnm_moved_at_int;              /* 0x447113 */
extern uchar tnm_int_on;                    /* 0x447114 */
extern uchar tnm_lock;                      /* 0x447115 */
extern uchar tnm_cursor_color;              /* 0x447116 */
extern uchar tnm_cursor_drawn;              /* 0x447118 */
extern uchar tnm_no_events;                 /* 0x447119 */
extern uchar tnm_no_hide;                   /* 0x44711A */
extern short tnm_saved_hide;                /* 0x35F560 (999 = none) */
extern int tnm_int_cb_id;                   /* 0x35F564 */
extern uchar mouseMask;                     /* 0x3644D0 INPUT mouse (sup-b) */

bool tnmouse_init(LGPoint size, LGPoint cursor_max);        /* 0x2E4540 */
void tnmouse_set_screensize(LGPoint size, LGPoint cursor_max); /* 0x2E462C */
void tnmouse_shutdown(void);                                /* 0x2E4744 */
void MouseSetDrawFunc(TnCursorDrawFunc f);                  /* 0x2E475C */
void MouseSetCursorBitmap(TnCursor *c);                     /* 0x2E47FC */
void MouseSetCursor(long ref);                              /* 0x2E4930 */
void MouseMoveTo(LGPoint p);                                /* 0x2E4A68 */
void MouseHide(void);                                       /* 0x2E4B1C */
void MouseShow(void);                                       /* 0x2E4B78 */
void MouseSaveHide(void);                                   /* 0x2E4BC4 */
void MouseHideRect(LGRect *r);                              /* 0x2E4BD4 */
void MouseRestoreHide(void);                                /* 0x2E4CA8 */
void MouseSaveUnderAt(LGPoint pos, grs_canvas *c, TnSaveUnder *su); /* 0x2E4D58 */
void MouseGetSaveRect(LGRect *r, TnSaveUnder *su);          /* 0x2E4DD4 */
void MouseFreeze(void);                                     /* 0x2E4E10 */
void MouseUnfreeze(void);                                   /* 0x2E4E18 */
void MouseConstrain(LGRect *r);                             /* 0x2E4E24 */
void MouseSetStencil(grs_stencil *s, short h, LGPoint origin); /* 0x2E4EA8 */
void MouseClearStencil(void);                               /* 0x2E4F8C */
void MouseIntDrawOn(void);                                  /* 0x2E5004 */
void MouseIntDrawOff(void);                                 /* 0x2E502C */
void tnmouse_poll(void);                                    /* 0x2E504C */
void MouseAddHandler(LGRect *r, ulong mask, void *handler); /* 0x2E5388 */
void MouseRemoveHandler(LGRect *r, ulong mask, void *handler); /* 0x2E53A4 */
void MouseRemoveAllHandlers(void);                          /* 0x2E5408 */
void tnmouse_dispatch(tnmouse_event *e);                    /* 0x2E5414 */
bool tnmouse_null_handler(void);                            /* 0x2E546C */
void tnmouse_draw(void);                                    /* 0x2E5470 */
void tnmouse_undraw(void);                                  /* 0x2E54BC */
void tnmouse_draw_cb(TnCursor *c, LGPoint pos, uchar cmd);  /* 0x2E54E0 */
void MouseSetLefty(bool lefty);                             /* 0x2E5508 */
void tnui_init(void);                                       /* 0x2E5510 */
void tnui_shutdown(void);                                   /* 0x2E5584 */

void cursor_lock_a(void);                                   /* 0x2F2480 */
bool cursor_sten_test(grs_stencil *s, short h, LGPoint pt); /* 0x2F249C */
void cursor_unlock_a(void);                                 /* 0x2F24E4 */
void cursor_lock_all(void);                                 /* 0x2F2500 */
void cursor_interrupt_draw(mouse_event *e, void *data);     /* 0x2F2528 */
void cursor_save_under(TnCursor *c, LGPoint pos, grs_canvas *canvas, TnSaveUnder *su); /* 0x2F25E0 */
void cursor_restore_under(TnSaveUnder *su);                 /* 0x2F26E8 */
bool cursor_in_stencil(short x, short y);                   /* 0x2F2748 */
ulong cursor_constrain(short x, short y);                   /* 0x2F2788 returns an LGPoint in eax */

#pragma pack(pop)
#endif
