/* g7_c.h -- batch G7, worker g7c: HUD widget callbacks (M017 hudcb.c), HUD view state (M021
 * hudvstat.c), view damage display (M033 / M034 dmgdisp.c), HUD hint helpers (M036 hudhint.c).
 * Everything here was read from __FF.EXE v1.09.
 *
 * HUD widgets ("gadgets", 0x62 bytes, made by the helmet code M016 / G7b) sit in a pool at
 * 0x389F38; hud_widgets[id] (0x389E20) points at them by id, and a few "current" pointers
 * (0x389DDC ...) at the ones of the active HUD layout.  The helmet task calls a widget's
 * event(w, ev), update(w) and draw(w) (canvas = the widget's own canvas for draw).
 *
 * The three HUD views (0 = left, 1 = middle / map, 2 = right) are 0x20-byte records at 0x38FC10
 * (they overlap the end of game_camera; g3c reads the mode byte as game_38FC12).  Each shows one
 * of 32 modes; mode_mask has a bit per mode available in that view; per mode a table of four
 * functions (activate(w, on), event(w, ev), update(view), draw(view)) and a 5-byte info record.
 */
#ifndef TN_G7_C_H
#define TN_G7_C_H
#include "game.h"
#include "2d.h"
#include "res.h"
#include "g1_a.h"     /* RndRange, RndStream, SndPlay, snd_rnd */
#include "g1_c.h"     /* game_player_slot, fileutil_2CD6CC, strutil_2DBCF8 */
#include "g2_b.h"     /* weapon_ready, game_rnd_35B704 */
#include "g3_c.h"     /* damage_27DF14, pbasim_28AB74, obj55_288EC0, g2_c.h (fire_*, game_396868) */
#include "sup_c.h"    /* tnui_back_canvas, MouseSetCursor */
#include "t2_a.h"     /* GfxDrawText, GfxTlucRect */

#pragma pack(push, 1)

/* A HUD widget (0x62 bytes; pool at 0x389F38). */
typedef struct HudWidget {
    uchar f00[8];               /* +00 */
    short x0, y0, x1, y1;       /* +08 screen rectangle */
    void *f10;                  /* +10 */
    void *event;                /* +14 (w, HudEvent *) Watcom */
    void *update;               /* +18 (w) */
    void *draw;                 /* +1C (w), canvas set to the widget's */
    uchar flags;                /* +20 0x20 redraw, 0x80 ... */
    uchar id;                   /* +21 (view widgets: the view number) */
    grs_canvas canvas;          /* +22 */
} HudWidget;
LG_STATIC_ASSERT(sizeof(HudWidget) == 0x62, HudWidget_size);

typedef struct {
    long type;                  /* 1 press, 2 release, 4 / 0x10 / 0x20 other buttons / repeats */
} HudEvent;

/* One HUD view (0x20 bytes, hud_views 0x38FC10). */
typedef struct {
    uchar index;                /* +00 */
    uchar flags;                /* +01 1 mode switch pending, 2 locked, 4 f10 is a camera,
                                       0x10 keep the cursor, 0x40; bits 1-4 from hud_mode_info */
    uchar mode;                 /* +02 current mode (0 = none) */
    uchar next_mode;            /* +03 mode to switch to at switch_time */
    ulong switch_time;          /* +04 game_time */
    ulong mode_mask;            /* +08 bit per available mode */
    ulong timer;                /* +0C game_time until which the view shows static */
    uchar *f10;                 /* +10 mode data (view 1: map display object) */
    uchar f14[0x0C];
} HudView;
LG_STATIC_ASSERT(sizeof(HudView) == 0x20, HudView_size);

/* Functions of one view mode (hud_mode_fns 0x35AEF4, 16 bytes per mode), all Watcom. */
typedef struct {
    void *activate;             /* (HudWidget *, bool on) */
    void *event;                /* (HudWidget *, HudEvent *) */
    void *update;               /* (long view) */
    void *draw;                 /* (long view) */
} HudModeFns;

/* Static-noise lines of the damage display (dmg_lines 0x391FE0, 8 bytes). */
typedef struct {
    ushort n;                   /* +0 lines left (0 = free) */
    ushort y;                   /* +2 first row */
    ushort base;                /* +4 colour base (0x41 = no offset) */
    ushort f6;
} DmgLine;

#pragma pack(pop)

/* ------------------------------------------------------------------ globals (orig data rows) */
extern uchar hud_save_valid;            /* 0x35A9A8 save-under bitmaps allocated */
extern uchar hud_btn_mode[];            /* 0x35A9A9 view mode of each view button kind */
extern uchar hud_view_first_w[3];       /* 0x35A9B3 first widget id of each view's buttons */
extern uchar hud_view_last_w[3];        /* 0x35A9B6 last one */
extern uchar hud_35A9B9;                /* 0x35A9B9 toggle (widget 0x1F) */
extern Ref hud_weapon_icon[];           /* 0x35A9BC icon per weapon type */
extern short hud_map_zoom;              /* 0x35AEEE */
extern uchar hud_35AEF0;                /* 0x35AEF0 bit 0: views not drawn in the helmet */
extern HudModeFns hud_mode_fns[28];     /* 0x35AEF4 modes 0-27 (the info table follows) */
extern uchar hud_mode_info[32][5];      /* 0x35B0B8 +0 flags (8 shared, 0x20 needs aircraft) */
extern uchar hud_last_damage;           /* 0x35B140 */
extern uchar hud_dmg_mode[];            /* 0x35B141 mode a view shows when damaged (per kind) */
extern RndStream hud_rnd;               /* 0x35B144 */
extern uchar dmg_noise_chance[];        /* 0x35B4A0 per damage / 8 */
extern RndStream dmg_rnd;               /* 0x35B4B0 */
extern long dmg_run_a[8], dmg_run_b[8]; /* 0x35B4BC / 0x35B4DC noise run lengths */
extern long dmg_run_c[8], dmg_run_d[8]; /* 0x35B4FC / 0x35B51C gap lengths */
extern ushort hud_389BC2;               /* 0x389BC2 */
extern short hud_bar_rects[2][4];       /* 0x389DB8 gauge rectangle x0,y0,x1,y1 (2 layouts) */
extern uchar *hud_tluc_389DC8;          /* 0x389DC8 translucency tables */
extern uchar *hud_tluc_389DD0;          /* 0x389DD0 */
extern HudWidget *hud_w_389DDC;         /* 0x389DDC current widgets of the HUD layout */
extern HudWidget *hud_w_389DE0;         /* 0x389DE0 */
extern HudWidget *hud_drag_widget;      /* 0x389DE4 widget holding the mouse */
extern HudWidget *hud_w_389DF0;         /* 0x389DF0 */
/* 0x389DF8 HudWidget *[3] view widgets: g1b's game_389DF8 (g1_b.h, uchar *) */
static inline HudWidget **hud_view_ws(void)  /* the array behind the scalar extern (no UB) */
{
    HudWidget **p = (HudWidget **)&game_389DF8;
    __asm__("" : "+r"(p));
    return p;
}
#define HUD_VIEW_W(v) (hud_view_ws()[v])
extern HudWidget *hud_w_389E04;         /* 0x389E04 */
extern HudWidget *hud_w_389E08;         /* 0x389E08 */
extern HudWidget *hud_w_389E10;         /* 0x389E10 */
extern HudWidget *hud_widgets[];        /* 0x389E20 by widget id */
extern HudWidget *hud_w_389F34;         /* 0x389F34 */
extern uchar hud_38B9AD;                /* 0x38B9AD */
extern grs_bitmap hud_save_bm[8];       /* 0x38B9B0 save-unders of widgets 0x17,7,0x1D,0x1C,0x1B,0x1A,8,0x16 */
extern ulong hud_38BA30;                /* 0x38BA30 game_time */
extern fix hud_38BA34;                  /* 0x38BA34 bar pixels per meter unit */
extern ulong hud_38BA38;                /* 0x38BA38 game_time */
extern ulong hud_blink_time;            /* 0x38BA3C tn_ticks of the next blink */
extern short hud_38BA40;                /* 0x38BA40 */
extern short hud_38BA42;                /* 0x38BA42 */
extern short hud_38BA44;                /* 0x38BA44 */
extern ushort hud_38BA46;               /* 0x38BA46 game seconds shown */
extern short hud_38BA48;                /* 0x38BA48 */
extern short hud_38BA4A;                /* 0x38BA4A */
extern short hud_38BA4C;                /* 0x38BA4C */
extern uchar hud_38BA4E;                /* 0x38BA4E */
extern uchar hud_blink;                 /* 0x38BA4F */
extern uchar hud_38BA50;                /* 0x38BA50 */
extern uchar hud_38BA51;                /* 0x38BA51 */
extern long hud_shake;                  /* 0x38FA74 extra random view shake */
extern HudView hud_views[3];            /* 0x38FC10 */
extern ulong hud_view_38FC70[3];        /* 0x38FC70 */
extern uchar hud_view_saved_mode[3];    /* 0x38FC7C */
extern uchar hud_38FC80;                /* 0x38FC80 */
extern uchar hud_38FD03;                /* 0x38FD03 */
extern short hud_38FFFC;                /* 0x38FFFC */
extern short dmg_shake_y[3];            /* 0x391FD0 per view */
extern short dmg_shake_x[3];            /* 0x391FD6 */
extern uchar dmg_shake_lvl[3];          /* 0x391FDC damage the shake was made for */
extern DmgLine dmg_lines[8];            /* 0x391FE0 */
extern uchar dmg_noise[0x100];          /* 0x392020 */
extern long dmg_noise_pos;              /* 0x392120 */
extern long hud_hint_mode;              /* 0x3923E0 */
extern uchar hud_hints[0x26];           /* 0x3923E4 per hint text (0x1780000 + i) shown */
extern long hud_397B6C;                 /* 0x397B6C */

/* ------------------------------------------------------------------ M017 hudcb.c */
void hudcb_2641C0(void);
void hudcb_2642C8(void *p);
void hudcb_2642D4(void);
void hudcb_264364(grs_bitmap *bm, long id);
void hudcb_2643F0(HudWidget *w, uchar from, uchar to);
void hudcb_26444C(long view);
void hudcb_264494(void);
void hudcb_264498(HudWidget *w, HudEvent *ev);
void hudcb_2644AC(HudWidget *w);
void hudcb_2644E4(HudWidget *w, HudEvent *ev);
void hudcb_26451C(void);
void hudcb_264520(HudWidget *w);
void hudcb_264560(void);
void hudcb_264598(HudWidget *w, HudEvent *ev);
void hudcb_2645F0(HudWidget *w);
void hudcb_264630(void);
void hudcb_264644(HudWidget *w);
void hudcb_264680(uchar on);
void hudcb_2646D8(uchar on);
void hudcb_2646F0(HudWidget *w, HudEvent *ev);
void hudcb_264738(HudWidget *w);
void hudcb_26477C(HudWidget *w);
void hudcb_2647D0(HudWidget *w, HudEvent *ev);
void hudcb_2647E8(HudWidget *w);
void hudcb_264840(HudWidget *w, HudEvent *ev);
void hudcb_26485C(uchar snd);
void hudcb_2648E4(HudWidget *w);
void hudcb_26493C(uchar v);
void hudcb_264948(HudWidget *w);
void hudcb_26494C(void);
void hudcb_264950(HudWidget *w, HudEvent *ev);
void hudcb_264990(HudWidget *w);
void hudcb_264A68(HudWidget *w, HudEvent *ev);
void hudcb_264A84(uchar out);
void hudcb_264AC4(HudWidget *w);
void hudcb_264AEC(HudWidget *w);
void hudcb_264B50(HudWidget *w, HudEvent *ev);
void hudcb_264B7C(void);
void hudcb_264B98(HudWidget *w, HudEvent *ev);
void hudcb_264C38(void);
void hudcb_264C5C(HudWidget *w);
void hudcb_264CA4(void);
void hudcb_264CDC(HudWidget *w, HudEvent *ev);
void hudcb_264D68(void);
void hudcb_264D84(HudWidget *w);
void hudcb_264DCC(void);
void hudcb_264DE8(HudWidget *w, HudEvent *ev);
void hudcb_264E70(void);
void hudcb_265028(HudWidget *w, HudEvent *ev);
void hudcb_265080(void);
void hudcb_265094(HudWidget *w);
void hudcb_265158(void);
void hudcb_26516C(HudWidget *w);
void hudcb_2651C8(ushort secs);
void hudcb_2651EC(HudWidget *w);
void hudcb_26521C(HudWidget *w);
void hudcb_265250(void);
void hudcb_265290(void);
void hudcb_2652A0(HudWidget *w);
void hudcb_2652F4(HudWidget *w);
void hudcb_265398(void);
void hudcb_2653D8(HudWidget *w);
void hudcb_26541C(HudWidget *w);
void hudcb_2654D0(HudWidget *w);
void hudcb_2654E0(HudWidget *w);
void hudcb_2654F0(HudWidget *w, HudEvent *ev);
void hudcb_265560(HudWidget *w, HudEvent *ev);
void hudcb_2655CC(HudWidget *w);
void hudcb_265664(HudWidget *w);

/* ------------------------------------------------------------------ M021 hudvstat.c */
uchar hudview_26BBA0(long view);
void hudview_26BBF4(void);
void hudview_26BC74(void);
void hudview_26BCA0(HudWidget *w, HudEvent *ev);
void hudview_26BD00(HudWidget *w, HudEvent *ev);
void hudview_26BD44(HudWidget *w);
void hudview_26BE64(HudWidget *w);
void hudview_26BF3C(void);
void hudview_26BFC4(uchar kind, uchar view, uchar on);
void hudview_26C224(long a, long b);                 /* (view, mode); as g2_c.h */
void hudview_26C2C8(long view, long mode, long set);
void hudview_26C2F0(long view, long mode, long delay);
void hudview_26C318(long view, long mode);           /* as g3_c.h */
uchar hudview_26C360(long view, long dir, uchar force);
long hudview_26C410(long mode);
void hudview_26C44C(uchar on);
void hudview_26C4A4(void);
void hudview_26C4A8(void);
void hudview_26C50C(long view);
void hudview_26C598(long a);                         /* as g2_c.h */
void hudview_26C5B4(long a);                         /* as g2_c.h */
void hudview_26C5F4(long a);                         /* as g2_c.h / g2_b2.h */
void hudview_26C600(long view);
void hudview_26C60C(long code);
Ref hudview_26C72C(long code);
void hudview_26C820(void);
void hudview_26C848(uchar on);
uchar hudview_26C898(void);
void hudview_26C8B4(void);
void hudview_26C980(long mode);
uchar hudview_26CAAC(long view);
uchar hudview_26CAC8(long view);
uchar hudview_26CAD8(long view);
uchar *hudview_26CAE8(void);
void hudview_26CAF0(uchar v);
void hudview_26CB14(void);

/* ------------------------------------------------------------------ M033 / M034 dmgdisp.c */
uchar dmgdisp_27A0D0(void);
long dmgdisp_27A118(long view);
void dmgdisp_27A2BC(long view, short *dx, short *dy);
void dmgdisp_27A3FC(void *view, uchar *cam);
uchar *dmgdisp_27A4E8(uchar *d, uchar *s, long n);
void dmgdisp_27A500(long x0, long y0, long x1, long y1, long dx, long dy);
void dmgdisp_27A618(void);
void dmgdisp_27A8C0(long lo, long hi);
void dmgdisp_27A908(void);
void dmgdisp_27A924(uchar *d, long n, uchar add);
void dmgdisp_27A970(uchar *d, long n, uchar add, long kind);
void dmgdisp_27AA80(void);
void dmgdisp_27AAC0(long amount);
void dmgdisp_27AB40(void);

/* ------------------------------------------------------------------ M036 hudhint.c */
void hudutil_27B560(ushort id, uchar v);
void hudutil_27B570(ushort id);
void hudutil_27B584(void);
void hudutil_27B5B0(Ref ref, short x, short y);
void hudutil_27B5E4(Ref id);
void hudutil_27B7F4(long which, uchar v);
void hudutil_27B8EC(long which);
void hudutil_27B8FC(long which);

/* ------------------------------------------------------------------ callees of other batches (orig) */

#endif
