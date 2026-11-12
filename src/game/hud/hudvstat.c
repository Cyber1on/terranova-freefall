/* hudvstat.c -- Terra Nova module M021 (0x26BBA0-0x26CB44): state of the three HUD views (which
 * mode each view shows, the modes it may cycle through, mode switching, the view widgets'
 * event / update / draw callbacks, the static shown while a view is damaged or switching, the
 * view-button icons).  Decompiled from __FF.EXE v1.09.
 *
 *   0x26BBA0 hudview_26BBA0 is it this view's turn (shared modes)
 *   0x26BBF4 hudview_26BBF4 reset the views         0x26BC74 hudview_26BC74 switch all views off
 *   0x26BCA0 hudview_26BCA0 view widget event        0x26BD00 hudview_26BD00 view 2 event (no widget)
 *   0x26BD44 hudview_26BD44 view widget update       0x26BE64 hudview_26BE64 view widget draw
 *   0x26BF3C hudview_26BF3C default modes            0x26BFC4 hudview_26BFC4 damage mode on / off
 *   0x26C224 hudview_26C224 set a view's mode        0x26C2C8 hudview_26C2C8 set or just allow a mode
 *   0x26C2F0 hudview_26C2F0 switch later             0x26C318 hudview_26C318 remove a mode
 *   0x26C360 hudview_26C360 next / previous mode     0x26C410 hudview_26C410 view allowing a mode
 *   0x26C44C hudview_26C44C camera mode (8) on / off 0x26C4A4 hudview_26C4A4 (empty)
 *   0x26C4A8 hudview_26C4A8 status text              0x26C50C hudview_26C50C draw static
 *   0x26C598 / 0x26C5B4 allow / forbid a mode        0x26C5F4 / 0x26C600 redraw a view
 *   0x26C60C hudview_26C60C view button click        0x26C72C hudview_26C72C view button icon
 *   0x26C820 hudview_26C820 toggle damage display    0x26C848 hudview_26C848 damage display on / off
 *   0x26C898 hudview_26C898 view 1 shows the map     0x26C8B4 hudview_26C8B4 aircraft view check
 *   0x26C980 hudview_26C980 remove a mode, fix cam   0x26CAAC / 0x26CAC8 / 0x26CAD8 mode tests
 *   0x26CAE8 hudview_26CAE8 view 1 data              0x26CAF0 hudview_26CAF0 set camera flag
 *   0x26CB14 hudview_26CB14 view 1 default mode
 *
 * Watcom merged code: 0x26C5F4 falls into 0x26C600 (mark a view widget dirty, 12 bytes);
 * 0x26C60C is its own function (reached by a jump from hudcb_264498); 0x26C4A4 is a bare `ret`
 * sharing 0x26C44C's; 0x26C128 / 0x26C192 are tails of 0x26C848 placed inside 0x26BFC4's range;
 * 0x26C820 falls into 0x26C848.  0x26C318 / 0x26C410 are inlined in a few callers.
 *
 * Quirks kept:
 *  - hudview_26BD44 tests hud_views[hud_views[0].mode].flags (indexes the views by a mode).
 *  - hudview_26C848(0): the search for a view whose saved mode is 8 can never succeed, so view 1
 *    always switches to mode 8 (when game_camera_38FB20 is set).
 *  - hudview_26C360 loops forever if the mask has no bit the walk can reach (never: mask != 0).
 */
#include "g7_c.h"
#include "g5_b.h"     /* ffpipe_38FFF0 */
#include "g6_a.h"     /* ffpipe_38FFF4 */
#include "g7_a.h"     /* added at the G5-G8 merge */
#include "g8_b.h"     /* added at the G5-G8 merge */
#include "g8_c.h"     /* added at the G5-G8 merge */

#define DRAW_BOX 18

#define PBA_SLOT(off) ((uchar *)&game_pba[game_player_slot] + (off))
#define F9C (*(uchar **)PBA_SLOT(0x9C))
#define DIRTY(w) ((w)->flags |= 0x20)
#define CAM_DA (game_camera[GAME_CAM_DA])

uchar hudview_26BBA0(long view)
{
    ulong cnt = 0, idx = 0;
    long i;

    for (i = 0; i < 3; i++)
        if (hud_mode_info[hud_views[i].mode][0] & 8) {
            cnt++;
            if (i < view)
                idx++;
        }
    if ((long)cnt > 1)
        return game_frame % cnt == idx;
    return 1;
}

void hudview_26BBF4(void)
{
    long i;

    for (i = 0; i < 3; i++) {
        memset(&hud_views[i], 0, 0x20);
        hud_views[i].index = (uchar)i;
        hud_view_38FC70[i] = 0;
    }
    hview3d_274410();
    mfdpage_271D60();
    hud_38FFFC = game_player_entity;
    ffpipe_38FFF4 = 0;
    ffpipe_38FFF0 = 0;
    F9C[0x1E] = 0;
}

void hudview_26BC74(void)
{
    hudview_26C224(0, 0);
    hudview_26C224(1, 0);
    hudview_26C224(2, 0);
    hview3d_2744D0();
}

void hudview_26BCA0(HudWidget *w, HudEvent *ev)
{
    if (CAM_DA && (hud_35AEF0 & 1))
        return;
    WATCOM_CALL(hud_mode_fns[hud_views[w->id].mode].event, (long)w, (long)ev);
    if (!hud_drag_widget && !(hud_views[w->id].flags & 0x10))
        MouseSetCursor(0x1F90016);
}

void hudview_26BD00(HudWidget *w, HudEvent *ev)
{
    if (CAM_DA && (hud_35AEF0 & 1))
        return;
    WATCOM_CALL(hud_mode_fns[hud_views[2].mode].event, (long)w, (long)ev);
    if (!hud_drag_widget && !(hud_views[2].flags & 0x10))
        MouseSetCursor(0x1F90016);
}

void hudview_26BD44(HudWidget *w)
{
    uchar d = damage_27DF14(game_player_entity, 6);
    long v;
    HudView *r;

    if (CAM_DA && !(hud_35AEF0 & 1))
        DIRTY(w);
    else if (CAM_DA)
        return;
    v = w->id;
    if (v == 0 && (hud_views[hud_views[0].mode].flags & 0x40) && !(*PBA_SLOT(0xCF) & 1))
        cmd_25F044();
    r = &hud_views[v];
    if ((r->flags & 1) && game_time >= r->switch_time)
        hudview_26C224(v, r->next_mode);
    WATCOM_CALL(hud_mode_fns[r->mode].update, v);
    if (r->timer) {
        if (r->timer <= game_time)
            r->timer = 0;
        DIRTY(HUD_VIEW_W(v));
    }
    if (d != hud_last_damage) {
        if (F9C[0x1E])
            hudview_26C848(1);
        hud_last_damage = d;
    }
}

void hudview_26BE64(HudWidget *w)
{
    long v;
    HudView *r;

    if (CAM_DA && !(hud_35AEF0 & 1)) {
        gr_set_canvas(&tnui_back_canvas);
        grd_canvas->gc.fcolor = 0;
        WATCOM_CALL(grd_canvas_table[DRAW_BOX], (short)(w->x0 - 1), (short)(w->y0 - 1),
                    (short)(w->x1 + 1), (short)(w->y1 + 1));
        gr_set_canvas(&w->canvas);
    } else if (CAM_DA)
        return;
    v = w->id;
    r = &hud_views[v];
    if (r->timer)
        hudview_26C50C(v);
    else if ((game_35ADE0 == 1 || game_35ADE0 == 3) && (r->flags & 8))
        hudview_26C50C(v);
    else
        WATCOM_CALL(hud_mode_fns[r->mode].draw, v);
    hudutil_27B5E4(r->mode + 0x1780003);
}

void hudview_26BF3C(void)
{
    hud_views[0].mode_mask = 0;
    hud_views[1].mode_mask = 0;
    hud_views[2].mode_mask = 0x10800;
    if (*(long *)PBA_SLOT(0x0D) >= 0)
        hud_views[2].mode_mask = 0x11800;
    hudview_26C224(0, (game_359708 == 0x27 && hud_38B9AD == 3) ? 0x1A : 1);
    hudview_26C224(1, 9);
    hudview_26C224(2, 10);
}

/* Remove the current mode m of a view: step to the next mode first if m is current. */
static void drop_current(long view)
{
    long m = hud_views[view].mode;

    hudview_26C360(view, 1, 1);
    hud_views[view].mode_mask &= ~(1UL << (m & 31));
    hudcb_26444C(view);
}

void hudview_26BFC4(uchar kind, uchar view, uchar on)
{
    HudView *r = &hud_views[view];
    uchar s;

    if (on) {
        if (r->mode == kind + 0x0D)
            return;
        hud_view_saved_mode[view] = r->mode;
        hudview_26C224(view, hud_dmg_mode[kind]);
        return;
    }
    if (r->mode != kind + 0x0D)
        return;
    s = hud_view_saved_mode[view];
    if ((hud_mode_info[s][0] & 0x20) && game_396868 == 0) {
        drop_current(view);
        return;
    }
    if (hud_view_saved_mode[view] == 8 && *(short *)&game_camera[GAME_CAM_BC] == game_player_entity) {
        drop_current(view);
        return;
    }
    r->mode_mask &= ~(1UL << (r->mode & 31));
    hudview_26C224(view, hud_view_saved_mode[view]);
}

void hudview_26C224(long a, long b)
{
    HudView *r = &hud_views[a];

    if (r->mode)
        WATCOM_CALL(hud_mode_fns[r->mode].activate, (long)HUD_VIEW_W(a), 0);
    r->mode = (uchar)b;
    if (b) {
        r->flags &= 0xE1;
        r->flags |= hud_mode_info[b][0];
        WATCOM_CALL(hud_mode_fns[r->mode].activate, (long)HUD_VIEW_W(a), 1);
        hud_views[a].mode_mask |= 1UL << (b & 31);
        DIRTY(HUD_VIEW_W(a));
        hudcb_26444C(a);
        DIRTY(hud_widgets[0x14]);
    }
    r->flags &= 0xFE;
}

void hudview_26C2C8(long view, long mode, long set)
{
    if (set) {
        hudview_26C224(view, mode);
        return;
    }
    hud_views[view].mode_mask |= 1UL << (mode & 31);
    hudcb_26444C(view);
}

void hudview_26C2F0(long view, long mode, long delay)
{
    hud_views[view].next_mode = (uchar)mode;
    hud_views[view].switch_time = delay + game_time;
    hud_views[view].flags |= 1;
}

void hudview_26C318(long view, long mode)
{
    if (hud_views[view].mode == mode)
        hudview_26C360(view, 1, 1);
    hud_views[view].mode_mask &= ~(1UL << (mode & 31));
    hudcb_26444C(view);
}

uchar hudview_26C360(long view, long dir, uchar force)
{
    HudView *r = &hud_views[view];
    long m;

    if (!r->mode_mask)
        return 0;
    if ((r->flags & 2) && !force)
        return 0;
    m = r->mode;
    do {
        if (dir > 0) {
            if (++m == 32)
                m = 0;
        } else {
            if (--m < 0)
                m = 31;
        }
    } while (!(r->mode_mask & (1UL << (m & 31))));
    if (r->mode == m)
        return 0;
    SndPlay(0x7E8, 0, 0x28, 1, 0x40, 0x40, 0);
    hudview_26C224(view, m);
    return 1;
}

long hudview_26C410(long mode)
{
    long i;

    for (i = 0; i < 3; i++)
        if (hud_views[i].mode_mask & (1UL << (mode & 31)))
            return i;
    return -1;
}

void hudview_26C44C(uchar on)
{
    long i;

    for (i = 0; i < 3; i++)
        if (hud_views[i].mode_mask & 0x100)
            return;
    i = (hud_views[1].mode != 9) + 1;
    if (on) {
        hudview_26C224(i, 8);
        return;
    }
    hud_views[i].mode_mask |= 0x100;
    hudcb_26444C(i);
}

void hudview_26C4A4(void)
{
}

void hudview_26C4A8(void)
{
    Ref ref = 0;

    switch (hud_389BC2 & 0x3F) {
    case 1: ref = 0x16F0042; break;
    case 2: ref = 0x16F0043; break;
    case 4: ref = 0x16F0044; break;
    }
    if (ref) {
        grd_canvas->gc.fcolor = 0x1D;
        strutil_2DBCF8(0x1F7, ref, 4, 2, 0);
    }
}

void hudview_26C50C(long view)
{
    grs_bitmap *f = RefLock(0x23D0000);
    HudWidget *w;
    long dx, dy;

    f->bits = (uchar *)f + 0x1C;
    w = HUD_VIEW_W(view);
    dx = RndRange(&hud_rnd, 0, f->w - w->canvas.bm.w);
    dy = RndRange(&hud_rnd, 0, f->h - w->canvas.bm.h);
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * f->type], (long)f, (short)-dx, (short)-dy);
    ResUnlock(0x23D);
}

void hudview_26C598(long a)
{
    hud_views[2].mode_mask |= 1UL << (a & 31);
}

void hudview_26C5B4(long a)
{
    long v;

    if ((ulong)a == 7)
        v = 1;
    else if ((ulong)a == 0x10)
        v = 2;
    else
        v = 0;
    hud_views[v].mode_mask &= ~(1UL << (a & 31));
}

void hudview_26C5F4(long a)
{
    hudview_26C600((a != 7) + 1);
}

void hudview_26C600(long view)
{
    DIRTY(HUD_VIEW_W(view));
}

void hudview_26C60C(long code)
{
    long view = code & 0xF, k = code >> 4;
    long snd = -1;
    ulong d = hud_btn_mode[k];
    HudView *r = &hud_views[view];

    if (r->mode == d && view == 0) {
        snd = SndPlay(0x7EA, 0, 0x40, 1, -1, 0x40, 0);
        d = 1;
        *(uchar *)&r->mode_mask |= 2;
        hudview_26C224(view, 1);
        cmd_25F044();
    }
    if (k == 7 && game_396868) {
        if (r->mode_mask & (1UL << 0x11))
            d = 0x11;
        else if (r->mode_mask & (1UL << 0x12))
            d = 0x12;
        else
            d = 8;
    } else if (k == 6 && game_3968B5)
        d = 0x13;
    if (r->mode_mask & (1UL << (d & 31))) {
        if (snd < 0)
            SndPlay(0x7EA, 0, 0x40, 1, -1, 0x40, 0);
        hudview_26C224(view, d);
    }
}

Ref hudview_26C72C(long code)
{
    long view = code & 0xF, k = code >> 4;
    ulong m = hud_views[view].mode;
    ulong d;

    if (k == 7) {
        if ((long)m >= 0x10 && (long)m <= 0x12 && !game_3968B5)
            return 0x2F0000B + k;
        if (m == 8 && *(short *)&game_camera[GAME_CAM_BC] == game_396868)
            return 0x2F0000B + k;
        if (game_3968B5)
            return 0x2F00015 + k;
        return 0x2F00001 + k;
    }
    if (k == 6) {
        if (((long)m >= 0x13 && (long)m <= 0x14) || m == 0x0C)
            return 0x2F0000B + k;
        if (hud_views[view].mode_mask & 0xF0000)
            return 0x2F00001 + k;
        return 0x2F00015 + k;
    }
    d = hud_btn_mode[k];
    if (m == d)
        return 0x2F0000B + k;
    if (hud_views[view].mode_mask & (1UL << (d & 31)))
        return 0x2F00001 + k;
    return 0x2F00015 + k;
}

void hudview_26C820(void)
{
    if (F9C[0x1E])
        hudview_26C848(0);
    else if (!hud_38FC80)
        hudview_26C848(1);
}

void hudview_26C848(uchar on)
{
    uchar d = damage_27DF14(game_player_entity, 6);
    long i;

    if (!on) {
        for (i = 0; i < 3; i++)
            hudview_26BFC4((uchar)i, (uchar)i, on);
        if (game_camera_38FB20) {
            for (i = 0; i < 3; i++)
                if (hud_view_saved_mode[i] == 8)
                    i = 4;
            if (i != 4) {           /* always: the loop leaves i at 3 or 5 */
                hud_views[1].mode_mask &= ~(1UL << (hud_views[2].mode & 31));
                hudview_26C224(1, 8);
            }
        }
        F9C[0x1E] = 0;
        DIRTY(hud_widgets[0x1C]);
        return;
    }
    if (d <= 0x14) {
        if (d <= 1) {
            for (i = 0; i < 3; i++)
                hudview_26BFC4((uchar)i, (uchar)i, 0);
        } else {
            hudview_26BFC4(0, 0, 0);
            hudview_26BFC4(1, 1, 1);
            hudview_26BFC4(2, 2, 0);
        }
    } else {
        for (i = 0; i < 3; i++)
            hudview_26BFC4((uchar)i, (uchar)i, 1);
    }
    F9C[0x1E] = 1;
    DIRTY(hud_widgets[0x1C]);
}

uchar hudview_26C898(void)
{
    ulong m = hud_views[1].mode;

    return m == 9 || m == 0x19 || m == 0x16;
}

void hudview_26C8B4(void)
{
    long r;

    *(short *)((uchar *)game_438C20 + 0x2E) = 1;
    r = HudGetCompatibleView();
    if (r == 0x11) {
        if (F9C[0x1E])
            hud_views[2].mode_mask |= 1UL << 0x11;
        else {
            if (hud_views[2].mode == 8)
                hudview_26C360(2, 1, 1);
            hud_views[2].mode_mask &= ~0x100UL;
            hudcb_26444C(2);
            hudview_26C224(2, 0x11);
        }
    }
    hudview_26C44C(F9C[0x1E] == 0);
}

void hudview_26C980(long mode)
{
    long i;

    for (i = 0; i < 3; i++)
        if (hud_views[i].mode_mask & (1UL << (mode & 31)))
            break;
    if (i == 3)
        return;
    if (hud_views[i].mode == mode)
        hudview_26C360(i, 1, 1);
    hud_views[i].mode_mask &= ~(1UL << (mode & 31));
    hudcb_26444C(i);
    if (!(hud_views[1].mode_mask & 0x100))
        return;
    if (hud_views[1].mode == 8)
        hudview_26C360(1, 1, 1);
    hud_views[1].mode_mask &= ~0x100UL;
    hudcb_26444C(1);
    if (F9C[0x1E] == 0)
        hudview_26C224(2, 8);
    else {
        hud_views[2].mode_mask |= 0x100;
        hudcb_26444C(2);
    }
}

uchar hudview_26CAAC(long view)
{
    uchar m = hud_views[view].mode;

    return m == 0x11 || m == 0x13;
}

uchar hudview_26CAC8(long view)
{
    return hud_views[view].mode == 4;
}

uchar hudview_26CAD8(long view)
{
    return hud_views[view].mode == 8;
}

uchar *hudview_26CAE8(void)
{
    return hud_views[1].f10;
}

void hudview_26CAF0(uchar v)
{
    long i;

    for (i = 0; i < 3; i++)
        if (hud_views[i].flags & 4)
            hud_views[i].f10[0x27] = v;
}

void hudview_26CB14(void)
{
    if (game_camera_38FB20 && !(hud_views[2].mode_mask & 0x100))
        hudview_26C224(1, 8);
    else
        hudview_26C224(1, 9);
}
