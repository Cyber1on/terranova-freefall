/* mfdfire.c -- Terra Nova module M026 (0x2721A0-0x27274F): the MFD weapon-fire page of the
 * player's PBA (a HUD view mode: event / update / draw).  Decompiled from __FF.EXE v1.09.
 *
 *   0x2721A0 mfdpage_2721A0 event: a press on the fire button fires the selected weapon
 *   0x2722B8 mfdpage_2722B8 update: redraw while the "fired" flash counter runs
 *   0x2722E8 mfdpage_2722E8 draw: weapon picture and name, fire button (coloured by state),
 *            charge count or reload bar, countdown digit of weapon 0xB
 *
 * The PBA's selected weapon is the record at PBA +9: +0 long ammo / state (0 = none),
 * +4 long weapon kind (bit index of ai_fire_mask / mfdp_35BF64 / mfdp_35BF66), +8 byte flag.
 * Fire button: x 7..0x4C, y from 0x29 (0x52 in hi-res), 0xA (0x14) high.
 */
#include "g8_c.h"

#define HI (game_35A540 >> 1)
#define FCOLOR(c) (grd_canvas->gc.fcolor = (c))
#define PBA_P(slot) ((uchar *)&game_pba[slot])
#define L32(p) (*(long *)(p))
#undef MU16
#define MU16(p) (*(ushort *)(p))

typedef void (*CanvasFn)();

static grs_bitmap *bm_get(Ref r)
{
    grs_bitmap *bm = RefGet(r);
    bm->bits = (uchar *)bm + 0x1C;
    return bm;
}

#define BITMAP(bm, x, y) \
    WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * (bm)->type], (long)(bm), \
                (long)(x), (long)(y))
#define CANVAS(i, ...) WATCOM_CALL((CanvasFn)grd_canvas_table[i], __VA_ARGS__)
#define DRAW_RECT_ 16
#define DRAW_BOX_ 18

void mfdpage_2721A0(HudWidget *w, HudEvent *ev)
{
    long dx, dy, y0;
    uchar *p;
    ulong bit;

    if (ev->type != 1)
        return;
    dx = *(short *)((uchar *)ev + 8) - w->x0;
    dy = *(short *)((uchar *)ev + 0xA) - w->y0;
    y0 = HI ? 0x52 : 0x29;
    if (dx < 7 || dy < y0 || dx >= 0x4D || dy >= y0 + (HI ? 0x14 : 0xA))
        return;
    p = PBA_P(game_player_slot) + 9;
    if (L32(p) == 0)
        return;
    bit = 1u << (p[4] & 31);
    if (ai_fire_mask & bit) {
        if (WeaponFire(game_player_entity))
            mfdp_38FE80 = 5;
        return;
    }
    if (mfdp_35BF64 & bit)
        return;
    if (fire_292970(game_player_entity, p[8] == 0))
        mfdp_38FE80 = 1;
}

void mfdpage_2722B8(long view)
{
    if (mfdp_38FE80) {
        HudWidget *w = HUD_VIEW_W(view);

        mfdp_38FE80--;
        w->flags |= 0x20;
    }
}

void mfdpage_2722E8(long view)
{
    uchar *pba = PBA_P(game_player_slot), *p, *s;
    grs_bitmap *bm;
    char buf[0x14];
    Ref ref;
    ulong bit;
    long y, h, x, i, n;

    bm = bm_get((0x20B + L32(pba + 0xD)) << 16);
    BITMAP(bm, 0, 0);
    FCOLOR(0x1D);
    GfxDrawText(0x1F7, RefGet(0x16E0030 + L32(pba + 0xD)), 6, 4);
    bit = 1u << (pba[0xD] & 31);
    p = pba + 9;
    if (!(mfdp_35BF64 & bit)) {
        if (mfdp_38FE80 > 1)
            FCOLOR(0x4E);
        else if (L32(p + 4) == 10 && game_396868)
            FCOLOR(0x26);
        else if (L32(p + 4) == 2) {
            s = *(uchar **)(PBA_P(game_player_slot) + 0x9C);
            if (MU16(s + 3) == MU16(s + 1))
                FCOLOR(0x26);
            else
                FCOLOR(0x84);
        } else if (L32(p + 4) == 5) {
            FCOLOR(0x26);
            s = *(uchar **)(PBA_P(game_player_slot) + 0x9C);
            n = MU16(PBA_P(game_player_slot) + 0xA0);
            for (i = 0; i < n; i++, s += 5)
                if (i != 0 && MU16(s + 3) < MU16(s + 1)) {
                    FCOLOR(0x84);
                    break;
                }
        } else
            FCOLOR(0x84);
        y = HI ? 0x52 : 0x29;
        h = HI ? 0x14 : 0xA;
        CANVAS(DRAW_BOX_, 7L, (long)(short)y, 0x4DL, (long)(short)(y + h));
        if (L32(p) == 0)
            ref = 0x16E0054;
        else if (p[8])
            ref = 0x16E0048;
        else
            ref = 0x16E003C;
        ref += L32(p + 4);
        s = RefLock(ref);
        x = 0x46 - gr_font_string_width(ResGet(0x1F7), (char *)s);
        GfxDrawText(0x1F7, (char *)s, x / 2 + 7, (HI ? 0x52 : 0x29) + 2);
        ResUnlock((ushort)(ref >> 16));
        bit = 1u << (p[4] & 31);
        if (bit & ai_fire_mask) {
            lg_sprintf(buf, RefGet(0x16E006E), L32(p));
            GfxDrawText(0x1F7, buf, 0x2A, HI ? 0x40 : 0x20);
        } else if (mfdp_35BF66 & bit) {
            ulong len = (ulong)(L32(p) * 0x44) / (ulong)damage_27E234(L32(p + 4));

            FCOLOR(0x26);
            y = HI ? 0x42 : 0x21;
            h = HI ? 0xC : 6;
            CANVAS(DRAW_BOX_, 7L, (long)(short)y, 0x4DL, (long)(short)(y + h));
            if (L32(p) != 0) {
                if (!len)
                    len = 1;
                FCOLOR(0x1D);
                CANVAS(DRAW_RECT_, 8L, (long)(short)(y + 1), (long)(short)(len + 8),
                       (long)(short)(y + h - 1));
            }
        }
    }
    if (L32(p + 4) == 0xB && p[8] && fire_35BF6C > game_time) {
        short d = (short)((fire_35BF6C - game_time) / 1000);

        if (d >= 0 && d <= 9) {
            bm = bm_get(0x1820000 + d);
            BITMAP(bm, 0x41, 3);
        }
    }
}
