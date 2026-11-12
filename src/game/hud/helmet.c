/* helmet.c -- Terra Nova module M016 (0x261480-0x2641BF; source file name unknown): the helmet /
 * HUD frame.  Builds the 69 HUD widgets (resource 0x2FB rectangles + stencils, callbacks from
 * hud_widget_defs), picks the widget set of the current layout, runs the per-frame HUD task,
 * blits dirty widgets, routes mouse events, shows tooltips, switches 320x200 <-> 320x400, and
 * draws the suit damage figure.  Decompiled from __FF.EXE v1.09.  Names are the map's.
 *
 *   0x261480 helm_zero_rect  (data: an 8-byte zero LGRect; the map's helm_261480)
 *   0x261488 helm_261488   (dead) reallocate the viewer strip buffer, 0xFA00 bytes
 *   0x2614B0 helm_2614B0   (dead) the same, 0x1F400 bytes
 *   0x2614D8 helm_2614D8   HUD init (helmet type)
 *   0x2617F4 helm_2617F4   reset mission HUD / effect state
 *   0x2618E0 helm_2618E0   palette of the frame bitmap, translucency tables
 *   0x261A00 helm_261A00   HUD shutdown
 *   0x261B54 helm_261B54   (no callers) update + draw widget hud_w_389E0C
 *   0x261BBC helm_261BBC   (no callers) draw one widget
 *   0x261BD8 helm_261BD8   update + draw the widgets of the layout
 *   0x261C5C helm_261C5C   overlay: hint text and pointer lines
 *   0x261D70 helm_261D70   overlay: bitmap 0x23D0013
 *   0x261DD4 helm_261DD4   overlay: hint box
 *   0x261E48 HelmTaskHook  the per-frame HUD task (game hook slot 11)
 *   0x262018 helm_262018   frame background and gauges
 *   0x26214C helm_26214C   mark every widget for redraw
 *   0x262184 helm_262184   make a widget
 *   0x262244 helm_262244   relocate a stencil resource
 *   0x262278 helm_262278   pick the widget set of the layout
 *   0x2623E4 helm_2623E4   mouse handler
 *   0x2625E4 helm_2625E4   hand a mouse event to the widget under it
 *   0x2626B8 helm_2626B8   radio messages to the player
 *   0x2627F8 helm_2627F8   kill callback: forget a dead target
 *   0x262814 helm_262814   blit the dirty widgets
 *   0x262A44 helm_262A44   (one caller) full-screen blit
 *   0x262A88 helm_262A88   an entity is gone: drop it as target
 *   0x262AC4 helm_262AC4   kill callback: the same
 *   0x262B00 helm_262B00   is ent in sensor range (or on the target list)?
 *   0x262C2C helm_262C2C   (no callers) target the nearest PBA
 *   0x262D2C helm_262D2C   (no callers) drop the target when out of range
 *   0x262D4C helm_262D4C   set the current target
 *   0x262DB8 helm_262DB8   right click on a pane
 *   0x262E34 helm_262E34   (table) type / team word of the current target
 *   0x262E88 helm_262E88   (table) install the radio text callback
 *   0x262EA0 helm_262EA0   switch between low and high resolution
 *   0x2633A0 helm_2633A0   (table) resolution switch requests
 *   0x263410 helm_263410   is the ground at (x, y) of a kind in terrain_bitmask?
 *   0x2634A4 helm_2634A4   (no callers) identity CLUT
 *   0x2634B8 helm_2634B8   CLUT entries of one suit part
 *   0x26350C helm_26350C   damage step of suit part
 *   0x26355C helm_26355C   damage step of the suit's power
 *   0x2635C0 helm_2635C0   (table) widget update: always redraw
 *   0x2635C8 helm_2635C8   draw the suit damage figure
 *   0x2637DC helm_2637DC   (table) widget draw: the player's suit figure
 *   0x263824 helm_263824   tooltips: find the widget under the mouse
 *   0x263940 helm_263940   tooltips: draw one
 *   0x263BC0 helm_263BC0   (table) tooltips on / off
 *   0x263BE4 helm_263BE4   set up the three view panes and their cameras
 *   0x263EB8 helm_263EB8   (table) helmet button event
 *   0x263EE8 helm_263EE8   (table) helmet button update
 *   0x263F00 helm_263F00   (table) helmet button draw
 *   0x263FC0 helm_263FC0   (table) helmet-off button event
 *   0x263FF0 helm_263FF0   (table) helmet-off button update
 *   0x264008 helm_264008   (table) helmet-off button draw
 *   0x2640D4 helm_2640D4   ammunition count of the selected weapon
 *
 * Quirks kept:
 *  - helm_262C2C starts its "nearest" search with a negative best distance, so it never finds
 *    one; it also takes the player position from the PBA indexed by the player's entity id, and
 *    would hand a PBA index (not an entity) to helm_262D4C.
 *  - helm_263940 compares the tooltip's right edge (w / 2 + y + offset) against the screen
 *    height to choose above / below; the text is copied unchecked into an 0x50-byte buffer.
 *  - helm_262DB8 passes the pane pointer to hud_269F28 as its point argument.
 *  - helm_263BE4 copies each view widget's rectangle and canvas onto themselves (no effect).
 *  - helm_262EA0 writes hud_35A544 as a dword.
 */
#include "g7_b.h"
#include "g8_a.h"     /* added at the G5-G8 merge */
#include "g8_b.h"     /* added at the G5-G8 merge */
#include "g8_c.h"     /* added at the G5-G8 merge */
#include "g10_b.h"     /* added at the G9/G10 merge */
#ifndef TN_WATCOM_ABI
#include "sharp3d.h"                    /* widescreen: the mouse beside the screen */
#include "cockpitpack.h"                /* cockpit packs: the art as drawn */
#endif

#ifndef TN_LINK_ORIGINAL_DATA
LGRect helm_zero_rect;
HudWidgetDef hud_widget_defs[69];
long hud_35A914;
uchar hud_tip_flags;
char *hud_res_names[4], *hud_res_names_hi[4];
uchar hud_pal_cycle[3][3];
uchar hud_35A958;
long hud_box_tab[4][4];
uchar hud_35A990;
ulong hud_tip_time;
uchar hud_suit_clr[7], hud_frame_clr[4];
uchar hud_clut[256];
LGRect hud_tip_rect;
uchar *hud_tluc_389DCC;
long hud_res_file, hud_res_file2;
HudWidget *hud_last, *hud_first, *hud_w_389E0C, *hud_radar, *hud_w_389E18, *hud_w_389E1C;
HudWidget hud_pool[69];
uchar hud_flags_cam[3], hud_flags_std[3];
uchar hud_38B9A8, hud_38B9A9;
uchar hud_pal_fx[3];
long hud_390300, hud_390304;
uchar hud_392407, hud_392408;
long hud_43D534;
ushort game_44136A, game_44136C;
uchar tnm_447117;
LGRect tnm_dirty[2];
uchar game_3599E8, game_35B150, game_35B175, game_35B19C;
long game_35B188, game_35B1A4, game_35B1A8, game_35B1AC, game_35B1B0;
long game_389BD0, game_389BD4;
#endif

#define W(i) (hud_widgets[i])
#define HIRES (game_35A540 >> 1)
#define ENTP(e) (&game_entities[(short)(e)])
#define PBA(s) ((uchar *)&game_pba[(short)(s)])
#define IDX15(v) ((short)((short)((v) << 1) >> 1))
#define SPK_VOICE(k) (((uchar *)&game_391E50[k])[6])
#define BRAIN_B(b, o) (((uchar *)&game_brains[b])[o])
#define BM_FIX(bm) ((bm)->bits = (uchar *)(bm) + 0x1C)
#define UBITMAP(bm, x, y) WC3(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * (bm)->type], (bm), (x), (y))
#define BITMAP(bm, x, y) WC3(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * (bm)->type], (bm), (x), (y))
#define RECT_FILL(a, b, c, d) WC4(grd_canvas_table[16], (a), (b), (c), (d))
#define BOX(a, b, c, d) WC4(grd_canvas_table[18], (a), (b), (c), (d))

/* draw resource bitmap ref unclipped at (0, 0) of the current canvas */
static void helm_draw_frame(Ref ref)
{
    grs_bitmap *bm = RefGet(ref);
    BM_FIX(bm);
    UBITMAP(bm, 0, 0);
#ifndef TN_WATCOM_ABI
    {
        /* cockpit packs (port/mods/cockpitpack.c): the suit's letter, the last of its res file's name */
        const char *name = HIRES ? hud_res_names_hi[hud_38B9AD & 3] : hud_res_names[hud_38B9AD & 3];
        int letter = name && strlen(name) > 5 ? name[strlen(name) - 5] : 0;
        if (ref == 0x2EE0000 && bm->w == 320 && bm->h > 0 && bm->h <= 400) {
            /* the cockpit's picture, the whole screen, as drawn */
            static uchar art[320 * 400];
            grs_canvas c, *old = grd_canvas;
            memset(art, 0, sizeof art);
            gr_init_canvas(&c, art, BMT_FLAT8, bm->w, bm->h);
            gr_set_canvas(&c);
            UBITMAP(bm, 0, 0);
            gr_set_canvas(old);
            port_ck_art(letter, art, bm->w, bm->h);
        }
        if (ref == 0x2F00000 || ref == 0x2EF0000) {
            /* the cockpit's frame over its 3D view, the goggles' over the screen: where it covers
             * the 3D (widescreen, the stretched screen: port/video/sharp3d.c), and the art a cockpit
             * pack's picture goes over */
            static const grs_bitmap *seen;
            static short seen_w, seen_h;
            static uchar mask[320 * 400];
            static unsigned gen;
            long off = grd_canvas->bm.bits - tnui_back_canvas.bm.bits, row = tnui_back_canvas.bm.row;
            if ((bm != seen || bm->w != seen_w || bm->h != seen_h) && bm->w > 0 && bm->h > 0 &&
                bm->w * bm->h <= (long)sizeof mask) {
                grs_canvas c, *old = grd_canvas;
                memset(mask, 0, sizeof mask);
                gr_init_canvas(&c, mask, BMT_FLAT8, bm->w, bm->h);
                gr_set_canvas(&c);
                UBITMAP(bm, 0, 0);
                gr_set_canvas(old);
                seen = bm;
                seen_w = bm->w;
                seen_h = bm->h;
                gen++;
            }
            if (bm == seen && row > 0) {
                port_s3d_overlay(mask, bm->w, bm->h, off % row, off / row);
                port_ck_piece(ref == 0x2EF0000, letter, mask, gen, bm->w, bm->h, off % row, off / row);
            } else
                port_ck_piece(-1, 0, NULL, 0, 0, 0, 0, 0);
        }
    }
#endif
}

static void helm_mark_all(void)
{
    HudWidget *w;
    for (w = hud_first; w <= hud_last; w++)
        w->flags |= 0x20;
}

static void helm_vd_alloc(size_t n)
{
    if (vd_canvas.bm.bits)
        Free(vd_canvas.bm.bits);
    vd_canvas.bm.bits = Malloc(n);
}

#ifndef TN_WATCOM_ABI
/* Freefall: the pirate suit's HUD layout (res 0x2FB of RESHELMP / RESHLMHP) puts the compass
 * strip, the radar widget (W(1) in the cockpit, W(35) in the goggles), across the middle of the
 * 3D view; the other suits' is at its top.  With a cockpit pack's picture shown (port_ck_on) it
 * moves up to just under the view's top edge, the weapon device's lower edge (owner's ask): the
 * widget's rectangle and canvas move, so its clicks and the nav marker's line go with it.  Only
 * those rectangles move (the pirate's own, at 320 x 400 and at 320 x 200). */
static const short helm_compass_move[4][3] = {  /* widget, its row as the layout has it, the new row */
    { 1, 116, 78 }, { 1, 58, 39 }, { 35, 180, 76 }, { 35, 90, 38 } };
static short helm_compass_y0[2];        /* W(1)'s and W(35)'s rows as the layout has them */

static void helm_compass_place(void)
{
    int k, moved = 0;
    for (k = 0; k < 4; k++) {
        HudWidget *w = &hud_pool[helm_compass_move[k][0]];
        short base = helm_compass_y0[helm_compass_move[k][0] == 35], y0, h = (short)(w->y1 - w->y0);
        if (base != helm_compass_move[k][1])
            continue;
        y0 = port_ck_on ? helm_compass_move[k][2] : base;
        if (w->y0 != y0) {
            w->y0 = y0;
            w->y1 = (short)(y0 + h);
            gr_init_sub_bitmap(&tnui_back_canvas.bm, &w->canvas.bm, w->x0, y0, (short)(w->x1 - w->x0), h);
            moved = 1;
        }
    }
    if (moved)
        helm_mark_all();
}
#endif

static void helm_make_widgets(void)
{
    HudWidgetRec *rec = RefLock(0x2FB0000);
    long i;
    for (i = 0; i < 69; i++) {
        hud_widgets[i] = &hud_pool[i];
        helm_262184(&hud_pool[i], &rec[i], &hud_widget_defs[i]);
    }
    ResUnlock(0x2FB);
#ifndef TN_WATCOM_ABI
    helm_compass_y0[0] = hud_pool[1].y0;
    helm_compass_y0[1] = hud_pool[35].y0;
#endif
}

static void helm_read_flags(void)
{
    void *t;
    RefExtract(ResGet(0x2FC), 0x2FC0000, hud_bar_rects);
    t = ResGet(0x2FD);
    RefExtract(t, 0x2FD0000, hud_flags_std);
    RefExtract(t, 0x2FD0001, hud_flags_cam);
}

void helm_261488(void)
{
    helm_vd_alloc(0xFA00);
}

void helm_2614B0(void)
{
    helm_vd_alloc(0x1F400);
}

void helm_2614D8(uchar type)
{
#ifndef TN_WATCOM_ABI
    int hi_later;
#endif
    uchar *pba;
    uchar *wt;
    long k, i;

    hud_38B9AD = type;
#ifndef TN_WATCOM_ABI
    /* Freefall: the mission's screen starts in 320 x 200 even when 320 x 400 is chosen (the
     * game's high detail flag survives from the options or the last mission); setting up the
     * high detail HUD on it overran the screen.  The switch is asked for at the end instead,
     * as a click on 320 x 400 would. */
    hi_later = (game_35A540 & 2) && tnui_back_canvas.bm.h < 400;
    if (hi_later)
        game_35A540 &= ~3;
#endif
    if (game_35A540 & 2) {
        hud_res_file = ResOpenResFile(hud_res_names_hi[type], 0, 0);
        hud_res_file2 = ResOpenResFile("reshlmhi.res", 0, 0);
        helm_vd_alloc(0x1F400);
    } else {
        hud_res_file = ResOpenResFile(hud_res_names[type], 0, 0);
        hud_res_file2 = ResOpenResFile("reshelm.res", 0, 0);
        helm_vd_alloc(0xFA00);
    }
    helm_2618E0();
    hud_pal_fx[0] = palette_install_effect(CYCLE, REAL_TIME, 0xFC, 3, 0xFA, 0, hud_pal_cycle[0], 0);
    hud_pal_fx[1] = palette_install_effect(CYCLE, REAL_TIME, 0xFD, 3, 0xFA, 0, hud_pal_cycle[1], 0);
    hud_pal_fx[2] = palette_install_effect(CYCLE, REAL_TIME, 0xFE, 3, 0xFA, 0, hud_pal_cycle[2], 0);
    gr_set_canvas(&tnui_back_canvas);
    helm_draw_frame(0x2EE0000);
    hud_38B9AE = 1;
    hud_eye_dz = 0;
    hud_helmet = 0;
    helm_make_widgets();
    helm_read_flags();
    helm_262278();
    radar_265700();
    hudcb_2651C8(0);
    hudcb_2641C0();
    HudInit();
    hudview_26BBF4();
    hudview_26BF3C();
    mfddata_266C90();
    ResLock(0x171);
    tnm_447117 = 1;
    MouseAddHandler(&tnui_full_rect, 0x4FF, WATCOM_FN(helm_2623E4));
    KeyAddHandler(WATCOM_FN(PlayKeyHandler), 0, 1, 0);
    game_hooks[11] = (GameHook)WATCOM_FN(HelmTaskHook);
    ((uchar *)&game_hook_mask)[1] |= 8;
    killobj_28952C(1, WATCOM_FN(helm_2627F8), 0xFFFFFFFF);
    for (k = 0; k < 0x3C; k += 0xF) {
        pba = PBA(game_player_slot);
        if (pba[0x45 + k]) {
            wt = WT(pba[0x45 + k]);
            ResGet(*(ushort *)(wt + 0x14));
            ResGet(*(ushort *)(wt + 0x16));
            if (*(ushort *)(wt + 0x18))
                ResGet(*(ushort *)(wt + 0x18));
        }
    }
    ResGet(0x7E7);
    ResGet(0x76D);
    for (i = 0; i < 0x100; i++)
        hud_clut[i] = (uchar)i;
    helm_2617F4();
#ifndef TN_WATCOM_ABI
    if (hi_later)
        game_35A540 |= 3;               /* HelmTaskHook switches on the first frame */
#endif
}

void helm_2617F4(void)
{
#ifndef TN_WATCOM_ABI
    void port_mission_begin(void);
    port_mission_begin();               /* gimbal mode off, the keys' hint from now (port/core/platform.c) */
#endif
    lg_memset(air_mission_flags, 0, 0, 5);
    game_35A95C = 0;
    air_35B6CC = 0;
    game_35B19C = 0;
    game_35B175 = 0;
    drawobj_35B1A0 = 0;
    game_35B188 = 0;
    game_35B1B0 = 0;
    game_35B1AC = 0;
    game_35B1A8 = 0;
    game_35B1A4 = 0;
    game_35B150 = 0;
    inst_siren_flags = 0;
    game_3599E8 = 0;
    game_35BF48 = 0;
    air_35B6ED = 0;
    game_389BD0 = 0;
    game_389BD4 = 0;
    hud_pitch_smooth = 0;
    inst_siren_level = 0;
    inst_siren_time = 0;
    game_35B6E0 = 0;
    strike_35BF55 = 0;
    strike_aborted = 0;
    inst_siren_count = 0;
    strike_active = 0;
    air_35B6D0 = 0;
    radio_msg_47_said = 0;
    if (hud_helmet)
        hud_26AC98(0);
}

void helm_2618E0(void)
{
    uchar *bm = RefGet(0x2EE0000);
    uchar *p;

    if (*(long *)(bm + 0x18)) {
        p = ResGet(0x2EE);
        p += *(long *)(bm + 0x18);
        gr_set_pal(*(short *)p, *(short *)(p + 2), p + 4);
    }
    planet_299E04();
    if (grd_ipal)
        ipal_update();
    else
        ipal_alloc();
    tluc8tab_p[0xF8] = gr_init_translucency_table(gr_malloc(0x100), 0x8000, 0x10000, 0xF01E03C0);
    tluc8tab_p[0xF9] = gr_init_translucency_table(gr_malloc(0x100), 0x4000, 0x10000, 0xE01C0380);
    hud_tluc_389DD0 = gr_malloc(0x100);
    gr_init_translucency_table(hud_tluc_389DD0, 0x8000, 0x10000, 0x380);
    hud_tluc_389DCC = gr_malloc(0x100);
    gr_init_translucency_table(hud_tluc_389DCC, 0x8000, 0x10000, 0x120380);
    hud_tluc_389DC8 = gr_malloc(0x100);
    gr_init_translucency_table(hud_tluc_389DC8, 0x8000, 0x10000, 0x1E03C0);
    sfx_28D4E8();
}

void helm_261A00(void)
{
    HudWidgetRec *rec;
    long i;

    ((uchar *)&game_hook_mask)[1] &= ~8;
    targ_current = 0;
    KeyRemoveHandler(WATCOM_FN(PlayKeyHandler), 0);
    MouseRemoveHandler(&tnui_full_rect, 0x4FF, WATCOM_FN(helm_2623E4));
    mfddata_266E84();
    hudview_26BC74();
    hud_267D68();
    radar_265720();
    mfddata_266340();
    hudcb_2642D4();
    rec = RefLock(0x2FB0000);
    for (i = 0; i < 69; i++, rec++) {
        if (rec->sten) {
            ResUnlock(rec->sten);
            ResDrop(rec->sten);
        }
    }
    ResUnlock(0x2FB);
    ResUnlock(0x171);
    ResDrop(0x171);
    gr_free(tluc8tab_p[0xF8]);
    gr_free(tluc8tab_p[0xF9]);
    gr_free(hud_tluc_389DD0);
    gr_free(hud_tluc_389DCC);
    gr_free(hud_tluc_389DC8);
    palette_remove_effect(hud_pal_fx[0]);
    palette_remove_effect(hud_pal_fx[1]);
    palette_remove_effect(hud_pal_fx[2]);
    ResCloseFile(hud_res_file);
    Free(vd_canvas.bm.bits);
    vd_canvas.bm.bits = 0;
    tnm_447117 = 0;
    MouseSetCursor(0x15E0014);
    ResCloseFile(hud_res_file2);
#ifndef TN_WATCOM_ABI
    port_s3d_stretch = 0;               /* the mission's screen is gone (port/video/sharp3d.c) */
#endif
}

#ifndef TN_WATCOM_ABI
/* Freefall: "GIMBAL MODE: ON" while gimbal mode is on (Advanced controls), in white, at the bottom
 * of the middle screen (view 1; at its top it covered the order menu's first line, P93; not while
 * the order menu's lines reach down there, as F7 in the seven-line menus).  In the goggles with
 * their screens hidden (Tab, hud_35AEF0 bit 0) it stays just there, drawn over the main view, and
 * so do the screen's map's view cone ("Map view cone") and the suit's mark and, with "Show enemies
 * without MFD", its contacts, or with "Radar without MFD" the screen's whole map as a round radar
 * (mapdisp.c port_map_hidden; not while the full map (M) fills the view: it has its own cone).  P105: that was above the weapon selector, and "hidden" was read as view 1 not drawn for
 * half a second: switching gimbal mode redraws view 1 even then, so the label and the cone went
 * away for half a second each time, and came half a second late after Tab. */
extern int port_gimbal;
extern int port_map_cone, port_map_enemies, port_map_radar;
void port_map_hidden(void *view1, long x, long y);
/* a canvas's place on the game's screen (the widgets' rectangles can differ: the goggles' middle
 * screen's canvas starts below its frame's top) */
static int canvas_xy(const grs_canvas *c, long *x, long *y)
{
    long off = c->bm.bits - tnui_back_canvas.bm.bits, row = tnui_back_canvas.bm.row;
    if (off < 0 || row <= 0 || off >= row * tnui_back_canvas.bm.h)
        return 0;
    *x = off % row;
    *y = off / row;
    return 1;
}
/* Freefall: the hint for new players, at the top of the main view for the first seconds of a
 * mission until the help screen was opened once (port/ui/help.c) */
void port_hint_mission(void);
int port_hint_show(void);
static void helm_keys_hint(HudWidget *w)
{
    /* the lines after the first with the Advanced controls: the head turns alone there, and F is
     * how the body follows it (owner: "it is important key"), X how it keeps following; Shift
     * frees the pointer for the screens' buttons */
    static char *const wide[3] = { "H: HELP   O: OPTIONS", "F: ALIGN BODY TO HEADVIEW   X: GIMBAL MODE",
                                   "SHIFT (HOLD): FREE MOUSE CURSOR" };
    static char *const narrow[4] = { "H: HELP   O: OPTIONS", "F: ALIGN BODY TO HEADVIEW", "X: GIMBAL MODE",
                                     "SHIFT (HOLD): FREE MOUSE CURSOR" };
    char *const *text = wide;
    extern int port_mouselook;
    grs_font *f;
    long i, n = port_mouselook ? 3 : 1, th, y;

    if (w != hud_w_389DF4 || !w->x1 || !port_hint_show())
        return;
    f = ResLock(0x1F7);
    if (n == 3 && gr_font_string_width(f, wide[1]) >= w->canvas.bm.w) {     /* a narrow view: X below */
        text = narrow;
        n = 4;
    }
    th = gr_font_string_height(f, text[0]);
    y = w->canvas.bm.h / 6;
    for (i = 0; i < n; i++) {
        long tw = gr_font_string_width(f, text[i]);
        if (tw < w->canvas.bm.w) {
            grd_canvas->gc.fcolor = 0x1D;
            GfxDrawText(0x1F7, text[i], (w->canvas.bm.w - tw) / 2, y + i * (th + (HIRES ? 4 : 2)));
        }
    }
    ResUnlock(0x1F7);
}
static void helm_gimbal_label(HudWidget *w)
{
    static char text[] = "GIMBAL MODE: ON";
    int hidden = hud_helmet && (hud_35AEF0 & 1);
    /* view 1; hidden, the goggles' own middle screen (helm_263BE4 picks the goggles' screens as
     * the views only while they show: after the goggles came on with their screens hidden the
     * views are still the cockpit's, its middle screen ten rows lower) */
    HudWidget *v1 = hidden ? W(hud_eye.obj == game_396868 ? 61 : 48) : HUD_VIEW_W(1);
    grs_font *f;
    long tw, th, x, y, ox = 0, oy = 0, vx, vy, mx, my;

    if (!v1 || !v1->x1)
        return;
    if (w == v1 && !hidden) {
        if (!port_gimbal)
            return;
    } else if (w == hud_w_389DF4 && hidden) {
        if (canvas_xy(&v1->canvas, &vx, &vy) && canvas_xy(&w->canvas, &mx, &my)) {
            ox = vx - mx;               /* view 1's place on the main view */
            oy = vy - my;
        } else {
            ox = v1->x0 - w->x0;
            oy = v1->y0 - w->y0;
        }
        if ((port_map_cone || port_map_enemies || port_map_radar) && !game_camera_38FB20)   /* not over the full map */
            port_map_hidden(v1, ox, oy);
        if (!port_gimbal)
            return;
    } else
        return;
    f = ResLock(0x1F7);
    tw = gr_font_string_width(f, text);
    th = gr_font_string_height(f, text);
    x = v1->canvas.bm.w / 2;
    y = v1->canvas.bm.h - th - (HIRES ? 2 : 1);   /* its bottom line, below the order menu's lines */
    if (!hidden && hud_views[1].mode == 7 && mfd_38FC87) {     /* the order menu (mfdpanel.c mfd_26E208) */
        MfdItem *it = mfd_38FC87->items;
        long n = 0, step = HIRES ? 0xE : 7;
        for (; it->fn; it++)
            n++;
        it = mfd_38FC87->items;
        if (2 + (n - 1) * step + th > y ||              /* its lines reach the bottom (F7) */
            ((it == mfd_35A038 || it == mfd_35A090 || it == mfd_35A2FC) && (HIRES ? 0x5C : 0x2E) + th > y))
            y = -1;                     /* or its status line is there: no label meanwhile */
    }
    if (y >= 0) {
        grd_canvas->gc.fcolor = 0x1D;
        GfxDrawText(0x1F7, text, ox + x - tw / 2, oy + y);
    }
    ResUnlock(0x1F7);
}
#endif

static void helm_widget_draw(HudWidget *w)
{
#ifndef TN_WATCOM_ABI
    int sv = w == HUD_VIEW_W(0) ? 1 : w == HUD_VIEW_W(1) ? 2 : w == HUD_VIEW_W(2) ? 3 : 0;
    unsigned seen = sv ? port_s3d_views(sv) : 0;    /* a side screen's sharp picture (sharp3d.c) */
#endif
    gr_set_canvas(&w->canvas);
    WATCOM_CALL((void (*)())w->draw, (long)w);
#ifndef TN_WATCOM_ABI
    if (sv && port_s3d_views(sv) == seen)
        port_s3d_view_plain(sv);        /* none this time (the order menu over the map: P94) */
    helm_gimbal_label(w);
    helm_keys_hint(w);
    /* widescreen: the screen's pixels there; not with the screens hidden (Tab, hud_35AEF0 bit 0),
     * where the game still draws a screen once when its mode changes (Auto target camera picking
     * a target, the squad list coming back) but the main view covers it: the display showed the
     * main view at the game's size there for that frame, the arm cut off by a box (P96) */
    if (hud_helmet && !(hud_35AEF0 & 1) && w != hud_w_389DF4 &&
        (w == HUD_VIEW_W(0) || w == HUD_VIEW_W(1) || w == HUD_VIEW_W(2)))
        port_s3d_cover(w->x0, w->y0, w->x1, w->y1);
#endif
    w->flags = (w->flags | 0x80) & ~0x20;
}

void helm_261B54(void)
{
    if (tn_ticks >= periodic_next_due)
        periodic_run(tn_ticks);
    if (hud_w_389E0C->update)
        WATCOM_CALL((void (*)())hud_w_389E0C->update, (long)hud_w_389E0C);
    if ((hud_w_389E0C->flags & 0x20) && hud_w_389E0C->draw && hud_w_389E0C->x1) {
        gr_set_canvas(&hud_w_389E0C->canvas);
        WATCOM_CALL((void (*)())hud_w_389E0C->draw, (long)hud_w_389E0C);
        hud_w_389E0C->flags = (hud_w_389E0C->flags | 0x80) & ~0x20;
    }
}

void helm_261BBC(HudWidget *w)
{
    helm_widget_draw(w);
}

void helm_261BD8(void)
{
    HudWidget *w;

    for (w = hud_first; w <= hud_last; w++) {
        if (w == hud_w_389E0C)
            continue;
        if (tn_ticks >= periodic_next_due)
            periodic_run(tn_ticks);
        if (w->update)
            WATCOM_CALL((void (*)())w->update, (long)w);
        if ((w->flags & 0x20) && w->draw && w->x1)
            helm_widget_draw(w);
    }
}

void helm_261C5C(void)
{
    long x = HIRES ? 0x140 : 0xA0;

    gr_set_canvas(&tnui_back_canvas);
    hudutil_27B5B0(0x1780025, 0x28, (short)x);
    x += 3;
    if (HIRES)
        x += 3;
    grd_canvas->gc.fcolor = 0x4E;
    gr_iline(0x53, (short)(x + 3), (short)(hud_390300 - 2), (short)(hud_390304 - 2));
    gr_iline(0x53, (short)(x - 3), (short)(radar_mark_x + hud_radar->x0 - 1),
             (short)(radar_mark_y + hud_radar->y0 + 2));
    if (drawobj_nav_sx >= 0)
        gr_iline(0x53, (short)x, (short)(drawobj_nav_sx + hud_w_389DF4->x0 - 2),
                 (short)(drawobj_nav_sy + hud_w_389DF4->y0 - 2));
    hud_38B9AE = 1;
}

void helm_261D70(void)
{
    long y = HIRES ? 0x16C : 0xB6;
    grs_bitmap *bm;

    gr_set_canvas(&tnui_back_canvas);
    bm = RefGet(0x23D0013);
    BM_FIX(bm);
    BITMAP(bm, 0x1D, (short)y);
    hud_38B9AE = 1;
}

void helm_261DD4(void)
{
    long *b = hud_box_tab[hud_hint_mode - 1];
    short k = HIRES ? 2 : 1;

    gr_set_canvas(&tnui_back_canvas);
    grd_canvas->gc.fcolor = 0x4E;
    BOX((short)b[0], (short)(k * (short)b[1]), (short)b[2], (short)(k * (short)b[3]));
}

void HelmTaskHook(void)
{
    uchar f = game_35A540;

#ifndef TN_WATCOM_ABI
    helm_compass_place();
#endif
    if (f & 1) {
        hud_43D534 = ((f & 2) != 0) + 1;
        helm_262EA0();
    }
    if (hud_zoom_reset)
        HudViewReset();
    if (hud_35A958) {
        hud_26B8F8(1);
        hud_35A958 = 0;
    }
    reticle_edge_check();
    helm_2626B8();
    if (hud_35A990) {
        gr_set_canvas(&tnui_back_canvas);
        helm_draw_frame(0x2EE0000);
        helm_mark_all();
        mfddata_26773C();
        hud_38B9AE = 1;
        hud_35A990 = 0;
    }
    cmd_25F2B8();
    radio_update();
    hud_38B9A8 = 0;
    helm_261BD8();
    hud_38B9A9 = hud_38B9A8;
    helm_262018();
    if (hud_392408 && ffpipe_38FFF0) {
        helm_261C5C();
        hud_35A990 = 1;
    } else if (hud_392407 && !(*(uchar **)(PBA(game_player_slot) + 0x9C))[0x1E]) {
        helm_261D70();
        hud_35A990 = 1;
    } else if (hud_hint_mode) {
        helm_261DD4();
        hud_35A990 = 1;
    }
    mfddata_267198();
    if (!(hud_tip_flags & 2) && game_359708 != 0x27)
        helm_263824();
    menudraw_27BB30();
    helm_262814();
    if (targ_current && !helm_262B00(targ_current))
        helm_262D4C(0);
    hud_269000();
    if (game_359708 == 0x27 && hud_helmet)
        mfd_26F094(0);
}

void helm_262018(void)
{
    HudWidget *w;
    long i;

    if (hud_helmet && hud_eye.obj == game_396868) {
#ifndef TN_WATCOM_ABI
        port_s3d_overlay(NULL, 0, 0, 0, 0);     /* no frame over the view */
        port_ck_piece(-1, 0, NULL, 0, 0, 0, 0, 0);
#endif
        return;
    }
    if (hud_helmet) {
        gr_set_canvas(&tnui_back_canvas);
        helm_draw_frame(0x2EF0000);
    } else {
        gr_set_canvas(&hud_w_389DF4->canvas);
        helm_draw_frame(0x2F00000);
    }
    gr_set_canvas(&tnui_back_canvas);
    if (*(long *)(game_camera + 0x24))
        hudcb_264B7C();
    if (hud_38BA4E)
        hudcb_264CA4();
    if (hud_38BA50)
        hudcb_264DCC();
    hudcb_264E70();
    hudcb_265080();
    hudcb_264630();
    helm_2640D4();
    if (hud_helmet) {
        if (hud_flags_cam[2])
            hudcb_265158();
        if (hud_flags_cam[1])
            hudcb_265250();
        helm_264008(W(57));
        for (i = 36; i < 38; i++) {
            w = W(i);
            gr_set_canvas(&w->canvas);
            if (w->draw)
                WATCOM_CALL((void (*)())w->draw, (long)w);
        }
    } else {
        if (hud_flags_std[2])
            hudcb_265158();
        if (hud_flags_std[1])
            hudcb_265250();
    }
}

void helm_26214C(void)
{
    HudWidget *last = hud_last, *w;

    for (w = hud_first; w <= last; w++)
        w->flags |= 0x20;
    hud_last = last;
    mfddata_26773C();
}

void helm_262184(HudWidget *w, HudWidgetRec *rec, HudWidgetDef *def)
{
    w->x0 = rec->x0;
    w->y0 = rec->y0;
    w->x1 = rec->x1;
    w->y1 = rec->y1;
    if (rec->sten) {
        w->f10 = ResLock(rec->sten);
        helm_262244(w->f10, ResSize(rec->sten));
    } else
        w->f10 = 0;
    w->event = def->event;
    w->update = def->update;
    w->draw = def->draw;
    w->flags = def->flags;
    w->id = def->id;
    gr_init_sub_bitmap(&tnui_back_canvas.bm, &w->canvas.bm, rec->x0, rec->y0,
                       (short)(rec->x1 - rec->x0), (short)(rec->y1 - rec->y0));
    gr_init_gc(&w->canvas);
}

void helm_262244(grs_stencil *s, ulong size)
{
    grs_sten_elem *e;
    long n, i;

    s->elem = (grs_sten_elem *)((uchar *)s + (long)s->elem);
    e = s->elem;
    n = (long)((size - 8) >> 3);
    for (i = 0; i < n; i++, e++)
        if (e->n)
            e->n = (grs_sten_elem *)((uchar *)e->n + (long)s);
}

void helm_262278(void)
{
    HudWidget *first, *radar, *last, *main;

    if (hud_helmet) {
        if (hud_eye.obj == game_396868) {
            main = W(58);
            radar = W(59);
            last = W(68);
        } else {
            hud_w_389E10 = W(38);
            hud_w_389E04 = W(39);
            hud_w_389E08 = W(40);
            hud_w_389E1C = W(41);
            hud_w_389F34 = W(42);
            hud_w_389DDC = W(43);
            hud_w_389DF0 = W(44);
            hud_w_389DE0 = W(45);
            main = W(34);
            radar = W(35);
            last = W(57);
        }
    } else {
        hud_w_389E10 = W(2);
        hud_w_389E04 = W(3);
        hud_w_389E08 = W(4);
        hud_w_389E1C = W(5);
        hud_w_389F34 = W(6);
        hud_w_389DDC = W(30);
        hud_w_389DF0 = W(24);
        hud_w_389DE0 = W(25);
        {
            uchar **views = &game_389DF8;   /* HudWidget *[3] declared as one uchar * (g1_b.h) */
            __asm__("" : "+r"(views));      /* keep gcc's bounds check out of it */
            views[0] = (uchar *)W(9);
            views[1] = (uchar *)W(10);
            views[2] = (uchar *)W(11);
        }
        main = W(0);
        radar = W(1);
        hud_w_389E0C = W(7);
        last = W(33);
        hud_w_389E18 = W(8);
    }
    first = main;
    hud_scr_w = main->canvas.bm.w;
    hud_scr_h = main->canvas.bm.h;
    hud_first = first;
    hud_radar = radar;
    hud_last = last;
    hud_w_389DF4 = main;
}

uchar helm_2623E4(TnEvent *ev, void *data)
{
    ulong t;
    uchar f;

    if (menudraw_27BFBC(ev))
        return 1;
    t = ev->type;
    if (t == 1 || t == 2 || t == 4 || t == 8 || t == 0x40) {
        hud_tip_time = game_time;
        f = hud_tip_flags;
        if ((f & 1) && !(f & 2)) {
            gr_set_canvas(&tnui_back_canvas);
            if (!hud_helmet)
                helm_draw_frame(0x2EE0000);
            helm_mark_all();
            mfddata_26773C();
            hud_38B9AE = 1;
            hud_tip_flags &= ~1;
        }
    }
    if (tnui_mouse_handler(ev, data))
        return 1;
    if (game_paused)
        return 1;
    if (ev->type == 0x400) {
        hud_268D80(hud_w_389DF4, ev);
        return 1;
    }
    if (hud_drag_widget) {
        WATCOM_CALL((void (*)())hud_drag_widget->event, (long)hud_drag_widget, (long)ev);
        return 1;
    }
#ifndef TN_WATCOM_ABI
    /* widescreen: the pointer beside the screen is over the wide 3D view (the game has it at the
     * screen's edge): the main view gets the event, at the real position */
    if (port_wide_ptr && hud_w_389DF4 && hud_w_389DF4->event) {
        HudWidget *w = hud_w_389DF4;
        if ((ev->pt.x <= w->x0 || ev->pt.x >= w->x1 - 1) && ev->pt.y >= w->y0 && ev->pt.y < w->y1) {
            ev->pt.x = (short)port_wide_ptr_x;
            ev->pt.y = (short)port_wide_ptr_y;
            WATCOM_CALL((void (*)())w->event, (long)w, (long)ev);
            hud_35A548 = w;
            if (!hud_drag_widget && !(w->flags & 0x10))
                MouseSetCursor(0x1F90016);
            return 1;
        }
    }
#endif
    if (!hud_helmet || (hud_35AEF0 & 1)) {
        if (helm_2625E4(ev, hud_first, hud_last))
            return 1;
    } else if (hud_eye.obj == game_396868) {
        if (helm_2625E4(ev, W(60), W(68)))
            return 1;
        if (helm_2625E4(ev, W(58), W(59)))
            return 1;
    } else {
        if (helm_2625E4(ev, W(47), W(55)))
            return 1;
        if (helm_2625E4(ev, W(34), W(57)))
            return 1;
    }
    hud_35A548 = 0;
    if (!hud_drag_widget)
        MouseSetCursor(0x1F90016);
    return 1;
}

uchar helm_2625E4(TnEvent *ev, HudWidget *first, HudWidget *last)
{
    HudWidget *w;
    LGPoint p;

    for (w = first; w <= last; w++) {
        if (ev->pt.y < w->y0 || ev->pt.y >= w->y1 || ev->pt.x < w->x0 || ev->pt.x >= w->x1)
            continue;
        if (w->f10) {
            p.x = ev->pt.x - w->x0;
            p.y = ev->pt.y - w->y0;
            if (!cursor_sten_test(w->f10, (short)(w->y1 - w->y0), p))
                continue;
        }
        if (!w->event)
            continue;
        WATCOM_CALL((void (*)())w->event, (long)w, (long)ev);
        hud_35A548 = w;
        if (w != hud_w_389DF4)
            ffpipe_29E160((FfCamera *)game_camera);
        if (!hud_drag_widget && !(w->flags & 0x10))
            MouseSetCursor(0x1F90016);
        return 1;
    }
    return 0;
}

void helm_2626B8(void)
{
    MsgAddr a;
    Msg *m;
    uchar b4, b8, bc, i;
    ushort br;

    a.a = 0x80;
    a.b = ENTP(game_player_entity)->f21;
    while ((m = MsgReceive(&a)) != 0) {
        uchar t = ((uchar *)m)[2];
        if (t == 0x12) {
            aiacc_25ADF8(m, &b8, &b4);
            for (i = 0; i < 3; i++)
                if (SPK_VOICE(i) == BRAIN_B(b8, 9))
                    break;
            RadioSpeak((schar)i, (schar)b4);
        } else if (t == 0x16) {
            aiacc_25AC64(m, &bc);
            if (bc > 3)
                bc = 3;
            br = *(ushort *)((uchar *)m + 0xA);
            for (i = 0; i < 3; i++)
                if (BRAIN_B(br, 9) == SPK_VOICE(i))
                    break;
            RadioSpeak((schar)i, (schar)(bc + 2));
        }
        MsgFree(m);
    }
    a.a = 0x82;
    a.b = game_441354;
    while ((m = MsgReceive(&a)) != 0)
        MsgFree(m);
}

void helm_2627F8(long ent, long b)
{
    if ((short)ent == targ_current)
        targ_current = 0;
    mfddata_2664AC((short)ent);
}

void helm_262814(void)
{
    HudWidget *w;
    uchar f, old, now;

    if (hud_38B9AE || hud_helmet) {
        tnui_add_dirty_rect(&tnui_full_rect);
        tnui_task_60f4();
        tnui_task_6174();
        for (w = hud_first; w <= hud_last; w++)
            w->flags &= 0x7F;
        hud_38B9AE = 0;
    } else {
        tnm_lock++;
        if (tnm_no_hide) {
            if (tnm_cursor_drawn) {
                MouseSaveUnderAt(tnm_draw_pos, &tnui_back_canvas, &tnm_su);
                cursor_save_under(&tnm_cursor, tnm_draw_pos, &tnui_back_canvas, &tnm_su2);
            }
            if (tnm_dirty[0].lr.x) {
                RectSect(&tnm_dirty[0], &tnui_full_rect, &tnm_dirty[0]);
                GfxBlit(&tnm_dirty[0]);
                memset(&tnm_dirty[0], 0, 8);
            }
            if (tnm_dirty[1].lr.x) {
                RectSect(&tnm_dirty[1], &tnui_full_rect, &tnm_dirty[1]);
                GfxBlit(&tnm_dirty[1]);
                memset(&tnm_dirty[1], 0, 8);
            }
        } else
            MouseSaveHide();
        for (w = hud_first; w <= hud_last; w++) {
            if (!(w->flags & 0x80))
                continue;
            if (!tnm_no_hide && tnm_cursor_drawn)
                MouseHideRect((LGRect *)&w->x0);
            GfxBlit((LGRect *)&w->x0);
            w->flags &= 0x7F;
            if (tn_ticks >= periodic_next_due)
                periodic_run(tn_ticks);
        }
        mfddata_267870();
        f = hud_tip_flags;
        if ((f & 1) && !(f & 2))
            GfxBlit(&hud_tip_rect);
        if (menu_active)
            menudraw_27BB50();
        if (tnm_no_hide) {
            if (tnm_cursor_drawn)
                cursor_restore_under(&tnm_su2);
        } else
            MouseRestoreHide();
        tnm_lock--;
    }
    old = game_35ADE0;
    now = hud_26B8F8((old + 1) & 2);
    if (old != now) {
        gfx_2E421C();
        if (game_35ADE0 == 0)
            planet_299E04();
        else if (game_35ADE0 == 2) {
            grd_gamma_on = 1;
            gr_set_pal(0xB0, 0x37, planet_pal_part);
        }
    }
}

void helm_262A44(void)
{
    HudWidget *last, *w;

    tnui_add_dirty_rect(&tnui_full_rect);
    tnui_task_60f4();
    tnui_task_6174();
    last = hud_last;
    for (w = hud_first; w <= last; w++)
        w->flags &= 0x7F;
    hud_last = last;
}

static void helm_targ_drop(short ent)
{
    W(20)->flags |= 0x20;
    if (ent == targ_current) {
        targ_current = 0;
        if (hud_views[0].mode == 0x15)
            hudview_26C224(0, 1);
    }
}

void helm_262A88(long ent)
{
    helm_targ_drop((short)ent);
}

void helm_262AC4(long ent, long arg)
{
    helm_targ_drop((short)ent);
}

uchar helm_262B00(short ent)
{
    fix d;
    long r, i;

    if (!ent)
        return 0;
    d = fix_fast_pyth_dist(ENTP(ent)->x - ENTP(game_player_entity)->x,
                           ENTP(ent)->y - ENTP(game_player_entity)->y);
    r = *(long *)(PBA(game_player_slot) + 0xB6) / 6;
    if (d <= r)
        return 1;
    for (i = 0; i < targ_count; i++)
        if (targ_list[i].ent == ent)
            return 1;
    return 0;
}

void helm_262C2C(void)
{
    short best = 0, e, i;
    short me = IDX15(game_pba[game_player_entity].f000);
    fix px = ENTP(me)->x, py = ENTP(me)->y, d;
    long bestd = (long)0xFFFF0000;
    uchar flag;

    i = IDX15(game_pba[0].f000);
    flag = (ushort)game_pba[i].f000 >> 15;
    while (i) {
        if (!flag) {
            e = IDX15(game_pba[i].f000);
            if (e != game_player_entity) {
                d = fix_fast_pyth_dist(ENTP(e)->x - px, ENTP(e)->y - py);
                if (bestd > d) {          /* never: bestd starts negative */
                    bestd = d;
                    best = i;
                }
            }
        }
        i = game_pba[i].f002;
        flag = (ushort)game_pba[i].f000 >> 15;
    }
    if (best)
        helm_262D4C(best);
}

void helm_262D2C(void)
{
    if (!targ_current)
        return;
    if (helm_262B00(targ_current))
        return;
    helm_262D4C(0);
}

void helm_262D4C(short ent)
{
    uchar ok = damage_27DF14(game_player_entity, 5);

    if (targ_current)
        killobj_289588(targ_current, WATCOM_FN(helm_262AC4));
    if (!ok)
        return;
    targ_current = ent;
    if (!ent) {
        if (hud_views[0].mode == 0x15)
            hudview_26C224(0, 1);
        W(20)->flags |= 0x20;
        return;
    }
    if (helm_262B00(ent)) {
        pba_targ_lost_time = 0;
        killobj_289554(targ_current, WATCOM_FN(helm_262AC4), -1);
        SndPlay(0x7E5, 0, 0x40, 1, -1, 0x40, 0);
        W(20)->flags |= 0x20;
        return;
    }
    targ_current = 0;
}

void helm_262DB8(void)
{
    union { HudWidget *w; LGPoint p; } u;

    if (hud_drag_widget)
        return;
    if (hud_eye.obj != game_player_entity)
        return;
    if (hud_35A548 == hud_w_389DF4) {
        u.w = hud_35A548;
        hud_269F28(u.p);
        return;
    }
    if (hud_35A548 == hud_radar) {
        radar_266248((short)(((LGPoint *)tnm_state_ptr)->x - hud_radar->x0));
        return;
    }
    if (hud_35A548 == HUD_VIEW_W(0) || hud_35A548 == HUD_VIEW_W(1) || hud_35A548 == HUD_VIEW_W(2))
        hview3d_273ADC(hud_35A548);
}

long helm_262E34(void)
{
    GameEntity *e;
    long r;

    if (!targ_current)
        return 0;
    e = ENTP(targ_current);
    r = e->f23;
    if (e->f21)
        r |= (long)(schar)BRAIN_B(e->f21, 5) << 8;
    return r;
}

void helm_262E88(void)
{
    hud_text_log[0] = 0;
    script_text_cb = (ScriptFunc)WATCOM_FN(hud_268A08);
}

void helm_262EA0(void)
{
    uchar saved;
    long w, h, i;
    HudWidget *v;
    LGPoint size, max, c;

#ifndef TN_WATCOM_ABI
    /* G46: a tool tip still showing keeps its rectangle of the old resolution, and the next
     * frame copies it to the screen without clipping (helm_262814): below the 200 rows of the
     * low resolution that is outside the back canvas and the screen.  The tip goes with the
     * old screen. */
    hud_tip_flags &= ~1;
    hud_tip_time = game_time;
#endif
    if (game_35ADE0 == 2 || game_35ADE0 == 1) {
        hud_26B8F8(3);
        hud_35A958 = 1;
    }
    saved = hud_helmet;
    radar_265720();
    hview3d_2744D0();
    hudcb_2642D4();
    KeyRemoveHandler(WATCOM_FN(PlayKeyHandler), 0);
    MouseRemoveHandler(&tnui_full_rect, 0x4FF, WATCOM_FN(helm_2623E4));
    MouseSetCursor(0x15E0014);
    ResCloseFile(hud_res_file);
    GameTimePause();
    if (tnm_cursor_drawn)
        MouseHide();
    game_35A540 &= ~1;
    if (game_35A540 & 2) {
        ResCloseFile(hud_res_file2);
        GfxInit(2);
        helm_vd_alloc(0x1F400);
        hud_res_file = ResOpenResFile(hud_res_names_hi[hud_38B9AD], 0, 0);
        tnui_full_rect.lr.y = 0x190;
        tnui_full_rect.lr.x = 0x140;
        hud_res_file2 = ResOpenResFile("reshlmhi.res", 0, 0);
        for (i = 0; i < 3; i++) {
            v = HUD_VIEW_W(i);
            v->y1 += v->y1;
            v->flags |= 0xA0;
        }
        w = 0x140;
        h = 0x190;
        hud_35A544 = 1;
    } else {
        ResCloseFile(hud_res_file2);
        GfxInit(0);
        helm_vd_alloc(0xFA00);
        hud_res_file = ResOpenResFile(hud_res_names[hud_38B9AD], 0, 0);
        tnui_full_rect.lr.x = 0x140;
        tnui_full_rect.lr.y = 0xC8;
        hud_res_file2 = ResOpenResFile("reshelm.res", 0, 0);
        for (i = 0; i < 3; i++) {
            v = HUD_VIEW_W(i);
            v->y1 = v->y1 / 2;
            v->flags |= 0xA0;
        }
        w = 0x140;
        h = 0xC8;
        hud_35A544 = 0;
    }
    size = tnui_full_rect.lr;
    max.y = (short)((long)(ushort)(tnui_full_rect.lr.y << 5) / 200);
    max.x = (short)((long)(ushort)tnui_full_rect.lr.x / 10);
    tnmouse_set_screensize(size, max);
    c.x = (short)((tnui_full_rect.lr.x - tnui_full_rect.ul.x) / 2);
    c.y = (short)((tnui_full_rect.lr.y - tnui_full_rect.ul.y) / 2);
    MouseMoveTo(c);
    hud_eye_dz = 0;
    *(short *)(targ_view + 8) = (short)w;
    *(short *)(targ_view + 0xA) = (short)h;
    hud_helmet = 0;
    *(short *)(targ_view + 0x36) = (short)w;
    *(short *)(targ_view + 0x3A) = (short)h;
    helm_2618E0();
    helm_262278();
    helm_make_widgets();
    mfddata_266EAC();
    helm_read_flags();
    MouseSetCursor(0x1F90016);
    tnm_447117 = 1;
    MouseAddHandler(&tnui_full_rect, 0x4FF, WATCOM_FN(helm_2623E4));
    KeyAddHandler(WATCOM_FN(PlayKeyHandler), 0, 1, 0);
    GameTimeResume2();
    gr_set_canvas(&tnui_back_canvas);
    helm_draw_frame(0x2EE0000);
    helm_mark_all();
    mfddata_26773C();
    hud_38B9AE = 1;
    hudcb_2641C0();
    radar_265700();
    hview3d_274410();
    hud_26AC98(hud_helmet);
    if (saved)
        hud_26AC98(saved);
    if (game_camera_38FB20) {
        hud_26AF24();
        hud_26AE98();
    }
    hudview_26C224(0, hud_views[0].mode);
    hudview_26C224(1, hud_views[1].mode);
    hudview_26C224(2, hud_views[2].mode);
    hud_26B780(0);
    (hud_helmet ? W(43) : W(30))->flags |= 0x20;
    MouseSetCursor(0x1F90000);
    MouseShow();
}

long helm_2633A0(long mode)
{
    uchar f = game_35A540;
    long v = hud_35A914;

#ifndef TN_WATCOM_ABI
    /* G21: the options are applied whole (options_2CA1E4) at the start, on loading a game and on
     * each Accept, and a switch already asked for was undone by asking for it again: Freefall's
     * 320 x 400 from the start, then a loaded game, began the mission in 320 x 200 */
    if ((f & 1) && (mode == 2) == ((f & 2) != 0))
        return mode;
#endif
    if (f & 1) {
        f &= ~1;
        if (f & 2) {
            f &= ~2;
            v = 0;
        } else {
            v = 1;
            f |= 2;
        }
    } else if (mode == 1 && (f & 2)) {
        f |= 1;
        v = 0;
        f &= ~2;
    } else if (mode == 2 && !(f & 2)) {
        v = 1;
        f |= 3;
    }
    hud_35A914 = v;
    game_35A540 = f;
    return mode;
}

bool helm_263410(fix x, fix y)
{
    uchar *row = *(uchar **)(game_map_35BFB4 + (y >> 16) * 4 + 0x84E);
    uchar t = row[(x >> 16) * 3] & 0x3F;
    uchar k;

    game_44659C = t;
    if (game_41DA28[t >> 3] & (1 << (game_44659C & 7)))
        k = game_41DD58[t];
    else
        k = (uchar)physics_2D637C(x, y, t);
    return (terrain_bitmask & (1u << (k & 31))) != 0;
}

void helm_2634A4(void)
{
    long i;
    for (i = 0; i < 0x100; i++)
        hud_clut[i] = (uchar)i;
}

void helm_2634B8(long part, long step)
{
    uchar *p = &hud_clut[0xB0 + part * 16];
    uchar c = hud_suit_clr[step];
    long n, i;

    if (part == 4) {
        *p = hud_suit_clr[step];
        return;
    }
    n = 6 - step;
    for (i = 0; i < 6; i++)
        p[i] = i < n ? 0 : c;
}

long helm_26350C(short slot, long part)
{
    uchar *r = PBA(slot) + part * 7;
    long a = *(short *)(r + 0x15), b = *(short *)(r + 0x17);

    return (12 - (b - a) * 11 / b) / 2;
}

long helm_26355C(short slot)
{
    long a, b;

    if (slot == game_player_slot) {
        a = game_44136A;
        b = game_44136C;
    } else {
        a = *(ushort *)(PBA(slot) + 0x32);
        b = *(ushort *)(PBA(slot) + 0x34);
    }
    return (12 - (a - b) * 11 / a) / 2;
}

void helm_2635C0(HudWidget *w)
{
    w->flags |= 0x20;
}

void helm_2635C8(short slot, LGRect *r)
{
    grs_bitmap *bm, tmp;
    grs_canvas c;
    LGPoint pos;
    long d;

    bm = RefGet(0x1FA0000);
    BM_FIX(bm);
    tmp = *bm;
    tmp.bits = ffmem_2C4F40(bm->w * bm->h);
    helm_2634B8(0, helm_26350C(slot, 0));
    helm_2634B8(1, helm_26350C(slot, 1));
    helm_2634B8(2, helm_26350C(slot, 3));
    helm_2634B8(3, helm_26350C(slot, 2));
    helm_2634B8(4, helm_26355C(slot));
    gr_make_canvas(&tmp, &c);
    gr_push_canvas(&c);
    WC1(grd_canvas_table[8], 0);
    grd_canvas->gc.fill_type = FILL_CLUT;
    grd_function_table = (void (**)())((uchar *)grd_function_fill_table + 0x618);
    grd_uline_fill_vector = (void (**)())((uchar *)grd_uline_fill_table + 0x24);
    grd_canvas->gc.fill_parm = (long)hud_clut;
    BITMAP(bm, 0, 0);
    grd_canvas->gc.fill_type = 0;
    grd_function_table = grd_function_fill_table;
    grd_uline_fill_vector = grd_uline_fill_table;
    gr_pop_canvas();
    pos = r->ul;
    d = r->lr.x - r->ul.x;
    if (d > 0)
        pos.x = (short)(pos.x + (d - bm->w) / 2);
    d = r->lr.y - r->ul.y;
    if (d > 0)
        pos.y = (short)(pos.y + (d - bm->h) / 2);
    BITMAP(&tmp, pos.x, pos.y);
#ifndef TN_WATCOM_ABI
    port_s3d_hudbits(&tmp, pos.x, pos.y);       /* the display: all its pixels the HUD's */
#endif
    ffmem_2C4FE4(tmp.bits);
}

void helm_2637DC(HudWidget *w)
{
    LGRect r = helm_zero_rect;

    if (hud_helmet && !(hud_35AEF0 & 1))
        gr_set_canvas(&W(56)->canvas);
    helm_2635C8(game_player_slot, &r);
}

void helm_263824(void)
{
    LGPoint pt;
    HudWidget *w;
    long i, lim;

    if (game_time - hud_tip_time < 0x5DC)
        return;
    pt = *(LGPoint *)tnm_state_ptr;
    if (hud_helmet) {
        for (i = 0x23, w = &hud_pool[0x23];; i++, w++) {
            lim = (hud_35AEF0 & 1) ? 0x2E : 0x39;
            if (i > lim)
                break;
            if (i >= 0x2F && i <= 0x31)
                continue;
            if (!(hud_35AEF0 & 1) && i == 0x2E)
                continue;
            if ((hud_35AEF0 & 1) && i == 0x38)
                continue;
            if (RectTestPt((LGRect *)&w->x0, pt) && game_time - hud_tip_time > 0x5DC)
                helm_263940(i, &pt);
        }
    } else {
        for (i = 1, w = &hud_pool[1]; i <= 0x21; i++, w++) {
            if (i >= 9 && i <= 0xB)
                continue;
            if (RectTestPt((LGRect *)&w->x0, pt) && game_time - hud_tip_time > 0x5DC)
                helm_263940(i, &pt);
        }
    }
}

void helm_263940(long idx, LGPoint *pt)
{
    char buf[0x50];
    long x = pt->x, y = pt->y, w, h, ty, off;
    long l = 0, r = 0, t = 0, b = 0;
    grs_font *f = ResLock(0x1F7);
    char *s = RefGet(idx + 0x16E0084), *d = buf;

    while ((*d++ = *s++) != 0)
        ;
    gr_set_canvas(&tnui_back_canvas);
    w = gr_font_string_width(f, buf);
    h = gr_font_string_height(f, buf);
    off = HIRES ? 0x16 : 0xB;
    if (w / 2 + y + off > tnui_full_rect.lr.y)
        ty = y - off;
    else
        ty = y + off;
    if (x - w / 2 < 0)
        l = -(x - w / 2) + 2;
    if (x + w / 2 > tnui_full_rect.lr.x)
        r = x + w / 2 - tnui_full_rect.lr.x + 3;
    if (ty - h / 2 < 0)
        t = -(ty - h / 2);
    if (h / 2 + ty > tnui_full_rect.lr.y)
        b = h / 2 + ty - tnui_full_rect.lr.y;
    hud_tip_rect.ul.x = (short)(x - w / 2 - 2 + l - r);
    hud_tip_rect.ul.y = (short)(ty - h / 2 - 2 + t - b);
    hud_tip_rect.lr.x = (short)(w / 2 + x + 3 + l - r);
    hud_tip_rect.lr.y = (short)(h / 2 + ty + 3 + t - b);
    grd_canvas->gc.fcolor = 0;
    RECT_FILL(hud_tip_rect.ul.x, hud_tip_rect.ul.y, hud_tip_rect.lr.x, hud_tip_rect.lr.y);
    grd_canvas->gc.fcolor = 0x84;
    BOX(hud_tip_rect.ul.x, hud_tip_rect.ul.y, hud_tip_rect.lr.x, hud_tip_rect.lr.y);
    shell_2C292C(&hud_tip_rect, buf, 0x1F7, &tnui_back_canvas);
#ifndef TN_WATCOM_ABI
    port_s3d_tip(&hud_tip_rect);        /* widescreen: shown whole with the stretched screen */
#endif
    hud_tip_flags |= 1;
    ResUnlock(0x1F7);
}

long helm_263BC0(long mode)
{
    uchar f = hud_tip_flags;

    if (mode == 1)
        f &= ~2;
    else if (mode == 2)
        f |= 2;
    hud_tip_flags = f;
    return mode;
}

void helm_263BE4(void)
{
    long base, i;
    HudWidget *v;

    if (hud_helmet && !(hud_35AEF0 & 1))
        base = hud_eye.obj == game_396868 ? 60 : 47;
    else if (!hud_helmet)
        base = 9;
    else
        return;
    for (i = 0; i < 3; i++) {
        v = W(base + i);
        (&game_389DF8)[i] = (uchar *)v;
        /* (the original copies the widget's rectangle and canvas onto themselves here) */
        v->flags |= 0x20;
        *(grs_canvas **)(ff_cams_390010[i] + 0x1C) = &v->canvas;
        FfCameraSetup(ff_cams_390010[i], v->x1 - v->x0, v->y1 - v->y0, 0x10000);
    }
}

static uchar helm_btn_off(void)
{
    return game_359708 == 0x2E || game_359708 == 0x2F;
}

void helm_263EB8(HudWidget *w, HudEvent *ev)
{
    if (helm_btn_off() || ev->type != 1)
        return;
    hud_26AC98((uchar)ev->type);    /* eax still holds the event type (1): helmet off */
    hud_35A9A4 = game_time + 0x1F4;
}

void helm_263EE8(HudWidget *w)
{
    if (!helm_btn_off())
        w->flags |= 0x20;
}

static void helm_btn_draw(HudWidget *w, long t)
{
    grd_canvas->gc.fcolor = game_time > hud_35A9A4 ? 0x46 : 0x44;
    RECT_FILL(w->x0, w->y0, w->x1, w->y1);
    grd_canvas->gc.fcolor = hud_frame_clr[t];
    gen_hline(w->x0, (short)(w->y1 - 1), w->x1);
    gen_vline((short)(w->x1 - 1), w->y0, w->y1);
}

void helm_263F00(HudWidget *w)
{
    long t;

    if (helm_btn_off())
        return;
    t = hud_38B9AD;
    if (t > 3)
        t -= 4;
    helm_btn_draw(w, t);
}

void helm_263FC0(HudWidget *w, HudEvent *ev)
{
    if (helm_btn_off() || ev->type != 1)
        return;
    hud_26AC98(0);
    hud_35A9A4 = game_time + 0x1F4;
}

void helm_263FF0(HudWidget *w)
{
    if (!helm_btn_off())
        w->flags |= 0x20;
}

void helm_264008(HudWidget *w)
{
    long t;

    if (helm_btn_off())
        return;
    t = hud_38B9AD;
    if (t > 3)
        t -= 4;
    gr_set_canvas(&W(57)->canvas);
    helm_btn_draw(w, t);
}

void helm_2640D4(void)
{
    char buf[0xC];
    uchar *p = PBA(game_player_slot) + 6;
    schar k = *(schar *)(p + 0xC8);
    uchar *slot = p + 0x3F + k * 15;
    HudWidget *w = hud_helmet ? W(42) : W(6);
    grs_font *f;
    long tw, th;

    gr_set_canvas(&tnui_back_canvas);
    f = ResLock(0x1F7);
    grd_canvas->gc.fcolor = 0x22;
    if (*(ushort *)(WT(*slot) + 6) > 1) {
        lg_sprintf(buf, "%d", *(ushort *)(slot + 4));
        tw = gr_font_string_width(f, buf);
        th = gr_font_string_height(f, buf);
        GfxDrawText(0x1F7, buf, w->x1 - tw, w->y1 - th);
    }
    ResUnlock(0x1F7);
}
