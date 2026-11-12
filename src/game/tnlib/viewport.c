/* viewport.c -- Terra Nova's screen regions ("viewports", tnui regions; names ours, no assert
 * path; the string "ViewportCheckPtr" gives the module name).  Decompiled from __FF.EXE.
 *
 *   0x2E55D8 viewport_2E55D8 (create)     0x2E575C viewport_2E575C (destroy)
 *   0x2E57F0 viewport_2E57F0 (show)       0x2E582C viewport_2E582C (hide)
 *   0x2E5848 viewport_2E5848 (draw now)   0x2E5954 viewport_2E5954 (move / resize)
 *   0x2E5A64 tnui_region_at               0x2E5B6C tnui_key_handler
 *   0x2E5BB0 tnui_region_event            0x2E5BC8 viewport_2E5BC8 (update, relative rect)
 *   0x2E5C94 viewport_2E5C94 (update, absolute rect)
 *   0x2E5CC8 tnui_add_dirty_rect          0x2E5DB0 tnui_add_blit_rect
 *   0x2E5E98 tnui_redraw_rect             0x2E5FAC tnui_redraw_rect_before
 *   0x2E6020 tnui_redraw_rect_after       0x2E6094 tnui_clear_back
 *   0x2E60E0 tnui_blit_all                0x2E60F4 tnui_task_60f4 (frame task 18)
 *   0x2E6174 tnui_task_6174 (frame task 0) 0x2E62D0 tnui_mouse_handler
 *   0x2E6304 tnui_save_under              0x2E6398 tnui_restore_under
 *   0x2E6410 tnui_draw_region_rect        0x2E64BC tnui_copy_region_rect
 *   0x2E6674 ViewportCheckPtr
 *
 * Regions live in the priority list tnui_list (tnui_init, src/tnui/tnmouse.c); the last node
 * is the topmost.  Drawing goes through the back buffer tnui_back_canvas: changes add
 * "dirty" rectangles (regions of type 2/3 redraw them into the back buffer) and "blit"
 * rectangles (copied from the back buffer to the screen).  Frame task 18 (tnui_task_60f4)
 * frees destroyed regions and redraws the dirty rectangles; frame task 0 (tnui_task_6174)
 * blits, keeping the mouse cursor drawn.  Each list keeps at most one rectangle that
 * contains another.  Mouse events go to the topmost visible region under the mouse (or the
 * capture region viewport_key_focus), with enter/leave events; key events to every live
 * region whose mask matches, topmost first, until one takes it.
 *
 * Quirks kept:
 *  - The rectangle lists have 16 entries and no bounds check.
 *  - viewport_2E5848 passes a non-NULL rectangle of a type 1/2 region unoffset (region
 *    relative) to the blit/dirty list; type 3 offsets it.
 *  - Event records are 12 bytes with +4 never written (stack garbage in the original; ours
 *    are zeroed) and +8 written only for the mouse enter/leave/move events.
 *  - create does not clear the node (LlistAddQueue reuses freed nodes): vflags keeps its
 *    upper bits, user/extra keep old contents.
 *  - tnui_region_event / tnui_mouse_handler return the event method's whole eax; when they
 *    return FALSE themselves only al is cleared (tnui_mouse_handler leaves the region
 *    pointer in the upper bytes; we return 0).
 *  - ViewportCheckPtr passes no argument for the %x of its message.
 *  - tnui_copy_region_rect resets the fill type but not the fill parameter of the back canvas.
 */
#include "t2_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
TnRegion *tnui_hover;
TnRegion *viewport_key_focus;
short tnui_blit_count, tnui_dirty_count, tnui_dead_count;
LGRect tnui_blit_rects[TNUI_MAX_RECTS], tnui_dirty_rects[TNUI_MAX_RECTS];
char viewport_check_msg[] = "ViewportCheckPtr: $%x not a valid viewport ptr!\n";
#endif

#define HEAD ((TnRegion *)&tnui_list.head)
#define TAIL ((TnRegion *)&tnui_list.tail)
#define FIRST ((TnRegion *)tnui_list.head.pnext)
#define LAST ((TnRegion *)tnui_list.tail.pprev)
#define SEND(r, ev) WATCOM_CALL((r)->event, (long)(r), (long)(ev))

static inline int visible_sect(TnRegion *n, LGRect *d)
{
    return n->rect.ul.y < d->lr.y && n->rect.lr.y > d->ul.y
        && n->rect.ul.x < d->lr.x && n->rect.lr.x > d->ul.x;
}

static inline void offset_rect(LGRect *out, LGRect *in, TnRegion *r)
{
    out->ul.x = in->ul.x + r->rect.ul.x;
    out->ul.y = in->ul.y + r->rect.ul.y;
    out->lr.x = in->lr.x + r->rect.ul.x;
    out->lr.y = in->lr.y + r->rect.ul.y;
}

TnRegion *viewport_2E55D8(TnRegionParams *p)
{
    TnRegionClass *cls = p->cls;
    TnRegion *r = LlistAddQueue(&tnui_list, p->prio);
    grs_canvas *c = &r->cnv;
    short w, h;

    r->type = p->type;
    r->rect = p->rect;
    r->fill_type = 0;
    r->vflags = (((r->vflags & ~TNRF_TRANSP) | ((p->flags & 1) << 1)) | TNRF_HIDDEN) & ~TNRF_DEAD;
    r->mask = cls->mask;
    r->event = cls->event;
    r->draw = cls->draw;
    w = r->rect.lr.x - r->rect.ul.x;
    h = r->rect.lr.y - r->rect.ul.y;
    switch (r->type) {
    case TNR_SCREEN:
        gr_init_sub_canvas(grd_screen_canvas, c, r->rect.ul.x, r->rect.ul.y, w, h);
        break;
    case TNR_BACK:
    case TNR_DRAWN:
        gr_init_sub_canvas(&tnui_back_canvas, c, r->rect.ul.x, r->rect.ul.y, w, h);
        c->bm.flags = (r->vflags & TNRF_TRANSP) != 0;
        break;
    case TNR_BITMAP:
        c->bm.w = w;
        c->bm.h = h;
        c->bm.row = c->bm.w;
        c->bm.bits = Calloc(c->bm.h * (ulong)c->bm.row);
        c->bm.type = BMT_FLAT8;
        c->bm.flags = (r->vflags & TNRF_TRANSP) != 0;
        c->bm.align = 0;
        c->gc = grd_defgc;
        c->gc.clip.f.right = c->bm.w << 16;
        c->gc.clip.f.bot = c->bm.h << 16;
        break;
    }
    if (p->flags & 2)
        tnui_save_under(r);
    else
        r->save = NULL;
    return r;
}

void viewport_2E575C(TnRegion *r)
{
    TnEvent ev;

#ifndef TN_WATCOM_ABI
    /* G25: the base's scripts can close a box twice (two clicks on its buttons before the next
     * frame frees it; found by the automatic clicks, a saved game's description box): the
     * second close freed its picture again.  A box already closed is left alone */
    if (r->vflags & TNRF_DEAD)
        return;
#endif

    if (r->mask & TNE_DESTROY) {
        ev.type = TNE_DESTROY; ev.pad = 0; ev.pt.x = ev.pt.y = 0;
        SEND(r, &ev);
    }
    if (r->type == TNR_BITMAP)
        (*f_free)(r->cnv.bm.bits);
    r->vflags |= TNRF_HIDDEN | TNRF_DEAD;
    tnui_dead_count++;
    if (r == viewport_key_focus)
        viewport_key_focus = NULL;
    if (r == tnui_hover)
        tnui_hover = NULL;
    if (r->save) {
        tnui_restore_under(r);
        (*f_free)(r->save);
    } else if (r->type != TNR_SCREEN)
        tnui_add_dirty_rect(&r->rect);
}

void viewport_2E57F0(TnRegion *r)
{
    if (!(r->vflags & TNRF_HIDDEN))
        return;
    r->vflags &= ~TNRF_HIDDEN;
    switch (r->type) {
    case TNR_BACK:
        tnui_add_blit_rect(&r->rect);
        break;
    case TNR_BITMAP:
    case TNR_DRAWN:
        tnui_add_dirty_rect(&r->rect);
        break;
    }
}

void viewport_2E582C(TnRegion *r)
{
    if (r->vflags & TNRF_HIDDEN)
        return;
    r->vflags |= TNRF_HIDDEN;
    tnui_add_dirty_rect(&r->rect);
}

void viewport_2E5848(TnRegion *r, LGRect *rect)
{
    LGRect tmp;
    LGRect *p;

    switch (r->type) {
    case TNR_SCREEN:
        gr_set_canvas(&r->cnv);
        if (tnm_no_hide)
            WATCOM_CALL(r->draw, (long)r, (long)rect);
        else {
            MouseSaveHide();
            MouseHideRect(&r->rect);
            WATCOM_CALL(r->draw, (long)r, (long)rect);
            MouseRestoreHide();
        }
        break;
    case TNR_BACK:
    case TNR_BITMAP:
        gr_set_canvas(&r->cnv);
        WATCOM_CALL(r->draw, (long)r, (long)rect);
        if (r->vflags & TNRF_HIDDEN)
            break;
        p = rect ? rect : &r->rect;        /* not offset (quirk) */
        if (r->type == TNR_BACK)
            tnui_add_blit_rect(p);
        else
            tnui_add_dirty_rect(p);
        break;
    case TNR_DRAWN:
        if (r->vflags & TNRF_HIDDEN)
            break;
        if (!rect)
            p = &r->rect;
        else {
            offset_rect(&tmp, rect, r);
            p = &tmp;
        }
        tnui_add_dirty_rect(p);
        break;
    }
}

void viewport_2E5954(TnRegion *r, long dx, long dy, long dprio, long w, long h)
{
    TnEvent ev;
    LGRect old = r->rect;
    LGPoint d;
    uchar redraw = 0;

    if (dprio)
        redraw = LlistMoveQueue(&tnui_list, r, (short)(dprio + r->prio));
    if (dx || dy || (w && h)) {
        d.x = dx; d.y = dy;
        RectMove(&r->rect, d);
        if (w && h) {
            r->cnv.bm.w = w;
            r->rect.lr.x = r->rect.ul.x + w;
            r->cnv.bm.h = h;
            r->rect.lr.y = r->rect.ul.y + h;
        }
        RectUnion(&old, &r->rect, &old);
        if (r->type != TNR_BITMAP)
            r->cnv.bm.bits += (ulong)r->cnv.bm.row * dy + dx;
        if (r->mask & TNE_MOVE) {
            ev.type = TNE_MOVE; ev.pad = 0; ev.pt = d;
            SEND(r, &ev);
        }
        redraw = 1;
    }
    if (redraw)
        tnui_add_dirty_rect(&old);
}

static void send_pt(TnRegion *r, ulong type, LGPoint pt)
{
    TnEvent ev;

    ev.type = type; ev.pad = 0; ev.pt = pt;
    SEND(r, &ev);
}

TnRegion *tnui_region_at(LGPoint pt)
{
    TnRegion *n;

    if (viewport_key_focus)
        return viewport_key_focus;
    for (n = LAST; n != HEAD; n = n->prev) {
        if (n->vflags & TNRF_HIDDEN)
            continue;
        if (pt.y < n->rect.ul.y || pt.y >= n->rect.lr.y || pt.x < n->rect.ul.x || pt.x >= n->rect.lr.x)
            continue;
        if (n != tnui_hover) {
            if (tnui_hover && (tnui_hover->mask & TNE_LEAVE))
                send_pt(tnui_hover, TNE_LEAVE, pt);
            if (n->mask & TNE_ENTER)
                send_pt(n, TNE_ENTER, pt);
            tnui_hover = n;
        }
        return n;
    }
    if (tnui_hover && (tnui_hover->mask & TNE_LEAVE))
        send_pt(tnui_hover, TNE_LEAVE, pt);
    tnui_hover = NULL;
    return NULL;
}

bool tnui_key_handler(void *ev, void *node)
{
    TnEvent *e = ev;
    TnRegion *n;

    for (n = LAST; n != HEAD; n = n->prev)
        if (!(n->vflags & TNRF_DEAD) && (n->mask & e->type) && (uchar)SEND(n, e))
            return TRUE;
    return FALSE;
}

uchar tnui_region_event(TnRegion *r, TnEvent *e)
{
    if (r->mask & e->type)
        return SEND(r, e);
    return 0;
}

void viewport_2E5BC8(TnRegion *r, LGRect *rect)
{
    LGRect tmp;
    LGRect *p;

    if (r->vflags & TNRF_HIDDEN)
        return;
    if (r->type < TNR_BACK || r->type > TNR_DRAWN)
        return;
    if (!rect)
        p = &r->rect;
    else {
        offset_rect(&tmp, rect, r);
        p = &tmp;
    }
    if (r->type == TNR_BACK)
        tnui_add_blit_rect(p);
    else
        tnui_add_dirty_rect(p);
}

void viewport_2E5C94(TnRegion *r, LGRect *rect)
{
    if (r->vflags & TNRF_HIDDEN)
        return;
    if (!rect)
        rect = &r->rect;
    if (r->type == TNR_BACK)
        tnui_add_blit_rect(rect);
    else if (r->type == TNR_BITMAP || r->type == TNR_DRAWN)
        tnui_add_dirty_rect(rect);
}

/* Adds a rectangle (clipped to the screen) to a list unless one there contains it; drops the
 * ones it contains. */
static void add_rect(LGRect *list, short *count, LGRect *r)
{
    LGRect t;
    LGRect *p;
    int i, j;

    if (!RectSect(r, &tnui_full_rect, &t))
        return;
    for (i = 0, p = list; i < *count; ) {
        if (p->ul.y <= t.ul.y && t.lr.y <= p->lr.y && t.ul.x >= p->ul.x && t.lr.x <= p->lr.x)
            return;
        if (t.ul.y <= p->ul.y && t.lr.y >= p->lr.y && t.ul.x <= p->ul.x && t.lr.x >= p->lr.x) {
            for (j = i; j < *count - 1; j++)
                list[j] = list[j + 1];
            (*count)--;
        } else {
            p++;
            i++;
        }
    }
    list[*count] = t;
    (*count)++;
}

void tnui_add_dirty_rect(LGRect *r)
{
    add_rect(tnui_dirty_rects, &tnui_dirty_count, r);
}

void tnui_add_blit_rect(LGRect *r)
{
    add_rect(tnui_blit_rects, &tnui_blit_count, r);
}

static inline void redraw_part(TnRegion *n, LGRect *d)
{
    LGRect t;

    RectSect(&n->rect, d, &t);
    tnui_draw_region_rect(n, &t);
}

/* Redraws a dirty rectangle: only the topmost visible region over it when that one is opaque
 * and covers it, else every visible region of type 2/3 over it, bottom up. */
void tnui_redraw_rect(LGRect *d)
{
    TnRegion *n;

    for (n = LAST; n != HEAD; n = n->prev) {
        if ((n->vflags & TNRF_HIDDEN) || !visible_sect(n, d))
            continue;
        if (!(n->vflags & TNRF_TRANSP) && d->ul.y >= n->rect.ul.y && n->rect.lr.y >= d->lr.y
                && n->rect.ul.x <= d->ul.x && n->rect.lr.x >= d->lr.x) {
            redraw_part(n, d);
            return;
        }
        break;
    }
    tnui_redraw_rect_after(HEAD, d);
}

static inline int redraws(TnRegion *n, LGRect *d)
{
    return n->type != TNR_SCREEN && n->type != TNR_BACK && !(n->vflags & TNRF_HIDDEN)
        && visible_sect(n, d);
}

void tnui_redraw_rect_before(TnRegion *end, LGRect *d)
{
    TnRegion *n;

    for (n = FIRST; n != end; n = n->next)
        if (redraws(n, d))
            redraw_part(n, d);
}

void tnui_redraw_rect_after(TnRegion *start, LGRect *d)
{
    TnRegion *n;

    for (n = start->next; n != TAIL; n = n->next)
        if (redraws(n, d))
            redraw_part(n, d);
}

void tnui_clear_back(void)
{
    MouseHide();
    memset(tnui_back_canvas.bm.bits, 0, tnui_back_canvas.bm.h * (ulong)tnui_back_canvas.bm.row);
    GfxBlit(&tnui_full_rect);
    tnui_blit_count = 0;
    tnui_dirty_count = 0;
    MouseShow();
}

void tnui_blit_all(void)
{
    MouseHide();
    GfxBlit(&tnui_full_rect);
    MouseShow();
}

/* Frame task 18: frees destroyed regions, redraws the dirty rectangles. */
void tnui_task_60f4(void)
{
    TnRegion *n, *next;
    LGRect *p;
    int i;

    if (tnui_dead_count) {
        for (n = FIRST; n != TAIL; n = next) {
            next = n->next;
            if (n->vflags & TNRF_DEAD)
                LlistFree(&tnui_list, n);
        }
        tnui_dead_count = 0;
    }
    if (tnui_dirty_count) {
        for (i = 0, p = tnui_dirty_rects; i < tnui_dirty_count; i++, p++) {
            tnui_redraw_rect(p);
            tnui_add_blit_rect(p);
        }
        tnui_dirty_count = 0;
    }
}

/* Frame task 0: copies the blit rectangles to the screen. */
#ifndef TN_WATCOM_ABI
void port_ffbutton_refresh(void);       /* the main menu's Freefall button (src/game/shell/uiwidget.c) */
#endif

void tnui_task_6174(void)
{
    LGRect t;
    LGRect *p;

#ifndef TN_WATCOM_ABI
    port_ffbutton_refresh();
#endif
    if (!tnui_blit_count)
        return;
    tnm_lock++;
    if (!tnm_no_hide)
        MouseSaveHide();
    else if (tnm_cursor_drawn) {
        /* draw the cursor into the back buffer at its new place before blitting */
        MouseGetSaveRect(&t, &tnm_su2);
        tnui_add_blit_rect(&t);
        t.ul.x = tnm_draw_pos.x - tnm_cursor.hot.x;
        t.ul.y = tnm_draw_pos.y - tnm_cursor.hot.y;
        t.lr.x = t.ul.x + tnm_cursor.bm.w;
        t.lr.y = t.ul.y + tnm_cursor.bm.h;
        tnui_add_blit_rect(&t);
        MouseSaveUnderAt(tnm_draw_pos, &tnui_back_canvas, &tnm_su);
        cursor_save_under(&tnm_cursor, tnm_draw_pos, &tnui_back_canvas, &tnm_su2);
    }
    for (p = tnui_blit_rects; p < tnui_blit_rects + tnui_blit_count; p++) {
        if (!tnm_no_hide && tnm_cursor_drawn)
            MouseHideRect(p);
        GfxBlit(p);
    }
    if (!tnm_no_hide)
        MouseRestoreHide();
    else if (tnm_cursor_drawn)
        cursor_restore_under(&tnm_su2);
    tnui_blit_count = 0;
    tnm_lock--;
}

bool tnui_mouse_handler(void *ev, void *node)
{
    TnEvent *e = ev;
    TnRegion *r = tnui_region_at(e->pt);

    if (!r || !(e->type & r->mask))
        return 0;
    return SEND(r, e);
}

static inline void under_bm(grs_bitmap *bm, TnRegion *r, uchar *bits)
{
    bm->bits = bits;
    bm->type = BMT_FLAT8;
    bm->align = 0;
    bm->flags = 0;
    bm->w = r->cnv.bm.w;
    bm->row = bm->w;
    bm->h = r->cnv.bm.h;
    bm->wlog = bm->hlog = 0;            /* left unset by the original */
}

void tnui_save_under(TnRegion *r)
{
    grs_bitmap bm;

    under_bm(&bm, r, NULL);
    bm.bits = r->save = (*f_malloc)(bm.h * (ulong)bm.row);
    gr_set_canvas(grd_screen_canvas);
    MouseHide();
    WATCOM_CALL(grd_canvas_table[GET_DEVICE_UBITMAP + 1 + 2 * bm.type], (long)&bm, r->rect.ul.x, r->rect.ul.y);
    MouseShow();
}

void tnui_restore_under(TnRegion *r)
{
    grs_bitmap bm;

    under_bm(&bm, r, r->save);
    gr_set_canvas(grd_screen_canvas);
    MouseHide();
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm.type], (long)&bm, r->rect.ul.x, r->rect.ul.y);
    MouseShow();
}

/* Redraws part (screen rectangle) of a region into the back buffer. */
void tnui_draw_region_rect(TnRegion *r, LGRect *rect)
{
    grs_canvas *c;
    short l, t, rt, b;

    if (r->type != TNR_DRAWN) {
        tnui_copy_region_rect(r, rect);
        return;
    }
    gr_set_canvas(&r->cnv);
    c = grd_canvas;
    rt = c->gc.clip.i.right;
    b = c->gc.clip.i.bot;
    c->gc.clip.f.sten = NULL;
    l = c->gc.clip.i.left;
    c->gc.clip.i.left = rect->ul.x - r->rect.ul.x;
    t = c->gc.clip.i.top;
    c->gc.clip.i.top = rect->ul.y - r->rect.ul.y;
    c->gc.clip.i.right = rect->lr.x - r->rect.ul.x;
    c->gc.clip.i.bot = rect->lr.y - r->rect.ul.y;
    WATCOM_CALL(r->draw, (long)r, (long)rect);
    c = grd_canvas;
    c->gc.clip.f.sten = NULL;
    c->gc.clip.i.left = l;
    c->gc.clip.i.top = t;
    c->gc.clip.i.right = rt;
    c->gc.clip.i.bot = b;
}

/* Copies the part of a region's bitmap inside a screen rectangle into the back buffer. */
void tnui_copy_region_rect(TnRegion *r, LGRect *rect)
{
    grs_bitmap bm = r->cnv.bm;
    short x, y;
    long ft;

    y = rect->ul.y;
    if (r->rect.ul.y >= y)
        y = r->rect.ul.y;
    else {
        bm.bits += (y - r->rect.ul.y) * (ulong)bm.row;
        bm.h -= y - r->rect.ul.y;
    }
    if (r->rect.lr.y > rect->lr.y)
        bm.h -= r->rect.lr.y - rect->lr.y;
    x = rect->ul.x;
    if (r->rect.ul.x >= x)
        x = r->rect.ul.x;
    else {
        bm.bits += x - r->rect.ul.x;
        bm.w -= x - r->rect.ul.x;
    }
    if (r->rect.lr.x > rect->lr.x)
        bm.w -= r->rect.lr.x - rect->lr.x;
    if (bm.w <= 0 || bm.h <= 0)
        return;
    gr_set_canvas(&tnui_back_canvas);
    ft = r->fill_type;
    grd_canvas->gc.fill_type = ft;
    grd_function_table = grd_function_fill_table + ft * (GRD_FUNCS_TN * REAL_BMT_TYPES);
    grd_uline_fill_vector = grd_uline_fill_table + ft * 9;
    grd_canvas->gc.fill_parm = r->fill_parm;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm.type], (long)&bm, x, y);
    grd_canvas->gc.fill_type = 0;
    grd_function_table = grd_function_fill_table;
    grd_uline_fill_vector = grd_uline_fill_table;
}

void ViewportCheckPtr(TnRegion *r)
{
    TnRegion *n;

    for (n = FIRST; n != TAIL; n = n->next)
        if (r == n)
            return;
    DbgReportError(1, viewport_check_msg);      /* no argument for the %x (quirk) */
}
