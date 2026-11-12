/*
 * hudmenu.c - M037 in-mission pop-up menu (0x27B910-0x27C11F, __FF.EXE v1.09).
 *
 *   0x27B910 menu_zero_rect  8 zero bytes in the code segment (data)
 *   0x27B918 menudraw_27B918 open the menu at a point
 *   0x27BB30 menudraw_27BB30 draw it (into menu_canvas) when open
 *   0x27BB50 menudraw_27BB50 blit it when open
 *   0x27BB64 menudraw_27BB64 draw one row, hot letter in the background colour
 *   0x27BE1C menudraw_27BE1C draw the whole menu
 *   0x27BF40 menudraw_27BF40 close it
 *   0x27BFBC menudraw_27BFBC mouse event
 *
 * Quirks kept:
 *  - menudraw_27BB64 cuts the string at the upper-cased hot letter and puts the UPPER-case
 *    letter back afterwards (the resource string is changed when its hot letter was lower case).
 *    When the letter is missing the original writes 0 to address 0 and draws the string at
 *    address 1; here that case cuts nothing and draws an empty tail (see below).
 *  - menudraw_27B918 moves the mouse to the original x but the clamped y.
 *  - The item callback is also handed the item pointer in ecx (ignored by the 3-arg callee).
 *  - menudraw_27BFBC calls cmd_261370 with registers its callees read: edx = remainder of the
 *    row division, ebx = row * 0x16, ecx = menu height (passed here as extra Watcom args).
 */
#include "g7_b.h"
#include "g8_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
LGRect menu_zero_rect;
uchar menu_active;
long menu_sel;
MenuItem menu_items[9];
grs_canvas menu_canvas;
LGRect menu_rect;
#endif

#define UBITMAP(bm, x, y) WC3(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * (bm)->type], (bm), (x), (y))
#define RECT_FILL(a, b, c, d) WC4(grd_canvas_table[16], (a), (b), (c), (d))
#define BOX(a, b, c, d) WC4(grd_canvas_table[18], (a), (b), (c), (d))

void menudraw_27B918(LGPoint pt)
{
    short w, h, x, y, hh;
    long lim, m;
    LGPoint mp;

    ResLock(0x1F7);
    gr_font_string_size((grs_font *)ResGet(0x1F7), (char *)RefGet(0x16F0029), &w, &h);
    ResUnlock(0x1F7);
    w = (short)(w * 3 / 2 + 4);
    h = (short)(h + 3);

    {
        long t = (long)pt.x - w / 2, lim2 = (long)tnui_back_canvas.bm.w - w;
        long v = t < lim2 ? t : lim2;
        x = (short)(v < 0 ? 0 : v);
    }
    lim = (long)tnui_back_canvas.bm.h - 10 * h + h / 2;
    m = ((ulong)(long)pt.y < (ulong)lim) ? pt.y : lim;
    if ((ulong)(long)(h / 2) > (ulong)m)
        m = h / 2;
    y = (short)((short)m - h / 2);

    mp.x = pt.x;
    mp.y = (short)((ushort)y + h / 2);
    MouseMoveTo(mp);

    hh = (short)(h * 10);
    menu_rect.ul.x = x;
    menu_rect.ul.y = y;
    menu_rect.lr.x = (short)(x + w);
    menu_rect.lr.y = (short)(y + hh);
    menu_active = 1;
    menu_sel = -1;
    gr_init_sub_canvas(&tnui_back_canvas, &menu_canvas, x, y, w, hh);
}

void menudraw_27BB30(void)
{
    if (!menu_active)
        return;
    gr_push_canvas(&menu_canvas);
    menudraw_27BE1C();
    gr_pop_canvas();
}

void menudraw_27BB50(void)
{
    if (!menu_active)
        return;
    GfxBlit(&menu_rect);
}

void menudraw_27BB64(char *s, char hot, long row, uchar mode)
{
    short W = (short)(menu_rect.lr.x - menu_rect.ul.x);
    long H = menu_rect.lr.y - menu_rect.ul.y;
    short y0 = (short)(row * H / 10);
    long y1 = y0 + H / 10;
    long fg = 0, bg = 0x1D;
    ushort pos = 0;

    if (mode == 1) {
        grd_canvas->gc.fcolor = 0;
        RECT_FILL(0, y0, W, (short)y1);
        bg = 0x1D;
        fg = 0x22;
    } else if (mode == 2) {
        grd_canvas->gc.fcolor = 0x26;
        RECT_FILL(0, y0, W, (short)y1);
        pos = 0x7FFF;
    }
    grd_canvas->gc.fcolor = fg;
    grd_canvas->gc.font = (grs_font *)ResLock(0x1F7);
    if (!hot) {
        long tw = gr_font_string_width(grd_canvas->gc.font, s);
        short off = (short)(((W - tw - 4) * pos) / 0xFFFF);
        GfxDrawText(0x1F7, s, off + 2, y0 + 1);
    } else {
        static char nohot[2];
        long tw = gr_font_string_width(grd_canvas->gc.font, s);
        long off = (pos * (W - tw - 4)) / 0xFFFF;
        char up = (char)toupper(hot);
        char tmp[2];
        char *p;
        long x;
        tmp[0] = up;
        tmp[1] = 0;
        p = s;
        for (;;) {
            if (*p == up)
                break;
            if (*p == 0) {
                p = NULL;
                break;
            }
            p++;
        }
        if (!p)
            p = nohot; /* original: address 0 */
        *p = 0;
        GfxDrawText(0x1F7, s, (short)off + 2, y0 + 1);
        x = off + gr_font_string_width(grd_canvas->gc.font, s);
        grd_canvas->gc.fcolor = bg;
        GfxDrawText(0x1F7, tmp, (short)x + 2, y0 + 1);
        x += gr_font_string_width(grd_canvas->gc.font, tmp);
        grd_canvas->gc.fcolor = fg;
        GfxDrawText(0x1F7, p + 1, (short)x + 2, y0 + 1);
        *p = up;
    }
    ResUnlock(0x1F7);
}

void menudraw_27BE1C(void)
{
    char buf[0x100];
    ulong i;
    MenuItem *it;
    short W = (short)(menu_rect.lr.x - menu_rect.ul.x);
    short H = (short)(menu_rect.lr.y - menu_rect.ul.y);

    grd_canvas->gc.fcolor = 0x22;
    RECT_FILL(0, 0, W, H);
    grd_canvas->gc.fcolor = 0x26;
    BOX(0, 0, (short)(menu_rect.lr.x - menu_rect.ul.x), (short)(menu_rect.lr.y - menu_rect.ul.y));
    ResLock(0x16F);
    menudraw_27BB64((char *)RefGet(0x16F00B4), 0, 0, 2);
    for (i = 0, it = menu_items; i < 9; i++, it++) {
        char *s;
        if (it->flags & 1) {
            s = (char *)RefGet(it->ref);
        } else {
            WATCOM_CALL((void (*)())it->fn, (long)it->ref, 0, (long)buf);
            s = buf;
        }
        if (*s)
            menudraw_27BB64(s, (char)it->hot, (long)i + 1, (uchar)(i == (ulong)menu_sel));
    }
    ResUnlock(0x16F);
}

void menudraw_27BF40(void)
{
    LGRect r = menu_zero_rect;
    if (!hud_helmet) {
        grs_bitmap *bm;
        gr_push_canvas(&tnui_back_canvas);
        bm = (grs_bitmap *)RefGet(0x2EE0000);
        bm->bits = (uchar *)bm + 0x1C;
        UBITMAP(bm, 0, 0);
        gr_pop_canvas();
    }
    menu_rect = r;
    helm_26214C();
    hud_38B9AE = 1;
    menu_active = 0;
}

uchar menudraw_27BFBC(TnEvent *ev)
{
    uchar done = 0;
    if (!menu_active) {
        uchar *rs = (uchar *)tn_replay_state_ptr;
        HudWidget *p = hud_w_389DF4;
        if (!((rs[3] & 0x20) || (rs[0x13] & 0x20)))
            return 0;
        if (ev->type != 1 || 1 >= game_391E4C)
            return 0;
        if (ev->pt.y < p->y0 || ev->pt.y >= p->y1 || ev->pt.x < p->x0 || ev->pt.x >= p->x1)
            return 0;
        menudraw_27B918(ev->pt);
        return 1;
    }
    ulong rem = 0, h = 0;
    if (ev->pt.y >= menu_rect.ul.y && ev->pt.y < menu_rect.lr.y &&
        ev->pt.x >= menu_rect.ul.x && ev->pt.x < menu_rect.lr.x) {
        long dy = (short)(ev->pt.y - menu_rect.ul.y);
        h = (ulong)(menu_rect.lr.y - menu_rect.ul.y);
        menu_sel = (long)((ulong)(dy * 10) / h) - 1;
        rem = (ulong)(dy * 10) % h;
    } else {
        menu_sel = -1;
    }
    if (ev->type == 2) {
        /* cmd_261370 takes the item in eax; the original also leaves the division remainder
         * in edx, sel * 0x16 in ebx and the menu height in ecx, which its callees see */
        if (menu_sel >= 0 && (ulong)menu_sel < 9)
            WATCOM_CALL((void (*)())WATCOM_FN(cmd_261370), (long)&menu_items[menu_sel], (long)rem,
                        menu_sel * 0x16, (long)h);
        done = 1;
    }
    if (!done)
        return 0;
    menudraw_27BF40();
    return 1;
}
