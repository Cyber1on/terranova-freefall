/* g9_b.h -- declarations for worker g9b (batch G9): the game's main module (startup, main
 * loop driver, crash code, FFLoadMission, MissionSetup, yes/no dialogs; src/game/main/ffmain.c,
 * M000), the in-mission key handler (playkey.c, M029), the view helpers (mishelp.c, M103) and
 * the CD -> hard disk resource cache ("installer": instpath.c M123, instcopy.c M124,
 * instdisk.c M125).  Globals are bound to their original addresses through symbols-g9-b.csv.
 */
#ifndef TN_G9_B_H
#define TN_G9_B_H
#include "game.h"
#include "lg.h"
#include "support.h"
#include "res.h"
#include "2d.h"
#include "sup_a.h"     /* _dos_findfirst */
#include "sup_c.h"     /* Mouse*, tnm_cursor_ref */
#include "t1_b.h"      /* strcmp, remove, atoi, CfgLookup, kb_flush */
#include "g1_b.h"      /* ffmem_*, game_low_mem, radio_file_names, radio_voice_class, game_391E50 */
#include "g1_c.h"      /* filelength, g1c_strcpy / g1c_strcat */
#include "g3_a.h"      /* game_441359 */
#include "g3_b.h"      /* game_441361 */
#include "g3_c.h"
#include "g4_a.h"      /* game_441365 */
#include "g7_a.h"      /* hud_441355 / hud_441357, hud_drag_widget, hud_aim_center */
#include "t1_a.h"      /* ScriptThread, ScriptFunc, ScriptOpReg */
#include "t1_c.h"      /* TBoxStyle, tbox_create, tmenu_create */
#include "g5_a.h"      /* GameMap */
#include "g8_c.h"      /* cmd_25F33C (G8c) */
#include "g9_a.h"      /* simtype_27ACC0, simload_283270 (G9a) */
#include "g4_c2.h"

#pragma pack(push, 1)

/* ------------------------------------------------------------------ Watcom CRT (orig rows) */
int access(const char *path, int mode);                      /* 0x301FAB */
int _dos_findnext(void *buf);                                /* 0x2FD129 */
int mkdir(const char *path);                                 /* 0x302146 int 21h 39h */
int chdir(const char *path);                                 /* 0x302163 int 21h 3Bh */
char *getcwd(char *buf, int size);                           /* 0x3020BD */
int stat(const char *path, void *st);                        /* 0x302180 (rejects wildcards) */
int system(const char *cmd);                                 /* 0x3022EB COMSPEC /c */
int tolower(int c);                                          /* 0x30234F */
unsigned _dos_getdiskfree(unsigned drive, void *df);         /* 0x302360 int 21h 36h */
void _harderr(void *handler, unsigned seg);                  /* 0x301FE6 eax = offset, edx = cs */
unsigned _dos_getfileattr(const char *path, ushort *attr);   /* 0x2DE7E8 int 21h 4300h */
int puts(const char *s);                                     /* 0x2DF557 */
void exit(int code);                                         /* 0x2DF6AA */

/* Watcom find_t (DTA) as _dos_findfirst fills it */
typedef struct {
    char reserved[21];
    uchar attrib;           /* +0x15 */
    ushort wr_time;         /* +0x16 */
    ushort wr_date;         /* +0x18 */
    long size;              /* +0x1A */
    char name[13];          /* +0x1E */
} G9bFind;

/* Watcom diskfree_t (_dos_getdiskfree) */
typedef struct {
    ushort total_clusters;  /* +0 */
    ushort avail_clusters;  /* +2 */
    ushort sectors_per_cluster; /* +4 */
    ushort bytes_per_sector;    /* +6 */
} G9bDiskFree;

/* ------------------------------------------------------------------ resource cache (M122-M125)
 * Install level (config INSTALL_LEVEL, game_install_level): 0 = everything runs from the CD and
 * the big resource files are copied to TARG_DIR while the game runs, 1 = speech is cached,
 * 2 = full install (nothing to copy). */
extern long game_install_level;          /* 0x35970C */
extern void *install_font;               /* 0x35F3C4 ResLock(0x167) while copying */
extern long install_35F3C8;              /* 0x35F3C8 cleared before a copy pass */
extern uchar install_crit_err;           /* 0x35F3CC set by the critical-error handler */
extern char install_targ_dir[0x90];      /* 0x446790 hard disk directory (TARG_DIR\...) */
extern char install_cd_dir[0x90];        /* 0x446820 CD directory (CD_DIR\...) */
extern schar game_391F6E;                /* 0x391F6E number of radio voices of the mission */

/* A file list: entry 0 is the header (name = findfirst pattern, size = number of entries
 * that follow); entries 1..n name the files wanted in the target directory. */
typedef struct {
    char *name;             /* +0 */
    uchar found;            /* +4 already in the target directory */
    uchar err;              /* +5 could not be opened on the CD */
    long size;              /* +6 file size (-1 on error) / header: count */
} InstallFile;
LG_STATIC_ASSERT(sizeof(InstallFile) == 10, InstallFile_size);

/* Progress text drawn while copying (0x4468B0) */
typedef struct {
    long base;              /* +0x00 Ref of the first message line */
    short count;            /* +0x04 copy steps so far */
    short period;           /* +0x06 copy steps per message line */
    short y;                /* +0x08 text position */
    short unused;           /* +0x0A zeroed, never read */
    short x;                /* +0x0C */
    schar nspk;             /* +0x0E speech: voices to list */
    schar step;             /* +0x0F line within the current block */
    schar nsteps;           /* +0x10 lines in all */
    schar spk;              /* +0x11 speech: voices listed */
} InstallProgress;
LG_STATIC_ASSERT(sizeof(InstallProgress) == 0x12, InstallProgress_size);
extern InstallProgress install_prog;     /* 0x4468B0 */

/* copy job of install_2DC760 (G9c, M122): +0x18 bytes still to copy */
typedef struct {
    uchar g_00[0x18];
    long left;              /* +0x18 */
} InstallJob;

/* M122 copy jobs (install_2DC5B0 / 2DC760 / 2DC7C8 / 2DC88C): g9_c.h */

char *install_2DC9C0(char *dir, char *name);             /* malloc'd dir\name */
char *install_2DCAA4(long hd, char *name);
void install_2DCABC(char *cd, char *targ);
uchar install_2DCB0C(char *name);
uchar install_2DCB38(char *name);
uchar install_2DCB60(InstallJob **job, char *name);
long install_2DCBE4(InstallFile *list, char *name);
long install_2DCC24(InstallFile *list);
void install_2DCD80(long total, long divisor, schar nsteps, long base);
void install_2DCDCC(void);
void install_2DCDF8(void);
void install_2DCEA4(void);
void install_2DD05C(schar nvoices, uchar show);
void install_2DD344(char *name, uchar copy, long base);
void install_2DD5C0(long mode);
void install_2DD78C(void);
void install_2DD828(void);
void install_2DD8C8(schar level);
void install_2DD940(void);                               /* far critical-error handler (asm) */
void install_2DD950(void);
uchar install_2DD964(char *path);
long install_2DDA9C(char *path);
long install_2DDAF4(char *path, uchar up);
long install_2DDC30(char *path);
void install_2DDC40(long drive, char *path, G9bDiskFree *df);
long install_2DDCC4(long drive, char *path);
long install_2DDD18(long drive, char *path);
extern uchar install_2DD930[8];          /* 0x2DD930 diskfree template (code segment data) */
extern uchar install_2DD938[8];          /* 0x2DD938 */

/* a FrameDesc as RefGet returns it */
typedef struct {
    grs_bitmap bm;          /* +0x00 */
    short anchor[4];        /* +0x10 */
    long pall_off;          /* +0x18 palette offset in its resource (0 = none) */
    uchar data[1];          /* +0x1C bits */
} G9bFrame;

/* resource files kept open by the game */
extern int res_base_file;                /* 0x3596D8 resbase.res */
extern int res_tnobj_file;               /* 0x3596DC restnobj.res */
extern int res_game_file;                /* 0x36F4A0 resgame.res */

/* ------------------------------------------------------------------ main module (M000) */
extern char univbe_drv_name[];           /* 0x355004 "univbe.drv" (main_2440C8 cuts it to "") */
extern uchar game_35971D;                /* 0x35971D command line -L */
extern uchar game_mission_count;         /* 0x35971F missions started (crash code) */
extern uchar game_hires;                 /* 0x359721 config "hires" */
extern LGPoint game_cursor_max;          /* 0x359722 */
extern long game_359728;                 /* 0x359728 quit without asking */
extern uchar game_quitting;              /* 0x35972C quit confirmed: keys ignored */
extern uchar game_in_mission;            /* 0x35972E set by MissionSetup, cleared by main_2451D0 */
extern uchar game_unit_tmpl[0x10];       /* 0x359730 unit template for pbasim_289A64 / ailow_24B8A0
                                            (+8..+0xD copied from the speaker records) */
extern long game_359740;                 /* 0x359740 quitting not allowed */
extern uchar dlg_text_params[0x14];      /* 0x359744 tbox_create parameters (+5 rect) */
extern TBoxStyle dlg_text_style;         /* 0x359758 */
extern TBoxStyle dlg_menu_style;         /* 0x359768 */
extern long dlg_text_margins;            /* 0x359778 */
extern uchar dlg_menu_params[0x12];      /* 0x35977C tmenu_create parameters (+5 rect) */
extern LGRect dlg_text_rect;             /* 0x35978E */
extern LGRect dlg_menu_rect;             /* 0x359796 */
extern uchar dlg_done;                   /* 0x35979E yes/no dialog answered */
extern uchar dlg_answer;                 /* 0x35979F 1 = yes */
extern uchar dlg_pal_saved;              /* 0x3597A0 the dialog loaded its own palette */
extern uchar dlg_pause_hooks[23];        /* 0x3597A1 hook slots stopped while a dialog is up */
extern int mission_res_file;             /* 0x35D0F8 open missN.res while loading, else -1 */
extern long *script_vars_35DBE8[];       /* 0x35DBE8 ScriptLoadGlobals variable table */
extern ScriptFunc script_funcs_35DCCC[]; /* 0x35DCCC ScriptLoadGlobals function table (natives) */
extern void *crt_35F460;                 /* 0x35F460 CRT handler pointer (= crt_2DF9E9) */
extern uchar main_3596EC[24];            /* 0x3596EC palette entries 0-7 */
extern uchar dlg_palette[0x300];         /* 0x36EFE0 palette searched for the dialog colours */
extern LGRect dlg_no_rect;               /* 0x36F2E0 "no" button */
extern LGRect dlg_yes_rect;              /* 0x36F2E8 "yes" button */
extern LGRect dlg_rect;                  /* 0x36F2F0 dialog frame */
extern grs_bitmap dlg_save;              /* 0x36F2F8 screen under the dialog */
extern char main_msg_buf[0x180];         /* 0x36F308 start / crash / exit messages */
extern long dlg_saved_cursor;            /* 0x36F488 */
extern ulong dlg_paused_hooks;           /* 0x36F48C hook bits the dialog cleared */
extern long game_36F494;                 /* 0x36F494 ff_options byte 0xA2 */
extern long game_paused_menu;            /* 0x36F4A8 options key ignored while set */
extern long game_36F4AC;                 /* 0x36F4AC dialog cursor choice outside missions */
extern long shell_thread_id;             /* 0x36F4B0 */
extern ScriptThread *shell_thread;       /* 0x36F4B4 shell script thread (0x3E90004) */
extern fix game_gamma;                   /* 0x36F4B8 */
extern GameMap game_map;                 /* 0x36F4C4 */
extern char mission_file_name[];         /* 0x3705EE name of the last mission file */
extern uchar dlg_cursor_was_drawn;       /* 0x3705FD */
extern uchar crash_report_on;            /* 0x3705FE main_244124 prints the crash code */
extern long game_43D318;                 /* 0x43D318 1 in a mission, 4 after it */
extern uchar rescustm_idx;               /* 0x442F18 custom mission index (rescustm.res) */
extern char *crash_msg;                  /* 0x446B00 message printed at exit */
extern uchar crash_446B04;               /* 0x446B04 */
extern char main_244010[0xA9];           /* 0x244010 "Capturing " template (code segment) */

/* callees without a header (orig rows in symbols-g9-b.csv).  G9c / G10 callees are declared in
 * g9_c.h / g10_a.h / g10_c.h (moved there at the G9/G10 merge). */
uchar univbe_2DE353(char *drv, long a, long b, long c);  /* 0x2DE353 cdecl */
void univbe_2DE33B(void);                /* 0x2DE33B */
void crt_2DF720(void);                   /* 0x2DF720 (AtExit) */
void crt_2DF9E9(void);                   /* 0x2DF9E9 */

void main_2440BC(void);
void main_2440C8(void);
void main_244124(void);
int game_main(int argc, char **argv);  /* 0x2441B8 the game's main(), called by the CRT's __CMain 0x30FD40 */
long main_244A24(void);
uchar main_244A30(uchar *ev);
void main_244BD0(void);
void FFLoadMission(long n, long arg);
void MissionSetup(void);
void main_2451D0(void);
void main_245280(void);
void main_2452B8(long n);
void main_245320(void);
long main_245334(void);                  /* 0x245334 (eax: what it leaves) */
long main_2453B8(void);
void main_2453DC(void);
void main_245468(void);             /* 0x245468 empty debug hook */
void main_24546C(void);
void main_245478(long v);
void main_2454BC(LGRect *r, uchar outer, uchar lines, uchar inner);
void main_245600(LGRect *r, uchar outer, uchar lines, uchar inner);
uchar main_245788(uchar *ev);
void main_2458C4(void *text);
long main_246094(void *res);                     /* 0x246094 */
void main_2461B0(void);
long main_2462BC(void);
uchar main_2462D8(uchar *pal, long start, long n, long r, long g, long b, uchar *close);

/* ------------------------------------------------------------------ view helpers (M103) */
extern long game_44135D;                 /* 0x44135D */
void mis103_2C9130(void);
int mis103_2C9180(int kind, long val, uchar *changed);         /* 0x2C9180 */

/* ------------------------------------------------------------------ M029 playkey.c */
/* movement key -> aircraft control table (0x35B344), 8 bytes, ended by mask 0 */
typedef struct {
    ushort mask;            /* +0 movement key bits (play_2765F4) */
    long val;               /* +2 control value handed to damage_27C8B8 */
    ushort clear;           /* +6 bits taken out once matched */
} PlayMove;
LG_STATIC_ASSERT(sizeof(PlayMove) == 8, PlayMove_size);
extern uchar play_move_keys[];          /* 0x35B338 scan codes of the movement keys (0-ended) */
extern PlayMove play_move_table[];      /* 0x35B344 */
extern long play_move_size;             /* 0x35B3B4 */
extern long play_move_default;          /* 0x35B3B8 control value with no key */
extern ushort play_last_bits;           /* 0x39025A keys of the last frame with ctrl held */
extern LGRect play_pause_rect_tmpl;     /* 0x35B330 */
extern LGRect play_pause_rect;          /* 0x390250 */
extern char play_str_alt5[], play_str_alt6[], play_str_alt7[];   /* 0x355904 / 0C / 14 */

void play_2759F0(char *text);
uchar play_27696C(ulong code);
void play_2765F4(void);                             /* 0x2765F4 (M029) */
uchar PlayKeyHandler(void *ev, void *data);                          /* 0x275AB4 */

#pragma pack(pop)
#endif
