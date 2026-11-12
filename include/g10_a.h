/* g10_a.h -- batch G10, worker g10a: the shell's UI widget library (M096, uiwidget.c) and the
 * first part of the shell screens module (M097 0x2B6A90-0x2BCA80, termscr.c: the squad /
 * mission / equipment screens of respterm.res).
 *
 * The shell runs in VESA 640x480 (uiwidget_2B6714 switches the mode); everything is drawn into
 * tnui_back_canvas and blitted.  Screens (shell_2B6EB4 enters one, shell_2B6F34 leaves) are
 * numbered by shell_screen (0x43D46C); each has an init function (shell_scr_init[]) and a mouse
 * handler (shell_scr_mouse[]).  Buttons are rectangles in uiw_rects[1..uiw_rect_count]; the
 * frame hook ShellUiHook sends the script key 0x90 + button when the button under the mouse
 * changes, button presses wake the shell thread (uiwidget_2B60B4) with a result code.
 */
#ifndef TN_G10_A_H
#define TN_G10_A_H
#include "game.h"
#include "2d.h"
#include "res.h"
#include "support.h"
#include "g9_a.h"      /* camp_*, mstat_*, unit_tab, ord_table, voice_soldier, ai_fire_mask ... */
#include "g9_c.h"      /* (g9_b.h) shell_thread */
#include "g1_a.h"      /* SndPlay */
#include "g5_a.h"      /* disp_blend_buf */
#include "t1_a.h"      /* ScriptSetPal, ScriptWake, ScriptSendKey, script_text_cb */
#include "t1_c.h"      /* TBox, tbox_create, tbox_style */
#include "t2_a.h"      /* GfxInit2, GfxDrawText, gfx_2E421C */
#include "t2_c.h"      /* TnRegion, viewport_*, tnui_add_blit_rect */
#include "sup_c.h"     /* tnui_full_rect, tnui_back_canvas, Mouse*, Key* */
#include "g1_b.h"      /* game_391E50, game_359708 */
#include "g1_c.h"      /* game_35CD9C */
#include "g2_a.h"      /* ord_table, voice_soldier */

/* ------------------------------------------------------------------ M096 uiwidget.c */
extern long uiw_hook_on;                 /* 0x35CCF8 ShellUiHook installed */
extern TBox *uiw_tbox;                   /* 0x35CCFC caption / script text box */
extern uchar uiw_pal_a[18];              /* 0x35CD00 colours 0xFA..0xFF */
extern uchar uiw_pal_b[18];              /* 0x35CD12 colours 0xF4..0xF9 */
extern TBoxStyle uiw_style;              /* 0x35CD24 text box style (font +2, text colour +9) */
extern TnRegionParams uiw_cover_params;  /* 0x35CD44 full-screen region while the hook runs */
extern uchar uiw_cover_on;               /* 0x35CD55 */
extern TnRegion *uiw_cover;              /* 0x35CD58 */
extern TnRegionParams uiw_tbox_params;   /* 0x35CD5C caption box: bottom quarter of the screen */
extern long uiw_tbox_margins;            /* 0x35CD6E */
extern TnRegionParams uiw_tbox_params2;  /* 0x35CD74 text box over a button */
extern long uiw_tbox_margins2;           /* 0x35CD86 */
extern short uiw_modes[];                /* 0x35CD88 graphics modes ([1 + n] for mode n) */
extern LGRect uiw_rects[50];             /* 0x43CC40 buttons 1..49 */
extern long uiw_hover;                   /* 0x43CDD0 button under the mouse (0 none) */
extern long uiw_rect_count;              /* 0x43CDD4 highest button number in use */
extern long uiw_mouse_x, uiw_mouse_y;    /* 0x43CDD8 / 0x43CDDC */
extern grs_font *uiw_font;               /* 0x43CDE0 resource 0x167 (locked by uiwidget_2B6098) */

void uiwidget_2B5C90(void);                                     /* 0x2B5C90 native 15 (palette) */
void uiwidget_2B5CC0(void);                                     /* 0x2B5CC0 native 9: caption box */
void uiwidget_2B5D94(long n, long centre_x, long centre_y);     /* 0x2B5D94 native 14: box on button n */
void uiwidget_2B5E48(long font, long colour);                   /* 0x2B5E48 native 19 */
void uiwidget_2B5E58(long mx, long my);                         /* 0x2B5E58 native 11 */
long uiwidget_2B5E70(void);                                     /* 0x2B5E70 native 13: lines */
long uiwidget_2B5E80(void);                                     /* 0x2B5E80 text width (no callers) */
long uiwidget_2B5EBC(long font, char *s);                       /* 0x2B5EBC (no callers) */
long uiwidget_2B5EDC(void);                                     /* 0x2B5EDC native 12: font height */
long uiwidget_2B5F04(long font);                                /* 0x2B5F04 (no callers) */
void uiwidget_2B5F24(void);                                     /* 0x2B5F24 native 10: close box */
void uiwidget_2B5F44(void);                                     /* 0x2B5F44 native 16: no buttons */
void uiwidget_2B5F68(long id, long x0, long y0, long x1, long y1);       /* 0x2B5F68 native 17 */
void uiwidget_2B5FA8(long on);                                  /* 0x2B5FA8 native 18: hook on/off */
long uiwidget_2B6014(void);                                     /* 0x2B6014 native 20: hover */
long uiwidget_2B601C(long pt);                                  /* 0x2B601C button at a point */
long uiwidget_2B607C(void);                                     /* 0x2B607C native 29: under mouse */
void uiwidget_2B6098(void);                                     /* 0x2B6098 lock the font */
void uiwidget_2B60A8(void);                                     /* 0x2B60A8 unlock it */
void uiwidget_2B60B4(long code);                                /* 0x2B60B4 wake the shell thread */
void uiwidget_2B60CC(void);                                     /* 0x2B60CC show the whole screen */
void uiwidget_2B60DC(long id, Ref bm, grs_font *f, long pt, void *s, long col); /* 0x2B60DC */
void uiwidget_2B61D8(long id, long box, long fill, grs_font *f, long pt, long size, char *s); /* 0x2B61D8 */
void uiwidget_2B63D8(long id, LGRect *r);                       /* 0x2B63D8 */
void ShellUiHook(int slot);                                     /* 0x2B6428 frame hook 12 */
void uiwidget_2B6480(long id, char *text);                      /* 0x2B6480 script text callback */
void uiwidget_2B648C(void);                                     /* 0x2B648C (no callers) */
void uiwidget_2B64F8(void);                                     /* 0x2B64F8 button frames (debug) */
void uiwidget_2B6560(void);                                     /* 0x2B6560 (no callers) */
uchar uiwidget_2B6580(void *ev);         /* 0x2B6580 key handler */
void uiwidget_2B6620(Ref bm, Ref pos);                          /* 0x2B6620 native 23 */
long uiwidget_2B66F0(long x, long y);                           /* 0x2B66F0 native 24: pixel */
void uiwidget_2B6670(long fill, long parm, long what);          /* 0x2B6670 native 25 */
void uiwidget_2B6700(long x, long y);                           /* 0x2B6700 native 27 */
void uiwidget_2B6714(long n);                                   /* 0x2B6714 native 26: mode */
void uiwidget_2B67B4(long how, char *s, long x, long y);        /* 0x2B67B4 rotated text (no callers) */
void uiwidget_2B6A48(Ref ref);                                  /* 0x2B6A48 native 30: palette */

/* ------------------------------------------------------------------ M097 termscr.c (part 1)
 * The shell's screen globals are shared with g10b (M097 0x2BCA80-0x2C3440, g10_b.h includes
 * this header).  Note the original often reads a short as `mov eax, [addr - 2]; sar eax, 16`. */

/* A scrolling list of a screen (g10b's reading), 0x4E bytes. */
#pragma pack(push, 2)
typedef struct {
    short count, page;
    ushort resid;
    short top, last, scroll, lines;
    schar items[0x40];
} ShList;
#pragma pack(pop)
LG_STATIC_ASSERT(sizeof(ShList) == 0x4E, ShList_size);

extern long shell_resfile;               /* 0x35CD90 respterm.res (-1 closed) */
extern long shell_35CD94;                /* 0x35CD94 background 0xCB instead of 0x1C0 */
extern void *shell_scr_init[29];         /* 0x35CDA0 screen init functions (Watcom) */
extern void *shell_scr_mouse[29];        /* 0x35CE14 screen mouse / key handlers (Watcom) */
extern LGRect shell_rects[5];            /* 0x35CE88 buttons 7..11 of screen 0 */
extern long shell_pos[13];               /* 0x35CEB0 packed x | y << 16 button positions */
extern LGPoint shell_pt[12];             /* 0x35CEE4 picture / name / text of screens 7..10 */
extern long shell_size[3];              /* 0x35CF14 cell sizes w | h << 16 of screen 25 */
extern uchar shell_35CF20, shell_35CF21, shell_35CF22;  /* 0x35CF20.. */
extern long shell_35CF24[2];             /* 0x35CF24 member card: tall weapon cell positions */
extern long shell_35CF2C;                /* 0x35CF2C member card: tall cell size */
extern long shell_35CF30[4];             /* 0x35CF30 member card: weapon / ammo cell positions */
extern long shell_35CF40;                /* 0x35CF40 member card: weapon cell size */
extern long shell_35CF44, shell_35CF48;  /* 0x35CF44 / 48 member card: soldier cell pos / size */
extern short shell_list_rect[6];         /* 0x35CF4C {btn base, page, x0, x1, y0, dy} (g10b) */
extern long shell_35CF58, shell_35CF5C, shell_35CF60, shell_35CF64, shell_35CF68, shell_35CF6C;  /* arrow positions (g10b) */

extern GameSpeaker shell_squad_save[4]; /* 0x43CE00 copy of the squad records (shell_2B9538) */
extern uchar shell_43D2F8[16];           /* 0x43D2F8; the soldier list is at +4 (see below) */
#define shell_soldiers ((schar *)(shell_43D2F8 + 4))   /* 0x43D2FC squad soldier list */
extern void *shell_handler;              /* 0x43D308 mouse / key handler of the screen */
extern char *shell_text;                 /* 0x43D310 text buffer (disp_blend_buf) */
extern grs_font *shell_font;             /* 0x43D314 resource shell_font_id (0x168) */
/* game_43D318 0x43D318 (2 open, 4 closed here): g9_b.h */
extern short shell_rep_btn;              /* 0x43D31C scroll auto-repeat (g10b) */
extern ulong shell_rep_tick;             /* 0x43D31E */
extern uchar camp_390328[][6];          /* 0x390328 per-member campaign table (orig) */
extern ShList shell_list_ms;             /* 0x43D322 missions */
extern ShList shell_list_5b4;            /* 0x43D370 */
extern ShList shell_list_5b3;            /* 0x43D3BE */
extern ShList shell_list_5b2;            /* 0x43D40C */
extern short shell_43D45A, shell_43D45C, shell_43D45E, shell_43D460;   /* 0x43D45A.. */
extern short shell_43D464, shell_43D466; /* 0x43D464 / 0x43D466 */
extern short shell_count;                /* 0x43D468 soldiers in the list */
extern short shell_43D46A;               /* 0x43D46A */
extern short shell_sel;                  /* 0x43D46C selected item */
extern short shell_screen;               /* 0x43D46E current screen */
extern short shell_43D470;               /* 0x43D470 wake code of the "back" button */
extern short shell_font_id;              /* 0x43D472 */
extern short shell_43D474;               /* 0x43D474 number of list buttons */
extern uchar shell_43D476[13];           /* 0x43D476 soldier -> position in the list (1..12) */
extern uchar shell_43D483, shell_43D484; /* 0x43D483 / 0x43D484 */

void shell_2B6A90(void);                 /* 0x2B6A90 build the soldier list */
uchar shell_2B6B00(long i);              /* 0x2B6B00 (no callers) group of soldier i */
uchar shell_2B6B24(long i);              /* 0x2B6B24 3 squad member, else camp status */
Ref shell_2B6B94(long i);                /* 0x2B6B94 name of soldier i */
ushort shell_2B6BC0(Ref r);              /* 0x2B6BC0 bitmap width */
ushort shell_2B6BCC(Ref r);              /* 0x2B6BCC bitmap height */
void shell_2B6BD8(void);                 /* 0x2B6BD8 native 73: open respterm.res */
void shell_2B6C28(void);                 /* 0x2B6C28 native 74: close it */
long shell_2B6C74(void);                 /* 0x2B6C74 native 80 */
void shell_2B6CDC(long screen);          /* 0x2B6CDC background and title */
void shell_2B6EB4(long screen);          /* 0x2B6EB4 native 75: enter a screen */
void shell_2B6F34(void);                 /* 0x2B6F34 native 76: leave it */
uchar shell_2B6F64(TnEvent *ev);         /* 0x2B6F64 screen 0 mouse */
void shell_2B6FCC(void);                 /* 0x2B6FCC screen 0 */
uchar shell_2B7178(TnEvent *ev);         /* 0x2B7178 screens 1..5 mouse */
void shell_2B7208(void);                 /* 0x2B7208 screen 1 */
void shell_2B7370(void);                 /* 0x2B7370 screen 2 */
void shell_2B749C(void);                 /* 0x2B749C screen 3 */
void shell_2B7598(void);                 /* 0x2B7598 screen 4 */
uchar shell_2B7694(long i);              /* 0x2B7694 entry i of list 0x5B6 reached */
void shell_2B76BC(void);                 /* 0x2B76BC screen 5 */
uchar shell_2B7820(TnEvent *ev);         /* 0x2B7820 screen 6 mouse */
void shell_2B7978(void);                 /* 0x2B7978 screen 6 */
uchar shell_2B7EC8(TnEvent *ev);         /* 0x2B7EC8 screens 7..10 mouse */
void shell_2B80F0(void);                 /* 0x2B80F0 screen 7 */
void shell_2B84F8(void);                 /* 0x2B84F8 screen 8 */
void shell_2B87BC(void);                 /* 0x2B87BC screen 9 */
void shell_2B8B94(void);                 /* 0x2B8B94 screen 10 */
uchar shell_2B8E80(TnEvent *ev);         /* 0x2B8E80 screen 25 mouse / key */
void shell_2B8F60(void);                 /* 0x2B8F60 screen 25: squad equipment */
void shell_2B9538(void);                 /* 0x2B9538 save the squad records */
void shell_2B960C(long m, ulong *out);   /* 0x2B960C list 0x6B4 mask */
void shell_2B9694(long m, ulong *out);   /* 0x2B9694 list 0x6B5 mask */
uchar shell_2B97C0(TnEvent *ev);         /* 0x2B97C0 screen 11 mouse handler */
void shell_2BA5B8(long idx);             /* 0x2BA5B8 list popup info panel */
void shell_2BB45C(void);                 /* 0x2BB45C screen 11 draw: mission squad */
void shell_2BBCA4(long at);              /* 0x2BBCA4 member card at x | y << 16 */
uchar shell_2BBFBC(TnEvent *ev);         /* 0x2BBFBC screen 12 mouse */
void shell_2BBFFC(void);                 /* 0x2BBFFC screen 12 draw */
uchar shell_2BC018(TnEvent *ev);         /* 0x2BC018 screen 13 mouse: list 0x5B2 */
void shell_2BC094(long n, long y);       /* 0x2BC094 its list line */
void shell_2BC13C(void);                 /* 0x2BC13C fill list 0x5B2 */
void shell_2BC158(void);                 /* 0x2BC158 screen 13 draw */
uchar shell_2BC21C(TnEvent *ev);         /* 0x2BC21C screens 14 / 15 mouse */
void shell_2BC230(void);                 /* 0x2BC230 screen 14 draw: entry text */
void shell_2BC42C(long n);               /* 0x2BC42C entry header */
void shell_2BC50C(void);                 /* 0x2BC50C screen 15 draw */
uchar shell_2BC5E8(TnEvent *ev);         /* 0x2BC5E8 screen 16 mouse: list 0x5B3 */
void shell_2BC65C(long n, long y);       /* 0x2BC65C its list line */
void shell_2BC704(void);                 /* 0x2BC704 fill list 0x5B3 */
void shell_2BC720(void);                 /* 0x2BC720 screen 16 draw */
uchar shell_2BC7E4(TnEvent *ev);         /* 0x2BC7E4 screen 17 mouse */
void shell_2BC7F8(void);                 /* 0x2BC7F8 screen 17 draw: entry text */

#endif
