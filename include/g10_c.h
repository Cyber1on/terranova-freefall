/* g10_c.h -- batch G10, worker g10c: the end of M097 (0x2C3440-0x2C3510: the shell's ambient
 * sound and squad restore), M105 (0x2CA030-0x2CCD30: the options screen and the in-mission
 * help screen) and M111 (0x2CEE20-0x2D2BE0: the custom scenario builder, rescustm.res, and
 * the random scenario generator run when a custom mission starts).
 *
 * Every G10c function is declared here (options_2CAAC0, options_2CA1E4 / 2CA2C0 / 2CA360 / 2CA44C
 * / 2CA8FC / 2CADE4 / 2CCC28 and rsb_2D0F90 / 2D2A4C moved here from g9_a/b/c.h at the G9/G10
 * merge); rescustm_idx stays in g9_b.h.
 */
#ifndef TN_G10_C_H
#define TN_G10_C_H
#include "g10_b.h"     /* g10_a.h (shell_*, SndPlay, game_391E50), g9_b.h (rescustm_idx) */
#include "g9_a.h"      /* camp_pos */
#include "g9_c.h"      /* game_442F32 */
#include "g2_c.h"      /* game_4413D0, damage_27CF30, interact_297400, aircraft_284A6C */
#include "g2_a.h"      /* grid_29582C, weapphys_4463D8 */
#include "g3_a.h"      /* physics_2D42E0 */
#include "g4_a.h"      /* AILowInit, ailow_24B8A0 / 24B8DC / 24B924 */
#include "g4_b.h"      /* aiperc_253AE8 */
#include "g6_b.h"      /* weather_2AFD4C */
#include "g8_c.h"      /* mapd_show_ents */
#include "g2_b2.h"     /* game_441366 */
#include "g1_a.h"      /* SndPlay, snd_chan_stop, GameTimePause, GameTimeResume2 */
#include "g1_b.h"      /* game_low_mem, radio_stop_all, radio_text_flags / msg_flags, ffmem_* */
#include "g2_c.h"      /* ordn_goal_c */
#include "g3_c.h"      /* main_246094, game_35A95C, game_35B77C */
#include "g5_b.h"      /* ffpipe_35C100, ffpipe_29E180, rndsky_2A6BD0 */
#include "g6_c.h"      /* ptc_desc_shift */
#include "g7_a.h"      /* hud_helmet, hud_38B9AE, hud_44244B / 44244C, hud_442F1A */
#include "g7_b.h"      /* helm_26214C */
#include "t1_c.h"      /* sprintf, gr_font_string_width / height, GfxDrawText */
#include "t2_a.h"      /* GfxSaveAndSetMode, GfxRestoreMode, GfxBlit */
#include "t2_b.h"      /* pcx_screenshot */
#include "t2_c.h"      /* TnEvent, tnui_add_dirty_rect */
#include "sup_c.h"     /* Mouse*, Key*, tnui_full_rect, tnui_back_canvas, tnm_* */

/* ------------------------------------------------------------------ M097 tail (shtail.c) */
extern long shell_snd_chan;             /* 0x43D2C8 channel of the shell's ambient sound (-1) */
extern uchar shell_snd_played[];        /* 0x43E670 per campaign position: ambient sound played */

void shell_2C3440(void);                /* 0x2C3440 play sound 0x7BA once per campaign step */
void shell_2C349C(void);                /* 0x2C349C stop it */
void shell_2C34C0(void);                /* 0x2C34C0 restore the squad records (shell_squad_save) */

/* ------------------------------------------------------------------ M105 options.c */
#pragma pack(push, 1)
/* a button or slider of the options screen (0x1B bytes) */
typedef struct {
    long ref;               /* +00 rectangle (resource 0x189) */
    LGRect rect;            /* +04 copied from ref by options_2CA834 */
    void *fn;               /* +0C called with arg (sliders: value * 30 / 100) */
    long idx;               /* +10 option number (options_2CA1B8), 0 none, -1 none */
    long arg;               /* +14 */
    uchar enabled;          /* +18 */
    ushort snd;             /* +19 click sound */
} OptButton;
/* the option setters (13 bytes): fn applies a value, fn2 hears every change */
typedef struct {
    void *fn;               /* +0 */
    void *fn2;              /* +4 */
    long value;             /* +8 the value fn was last given */
    uchar pct;              /* +C fn gets value * 100 / 30 */
} OptApply;
#pragma pack(pop)
LG_STATIC_ASSERT(sizeof(OptButton) == 0x1B, OptButton_size);
LG_STATIC_ASSERT(sizeof(OptApply) == 13, OptApply_size);

#define OPT_N 25
extern long opt_val[OPT_N];             /* 0x4427E4 values being edited ([8] = game_442804) */
/* values in force: a long[OPT_N] view of g9c's save_43D510 (0x43D510, saved as res 0x6F4) */
#define opt_applied ((long *)save_43D510)
extern long opt_allowed[OPT_N];         /* 0x44277C option may be changed */
extern OptApply opt_apply[OPT_N];       /* 0x35D8E4 */
extern OptButton opt_sliders[5];        /* 0x35D114 (4 + an empty one) */
extern OptButton opt_buttons[0x45];     /* 0x35D19C */
extern long opt_page_count[4];          /* 0x35DA44 buttons of each detail page */
extern short opt_page_btn[4][6];        /* 0x35DA54 their numbers (-1 none) */
extern uchar opt_hook_off[0x17];        /* 0x35DA2B hooks the options screen turns off */
extern uchar opt_help_hook_off[0x17];   /* 0x35DA8C hooks the help screen turns off */
extern long opt_page_title[4];          /* 0x2CA044 detail page titles (code segment) */
extern long opt_page_bm[4];             /* 0x2CA054 detail page backgrounds (code segment) */
extern long *opt_page_val[4];           /* 0x2CA034 option shown by each page (code segment) */
extern ulong opt_hook_saved;            /* 0x4427E0 hooks turned off */
extern void *opt_save_buf;              /* 0x442768 screen save (0x4B000 bytes) */
extern long opt_cursor;                 /* 0x44276C cursor to restore */
extern long opt_blocking;               /* 0x442770 options_2CA2C0 added the blocking handlers */
extern long opt_page;                   /* 0x442774 detail page 0..3 */
extern long opt_no_show;                /* 0x442778 a closing button: no MouseShow */
extern uchar opt_cursor_drawn;          /* 0x442848 cursor shown when the screen opened */
extern uchar opt_442849;                /* 0x442849 */
extern LGRect opt_saved_rect;           /* 0x442460 tnui_full_rect */
extern uchar opt_saved_pal[0x300];      /* 0x442468 */
extern long opt_35D100;                 /* 0x35D100 (1) */
extern uchar opt_mouse_down;            /* 0x35D104 */
extern uchar opt_in_click;              /* 0x35DA29 a button's function runs */
extern uchar opt_dragging;              /* 0x35DA2A a slider is dragged */
extern uchar opt_35DA32, opt_35DA33;    /* 0x35DA32 / 0x35DA33 cleared on open */
extern long opt_35DA88;                 /* 0x35DA88 option 23 */
extern uchar opt_35DA93, opt_35DA94;    /* 0x35DA93 / 0x35DA94 cleared by the help screen */
extern uchar game_35ADE1;               /* 0x35ADE1 option 7: 1 when on */

void options_2CA064(long v);            /* 0x2CA064 option 14: ffpipe_35C100 */
void options_2CA06C(long v);            /* 0x2CA06C option 16: ffpipe_29E180 */
void options_2CA074(long v);            /* 0x2CA074 option 15: rndsky_2A6BD0 */
void options_2CA07C(long v);            /* 0x2CA07C option 18: ptc_desc_shift = 3 - v */
void options_2CA09C(long v);            /* 0x2CA09C option 6 changed: detail preset 1..4 */
void options_2CA180(long v);            /* 0x2CA180 options 13..16 changed: preset "custom" */
void options_2CA18C(long v);            /* 0x2CA18C option 4: mouse buttons */
void options_2CA1AC(long v);            /* 0x2CA1AC option 7 */
void options_2CA1B8(long i, long v);    /* 0x2CA1B8 tell an option's fn2 */
void options_2CA1E4(void);              /* 0x2CA1E4 */
void options_2CA23C(void);              /* 0x2CA23C redraw what the screen covered */
void options_2CA2C0(long a);            /* 0x2CA2C0 */
void options_2CA360(void);              /* 0x2CA360 */
uchar options_2CA44C(void *ev);         /* 0x2CA44C key / mouse handler */
uchar options_2CA450(TnEvent *e);       /* 0x2CA450 key / mouse handler */
OptButton *options_2CA834(OptButton *t, LGPoint pt, long n);    /* 0x2CA834 hit test */
void options_2CA890(void);              /* 0x2CA890 (no callers) set the cursor */
void options_2CA8FC(void);              /* 0x2CA8FC */
void options_2CAAC0(LGRect *r, char *text, long font, grs_canvas *c); /* 0x2CAAC0 */
void options_2CAB58(long rect, long text, long font, grs_canvas *c);   /* 0x2CAB58 */
void options_2CAB84(void);              /* 0x2CAB84 (no callers) redraw */
void options_2CABAC(grs_canvas *c);     /* 0x2CABAC button labels */
void options_2CAC54(grs_canvas *c);     /* 0x2CAC54 every option */
void options_2CAD30(grs_canvas *c);     /* 0x2CAD30 background */
void options_2CAD68(LGRect *r, long v); /* 0x2CAD68 (no callers) a slider */
void options_2CAD9C(LGRect *r, long i, long ref);  /* 0x2CAD9C a slider tick */
void options_2CADE4(long a);            /* 0x2CADE4 */
void options_2CAF94(long v);            /* 0x2CAF94 slider 0 */
void options_2CB044(long v);            /* 0x2CB044 slider 1 */
void options_2CB0F4(long v);            /* 0x2CB0F4 slider 2 */
void options_2CB1A4(long v);            /* 0x2CB1A4 slider 3 */
void options_2CB254(long v);            /* 0x2CB254 option 4 */
void options_2CB584(long v);            /* 0x2CB584 option 8 */
void options_2CB7B4(long v);            /* 0x2CB7B4 option 9 */
void options_2CB918(long v);            /* 0x2CB918 option 11 */
void options_2CBA70(long v);            /* 0x2CBA70 option 12 */
void options_2CBBC8(void);              /* 0x2CBBC8 accept and close */
void options_2CBBEC(void);              /* 0x2CBBEC close */
void options_2CBBF8(void);              /* 0x2CBBF8 quit the game (main_245334) */
void options_2CBC08(uchar clear);       /* 0x2CBC08 close the screen */
void options_2CBCF4(long dir);          /* 0x2CBCF4 next / previous detail page */
void options_2CBD04(long dir);          /* 0x2CBD04 change the detail page */
void options_2CBDA8(void);              /* 0x2CBDA8 toggle option 14 */
void options_2CBDDC(void);              /* 0x2CBDDC toggle option 15 */
void options_2CBE10(void);              /* 0x2CBE10 toggle option 16 */
void options_2CBE44(void);              /* 0x2CBE44 toggle option 17 */
void options_2CBE78(long v);            /* 0x2CBE78 option 13 */
void options_2CBE8C(long v);            /* 0x2CBE8C option 18 */
void options_2CBEA0(long v);            /* 0x2CBEA0 detail page (v: option 6) */
void options_2CC01C(void);              /* 0x2CC01C detail preview picture */
void options_2CC234(long v);            /* 0x2CC234 option 19 */
void options_2CC3D0(long a);            /* 0x2CC3D0 option 21 (two check boxes) */
void options_2CC5DC(long a);            /* 0x2CC5DC option 22 (two check boxes) */
void options_2CC7D8(long v);            /* 0x2CC7D8 option 23 */
void options_2CC948(long v);            /* 0x2CC948 option 24 */
void options_2CCAB8(long v);            /* 0x2CCAB8 option 21 applied: radio_text_flags */
void options_2CCAD4(long v);            /* 0x2CCAD4 option 22 applied: radio_msg_flags */
void options_2CCAF0(long v);            /* 0x2CCAF0 option 23 applied */
void options_2CCAF8(void);              /* 0x2CCAF8 abort the mission */
uchar options_2CCB38(TnEvent *e);       /* 0x2CCB38 help screen handler */
void options_2CCB58(void);              /* 0x2CCB58 close the help screen */
void options_2CCBE0(void);              /* 0x2CCBE0 draw the help screen */
void options_2CCC28(void);              /* 0x2CCC28 */

/* ------------------------------------------------------------------ M111 rescustm.c */
#pragma pack(push, 1)
/* a clickable area of the scenario builder (0x11 bytes) */
typedef struct {
    long ref;               /* +00 rectangle (resource 0x973) */
    LGRect rect;            /* +04 copied from ref by rsb_2CF6FC */
    void *fn;               /* +0C called with arg */
    uchar arg;              /* +10 */
} RsbButton;
/* one of the five unit slots (4 bytes) */
typedef struct {
    uchar idx;              /* +0 index in the type list (class 1: list1, 2: n1 + list2, 3: n1 + n2 + list3) */
    uchar type;             /* +1 unit type */
    uchar cls;              /* +2 class 1 soldier squad, 2 vehicle, 3 aircraft */
    uchar count;            /* +3 0..6 */
} RsbSlot;
#pragma pack(pop)
LG_STATIC_ASSERT(sizeof(RsbButton) == 0x11, RsbButton_size);

extern uchar rsb_2CEE20[7];             /* 0x2CEE20 vehicle template (code segment) */
extern uchar rsb_2CEE27[14];            /* 0x2CEE27 squad template (code segment) */
extern uchar rsb_2CEE35[7];             /* 0x2CEE35 aircraft template (code segment) */
extern fix rsb_gravity[9];              /* 0x35E4FC weapphys_4463D8 for each difficulty */
extern RsbButton rsb_buttons[0x3D];     /* 0x35E520 main page */
extern RsbButton rsb_grid_buttons[0x37];/* 0x35E930 weapon grid page */
extern long rsb_resfile;                /* 0x35ECD8 rescustm.res */
extern uchar rsb_show_extra;            /* 0x35ECDC size 8 (custom) chosen */
extern long rsb_mode;                   /* 0x35ECE0 0 main page, 1 weapon grid */
extern long rsb_result;                 /* 0x35ECE4 1 start, 2 cancel */
extern long rsb_dirty;                  /* 0x35ECE8 redraw */
extern uchar rsb_sel;                   /* 0x35ECEC slot of the open list (0xFF) */
extern uchar rsb_n1;                    /* 0x35ECED squad types */
extern uchar rsb_n2;                    /* 0x35ECEE vehicle types */
extern uchar rsb_n3;                    /* 0x35ECEF aircraft types */
extern uchar rsb_nnames;                /* 0x35ECF0 weapon names */
/* rsb_pop: 1 a list is open, 2 it was drawn, 4 count, 8 type, 0x10 mission, 0x20 difficulty */
extern uchar rsb_pop;                   /* 0x35ECF1 open list */
extern ulong rsb_hint_time;             /* 0x35ECF4 tn_ticks until the hint goes */
extern uchar rsb_hint;                  /* 0x35ECF8 hint shown (0xFF none) */
extern uchar rsb_nregions;              /* 0x35ECF9 regions of the mission (res 0x960) */
extern uchar rsb_grid_cur;              /* 0x35ECFA weapon grid cell row * 4 + column */
extern short rsb_ent;                   /* 0x35ECFC entity made by rsb_2D0F90 */
extern long rsb_units;                  /* 0x35ED00 units placed */
extern long rsb_min_squad;              /* 0x35ED04 (100) */
extern long rsb_last_squad;             /* 0x35ED08 */
extern ulong rsb_timer;                 /* 0x35ED0C game_time of rsb_2D2B94 */
extern uchar rsb_timer_on;              /* 0x35ED10 */
extern uchar rsb_used[0x20];            /* 0x442E70 region used */
extern LGRect rsb_clip;                 /* 0x442E90 mouse constraint of the open list */
extern uchar rsb_grid_save[25];         /* 0x442E98 rsb_grid when the grid page opened */
extern uchar rsb_list3[15];             /* 0x442EB1 aircraft types (res 0x966) */
extern uchar rsb_list1[15];             /* 0x442EC0 squad types (res 0x962) */
extern uchar rsb_list2[15];             /* 0x442ECF vehicle types (res 0x964) */
extern uchar rsb_grid[25];              /* 0x442EDE 5 rows of 4 weapon numbers + kind (4 / 2) */
extern uchar rsb_names[0x21];           /* 0x442EF7 weapons offered (res 0x968) */
extern uchar rsb_side;                  /* 0x442F19 1 / 2 / 4: mission kind 0x2A / 0x2C / 0x2B */
extern uchar rsb_difficulty;            /* 0x442F1C 0..8 (rsb_gravity) */
extern uchar rsb_size;                  /* 0x442F1D 1 / 2 / 4 weapon preset, 8 custom */
extern RsbSlot rsb_slots[5];            /* 0x442F1E */

void rsb_2CEE3C(void);                  /* 0x2CEE3C open the screen */
void rsb_2CEEA4(void);                  /* 0x2CEEA4 close it */
void rsb_2CEF28(void);                  /* 0x2CEF28 per frame */
void rsb_2CEF9C(void);                  /* 0x2CEF9C defaults (res 0x967) */
uchar rsb_2CF048(uchar type, uchar cls);/* 0x2CF048 type -> index in its class list */
void rsb_2CF0C8(void);                  /* 0x2CF0C8 draw the main page */
void rsb_2CF11C(void);                  /* 0x2CF11C the hint */
void rsb_2CF1A0(void);                  /* 0x2CF1A0 every control */
uchar rsb_2CF25C(TnEvent *e);           /* 0x2CF25C key / mouse handler */
RsbButton *rsb_2CF6FC(RsbButton *t, LGPoint pt, long n);   /* 0x2CF6FC hit test */
void rsb_2CF754(void);                  /* 0x2CF754 labels */
void rsb_2CF8F0(uchar a);               /* 0x2CF8F0 mission (0 previous, 1 next, 0x40 draw) */
void rsb_2CF9EC(uchar a);               /* 0x2CF9EC rsb_side */
void rsb_2CFAC0(uchar a);               /* 0x2CFAC0 hud_442F1A (weather) */
void rsb_2CFB94(uchar a);               /* 0x2CFB94 slot type (bit = slot, 0x80 next, 0x40 draw) */
void rsb_2CFE2C(uchar a);               /* 0x2CFE2C slot count */
void rsb_2CFF10(uchar a);               /* 0x2CFF10 difficulty */
void rsb_2CFFEC(uchar a);               /* 0x2CFFEC toggle a cheat bit of game_442F1B */
void rsb_2D00D8(uchar a);               /* 0x2D00D8 rsb_size */
void rsb_2D01C4(uchar a);               /* 0x2D01C4 game_442F32 */
void rsb_2D02A8(void);                  /* 0x2D02A8 start */
void rsb_2D0324(void);                  /* 0x2D0324 cancel */
void rsb_2D0330(void);                  /* 0x2D0330 quit */
void rsb_2D0340(void);                  /* 0x2D0340 load the type lists */
long rsb_2D04A4(void);                  /* 0x2D04A4 (no callers) first empty region */
void rsb_2D04D4(void);                  /* 0x2D04D4 open the weapon grid (size 8) */
void rsb_2D05C8(uchar a);               /* 0x2D05C8 weapon preset a into rsb_grid */
void rsb_2D085C(void);                  /* 0x2D085C grid: cancel */
void rsb_2D093C(void);                  /* 0x2D093C grid: accept */
void rsb_2D0954(void);                  /* 0x2D0954 grid per frame */
void rsb_2D0984(void);                  /* 0x2D0984 draw the grid page */
void rsb_2D0A20(void);                  /* 0x2D0A20 grid labels */
void rsb_2D0BA8(void);                  /* 0x2D0BA8 (no callers) */
void rsb_2D0C00(uchar v);               /* 0x2D0C00 grid cell */
void rsb_2D0E00(uchar row);             /* 0x2D0E00 copy a row to every row */
void rsb_2D0E68(uchar w);               /* 0x2D0E68 weapon of the cell */
void rsb_2D0F90(void);                  /* 0x2D0F90 */
void rsb_2D1788(uchar k, LGPoint pt);   /* 0x2D1788 count list */
void rsb_2D1BEC(uchar k, LGPoint pt);   /* 0x2D1BEC type list */
void rsb_2D21B4(uchar a, LGPoint pt);   /* 0x2D21B4 mission list */
void rsb_2D2600(uchar a, LGPoint pt);   /* 0x2D2600 difficulty list */
void rsb_2D2A1C(void);                  /* 0x2D2A1C (no callers) shade the screen */
void rsb_2D2A4C(void);                  /* 0x2D2A4C */
void rsb_2D2B04(void);                  /* 0x2D2B04 clear the screen */
uchar rsb_2D2B44(void);                 /* 0x2D2B44 game_35ECF2 & 1 */
void rsb_2D2B54(void);                  /* 0x2D2B54 reset */
uchar rsb_2D2B94(long t);               /* 0x2D2B94 1 when t has passed since the first call */

#endif
