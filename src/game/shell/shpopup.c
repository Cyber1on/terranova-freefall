/* shpopup.c -- M097 0x2C1520-0x2C32D4: the shell's modal pop-ups.
 *
 *   0x2C1520 shell_2C1520 notice handlers off (no callers)
 *   0x2C1548 shell_2C1548 notice key / mouse handler
 *   0x2C16D4 shell_2C16D4 open the notices of the mission (resmiss.res 0x6B4..0x6B6)
 *   0x2C18A0 shell_2C18A0 draw the next notice
 *   0x2C205C shell_2C205C quit dialog handler      0x2C21F8 shell_2C21F8 open the quit dialog
 *   0x2C2438 shell_2C2438 pop-up list of strings   0x2C292C shell_2C292C text in a box
 *   0x2C29F8 shell_2C29F8 pop-up list of soldiers  0x2C2F84 shell_2C2F84 (no callers)
 *   0x2C2FC8 shell_2C2FC8 kit letters              0x2C3060 shell_2C3060 framed box
 *   0x2C3124 shell_2C3124 pop-up list key / mouse handler
 *
 * The two pop-up lists run their own loop (GameTimeFrame) until shell_2C3124 sets flag 1:
 * a click on an item that may be chosen, a release after the mouse moved to another item, or
 * Esc / button 4 (flag 8: keep the old selection).  They return the item chosen.
 *
 * Quirks kept:
 *  - shell_2C18A0 returns without closing respterm.res when no notice is left;
 *  - shell_2C29F8 sets flag 4 when the mouse is over another item but never clears it;
 *  - shell_2C16D4 adds to the notice counts without resetting them;
 *  - shell_2C18A0 takes a notice of kind 1 when none is left (count byte wraps).
 */
#include "g10_b.h"
#include "g10_c.h"     /* added at the G9/G10 merge */

#ifndef TN_LINK_ORIGINAL_DATA
uchar shell_note_count;
long shell_note_done;
uchar shell_note_mission;
uchar shell_note_n[3];
long shell_quit_result;
uchar shell_pop_flags;
uchar shell_pop_ok = 1;
long shell_pop_cur;
uchar shell_pop_out;
LGRect shell_pop_sel;
LGRect shell_pop_box;
uchar shell_note_b[13];
uchar shell_note_c[4];
uchar shell_note_a[15];
#endif

#define DRAW_RECT 16
#define DRAW_BOX 18
#define FONT_STRING 315

static void font_string(grs_font *f, char *s, long x, long y)
{
    WATCOM_CALL(grd_canvas_table[FONT_STRING], (long)f, (long)s, x, y);
}

static void fill_rect(long x0, long y0, long x1, long y1)
{
    WATCOM_CALL(grd_canvas_table[DRAW_RECT], x0, y0, x1, y1);
}

static void box(long x0, long y0, long x1, long y1)
{
    WATCOM_CALL(grd_canvas_table[DRAW_BOX], x0, y0, x1, y1);
}

static void draw_frame(Ref ref, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);

    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, x, y);
}

static void frame_pal(Ref ref, Id id)
{
    uchar *bm = RefGet(ref), *p;

    if (*(long *)(bm + 0x18)) {
        p = (uchar *)ResGet(id) + *(long *)(bm + 0x18);
        gr_set_pal(*(short *)p, *(short *)(p + 2), p + 4);
    }
}

static short ev_key(TnEvent *ev)
{
    return *(short *)&ev->pad;
}

static long in_rect(TnEvent *ev, LGRect *r)
{
    return ev->pt.y >= r->ul.y && ev->pt.y < r->lr.y && ev->pt.x >= r->ul.x && ev->pt.x < r->lr.x;
}

static void clear_screen(void)
{
    gr_set_canvas(&tnui_back_canvas);
    grd_canvas->gc.fcolor = 0;
    fill_rect(tnui_full_rect.ul.x, tnui_full_rect.ul.y, tnui_full_rect.lr.x, tnui_full_rect.lr.y);
}

/* ------------------------------------------------------------------ notices */

static void notice_off(void)
{
    KeyRemoveHandler(WATCOM_FN(shell_2C1548), 0);
    MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(shell_2C1548));
}

void shell_2C1520(void)
{
    notice_off();
}

uchar shell_2C1548(TnEvent *ev)
{
    LGRect *r = RefGet(0xDB0004);
    uchar n = shell_note_count - 1;
    short key;

    if (ev->type == 0)
        return 1;
    if (ev->type == 1) {
        if (!in_rect(ev, r))
            return 1;
    } else if (ev->type == 0x100) {
        key = ev_key(ev);
        if (key == 0x4F || key == 0x6F) {
            if (!game_paused_menu)
                options_2CA8FC();
            return 1;
        }
        if (key == 0x458 || key == 0x478) {
            main_245334();
            return 1;
        }
        if (key != 0xD && key != 0x1B)
            return 1;
    } else
        return 1;
    shell_note_count = n;
    if (n)
        shell_2C18A0();
    else {
        notice_off();
        shell_note_done = 1;
    }
    return 1;
}

/* the notices of the mission in list res (2-byte entries {mission, ?}) */
static void note_scan(Id id, long count, uchar *list, uchar *n)
{
    uchar *d = REFPTR((RefTable *)ResLock(id), 0);
    long i;

    for (i = 0; i < count; i++, d += 2)
        if (d[0] == game_359708)
            list[(*n)++] = (uchar)i;
    ResUnlock(id);
}

long shell_2C16D4(void)
{
    long f;

    if (game_359708 == 0x25 || shell_note_done || shell_note_mission == game_359708)
        return 0;
    f = ResOpenResFile("resmiss.res", 0, 0);
    note_scan(0x6B4, 0xF, shell_note_a, &shell_note_n[0]);
    note_scan(0x6B5, 0xC, shell_note_b, &shell_note_n[1]);
    if (game_359708 == 0x24)
        shell_note_b[shell_note_n[1]++] = 0xB;
    note_scan(0x6B6, 4, shell_note_c, &shell_note_n[2]);
    if (!shell_note_n[1] && !shell_note_n[0] && !shell_note_n[2]) {
        ResCloseFile(f);
        shell_note_mission = (uchar)game_359708;
        return 0;
    }
    KeyAddHandler(WATCOM_FN(shell_2C1548), 0, 0, 0);
    MouseAddHandler(&tnui_full_rect, 0xFF, WATCOM_FN(shell_2C1548));
    shell_note_mission = (uchar)game_359708;
    shell_note_count = shell_note_n[0] + shell_note_n[1] + shell_note_n[2];
    shell_2C18A0();
    ResCloseFile(f);
    return 1;
}

void shell_2C18A0(void)
{
    char buf[0x200];
    uchar kind, idx;
    grs_font *f168;
    LGRect *r0, *r1, *r2, *r3, *r4;
    Ref title;
    LGPoint pt;

    shell_resfile = ResOpenResFile("respterm.res", 0, 0);
    if (!shell_note_count)
        return;
    if (shell_note_n[2]) {
        kind = 2;
        shell_note_n[2]--;
    } else if (shell_note_n[0]) {
        kind = 0;
        shell_note_n[0]--;
    } else {
        kind = 1;
        shell_note_n[1]--;
    }
    if (tnm_cursor_drawn)
        MouseHide();
    MouseSetCursor(0x16B0001);
    ResLock(0xDB);
    ResLock(0xDA);
    ResLock(0x167);
    f168 = ResLock(0x168);
    r1 = RefGet(kind == 2 ? 0xDD0002 : 0xDB0001);
    r4 = RefGet(kind == 2 ? 0xDD0004 : 0xDB0004);
    r2 = RefGet(kind == 2 ? 0xDD0001 : 0xDB0002);
    r3 = RefGet(kind == 2 ? 0xDD0003 : 0xDB0003);
    r0 = RefGet(kind == 2 ? 0xDD0000 : 0xDB0000);
    clear_screen();
    GfxBlit(&tnui_full_rect);
    gr_set_canvas(&tnui_back_canvas);
    frame_pal(0xD40000, 0xD4);
    draw_frame(kind == 2 ? 0xDC0001 : 0xDA0002, r2->ul.x, r2->ul.y);
    if (kind == 0) {
        idx = shell_note_a[shell_note_n[0]];
        frame_pal(0xD40000, 0xD4);
        draw_frame(0xD40000 | idx, (short)(r1->ul.x + 6), (short)(r1->ul.y + 6));
        g1c_strcpy(buf, RefGet(0x5AB0042 + idx));
        grd_canvas->gc.fcolor = 0xF2;
        title = 0x16E0000 + idx;
    } else if (kind == 1) {
        idx = shell_note_b[shell_note_n[1]];
        frame_pal(0x1C00000, 0x1C0);
        draw_frame(0xD50000 | idx, (short)(r1->ul.x + 6), (short)(r1->ul.y + 6));
        g1c_strcpy(buf, RefGet(0x5AB0051 + idx));
        grd_canvas->gc.fcolor = 0xF2;
        title = 0x16E0030 + idx;
    } else {
        idx = shell_note_c[shell_note_n[2]];
        frame_pal(0xDC0000, 0xDC);
        draw_frame(0xD80000 | idx, r1->ul.x, r1->ul.y);
        g1c_strcpy(buf, RefGet(0x5AB005D + idx));
        grd_canvas->gc.fcolor = 0xF2;
        title = 0x5AB0110 + idx;
    }
    options_2CAAC0(r2, RefGet(title), 0x168, &tnui_back_canvas);
    draw_frame(kind == 2 ? 0xDC0002 : 0xDA0001, r1->ul.x, r1->ul.y);
    grd_canvas->gc.fcolor = 0xF2;
    draw_frame(kind == 2 ? 0xDC0000 : 0xDA0000, r0->ul.x, r0->ul.y);
    options_2CAAC0(r0, RefGet(0x5AB0181), 0x169, &tnui_back_canvas);
    draw_frame(kind == 2 ? 0xDC0004 : 0xDA0004, r4->ul.x, r4->ul.y);
    options_2CAAC0(r4, RefGet(0x5AB011E), 0x168, &tnui_back_canvas);
    draw_frame(kind == 2 ? 0xDC0003 : 0xDA0003, r3->ul.x, r3->ul.y);
    gr_font_string_wrap(f168, buf, (short)((short)(r3->lr.x - r3->ul.x) - 0x12));
    font_string(f168, buf, (short)(r3->ul.x + 0xC), (short)(r3->ul.y + 8));
    pt.x = (r4->ul.x + r4->lr.x) / 2;
    pt.y = (r4->ul.y + r4->lr.y) / 2;
    MouseMoveTo(pt);
    GfxBlit(&tnui_full_rect);
    ResUnlock(0x167);
    ResUnlock(0x168);
    ResUnlock(0xDB);
    ResUnlock(0xDA);
    ResCloseFile(shell_resfile);
    uiwidget_2B5C90();
    MouseShow();
}

/* ------------------------------------------------------------------ quit dialog */

static void quit_off(long result)
{
    shell_quit_result = result;
    KeyRemoveHandler(WATCOM_FN(shell_2C205C), 0);
    MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(shell_2C205C));
}

uchar shell_2C205C(TnEvent *ev)
{
    LGRect *r0, *r1, *r2;
    short key;

    ResLock(0x183);
    ResLock(0x184);
    r0 = RefGet(0x1840000);
    r1 = RefGet(0x1840001);
    r2 = RefGet(0x1840002);
    if (ev->type == 1) {
        if (in_rect(ev, r0))
            quit_off(1);
        else if (in_rect(ev, r1))
            quit_off(2);
        else if (in_rect(ev, r2) && main_245334())
            shell_quit_result = 3;
    } else if (ev->type == 0x100) {
        key = ev_key(ev);
        if (key == 0x1B)
            quit_off(2);
        else if (key == 0x458 || key == 0x478)
            main_245334();
    }
    ResUnlock(0x183);
    ResUnlock(0x184);
    return 1;
}

void shell_2C21F8(void)
{
    LGRect *r0, *r1, *r2;

    shell_resfile = ResOpenResFile("respterm.res", 0, 0);
    ResLock(0x183);
    ResLock(0x184);
    clear_screen();
    r0 = RefGet(0x1840000);
    r1 = RefGet(0x1840001);
    r2 = RefGet(0x1840002);
    frame_pal(0x1830000, 0x183);
    uiwidget_2B5C90();
    draw_frame(0x1830000, 0, 0);
    draw_frame(0x1830001, r0->ul.x, r0->ul.y);
    draw_frame(0x1830002, r1->ul.x, r1->ul.y);
    draw_frame(0x1830003, r2->ul.x, r2->ul.y);
    grd_canvas->gc.fcolor = 0xFF;
    options_2CAAC0(r0, RefGet(0x5AB0183), 0x168, &tnui_back_canvas);
    options_2CAAC0(r1, RefGet(0x5AB0184), 0x168, &tnui_back_canvas);
    options_2CAAC0(r2, RefGet(0x16F00B8), 0x168, &tnui_back_canvas);
    GfxBlit(&tnui_full_rect);
    KeyAddHandler(WATCOM_FN(shell_2C205C), 0, 0, 0);
    MouseAddHandler(&tnui_full_rect, 0xFF, WATCOM_FN(shell_2C205C));
    ResCloseFile(shell_resfile);
    ResUnlock(0x183);
    ResUnlock(0x184);
    MouseShow();
}

/* ------------------------------------------------------------------ pop-up lists */

/* set up a list of n items of height h, item sel over r */
static void pop_open(LGRect *r, long n, uchar sel, long h)
{
    shell_pop_flags |= 2;
    shell_pop_cur = sel;
    shell_pop_box.ul.x = 0;
    shell_pop_ok = 1;
    shell_pop_box.lr.x = 0x27F;
    shell_pop_box.ul.y = r->ul.y - h * sel;
    shell_pop_box.lr.y = r->lr.y + h * ((uchar)n - sel - 1);
    KeyAddHandler(WATCOM_FN(shell_2C3124), 0, 0, 0);
    MouseAddHandler(&tnui_full_rect, 0xFF, WATCOM_FN(shell_2C3124));
}

/* the frame around the list; the mouse position into *mx / *my */
static void pop_frame(LGRect *r, short *mx, short *my)
{
    *mx = ((short *)tnm_state_ptr)[0];
    *my = ((short *)tnm_state_ptr)[1];
    gr_set_canvas(&tnui_back_canvas);
    grd_canvas->gc.fcolor = 0;
    box((short)(r->ul.x - 3), (short)(shell_pop_box.ul.y - 3), (short)(r->lr.x + 3),
        (short)(shell_pop_box.lr.y + 3));
    box((short)(r->ul.x - 1), (short)(shell_pop_box.ul.y - 1), (short)(r->lr.x + 1),
        (short)(shell_pop_box.lr.y + 1));
    grd_canvas->gc.fcolor = 0x48;
    box((short)(r->ul.x - 4), (short)(shell_pop_box.ul.y - 4), (short)(r->lr.x + 4),
        (short)(shell_pop_box.lr.y + 4));
    box((short)(r->ul.x - 2), (short)(shell_pop_box.ul.y - 2), (short)(r->lr.x + 2),
        (short)(shell_pop_box.lr.y + 2));
}

static void pop_show(void)
{
    shell_2BA5B8((uchar)shell_pop_cur);
    MouseHide();
    GfxBlit(&tnui_full_rect);
    MouseShow();
    shell_pop_flags &= ~2;
}

static uchar pop_close(LGRect *r, short mx, uchar sel, uchar cur)
{
    uchar res;

    KeyRemoveHandler(WATCOM_FN(shell_2C3124), 0);
    MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(shell_2C3124));
    if (mx < r->ul.x || mx > r->lr.x || (shell_pop_flags & 8))
        res = sel;
    else
        res = cur;
    shell_pop_flags = 0;
    return res;
}

static long hover(LGRect *R, short mx, short my, short x0)
{
    return my > R->ul.y && my < R->lr.y && mx < R->lr.x && mx > x0;
}

uchar shell_2C2438(LGRect *r, long base, long n, long sel, long mask, schar *items)
{
    long h = r->lr.y - r->ul.y, i, top;
    uchar cur = (uchar)sel, en;
    short mx = 0, my = 0, x0;
    LGRect R;

    pop_open(r, n, (uchar)sel, h);
    while (!(shell_pop_flags & 1)) {
        GameTimeFrame();
        if (!(shell_pop_flags & 2))
            continue;
        x0 = r->ul.x;
        top = r->ul.y;
        pop_frame(r, &mx, &my);
        top -= (uchar)sel * h;
        for (i = 0; i < (uchar)n; i++) {
            en = !(mask & (1 << i));
            R.ul.x = x0;
            R.lr.x = r->lr.x;
            R.ul.y = top + i * h;
            R.lr.y = top + (i + 1) * h;
            if (hover(&R, mx, my, x0)) {
                shell_2C3060(&R, en ? 0xBF : 0x48);
                grd_canvas->gc.fcolor = en ? 0xF2 : 0x8F;
                options_2CAAC0(&R, RefGet(items[i] + base), 0x167, &tnui_back_canvas);
                shell_pop_sel = R;
                cur = (uchar)i;
                shell_pop_cur = i;
                shell_pop_ok = en != 0;
                if ((uchar)sel != i)
                    shell_pop_flags |= 4;
                else
                    shell_pop_flags &= ~4;
            } else {
                if ((uchar)sel == i) {
                    shell_2C3060(&R, 0x7F);
                    grd_canvas->gc.fcolor = 0xF2;
                } else {
                    shell_2C3060(&R, 0x48);
                    grd_canvas->gc.fcolor = en ? 0xF2 : 0x8F;
                }
                options_2CAAC0(&R, RefGet(items[i] + base), 0x167, &tnui_back_canvas);
            }
        }
        pop_show();
    }
    return pop_close(r, mx, (uchar)sel, cur);
}

void shell_2C292C(LGRect *r, char *s, long font, grs_canvas *c)
{
    char buf[0xC8];
    grs_font *f;
    short h;

    gr_set_canvas(c);
    f = ResLock((ushort)font);
    g1c_strcpy(buf, s);
    gr_font_string_width(f, buf);
    h = gr_font_string_height(f, buf);
    GfxDrawText((ushort)font, buf, r->ul.x + 3, r->ul.y + (r->lr.y - r->ul.y - h) / 2);
    ResUnlock((ushort)font);
}

static void soldier_colour(long i)
{
    switch (shell_2B6B24((schar)i)) {
    case 0: grd_canvas->gc.fcolor = 0xF2; break;
    case 1: grd_canvas->gc.fcolor = 0x8F; break;
    case 2: grd_canvas->gc.fcolor = 0x17; break;
    case 3: grd_canvas->gc.fcolor = 0xEB; break;
    }
}

static void soldier_line(LGRect *R, long i)
{
    shell_2C2FC8((schar)camp_soldiers[shell_soldiers[(schar)i]][5], R);
    shell_2C292C(R, RefGet(shell_2B6B94((schar)i)), 0x167, &tnui_back_canvas);
}

uchar shell_2C29F8(LGRect *r, long n, long sel)
{
    long h = r->lr.y - r->ul.y, i, top;
    uchar cur = (uchar)sel;
    short mx = 0, my = 0, x0;
    LGRect R;

    pop_open(r, n, (uchar)sel, h);
    while (!(shell_pop_flags & 1)) {
        GameTimeFrame();
        if (!(shell_pop_flags & 2))
            continue;
        x0 = r->ul.x;
        top = r->ul.y;
        pop_frame(r, &mx, &my);
        top -= (uchar)sel * h;
        for (i = 0; i < (uchar)n; i++) {
            R.ul.x = x0;
            R.lr.x = r->lr.x;
            R.ul.y = top + i * h;
            R.lr.y = top + (i + 1) * h;
            if (hover(&R, mx, my, x0)) {
                if (!shell_2B6B24((schar)i) || (uchar)sel == i)
                    shell_2C3060(&R, 0xBF);
                else
                    shell_2C3060(&R, 0x48);
                if ((uchar)sel == i)
                    grd_canvas->gc.fcolor = 0xF2;
                else
                    soldier_colour(i);
                soldier_line(&R, i);
                shell_pop_sel = R;
                cur = (uchar)i;
                shell_pop_cur = i;
                if (shell_2B6B24((schar)cur) && (uchar)sel != i)
                    shell_pop_ok = 0;
                else
                    shell_pop_ok = 1;
                if ((uchar)sel != cur)
                    shell_pop_flags |= 4;
            } else {
                if ((uchar)sel == i) {
                    shell_2C3060(&R, 0x7F);
                    grd_canvas->gc.fcolor = 0xF2;
                } else {
                    shell_2C3060(&R, 0x48);
                    soldier_colour(i);
                }
                soldier_line(&R, i);
            }
        }
        pop_show();
    }
    return pop_close(r, mx, (uchar)sel, cur);
}

void shell_2C2F84(uchar c)
{
    grs_canvas *cv = grd_canvas;

    switch (c) {
    case 0: cv->gc.fcolor = 0xF2; break;
    case 1: cv->gc.fcolor = 0x8F; break;
    case 2: cv->gc.fcolor = 0x17; break;
    case 3: cv->gc.fcolor = 0xEB; break;
    }
    grd_canvas = cv;
}

/* the kit's two letters (string 0x16F009B, 2 per kit from 1), right aligned in R */
void shell_2C2FC8(long kit, LGRect *r)
{
    char *t = RefGet(0x16F009B), b[3];
    short h, w;

    b[0] = t[2 * kit - 2];
    b[1] = t[2 * kit - 1];
    b[2] = 0;
    h = gr_font_string_height(ResGet(0x167), b);
    w = gr_font_string_width(ResGet(0x167), b);
    GfxDrawText(0x167, b, r->lr.x - w - 2, r->ul.y + (r->lr.y - r->ul.y - h) / 2);
}

void shell_2C3060(LGRect *r, long col)
{
    uchar a, c;

    grd_canvas->gc.fcolor = (uchar)col;
    fill_rect(r->ul.x, r->ul.y, r->lr.x, r->lr.y);
    if ((uchar)col == 0xBF) {
        a = 0xB1;
        c = 0xC0;
    } else {
        a = 0x4E;
        c = 0x40;
    }
    grd_canvas->gc.fcolor = a;
    gen_hline(r->ul.x, r->ul.y, (short)(r->lr.x - 1));
    gen_vline(r->ul.x, r->ul.y, (short)(r->lr.y - 1));
    grd_canvas->gc.fcolor = c;
    gen_hline(r->ul.x, (short)(r->lr.y - 1), (short)(r->lr.x - 1));
    gen_vline((short)(r->lr.x - 1), (short)(r->ul.y + 1), (short)(r->lr.y - 1));
}

uchar shell_2C3124(TnEvent *ev)
{
    short mx, my;
    LGRect *s = &shell_pop_sel;
    short key;

    switch (ev->type) {
    case 1:
        if (shell_pop_ok)
            shell_pop_flags |= 3;
        return 1;
    case 2:
        if ((shell_pop_flags & 4) && shell_pop_ok)
            shell_pop_flags |= 3;
        return 1;
    case 4:
        shell_pop_flags |= 9;
        return 1;
    case 0x40:
        mx = ((short *)tnm_state_ptr)[0];
        my = ((short *)tnm_state_ptr)[1];
        if ((mx < s->ul.x || mx > s->lr.x || my < s->ul.y || my > s->lr.y) && !shell_pop_out) {
            shell_pop_flags |= 2;
            shell_pop_out = 1;
        } else if (mx > s->ul.x && mx < s->lr.x && my > s->ul.y && my < s->lr.y && shell_pop_out) {
            shell_pop_flags |= 2;
            shell_pop_out = 0;
        }
        if (my > s->lr.y || my < s->ul.y)
            shell_pop_flags |= 2;
        return 1;
    case 0x100:
        key = ev_key(ev);
        if (key == 0x1B)
            shell_pop_flags |= 9;
        else if (key == 0x458 || key == 0x478) {
            main_245334();
            shell_pop_flags |= 9;
        }
        return 1;
    }
    return 0;
}
