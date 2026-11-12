/* mfdkeys.c -- Terra Nova module M027 (0x272750-0x27297F): activate / event / update functions
 * of the 3D HUD view modes that show what an entity sees (member, player, player with
 * look-down / look-left / look-right / map camera, aircraft).  Decompiled from __FF.EXE v1.09.
 *
 *   0x272750 mfdkey_272750 activate: eye of the current squad member
 *   0x272794 mfdkey_272794 event: member list buttons (mfd_26D1BC area) or the 3D view's event
 *   0x2727EC mfdkey_2727EC update: 3D view update, then the member list's
 *   0x2727F8 mfdkey_2727F8 activate helper (w, on, flag, kind): player's eye of kind `kind`
 *   0x27285C / 0x272878 / 0x272894 activate with kind 0xC000 / 0x4000 / 0x8000 (flag set)
 *   0x2728B0 mfdkey_2728B0 activate with kind 0
 *   0x2728C4 mfdkey_2728C4 activate kind 0; switching off re-sets up camera 0 for its canvas
 *   0x272908 mfdkey_272908 activate: eye of the aircraft (game_396868)
 *
 * Every activate function ends by setting the +0x2E word of game_438C20 (camera list changed).
 */
#include "g8_c.h"

#define CAMS_CHANGED() (*(short *)((uchar *)game_438C20 + 0x2E) = 1)
#define MEMBER(i) ((uchar *)&game_391E50[i])

void mfdkey_272750(HudWidget *w, uchar on)
{
    if (on)
        hview3d_274538(w->id, *(short *)(MEMBER(mfd_3599D4) + 2), 0);
    CAMS_CHANGED();
}

void mfdkey_272794(HudWidget *w, HudEvent *ev)
{
    if (mfd_26D1BC(w, *(short *)((uchar *)ev + 0xA))) {
        if (ev->type == 1 || ev->type == 4)
            if (mfd_26D300(ev->type == 1 ? 1 : -1))
                hudview_26C224(0, 4);
        return;
    }
    hview3d_274670(w, ev);
}

void mfdkey_2727EC(long view)
{
    hview3d_274DA4(view);
    mfd_26D1F4();
}

void mfdkey_2727F8(HudWidget *w, uchar on, uchar flag, ushort kind)
{
    if (on) {
        hview3d_274538(w->id, game_player_entity, kind);
        if (flag)
            w->flags |= 0x40;
        hud_views[w->id].timer = game_time + 0x96;
    } else
        w->flags &= ~0x40;
    CAMS_CHANGED();
}

void mfdkey_27285C(HudWidget *w, uchar on)
{
    mfdkey_2727F8(w, on, 1, 0xC000);
}

void mfdkey_272878(HudWidget *w, uchar on)
{
    mfdkey_2727F8(w, on, 1, 0x4000);
}

void mfdkey_272894(HudWidget *w, uchar on)
{
    mfdkey_2727F8(w, on, 1, 0x8000);
}

void mfdkey_2728B0(HudWidget *w, uchar on)
{
    mfdkey_2727F8(w, on, 0, 0);
}

void mfdkey_2728C4(HudWidget *w, uchar on)
{
    mfdkey_2727F8(w, on, 0, 0);
    if (!on) {
        grs_canvas *c = *(grs_canvas **)(ff_cams_390010[0] + 0x1C);

        FfCameraSetup(ff_cams_390010, c->bm.w, c->bm.h, 0x10000);
    }
}

void mfdkey_272908(HudWidget *w, uchar on)
{
    if (on) {
        hview3d_274538(w->id, game_396868, 0);
        if (air_35B6C2) {
            hud_views[w->id].timer = game_time + 0x96;
            air_35B6C2 = 0;
        }
    } else if (w == hud_drag_widget) {
        hud_drag_widget = NULL;
        MouseClearStencil();
    }
    CAMS_CHANGED();
}
