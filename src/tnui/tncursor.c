/* tncursor.c -- Terra Nova's cursor drawing ("tnui", names ours), the part that also runs at
 * mouse-interrupt time and is therefore DPMI-locked.  Decompiled from __FF.EXE.
 *
 *   0x2F2480 cursor_lock_a         0x2F249C cursor_sten_test      0x2F24E4 cursor_unlock_a
 *   0x2F2500 cursor_lock_all       0x2F2528 cursor_interrupt_draw 0x2F25E0 cursor_save_under
 *   0x2F26E8 cursor_restore_under  0x2F2748 cursor_in_stencil     0x2F2788 cursor_constrain
 * (0x2F2850, listed with this range in the support map, is DSTRUCT RectTestSect: rect.c.)
 *
 * The cursor is drawn on tnm_screen_canvas / tnm_back_canvas, sub canvases of the screen and
 * of the game's back buffer canvas that MouseSetStencil / MouseConstrain clip.  The stencil
 * (rows of spans) also restricts where the mouse may go: cursor_constrain keeps the old x or y
 * when the new point is outside.
 *
 * Quirks kept:
 *  - cursor_lock_all locks the code range [cursor_lock_all, cursor_lock_all] (empty) plus
 *    grd_interrupt; cursor_lock_a / cursor_unlock_a (lock [cursor_lock_a, cursor_unlock_a])
 *    are referenced but never called.
 *  - cursor_sten_test tests the row pointer (elem + y) for NULL, not the stencil.
 */
#include "sup_c.h"
#ifndef TN_WATCOM_ABI
#include "sharp3d.h"                    /* widescreen: the display draws the pointer beside the screen */
#endif

#define GET_BITMAP_FN(type) grd_canvas_table[GET_DEVICE_UBITMAP + 1 + 2 * (type)]
#define DRAW_BITMAP_FN(type) grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * (type)]

void cursor_lock_a(void)
{
    dpmi_lock_region(cursor_lock_a, cursor_unlock_a);
}

/* Is pt inside the stencil (h rows of span lists, row y at elem[y])? */
bool cursor_sten_test(grs_stencil *s, short h, LGPoint pt)
{
    grs_sten_elem *e;
    if (pt.y < 0 || h <= pt.y)
        return FALSE;
    e = (grs_sten_elem *)((ulong)s->elem + pt.y * 8);
    if (e == NULL)
        return FALSE;
    do {
        if (pt.x >= e->l && pt.x <= e->r)
            return TRUE;
        e = e->n;
    } while (e);
    return FALSE;
}

void cursor_unlock_a(void)
{
    dpmi_unlock_region(cursor_lock_a, cursor_unlock_a);
}

void cursor_lock_all(void)
{
    dpmi_lock_region(cursor_lock_all, cursor_lock_all);
    dpmi_lock_data(&grd_interrupt, 1);
}

/* INPUT mouse callback (interrupt time) installed by MouseIntDrawOn. */
void cursor_interrupt_draw(mouse_event *e, void *data)
{
    if (!(e->type & 1) || tnm_frozen)                   /* motion only */
        return;
    grd_interrupt = 1;
    if (tnm_stencil && !cursor_in_stencil(e->x, e->y)) {
        tnm_moved_at_int = 1;
        *(ulong *)&tnm_pos = cursor_constrain(e->x, e->y);
    } else {
        tnm_pos.x = e->x;
        tnm_pos.y = e->y;
    }
    if (tnm_cursor_drawn && tnm_int_on && !tnm_lock) {
        cursor_restore_under(NULL);
        tnm_draw_pos = tnm_pos;
        cursor_save_under(&tnm_cursor, tnm_draw_pos, grd_screen_canvas, NULL);
    }
    grd_interrupt = 0;
}

/* Saves the pixels of canvas under cursor c at pos (hotspot applied) into su (default
 * tnm_su), then draws c in the cursor canvas matching canvas. */
void cursor_save_under(TnCursor *c, LGPoint pos, grs_canvas *canvas, TnSaveUnder *su)
{
    grs_canvas *old;
    if (!su)
        su = &tnm_su;
#ifndef TN_WATCOM_ABI
    if (port_wide_ptr || port_wide_cursor) {    /* the display draws it (platform.c) */
        su->drawn = 0;
        return;
    }
#endif
    su->drawn = 1;
    su->canvas = canvas;
    su->pos.x = pos.x - c->hot.x;
    su->pos.y = pos.y - c->hot.y;
    old = grd_canvas;
    gr_set_canvas(su->canvas);
    WATCOM_CALL(GET_BITMAP_FN(su->cur.bm.type), (long)&su->cur.bm, su->pos.x, su->pos.y);
    gr_set_canvas(canvas == grd_screen_canvas ? &tnm_screen_canvas : &tnm_back_canvas);
    grd_canvas->gc.fcolor = tnm_cursor_color;
    if (tnm_stencil)
        WATCOM_CALL(DRAW_BITMAP_FN(c->bm.type), (long)c, (short)(su->pos.x - tnm_sten_origin.x),
                    (short)(su->pos.y - tnm_sten_origin.y));
    else
        WATCOM_CALL(DRAW_BITMAP_FN(c->bm.type), (long)c, su->pos.x, su->pos.y);
    gr_set_canvas(old);
}

void cursor_restore_under(TnSaveUnder *su)
{
    grs_canvas *old;
    if (!su)
        su = &tnm_su;
    if (!su->drawn)
        return;
    old = grd_canvas;
    gr_set_canvas(su->canvas);
    WATCOM_CALL(DRAW_BITMAP_FN(su->cur.bm.type), (long)&su->cur.bm, su->pos.x, su->pos.y);
    gr_set_canvas(old);
    su->drawn = 0;
}

bool cursor_in_stencil(short x, short y)
{
    LGPoint pt;
    pt.x = x - tnm_sten_origin.x;
    pt.y = y - tnm_sten_origin.y;
    return cursor_sten_test(tnm_stencil, tnm_sten_h, pt);
}

/* New position for a move to (x, y) outside the stencil: slide along x or y, else stay. */
ulong cursor_constrain(short x, short y)
{
    LGPoint pt, r;
    pt.x = x - tnm_sten_origin.x;
    pt.y = tnm_pos.y - tnm_sten_origin.y;
    if (cursor_sten_test(tnm_stencil, tnm_sten_h, pt)) {
        r.x = x;
        r.y = tnm_pos.y;
        return *(ulong *)&r;
    }
    pt.x = tnm_pos.x - tnm_sten_origin.x;
    pt.y = y - tnm_sten_origin.y;
    if (cursor_sten_test(tnm_stencil, tnm_sten_h, pt)) {
        r.x = tnm_pos.x;
        r.y = y;
        return *(ulong *)&r;
    }
    return *(ulong *)&tnm_pos;
}
