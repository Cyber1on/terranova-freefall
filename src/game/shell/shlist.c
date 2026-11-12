/* shlist.c -- M097 0x2C0304-0x2C1520 and 0x2C32D4-0x2C3440: the shell's scrolling lists.
 *
 *   0x2C0304 shell_2C0304 squad strip           0x2C0418 shell_2C0418 header (tabs, counts)
 *   0x2C066C shell_2C066C fill the four lists   0x2C06D8 shell_2C06D8 tab button
 *   0x2C072C shell_2C072C list arrows           0x2C089C shell_2C089C text arrows
 *   0x2C0A08 shell_2C0A08 fill a list           0x2C0ACC shell_2C0ACC draw a list
 *   0x2C0B7C shell_2C0B7C list button           0x2C0C78 shell_2C0C78 item chosen
 *   0x2C0DE4 shell_2C0DE4 fill the mission list 0x2C0E74 shell_2C0E74 draw it
 *   0x2C0E9C shell_2C0E9C mission chosen        0x2C0F34 shell_2C0F34 text screen events
 *   0x2C11B0 shell_2C11B0 scroll the text       0x2C12B4 shell_2C12B4 (no callers)
 *   0x2C12DC shell_2C12DC wake with the screen  0x2C12EC shell_2C12EC unit colour
 *   0x2C1334 shell_2C1334 (no callers)          0x2C136C shell_2C136C unit card (no callers)
 *   0x2C32D4 shell_2C32D4 unread entries        0x2C3390 shell_2C3390 the mission has notes
 *
 * A list (ShList) holds item numbers listed from the end: line k of the page shows
 * items[last - top - k].  The lists of resources 0x5B2..0x5B4 hold the entries (5 bytes:
 * mission, Ref) of the missions reached so far (campaign position order); "last" counts only
 * the entries of missions up to camp_pos, minus one.
 *
 * Quirks kept:
 *  - shell_2C0E74 always draws the arrows of shell_list_ms whatever list it is given;
 *  - shell_2C0418 / 2C32D4 index the "read" bits with signed item / 8 and item % 8.
 */
#include "g10_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar shell_tab_of[0x24] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 1, 1, 1, 2, 2, 3, 3, 5, 5,
    /* 0x35CFC4: the shorts the six tab buttons wake with (index 1..6) */
    0x05, 0x05, 0x0D, 0x00, 0x10, 0x00, 0x12, 0x00, 0x00, 0x00, 0x14, 0x00, 0xFF, 0xFF,
};
long shell_35CFD2 = 0x00C9023E;
#endif

#define CLEAR 8
#define FONT_STRING 315

static void font_string(grs_font *f, char *s, long x, long y)
{
    WATCOM_CALL(grd_canvas_table[FONT_STRING], (long)f, (long)s, x, y);
}

/* a FrameDesc resource: bits follow the 0x1C-byte header */
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

static void frame_pal(Ref ref, Id id)
{
    uchar *bm = RefGet(ref), *p;

    if (*(long *)(bm + 0x18)) {
        p = (uchar *)ResGet(id) + *(long *)(bm + 0x18);
        gr_set_pal(*(short *)p, *(short *)(p + 2), p + 4);
    }
}

/* redraw the current screen */
static void redraw(void)
{
    shell_2B6CDC(shell_screen);
    ((void (WATCOMCALL *)(void))shell_scr_init[shell_screen])();
    uiwidget_2B60CC();
}

static LGRect rect(long x0, long y0, long x1, long y1)
{
    LGRect r;

    r.ul.x = x0;
    r.ul.y = y0;
    r.lr.x = x1;
    r.lr.y = y1;
    return r;
}

/* entries of a list not yet marked read in bits[] */
static long unread(ShList *l, uchar *bits)
{
    long i, it, n = 0;

    for (i = 0; i < l->last + 1; i++) {
        it = l->items[i];
        if (!((schar)bits[it / 8] & (1 << (it % 8))))
            n++;
    }
    return n;
}

/* ------------------------------------------------------------------ header */

void shell_2C0304(void)
{
    long i, x = 0xAF;
    schar v;
    Ref ref;
    char *s;

    for (i = 0; i < shell_count; i++) {
        v = game_391E50[i].voice;
        draw_frame(v | 0xD70000, (short)(x + 3), 0x5D);
        grd_canvas->gc.fcolor = 0xF2;
        ref = 0x16E0068 + camp_soldiers[(schar)voice_soldier[v]][5];
        s = RefLock(ref);
        font_string(uiw_font, s, (short)(x + (0x48 - gr_font_string_width(uiw_font, s)) / 2), 0x92);
        x += 0x79;
        ResUnlock((ulong)ref >> 16);
    }
}

void shell_2C0418(void)
{
    char buf[0x20];
    long i, x = 0x24;
    LGRect r;

    for (i = 0; i < 6; i++) {
        if (shell_tab_of[shell_screen] == i + 1)
            draw_uframe(0xD90006 + i, (short)x, 0x168);
        else
            draw_uframe(0xD90000 + i, (short)x, 0x168);
        r = rect(x, 0x168, x + 0x58, 0x1B0);
        x += 0x60;
        uiwidget_2B63D8(i + 1, &r);
    }
    grd_canvas->gc.fcolor = 0x17;
    lg_sprintf(buf, "New: %d", unread(&shell_list_5b2, camp_43E660));
    font_string(shell_font, buf, 0x29, 0x190);
    lg_sprintf(buf, "Total: %d", shell_list_5b2.last + 1);
    font_string(shell_font, buf, 0x29, 0x1A4);
    i = unread(&shell_list_5b3, camp_43E668);
    grd_canvas->gc.fcolor = 0x17;
    lg_sprintf(buf, "%s: %d", RefGet(0x5AB0180), i);
    font_string(shell_font, buf, 0x89, 0x190);
    lg_sprintf(buf, "%s: %d", RefGet(0x5AB00DA), shell_list_5b3.last + 1);
    font_string(shell_font, buf, 0x89, 0x1A4);
}

void shell_2C066C(void)
{
    long f = ResOpenResFile("respterm.res", 0, 0);

    shell_2C0A08(&shell_list_5b2, 0x5B2, 0xE);
    shell_2C0A08(&shell_list_5b3, 0x5B3, 0xE);
    shell_2C0A08(&shell_list_5b4, 0x5B4, 0xE);
    shell_2C0DE4(&shell_list_ms, 0xE);
    ResCloseFile(f);
}

/* tab buttons 1..6: wake the shell with the code of the tab (0x63: none) */
uchar shell_2C06D8(long code)
{
    long v = *(short *)&shell_tab_of[0x16 + 2 * code];

    if (v != 0x63) {
        click(v == -1 ? 0x786 : 0x784);
        uiwidget_2B60B4(v);
    }
    return 1;
}

/* ------------------------------------------------------------------ arrows */

static void arrows(long id, long pos, long up, long down)
{
    grs_bitmap *bm = RefGet(0xD20006);
    short w = bm->w, h = bm->h, x = (short)pos, y = (short)(pos >> 16);
    LGRect r;

    draw_frame(up ? 0xD20006 : 0xD2000A, x, y);
    r = rect(x, y, (short)(x + w), (short)(y + h));
    uiwidget_2B63D8(id, &r);
    y += h + 2;
    draw_frame(down ? 0xD20007 : 0xD2000B, x, y);
    r = rect(x, y, (short)(x + w), (short)(y + h));
    uiwidget_2B63D8(id + 1, &r);
}

void shell_2C072C(ShList *l, long id, long pos)
{
    arrows(id, pos, l->top > 0, l->page + l->top < l->last);
}

void shell_2C089C(ShList *l, long id, long pos)
{
    arrows(id, pos, l->scroll > 0, l->scroll < l->lines - 5);
}

/* ------------------------------------------------------------------ lists */

void shell_2C0A08(ShList *l, long resid, long page)
{
    long n, pos, i, k = 0;
    schar *p, *e;

    l->page = page;
    l->resid = resid;
    n = (ulong)gResDesc[(ushort)resid].size / 5;
    l->count = n;
    p = ResGet((ushort)resid);
    l->last = 0;
    for (pos = 1; pos < 0x3E; pos++) {
        if (!camp_missions[pos] || k >= n)
            break;
        for (i = 0, e = p; i < n; i++, e += 5)
            if (camp_level[*e] == pos) {
                l->items[k++] = i;
                if (pos <= camp_pos)
                    l->last++;
            }
    }
    l->top = 0;
    shell_rep_btn = 0;
    l->last--;
}

void shell_2C0ACC(ShList *l, short *tbl, void (WATCOMCALL *fn)(long item, long y))
{
    long i, stop, y, k = 0;
    LGRect r;

    grd_canvas->gc.font = uiw_font;
    i = l->last - l->top;
    stop = i - tbl[1];
    if (stop < 0)
        stop = 0;
    y = tbl[4];
    for (; i >= stop; i--, k++) {
        fn(l->items[i], y);
        r = rect(tbl[2], y, tbl[3], y + 0xC);
        uiwidget_2B63D8(tbl[0] + k, &r);
        y += tbl[5];
    }
}

/* list buttons: 7 / 8 scroll the list, 1..6 are the tabs */
uchar shell_2C0B7C(ShList *l, long code)
{
    if (code == 7) {
        if (l->top > 0) {
            click(0x78D);
            l->top--;
            uiwidget_2B60B4(shell_screen);
        } else
            click(0x785);
        return 1;
    }
    if (code == 8) {
        if (l->page + l->top < l->last) {
            click(0x78D);
            l->top++;
            uiwidget_2B60B4(shell_screen);
        } else
            click(0x785);
        return 1;
    }
    if (code > 6)
        return 0;
    return shell_2C06D8(code);
}

/* list line button `code` (9..) chosen: its text into shell_text */
void shell_2C0C78(ShList *l, long code, long base, long wake1, long wake2)
{
    long ref, r;
    char *s;

    shell_sel = l->last - (code - 9 + l->top);
    ref = *(long *)((uchar *)ResGet(l->resid) + shell_sel * 5 + 1);
    if (ref && gResDesc2[(ulong)ref >> 16].type == 0x11) {
        uiwidget_2B60B4(wake2);
        return;
    }
    if (l == &shell_list_5b4) {
        r = ((long *)ResGet(0x5B5))[l->items[shell_sel]];
        shell_text[0] = 0;
        for (;;) {
            s = RefGet(r++);
            if (!strcmp(s, "END"))
                break;
            g1c_strcat(shell_text, s);
        }
    } else
        g1c_strcpy(shell_text, RefGet(l->items[shell_sel] + base));
    gr_font_string_wrap(shell_font, shell_text, 0x1AA);
    l->lines = gr_font_string_height(shell_font, shell_text) / shell_font->h;
    l->scroll = 0;
    uiwidget_2B60B4(wake1);
}

/* the missions played: campaign positions 3..camp_pos */
void shell_2C0DE4(ShList *l, long page)
{
    long i;
    schar lv;

    l->page = page;
    l->resid = 0;
    if (camp_pos <= 2)
        l->last = 0;
    else {
        lv = camp_level[game_359710];
        l->last = (short)camp_pos - lv;
        if (lv <= 2)
            l->last -= 3 - lv;
    }
    l->count = l->last;
    for (i = 0; i < l->count; i++)
        l->items[i] = i;
    l->top = 0;
    shell_rep_btn = 0;
    l->last--;
}

void shell_2C0E74(ShList *l, short *tbl, void (WATCOMCALL *fn)(long item, long y))
{
    shell_2C0ACC(l, tbl, fn);
    if (l->count >= l->page)
        shell_2C072C(&shell_list_ms, 7, shell_35CFD2);
}

/* mission list line chosen: load its statistics, show the debriefing (screen 21) */
void shell_2C0E9C(ShList *l, long code)
{
    shell_sel = l->last - (code - 9 + l->top);
    shell_35CF70 = 1;
    mstat_2C665C((uchar)((uchar)shell_sel + camp_level[game_359710]));
    MouseHide();
    WATCOM_CALL(grd_canvas_table[CLEAR], 0);
    MouseShow();
    frame_pal(0x1C00000, 0x1C0);
    uiwidget_2B5C90();
    shell_2B6CDC(0x15);
    uiwidget_2B60B4(0x15);
}

/* events of a text screen (19 and the like): 7 / 8 and page keys scroll, 9 goes back */
uchar shell_2C0F34(TnEvent *ev, ShList *l, long wakecode)
{
    long btn = uiwidget_2B601C(*(long *)&ev->pt);
    short key;

    switch (ev->type) {
    case 0x40:
    case 0x80:
        if (btn == shell_rep_btn) {
            if ((long)tn_ticks >= (long)shell_rep_tick)
                shell_2C11B0(l, btn);
        } else
            shell_rep_btn = 0;
        return 1;
    case 0x100:
        key = *(short *)&ev->pad;
        if (key == 0x849 || key == 0x2849) {
            if (l->scroll > 0) {
                click(0x78D);
                l->scroll -= 10;
                if (l->scroll < 0)
                    l->scroll = 0;
                redraw();
            } else
                click(0x785);
            return 1;
        }
        if (key == 0x851 || key == 0x2851) {
            if (l->scroll < l->lines - 5) {
                click(0x78D);
                l->scroll += 10;
                if (l->scroll > l->lines - 5)
                    l->scroll = l->lines - 5;
                redraw();
            } else
                click(0x785);
            return 1;
        }
        return 0;
    case 1:
        shell_rep_btn = 0;
        if (!btn)
            return 0;
        if (btn == 7 || btn == 8) {
            shell_2C11B0(l, btn);
            return 1;
        }
        if (btn == 9) {
            click(0x78F);
            uiwidget_2B60B4(wakecode);
            return 1;
        }
        if (btn > 6)
            return 0;
        return shell_2C06D8(btn);
    case 2:
        shell_rep_btn = 0;
        return 1;
    }
    return 0;
}

void shell_2C11B0(ShList *l, long code)
{
    if (code == 7) {
        if (l->scroll > 0) {
            click(0x78D);
            l->scroll--;
            redraw();
        } else
            click(0x785);
    } else if (code == 8) {
        if (l->scroll < l->lines - 5) {
            click(0x78D);
            l->scroll++;
            redraw();
        } else
            click(0x785);
    }
    shell_rep_btn = code;
    shell_rep_tick = tn_ticks + 100;
}

uchar shell_2C12B4(uchar *p, long v)
{
    long i;

    for (i = 0; i < 4; i++)
        if ((schar)p[9 + i] == v)
            return 1;
    return 0;
}

void shell_2C12DC(void)
{
    uiwidget_2B60B4(shell_screen);
}

/* colour of a unit type: 0x8F / 0xBF when selected, -1 / 0x48 not (by its damage flag) */
long shell_2C12EC(long unit, long sel)
{
    UnitTabRec rec;

    damage_27E200(&rec, unit);
    if (((uchar *)&rec)[0xB])
        return (uchar)sel ? 0x8F : -1;
    return (uchar)sel ? 0xBF : 0x48;
}

long shell_2C1334(long a, long b)
{
    UnitTabRec rb, ra;

    damage_27E200(&ra, a);
    damage_27E200(&rb, b);
    if (((uchar *)&ra)[0xB] || ((uchar *)&rb)[0xB])
        return a;
    return b;
}

void shell_2C136C(long unit, long x, long y)
{
    char buf[0x3C];
    UnitTabRec rec;
    long k, mask;
    Ref ref;
    char *a, *b, *s;
    short sx, sy, sx2;

    damage_27E200(&rec, unit);
    b = RefGet(0x5AB0122);
    a = RefGet(0x16E0030 + unit);
    sy = (short)y + 4;
    sx = (short)x + 5;
    sprintf(buf, "%s %s:", a, b);
    grd_canvas->gc.fcolor = 0xF2;
    grd_canvas->gc.font = uiw_font;
    font_string(grd_canvas->gc.font, buf, sx, sy);
    sy += 0xE;
    sx2 = sx + 0x78;
    sx += 0x14;
    for (k = 0, ref = 0x16E0068; k < 6; k++, ref++) {
        mask = k ? 1 << (k - 1) : 0;
        if (!(((schar *)&rec)[0xA] & mask))
            continue;
        font_string(grd_canvas->gc.font, RefLock(ref), sx, sy);
        ResUnlock((ushort)(ref >> 16));
        s = RefLock(0x5AB0124);
        if ((schar)(((uchar *)&rec)[0xA] & 0x1F) / mask > 1)
            font_string(grd_canvas->gc.font, s, sx2, sy);
        sy += 0xE;
        ResUnlock(0x5AB);
    }
}

/* ------------------------------------------------------------------ notes */

long shell_2C32D4(void)
{
    if (unread(&shell_list_5b2, camp_43E660))
        return 1;
    if (unread(&shell_list_5b3, camp_43E668))
        return 1;
    return 0;
}

static long has_mission(Id id)
{
    long n = (ulong)gResDesc[id].size / 5, i;
    schar *p = ResGet(id);

    for (i = 0; i < n; i++, p += 5)
        if (*p == game_359708)
            return 1;
    return 0;
}

uchar shell_2C3390(void)
{
    long f = ResOpenResFile("respterm.res", 0, 0);

    if (has_mission(0x5B2) || has_mission(0x5B3)) {
        ResCloseFile(f);
        return 1;
    }
    ResCloseFile(f);
    return 0;
}
