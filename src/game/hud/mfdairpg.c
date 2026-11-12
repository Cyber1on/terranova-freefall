/* mfdairpg.c -- Terra Nova module M024 (0x271C60-0x271D5F): an MFD page (view mode 0x10:
 * the aircraft page).  Decompiled from __FF.EXE v1.09.
 *
 *   0x271C60 mfdpage_271C60 event: a press calls the aircraft (mode 0x12)
 *   0x271C94 mfdpage_271C94 draw: background, aircraft state text and count
 */
#include "g8_b.h"

#define PBA_PLAYER ((uchar *)&game_pba[game_player_slot])

typedef void (*CanvasFn)();

void mfdpage_271C60(HudWidget *w, HudEvent *ev)
{
    if (ev->type == 1 && aircraft_283A94()) {
        hudview_26C224(w->id, 0x12);
        ((uchar *)&hud_views[w->id].mode_mask)[2] &= 0xFE;
    }
}

void mfdpage_271C94(long view)
{
    char buf[0x20];
    grs_bitmap *bm = RefGet(0x23D0008);
    uchar n;

    (void)view;
    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, 0, 0);
    grd_canvas->gc.fcolor = 0x1D;
    GfxDrawText(0x1F7, RefGet(PBA_PLAYER[0xDC] ? 0x16E006F : 0x16E0070), 4, 4);
    n = PBA_PLAYER[0xDC];
    lg_sprintf(buf, RefGet(0x16E006E), (long)n);
    GfxDrawText(0x1F7, buf, 0x32, 0x2D);
}
