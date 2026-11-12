/* g8_a.h -- batch G8, worker g8a: M022 MFD panels and descrambler (0x26CB50-0x27161F,
 * src/game/hud/mfdpanel.c) and M023 MFD system-status page (0x271620-0x271C5F,
 * src/game/hud/mfdsyspg.c).  Read from __FF.EXE v1.09.
 *
 * The MFD ("multi-function display") is a HUD view mode (hud_mode_fns, g7_c.h).  M022 holds
 * the squad modes (1 squad list, 2 squad member, 3 member pages: status / systems / weapons),
 * the order menu (7), the descrambler (26), the two scanner/charge dialogs (23, 27), and the
 * squad-order helpers that send AI messages (mfd_26FB38 .. mfd_271470).  M023 draws the
 * system-damage list of a PBA (mode 11).
 *
 * Squad member records are g1b's GameSpeaker (0x32 bytes at game_391E50, count game_391E4C);
 * this module reads them by offset: +0 short kind (1 soldier, 2 other), +2 short entity,
 * +4 short PBA slot, +6 uchar voice (name text 0x16C0000 + voice, portrait 0x21A0000 + voice),
 * +7 schar radio channel, +0xD schar (text 0x16E0030 + it), +0x31 uchar name colour.
 * Index 0 is the player; the lists start at 1.
 *
 * Globals are bound to their original addresses through symbols-g8-a.csv.
 */
#ifndef TN_G8_A_H
#define TN_G8_A_H
#include "game.h"
#include "2d.h"
#include "res.h"
#include "support.h"
#include "g7_b.h"      /* g7_a.h / g7_c.h: HudWidget, HudView, hud_*; g1_b.h GameSpeaker,
                          RadioSpeak, radio_talking; g2_a.h ord_table, voice_soldier,
                          soldier_skill; g4_c1.h aiacc_25ACE0 */

#pragma pack(push, 1)

/* One line of the MFD order menu (0x16 bytes; lists such as 0x35A038 / 0x35A090 / 0x35A2FC,
 * ended by fn == 0). */
typedef struct {
    void *fn;           /* +00 Watcom fn(arg, short member, char *out): writes the line text */
    long arg;           /* +04 argument; with flags & 1 the Ref of a fixed text */
    ushort key;         /* +08 key code (0x83B..0x843 = F1..F9) */
    uchar f0a;          /* +0A */
    uchar flags;        /* +0B bit 0: static text */
    short cmd;          /* +0C command (0x19, 0x1A, 0x3D, 0x42 ...) */
    uchar f0e[8];
} MfdItem;

typedef struct {
    long f00;
    MfdItem *items;     /* +04 */
} MfdMenu;

#pragma pack(pop)

/* ------------------------------------------------------------------ globals (orig data rows) */
extern Ref mfd_35B154[3];               /* 0x35B154 page button rectangles (mode 3) */
extern void *mfd_35B160[4];             /* 0x35B160 mode 3 page draw functions */
extern uchar mfd_35B170[];              /* 0x35B170 text colour per weapon_ready result */
extern LGRect mfd_35B176;               /* 0x35B176 descrambler text rectangle */
extern long mfd_35B180;                 /* 0x35B180 descrambler scroll position */
extern uchar mfd_35B184;                /* 0x35B184 descrambler state (1 clear, 2 timing, 4 done) */
extern uchar mfd_35B18C;                /* 0x35B18C descrambler blink phase 0..2 */
extern ulong mfd_35B190;                /* 0x35B190 game_time of the next blink */
extern long mfd_35B194;                 /* 0x35B194 descrambler sound channel */
extern ulong mfd_35B198;                /* 0x35B198 game_time the message completes */
extern Ref mfd_35B1B4[];                /* 0x35B1B4 per mission: order text override */
extern uchar mfd_35B6E5;                /* 0x35B6E5 */
extern schar mfdpage_35B274;            /* 0x35B274 system page count (player) */
extern schar mfdpage_35B275;            /* 0x35B275 system page count (other) */
extern schar mfdpage_35B276;            /* 0x35B276 system page shown (player) */
extern schar mfdpage_35B277;            /* 0x35B277 system page shown (other) */
extern char mfd_3558C8[];               /* 0x3558C8 constant string */
extern MfdItem mfd_35A038[];            /* 0x35A038 order menu list */
extern MfdItem mfd_35A090[];            /* 0x35A090 order menu list */
extern MfdItem mfd_35A2FC[];            /* 0x35A2FC order menu list */
extern short mfd_3599D4;                /* 0x3599D4 current squad member (read as a byte too) */
extern uchar mfd_3599EA;                /* 0x3599EA mode 27 choice */
extern short hud_389BC4;                /* 0x389BC4 entity (radar pick; read as a byte too) */
extern short hud_389BDC;                /* 0x389BDC entity */
extern HudWidget *hud_w_389E44;         /* 0x389E44 MFD widget */
extern short mfd_38FC81;                /* 0x38FC81 entity of the member shown */
extern MfdMenu *mfd_38FC87;             /* 0x38FC87 current order menu */
extern short mfd_38FC90[];              /* 0x38FC90 per radio channel: target entity */
extern char mfd_38FC98[0x1B];           /* 0x38FC98 descrambler text line */
extern long mfd_38FCC0[];               /* 0x38FCC0 args of the menu lines shown */
extern schar mfd_38FCE8[];              /* 0x38FCE8 per channel: two bytes (b, a) of the last order */
extern uchar mfd_38FCF4[];              /* 0x38FCF4 per channel */
extern uchar mfd_38FCF8[4];             /* 0x38FCF8 per channel: charges left (reset to 5) */
extern long mfd_38FCFC;                 /* 0x38FCFC menu line picked */
extern schar mfd_38FD00;                /* 0x38FD00 mode 23 maximum */
extern schar mfd_38FD01;                /* 0x38FD01 mode 23 / 27 value */
extern uchar mfd_38FD02;                /* 0x38FD02 mode 3 page (0 = status) */
extern char mfd_38FD04[3];              /* 0x38FD04 key label */
extern char mfd_38FD10[][0x14];         /* 0x38FD10 weapon names */
extern uchar mfd_392141[];              /* 0x392141 per channel 12 bytes: short ent, fix x, fix y */
extern schar mfd_395AD8[16];            /* 0x395AD8 8 (mission, value) pairs */
extern schar mfd_395AEC[16];            /* 0x395AEC 8 (mission, value) pairs */
extern schar mfd_395B00[4];             /* 0x395B00 2 (mission, value) pairs */
extern fix mfd_4413D8, mfd_4413DC;      /* 0x4413D8 descrambler site x, y */

/* ------------------------------------------------------------------ M022 mfdpanel.c */
void mfd_26CB50(HudWidget *w, uchar on);
void mfd_26CB78(HudWidget *w, HudEvent *ev);
void mfd_26CC48(long view);
void mfd_26CD00(void);
void mfd_26CFCC(void);
void mfd_26D194(long view);
uchar mfd_26D1BC(HudWidget *w, long y);
void mfd_26D1F4(void);
uchar mfd_26D300(long dir);
void mfd_26D398(HudWidget *w, uchar on);
void mfd_26D3DC(HudWidget *w, HudEvent *ev);
void mfd_26D52C(void);
void mfd_26D618(HudWidget *w, uchar on);
void mfd_26D630(HudWidget *w, HudEvent *ev);
void mfd_26D748(long view);
void mfd_26D754(void);
void mfd_26D764(void);
void mfd_26DC8C(void);
void mfd_26DCC0(void);
void mfd_26DEE4(void);
void mfd_26DEE8(void);
void mfd_26DEF8(void);
void mfd_26DF10(void);
void mfd_26DF20(HudWidget *w, HudEvent *ev);
void mfd_26E208(void);
void mfd_26E724(LGRect *r, long noise);
void MfdDescramblerPanel(void);
void mfd_26EFAC(HudWidget *w, HudEvent *ev);
void mfd_26F088(HudWidget *w, uchar on);
void mfd_26F094(long a);
void mfd_26F13C(void);
void mfd_26F2C8(HudWidget *w, HudEvent *ev);
void mfd_26F4F8(HudWidget *w, uchar on);
void mfd_26F504(HudWidget *w, uchar on);
void mfd_26F5AC(HudWidget *w, HudEvent *ev);
void mfd_26F8A8(void);
void mfd_26FAB4(long ent);
void mfd_26FB38(long a, long b, long ent, long cmd);
void mfd_26FD8C(void);
void mfd_26FDA8(long ent, long target, long c);
void mfd_26FF38(long ent, long ent2);
void mfd_270090(long ent, long what);
void mfd_27016C(long i, long x, long y, long h, long w);
void mfd_27022C(long i, long x, long y, long h, long w);
long mfd_27047C(long k, long what, long cmd);
void mfd_27065C(long ent, long what);
void mfd_270930(long ent, long target);
void mfd_270CE0(long ent, long target, long cmd);
long mfd_270E70(void);
uchar mfd_270E80(long ent);
uchar mfd_270F0C(void);
void mfd_270F1C(long v);
void mfd_270FB0(long brain, char *out);
long mfd_271428(void);
void mfd_271470(long ent, long target);
void mfd_2715A8(HudWidget *w, HudEvent *ev);

/* ------------------------------------------------------------------ M023 mfdsyspg.c */
void mfdpage_271620(HudWidget *w, uchar on);
void mfdpage_271638(long page);
void mfdpage_271648(HudWidget *w, HudEvent *ev);
void mfdpage_271684(long view);
void mfdpage_271690(void);
void mfdpage_2716E8(void);
void mfdpage_271714(void);
void mfdpage_27172C(uchar *p);
void mfdpage_2717F0(long ent);
long mfdpage_271828(uchar *a, uchar *b);
void mfdpage_271854(uchar *p, schar *page, schar *npages);
void mfdpage_271C0C(void);

#endif
