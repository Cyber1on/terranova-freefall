/* g10_b.h -- batch G10, worker g10b: the shell screens of M097 from 0x2BCA80 to 0x2C3440
 * (respterm.res screens 18..28: mission notes and objectives lists, the mission list, the
 * debriefing, statistics, times, soldier details, the campaign text pager and the soldier
 * table; the scrolling list helpers, the post-mission notice popup, the quit dialog and the
 * two pop-up selection lists).
 *
 * Lists (ShList, g10_a.h): count, page (lines per page), resid, top, last (index of the last
 * item, count - 1), scroll / lines (wrapped text of the selected item), items[] (item numbers,
 * listed last first).  The screens share shell_text (the selected item's text) and the header
 * drawn by shell_2C0418 (six tab bitmaps = buttons 1..6, "New: n" / "Total: n" counts).
 */
#ifndef TN_G10_B_H
#define TN_G10_B_H
#include "g10_a.h"
#include "g1_b.h"      /* game_359708, ffmem_2C4F40 / 2C4FE4 */
#include "g1_c.h"      /* fileutil_2CD804, GfxDrawText */
#include "g2_a.h"      /* ord_table, voice_soldier */
#include "g2_c.h"      /* ordn_mission_state */
#include "g3_c.h"      /* damage_27E200, UnitTabRec */
#include "g7_a.h"      /* hud_text_log, hud_26843C, hud_2686EC */
#include "g7_b.h"
#include "g9_b.h"      /* game_paused_menu */

/* The handlers get a TnEvent (t2_c.h): type +0 (1 click, 2 release, 4, 0x40 / 0x80 move /
 * drag, 0x100 key), key code the short at +4 (pad), point +8. */

/* ------------------------------------------------------------------ data (0x35CF70..) */
extern uchar shell_35CF70;              /* 0x35CF70 mission list entered (Esc / back clears) */
extern long shell_35CF72;               /* 0x35CF72 mission list arrows position {0x23E, 0xC9} */
extern long shell_dbf_pt[7];            /* 0x35CF76 debriefing buttons: 1, 2, soldiers 3..6,
                                           [6] 0x35CF8E the back button of screens 21..28 */
extern schar shell_soldier;             /* 0x35CF92 soldier shown by screen 24 */
extern long shell_tab_pt[4];            /* 0x35CF94 screen 24 soldier tabs */
extern uchar shell_pct_col[5];          /* 0x35CFA4 colour of a weapon's ammo % / 25 */
extern uchar shell_text_col[5];         /* 0x35CFA9 colours of the pager's ^R0..^R4 */
extern uchar shell_tab_of[0x24];        /* 0x35CFAE tab lit per screen; from +0x16 (0x35CFC4)
                                           the shorts the tab buttons wake with (shared bytes) */
extern long shell_35CFD2;               /* 0x35CFD2 mission list arrows position (2C0E74) */
extern uchar shell_note_count;          /* 0x35CFD6 notices still to show */
extern long shell_note_done;            /* 0x35CFD8 the notices were closed (script reads it) */
extern uchar shell_note_mission;        /* 0x35CFDC mission the notices were made for */
extern uchar shell_note_n[3];           /* 0x35CFDD notices per kind: 0 medals (res 0x6B4),
                                           1 (0x6B5), 2 (0x6B6) */
extern long shell_quit_result;          /* 0x35CFE0 quit dialog: 1, 2, 3 (script reads it) */
extern uchar shell_pop_flags;           /* 0x35CFE4 pop-up list: 1 done, 2 redraw, 4 moved off
                                           the selection, 8 cancelled */
extern uchar shell_pop_ok;              /* 0x35CFE5 the item under the mouse may be chosen */
extern long shell_pop_cur;              /* 0x35CFE8 item under the mouse */
extern uchar shell_pop_out;             /* 0x35CFEC the mouse left the item */
extern LGRect shell_pop_sel;            /* 0x43CDF0 rectangle of the item under the mouse */
extern LGRect shell_pop_box;            /* 0x43CDF8 the pop-up list's box */
extern uchar shell_note_b[13];          /* 0x43D2CC notices of kind 1 */
extern uchar shell_note_c[4];           /* 0x43D2E0 notices of kind 2 */
extern uchar shell_note_a[15];          /* 0x43D2E4 notices of kind 0 */

/* ------------------------------------------------------------------ M097 debrief.c */
uchar shell_2BCA80(TnEvent *ev);        /* 0x2BCA80 screen 18 mouse: objectives list */
void shell_2BCAE8(long item, long y);   /* 0x2BCAE8 its list line */
void shell_2BCB24(void);                /* 0x2BCB24 (no callers) refill the list */
void shell_2BCB40(void);                /* 0x2BCB40 screen 18 draw */
uchar shell_2BCBDC(TnEvent *ev);        /* 0x2BCBDC screen 19 mouse: objective text */
void shell_2BCBF0(void);                /* 0x2BCBF0 screen 19 draw */
uchar shell_2BCDF0(TnEvent *ev);        /* 0x2BCDF0 screen 20 mouse: mission list */
void shell_2BCE58(long item, long y);   /* 0x2BCE58 its list line */
void shell_2BCEB4(void);                /* 0x2BCEB4 (no callers) refill the list */
void shell_2BCEC8(void);                /* 0x2BCEC8 screen 20 draw */
uchar shell_2BCFBC(TnEvent *ev);        /* 0x2BCFBC screen 21 mouse: debriefing */
void shell_2BD1B0(uchar kind, long x, long y);  /* 0x2BD1B0 (no callers) status word */
void shell_2BD2A4(void);                /* 0x2BD2A4 screen 21 draw */
uchar shell_2BD77C(TnEvent *ev);        /* 0x2BD77C screen 22 mouse */
void shell_2BD810(void);                /* 0x2BD810 screen 22 draw: statistics */
void shell_2BE1A8(long base, long t, long x, long y);   /* 0x2BE1A8 a goal's time */
void shell_2BE274(void);                /* 0x2BE274 screen 23 draw: times */
uchar shell_2BE85C(TnEvent *ev);        /* 0x2BE85C screen 23 mouse */
short shell_2BE8E8(void *pba);          /* 0x2BE8E8 rearm cost of a soldier's weapons */
void shell_2BE968(void);                /* 0x2BE968 screen 24 draw: soldier */
uchar shell_2BF34C(TnEvent *ev);        /* 0x2BF34C screen 24 mouse */
void shell_2BF440(void);                /* 0x2BF440 screen 26 draw: mission text */
uchar shell_2BF668(TnEvent *ev);        /* 0x2BF668 screen 26 mouse */
void shell_2BF73C(void);                /* 0x2BF73C empty */
void shell_2BF740(void);                /* 0x2BF740 pager start: clear, line 0 */
void shell_2BF760(void);                /* 0x2BF760 screen 27 draw: text pager (res 0x5B0) */
uchar shell_2BFA08(TnEvent *ev);        /* 0x2BFA08 screen 27 mouse */
long shell_2BFA88(short *a, short *b);  /* 0x2BFA88 qsort compare, descending */
void shell_2BFAC8(void);                /* 0x2BFAC8 screen 28 draw: soldier table */
uchar shell_2C02B8(TnEvent *ev);        /* 0x2C02B8 screen 28 mouse */

/* ------------------------------------------------------------------ M097 shlist.c */
void shell_2C0304(void);                /* 0x2C0304 squad strip (portraits and names) */
void shell_2C0418(void);                /* 0x2C0418 header: tabs and counts */
void shell_2C066C(void);                /* 0x2C066C fill the four lists */
uchar shell_2C06D8(long code);          /* 0x2C06D8 tab button */
void shell_2C072C(ShList *l, long id, long pos);   /* 0x2C072C list arrows (pos x | y << 16) */
void shell_2C089C(ShList *l, long id, long pos);   /* 0x2C089C text arrows */
void shell_2C0A08(ShList *l, long resid, long page); /* 0x2C0A08 fill a list from a resource */
void shell_2C0ACC(ShList *l, short *tbl, void (WATCOMCALL *fn)(long item, long y)); /* 0x2C0ACC */
uchar shell_2C0B7C(ShList *l, long code);          /* 0x2C0B7C list button */
void shell_2C0C78(ShList *l, long code, long base, long wake1, long wake2); /* 0x2C0C78 (ret 4) */
void shell_2C0DE4(ShList *l, long page);           /* 0x2C0DE4 fill the mission list */
void shell_2C0E74(ShList *l, short *tbl, void (WATCOMCALL *fn)(long item, long y)); /* 0x2C0E74 */
void shell_2C0E9C(ShList *l, long code);           /* 0x2C0E9C mission chosen */
uchar shell_2C0F34(TnEvent *ev, ShList *l, long wakecode); /* 0x2C0F34 text screen mouse/keys */
void shell_2C11B0(ShList *l, long code);           /* 0x2C11B0 scroll the text */
uchar shell_2C12B4(uchar *p, long v);              /* 0x2C12B4 (no callers) */
void shell_2C12DC(void);                /* 0x2C12DC wake with the screen */
long shell_2C12EC(long unit, long sel);              /* 0x2C12EC colour of a unit */
long shell_2C1334(long a, long b);                   /* 0x2C1334 (no callers) */
void shell_2C136C(long unit, long x, long y);        /* 0x2C136C (no callers) unit card */
long shell_2C32D4(void);                /* 0x2C32D4 unread notes or objectives */
uchar shell_2C3390(void);               /* 0x2C3390 the mission has notes */

/* ------------------------------------------------------------------ M097 shpopup.c */
void shell_2C1520(void);                /* 0x2C1520 (no callers) notice handlers off */
uchar shell_2C1548(TnEvent *ev);        /* 0x2C1548 notice key / mouse handler */
long shell_2C16D4(void);                /* 0x2C16D4 open the post-mission notices */
void shell_2C18A0(void);                /* 0x2C18A0 draw the next notice */
uchar shell_2C205C(TnEvent *ev);        /* 0x2C205C quit dialog handler */
void shell_2C21F8(void);                /* 0x2C21F8 open the quit dialog */
uchar shell_2C2438(LGRect *r, long base, long n, long sel, long mask, schar *items); /* (ret 8) */
void shell_2C292C(LGRect *r, char *s, long font, grs_canvas *c);     /* 0x2C292C */
uchar shell_2C29F8(LGRect *r, long n, long sel);   /* 0x2C29F8 soldier pop-up list */
void shell_2C2F84(uchar c);             /* 0x2C2F84 (no callers) set a soldier colour */
void shell_2C2FC8(long kit, LGRect *r); /* 0x2C2FC8 kit letters, right aligned */
void shell_2C3060(LGRect *r, long col); /* 0x2C3060 3D framed box */
uchar shell_2C3124(TnEvent *ev);        /* 0x2C3124 pop-up list key / mouse handler */

#endif
