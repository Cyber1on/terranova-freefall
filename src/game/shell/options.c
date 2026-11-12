/* options.c -- M105 (0x2CA030-0x2CCD30): the options screen (resources 0x188 bitmaps, 0x189
 * rectangles, 0x173 texts, 0x18A detail preview) and the in-mission help screen (0x187).
 *
 * Options are 25 values: opt_val is what the screen edits, opt_applied what is in force.
 * options_2CA1E4 applies opt_applied through opt_apply[i].fn; options_2CA1B8 tells fn2 about a
 * change.  The screen has sliders (opt_sliders, drag), buttons (opt_buttons; a click calls the
 * button's fn with its arg, then options_2CA1B8(idx, arg)) and four "detail" pages.
 *
 *   0x2CA030 (data) the release point of a fake event (320, 200)
 *   0x2CA034 / 044 / 054 (data) per detail page: option, title text, background bitmap
 *   0x2CA064 .. 0x2CA1AC option setters      0x2CA1B8 options_2CA1B8 tell fn2
 *   0x2CA1E4 apply all                        0x2CA23C redraw what the screen covered
 *   0x2CA2C0 save the screen state            0x2CA360 restore it
 *   0x2CA44C blocking handler (eats events)   0x2CA450 the screen's key / mouse handler
 *   0x2CA834 hit test                         0x2CA890 (no callers) cursor
 *   0x2CA8FC open the screen                  0x2CAAC0 text centred in a rectangle
 *   0x2CAB58 the same from two refs           0x2CAB84 (no callers) redraw everything
 *   0x2CABAC labels  0x2CAC54 options  0x2CAD30 background  0x2CAD68 / 2CAD9C slider ticks
 *   0x2CADE4 defaults / undo                  0x2CAF94 .. 0x2CC948 one option each
 *   0x2CBBC8 / 2CBBEC / 2CBBF8 / 2CCAF8 the closing buttons, 0x2CBC08 close
 *   0x2CBCF4 / 2CBD04 detail page change      0x2CBDA8 .. 0x2CBE8C detail buttons
 *   0x2CBEA0 detail page                      0x2CC01C detail preview
 *   0x2CCAB8 / 2CCAD4 / 2CCAF0 setters        0x2CCB38 .. 0x2CCC28 help screen
 *
 * Quirks kept:
 *  - options_2CA07C stores a dword into ptc_desc_shift (particle_2A6134 reads its low byte);
 *  - options_2CA2C0 / 2CA360 draw with the back canvas's GET / DRAW bitmap entries at
 *    (640, 480) / (0, 0) of whatever canvas is current;
 *  - options_2CBEA0's page 2 path is code placed after options_2CB254's return;
 *  - disabled options draw their labels in colour 0xFD (options 21..24: always 0xF7).
 */
#include "g10_c.h"

#ifndef TN_WATCOM_ABI
extern int port_game_options_up;        /* port/ui/options.c */
extern int port_options_open;            /* port/ui/options.c: the Freefall options screen is up */
void snd_finish_pending(void);           /* sound.c */
#ifndef TN_WATCOM_ABI
void port_help_open(void);              /* port/ui/help.c: Freefall's help screen over the game's */
void port_help_close(void);
int port_help_key(int code);
#endif
#endif

#ifndef TN_LINK_ORIGINAL_DATA
long opt_val[OPT_N];
long opt_allowed[OPT_N];
OptApply opt_apply[OPT_N];            /* filled in by the game: see the notes */
OptButton opt_sliders[5];
OptButton opt_buttons[0x45];
long opt_page_count[4] = { 4, 4, 3, 3 };
short opt_page_btn[4][6] = {
    { 3, 4, 5, 6, -1, -1 }, { 39, 40, 41, 42, -1, -1 },
    { 35, 36, 37, 38, -1, -1 }, { 43, 44, 45, -1, -1, -1 },
};
uchar opt_hook_off[0x17] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
uchar opt_help_hook_off[0x17] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
long opt_page_title[4] = { 0x1730018, 0x1730022, 0x1730027, 0x173002C };
long opt_page_bm[4] = { 0x1880011, 0x1880011, 0x1880012, 0x1880012 };
long *opt_page_val[4] = { &opt_val[6], &opt_val[13], 0, &opt_val[18] };
ulong opt_hook_saved;
void *opt_save_buf;
long opt_cursor;
long opt_blocking;
long opt_page;
long opt_no_show;
uchar opt_cursor_drawn;
uchar opt_442849;
LGRect opt_saved_rect;
uchar opt_saved_pal[0x300];
long opt_35D100 = 1;
uchar opt_mouse_down;
uchar opt_in_click;
uchar opt_dragging;
uchar opt_35DA32, opt_35DA33;
long opt_35DA88;
uchar opt_35DA93, opt_35DA94;
uchar game_35ADE1;
#endif

#define FONT 0x164
#define GET_DEVICE_BITMAP 207     /* canvas table: get bitmap (+2 * type) */

/* The button tables hold the original functions' addresses; the handler compares them with
 * the closing buttons. */
#ifdef TN_WATCOM_ABI
#define IS_FN(p, f, a) ((void *)(p) == WATCOM_FN(f) || (void *)(p) == (void *)(a))
#else
#define IS_FN(p, f, a) ((void *)(p) == (void *)(f))
#endif

/* a frame (FrameDesc: bitmap, bits at +0x1C) drawn unclipped / clipped */
static void ubitmap(long ref, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);

    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, x, y);
}

static void bitmap(long ref, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);

    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, x, y);
}

/* ubitmap at the top left of rectangle rect (the rectangle is fetched first) */
static void ubitmap_at(long rect, long ref)
{
    LGRect *r = RefGet(rect);
    grs_bitmap *bm = RefGet(ref);

    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, r->ul.x, r->ul.y);
}

/* label: text first, then its rectangle */
static void label_tr(long rect, long text)
{
    char *t = RefGet(text);

    options_2CAAC0(RefGet(rect), t, FONT, &tnui_back_canvas);
}

/* label: rectangle first, then the text */
static void label_rt(long rect, long text)
{
    LGRect *r = RefGet(rect);

    options_2CAAC0(r, RefGet(text), FONT, &tnui_back_canvas);
}

static void set_pal_of(Id id, long frame)
{
    uchar *f = RefGet(frame);
    long off = *(long *)(f + 0x18);

    if (off) {
        uchar *p = (uchar *)ResGet(id) + *(long *)(f + 0x18);
        gr_set_pal(*(short *)p, *(short *)(p + 2), p + 4);
    }
}

static void restore_hooks(void)
{
    ulong saved = opt_hook_saved;
    int i;

    for (i = 0; i <= 0x16; i++) {
        if (!(saved & (0xFFFFFFFFu << i)))
            break;
        if (saved & (1u << i))
            game_hook_mask |= 1u << i;
    }
}

static void set_cursor(void)
{
    if (opt_val[4] == 2) {
        MouseSetCursor(0x1880006);
        tnm_cursor.hot.x = 9;
        tnm_cursor.hot.y = 0;
    } else {
        MouseSetCursor(0x1880005);
    }
}

/* ------------------------------------------------------------------ setters */
void options_2CA064(long v)
{
    ffpipe_35C100 = v;
}

void options_2CA06C(long v)
{
    ffpipe_29E180(v);
}

void options_2CA074(long v)
{
    rndsky_2A6BD0(v);
}

void options_2CA07C(long v)
{
    ptc_desc_shift = 3 - v;
}

void options_2CA09C(long v)
{
    switch (v) {
    case 1:
        opt_val[13] = 1; opt_val[16] = 0; opt_val[14] = 0; opt_val[15] = 0; opt_val[18] = 1;
        break;
    case 2:
        opt_val[13] = 2; opt_val[16] = 0; opt_val[14] = 0; opt_val[15] = 1; opt_val[18] = 2;
        break;
    case 3:
        opt_val[13] = 2; opt_val[16] = 1; opt_val[14] = 1; opt_val[15] = 1; opt_val[18] = 2;
        break;
    case 4:
        opt_val[13] = 3; opt_val[16] = 1; opt_val[14] = 1; opt_val[15] = 1; opt_val[18] = 3;
        break;
    }
    options_2CC01C();
}

void options_2CA180(long v)
{
    (void)v;
    opt_val[6] = 0;
}

void options_2CA18C(long v)
{
    opt_mouse_down = 0;
    MouseSetLefty(v == 1 ? 0 : 1);
}

void options_2CA1AC(long v)
{
    game_35ADE1 = v == 1;
}

void options_2CA1B8(long i, long v)
{
    if (i != -1 && i < OPT_N && opt_apply[i].fn2)
        WATCOM_CALL(opt_apply[i].fn2, v);
}

void options_2CA1E4(void)
{
    int i;
    long v;

    for (i = 0; i < OPT_N; i++) {
        if (!opt_apply[i].fn)
            continue;
        if (opt_apply[i].pct)
            v = opt_applied[i] * 100 / 30;
        else
            v = opt_applied[i];
        WATCOM_CALL(opt_apply[i].fn, v);
        opt_apply[i].value = v;
    }
}

#ifndef TN_WATCOM_ABI
/* Freefall's options screen (port/ui/options.c) shows some of these options as the game's own:
 * FX, music and speech volume (1..3: 0..29) and Stereo sound (11: 1 Normal, 2 Reversed).  The
 * main thread asks for a value; the game's thread puts it in force (port_game_options_apply,
 * from port_frame_limit) as this screen's Accept does, through opt_apply */
static volatile long ff_want[OPT_N];
static volatile int ff_asked[OPT_N];

long port_game_option(int i)
{
    if (i < 0 || i >= OPT_N)
        return 0;
    return ff_asked[i] ? ff_want[i] : opt_applied[i];
}

int port_game_option_allowed(int i)
{
    if (i == 8 && game_in_mission)      /* the difficulty: between missions only (options_2CB584) */
        return 0;
    return i >= 0 && i < OPT_N && opt_allowed[i] != 0;
}

void port_game_option_set(int i, long v)
{
    if (!port_game_option_allowed(i))
        return;
    ff_want[i] = v;
    ff_asked[i] = 1;
}

/* a sound for the Freefall screens (button clicks), asked on the main thread and played here on
 * the game's (a ring of 16) */
static volatile unsigned short ff_snd[16];
static volatile unsigned ff_snd_in, ff_snd_out;

void port_game_sound(int id)
{
    unsigned n = ff_snd_in;
    if (!id || n - ff_snd_out >= 16)
        return;
    ff_snd[n % 16] = (unsigned short)id;
    ff_snd_in = n + 1;
}

void port_game_options_apply(void)
{
    int i;
    long v;

    /* the sounds' ends (their end callbacks: the stereo test's "right" after its "left"), which
     * the game's periodic task runs, but the game stands still under the options screen; the
     * test and the volume samples also wait for their last sound's end before playing again,
     * so they played once and never again (owner's test) */
    if (port_options_open)
        snd_finish_pending();
    while (ff_snd_out != ff_snd_in) {
        SndPlay(ff_snd[ff_snd_out % 16], 0, 0x40, 1, -1, 0x40, 0);    /* as the game's buttons */
        ff_snd_out++;
    }

    for (i = 0; i < OPT_N; i++) {
        if (!ff_asked[i])
            continue;
        ff_asked[i] = 0;
        opt_val[i] = opt_applied[i] = ff_want[i];
        if (!opt_apply[i].fn)
            continue;
        v = opt_apply[i].pct ? opt_applied[i] * 100 / 30 : opt_applied[i];
        WATCOM_CALL(opt_apply[i].fn, v);
        opt_apply[i].value = v;
        /* and what the game's screen does after a click or a slider's move (options_2CA1B8):
         * the FX and speech volumes play a sample at the new volume, Stereo sound its
         * "left, right" test (owner) */
        if (opt_apply[i].fn2 && opt_apply[i].fn2 != opt_apply[i].fn)
            options_2CA1B8(i, v);
    }
}
#endif

/* ------------------------------------------------------------------ screen state */
void options_2CA23C(void)
{
    switch (game_43D318) {
    case 1:
        if (hud_helmet == 0) {
            gr_set_canvas(&tnui_back_canvas);
            ubitmap(0x2EE0000, 0, 0);
            helm_26214C();
            hud_38B9AE = 1;
        }
        return;
    case 2:
        shell_2C12DC();
        return;
    }
    if (opt_35D100 == 0)
        tnui_add_dirty_rect(&tnui_full_rect);
}

void options_2CA2C0(long a)
{
    gr_get_pal(0, 0x100, opt_saved_pal);
    opt_blocking = a;
    opt_saved_rect = tnui_full_rect;
    if (opt_35D100 && game_43D318 != 1)
        WATCOM_CALL(grd_canvas_table[GET_DEVICE_BITMAP + 2 * tnui_back_canvas.bm.type],
                    (long)&tnui_back_canvas, 0x280, 0x1E0);
    opt_cursor = tnm_cursor_ref;
    if (a) {
        KeyAddHandler(WATCOM_FN(options_2CA44C), 0, 0, 0);
        MouseAddHandler(&tnui_full_rect, 0xFF, WATCOM_FN(options_2CA44C));
    }
}

void options_2CA360(void)
{
    LGPoint max;

    gr_set_pal(0, 0x100, opt_saved_pal);
    if (opt_blocking) {
        KeyRemoveHandler(WATCOM_FN(options_2CA44C), 0);
        MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(options_2CA44C));
    }
    tnui_full_rect = opt_saved_rect;
    max.x = (ushort)tnui_full_rect.lr.x / 10;
    max.y = (ushort)(tnui_full_rect.lr.y << 5) / 200;
    tnmouse_set_screensize(tnui_full_rect.lr, max);
    if (opt_cursor)
        MouseSetCursor(opt_cursor);
    options_2CA23C();
    if (opt_35D100 && game_43D318 != 1)
        WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * tnui_back_canvas.bm.type],
                    (long)&tnui_back_canvas, 0, 0);
}

uchar options_2CA44C(void *ev)
{
    (void)ev;
    return 1;
}

/* ------------------------------------------------------------------ handler */
static void slider_set(OptButton *s, TnEvent *e)
{
    long v = (e->pt.x - s->rect.ul.x) * 100 / (s->rect.lr.x - s->rect.ul.x);

    WATCOM_CALL(s->fn, v * 30 / 100);
    options_2CA1B8(s->idx, v);
}

uchar options_2CA450(TnEvent *e)
{
    OptButton *b;
    ushort key;

    switch (e->type) {
    case 0x100:
        key = *(ushort *)&e->pad;
        if (key == 0x4F || key == 0x6F) {
            memcpy(opt_applied, opt_val, sizeof(opt_val));
            options_2CA1E4();
            options_2CBC08(0);
            return 1;
        }
        if (key == 0x458 || key == 0x478) {
            if (main_245334())
                options_2CBC08(1);
            return 1;
        }
        if (key == 0x2837) {
            pcx_screenshot(0, "nova");
            return 1;
        }
        return 0;
    case 0x40:
        if (opt_dragging) {
            b = options_2CA834(opt_sliders, e->pt, 5);
            if (!b)
                return 1;
            slider_set(b, e);
            GfxBlit(&tnui_full_rect);
            return 1;
        }
        if (!opt_mouse_down || !options_2CA834(opt_sliders, e->pt, 5))
            return 1;
        /* fall through: a press moved onto a slider */
    case 1:
        opt_mouse_down = 1;
        b = options_2CA834(opt_buttons, e->pt, 0x45);
        if (b) {
            if (IS_FN(b->fn, options_2CBBC8, 0x2CBBC8) || IS_FN(b->fn, options_2CBBF8, 0x2CBBF8) ||
                IS_FN(b->fn, options_2CBBEC, 0x2CBBEC) || IS_FN(b->fn, options_2CCAF8, 0x2CCAF8))
                opt_no_show = 1;
            else
                MouseHide();
            opt_in_click = 1;
            if (b->snd)
                SndPlay(b->snd, 0, 0x40, 1, -1, 0x40, 0);
            WATCOM_CALL(b->fn, b->arg);
            opt_in_click = 0;
            if (!IS_FN(b->fn, options_2CBDA8, 0x2CBDA8) && !IS_FN(b->fn, options_2CBE10, 0x2CBE10) &&
                !IS_FN(b->fn, options_2CBDDC, 0x2CBDDC) && !IS_FN(b->fn, options_2CBE44, 0x2CBE44) &&
                b->idx != 0)
                options_2CA1B8(b->idx, b->arg);
            if (!IS_FN(b->fn, options_2CBBC8, 0x2CBBC8) && !IS_FN(b->fn, options_2CBBF8, 0x2CBBF8) &&
                !IS_FN(b->fn, options_2CBBEC, 0x2CBBEC))
                GfxBlit(&tnui_full_rect);
            if (opt_no_show == 0)
                MouseShow();
            else
                opt_no_show = 0;
            return 1;
        }
        b = options_2CA834(opt_sliders, e->pt, 5);
        if (!b)
            return 1;
        opt_dragging = 1;
        MouseHide();
        MouseConstrain(&b->rect);
        slider_set(b, e);
        GfxBlit(&tnui_full_rect);
        return 1;
    case 2:
        opt_mouse_down = 0;
        if (!opt_dragging)
            return 1;
        b = options_2CA834(opt_sliders, e->pt, 5);
        if (b)
            slider_set(b, e);
        MouseClearStencil();
        GfxBlit(&tnui_full_rect);
        MouseShow();
        opt_dragging = 0;
        return 1;
    }
    return 1;
}

OptButton *options_2CA834(OptButton *t, LGPoint pt, long n)
{
    long i;

    for (i = 0; i < n; i++, t++) {
        if (!t->enabled)
            continue;
        t->rect = *(LGRect *)RefGet(t->ref);
        if (pt.y >= t->rect.ul.y && pt.y < t->rect.lr.y && pt.x >= t->rect.ul.x && pt.x < t->rect.lr.x)
            return t;
    }
    return 0;
}

void options_2CA890(void)
{
    LGPoint max = { 9, 13 };

    set_cursor();
    tnmouse_set_screensize(tnui_full_rect.lr, max);
}

/* ------------------------------------------------------------------ open */
void options_2CA8FC(void)
{
    LGPoint max = { 9, 13 };
    ulong mask, bit, saved;
    int i;

    opt_35DA32 = 0;
    opt_35DA33 = 0;
    opt_cursor_drawn = tnm_cursor_drawn;
    if (opt_cursor_drawn)
        MouseHide();
    options_2CA2C0(1);
    opt_save_buf = ffmem_2C4F40(0x4B000);
    GfxSaveAndSetMode(0x14, opt_save_buf);
    ResLock(0x188);
    ResLock(0x189);
    set_pal_of(0x188, 0x1880000);
    options_2CADE4(0);
    options_2CAD30(&tnui_back_canvas);
    options_2CABAC(&tnui_back_canvas);
    options_2CAC54(&tnui_back_canvas);
    GfxBlit(&tnui_full_rect);
    saved = 0;
    for (i = 0; i <= 0x16; i++) {
        mask = game_hook_mask;
        if (!(mask & (0xFFFFFFFFu << i)))
            break;
        bit = 1u << i;
        if ((mask & bit) && opt_hook_off[i]) {
            saved |= bit;
            game_hook_mask = mask & ~bit;
        }
    }
    opt_hook_saved = saved;
    if (game_in_mission) {
        GameTimePause();
        game_slow = 1;
    }
    KeyAddHandler(WATCOM_FN(options_2CA450), 0, 0, 0);
    MouseAddHandler(&tnui_full_rect, 0xFF, WATCOM_FN(options_2CA450));
    set_cursor();
    tnmouse_set_screensize(tnui_full_rect.lr, max);
    MouseShow();
    if (opt_mouse_down)
        opt_mouse_down = 0;
#ifndef TN_WATCOM_ABI
    port_game_options_up = 1;           /* Freefall shows its options button (port/ui/options.c) */
#endif
}

/* ------------------------------------------------------------------ drawing */
void options_2CAAC0(LGRect *r, char *text, long font, grs_canvas *c)
{
    char buf[200];
    ushort id = (ushort)font;
    grs_font *f;
    short w, h;

    gr_set_canvas(c);
    f = ResLock(id);
    sprintf(buf, "%s", text);
    w = gr_font_string_width(f, buf);
    h = gr_font_string_height(f, buf);
    GfxDrawText(id, buf, r->ul.x + (r->lr.x - r->ul.x - w) / 2, r->ul.y + (r->lr.y - r->ul.y - h) / 2);
    ResUnlock(id);
}

void options_2CAB58(long rect, long text, long font, grs_canvas *c)
{
    LGRect *r = RefGet(rect);

    options_2CAAC0(r, RefGet(text), (ushort)font, c);
}

void options_2CAB84(void)
{
    options_2CAD30(&tnui_back_canvas);
    options_2CABAC(&tnui_back_canvas);
    options_2CAC54(&tnui_back_canvas);
    GfxBlit(&tnui_full_rect);
}

static void label_c(long rect, long text, grs_canvas *c)
{
    char *t = RefGet(text);

    options_2CAAC0(RefGet(rect), t, FONT, c);
}

void options_2CABAC(grs_canvas *c)
{
    gr_set_canvas(c);
    grd_canvas->gc.fcolor = 0xF7;
    label_c(0x189003B, 0x1730035, c);
    label_c(0x189001B, 0x173001E, c);
    label_c(0x189003C, 0x1730050, c);
    label_c(0x189001A, 0x173001D, c);
}

void options_2CAC54(grs_canvas *c)
{
    gr_set_canvas(c);
    grd_canvas->gc.fcolor = 0xF7;
    label_c(0x1890003, 0x1730000, c);
    grd_canvas->gc.fcolor = 0xF7;
    options_2CAF94(opt_val[0]);
    options_2CB044(opt_val[1]);
    options_2CB0F4(opt_val[2]);
    options_2CB1A4(opt_val[3]);
    options_2CB254(opt_val[4]);
    options_2CBEA0(opt_val[6]);
    options_2CB584(opt_val[8]);
    options_2CB7B4(opt_val[9]);
    options_2CB918(opt_val[11]);
    options_2CBA70(opt_val[12]);
    options_2CC948(opt_val[24]);
    options_2CC7D8(opt_val[23]);
    options_2CC5DC(opt_val[22]);
    options_2CC3D0(opt_val[21]);
    options_2CC234(opt_val[19]);
}

void options_2CAD30(grs_canvas *c)
{
    gr_set_canvas(c);
    bitmap(0x1880000, 0, 0);
}

void options_2CAD68(LGRect *r, long v)
{
    long i;

    for (i = 0; i < 30; i++)
        options_2CAD9C(r, i, 0x1880002);
    options_2CAD9C(r, v, 0x1880003);
}

void options_2CAD9C(LGRect *r, long i, long ref)
{
    short x = r->ul.x + i * 6;
    short y = r->ul.y;

    bitmap(ref, x, y);
}

/* ------------------------------------------------------------------ defaults */
void options_2CADE4(long a)
{
    int i;

    if (a == 0) {
        memcpy(opt_val, opt_applied, sizeof(opt_val));
        return;
    }
    opt_442849 = 0;
    opt_page = 0;
    for (i = 0; i <= 6; i++)
        opt_allowed[i] = 1;
    opt_allowed[7] = 0;
    opt_allowed[8] = 1;
    opt_allowed[9] = (!game_low_mem || game_hires) ? 1 : 0;
    for (i = 10; i <= 19; i++)
        opt_allowed[i] = 1;
    for (i = 21; i <= 24; i++)
        opt_allowed[i] = 1;
    opt_val[0] = 0xF;
    opt_val[1] = opt_val[2] = opt_val[3] = 0x18;
    opt_val[4] = opt_val[5] = 1;
    opt_val[6] = 4;
    opt_val[7] = 0;
    opt_val[8] = 3;
    opt_val[9] = opt_val[10] = opt_val[11] = opt_val[12] = 1;
    opt_val[13] = 3;
    opt_val[14] = opt_val[15] = opt_val[16] = opt_val[17] = 1;
    opt_val[18] = 3;
    opt_val[19] = 2;
    opt_val[21] = opt_val[22] = opt_val[23] = opt_val[24] = 1;
#ifndef TN_WATCOM_ABI
    /* Freefall's defaults (owner's choice): the 320 x 400 mission screen with its high detail
     * HUD where the game allows it, and Ground detail Excellent (the game: High).  Master
     * detail shows no choice then, as when the player picks a ground detail. */
    if (opt_allowed[9])
        opt_val[9] = 2;
    opt_val[13] = 4;
    opt_val[6] = 0;
#endif
    memcpy(opt_applied, opt_val, sizeof(opt_val));
    options_2CA1E4();
}

/* ------------------------------------------------------------------ the options */
/* sliders 0..3: 30 ticks, the value's tick lit */
static void slider(int n, long v, long rect, long lrect, long ltext)
{
    LGRect *r;
    long cur, i;

    gr_set_canvas(&tnui_back_canvas);
    if (opt_allowed[n] == 0) {
        grd_canvas->gc.fcolor = 0xFD;
    } else {
        opt_val[n] = v;
        r = RefGet(rect);
        cur = opt_val[n];
        for (i = 0; i < 30; i++)
            options_2CAD9C(r, i, 0x1880002);
        options_2CAD9C(r, cur, 0x1880003);
        grd_canvas->gc.fcolor = 0xF7;
    }
    label_tr(lrect, ltext);
    opt_sliders[n].enabled = opt_allowed[n] != 0;
}

void options_2CAF94(long v)
{
    slider(0, v, 0x1890028, 0x1890026, 0x1730046);
}

void options_2CB044(long v)
{
    slider(1, v, 0x1890006, 0x1890004, 0x1730001);
}

void options_2CB0F4(long v)
{
    slider(2, v, 0x1890009, 0x1890007, 0x1730002);
}

void options_2CB1A4(long v)
{
    slider(3, v, 0x189000C, 0x189000A, 0x1730003);
}

/* a radio group: the old choice's mark erased, the new one drawn */
static void radio(long *val, long v, long base, long off_bm, long on_bm)
{
    ubitmap_at(base + *val, off_bm);
    *val = v;
    {
        LGRect *r = RefGet(base + v);
        grs_bitmap *bm;

        grd_canvas->gc.fcolor = 0xF7;
        bm = RefGet(on_bm);
        bm->bits = (uchar *)bm + 0x1C;
        WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, r->ul.x, r->ul.y);
    }
}

void options_2CB254(long v)
{
    gr_set_canvas(&tnui_back_canvas);
    if (opt_allowed[4] == 0)
        grd_canvas->gc.fcolor = 0xFD;
    else
        radio(&opt_val[4], v, 0x1890017, 0x1880007, 0x1880001);
    label_tr(0x1890017, 0x173004A);
    label_tr(0x1890018, 0x173004B);
    label_tr(0x1890019, 0x173004C);
    if (opt_allowed[4] == 0) {
        opt_buttons[22].enabled = 0;
        opt_buttons[23].enabled = 0;
        return;
    }
    opt_buttons[22].enabled = 1;
    opt_buttons[23].enabled = 1;
    if (v == 2) {
        MouseSetCursor(0x1880006);
        tnm_cursor.hot.x = 9;
        tnm_cursor.hot.y = 0;
    } else {
        MouseSetCursor(0x1880005);
    }
}

void options_2CB584(long v)
{
    long i;

    gr_set_canvas(&tnui_back_canvas);
    if (game_in_mission)
        grd_canvas->gc.fcolor = 0xFD;
    else
        radio(&opt_val[8], v, 0x1890035, 0x1880007, 0x1880001);
    for (i = 0; i < 6; i++)
        label_tr(0x1890035 + i, 0x173000E + i);
    if (game_in_mission) {
        grd_canvas->gc.fcolor = 0xF7;
        label_tr(0x1890035 + opt_val[8], 0x173000E + opt_val[8]);
    }
    for (i = 46; i <= 50; i++)
        opt_buttons[i].enabled = game_in_mission ? 0 : 1;
}

void options_2CB7B4(long v)
{
    gr_set_canvas(&tnui_back_canvas);
    if (opt_allowed[9] == 0)
        grd_canvas->gc.fcolor = 0xFD;
    else
        radio(&opt_val[9], v, 0x1890023, 0x1880007, 0x1880001);
    label_rt(0x1890023, 0x1730047);
    label_rt(0x1890024, 0x1730048);
    label_rt(0x1890025, 0x1730049);
    opt_buttons[7].enabled = opt_buttons[8].enabled = opt_allowed[9] != 0;
}

void options_2CB918(long v)
{
    gr_set_canvas(&tnui_back_canvas);
    if (opt_allowed[11] == 0)
        grd_canvas->gc.fcolor = 0xFD;
    else
        radio(&opt_val[11], v, 0x189000D, 0x1880007, 0x1880001);
    label_tr(0x189000D, 0x173000B);
    label_tr(0x189000E, 0x173000C);
    label_tr(0x189000F, 0x173000D);
    opt_buttons[30].enabled = opt_buttons[31].enabled = opt_allowed[11] != 0;
}

void options_2CBA70(long v)
{
    gr_set_canvas(&tnui_back_canvas);
    if (opt_allowed[12] == 0)
        grd_canvas->gc.fcolor = 0xFD;
    else
        radio(&opt_val[12], v, 0x1890010, 0x1880007, 0x1880001);
    label_tr(0x1890010, 0x1730004);
    label_tr(0x1890011, 0x1730005);
    label_tr(0x1890012, 0x1730006);
    opt_buttons[20].enabled = opt_buttons[21].enabled = opt_allowed[12] != 0;
}

/* ------------------------------------------------------------------ closing */
void options_2CBBC8(void)
{
    memcpy(opt_applied, opt_val, sizeof(opt_val));
    options_2CA1E4();
    options_2CBC08(0);
}

void options_2CBBEC(void)
{
    options_2CA1E4();
    options_2CBC08(0);
}

void options_2CBBF8(void)
{
    if (main_245334())
        options_2CBC08(1);
}

void options_2CBC08(uchar clear)
{
    TnEvent e;

#ifndef TN_WATCOM_ABI
    port_game_options_up = 0;
#endif
    if (opt_mouse_down) {
        e.type = 2;
        e.pad = 0;              /* not set by the original; type 2 does not read it */
        e.pt.x = 0x140;
        e.pt.y = 0xC8;
        options_2CA450(&e);
    }
    MouseHide();
    KeyRemoveHandler(WATCOM_FN(options_2CA450), 0);
    MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(options_2CA450));
    ResUnlock(0x188);
    ResUnlock(0x189);
    restore_hooks();
    if (game_in_mission) {
        game_slow = 0;
        GameTimeResume2();
    }
    GfxRestoreMode(opt_save_buf);
    ffmem_2C4FE4(opt_save_buf);
    if (clear)
        memset(opt_saved_pal, 0, 0x300);
    options_2CA360();
    MouseShow();
    if (!opt_cursor_drawn)
        MouseHide();
}

/* ------------------------------------------------------------------ detail pages */
void options_2CBCF4(long dir)
{
    options_2CBD04(dir);
    options_2CBEA0(opt_val[6]);
}

static void page_enable(long page, uchar on)
{
    int k;
    short b;

    for (k = 0; k < 6; k++) {
        b = opt_page_btn[page][k];
        if (b != -1)
            opt_buttons[b].enabled = on;
    }
}

void options_2CBD04(long dir)
{
    long page = opt_page;
    int i;

    for (i = 0; i < 4; i++)
        page_enable(i, 0);
    page = (dir ? page + 1 : page + 3) % 4;
    page_enable(page, 1);
    opt_page = page;
}

static void toggle(long i)
{
    long v = opt_val[i] == 0;

    opt_442849 = 0;
    opt_val[i] = v;
    options_2CA1B8(i, v);
    options_2CBEA0(opt_val[6]);
}

void options_2CBDA8(void)
{
    toggle(14);
}

void options_2CBDDC(void)
{
    toggle(15);
}

void options_2CBE10(void)
{
    toggle(16);
}

void options_2CBE44(void)
{
    toggle(17);
}

void options_2CBE78(long v)
{
    opt_val[13] = v;
    opt_442849 = 0;
    options_2CBEA0(opt_val[6]);
}

void options_2CBE8C(long v)
{
    opt_val[18] = v;
    opt_442849 = 0;
    options_2CBEA0(opt_val[6]);
}

static void mark(long rect, long ref)
{
    ubitmap_at(rect, ref);
}

void options_2CBEA0(long v)
{
    long *val[4];
    long bm[4], title[4];
    LGRect *r;
    long i, page;

    memcpy(val, opt_page_val, sizeof(val));
    memcpy(title, opt_page_title, sizeof(title));
    memcpy(bm, opt_page_bm, sizeof(bm));
    gr_set_canvas(&tnui_back_canvas);
    r = RefGet(0x1890000);
    {
        grs_bitmap *b = RefGet(bm[opt_page]);

        b->bits = (uchar *)b + 0x1C;
        WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * b->type], (long)b,
                    (short)(r->ul.x - 2), r->ul.y);
    }
    mark(0x189001E, 0x1880010);
    label_rt(0x189001E, title[opt_page]);
    if (opt_page == 0) {
        opt_val[6] = v;
        opt_442849 = 0;
    }
    page = opt_page;
    if (page == 2) {
        if (opt_val[14])
            mark(0x189001F, 0x1880001);
        if (opt_val[15])
            mark(0x1890020, 0x1880001);
        if (opt_val[16])
            mark(0x1890021, 0x1880001);
    } else if ((ulong)page <= 3) {
        if (opt_page != 0 || opt_val[6] != 0)
            mark(0x189001E + *val[opt_page], 0x1880001);
    }
    for (i = 0; i < opt_page_count[opt_page]; i++) {
        LGRect *lr = RefGet(0x189001F + i);

        options_2CAAC0(lr, RefGet(title[opt_page] + i + 1), FONT, &tnui_back_canvas);
    }
    r = RefGet(0x1890001);
    {
        grs_bitmap *b = RefGet(0x1880008);

        b->bits = (uchar *)b + 0x1C;
        WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * b->type], (long)b,
                    (short)(r->ul.x - 1), (short)(r->ul.y - 1));
    }
    r = RefGet(0x1890002);
    {
        grs_bitmap *b = RefGet(0x1880009);

        b->bits = (uchar *)b + 0x1C;
        WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * b->type], (long)b,
                    r->ul.x, (short)(r->ul.y - 1));
    }
    options_2CC01C();
}

void options_2CC01C(void)
{
    LGRect *r = RefGet(0x189001C);
    short x = r->ul.x - 0x50, y = r->ul.y - 0x32;
    short cl, ct, cr, cb;
    grs_canvas *c = grd_canvas;

    cl = c->gc.clip.i.left;
    ct = c->gc.clip.i.top;
    cr = c->gc.clip.i.right;
    cb = c->gc.clip.i.bot;
    c->gc.clip.i.sten = 0;
    c->gc.clip.i.left = r->ul.x;
    c->gc.clip.i.top = r->ul.y;
    c->gc.clip.i.right = r->lr.x;
    c->gc.clip.i.bot = r->lr.y;
    bitmap(opt_val[15] ? 0x18A0002 : 0x18A0003, x, y);
    bitmap(opt_val[14] ? 0x18A0000 : 0x18A0001, x, y);
    switch (opt_val[13]) {
    case 1:
        bitmap(0x18A0009, x, y);
        break;
    case 2:
        bitmap(0x18A0008, x, y);
        break;
    case 3:
        bitmap(0x18A0007, x, y);
        break;
    }
    if (opt_val[16] == 0)
        bitmap(0x18A0004, x, y);
    if (opt_val[18] == 3)
        bitmap(0x18A0005, x, y);
    else if (opt_val[18] == 2)
        bitmap(0x18A0006, x, y);
    c = grd_canvas;
    c->gc.clip.i.sten = 0;
    c->gc.clip.i.left = cl;
    c->gc.clip.i.top = ct;
    c->gc.clip.i.right = cr;
    c->gc.clip.i.bot = cb;
}

void options_2CC234(long v)
{
    gr_set_canvas(&tnui_back_canvas);
    if (opt_allowed[19] == 0)
        grd_canvas->gc.fcolor = 0xFD;
    else
        radio(&opt_val[19], v, 0x1890014, 0x188000D, 0x188000C);
    label_rt(0x1890013, 0x1730007);
    label_rt(0x1890014, 0x1730008);
    label_rt(0x1890015, 0x1730009);
    label_rt(0x1890016, 0x173000A);
    opt_buttons[51].enabled = opt_buttons[52].enabled = opt_buttons[53].enabled =
        opt_allowed[19] != 0;
}

/* two check boxes (bits 1 and 2 of the value); a = 4 / 8 / 0x10 / 0x20 sets the value to
 * 1 / 2 / 0 / 3, otherwise a click toggles the bits in a */
static void checks(int n, long a, long r1, long r2)
{
    long *val = &opt_val[n];
    uchar b1, b2;

    gr_set_canvas(&tnui_back_canvas);
    if (opt_allowed[n] == 0) {
        grd_canvas->gc.fcolor = 0xFD;
    } else {
        switch (a & 0x3C) {
        case 4:
            *val = 1;
            break;
        case 8:
            *val = 2;
            break;
        case 0x10:
            *val = 0;
            break;
        case 0x20:
            *val = 3;
            break;
        default:
            if (opt_in_click)
                *val ^= a;
            break;
        }
        b2 = *val & 2;
        b1 = *val & 1;
        ubitmap_at(r1, b1 ? 0x188000A : 0x188000B);
        ubitmap_at(r2, b2 ? 0x188000A : 0x188000B);
        grd_canvas->gc.fcolor = 0xF7;
    }
    grd_canvas->gc.fcolor = 0xF7;
}

void options_2CC3D0(long a)
{
    checks(21, a, 0x189002A, 0x189002B);
    label_rt(0x1890029, 0x173003A);
    label_rt(0x189002A, 0x173003B);
    label_rt(0x189002B, 0x173003C);
    opt_buttons[56].enabled = opt_buttons[57].enabled = opt_allowed[21] != 0;
}

void options_2CC5DC(long a)
{
    checks(22, a, 0x189002D, 0x189002E);
    label_rt(0x189002C, 0x173003D);
    label_rt(0x189002D, 0x173003E);
    label_rt(0x189002E, 0x173003F);
    opt_buttons[58].enabled = opt_buttons[59].enabled = opt_allowed[22] != 0;
}

void options_2CC7D8(long v)
{
    gr_set_canvas(&tnui_back_canvas);
    if (opt_allowed[23] == 0)
        grd_canvas->gc.fcolor = 0xFD;
    else
        radio(&opt_val[23], v, 0x189002F, 0x188000B, 0x188000A);
    grd_canvas->gc.fcolor = 0xF7;
    label_rt(0x189002F, 0x1730040);
    label_rt(0x1890030, 0x1730041);
    label_rt(0x1890031, 0x1730042);
    opt_buttons[60].enabled = opt_buttons[61].enabled = opt_allowed[23] != 0;
}

void options_2CC948(long v)
{
    gr_set_canvas(&tnui_back_canvas);
    if (opt_allowed[24] == 0)
        grd_canvas->gc.fcolor = 0xFD;
    else
        radio(&opt_val[24], v, 0x1890032, 0x188000B, 0x188000A);
    grd_canvas->gc.fcolor = 0xF7;
    label_rt(0x1890032, 0x1730043);
    label_rt(0x1890033, 0x1730044);
    label_rt(0x1890034, 0x1730045);
    opt_buttons[62].enabled = opt_buttons[63].enabled = opt_allowed[24] != 0;
}

void options_2CCAB8(long v)
{
    if (v != radio_text_flags)
        radio_stop_all();
    radio_text_flags = v;
}

void options_2CCAD4(long v)
{
    if (v != radio_msg_flags)
        radio_stop_all();
    radio_msg_flags = v;
}

void options_2CCAF0(long v)
{
    opt_35DA88 = v;
}

void options_2CCAF8(void)
{
    void *text = RefGet(0x16F00B9);

    if (game_in_mission && main_246094(text)) {
        game_35A95C = 1;
        game_35B77C = 1;
        ordn_goal_c = 1;
        options_2CBC08(1);
    }
}

/* ------------------------------------------------------------------ help screen */
uchar options_2CCB38(TnEvent *e)
{
    ulong t = e->type;

#ifndef TN_WATCOM_ABI
    if (t == 0x100 && port_help_key(*(short *)((uchar *)e + 4)))
        return 1;                       /* H on Freefall's help screen: the game's own */
#endif
    if (t == 1 || t == 4 || t == 0x100)
        options_2CCB58();
    return 1;
}

void options_2CCB58(void)
{
    KeyRemoveHandler(WATCOM_FN(options_2CCB38), 0);
    MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(options_2CCB38));
    ResUnlock(0x187);
#ifndef TN_WATCOM_ABI
    port_help_close();
#endif
    restore_hooks();
    game_slow = 0;
    GameTimeResume2();
    GfxRestoreMode(opt_save_buf);
    ffmem_2C4FE4(opt_save_buf);
    options_2CA360();
    MouseShow();
}

void options_2CCBE0(void)
{
    gr_set_canvas(&tnui_back_canvas);
    bitmap(0x1870000, 0, 0);
    GfxBlit(&tnui_full_rect);
}

void options_2CCC28(void)
{
    ulong mask, bit, saved;
    int i;

    opt_35DA93 = 0;
    opt_35DA94 = 0;
#ifndef TN_WATCOM_ABI
    port_help_open();                   /* the mission's screen read back while it is still up */
#endif
    MouseHide();
    options_2CA2C0(0);
    opt_save_buf = ffmem_2C4F40(0x4B000);
    GfxSaveAndSetMode(0x14, opt_save_buf);
    ResLock(0x187);
    set_pal_of(0x187, 0x1870000);
    mask = game_hook_mask;
    saved = 0;
    for (i = 0; i <= 0x16; i++) {
        if (!(mask & (0xFFFFFFFFu << i)))
            break;
        bit = 1u << i;
        if ((mask & bit) && opt_help_hook_off[i]) {
            saved |= bit;
            mask &= ~bit;
        }
    }
    game_hook_mask = mask;
    opt_hook_saved = saved;
    GameTimePause();
    game_slow = 1;
    KeyAddHandler(WATCOM_FN(options_2CCB38), 0, 0, 0);
    MouseAddHandler(&tnui_full_rect, 0xFF, WATCOM_FN(options_2CCB38));
    options_2CCBE0();
}
