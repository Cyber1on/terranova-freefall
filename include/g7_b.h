/* g7_b.h -- batch G7, worker g7b: M016 helmet / HUD frame and tooltips (0x261480-0x2641BF,
 * src/game/hud/helmet.c), M018 radar strip (0x265700-0x26631F, src/game/hud/hudradar.c),
 * M037 in-mission menu (0x27B910-0x27C11F, src/game/hud/hudmenu.c).  Read from __FF.EXE v1.09.
 *
 * The helmet code builds the HUD: 69 HudWidgets (g7c's struct, pool hud_pool 0x389F38, pointers
 * hud_widgets[] 0x389E20) from the rectangles + stencils of resource 0x2FB and the callbacks of
 * hud_widget_defs; it picks the widget set of the current layout (helmet on / off, aircraft
 * camera), runs the per-frame task (HelmTaskHook, game hook slot 11), blits the dirty widgets,
 * routes mouse events to the widgets, shows tooltips and switches between 320x200 and 320x400.
 *
 * Names: the map's placeholders are kept (helm_*, radar_*, menudraw_*, HelmTaskHook).  Shared
 * globals use the names of g7a (g7_a.h) and g7c (g7_c.h); prototypes already declared by other
 * batches are not repeated: g2_a.h (helm_262D4C), g2_b2.h (helm_262A88), g3_a.h (helm_263410),
 * g3_c.h (helm_2627F8), g5_a.h (HelmTaskHook), g7_c.h (helm_26214C), g7_a.h (helm_262278,
 * helm_263BE4).  Globals are bound to their original addresses through symbols-g7-b.csv.
 * Structs are byte-packed.
 */
#ifndef TN_G7_B_H
#define TN_G7_B_H
#include "game.h"
#include "2d.h"
#include "res.h"
#include "support.h"
#include "g7_a.h"      /* HudInit, hud_26xxxx, HudViewReset, hud_* globals; g7_c.h HudWidget,
                          hud_widgets, hudcb_*, hudview_*, hudutil_27B5B0; g1_b.h FfCameraSetup,
                          RadioSpeak, game_391E4C / 391E50, radio_update; g2_a.h targ_*; g3_a.h
                          helm_263410, physics_2D637C; g5_b.h / g5_c.h vd_canvas, ffpipe_*,
                          planet_*, ff_cams_390010 (g5_a.h) */
#include "g2_b.h"      /* reticle_edge_check, game_weapon_types, game_cls3, game_rnd_35B704 */
#include "g4_a.h"      /* game_441354 */
#include "g4_c1.h"     /* aiacc_25AC64 / 25ADF8 */
#include "g6_c.h"      /* sfx_28D4E8 */
#include "t1_b.h"      /* toupper */
#include "t2_a.h"      /* GfxInit, GfxBlit, GfxDrawText, ipal_*, gfx_2E421C, hmd_data */
#include "t2_c.h"      /* tnui_add_dirty_rect, tnui_task_*, TnEvent */
#include "sup_c.h"     /* Mouse*, Key*, tnm_*, cursor_* */

#pragma pack(push, 1)

/* A widget definition (hud_widget_defs 0x35A54C, 14 bytes, 69 of them): callbacks and flags
 * copied into the widget by helm_262184. */
typedef struct {
    void *event;                /* +0 Watcom (w, ev) */
    void *update;               /* +4 (w) */
    void *draw;                 /* +8 (w) */
    uchar flags;                /* +C */
    uchar id;                   /* +D */
} HudWidgetDef;
LG_STATIC_ASSERT(sizeof(HudWidgetDef) == 14, HudWidgetDef_size);

/* Resource 0x2FB: one 10-byte record per widget. */
typedef struct {
    short x0, y0, x1, y1;       /* +0 screen rectangle */
    ushort sten;                /* +8 stencil resource id (0 = rectangular) */
} HudWidgetRec;

/* An in-mission menu item (menu_items 0x35B544, 0x16 bytes, 9 of them). */
typedef struct {
    void *fn;                   /* +00 Watcom fn(ref, 0, buf): writes the item text into buf */
    Ref ref;                    /* +04 text resource (flags & 1) or argument of fn */
    short hot;                  /* +08 hot key letter (low byte) */
    uchar f0a;
    uchar flags;                /* +0B 1: ref is the text */
    uchar f0c[0x0A];
} MenuItem;
LG_STATIC_ASSERT(sizeof(MenuItem) == 0x16, MenuItem_size);

/* A radar contact (6 bytes; list of the player's PBA +0xBE or the aircraft +0x6E, ent 0 ends it). */
typedef struct {
    short ent;                  /* +0 */
    ushort dist;                /* +2 distance * 6 (whole units) */
    short f4;
} RadarContact;

#pragma pack(pop)

typedef void (WATCOMCALL *HudWidgetFn)(HudWidget *w);
typedef void (WATCOMCALL *HudEventFn)(HudWidget *w, void *ev);
typedef void (WATCOMCALL *MenuTextFn)(Ref ref, long a, char *buf);

/* ------------------------------------------------------------------ globals (orig data rows) */
/* M016 helmet */
extern LGRect helm_zero_rect;           /* 0x261480 zeros (data in the code segment) */
extern HudWidgetDef hud_widget_defs[69];/* 0x35A54C */
extern long hud_35A914;                 /* 0x35A914 */
extern uchar hud_tip_flags;             /* 0x35A918 1 tooltip shown, 2 tooltips off */
extern char *hud_res_names[4];          /* 0x35A91C "reshelm?.res" by helmet type (low res) */
extern char *hud_res_names_hi[4];       /* 0x35A92C (high res) */
extern uchar hud_pal_cycle[3][3];       /* 0x35A945 palette cycle data of colours 0xFC-0xFE */
extern uchar hud_35A958;                /* 0x35A958 restore the palette mode next frame */
extern long hud_box_tab[][4];           /* 0x35A960 x0, y0, x1, y1 of hint boxes (read as shorts;
                                           y doubled in high res) */
extern uchar hud_35A990;                /* 0x35A990 an overlay was drawn over the frame */
extern ulong hud_tip_time;              /* 0x35A994 game_time of the last mouse click / move */
extern uchar hud_suit_clr[7];           /* 0x35A998 suit figure colours by damage step */
extern uchar hud_frame_clr[4];          /* 0x35A99F frame colour by helmet type */
extern uchar hud_clut[256];             /* 0x389CB0 fill CLUT of the suit figure */
extern LGRect hud_tip_rect;             /* 0x389DB0 tooltip box */
extern uchar *hud_tluc_389DCC;          /* 0x389DCC translucency table */
extern long hud_res_file;               /* 0x389DD4 reshelm?.res */
extern long hud_res_file2;              /* 0x389DD8 reshelm.res / reshlmhi.res */
extern HudWidget *hud_last;             /* 0x389DE8 last widget of the current layout */
extern HudWidget *hud_first;            /* 0x389DEC first one */
extern HudWidget *hud_w_389E0C;         /* 0x389E0C widget updated before the others */
extern HudWidget *hud_radar;            /* 0x389E14 radar strip widget */
extern HudWidget *hud_w_389E18;         /* 0x389E18 */
extern HudWidget *hud_w_389E1C;         /* 0x389E1C */
extern HudWidget hud_pool[69];          /* 0x389F38 */
extern uchar hud_flags_cam[3];          /* 0x38B9A2 resource 0x2FD0001 (helmet off) */
extern uchar hud_flags_std[3];          /* 0x38B9A5 resource 0x2FD0000 (helmet on) */
extern uchar hud_38B9A8;                /* 0x38B9A8 cleared before the widgets are drawn */
extern uchar hud_38B9A9;                /* 0x38B9A9 copy of it afterwards */
extern uchar hud_pal_fx[3];             /* 0x38B9AA palette effect ids */
extern long hud_390300, hud_390304;     /* 0x390300 / 0x390304 end of an overlay line */
extern uchar hud_392407, hud_392408;    /* 0x392407 / 0x392408 overlay requests */
extern long hud_43D534;                 /* 0x43D534 */
extern ushort game_44136A, game_44136C; /* 0x44136A / 0x44136C player suit values */
extern uchar tnm_447117;                /* 0x447117 */
extern LGRect tnm_dirty[2];             /* 0x447050 rects the mouse code wants blitted */
/* globals of other modules that helm_2617F4 resets */
extern uchar game_3599E8;               /* 0x3599E8 */
extern uchar game_35B150;               /* 0x35B150 */
extern uchar game_35B175;               /* 0x35B175 */
extern long game_35B188;                /* 0x35B188 */
extern uchar game_35B19C;               /* 0x35B19C */
extern long game_35B1A4, game_35B1A8, game_35B1AC, game_35B1B0;  /* 0x35B1A4.. */
extern long game_389BD0, game_389BD4;   /* 0x389BD0 / 0x389BD4 */
/* M018 radar */
extern uchar radar_tick_w[8];           /* 0x35AA40 compass tick widths */
extern uchar *radar_tick_bits[8];       /* 0x35AA48 compass tick bitmaps */
extern grs_bitmap radar_bm;             /* 0x35AA68 bitmap the ticks are drawn with */
extern Ref radar_tick_refs[8][2];       /* 0x35AA78 compass letters (helmet off) */
extern uchar radar_noise;               /* 0x35AABC contacts hidden by damage this frame */
extern ushort radar_x[64];              /* 0x38BA60 screen x of each contact */
extern long radar_mark_x, radar_mark_y; /* 0x38BAE0 / 0x38BAE4 nav point marker (-1 none) */
extern ulong radar_time;                /* 0x38BAE8 game_time of the last redraw */
extern Ref radar_38BAEC;                /* 0x38BAEC */
extern ushort radar_count;              /* 0x38BAF0 contacts drawn */
extern short hud_389BC8;                /* 0x389BC8 entity picked on the radar */
/* M037 menu */
extern LGRect menu_zero_rect;           /* 0x27B910 zeros (data in the code segment) */
extern uchar menu_active;               /* 0x35B53C */
extern long menu_sel;                   /* 0x35B540 highlighted item (-1 none) */
extern MenuItem menu_items[9];          /* 0x35B544 */
extern grs_canvas menu_canvas;          /* 0x392410 */
extern LGRect menu_rect;                /* 0x392450 */

/* ------------------------------------------------------------------ M016 helmet.c */
void helm_261488(void);                 /* (dead) */
void helm_2614B0(void);                 /* (dead) */
void helm_2614D8(uchar type);
void helm_2617F4(void);
void helm_2618E0(void);
void helm_261A00(void);
void helm_261B54(void);                 /* (no callers) */
void helm_261BBC(HudWidget *w);         /* (no callers) */
void helm_261BD8(void);
void helm_261C5C(void);
void helm_261D70(void);
void helm_261DD4(void);
void HelmTaskHook(void);                                   /* 0x261E48 (G7b) */
void helm_262018(void);
void helm_26214C(void);                                              /* 0x26214C (G7b) */
void helm_262184(HudWidget *w, HudWidgetRec *rec, HudWidgetDef *def);
void helm_262244(grs_stencil *s, ulong size);
void helm_262278(void);                                              /* 0x262278 (G7b) */
uchar helm_2623E4(TnEvent *ev, void *data);
uchar helm_2625E4(TnEvent *ev, HudWidget *first, HudWidget *last);
void helm_2626B8(void);
void helm_2627F8(long ent, long b);              /* 0x2627F8 G7b */
void helm_262814(void);
void helm_262A44(void);
void helm_262A88(long ent);                                     /* 0x262A88 (G7b) */
void helm_262AC4(long ent, long arg);
uchar helm_262B00(short ent);
void helm_262C2C(void);                 /* (no callers) */
void helm_262D2C(void);                 /* (no callers) */
void helm_262D4C(short ent);
void helm_262DB8(void);
long helm_262E34(void);
void helm_262E88(void);
void helm_262EA0(void);
long helm_2633A0(long mode);
bool helm_263410(fix x, fix y);                     /* 0x263410 G7b */
void helm_2634A4(void);                 /* (no callers) */
void helm_2634B8(long part, long step);
long helm_26350C(short slot, long part);
long helm_26355C(short slot);
void helm_2635C0(HudWidget *w);
void helm_2635C8(short slot, LGRect *r);
void helm_2637DC(HudWidget *w);
void helm_263824(void);
void helm_263940(long idx, LGPoint *pt);
long helm_263BC0(long mode);
void helm_263BE4(void);                                              /* 0x263BE4 (G7b) */
void helm_263EB8(HudWidget *w, HudEvent *ev);
void helm_263EE8(HudWidget *w);
void helm_263F00(HudWidget *w);
void helm_263FC0(HudWidget *w, HudEvent *ev);
void helm_263FF0(HudWidget *w);
void helm_264008(HudWidget *w);
void helm_2640D4(void);

/* ------------------------------------------------------------------ M018 hudradar.c */
void radar_265700(void);
void radar_265720(void);
void radar_265724(HudWidget *w, TnEvent *ev);
void radar_2657A8(HudWidget *w);
void radar_2657F4(HudWidget *w);
void radar_266248(long x);
short radar_266254(short x);

/* ------------------------------------------------------------------ M037 hudmenu.c */
void menudraw_27B918(LGPoint pt);
void menudraw_27BB30(void);
void menudraw_27BB50(void);
void menudraw_27BB64(char *s, char hot, long row, uchar mode);
void menudraw_27BE1C(void);
void menudraw_27BF40(void);
uchar menudraw_27BFBC(TnEvent *ev);

/* ------------------------------------------------------------------ callees of other batches
 * shell_2C292C (G10b): g10_b.h; PlayKeyHandler (G9b): g9_b.h (moved there at the G9/G10 merge) */


#endif
