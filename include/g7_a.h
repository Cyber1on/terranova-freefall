/* g7_a.h -- batch G7, worker g7a: M020 hud main (0x267B50-0x26BB9F, src/game/hud/hudmain.c).
 * Read from __FF.EXE v1.09.
 *
 * The HUD main module owns the first-person view: the eye position (hud_ffview, from the player,
 * another entity or the ragdoll camera), the game camera (game_camera, an FfCamera) set up for
 * the main HUD pane, the HUD text messages (a priority list of HudMsg nodes made from
 * HudMsgTmpl templates: radio text, acknowledgements, the target callback), the target
 * brackets and labels, the mission banner, view shake, helmet on/off, and the overhead map
 * (the map display object of M030 drawn into the main pane, with a draggable layer legend).
 *
 * Names: the map's placeholders are kept for functions other modules call (hud_2xxxxx,
 * HudInit, HudGetCompatibleView, HudViewReset); internal and dead functions got names after
 * what they do.  Prototypes already declared by earlier batches are not repeated here: g1_b.h
 * (hud_2688A4, hud_268BD4), g1_c.h (hud_268E98), g2_c.h (hud_2683F8, hud_26843C, hud_26AE98,
 * hud_26AF24, hud_26B970), g3_c.h (hud_268FD0, hud_26BA18), g5_c.h (hud_26A11C), g7_c.h
 * (hud_268098, hud_26AFB0, hud_26B800, HudGetCompatibleView).
 * The HUD panes are g7c's HudWidget (g7_c.h); g7c's hud_drag_widget (0x389DE4) is the widget
 * holding the mouse, hud_widgets[26] / [30] / [43] are 0x389E88 / 0x389E98 / 0x389ECC.
 * Globals are bound to their original addresses through symbols-g7-a.csv.  Structs are
 * byte-packed.
 */
#ifndef TN_G7_A_H
#define TN_G7_A_H
#include "game.h"
#include "2d.h"
#include "res.h"
#include "support.h"
#include "edms.h"
#include "g1_b.h"      /* hud_2688A4 / 268BD4, FfCameraSetup, CameraInitZoom, cam_view_tab,
                          game_438C20, radio_msg_*, ff_options, radio_msg_play */
#include "g2_a.h"      /* helm_262D4C, targ_list, ord_table, OrdType */
#include "g2_b2.h"     /* objdmg_status_str, game_441367, game_396E14 / 396E18 */
#include "g2_c.h"      /* hud_2683F8 / 26843C / 26AE98 / 26AF24 / 26B970, ordn_mission_state,
                          game_35ADE0, game_4413D0, game_camera_38FB20 */
#include "g3_a.h"      /* damage_27E1E0 / 27C880, CursCtrl, cursctrl_*, vec3_*, ragdoll_cam,
                          game_cls0 */
#include "g3_b.h"      /* biped_27EC90 */
#include "g3_c.h"      /* hud_268FD0 / 26BA18, damage_27D458 / 27DF14, pbasim_*, PbaPlayerMove,
                          game_42015A, game_35CADA */
#include "g5_a.h"      /* hz_playing, hz_play_cam, haze_2B5B70 */
#include "g5_b.h"      /* FfView, FfCamera, SkyRec, ff_cam, ffpipe_*, rndsky_*, sky_ref_w,
                          ffpipe_41EB60, game_35A540 */
#include "g5_c.h"      /* hud_26A11C, drawobj_29CCE0, planet_* */
#include "g6_a.h"      /* drawobj_29CBA4, drawobj_29DC20 */
#include "g6_b.h"      /* weather_*, terr_tex_bm */
#include "g7_c.h"      /* HudWidget, hud_268098 / 26AFB0 / 26B800, HudGetCompatibleView,
                          hud_shake, hud_drag_widget, hud_widgets, hud_389BC2, hud_38FC80,
                          dmgdisp_*, hudutil_*, hudview_* */
#include "t1_a.h"      /* ScriptWake, ScriptFind */
#include "t1_b.h"      /* strcmp */
#include "t1_c.h"      /* itoa, strpool_dup / free, gr_font_string_width / height */
#include "t2_c.h"      /* TnEvent */

#pragma pack(push, 1)

/* A HUD message template (0x19 bytes, data at 0x35AE18 / 3C / 58 / 74). */
typedef struct {
    short id;                   /* +00 */
    short prior;                /* +02 list priority */
    ulong flags;                /* +04 HUDMSG_xxx */
    uchar color;                /* +08 */
    LGPoint pos;                /* +09 (y doubled at draw time in hires) */
    char *text;                 /* +0D copied with strpool_dup */
    void *cb;                   /* +11 Watcom cb(HudMsg *, HudMsgEv *) */
    long time;                  /* +15 ms left (0 = no timeout) */
} HudMsgTmpl;
LG_STATIC_ASSERT(sizeof(HudMsgTmpl) == 0x19, HudMsgTmpl_size);

/* A HUD message: node of hud_msgs (LlistAddQueue, node size 0x25). */
typedef struct HudMsg {
    queue q;                    /* +00 */
    short id;                   /* +0A */
    ulong flags;                /* +0C */
    uchar color;                /* +10 */
    LGPoint pos;                /* +11 */
    char *text;                 /* +15 */
    void *cb;                   /* +19 */
    long time;                  /* +1D */
    short f21;                  /* +21 */
    uchar g23[2];
} HudMsg;
LG_STATIC_ASSERT(sizeof(HudMsg) == 0x25, HudMsg_size);

#define HUDMSG_HIDDEN   0x00001     /* queued behind another one: not drawn, not ticked */
#define HUDMSG_DRAWCB   0x00002     /* drawn by cb(m, {code 2}) */
#define HUDMSG_TICKCB   0x00004     /* cb(m, {code 1}) every tick */
#define HUDMSG_KILLCB   0x00008     /* cb(m, {code 4}) when killed */
#define HUDMSG_RADIO    0x00010
#define HUDMSG_ACK20    0x00020
#define HUDMSG_ACK40    0x00040
#define HUDMSG_FONT     0x03800     /* index into hud_fonts */
#define HUDMSG_DEAD     0x08000     /* killed, freed by the next tick */
#define HUDMSG_OUTLINE  0x10000     /* drawn with a black outline (hud_268098) */

/* The event a message callback gets (12-byte local of the caller). */
typedef struct {
    ulong type;                 /* +0 always 0x1000 */
    short code;                 /* +4 1 tick, 2 draw, 4 kill */
    uchar pad[6];
} HudMsgEv;

/* What the main view looks through (0x38FBE0, 10 bytes; copied whole by hud_26BA6C,
 * filled by damage_27E1E0). */
typedef struct {
    short obj;                  /* +0 entity whose eyes we use */
    short zoom;                 /* +2 cam_view_tab index */
    short hdg;                  /* +4 heading offset (non-player views) */
    CursCtrl *ctrl;             /* +6 cursor control of the view */
} HudEye;
LG_STATIC_ASSERT(sizeof(HudEye) == 10, HudEye_size);

/* The last eye position used (0x35AEB8): small moves are snapped back to it. */
typedef struct {
    fix x, y, z;                /* +0 */
    ushort heading;             /* +C */
    ushort pitch;               /* +E */
} HudEyePos;

#pragma pack(pop)

/* ------------------------------------------------------------------ globals (orig data rows) */
extern uchar hud_name_init[0x20];       /* 0x267B50 zeros (initialiser of a name buffer) */
extern HudWidget *hud_35A548;           /* 0x35A548 widget hud_267DD4 aims through */
extern long hud_35A544;  /* a dword (helm_262EA0 writes 0 / 1); the original reads its low byte as a shift count */                /* 0x35A544 map y shift (half-height views) */
extern uchar hud_35A944;                /* 0x35A944 target lock enabled */
extern ulong hud_35A9A4;                /* 0x35A9A4 game_time + 500 at helmet changes */
extern uchar hud_banner_on;             /* 0x35ADE2 mission banner shown */
extern ulong hud_banner_until;          /* 0x35ADE4 game_time */
extern grs_bitmap hud_map_bm[2];        /* 0x35ADE8 map legend background (lo, hi res) */
extern ushort hud_fonts[8];             /* 0x35AE08 message fonts by flags bits 11-13 */
extern HudMsgTmpl hud_tmpl_target;      /* 0x35AE18 target brackets (cb hud_26A188) */
extern long hud_dbg_count;              /* 0x35AE34 */
extern HudMsgTmpl hud_tmpl_radio_log;   /* 0x35AE3C radio log replay (hud_radio_log_show) */
extern HudMsgTmpl hud_tmpl_radio;       /* 0x35AE58 radio text (hud_2688A4) */
extern HudMsgTmpl hud_tmpl_ack;         /* 0x35AE74 acknowledgement (hud_268BD4) */
extern uchar hud_cmd_35AE90[];          /* 0x35AE90 command record for cmd_261370 */
extern RndStream hud_noise_rnd;         /* 0x35AEA8 static noise */
extern ushort hud_pitch_smooth;         /* 0x35AEB4 smoothed vehicle pitch */
extern HudEyePos hud_last_eye;          /* 0x35AEB8 */
extern uchar hud_aim_center;            /* 0x35AEC8 aim at the pane centre, not the mouse */
extern short hud_last_target;           /* 0x35AECA */
extern uchar *hud_pal_tab[];            /* 0x35AECC fill tables by palette mode */
extern uchar hud_38B9AE;                /* 0x38B9AE set when the helmet comes off */
extern char hud_text_log[0x400];        /* 0x43CEC8 text of radio messages while text is off */
extern long hud_dbg_lines[10][6];       /* 0x38F860 */
extern char hud_text[0x100];            /* 0x38F950 last radio text (unwrapped) */
extern fix hud_move_a;                  /* 0x38FA50 */
extern short hud_turn_a;                /* 0x38FA54 */
extern ushort hud_dir_a;                /* 0x38FA56 */
extern uchar *hud_fill_tab[4];          /* 0x38FA58 fill (palette) tables of the 3D view */
extern fix hud_move_b;                  /* 0x38FA68 */
extern short hud_turn_b;                /* 0x38FA6C */
extern ushort hud_dir_b;                /* 0x38FA6E */
extern long hud_zoom_reset;             /* 0x38FA70 */
/* 0x38FA74 hud_shake: g7_c.h */
extern long hud_shake_max;              /* 0x38FA78 */
extern long hud_pal_debug;              /* 0x38FA7C nonzero: draw the palette chart */
extern LGPoint hud_aim_pt;              /* 0x38FA80 */
extern long hud_weather;                /* 0x38FA84 weather view index */
extern fix hud_map_x, hud_map_y;        /* 0x38FA88 / 0x38FA8C map centre */
extern LGPoint hud_map_pos;             /* 0x38FA90 map legend position */
extern ulong hud_last_time;             /* 0x38FA94 game_time of the last think */
extern HudMsg *hud_msg_target;          /* 0x38FA98 */
extern LlistHead hud_msgs;              /* 0x38FA9C */
extern FfView hud_ffview;               /* 0x38FAB8 the eye (+0xC heading is g6b's game_38FAC4,
                                           +0x5A view entity drawobj_38FB12: not used here) */
extern SkyRec *hud_sky;                 /* 0x38FB14 */
extern long hud_sky_file;               /* 0x38FB18 */
extern fix hud_eye_dz;                  /* 0x38FB1C eye height above the body position */
extern HudEye hud_eye;                  /* 0x38FBE0 */
extern short hud_msg_dead;              /* 0x38FBEA killed nodes not yet freed */
extern short hud_ack_count;             /* 0x38FBEC */
extern short hud_radio_count;           /* 0x38FBEE */
extern short hud_zoom_player;           /* 0x38FBF0 */
extern short hud_zoom_other;            /* 0x38FBF2 */
extern short hud_scr_w;                 /* 0x38FBF4 */
extern short hud_scr_h;                 /* 0x38FBF6 */
extern uchar hud_map_drag;              /* 0x38FBF9 legend being dragged */
extern uchar hud_out_of_area;           /* 0x38FBFA */
extern uchar hud_map_scale;             /* 0x38FBFB */
extern uchar hud_map_layers;            /* 0x38FBFC */
extern uchar hud_map_grab;              /* 0x38FBFD map being scrolled with the mouse */
extern uchar hud_helmet;                /* 0x38FBFE */
extern short hud_radio_wait;            /* 0x38FC00 */
extern short hud_buttons;               /* 0x38FC02 */
extern LGPoint hud_drag_off;            /* 0x38FC04 */
extern LGPoint hud_grab_pt;             /* 0x38FC08 */
extern HudWidget *hud_w_389DF4;         /* 0x389DF4 main (3D view) pane = (&game_389DF8)[-1] */
extern short hud_targ_sel[5];           /* 0x397F20 x0, y0, x1, y1, ent of the selected target */
extern short hud_441355;                /* 0x441355 view heading offset */
extern short hud_441357;                /* 0x441357 view pitch */
extern char hud_sky_name[];             /* 0x442424 */
extern uchar hud_442F1A;                /* 0x442F1A */
extern uchar hud_44244B;                /* 0x44244B weather kind + 1 */
extern long hud_44244C;                 /* 0x44244C weather scale */
extern uchar hud_map_bits[];            /* 0x38DCD0 */
extern char hud_str_check[];            /* 0x355814 legend check mark */
extern char hud_str_bad_offset[];       /* 0x355808 "BAD OFFSET" */
extern char hud_str_unknown_view[];     /* 0x355818 */
extern char hud_str_sky0s[];            /* 0x3557D8 "SKY0S.RES" */
extern char hud_str_no_sky[];           /* 0x3557E4 */

/* ------------------------------------------------------------------ M020 hudmain.c */
void HudInit(void);
void hud_267D68(void);                          /* shutdown */
void hud_267DD4(HudWidget *pane);               /* think */
HudMsg *hud_msg_add(HudMsgTmpl *t);
void hud_msg_kill(HudMsg *m);
void hud_msg_tick(long dt);
void hud_layout_pos(char idx, long *out);       /* (no callers) */
void hud_268098(long font, void *text, long x, long y, long color);  /* 0x268098 (G7a) */
void hud_msg_draw(void);
void hud_2683F8(long a);                         /* 0x2683F8 */
void hud_268414(void);
uchar hud_26843C(long a, long b);                /* 0x26843C */
long hud_banner_line(char line, char mission, LGPoint *pos, long x1, char state, ushort font,
                     long col, long col_done, long col_state);
short hud_2686EC(long mission, LGPoint p0, LGPoint p1, long font, long col, long col_done,
                 long col_state);
HudMsg *hud_radio_log_show(char *s);
void hud_2688A4(char *s);                          /* 0x2688A4 (G7a) show radio text */
long hud_2689A0(void);
void hud_radio_wake(void);                      /* (no callers) */
void hud_268A08(short a, char *s);
void hud_268A44(HudMsg *m, HudMsgEv *ev);
void hud_268B30(void);
void hud_268BD4(char *s, long t);                  /* 0x268BD4 (G7a) show ack text */
void hud_268C74(HudMsg *m, HudMsgEv *ev);
long hud_mouse_buttons(long a, long b, long buttons);  /* (no callers) */
void hud_268D80(HudWidget *pane, TnEvent *ev);
void hud_268E98(void);                                         /* 0x268E98 (G7a) */
void hud_damage_shake(void);                    /* (no callers) */
void hud_shake_add(long amt);
void hud_268FD0(long dist, long b);              /* 0x268FD0 */
void hud_269000(void);
void hud_269028(void);
void hud_noise_draw(void);
void hud_text_box(char *text, ushort font, uchar fg, uchar bg, short *r);  /* (no callers) */
void hud_cam_reset(void);
void hud_eye_update(void);
void hud_eye_player(void);
void hud_eye_from_rec(short *rec);              /* (no callers) */
void hud_draw_fps(void);                        /* (no callers) */
void hud_draw_speed(void);                      /* (no callers) */
void hud_draw_palette(void);
void hud_drive(void);                           /* (no callers) */
uchar hud_aim_centered(void);                   /* (no callers) */
void hud_aim_point(LGPoint *out);
uchar hud_aim_on_target(void);
void hud_reticle(void);
void hud_269F28(LGPoint pt);
void hud_26A11C(short *rect, long arg);                            /* 0x26A11C (G7) */
void hud_26A188(HudMsg *m, HudMsgEv *ev);
void hud_bracket(long col, short *r);
void hud_status_ticks(uchar *cols, short *r, uchar flag);
uchar hud_26A6D4(short ent, char *out);
void hud_target_label(short ent, LGRect r);     /* custom: eax ent, rect by value on the stack */
uchar hud_26AC98(uchar helmet);
void hud_helmet_set(uchar helmet);
void hud_26AE98(void);                           /* 0x26AE98 */
void hud_26AF24(void);                           /* 0x26AF24 */
void hud_26AFB0(long step);                                          /* 0x26AFB0 (G7a) */
void hud_map_mouse(HudWidget *pane, TnEvent *ev);
void hud_map_draw(void);
void hud_map_legend(void);
void hud_cam_setup(void);
void hud_26B780(long zoom);
void hud_26B800(long step);                                          /* 0x26B800 (G7a) */
uchar hud_26B8F8(uchar mode);
long HudGetCompatibleView(void);                                     /* 0x26B9D8 (G7a) */
void hud_26B970(long a);                         /* 0x26B970 */
void hud_26BA18(void);                           /* 0x26BA18 G7a */
void hud_26BA6C(HudEye *v);
uchar hud_dbg_line(long a, long b, long c, long d, long e, long f);  /* (no callers) */
void hud_26BB5C(void);
void HudViewReset(void);
void hud_26BB98(long v);

/* ------------------------------------------------------------------ callees of other batches (orig) */


#endif
