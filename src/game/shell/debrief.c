/* debrief.c -- M097 0x2BCA80-0x2C0304: the shell screens 18..28 (respterm.res).
 *
 *   screen 18 objectives list   0x2BCB40 draw, 0x2BCA80 mouse, 0x2BCAE8 its list line
 *   screen 19 objective text    0x2BCBF0 draw, 0x2BCBDC mouse
 *   screen 20 mission list      0x2BCEC8 draw, 0x2BCDF0 mouse, 0x2BCE58 its list line
 *   screen 21 debriefing        0x2BD2A4 draw, 0x2BCFBC mouse
 *   screen 22 statistics        0x2BD810 draw, 0x2BD77C mouse
 *   screen 23 times             0x2BE274 draw, 0x2BE85C mouse, 0x2BE1A8 a goal's time
 *   screen 24 soldier           0x2BE968 draw, 0x2BF34C mouse, 0x2BE8E8 rearm cost
 *   screen 26 mission text      0x2BF440 draw, 0x2BF668 mouse
 *   screen 27 text pager        0x2BF760 draw, 0x2BFA08 mouse, 0x2BF740 start, 0x2BF73C (empty)
 *   screen 28 soldier table     0x2BFAC8 draw, 0x2C02B8 mouse, 0x2BFA88 sort compare
 *   (no callers) 0x2BCB24 / 0x2BCEB4 refill a list, 0x2BD1B0 a status word
 *
 * The debriefing screens read the statistics of the mission shown (ordn_mission_state,
 * MissionStats); screen 20 loads another mission's into it (shell_2C0E9C).
 *
 * Quirks kept:
 *  - shell_2BE968 never lists weapon 0 of the soldier (the loop skips i == 0) and colours a
 *    weapon's ammo % with shell_pct_col[pct / 25] (pct read back from edx, which lg_sprintf
 *    preserves);  its "kills" line shows MsSoldier.kills;
 *  - shell_2BE274 prints (uchar)(f81d % 1000 + 0x7C) (byte wrap) and draws its lines with
 *    x0 > x1;
 *  - shell_2BD2A4 passes hud_2686EC an uninitialised p1.y (unused there; 0 here);
 *  - shell_2BFAC8 recomputes the percentage of each row and throws it away;
 *  - shell_2BF760: the x of a line in a 1-column block is whatever the last ^C set (or
 *    uninitialised before any; 0 here).
 */
#include "g10_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar shell_35CF70;
long shell_35CF72 = 0x00C9023E;
long shell_dbf_pt[7] = {
    0x00860078, 0x0086014A, 0x00C20078, 0x00C2014A, 0x00E90078, 0x00E9014A, 0x01A901F4,
};
schar shell_soldier;
long shell_tab_pt[4] = { 0x00F30028, 0x011B0028, 0x01430028, 0x016B0028 };
uchar shell_pct_col[5] = { 0x8F, 0x17, 0x17, 0xF2, 0xF2 };
uchar shell_text_col[5] = { 0x24, 0x91, 0x48, 0x59, 0xE5 };
#endif

#define MS ((MissionStats *)ordn_mission_state)
#define CLEAR 8
#define DRAW_RECT 16
#define FONT_STRING 315

static void font_string(grs_font *f, char *s, long x, long y)
{
    WATCOM_CALL(grd_canvas_table[FONT_STRING], (long)f, (long)s, x, y);
}

static void fill_rect(long x0, long y0, long x1, long y1)
{
    WATCOM_CALL(grd_canvas_table[DRAW_RECT], x0, y0, x1, y1);
}

static grs_bitmap *frame(Ref ref)
{
    grs_bitmap *bm = RefGet(ref);

    bm->bits = (uchar *)bm + 0x1C;
    return bm;
}

static void draw_frame(Ref ref, long x, long y)
{
    grs_bitmap *bm = frame(ref);

    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, x, y);
}

static void draw_uframe(Ref ref, long x, long y)
{
    grs_bitmap *bm = frame(ref);

    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, x, y);
}

static void click(ushort id)
{
    SndPlay(id, 0, 0x40, 1, -1, 0x40, 0);
}

static void clear(void)
{
    WATCOM_CALL(grd_canvas_table[CLEAR], 0);
}

static void frame_pal(Ref ref, Id id)
{
    uchar *bm = RefGet(ref), *p;

    if (*(long *)(bm + 0x18)) {
        p = (uchar *)ResGet(id) + *(long *)(bm + 0x18);
        gr_set_pal(*(short *)p, *(short *)(p + 2), p + 4);
    }
}

static long ev_btn(TnEvent *ev)
{
    return uiwidget_2B601C(*(long *)&ev->pt);
}

static short ev_key(TnEvent *ev)
{
    return *(short *)&ev->pad;
}

static short width(grs_font *f, char *s)
{
    return gr_font_string_width(f, s);
}

/* s centred in a column [x, x + w) */
static void centred(grs_font *f, char *s, long x, long w, long y)
{
    font_string(f, s, (short)(x + (w - width(f, s)) / 2), y);
}

/* the "back" button of screens 21..28 */
static void back_button(long id)
{
    uiwidget_2B60DC(id, 0xD20009, shell_font, shell_dbf_pt[6], RefGet(0x5AB00E3), 0xF2);
}

/* ------------------------------------------------------------------ 18 / 19 objectives */

uchar shell_2BCA80(TnEvent *ev)
{
    long btn;

    if (ev->type != 1)
        return 0;
    btn = ev_btn(ev);
    if (!btn)
        return 1;
    if (btn >= 9) {
        shell_2C0C78(&shell_list_5b4, btn, 0, 0x13, 0);
        click(0x787);
        return 1;
    }
    return shell_2C0B7C(&shell_list_5b4, btn);
}

void shell_2BCAE8(long item, long y)
{
    grd_canvas->gc.fcolor = 0x17;
    font_string(grd_canvas->gc.font, RefGet(0x5AE0000 + item), 0x40, (short)y);
}

void shell_2BCB24(void)
{
    shell_2C0A08(&shell_list_5b4, 0x5B4, 0xE);
}

void shell_2BCB40(void)
{
    shell_2C0418();
    grd_canvas->gc.fcolor = 0xF2;
    grd_canvas->gc.font = shell_font;
    font_string(grd_canvas->gc.font, RefGet(0x5AB00A1), 0x40, 0x56);
    gen_hline(0x40, 0x6C, 0x23E);
    shell_2C0ACC(&shell_list_5b4, shell_list_rect, WATCOM_FN(shell_2BCAE8));
    if (shell_list_5b4.last >= shell_list_5b4.page)
        shell_2C072C(&shell_list_5b4, 7, shell_35CF6C);
}

uchar shell_2BCBDC(TnEvent *ev)
{
    return shell_2C0F34(ev, &shell_list_5b4, 0x12);
}

void shell_2BCBF0(void)
{
    long item = shell_list_5b4.items[shell_sel], ref;
    grs_canvas *c;
    short l, t, r, b;

    shell_2C0418();
    grd_canvas->gc.fcolor = 0xF2;
    grd_canvas->gc.font = shell_font;
    font_string(grd_canvas->gc.font, RefGet(0x5AB00A1), 0x40, 0x56);
    font_string(grd_canvas->gc.font, RefGet(0x5AE0000 + item), 0xA0, 0x56);
    gen_hline(0x40, 0x6E, 0x23E);
    ref = *(long *)((uchar *)ResGet(0x5B4) + item * 5 + 1);
    if (ref)
        draw_frame(ref, 0x1C0, 0x88);
    if (shell_list_5b4.lines > 9)
        shell_2C089C(&shell_list_5b4, 7, shell_35CF58);
    c = grd_canvas;
    c->gc.font = shell_font;
    l = c->gc.clip.i.left;
    t = c->gc.clip.i.top;
    r = c->gc.clip.i.right;
    b = c->gc.clip.i.bot;
    c->gc.fcolor = 0x17;
    c->gc.clip.i.sten = 0;
    c->gc.clip.i.right = 0x1EA;
    c->gc.clip.i.left = 0x40;
    c->gc.clip.i.top = 0x7C;
    c->gc.clip.i.bot = 0x13B;
    font_string(c->gc.font, shell_text, 0x40, (short)(0x7C - shell_list_5b4.scroll * 19));
    c = grd_canvas;
    c->gc.clip.i.sten = 0;
    c->gc.clip.i.left = l;
    c->gc.clip.i.right = r;
    c->gc.clip.i.top = t;
    c->gc.clip.i.bot = b;
    uiwidget_2B61D8(9, 0, 0x48, shell_font, shell_35CF5C, shell_35CF60, RefGet(0x5AB002D));
}

/* ------------------------------------------------------------------ 20 mission list */

uchar shell_2BCDF0(TnEvent *ev)
{
    long btn;

    if (ev->type != 1)
        return 0;
    btn = ev_btn(ev);
    if (!btn)
        return 1;
    if (btn >= 9) {
        click(0x787);
        shell_2C0E9C(&shell_list_ms, btn);
        return 1;
    }
    return shell_2C0B7C(&shell_list_ms, btn);
}

void shell_2BCE58(long item, long y)
{
    char buf[0x30];

    grd_canvas->gc.fcolor = 0x17;
    equip_279B50(camp_missions[item + camp_level[game_359710]], buf);
    font_string(grd_canvas->gc.font, buf, 0x40, (short)y);
}

void shell_2BCEB4(void)
{
    shell_2C0DE4(&shell_list_ms, 0xE);
}

void shell_2BCEC8(void)
{
    frame_pal(0x1C00000, 0x1C0);
    uiwidget_2B5C90();
    shell_35CF70 = 0;
    shell_2C0418();
    grd_canvas->gc.fcolor = 0xF2;
    grd_canvas->gc.font = shell_font;
    font_string(grd_canvas->gc.font, RefGet(0x5AB010F), 0x40, 0x56);
    gen_hline(0x40, 0x6C, 0x23E);
    shell_2C0E74(&shell_list_ms, shell_list_rect, WATCOM_FN(shell_2BCE58));
    if (shell_list_ms.last >= shell_list_ms.page)
        shell_2C072C(&shell_list_ms, 7, shell_35CF72);
}

/* ------------------------------------------------------------------ 21 debriefing */

uchar shell_2BCFBC(TnEvent *ev)
{
    long btn;

    if (ev->type == 0x100) {
        if (ev_key(ev) != 0xD)
            return 0;
        if (shell_35CF70) {
            MouseHide();
            clear();
            MouseShow();
            uiwidget_2B60B4(0x14);
            click(0x778);
        } else {
            click(0x778);
            uiwidget_2B60B4(-1);
        }
        return 1;
    }
    if (ev->type != 1)
        return 0;
    btn = ev_btn(ev);
    switch (btn) {
    case 1:
        click(0x77B);
        uiwidget_2B60B4(0x16);
        break;
    case 2:
        click(0x77B);
        uiwidget_2B60B4(0x17);
        break;
    case 3: case 4: case 5: case 6:
        click(0x77B);
        shell_soldier = (schar)(btn - 3);
        uiwidget_2B60B4(0x18);
        break;
    case 7:
        if (shell_35CF70) {
            MouseHide();
            clear();
            MouseShow();
            click(0x77B);
            uiwidget_2B60B4(0x14);
        } else {
            click(0x77B);
            uiwidget_2B60B4(-1);
        }
        break;
    }
    return 0;
}

void shell_2BD1B0(uchar kind, long x, long y)
{
    char buf[0x10];

    switch (kind) {
    case 1:
        g1c_strcpy(buf, RefGet(0x5AB00EF));
        grd_canvas->gc.fcolor = 0xF2;
        break;
    case 3:
        g1c_strcpy(buf, RefGet(0x5AB00F0));
        grd_canvas->gc.fcolor = 0x8F;
        break;
    case 2:
    case 4:
        g1c_strcpy(buf, RefGet(0x5AB00EE));
        grd_canvas->gc.fcolor = 0x40;
        break;
    default:
        g1c_strcpy(buf, RefGet(0x1710036));
        grd_canvas->gc.fcolor = 0x40;
        break;
    }
    font_string(shell_font, buf, (short)x, (short)y);
}

void shell_2BD2A4(void)
{
    char head[0x28], name[0x20], line[0x50];
    MsSoldier sold;
    long w1, w2, x, i, t;
    LGPoint p0, p1;
    short h;

    shell_43D484 = 0;
    g1c_strcpy(head, RefGet(0x5AB00DF));
    w1 = width(shell_font, head);
    equip_279B50(MS->mission, name);
    lg_sprintf(line, " : %s", name);
    w2 = width(shell_font, line);
    x = 0x140 - (w2 + w1) / 2;
    grd_canvas->gc.fcolor = 0xF2;
    fill_rect((short)(x - 3), 0x4F, (short)(w2 + x + w1 + 3), 0x69);
    grd_canvas->gc.fcolor = 0x8F;
    font_string(shell_font, head, (short)x, 0x51);
    grd_canvas->gc.fcolor = 0;
    font_string(shell_font, line, (short)(w1 + x), 0x51);
    grd_canvas->gc.fcolor = 0xF2;
    g1c_strcpy(line, RefGet(0x5AB00E0));
    font_string(shell_font, line, (short)(0x14A - width(shell_font, line) / 2), 0x6E);

    lg_sprintf(line, "%s: %d", RefGet(0x5AB00D8), (long)(short)mstat_2C71E0());
    uiwidget_2B60DC(1, 0xD20008, shell_font, shell_dbf_pt[0], line, 0xF2);
    t = MS->time;
    lg_sprintf(line, "%s: %02d:%02d", RefGet(0x5AB00DD), t / 60, t - t / 60 * 60);
    uiwidget_2B60DC(2, 0xD20008, shell_font, shell_dbf_pt[1], line, 0xF2);
    grd_canvas->gc.fcolor = 0xF2;
    g1c_strcpy(line, RefGet(0x5AB00DE));
    font_string(shell_font, line, (short)(0x14A - width(shell_font, line) / 2), 0xA8);

    for (i = 0; i < MS->count; i++) {
        char *a, *b;

        sold = MS->sold[i];
        b = RefGet(0x5AB00EB + (schar)sold.state);
        a = RefGet(0x16C0000 + sold.voice);
        lg_sprintf(line, "%s: %s", a, b);
        uiwidget_2B60DC(3 + i, 0xD20008, shell_font, shell_dbf_pt[2 + i], line, 0xF2);
    }
    p0.x = 0x28;
    p0.y = 0x116;
    p1.x = 0x262;
    p1.y = 0;
    h = hud_2686EC((schar)MS->mission, p0, p1, 0x167, 0xF2, 0xF2, 0x8F);
    if (!shell_35CD94) {
        grd_canvas->gc.fcolor = 0xF2;
        gr_font_string_wrap(uiw_font, hud_text_log, 0x24E);
        font_string(uiw_font, hud_text_log, 0x1E, (short)(h + 3));
        gr_font_string_unwrap(hud_text_log);
    }
    back_button(7);
}

uchar shell_2BD77C(TnEvent *ev)
{
    if (ev->type == 1) {
        if (ev_btn(ev) == 7) {
            click(0x77C);
            uiwidget_2B60B4(0x15);
        }
        return 0;
    }
    if (ev->type == 0x100 && ev_key(ev) == 0xD) {
        click(0x77C);
        uiwidget_2B60B4(0x15);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ 22 statistics */

/* one row of per-soldier numbers (field at byte offset off of MsSoldier) and its total */
static void stat_row(long off, long y)
{
    char buf[0x28];
    long i, x = 0x7E, sum = 0;
    short v;

    for (i = 0; i < MS->count; i++, x += 0x6A) {
        v = *(short *)((uchar *)&MS->sold[i] + off);
        lg_sprintf(buf, "%d", (long)v);
        sum += v;
        centred(shell_font, buf, x, 0x6A, y);
    }
    lg_sprintf(buf, "%d", sum);
    grd_canvas->gc.fcolor = 0x17;
    centred(shell_font, buf, 0x226, 0x50, y);
}

void shell_2BD810(void)
{
    char buf[0x28];
    long i, x, k, hits, shots, sum;
    uchar cls, type, any;
    schar row;
    Ref ref;
    MsSoldier *s;

    grd_canvas->gc.fcolor = 0xF2;
    for (i = 0, x = 0x7E; i < MS->count; i++, x += 0x6A) {
        g1c_strcpy(buf, RefGet(0x16C0000 + MS->sold[i].voice));
        centred(shell_font, buf, x, 0x6A, 0x54);
    }
    g1c_strcpy(buf, RefGet(0x5AB00DA));
    centred(shell_font, buf, 0x226, 0x50, 0x54);
    grd_canvas->gc.fcolor = 0x8F;
    gen_vline(0x226, 0x4C, 0x1C8);
    gen_hline(0x1E, 0x6C, 0x26C);
    gen_hline(0x1E, 0x8B, 0x26C);

    grd_canvas->gc.fcolor = 0xF2;
    g1c_strcpy(buf, RefGet(0x5AB00D8));
    font_string(shell_font, buf, 0x1E, 0x73);
    stat_row(8, 0x73);
    grd_canvas->gc.fcolor = 0xF2;
    grd_canvas->gc.fcolor = 0xBF;
    gen_hline(0x1E, 0x15F, 0x26C);
    gen_hline(0x1E, 0x17B, 0x26C);

    grd_canvas->gc.fcolor = 0xF2;
    g1c_strcpy(buf, RefGet(0x5AB00DB));
    font_string(shell_font, buf, 0x1E, 0x162);
    stat_row(0xA, 0x162);

    grd_canvas->gc.fcolor = 0xF2;
    g1c_strcpy(buf, RefGet(0x5AB00DC));
    width(shell_font, buf);
    font_string(shell_font, buf, 0x1E, 0x17E);
    hits = shots = 0;
    for (i = 0, x = 0x7E; i < MS->count; i++, x += 0x6A) {
        s = &MS->sold[i];
        lg_sprintf(buf, "%d/%d", (long)s->f02, (long)s->shots);
        hits += s->f02;
        shots += s->shots;
        centred(shell_font, buf, x, 0x6A, 0x17E);
        lg_sprintf(buf, "%d%%", s->shots ? s->f02 * 100 / s->shots : 0);
        centred(shell_font, buf, x, 0x6A, 0x196);
    }
    lg_sprintf(buf, "%d/%d", hits, shots);
    grd_canvas->gc.fcolor = 0x17;
    centred(shell_font, buf, 0x226, 0x50, 0x17E);
    lg_sprintf(buf, "%d%%", shots ? hits * 100 / shots : 0);
    centred(shell_font, buf, 0x226, 0x50, 0x196);

    /* kills per kill table entry */
    grd_canvas->gc.fcolor = 0xF2;
    row = 0;
    for (k = 0; k < 0x7A; k++) {
        any = 0;
        for (i = 0; i < MS->count; i++)
            if (MS->sold[i].kill_tab[k])
                any = 1;
        if (!any)
            continue;
        mstat_2C614C((short)k, &cls, &type);
        if (cls == 1)
            ref = ((uchar)type + 0x12) | 0x17E0000;
        else if (cls)
            ref = ((cls + 0x17D) << 16) | type;
        else
            ref = 0x5AB00F1;
        g1c_strcpy(buf, RefGet(ref));
        width(uiw_font, buf);
        font_string(uiw_font, buf, 0x1E, (short)(row * 16 + 0x92));
        sum = 0;
        for (i = 0, x = 0x7E; i < MS->count; i++, x += 0x6A) {
            lg_sprintf(buf, "%d", (long)MS->sold[i].kill_tab[k]);
            sum += MS->sold[i].kill_tab[k];
            centred(uiw_font, buf, x, 0x6A, (short)(row * 16 + 0x92));
        }
        lg_sprintf(buf, "%d", sum);
        grd_canvas->gc.fcolor = 0x17;
        centred(uiw_font, buf, 0x226, 0x50, (short)(row * 16 + 0x92));
        row++;
        grd_canvas->gc.fcolor = 0xF2;
    }
    back_button(7);
}

/* ------------------------------------------------------------------ 23 times */

void shell_2BE1A8(long base, long t, long x, long y)
{
    char buf[0x50];
    schar h, m, s;

    if (t) {
        fileutil_2CD804(t + base, (char *)&h, (char *)&m, (char *)&s);
        lg_sprintf(buf, "%s %02d:%02d:%02d", RefGet(0x5AB00FB), (long)h, (long)m, (long)s);
        grd_canvas->gc.fcolor = 0x17;
        font_string(shell_font, buf, (short)x, (short)y);
    } else {
        grd_canvas->gc.fcolor = 0x8F;
        font_string(shell_font, RefGet(0x5AB00FC), (short)x, (short)y);
    }
    grd_canvas->gc.fcolor = 0xF2;
}

/* a time line: "<label>: hh:mm:ss" */
static void time_line(char *buf, char *fmt, Ref label, ulong t)
{
    schar h, m, s;

    fileutil_2CD804(t, (char *)&h, (char *)&m, (char *)&s);
    lg_sprintf(buf, fmt, RefGet(label), (long)h, (long)m, (long)s);
}

void shell_2BE274(void)
{
    char buf[0x50], name[0x20];
    long y, saved, g;
    ulong v, t = MS->time;
    uchar a, b, c;
    char *s1, *s2;

    grd_canvas->gc.fcolor = 0xF2;
    equip_279B50(MS->mission, name);
    centred(shell_font, name, 0x28, 0x24E, 0x54);
    time_line(buf, "%s : %02d:%02d:%02d", 0x5AB00E4, t);
    centred(shell_font, buf, 0x28, 0x24E, 0x70);
    time_line(buf, "%s: %02d:%02d:%02d", 0x5AB00FA, MS->f821);
    font_string(shell_font, buf, 0x28, 0x94);
    y = 0xB8;
    if (MS->f818) {
        time_line(buf, "%s: %02d:%02d:%02d", 0x5AB00FE, MS->f818 + MS->f821);
        font_string(shell_font, buf, 0x28, (short)y);
        y = 0xDC;
    }
    time_line(buf, "%s: %02d:%02d:%02d", 0x5AB00FD, MS->f821 + t);
    font_string(shell_font, buf, 0x28, (short)y);
    y += 0x24;
    saved = y;
    for (g = 1; g <= 3; g++)
        if (hud_26843C(MS->mission, g)) {
            font_string(shell_font, RefGet(0x172000A + g - 1), 0x2D, (short)y);
            shell_2BE1A8(MS->f821, MS->goal_time[g - 1], 0x140, (short)y);
            y += 0x24;
        }
    s2 = RefGet(0x5AB0114 + MS->music);
    s1 = RefGet(0x5AB0101);
    lg_sprintf(buf, "%s: %s", s1, s2);
    centred(shell_font, buf, 0x28, 0x24E, 0x176);
    time_line(buf, "%s: %02d:%02d:%02d", 0x5AB00FF, game_440974);
    centred(shell_font, buf, 0x28, 0x24E, 0x198);
    v = MS->f81d;
    a = (uchar)(v % 1000);
    b = (uchar)(v / 1000 % 100);
    c = (uchar)(v / 100000);
    s2 = RefGet(0x5AB0102 + b);
    s1 = RefGet(0x5AB0100);
    lg_sprintf(buf, "%s: %02d %s %03d", s1, (long)c, s2, (long)(uchar)(a + 0x7C));
    centred(shell_font, buf, 0x28, 0x24E, 0x1B4);
    grd_canvas->gc.fcolor = 0xBF;
    gen_hline(0x26C, 0x6D, 0x1E);
    gen_hline(0x26C, 0x8A, 0x1E);
    gen_hline(0x25D, (short)(saved - 5), 0x2D);
    gen_hline(0x269, 0x173, 0x21);
    gen_hline(0x269, 0x194, 0x21);
    back_button(7);
}

uchar shell_2BE85C(TnEvent *ev)
{
    return shell_2BD77C(ev);
}

/* ------------------------------------------------------------------ 24 soldier */

static long ammo_pct(uchar *w)
{
    ushort max = *(ushort *)(w + 1);

    return max ? (long)*(ushort *)(w + 3) * 100 / max : 0;
}

short shell_2BE8E8(void *pba)
{
    uchar *w = *(uchar **)((uchar *)pba + 0x96);
    short sum = 0;
    long i;

    for (i = 0; i < *(ushort *)((uchar *)pba + 0x9A); i++, w += 5)
        sum += (100 - (short)ammo_pct(w)) / 10 * 0x78;
    return sum;
}

void shell_2BE968(void)
{
    char buf[0x50];
    long sel = shell_soldier, i, k, y, pct;
    MsSoldier *sold = &MS->sold[sel];
    uchar *pba = MS->pba[sel], *w, *p, *ord;
    schar voice = sold->voice;
    short v;
    char *s1, *s2;

    for (i = 0; i < MS->count; i++) {
        g1c_strcpy(buf, RefGet(0x16C0000 + MS->sold[i].voice));
        uiwidget_2B60DC(2 + i, 0xD20009, uiw_font, shell_tab_pt[i], buf,
                        shell_soldier == i ? 0xF2 : 0xBF);
    }
    draw_uframe(voice | 0xD30000, 0x28, 0x50);
    grd_canvas->gc.fcolor = 0xF2;
    g1c_strcpy(buf, RefGet(0x16C0041 + voice));
    centred(shell_font, buf, 0xB4, 0x1B8, 0x50);

    if (sold->state == 2)
        grd_canvas->gc.fcolor = 0x8F;
    s2 = RefGet(0x5AB00EB + (schar)sold->state);
    s1 = RefGet(0x5AB00E5);
    lg_sprintf(buf, "%s: %s", s1, s2);
    font_string(shell_font, buf, 0xB4, 0x68);
    if (sold->state == 2)
        grd_canvas->gc.fcolor = 0xF2;
    if (sold->state == 2) {
        s2 = RefGet(0x5AB0125 + voice);
        s1 = RefGet(0x5AB00E7);
        lg_sprintf(buf, "%s: %s", s1, s2);
    } else {
        v = (short)ammo_pct(*(uchar **)(pba + 0x96));
        lg_sprintf(buf, "%s: %d%%", RefGet(0x5AB00E6), (long)(v < 1 ? 1 : v));
    }
    font_string(shell_font, buf, (short)(0x26C - width(shell_font, buf)), 0x68);

    grd_canvas->gc.fcolor = 0xF2;
    lg_sprintf(buf, "%s: %d", RefGet(0x5AB00E9), (long)sold->kills);
    font_string(shell_font, buf, 0xB4, 0x7B);
    if (voice != 0x20) {
        lg_sprintf(buf, "%s: %d", RefGet(0x5AB00EA), (long)speaker_score[voice]);
        font_string(shell_font, buf, (short)(0x26C - width(shell_font, buf)), 0x7B);
    }
    s2 = RefGet(0x5AB0110 + pba[1]);
    s1 = RefGet(0x5AB0118);
    lg_sprintf(buf, "%s: %s", s1, s2);
    font_string(shell_font, buf, 0xB4, 0x8E);
    lg_sprintf(buf, "%s: ", RefGet(0x5AB0119));
    if (*(long *)(pba + 7) == -1)
        g1c_strcat(buf, RefGet(0x5AB011D));
    else
        g1c_strcat(buf, RefGet(0x16E0030 + *(long *)(pba + 7)));
    font_string(shell_font, buf, (short)(0x26C - width(shell_font, buf)), 0x8E);

    grd_canvas->gc.fcolor = 0x40;
    gen_vline(0x15E, 0xA7, 0x1C8);
    grd_canvas->gc.fcolor = 0xF2;
    gen_hline(0xB4, 0xA7, 0x26C);
    gen_hline(0xB4, 0xBC, 0x26C);
    g1c_strcpy(buf, RefGet(0x5AB00E8));
    centred(shell_font, buf, 0xB4, 0x96, 0xA9);

    /* weapons and their ammunition (weapon 0 is skipped) */
    y = 0xC1;
    w = *(uchar **)(pba + 0x96);
    for (i = 0; i < *(ushort *)(pba + 0x9A); i++, w += 5) {
        if (!i)
            continue;
        pct = (short)ammo_pct(w);
        lg_sprintf(buf, "%d%%", pct);
        grd_canvas->gc.fcolor = shell_pct_col[pct / 25];
        font_string(uiw_font, RefGet(0x16E0060 + i), 0xB4, (short)y);
        font_string(uiw_font, buf, (short)(0x14A - width(uiw_font, buf)), (short)y);
        y = (short)(y + 0xD);
    }
    grd_canvas->gc.fcolor = 0xF2;
    lg_sprintf(buf, "%s: %d", RefGet(0x5AB011C), (long)shell_2BE8E8(pba));
    font_string(uiw_font, buf, 0xB4, (short)(y + 0xD));

    /* equipment: two groups of two slots (15 bytes each from pba + 0x3F) */
    g1c_strcpy(buf, RefGet(0x5AB011A));
    centred(shell_font, buf, 0x172, 0xFA, 0xA9);
    y = 0xC1;
    for (k = 0, p = pba + 0x3F; k < 4; k++, p += 0xF) {
        if (k % 2 == 0) {
            grd_canvas->gc.fcolor = 0xF2;
            s1 = RefGet(0x1710038 + k / 2 * 2);
            font_string(uiw_font, RefGet(0x1710038 + k / 2 * 2),
                        (short)(0x172 + (0xFA - width(uiw_font, s1)) / 2), (short)y);
            y = (short)(y + 0xD);
            grd_canvas->gc.fcolor = 0x40;
            gen_hline(0x15E, (short)y, 0x26C);
        }
        if (!p[0])
            continue;
        grd_canvas->gc.fcolor = 0xF2;
        font_string(uiw_font, RefGet(0x16E0000 + p[0]), 0x172, (short)y);
        ord = (uchar *)ord_table + p[0] * 0x24;
        if (*(short *)(p + 2) == *(short *)(ord + 0xC))
            s1 = RefGet(0x5AB011E);
        else if (!*(short *)(p + 2)) {
            grd_canvas->gc.fcolor = 0x8F;
            s1 = RefGet(0x5AB011F);
        } else {
            grd_canvas->gc.fcolor = 0x17;
            s1 = RefGet(0x5AB0120);
        }
        g1c_strcpy(buf, s1);
        font_string(uiw_font, buf, (short)(0x26C - width(uiw_font, buf)), (short)y);
        y = (short)(y + 0xD);
        if (*(ushort *)(ord + 0xA) > 1) {
            grd_canvas->gc.fcolor = 0xF2;
            lg_sprintf(buf, "%s : %d", RefGet(0x5AB0121),
                       (long)*(ushort *)(ord + 0xA) - *(ushort *)(p + 4));
            font_string(uiw_font, buf, 0x177, (short)y);
            y = (short)(y + 0xD);
        }
    }
    back_button(1);
}

uchar shell_2BF34C(TnEvent *ev)
{
    long btn;

    if (ev->type == 0x100) {
        if (ev_key(ev) != 0xD)
            return 0;
        click(0x77C);
        uiwidget_2B60B4(0x15);
        return 1;
    }
    if (ev->type != 1)
        return 0;
    btn = ev_btn(ev);
    if (btn == 1) {
        click(0x77C);
        uiwidget_2B60B4(0x15);
    } else if (btn >= 2 && btn < MS->count + 2 && shell_soldier != btn - 2) {
        click(0x77B);
        shell_soldier = (schar)(btn - 2);
        uiwidget_2B60B4(0x18);
    }
    return 0;
}

/* ------------------------------------------------------------------ 26 mission text */

void shell_2BF440(void)
{
    char line[0x34], head[0x28], name[0x28];
    long w1, w2, x;
    uchar m = game_359708 > 0x2F ? 0 : (uchar)game_359708;
    schar mi;

    g1c_strcpy(head, RefGet(0x5AB00DF));
    w1 = width(shell_font, head);
    if (game_440928)
        m = (uchar)game_440928 + 0x2D;
    mi = (schar)m;
    equip_279B50(mi, name);
    lg_sprintf(line, " : %s", name);
    w2 = width(shell_font, line);
    x = (short)(0x140 - (w2 + w1) / 2);
    grd_canvas->gc.fcolor = 0xF2;
    fill_rect((short)(x - 3), 0x4F, (short)(w2 + x + w1 + 3), 0x69);
    grd_canvas->gc.fcolor = 0x8F;
    font_string(shell_font, head, (short)x, 0x51);
    grd_canvas->gc.fcolor = 0;
    font_string(shell_font, line, (short)(w1 + x), 0x51);
    grd_canvas->gc.fcolor = 0xF2;
    g1c_strcpy(shell_text, RefGet(game_440928 ? 0x5AF005F + game_440928 : 0x5AF0000 + mi));
    gr_font_string_wrap(uiw_font, shell_text, 0x244);
    font_string(uiw_font, shell_text, 0x23, 0x78);
    uiwidget_2B60DC(1, 0xD20009, shell_font, 0x01AE01EA, RefGet(0x5AB00E3), 0xF2);
}

static void mission_text_done(void)
{
    if ((game_359708 == 0x2E || game_359708 == 0x2F) && !game_440928)
        SndPlay(0x786, 0, 0x40, 1, -1, 0x40, 0);
    game_440928 = 0;
    uiwidget_2B60B4(-1);
}

uchar shell_2BF668(TnEvent *ev)
{
    if (ev->type == 0x100) {
        if (ev_key(ev) != 0xD)
            return 0;
        mission_text_done();
        return 1;
    }
    if (ev->type == 1 && ev_btn(ev) == 1)
        mission_text_done();
    return 0;
}

/* ------------------------------------------------------------------ 27 text pager */

void shell_2BF73C(void)
{
}

#ifndef TN_WATCOM_ABI
/* Freefall: a page of its own before the game's credits (port/core/main.c port_credit_line), not in
 * lockstep runs (frame comparisons with the original) */
extern int port_lockstep;
const char *port_credit_line(int i);
static int ff_page;
#define CREDIT_LINE(i) (ff_page ? (char *)port_credit_line(i) : RefGet(0x5B00000 + (i)))
#else
#define CREDIT_LINE(i) RefGet(0x5B00000 + (i))
#endif

void shell_2BF740(void)
{
    clear();
    shell_43D45A = 0;
    shell_43D483 = 0;
#ifndef TN_WATCOM_ABI
    ff_page = !port_lockstep;
#endif
}

/* Lines of resource 0x5B0 from shell_43D45A on, until ^P (page) or ^E (end).  Codes: ^Rn
 * colour n, ^Cca c columns aligned a (0 centred, else around x 0x140), ^1..^9 blank lines. */
void shell_2BF760(void)
{
    schar cols = 1, col_i = 0, align = 0;
    uchar done = 0;
    char *buf = ffmem_2C4F40(0x50);
    short y = 0x1E, x = 0, ny;
    long w;
    char c;

    do {
        g1c_strcpy(buf, CREDIT_LINE(shell_43D45A));
        while (buf[0] == '^' && !done) {
            c = buf[1];
            if (c == 'R')
                grd_canvas->gc.fcolor = shell_text_col[buf[2] - 0x30];
            else if (c == 'C') {
                cols = buf[2] - '0';
                align = buf[3] - '0';
                x = 0x280 / (cols + 1);
                col_i = 0;
            } else if (c == 'E') {
                shell_43D483 = 1;
                done = 1;
            } else if (c == 'P')
                done = 1;
            else if (c >= '1' && c <= '9') {
                y += (c - '0') * 0x14;
                if (cols != 1) {
                    if (col_i)
                        y += 0x14;
                    x = 0x280 / (cols + 1);
                }
                col_i = 0;
            }
            shell_43D45A++;
            if (!done)
                g1c_strcpy(buf, CREDIT_LINE(shell_43D45A));
        }
        if (done)
            break;
        w = width(shell_font, buf);
        if (!align)
            font_string(shell_font, buf, (short)(x - w / 2), y);
        else if (!col_i)
            font_string(shell_font, buf, (short)(0x138 - w), y);
        else
            font_string(shell_font, buf, 0x148, y);
        ny = y + 0x14;
        if (cols != 1) {
            col_i = (col_i + 1) % cols;
            x = 0x280 / (cols + 1) * (col_i + 1);
            if (!col_i)
                y = ny;
        } else
            y = ny;
        shell_43D45A++;
    } while (!done);
    ffmem_2C4FE4(buf);
#ifndef TN_WATCOM_ABI
    if (ff_page) {                      /* the game's own pages next */
        ff_page = 0;
        shell_43D45A = 0;
    }
#endif
}

uchar shell_2BFA08(TnEvent *ev)
{
    if (ev->type == 0x100) {
        if (ev_key(ev) == 0x1B) {
            uiwidget_2B60B4(-1);
            return 1;
        }
        if (ev_key(ev) != 0x20)
            return 0;
    } else if (ev->type != 1)
        return 0;
    uiwidget_2B60B4(shell_43D483 ? -1 : 0x1B);
    return 1;
}

/* ------------------------------------------------------------------ 28 soldier table */

/* qsort compare of the table rows {kills, assists, %, soldier}: descending */
long shell_2BFA88(short *a, short *b)
{
    if (b[0] != a[0])
        return b[0] - a[0];
    if (b[1] != a[1])
        return b[1] - a[1];
    return b[2] - a[2];
}

static void header(Ref ref, long x, long half)
{
    char buf[0x3C];

    lg_sprintf(buf, RefGet(ref));
    font_string(shell_font, buf, (short)(x - (width(shell_font, buf) + half) / 2), 0x3C);
}

void shell_2BFAC8(void)
{
    short rec[13][4];
    char buf[0x3C];
    long s, j, k;
    short y, hits, shots;
    uchar *p, *e;

    for (s = 0; s < 13; s++) {
        short kills = 0, assists = 0;

        p = camp_soldiers[s];
        hits = shots = 0;
        for (j = 0, e = p; j < p[4]; j++, e += 0xA) {
            kills += *(short *)(e + 0x18);
            hits += *(short *)(e + 0x1E);
            shots += *(short *)(e + 0x20);
            assists += *(short *)(e + 0x1C);
        }
        rec[s][0] = kills;
        rec[s][1] = assists;
        rec[s][2] = hits ? shots * 100 / hits : 0;
        rec[s][3] = s;
    }
    lg_qsort(rec, 13, 8, WATCOM_FN(shell_2BFA88));

    grd_canvas->gc.fcolor = 0xF2;
    lg_sprintf(buf, RefGet(0x5B10000));
    font_string(shell_font, buf, (short)(0x140 - width(shell_font, buf) / 2), 0x1E);
    header(0x16E0081, 0xC8, 0xAF);
    lg_sprintf(buf, RefGet(0x16E0083));
    font_string(shell_font, buf, 0xC8, 0x3C);
    header(0x5AB00D9, 0x140, 0);
    header(0x5AB00DB, 0x17C, 0);
    header(0x5B10001, 0x1B8, 0);
    header(0x5B10002, 0x1E5, 0);
    header(0x16E0082, 0x230, 0);
    gen_hline(0x19, 0x52, 0x25D);

    y = 0x52;
    for (k = 0; k < 13; k++, y += 0x16) {
        p = camp_soldiers[rec[k][3]];
        g1c_strcpy(buf, RefGet(0x16C00A5 + p[0]));
        width(shell_font, buf);
        font_string(shell_font, buf, 0x19, y);
        g1c_strcpy(buf, RefGet(0x16C00C6 + p[0]));
        width(shell_font, buf);
        font_string(shell_font, buf, 0x7D, y);
        g1c_strcpy(buf, RefGet(0x16C0021 + p[0]));
        width(shell_font, buf);
        font_string(shell_font, buf, 0xC8, y);
        lg_sprintf(buf, "%d", (long)rec[k][0]);
        font_string(shell_font, buf, (short)(0x140 - width(shell_font, buf) / 2), y);
        lg_sprintf(buf, "%d", (long)rec[k][1]);
        font_string(shell_font, buf, (short)(0x17C - width(shell_font, buf) / 2), y);
        lg_sprintf(buf, "%d%%", (long)rec[k][2]);
        font_string(shell_font, buf, (short)(0x1B8 - width(shell_font, buf) / 2), y);
        lg_sprintf(buf, "%d", (long)p[4]);
        font_string(shell_font, buf, (short)(0x1E5 - width(shell_font, buf) / 2), y);
        g1c_strcpy(buf, RefGet(0x16E0068 + p[5]));
        font_string(shell_font, buf, (short)(0x230 - width(shell_font, buf) / 2), y);
    }
    back_button(1);
}

uchar shell_2C02B8(TnEvent *ev)
{
    if (ev->type == 0x100) {
        if (ev_key(ev) != 0xD)
            return 0;
    } else if (ev->type != 1 || ev_btn(ev) != 1)
        return 0;
    uiwidget_2B60B4(-1);
    return 1;
}
