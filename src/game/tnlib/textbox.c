/* textbox.c -- TN text menus and text boxes on tnui regions (Terra Nova v1.09; names ours, no
 * assert path).  Decompiled from __FF.EXE.
 *
 *   0x2ECCF0 tbox_draw_back     0x2ECE84 tmenu_create      0x2ECF5C tmenu_run
 *   0x2ECFD0 tmenu_key_handler  0x2ED040 tmenu_set_items   0x2ED098 tmenu_event
 *   0x2ED16C tmenu_draw         0x2ED220 tmenu_item_at     0x2ED270 tmenu_draw_item
 *   0x2ED360 tbox_create        0x2ED3A0 tbox_set_text     0x2ED490 tbox_event
 *   0x2ED4B0 tbox_draw
 *
 * Both widgets are tnui regions (viewport_2E55D8, batch T2c) of a class whose event and draw
 * methods are here (tmenu_class 0x35F9D4, tbox_class 0x35F9E4).  They draw in the current
 * style tbox_style: a background (bitmap, image or filled rect), then a bevel or a 4-ring frame.
 * A menu is a column of strings, one hot key each; tmenu_run shows one modally and returns
 * the key chosen (ESC 0x1B when cancelled).
 */
#include "t1_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
TBoxStyle tbox_styles[2] = {
    { 1, 0, 0x1F8, 0, 0x24, 0x1D, { 0x1D, 0 }, { 0x20, 0x28, 0x28, 0x20 } },
    { 2, 0, 0x1F8, 0, 0x24, 0x1D, { 0x1D, 0 }, { 0x29, 0x28, 0x27, 0x26 } },
};
TBoxStyle *tbox_style = &tbox_styles[0];
short tbox_margin_x = 3, tbox_margin_y = 2;
TBoxClass tmenu_class = { 0x87, 0x41, 0x80, WATCOM_FN(tmenu_event), WATCOM_FN(tmenu_draw), 0x6F70 };
TBoxClass tbox_class = { 0x7E, 0, 0x10, WATCOM_FN(tbox_event), WATCOM_FN(tbox_draw), 0 };
TMenu *tmenu_modal;
uchar tmenu_key;
uchar tmenu_running;
#endif

#define FCOLOR(c) (grd_canvas->gc.fcolor = (c))
#define MOUSE_POS() (*(LGPoint *)tnm_state_ptr)

/* Draws the background and border of a region in its own canvas (current canvas). */
void tbox_draw_back(TBoxRegion *r, TBoxStyle *st, short sel)
{
    if (st->flags & TBS_BITMAP) {
        grs_bitmap *bm = RefGet(st->back);
        bm->bits = (uchar *)bm + 0x1C;
        WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, 0, 0);
    } else if (st->flags & TBS_IMAGE) {
        gfx_2E4238(st->back, 0, 0);
    } else {
        FCOLOR(st->bgcolor);
        WATCOM_CALL(grd_canvas_table[16 /* DRAW_RECT */], 0, 0, r->cnv.bm.w, r->cnv.bm.h);
    }

    if (st->flags & TBS_BEVEL) {
        short w1 = r->cnv.bm.w - 1, h1 = r->cnv.bm.h - 1;
        FCOLOR(st->border[sel]);
        gen_hline(0, 0, w1 - 1);
        gen_vline(0, 0, h1 - 1);
        FCOLOR(st->border[(short)(sel ^ 1)]);
        gen_hline(0, h1, w1);
        gen_vline(w1, 0, h1);
    } else if (st->flags & TBS_FRAME) {
        short x0 = 0, y0 = 0, x1 = grd_canvas->bm.w, y1 = grd_canvas->bm.h;
        uchar *c;
        for (c = st->border; c != st->border + 4; c++) {
            FCOLOR(*c);
            WATCOM_CALL(grd_canvas_table[18 /* DRAW_BOX */], x0, y0, x1, y1);
            x0++, y0++, x1--, y1--;
        }
    }
}

/* Item index under a screen point, or -1. */
short tmenu_item_at(TMenu *m, LGPoint pt)
{
    short dy;
    long i;

    if (!RectTestPt(&m->r.rect, pt))
        return -1;
    dy = pt.y - (short)(m->r.rect.ul.y + m->ymargin);
    i = (long)dy / m->lineh;
    if ((short)i < 0 || (short)i >= m->items.count)
        return -1;
    return (short)i;
}

static void tmenu_init_items(TMenu *m, TMenuItems *items)
{
    grs_font *f;

    if (items != NULL)
        m->items = *items;
    else
        m->items.count = 0;
    f = ResGet(m->r.style->font);
    m->lineh = f->h;
    m->hilite = tmenu_item_at(m, MOUSE_POS());
    viewport_2E5848((TnRegion *)m, NULL);
}

/* Creates a menu region from the region parameters (class set here). */
TMenu *tmenu_create(void *params, TMenuItems *items, uchar hicolor)
{
    TMenu *m;
    short border;

    *(void **)params = &tmenu_class;
    m = (TMenu *)viewport_2E55D8(params);
    m->r.style = tbox_style;
    m->hicolor = hicolor;
    border = (m->r.style->flags & TBS_FRAME) ? 4 : 1;
    m->xmargin = tbox_margin_x + border;
    m->ymargin = tbox_margin_y + border;
    m->r.cnv.gc.bcolor = m->r.style->bgcolor;
    tmenu_init_items(m, items);
    return m;
}

/* Shows a menu modally until a hot key or ESC; returns the key (0x1B = cancelled). */
uchar tmenu_run(void *params, TMenuItems *items, uchar hicolor)
{
    tmenu_modal = tmenu_create(params, items, hicolor);
    viewport_2E57F0((TnRegion *)tmenu_modal);
    viewport_key_focus = (TnRegion *)tmenu_modal;
    KeyAddHandler(WATCOM_FN(tmenu_key_handler), NULL, 0, 0);
    tmenu_key = 0x1B;
    tmenu_running = TRUE;
    do
        GameTimeFrame();
    while (tmenu_running);
    KeyRemoveHandler(WATCOM_FN(tmenu_key_handler), NULL);
    viewport_key_focus = NULL;
    viewport_2E575C((TnRegion *)tmenu_modal);
    return tmenu_key;
}

/* Key handler of tmenu_run: ESC cancels, a hot key chooses. */
bool tmenu_key_handler(void *ev)
{
    TMenu *m = tmenu_modal;
    ushort code;
    long i;

    if (*(ulong *)ev != 0x100)
        return FALSE;
    code = *(ushort *)((uchar *)ev + 4);
    if (code == 0x1B) {
        tmenu_running = FALSE;
        return TRUE;
    }
    for (i = 0; i < m->items.count; i++) {
        if ((uchar)m->items.keys[i] == code) {
            tmenu_key = (uchar)code;
            tmenu_running = FALSE;
            return TRUE;
        }
    }
    return FALSE;
}

/* Replaces the items of a menu and redraws it. */
void tmenu_set_items(TMenu *m, TMenuItems *items)
{
    tmenu_init_items(m, items);
}

/* Region event method: click chooses (posts the item's key), motion highlights. */
bool tmenu_event(TMenu *m, void *ev)
{
    ulong type = *(ulong *)ev;
    LGPoint pt = *(LGPoint *)((uchar *)ev + 8);
    short i, old;

    if (type == 1) {
        i = tmenu_item_at(m, pt);
        tnkey_dispatch_code(i >= 0 ? (uchar)m->items.keys[i] : 0x1B);
        return TRUE;
    }
    if (type == 0x40) {
        if (m->hicolor) {
            i = tmenu_item_at(m, pt);
            old = m->hilite;
            if (i != old) {
                m->hilite = i;
                if (old >= 0)
                    tmenu_draw_item(m, old);
                if (i >= 0)
                    tmenu_draw_item(m, i);
            }
        }
        return TRUE;
    }
    if (type == 0x800000) {
        old = m->hilite;
        if (old >= 0) {
            m->hilite = -1;
            tmenu_draw_item(m, old);
        }
        return TRUE;
    }
    return FALSE;
}

/* Region draw method (into the region's canvas, already current). */
void tmenu_draw(TMenu *m)
{
    long x, y, i;

    tbox_draw_back(&m->r, m->r.style, 0);
    x = m->xmargin;
    y = m->ymargin;
    for (i = 0; i < m->items.count; i++) {
        if (m->hicolor && i == m->hilite)
            FCOLOR(m->hicolor);
        else
            FCOLOR(m->r.style->textcolor);
        GfxDrawText(m->r.style->font, m->items.strings[i], x, y);
        y += m->lineh;
    }
}

/* Redraws one item (highlight change). */
void tmenu_draw_item(TMenu *m, int i)
{
    LGRect r;

    r.ul.x = m->xmargin;
    r.ul.y = (short)(m->lineh * i) + m->ymargin;
    r.lr.x = m->r.cnv.bm.w - m->xmargin;
    r.lr.y = r.ul.y + m->lineh;
    if (m->r.type != 3) {
        if (m->r.type == 0) {
            MouseSaveHide();
            MouseHideRect(&m->r.rect);
        }
        gr_set_canvas(&m->r.cnv);
        if (m->hicolor && (short)i == m->hilite)
            FCOLOR(m->hicolor);
        else
            FCOLOR(m->r.style->textcolor);
        GfxDrawText(m->r.style->font, m->items.strings[(short)i], r.ul.x, r.ul.y);
        if (m->r.type == 0)
            MouseRestoreHide();
    }
    if (m->r.type != 0)
        viewport_2E5BC8((TnRegion *)m, &r);
}

/* Creates a text box region showing a copy of text. */
TBox *tbox_create(void *params, char *text, long margins, uchar flags)
{
    TBox *b;

    *(void **)params = &tbox_class;
    b = (TBox *)viewport_2E55D8(params);
    *(long *)&b->mx = margins;
    b->r.style = tbox_style;
    b->tflags = flags;
    b->r.cnv.gc.bcolor = b->r.style->bgcolor;
    b->text = NULL;
    tbox_set_text(b, text);
    return b;
}

/* Replaces the text (NULL clears it), lays it out and redraws. */
void tbox_set_text(TBox *b, char *text)
{
    grs_font *f;
    short w, h;

    if (b->text != NULL) {
        strpool_free(b->text);
        b->text = NULL;
    }
    if (text != NULL) {
        b->text = strpool_dup(text);
        f = ResLock(b->r.style->font);
        if (b->tflags & 1)
            b->nlines = gr_font_string_wrap(f, b->text, (short)(b->r.cnv.bm.w - 2 * b->mx));
        b->x = b->mx;
        b->y = b->my;
        if (b->tflags & 6) {
            gr_font_string_size(f, b->text, &w, &h);
            if (b->tflags & 2)
                b->x = (b->r.cnv.bm.w - w) / 2;
            if (b->tflags & 4)
                b->y = (b->r.cnv.bm.h - h) / 2;
        }
        ResUnlock(b->r.style->font);
    }
    viewport_2E5848((TnRegion *)b, NULL);
}

/* Region event method: frees the text when the region is destroyed (0x100000). */
bool tbox_event(TBox *b, void *ev)
{
    if (*(ulong *)ev != 0x100000)
        return FALSE;
    if (b->text != NULL)
        strpool_free(b->text);
    return TRUE;
}

/* Region draw method. */
void tbox_draw(TBox *b)
{
    tbox_draw_back(&b->r, b->r.style, 0);
    if (b->text != NULL) {
        FCOLOR(b->r.style->textcolor);
        GfxDrawText(b->r.style->font, b->text, b->x, b->y);
    }
}
