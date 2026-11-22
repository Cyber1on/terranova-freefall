/* hudmain.c -- Terra Nova module M020 (0x267B50-0x26BB9F; r:\prj\tn\v109\tn\src\tn\hud.c ?):
 * the HUD main module.  Decompiled from __FF.EXE v1.09.
 *
 *   0x267B50 hud_name_init (data: 32 zero bytes)   0x267B70 HudInit
 *   0x267D68 hud_267D68 shutdown                   0x267DD4 hud_267DD4 think (pane)
 *   0x267EF8 hud_msg_add                           0x267F5C hud_msg_kill
 *   0x267FA8 hud_msg_tick                          0x268058 hud_layout_pos (no callers)
 *   0x268098 hud_268098 outlined text              0x26817C hud_msg_draw
 *   0x2683F8 hud_2683F8 banner on / off            0x268414 hud_268414 banner toggle
 *   0x26843C hud_26843C banner line present        0x268484 hud_banner_line
 *   0x2686EC hud_2686EC banner (3 lines)           0x2687C0 hud_radio_log_show
 *   0x2688A4 hud_2688A4 radio text                 0x2689A0 hud_2689A0 radio wait count
 *   0x2689E0 hud_radio_wake (no callers)           0x268A08 hud_268A08 radio text or log
 *   0x268A44 hud_268A44 radio message callback     0x268B30 hud_268B30 replay last radio
 *   0x268BD4 hud_268BD4 acknowledgement text       0x268C74 hud_268C74 ack callback
 *   0x268CFC hud_mouse_buttons (no callers)        0x268D80 hud_268D80 main pane mouse
 *   0x268E98 hud_268E98 fire / select weapon       0x268F08 hud_damage_shake (no callers)
 *   0x268F6C hud_shake_add                         0x268FD0 hud_268FD0 shake by distance
 *   0x269000 hud_269000 calm the shake             0x269028 hud_269028 draw the main view
 *   0x26934C hud_noise_draw                        0x269410 hud_text_box (no callers)
 *   0x269480 hud_cam_reset                         0x2694F4 hud_eye_update
 *   0x2695A0 hud_eye_player                        0x269838 hud_eye_from_rec (no callers)
 *   0x2698A4 hud_draw_fps (no callers)             0x269994 hud_draw_speed (no callers)
 *   0x269A38 hud_draw_palette                      0x269BA0 hud_drive (no callers)
 *   0x269D2C hud_aim_centered (no callers)         0x269D34 hud_aim_point
 *   0x269DBC hud_aim_on_target                     0x269E7C hud_reticle
 *   0x269F28 hud_269F28 pick a target              0x26A11C hud_26A11C add a target rect
 *   0x26A188 hud_26A188 target callback            0x26A380 hud_bracket
 *   0x26A4F4 hud_status_ticks                      0x26A6D4 hud_26A6D4 target name
 *   0x26A7C8 hud_target_label                      0x26AC98 hud_26AC98 helmet on / off
 *   0x26AD2C hud_helmet_set                        0x26AE98 hud_26AE98 open the map
 *   0x26AF24 hud_26AF24 close the map              0x26AFB0 hud_26AFB0 map zoom
 *   0x26B064 hud_map_mouse                         0x26B474 hud_map_draw
 *   0x26B4B0 hud_map_legend                        0x26B734 hud_cam_setup
 *   0x26B780 hud_26B780 set zoom                   0x26B800 hud_26B800 zoom step
 *   0x26B8F8 hud_26B8F8 palette mode               0x26B970 hud_26B970 palette toggle
 *   0x26B9D8 HudGetCompatibleView                  0x26BA18 hud_26BA18 back to the player
 *   0x26BA6C hud_26BA6C set the view               0x26BAFC hud_dbg_line (no callers)
 *   0x26BB5C hud_26BB5C out-of-area flag           0x26BB70 HudViewReset
 *   0x26BB98 hud_26BB98 zoom reset value
 *
 * Messages: hud_msgs is a priority list (LlistAddQueue) of HudMsg nodes made from templates.
 * A tick (hud_msg_tick, from the think) counts down their time and kills the expired ones;
 * killed nodes are only marked (HUDMSG_DEAD) and freed at the end of the tick.  Flags pick a
 * callback for ticks, drawing and death (event {0x1000, code 1 / 2 / 4}).  Radio texts and
 * acknowledgements queue behind each other with HUDMSG_HIDDEN: the death callbacks show the
 * next one.
 *
 * Quirks kept:
 *  - hud_radio_log_show / hud_2688A4 set the template's hires y and then overwrite the whole
 *    position with the layout table entry, so the hires y never shows.
 *  - hud_268A08 appends to hud_text_log with strutil_2DBCE0(end, s, 0x400 - len): no overflow
 *    check beyond that.
 *  - hud_noise_draw steps x by the bitmap height, not its width.
 *  - hud_mouse_buttons computes an x position it never uses.
 *  - hud_map_mouse calls hudview_26CAF0 even when the click is below the four legend rows.
 *  - hud_drive and hud_draw_speed hand the entity's body byte to EDMS as the handle (no
 *    game_body_to_phys lookup).
 */
#include "g7_a.h"
#include "g7_b.h"     /* added at the G5-G8 merge */
#include "g8_b.h"     /* added at the G5-G8 merge */
#include "g8_c.h"     /* added at the G5-G8 merge */

#ifndef TN_WATCOM_ABI
#include "sharp3d.h"                    /* port/video/sharp3d.c: also widescreen's mouse beside the screen */
#include "helmetfx.h"                   /* port/mods/helmetfx.c: the layered helmet's moves */
extern int port_mouselook;              /* port/ui/options.c: Advanced controls */
int port_aim_box_now(void);             /* port/ui/options.c: "Auto-aim" box, pixels each way (20 the game's, 0 Off) */
extern int port_aim_sign;               /* port/ui/options.c: "Auto-aim highlight" On / Off */
void port_hud_aim_sign(void);
#endif

#define HUD_CAM ((FfCamera *)game_camera)
#define MAP ((uchar *)game_camera_38FB20)
#define HIRES ((game_35A540 >> 1) != 0)
#define HENT(i) (&game_entities[(short)(i)])
#define PBA_PLAYER ((uchar *)&game_pba[game_player_slot])
#define MSG_FIRST ((HudMsg *)hud_msgs.head.pnext)
#define MSG_END ((HudMsg *)&hud_msgs.tail)
#define MSG_NEXT(m) ((HudMsg *)(m)->q.pnext)
#define D32(p, o) (*(long *)((uchar *)(p) + (o)))
#define S16(p, o) (*(short *)((uchar *)(p) + (o)))
#define EYE_ENT (*(short *)hud_ffview.g_5a)

typedef void (*HudMsgCb)(long m, long ev);
typedef void (*CanvasFn)();

static void msg_call(HudMsg *m, short code)
{
    HudMsgEv ev;
    ev.type = 0x1000;
    ev.code = code;
    WATCOM_CALL((HudMsgCb)m->cb, (long)m, (long)&ev);
}

static void fill_type_0(void)
{
    grd_canvas->gc.fill_type = 0;
    grd_function_table = grd_function_fill_table;
    grd_uline_fill_vector = grd_uline_fill_table;
}

static void fill_type_1(void)
{
    grd_canvas->gc.fill_type = 1;
    grd_function_table = grd_function_fill_table + 390;
    grd_uline_fill_vector = grd_uline_fill_table + 9;
}

/* Watcom inlined strcpy / strcat */
static char *s_strcpy(char *d, const char *s)
{
    char *p = d;
    while ((*p++ = *s++) != 0)
        ;
    return d;
}

static char *s_strcat(char *d, const char *s)
{
    s_strcpy(d + strlen(d), s);
    return d;
}

static long iabs(long a)                 /* cdq / xor / sub (INT_MIN stays negative) */
{
    long s = a >> 31;
    return (a ^ s) - s;
}

/* The HUD layout table (res 0x2FE, one per helmet state): LGPoint per text kind. */
static long layout(long idx)
{
    Ref r = 0x2FE0000 | (hud_helmet != 0);
    long v = ((long *)RefLock(r))[idx];
    ResUnlock(0x2FE);
    return v;
}

static void cam_setup(void)
{
    HudWidget *w = hud_w_389DF4;
    FfCameraSetup(game_camera, w->canvas.bm.w, w->canvas.bm.h, cam_view_tab[hud_eye.zoom]);
    if (game_35CADA != 0xFF)
        weather_2AFE38(hud_weather, HUD_CAM);
}

static void damage_shake(void)
{
    dmgdisp_27AAC0((short)game_441367);
#ifndef TN_WATCOM_ABI
    if (game_441367 != 0)
        port_helmet_hit((short)game_441367);
#endif
    if (game_441367 != 0 && RndRange(&game_rnd_35B704, 0, 0x32) < (short)game_441367 + 0x32)
        hud_shake_add((short)game_441367 * 6);
    game_441367 = 0;
}

/* map centre from the pane size */
static void map_centre(HudWidget *w)
{
    uchar *map = MAP;
    hud_map_x = (w->canvas.bm.w / 2) * D32(map, 0xA) + D32(map, 0);
    hud_map_y = ((w->canvas.bm.h / 2) >> (hud_35A544 & 31)) * D32(map, 0xA) + D32(map, 4);
}

/* ------------------------------------------------------------------ init / shutdown / think */

void HudInit(void)
{
    LlistInit(&hud_msgs, 0x25, 0x10);
    hud_last_time = game_time;
    hud_out_of_area = 0;
    hud_msg_dead = 0;
    hud_msg_target = hud_msg_add(&hud_tmpl_target);
    hud_zoom_player = 0;
    hud_zoom_other = 0;
    damage_27E1E0((short *)&hud_eye, game_player_entity, 0, 0, (long)&cursctrl_player);
    hud_turn_b = 0;
    hud_move_b = 0;
    hud_move_a = 0;
    hud_turn_a = 0;
    gr_init_bitmap(&terr_tex_bm, planet_res30, 2, 0, 0x40, 0x40);
    if (game_35CD9C != 0 && (hud_442F1A & 2))
        memcpy(hud_sky_name, hud_str_sky0s, 10);
    hud_sky_file = ResOpenResFile(hud_sky_name, 0, 0);
    if (hud_sky_file < 0)
        DbgReportError(1, hud_str_no_sky, hud_sky_name);
    hud_sky = rndsky_2A7AEC(0x99);
    hud_cam_reset();
    hud_weather = weather_2AFF54(HUD_CAM, 1);
    weather_2AFD4C((signed char)(hud_44244B - 1), hud_44244C);
    ResExtract(0x96, planet_tab37);
    ResExtract(0x97, planet_tab38);
    game_camera_38FB20 = 0;
    if (D32(game_4413D0, 0) != 0) {
        hud_map_x = D32(game_4413D0, 0);
        hud_map_y = D32(game_4413D0, 4);
    } else {
        hud_map_x = 0x1000000;
        hud_map_y = 0x1000000;
    }
    hud_map_bm[0].bits = hud_map_bits;
    hud_map_bm[1].bits = hud_map_bits;
    hud_map_pos.x = 0x1E;
    hud_map_pos.y = 0x1E;
    hud_map_grab = 0;
    hud_map_scale = 4;
    hud_text[0] = 0;
    hud_map_drag = 0;
    dmgdisp_27AA80();
    game_35ADE0 = 0;
    lg_memset32((ulong *)hud_fill_tab, 0, 0, 4);
    hud_map_layers = 8;
    hud_banner_on = 0;
}

void hud_267D68(void)
{
    HudMsg *m;
    MouseClearStencil();
    rndsky_2A7BA4(0x99);
    ResCloseFile(hud_sky_file);
    weather_2AFFFC(hud_weather);
    for (m = MSG_FIRST; m != MSG_END; m = MSG_NEXT(m))
        hud_msg_kill(m);
    LlistDestroy(&hud_msgs);
    if (game_camera_38FB20 != 0) {
        mapdisp_276BB4(MAP);
        game_camera_38FB20 = 0;
    }
}

void hud_267DD4(HudWidget *pane)
{
    ulong now = game_time;
    long dt = now - hud_last_time;
    short *ms;
    long dx, dy;
    ushort fl;
    if (dt == 0)
        return;
    ms = (short *)tnm_state_ptr;
    dx = (short)(ms[0] - pane->x0);
    dy = (short)(ms[1] - pane->y0);
    fl = HUD_CAM->flags;
#ifndef TN_WATCOM_ABI
    if (port_wide_ptr)                  /* widescreen: the pointer is beside the screen */
        dx = dy = -1;
#endif
    ffpipe_29E120(HUD_CAM, dx >> (fl & 1), dy >> ((fl >> 1) & 1));
    hud_msg_tick(dt);
    if (hud_2689A0())
        ScriptWake(ScriptFind(game_36F49C), 0x103, 0);
    if (game_35CADA != 0xFF)
        weather_2AF4E0(&hud_ffview, hud_weather);
    if (hud_eye.obj == game_player_entity && (PBA_PLAYER[0xCF] & 2)) {
        if (hud_35A548 == hud_w_389DF4)
            hud_reticle();
        else if (game_camera_38FB20 == 0)
            weapons_290858();
#ifndef TN_WATCOM_ABI
        {
            void port_gun_quiet(void);  /* weapons.c: G38, no laser sound without a shot */
            port_gun_quiet();
        }
#endif
    }
    hud_last_time = now;
    pane->flags |= 0x20;
    hud_w_389DF4->flags |= 0x80;
}

/* ------------------------------------------------------------------ messages */

HudMsg *hud_msg_add(HudMsgTmpl *t)
{
    HudMsg *m = LlistAddQueue(&hud_msgs, t->prior);
    m->id = t->id;
    m->flags = t->flags;
    m->color = t->color;
    m->pos = t->pos;
    m->text = t->text ? strpool_dup(t->text) : t->text;
    m->cb = t->cb;
    m->f21 = 0;
    m->time = t->time;
    return m;
}

void hud_msg_kill(HudMsg *m)
{
    if (m->flags & HUDMSG_DEAD)
        return;
    if (m->flags & HUDMSG_KILLCB)
        msg_call(m, 4);
    if (m->text)
        strpool_free(m->text);
    m->flags |= HUDMSG_DEAD;
    hud_msg_dead++;
}

void hud_msg_tick(long dt)
{
    HudMsg *m, *next;
    for (m = MSG_FIRST; m != MSG_END; m = MSG_NEXT(m)) {
        if ((ushort)m->flags & (HUDMSG_DEAD | HUDMSG_HIDDEN))
            continue;
        if (m->flags & HUDMSG_TICKCB) {
            msg_call(m, 1);
            if (m->flags & HUDMSG_DEAD)
                continue;
        }
        if (m->time != 0) {
            m->time -= dt;
            if (m->time <= 0)
                hud_msg_kill(m);
        }
    }
    if (hud_msg_dead != 0) {
        for (m = MSG_FIRST; m != MSG_END; m = next) {
            next = MSG_NEXT(m);
            if (m->flags & HUDMSG_DEAD)
                LlistFree(&hud_msgs, m);
        }
        hud_msg_dead = 0;
    }
}

void hud_layout_pos(char idx, long *out)
{
    *out = layout(idx);
}

/* Text with a one-pixel black outline (8 copies in colour 0, then the text in colour). */
void hud_268098(long font, void *text, long x, long y, long color)
{
    ushort f = (ushort)font;
    long sx = (short)x, sy = (short)y;
    grd_canvas->gc.fcolor = 0;
    GfxDrawText(f, text, sx - 1, sy - 1);
    GfxDrawText(f, text, sx, sy - 1);
    GfxDrawText(f, text, sx + 1, sy - 1);
    GfxDrawText(f, text, sx - 1, sy);
    GfxDrawText(f, text, sx + 1, sy);
    GfxDrawText(f, text, sx - 1, sy + 1);
    GfxDrawText(f, text, sx, sy + 1);
    GfxDrawText(f, text, sx + 1, sy + 1);
    grd_canvas->gc.fcolor = color;
    GfxDrawText(f, text, sx, sy);
}

void hud_msg_draw(void)
{
    HudMsg *m;
    long v;
    LGPoint p0, p1;
    for (m = MSG_FIRST; m != MSG_END; m = MSG_NEXT(m)) {
        ushort font;
        long y;
        if (m->flags & HUDMSG_HIDDEN)
            continue;
        grd_canvas->gc.fcolor = m->color;
        if (m->flags & HUDMSG_DRAWCB) {
            msg_call(m, 2);
            continue;
        }
        y = HIRES ? m->pos.y * 2 : m->pos.y;
        font = hud_fonts[(m->flags & HUDMSG_FONT) >> 11];
        if (m->flags & HUDMSG_OUTLINE)
            hud_268098(font, m->text, m->pos.x, (short)y, m->color);
        else
            GfxDrawText(font, m->text, m->pos.x, y);
    }
    v = layout(0);
    memcpy(&p0, &v, 4);
    v = layout(1);
    memcpy(&p1, &v, 4);
    if (hud_banner_on) {
        signed char n = 0;
        long k;
        for (k = 0; k < 3; k++)
            if (strlen(RefGet(0x1750000 + game_359708 * 3 + k)) >= 2)
                n++;
        p1.y += (2 - n) * 15;
        p0.y += (2 - n) * 15;
        hud_2686EC((signed char)game_359708, p0, p1, 0x1F7, 0x1D, 0x84, 0x44);
        if (game_time > hud_banner_until)
            hud_banner_on = 0;
    }
    if (hud_pal_debug != 0)
        hud_draw_palette();
}

/* ------------------------------------------------------------------ mission banner */

void hud_2683F8(long a)
{
    hud_banner_on = (uchar)a;
    if (a != 0)
        hud_banner_until = game_time + 4000;
}

void hud_268414(void)
{
    hud_banner_on = hud_banner_on == 0;
    if (hud_banner_on)
        hud_banner_until = game_time + 4000;
}

uchar hud_26843C(long a, long b)
{
    return strlen(RefGet(0x1750000 + a * 3 + b - 1)) >= 2;
}

/* One banner line: "<label><objective>" wrapped left of the right-aligned state text
 * (state 1 done: col_done, 2..4 other states, else the "unknown" string).  Returns the height
 * used: h * (lines + 1). */
long hud_banner_line(char line, char mission, LGPoint *pos, long x1, char state, ushort font,
                     long col, long col_done, long col_state)
{
    char b0[0x78], b78[0x28];
    long color = col_state;
    Ref ref;
    short w, h;
    int n;
    if (strlen(RefGet(0x1750000 + mission * 3 + line - 1)) < 2)
        return 0;
    switch ((uchar)(state - 1)) {
    case 0: color = col_done; ref = 0x172000D; break;
    case 1: ref = 0x172000E; break;
    case 2: ref = 0x1720010; break;     /* (the jump table has 3 and 4 this way round) */
    case 3: ref = 0x172000F; break;
    default: ref = 0x1710036; break;
    }
    s_strcpy(b78, RefGet(ref));
    w = gr_font_string_width(ResGet(font), b78);
    h = gr_font_string_height(ResGet(font), b78);
    s_strcpy(b0, RefGet(0x1720009 + line));
    s_strcat(b0, RefGet(0x1750000 + mission * 3 + line - 1));
    n = gr_font_string_wrap(ResGet(font), b0, (short)(x1 - pos->x - (w + 4)));
    hud_268098(font, b0, pos->x, pos->y, col);
    hud_268098(font, b78, (short)(x1 - w), (short)((n - 1) * h + pos->y), color);
    return h * (short)(n + 1);
}

short hud_2686EC(long mission, LGPoint p0, LGPoint p1, long font, long col, long col_done,
                 long col_state)
{
    signed char m = (signed char)mission;
    ushort f = (ushort)font;
    p0.y += hud_banner_line(1, m, &p0, p1.x, (signed char)ordn_mission_state[0x809], f, col,
                            col_done, col_state);
    p0.y += hud_banner_line(2, m, &p0, p1.x, (signed char)ordn_mission_state[0x80A], f, col,
                            col_done, col_state);
    p0.y += hud_banner_line(3, m, &p0, p1.x, (signed char)ordn_mission_state[0x80B], f, col,
                            col_done, col_state);
    return p0.y;
}

/* ------------------------------------------------------------------ radio / ack text */

HudMsg *hud_radio_log_show(char *s)
{
    HudMsg *m;
    long v;
    if (HIRES)
        hud_tmpl_radio_log.pos.y = 0x28;     /* overwritten just below (quirk) */
    hud_tmpl_radio_log.text = s;
    v = layout(3);
    memcpy(&hud_tmpl_radio_log.pos, &v, 4);
    m = hud_msg_add(&hud_tmpl_radio_log);
    m->time = (long)strlen(s) * 80 + 1000;
    gr_font_string_wrap(ResGet(hud_fonts[(m->flags & HUDMSG_FONT) >> 11]), m->text, 0xF0);
    if (hud_radio_count != 0) {
        m->flags |= HUDMSG_HIDDEN;
        m->time += 1500;
    }
    hud_radio_count++;
    return m;
}

void hud_2688A4(char *s)
{
    HudMsg *m, *n;
    long v;
    if (HIRES)
        hud_tmpl_radio.pos.y = 0x20;         /* overwritten just below (quirk) */
    hud_tmpl_radio.text = s;
    v = layout(2);
    memcpy(&hud_tmpl_radio.pos, &v, 4);
    m = hud_msg_add(&hud_tmpl_radio);
    m->time = (long)strlen(s) * 140;
    gr_font_string_wrap(ResGet(hud_fonts[(m->flags & HUDMSG_FONT) >> 11]), m->text, 0xF0);
    if (++hud_radio_count > 1) {
        m->flags |= HUDMSG_HIDDEN;
        for (n = MSG_FIRST; n != MSG_END; n = MSG_NEXT(n))
            if ((n->flags & HUDMSG_RADIO) && !(n->flags & HUDMSG_HIDDEN) && n != m) {
                hud_msg_kill(n);
                break;
            }
    }
}

long hud_2689A0(void)
{
    short c = hud_radio_wait;
    long r;
    if (hud_38FC80 != 0 || hud_radio_count != 0) {
        c = 4;
        r = 0;
    } else {
        if (c != 0)
            c--;
        r = c == 0;
    }
    hud_radio_wait = c;
    return r;
}

void hud_radio_wake(void)
{
    if (hud_2689A0())
        ScriptWake(ScriptFind(game_36F49C), 0x103, 0);
}

void hud_268A08(short a, char *s)
{
    long len = strlen(hud_text_log);
    if (a == 0) {
        strutil_2DBCE0(hud_text_log + len, s, 0x400 - len);
        return;
    }
    hud_2688A4(s);
}

void hud_268A44(HudMsg *m, HudMsgEv *ev)
{
    HudMsg *n;
    ushort code = ev->code;
    if (code == 4) {
        if (--hud_radio_count == 0) {
            s_strcpy(hud_text, m->text);
            gr_font_string_unwrap(hud_text);
            return;
        }
        for (n = MSG_FIRST; n != MSG_END; n = MSG_NEXT(n))
            if ((n->flags & (HUDMSG_ACK20 | HUDMSG_HIDDEN)) == (HUDMSG_ACK20 | HUDMSG_HIDDEN)) {
                n->flags &= ~HUDMSG_HIDDEN;
                return;
            }
        for (n = MSG_FIRST; n != MSG_END; n = MSG_NEXT(n))
            if ((n->flags & (HUDMSG_RADIO | HUDMSG_HIDDEN)) == (HUDMSG_RADIO | HUDMSG_HIDDEN)) {
                n->flags &= ~HUDMSG_HIDDEN;
                return;
            }
        return;
    }
    if (code == 1) {
        if ((m->flags & HUDMSG_ACK20) && hud_38FC80 != 0) {
            m->time = 2000;
            return;
        }
        if (hud_radio_count > 1 && m->time < 2000)
            m->time = -1;
    }
}

void hud_268B30(void)
{
    HudMsg *n;
    if ((radio_msg_flags & 1) && radio_msg_cur == -1 && radio_msg_last != -1
        && (radio_msg_last & 0x1000)) {
        radio_msg_play((short)radio_msg_last);
        return;
    }
    if (radio_msg_flags & 2) {
        if (hud_radio_count != 0) {
            for (n = MSG_FIRST; n != MSG_END; n = MSG_NEXT(n))
                if (!(n->flags & HUDMSG_HIDDEN) && !(n->flags & HUDMSG_DRAWCB))
                    hud_msg_kill(n);
            return;
        }
        if (strlen(hud_text) != 0)
            hud_radio_log_show(hud_text);
    }
}

void hud_268BD4(char *s, long t)
{
    HudMsg *m;
    long v;
    hud_tmpl_ack.text = s;
    hud_tmpl_ack.prior = (short)t;
    v = layout(4);
    memcpy(&hud_tmpl_ack.pos, &v, 4);
    m = hud_msg_add(&hud_tmpl_ack);
    gr_font_string_wrap(ResGet(hud_fonts[(m->flags & HUDMSG_FONT) >> 11]), m->text, 0x5A);
    if (hud_ack_count != 0)
        m->flags |= HUDMSG_HIDDEN;
    hud_ack_count++;
}

void hud_268C74(HudMsg *m, HudMsgEv *ev)
{
    HudMsg *n;
    uchar shown = 0;
    if (ev->code != 4)
        return;
    if (!(m->flags & HUDMSG_HIDDEN) && hud_ack_count > 1) {
        for (n = MSG_FIRST; n != MSG_END; n = MSG_NEXT(n)) {
            if ((n->flags & (HUDMSG_ACK40 | HUDMSG_HIDDEN)) != (HUDMSG_ACK40 | HUDMSG_HIDDEN))
                continue;
            if (strcmp(m->text, n->text) == 0)
                hud_msg_kill(n);
            else if (!shown) {
                n->flags &= ~HUDMSG_HIDDEN;
                shown = 1;
            }
        }
    }
    hud_ack_count--;
}

/* ------------------------------------------------------------------ mouse / weapons */

long hud_mouse_buttons(long a, long b, long buttons)
{
    short w, h;
    long x;
    if ((hud_buttons ^ buttons) & buttons & 2)
        hud_268E98();
    w = hud_scr_w;
    hud_buttons = buttons;
    h = hud_scr_h;
    x = w / 2 + (((a >> 4) * w) >> 12);      /* computed, never used (quirk) */
    (void)x;
    return h / 2 + (((b >> 4) * h) >> 12);
}

void hud_268D80(HudWidget *pane, TnEvent *ev)
{
    LGPoint d;
    uchar *rs;
    if (game_camera_38FB20 != 0) {
        hud_map_mouse(pane, ev);
        return;
    }
    d.x = ev->pt.x - pane->x0;
    d.y = ev->pt.y - pane->y0;
    switch (ev->type) {
    case 1:
        if (hud_389BC2 != 0 && hview3d_274B00(game_camera, -1, d, 0))
            return;
        hud_268E98();
        return;
    case 2:
        if (PBA_PLAYER[0xCF] & 2)
            weapons_290858();
        return;
    case 4:
        if (hud_eye.obj == game_396868)
            return;
        hud_269F28(d);
        rs = tn_replay_state_ptr;
        if ((rs[3] & 0x20) || (rs[0x13] & 0x20))
            cmd_261370(hud_cmd_35AE90);
        return;
    case 0x40:
    case 0x80:
        MouseSetCursor(0x1F90000);
#ifndef TN_WATCOM_ABI
        port_hud_aim_sign();            /* "Auto-aim highlight" over it again */
#endif
        return;
    }
}

void hud_268E98(void)
{
    uchar *p;
    signed char idx;
    if (hud_eye.obj != game_player_entity)
        return;
    p = PBA_PLAYER;
    idx = p[0xCE];
    if (ord_table[p[0x45 + idx * 15]].g0[0] <= 2) {
        weapons_290858();
        p[0xCF] |= 2;
    }
}

/* ------------------------------------------------------------------ view shake */

void hud_damage_shake(void)
{
    damage_shake();
}

void hud_shake_add(long amt)
{
    long m = hud_shake_max, t;
    long v = amt >> ((HENT(game_player_entity)->type - 14) & 31);
    if (m != 0 || v >= 0x200) {
        t = m / 2 + v;
        if (v > m)
            m = v;
        if (t > m)
            m = t;
        v = m;
    }
    hud_shake = v;
    hud_shake_max = m;
}

void hud_268FD0(long dist, long b)
{
    hud_shake_add(dist >= b ? 0x200 : 0x800 - dist * 0x600 / b);
}

void hud_269000(void)
{
    long m = hud_shake_max > 0x40 ? hud_shake_max - 0x40 : 0;
    hud_shake = 0;
    hud_shake_max = m;
}

/* ------------------------------------------------------------------ main view */

void hud_269028(void)
{
#ifndef TN_WATCOM_ABI
    port_s3d_view_drawn(0);             /* port/video/sharp3d.c */
    port_s3d_stretch = 1;               /* widescreen: the mission's screen, stretched */
    if (hud_w_389DF4)
        port_s3d_window(hud_w_389DF4->f10, hud_w_389DF4->x0, hud_w_389DF4->y0, hud_w_389DF4->x1,
                        hud_w_389DF4->y1);
#endif
    damage_shake();
    if (game_camera_38FB20 != 0) {
        hud_map_draw();
        hudutil_27B5E4(0x1780001);
        return;
    }
    game_camera[0x2E] &= ~4;
    D32(game_camera, 0x24) = (long)hud_fill_tab[0];
    if (hud_fill_tab[0] != 0) {
        if (game_camera[0x2C] == 3 || game_camera[0x2C] == 4)
            game_camera[0x2E] |= 4;
        else {
            fill_type_1();
            grd_canvas->gc.fill_parm = D32(game_camera, 0x24);
        }
    } else
        fill_type_0();
    hud_eye_update();
    ffpipe_41EB60 = hud_weather;
    if (game_396868 == hud_eye.obj) {
        uchar a = damage_27DF14(game_player_entity, 7);
        uchar noise = 0;
        if (a < 0x3C)
            noise = RndRange(&game_rnd_35B704, 0, 200) < 0x3C - a;
        if (noise)
            hud_noise_draw();
        else {
            ffpipe_29EDB4(&hud_ffview, HUD_CAM);
#ifndef TN_WATCOM_ABI
            port_s3d_render(0, &hud_ffview, HUD_CAM, 0);   /* the sharp 3D view (port/video/sharp3d.c) */
#endif
        }
    } else if (dmgdisp_27A0D0())
        dmgdisp_27A3FC(&hud_ffview, game_camera);
    else {
        ffpipe_29EDB4(&hud_ffview, HUD_CAM);
#ifndef TN_WATCOM_ABI
        port_s3d_render(0, &hud_ffview, HUD_CAM, 0);
#endif
    }
    memcpy(game_camera + 0xA9, (uchar *)&ff_cam + 0xA9, 0x11);
    fill_type_0();
    dmgdisp_27A618();
    hud_msg_draw();
    if (hud_eye.obj == game_396868) {
        long x, y;
        grd_canvas->gc.fcolor = 0x44;
        if (game_359708 == 0x27 && hud_helmet == 0) {
            y = HIRES ? 0x30 : 0x18;
            x = 0x69;
        } else {
            y = HIRES ? 0x1C : 0xC;
            x = 0x78;
        }
        GfxDrawText(0x1F8, RefGet(0x1720003), x, y);
        hudutil_27B5E4(0x1780002);
    } else
        hudutil_27B5E4(0x1780000);
    if (hud_out_of_area != 0) {
        grd_canvas->gc.fcolor = 0x4E;
        GfxDrawText(0x169, RefGet(0x1720004), 0x50, 0x1E);
    } else if (pbasim_28A9B8(PBA_PLAYER + 6)) {
        pbasim_289B3C();
        pbasim_289B7C();
        game_35B77C = 1;
    }
    if (game_359708 == 0x2E || game_359708 == 0x2F)
        hudutil_27B5B0(0x1780020, 0xA8, 2);
}

/* Static over the whole pane (the remote camera's signal breaking up). */
void hud_noise_draw(void)
{
    grs_bitmap *bm = RefLock(0x23D0000);
    long x0, x, y;
    bm->bits = (uchar *)bm + 0x1C;
    x0 = RndRange(&hud_noise_rnd, 0, bm->w);
    y = -RndRange(&hud_noise_rnd, 0, bm->h);
    while (hud_w_389DF4->canvas.bm.h > y) {
        for (x = -x0; x < hud_w_389DF4->canvas.bm.w; x += bm->h)    /* h, not w (quirk) */
            WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_UBITMAP + 1 + 2 * bm->type],
                        (long)bm, (short)x, (short)y);
        y += bm->h;
    }
    ResUnlock(0x23D);
}

void hud_text_box(char *text, ushort font, uchar fg, uchar bg, short *r)
{
    grd_canvas->gc.fcolor = bg;
    WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_URECT], r[0], r[1], r[2], r[3]);
    grd_canvas->gc.fcolor = fg;
    GfxDrawText(font, text, r[0] + 4, r[1] + 2);
}

void hud_cam_reset(void)
{
    HUD_CAM->sky = hud_sky;
    sky_ref_w = hud_scr_w;
    targ_view = (uchar *)&hud_w_389DF4->canvas;
    D32(game_camera, 0x40) = 0;
    D32(game_camera, 0x24) = 0;
    D32(game_camera, 0x44) = 0;
    S16(game_camera, 0x3E) = 0;
    ffpipe_29E160(HUD_CAM);
    CameraInitZoom(game_camera, ff_options[0x52], hud_eye.zoom);
}

void hud_eye_update(void)
{
    GameEntity *e;
    if (hz_playing != 0 && hz_play_cam == 3) {
        haze_2B5B70(&hud_ffview);
        return;
    }
    if (hud_eye.obj == game_player_entity) {
        hud_eye_player();
        return;
    }
    e = HENT(hud_eye.obj);
    hud_ffview.x = e->x;
    hud_ffview.y = e->y;
    hud_ffview.bank = 0;
    hud_ffview.z = e->z;
    hud_ffview.pitch = e->f1d;
    hud_ffview.heading = e->heading + hud_eye.hdg;
    EYE_ENT = hud_eye.obj;
}

void hud_eye_player(void)
{
    GameEntity *e = HENT(game_player_entity);
    hud_ffview.heading = e->heading + hud_441355;
    hud_ffview.pitch = hud_441357;
    if (!(hmd_data.flags & 2)) {
        ushort step = (ushort)(game_frame_dt * 5);
        ushort target, diff, a;
        if (step > 0x2000)
            step = 0x2000;
        target = drawobj_29DC20(game_player_entity);
        diff = target - hud_pitch_smooth;
        if (diff < step || (long)diff > 0xFFFF - (long)step)
            a = target;
        else if ((long)diff < 4 * (long)step)
            a = hud_pitch_smooth + step;
        else if (diff < 0x8000)
            a = hud_pitch_smooth + 2 * step;
        else if ((long)diff > 0xFFFF - 4 * (long)step)
            a = hud_pitch_smooth - step;
        else
            a = hud_pitch_smooth - 2 * step;
        hud_pitch_smooth = a;
        hud_ffview.pitch += a;
    }
    hud_ffview.bank = 0;
    EYE_ENT = game_player_entity;
    if (ragdoll_cam != 0) {
        vec3_2822F0(&hud_ffview.x);
        vec3_28236C((short *)&hud_ffview.pitch, (short *)&hud_ffview.heading);
        hud_last_eye.pitch = hud_ffview.pitch;
    } else
        biped_27EC90(&hud_ffview.x, game_player_entity);
    hud_ffview.z += hud_eye_dz;
#ifndef TN_WATCOM_ABI
    port_helmet_eye();                  /* before the shake: the helmet does not follow it */
#endif
    if (hud_shake != 0) {
        hud_ffview.pitch += RndRange(&game_rnd_35B704, -hud_shake, hud_shake);
        hud_ffview.heading += RndRange(&game_rnd_35B704, -hud_shake, hud_shake);
    }
    /* small moves snap back to the last position (no jitter) */
    if (iabs(hud_ffview.x - hud_last_eye.x) < 0x40 && iabs(hud_ffview.y - hud_last_eye.y) < 0x40
        && iabs(hud_ffview.z - hud_last_eye.z) < 0x40
        && iabs((long)hud_ffview.heading - (long)hud_last_eye.heading) < 0x10
        && iabs((long)hud_ffview.pitch - (long)hud_last_eye.pitch) < 0x10) {
        hud_ffview.x = hud_last_eye.x;
        hud_ffview.y = hud_last_eye.y;
        hud_ffview.z = hud_last_eye.z;
        hud_ffview.heading = hud_last_eye.heading;
        hud_ffview.pitch = hud_last_eye.pitch;
    } else {
        hud_last_eye.x = hud_ffview.x;
        hud_last_eye.y = hud_ffview.y;
        hud_last_eye.z = hud_ffview.z;
        hud_last_eye.heading = hud_ffview.heading;
        hud_last_eye.pitch = hud_ffview.pitch;
    }
}

void hud_eye_from_rec(short *rec)
{
    GameEntity *e = HENT(rec[0]);
    hud_ffview.x = e->x;
    hud_ffview.y = e->y;
    hud_ffview.bank = 0;
    hud_ffview.z = e->z;
    hud_ffview.pitch = e->f1d;
    hud_ffview.heading = e->heading + rec[2];
    EYE_ENT = rec[0];
}

/* ------------------------------------------------------------------ debug displays */

void hud_draw_fps(void)
{
    char buf[0x10];
    long v, t;
    ulong dt = game_frame_dt;
    if (dt == 0)
        v = 0;
    else
        v = (ulong)((long)game_time_rate * 100000) / (dt << 8);
    buf[0xD] = 0;
    buf[0xC] = v % 10 + '0';
    buf[0xB] = v / 10 % 10 + '0';
    buf[0xA] = '.';
    buf[9] = v / 100 % 10 + '0';
    t = v / 1000;
    buf[8] = t > 0 ? t + '0' : ' ';
    itoa(game_frame, buf, 10);
    GfxDrawText(0x1F6, buf + 8, 0x42, (ushort)hud_scr_h - 0x1E);
    GfxDrawText(0x1F6, buf, 0x24, (ushort)hud_scr_h - 0x1E);
}

void hud_draw_speed(void)
{
    State st;
    char buf[0xC];
    fix s;
    EDMS_get_state(HENT(game_player_entity)->body, &st);
    s = fix_sqrt(fix_mul(st.X_dot, st.X_dot) + fix_mul(st.Y_dot, st.Y_dot)
                 + fix_mul(st.Z_dot, st.Z_dot));
    itoa(fix_mul(s, 0x23CA8) >> 16, buf, 10);
    GfxDrawText(0x1F6, buf, 0xF0, (ushort)hud_scr_h - 0x1E);
}

/* Palette chart: three rows of colour boxes, each with its 16 light levels below it. */
void hud_draw_palette(void)
{
    long row, base, col, c, k, y, y1;
    WATCOM_CALL((CanvasFn)grd_canvas_table[8], 0);
    for (row = -0x1E, base = 0; row != 0xBA; row += 0x48, base += 0x53) {
        c = base;
        if (c > 0xFF)
            continue;
        col = 4;
        for (;;) {
            grd_canvas->gc.fcolor = c;
            WATCOM_CALL((CanvasFn)grd_canvas_table[16], (short)col, (short)(row - 1),
                        (short)(col + 3), (short)(row + 2));
            y = row;
            y1 = (short)(row + 3);
            for (k = 0xF00 + c; k != c - 0x100; k -= 0x100) {
                y += 4;
                y1 = (short)(y1 + 4);
                grd_canvas->gc.fcolor = planet_light[k];
                WATCOM_CALL((CanvasFn)grd_canvas_table[16], (short)col, (short)y, (short)(col + 3),
                            (short)y1);
            }
            c++;
            col += 4;
            if (col >= 0x150 || c > 0xFF)
                break;
        }
    }
}

/* Debug drive: turn and move the player from hud_turn_* / hud_move_* (no callers). */
void hud_drive(void)
{
    GameEntity *e;
    uchar *p;
    fix s, c, dx, dy;
    ulong t = (ulong)(game_frame_dt * game_time_rate) >> 8;
    long v = (long)(t * (short)(hud_turn_a + hud_turn_b)) << 4;
    v /= 1024;
    e = HENT(game_player_entity);
    e->heading += v;
    fix_sincos((ushort)(e->heading + hud_dir_b), &s, &c);
    dx = fix_mul(hud_move_b, c);
    dy = fix_mul(hud_move_b, s);
    fix_sincos((ushort)(HENT(game_player_entity)->heading + hud_dir_a), &s, &c);
    dx += fix_mul(hud_move_a, c);
    dy += fix_mul(hud_move_a, s);
    p = PBA_PLAYER;
    PbaPlayerMove(p + 6, &dx, &dy);
    EDMS_control_marble(HENT(game_player_entity)->body, dx, dy, D32(PBA_PLAYER, 0xAE));
    D32(PBA_PLAYER, 0xAE) = 0;
}

/* ------------------------------------------------------------------ aiming / targets */

uchar hud_aim_centered(void)
{
    return hud_aim_center;
}

void hud_aim_point(LGPoint *out)
{
    HudWidget *w = hud_w_389DF4;
    if (hud_aim_center) {
        out->x = (w->x0 + w->x1) / 2;
        out->y = (w->y0 + w->y1) / 2;
        if (hud_helmet == 0)
            out->y -= 10;
    } else {
        short *ms = (short *)tnm_state_ptr;
        out->x = ms[0] - w->x0;
        out->y = ms[1] - w->y0;
#ifndef TN_WATCOM_ABI
        if (port_wide_ptr) {            /* widescreen: aim where the pointer is, beside the screen */
            out->x = (short)(port_wide_ptr_x - w->x0);
            out->y = (short)(port_wide_ptr_y - w->y0);
        }
#endif
    }
}

uchar hud_aim_on_target(void)
{
#ifndef TN_WATCOM_ABI
    /* "Auto-aim" (Gameplay page): the box's size, the game's 20 pixels or smaller (0: Off) */
    short b = (short)port_aim_box_now();
    signed char r = ((hud_helmet != 0) + 1) * b;
#else
    signed char r = ((hud_helmet != 0) + 1) * 20;
#endif
    short cx = (hud_targ_sel[0] + hud_targ_sel[2]) / 2;
    short cy = (hud_targ_sel[3] + hud_targ_sel[1]) / 2;
    LGPoint pt;
#ifndef TN_WATCOM_ABI
    if (port_wide_ptr) {                /* widescreen: the current target as the wide view drew it */
        port_wide_targ l[PORT_WIDE_TARGS];
        int n = port_wide_targets(l), i;
        for (i = 0; i < n && !(l[i].cur && l[i].ent == targ_current); i++)
            ;
        if (i == n)
            return 0;
        cx = (l[i].x0 + l[i].x1) / 2;
        cy = (l[i].y0 + l[i].y1) / 2;
        hud_aim_point(&pt);
        return cx - b < pt.x && pt.x < cx + b && cy - r < pt.y && pt.y < cy + r;
    }
#endif
    if (hud_targ_sel[4] == 0)
        return 0;
    hud_aim_point(&pt);
#ifndef TN_WATCOM_ABI
    if (cx - b < pt.x && pt.x < cx + b && cy - r < pt.y && pt.y < cy + r)
#else
    if (cx - 20 < pt.x && pt.x < cx + 20 && cy - r < pt.y && pt.y < cy + r)
#endif
        return 1;
    return 0;
}

void hud_reticle(void)
{
    uchar *p = PBA_PLAYER;
    short t = 0;
    signed char idx;
    ulong pt;
    if (game_camera_38FB20 != 0)
        return;
    idx = p[0xCE];
    if (weapon_ready(p + 0x45 + idx * 15, p + 0x86))
        return;
    if (EYE_ENT != game_player_entity)
        return;
    hud_aim_point(&hud_aim_pt);
    if (hud_35A944 != 0 && targ_current != 0 && hud_aim_on_target())
        t = targ_current;
    memcpy(&pt, &hud_aim_pt, 4);
    reticle_target(t, pt, game_camera, &hud_ffview);
}

/* Click in the 3D view: the nearest target whose rectangle (+20) holds the point. */
void hud_269F28(LGPoint pt)
{
    short best = 0;
    long bestd = 0, i;
    if (game_camera_38FB20 != 0) {
        hview3d_273AC4(hud_w_389DF4);
        return;
    }
#ifndef TN_WATCOM_ABI
    /* widescreen: a click beside the screen, among the units the wide view drew (the same
     * choice as below) */
    if (port_wide_ptr && (pt.x < 0 || pt.x >= hud_w_389DF4->x1 - hud_w_389DF4->x0)) {
        port_wide_targ l[PORT_WIDE_TARGS];
        long n = port_wide_targets(l), d;
        for (i = 0; i < n; i++) {
            short cx = (l[i].x0 + l[i].x1) / 2, cy = (l[i].y0 + l[i].y1) / 2;
            GameEntity *e;
            if (!(l[i].x0 - 20 < pt.x && pt.x < l[i].x1 + 20 && l[i].y0 - 20 < pt.y && pt.y < l[i].y1 + 20))
                continue;
            e = HENT(l[i].ent);
            if (e->cls == 0) {
                if (game_cls0_types[e->type][0] == 0)
                    continue;
            } else if (e->cls > 4)
                continue;
            d = iabs(cx - pt.x) * iabs(cx - pt.x) + iabs(cy - pt.y) * iabs(cy - pt.y);
            if (best == 0 || d < bestd) {
                best = l[i].ent;
                bestd = d;
            }
        }
        helm_262D4C(best);
        return;
    }
#endif
    for (i = 0; i < targ_count; i++) {
        short *r = (short *)&targ_list[i];
        long x0 = r[0], y0 = r[1], x1 = r[2], y1 = r[3], d;
        short cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        GameEntity *e;
        if (!(x0 - 20 < pt.x && pt.x < x1 + 20 && y0 - 20 < pt.y && pt.y < y1 + 20))
            continue;
        e = HENT(r[4]);
        if (e->cls == 0) {
            if (game_cls0_types[e->type][0] == 0)
                continue;
        } else if (e->cls > 4)
            continue;
        d = iabs(cx - pt.x) * iabs(cx - pt.x) + iabs(cy - pt.y) * iabs(cy - pt.y);
        if (best == 0) {
            best = r[4];
            bestd = d;
        } else if (d < bestd) {
            bestd = d;
            best = r[4];
        }
    }
    helm_262D4C(best);
}

void hud_26A11C(short *rect, long arg)
{
    long n = targ_count;
#ifndef TN_WATCOM_ABI
    if (port_s3d_on) {                  /* the sharp view drawing (put back afterwards anyway) */
        if (port_s3d_targ_rec) {        /* widescreen: its units (the mouse, the HUD's layer) */
            short pt[4];
            memcpy(pt, rect, 8);
            port_hud_noclamp = 1;
            drawobj_29CBA4((short)arg, pt);
            port_hud_noclamp = 0;
            port_wide_target(rect, pt, (short)arg,
                             game_42015A ? 0x26
                                         : damage_27D458((NearList *)(PBA_PLAYER + 0xB2), (short)arg),
                             (short)arg == targ_current);
        }
        return;
    }
#endif
    if (n < 0x40 && rect[3] >= rect[1] && rect[2] >= rect[0] && rect[2] >= 0
        && rect[0] <= S16(targ_view, 8)) {
        short *d = (short *)&targ_list[n];
        memcpy(d, rect, 8);
        n++;
        d[4] = (short)arg;
    }
    targ_count = n;
}

#ifndef TN_WATCOM_ABI
static uchar hud_label_extra;           /* hud_target_label for the wide layer only (no game rnd) */

/* "Auto-aim highlight" (port/ui/options.c): while a shot would go to the locked target
 * (hud_reticle's test), the 3D view's crosshair (frames 0-10 of the pointer resource 505, set by
 * hview3d_2754FC) shows its bright green middle (0x89) in the red (0x44) of the HUD's cross
 * (frame 0x16).  Only the pointer's picture changes. */
#define SIGN_REF 0x1F90000
static uchar sign_bits[32 * 32];        /* the biggest frame is 17 x 28 (goggles) */
static TnCursor sign_cur;
static long sign_from;                  /* the crosshair the sign stands in for (0: none shown) */

void port_hud_aim_sign(void)
{
    long cur = tnm_cursor_ref;
    HudWidget *w = hud_w_389DF4;
    LGPoint pt;
    uchar *p;
    int on, i, n;

    if (cur != 0)                       /* the game has set a pointer since: no sign shows */
        sign_from = 0;
    on = port_aim_sign && game_camera_38FB20 == 0 && EYE_ENT == game_player_entity &&
         hud_35A944 != 0 && targ_current != 0 && hud_aim_on_target();
    if (on && !port_wide_ptr) {         /* the pointer in the main view (not a side screen's) */
        hud_aim_point(&pt);
        on = pt.x >= 0 && pt.y >= 0 && pt.x < w->x1 - w->x0 && pt.y < w->y1 - w->y0;
    }
    if (sign_from) {                    /* the sign shows */
        if (on)
            return;
        MouseSetCursor(sign_from);
        sign_from = 0;
        cur = tnm_cursor_ref;
    }
    if (!on || cur < SIGN_REF || cur > SIGN_REF + 10)
        return;
    p = RefLock(cur);
    if (!p)
        return;
    sign_cur.bm = *(grs_bitmap *)p;
    sign_cur.hot = *(LGPoint *)(p + 0x10);
    n = sign_cur.bm.row * sign_cur.bm.h;
    if (sign_cur.bm.type != BMT_FLAT8 || n <= 0 || n > (int)sizeof sign_bits) {
        ResUnlock((Id)(cur >> 16));
        return;
    }
    memcpy(sign_bits, p + 0x1C, n);
    ResUnlock((Id)(cur >> 16));
    for (i = 0; i < n; i++)
        if (sign_bits[i] == 0x89)
            sign_bits[i] = 0x44;
    sign_cur.bm.bits = sign_bits;
    MouseSetCursorBitmap(&sign_cur);
    if (tnm_cursor_ref == 0)            /* (taken: it fits the save-under) */
        sign_from = cur;
}

/* "AI debug display" (port/mods/ai.c): over every unit with a brain in the main view, its
 * squad's tactic, its health and its target, drawn as the target labels are.  ex: the wide
 * view's layer is the canvas (its columns left of the game's view), -1 the game's view.
 * Nothing of the game changes: the places are the wide view's list or drawobj_29CCE0's box */
extern int port_ai_debug;
int port_ai_label(int b, char *buf, int n, int *colour);
static void hud_ai_debug(long ex)
{
    port_wide_targ l[PORT_WIDE_TARGS];
    long n = ex >= 0 ? port_wide_targets(l) : 0, k;
    char line[0x60];
    short rect[4], box[GAME_MAX_BRAINS][4], w, h;
    int b, col, nb = 0, i, tries;
    long keep = grd_canvas->gc.fcolor;
    for (b = 1; b < GAME_MAX_BRAINS; b++) {
        short e = *(short *)((uchar *)&game_brains[b] + 0xB);
        long x, y;
        if (e == game_player_entity || !port_ai_label(b, line, sizeof line, &col))
            continue;
        if (ex >= 0) {
            for (k = 0; k < n && l[k].ent != e; k++)
                ;
            if (k == n)
                continue;
            x = (l[k].x0 + l[k].x1) / 2 + ex;
            y = l[k].y0;
        } else {
            if (!drawobj_29CCE0(HENT(e), rect))
                continue;
            x = (rect[0] + rect[2]) / 2;
            y = rect[1];
        }
        y -= 2;
        /* above the labels already placed that it would cover (units close together) */
        gr_font_string_size(ResGet((Id)0x1F7), line, &w, &h);
        for (tries = 0; tries < 12; tries++) {
            for (i = 0; i < nb; i++)
                if (x - w / 2 < box[i][2] && x + w / 2 > box[i][0] && y - h < box[i][3] && y > box[i][1])
                    break;
            if (i == nb)
                break;
            y = box[i][1] - 1;
        }
        box[nb][0] = (short)(x - w / 2);
        box[nb][1] = (short)(y - h);
        box[nb][2] = (short)(x + w / 2);
        box[nb][3] = (short)y;
        nb++;
        grd_canvas->gc.fcolor = col;
        port_label_piece(0x1F7, line, x, y, 1, 2, 1);
    }
    grd_canvas->gc.fcolor = keep;
}

/* hud_26A188 in widescreen: the same, its brackets and label drawn on the wide view's layer
 * (port/video/sharp3d.c) where the wide view has the units, also beyond the game's view.  The game's
 * values change as in hud_26A188; the other targets' brackets come from the wide view's list. */
static void hud_targets_wide(HudMsgEv *ev, long ex)
{
    port_wide_targ l[PORT_WIDE_TARGS];
    long n = port_wide_targets(l), i, k;
    short rect[4], wr[4], e;
    char buf[0x28];
    uchar vis;
    if ((ushort)ev->code == 2) {
        for (i = 0; i < targ_count; i++) {      /* the game's calls, for what they change */
            short *r = (short *)&targ_list[i];
            uchar col;
            if (r[4] == 0 || r[4] == targ_current)
                continue;
            memcpy(rect, r, 8);
            if (game_42015A)
                col = 0x26;
            else
                col = damage_27D458((NearList *)(PBA_PLAYER + 0xB2), r[4]);
            if (col != 0)
                drawobj_29CBA4(r[4], rect);
        }
        if (port_s3d_hud_begin() >= 0) {        /* the brackets where the wide view has them */
            for (i = 0; i < n; i++) {
                if (l[i].ent == 0 || l[i].ent == targ_current || l[i].col == 0)
                    continue;
                wr[0] = wr[2] = (short)(l[i].px + ex);
                wr[1] = wr[3] = l[i].py;
                hud_bracket(l[i].col, wr);
            }
            if (port_ai_debug)
                hud_ai_debug(ex);
            port_s3d_hud_end();
        }
    }
    e = targ_current;
    if (e == 0) {
        e = S16(game_camera, 0xB5);
        if (e == 0 || game_camera[0xB7] != 0) {
            hud_targ_sel[4] = 0;
            hud_last_target = 0;
            return;
        }
    }
    vis = drawobj_29CCE0(HENT(e), rect);
    if (!vis)
        hud_targ_sel[4] = 0;
    else {
        if (e != hud_last_target && hud_26A6D4(e, buf))
            SndPlay(0x7EC, 0, 0x40, 1, -1, 0x40, 0);
        hud_targ_sel[4] = e;
        hud_last_target = e;
        memcpy(hud_targ_sel, rect, 8);
        drawobj_29CBA4(e, rect);
    }
    for (k = 0; k < n && l[k].ent != e; k++)
        ;
    if (k < n) {                        /* where the wide view has it */
        wr[0] = wr[2] = (short)(l[k].px + ex);
        wr[1] = wr[3] = l[k].py;
    } else if (vis) {
        memcpy(wr, rect, 8);
        wr[0] += ex;
        wr[2] += ex;
    } else
        return;
    if (port_s3d_hud_begin() >= 0) {
        LGRect r;
        memcpy(&r, wr, 8);
        hud_label_extra = !vis;
        hud_target_label(e, r);
        hud_label_extra = 0;
        port_s3d_hud_end();
    }
}
#endif

/* Callback of hud_tmpl_target: code 1 clears the target list, code 2 draws brackets on the
 * other targets; both then label the current target (or the one under the camera probe). */
void hud_26A188(HudMsg *m, HudMsgEv *ev)
{
    short rect[4], e;
    char buf[0x28];
    long i;
    if (game_camera_38FB20 != 0)
        return;
    if ((ushort)ev->code == 1) {
        targ_count = 0;
        return;
    }
#ifndef TN_WATCOM_ABI
    if ((ushort)ev->code == 2)
        port_hud_aim_sign();            /* "Auto-aim highlight": the target or the pointer moved */
    {
        long ex = port_s3d_hud_begin();         /* widescreen: the wide view's layer */
        if (ex >= 0) {
            port_s3d_hud_end();
            hud_targets_wide(ev, ex);
            return;
        }
    }
    if ((ushort)ev->code == 2 && port_ai_debug)
        hud_ai_debug(-1);
#endif
    if ((ushort)ev->code == 2 && targ_count != 0) {
        for (i = 0; i < targ_count; i++) {
            short *r = (short *)&targ_list[i];
            uchar col;
            if (r[4] == 0 || r[4] == targ_current)
                continue;
            memcpy(rect, r, 8);
            if (game_42015A)
                col = 0x26;
            else
                col = damage_27D458((NearList *)(PBA_PLAYER + 0xB2), r[4]);
            if (col != 0) {
                drawobj_29CBA4(r[4], rect);
                hud_bracket(col, rect);
            }
        }
    }
    e = targ_current;
    if (e == 0) {
        e = S16(game_camera, 0xB5);
        if (e == 0 || game_camera[0xB7] != 0) {
            hud_targ_sel[4] = 0;
            hud_last_target = 0;
            return;
        }
    }
    if (!drawobj_29CCE0(HENT(e), rect)) {
        hud_targ_sel[4] = 0;
        return;
    }
    if (e != hud_last_target && hud_26A6D4(e, buf))
        SndPlay(0x7EC, 0, 0x40, 1, -1, 0x40, 0);
    hud_targ_sel[4] = e;
    hud_last_target = e;
    memcpy(hud_targ_sel, rect, 8);
    drawobj_29CBA4(e, rect);
    {
        LGRect r;
        memcpy(&r, rect, 8);
        hud_target_label(e, r);
    }
}

/* Corner brackets around the centre of r (colour col, < 0: current colour). */
void hud_bracket(long col, short *r)
{
    long cx, cy, x0, x1, y0, y1, dx, dy;
    if (col >= 0)
        grd_canvas->gc.fcolor = col;
    cx = (r[0] + r[2]) / 2;
    cy = (r[1] + r[3]) / 2;
    x0 = cx - 5;
    x1 = cx + 5;
    if (HIRES) {
        y0 = cy - 9;
        y1 = cy + 8;
    } else {
        y0 = cy - 5;
        y1 = cy + 5;
    }
    x0 = (short)x0;
    x1 = (short)x1;
    y0 = (short)y0;
    y1 = (short)y1;
    dx = (x1 - x0) / 4;
    dy = (y1 - y0) / 4;
    if (dx < 2)
        dx = 2;
    if (dy < 2)
        dy = 2;
    gen_hline(x0, y0, x0 + dx);
    gen_vline(x0, y0, y0 + dy);
    gen_hline(x1 - dx, y0, x1);
    gen_vline(x1, y0, y0 + dy);
    gen_hline(x0, y1, x0 + dx);
    gen_vline(x0, y1 - dy, y1);
    gen_hline(x1 - dx, y1, x1);
    gen_vline(x1, y1 - dy, y1);
}

/* Four status ticks around the current target (colours from objdmg_status_str), doubled when
 * the aim is on it. */
void hud_status_ticks(uchar *cols, short *r, uchar flag)
{
    long cx = (r[0] + r[2]) / 2, cy = (r[1] + r[3]) / 2, top, bot;
    if (HIRES) {
        top = cy - 8;
        bot = cy + 7;
    } else {
        top = cy - 4;
        bot = cy + 4;
    }
    grd_canvas->gc.fcolor = cols[0];
    gen_hline(cx - 3, top - 2, cx + 3);
    if (flag)
        gen_hline(cx - 2, top - 1, cx + 2);
    grd_canvas->gc.fcolor = cols[2];
    gen_hline(cx - 3, bot + 2, cx + 3);
    if (flag)
        gen_hline(cx - 2, bot + 1, cx + 2);
    grd_canvas->gc.fcolor = cols[1];
    gen_vline(cx - 6, top + 1, bot - 1);
    if (flag)
        gen_vline(cx - 5, top + 2, bot - 2);
    grd_canvas->gc.fcolor = cols[3];
    gen_vline(cx + 6, top + 1, bot - 1);
    if (flag)
        gen_vline(cx + 5, top + 2, bot - 2);
}

/* The display name of an entity (0: no name for it). */
uchar hud_26A6D4(short ent, char *out)
{
    GameEntity *e = HENT(ent);
    uchar cls = e->cls, n;
    Ref ref;
    uchar *rec;
    if (cls >= 5)
        return 0;
    n = e->f24;
    if (n != 0) {
        if (n > 0x1D) {
            memcpy(out, hud_str_bad_offset, 11);
            return 1;
        }
        ref = 0x1770000 + n;
    } else {
        if (cls == 0 && game_cls0_types[e->type][0] == 0)
            return 0;
        ref = ((ulong)(cls + 0x17D) << 16) | e->type;
        if (cls <= 1) {
            rec = mfddata_2665B4(ent);
            if (rec)
                ref = 0x16C0000 + rec[6];
        }
    }
    s_strcpy(out, RefGet(ref));
    return 1;
}

#ifndef TN_WATCOM_ABI
/* "Target label: Around the box" (port/ui/options.c): the label the game made (line: the name, a
 * blank line, the distance, a blank line, the mark) in three pieces: the name where the game has
 * it, right of the box; the distance centred above the box (the current target's status ticks or
 * the brackets); the mark (Locked, Out of range) centred under the health bar.  A piece that
 * would leave the canvas goes to the other side.  0 when the line is not in that form. */
static int hud_label_around(int cur, char *line, LGRect r)
{
    char name[0x78], dist[0x20], *a, *b, *mark;
    long cx = (r.ul.x + r.lr.x) / 2, cy = (r.ul.y + r.lr.y) / 2, top, bot, gap = HIRES ? 3 : 2;
    short w, dh, mh;
    grs_font *f = ResGet(0x1F7);
    a = strstr(line, "\n\n");
    b = a ? strstr(a + 2, "\n\n") : NULL;
    if (!b || a - line >= (long)sizeof name || b - (a + 2) >= (long)sizeof dist)
        return 0;
    memcpy(name, line, a - line);
    name[a - line] = 0;
    memcpy(dist, a + 2, b - (a + 2));
    dist[b - (a + 2)] = 0;
    mark = b + 2;
    gr_font_string_size(f, dist, &w, &dh);
    gr_font_string_size(f, mark, &w, &mh);
    if (cur) {                          /* the status ticks' lines; the health bar */
        top = cy - (HIRES ? 10 : 6);
        bot = cy + 12;
    } else {                            /* the brackets */
        top = cy - (HIRES ? 9 : 5);
        bot = cy + (HIRES ? 8 : 5);
    }
    port_label_piece(0x1F7, name, cx + 12, cy - 5, 0, 0, 0);
    if (top - gap - dh < grd_canvas->gc.clip.i.top) {          /* at the top: both under the box */
        port_label_piece(0x1F7, dist, cx, bot + gap, 1, 0, 1);
        port_label_piece(0x1F7, mark, cx, bot + gap + dh, 1, 0, 1);
    } else if (*mark && bot + gap + mh > grd_canvas->gc.clip.i.bot) {    /* at the bottom: above */
        port_label_piece(0x1F7, dist, cx, top - gap - mh, 1, 2, 1);
        port_label_piece(0x1F7, mark, cx, top - gap, 1, 2, 1);
    } else {
        port_label_piece(0x1F7, dist, cx, top - gap, 1, 2, 1);
        port_label_piece(0x1F7, mark, cx, bot + gap, 1, 0, 1);
    }
    return 1;
}
#endif

/* Label of a target: name (garbled by the player's sensor damage), distance and in-range
 * marker; for the current target also the status ticks and a health bar. */
void hud_target_label(short ent, LGRect r)
{
    char line[0x78];
    char name[0x20];
    uchar cols[4];
    uchar sp, se, garble, len;
    GameEntity *e, *v;
    char *p;
    long d, cx, y;
    Ref ref;
    memcpy(name, hud_name_init, 0x20);
    sp = damage_27DF14(game_player_entity, 2);
    se = damage_27DF14(ent, 0);
    if (ent == targ_current) {
        objdmg_status_str(cols, ent);
        hud_status_ticks(cols, (short *)&r, hud_aim_on_target());
    }
    e = HENT(ent);
    if (!hud_26A6D4(ent, name))
        return;
#ifndef TN_WATCOM_ABI
    if (hud_label_extra) {              /* a label the game itself does not draw: its own dice */
        static unsigned dice = 1;
        dice = dice * 1103515245u + 12345u;
        garble = sp > 0x2D ? (sp < 0x41 ? (dice >> 16) % 8 : 0xFF) : sp > 5 ? (dice >> 16) % 5 : 0;
    } else
#endif
    if (sp > 0x2D)
        garble = sp < 0x41 ? RndRange(&game_rnd_35B704, 0, 8) : 0xFF;
    else
        garble = sp > 5 ? RndRange(&game_rnd_35B704, 0, 5) : 0;
    if (game_396E18 != 0 && e->f23 > 3 && (game_396E14 & (1ul << (e->f23 & 31))))
        s_strcat(name, RefGet(0x1720013));
    if (garble == 0)
        s_strcpy(name, RefGet(0x1710036));
    else if (garble < 2)
        s_strcpy(name, RefGet(0x171002E + e->cls));
    for (p = name; *p; p++)
        if (*p == ' ')
            *p = '\n';
    v = HENT(hud_eye.obj);
    d = (fix_pyth_dist(fix_pyth_dist(e->x - v->x, e->y - v->y), e->z - v->z) * 6) >> 16;
    if (ent == targ_current) {
        uchar *pb = PBA_PLAYER;
        signed char idx = pb[0xCE];
        ref = d >= ord_table[pb[0x45 + idx * 15]].range * 6 ? 0x1720011 : 0x1720001;
    } else
        ref = 0x1720002;
    {
        char *mark = RefGet(ref);
        lg_sprintf(line, RefGet(0x1720000), name, d, mark);
    }
    grd_canvas->gc.fcolor = 0x44;
#ifndef TN_WATCOM_ABI
    /* the sharp view: in its picture, sharp and at 3/4 of the size (port/video/sharp3d.c); "Target
     * label: Around the box" draws the name, the distance and the mark apart */
    if (!port_target_label || !hud_label_around(ent == targ_current, line, r))
        port_label_piece(0x1F7, line, (r.ul.x + r.lr.x) / 2 + 12, (r.ul.y + r.lr.y) / 2 - 5, 0, 0, 0);
#else
    GfxDrawText(0x1F7, line, (r.ul.x + r.lr.x) / 2 + 12, (r.ul.y + r.lr.y) / 2 - 5);
#endif
    if (ent != targ_current)
        return;
    if (e->cls == 0)
        se = (ulong)(ushort)S16(game_cls0[e->slot], 7) * 100
             / (ushort)S16(game_cls0_types[e->type], 1);
    cx = (r.ul.x + r.lr.x) / 2;
    y = (short)((r.ul.y + r.lr.y) / 2 + 12);
    grd_canvas->gc.fcolor = 0x1D;
    gen_vline(cx - 7, y, y);
    gen_vline(cx + 7, y, y);
    grd_canvas->gc.fcolor = 0x84;
    gen_hline(cx - 6, y, cx + 6);
    len = se * 12 / 100;
    if (len == 0)
        len = 1;
    if (len < 12) {
        grd_canvas->gc.fcolor = 0x44;
        gen_hline(len + cx - 6, y, cx + 6);
    }
}

/* Watcom entry of hud_target_label: eax = ent, the LGRect by value on the stack (ret 8); every
 * other register is preserved. */
#ifdef TN_WATCOM_ABI
__asm__(".text\n.globl W_hud_target_label\n"
        "W_hud_target_label:\n"
        "\tpushl %ecx\n\tpushl %edx\n\tpushl %ebx\n"
        "\tpushl 20(%esp)\n\tpushl 20(%esp)\n\tpushl %eax\n"
        "\tcall hud_target_label\n"
        "\taddl $12, %esp\n"
        "\tpopl %ebx\n\tpopl %edx\n\tpopl %ecx\n"
        "\tret $8\n");
#else
#endif

/* ------------------------------------------------------------------ helmet */

uchar hud_26AC98(uchar helmet)
{
    long map = game_camera_38FB20;
    uchar had;
    if (tnm_frozen) {
        hud_drag_widget = 0;
        MouseIntDrawOn();
        MouseUnfreeze();
        MouseSetCursor(0x1F90016);
    }
    if (map != 0)
        hud_26AF24();
    had = hud_drag_widget == hud_w_389DF4;
    hud_35A9A4 = game_time + 500;
    hud_drag_widget = 0;
    hud_helmet_set(helmet);
    if (had)
        hud_drag_widget = hud_w_389DF4;
    hud_35A548 = 0;
    if (map != 0)
        hud_26AE98();
    return 1;
}

static void pane_stencil(void)
{
    HudWidget *w = hud_w_389DF4;
    LGPoint o;
    o.x = w->x0;
    o.y = w->y0;
    MouseSetStencil(w->f10, (short)(w->y1 - w->y0), o);
}

void hud_helmet_set(uchar helmet)
{
    hud_helmet = helmet;
    SndPlay(0x7ED, 0, 0x40, 1, -1, 0x40, 0);
    if (helmet) {
        helm_262278();
        gr_set_canvas(&tnui_back_canvas);
        WATCOM_CALL((CanvasFn)grd_canvas_table[8], 0);
        if (hud_eye.ctrl->hidden != 0 && hud_w_389DF4->f10 != 0)
            pane_stencil();
        else
            MouseClearStencil();
        helm_263BE4();
    } else {
        grs_bitmap *bm;
        helm_262278();
        gr_set_canvas(&tnui_back_canvas);
        bm = RefGet(0x2EE0000);
        bm->bits = (uchar *)bm + 0x1C;
        WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, 0, 0);
        helm_26214C();
        if (hud_eye.ctrl->hidden != 0)
            pane_stencil();
        helm_263BE4();
        hud_38B9AE = 1;
    }
    targ_view = (uchar *)&hud_w_389DF4->canvas;
    sky_ref_w = hud_scr_w;
    S16(game_438C20, 0x2E) = 1;
    cam_setup();
}

/* ------------------------------------------------------------------ overhead map */

void hud_26AE98(void)
{
    TnEvent ev;
    uchar *map;
    ev.type = 2;
    memcpy(&ev.pt, tnm_state_ptr, 4);
    hud_268D80(hud_w_389DF4, &ev);
    map = mapdisp_276ACC(&hud_w_389DF4->canvas, hud_map_x, hud_map_y, hud_map_scale);
    game_camera_38FB20 = (long)map;
#ifndef TN_WATCOM_ABI
    {                                   /* now it is the full map: widescreen's zoom (mapdisp.c) */
        long z = S16(map, 8);
        while (z < 7 && !port_map_zoom_ok(map, z))
            z++;
        mapdisp_276BC8(map, z);
    }
#endif
    map[0x26] |= 5;
    map[0x27] = hud_map_layers;
    hudview_26C8B4();
    hud_widgets[26]->flags |= 0x20;
    hud_widgets[30]->flags |= 0x20;
    hud_eye.ctrl = &cursctrl_other;
}

void hud_26AF24(void)
{
    if (hud_map_grab) {
        hud_drag_widget = 0;
        MouseIntDrawOn();
        MouseUnfreeze();
        MouseSetCursor(0x1F90016);
    }
    mapdisp_276BB4(MAP);
    S16(game_438C20, 0x2E) = 1;
    game_camera_38FB20 = 0;
    hud_eye.ctrl = damage_27C880(hud_eye.obj);
    hudview_26C980(HudGetCompatibleView());
    hud_widgets[26]->flags |= 0x20;
    hud_widgets[30]->flags |= 0x20;
    if (tnm_frozen)
        MouseUnfreeze();
}

void hud_26AFB0(long step)
{
    long s = step + S16(MAP, 8);
    if (s < 3 || s > 7)
        return;
#ifndef TN_WATCOM_ABI
    if (step < 0 && !port_map_zoom_ok(MAP, s))    /* widescreen: not past the land's sides */
        return;
#endif
    hud_map_scale = s;
    mapdisp_276BC8(MAP, (uchar)s);
    map_centre(hud_w_389DF4);
    hud_widgets[30]->flags |= 0x20;
    SndPlay(0x7E9, 0, 0x40, 1, -1, 0x40, 0);
}

void hud_map_mouse(HudWidget *pane, TnEvent *ev)
{
    LGPoint d, mp;
    long hb, row;
    d.x = ev->pt.x - pane->x0;
    d.y = ev->pt.y - pane->y0;
    switch (ev->type) {
    case 1:
    case 0x10:
        mp = hud_map_pos;
        if (d.y >= mp.y && (short)(mp.y + (HIRES ? 0x54 : 0x2C)) > d.y && d.x >= mp.x
            && (short)(mp.x + 0x54) > d.x) {
            if (d.y - mp.y <= (HIRES ? 0x10 : 8)) {
                /* title bar: drag the legend */
                hud_map_drag = 1;
                hud_drag_off.x = d.x - hud_map_pos.x;
                hud_drag_off.y = d.y - hud_map_pos.y;
                if (pane->f10 != 0) {
                    LGPoint o;
                    o.x = pane->x0;
                    o.y = pane->y0;
                    MouseSetStencil(pane->f10, (short)(pane->y1 - pane->y0), o);
                }
                hud_drag_widget = pane;
                MouseIntDrawOff();
                return;
            }
            hb = HIRES ? 0x10 : 8;
            row = (d.y - (hud_map_pos.y + hb + 2)) / (HIRES ? 0x10 : 8);
            if (row >= 0 && row < 4)
                MAP[0x27] ^= (uchar)(1 << row);
            hud_map_layers = MAP[0x27];
            hudview_26CAF0(MAP[0x27]);
            return;
        }
#ifndef TN_WATCOM_ABI
        if (port_wide_ptr) {            /* widescreen: the wide map beside the game's, on the land */
            MapDisp *mm = (MapDisp *)MAP;
            long lo = -(mm->x / mm->scale), hi = (0x1FFFFFF - mm->x) / mm->scale;
            d.x = (short)(d.x < lo ? lo : d.x > hi ? hi : d.x);
            lo = -(mm->y / mm->scale) << hud_35A544;
            hi = ((0x1FFFFFF - mm->y) / mm->scale) << hud_35A544;
            d.y = (short)(d.y < lo ? lo : d.y > hi ? hi : d.y);
        }
#endif
        if (hud_389BC2 & 0x3F)
            hview3d_273D40(MAP, d);
        else
            hview3d_273CFC(MAP, d);
        return;
    case 2:
        if (hud_map_drag) {
            MouseClearStencil();
            hud_drag_widget = 0;
            MouseIntDrawOn();
            hud_map_drag = 0;
        }
        return;
    case 4:
        if (!hud_map_drag) {
            hud_drag_widget = pane;
            hud_map_grab = 1;
            hud_grab_pt = ev->pt;
            MouseIntDrawOff();
            MouseFreeze();
            MouseSetCursor(0x1F90018);
        }
        return;
    case 8:
        if (pane == hud_drag_widget) {
            HudWidget *w = hud_drag_widget;
            hud_map_grab = 0;
            hud_drag_widget = 0;
            MouseIntDrawOn();
            MouseUnfreeze();
            MouseSetCursor(0x1F90016);
            map_centre(w);
        }
        return;
    case 0x40:
        if (hud_map_drag) {
            LGPoint n;
            n.x = d.x - hud_drag_off.x;
            n.y = d.y - hud_drag_off.y;
            if (n.y >= 0 && n.y < (long)(ushort)hud_scr_h - (HIRES ? 0x10 : 8) && n.x >= -0x2A
                && n.x < (long)(ushort)hud_scr_w - 0x2A)
                hud_map_pos = n;
        } else if (pane == hud_drag_widget) {
            LGPoint dl;
            dl.x = ev->pt.x - hud_grab_pt.x;
            dl.y = ev->pt.y - hud_grab_pt.y;
            mapdisp_276CDC(MAP, dl);
            MouseMoveTo(hud_grab_pt);
        }
        /* fall through */
    case 0x80:
        if (pane != hud_drag_widget)
            MouseSetCursor(0x1F90016);
        return;
    }
}

void hud_map_draw(void)
{
    fill_type_0();
    hud_map_legend();
    hud_msg_draw();
}

/* The map, then the layer legend (a bitmap of what is under it, darkened, with a title line,
 * check marks and the four layer names) at hud_map_pos. */
void hud_map_legend(void)
{
    grs_canvas c;
    grs_bitmap *bm;
    long y, i;
    mapdisp_276EF8(MAP, 0);
#ifndef TN_WATCOM_ABI
    port_s3d_map(0, MAP, 0);            /* the map drawn sharp (port/video/sharp3d.c) */
#endif
    bm = HIRES ? &hud_map_bm[1] : &hud_map_bm[0];
    WATCOM_CALL((CanvasFn)grd_canvas_table[207 + 2 * bm->type], (long)bm, hud_map_pos.x,
                hud_map_pos.y);
    bm = HIRES ? &hud_map_bm[1] : &hud_map_bm[0];
    gr_make_canvas(bm, &c);
    gr_set_canvas(&c);
    grd_canvas->gc.fcolor = 0x1D;
    WATCOM_CALL((CanvasFn)grd_uline_fill_vector[GRL_UHLINE], 0, HIRES ? 0x10 : 8, 0x53,
                grd_canvas->gc.fcolor, grd_canvas->gc.fill_parm);
    gr_set_canvas(&hud_w_389DF4->canvas);
    fill_type_1();
    grd_canvas->gc.fill_parm = (long)planet_tab3a;
    bm = HIRES ? &hud_map_bm[1] : &hud_map_bm[0];
    WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_UBITMAP + 1 + 2 * bm->type], (long)bm,
                hud_map_pos.x, hud_map_pos.y);
    fill_type_0();
    grd_canvas->gc.fcolor = 0x1D;
    y = hud_map_pos.y + (HIRES ? 0x10 : 8) + 2;
    for (i = 0; i < 4; i++) {
        if (MAP[0x27] & (1 << i))
            GfxDrawText(0x1F7, hud_str_check, hud_map_pos.x + 3, y);
        GfxDrawText(0x1F7, RefGet(0x1720006 + i), hud_map_pos.x + 9, y);
        y += HIRES ? 0x10 : 8;
    }
}

/* ------------------------------------------------------------------ camera / zoom / palette */

void hud_cam_setup(void)
{
    cam_setup();
}

void hud_26B780(long zoom)
{
    if (zoom == hud_eye.zoom)
        return;
    hud_eye.zoom = zoom;
    if (hud_eye.obj == game_player_entity)
        hud_zoom_player = zoom;
    else
        hud_zoom_other = zoom;
    cam_setup();
}

void hud_26B800(long step)
{
    long z, maxz;
    if (game_359708 == 0x27)
        return;
    if (step < 0) {
        if (hud_eye.zoom == 0)
            return;
        z = step + hud_eye.zoom;
        if (z < 0)
            z = 0;
    } else {
        maxz = 3 - (HENT(game_player_entity)->type != 0xE);
        if (hud_eye.zoom == maxz)
            return;
        z = step + hud_eye.zoom;
        if (maxz < z)
            z = maxz;
    }
    SndPlay(0x7E9, 0, 0x40, 1, -1, 0x40, 0);
    (hud_helmet ? hud_widgets[43] : hud_widgets[30])->flags |= 0x20;
    if (z == hud_eye.zoom)
        return;
    hud_eye.zoom = z;
    if (hud_eye.obj == game_player_entity)
        hud_zoom_player = z;
    else
        hud_zoom_other = z;
    hud_cam_setup();
}

uchar hud_26B8F8(uchar mode)
{
    if (mode != game_35ADE0) {
        uchar *p = hud_pal_tab[mode];
        long inc, i;
#ifndef TN_WATCOM_ABI
        if (mode & 1)                   /* Infrared: Sci-fi, the switch-over picture in its blues */
            p = port_ir_wash(p);
#endif
        game_35ADE0 = mode;
        inc = p == planet_tab3b;
        hud_fill_tab[0] = p;
        hud_fill_tab[1] = p + inc;
        hud_fill_tab[2] = p + 2 * inc;
        hud_fill_tab[3] = p + 3 * inc;
        if (mode != 0)
            for (i = 0; i < 3; i++)
                if (hud_views[i].flags & 0xC)
                    hudview_26C600(i);
    }
    return mode;
}

void hud_26B970(long a)
{
    long cur = game_35ADE0 == 1 || game_35ADE0 == 2;
    if (cur ^ ((uchar)a != 0))
        hud_26B8F8(game_35ADE0 ^ 1);
    SndPlay(0x7E8, 0, 0x40, 1, -1, 0x40, 0);
}

long HudGetCompatibleView(void)
{
    if (hud_eye.obj == game_player_entity)
        return 8;
    if (hud_eye.obj == game_396868)
        return 0x11;
    DbgReportError(1, hud_str_unknown_view, hud_eye.obj);
    return 0;
}

void hud_26BA18(void)
{
    if (hud_eye.obj != game_396868)
        return;
    damage_27E1E0((short *)&hud_eye, game_player_entity, 0, 0, (long)&cursctrl_player);
    hud_26AC98(hud_helmet);
    hudview_26C318(hudview_26C410(8), 8);
}

void hud_26BA6C(HudEye *v)
{
    short old = hud_eye.zoom;
    hud_eye = *v;
    hud_eye.zoom = hud_eye.obj == game_player_entity ? hud_zoom_player : hud_zoom_other;
    if (old != hud_eye.zoom)
        cam_setup();
}

uchar hud_dbg_line(long a, long b, long c, long d, long e, long f)
{
    long n = hud_dbg_count;
    if (n == 10)
        return 0;
    hud_dbg_lines[n][0] = a;
    hud_dbg_lines[n][1] = b;
    hud_dbg_lines[n][2] = c;
    hud_dbg_lines[n][3] = d;
    hud_dbg_lines[n][4] = e;
    hud_dbg_lines[n][5] = f;
    hud_dbg_count = n + 1;
    return 1;
}

void hud_26BB5C(void)
{
    hud_out_of_area = game_397B70 == 0;
}

void HudViewReset(void)
{
    CameraInitZoom(game_camera, 4 - hud_zoom_reset, 0);
    hud_zoom_reset = 0;
}

void hud_26BB98(long v)
{
    hud_zoom_reset = v;
}
