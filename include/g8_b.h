/* g8_b.h -- batch G8, worker g8b: M019 MFD / comm data (0x266320-0x267B4F,
 * src/game/hud/mfddata.c), M024 aircraft MFD page (0x271C60-0x271D5F, mfdairpg.c), M025 weapon
 * MFD page (0x271D60-0x27219F, mfdwpnpg.c) and M028 3D HUD views (0x272980-0x2759EF,
 * hview3d.c).  Read from __FF.EXE v1.09.
 *
 * M019 has two halves:
 *  - the squad member list (g1b's GameSpeaker records, 4 x 0x32 bytes at game_391E50, count
 *    game_391E4C; record 0 is the player) and the AI messages the MFD sends to squad members;
 *  - the suit-damage figure of the helmet HUD: up to 70 parts (SuitPart) in 5 groups of 15
 *    (0 / 1 outside the helmet, 2 / 3 inside it, 4 animated "destroyed" overlays), each a
 *    sub-bitmap of tnui_back_canvas with its own bitmap; damage spreads by random rolls and
 *    link tables from resource 0x307.
 * Member record fields used here (see g8_a.h too): +0 short active (1, 2 = gone), +2 short
 * entity, +4 short PBA slot, +6 voice, +7 radio channel, +8 byte, +9..+0xC the four weapon
 * types, +0xD byte, +0x2C long -1, +0x31 name colour.
 *
 * Globals are bound to their original addresses through symbols-g8-b.csv.
 */
#ifndef TN_G8_B_H
#define TN_G8_B_H
#include "game.h"
#include "2d.h"
#include "res.h"
#include "support.h"
#include "g8_a.h"      /* g7_b.h / g7_a.h / g7_c.h: HudWidget, HudView, HudEvent, hud_*, hudview_*;
                          mfd_3599D4, mfd_38FC81, mfd_38FD10 */
#include "g4_a.h"      /* ailow_24B24C, mfddata_266A4C */
#include "g4_c1.h"     /* aiacc_* (g4_c2.h too) */
#include "g1_a.h"      /* RndRange */
#include "g1_c.h"      /* GfxDrawText, strutil_2DBCE0 / 2DBCF8, game_player_slot */
#include "t1_c.h"      /* Msg, MsgAlloc, MsgSend, lg_memset, gr_font_string_width */
#include "t2_b.h"      /* anim_2E68A0, anim_2E6A34, AnimGetSize */

#pragma pack(push, 1)

/* One part of the suit-damage figure (0x6F bytes, pool mfd_38BE68[70]). */
typedef struct {
    uchar f00[8];
    short *rect;            /* +08 x0, y0, x1, y1 in the resource (moved by -dx / -dy) */
    void *draw;             /* +0C Watcom (part): draws it (mfddata_2679D0 ..) */
    void *draw2;            /* +10 Watcom (part) */
    uchar flags;            /* +14 0x80 to blit, 0x20 changed */
    long idx;               /* +15 bitmap index in its resource */
    uchar kind;             /* +19 4: no canvas */
    void *anim;             /* +1A ActAnim of a group-4 part */
    uchar f1e[0x11];
    grs_canvas canvas;      /* +2F sub-bitmap of tnui_back_canvas */
} SuitPart;
LG_STATIC_ASSERT(sizeof(SuitPart) == 0x6F, SuitPart_size);

#pragma pack(pop)

/* ------------------------------------------------------------------ globals (orig data rows) */
extern uchar mfd_35AAC0;                /* 0x35AAC0 flag byte of the MFD's AI messages */
extern uchar mfd_35AAC4[6];             /* 0x35AAC4 parts per group (0x2FF..0x304 resources) */
extern uchar mfd_35AACB, mfd_35AACC;    /* 0x35AACB / 0x35AACC dx, dy of the part rectangles */
extern uchar mfd_35AAD0[][11];          /* 0x35AAD0 part defs, 15 per group: +0 draw, +4 draw2,
                                           +8 flags, +9 bitmap index, +0xA kind */
extern ushort mfd_35ADD2, mfd_35ADD4;   /* 0x35ADD2 / 0x35ADD4 lowest suit values seen */
extern uchar mfd_35ADDC;                /* 0x35ADDC destroyed-part animation running */
extern uchar mfd_35B3ED[];              /* 0x35B3ED name colour per member index */
extern uchar mfd_38BB00[5][0x78];       /* 0x38BB00 damage links, 8 bytes per part: +0 level,
                                           +1 chance %, +3 linked part (1-based), +4 group-4
                                           overlay (1-based), +6 / +7 dependencies */
extern SuitPart *mfd_38BCE0[70];        /* 0x38BCE0 parts, 15 per group */
extern SuitPart *mfd_38BDF8[10];        /* 0x38BDF8 last part of groups 4..0, then first of 4..0 */
#define SP_LAST(g) (mfd_38BDF8[4 - (g)])
#define SP_FIRST(g) (mfd_38BDF8[9 - (g)])
extern uchar mfd_38BE20[0x46];          /* 0x38BE20 part state: 1 rolled, 2 damaged */
extern SuitPart mfd_38BE68[70];         /* 0x38BE68 */
extern uchar mfd_390349[][0x1EE];       /* 0x390349 per soldier: +0 flag */

/* ------------------------------------------------------------------ M019 mfddata.c */
void mfddata_266320(void);
void mfddata_266340(void);                                           /* 0x266340 (G8b) */
void mfddata_266390(long idx, long voice, long b, uchar *types, long d);  /* ret 4 */
void mfddata_26645C(long ent);                                       /* 0x26645C */
void mfddata_2664AC(long ent);                                       /* 0x2664AC */
uchar *mfddata_2665B4(long ent);                                     /* 0x2665B4 MFD record of ent */
uchar mfddata_2665E8(long ent);                                      /* 0x2665E8 (copy kept in g2_b2.h until the G9 merge) */
void mfddata_266618(long ent, long a, long b);
void mfddata_266680(long ent, long a, long b);
void mfddata_2666E4(long ent, long a, long b);
void mfddata_266748(long brain, long squad);
void mfddata_2667AC(long brain, long what);
void mfddata_266860(short *ord, long b);
void mfddata_266A4C(void);                                      /* 0x266A4C (G8b) */
void mfddata_266AF4(long ent, long b, long member);
void mfddata_266C64(void);
void mfddata_266C90(void);                                           /* 0x266C90 (G8b) */
void mfddata_266E84(void);                                           /* 0x266E84 (G8b) */
void mfddata_266EAC(void);                                           /* 0x266EAC (G8b) */
void mfddata_2670B8(void);
void mfddata_267158(SuitPart *first, SuitPart *last);
void mfddata_267198(void);                                           /* 0x267198 (G8b) */
void mfddata_267224(SuitPart *first, SuitPart *last, long g, long level);
void mfddata_26747C(void);
void mfddata_2675B4(void);
void mfddata_267690(SuitPart *p, uchar *def, short *rect);
void mfddata_26773C(void);                                           /* 0x26773C (G8b) */
void mfddata_26776C(SuitPart *p);
void mfddata_2677B4(uchar *anim, long flags, long c);
void mfddata_267824(SuitPart *p);
void mfddata_267840(SuitPart *first, SuitPart *last);
void mfddata_267870(void);                                           /* 0x267870 (G8b) */
void mfddata_2679CC(void);              /* a bare ret (267870 falls into it) */
void mfddata_2679D0(SuitPart *p);
void mfddata_267A30(SuitPart *p);
void mfddata_267A90(SuitPart *p);
void mfddata_267AF0(SuitPart *p);

/* callees without a header */

/* ------------------------------------------------------------------ M024 mfdairpg.c */
void mfdpage_271C60(HudWidget *w, HudEvent *ev);
void mfdpage_271C94(long view);

/* ------------------------------------------------------------------ M025 mfdwpnpg.c */
extern Ref mfd_35B278[];                /* 0x35B278 weapon picture per weapon type (high word) */
void mfdpage_271D60(void);                                           /* 0x271D60 (G8b) */
void mfdpage_271DAC(HudWidget *w, HudEvent *ev);
void mfdpage_271E08(long view);
void mfdpage_271E14(long view);
void mfdpage_272100(Ref ref, long num, long den, long x, long y);   /* ret 4 */

/* ------------------------------------------------------------------ M028 hview3d.c */
#pragma pack(push, 1)
/* One button of a view's bottom strip (8 bytes, lists end with ref 0). */
typedef struct {
    Ref ref;                /* +0 text */
    uchar x0, x1;           /* +4 x range */
    short tx;               /* +6 text x */
} HvButton;
LG_STATIC_ASSERT(sizeof(HvButton) == 8, HvButton_size);

/* One map waypoint (0x16 bytes, hv_38FE90[16]). */
typedef struct {
    fix x, y, z;            /* +0 */
    uchar pad[6];
    long region;            /* +0x12 misobj_2C9E60 marker, 0xFF = free */
} HvWaypoint;
LG_STATIC_ASSERT(sizeof(HvWaypoint) == 0x16, HvWaypoint_size);
#pragma pack(pop)

extern char hv_3558E0[];                /* 0x3558E0 "r:\prj\tn\v109\tn\src\tn\hview3d.c" */
extern short hv_35AEEC;                 /* 0x35AEEC squad map zoom */
extern Ref hv_35B2C0[4];                /* 0x35B2C0 target map button rectangles */
extern HvButton hv_35B2D0[];            /* 0x35B2D0 aircraft view buttons */
extern HvButton hv_35B2F0[];            /* 0x35B2F0 aircraft view buttons (returning) */
extern HvButton hv_35B310[];            /* 0x35B310 vehicle view buttons */
extern HvButton hv_35B320[];            /* 0x35B320 aircraft view buttons (with a target) */
extern ushort hv_389BC6;                /* 0x389BC6 squad picked on the map */
extern HvWaypoint hv_38FE90[16];        /* 0x38FE90 */
extern short hv_38FFF8, hv_38FFFA;      /* 0x38FFF8 / 0x38FFFA zoom button position */
extern LGPoint hv_38FFFE, hv_390002, hv_390006;   /* drag start points of the three maps */
extern Ref hv_390240;                   /* 0x390240 view 0x15 caption */
extern uchar hv_390244, hv_390245, hv_390246;     /* its x, y, colour */
extern uchar hv_390247[3];              /* 0x390247 per view weather / pipe byte */
extern uchar hv_428B83;                 /* 0x428B83 camera zoom */

/* callees without a header */

void hview3d_272980(HudWidget *w, uchar on);
void hview3d_272A48(HudWidget *w, HudEvent *ev);
void hview3d_272CFC(long view);
void hview3d_272DBC(HudWidget *w, uchar on);
void hview3d_272E84(long view);
void hview3d_272F10(HudWidget *w, uchar on);
void hview3d_273020(HudWidget *w, HudEvent *ev);
void hview3d_273500(long view);
short hview3d_2739F4(HudWidget *w, fix dist);
short hview3d_273A5C(HudWidget *w, fix dist);
void hview3d_273AC4(HudWidget *w);                                   /* 0x273AC4 (G8b) */
void hview3d_273ADC(HudWidget *w);                                   /* 0x273ADC (G8b) */
void hview3d_273CFC(uchar *map, LGPoint pt);                         /* 0x273CFC (G8b) */
void hview3d_273D40(uchar *map, LGPoint pt);                         /* 0x273D40 (G8b) */
uchar hview3d_274B00(void *cam, long pane, LGPoint pt, uchar c);     /* 0x274B00 (G8b) */
void hview3d_273B04(HudWidget *w, HudEvent *ev);
void hview3d_273CAC(long view);
void hview3d_273E08(HudWidget *w, HudEvent *ev);
void hview3d_2740C8(HudWidget *w);
void hview3d_27414C(void);
void hview3d_274184(uchar *map);
void hview3d_27421C(long brain);
void hview3d_27434C(void);
void hview3d_274394(void);
void hview3d_2743DC(void);
void hview3d_274410(void);                                           /* 0x274410 (G8b) */
void hview3d_2744D0(void);                                           /* 0x2744D0 (G8b) */
void hview3d_274510(long a);                     /* 0x274510 */
void hview3d_274538(long view, long ent, long kind);
void hview3d_274570(HudWidget *w, long pt, long cam);
void hview3d_274670(HudWidget *w, HudEvent *ev);
void hview3d_274C30(long view);
void hview3d_274DA4(long view);
fixang hview3d_2750CC(long ent);
void hview3d_275260(HudWidget *w);
void hview3d_2754FC(HudWidget *w, long pt);
void hview3d_275654(HudWidget *w, HvButton *list);
void hview3d_2757A0(long view, Ref ref, long x, long y, long color);   /* ret 4 */
void hview3d_2757C4(uchar *p);
long hview3d_2757F8(long x, HvButton *list);
void hview3d_275830(HudWidget *w);
void hview3d_275988(HudWidget *w);


#endif
