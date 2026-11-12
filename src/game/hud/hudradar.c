/*
 * hudradar.c - M018 radar / compass strip of the helmet HUD (0x265700-0x26631F, __FF.EXE v1.09).
 *
 *   0x265700 radar_265700   reset (falls into radar_265720)
 *   0x265720 radar_265720   empty (ret)
 *   0x265724 radar_265724   widget event: click picks a contact
 *   0x2657A8 radar_2657A8   widget update: redraw flag every 250 ticks
 *   0x2657F4 radar_2657F4   widget draw: compass ticks, view brackets, contacts, nav marker
 *   0x266248 radar_266248   select the contact nearest strip x
 *   0x266254 radar_266254   contact nearest strip x
 *
 * Quirks kept:
 *  - The contact list of the player is taken from the PBA indexed by the player's ENTITY id
 *    (game_pba[game_player_entity] + 0xBE), as is done elsewhere in the HUD.
 *  - radar_2657F4 counts every list entry (also the viewer's own entry and contacts drawn in
 *    colour 0, whose radar_x is left stale); when the noise roll hides the contacts it stores a
 *    stale count: the speaker-search index of the last speaker (8 when there are none).
 *  - radar_mark_x is masked with 0xFF even when the strip is wider than 256.
 *  - radar_266254 compares the distance of an equally near contact against the caller's edi
 *    (uninitialised); here it starts at 0x7FFFFFFF (only matters for a first contact exactly
 *    0x7FFF away).
 */
#include "g7_b.h"
#include "g8_c.h"     /* added at the G5-G8 merge */

#ifndef TN_WATCOM_ABI
int port_ml_improved(void);     /* Freefall: mouselook in Improved Controls (port/core/platform.c) */
#endif

#ifndef TN_LINK_ORIGINAL_DATA
uchar radar_tick_w[8];
uchar *radar_tick_bits[8];
grs_bitmap radar_bm;
Ref radar_tick_refs[8][2];
uchar radar_noise;
ushort radar_x[64];
long radar_mark_x, radar_mark_y;
ulong radar_time;
Ref radar_38BAEC;
ushort radar_count;
short hud_389BC8;
#endif

#define HIRES (game_35A540 >> 1)
#define BM_FIX(bm) ((bm)->bits = (uchar *)(bm) + 0x1C)
#define BITMAP(bm, x, y) WC3(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * (bm)->type], (bm), (x), (y))
#define RECT_FILL(a, b, c, d) WC4(grd_canvas_table[16], (a), (b), (c), (d))
#define UHLINE(x0, y, x1) WC5(grd_uline_fill_vector[2], (x0), (y), (x1), grd_canvas->gc.fcolor, \
                              grd_canvas->gc.fill_parm)
#define UVLINE(x, y0, y1) WC5(grd_uline_fill_vector[3], (x), (y0), (y1), grd_canvas->gc.fcolor, \
                              grd_canvas->gc.fill_parm)
#define E(e) (game_entities[(short)(e)])

/* The contact list radar_2657F4 / radar_266254 read: the aircraft's when the viewer is the
 * aircraft, else the player's (PBA indexed by entity, see above). */
static RadarContact *radar_list(void)
{
    if (hud_eye.obj == game_396868)
        return *(RadarContact **)(game_cls3[E(game_396868).slot] + 0x6E);
    return *(RadarContact **)((uchar *)&game_pba[game_player_entity] + 0xBE);
}

void radar_265700(void)
{
    radar_38BAEC = 0x23D0010;
    radar_time = 0;
    radar_count = 0;
}

void radar_265720(void)
{
}

void radar_265724(HudWidget *w, TnEvent *ev)
{
    if (ev->type == 1) {
        short e;
        if (hud_389BC2 == 0 || !(hud_389BC2 & 2))
            return;
        e = radar_266254((short)(ev->pt.x - w->x0));
        if (e == 0)
            return;
        hud_389BC8 = e;
        cmd_2601D8();
        cmd_25F0AC(0x2EE);
    } else if (ev->type == 4) {
        uchar *rs = (uchar *)tn_replay_state_ptr;
        if ((rs[5] & 4) || (rs[6] & 0x40))
            helm_262D4C(radar_266254((short)(ev->pt.x - hud_radar->x0)));
    }
}

void radar_2657A8(HudWidget *w)
{
    uchar flag = hud_helmet ? hud_flags_cam[0] : hud_flags_std[0];
    if (flag) {
        w->flags |= 0x20;
        return;
    }
    if (radar_time + 0xFA < game_time) {
        w->flags |= 0x20;
        radar_time = game_time;
    }
}

void radar_2657F4(HudWidget *w)
{
    short W = w->canvas.bm.w;
    uchar noise = 0;
    short H8 = HIRES ? 0x10 : 8;
    ushort hdg;
    long cx, i, n, k;
    short s48, s40, nb, m;
    long s2c;
    RadarContact *list, *c;
    fix camx, camy;
    uchar dmg;

    if (!(hud_helmet ? hud_flags_cam[0] : hud_flags_std[0])) {
        grd_canvas->gc.fcolor = 0;
        RECT_FILL(0, 0, W, (short)(H8 + 1));
    }
    hdg = E(hud_eye.obj).heading;
    grd_canvas->gc.fcolor = 0x1D;
    if (W == 0x100)
        cx = (-(long)hdg) / 256 + 0x80;
    else
        cx = W / 2 + ((-(long)(short)hdg * W) >> 16);
    if ((short)cx < 0)
        cx += W;

    /* compass ticks / letters */
    for (i = 0; i < 8; i++) {
        if ((short)cx >= W)
            cx -= W;
        if (!(i & 1)) {
            if (hud_helmet ? hud_flags_cam[0] : hud_flags_std[0]) {
                grs_bitmap *bm = (grs_bitmap *)RefGet(radar_tick_refs[i][0]);
                BM_FIX(bm);
                BITMAP(bm, (short)(cx - 3), 1);
                if (radar_tick_refs[i][1]) {
                    bm = (grs_bitmap *)RefGet(radar_tick_refs[i][1]);
                    BM_FIX(bm);
                    BITMAP(bm, (short)(cx + 3), 1);
                }
            } else {
                radar_bm.bits = radar_tick_bits[i];
                radar_bm.w = radar_tick_w[i];
                radar_bm.row = (radar_bm.w + 7) >> 3;
                BITMAP(&radar_bm, (short)(cx - 2), (short)((H8 >> 1) - 2));
            }
        }
        cx = (short)cx + W / 8;
    }

    /* view brackets */
    s48 = (short)(((long)*(ushort *)(game_camera + 0x18) * W) >> 16);
    s40 = (short)(W / 2 - 1 - s48);
    s2c = ((long)W * hud_441355) >> 16;
    if (s2c != 0) {
        short a = s40, b = (short)(s40 + 2 * s48);
#ifndef TN_WATCOM_ABI
        grd_canvas->gc.fcolor = port_ml_improved() ? 0x2F : 0x50;  /* Freefall: white as the letters */
#else
        grd_canvas->gc.fcolor = 0x50;
#endif
        UVLINE(a, 0, 1);
        UVLINE(a, (short)(H8 - 2), (short)(H8 - 1));
        UHLINE((short)(a + 1), 0, a);
        UHLINE((short)(a + 1), (short)(H8 - 1), a);
        UVLINE(b, 0, 1);
        UVLINE(b, (short)(H8 - 2), (short)(H8 - 1));
        UHLINE((short)(b - 1), 0, b);
        UHLINE((short)(b - 1), (short)(H8 - 1), b);
    }
    grd_canvas->gc.fcolor = (hmd_data.flags & 1) ? 0x9C : 0x4E;
    nb = (short)(s40 + s2c);
    UHLINE((short)(nb + 2), 0, nb);
    UHLINE((short)(nb + 2), (short)(H8 - 1), nb);
    UVLINE(nb, 0, (short)(H8 - 1));
    m = (short)(nb + 2 * s48);
    UHLINE((short)(m - 2), 0, m);
    UHLINE((short)(m - 2), (short)(H8 - 1), m);
    UVLINE(m, 0, (short)(H8 - 1));

    /* make sure every speaker is in the contact list */
    list = radar_list();
    n = 8; /* stale count (the tick loop's counter) */
    for (k = 0; k < game_391E4C; k++) {
        short se = ((short *)&game_391E50[k])[1];
        n = 0;
        if (list[0].ent) {
            while (list[n].ent != se) {
                n++;
                if (!list[n].ent)
                    break;
            }
        }
        if (!list[n].ent && n + 1 < 0x40) {
            list[n].ent = se;
            list[n].dist = (ushort)((fix_fast_pyth_dist(E(se).x - E(game_player_entity).x,
                                                        E(se).y - E(game_player_entity).y) * 6) >> 16);
            list[n + 1].ent = 0;
        }
    }

    camx = E(hud_eye.obj).x;
    camy = E(hud_eye.obj).y;
    dmg = damage_27DF14(hud_eye.obj, 1);
    if (dmg < 0x4B && !radar_noise)
        noise = (uchar)(0x4B - (dmg < 0x19 ? 0x19 : dmg));
    if (RndRange(&game_rnd_35B704, 0, 100) < (long)noise) {
        radar_noise = 1;
    } else {
        n = 0;
        for (c = list; c->ent; c++, n++) {
            short e = c->ent, s44, s3c;
            long k2;
            uchar col;
            fixang a;
            if (e == hud_eye.obj)
                continue;
            a = fix_atan2(E(e).y - camy, E(e).x - camx);
            if (W == 0x100) {
                radar_x[n] = (ushort)(((long)(ushort)a - (long)hdg) / 256 + 0x80);
            } else {
                long r = ((long)(ushort)a + 0x10000 - (long)hdg) % 0x10000;
                radar_x[n] = (ushort)(W / 2 + ((r * W) >> 16));
                if ((long)radar_x[n] >= W)
                    radar_x[n] -= W;
            }
            s3c = (short)(H8 / 2);
            s44 = (short)(H8 - (c->dist >> (HIRES ? 2 : 3)));
            if (HIRES && s44 < 2)
                s44 = 2;
            k2 = 0;
            if (game_391E4C > 0)
                for (; k2 < game_391E4C; k2++)
                    if (((short *)&game_391E50[k2])[1] == e)
                        break;
            if (k2 == game_391E4C)
                k2 = -1;
            if (k2 != -1 && s44 < 1)
                s44 = 1;
            if (s44 < 0)
                s44 = 0;
            s3c = (short)(s3c + ((s44 + 1) >> 1));
            {
                uchar cls = E(e).cls;
                if (cls < 1)
                    col = 0;
                else if (cls <= 3)
                    col = mapdisp_2769E8(e);
                else if (cls == 4)
                    col = instsim_288260(E(e).type) ? mapdisp_2769E8(e) : 0;
                else
                    col = 0;
            }
            if (col) {
                grd_canvas->gc.fcolor = col;
                gen_vline((short)radar_x[n], (short)(s3c - s44), s3c);
            }
        }
        radar_noise = 0;
    }
    radar_count = (ushort)n;

    /* nav point marker */
    if (ffpipe_38FFF0 && ffpipe_38FFF4) {
        fixang a = fix_atan2(ffpipe_38FFF4 - camy, ffpipe_38FFF0 - camx);
        short s50;
        long yc, y0, y1;
        if (W == 0x100) {
            radar_mark_x = ((long)(ushort)a - (long)hdg) / 256 + 0x80;
        } else {
            long r = ((long)(ushort)a + 0x10000 - (long)hdg) % 0x10000;
            radar_mark_x = ((r * W) >> 16) + W / 2;
            if (W <= radar_mark_x)
                radar_mark_x -= W;
        }
        radar_mark_x &= 0xFF;
        yc = H8 / 2;
        s50 = (short)(H8 - 2);
        if (s50 < 0)
            s50 = 0;
        yc = (short)yc + ((s50 + 1) >> 1);
        grd_canvas->gc.fcolor = 0x9C;
        gen_vline((short)radar_mark_x, (short)(yc - s50), (short)yc);
        radar_mark_y = (short)yc - s50 / 2;
        y1 = (short)(yc - 1);
        y0 = (short)(yc - 1 - (s50 - 2));
        gen_vline((short)((radar_mark_x + W - 1) % W), (short)y0, (short)y1);
        gen_vline((short)((radar_mark_x + W + 1) % W), (short)y0, (short)y1);
    } else {
        radar_mark_x = -1;
    }
}

void radar_266248(long x)
{
    helm_262D4C(radar_266254((short)x));
}

short radar_266254(short x)
{
    RadarContact *c = radar_list();
    short best = 0x7FFF;
    long res = 0, bd = 0x7FFFFFFF, i;
    for (i = 0; i < radar_count; i++, c++) {
        long d = x - (long)radar_x[i];
        if (d < 0)
            d = -d;
        if ((short)d > best)
            continue;
        if ((short)d == best && (long)c->dist > bd)
            continue;
        best = (short)d;
        res = c->ent;
        bd = c->dist;
    }
    return (short)res;
}
