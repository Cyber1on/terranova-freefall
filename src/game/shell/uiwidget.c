/* uiwidget.c -- Terra Nova module M096 (source name unknown): the shell's UI widget library.
 * Script natives for the shell script (caption / text boxes, buttons as screen rectangles,
 * palette, graphics mode, mouse), button drawing helpers for the shell screens, and the frame
 * hook that reports the button under the mouse.  Decompiled from __FF.EXE v1.09.
 *
 *   0x2B5C90 uiwidget_2B5C90 set the shell UI colours      0x2B5CC0 uiwidget_2B5CC0 caption box
 *   0x2B5D94 uiwidget_2B5D94 text box over a button        0x2B5E48 uiwidget_2B5E48 font, colour
 *   0x2B5E58 uiwidget_2B5E58 text box margins              0x2B5E70 uiwidget_2B5E70 its lines
 *   0x2B5E80 uiwidget_2B5E80 its text width                0x2B5EBC uiwidget_2B5EBC string width
 *   0x2B5EDC uiwidget_2B5EDC font height                   0x2B5F04 uiwidget_2B5F04 font height
 *   0x2B5F24 uiwidget_2B5F24 close the text box            0x2B5F44 uiwidget_2B5F44 clear buttons
 *   0x2B5F68 uiwidget_2B5F68 set a button                  0x2B5FA8 uiwidget_2B5FA8 hook on / off
 *   0x2B6014 uiwidget_2B6014 button under the mouse        0x2B601C uiwidget_2B601C button at pt
 *   0x2B607C uiwidget_2B607C button under the mouse now    0x2B6098 / 0x2B60A8 lock / unlock font
 *   0x2B60B4 uiwidget_2B60B4 wake the shell thread         0x2B60CC uiwidget_2B60CC show screen
 *   0x2B60DC uiwidget_2B60DC bitmap button with a label    0x2B61D8 uiwidget_2B61D8 drawn button
 *   0x2B63D8 uiwidget_2B63D8 set a button from a rect      0x2B6428 ShellUiHook frame hook 12
 *   0x2B6480 uiwidget_2B6480 script text callback          0x2B648C uiwidget_2B648C cover region
 *   0x2B64F8 uiwidget_2B64F8 button frames                 0x2B6560 uiwidget_2B6560 cover off
 *   0x2B6580 uiwidget_2B6580 key handler (cover region)    0x2B6620 uiwidget_2B6620 draw bitmap
 *   0x2B6670 uiwidget_2B6670 fill type / parameter         0x2B66F0 uiwidget_2B66F0 get pixel
 *   0x2B6700 uiwidget_2B6700 move the mouse                0x2B6714 uiwidget_2B6714 graphics mode
 *   0x2B67B4 uiwidget_2B67B4 rotated text                  0x2B6A48 uiwidget_2B6A48 palette
 *
 * Quirks kept:
 *  - uiwidget_2B5EBC measures the caller's edx (the string argument) with the font of its id;
 *  - uiwidget_2B5F68 / 2B63D8 ignore button 0 and numbers >= 50; uiwidget_2B61D8 passes any
 *    number >= 0 on (2B5F68 then ignores 0);
 *  - uiwidget_2B5E58 / 2B5E70 / 2B5E80 / 2B6480 do not test for a missing text box;
 *  - uiwidget_2B61D8 draws the 3D frame only for fill colours 0xBF and 0x48;
 *  - uiwidget_2B67B4 with rotation 0 draws an empty (all transparent) bitmap: the text is drawn
 *    into the second buffer and only copied for rotations 1..3;
 *  - uiwidget_2B6714 sets the mouse speed from the screen size as x1 / 10, y1 * 32 / 200.
 */
#include "g10_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
long uiw_hook_on;
TBox *uiw_tbox;
uchar uiw_pal_a[18] = { 0xD0, 0xFF, 0x00, 0x60, 0x60, 0x60, 0xCA, 0x00, 0x00, 0xBB, 0x00, 0x00, 0x97, 0x00, 0x00, 0xFF, 0xFF, 0xFF };
uchar uiw_pal_b[18] = { 0xAA, 0xAA, 0xAA, 0x90, 0x90, 0x90, 0x78, 0x78, 0x78, 0x5A, 0x5A, 0x5A, 0x32, 0x32, 0x32, 0x10, 0x10, 0x10 };
TBoxStyle uiw_style;
TnRegionParams uiw_cover_params;
uchar uiw_cover_on;
TnRegion *uiw_cover;
TnRegionParams uiw_tbox_params;
long uiw_tbox_margins;
TnRegionParams uiw_tbox_params2;
long uiw_tbox_margins2;
short uiw_modes[4] = { 0, 0, 20, 2 };
LGRect uiw_rects[50];
long uiw_hover;
long uiw_rect_count;
long uiw_mouse_x, uiw_mouse_y;
grs_font *uiw_font;
#endif

/* canvas function table entries */
#define DRAW_RECT 16
#define DRAW_BOX 18
#define FONT_STRING 315
#define PIX_GET 3

static void draw_bitmap(grs_bitmap *bm, long x, long y)
{
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, x, y);
}

static void font_string(grs_font *f, char *s, long x, long y)
{
    WATCOM_CALL(grd_canvas_table[FONT_STRING], (long)f, (long)s, x, y);
}

/* (a - b) / 2 rounded toward zero, as the compiler's sar sequence */
static long half(long v)
{
    return v / 2;
}

static void kill_tbox(void)
{
    if (uiw_tbox) {
        viewport_2E575C((TnRegion *)uiw_tbox);
        uiw_tbox = NULL;
    }
}

static long caption_font(void)
{
    return 0x165 + (tnui_full_rect.lr.x > 320);
}

/* ------------------------------------------------------------------ text boxes */

void uiwidget_2B5C90(void)
{
    ScriptSetPal(0xF4, 6, uiw_pal_b);
    ScriptSetPal(0xFA, 6, uiw_pal_a);
}

void uiwidget_2B5CC0(void)
{
    TBoxStyle *old;
    long d;

    kill_tbox();
    d = tnui_full_rect.lr.y - tnui_full_rect.ul.y;
    d = d * 3 / 4;
    uiw_tbox_params.rect = tnui_full_rect;
    old = tbox_style;
    uiw_tbox_params.rect.ul.y = (short)(uiw_tbox_params.rect.ul.y + d);
    uiw_style.font = (ushort)caption_font();
    tbox_style = &uiw_style;
    uiw_tbox = tbox_create(&uiw_tbox_params, NULL, uiw_tbox_margins, 7);
    viewport_2E57F0((TnRegion *)uiw_tbox);
    script_text_cb = (ScriptFunc)WATCOM_FN(uiwidget_2B6480);
    tbox_style = old;
}

void uiwidget_2B5D94(long n, long centre_x, long centre_y)
{
    TBoxStyle *old;
    uchar flags;

    kill_tbox();
    uiw_tbox_params2.rect = uiw_rects[n];
    flags = 1 | (centre_x ? 2 : 0) | (centre_y ? 4 : 0);
    old = tbox_style;
    uiw_style.font = (ushort)caption_font();
    tbox_style = &uiw_style;
    uiw_tbox = tbox_create(&uiw_tbox_params2, NULL, uiw_tbox_margins2, flags);
    viewport_2E57F0((TnRegion *)uiw_tbox);
    script_text_cb = (ScriptFunc)WATCOM_FN(uiwidget_2B6480);
    tbox_style = old;
}

void uiwidget_2B5E48(long font, long colour)
{
    uiw_style.font = (ushort)font;
    uiw_style.textcolor = (uchar)colour;
}

void uiwidget_2B5E58(long mx, long my)
{
    uiw_tbox->mx = (short)mx;
    uiw_tbox->my = (short)my;
    viewport_2E57F0((TnRegion *)uiw_tbox);
}

long uiwidget_2B5E70(void)
{
    return uiw_tbox->nlines;
}

long uiwidget_2B5E80(void)
{
    ushort id = uiw_tbox->r.style->font;
    char *s = uiw_tbox->text;
    long w = gr_font_string_width(ResLock(id), s);

    ResUnlock(id);
    return w;
}

long uiwidget_2B5EBC(long font, char *s)
{
    ushort id = (ushort)font;
    long w = gr_font_string_width(ResLock(id), s);

    ResUnlock(id);
    return w;
}

long uiwidget_2B5EDC(void)
{
    long h = ((grs_font *)ResLock(uiw_style.font))->h;

    ResUnlock(uiw_style.font);
    return h;
}

long uiwidget_2B5F04(long font)
{
    ushort id = (ushort)font;
    long h = ((grs_font *)ResLock(id))->h;

    ResUnlock(id);
    return h;
}

void uiwidget_2B5F24(void)
{
    kill_tbox();
}

/* ------------------------------------------------------------------ buttons */

void uiwidget_2B5F44(void)
{
    memset(uiw_rects, 0, sizeof uiw_rects);
    uiw_rect_count = 0;
    uiw_hover = 0;
}

void uiwidget_2B5F68(long id, long x0, long y0, long x1, long y1)
{
    LGRect *r;

    if (id > 0 && id < 50) {
        r = &uiw_rects[id];
        r->ul.x = (short)x0;
        r->ul.y = (short)y0;
        r->lr.x = (short)x1;
        r->lr.y = (short)y1;
        if (id > uiw_rect_count)
            uiw_rect_count = id;
    }
}

void uiwidget_2B5FA8(long on)
{
    if (on) {
        game_hook_mask |= 0x1000;
        uiw_hook_on = 1;
        game_hooks[12] = (GameHook)WATCOM_FN(ShellUiHook);
        return;
    }
    game_hook_mask &= ~0x1000;
    uiw_hook_on = 0;
    if (uiw_cover_on) {
        viewport_2E575C(uiw_cover);
        uiw_cover_on = 0;
    }
}

long uiwidget_2B6014(void)
{
    return uiw_hover;
}

long uiwidget_2B601C(long pt)
{
    short x = (short)pt, y = (short)(pt >> 16);
    long i;

    for (i = 1; i <= uiw_rect_count; i++) {
        LGRect *r = &uiw_rects[i];
        if (y >= r->ul.y && y < r->lr.y && x >= r->ul.x && x < r->lr.x)
            return i;
    }
    return 0;
}

long uiwidget_2B607C(void)
{
    LGPoint p;

    mouse_get_xy(&p.x, &p.y);
    return uiwidget_2B601C(*(long *)&p);
}

void uiwidget_2B6098(void)
{
    uiw_font = ResLock(0x167);
}

void uiwidget_2B60A8(void)
{
    ResUnlock(0x167);
}

void uiwidget_2B60B4(long code)
{
    ScriptWake(shell_thread, 0x101, code);
}

void uiwidget_2B60CC(void)
{
    tnui_add_blit_rect(&tnui_full_rect);
    tnui_task_6174();
}

/* A bitmap (FrameDesc resource) at pt with s centred on it in colour col; button id covers it. */
void uiwidget_2B60DC(long id, Ref bmref, grs_font *f, long pt, void *s, long col)
{
    grs_bitmap *bm = RefGet(bmref);
    short x = (short)pt, y = (short)(pt >> 16);
    long dx, dy;

    bm->bits = (uchar *)bm + 0x1C;
    draw_bitmap(bm, x, y);
    dx = half(bm->w - gr_font_string_width(f, s));
    grd_canvas->gc.font = f;
    grd_canvas->gc.fcolor = (uchar)col;
    dy = half(bm->h - (f->h - 1));
    font_string(grd_canvas->gc.font, s, (short)(x + dx), (short)(dy + y));
    uiwidget_2B5F68(id, x, y, (short)(x + bm->w), (short)(y + bm->h));
}

/* A drawn button: pt / size give the rectangle; fill != 0 fills it (and for 0xBF / 0x48 adds a
 * light / dark 3D frame), else box != 0 frames it in that colour; s centred in colour 0xF2. */
void uiwidget_2B61D8(long id, long box, long fill, grs_font *f, long pt, long size, char *s)
{
    short x = (short)pt, y = (short)(pt >> 16);
    short x1 = (short)(x + (short)size), y1 = (short)(y + (short)(size >> 16));
    uchar c = (uchar)fill, hi = 0, lo = 0;
    long dx, dy;

    if (c) {
        grd_canvas->gc.fcolor = c;
        WATCOM_CALL(grd_canvas_table[DRAW_RECT], x, y, x1, y1);
        if (c == 0xBF) {
            lo = 0xC0;
            hi = 0xB1;
        } else if (c == 0x48) {
            lo = 0x40;
            hi = 0x4E;
        }
        if (hi) {
            grd_canvas->gc.fcolor = hi;
            gen_hline(x, y, (short)(x1 - 1));
            gen_vline(x, y, (short)(y1 - 1));
            grd_canvas->gc.fcolor = lo;
            gen_hline(x, (short)(y1 - 1), (short)(x1 - 1));
            gen_vline((short)(x1 - 1), (short)(y + 1), (short)(y1 - 1));
        }
    } else if ((uchar)box) {
        grd_canvas->gc.fcolor = (uchar)box;
        WATCOM_CALL(grd_canvas_table[DRAW_BOX], x, y, x1, y1);
    }
    dx = half((short)size - gr_font_string_width(f, s));
    grd_canvas->gc.fcolor = 0xF2;
    grd_canvas->gc.font = f;
    dy = half((short)(size >> 16) - (gr_font_string_height(f, s) - 1));
    font_string(grd_canvas->gc.font, s, (short)(dx + x), (short)(dy + y));
    if (id >= 0)
        uiwidget_2B5F68(id, x, y, x1, y1);
}

void uiwidget_2B63D8(long id, LGRect *r)
{
    LGRect t = *r;

    if (id > 0 && id < 50) {
        uiw_rects[id] = t;
        if (id > uiw_rect_count)
            uiw_rect_count = id;
    }
}

#ifndef TN_WATCOM_ABI
/* Freefall: a "Freefall Options" button on the game's main menu, in the free corner bottom left,
 * drawn as the Exit button opposite it: its frame copied from Exit's, the label in Exit's font
 * colour (white under the mouse, as the game's buttons).  The game has no button there, so its
 * clicks do nothing in the game; port/ui/options.c opens the options screen on them
 * (port_menu_ffbutton).  Not in the frame comparison runs (lockstep), where the picture stays the
 * original's.  The main menu is told by its buttons (uiw_rects: New Game and Exit where they are,
 * 7 buttons) */
#define FFB_X0 4                        /* Exit (492..636 x 454..478) mirrored */
#define FFB_Y0 454
#define FFB_W 144
#define FFB_H 24
#define EXIT_X0 492
extern int port_lockstep, port_game_dialog_up, port_game_options_up;
int port_nearest_colour(int r, int g, int b);   /* the palette's (port/ui/options.c) */
int port_menu_ffbutton;                 /* the button is up (port/ui/options.c) */

static int main_menu_up(void)
{
    static const short new_game[4] = { 81, 358, 291, 381 }, exit_[4] = { 492, 454, 636, 478 };
    return uiw_rect_count == 7 && !memcmp(&uiw_rects[1], new_game, sizeof new_game) &&
           !memcmp(&uiw_rects[7], exit_, sizeof exit_) && tnui_back_canvas.bm.w == 640 &&
           tnui_back_canvas.bm.h == 480;
}

/* a button drawn as Exit: its frame copied from Exit's, the label in Exit's font colour (white
 * under the mouse).  The Freefall Options button bottom left, the Mission Selector button
 * (port/ui/missions.c) above the credit line in the middle (owner's mockup, game pixels
 * 248..392 x 440..464) */
typedef struct {
    short x0, y0;
    const char *s;
    int *up;                            /* the button is up (port/ui/options.c, port/ui/missions.c) */
    uchar shown[FFB_H][FFB_W];          /* the button as last drawn, to see the menu draw over it */
    int hover_was;
} FfButton;
extern volatile int port_ms_clicks;     /* the Mission Selector's clicks (port/ui/missions.c) */
int port_menu_msbutton;                 /* the Mission Selector button is up (port/ui/missions.c) */
static FfButton ff_buttons[2] = {
    { FFB_X0, FFB_Y0, "Freefall Options", &port_menu_ffbutton, { { 0 } }, -1 },
    { 248, 440, "Mission Selector", &port_menu_msbutton, { { 0 } }, -1 },
};

static void ff_button(FfButton *t, LGPoint p, int label, int top, int lift_in, int *lift_out, uchar black)
{
    grs_bitmap *b = &tnui_back_canvas.bm;
    grs_canvas *was;
    LGRect r = { { t->x0, t->y0 }, { t->x0 + FFB_W, t->y0 + FFB_H } };
    int hover, x, y, same = *t->up, lift = lift_in;
    grs_font *f;

    hover = p.x >= t->x0 && p.x < t->x0 + FFB_W && p.y >= t->y0 && p.y < t->y0 + FFB_H;
    for (y = 0; y < FFB_H && same; y++)
        same = !memcmp(b->bits + (t->y0 + y) * b->row + t->x0, t->shown[y], FFB_W);
    if (same && hover == t->hover_was)
        return;
    MouseHide();
    was = grd_canvas;
    gr_set_canvas(&tnui_back_canvas);
    f = ResGet((Id)0x167);              /* the shell widgets' font (uiwidget_2B6098) */
    grd_canvas->gc.font = f;
    for (;;) {
        int ink_top = -1;
        for (y = 0; y < FFB_H; y++)
            memcpy(b->bits + (t->y0 + y) * b->row + t->x0, b->bits + (FFB_Y0 + y) * b->row + EXIT_X0, FFB_W);
        for (y = 7; y < 18; y++)        /* Exit's label off: the inside is black */
            memset(b->bits + (t->y0 + y) * b->row + t->x0 + 12, black, 120);
        grd_canvas->gc.fcolor = hover ? (uchar)port_nearest_colour(63, 63, 63) : (uchar)label;
        font_string(f, (char *)t->s, (short)(t->x0 + (FFB_W - gr_font_string_width(f, (char *)t->s)) / 2),
                    (short)(t->y0 + top - (lift > -100 ? lift : 0)));
        if (lift > -100)
            break;
        for (y = 7; y < 18 && ink_top < 0; y++)     /* the first time: line its ink up with Exit's */
            for (x = 12; x < 132; x++)
                if (b->bits[(t->y0 + y) * b->row + t->x0 + x] != black) {
                    ink_top = y;
                    break;
                }
        lift = ink_top < 0 ? 0 : ink_top - top;
    }
    *lift_out = lift;
    gr_set_canvas(was);
    for (y = 0; y < FFB_H; y++)
        memcpy(t->shown[y], b->bits + (t->y0 + y) * b->row + t->x0, FFB_W);
    tnui_add_blit_rect(&r);
    MouseShow();
    t->hover_was = hover;
    *t->up = 1;
}

static void ff_menu_button(LGPoint p)
{
    static int label = -1, lift = -100;  /* lift: the font's rows above its ink */
    grs_bitmap *b = &tnui_back_canvas.bm;
    int x, y, top = -1, i;
    uchar ink = 0, black;

    if (port_lockstep || !main_menu_up() || port_game_dialog_up || port_game_options_up) {
        port_menu_ffbutton = port_menu_msbutton = 0;
        return;
    }
    /* Exit's label: its colour (the most pixels that are not the black inside, while Exit is not
     * under the mouse) and its top row */
    black = b->bits[(FFB_Y0 + 7) * b->row + EXIT_X0 + 13];
    {
        static unsigned short n[256];
        memset(n, 0, sizeof n);
        for (y = 7; y < 18; y++)
            for (x = 14; x < 130; x++) {
                uchar c = b->bits[(FFB_Y0 + y) * b->row + EXIT_X0 + x];
                if (c != black) {
                    if (++n[c] > n[ink] || !ink)
                        ink = c;
                    if (top < 0)
                        top = y;
                }
            }
    }
    if (ink && uiw_hover != 7)
        label = ink;
    if (label < 0 || top < 0)
        return;                         /* Exit not drawn yet */
    for (i = 0; i < 2; i++)
        ff_button(&ff_buttons[i], p, label, top, lift, &lift, black);
}

/* Freefall: our credit line on the main menu in place of the game's copyright line, in its font
 * and colour (owner: first above it, then instead of it).  The copyright is the menu's
 * own text (RESBABL2.RES), drawn into the back picture; its ink (the most pixels that are not
 * black between the bottom buttons) gives the colour, its top row and its middle, found once (our
 * line has the same ink).  The copyright's ink goes black and our line is drawn on its row,
 * between the Freefall Options and Exit buttons.  Redrawn when the game draws over it, as the
 * button.  Not in lockstep runs. */
#define CR_X0 (FFB_X0 + FFB_W + 4)      /* the gap between the bottom buttons */
#define CR_W (EXIT_X0 - 4 - CR_X0)
#define CR_Y0 465                       /* the copyright's rows (467..476), under the Mission
                                         * Selector button (440..464) */
#define CR_H 15
static void ff_menu_credit(void)
{
    static uchar shown[CR_H][CR_W];
    static int lift = -100, have, top = -1, mid;
    static uchar ink;
    grs_bitmap *b = &tnui_back_canvas.bm;
    grs_canvas *was;
    LGRect r;
    char *s = "Terra Nova: Freefall - a modern SDL3 port by Cyberion";
    grs_font *f;
    int x, y, same = have, ytext;
    uchar black;

    if (port_lockstep || !main_menu_up() || port_game_dialog_up || port_game_options_up) {
        have = 0;
        return;
    }
    for (y = 0; y < CR_H && same; y++)
        same = !memcmp(b->bits + (CR_Y0 + y) * b->row + CR_X0, shown[y], CR_W);
    if (same)
        return;
    black = b->bits[(FFB_Y0 + 7) * b->row + EXIT_X0 + 13];  /* inside the Exit button */
    if (top < 0) {                      /* the copyright, between the buttons */
        static unsigned short n[256];
        int left = -1, right = -1;
        memset(n, 0, sizeof n);
        for (y = CR_Y0; y < 480; y++)
            for (x = CR_X0; x < CR_X0 + CR_W; x++) {
                uchar c = b->bits[y * b->row + x];
                if (c != black && ++n[c] > n[ink])
                    ink = c;
            }
        if (!ink || n[ink] < 200)
            return;                     /* the copyright not drawn yet */
        for (y = CR_Y0; y < 480 && top < 0; y++)
            for (x = CR_X0; x < CR_X0 + CR_W; x++)
                if (b->bits[y * b->row + x] == ink) {
                    top = y;
                    break;
                }
        for (x = CR_X0; x < CR_X0 + CR_W; x++)
            for (y = top; y < 480; y++)
                if (b->bits[y * b->row + x] == ink) {
                    if (left < 0)
                        left = x;
                    right = x;
                    break;
                }
        mid = (left + right + 1) / 2;
    }
    MouseHide();
    was = grd_canvas;
    gr_set_canvas(&tnui_back_canvas);
    f = ResGet((Id)0x167);              /* the shell's font, as the copyright's */
    grd_canvas->gc.font = f;
    grd_canvas->gc.fcolor = ink;
    for (y = top; y < CR_Y0 + CR_H; y++)        /* the copyright off */
        for (x = CR_X0; x < CR_X0 + CR_W; x++)
            if (b->bits[y * b->row + x] == ink)
                b->bits[y * b->row + x] = black;
    ytext = top;                        /* our ink's top row: the copyright's */
    for (;;) {
        int ink_top = -1;
        font_string(f, s, (short)(mid - gr_font_string_width(f, s) / 2),
                    (short)(ytext - (lift > -100 ? lift : 0)));
        if (lift > -100)
            break;
        for (y = ytext - 4; y < CR_Y0 + CR_H && ink_top < 0; y++)   /* the first time: ink at ytext */
            for (x = CR_X0; x < CR_X0 + CR_W; x++)
                if (y >= CR_Y0 && b->bits[y * b->row + x] == ink) {
                    ink_top = y;
                    break;
                }
        lift = ink_top < 0 ? 0 : ink_top - ytext;
        if (!lift)
            break;
        for (y = CR_Y0; y < CR_Y0 + CR_H; y++)  /* off again, drawn lifted next */
            for (x = CR_X0; x < CR_X0 + CR_W; x++)
                if (b->bits[y * b->row + x] == ink)
                    b->bits[y * b->row + x] = black;
    }
    gr_set_canvas(was);
    for (y = 0; y < CR_H; y++)
        memcpy(shown[y], b->bits + (CR_Y0 + y) * b->row + CR_X0, CR_W);
    r.ul.x = CR_X0;
    r.ul.y = CR_Y0;
    r.lr.x = CR_X0 + CR_W;
    r.lr.y = CR_Y0 + CR_H;
    tnui_add_blit_rect(&r);
    MouseShow();
    have = 1;
}

/* frame task 0 (tnui_task_6174, viewport.c), before it copies the frame's rects to the screen:
 * the button again where the game drew over it after the shell hook (its dirty rects, frame task
 * 18), so the screen never shows a frame without it (P91: it blinked while the mouse went over the
 * other buttons) */
static LGPoint ffb_mouse;
static int ffb_seen;

void port_ffbutton_refresh(void)
{
    if (ffb_seen) {
        ff_menu_button(ffb_mouse);
        ff_menu_credit();
    }
}
#endif

#ifndef TN_WATCOM_ABI
/* Mission Selector, Play Mission (port/ui/missions.c): the way to the mission's outfitting walked
 * for the player, as the player would: in the quarters the door (Enter Base), in the base hall the
 * door of the briefing (Start Mission Briefing); missions.c skips the movies on the way. Each
 * screen is told by its buttons (uiw_rects); on any other screen the walk ends and the player goes
 * on by hand. The pointer is put on the door, then the click is the script's (key 0x81, as
 * ScriptMouseHandler sends) once the door is the button under the pointer. */
extern volatile int port_ms_walk;       /* 1 quarters next, 2 base hall next, 0 none */
void port_ms_walked(int step);          /* missions.c: 1 quarters done, 2 hall done, 0 given up */

static int rect_is(long n, short x0, short y0, short x1, short y1)
{
    LGRect *r = &uiw_rects[n];
    return n <= uiw_rect_count && r->ul.x == x0 && r->ul.y == y0 && r->lr.x == x1 && r->lr.y == y1;
}

static void ms_walk(void)
{
    static int frames, seen_other;
    long door;
    int step = port_ms_walk;

    if (step != 1 && step != 2) {
        frames = seen_other = 0;
        return;
    }
    if (step == 1 && uiw_rect_count == 6 && rect_is(3, 399, 175, 488, 350))
        door = 3;                       /* the quarters */
    else if (step == 2 && uiw_rect_count == 3 && rect_is(2, 280, 235, 391, 325))
        door = 2;                       /* the base hall */
    else {
        /* not the screen expected: the main menu still fading out (its 7 buttons), else a screen
         * the walk does not know */
        if (!(uiw_rect_count == 7 && rect_is(7, 492, 454, 636, 478)) && ++seen_other > 150) {
            port_ms_walk = 0;
            port_ms_walked(0);
        }
        frames = 0;
        return;
    }
    seen_other = 0;
    if (uiw_hover != door) {
        LGPoint c;
        c.x = (short)((uiw_rects[door].ul.x + uiw_rects[door].lr.x) / 2);
        c.y = (short)((uiw_rects[door].ul.y + uiw_rects[door].lr.y) / 2);
        MouseMoveTo(c);
        frames = 0;
        return;
    }
    if (++frames < 4)                   /* the script has shown the door's label */
        return;
    frames = 0;
    port_ms_walk = step == 1 ? 2 : 0;
    port_ms_walked(step);
    ScriptSendKey(0x81);
}
#endif

/* Frame hook 12: the button under the mouse; a change sends script key 0x90 + button. */
void ShellUiHook(int slot)
{
    LGPoint p = *(LGPoint *)tnm_state_ptr;
    long n;

    uiw_mouse_x = p.x;
    uiw_mouse_y = p.y;
    n = uiwidget_2B601C(*(long *)&p);
    if (n != uiw_hover) {
        uiw_hover = n;
        ScriptSendKey((ushort)(0x90 + (short)uiw_hover));
    }
#ifndef TN_WATCOM_ABI
    ms_walk();
    ffb_mouse = p;
    ffb_seen = 1;
    ff_menu_button(p);
    ff_menu_credit();
    {   /* the Mission Selector's button clicks (port/ui/missions.c, main thread): the builder's
         * click sound (rescustm.c click_sound), played here on the game's thread */
        static int heard;
        if (port_ms_clicks != heard) {
            heard = port_ms_clicks;
            SndPlay(0x7CC, 0, 0x28, 1, 0x40, 0x40, 0);
        }
    }
#endif
}

void uiwidget_2B6480(long id, char *text)
{
    tbox_set_text(uiw_tbox, text);
}

static void cover_on(void)
{
    if (!uiw_cover_on && uiw_hook_on) {
        uiw_cover_params.rect = tnui_full_rect;
        uiw_cover = viewport_2E55D8(&uiw_cover_params);
        viewport_2E57F0(uiw_cover);
        uiw_cover_on = 1;
    }
}

static void cover_off(void)
{
    if (uiw_cover_on) {
        viewport_2E575C(uiw_cover);
        uiw_cover_on = 0;
    }
}

void uiwidget_2B648C(void)
{
    cover_on();
}

void uiwidget_2B64F8(void)
{
    long i;

    grd_canvas->gc.fcolor = 0xFF;
    for (i = 1; i <= uiw_rect_count; i++)
        WATCOM_CALL(grd_canvas_table[DRAW_BOX], uiw_rects[i].ul.x, uiw_rects[i].ul.y,
                    uiw_rects[i].lr.x, uiw_rects[i].lr.y);
}

void uiwidget_2B6560(void)
{
    cover_off();
}

/* Key handler: key 9 (Tab) down puts a region over the whole screen while the hook runs,
 * released (0x200 event) takes it away. */
uchar uiwidget_2B6580(void *ev)
{
    if (((uchar *)ev)[4] != 9)
        return 0;
    if (*(ulong *)ev == 0x200)
        cover_off();
    else
        cover_on();
    return 1;
}

/* ------------------------------------------------------------------ drawing natives */

void uiwidget_2B6620(Ref bmref, Ref pos)
{
    short *p = RefLock(pos);
    grs_bitmap *bm;

    if (p) {
        bm = RefGet(bmref);
        bm->bits = (uchar *)bm + 0x1C;
        draw_bitmap(bm, p[0], p[1]);
        ResUnlock((ushort)((ulong)pos >> 16));
    }
}

void uiwidget_2B6670(long fill, long parm, long what)
{
    if (what & 1) {
        grd_canvas->gc.fill_type = fill;
        grd_function_table = grd_function_fill_table + fill * GRD_FUNCS_TN * REAL_BMT_TYPES;
        grd_uline_fill_vector = grd_uline_fill_table + fill * GRD_LINE_TYPES;
    }
    if (what & 2)
        grd_canvas->gc.fill_parm = parm;
}

long uiwidget_2B66F0(long x, long y)
{
    return WATCOM_CALL(grd_pixel_table[PIX_GET], (short)x, (short)y);
}

void uiwidget_2B6700(long x, long y)
{
    LGPoint p;

    p.x = (short)x;
    p.y = (short)y;
    MouseMoveTo(p);
}

void uiwidget_2B6714(long n)
{
    LGPoint speed;

    MouseHide();
    MouseRemoveHandler(&tnui_full_rect, 1, WATCOM_FN(ScriptMouseHandler));
    gfx_2E421C();
    GfxInit2(uiw_modes[n + 1]);
    speed.y = (short)((ushort)(tnui_full_rect.lr.y << 5) / 200);
    speed.x = (short)((ushort)tnui_full_rect.lr.x / 10);
    tnmouse_set_screensize(tnui_full_rect.lr, speed);
    MouseAddHandler(&tnui_full_rect, 1, WATCOM_FN(ScriptMouseHandler));
    MouseShow();
}

/* s in the current font and colour at (x, y), turned by how & 3 quarter turns (1: 90 degrees
 * clockwise, 2: 180, 3: 270); 0 draws nothing (see the quirks). */
void uiwidget_2B67B4(long how, char *s, long x, long y)
{
    uchar mode = how & 3;
    short w = gr_font_string_width(grd_canvas->gc.font, s);
    short h = gr_font_string_height(grd_canvas->gc.font, s);
    long lw = w, lh = h, size = lw * lh;
    uchar *dst, *src;
    grs_bitmap text_bm, out_bm;
    grs_canvas cv;
    uchar col;
    grs_font *font;
    short row, cx;

    dst = ffmem_2C4F40(size);
    src = ffmem_2C4F40(size);
    memset(dst, 0, size);
    memset(src, 0, size);
    gr_init_bitmap(&text_bm, src, BMT_FLAT8, 1, w, h);
    if (mode & 1)
        gr_init_bitmap(&out_bm, dst, BMT_FLAT8, 1, h, w);
    else
        gr_init_bitmap(&out_bm, dst, BMT_FLAT8, 1, w, h);
    gr_make_canvas(&text_bm, &cv);
    col = (uchar)grd_canvas->gc.fcolor;
    font = grd_canvas->gc.font;
    gr_push_canvas(&cv);
    grd_canvas->gc.fcolor = col;
    grd_canvas->gc.font = font;
    font_string(font, s, 0, 0);
    gr_pop_canvas();
    if (mode) {
        for (row = 0; row < h; row++)
            for (cx = 0; cx < w; cx++) {
                uchar pix = src[w * row + cx];
                if (mode == 1)
                    dst[cx * h + h - row - 1] = pix;
                else if (mode == 2)
                    dst[(h - row - 1) * w + (w - cx) - 1] = pix;
                else if (mode == 3)
                    dst[row + (w - cx - 1) * h] = pix;
            }
    }
    ffmem_2C4FE4(src);
    draw_bitmap(&out_bm, (short)x, (short)y);
    ffmem_2C4FE4(dst);
}

void uiwidget_2B6A48(Ref ref)
{
    uchar *bm = RefGet(ref), *p;

    if (*(long *)(bm + 0x18)) {
        p = (uchar *)ResGet(REFID(ref)) + *(long *)(bm + 0x18);
        gr_set_pal(*(short *)p, *(short *)(p + 2), p + 4);
    }
}
