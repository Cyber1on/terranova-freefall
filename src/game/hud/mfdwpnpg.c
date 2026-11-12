/* mfdwpnpg.c -- Terra Nova module M025 (0x271D60-0x27219F): the MFD weapon page (view mode
 * 0x0D).  Decompiled from __FF.EXE v1.09.
 *
 *   0x271D60 mfdpage_271D60 init: weapon names 0x16E0000.. into mfd_38FD10[18]
 *   0x271DAC mfdpage_271DAC event: press / button 4 cycles the player's weapon
 *   0x271E08 mfdpage_271E08 update: redraw the view widget
 *   0x271E14 mfdpage_271E14 draw: weapon picture, name, ammo or state, condition in %
 *   0x272100 mfdpage_272100 bitmap with a bar (fraction num / den of its width left open)
 *
 * The selected weapon is the 15-byte slot (schar)pba[0xCE] at pba + 0x45: +0 type, +2 short
 * condition, +4 ushort ammo.  game_weapon_types +6: ammo capacity, +8: full condition.
 */
#include "g8_b.h"

#define PBA_PLAYER ((uchar *)&game_pba[game_player_slot])
#define HIRES ((game_35A540 >> 1) != 0)

typedef void (*CanvasFn)();

void mfdpage_271D60(void)
{
    long i;
    for (i = 0; i < 18; i++)
        g1c_strcpy(mfd_38FD10[i], (char *)RefGet(0x16E0000 + i));
}

void mfdpage_271DAC(HudWidget *w, HudEvent *ev)
{
    if (ev->type == 1 || ev->type == 4) {
        pbasim_28AB74(PBA_PLAYER + 6, ev->type == 1 ? 1 : -1);
        HUD_VIEW_W(w->id)->flags |= 0x20;
        hud_w_389F34->flags |= 0x20;
    }
}

void mfdpage_271E14(long view)
{
    char fmt[0x28], buf[0x28];
    uchar *p6 = PBA_PLAYER + 6;
    uchar *ws = p6 + 0x3F + 15 * (schar)p6[0xC8];
    grs_bitmap *bm;
    HudWidget *w;
    short tw;
    long r, ammo = 0, max;
    uchar drawn = 0;

    bm = RefGet(mfd_35B278[ws[0]] & 0xFFFF0000);
    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, 0, 0);
    grd_canvas->gc.fcolor = 0x1D;
    tw = gr_font_string_width(ResGet(0x1F7), mfd_38FD10[ws[0]]);
    w = HUD_VIEW_W(view);
    GfxDrawText(0x1F7, mfd_38FD10[ws[0]], ((w->x1 - w->x0) - tw) / 2, 2);
    r = weapon_ready(ws, p6 + 0x80);
    switch (r) {
    case 0:
        if (*(ushort *)&WT(ws[0])[6] > 1) {
            ammo = *(ushort *)(ws + 4);
            strutil_2DBCE0(fmt, RefGet(0x1710027), 0x28);
            lg_sprintf(buf, fmt, ammo);
            GfxDrawText(0x1F7, buf, 2, HIRES ? 0x58 : 0x2C);
            drawn = 1;
        }
        break;
    case 3:
        GfxDrawText(0x1F7, RefGet(0x1710029), 2, HIRES ? 0x58 : 0x2C);
        drawn = 1;
        break;
    case 4:
        mfdpage_272100(0x2190006, 1, 1, 0x2A, HIRES ? 0x58 : 0x2C);
        break;
    }
    if (!drawn) {
        grd_canvas->gc.fcolor = 0x1D;
        strutil_2DBCF8(0x1F7, 0x16F0047 + r, 2, HIRES ? 0x58 : 0x2C, 0);
    }
    grd_canvas->gc.fcolor = 0x1D;
    max = *(short *)&WT(ws[0])[8];
    lg_sprintf(buf, "%d%%", *(short *)(ws + 2) * 100 / max);
    GfxDrawText(0x1F7, buf, 2, HIRES ? 0x46 : 0x23);
}

void mfdpage_271E08(long view)
{
    HUD_VIEW_W(view)->flags |= 0x20;
}

void mfdpage_272100(Ref ref, long num, long den, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);
    void (**t)();

    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, (short)x,
                (short)y);
    grd_canvas->gc.fcolor = 0;
    t = grd_canvas_table;
    WATCOM_CALL((CanvasFn)t[16], (short)(bm->w * num / den + x), (short)y, (short)(bm->w + x),
                (short)(bm->h + y));
}
