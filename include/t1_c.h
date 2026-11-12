/* t1_c.h -- declarations for worker t1c (batch T1): TN support code outside the libraries
 * (src/game/tnlib/): joystick glue + kbhit (tnjoy.c), the message pool (msgpool.c), text menus
 * and text boxes on tnui regions (textbox.c), asm fill/swap/qsort helpers (lgmem.c), font
 * string width/height (strwid.c), the small-string pool (strpool.c) and Watcom CRT number
 * conversions + sprintf (crtconv.c).  Names are ours unless noted (no assert paths here).
 * Globals are bound to their original addresses through symbols-t1-c.csv.
 */
#ifndef TN_T1_C_H
#define TN_T1_C_H
#include "support.h"
#include "sup_c.h"
#include "res.h"
#include "2d.h"
#include "t2_c.h"      /* tnui regions: viewport_*, viewport_key_focus (owner t2c) */

#pragma pack(push, 1)

/* ------------------------------------------------------------------ tnjoy.c */
/* Joystick driver (0x2F34F4-0x2F3740, marked crt in the map; original code, names ours). */
uchar joy_init(int mode);                   /* 0x2F35B4 returns TRUE if a stick answers */
void joy_center(void);                      /* 0x2F34F4 */
void joy_read_axes(char *axes);             /* 0x2F3544 fills 4 bytes */
uchar joy_read_buttons(void);               /* 0x2F3580 */
/* replay layer: replay_29A940 / replay_29A518 are in g1_c.h (owner g1c; tnjoy.c includes it) */
extern uchar tn_replay_recording;           /* 0x35C02C */
extern uchar tn_replay_playing;             /* 0x35C02D */

extern uchar tn_joy_present;                /* 0x35F590 */
extern char tn_joy_axes[4];                 /* 0x448D80 */
extern uchar tn_joy_buttons;                /* 0x448D84 */
extern int _cbyte;                          /* 0x364A0C Watcom ungetch character */

bool TNJoyInit(int mode);                   /* 0x2E7B60 */
void TNJoyPoll(void);                       /* 0x2E7BA0 */
int kbhit(void);                            /* 0x2E7BF4 Watcom CRT (DOS int 21h ah=0Bh) */

/* ------------------------------------------------------------------ msgpool.c */
typedef struct { ushort a, b; } MsgAddr;    /* printed "%d.%d" */
typedef struct {
    ushort next;        /* +0  free list / bucket chain (pool index; entry 0 heads the free list) */
    uchar pad2;         /* +2 */
    uchar flag;         /* +3  cleared by MsgAlloc */
    ulong time;         /* +4  *msg_time_ptr when sent */
    MsgAddr from;       /* +8 */
    MsgAddr to;         /* +C  hash key */
    short data[8];      /* +10 */
} Msg;
LG_STATIC_ASSERT(sizeof(Msg) == 32, Msg_size);
typedef struct { ushort head, tail; } MsgBucket;
typedef struct {
    MsgBucket bucket[16];       /* +00 0x44B780 */
    ulong key;                  /* +40 0x44B7C0 receive iterator: destination */
    MsgBucket *cur_bucket;      /* +44 0x44B7C4 */
    ushort cur;                 /* +48 0x44B7C8 next entry to look at */
    ushort prev;                /* +4A 0x44B7CA entry before it */
} MsgHash;
LG_STATIC_ASSERT(sizeof(MsgHash) == 0x4C, MsgHash_size);
#define MSG_HASH(a, b) ((((ulong)(a)) * 7 + ((ulong)(b)) * 3) & 15)

extern MsgHash msg_hash;                    /* 0x44B780 */
extern long msg_pool_count;                 /* 0x44B7CC */
extern Msg *msg_pool;                       /* 0x44B7D0 */
extern ulong *msg_time_ptr;                 /* 0x35F98C (defaults to a zero at 0x35F988) */
extern void *msg_dump_func;                 /* 0x35F990 = MsgFormat */

void MsgPoolInit(long count);               /* 0x2EC5B0 */
void MsgPoolShutdown(void);                 /* 0x2EC610 (AtExit) */
Msg *MsgAlloc(void);                        /* 0x2EC630 */
void MsgFree(Msg *m);                       /* 0x2EC65C */
void MsgSend(Msg *m);                       /* 0x2EC678 */
Msg *MsgReceive(MsgAddr *to);               /* 0x2EC708 */
void MsgFlush(MsgAddr *to);                 /* 0x2EC820 */
void MsgDump(Msg *m);                       /* 0x2EC850 (unreferenced) */
int MsgFormat(char *buf, Msg *m);           /* 0x2EC86C */

/* ------------------------------------------------------------------ textbox.c */
/* Drawing style (current one in tbox_style). */
typedef struct {
    uchar flags;        /* +0 TBS_xxx */
    uchar pad1;
    ushort font;        /* +2 font resource id */
    ulong back;         /* +4 background: Ref of a bitmap (TBS_BITMAP) or image id (TBS_IMAGE) */
    uchar bgcolor;      /* +8 */
    uchar textcolor;    /* +9 */
    uchar pad[2];
    uchar border[4];    /* +C bevel: [sel] top/left, [sel^1] bottom/right; frame: 4 rings */
} TBoxStyle;
#define TBS_BEVEL 1
#define TBS_FRAME 2
#define TBS_BITMAP 4
#define TBS_IMAGE 8

/* tnui region (layout owned by batch T2c, viewport_2E55D8); only the fields used here. */
typedef struct {
    void *cls;          /* +0 */
    uchar hdr[6];
    uchar type;         /* +A 0..3 */
    uchar vflags;       /* +B */
    LGRect rect;        /* +C screen rectangle */
    grs_canvas cnv;     /* +14 (bm.w +1C, bm.h +1E, gc.bcolor +28) */
    uchar more[0x18];   /* +54 */
    TBoxStyle *style;   /* +6C */
} TBoxRegion;

typedef struct {
    short count;        /* +0 */
    long unk2;          /* +2 */
    char *keys;         /* +6 hot key per item */
    char **strings;     /* +A */
} TMenuItems;
LG_STATIC_ASSERT(sizeof(TMenuItems) == 14, TMenuItems_size);

typedef struct {
    TBoxRegion r;
    TMenuItems items;   /* +70 */
    short lineh;        /* +7E font height */
    short hilite;       /* +80 item under the mouse, -1 none */
    uchar hicolor;      /* +82 0: no highlighting */
    short xmargin;      /* +83 */
    short ymargin;      /* +85 */
} TMenu;

typedef struct {
    TBoxRegion r;
    char *text;         /* +70 string-pool copy */
    short mx, my;       /* +74 margins */
    short x, y;         /* +78 text position */
    uchar nlines;       /* +7C gr_font_string_wrap result */
    uchar tflags;       /* +7D 1 wrap, 2 centre x, 4 centre y */
} TBox;

typedef struct {
    ushort a, b, c;     /* +0 */
    void *event;        /* +6 bool (*)(region, event) */
    void *draw;         /* +A void (*)(region) */
    ushort d;           /* +E */
} TBoxClass;

extern TBoxClass tmenu_class;               /* 0x35F9D4 */
extern TBoxClass tbox_class;                /* 0x35F9E4 */
extern TBoxStyle *tbox_style;               /* 0x35F9B8 current style */
extern TBoxStyle tbox_styles[2];            /* 0x35F994 */
extern short tbox_margin_x;                 /* 0x35F9C8 */
extern short tbox_margin_y;                 /* 0x35F9CA */
extern TMenu *tmenu_modal;                  /* 0x44B7F0 */
extern uchar tmenu_key;                     /* 0x44B7F4 */
extern uchar tmenu_running;                 /* 0x44B7F5 */
/* viewport_key_focus 0x447240 (TnRegion *): t2_c.h */
extern void *tnm_state_ptr;                 /* 0x4470BC first dword: mouse position */

/* tnui regions (viewport_2E55D8 .. viewport_2E5C94): t2_c.h; gfx glue (batch T2a) */
void gfx_2E4238(ulong id, int x, int y);    /* 0x2E4238 draw image */
void GfxDrawText(int font, char *s, int x, int y); /* 0x2E4304 */
void GameTimeFrame(void);                   /* 0x2DE888 (G1a) */
int gen_hline(short x0, short y, short x1); /* 0x2ED5A0 */
int gen_vline(short x, short y0, short y1); /* 0x2ED650 */

void tbox_draw_back(TBoxRegion *r, TBoxStyle *st, short sel);   /* 0x2ECCF0 */
TMenu *tmenu_create(void *params, TMenuItems *items, uchar hicolor); /* 0x2ECE84 */
uchar tmenu_run(void *params, TMenuItems *items, uchar hicolor);    /* 0x2ECF5C */
bool tmenu_key_handler(void *ev);           /* 0x2ECFD0 */
void tmenu_set_items(TMenu *m, TMenuItems *items); /* 0x2ED040 */
bool tmenu_event(TMenu *m, void *ev);       /* 0x2ED098 */
void tmenu_draw(TMenu *m);                  /* 0x2ED16C */
short tmenu_item_at(TMenu *m, LGPoint pt);  /* 0x2ED220 */
void tmenu_draw_item(TMenu *m, int i);      /* 0x2ED270 */
TBox *tbox_create(void *params, char *text, long margins, uchar flags); /* 0x2ED360 */
void tbox_set_text(TBox *b, char *text);    /* 0x2ED3A0 */
bool tbox_event(TBox *b, void *ev);         /* 0x2ED490 */
void tbox_draw(TBox *b);                    /* 0x2ED4B0 */

/* ------------------------------------------------------------------ lgmem.c */
void *lg_memset(void *dst, ulong pattern, int unused, ulong n);    /* 0x2F0E80 eax,edx,ecx */
ulong *lg_memset32(ulong *dst, ulong val, int unused, ulong n);   /* 0x2F0EB7 eax,edx,ecx */
void lg_memswap(void *a, void *b, ulong n);                        /* 0x2F0F23 esi,edi,ecx */
void lg_qsort(void *base, ulong num, ulong width, void *cmp);      /* 0x2F0F49 */

/* ------------------------------------------------------------------ strwid.c */
short gr_font_string_width(grs_font *f, char *s);   /* 0x2F2AA0 (2D STRWID.C) */
short gr_font_string_height(grs_font *f, char *s);  /* 0x2F2B00 (name ours) */

/* ------------------------------------------------------------------ strpool.c */
#define STRPOOL_N 5
extern short strpool_sizes[STRPOOL_N];      /* 0x3604F8 8,16,32,64,128 */
extern short strpool_counts[STRPOOL_N];     /* 0x360502 64,32,32,16,8 */
extern uchar strpool_need_init;             /* 0x36050C */
extern Pool *strpool_ptrs[STRPOOL_N];       /* 0x44C5E0 */
extern Pool strpool_pools[STRPOOL_N];       /* 0x44C5F4 */
char *strpool_dup(char *s);                 /* 0x2F2F20 */
void strpool_free(char *s);                 /* 0x2F2FF0 */

/* ------------------------------------------------------------------ crtconv.c (Watcom CRT) */
extern const char utoa_alphabet[37];        /* 0x2F315E (in the code segment) */
extern const char ultoa_alphabet[37];       /* 0x2F327E */
char *utoa(unsigned value, char *buf, int radix);   /* 0x2F3183 */
char *itoa(int value, char *buf, int radix);        /* 0x2F31CB */
char *ultoa(ulong value, char *buf, int radix);     /* 0x2F32A3 */
char *ltoa(long value, char *buf, int radix);       /* 0x2F32E9 */
void mem_putc(void *specs, int c);                  /* 0x2F3304 sprintf's output callback */
int sprintf(char *buf, const char *fmt, ...);       /* 0x2F3312 */
int __prtf(void *dest, const char *fmt, char **args, void *putc); /* 0x313F6B Watcom CRT */

#pragma pack(pop)
#endif
