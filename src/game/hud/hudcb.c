/* hudcb.c -- Terra Nova module M017 (0x2641C0-0x2656F8): HUD widget callbacks (the event /
 * update / draw functions the helmet code M016 puts into the HUD widgets: view buttons, map
 * zoom, gauges, clock, weapon icon, the save-under bitmaps of a few widgets).  Decompiled from
 * __FF.EXE v1.09.
 *
 *   0x2641C0 hudcb_2641C0 grab the save-under bitmaps   0x2642C8 hudcb_2642C8 free (no callers)
 *   0x2642D4 hudcb_2642D4 free the save-unders           0x264364 hudcb_264364 grab one
 *   0x2643F0 hudcb_2643F0 recolour a widget canvas       0x26444C hudcb_26444C redraw view buttons
 *   0x264494 hudcb_264494 (empty)                        0x264498 hudcb_264498 view button event
 *   0x2644AC hudcb_2644AC view button draw               0x2644E4 hudcb_2644E4 target button event
 *   0x26451C hudcb_26451C (empty)                        0x264520 hudcb_264520 target button draw
 *   0x264560 hudcb_264560 (empty)                        0x264598 hudcb_264598 toggle button event
 *   0x2645F0 hudcb_2645F0 toggle button draw             0x264630 hudcb_264630 zoom number (current)
 *   0x264644 hudcb_264644 zoom number draw               0x264680 hudcb_264680 blink on / off
 *   0x2646D8 hudcb_2646D8 set blink state                0x2646F0 hudcb_2646F0 blink button event
 *   0x264738 hudcb_264738 blink update                   0x26477C hudcb_26477C blink draw
 *   0x2647D0 hudcb_2647D0 fire button event              0x2647E8 hudcb_2647E8 camera button draw
 *   0x264840 hudcb_264840 map centre button event        0x26485C hudcb_26485C centre the map
 *   0x2648E4 hudcb_2648E4 map centre draw                0x26493C hudcb_26493C map centre state
 *   0x264948 hudcb_264948 mark redraw (update)           0x26494C hudcb_26494C (empty)
 *   0x264950 hudcb_264950 map button event               0x264990 hudcb_264990 map button draw
 *   0x264A68 hudcb_264A68 map zoom button event          0x264A84 hudcb_264A84 zoom the map
 *   0x264AC4 hudcb_264AC4 zoom flash update              0x264AEC hudcb_264AEC zoom flash draw
 *   0x264B50 hudcb_264B50 fire button 2 event            0x264B7C hudcb_264B7C recolour widget e10
 *   0x264B98 hudcb_264B98 zoom - button event            0x264C38 hudcb_264C38 zoom - flash start
 *   0x264C5C hudcb_264C5C zoom - flash update            0x264CA4 hudcb_264CA4 recolour widget e04
 *   0x264CDC hudcb_264CDC zoom + button event            0x264D68 hudcb_264D68 zoom + flash start
 *   0x264D84 hudcb_264D84 zoom + flash update            0x264DCC hudcb_264DCC recolour widget e08
 *   0x264DE8 hudcb_264DE8 jump jet button event          0x264E70 hudcb_264E70 jump jet gauge
 *   0x265028 hudcb_265028 weapon cycle event             0x265080 hudcb_265080 weapon icon (current)
 *   0x265094 hudcb_265094 weapon icon draw               0x265158 hudcb_265158 number (current)
 *   0x26516C hudcb_26516C number draw                    0x2651C8 hudcb_2651C8 set the clock
 *   0x2651EC hudcb_2651EC clock update                   0x26521C hudcb_26521C clock draw
 *   0x265250 hudcb_265250 clock draw (current)           0x265290 hudcb_265290 (no callers)
 *   0x2652A0 hudcb_2652A0 bar 0 update                   0x2652F4 hudcb_2652F4 bar 0 draw
 *   0x265398 hudcb_265398 bar 7 scale (no callers)       0x2653D8 hudcb_2653D8 bar 7 update
 *   0x26541C hudcb_26541C bar 7 draw                     0x2654D0 / 0x2654E0 view arrow update
 *   0x2654F0 / 0x265560 view arrow - / + event           0x2655CC / 0x265664 view arrow draw
 *
 * Watcom merged code across functions; each function here is written on its own:
 *  - 0x264630, 0x265080, 0x265158 set a widget's canvas and fall into the next function;
 *  - 0x264644 shares its sprintf / text tail with the bytes after 0x264560 (0x264561 is not a
 *    function: 0x264560 is a bare `ret`);
 *  - 0x264840 falls into 0x26485C; 0x264A68 jumps into 0x264A84, whose tail sits inside
 *    0x264990's range; 0x26493C falls through 0x264948 (mark redraw) and 0x26494C (ret);
 *  - 0x264494 / 0x26451C / 0x264560 / 0x26494C are empty callbacks sharing a `ret`.
 *
 * Quirks kept:
 *  - hudcb_2641C0 / 265398 / 2652A0 / 26516C index game_pba by the player's entity id, not its
 *    PBA slot (the others use game_player_slot).
 *  - hudcb_2642D4 frees the save-unders without clearing the pointers.
 *  - Divisions by meter maxima are not guarded (the original faults on 0).
 */
#include "g7_c.h"
#include "g5_b.h"     /* game_35A540 */
#include "g7_a.h"     /* added at the G5-G8 merge */
#include "g8_c.h"     /* added at the G5-G8 merge */

#define DRAW_CLEAR 8
#define DRAW_URECT 15
#define DRAW_RECT 16
#define DRAW_BOX 18
#define GET_DEVICE_BITMAP 207

#define PBA_SLOT(off) ((uchar *)&game_pba[game_player_slot] + (off))
#define PBA_ENT(off) ((uchar *)&game_pba[game_player_entity] + (off))
#define DIRTY(w) ((w)->flags |= 0x20)

/* A FrameDesc resource (grs_bitmap + update rect + palette offset, 0x1C bytes) drawn at 0,0. */
static void draw_frame_u(Ref ref)
{
    grs_bitmap *bm = RefGet(ref);
    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, 0, 0);
}

static void draw_frame(Ref ref, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);
    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, x, y);
}

static void draw_bm_u(grs_bitmap *bm)
{
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, 0, 0);
}

static void draw_bm(grs_bitmap *bm, long x, long y)
{
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, x, y);
}

static void gr_clear0(void)
{
    WATCOM_CALL(grd_canvas_table[DRAW_CLEAR], 0);
}

static void snd_click(ushort id)
{
    SndPlay(id, 0, 0x40, 1, -1, 0x40, 0);
}

/* ------------------------------------------------------------------ save-under bitmaps */

#ifndef TN_WATCOM_ABI
/* G42: the health and energy bars' rectangles end before the end of their art: by one column in
 * the 200-row helmet and by one column and a whole segment in the 400-row one, so that end never
 * goes dark as the bar runs down. The rectangle is carried on to the right over the bar's own
 * colours (those in its middle row), up to the frame, at most 8 columns; so is the goggles' twin
 * of the bar (the same rectangle, another widget, drawing the same saved bar). The game's grid
 * picture drawn over the emptied bar ends at the old width, so in the added columns the art's
 * grid colours (those of the rows just above and below the bar) are put back. */
static short hud_bar_ow[2];             /* the rectangle's own width: 0 health, 1 energy */
static uchar hud_bar_grid[2][256];

static void hudcb_bar_widen(HudWidget *w, long n)
{
    w->x1 += n;
    gr_init_sub_bitmap(&tnui_back_canvas.bm, &w->canvas.bm, w->x0, w->y0,
                       (short)(w->x1 - w->x0), (short)(w->y1 - w->y0));
    gr_init_gc(&w->canvas);
}

static void hudcb_bar_to_art(long id, int k)
{
    HudWidget *w = hud_widgets[id];
    const grs_bitmap *b = &tnui_back_canvas.bm;
    uchar in[256] = { 0 };
    long y = (w->y0 + w->y1) / 2, x, n = 0, i;
    hud_bar_ow[k] = (short)(w->x1 - w->x0);
    if (w->x1 <= w->x0 || y < 0 || y >= b->h || w->y0 < 1 || w->y1 >= b->h)
        return;
    for (x = w->x0; x < w->x1 && x < b->w; x++)
        in[b->bits[(size_t)y * b->row + x]] = 1;
    while (n < 8 && w->x1 + n < b->w && in[b->bits[(size_t)y * b->row + w->x1 + n]])
        n++;
    if (!n)
        return;
    memset(hud_bar_grid[k], 0, 256);
    for (x = w->x0; x < w->x1 + n; x++) {
        hud_bar_grid[k][b->bits[(size_t)(w->y0 - 1) * b->row + x]] = 1;
        hud_bar_grid[k][b->bits[(size_t)w->y1 * b->row + x]] = 1;
    }
    for (i = 0; i < 69; i++) {
        HudWidget *t = hud_widgets[i];
        if (i != id && t && t->x0 == w->x0 && t->y0 == w->y0 && t->x1 == w->x1 && t->y1 == w->y1)
            hudcb_bar_widen(t, n);
    }
    hudcb_bar_widen(w, n);
}

/* the emptied part of the added columns: the art's grid back (current canvas = the bar's) */
static void hudcb_bar_tail(int k, const grs_bitmap *bm, long v)
{
    grs_bitmap *c = &grd_canvas->bm;
    long x, y;
    for (x = v > hud_bar_ow[k] ? v : hud_bar_ow[k]; x < bm->w && x < c->w; x++)
        for (y = 0; y < bm->h && y < c->h; y++) {
            uchar a = bm->bits[(size_t)y * bm->row + x];
            if (hud_bar_grid[k][a])
                c->bits[(size_t)y * c->row + x] = a;
        }
}
#endif

void hudcb_2641C0(void)
{
    gr_set_canvas(&tnui_back_canvas);
#ifndef TN_WATCOM_ABI
    hudcb_bar_to_art(0x17, 0);
    hudcb_bar_to_art(0x16, 1);
#endif
    hudcb_264364(&hud_save_bm[5], 0x1A);
    hudcb_264364(&hud_save_bm[4], 0x1B);
    hudcb_264364(&hud_save_bm[3], 0x1C);
    hudcb_264364(&hud_save_bm[2], 0x1D);
    hudcb_264364(&hud_save_bm[1], 7);
    hudcb_264364(&hud_save_bm[6], 8);
    hudcb_264364(&hud_save_bm[0], 0x17);
    hudcb_264364(&hud_save_bm[7], 0x16);
    hud_save_valid = 1;
    hud_38BA44 = hud_save_bm[0].w;
    hud_blink_time = 0;
    hud_blink = 0;
    hud_38BA48 = 0;
    /* fix_div(w << 16, meter maximum) inline (idiv of w << 32) */
    hud_38BA34 = (fix)(((long long)hud_save_bm[7].w << 32) / *(long *)PBA_ENT(0x8A));
    hud_38BA4A = 0;
    hud_38BA4C = 0;
    hud_38BA51 = 0;
    hud_38BA42 = hud_save_bm[7].w;
}

void hudcb_2642C8(void *p)
{
    if (p)
        f_free(p);
}

void hudcb_2642D4(void)
{
    static const uchar order[8] = { 5, 4, 3, 2, 1, 6, 0, 7 };
    int i;
    if (!hud_save_valid)
        return;
    for (i = 0; i < 8; i++)
        if (hud_save_bm[order[i]].bits)
            f_free(hud_save_bm[order[i]].bits);
    hud_save_valid = 0;
}

/* Allocates bm the size of widget id and copies the screen under it (current canvas). */
void hudcb_264364(grs_bitmap *bm, long id)
{
    HudWidget *w = hud_widgets[id];
    bm->type = BMT_FLAT8;
    bm->w = bm->row = w->x1 - w->x0;
    bm->h = w->y1 - w->y0;
    if (bm->row == 0 || bm->h == 0) {
        bm->bits = NULL;
        return;
    }
    bm->bits = f_malloc((ulong)bm->row * bm->h);
    WATCOM_CALL(grd_canvas_table[GET_DEVICE_BITMAP + 2 * bm->type], (long)bm, w->x0, w->y0);
}

/* Replaces colour `from` by `to` in the widget's canvas bitmap. */
void hudcb_2643F0(HudWidget *w, uchar from, uchar to)
{
    uchar *p = w->canvas.bm.bits;
    long x, y;
    for (y = 0; y < w->canvas.bm.h; y++) {
        for (x = 0; x < w->canvas.bm.w; x++, p++)
            if (*p == from)
                *p = to;
        p += w->canvas.bm.row - w->canvas.bm.w;
    }
}

void hudcb_26444C(long view)
{
    long i;
    if (view == 1)
        return;
    for (i = hud_view_first_w[view]; i <= hud_view_last_w[view]; i++)
        DIRTY(hud_widgets[i]);
    DIRTY(hud_widgets[0x1E]);
}

void hudcb_264494(void)
{
}

/* view mode buttons */
void hudcb_264498(HudWidget *w, HudEvent *ev)
{
    if (ev->type == 1)
        hudview_26C60C(w->id);
}

void hudcb_2644AC(HudWidget *w)
{
    draw_frame(hudview_26C72C(w->id), 0, 0);
}

/* target button */
void hudcb_2644E4(HudWidget *w, HudEvent *ev)
{
    if (ev->type != 1 || targ_current == 0)
        return;
    hudview_26C224(0, hud_views[0].mode == 0x15 ? 1 : 0x15);
    DIRTY(hud_widgets[0x14]);
}

void hudcb_26451C(void)
{
}

void hudcb_264520(HudWidget *w)
{
    draw_frame_u(targ_current ? 0x2F00013 : 0x2F0001D);
}

void hudcb_264560(void)
{
}

void hudcb_264598(HudWidget *w, HudEvent *ev)
{
    uchar v;
    if (ev->type != 1)
        return;
    v = hud_35A9B9 == 0;
    DIRTY(hud_widgets[0x1F]);
    hud_397B6C = v;
    hud_35A9B9 = v;
    SndPlay(0x7E2, 0, 0x28, 1, 0x40, *(signed char *)RefGet(0x89C0076), 0);
}

void hudcb_2645F0(HudWidget *w)
{
    draw_frame_u(hud_35A9B9 ? 0x2F00024 : 0x2F00023);
}

/* ------------------------------------------------------------------ zoom number */

void hudcb_264630(void)
{
    gr_set_canvas(&hud_w_389DDC->canvas);
    hudcb_264644(hud_w_389DDC);
}

void hudcb_264644(HudWidget *w)
{
    char buf[12];
    long n;
    gr_clear0();
    grd_canvas->gc.fcolor = 0x44;
    if (game_camera_38FB20)
        n = *(short *)((uchar *)game_camera_38FB20 + 8) - 2;
    else
        n = *(short *)&game_camera[GAME_CAM_ZOOM] + 1;
    lg_sprintf(buf, "%d", n);
    GfxDrawText(0x1F6, buf, 1, 0);
}

/* ------------------------------------------------------------------ blinking button */

void hudcb_264680(uchar on)
{
    uchar b = hud_blink;
    if (on) {
        hud_blink_time = tn_ticks + 500;
        if (b != 1) {
            DIRTY(hud_widgets[0x15]);
            b = 1;
        }
    } else {
        hud_blink_time = 0;
        if (b) {
            DIRTY(hud_widgets[0x15]);
            b = 0;
        }
    }
    hud_blink = b;
}

void hudcb_2646D8(uchar on)
{
    if (on == hud_blink)
        return;
    hud_blink = on;
    DIRTY(hud_widgets[0x15]);
}

void hudcb_2646F0(HudWidget *w, HudEvent *ev)
{
    if (ev->type != 1)
        return;
    if (hud_38FD03) {
        hud_blink_time = 0;
        hudcb_2646D8(0);
        return;
    }
    snd_click(0x7EB);
    cmd_25F138();
}

void hudcb_264738(HudWidget *w)
{
    uchar b, nb;
    if (hud_blink_time == 0 || hud_blink_time > tn_ticks)
        return;
    hud_blink_time = tn_ticks + 500;
    b = hud_blink;
    nb = b == 0;
    if (nb != b) {
        hud_blink = nb;
        DIRTY(hud_widgets[0x15]);
    }
}

void hudcb_26477C(HudWidget *w)
{
    ResLock(0x2F0);
    draw_frame_u(hud_blink ? 0x2F00014 : 0x2F0000A);
    ResUnlock(0x2F0);
}

void hudcb_2647D0(HudWidget *w, HudEvent *ev)
{
    if (ev->type == 1 || ev->type == 4)
        fire_2921A8();
}

void hudcb_2647E8(HudWidget *w)
{
    if (game_camera_38FB20)
        draw_frame_u(0x2F00021);
    else
        draw_bm_u(&hud_save_bm[5]);
}

/* ------------------------------------------------------------------ tactical map buttons */

void hudcb_264840(HudWidget *w, HudEvent *ev)
{
    if (ev->type == 1 && hudview_26C898())
        hudcb_26485C(1);
}

/* centre the map on the player */
void hudcb_26485C(uchar snd)
{
    uchar *map;
    GameEntity *e;
    if (game_359708 == 0x2E)
        return;
    map = hudview_26CAE8();
    if (!map)
        return;
    if (snd)
        snd_click(0x7E1);
    map[0x26] &= ~1;
    e = &game_entities[game_player_entity];
    mapdisp_276C4C(map, e->x, e->y);
    hud_38BA51 = 1;
    DIRTY(hud_widgets[0x1B]);
}

void hudcb_2648E4(HudWidget *w)
{
    if (hud_38BA51)
        draw_frame_u(0x2F00020);
    else
        draw_bm_u(&hud_save_bm[4]);
}

void hudcb_26493C(uchar v)
{
    hud_38BA51 = v;
    hudcb_264948(hud_widgets[0x1B]);
}

void hudcb_264948(HudWidget *w)
{
    DIRTY(w);
}

void hudcb_26494C(void)
{
}

void hudcb_264950(HudWidget *w, HudEvent *ev)
{
    if (game_359708 == 0x2E)
        return;
    if (ev->type != 1 && ev->type != 4)
        return;
    snd_click(0x7E1);
    hudview_26C820();
}

void hudcb_264990(HudWidget *w)
{
    if (!hud_save_bm[3].bits)
        return;
    if ((*(uchar **)PBA_SLOT(0x9C))[0x1E])
        draw_frame_u(0x2F0001F);
    else
        draw_bm_u(&hud_save_bm[3]);
}

void hudcb_264A68(HudWidget *w, HudEvent *ev)
{
    if (!hudview_26C898())
        return;
    if (ev->type == 1)
        hudcb_264A84(0);
    else if (ev->type == 4)
        hudcb_264A84(1);
}

#ifndef TN_WATCOM_ABI
int port_map_zoom_ok(const void *map, long zoom);   /* mapdisp.c: widescreen */
#endif

/* map zoom one step in (out = 0) or out */
void hudcb_264A84(uchar out)
{
    uchar *map;
    long z;
    if (game_359708 == 0x2E)
        return;
    map = hudview_26CAE8();
    if (!map)
        return;
    if (out) {
        if (*(short *)(map + 8) <= 0)
            return;
#ifndef TN_WATCOM_ABI
        if (!port_map_zoom_ok(map, *(short *)(map + 8) - 1))     /* widescreen (mapdisp.c) */
            return;
#endif
    } else {
        if (*(short *)(map + 8) >= 7)
            return;
    }
    snd_click(0x7E1);
    z = *(short *)(map + 8);
    mapdisp_276BC8(map, out ? z - 1 : z + 1);
    hud_map_zoom = *(short *)(map + 8);
    hud_38BA48 = 3;
}

void hudcb_264AC4(HudWidget *w)
{
    if (hud_38BA48) {
        hud_38BA48--;
        DIRTY(w);
    }
}

void hudcb_264AEC(HudWidget *w)
{
    if (hud_38BA48)
        draw_frame_u(0x2F00022);
    else
        draw_bm(&hud_save_bm[2], 0, 0);
}

void hudcb_264B50(HudWidget *w, HudEvent *ev)
{
    long t = ev->type;
    if (t == 1 || t == 4 || t == 0x10 || t == 0x20)
        fire_292180();
}

/* ------------------------------------------------------------------ camera zoom buttons */

void hudcb_264B7C(void)
{
    hudcb_2643F0(hud_w_389E10, 0x46, 0x43);
}

void hudcb_264B98(HudWidget *w, HudEvent *ev)
{
    long t = ev->type;
    if (t == 1 || t == 4) {
        if (game_camera_38FB20)
            hud_26AFB0(-1);
        else
            hud_26B800(-1);
    } else if (t == 0x10 || t == 0x20) {
        if (game_camera_38FB20)
            hud_26AFB0(-1);
        else
            hud_26B800(-0x63);
    } else
        return;
    if (game_359708 != 0x27) {
        hud_38BA4C = 300;
        hud_38BA30 = game_time;
    }
}

void hudcb_264C38(void)
{
    if (game_359708 != 0x27) {
        hud_38BA4C = 300;
        hud_38BA30 = game_time;
    }
}

void hudcb_264C5C(HudWidget *w)
{
    short d = hud_38BA4C;
    ulong t = game_time;
    hud_38BA4E = 0;
    if (d) {
        /* only the low 16 bits of the subtraction matter (the original subtracts the low word
         * of hud_38BA30 from the whole time) */
        d = (short)(d - (short)((ushort)t - (ushort)hud_38BA30));
        if (d < 0)
            d = 0;
        hud_38BA4E = 1;
    }
    hud_38BA30 = t;
    hud_38BA4C = d;
}

void hudcb_264CA4(void)
{
    if (hud_38BA4C)
        hudcb_2643F0(hud_w_389E04, 0x46, 0x43);
    else
        hudcb_2643F0(hud_w_389E04, 0x43, 0x46);
    hud_w_389E04->flags |= 0x80;
}

void hudcb_264CDC(HudWidget *w, HudEvent *ev)
{
    long t = ev->type;
    if (t == 1 || t == 4) {
        if (game_camera_38FB20)
            hud_26AFB0(1);
        else
            hud_26B800(1);
    } else if (t == 0x10 || t == 0x20) {
        if (game_camera_38FB20)
            hud_26AFB0(1);
        else
            hud_26B800(0x63);
    } else
        return;
    hud_38BA4A = 300;
    hud_38BA38 = game_time;
}

void hudcb_264D68(void)
{
    hud_38BA4A = 300;
    hud_38BA38 = game_time;
}

void hudcb_264D84(HudWidget *w)
{
    short d = hud_38BA4A;
    ulong t = game_time;
    hud_38BA50 = 0;
    if (d) {
        d = (short)(d - (short)((ushort)t - (ushort)hud_38BA38));
        if (d < 0)
            d = 0;
        hud_38BA50 = 1;
    }
    hud_38BA38 = t;
    hud_38BA4A = d;
}

void hudcb_264DCC(void)
{
    hudcb_2643F0(hud_w_389E08, 0x46, 0x43);
}

/* ------------------------------------------------------------------ jump jets */

void hudcb_264DE8(HudWidget *w, HudEvent *ev)
{
    if (game_359708 == 0x2E)
        return;
    if (ev->type == 1) {
        if (*(ushort *)PBA_SLOT(0xAA) == 0)
            return;
        obj55_288EC0(PBA_SLOT(0xA2), 1);
        control_2D8568();
        hud_drag_widget = w;
    } else if (ev->type == 2) {
        obj55_288EC0(PBA_SLOT(0xA2), 0);
        hud_drag_widget = NULL;
    }
}

/* the jump jet fuel gauge: vertical in mission kind 0x27, else horizontal */
void hudcb_264E70(void)
{
    short *r;
    long cur, max;
    ushort v;
    gr_set_canvas(&tnui_back_canvas);
    r = game_camera[GAME_CAM_DA] ? hud_bar_rects[1] : hud_bar_rects[0];
    cur = *(ushort *)PBA_SLOT(0xAA);
    max = *(ushort *)PBA_SLOT(0xAC);
    if (game_359708 == 0x27) {
        v = (ushort)(r[3] - (r[3] - r[1]) * cur / max);
        if (v != (long)r[3]) {
            grd_canvas->gc.fcolor = 0x44;
            WATCOM_CALL(grd_canvas_table[DRAW_URECT], r[0], (short)v, r[2], r[3]);
        }
        if (v != (long)r[1]) {
            grd_canvas->gc.fcolor = 0;
            WATCOM_CALL(grd_canvas_table[DRAW_URECT], r[0], r[1], r[2], (short)v);
        }
    } else {
        v = (ushort)(r[0] + (r[2] - r[0]) * cur / max);
        if (v != (long)r[0]) {
            grd_canvas->gc.fcolor = 0x44;
            WATCOM_CALL(grd_canvas_table[DRAW_URECT], r[0], r[1], (short)v, r[3]);
        }
        if (v != (long)r[2]) {
            grd_canvas->gc.fcolor = 0;
            WATCOM_CALL(grd_canvas_table[DRAW_URECT], (short)v, r[1], r[2], r[3]);
        }
    }
}

/* ------------------------------------------------------------------ weapon */

void hudcb_265028(HudWidget *w, HudEvent *ev)
{
    if (ev->type != 1 && ev->type != 4)
        return;
    pbasim_28AB74(PBA_SLOT(6), ev->type == 1 ? 1 : -1);
    DIRTY(w);
}

void hudcb_265080(void)
{
    gr_set_canvas(&hud_w_389F34->canvas);
    hudcb_265094(hud_w_389F34);
}

void hudcb_265094(HudWidget *w)
{
    uchar *p6 = PBA_SLOT(6);
    uchar *ws = p6 + 0x3F + (signed char)p6[0xC8] * 0xF;
    uchar r;
    draw_frame_u(hud_weapon_icon[ws[0]]);
#ifndef TN_WATCOM_ABI
    {   /* the weapon's picture over the main view (the Heavy's and Scout's box): all its pixels
         * the HUD's, kept until it is drawn again, so none of its colours that the 3D happens to
         * have there shows the pack's art behind it (green specks at night, owner) */
        extern void port_s3d_hudkeep(const void *bm, long x, long y);
        port_s3d_hudkeep(RefGet(hud_weapon_icon[ws[0]]), 0, 0);
    }
#endif
    r = weapon_ready(ws, p6 + 0x80);
    if (r == 0 || r == 3)
        return;
#ifndef TN_WATCOM_ABI
    {
        /* G36: the Scout's and the pirate's goggles have a 25 x 25 weapon box for the 26 x 26
         * picture, which the game draws whole (unclipped), so the tint of the box's size left the
         * picture's last column and row as they were: a gap along the window's right and bottom
         * edges in the red box (no ammunition).  The tint covers the picture. */
        grs_bitmap *bm = RefGet(hud_weapon_icon[ws[0]]);
        if (bm->w > w->canvas.bm.w || bm->h > w->canvas.bm.h) {
            long tw = bm->w > w->canvas.bm.w ? bm->w : w->canvas.bm.w;
            long th = bm->h > w->canvas.bm.h ? bm->h : w->canvas.bm.h;
            gr_set_canvas(&tnui_back_canvas);
            GfxTlucRect(r == 4 || r == 5 ? hud_tluc_389DC8 : hud_tluc_389DD0, w->x0, w->y0, w->x0 + tw, w->y0 + th);
            gr_set_canvas(&w->canvas);
            return;
        }
    }
#endif
    if (r == 4 || r == 5)
        GfxTlucRect(hud_tluc_389DC8, 0, 0, w->canvas.bm.w, w->canvas.bm.h);
    else
        GfxTlucRect(hud_tluc_389DD0, 0, 0, w->canvas.bm.w, w->canvas.bm.h);
}

void hudcb_265158(void)
{
    gr_set_canvas(&hud_w_389DF0->canvas);
    hudcb_26516C(hud_w_389DF0);
}

void hudcb_26516C(HudWidget *w)
{
    char buf[0x20];
    gr_clear0();
    lg_sprintf(buf, "%3d", *(short *)PBA_ENT(0xCC) - 0x111);
    GfxDrawText(0x1F5, buf, 0, 0);
}

/* ------------------------------------------------------------------ clock */

void hudcb_2651C8(ushort secs)
{
    hud_38BA46 = secs;
    game_time = (ulong)secs * 1000;
}

void hudcb_2651EC(HudWidget *w)
{
    ulong s = game_time / 1000;
    if (hud_38BA46 != s) {
        hud_38BA46 = (ushort)s;
        DIRTY(w);
    }
}

void hudcb_26521C(HudWidget *w)
{
    char buf[12];
    gr_clear0();
    fileutil_2CD6CC(buf);
    GfxDrawText(0x1F6, buf, 2, 0);
}

void hudcb_265250(void)
{
    char buf[12];
    gr_set_canvas(&hud_w_389DE0->canvas);
    gr_clear0();
    fileutil_2CD6CC(buf);
    GfxDrawText(0x1F6, buf, 2, 0);
}

/* ------------------------------------------------------------------ bars */

void hudcb_265290(void)
{
    hud_38BA44 = hud_save_bm[0].w;
}

void hudcb_2652A0(HudWidget *w)
{
    uchar *sys = *(uchar **)PBA_ENT(0x9C);
    long v = hud_save_bm[0].w * (long)*(ushort *)(sys + 3) / (long)*(ushort *)(sys + 1);
    if (v != hud_38BA44) {
        hud_38BA44 = (short)v;
        DIRTY(w);
    }
}

void hudcb_2652F4(HudWidget *w)
{
    long h;
    draw_bm(&hud_save_bm[0], 0, 0);
    grd_canvas->gc.fcolor = 0;
    h = (game_35A540 >> 1) ? hud_save_bm[0].h * 2 : hud_save_bm[0].h;
    WATCOM_CALL(grd_canvas_table[DRAW_RECT], hud_38BA44, 0, hud_save_bm[0].w, (short)h);
    draw_frame(0x23D000E, 2, 0);
#ifndef TN_WATCOM_ABI
    hudcb_bar_tail(0, &hud_save_bm[0], hud_38BA44);
#endif
}

void hudcb_265398(void)
{
    hud_38BA34 = (fix)(((long long)hud_save_bm[7].w << 32) / *(long *)PBA_ENT(0x8A));
    hud_38BA42 = hud_save_bm[7].w;
}

void hudcb_2653D8(HudWidget *w)
{
    long long p = (long long)*(long *)PBA_SLOT(0x86) * hud_38BA34;
    hud_38BA40 = (short)((long)(p >> 16) >> 16);
#ifndef TN_WATCOM_ABI
    /* G43: the scale (width / maximum, a 16.16 fraction cut short) times a full charge came to one
     * column less than the width, a dark line in the full bar. Worked out as the health bar is. */
    if (*(long *)PBA_ENT(0x8A) > 0)
        hud_38BA40 = (short)((long long)hud_save_bm[7].w * *(long *)PBA_SLOT(0x86) / *(long *)PBA_ENT(0x8A));
#endif
    if (hud_38BA42 != hud_38BA40)
        DIRTY(w);
}

void hudcb_26541C(HudWidget *w)
{
    long h;
    draw_bm_u(&hud_save_bm[7]);
    grd_canvas->gc.fcolor = 0;
    h = (game_35A540 >> 1) ? hud_save_bm[7].h * 2 : hud_save_bm[7].h;
    WATCOM_CALL(grd_canvas_table[DRAW_RECT], hud_38BA40, 0, hud_save_bm[7].w, (short)h);
    draw_frame(0x23D000F, 3, 0);
#ifndef TN_WATCOM_ABI
    hudcb_bar_tail(1, &hud_save_bm[7], hud_38BA40);
#endif
    hud_38BA42 = hud_38BA40;
}

/* ------------------------------------------------------------------ view arrows */

void hudcb_2654D0(HudWidget *w)
{
    if (!(hud_35AEF0 & 1))
        DIRTY(w);
}

void hudcb_2654E0(HudWidget *w)
{
    if (!(hud_35AEF0 & 1))
        DIRTY(w);
}

void hudcb_2654F0(HudWidget *w, HudEvent *ev)
{
    uchar v;
    if (ev->type != 1)
        return;
    v = w->id;
    if (v == 0) {
        hudview_26C360(0, -1, 0);
        return;
    }
    if (v == 1) {
        hudcb_264A84(1);
        return;
    }
    if (hudview_26C360(v, -1, 0))
        snd_click(0x7EA);
}

void hudcb_265560(HudWidget *w, HudEvent *ev)
{
    uchar v;
    if (ev->type != 1)
        return;
    v = w->id;
    if (v == 0) {
        hudview_26C360(0, 1, 0);
        return;
    }
    if (v == 1) {
        hudcb_264A84(0);
        return;
    }
    if (hudview_26C360(v, 1, 0))
        snd_click(0x7EA);
}

static void arrow_draw(HudWidget *w, Ref ref)
{
    if (hud_35AEF0 & 1)
        return;
    gr_set_canvas(&tnui_back_canvas);
    grd_canvas->gc.fcolor = 0;
    WATCOM_CALL(grd_canvas_table[DRAW_BOX], (short)(w->x0 - 1), (short)(w->y0 - 1),
                (short)(w->x1 + 1), (short)(w->y1 + 1));
    gr_set_canvas(&w->canvas);
    draw_frame(ref, 0, 0);
}

void hudcb_2655CC(HudWidget *w)
{
    arrow_draw(w, 0x2F00023);
}

void hudcb_265664(HudWidget *w)
{
    arrow_draw(w, 0x2F00024);
}
