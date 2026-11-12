/* tnmouse.c -- Terra Nova's mouse and cursor layer ("tnui"; TN replaces the LG UI library,
 * which is not linked; all names ours).  Decompiled from __FF.EXE.
 *
 *   0x2E4540 tnmouse_init          0x2E462C tnmouse_set_screensize 0x2E4744 tnmouse_shutdown
 *   0x2E475C MouseSetDrawFunc      0x2E47FC MouseSetCursorBitmap   0x2E4930 MouseSetCursor
 *   0x2E4A68 MouseMoveTo           0x2E4B1C MouseHide              0x2E4B78 MouseShow
 *   0x2E4BC4 MouseSaveHide         0x2E4BD4 MouseHideRect          0x2E4CA8 MouseRestoreHide
 *   0x2E4D58 MouseSaveUnderAt      0x2E4DD4 MouseGetSaveRect       0x2E4E10 MouseFreeze
 *   0x2E4E18 MouseUnfreeze         0x2E4E24 MouseConstrain         0x2E4EA8 MouseSetStencil
 *   0x2E4F8C MouseClearStencil     0x2E5004 MouseIntDrawOn         0x2E502C MouseIntDrawOff
 *   0x2E504C tnmouse_poll          0x2E5388 MouseAddHandler        0x2E53A4 MouseRemoveHandler
 *   0x2E5408 MouseRemoveAllHandlers 0x2E5414 tnmouse_dispatch      0x2E546C tnmouse_null_handler
 *   0x2E5470 tnmouse_draw          0x2E54BC tnmouse_undraw         0x2E54E0 tnmouse_draw_cb
 *   0x2E5508 MouseSetLefty         0x2E5510 tnui_init              0x2E5584 tnui_shutdown
 * (The jump table at 0x2E55C8 belongs to the next function, 0x2E55D8, not to this file.)
 *
 * The cursor is drawn by tnm_draw_func (default tnmouse_draw_cb -> cursor_save_under /
 * cursor_restore_under in tncursor.c).  tnmouse_hide_count is 1 when the cursor is visible
 * (LG style: MouseHide decrements, MouseShow increments up to 1); tnm_lock is the LG
 * "MouseLock" that keeps the interrupt-time cursor code (cursor_interrupt_draw) off the
 * screen while the cursor is changed.  Most functions inline the hide/show pair (macros
 * below; the lock nesting is reproduced exactly).
 *
 * tnmouse_poll is the game's per-frame task 7: it reads the INPUT mouse queue, splits each
 * event into one event per button bit, adds a motion (0x40) or idle (0x80) event, and hands
 * them to the handlers whose rectangle contains the point (mask 0x400 = anywhere).
 *
 * Quirks kept:
 *  - MouseHideRect leaves tnm_lock one higher on every call and lowers tnmouse_hide_count even
 *    when the cursor is not in the rectangle; MouseRestoreHide undoes both by the difference
 *    between the saved and the current hide count (byte arithmetic).
 *  - tnmouse_shutdown calls cursor_lock_all again instead of unlocking.
 *  - In replay mode tnmouse_poll dispatches only the game's events (the queue is drained).
 *  - Mouse events have 4 unused bytes (+4) left uninitialised (stack garbage in the original).
 *  - A full event buffer (36) plus a motion event writes a 37th event over the mouse_event
 *    work area, as in the original stack frame.
 */
#include "sup_c.h"
#include "res.h"
#ifndef TN_WATCOM_ABI
void port_keep_cursor(long ref, const void *c);    /* port/ui/options.c: the options screen's pointer */
#endif

#ifndef TN_LINK_ORIGINAL_DATA
LlistHead tnmouse_handlers;
short tnmouse_hide_count;
#endif

#define NULL_HANDLER WATCOM_FN(tnmouse_null_handler)
#define GET_BITMAP_FN(type) grd_canvas_table[GET_DEVICE_UBITMAP + 1 + 2 * (type)]

/* Inlined hide / show pair (the lock nesting matters: the cursor code reads tnm_lock). */
#define HIDE_INLINE() do { tnm_lock++; tnmouse_hide_count--; \
        if (tnm_cursor_drawn) { tnmouse_undraw(); tnm_cursor_drawn = 0; } tnm_lock--; } while (0)
#define SHOW_INLINE() do { tnm_lock++; if (tnmouse_hide_count < 1) { tnmouse_hide_count++; \
        if (tnmouse_hide_count == 1 && !tnm_cursor_drawn) { tnm_cursor_drawn = 1; tnmouse_draw(); } } \
        tnm_lock--; } while (0)

#define PT(p) (*(ulong *)&(p))

bool tnmouse_init(LGPoint size, LGPoint cursor_max)
{
    if ((short)mouse_init(size.x, size.y))
        return FALSE;
    cursor_lock_all();
    tnm_state_ptr = tnm_state_copy;
    mouse_set_timestamp_register(&tn_ticks);
    mouseMask &= ~1;                                    /* no motion events in the queue */
    tnm_4470d0 = 0;
    tnm_poll_time = tn_ticks;
    tnm_click_time[1] = tnm_click_time[0] = tn_ticks - 30000;
    MouseHide();
    tnm_draw_func = WATCOM_FN(tnmouse_draw_cb);
    MouseShow();
    PT(tnm_cursor_max) = 0;
    tnmouse_set_screensize(size, cursor_max);
    LlistInit(&tnmouse_handlers, sizeof(tnmouse_node), 4);
    tn_task_func[7] = WATCOM_FN(tnmouse_poll);
    *(uchar *)&tn_task_mask |= TN_TASK_MOUSE;
    AtExit(WATCOM_FN(tnmouse_shutdown));
    tnm_lefty = 0;
    return TRUE;
}

/* Also sizes the two save-under bitmaps for cursors up to cursor_max. */
void tnmouse_set_screensize(LGPoint size, LGPoint cursor_max)
{
    long oldsize, newsize;
    mouse_set_screensize(size.x, size.y);
    tnm_screen_size = size;
    oldsize = tnm_cursor_max.y * tnm_cursor_max.x;
    tnm_cursor_max = cursor_max;
    newsize = tnm_cursor_max.y * tnm_cursor_max.x;
    tnm_su.cur.bm.type = BMT_FLAT8;
    tnm_su.cur.bm.flags = 0;
    tnm_su.cur.bm.align = 0;
    tnm_su.cur.bm.w = tnm_cursor_max.x;
    tnm_su.cur.bm.h = tnm_cursor_max.y;
    tnm_su.cur.bm.row = tnm_su.cur.bm.w;
    tnm_su2.cur.bm.type = BMT_FLAT8;
    tnm_su2.cur.bm.flags = 0;
    tnm_su2.cur.bm.w = tnm_cursor_max.x;
    tnm_su2.cur.bm.h = tnm_cursor_max.y;
    tnm_su2.cur.bm.align = 0;
    tnm_su2.cur.bm.row = tnm_su2.cur.bm.w;
    if (newsize > oldsize) {
        if (oldsize > 0) {
            Free(tnm_su.cur.bm.bits);
            Free(tnm_su2.cur.bm.bits);
        }
        tnm_su.cur.bm.bits = Malloc(newsize);
        tnm_su2.cur.bm.bits = Malloc(newsize);
    }
    MouseClearStencil();
}

void tnmouse_shutdown(void)
{
    *(uchar *)&tn_task_mask &= ~TN_TASK_MOUSE;
    LlistDestroy(&tnmouse_handlers);
    cursor_lock_all();                                  /* sic */
}

/* NULL = default (tnmouse_draw_cb).  Unreferenced. */
void MouseSetDrawFunc(TnCursorDrawFunc f)
{
    HIDE_INLINE();
    tnm_draw_func = f ? f : WATCOM_FN(tnmouse_draw_cb);
    SHOW_INLINE();
}

/* Sets the cursor from a bitmap + hotspot; ignored when larger than the save-under.
 * Unreferenced. */
void MouseSetCursorBitmap(TnCursor *c)
{
    if (tnm_cursor_max.x < c->bm.w || tnm_cursor_max.y < c->bm.h)
        return;
    tnm_lock++;
    HIDE_INLINE();
    if (tnm_cursor_ref) {
        ResUnlock((Id)(tnm_cursor_ref >> 16));
        tnm_cursor_ref = 0;
    }
    tnm_cursor = *c;
    tnm_su.cur.bm.w = tnm_cursor.bm.w;
    tnm_su.cur.bm.h = tnm_cursor.bm.h;
    tnm_su2.cur.bm.w = tnm_cursor.bm.w;
    tnm_su2.cur.bm.h = tnm_cursor.bm.h;
    SHOW_INLINE();
    tnm_lock--;
}

/* Sets the cursor from a FrameDesc resource (Ref); keeps it locked while in use. */
void MouseSetCursor(long ref)
{
    uchar *p;
    if (ref == tnm_cursor_ref)
        return;
    tnm_lock++;
    HIDE_INLINE();
    if (tnm_cursor_ref)
        ResUnlock((Id)(tnm_cursor_ref >> 16));
    tnm_cursor_ref = ref;
    p = RefLock(ref);
    if (tnm_cursor_max.x >= ((grs_bitmap *)p)->w && tnm_cursor_max.y >= ((grs_bitmap *)p)->h) {
        tnm_cursor.bm = *(grs_bitmap *)p;
        tnm_cursor.hot = *(LGPoint *)(p + 0x10);        /* FrameDesc anchor */
        tnm_su.cur.bm.w = tnm_cursor.bm.w;
        tnm_su.cur.bm.h = tnm_cursor.bm.h;
        tnm_su2.cur.bm.w = tnm_cursor.bm.w;
        tnm_su2.cur.bm.h = tnm_cursor.bm.h;
        tnm_cursor.bm.bits = p + 0x1C;                  /* pixels follow the FrameDesc */
#ifndef TN_WATCOM_ABI
        port_keep_cursor(ref, &tnm_cursor);             /* the options screen's (port/ui/options.c) */
#endif
    }
    SHOW_INLINE();
    tnm_lock--;
}

void MouseMoveTo(LGPoint p)
{
    mouse_put_xy(p.x, p.y);
    tnm_last_pos = tnm_pos = p;
    HIDE_INLINE();
    SHOW_INLINE();
}

void MouseHide(void)
{
    tnmouse_hide_count--;
    tnm_lock++;
    if (tnm_cursor_drawn) {
        tnmouse_undraw();
        tnm_cursor_drawn = 0;
    }
    tnm_lock--;
}

void MouseShow(void)
{
    SHOW_INLINE();
}

/* MouseSaveHide; MouseHideRect(r)...; MouseRestoreHide: hide the cursor while drawing into
 * rectangles (it is undrawn only when it overlaps one). */
void MouseSaveHide(void)
{
    tnm_saved_hide = tnmouse_hide_count;
}

void MouseHideRect(LGRect *r)
{
    short h = tnmouse_hide_count - 1;
    short x0, y0, x1, y1;
    if (tnm_no_hide) {
        tnmouse_hide_count = h;
        return;
    }
    tnm_lock++;                                         /* undone by MouseRestoreHide */
    x0 = tnm_draw_pos.x - tnm_cursor.hot.x;
    y0 = tnm_draw_pos.y - tnm_cursor.hot.y;
    y1 = tnm_cursor.bm.h + y0;
    x1 = tnm_cursor.bm.w + x0;
    if (y0 < r->lr.y && y1 > r->ul.y && x0 < r->lr.x && x1 > r->ul.x) {
        tnm_lock++;
        tnmouse_hide_count = h;
        if (tnm_cursor_drawn) {
            tnmouse_undraw();
            tnm_cursor_drawn = 0;
        }
        tnm_lock--;
        return;
    }
    tnmouse_hide_count--;
}

void MouseRestoreHide(void)
{
    short h;
    if (tnm_saved_hide == 999)
        return;
    tnm_lock -= (uchar)((uchar)tnm_saved_hide - (uchar)tnmouse_hide_count);
    h = tnm_saved_hide;
    tnm_saved_hide = 999;
    tnmouse_hide_count = h;
    tnm_lock++;
    if (h == 1) {
        if (!tnm_cursor_drawn) {
            tnm_cursor_drawn = 1;
            tnmouse_draw();
        }
    } else if (tnm_cursor_drawn) {
        tnmouse_undraw();
        tnm_cursor_drawn = 0;
    }
    tnm_lock--;
}

/* Grabs the pixels under the cursor at pos from canvas c into su (default tnm_su);
 * su->canvas is not changed. */
void MouseSaveUnderAt(LGPoint pos, grs_canvas *c, TnSaveUnder *su)
{
    grs_canvas *old;
    if (!su)
        su = &tnm_su;
    su->pos.x = pos.x - tnm_cursor.hot.x;
    su->drawn = 1;
    su->pos.y = pos.y - tnm_cursor.hot.y;
    old = grd_canvas;
    gr_set_canvas(c);
    WATCOM_CALL(GET_BITMAP_FN(su->cur.bm.type), (long)&su->cur.bm, su->pos.x, su->pos.y);
    gr_set_canvas(old);
}

void MouseGetSaveRect(LGRect *r, TnSaveUnder *su)
{
    if (!su)
        su = &tnm_su;
    r->ul.x = su->pos.x;
    r->ul.y = su->pos.y;
    r->lr.x = r->ul.x + su->cur.bm.w;
    r->lr.y = r->ul.y + su->cur.bm.h;
}

void MouseFreeze(void)
{
    tnm_frozen = 1;
}

void MouseUnfreeze(void)
{
    tnm_frozen = 0;
}

/* Confines the mouse to r (lr exclusive) and clips the cursor canvases to it. */
void MouseConstrain(LGRect *r)
{
    mouse_constrain_xy(r->ul.x, r->ul.y, (short)(r->lr.x - 1), (short)(r->lr.y - 1));
    tnm_screen_canvas.gc.clip.i.left = r->ul.x;
    tnm_screen_canvas.gc.clip.i.top = r->ul.y;
    tnm_screen_canvas.gc.clip.i.right = r->lr.x;
    tnm_screen_canvas.gc.clip.i.bot = r->lr.y;
    tnm_back_canvas.gc.clip.i.left = r->ul.x;
    tnm_back_canvas.gc.clip.i.top = r->ul.y;
    tnm_screen_canvas.gc.clip.f.sten = NULL;
    tnm_back_canvas.gc.clip.i.right = r->lr.x;
    tnm_back_canvas.gc.clip.f.sten = NULL;
    tnm_back_canvas.gc.clip.i.bot = r->lr.y;
}

/* Confines the mouse to a stencil (rows of spans, h rows) placed at origin. */
void MouseSetStencil(grs_stencil *s, short h, LGPoint origin)
{
    tnm_stencil = s;
    tnm_sten_h = h;
    tnm_sten_origin = origin;
    tnm_lock++;
    gr_init_sub_canvas(grd_screen_canvas, &tnm_screen_canvas, tnm_sten_origin.x, tnm_sten_origin.y,
                       (short)(grd_screen_canvas->bm.w - tnm_sten_origin.x), h);
    tnm_screen_canvas.gc.clip.i.bot = tnm_sten_h;
    tnm_screen_canvas.gc.clip.f.sten = tnm_stencil;
    tnm_screen_canvas.gc.clip.i.top = 0;
    gr_init_sub_canvas(&tnui_back_canvas, &tnm_back_canvas, tnm_sten_origin.x, tnm_sten_origin.y,
                       (short)(tnui_back_canvas.bm.w - tnm_sten_origin.x), tnm_sten_h);
    tnm_back_canvas.gc.clip.i.top = 0;
    tnm_back_canvas.gc.clip.i.bot = tnm_sten_h;
    tnm_lock--;
    tnm_back_canvas.gc.clip.f.sten = tnm_stencil;
}

void MouseClearStencil(void)
{
    mouse_constrain_xy(0, 0, (short)(tnm_screen_size.x - 1), (short)(tnm_screen_size.y - 1));
    gr_init_sub_canvas(grd_screen_canvas, &tnm_screen_canvas, 0, 0, tnm_screen_size.x, tnm_screen_size.y);
    gr_init_sub_canvas(&tnui_back_canvas, &tnm_back_canvas, 0, 0, tnm_screen_size.x, tnm_screen_size.y);
    tnm_stencil = NULL;
}

/* Draw the cursor from the mouse interrupt (cursor_interrupt_draw) instead of in the poll. */
void MouseIntDrawOn(void)
{
    if (!tnm_int_on) {
        mouse_set_callback(WATCOM_FN(cursor_interrupt_draw), NULL, &tnm_int_cb_id);
        tnm_int_on = 1;
    }
}

void MouseIntDrawOff(void)
{
    if (tnm_int_on) {
        mouse_unset_callback(tnm_int_cb_id);
        tnm_int_on = 0;
    }
}

#define EVBUF 36
void tnmouse_poll(void)
{
    /* the original's frame: 36 events at +0, mouse_event at +0x1B0 (overlaid by a 37th event) */
    uchar frame[EVBUF * sizeof(tnmouse_event) + sizeof(mouse_event)];
    tnmouse_event *buf = (tnmouse_event *)frame, *ev = buf, *end;
    mouse_event *me = (mouse_event *)(frame + EVBUF * sizeof(tnmouse_event));
    tnmouse_event *p;
    LGPoint m;
    int n;

    tnm_lock++;
    if (!tnm_no_events) {
        if (tnm_moved_at_int) {
            mouse_put_xy(tnm_pos.x, tnm_pos.y);
            tnm_moved_at_int = 0;
        } else if (!tnm_int_on) {
            mouse_get_xy(&m.x, &m.y);
            if (tnm_stencil && !cursor_in_stencil(m.x, m.y)) {
                PT(tnm_pos) = cursor_constrain(m.x, m.y);
                mouse_put_xy(tnm_pos.x, tnm_pos.y);
            } else
                tnm_pos = m;
        }
        while ((short)mouse_next(me) != 2) {
            uint bits = me->type >> 1, bit;
            for (bit = 1; bits; bit <<= 1) {
                if (bits & bit) {
                    ev->type = bit;
                    ev->x = me->x;
                    ev->y = me->y;
                    ev++;
                    if (ev == buf + EVBUF)
                        goto full;
                    bits &= ~bit;
                }
            }
        }
    }
full:
    if (tnm_pos.x != tnm_last_pos.x || tnm_pos.y != tnm_last_pos.y) {
        ev->type = TNM_MOTION;
        PT(ev->x) = PT(tnm_pos);
        ev++;
        tnm_last_pos = tnm_pos;
        if (tnmouse_hide_count == 1 && !tnm_frozen) {
            HIDE_INLINE();
            SHOW_INLINE();
        }
    } else if (ev == buf) {
        ev->type = TNM_IDLE;
        PT(ev->x) = PT(tnm_pos);
        ev++;
    }
    tnm_lock--;
    if (tn_replay_playing) {
        game_replay_get_mouse((void **)&p, &n);
        end = p + n;
    } else {
        p = buf;
        end = ev;
    }
    for (; p < end; p++) {
        if (tn_replay_recording)
            game_replay_record_mouse(p);
        if (tnm_lefty == 1) {                           /* swap left and right buttons */
            if (p->type == 1) p->type = 4;
            else if (p->type == 4) p->type = 1;
            else if (p->type == 2) p->type = 8;
            else if (p->type == 8) p->type = 2;
        }
        tnmouse_dispatch(p);
    }
    if (tnm_removed) {
        llist *q, *next;
        for (q = tnmouse_handlers.head.pnext; q != &tnmouse_handlers.tail; q = next) {
            next = q->pnext;
            if (((tnmouse_node *)q)->handler == NULL_HANDLER)
                LlistFree(&tnmouse_handlers, q);
        }
        tnm_removed = 0;
    }
    tnm_poll_time = tn_ticks;
}

void MouseAddHandler(LGRect *r, ulong mask, void *handler)
{
    tnmouse_node *n = LlistAddHead(&tnmouse_handlers);
    n->r = *r;
    n->mask = mask;
    n->handler = handler;
}

/* Marks the matching handler dead (freed by tnmouse_poll); silent when not found. */
void MouseRemoveHandler(LGRect *r, ulong mask, void *handler)
{
    llist *q;
    for (q = tnmouse_handlers.head.pnext; q != &tnmouse_handlers.tail; q = q->pnext) {
        tnmouse_node *n = (tnmouse_node *)q;
        if (handler == n->handler && mask == n->mask) {
            uchar *a = (uchar *)r, *b = (uchar *)&n->r;
            int i;
            for (i = 0; i < 8 && a[i] == b[i]; i++)
                ;
            if (i == 8) {
                n->handler = NULL_HANDLER;
                tnm_removed++;
                return;
            }
        }
    }
}

void MouseRemoveAllHandlers(void)
{
    LlistFreeAll(&tnmouse_handlers);
}

void tnmouse_dispatch(tnmouse_event *e)
{
    llist *q;
    for (q = tnmouse_handlers.head.pnext; q != &tnmouse_handlers.tail; q = q->pnext) {
        tnmouse_node *n = (tnmouse_node *)q;
        if (!(n->mask & e->type))
            continue;
        if (e->type == TNM_ANYWHERE ||
            (e->y >= n->r.ul.y && e->y < n->r.lr.y && e->x >= n->r.ul.x && e->x < n->r.lr.x))
            if (n->handler(e, n))
                break;
    }
}

bool tnmouse_null_handler(void)
{
    return FALSE;
}

void tnmouse_draw(void)
{
    if (tnm_draw_func) {
        if (!tnm_frozen)
            tnm_draw_pos = tnm_pos;
        WATCOM_CALL(tnm_draw_func, (long)&tnm_su.cur, PT(tnm_draw_pos), 0);
        WATCOM_CALL(tnm_draw_func, (long)&tnm_cursor, PT(tnm_draw_pos), 1);
    }
}

void tnmouse_undraw(void)
{
    if (tnm_draw_func)
        WATCOM_CALL(tnm_draw_func, (long)&tnm_su.cur, PT(tnm_draw_pos), 2);
}

/* Default draw function: 0/1 save the pixels under c and draw it, 2 restore them. */
void tnmouse_draw_cb(TnCursor *c, LGPoint pos, uchar cmd)
{
    if (cmd <= 1)
        cursor_save_under(c, pos, grd_screen_canvas, NULL);
    else if (cmd == 2)
        cursor_restore_under(NULL);
}

void MouseSetLefty(bool lefty)
{
    tnm_lefty = lefty;
}

/* Init of the gfx/window layer that follows (0x2E55D8-): window list, two tasks, a mouse
 * handler for the whole screen and a key handler. */
void tnui_init(void)
{
    LlistInit(&tnui_list, 0xBC, 0x10);
    tn_task_func[18] = WATCOM_FN(tnui_task_60f4);
    tn_task_func[0] = WATCOM_FN(tnui_task_6174);
    tn_task_mask |= TN_TASK_UI | TN_TASK_UI2;
    MouseAddHandler(&tnui_full_rect, 0xFF, WATCOM_FN(tnui_mouse_handler));
    KeyAddHandler(WATCOM_FN(tnui_key_handler), NULL, TRUE, FALSE);
    AtExit(WATCOM_FN(tnui_shutdown));
}

void tnui_shutdown(void)
{
    tn_task_mask &= ~(TN_TASK_UI | TN_TASK_UI2);
    KeyRemoveHandler(WATCOM_FN(tnui_key_handler), NULL);
    MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(tnui_mouse_handler));
    LlistDestroy(&tnui_list);
}
