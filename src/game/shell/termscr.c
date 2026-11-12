/* termscr.c -- M097 part 1 (0x2B6A90-0x2BCA80): the shell's respterm.res screens.
 *
 * The shell (between missions) shows screens numbered 0..28 (shell_screen); shell_2B6EB4
 * enters one: background + title (shell_2B6CDC), the screen's init function from
 * shell_scr_init[] draws it and defines buttons (uiwidget_2B5F68 / 2B60DC / 2B61D8), and the
 * screen's mouse handler from shell_scr_mouse[] wakes the shell script thread with a code when a
 * button is pressed (uiwidget_2B60B4).  This part holds the framework, the main briefing
 * room (screen 0), the soldier / weapon / equipment lists (1..5) and their record pages (6..10),
 * the mission map (11), ...  Screens 18..28 are g10b's (M097 0x2BCA80-0x2C3440).
 *
 * Quirks kept:
 *  - shell_2B6A90 clears bytes 0x43D2F9..0x43D2F7+count after building the soldier list at
 *    0x43D2FC: with more than 4 soldiers the first entries of the list are zeroed again;
 *  - shell_2B6CDC: the background of screen 0x1C gets bits = bm + screen (= + 0x1C as well);
 *  - shell_2B7820's up / down buttons wake the shell with shell_screen;
 */
#include "g10_a.h"
#include "g1_c.h"      /* g1c_strcpy / g1c_strcat */
#include "g10_b.h"     /* g10b: list helpers, header, squad strip */

#ifndef TN_LINK_ORIGINAL_DATA
long shell_resfile = -1;
long shell_35CD94;
LGRect shell_rects[5] = {
    { { 0x4A, 0x7E }, { 0x230, 0x98 } }, { { 0x4A, 0x9C }, { 0x230, 0xB6 } },
    { { 0x4A, 0xD8 }, { 0x230, 0xF2 } }, { { 0x4A, 0xBA }, { 0x230, 0xD4 } },
    { { 0x4A, 0xF6 }, { 0x230, 0x110 } },
};
#define P(x, y) ((x) | ((long)(y) << 16))
long shell_pos[13] = {
    P(400, 408), P(400, 408), P(400, 408), P(400, 408), P(400, 408), P(20, 410), P(204, 410),
    P(392, 410), P(504, 410), P(424, 57), P(424, 96), P(424, 140), P(424, 176),
};
#undef P
LGPoint shell_pt[12] = {
    { 40, 72 }, { 40, 38 }, { 40, 280 }, { 40, 72 }, { 40, 38 }, { 40, 240 },
    { 40, 72 }, { 40, 38 }, { 40, 320 }, { 40, 72 }, { 40, 38 }, { 40, 280 },
};
uchar shell_43D2F8[16];
void *shell_handler;
char *shell_text;
grs_font *shell_font;
short shell_43D45A, shell_43D45C, shell_43D45E, shell_43D460, shell_43D464, shell_43D466;
short shell_count, shell_43D46A, shell_sel, shell_screen, shell_43D470, shell_font_id, shell_43D474;
uchar shell_43D476[13];
uchar shell_43D483, shell_43D484;
#endif

#define DRAW_RECT 16
#define DRAW_BOX 18
#define FONT_STRING 315

static void draw_bitmap(grs_bitmap *bm, long x, long y)
{
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, x, y);
}

/* a FrameDesc resource: bits follow the 0x1C-byte header */
static void draw_frame(Ref ref, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);

    bm->bits = (uchar *)bm + 0x1C;
    draw_bitmap(bm, x, y);
}

static void font_string(grs_font *f, char *s, long x, long y)
{
    WATCOM_CALL(grd_canvas_table[FONT_STRING], (long)f, (long)s, x, y);
}

static void fill_rect(long x0, long y0, long x1, long y1)
{
    WATCOM_CALL(grd_canvas_table[DRAW_RECT], x0, y0, x1, y1);
}

static void click(ushort id)
{
    SndPlay(id, 0, 0x40, 1, -1, 0x40, 0);
}

/* the palette of a FrameDesc resource (offset at +0x18 into resource id) */
static void frame_pal(Ref ref, Id id)
{
    uchar *bm = RefGet(ref), *p;

    if (*(long *)(bm + 0x18)) {
        p = (uchar *)ResGet(id) + *(long *)(bm + 0x18);
        gr_set_pal(*(short *)p, *(short *)(p + 2), p + 4);
    }
}

static long pt(long x, long y)
{
    return (ushort)x | (y << 16);
}

static long ev_pt(TnEvent *ev)
{
    return *(long *)&ev->pt;
}

/* ------------------------------------------------------------------ soldier list */

void shell_2B6A90(void)
{
    short count = shell_count;
    long grp, k, n = 0, i;

    for (grp = 0; grp < 6; grp++)
        for (k = 1; k < 13; k++)
            if (camp_soldiers[k][5] == grp) {
                shell_43D476[k] = (uchar)n;
                shell_soldiers[n] = (schar)k;
                n++;
            }
    for (i = 1; i < count; ) {
        i++;
        shell_43D2F8[i - 1] = 0;
    }
    shell_count = count;
}

uchar shell_2B6B00(long i)
{
    return camp_soldiers[shell_soldiers[(schar)i]][5];
}

/* 3 if soldier i of the list is a squad member, else its camp status */
uchar shell_2B6B24(long i)
{
    short count = shell_count;
    long k;

    for (k = 1; k < count; k++)
        if (shell_soldiers[(schar)i] == voice_soldier[(uchar)game_391E50[k].voice]) {
            shell_count = count;
            return 3;
        }
    shell_count = count;
    return camp_status[shell_soldiers[(schar)i]];
}

Ref shell_2B6B94(long i)
{
    return 0x16C0000 + camp_soldiers[shell_soldiers[(schar)i]][0];
}

ushort shell_2B6BC0(Ref r)
{
    return ((grs_bitmap *)RefGet(r))->w;
}

ushort shell_2B6BCC(Ref r)
{
    return ((grs_bitmap *)RefGet(r))->h;
}

/* ------------------------------------------------------------------ framework */

/* native 73: open respterm.res */
void shell_2B6BD8(void)
{
    shell_resfile = ResOpenResFile("respterm.res", 0, 0);
    shell_text = (char *)disp_blend_buf;
    game_43D318 = 2;
    shell_font_id = 0x168;
    shell_font = ResLock(0x168);
    shell_2B6A90();
}

/* native 74: close it */
void shell_2B6C28(void)
{
    ResUnlock(0x168);
    game_43D318 = 4;
    if (shell_resfile >= 0) {
        ResCloseFile(shell_resfile);
        shell_resfile = -1;
    }
    if (shell_text)
        shell_text = NULL;
}

/* native 80 */
long shell_2B6C74(void)
{
    short v;

    shell_count = game_359708 == 0x1D ? 1 : game_391F6E;
    v = game_391F6E != 1;
    shell_43D466 = v;
    shell_43D46A = v;
    shell_43D45E = 0;
    shell_43D470 = game_359708 == 0x27 ? 0x19 : 0xB;
    return shell_43D470;
}

/* background and title of a screen */
void shell_2B6CDC(long screen)
{
    char *s;
    grs_bitmap *bm;
    grs_font *f;

    uiwidget_2B5F44();
    gr_set_canvas(&tnui_back_canvas);
    if (screen == 0x1C) {
        bm = RefGet(0xCF0000);
        bm->bits = (uchar *)bm + screen;
        draw_bitmap(bm, 0, 0);
    } else if (screen == 0x1B)
        draw_frame(0xCE0000, 0, 0);
    else if (shell_35CD94)
        draw_frame(0xCB0000, 0, 0);
    else if (game_35CD9C) {
        ResLock(0xCC);
        draw_frame(0xCC0000, 0, 0);
        ResUnlock(0xCC);
    } else
        draw_frame(0x1C00000, 0, 0);
    s = RefLock(screen + 0x5AB0000);
    if (*s) {
        bm = RefGet(0xD20000);
        bm->bits = (uchar *)bm + 0x1C;
        draw_bitmap(bm, (short)((0x280 - bm->w) / 2), 0x1C);
        f = ResLock(0x165);
        grd_canvas->gc.fcolor = 0xF2;
        GfxDrawText(0x165, s, (0x280 - (short)gr_font_string_width(f, s)) / 2, 0x25);
        ResUnlock(0x165);
    }
    ResUnlock((ushort)((ulong)(screen + 0x5AB0000) >> 16));
}

/* native 75: enter a screen */
void shell_2B6EB4(long screen)
{
    MouseHide();
    shell_screen = (short)screen;
    weapons_28F020();
    uiwidget_2B6098();
    shell_2B6CDC(shell_screen);
    WATCOM_CALL(shell_scr_init[shell_screen]);
    shell_handler = shell_scr_mouse[shell_screen];
    MouseAddHandler(&tnui_full_rect, 0xFF, shell_handler);
    KeyAddHandler(shell_handler, (void *)0x1B, 0, 0);
    uiwidget_2B60CC();
    MouseShow();
}

/* native 76: leave it */
void shell_2B6F34(void)
{
    MouseRemoveHandler(&tnui_full_rect, 0xFF, shell_handler);
    KeyRemoveHandler(shell_handler, (void *)0x1B);
    uiwidget_2B60A8();
}

/* ------------------------------------------------------------------ screen 0: briefing room */

uchar shell_2B6F64(TnEvent *ev)
{
    long n;

    if (ev->type != 1)
        return 0;
    n = uiwidget_2B601C(ev_pt(ev));
    if (!n)
        return 1;
    if (n <= 6) {
        shell_43D460 = shell_screen;
        return shell_2C06D8(n);
    }
    click(0x787);
    uiwidget_2B60B4(n - 6);
    return 1;
}

void shell_2B6FCC(void)
{
    char buf[0x28];
    long i, w;
    short y;

    if (shell_35CD94)
        frame_pal(0xCB0000, 0xCB);
    else
        frame_pal(0x1C00000, 0x1C0);
    uiwidget_2B5C90();
    draw_frame(0xD2000C, 0x28, 0x78);
    w = ((grs_bitmap *)RefGet(0xD2000C))->w;
    y = 0x82;
    for (i = 0; i < 5; i++) {
        g1c_strcpy(buf, RefGet(0x5AB00C7 + i));
        font_string(shell_font, buf, (short)((w - (short)gr_font_string_width(shell_font, buf)) / 2 + 0x28), y);
        y += 0x1E;
    }
    uiwidget_2B63D8(7, &shell_rects[0]);
    uiwidget_2B63D8(8, &shell_rects[1]);
    uiwidget_2B63D8(9, &shell_rects[3]);
    uiwidget_2B63D8(10, &shell_rects[2]);
    uiwidget_2B63D8(11, &shell_rects[4]);
    shell_43D470 = 0;
    shell_2C0418();
}

/* ------------------------------------------------------------------ screens 1..5: lists */

uchar shell_2B7178(TnEvent *ev)
{
    long n, code;

    if (ev->type != 1)
        return 0;
    n = uiwidget_2B601C(ev_pt(ev));
    if (n) {
        if (n > shell_43D474) {
            click(0x789);
            code = shell_43D470;
        } else {
            click(0x788);
            shell_sel = (short)(n - 1);
            code = shell_43D45C;
        }
        uiwidget_2B60B4(code);
    }
    return 1;
}

/* screen 1: the 13 soldiers */
void shell_2B7208(void)
{
    long k;
    short x, y;

    shell_43D45C = 6;
    shell_43D474 = 0xD;
    shell_43D464 = 0;
    for (k = 0; k < 13; k++) {
        x = (short)(k / 7 * 0x118 + 0x28);
        y = (short)(k % 7 * 0x27 + 0x48);
        uiwidget_2B60DC(k + 1, 0xD20003, uiw_font, pt(x, y), RefGet(0x16C0062 + camp_soldiers[k][0]), 0xF2);
        draw_frame(0x5B80000 + camp_soldiers[k][6], (short)(x + 0x1A), (short)(y + 2));
        draw_frame(0x5B90000 + camp_soldiers[k][5], (short)(x + 0xE6), (short)(y + 2));
    }
    uiwidget_2B60DC(0xE, 0xD20001, shell_font, shell_pos[0],
                    RefGet(shell_43D484 ? 0x5AB0026 : 0x5AB001D), 0xF2);
}

/* the two-column list of screens 2..4: n entries, names from res(i) */
static void two_columns(long n, long hh, Ref (*res)(long i))
{
    long half = (n + 1) / 2, i;
    short x;

    for (i = 0; i < n; i++) {
        x = i < half ? 0x28 : 0x148;
        uiwidget_2B60DC(i + 1, 0xD20002, shell_font, pt(x, (short)(i % half * hh + 0x6E)), RefGet(res(i)), 0xF2);
    }
}

static Ref res_a(long i) { return 0x16E0000 + camp_sel_a[i]; }
static Ref res_b(long i) { return 0x16E0030 + camp_sel_b[i]; }
static Ref res_c(long i) { return 0x5AB0110 + camp_sel_c[i]; }

/* screen 2: weapons reached */
void shell_2B7370(void)
{
    long n = camp_done[0], hh;

    shell_43D45C = 7;
    shell_43D474 = (short)n;
    shell_43D464 = 0;
    hh = ((grs_bitmap *)RefGet(0xD20002))->h + 2;
    frame_pal(0x1C00000, 0x1C0);
    uiwidget_2B5C90();
    two_columns(n, hh, res_a);
    uiwidget_2B60DC(0x10, 0xD20001, shell_font, shell_pos[1], RefGet(0x5AB001D), 0xF2);
}

/* screen 3 */
void shell_2B749C(void)
{
    long n = camp_done[1], hh;

    shell_43D45C = 8;
    shell_43D464 = 0;
    shell_43D474 = (short)n;
    hh = ((grs_bitmap *)RefGet(0xD20002))->h + 2;
    two_columns(n, hh, res_b);
    uiwidget_2B60DC(0xD, 0xD20001, shell_font, shell_pos[2], RefGet(0x5AB001D), 0xF2);
}

/* screen 4 */
void shell_2B7598(void)
{
    long n = camp_done[2], hh;

    shell_43D45C = 9;
    shell_43D464 = 0;
    shell_43D474 = (short)n;
    hh = ((grs_bitmap *)RefGet(0xD20002))->h + 2;
    two_columns(n, hh, res_c);
    uiwidget_2B60DC(0xD, 0xD20001, shell_font, shell_pos[3], RefGet(0x5AB001D), 0xF2);
}

/* entry i of list 0x5B6 is reached by the campaign */
uchar shell_2B7694(long i)
{
    return camp_level[*(schar *)RefGet(0x5B60000 | i)] <= camp_pos;
}

/* screen 5: the 19 entries of list 0x5B6 that are reached */
void shell_2B76BC(void)
{
    long hh, n = 0, i, j, half;
    short x;

    shell_43D45C = 0xA;
    shell_43D474 = 0x13;
    shell_43D464 = 0;
    hh = ((grs_bitmap *)RefGet(0xD20002))->h + 2;
    for (i = 0; i < 0x13; i++)
        if (camp_level[*(schar *)RefGet(0x5B60000 | i)] <= camp_pos)
            n++;
    half = (n + 1) / 2;
    for (i = 0, j = 0; i < 0x13; i++)
        if (camp_level[*(schar *)RefGet(0x5B60000 | i)] <= camp_pos) {
            x = j < half ? 0x28 : 0x148;
            uiwidget_2B60DC(i + 1, 0xD20002, shell_font, pt(x, (short)(hh * (j % half) + 0x6E)),
                            RefGet(0x5AB002F + i), 0xF2);
            j++;
        }
    uiwidget_2B60DC(0x14, 0xD20001, shell_font, shell_pos[4], RefGet(0x5AB001D), 0xF2);
}

/* ------------------------------------------------------------------ screens 6..10: records */

/* screen 6 (soldier record) */
uchar shell_2B7820(TnEvent *ev)
{
    long n;

    if (ev->type != 1)
        return 0;
    n = uiwidget_2B601C(ev_pt(ev));
    if (!n)
        return 1;
    switch (n - 1) {
    case 0:
        if (shell_sel < 0xC) {
            click(0x78A);
            shell_sel++;
        } else
            click(0x785);
        uiwidget_2B60B4(shell_screen);
        break;
    case 1:
        if (shell_sel > 0) {
            click(0x78A);
            shell_sel--;
        } else
            click(0x785);
        uiwidget_2B60B4(shell_screen);
        break;
    case 2:
        click(0x78F);
        uiwidget_2B60B4(1);
        break;
    case 3:
        click(0x789);
        shell_43D45E = 0;
        uiwidget_2B60B4(shell_43D470);
        break;
    }
    return 1;
}

/* screen 6: the record of soldier shell_sel */
void shell_2B7978(void)
{
    char buf[0x200];
    uchar *rec = camp_soldiers[shell_sel];
    schar id = (schar)rec[0];
    uchar *p, a, b, c;
    long k, n, w;

    draw_frame(0xD30000 | (long)id, 0x28, 0x30);
    grd_canvas->gc.fcolor = 0x17;
    lg_sprintf(buf, RefLock(0x5AB0099), 0x5AB00A5 + camp_soldiers[shell_sel][1]);
    ResUnlock(0x5AB);
    font_string(uiw_font, buf, 0x30, 0xB4);
    p = ResLock(0x5B7);
    p += *(long *)(p + 2) + id * 3;
    a = p[0];
    b = p[1];
    c = p[2];
    ResUnlock(0x5B7);
    grd_canvas->gc.fcolor = 0xF2;
    font_string(uiw_font, RefGet(0x5AB009A), 0xB6, 0x30);
    lg_sprintf(buf, "%S\n%S\n%d\n%S\n%S\n%d\n%S", id + 0x16C0062, id + 0x16C0021, (long)a,
               id + 0x16C0083, 0x5AB00BA + rec[6], (long)b, 0x16E0068 + rec[5]);
    grd_canvas->gc.fcolor = 0x17;
    font_string(uiw_font, buf, 0xFA, 0x30);
    grd_canvas->gc.fcolor = 0xF2;
    g1c_strcpy(buf, RefGet(0x5AB009B));
    font_string(uiw_font, buf, 0xB6, 0xA0);
    w = gr_font_string_width(uiw_font, buf);
    lg_sprintf(buf, RefGet(0x5AB009C), (long)c);
    grd_canvas->gc.fcolor = 0x17;
    font_string(uiw_font, buf, (short)(w + 0xBC), 0xA0);
    grd_canvas->gc.fcolor = 0xF2;
    font_string(uiw_font, RefGet(0x5AB009D), 0x177, 0x30);
    n = 0;
    buf[0] = 0;
    for (k = 0; k < 8; k++)
        if (rec[8 + k]) {
            if (buf[0])
                g1c_strcat(buf, "\n");
            lg_sprintf(buf + strlen(buf), "%S", 0x5AB00B2 + k);
            if (rec[8 + k] > 1)
                lg_sprintf(buf + strlen(buf), " (%d)", (long)rec[8 + k]);
            n++;
        }
    if (!n)
        g1c_strcpy(buf, RefGet(0x5AB009E));
    grd_canvas->gc.fcolor = 0x17;
    font_string(uiw_font, buf, 0x177, 0x43);
    grd_canvas->gc.fcolor = 0x17;
    g1c_strcpy(shell_text, RefGet(0x5AB0079 + id));
    gr_font_string_wrap(uiw_font, shell_text, 0x230);
    font_string(uiw_font, shell_text, 0x28, 0xD8);
    uiwidget_2B60DC(1, 0xD20001, shell_font, shell_pos[5], RefGet(0x5AB001E), shell_sel < 0xC ? 0xF2 : 0x48);
    uiwidget_2B60DC(2, 0xD20001, shell_font, shell_pos[6], RefGet(0x5AB001F), shell_sel > 0 ? 0xF2 : 0x48);
    uiwidget_2B60DC(3, 0xD20004, shell_font, shell_pos[7], RefGet(0x5AB0020), 0xF2);
    uiwidget_2B60DC(4, 0xD20004, shell_font, shell_pos[8], RefGet(0x5AB001D), 0xF2);
}

/* screens 7..10 (records of the list entries): next / previous / list / back */
uchar shell_2B7EC8(TnEvent *ev)
{
    long n, b;

    if (ev->type != 1)
        return 0;
    n = uiwidget_2B601C(ev_pt(ev));
    if (!n)
        return 1;
    switch (n - 1) {
    case 0:
        if (shell_sel >= shell_43D474 - 1)
            break;
        if (shell_screen != 0xA) {
            click(0x78A);
            shell_sel++;
            uiwidget_2B60B4(shell_screen);
            return 1;
        }
        for (b = shell_sel + 1; b < shell_43D474 && !shell_2B7694(b); b++)
            ;
        if (b >= shell_43D474)
            break;
        shell_sel = (short)b;
        click(0x78A);
        uiwidget_2B60B4(shell_screen);
        return 1;
    case 1:
        if (shell_sel <= shell_43D464)
            break;
        if (shell_screen != 0xA) {
            click(0x78A);
            shell_sel--;
            uiwidget_2B60B4(shell_screen);
            return 1;
        }
        for (b = shell_sel - 1; b >= 0 && !shell_2B7694(b); b--)
            ;
        if (b < 0)
            break;
        shell_sel = (short)b;
        click(0x78A);
        uiwidget_2B60B4(shell_screen);
        return 1;
    case 2:
        click(0x78F);
        uiwidget_2B60B4(shell_screen - 5);
        return 1;
    case 3:
        click(0x789);
        uiwidget_2B60B4(shell_43D470);
        return 1;
    default:
        return 1;
    }
    click(0x785);
    return 1;
}

/* a picture framed in colour 0xF2 at p */
static void framed(Ref ref, LGPoint p)
{
    grd_canvas->gc.fcolor = 0xF2;
    fill_rect((short)(p.x - 1), (short)(p.y - 1), (short)(shell_2B6BC0(ref) + p.x + 1),
              (short)(shell_2B6BCC(ref) + p.y + 1));
    draw_frame(ref, p.x, p.y);
}

/* screen 7: weapon record */
void shell_2B80F0(void)
{
    char buf[0x78];
    schar id = camp_sel_a[shell_sel];
    Ref ref = 0xD40000 | (long)id;
    uchar *o = (uchar *)ord_table + id * 0x24;
    ushort v;

    frame_pal(0xD40000, 0xD4);
    uiwidget_2B5C90();
    framed(ref, shell_pt[0]);
    grd_canvas->gc.fcolor = 0x17;
    lg_sprintf(buf, RefGet(0x5AB0185), (long)*(ushort *)(o + 0xE) * 6);
    font_string(shell_font, buf, 0x28, 0xB4);
    lg_sprintf(buf, RefGet(0x5AB0187), RefGet(0x5AB0149 + id));
    font_string(shell_font, buf, 0x28, 0xC6);
    lg_sprintf(buf, RefGet(0x5AB0186), (long)(o[1] != 0) + 1);
    font_string(shell_font, buf, 0x28, 0xD8);
    v = *(ushort *)(o + 0xA);
    if (v > 1) {
        lg_sprintf(buf, RefGet(0x5AB0189), (long)v);
        font_string(shell_font, buf, 0x28, 0xEA);
    }
    g1c_strcpy(shell_text, RefGet(0x5AB0042 + camp_sel_a[shell_sel]));
    gr_font_string_wrap(shell_font, shell_text, 0x21C);
    font_string(shell_font, shell_text, shell_pt[2].x, shell_pt[2].y);
    grd_canvas->gc.fcolor = 0xF2;
    font_string(shell_font, RefGet(0x16E0000 + camp_sel_a[shell_sel]), shell_pt[1].x, shell_pt[1].y);
    uiwidget_2B60DC(1, 0xD20001, shell_font, shell_pos[10], RefGet(0x5AB00CC), shell_sel < camp_done[0] - 1 ? 0xF2 : 0x48);
    uiwidget_2B60DC(2, 0xD20001, shell_font, shell_pos[9], RefGet(0x5AB00D0), shell_sel != 0 ? 0xF2 : 0x48);
    uiwidget_2B60DC(3, 0xD20001, shell_font, shell_pos[11], RefGet(0x5AB00D4), 0xF2);
    uiwidget_2B60DC(4, 0xD20001, shell_font, shell_pos[12], RefGet(0x5AB001D), 0xF2);
}

/* screen 8 */
void shell_2B84F8(void)
{
    char buf[0x28];
    schar id = camp_sel_b[shell_sel];
    Ref ref = 0xD50000 | (long)id;

    framed(ref, shell_pt[3]);
    g1c_strcpy(shell_text, RefGet(0x5AB0051 + id));
    gr_font_string_wrap(shell_font, shell_text, 0x21C);
    grd_canvas->gc.fcolor = 0x17;
    font_string(shell_font, shell_text, shell_pt[5].x, shell_pt[5].y);
    if ((1 << (id & 31)) & ai_fire_mask) {
        lg_sprintf(buf, RefGet(0x5AB0158 + id), unit_tab[id].f0);
        font_string(shell_font, buf, 0x28, 0xB4);
    }
    grd_canvas->gc.fcolor = 0xF2;
    font_string(shell_font, RefGet(0x16E0030 + id), shell_pt[4].x, shell_pt[4].y);
    uiwidget_2B60DC(1, 0xD20001, shell_font, shell_pos[10], RefGet(0x5AB00CD), shell_sel < camp_done[1] - 1 ? 0xF2 : 0x48);
    uiwidget_2B60DC(2, 0xD20001, shell_font, shell_pos[9], RefGet(0x5AB00D1), shell_sel != 0 ? 0xF2 : 0x48);
    uiwidget_2B60DC(3, 0xD20001, shell_font, shell_pos[11], RefGet(0x5AB00D5), 0xF2);
    uiwidget_2B60DC(4, 0xD20001, shell_font, shell_pos[12], RefGet(0x5AB001D), 0xF2);
}

/* screen 9 */
void shell_2B87BC(void)
{
    char buf[0x28];
    long id = camp_sel_c[shell_sel];
    Ref ref = 0xD80000 | id;
    uchar *t = game_pba_types[id];

    framed(ref, shell_pt[6]);
    g1c_strcpy(shell_text, RefGet(0x5AB005D + camp_sel_c[shell_sel]));
    gr_font_string_wrap(shell_font, shell_text, 0x21C);
    grd_canvas->gc.fcolor = 0x17;
    font_string(shell_font, shell_text, shell_pt[8].x, shell_pt[8].y);
    lg_sprintf(buf, RefGet(0x5AB018A), RefGet(0x5AB0170 + id));
    font_string(shell_font, buf, 0x10E, 0xC8);
    lg_sprintf(buf, RefGet(0x5AB018B), (long)*(short *)(t + 0x13), (long)*(short *)(t + 0xF));
    font_string(shell_font, buf, 0x10E, 0xDA);
    lg_sprintf(buf, RefGet(0x5AB018C), RefGet(0x5AB0174 + id));
    font_string(shell_font, buf, 0x10E, 0xEC);
    lg_sprintf(buf, RefGet(0x5AB018D), RefGet(0x5AB0178 + id));
    font_string(shell_font, buf, 0x10E, 0xFE);
    lg_sprintf(buf, RefGet(0x5AB018E), RefGet(0x5AB017C + id));
    font_string(shell_font, buf, 0x10E, 0x110);
    grd_canvas->gc.fcolor = 0xF2;
    font_string(shell_font, RefGet(0x5AB0110 + camp_sel_c[shell_sel]), shell_pt[7].x, shell_pt[7].y);
    uiwidget_2B60DC(1, 0xD20001, shell_font, shell_pos[10], RefGet(0x5AB00CE), shell_sel < camp_done[2] - 1 ? 0xF2 : 0x48);
    uiwidget_2B60DC(2, 0xD20001, shell_font, shell_pos[9], RefGet(0x5AB00D2), shell_sel != 0 ? 0xF2 : 0x48);
    uiwidget_2B60DC(3, 0xD20001, shell_font, shell_pos[11], RefGet(0x5AB00D6), 0xF2);
    uiwidget_2B60DC(4, 0xD20001, shell_font, shell_pos[12], RefGet(0x5AB001D), 0xF2);
}

/* screen 10: entry shell_sel of list 0x5B6 */
void shell_2B8B94(void)
{
    Ref ref = 0xD60000 | (long)shell_sel;
    schar *p;
    long i, b;

    for (i = 0; i < 0x13; i++)                  /* (result unused) */
        (void)camp_level[*(schar *)RefGet(0x5B60000 | i)];
    framed(ref, shell_pt[9]);
    p = RefGet(0x5B60000 | (long)shell_sel);
    g1c_strcpy(shell_text, RefGet(0x5AB0060 + (camp_level[p[2]] <= camp_pos ? p[3] : p[1])));
    gr_font_string_wrap(shell_font, shell_text, 0x21C);
    grd_canvas->gc.fcolor = 0x17;
    font_string(shell_font, shell_text, shell_pt[11].x, shell_pt[11].y);
    grd_canvas->gc.fcolor = 0xF2;
    font_string(shell_font, RefGet(0x5AB002F + shell_sel), shell_pt[10].x, shell_pt[10].y);
    for (b = shell_sel + 1; b < shell_43D474 && !shell_2B7694(b); b++)
        ;
    uiwidget_2B60DC(1, 0xD20001, shell_font, shell_pos[10], RefGet(0x5AB00CF), b < shell_43D474 ? 0xF2 : 0x48);
    for (b = shell_sel - 1; b >= 0 && !shell_2B7694(b); b--)
        ;
    uiwidget_2B60DC(2, 0xD20001, shell_font, shell_pos[9], RefGet(0x5AB00D3), b >= 0 ? 0xF2 : 0x48);
    uiwidget_2B60DC(3, 0xD20001, shell_font, shell_pos[11], RefGet(0x5AB00D7), 0xF2);
    uiwidget_2B60DC(4, 0xD20001, shell_font, shell_pos[12], RefGet(0x5AB001D), 0xF2);
}

/* ------------------------------------------------------------------ screen 25 */

uchar shell_2B8E80(TnEvent *ev)
{
    long n;

    if (ev->type < 1)
        return 0;
    if (ev->type != 1) {
        if (ev->type != 0x100 || *(short *)((uchar *)ev + 4) != 0xD)
            return 0;
        click(0x786);
        shell_2B9538();
        uiwidget_2B60B4(-1);
        return 1;
    }
    n = uiwidget_2B601C(ev_pt(ev));
    if (!n)
        return 0;
    switch (n - 1) {
    case 0:
        click(0x78C);
        uiwidget_2B60B4(-2);
        break;
    case 1:
        uiwidget_2B60B4(-3);
        break;
    case 2:
        click(0x786);
        shell_2B9538();
        uiwidget_2B60B4(-1);
        break;
    case 3:
        uiwidget_2B60B4(-4);
        break;
    }
    return 1;
}

/* ------------------------------------------------------------------ screen 25: squad equipment */

static void cell(long id, long fill, long x, long y, long size, char *s)
{
    uiwidget_2B61D8(id, 0, fill, uiw_font, pt(x, y), size, s);
}

void shell_2B8F60(void)
{
    char buf[0x20];
    uchar m = game_359708 > 0x2F ? 0 : (uchar)game_359708;
    GameSpeaker *sp;
    long i, w, x0;
    short x;

    shell_2C0304();
    frame_pal(0x1C00000, 0x1C0);
    uiwidget_2B5C90();
    cell(-1, 0, 0x21, 0xA4, shell_size[0], RefGet(0x16E0081));
    cell(-1, 0, 0x21, 0xBB, shell_size[0], RefGet(0x5AB0118));
    cell(-1, 0, 0x21, 0xD3, shell_size[0], RefGet(0x16E007E));
    cell(-1, 0, 0x21, 0xE1, shell_size[0], RefGet(0x16E0080));
    cell(-1, 0, 0x21, 0xFB, shell_size[0], RefGet(0x16E007F));
    cell(-1, 0, 0x21, 0x109, shell_size[0], RefGet(0x16E0080));
    cell(-1, 0, 0x21, 0x121, shell_size[0], RefGet(0x5AB0119));
    x = 0x9A;
    for (i = 0; i < shell_count; i++) {
        uchar *b = (uchar *)&game_391E50[i];
        cell(-1, 0x8F, x, 0xA4, shell_size[0], RefGet(0x16C0000 + (schar)b[6]));
        cell(-1, 0x8F, x, 0xBB, shell_size[0], RefGet(0x5AB0110 + b[8]));
        cell(-1, 0x8F, x, 0xD3, shell_size[0], RefGet(0x16E0000 + (schar)b[9]));
        cell(-1, 0x8F, x, 0xE2, shell_size[0], RefGet(0x16E0000 + (schar)b[0xA]));
        cell(-1, 0x8F, x, 0xFB, shell_size[1], RefGet(0x16E0000 + (schar)b[0xB]));
        cell(-1, 0x8F, x, 0x121, shell_size[0], "NONE");
        x += 0x79;
    }
    grd_canvas->gc.fcolor = 0x48;
    gen_hline(0x30, 0x136, 0x276);
    equip_279B50(game_359708, buf);
    w = gr_font_string_width(shell_font, buf);
    x0 = 0x140 - (short)w / 2;
    grd_canvas->gc.fcolor = 0xF2;
    fill_rect((short)(x0 - 3), 0x47, (short)(x0 + w + 3), 0x5A);
    grd_canvas->gc.fcolor = 0;
    font_string(shell_font, buf, (short)(x0 + 1), 0x47);
    grd_canvas->gc.fcolor = 0;
    fill_rect(0x27, 0x16C, 0x1AE, 0x1CC);
    grd_canvas->gc.fcolor = 0xF2;
    WATCOM_CALL(grd_canvas_table[DRAW_BOX], 0x27, 0x16C, 0x1AE, 0x1CC);
    g1c_strcpy(shell_text, RefGet(0x5AF0030 + (schar)m));
    gr_font_string_wrap(uiw_font, shell_text, 0x181);
    font_string(uiw_font, shell_text, 0x2A, 0x16D);
    if (intro_2DC06C())
        cell(1, 0x48, 0x1B9, 0x16D, shell_size[2], RefGet(0x5AB0028));
    cell(2, 0x48, 0x1B9, 0x181, shell_size[2], RefGet(0x5AB0029));
    cell(4, 0x48, 0x1B9, 0x195, shell_size[2], RefGet(0x5AB002A));
    uiwidget_2B60DC(3, 0xD20001, shell_font, pt(0x1B9, 0x1AE), RefGet(0x5AB0022), 0xF2);
}

/* keep a copy of the squad records; drop ammunition that does not fit the weapons */
void shell_2B9538(void)
{
    long i, j;
    uchar *b;

    shell_35CF22 = (uchar)shell_count;
    for (i = 0; i < shell_count; i++) {
        shell_squad_save[i] = game_391E50[i];
        b = (uchar *)&game_391E50[i];
        if (!b[8]) {
            b[0xC] = 0;
            b[0xA] = 0;
        }
        for (j = 0; j < 4; j += 2) {
            if (((uchar *)ord_table)[(schar)b[9 + j] * 0x24 + 1])
                b[0xA + j] = 0;
            if (((uchar *)ord_table)[(schar)b[0xA + j] * 0x24 + 1])
                b[0xA + j] = 0;
        }
    }
}

/* bit i of *out: entry i of list 0x6B4 (resmiss.res) is not marked for m */
void shell_2B960C(long m, ulong *out)
{
    long f = ResOpenResFile("resmiss.res", 0, 0), i;
    uchar *p = ResLock(0x6B4), *base = p + *(long *)(p + 2) + 1;

    *out = 0;
    for (i = 0; i < camp_done[0]; i++)
        if (!((schar)base[camp_sel_a[i] * 2] & (1 << ((uchar)m & 31))))
            *out |= 1 << (i & 31);
    ResUnlock(0x6B4);
    ResCloseFile(f);
}

/* the same for list 0x6B5, from entry 2 on (bit i - 2) */
void shell_2B9694(long m, ulong *out)
{
    long f = ResOpenResFile("resmiss.res", 0, 0), i;
    uchar *p = ResLock(0x6B5), *base = p + *(long *)(p + 2) + 1;

    *out = 0;
    for (i = 2; i < camp_done[1]; i++)
        if (!((schar)base[camp_sel_b[i] * 2] & (1 << ((uchar)m & 31))))
            *out |= 1 << ((i - 2) & 31);
    ResUnlock(0x6B5);
    ResCloseFile(f);
}

/* ------------------------------------------------------------------ screen 11: squad equipment */

/* the cell of squad member m in row y */
static void member_rect(LGRect *r, long m, short y)
{
    r->ul.x = (short)(0x21 + (short)((schar)m + 1) * 0x79);
    r->ul.y = y;
    r->lr.x = (short)(r->ul.x + (short)shell_size[0]);
    r->lr.y = (short)(y + (short)(shell_size[0] >> 16));
}

static uchar ord_b1(long t)
{
    return ((uchar *)ord_table)[t * 0x24 + 1];
}

/* the entry of camp_sel_a equal to v (0 if none) */
static uchar find_a(uchar v)
{
    long i;

    for (i = 0; i < camp_done[0]; i++)
        if ((uchar)camp_sel_a[i] == v)
            return (uchar)i;
    return 0;
}

uchar shell_2B97C0(TnEvent *ev)
{
    LGRect r;
    ulong mask;
    uchar *b;
    long n, i, m;
    uchar j, c, s;

    if (ev->type < 1)
        return 0;
    if (ev->type != 1) {
        if (ev->type != 0x100 || *(short *)((uchar *)ev + 4) != 0xD)
            return 0;
        click(0x786);
        shell_2B9538();
        uiwidget_2B60B4(-1);
        return 1;
    }
    n = uiwidget_2B601C(ev_pt(ev));
    if (!n || n - 1 > 0x25)
        return 1;
    switch (n) {
    case 1:
        click(0x78C);
        uiwidget_2B60B4(-2);
        return 1;
    case 2:
        uiwidget_2B60B4(-3);
        return 1;
    case 3:
        click(0x786);
        shell_2B9538();
        uiwidget_2B60B4(-1);
        return 1;
    case 4:
        uiwidget_2B60B4(-4);
        return 1;
    case 5:
        click(0x787);
        shell_43D460 = shell_screen;
        uiwidget_2B60B4(1);
        return 1;
    case 6:
        shell_43D460 = shell_screen;
        uiwidget_2B60B4(4);
        return 1;
    case 7:
        click(0x787);
        shell_43D460 = shell_screen;
        uiwidget_2B60B4(2);
        return 1;
    case 8:
        click(0x787);
        shell_43D460 = shell_screen;
        uiwidget_2B60B4(3);
        return 1;
    case 9: case 10:
        return 1;
    case 11: case 12: case 13: case 14:         /* soldier */
        shell_35CF20 = (uchar)(n - 11);
        shell_35CF21 = 0;
        m = (schar)shell_35CF20;
        member_rect(&r, m, 0xA4);
        s = voice_soldier[(uchar)game_391E50[m].voice];
        if (squad_390320[m])
            return 1;
        click(0x791);
        s = shell_soldiers[(schar)shell_2C29F8(&r, 0xC, shell_43D476[(schar)s])];
        m = (schar)shell_35CF20;
        b = (uchar *)&game_391E50[m];
        b[6] = camp_soldiers[(schar)s][0];
        c = camp_soldiers[(schar)s][5];
        if (c == 3)
            b[0xD] = 0;
        else if (c == 5)
            b[0xD] = 1;
        else if ((schar)b[0xD] < 2) {
            if (!shell_43D2F8[m])
                b[0xD] = camp_390328[camp_soldiers[(schar)s][5]][0];
            else
                b[0xD] = shell_43D2F8[m];
        }
        break;
    case 15: case 16: case 17: case 18:         /* class */
        shell_35CF20 = (uchar)(n - 15);
        shell_35CF21 = 1;
        member_rect(&r, (schar)shell_35CF20, 0xBB);
        j = 0;
        for (i = 0; i < camp_done[2]; i++)
            if (camp_sel_c[i] == (long)game_391E50[(schar)shell_35CF20].g_08[0]) {
                j = (uchar)i;
                break;
            }
        click(0x791);
        c = camp_sel_c[(schar)shell_2C2438(&r, 0x5AB0110, (uchar)camp_done[2], j, 0, camp_sel_c)];
        b = (uchar *)&game_391E50[(schar)shell_35CF20];
        b[8] = c;
        if (!c) {
            if (ord_b1((schar)b[9])) {
                b[9] = 4;
                b[0xA] = 4;
            }
            b = (uchar *)&game_391E50[(schar)shell_35CF20];
            if (ord_b1((schar)b[0xB])) {
                b[0xB] = 4;
                b[0xC] = 4;
            }
        }
        b = (uchar *)&game_391E50[(schar)shell_35CF20];
        if (b[8] == 2)
            break;
        if (b[9] == 0xD || b[9] == 0xE) {
            b[9] = 4;
            b[0xA] = 4;
        }
        if (b[0xB] == 0xD || b[0xB] == 0xE) {
            b[0xB] = 4;
            b[0xC] = 4;
        }
        if (b[0xD] == 0xA)
            b[0xD] = 9;
        break;
    case 19: case 20: case 21: case 22:         /* weapon 1 */
        shell_35CF20 = (uchar)(n - 19);
        b = (uchar *)&game_391E50[(schar)shell_35CF20];
        shell_35CF21 = 2;
        if (!b[9])
            b[9] = 4;
        member_rect(&r, (schar)shell_35CF20, ord_b1((schar)b[9]) ? 0xDA : 0xD3);
        shell_2B960C(b[8], &mask);
        j = find_a(b[9]);
        click(0x791);
        c = camp_sel_a[(schar)shell_2C2438(&r, 0x16E0000, (uchar)camp_done[0], j, mask, camp_sel_a)];
        b = (uchar *)&game_391E50[(schar)shell_35CF20];
        b[9] = c;
        if (!ord_b1((schar)c) && !b[0xA])
            b[0xA] = c;
        break;
    case 23: case 24: case 25: case 26:         /* ammunition 1 */
        shell_35CF20 = (uchar)(n - 23);
        b = (uchar *)&game_391E50[(schar)shell_35CF20];
        shell_35CF21 = 3;
        if (!b[0xA])
            b[0xA] = 4;
        member_rect(&r, (schar)shell_35CF20, 0xE1);
        shell_2B960C(game_391E50[(schar)shell_35CF20].g_08[0], &mask);
        j = find_a(game_391E50[(schar)shell_35CF20].g_08[2]);
        click(0x791);
        c = camp_sel_a[(schar)shell_2C2438(&r, 0x16E0000, (uchar)camp_done[0], j, mask, camp_sel_a)];
        b = (uchar *)&game_391E50[(schar)shell_35CF20];
        if (ord_b1((schar)c))
            b[9] = c;
        else
            b[0xA] = c;
        break;
    case 27: case 28: case 29: case 30:         /* weapon 2 */
        shell_35CF20 = (uchar)(n - 27);
        shell_35CF21 = 4;
        b = (uchar *)&game_391E50[(schar)shell_35CF20];
        if (!b[0xB])
            b[0xB] = 4;
        member_rect(&r, (schar)shell_35CF20, ord_b1((schar)b[0xB]) ? 0x102 : 0xFB);
        shell_2B960C(b[8], &mask);
        j = find_a(game_391E50[(schar)shell_35CF20].g_08[3]);
        click(0x791);
        c = camp_sel_a[(schar)shell_2C2438(&r, 0x16E0000, (uchar)camp_done[0], j, mask, camp_sel_a)];
        b = (uchar *)&game_391E50[(schar)shell_35CF20];
        b[0xB] = c;
        if (!ord_b1((schar)c) && !b[0xC])
            b[0xC] = c;
        break;
    case 31: case 32: case 33: case 34:         /* ammunition 2 */
        shell_35CF20 = (uchar)(n - 31);
        b = (uchar *)&game_391E50[(schar)shell_35CF20];
        shell_35CF21 = 5;
        if (!b[0xC])
            b[0xC] = 4;
        member_rect(&r, (schar)shell_35CF20, 0x109);
        shell_2B960C(game_391E50[(schar)shell_35CF20].g_08[0], &mask);
        j = find_a(game_391E50[(schar)shell_35CF20].g_08[4]);
        click(0x791);
        c = camp_sel_a[(schar)shell_2C2438(&r, 0x16E0000, (uchar)camp_done[0], j, mask, camp_sel_a)];
        b = (uchar *)&game_391E50[(schar)shell_35CF20];
        if (ord_b1((schar)c))
            b[0xB] = c;
        else
            b[0xC] = c;
        break;
    default:                                    /* 35..38: equipment */
        shell_35CF20 = (uchar)(n - 35);
        shell_35CF21 = 6;
        if (game_359708 == 0x24 && !shell_35CF20)
            return 1;
        if (game_359708 == 0x2F)
            return 1;
        member_rect(&r, (schar)shell_35CF20, 0x121);
        shell_2B9694(game_391E50[(schar)shell_35CF20].g_08[0], &mask);
        j = 0;
        for (i = 0; i < camp_done[1]; i++)
            if (camp_sel_b[i] == (schar)game_391E50[(schar)shell_35CF20].g_08[5]) {
                j = (uchar)i;
                break;
            }
        click(0x791);
        c = camp_sel_b[2 + (schar)shell_2C2438(&r, 0x16E0030, (uchar)(camp_done[1] - 2), (uchar)(j - 2), mask,
                                                camp_sel_b + 2)];
        game_391E50[(schar)shell_35CF20].g_08[5] = c;
        break;
    }
    uiwidget_2B60B4(0xB);
    return 1;
}

/* ------------------------------------------------------------------ list popup info panel */

static void centred(char *buf, Ref ref, long k, long y)
{
    g1c_strcpy(buf, RefGet(ref));
    font_string(uiw_font, buf, (short)((0x73 - (short)gr_font_string_width(uiw_font, buf)) / 2 + k), y);
}

/* (box, fill) of a cell: selected cells are filled 0xBF, others framed 0x48 */
static void sel_cell(long sel, long y, long size, char *s)
{
    uiwidget_2B61D8(-1, sel ? 0 : 0x48, sel ? 0xBF : 0, uiw_font, pt(0x1E, y), size, s);
}

/* The info panel of the list popups of screen 11 for entry idx of the list of slot
 * shell_35CF21, and the column of squad member shell_35CF20 with that entry. */
void shell_2BA5B8(long idx)
{
    char buf[0x78];
    uchar i = (uchar)idx, slot;
    long id, st, m;
    uchar s, cls, w1, a1, w2, a2, eq, g, t;
    uchar *b;

    grd_canvas->gc.fcolor = 0;
    fill_rect(0x19, 0x4B, 0x96, 0x136);
    grd_canvas->gc.fcolor = 0xF2;
    WATCOM_CALL(grd_canvas_table[DRAW_BOX], 0x19, 0x4B, 0x96, 0x136);
    WATCOM_CALL(grd_canvas_table[DRAW_BOX], 0x1E, 0x50, 0x91, 0x9B);
    switch (shell_35CF21) {
    case 0:
        id = shell_soldiers[i];
        centred(buf, 0x16C0000 + camp_soldiers[id][0], 0x1E, 0x52);
        gen_hline(0x23, 0x60, 0x8C);
        centred(buf, 0x16E0082, 0x1E, 0x62);
        centred(buf, 0x16E0068 + camp_soldiers[id][5], 0x1E, 0x6D);
        gen_hline(0x23, 0x7A, 0x8C);
        centred(buf, 0x5AB00E5, 0x1E, 0x7C);
        st = shell_2B6B24((schar)i);
        switch ((uchar)st) {
        case 0: grd_canvas->gc.fcolor = 0xF2; break;
        case 1: grd_canvas->gc.fcolor = 0x8F; break;
        case 2: grd_canvas->gc.fcolor = 0x17; break;
        case 3: grd_canvas->gc.fcolor = 0xEB; break;
        }
        switch ((uchar)st) {
        case 0: g1c_strcpy(buf, RefGet(0x5AB0145)); break;
        case 1: g1c_strcpy(buf, RefGet(0x5AB0147)); break;
        case 2: g1c_strcpy(buf, RefGet(0x5AB0148)); break;
        case 3: g1c_strcpy(buf, RefGet(0x5AB0146)); break;
        }
        font_string(uiw_font, buf, (short)((0x73 - (short)gr_font_string_width(uiw_font, buf)) / 2 + 0x1E), 0x87);
        grd_canvas->gc.fcolor = 0xF2;
        break;
    case 1:
        id = camp_sel_c[i];
        centred(buf, 0x5AB0110 + id, 0x22, 0x52);
        lg_sprintf(buf, RefGet(0x5AB018A), RefGet(0x5AB0170 + id));
        font_string(uiw_font, buf, 0x22, 0x5E);
        lg_sprintf(buf, RefGet(0x5AB018B), (long)*(short *)(game_pba_types[id] + 0x13),
                   (long)*(short *)(game_pba_types[id] + 0xF));
        font_string(uiw_font, buf, 0x22, 0x6A);
        lg_sprintf(buf, RefGet(0x5AB018C), RefGet(0x5AB0174 + id));
        font_string(uiw_font, buf, 0x22, 0x76);
        lg_sprintf(buf, RefGet(0x5AB018D), RefGet(0x5AB0178 + id));
        font_string(uiw_font, buf, 0x22, 0x82);
        lg_sprintf(buf, RefGet(0x5AB018E), RefGet(0x5AB017C + id));
        font_string(uiw_font, buf, 0x22, 0x8E);
        break;
    case 2: case 3: case 4: case 5: {
        uchar *o;
        ushort v;

        id = camp_sel_a[i];
        o = (uchar *)ord_table + id * 0x24;
        centred(buf, 0x16E0000 + id, 0x22, 0x52);
        lg_sprintf(buf, RefGet(0x5AB0185), (long)*(ushort *)(o + 0xE) * 6);
        font_string(uiw_font, buf, 0x22, 0x5E);
        lg_sprintf(buf, RefGet(0x5AB0187), RefGet(0x5AB0149 + id));
        font_string(uiw_font, buf, 0x22, 0x6A);
        lg_sprintf(buf, RefGet(0x5AB0186), (long)(o[1] != 0) + 1);
        font_string(uiw_font, buf, 0x22, 0x76);
        v = *(ushort *)((uchar *)ord_table + (schar)camp_sel_a[i] * 0x24 + 0xA);
        if (v > 1) {
            lg_sprintf(buf, RefGet(0x5AB0189), (long)v);
            font_string(uiw_font, buf, 0x22, 0x82);
        }
        break;
    }
    case 6:
        id = camp_sel_b[2 + i];
        centred(buf, 0x16E0030 + id, 0x22, 0x52);
        font_string(uiw_font, RefGet(0x5AB0164 + id), 0x22, 0x5E);
        if ((1 << (id & 31)) & ai_fire_mask) {
            lg_sprintf(buf, RefGet(0x5AB0158 + id), unit_tab[id].f0);
            font_string(uiw_font, buf, 0x22, 0x88);
        }
        break;
    }

    /* the member's column with the entry */
    slot = shell_35CF21;
    m = (schar)shell_35CF20;
    b = (uchar *)&game_391E50[m];
    s = slot == 0 ? camp_soldiers[shell_soldiers[i]][0] : b[6];
    cls = shell_35CF21 == 1 ? (uchar)camp_sel_c[i] : game_391E50[(schar)shell_35CF20].g_08[0];
    w1 = shell_35CF21 == 2 ? (uchar)camp_sel_a[i] : game_391E50[(schar)shell_35CF20].g_08[1];
    a1 = shell_35CF21 == 3 ? (uchar)camp_sel_a[i] : game_391E50[(schar)shell_35CF20].g_08[2];
    w2 = shell_35CF21 == 4 ? (uchar)camp_sel_a[i] : game_391E50[(schar)shell_35CF20].g_08[3];
    a2 = shell_35CF21 == 5 ? (uchar)camp_sel_a[i] : game_391E50[(schar)shell_35CF20].g_08[4];
    eq = shell_35CF21 == 6 ? (uchar)camp_sel_b[2 + i] : game_391E50[(schar)shell_35CF20].g_08[5];
    if (!cls) {
        if (ord_b1((schar)w1))
            w1 = 4;
        if (ord_b1((schar)w2))
            w2 = 4;
    }
    g = camp_soldiers[voice_soldier[(schar)s]][5];
    if (g == 3)
        eq = 0;
    else if (g == 5)
        eq = 1;
    else if ((schar)eq < 2) {
        t = shell_43D2F8[(schar)shell_35CF20];
        eq = t ? t : camp_390328[(schar)g][0];
    }
    sel_cell(shell_35CF21 == 0, 0xA4, shell_size[0], RefGet(0x16C0000 + (schar)s));
    sel_cell(shell_35CF21 == 1, 0xBB, shell_size[0], RefGet(0x5AB0110 + (schar)cls));
    if (ord_b1((schar)w1))
        sel_cell(shell_35CF21 == 2, 0xD3, shell_size[1], RefGet(0x16E0000 + (schar)w1));
    else {
        sel_cell(shell_35CF21 == 2, 0xD3, shell_size[0], RefGet(0x16E0000 + (schar)w1));
        if (cls)
            sel_cell(shell_35CF21 == 3, 0xE1, shell_size[0], RefGet(0x16E0000 + (schar)a1));
    }
    if (ord_b1((schar)w2))
        sel_cell(shell_35CF21 == 4, 0xFB, shell_size[1], RefGet(0x16E0000 + (schar)w2));
    else {
        sel_cell(shell_35CF21 == 4, 0xFB, shell_size[0], RefGet(0x16E0000 + (schar)w2));
        if (cls)
            sel_cell(shell_35CF21 == 5, 0x109, shell_size[0], RefGet(0x16E0000 + (schar)a2));
    }
    if ((schar)eq < 2)
        uiwidget_2B61D8(-1, 0, 0x8F, uiw_font, pt(0x1E, 0x121), shell_size[0],
                        RefGet(game_359708 == 0x1F && shell_35CF20 == 1 ? 0x16E003B : 0x16E0030 + (schar)eq));
    else
        sel_cell(shell_35CF21 == 6, 0x121, shell_size[0],
                 RefGet(game_359708 == 0x1F && shell_35CF20 == 1 ? 0x16E003B : 0x16E0030 + (schar)eq));
}

/* ------------------------------------------------------------------ screen 11: mission squad */

/* the mission title box at the top (equip_279B50 names mission game_359708) */
static void mission_title(void)
{
    char buf[0x20];
    long w, x0;

    grd_canvas->gc.fcolor = 0x48;
    gen_hline(0x30, 0x136, 0x276);
    equip_279B50(game_359708, buf);
    w = gr_font_string_width(shell_font, buf);
    x0 = 0x140 - (short)w / 2;
    grd_canvas->gc.fcolor = 0xF2;
    fill_rect((short)(x0 - 3), 0x47, (short)(x0 + w + 3), 0x5A);
    grd_canvas->gc.fcolor = 0;
    font_string(shell_font, buf, (short)(x0 + 1), 0x47);
}

void shell_2BB45C(void)
{
    uchar m = game_359708 > 0x2F ? 0 : (uchar)game_359708;
    long i, x = 0x9A, fill, lt;

    shell_2C0304();
    frame_pal(0x1C00000, 0x1C0);
    uiwidget_2B5C90();
    cell(-1, 0, 0x21, 0xA4, shell_size[0], RefGet(0x16E0081));
    cell(-1, 0, 0x21, 0xBB, shell_size[0], RefGet(0x5AB0118));
    cell(-1, 0, 0x21, 0xD3, shell_size[0], RefGet(0x16E007E));
    cell(-1, 0, 0x21, 0xE1, shell_size[0], RefGet(0x16E0080));
    cell(-1, 0, 0x21, 0xFB, shell_size[0], RefGet(0x16E007F));
    cell(-1, 0, 0x21, 0x109, shell_size[0], RefGet(0x16E0080));
    cell(-1, 0, 0x21, 0x121, shell_size[0], RefGet(0x5AB0119));
    for (i = 0; i < shell_count; i++, x += 0x79) {
        uchar *b = (uchar *)&game_391E50[i];

        cell(i ? i + 0xB : -1, squad_390320[i] ? 0x8F : 0xBF, x, 0xA4, shell_size[0],
             RefGet(0x16C0000 + (schar)b[6]));
        cell(i + 0xF, 0xBF, x, 0xBB, shell_size[0], RefGet(0x5AB0110 + b[8]));
        if (ord_b1((schar)b[9]))
            cell(i + 0x13, 0xBF, x, 0xD3, shell_size[1], RefGet(0x16E0000 + (schar)b[9]));
        else {
            cell(i + 0x13, 0xBF, x, 0xD3, shell_size[0], RefGet(0x16E0000 + (schar)b[9]));
            if (b[8])
                cell(i + 0x17, 0xBF, x, 0xE1, shell_size[0], RefGet(0x16E0000 + (schar)b[0xA]));
        }
        if (ord_b1((schar)b[0xB]))
            cell(i + 0x1B, 0xBF, x, 0xFB, shell_size[1], RefGet(0x16E0000 + (schar)b[0xB]));
        else {
            cell(i + 0x1B, 0xBF, x, 0xFB, shell_size[0], RefGet(0x16E0000 + (schar)b[0xB]));
            if (b[8])
                cell(i + 0x1F, 0xBF, x, 0x109, shell_size[0], RefGet(0x16E0000 + (schar)b[0xC]));
        }
        lt = (schar)b[0xD] < 2;
        if ((game_359708 == 0x24 && i == 0) || game_359708 == 0x2F)
            fill = 0x8F;
        else
            fill = lt ? 0x8F : 0xBF;
        if (!lt)
            shell_43D2F8[i] = b[0xD];
        cell(lt ? -1 : i + 0x23, fill, x, 0x121, shell_size[0],
             RefGet(game_359708 == 0x1F && i == 1 ? 0x16E003B : 0x16E0030 + (schar)b[0xD]));
    }
    mission_title();
    uiwidget_2B60DC(5, 0xD20001, shell_font, pt(0x1A, 0x144), RefGet(0x5AB00C7), 0xF2);
    uiwidget_2B60DC(6, 0xD20001, shell_font, pt(0xB3, 0x144), RefGet(0x5AB0024), 0xF2);
    uiwidget_2B60DC(7, 0xD20001, shell_font, pt(0x14C, 0x144), RefGet(0x5AB0023), 0xF2);
    uiwidget_2B60DC(8, 0xD20001, shell_font, pt(0x1E5, 0x144), RefGet(0x5AB0025), 0xF2);
    grd_canvas->gc.fcolor = 0;
    fill_rect(0x27, 0x16C, 0x1AE, 0x1CC);
    grd_canvas->gc.fcolor = 0xF2;
    WATCOM_CALL(grd_canvas_table[DRAW_BOX], 0x27, 0x16C, 0x1AE, 0x1CC);
    g1c_strcpy(shell_text, RefGet(0x5AF0030 + (schar)m));
    gr_font_string_wrap(uiw_font, shell_text, 0x181);
    font_string(uiw_font, shell_text, 0x2A, 0x16D);
    if (!game_35CD9C) {
        if (intro_2DC06C())
            cell(1, 0x48, 0x1B9, 0x16D, shell_size[2], RefGet(0x5AB0028));
        cell(2, 0x48, 0x1B9, 0x181, shell_size[2], RefGet(0x5AB0029));
        cell(4, 0x48, 0x1B9, 0x195, shell_size[2], RefGet(0x5AB002A));
    }
    uiwidget_2B60DC(3, 0xD20001, shell_font, pt(0x1B9, 0x1AE), RefGet(0x5AB0022), 0xF2);
}

/* the member card of the mission squad screen (pt = x | y << 16): picture, the two weapon
   slots (a tall cell for a two-handed weapon, else the two sets), and the soldier button */
void shell_2BBCA4(long at)
{
    GameSpeaker *sp = &game_391E50[shell_43D46A];
    uchar *b = (uchar *)sp;
    short ax = (short)at, ay = (short)(at >> 16);
    UnitTabRec rec;
    char buf[0x28];
    long k, j, p, w, col;

    draw_frame(0xD80000 | b[8], ax, ay);
    for (k = 0; k < 2; k++) {
        w = (schar)b[9 + 2 * k];
        if (w >= 0 && ord_b1(w)) {
            p = shell_35CF24[k];
            cell(5 + 2 * k, 2 * k == shell_43D45E ? 0xBF : 0x48, (short)(p + ax),
                 (short)((p >> 16) + ay), shell_35CF2C, RefGet(0x16E0021 + w));
            continue;
        }
        for (j = 2 * k; j < 2 * k + 2; j++) {
            p = shell_35CF30[j];
            w = (schar)b[9 + j];
            g1c_strcpy(buf, RefGet(w >= 0 ? 0x16E0021 + w : 0x5AB011D));
            cell(j + 5, j == shell_43D45E ? 0xBF : 0x48, (short)(p + ax), (short)((p >> 16) + ay),
                 shell_35CF40, buf);
        }
    }
    damage_27E200(&rec, (schar)b[0xD]);        /* (result unused) */
    if ((schar)b[0xD] >= 0)
        g1c_strcpy(buf, RefGet(game_359708 == 0x1F && shell_43D46A == 1 ? 0x16E003B
                                                                       : 0x16E0030 + (schar)b[0xD]));
    else
        g1c_strcpy(buf, RefGet(0x5AB011D));
    col = shell_2C12EC((schar)b[0xD], (schar)b[0xD] >= 0);
    if ((short)col >= 0)
        cell(9, (uchar)col, (short)(shell_35CF44 + ax), (short)((shell_35CF44 >> 16) + ay),
             shell_35CF48, buf);
}

/* screen 12 mouse: tabs 1..6, any other button wakes with the selection */
uchar shell_2BBFBC(TnEvent *ev)
{
    long n;

    if (ev->type != 1)
        return 0;
    n = uiwidget_2B601C(ev_pt(ev));
    if (!n)
        return 0;
    if (n <= 6) {
        shell_43D460 = shell_screen;
        return shell_2C06D8(n);
    }
    uiwidget_2B60B4(shell_sel);
    return 0;
}

/* screen 12 */
void shell_2BBFFC(void)
{
    shell_43D484 = 0;
    shell_43D470 = 0xC;
    shell_2C0418();
}

/* ------------------------------------------------------------------ screens 13 / 14: list 0x5B2 */

static uchar list_mouse(TnEvent *ev, ShList *l, long base, long w1, long w2, long rel)
{
    long n;

    if (ev->type < 1)
        return 0;
    if (ev->type == 1) {
        n = uiwidget_2B601C(ev_pt(ev));
        if (!n)
            return 1;
        if (n < 9)
            return shell_2C0B7C(l, n);
        click(0x787);
        shell_2C0C78(l, n, base, w1, w2);
        return 1;
    }
    return rel && ev->type == 2;
}

uchar shell_2BC018(TnEvent *ev)
{
    return list_mouse(ev, &shell_list_5b2, 0x5AC0024, 0xE, 0xF, 1);
}

/* the list line of an entry: name and kind, lit when it has been read */
static void list_line(long n, long y, uchar *seen, Ref names, Ref kinds, long x2)
{
    grd_canvas->gc.fcolor = (schar)seen[n / 8] & (1 << (n % 8)) ? 0x17 : 0xF2;
    font_string(grd_canvas->gc.font, RefGet(names + n), 0x40, (short)y);
    font_string(grd_canvas->gc.font, RefGet(kinds + n), x2, (short)y);
}

void shell_2BC094(long n, long y)
{
    list_line(n, y, camp_43E660, 0x5AC0000, 0x5AC0012, 0x12C);
}

void shell_2BC13C(void)
{
    shell_2C0A08(&shell_list_5b2, 0x5B2, 0xE);
}

static void two_titles(Ref a, Ref b, long xb)
{
    grd_canvas->gc.fcolor = 0xF2;
    grd_canvas->gc.font = shell_font;
    font_string(grd_canvas->gc.font, RefGet(a), 0x40, 0x56);
    font_string(grd_canvas->gc.font, RefGet(b), xb, 0x56);
    gen_hline(0x40, 0x6C, 0x23E);
}

/* screen 13: list of entries of resource 0x5B2 */
void shell_2BC158(void)
{
    shell_2C0418();
    two_titles(0x5AB009F, 0x5AB00A0, 0x12C);
    shell_2C0ACC(&shell_list_5b2, shell_list_rect, WATCOM_FN(shell_2BC094));
    if (shell_list_5b2.last >= shell_list_5b2.page)
        shell_2C072C(&shell_list_5b2, 7, shell_35CF64);
}

uchar shell_2BC21C(TnEvent *ev)
{
    return shell_2C0F34(ev, &shell_list_5b2, 0xD);
}

/* the picture of an entry (FrameDesc ref at +1 of its 5-byte record in resource id) */
static void entry_picture(Id id, long n)
{
    long ref = *(long *)((uchar *)ResGet(id) + n * 5 + 1);

    if (ref)
        draw_frame(ref, 0x1C0, 0x88);
}

/* draw s scrolled by l->scroll lines inside (0x40, 0x90)-(0x1EA, 0x13B) */
static void scrolled_text(ShList *l, char *s, Ref lock)
{
    grs_canvas *c = grd_canvas;
    short left = c->gc.clip.i.left, top = c->gc.clip.i.top;
    short right = c->gc.clip.i.right, bot = c->gc.clip.i.bot;

    c->gc.font = shell_font;
    c->gc.fcolor = 0x17;
    c->gc.clip.f.sten = NULL;
    c->gc.clip.i.left = 0x40;
    c->gc.clip.i.top = 0x90;
    c->gc.clip.i.right = 0x1EA;
    c->gc.clip.i.bot = 0x13B;
    if (lock) {
        s = RefLock(lock);
        gr_font_string_wrap(shell_font, s, 0x1AA);
    }
    font_string(grd_canvas->gc.font, s, 0x40, (short)(0x90 - 19 * l->scroll));
    if (lock) {
        gr_font_string_unwrap(s);
        ResUnlock(REFID(lock));
    }
    c = grd_canvas;
    c->gc.clip.f.sten = NULL;
    c->gc.clip.i.left = left;
    c->gc.clip.i.top = top;
    c->gc.clip.i.right = right;
    c->gc.clip.i.bot = bot;
}

/* screen 14: an entry of list 0x5B2 */
void shell_2BC230(void)
{
    long n = shell_list_5b2.items[shell_sel];

    camp_43E660[n / 8] = (schar)camp_43E660[n / 8] | 1 << (n % 8);
    shell_2BC42C(n);
    entry_picture(0x5B2, n);
    if (shell_list_5b2.lines > 9)
        shell_2C089C(&shell_list_5b2, 7, shell_35CF58);
    scrolled_text(&shell_list_5b2, NULL, 0x5AC0024 + n);
    shell_2C0418();
    uiwidget_2B61D8(9, 0, 0x48, shell_font, shell_35CF5C, shell_35CF60, RefGet(0x5AB0027));
}

/* name / kind header of entry n of list 0x5B2 */
void shell_2BC42C(long n)
{
    grd_canvas->gc.fcolor = 0xF2;
    grd_canvas->gc.font = shell_font;
    font_string(grd_canvas->gc.font, RefGet(0x5AB00A2), 0x40, 0x56);
    font_string(grd_canvas->gc.font, RefGet(0x5AC0000 + n), 0xA0, 0x56);
    font_string(grd_canvas->gc.font, RefGet(0x5AB00A3), 0x40, 0x6C);
    font_string(grd_canvas->gc.font, RefGet(0x5AC0012 + n), 0xA0, 0x6C);
    gen_hline(0x40, 0x81, 0x23E);
}

/* screen 15: entry of list 0x5B2 without text */
void shell_2BC50C(void)
{
    char *s;

    shell_2C0418();
    shell_2BC42C(shell_sel);
    grd_canvas->gc.fcolor = 0xBF;
    fill_rect(0x40, 0x99, 0x180, 0x159);
    grd_canvas->gc.fcolor = 0xF2;
    grd_canvas->gc.font = shell_font;
    s = RefLock(0x5AB00A4);
    font_string(grd_canvas->gc.font, s, (short)((0x1C0 - (short)gr_font_string_width(shell_font, s)) / 2), 0xEF);
    ResUnlock(0x5AB);
    uiwidget_2B60DC(9, 0xD20005, shell_font, shell_35CF5C, RefGet(0x5AB002B), 0xF2);
}

/* ------------------------------------------------------------------ screens 16 / 17: list 0x5B3 */

uchar shell_2BC5E8(TnEvent *ev)
{
    if (ev->type != 1)
        return 0;
    return list_mouse(ev, &shell_list_5b3, 0x5AD0042, 0x11, 0, 0);
}

void shell_2BC65C(long n, long y)
{
    list_line(n, y, camp_43E668, 0x5AD0000, 0x5AD0021, 0x1C0);
}

void shell_2BC704(void)
{
    shell_2C0A08(&shell_list_5b3, 0x5B3, 0xE);
}

/* screen 16 */
void shell_2BC720(void)
{
    shell_2C0418();
    two_titles(0x5AB00A1, 0x5AB00A0, 0x1C0);
    shell_2C0ACC(&shell_list_5b3, shell_list_rect, WATCOM_FN(shell_2BC65C));
    if (shell_list_5b3.last >= shell_list_5b3.page)
        shell_2C072C(&shell_list_5b3, 7, shell_35CF68);
}

uchar shell_2BC7E4(TnEvent *ev)
{
    return shell_2C0F34(ev, &shell_list_5b3, 0x10);
}

/* screen 17: an entry of list 0x5B3 (text already in shell_text) */
void shell_2BC7F8(void)
{
    long n = shell_list_5b3.items[shell_sel];

    camp_43E668[n / 8] = (schar)camp_43E668[n / 8] | 1 << (n % 8);
    grd_canvas->gc.fcolor = 0xF2;
    grd_canvas->gc.font = shell_font;
    font_string(grd_canvas->gc.font, RefGet(0x5AB00A1), 0x40, 0x56);
    font_string(grd_canvas->gc.font, RefGet(0x5AD0000 + n), 0xA0, 0x56);
    font_string(grd_canvas->gc.font, RefGet(0x5AB00A0), 0x40, 0x6C);
    font_string(grd_canvas->gc.font, RefGet(0x5AD0021 + n), 0xA0, 0x6C);
    gen_hline(0x40, 0x81, 0x23E);
    entry_picture(0x5B3, n);
    if (shell_list_5b3.lines > 9)
        shell_2C089C(&shell_list_5b3, 7, shell_35CF58);
    scrolled_text(&shell_list_5b3, shell_text, 0);
    shell_2C0418();
    uiwidget_2B61D8(9, 0, 0x48, shell_font, shell_35CF5C, shell_35CF60, RefGet(0x5AB002C));
}
