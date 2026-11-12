/* hview3d.c -- Terra Nova module M028 (0x272980-0x2759EF; r:\prj\tn\v109\tn\src\tn\hview3d.c):
 * the 3D and map HUD views.  Decompiled from __FF.EXE v1.09.
 *
 *   0x272980 hview3d_272980 squad map (mode 9?) on / off   0x272A48 hview3d_272A48 its event
 *   0x272CFC hview3d_272CFC its draw                       0x272DBC hview3d_272DBC map 2 on / off
 *   0x272E84 hview3d_272E84 its draw                       0x272F10 hview3d_272F10 target map on / off
 *   0x273020 hview3d_273020 target map event               0x273500 hview3d_273500 its draw
 *   (the remaining functions are listed where they are defined)
 *
 * The map views keep a map display object (G8c mapdisp, HudView.f10): +0 fix x, +4 fix y,
 * +8 short zoom, +0xA fix scale (world units per pixel), +0x26 flags (1 dragged: do not follow,
 * 4), +0x27 layers.
 *
 * Quirks kept:
 *  - hview3d_272A48 passes the absolute event y to mfd_26D1BC for a press but the y relative to
 *    the widget for button 4.
 *  - hview3d_272F10 compares the mode with 0x18 / 0x19 and uses 4 either way.
 *  - hview3d_273020 (mode 0x16 pick) looks up the member whose entity is hud_389BC4 and reads
 *    record [-1] when there is none.
 *  - hview3d_272E84 adds widget 1's position to hud_390300 / hud_390304 as dwords.
 */
#include "g8_b.h"
#include "g8_c.h"     /* added at the G5-G8 merge */
#include "g9_a.h"     /* added at the G5-G8 merge */
#include "g9_c.h"     /* added at the G9/G10 merge */

#define HENT(i) (&game_entities[(short)(i)])
#define REC(i) ((uchar *)&game_391E50[i])
#define RS16(r, o) (*(short *)((r) + (o)))
#define MAP_X(m) (*(fix *)(m))
#define MAP_Y(m) (*(fix *)((m) + 4))
#define MAP_ZOOM(m) (*(short *)((m) + 8))
#define MAP_SCALE(m) (*(fix *)((m) + 0xA))
#define VIEW_MAP(v) (hud_views[v].f10)
#define EV_PT(ev) (*(LGPoint *)((uchar *)(ev) + 8))
#define MOUSE (*(LGPoint *)tnm_state_ptr)
#define MODEMASK_B2(v) (((uchar *)&hud_views[v].mode_mask)[2])
#define HIRES ((game_35A540 >> 1) != 0)

typedef void (*CanvasFn)();

#ifndef TN_WATCOM_ABI
void port_s3d_view_drawn(int slot);     /* port/video/sharp3d.c: the side screens' views drawn sharp */
void port_s3d_render(int slot, void *view, void *cam, int mirror);
void port_s3d_map(int slot, void *map, long b);     /* ... and their maps */
extern volatile int port_wide_map, port_wide_map_x, port_wide_map_y;
int port_map_zoom_ok(const void *map, long zoom);
extern int port_smooth_fps;             /* port/ui/options.c: 60 frames a second */
extern int port_aim_box;                /* port/ui/options.c: "Auto-aim" box (0: Off) */
void port_hud_aim_sign(void);           /* hudmain.c: "Auto-aim highlight" on the crosshair */
extern int port_map_cone;               /* port/video/mapcone.c: the maps' view cone */
/* a map drawn between the game's own drawings (hview3d_273CAC): the random stream its contacts'
 * flicker takes (shared with the game's objects) and the nav point's place the map keeps for the
 * hint lines (mapdisp_277424) are put back after it, so the game's values stay as the original's */
static uchar hv_extra[3];
static ulong hv_rnd;
static long hv_nav[2];
static void extra_begin(long view)
{
    if (view >= 0 && view < 3 && hv_extra[view]) {
        hv_rnd = game_rnd_35B704.curr;
        hv_nav[0] = hud_390300;
        hv_nav[1] = hud_390304;
    }
}
static void extra_end(long view)
{
    if (view >= 0 && view < 3 && hv_extra[view]) {
        game_rnd_35B704.curr = hv_rnd;
        hud_390300 = hv_nav[0];
        hud_390304 = hv_nav[1];
        hv_extra[view] = 0;
    }
}
#endif

static grs_bitmap *bm_get(Ref r)
{
    grs_bitmap *bm = RefGet(r);
    bm->bits = (uchar *)bm + 0x1C;
    return bm;
}

#define UBITMAP(bm, x, y) \
    WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * (bm)->type], (long)(bm), \
                (long)(x), (long)(y))
#define BITMAP(bm, x, y) \
    WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * (bm)->type], (long)(bm), \
                (long)(x), (long)(y))

static void map_new(HudWidget *w, short ent, long scale)
{
    HudView *v = &hud_views[w->id];
    v->f10 = mapdisp_276ACC(&w->canvas, HENT(ent)->x, HENT(ent)->y, scale);
    v->f10[0x26] &= 0xFE;
    v->f10[0x26] |= 4;
    v->f10[0x27] = hud_map_layers;
}

static void map_free(HudWidget *w)
{
    HudView *v = &hud_views[w->id];
    if (tnm_frozen) {
        hud_drag_widget = 0;
        MouseIntDrawOn();
        MouseUnfreeze();
        MouseSetCursor(0x1F90016);
    }
    MouseSetCursor(0x1F90016);
    mapdisp_276BB4(v->f10);
    v->f10 = 0;
}

/* member index of entity hud_389BC4, -1 if none */
static schar member_of_pick(void)
{
    schar k = 0;
    while (k < game_391E4C) {
        if (hud_389BC4 == RS16(REC(k), 2))
            break;
        k++;
    }
    if (k == game_391E4C)
        k = -1;
    return k;
}

/* send the squad of brain to map point (rx, ry) of map m */
static LGPoint map_point(uchar *m, short rx, short ry)
{
    LGPoint p;
    p.x = (short)((rx * MAP_SCALE(m) + MAP_X(m)) >> 16);
    p.y = (short)((((long)ry >> hud_35A544) * MAP_SCALE(m) + MAP_Y(m)) >> 16);
    return p;
}

static long pt_long(LGPoint p)
{
    return (ushort)p.x | (long)p.y << 16;
}

void hview3d_272980(HudWidget *w, uchar on)
{
    HudView *v = &hud_views[w->id];
    grs_bitmap *bm;
    short e;

    if (!on) {
        mapdisp_276BB4(v->f10);
        v->f10 = 0;
        return;
    }
    mfd_38FC81 = RS16(REC(mfd_3599D4), 2);
    e = mfd_38FC81;
    v->f10 = mapdisp_276ACC(&w->canvas, HENT(e)->x, HENT(e)->y, hv_35AEEC);
    v->f10[0x26] &= 0xFE;
    v->f10[0x26] |= 4;
    v->f10[0x27] = hud_map_layers;
    bm = RefGet(0x23D0002);
    hv_38FFF8 = w->canvas.bm.w - bm->w;
    hv_38FFFA = bm->h;
}

void hview3d_272A48(HudWidget *w, HudEvent *ev)
{
    uchar *m = VIEW_MAP(w->id);
    short rx = EV_PT(ev).x - w->x0, ry = EV_PT(ev).y - w->y0;
    short e;
    uchar brain;
    LGPoint p;
#ifndef TN_WATCOM_ABI
    /* widescreen, the cockpit: the pointer is on the wide map beside the game's (sharp3d.h): the
     * map's place there, not its zoom buttons at the edge */
    int wide = port_wide_map == 1 + w->id;
    if (wide) {
        rx = (short)(port_wide_map_x - w->x0);
        ry = (short)(port_wide_map_y - w->y0);
    }
#define ZOOM_AT(x) ((x) >= hv_38FFF8 && !wide)
#else
#define ZOOM_AT(x) ((x) >= hv_38FFF8)
#endif

    if (ev->type == 1) {
        if (mfd_26D1BC(w, EV_PT(ev).y)) {
            if (mfd_26D300(1)) {
                m[0x26] &= 0xFE;
                mapdisp_276C4C(m, HENT(mfd_38FC81)->x, HENT(mfd_38FC81)->y);
            }
            return;
        }
        if (ZOOM_AT(rx)) {
            if (ry < hv_38FFFA) {
                if (MAP_ZOOM(m) < 7) {
                    mapdisp_276BC8(m, MAP_ZOOM(m) + 1);
                    hv_35AEEC = MAP_ZOOM(m);
                }
                return;
            }
            e = RS16(REC(mfd_3599D4), 2);
            m[0x26] &= 0xFE;
            mapdisp_276C4C(m, HENT(e)->x, HENT(e)->y);
            return;
        }
        brain = HENT(RS16(REC(mfd_3599D4), 2))->f21;
        p = map_point(m, rx, ry);
#ifndef TN_WATCOM_ABI
        if (wide) {                     /* (the wide map reaches past the land's edges) */
            p.x = p.x < 0 ? 0 : p.x > 0x1FF ? 0x1FF : p.x;
            p.y = p.y < 0 ? 0 : p.y > 0x1FF ? 0x1FF : p.y;
        }
#endif
        ailow_253334(brain);
        mfddata_266618(game_brains[brain].f006, pt_long(p), brain);
        SndPlay(0x7E2, 0, 0x40, 1, -1, 0x40, 0);
        return;
    }
    if (ev->type == 4) {
        if (mfd_26D1BC(w, ry)) {
            if (mfd_26D300(-1)) {
                m[0x26] &= 0xFE;
                mapdisp_276C4C(m, HENT(mfd_38FC81)->x, HENT(mfd_38FC81)->y);
            }
            return;
        }
        if (ZOOM_AT(rx)) {
            if (ry < hv_38FFFA && MAP_ZOOM(m) > 0
#ifndef TN_WATCOM_ABI
                && port_map_zoom_ok(m, MAP_ZOOM(m) - 1)  /* widescreen (mapdisp.c) */
#endif
                ) {
                mapdisp_276BC8(m, MAP_ZOOM(m) - 1);
                hv_35AEEC = MAP_ZOOM(m);
            }
            return;
        }
    }
    hview3d_273B04(w, ev);
#undef ZOOM_AT
}

void hview3d_272CFC(long view)
{
    uchar *m = VIEW_MAP(view);
    grs_bitmap *bm;

#ifndef TN_WATCOM_ABI
    extra_begin(view);
#endif
    if (!(m[0x26] & 1))
        mapdisp_276C6C(m, HENT(mfd_38FC81)->x, HENT(mfd_38FC81)->y);
    mapdisp_276EF8(m, mfd_38FC81);
#ifndef TN_WATCOM_ABI
    port_s3d_map(1 + view, m, mfd_38FC81);
#endif
    bm = bm_get(0x23D0002);
    UBITMAP(bm, hv_38FFF8, 0);
    bm = bm_get(0x23D0003);
    UBITMAP(bm, hv_38FFF8, hv_38FFFA);
    mfd_26D1F4();
#ifndef TN_WATCOM_ABI
    extra_end(view);
#endif
}

void hview3d_272DBC(HudWidget *w, uchar on)
{
    if (on) {
        map_new(w, hud_38FFFC, hud_map_zoom);   /* 0x35AEEE, not the squad map's 0x35AEEC */
        hudcb_26493C(1);
    } else
        map_free(w);
}

void hview3d_272E84(long view)
{
    uchar *m = VIEW_MAP(view);
    HudWidget *w1;

#ifndef TN_WATCOM_ABI
    extra_begin(view);
#endif
    if (!(m[0x26] & 1))
        mapdisp_276C6C(m, HENT(hud_38FFFC)->x, HENT(hud_38FFFC)->y);
    mapdisp_276EF8(m, hud_38FFFC);
#ifndef TN_WATCOM_ABI
    port_s3d_map(1 + view, m, hud_38FFFC);
#endif
    w1 = HUD_VIEW_W(1);
    hud_390300 += w1->x0;
    hud_390304 += w1->y0;
    if (hud_389BC2)
        hudview_26C4A8();
#ifndef TN_WATCOM_ABI
    extra_end(view);
#endif
}

void hview3d_272F10(HudWidget *w, uchar on)
{
    uchar mode = hud_views[w->id].mode, scale;
    short e;

    scale = (mode == 0x14 || mode == 0x16) ? 7 : 4;
    if (!on) {
        map_free(w);
        return;
    }
    if (hud_views[w->id].mode == 0x19)
        hview3d_274394();
    mode = hud_views[w->id].mode;
    e = (mode == 0x16 || mode == 0x18 || mode == 0x19) ? game_player_entity : game_396868;
    map_new(w, e, scale);
}

void hview3d_273020(HudWidget *w, HudEvent *ev)
{
    uchar id = w->id;
    uchar *m = VIEW_MAP(id);
    uchar mode;

    switch (ev->type) {
    case 1:
        mode = hud_views[id].mode;
        if (mode == 0x18) {
            short rx = MOUSE.x - w->x0, ry = MOUSE.y - w->y0;
            ulong i;
            LGRect r;
            schar k;
            for (i = 0; i < 4; i++) {
                r = *(LGRect *)RefGet(hv_35B2C0[i]);
                if (ry < r.ul.y || ry >= r.lr.y || rx < r.ul.x || rx >= r.lr.x)
                    continue;
                if (i < 3) {
                    simtype_27B144((schar)i);
                    k = member_of_pick();
                    if (k != -1)
                        RadioSpeak(k, 2);
                }
                SndPlay(0x7E2, 0, 0x40, 1, -1, 0x40, 0);
                cmd_25F044();
            }
            return;
        }
        if (mode == 0x14 || mode == 0x16) {
            short e = hview3d_2739F4(w, hud_views[id].mode == 0x16 ? 0x10000 : 0x20000);
            if (hud_views[w->id].mode == 0x16) {
                schar k = member_of_pick();
                simtype_27ADAC(w, e, (schar)REC(k)[7]);
                hudview_26C224(w->id, 0x17);
                return;
            }
            if (e != game_396868) {
                air_target[0] = e;
                ailow_24BB1C(HENT(game_396868)->f21, 0);
            }
            MODEMASK_B2(w->id) &= 0xEF;
            hudview_26C224(w->id, game_3968B5 ? 0x13 : 0x11);
            return;
        } else {
            short hgt = w->y1 - w->y0;
            short rx = MOUSE.x - w->x0, ry = MOUSE.y - w->y0;
            short sw, sh;
            grs_font *f = ResLock(0x1F7);
            gr_font_string_size(f, RefGet(0x16E0074), &sw, &sh);
            ResUnlock(0x1F7);
            if (rx < 0 || rx >= sw || ry < hgt - sh || ry > hgt) {
                uchar brain = HENT(game_396868)->f21;
                LGPoint p;
                SndPlay(0x7E2, 0, 0x40, 1, -1, 0x40, 0);
                p = map_point(m, rx, ry);
                ailow_24BB1C(brain, 1);
                mfddata_266618(game_brains[brain].f006, pt_long(p), brain);
            }
            hudview_26C224(w->id, game_3968B5 ? 0x13 : 0x11);
            MODEMASK_B2(w->id) &= 0xFB;
        }
        return;
    case 4:
        if ((((uchar *)tn_replay_state_ptr)[5] & 4) || (((uchar *)tn_replay_state_ptr)[6] & 0x40)) {
            short e = hview3d_2739F4(w, 0);
            if (e)
                helm_262D4C(e);
            return;
        }
        hud_drag_widget = w;
        hv_38FFFE = EV_PT(ev);
        MouseIntDrawOff();
        MouseFreeze();
        MouseSetCursor(0x1F90018);
        m[0x26] |= 1;
        if (w->id == 1)
            hudcb_26493C(0);
        return;
    case 8:
        if (w == hud_drag_widget) {
            hud_drag_widget = 0;
            MouseIntDrawOn();
            MouseUnfreeze();
            MouseSetCursor(0x1F90016);
        }
        return;
    case 0x40:
        if (w == hud_drag_widget) {
            LGPoint d;
            d.x = EV_PT(ev).x - hv_38FFFE.x;
            d.y = EV_PT(ev).y - hv_38FFFE.y;
            mapdisp_276CDC(m, d);
            MouseMoveTo(hv_38FFFE);
        }
        return;
    }
}

#ifndef TN_WATCOM_ABI
static void target_map_draw(long view);
void hview3d_273500(long view)
{
    extra_begin(view);
    target_map_draw(view);
    extra_end(view);
}
static void target_map_draw(long view)
#else
void hview3d_273500(long view)
#endif
{
    uchar *m = VIEW_MAP(view);
    uchar mode = hud_views[view].mode;
    char buf[0x40];
    grs_bitmap *bm;
    LGRect r1, r2;
    short e;

    if (!(m[0x26] & 1) && mode == 0x16)
        mapdisp_276C6C(m, HENT(game_396868)->x, HENT(game_396868)->y);
    mode = hud_views[view].mode;
    mapdisp_276EF8(m, (mode == 0x16 || mode == 0x19) ? game_player_entity : game_396868);
#ifndef TN_WATCOM_ABI
    port_s3d_map(1 + view, m, (mode == 0x16 || mode == 0x19) ? game_player_entity : game_396868);
#endif
    mode = hud_views[view].mode;
    if (mode == 0x14 || mode == 0x16) {
        HudWidget *w = HUD_VIEW_W(view);
        if (MOUSE.y >= w->y0 && MOUSE.y < w->y1 && MOUSE.x >= w->x0 && MOUSE.x < w->x1) {
            Ref ref;
            grd_canvas->gc.fcolor = 0x4E;
            e = hview3d_2739F4(w, hud_views[view].mode == 0x16 ? 0x10000 : 0x20000);
            if (e && e != game_396868)
                ref = (HENT(e)->cls + 0x17D) << 16 | HENT(e)->type;
            else
                ref = 0x16E0079;
            g1c_strcpy(buf, RefGet(ref));
            GfxDrawText(0x1F7, buf, 10, 0x28);
        }
        grd_canvas->gc.fcolor = 0x44;
        GfxDrawText(0x1F7, RefGet(0x16E0078), 10, 5);
        return;
    }
    if (mode == 0x18) {
        uchar f = simtype_27B508(RefGet(0x242000A));
        if (f & 1) {
            short y;
            bm = bm_get(0x2410002);
            y = ((short *)RefGet(hv_35B2C0[0]))[1];
            BITMAP(bm, ((short *)RefGet(hv_35B2C0[0]))[0], y);
        }
        if (f & 2) {
            short y;
            bm = bm_get(0x2410003);
            y = ((short *)RefGet(hv_35B2C0[1]))[1];
            BITMAP(bm, (short)(((short *)RefGet(hv_35B2C0[1]))[0] - 1), y);
        }
        if (f & 4) {
            short y;
            bm = bm_get(0x2410004);
            y = ((short *)RefGet(hv_35B2C0[2]))[1];
            BITMAP(bm, ((short *)RefGet(hv_35B2C0[2]))[0], y);
        }
        grd_canvas->gc.fcolor = 0x44;
        GfxDrawText(0x1F7, RefGet(0x243000D), 10, 5);
        grd_canvas->gc.fcolor = 0x1D;
        r1 = *(LGRect *)RefGet(0x242000D);
        GfxDrawText(0x1F7, RefGet(0x243000F), r1.ul.x + 1, r1.ul.y + 1);
        return;
    }
    if (mode == 0x19) {
        r1 = *(LGRect *)RefGet(0x242000D);
        r2 = *(LGRect *)RefGet(0x242000C);
        r2.lr.x = r1.lr.x;
        grd_canvas->gc.fcolor = 0;
        WATCOM_CALL((CanvasFn)grd_canvas_table[16], (long)r2.ul.x, (long)r2.ul.y, (long)r1.lr.x,
                    (long)r2.lr.y);
        grd_canvas->gc.fcolor = 0x1D;
        GfxDrawText(0x1F7, RefGet(0x243000F), r1.ul.x + 1, r1.ul.y + 1);
        grd_canvas->gc.fcolor = 0x36;
        GfxDrawText(0x1F7, RefGet(0x16F0081), r2.ul.x + 1, r2.ul.y + 1);
        hview3d_274184(m);
        return;
    }
    if (mode == 0x12) {
        HudWidget **ws = &hud_w_389E44, *w;
        char *s;
        short hgt, sw, sh;
        grs_font *f;
        __asm__("" : "+r"(ws));
        w = ws[view];
        s = RefLock(0x16E0074);
        hgt = w->y1 - w->y0;
        f = ResLock(0x1F7);
        grd_canvas->gc.font = f;
        gr_font_string_size(f, s, &sw, &sh);
        grd_canvas->gc.fcolor = 0x26;
        WATCOM_CALL((CanvasFn)grd_canvas_table[16], 0L, (long)(short)(hgt - sh - 1), (long)sw,
                    (long)hgt);
        grd_canvas->gc.fcolor = 0x1D;
        WATCOM_CALL((CanvasFn)grd_canvas_table[315], (long)grd_canvas->gc.font, (long)s, 0L,
                    (long)(short)(hgt - sh - 1));
        s = RefGet(0x16F0042);
        WATCOM_CALL((CanvasFn)grd_canvas_table[315], (long)grd_canvas->gc.font, (long)s, 0L, 0L);
        ResUnlock(0x16E);
        ResUnlock(0x1F7);
    }
}

static short pick(HudWidget *w, fix dist, AihelpIter it)
{
    fix x, y;
    short e;
    simtype_27B240(w, &x, &y);
    e = aihelp_2D30E0(x, y, it);
    if (dist && fix_fast_pyth_dist(HENT(e)->x - x, HENT(e)->y - y) >= dist)
        e = 0;
    return e;
}

short hview3d_2739F4(HudWidget *w, fix dist)
{
    return pick(w, dist, (AihelpIter)WATCOM_FN(aihelp_2D35FC));
}

short hview3d_273A5C(HudWidget *w, fix dist)
{
    return pick(w, dist, (AihelpIter)WATCOM_FN(aihelp_2D35A0));
}

void hview3d_273AC4(HudWidget *w)
{
    short e = hview3d_2739F4(w, 0);
    if (e)
        helm_262D4C(e);
}

void hview3d_273ADC(HudWidget *w)
{
    short e;
    if (!(hud_views[w->id].flags & 4))
        return;
    e = hview3d_2739F4(w, 0);
    if (e)
        helm_262D4C(e);
}

void hview3d_273B04(HudWidget *w, HudEvent *ev)
{
    long v = (schar)w->id;
    uchar *m = hud_views[v].f10;
    LGPoint rel;

    switch (ev->type) {
    case 1:
        rel.x = EV_PT(ev).x - w->x0;
        rel.y = EV_PT(ev).y - w->y0;
        if (hud_389BC2 & 0x3F)
            hview3d_273D40(m, rel);
        else if (hud_views[v].mode == 9) {
            ffpipe_38FFF0 = rel.x * MAP_SCALE(m) + MAP_X(m);
            ffpipe_38FFF4 = ((long)rel.y >> hud_35A544) * MAP_SCALE(m) + MAP_Y(m);
        }
        SndPlay(0x7E2, 0, 0x40, 1, -1, 0x40, 0);
        return;
    case 4:
        if (game_359708 == 0x2E)
            return;
        hud_drag_widget = w;
        hv_390002 = EV_PT(ev);
        MouseIntDrawOff();
        MouseFreeze();
        MouseSetCursor(0x1F90018);
        m[0x26] |= 1;
        if (w->id == 1)
            hudcb_26493C(0);
        return;
    case 8:
        if (w == hud_drag_widget) {
            hud_drag_widget = 0;
            MouseIntDrawOn();
            MouseUnfreeze();
            MouseSetCursor(0x1F90016);
        }
        return;
    case 0x40:
        if (w == hud_drag_widget) {
            LGPoint d;
            d.x = EV_PT(ev).x - hv_390002.x;
            d.y = EV_PT(ev).y - hv_390002.y;
            mapdisp_276CDC(m, d);
            MouseMoveTo(hv_390002);
        }
        return;
    }
}

void hview3d_273CAC(long view)
{
    if (game_time >= hud_view_38FC70[view] || hud_389BC2 || hud_drag_widget == HUD_VIEW_W(view)) {
        HUD_VIEW_W(view)->flags |= 0x20;
        hud_view_38FC70[view] = game_time + 0x4B;
    }
#ifndef TN_WATCOM_ABI
    /* Freefall, at Frame rate 60 fps or with the map's view cone: the map each frame, as the goggles
     * draw it (the cockpit's every 75 ms) */
    else if ((port_smooth_fps || port_map_cone) && view >= 0 && view < 3) {
        HUD_VIEW_W(view)->flags |= 0x20;
        hv_extra[view] = 1;
    }
#endif
}

void hview3d_273CFC(uchar *map, LGPoint pt)
{
    ffpipe_38FFF0 = MAP_X(map) + MAP_SCALE(map) * pt.x;
    ffpipe_38FFF4 = MAP_Y(map) + ((long)pt.y >> hud_35A544) * MAP_SCALE(map);
}

void hview3d_273D40(uchar *map, LGPoint pt)
{
    fix wx = pt.x * MAP_SCALE(map) + MAP_X(map);
    fix wy = ((long)pt.y >> hud_35A544) * MAP_SCALE(map) + MAP_Y(map);
    ushort k = hud_389BC2 & 0x3F;
    short *pk = &hud_389BC8, x, y, e;

    __asm__("" : "+r"(pk));
    if (k < 1)
        return;
    x = (wx + 0x8000) >> 16;
    y = (wy + 0x8000) >> 16;
    if (k == 1) {
        pk[0] = x;
        pk[1] = y;
    } else if (k == 2) {
        e = aihelp_2D30E0(wx, wy, (AihelpIter)WATCOM_FN(aihelp_2D35A0));
        pk[0] = x;
        pk[1] = y;
        if (e > 0)
            pk[0] = e;
    } else
        return;
    cmd_2601D8();
    cmd_25F0AC(0x2EE);
}

void hview3d_273E08(HudWidget *w, HudEvent *ev)
{
    uchar *m = VIEW_MAP(w->id);
    LGRect r;
    short rx, ry;
    uchar done, brain;

    switch (ev->type) {
    case 1:
        done = 0;
        rx = EV_PT(ev).x - w->x0;
        ry = EV_PT(ev).y - w->y0;
        r = *(LGRect *)RefGet(0x242000D);
        if (ry >= r.ul.y && ry < r.lr.y && rx >= r.ul.x && rx < r.lr.x) {
            schar k = member_of_pick();
            brain = HENT(RS16(REC(k), 2))->f21;
            if ((hud_389BC2 >> 8) & 2)
                cmd_25FB30(brain);
            if ((hud_389BC2 >> 8) & 4) {
                ailow_253334(brain);
                hv_389BC6 = game_brains[brain].f006;
            }
            hview3d_27421C(brain);
            done = 1;
            cmd_25F044();
        }
        r = *(LGRect *)RefGet(0x242000C);
        if (ry >= r.ul.y && ry < r.lr.y && rx >= r.ul.x && rx < r.lr.x) {
            hview3d_27414C();
            done = 1;
        }
        SndPlay(0x7E2, 0, 0x40, 1, -1, 0x40, 0);
        if (!done)
            hview3d_2740C8(w);
        return;
    case 4:
        hv_390006 = EV_PT(ev);
        hud_drag_widget = w;
        MouseIntDrawOff();
        MouseFreeze();
        MouseSetCursor(0x1F90018);
        m[0x26] |= 1;
        if (w->id == 1)
            hudcb_26493C(0);
        return;
    case 8:
        if (w == hud_drag_widget) {
            hud_drag_widget = 0;
            MouseIntDrawOn();
            MouseUnfreeze();
            MouseSetCursor(0x1F90016);
        }
        return;
    case 0x40:
        if (w == hud_drag_widget) {
            LGPoint d;
            d.x = EV_PT(ev).x - hv_390006.x;
            d.y = EV_PT(ev).y - hv_390006.y;
            mapdisp_276CDC(m, d);
            MouseMoveTo(hv_390006);
        }
        return;
    }
}

void hview3d_2740C8(HudWidget *w)
{
    long i = 0;
    HvWaypoint *p;

    if (hv_38FE90[0].region != 0xFF)
        do
            i++;
        while (i < 15 && hv_38FE90[i].region != 0xFF);
    if (i >= 15)
        return;
    p = &hv_38FE90[i];
    simtype_27B240(w, &p->x, &p->y);
    p->z = map_2949C4(p->x, p->y);
    p->region = misobj_2C9E60(p->x >> 16, p->y >> 16);
}

void hview3d_27414C(void)
{
    long i;
    for (i = 15; i >= 0; i--) {
        if (hv_38FE90[i].region != 0xFF) {
            misobj_2C9EE8(hv_38FE90[i].region);
            hv_38FE90[i].region = 0xFF;
            return;
        }
    }
}

void hview3d_274184(uchar *map)
{
    HvWaypoint *p;
    short x, y;

    if (hv_38FE90[0].region == 0xFF)
        return;
    for (p = hv_38FE90;; p++) {
        x = fix_mul(p->x - MAP_X(map), *(fix *)(map + 0xE)) >> 16;
        y = fix_mul((p->y - MAP_Y(map)) << hud_35A544, *(fix *)(map + 0xE)) >> 16;
        grd_canvas->gc.fcolor = 0x50;
        WATCOM_CALL((CanvasFn)grd_canvas_table[131], (long)x, (long)y, 2L);
        if (p[1].region == 0xFF)
            break;
    }
}

void hview3d_27421C(long brain)
{
    uchar buf[0x10];
    long n = 0, i;
    Msg *m;
    ulong from, to;

    if (hv_38FE90[0].region == 0xFF)
        do
            n++;
        while (n < 16 && hv_38FE90[n].region == 0xFF);
    if (n == 16)
        return;
    m = MsgAlloc();
    if (!m)
        return;
    for (i = 0; i < 16; i++)
        buf[i] = (uchar)hv_38FE90[i].region;
    to = *(ulong *)((uchar *)&game_squads[game_brains[(uchar)brain].f006] + 3);
    from = *(ulong *)((uchar *)&game_squads[game_brains[HENT(RS16(REC(0), 2))->f21].f006] + 3);
    aiacc_25B0F0(m, from, to, buf);
    m->flag = 6;
    MsgSend(m);
}

static void wp_clear(void)
{
    long i;
    for (i = 0; i < 16; i++)
        hv_38FE90[i].region = 0xFF;
}

void hview3d_27434C(void)
{
    wp_clear();
}

void hview3d_274394(void)
{
    wp_clear();
}

void hview3d_2743DC(void)
{
    long i;
    for (i = 0; i < 16; i++) {
        if (hv_38FE90[i].region != 0xFF) {
            misobj_2C9EE8(hv_38FE90[i].region);
            hv_38FE90[i].region = 0xFF;
        }
    }
}

void hview3d_274410(void)
{
    long v;
    for (v = 0; v < 3; v++) {
        FfCamera *cam = (FfCamera *)ff_cams_390010[v];
        HudWidget *w;
        *(long *)&cam->g_20[0] = 0x28;
        *(long *)&cam->g_20[4] = *(long *)(game_camera + 0x24);
        *(long *)&cam->sky = *(long *)(game_camera + 0x28);
        cam->canvas = &HUD_VIEW_W(v)->canvas;
        CameraInitZoom((uchar *)cam, hv_428B83, 0);
        ffpipe_29E160(cam);
        cam->hview_ok = 0;
        w = HUD_VIEW_W(v);
        FfCameraSetup(cam, w->x1 - w->x0, w->y1 - w->y0, 0x10000);
        hv_390247[v] = (uchar)weather_2AFF54(cam, 0);
        weather_2AFD4C((schar)(hud_44244B - 1), hud_44244C);
    }
}

void hview3d_2744D0(void)
{
    long c;
    weather_2AFFFC(hv_390247[0]);
    weather_2AFFFC(hv_390247[1]);
    weather_2AFFFC(hv_390247[2]);
    for (c = 0; c < 3; c++)
        hudview_26BFC4(c, c, 0);
}

void hview3d_274510(long a)
{
    long i;
    for (i = 0; i < 3; i++)
        *(uchar **)&ff_cams_390010[i][0x24] = (uchar)a ? planet_tab3b : 0;
}

void hview3d_274538(long view, long ent, long kind)
{
    void *c = (short)kind ? (void *)&cursctrl_other : (void *)damage_27C880((short)ent);
    damage_27E1E0((short *)hud_views[view].f14, (short)ent, 0, (ushort)kind, (long)c);
}

void hview3d_274570(HudWidget *w, long pt, long cam)
{
    uchar *pba = (uchar *)&game_pba[game_player_slot];
    uchar *ws = pba + 0x45 + 15 * (schar)pba[0xCE];
    short t;
    uchar k;

    if (weapon_ready(ws, pba + 0x86))
        return;
    hview3d_275260(w);
    t = (hud_35A944 && targ_current) ? targ_current : 0;
#ifndef TN_WATCOM_ABI
    if (!port_aim_box)                  /* "Auto-aim" Off: where the pointer is */
        t = 0;
#endif
    pba = (uchar *)&game_pba[game_player_slot];
    k = *(uchar *)&ord_table[pba[0x45 + 15 * (schar)pba[0xCE]]];
    if (k == 0)
        pba[0xCF] |= 2;
    else if (k > 2)
        return;
    reticle_target(t, pt, ff_cams_390010[cam], &ff_view);
}

/* index of the button of list l under x, -1 if none */
static long button_at(HvButton *l, long x)
{
    long i;
    for (i = 0; l[i].ref; i++)
        if (x >= l[i].x0 && x < l[i].x1)
            return i;
    return -1;
}

void hview3d_274670(HudWidget *w, HudEvent *ev)
{
    LGPoint rel;
    LGRect r;
    uchar id, mode, inside;
    HudView *v;
    long b;

    rel.x = EV_PT(ev).x - w->x0;
    if (w->flags & 0x40)
        rel.x = w->canvas.bm.w - 1 - rel.x;
    rel.y = EV_PT(ev).y - w->y0;
    id = w->id;
    r = *(LGRect *)&w->x0;
    mode = hud_views[id].mode;
    if (mode == 0x11 || mode == 0x13 || (mode == 8 && hud_eye.obj == game_396868))
        r.lr.y -= HIRES ? 0x10 : 8;
    inside = EV_PT(ev).y >= r.ul.y && EV_PT(ev).y < r.lr.y && EV_PT(ev).x >= r.ul.x
             && EV_PT(ev).x < r.lr.x;
    v = &hud_views[id];
    switch (ev->type) {
    case 0x40:
    case 0x80:
        MouseSetCursor(0x1F90016);
        return;
    case 8:
        weapons_290858();
        return;
    case 2:
        if (((uchar *)&game_pba[game_player_slot])[0xCF] & 2)
            weapons_290858();
        return;
    case 4:
        if (hud_views[id].mode != 8)
            return;
        if ((((uchar *)tn_replay_state_ptr)[5] & 4) || (((uchar *)tn_replay_state_ptr)[6] & 0x40)) {
            uchar *cam = ff_cams_390010[id];
            if (*(short *)(cam + 0xB5) && !cam[0xB7])
                helm_262D4C(*(short *)(cam + 0xB5));
            else
                helm_262D4C(0);
        }
        return;
    case 1:
        break;
    default:
        return;
    }
    if (hud_389BC2) {
        hview3d_275260(w);
        hview3d_274B00(ff_cams_390010[id], id, rel, 1);
        return;
    }
    if (!(v->flags & 0x10))
        return;
    mode = hud_views[id].mode;
    if ((mode == 0x11 || mode == 0x13) && !air_3968B4 && !inside) {
        b = button_at(hv_35B2D0, rel.x);
        if (air_target[0]) {
            aircraft_28559C(game_player_entity);
            return;
        }
        if (b == 0) {
            if (hud_views[id].mode == 0x11) {
                aircraft_283AFC();
                ((uchar *)((HudEye *)v->f14)->ctrl)[1] = 4;
                return;
            }
            hudview_26C224(id, 0x14);
            MODEMASK_B2(id) &= 0xFD;
        } else if (b == 1) {
            hudview_26C224(id, 0x12);
            MODEMASK_B2(id) &= 0xFD;
        } else if (b == 2) {
            hud_26BA6C((HudEye *)v->f14);
            hud_26AC98(hud_helmet);
            if (game_camera_38FB20) {
                hud_26AF24();
                hudview_26C318(1, 8);
            }
            hudview_26C224(id, 8);
            hud_views[id].mode_mask &= 0xFFFDE3FF;
        }
        return;
    }
    if (hud_views[id].mode == 8 && hud_eye.obj == game_396868) {
        if (!inside) {
            if (button_at(hv_35B310, rel.x) != 0)
                return;
            hud_26BA6C((HudEye *)v->f14);
            helm_262278();
            hud_26AC98(hud_helmet);
            if (game_camera_38FB20) {
                hud_26AF24();
                hudview_26C318(id, 8);
                return;
            }
            hudview_26C224(w->id, game_3968B5 ? 0x13 : 0x11);
            ((uchar *)&hud_views[id].mode_mask)[1] = (((uchar *)&hud_views[id].mode_mask)[1] & 0xE2) | 0x1C;
            return;
        }
    } else if (!game_camera_38FB20)
        return;
    hview3d_274570(w, pt_long(rel), id);
}

uchar hview3d_274B00(void *camp, long pane, LGPoint pt, uchar c)
{
    uchar *cam = camp;
    short e;
    GameEntity *en;

    if (!(hud_389BC2 & 6))
        return 0;
    if (c) {
        ffpipe_29E120((FfCamera *)cam, pt.x, pt.y);
        gr_set_canvas(&HUD_VIEW_W(pane)->canvas);
        hview3d_274DA4(pane);
    }
    e = (*(short *)(cam + 0xB5) && !cam[0xB7]) ? *(short *)(cam + 0xB5) : 0;
    if (c)
        ffpipe_29E160((FfCamera *)cam);
    if (!e)
        return 0;
    en = HENT(e);
    if (hud_389BC2 & 4) {
        if (en->cls != 1 || game_brains[en->f21].f005 == 2)
            return 0;
        hud_389BC8 = game_brains[en->f21].f006;
        cmd_2601D8();
        cmd_25F0AC(0x2EE);
        return en->cls;
    }
    if (en->cls == 1 && game_brains[en->f21].f005 != 2)
        return 0;
    hud_389BC8 = e;
    cmd_2601D8();
    cmd_25F0AC(0x2EE);
    return 1;
}

static void fill_type_0(void)
{
    grd_canvas->gc.fill_type = 0;
    grd_function_table = grd_function_fill_table;
    grd_uline_fill_vector = grd_uline_fill_table;
}

void hview3d_274C30(long view)
{
    uchar *pba = (uchar *)&game_pba[game_player_slot];
    uchar *ws = pba + 0x45 + 15 * (schar)pba[0xCE];
    LGPoint rel;
    short t;

    if (hud_views[view].mode == 8 && !weapon_ready(ws, pba + 0x86)) {
        HudWidget *w;
        hview3d_275260(HUD_VIEW_W(view));
        ffpipe_29EB64(&ff_view, (FfCamera *)ff_cams_390010[view]);
        w = HUD_VIEW_W(view);
        rel.x = MOUSE.x - w->x0;
        rel.y = MOUSE.y - w->y0;
        t = (hud_35A944 && targ_current) ? targ_current : 0;
#ifndef TN_WATCOM_ABI
        if (!port_aim_box)
            t = 0;
#endif
        if (((uchar *)&game_pba[game_player_slot])[0xCF] & 2) {
            if (hud_35A548 == HUD_VIEW_W(0) || hud_35A548 == HUD_VIEW_W(1)
                || hud_35A548 == HUD_VIEW_W(2))
                reticle_target(t, pt_long(rel), ff_cams_390010[view], &ff_view);
            else
                weapons_290858();
        }
    }
#ifndef TN_WATCOM_ABI
    if (hud_views[view].mode == 8) {
        void port_gun_quiet(void);      /* weapons.c: G38, no laser sound without a shot */
        port_gun_quiet();
    }
#endif
    if (hud_helmet && !(hud_35AEF0 & 1))
        HUD_VIEW_W(view)->flags |= 0x20;
    else if (hudview_26BBA0(view))
        HUD_VIEW_W(view)->flags |= 0x20;
#ifndef TN_WATCOM_ABI
    /* Frame rate 60 fps: every 3D view each frame, as the goggles draw them (the cockpit's take
     * turns, hudview_26BBA0: each of three at a third of the frame rate) */
    else if (port_smooth_fps && (hud_mode_info[hud_views[view].mode][0] & 8))
        HUD_VIEW_W(view)->flags |= 0x20;
#endif
}

/* mirror the rows of canvas c (0x274F09; w / 4 word swaps per row, so it runs away below 4) */
static void mirror_rows(grs_canvas *c)
{
    uchar *row = c->bm.bits, *a, *b, t0, t1;
    long h = c->bm.h;
    ulong n;

    while (h-- != 0) {
        n = (ulong)(long)c->bm.w >> 2;
        a = row;
        b = row + c->bm.w;
        do {
            b -= 2;
            t0 = a[0];
            t1 = a[1];
            a[0] = b[1];
            a[1] = b[0];
            b[0] = t1;
            b[1] = t0;
            a += 2;
        } while (--n);
        row += c->bm.row;
    }
}

void hview3d_274DA4(long view)
{
    long k = dmgdisp_27A118(view);
    HudWidget *w;
    FfCamera *cam;
    uchar mode;

    if (k == 2) {
        fill_type_0();
        hudview_26C50C(view);
        return;
    }
    w = HUD_VIEW_W(view);
#ifndef TN_WATCOM_ABI
    port_s3d_view_drawn(1 + view);
#endif
    hview3d_275260(w);
    if (game_35CADA != 0xFF)
        weather_2AF4E0(&ff_view, hv_390247[view]);
    if (hud_fill_tab[0]) {
        grd_canvas->gc.fill_type = 1;
        grd_function_table = grd_function_fill_table + 390;
        grd_uline_fill_vector = grd_uline_fill_table + 9;
        grd_canvas->gc.fill_parm = (long)hud_fill_tab[0];
    }
    ffpipe_41EB60 = hv_390247[view];
    cam = (FfCamera *)ff_cams_390010[view];
    if (k == 1)
        dmgdisp_27A3FC(&ff_view, (uchar *)cam);
    else {
        ffpipe_29EDB4(&ff_view, cam);
#ifndef TN_WATCOM_ABI
        port_s3d_render(1 + view, &ff_view, cam, (w->flags & 0x40) != 0);
#endif
    }
    fill_type_0();
    if (w->flags & 0x40)
        mirror_rows(&w->canvas);
    if (view == 1 && hud_389BC2)
        hudview_26C4A8();
    mode = hud_views[view].mode;
    if (mode == 0x11 || mode == 0x13) {
        hview3d_275830(w);
        if (air_3968B4) {
            grd_canvas->gc.fcolor = 0x44;
            GfxDrawText(0x1F7, RefGet(0x16E0077), 0x18, 0x18);
        } else if (game_3968B5) {
            if (air_target[0]) {
                grd_canvas->gc.fcolor = 0x44;
                GfxDrawText(0x1F7, RefGet(0x16E0071), 0xF, 0x28);
                hview3d_275654(w, hv_35B320);
            } else
                hview3d_275654(w, hv_35B2F0);
        } else
            hview3d_275654(w, hv_35B2D0);
        return;
    }
    if (mode == 8 && hud_eye.obj == game_396868) {
        hview3d_275830(w);
        hview3d_275654(w, hv_35B310);
        return;
    }
    if (hud_views[view].mode == 0x15) {
        grd_canvas->gc.fcolor = 0x44;
        GfxDrawText(0x1F7, RefGet(0x16E007B), 10, 5);
        if (hv_390240) {
            grd_canvas->gc.fcolor = hv_390246;
            GfxDrawText(0x1F7, RefGet(hv_390240), hv_390244, hv_390245);
        }
    }
}

#define VIEW_EYE(v) (*(short *)&hud_views[v].f14[0])     /* +0x14 entity the view looks from */
#define VIEW_DH(v) (*(short *)&hud_views[v].f14[4])      /* +0x18 heading offset */
#define VIEW_CTRL(v) (*(uchar **)&hud_views[v].f14[6])   /* +0x1A control record */
#define ENT_POS(e) ((ushort)(HENT(e)->x >> 16) | (long)(HENT(e)->y >> 16) << 16)

/* heading from the entity towards what its brain is busy with, else its own heading */
fixang hview3d_2750CC(long ent)
{
    uchar brain = HENT(ent)->f21;
    uchar *g = (uchar *)&game_brains[brain];
    uchar sq = g[6];
    long pt = (ushort)RS16(g, 0xA7) | (long)RS16(g, 0xAB) << 16;
    long a = *(long *)(g + 0xE);
    fixang h = HENT(ent)->heading;
    uchar kind = g[0x30];
    short e;

    if (kind == 2 || kind == 4) {
        if (g[0x30] == 2) {     /* kind 4 reaches this test too and fails it */
            e = RS16((uchar *)&game_squads[sq], 0xA9);
            if (e != 0)
                pt = ENT_POS(e);
        }
    } else if (kind == 8 || kind == 0x10) {
        if (((uchar *)&game_brains[brain])[0xCA]) {
            e = RS16((uchar *)&game_squads[sq], 0xBF);
            pt = ENT_POS(e);
        }
    }
    {
        long ax = (short)a, ay = (short)(a >> 16), px = (short)pt, py = (short)(pt >> 16);
        long dx = ax - px, dy = ay - py;
        if (dx < 0)
            dx = -dx;
        if (dy < 0)
            dy = -dy;
        if (dx > 1 || dy > 1)
            h = fix_atan2(py - ay, px - ax);
    }
    return h;
}

/* set ff_view to the eye of view widget w */
void hview3d_275260(HudWidget *w)
{
    long id = w->id;
    short e, a8, a4;
    uchar mode;

    ff_view.bank = 0;
    e = VIEW_EYE(id);
    *(short *)ff_view.g_5a = e;
    if (hz_playing && id == hz_play_cam) {
        haze_2B5B70(&ff_view);
        return;
    }
    ff_view.z = HENT(e)->z + hud_eye_dz + 0x3000;
    ff_view.x = HENT(e)->x;
    ff_view.y = HENT(e)->y;
    mode = hud_views[id].mode;
    if (mode == 0x15) {
        FfCamera *cam = (FfCamera *)ff_cams_390010[id];
        GameEntity *t;
        fix dx, dy, dz, d, zoom;
        if (!targ_current)
            return;
        t = HENT(targ_current);
        dx = t->x - ff_view.x;
        dy = t->y - ff_view.y;
        dz = t->z - ff_view.z;
        d = fix_safe_pyth_dist(dx, dy, hv_3558E0, 0x3A6);
        ff_view.heading = fix_atan2(dy, dx);
        ff_view.pitch = -fix_atan2(dz, d);
        zoom = d;
        if (zoom < 0x10000)
            zoom = 0x10000;
        else if (zoom > 0x100000)
            zoom = 0x100000;
        FfCameraSetup(cam, cam->canvas->bm.w, cam->canvas->bm.h, zoom);
        return;
    }
    if (mode == 4) {
        dmgdisp_27A2BC(id, &a8, &a4);
        ff_view.heading = a8 + hview3d_2750CC(e);
        ff_view.pitch = HENT(e)->f1d + a4;
        return;
    }
    dmgdisp_27A2BC(id, &a8, &a4);
    ff_view.heading = a8 + HENT(e)->heading + VIEW_DH(id);
    mode = hud_views[id].mode;
    if (mode == 8
        || ((*(uchar **)((uchar *)&game_pba[game_player_slot] + 0x9C))[0x1E]
            && mode != 0x11 && mode != 0x13)) {
        ff_view.heading += hud_441355;
        ff_view.pitch = drawobj_29DC20(game_player_entity) + hud_441357;
        return;
    }
    ff_view.pitch = HENT(e)->f1d + a4;
}

/* mouse over view widget w at pt (packed x, y): set the view's control state and the cursor */
void hview3d_2754FC(HudWidget *w, long pt)
{
    long id = w->id;
    short buf[6];
    short cw = w->canvas.bm.w, ch = w->canvas.bm.h;
    long wh;
    uchar mode = hud_views[id].mode;
    uchar *p;

    if (mode == 0x11 || mode == 0x13)
        ch -= HIRES ? 0x10 : 8;
    wh = (ushort)cw | (long)ch << 16;
    p = VIEW_CTRL(id);
    if (RS16(p, 2) != 0) {
        if (game_player_entity == VIEW_EYE(id)) {
            *VIEW_CTRL(id) = (uchar)damage_27C9C4(pt, wh, 0, &hud_move_b);
        } else {
            mode = hud_views[id].mode;
            if (mode == 0x11 || mode == 0x13) {
                *VIEW_CTRL(id) = (uchar)damage_27C8B8(pt, wh, buf);
                damage_27CB58(buf, (fix *)(game_cls3[HENT(VIEW_EYE(id))->slot] + 0x132));
            }
        }
    } else {
        mode = hud_views[id].mode;
        if ((mode == 0x11 || mode == 0x13) && !air_3968B4 && (short)(pt >> 16) >= ch)
            *VIEW_CTRL(id) = 4;
        else
            *VIEW_CTRL(id) = (uchar)damage_27C8B8(pt, wh, buf);
    }
    MouseSetCursor(damage_27C9F4(*VIEW_CTRL(id)));
#ifndef TN_WATCOM_ABI
    port_hud_aim_sign();
#endif
}

/* the button strip along the bottom of view widget w */
void hview3d_275654(HudWidget *w, HvButton *list)
{
    long h = w->canvas.bm.h;
    HvButton *b;

    grd_canvas->gc.fill_type = 3;
    grd_function_table = grd_function_fill_table + 1170;
    grd_uline_fill_vector = grd_uline_fill_table + 27;
    grd_canvas->gc.fill_parm = 0x4000;
    for (b = list; b->ref; b++) {
        grd_canvas->gc.fcolor = 0x26;
        WATCOM_CALL((CanvasFn)grd_canvas_table[15], (long)(short)b->x0,
                    (long)(short)(h - (HIRES ? 0x10 : 8)), (long)(short)b->x1, (long)(short)h);
    }
    fill_type_0();
    grd_canvas->gc.fcolor = 0x1D;
    for (b = list; b->ref; b++)
        GfxDrawText(0x1F7, RefGet(b->ref), b->tx, h - ((HIRES ? 0x10 : 8) - 1));
}

void hview3d_2757A0(long view, Ref ref, long x, long y, long color)
{
    if (view == 0x15) {
        hv_390240 = ref;
        hv_390244 = (uchar)x;
        hv_390245 = (uchar)y;
        hv_390246 = (uchar)color;
    }
}

/* draw a text item {Ref; uchar x, y, colour} */
void hview3d_2757C4(uchar *p)
{
    grd_canvas->gc.fcolor = p[6];
    GfxDrawText(0x1F7, RefGet(*(Ref *)p), p[4], p[5]);
}

long hview3d_2757F8(long x, HvButton *list)
{
    return button_at(list, x);
}

/* compass ticks along the top of view widget w */
void hview3d_275830(HudWidget *w)
{
    long cw = w->canvas.bm.w;
    long id = w->id;
    long scale = (cw << 16) / (2 * (long)ff_cam.fov_x);
    long half = cw / 2;
    long i;
    short a = 0, x;
    grs_bitmap *bm;

    grd_canvas->gc.fcolor = 0x1D;
    for (i = 0; i < 8; i++, a += 0x2000) {
        x = (short)(((long)(short)(a - HENT(VIEW_EYE(id))->heading) * scale >> 16) + half);
        if (x > -5 && x < cw) {
            WATCOM_CALL((CanvasFn)grd_canvas_table[10], (long)x, 0L);
            bm = bm_get(radar_tick_refs[i][0]);
            BITMAP(bm, (short)(x - 3), 1);
            if (radar_tick_refs[i][1]) {
                bm = bm_get(radar_tick_refs[i][1]);
                BITMAP(bm, (short)(x + 3), 1);
            }
        }
    }
}

void hview3d_275988(HudWidget *w)
{
    mirror_rows(&w->canvas);
}
