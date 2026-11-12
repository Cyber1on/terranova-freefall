/* hudhint.c -- Terra Nova module M036 (0x27B560-0x27B906): the HUD hint texts (resource
 * 0x178 strings shown over the views and gauges while a flag in hud_hints[] is set; set by the
 * tutorial / mission scripts).  Decompiled from __FF.EXE v1.09.
 *
 *   0x27B560 hudutil_27B560 set a hint flag            0x27B570 hudutil_27B570 show a hint
 *   0x27B584 hudutil_27B584 clear the hints            0x27B5B0 hudutil_27B5B0 draw a string
 *   0x27B5E4 hudutil_27B5E4 draw the hint of an item   0x27B7F4 hudutil_27B7F4 set hint group
 *   0x27B8EC hudutil_27B8EC hint group on              0x27B8FC hudutil_27B8FC hint group off
 *
 * 0x27B7B8 is the jump table of 0x27B7F4's switch (data).  Every flag store is followed by a
 * helmet redraw (helm_26214C) except hud_hints[0x23] / [0x24] and the hint mode.
 */
#include "g7_c.h"
#include "g5_b.h"     /* game_35A540 */
#include "g7_a.h"     /* added at the G5-G8 merge */
#include "g7_b.h"     /* added at the G5-G8 merge */

#define DRAW_CLEAR 8

void hudutil_27B560(ushort id, uchar v)
{
    hud_hints[id] = v;
    helm_26214C();
}

void hudutil_27B570(ushort id)
{
    hud_hints[id] = 1;
    helm_26214C();
}

void hudutil_27B584(void)
{
    memset(hud_hints, 0, 0x23);
    hud_hints[0x23] = 0;
    hud_hints[0x24] = 0;
    hud_hint_mode = 0;
}

void hudutil_27B5B0(Ref ref, short x, short y)
{
    void *p = RefLock(ref);

    hud_268098(0x1F7, p, x, y, 0x4E);
    ResUnlock((ushort)(ref >> 16));
}

static void draw_frame(Ref ref)
{
    grs_bitmap *bm = RefGet(ref);

    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, 0, 0);
}

void hudutil_27B5E4(Ref id)
{
    ulong r = id;
    long x, y;
    void *p;

    if (r == 0x1780000 && hud_hints[0x1E])
        r = 0x178001E;
    if (r == 0x1780004) {
        if (hud_hints[0x1D]) {
            draw_frame(0x23D0011);
            r = 0x178001D;
        } else if (hud_hints[0x1F]) {
            draw_frame(0x23D0012);
            r = 0x178001F;
        } else if (hud_hints[0x21]) {
            WATCOM_CALL(grd_canvas_table[DRAW_CLEAR], 0);
            r = 0x1780021;
        } else if (hud_hints[0x22]) {
            WATCOM_CALL(grd_canvas_table[DRAW_CLEAR], 0);
            return;
        }
    }
    if (!hud_hints[0x25] && !hud_hints[r & 0xFFFF])
        return;
    if ((r >= 0x1780000 && r <= 0x1780002) || r == 0x178001E) {
        if (game_camera[GAME_CAM_DA]) {
            x = 0x1C;
            y = 9;
        } else {
            x = 0x38;
            y = 2;
        }
    } else if (r == 0x178001F) {
        x = 0x10;
        y = 5;
    } else if (r == 0x1780013) {
        x = 2;
        y = 0x1E;
    } else if (r == 0x178000D) {
        x = 2;
        y = 0x18;
    } else {
        x = 2;
        y = 2;
    }
    if (r == 0x178000C && game_359708 == 0x2E)
        r = 0x1780024;
    if (game_35A540 >> 1)
        y *= 2;
    p = RefLock(r);
    hud_268098(0x1F7, p, (short)x, (short)y, 0x4E);
    ResUnlock((ushort)(r >> 16));
}

void hudutil_27B7F4(long which, uchar v)
{
    switch ((ulong)which) {
    case 0:
        hud_hints[0x1D] = v;
        helm_26214C();
        break;
    case 1:
        hud_hints[1] = v;
        helm_26214C();
        hud_hints[0x0C] = v;
        helm_26214C();
        break;
    case 2:
        hud_hints[0] = v;
        helm_26214C();
        hud_hints[0x0B] = v;
        helm_26214C();
        break;
    case 3:
        hud_hints[0x13] = v;
        helm_26214C();
        hud_hints[0x14] = v;
        helm_26214C();
        break;
    case 4:
        hud_hints[0x0E] = v;
        helm_26214C();
        break;
    case 5:
        hud_hints[0x0F] = v;
        helm_26214C();
        break;
    case 6:
        hud_hints[0x0D] = v;
        helm_26214C();
        break;
    case 7:
        hud_hints[0x22] = v;
        helm_26214C();
        break;
    case 8:
        hud_hints[0x24] = v;
        break;
    case 9:
        hud_hints[0x1E] = v;
        helm_26214C();
        break;
    case 10:
        hud_hints[0x1F] = v;
        helm_26214C();
        hud_hints[0x23] = v;
        break;
    case 11:
        hud_hints[0x21] = v;
        helm_26214C();
        break;
    case 12:
        hud_hint_mode = v != 0;
        break;
    case 13:
        hud_hint_mode = v ? 2 : 0;
        break;
    case 14:
        hud_hint_mode = v ? 3 : 0;
        break;
    }
}

void hudutil_27B8EC(long which)
{
    hudutil_27B7F4(which, 1);
}

void hudutil_27B8FC(long which)
{
    hudutil_27B7F4(which, 0);
}
