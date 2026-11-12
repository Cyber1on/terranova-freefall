/* mfdsyspg.c -- Terra Nova module M023 (0x271620-0x271C5F): MFD system status pages (view
 * mode 11 on view 2, and the systems pages of mode 3).  Decompiled from __FF.EXE v1.09.
 *
 *   0x271620 mfdpage_271620 mode 11 activate      0x271638 mfdpage_271638 show page n
 *   0x271648 mfdpage_271648 mode 11 event (next page)
 *   0x271684 mfdpage_271684 mode 11 update        0x271690 mfdpage_271690 mode 11 draw
 *   0x2716E8 mfdpage_2716E8 damaged-systems list of the player
 *   0x271714 mfdpage_271714 condition page of the player (falls into 0x27172C)
 *   0x27172C mfdpage_27172C condition page: four body parts in %
 *   0x2717F0 mfdpage_2717F0 damaged-systems list of entity ent (reached by a jump from
 *            mfd_26DEE8; not in the function map)
 *   0x271828 mfdpage_271828 lg_qsort compare: by % then by system number
 *   0x271854 mfdpage_271854 damaged-systems list, 5 lines per page
 *   0x271C0C mfdpage_271C0C system-status key: show / next page / switch off
 *
 * A PBA's systems (pointer at PBA +0x9C, count at +0xA0) are 5-byte records:
 * +1 ushort maximum, +3 ushort current.  The condition page reads 4 records of 7 bytes at
 * PBA +6+0x0F: short current, short maximum.
 *
 * Quirk kept: mfdpage_271854 collects the systems into a 16-byte stack list; more than 9
 * systems would overrun it (the C list is larger).
 */
#include "g8_a.h"
#include "t1_c.h"      /* lg_qsort */

#define HI (game_35A540 >> 1)
#define FCOLOR(c) (grd_canvas->gc.fcolor = (c))
#define DIRTY(w) ((w)->flags |= 0x20)
#define PLAYER_P ((uchar *)&game_pba[game_player_slot] + 6)
#define MS16(p) (*(short *)(p))
#define MU16(p) (*(ushort *)(p))

static void draw_ufr(Ref ref, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);
    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, x, y);
}

void mfdpage_271620(HudWidget *w, uchar on)
{
    if (on) {
        mfdpage_35B274 = 1;
        mfdpage_35B276 = 0;
    }
}

void mfdpage_271638(long page)
{
    mfdpage_35B276 = (schar)page;
    DIRTY(HUD_VIEW_W(2));
}

void mfdpage_271648(HudWidget *w, HudEvent *ev)
{
    uchar c = mfdpage_35B276;

    if (ev->type == 1) {
        c = (uchar)(((uchar)mfdpage_35B276 + 1) % ((uchar)mfdpage_35B274 + 1));
        DIRTY(HUD_VIEW_W(2));
    }
    mfdpage_35B276 = c;
}

void mfdpage_271684(long view)
{
    DIRTY(HUD_VIEW_W(view));
}

void mfdpage_271690(void)
{
    if (mfdpage_35B276 == mfdpage_35B274)
        mfdpage_27172C(PLAYER_P);
    else
        mfdpage_271854(PLAYER_P, &mfdpage_35B276, &mfdpage_35B274);
}

void mfdpage_2716E8(void)
{
    mfdpage_271854(PLAYER_P, &mfdpage_35B276, &mfdpage_35B274);
}

void mfdpage_271714(void)
{
    mfdpage_27172C(PLAYER_P);
}

void mfdpage_27172C(uchar *p)
{
    char buf[0x50];
    Ref id = 0x1710037;
    long i, pct, col;

    draw_ufr(0x2190001, 0, 0);
    FCOLOR(0x4E);
    GfxDrawText(0x1F7, RefGet(0x171002C), 0x14, 2);
    FCOLOR(0x1D);
    for (i = 0; i < 4; i++, p += 7, id++) {
        short cur = MS16(p + 0xF), mx = MS16(p + 0x11);

        pct = cur * 100 / mx;
        if (cur > mx * 0x49 / 100)
            col = 0x82;
        else if (cur < mx * 0x25 / 100 && cur > 0) {
            col = 0x41;
            if (!pct)
                pct = 1;
        } else if (cur == 0)
            col = 0x25;
        else
            col = 0x4D;
        FCOLOR(col);
        GfxDrawText(0x1F7, RefGet(id), 5, (HI ? 0xC : 6) * i + (HI ? 0x12 : 9));
        lg_sprintf(buf, RefGet(0x171002D), pct);
        GfxDrawText(0x1F7, buf, 0x32, (HI ? 0xC : 6) * i + (HI ? 0x12 : 9));
    }
}

void mfdpage_2717F0(long ent)
{
    mfdpage_271854((uchar *)&game_pba[game_entities[(short)ent].slot] + 6, &mfdpage_35B277,
                   &mfdpage_35B275);
}

long mfdpage_271828(uchar *a, uchar *b)
{
    if (a[1] != b[1])
        return a[1] - b[1];
    return a[0] - b[0];
}

void mfdpage_271854(uchar *p, schar *page, schar *npages)
{
    char buf[0x20];
    uchar list[0x200];
    uchar *sys;
    long n = 0, idx, pages, v, start, j, y;

    draw_ufr(0x23D0009, 0, 0);
    sys = *(uchar **)(p + 0x96);
    for (idx = 0; idx < MU16(p + 0x9A); idx++, sys += 5)
        if (idx) {
            list[n * 2 + 1] = (uchar)(MU16(sys + 3) * 100 / MU16(sys + 1));
            list[n * 2] = (uchar)idx;
            n++;
        }
    pages = (n + 4) / 5;
    if (pages < 1)
        pages = 1;
    *npages = (schar)pages;
    v = *npages - 1;
    if (v >= *page)
        v = *page;
    *page = (schar)v;
    lg_qsort(list, n, 2, WATCOM_FN(mfdpage_271828));
    start = *page * 5;
    if (start + 5 < n)
        n = start + 5;
    FCOLOR(0x1D);
    if (*npages < 2)
        GfxDrawText(0x1F7, RefGet(0x171002A), 9, 2);
    else {
        lg_sprintf(buf, "%s  %i/%i", (char *)RefGet(0x171002A), *page + 1, *npages);
        GfxDrawText(0x1F7, buf, 9, 2);
    }
    y = HI ? 0x12 : 9;
    for (j = start; j < n; j++) {
        FCOLOR(0x1D);
        GfxDrawText(0x1F7, RefGet(0x16E0060 + list[j * 2]), 5, y);
        lg_sprintf(buf, RefGet(0x171002D), list[j * 2 + 1]);
        GfxDrawText(0x1F7, buf, 0x32, y);
        y += HI ? 0xE : 7;
    }
    lg_sprintf(buf, RefGet(0x171002B), (uchar)objacc_28B8B0((ObjMeter *)(p + 0x80)));
    FCOLOR(0x1D);
    GfxDrawText(0x1F7, buf, 4, HI ? 0x59 : 0x2C);
}

void mfdpage_271C0C(void)
{
    if (hud_views[2].mode != 0xB)
        hudview_26C224(2, 0xB);
    else if (mfdpage_35B276 == mfdpage_35B274) {
        mfdpage_35B276 = 0;
        DIRTY(HUD_VIEW_W(2));
    } else
        hudview_26C224(2, 0xA);
}
