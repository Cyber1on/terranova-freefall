/* instcopy.c -- resource cache: copy passes and their progress text, module M124
 * (Terra Nova v1.09, __FF.EXE).
 *
 *   0x2DCD80 install_2DCD80  set up the progress text (no callers)
 *   0x2DCDCC install_2DCDCC  next text line (wraps to a second column)
 *   0x2DCDF8 install_2DCDF8  progress tick of a data copy
 *   0x2DCEA4 install_2DCEA4  progress tick of the speech copy (one block per voice)
 *   0x2DD05C install_2DD05C  copy the mission's radio speech files (TARG_DIR\SPEECH)
 *   0x2DD344 install_2DD344  copy one data file (TARG_DIR\DATA)
 *   0x2DD5C0 install_2DD5C0  a copy pass: 0 mission start, 1 shell, 2 exit (clean up)
 *   0x2DD78C install_2DD78C  script native: swap resbase.res for restnobj.res
 *   0x2DD828 install_2DD828  script native: swap restnobj.res for resbase.res
 *   0x2DD8C8 install_2DD8C8  check the free space in TARG_DIR
 */
#include "g9_b.h"
#include "g9_c.h"     /* added at the G9/G10 merge */

#define FONT_STRING 315                 /* canvas table: string in a font */
#define DRAW_CLEAR 8

static void prog_text(char *s)
{
    WATCOM_CALL(grd_canvas_table[FONT_STRING], (long)install_font, (long)s,
                (long)install_prog.x, (long)install_prog.y);
}

void install_2DCD80(long total, long divisor, schar nsteps, long base)
{
    install_prog.unused = 0;
    install_prog.count = 0;
    install_prog.base = base;
    install_prog.period = (short)(total / divisor / (nsteps - 1));
    install_prog.nsteps = nsteps;
    install_prog.step = 0;
    install_prog.spk = 0;
}

void install_2DCDCC(void)
{
    short y = (short)(install_prog.y + 14);
    if (y > 0x1b8) {
        install_prog.x = 0x15e;
        y = 0x32;
    }
    install_prog.y = y;
}

void install_2DCDF8(void)
{
    long count = install_prog.count, period = install_prog.period, q;

    if (count % period == 0) {
        q = count / period;
        if ((ulong)q < (ulong)(long)install_prog.nsteps) {
            prog_text(RefGet(q + install_prog.base));
            install_2DCDCC();
        }
    }
    install_prog.count++;
}

void install_2DCEA4(void)
{
    char buf[0x3c];
    GameSpeaker *s;
    char *voice, *who, *fmt;

    if (install_prog.count % install_prog.period == 0) {
        if (install_prog.step % 9 != 0) {
            prog_text(RefGet(install_prog.base + install_prog.step));
            install_prog.step++;
        } else {
            if (install_prog.spk >= install_prog.nspk)
                return;
            install_2DCDCC();
            install_prog.step = 1;
            s = &game_391E50[install_prog.spk];
            voice = RefGet(0x16c0000 + (uchar)s->voice);
            who = RefGet(0x1c1001d + s->g_08[0]);
            fmt = RefGet(0x1c10001);
            lg_sprintf(buf, fmt, who, voice);
            install_prog.spk++;
            prog_text(buf);
        }
        install_2DCDCC();
    }
    install_prog.count++;
}

/* Runs the copies a file list has started; tick is the progress function (or 0). */
static void copy_list(InstallFile *list, uchar show, void (*tick)(void))
{
    InstallJob *job;
    long i, r;

    for (i = 1; i <= list[0].size; i++) {
        if (install_2DCB60(&job, list[i].name) || !job)
            continue;
        while (job->left != 0) {
            r = install_2DC7C8(job);
            if (show)
                tick();
            if (r < 0)
                break;
        }
        if (job->left == 0)
            install_2DC88C(job, 0);
    }
}

void install_2DD05C(schar nvoices, uchar show)
{
    char cd[0x80], targ[0x80];
    InstallFile list[4];
    long total, i, q;
    schar n, steps;

    if (nvoices == 1)
        return;
    if (!CfgLookup("TARG_DIR", targ, 0x80))
        return;
    g1c_strcat(targ, "\\SPEECH");
    if (!CfgLookup("CD_DIR", cd, 0x80))
        return;
    g1c_strcat(cd, "\\SPEECH");
    if (strcmp(cd, targ) == 0)
        return;
    install_2DCABC(cd, targ);
    memset(list, 0, sizeof(list));
    list[0].name = "resbrk*.res";
    list[0].size = nvoices ? nvoices - 1 : 0;
    for (i = 1; i < nvoices; i++)
        list[i].name = radio_file_names[(schar)radio_voice_class(game_391E50[i].voice)];
    total = install_2DCC24(list);
    if (show) {
        n = game_359708 == 0x1d ? 1 : nvoices;
        install_prog.nspk = n;
        steps = (schar)(n * 9);
        q = total / 0x10000;
        install_prog.unused = 0;
        install_prog.count = 0;
        install_prog.period = (short)(q / (steps - 1));
        install_prog.base = 0x1c10001;
        install_prog.nsteps = steps;
        install_prog.step = 0;
        install_prog.spk = 0;
        prog_text(RefGet(0x1c10000));
        install_2DCDCC();
    }
    copy_list(list, show, install_2DCEA4);
}

void install_2DD344(char *name, uchar copy, long base)
{
    char targ[0x80], cd[0x80];
    InstallFile list[2];
    long total;

    if (!CfgLookup("TARG_DIR", targ, 0x80))
        return;
    g1c_strcat(targ, "\\DATA");
    if (!CfgLookup("CD_DIR", cd, 0x80))
        return;
    g1c_strcat(cd, "\\DATA");
    if (strcmp(cd, targ) == 0)
        return;
    install_2DCABC(cd, targ);
    memset(list, 0, sizeof(list));
    list[0].name = name;
    if (copy) {
        list[0].size = 1;
        list[1].name = name;
    } else
        list[0].size = 0;
    total = install_2DCC24(list);
    if (base) {
        if (install_prog.x != 0x15e && base != 0x1c10013) {
            install_prog.x = 0x15e;
            install_prog.y = 0x32;
        } else
            install_2DCDCC();
        install_prog.base = base;
        install_prog.unused = 0;
        install_prog.count = 0;
        install_prog.nsteps = 9;
        install_prog.period = (short)(total / 0x10000 / 8);
        install_prog.step = 0;
        install_prog.spk = 0;
    }
    copy_list(list, base != 0, install_2DCDF8);
}

void install_2DD5C0(long mode)
{
    uchar first = game_install_level == 0;
    G9bFrame *f;
    uchar *pal;
    void *buf;

    if (mode == 1 && !first)
        return;
    if (game_install_level == 2)
        return;
    if (game_install_level == 1 && mode == 0 && game_391F6E == 1)
        return;
    install_font = ResLock(0x167);
    if (mode != 2) {
        install_prog.x = 0x3c;
        install_prog.y = 0x32;
        WATCOM_CALL(grd_canvas_table[DRAW_CLEAR], 0);
        f = RefGet(0x1c00000);
        if (f->pall_off) {
            pal = (uchar *)ResGet(0x1c0) + f->pall_off;
            gr_set_pal(*(short *)pal, *(short *)(pal + 2), pal + 4);
        }
        grd_canvas->gc.fcolor = 0xf2;
        f = RefGet(0x1c00000);
        f->bm.bits = f->data;
        WATCOM_CALL(grd_canvas_table[0x8F + 2 * f->bm.type], (long)f, 0, 0);
    }
    buf = ffmem_2C4F40(0x10000);
    install_2DC5B0(buf, 0x10000);
    install_35F3C8 = 0;
    if (mode == 0) {
        if (game_install_level < 2)
            install_2DD05C(game_391F6E, 1);
        if (first) {
            install_2DD344("resbase.res", 0, 0);
            install_2DD344("restnobj.res", 1, 0x1c1000a);
        }
    } else if (mode == 1) {
        if (first) {
            install_2DD344("restnobj.res", 0, 0);
            install_2DD344("resbase.res", 1, 0x1c10013);
        }
    } else {
        if (game_install_level < 2)
            install_2DD05C(0, 0);
        if (first) {
            install_2DD344("resbase.res", 0, 0);
            install_2DD344("restnobj.res", 0, 0);
        }
    }
    ffmem_2C4FE4(buf);
    install_2DC5B0(0, 0);
    ResUnlock(0x167);
}

static void drop_cursor(void)
{
    MouseHide();
    if (tnm_cursor_ref) {
        ResUnlock((Id)((ulong)tnm_cursor_ref >> 16));
        tnm_cursor_ref = 0;
    }
}

static void reopen_game(void)
{
    if (game_low_mem) {
        ResCloseFile(res_game_file);
        res_game_file = ResOpenResFile("resgame.res", 0, 0);
    }
}

void install_2DD78C(void)
{
    drop_cursor();
    ResCloseFile(res_base_file);
    reopen_game();
    install_2DD5C0(0);
    res_tnobj_file = ResOpenResFile("restnobj.res", 0, 0);
    if (res_tnobj_file < 0)
        DbgReportError(1, "Can't open restnobj.res!\n");
    MouseSetCursor(0x16b0001);
    kb_flush();
    MouseShow();
}

void install_2DD828(void)
{
    drop_cursor();
    ResCloseFile(res_tnobj_file);
    reopen_game();
    install_2DD5C0(1);
    res_base_file = ResOpenResFile("resbase.res", 0, 0);
    if (res_base_file < 0)
        DbgReportError(1, "Can't open resbase.res!\n");
    MouseSetCursor(0x16b0001);
    kb_flush();
    MouseShow();
}

void install_2DD8C8(schar level)
{
    char dir[0x80];
    long need;

    if (!CfgLookup("TARG_DIR", dir, 0x80))
        return;
    if (level == 2)
        need = 0x200;
    else if (level == 1)
        need = 0x1400;
    else
        need = 0x5000;
    if (need > install_2DDCC4(-1, dir))
        DbgReportError(1, "There is not enough HD Space. Terra Nova needs %d KB free.\n", need);
}
