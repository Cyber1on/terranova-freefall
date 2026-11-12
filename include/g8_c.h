/* g8_c.h -- batch G8, worker g8c: M015 squad orders / command panel (0x25EF10-0x26147F,
 * src/game/hud/cmdpanel.c), M026 MFD weapon-fire page (0x2721A0-0x27274F, mfdfire.c), M027 MFD
 * eye views (0x272750-0x27297F, mfdkeys.c) and M030 tactical map display (0x2769D0-0x278CBF,
 * mapdisp.c).  Read from __FF.EXE v1.09.
 *
 * The G8c functions other batches call are declared at the end of this header (moved here
 * from g3_c.h, g7_a/b/c.h and g8_b.h at the G5-G8 merge).
 * Globals are bound to their original addresses through symbols-g8-c.csv.
 */
#ifndef TN_G8_C_H
#define TN_G8_C_H
#include "game.h"
#include "2d.h"
#include "res.h"
#include "lg.h"
#include "g8_b.h"      /* g8_a.h, g7_*.h: HUD types, hudview_*, mfd_*, hview3d_*, mfddata_* */
#include "g1_a.h"      /* RndRange */
#include "g1_b.h"      /* GameSpeaker game_391E50, game_359708 */
#include "g1_c.h"      /* GfxDrawText, fix_sincos */
#include "g2_a.h"      /* timers_*, ObjRingEntry, ord_table, targ_current */
#include "g2_b.h"      /* game_rnd_35B704, game_map_35BFB4 */
#include "g2_c.h"      /* ShapeItem, interact_2973E8, strike_entity, game_396868, game_35ADE0 */
#include "g3_a.h"      /* game_41DD58, physics_2D47D8 */
#include "g3_c.h"      /* NearList, damage_27D*, instsim_288260, mapdisp_2769E8 */
#include "g4_c2.h"     /* aihelp_2D3298 / 2D313C */
#include "g5_b.h"      /* mapann_list, MapAnnNode, ffpipe_38FFF0, game_35A540 */
#include "g5_c.h"      /* planet_res47, planet_light, planet_tab36, planet_tab41 */
#include "g6_a.h"      /* ffpipe_38FFF4 */
#include "t1_c.h"      /* gr_font_string_width */
#include "g4_a.h"      /* ai_fire_mask */
#include "g5_a.h"      /* ff_cams_390010 */
#include "sup_c.h"     /* MouseClearStencil */
#include "g9_a.h"      /* simtype_27B200 / 27B368 / 27B3C8 / 27B420 / 27B4D8, mapd_nav and
                          cmd_39238A (owner g9a since the G9/G10 merge) */

#pragma pack(push, 1)

/* ------------------------------------------------------------------ M030 tactical map */

/* One map display (0x28 bytes, pool mapd_pool[4] at 0x390260): an off-screen bitmap of the
 * terrain at a zoom level, scrolled with memmove and redrawn in strips. */
typedef struct {
    fix x, y;            /* +0x00 world position of the top-left pixel */
    short zoom;          /* +0x08 index into mapd_scales */
    fix scale;           /* +0x0A world units per pixel */
    fix inv;             /* +0x0E fix_div(1.0, scale) */
    grs_canvas *canvas;  /* +0x12 target canvas (size of the bitmap) */
    grs_bitmap bm;       /* +0x16 terrain bitmap (type 2, row = w, bits from f_malloc) */
    uchar flags;         /* +0x26 2 in use, 1 set at creation, 4 show near lists, 8 lit shading */
    uchar layers;        /* +0x27 1 names, 2 member circles, 4 weapon range, 0x18 annotations,
                            0x20 squad paths */
} MapDisp;
LG_STATIC_ASSERT(sizeof(MapDisp) == 0x28, MapDisp_size);

extern MapDisp mapd_pool[4];            /* 0x390260 */
extern fix mapd_scales[];               /* 0x35B3BC scale per zoom */
extern short mapd_ann_shift[];          /* 0x35B3DC annotation flag bit per zoom */
extern uchar mapd_show_ents;            /* 0x35B3EC draw every entity */
extern uchar mapd_jammed;               /* 0x35B3F1 near-list display blanked this frame */
extern uchar mapd_shade[64];            /* 0x35B3F2 slope -> shade */
extern uchar mapd_shade8[64];           /* 0x35B432 slope -> shade over the mission 8 overlay */
extern ushort mapd_lbl_angle[4];        /* 0x35B472 name-tag directions */
extern schar mapd_lbl_dy[4];            /* 0x35B47A */
extern schar mapd_lbl_dx[4];            /* 0x35B47E */
extern schar mapd_lbl_wsh[4];           /* 0x35B482 text width shift */
extern ulong mapd_ring_time;            /* 0x35B488 game_time of the next blast-ring step */
extern ulong mapd_lbl_time;             /* 0x39030C game_time of the next name-tag layout */
extern uchar mapd_lbl_pos[4];           /* 0x390310 name-tag direction per member */
extern ushort mapd_ann_fonts[];         /* 0x35C020 annotation fonts */
extern ushort mapd_390308;              /* 0x390308 squad whose path is not drawn */
/* mapd_nav (0x392170): g9_a.h */

void mapdisp_277424(MapDisp *m);
void mapdisp_2774C0(MapDisp *m, LGRect *r);
void mapdisp_27782C(MapDisp *m, long n);
void mapdisp_277874(MapDisp *m, long n);
void mapdisp_2778BC(MapDisp *m, long n);
void mapdisp_277930(MapDisp *m, long n);
void mapdisp_2779A0(MapDisp *m, fix x, fix y, fix *ox, fix *oy);    /* ret 4 */
fix mapdisp_277A48(MapDisp *m, fix x);
fix mapdisp_277A78(MapDisp *m, fix y);
void mapdisp_277AB0(MapDisp *m, uchar *rec);
void mapdisp_277BAC(MapDisp *m, long ent, long sys_ent, uchar vis);
void mapdisp_277F60(MapDisp *m, long ent);
void mapdisp_278114(MapDisp *m, long ent);
void mapdisp_278254(MapDisp *m, uchar *path, uchar colour);
void mapdisp_278488(MapDisp *m);
void mapdisp_278688(MapDisp *m);
void mapdisp_2788C8(uchar *pos);
void mapdisp_2789D8(MapDisp *m, LGPoint p);
void mapdisp_278A24(ObjRingEntry *e, MapDisp *m);

/* ------------------------------------------------------------------ M015 order panel */

extern uchar cmd_389BC0[0x10];          /* 0x389BC0 current command record (see cmdpanel.c;
                                           +2 is hud_389BC2, +4 hud_389BC4, +6 hv_389BC6, +8
                                           hud_389BC8: cmdpanel.c only uses this name) */
extern ulong cmd_389BD8;                /* 0x389BD8 game_time the panel closes (0 none) */
extern ushort cmd_38FC83;               /* 0x38FC83 squad of the member shown */
extern short cmd_38FC85;                /* 0x38FC85 */
extern long cmd_3599D8;                 /* 0x3599D8 view mode restored on close */
extern long cmd_3599DC;                 /* 0x3599DC strike message argument */
extern MfdMenu cmd_menu_3599E0;         /* 0x3599E0 name menu (items cmd_items_389BE0) */
extern uchar cmd_3599E9;                /* 0x3599E9 argument of the last line run */
extern ushort cmd_flagtab[10];          /* 0x3599EC 5 x {command, flags to add} */
extern MfdMenu cmd_menus[6];            /* 0x35A3C4 member menus by state; [4] 0x35A3E4 default,
                                           [5] 0x35A3EC team */
extern MfdItem cmd_items_35A44C[11];    /* 0x35A44C member shortcut lines */
extern MfdItem cmd_items_389BE0[];      /* 0x389BE0 lines built by cmd_25F33C */
extern char *cmd_389B14[];              /* 0x389B14 line texts by arg ([0x23..0x29] names) */
extern schar cmd_391F4D[];              /* 0x391F4D voice -> state record */
extern ulong cmd_25EF10;                /* 0x25EF10 (code segment) 0: default position */

void cmd_25EF14(long member, MfdMenu *menu);
void cmd_25F0C0(long member, long on);
void cmd_25F33C(long n, char **names, ushort *keys);
uchar cmd_25F3B8(long key);
void cmd_25F7E8(MfdItem *it);
ulong cmd_25F9AC(long ent);
ulong cmd_25F9E4(uchar *cmd);
short cmd_25FD80(void);
long cmd_25FFC8(long kind);
uchar cmd_26016C(long kind);
uchar cmd_26018C(void);
void cmd_2601A8(uchar *cmd);
void cmd_2603C4(uchar *cmd);
MfdMenu *cmd_260BFC(long member);
char *cmd_260D10(long arg, long member, char *out);
uchar cmd_261320(void);
uchar cmd_2613E0(long key);

/* ------------------------------------------------------------------ M026 / M027 */

extern ushort mfdp_35BF64;              /* 0x35BF64 weapon kinds (bits) that cannot fire */
extern ushort mfdp_35BF66;              /* 0x35BF66 weapon kinds (bits) with a reload bar */
extern ushort mfdp_38FE80;              /* 0x38FE80 "fired" flash countdown */

void mfdpage_2721A0(HudWidget *w, HudEvent *ev);
void mfdpage_2722B8(long view);
void mfdpage_2722E8(long view);
void mfdkey_272750(HudWidget *w, uchar on);
void mfdkey_272794(HudWidget *w, HudEvent *ev);
void mfdkey_2727EC(long view);
void mfdkey_2727F8(HudWidget *w, uchar on, uchar flag, ushort kind);
void mfdkey_27285C(HudWidget *w, uchar on);
void mfdkey_272878(HudWidget *w, uchar on);
void mfdkey_272894(HudWidget *w, uchar on);
void mfdkey_2728B0(HudWidget *w, uchar on);
void mfdkey_2728C4(HudWidget *w, uchar on);
void mfdkey_272908(HudWidget *w, uchar on);

#pragma pack(pop)

/* Declared here at the G5-G8 merge (one declaration per symbol, in its owner's header). */
void cmd_25F044(void);                                               /* 0x25F044 (G8c) */
void cmd_25F0AC(long a);                                             /* 0x25F0AC (G8c) */
void cmd_25F138(void);                                               /* 0x25F138 (G8c) */
void cmd_25F2B8(void);                                               /* 0x25F2B8 (G8c) */
void cmd_25FB30(long brain);                                /* 0x25FB30 (G8c; the argument is not read) */
void cmd_2601D8(void);                                               /* 0x2601D8 (G8c) */
void cmd_261370(uchar *cmd);                                         /* 0x261370 (G8c) */
uchar mapdisp_2769E8(long ent);                  /* 0x2769E8 */
uchar *mapdisp_276ACC(grs_canvas *c, fix x, fix y, long scale);      /* 0x276ACC (G8c) new map */
void mapdisp_276BB4(uchar *map);                                     /* 0x276BB4 (G8c) free a map */
void mapdisp_276BC8(uchar *map, long zoom);                          /* 0x276BC8 (G8c) */
void mapdisp_276C4C(uchar *map, fix x, fix y);                       /* 0x276C4C (G8c) */
void mapdisp_276C6C(uchar *map, fix x, fix y);              /* 0x276C6C (G8c) */
void mapdisp_276CDC(uchar *map, LGPoint delta);                      /* 0x276CDC (G8c) scroll */
void mapdisp_276EF8(uchar *map, long b);                             /* 0x276EF8 (G8c) draw */

#endif
