/* playkey.c -- in-mission keys, module M029 (Terra Nova v1.09, __FF.EXE).
 *
 *   0x2759F0 play_2759F0     draw the "paused" banner
 *   0x275AB4 PlayKeyHandler  the mission's key handler
 *   0x2765F4 play_2765F4     movement keys -> strafe / jump (each frame)
 *   0x27696C play_27696C     ctrl-escape (stop a replay), Alt-5 / 6 / 7 (debugger breaks)
 *
 * Key codes are cooked codes (kb_cook): 0x100 shift-ish down flag is not part of them; 0x200
 * ctrl, 0x400 alt, 0x800 function / keypad keys (F1 = 0x83B ...), 0x1000 / 0x2000 more
 * modifier bits.  The handler switches on toupper(code); the weapon keys read the raw code.
 */
#include "g9_b.h"
#include "g1_c.h"
#include "g2_a.h"
#include "g2_b.h"
#include "g3_a.h"
#include "g3_b.h"
#include "g3_c.h"
#include "g4_a.h"
#include "g7_a.h"
#include "g7_b.h"
#include "g7_c.h"
#include "g8_c.h"
#include "g10_c.h"     /* added at the G9/G10 merge */

#define PBA(i) ((uchar *)&game_pba[i])
#define ENT_SLOT(e) (game_entities[e].slot)

#ifndef TN_WATCOM_ABI
extern int port_mouselook;              /* port/ui/options.c: Advanced controls */
void port_ml_whirr_stop(void);          /* src/game/input/control.c: the mouse's head loops off */
#endif

void play_2759F0(char *text)
{
    gr_set_canvas(&tnui_back_canvas);
    play_pause_rect = play_pause_rect_tmpl;
    if (tnui_full_rect.lr.y > 200) {
        play_pause_rect.ul.y *= 2;
        play_pause_rect.lr.y *= 2;
    }
    main_2454BC(&play_pause_rect, 0x2a, 0x28, 0);
    grd_canvas->gc.fcolor = 0x2d;
    options_2CAAC0(&play_pause_rect, text, 0x1f8, &tnui_back_canvas);
    GfxBlit(&tnui_full_rect);
}

static uchar is_pause_key(ulong type, ulong code)
{
    return type == 0x100 && (code == 0x87f || toupper(code) == 'P');
}

/* weapon keys 1-4 (and shifted ! @ # $): slot 0..3; an odd slot falls back to the even one
 * below it when the armour has no second hand-held weapon or the even slot's ordnance flag says
 * so; nothing happens when the slot is the current one or is empty */
static uchar weapon_key(ulong code)
{
    uchar *p = PBA(game_player_slot);
    long cur = (schar)p[0xce], i, want;

    if (code >= 0x31 && code <= 0x34)
        i = code - 0x31;
    else if (code == 0x21)
        i = 0;
    else if (code == 0x40)
        i = 1;
    else if (code == 0x23)
        i = 2;
    else
        i = 3;                          /* 0x24 */
    want = i;
    if (i % 2 == 1) {
        p = PBA(game_player_slot);
        i -= 1;
        if (!p[7] || ord_table[p[i * 15 + 0x45]].g0[1])
            want = i;
    }
    if (cur == want)
        return 1;
    p = PBA(game_player_slot);
    if (!p[want * 15 + 0x45])
        return 1;
    {
        uchar f = p[0xcf];
        p[0xce] = (uchar)want;
        if (f & 2)
            weapons_290858();
    }
    p = PBA(game_player_slot);
    *(long *)(p + 0x82) = -1;
    ((uchar *)hud_w_389F34)[0x20] |= 0x20;
    pbasim_28AB1C(p + 6, (uchar)want);
    pbasim_28AA50();
    return 1;
}

static uchar fire_key(void)
{
    uchar *p = PBA(game_player_slot);

    if (*(long *)(p + 0xd) != -1 && *(long *)(p + 9) != 0) {
        if ((1UL << (p[0xd] & 31)) & ai_fire_mask)
            WeaponFire(game_player_entity);
        else
            fire_292970(game_player_entity, p[0x11] == 0);
    }
    return 1;
}

static uchar map_layer(uchar bit)
{
    hud_map_layers ^= bit;
    if (game_camera_38FB20)
        ((uchar *)game_camera_38FB20)[0x27] = hud_map_layers;
    hudview_26CAF0(hud_map_layers);
    return 1;
}

uchar PlayKeyHandler(void *ev, void *data)
{
    ulong type = *(ulong *)ev;
    ulong code = *(ushort *)((uchar *)ev + 4);
    ulong k;
    (void)data;

    if (game_quitting)
        return 0;
    if (game_paused && !is_pause_key(type, code))
        return 0;
    if (tnui_key_handler(ev, (void *)code))
        return 1;
    if (is_pause_key(type, code)) {
        if (game_paused) {
            *(ushort *)&tn_task_mask |= 0xe0c;
            game_slow = 0;
            GameTimeResume2();
            snd_radio_mode(0);
            game_paused_menu = 0;
        } else {
            *(ushort *)&tn_task_mask &= 0xf1f3;
            GameTimePause();
#ifndef TN_WATCOM_ABI
            if (port_mouselook)         /* mouselook's head turning stops with the pause */
                port_ml_whirr_stop();   /* (control.c) */
#endif
            game_slow = 1;
            MouseHide();
            gr_set_canvas(grd_canvas);
            play_2759F0(RefGet(0x1720012));
            game_paused_menu = 1;
            snd_radio_mode(1);
            MouseShow();
        }
        return 1;
    }
    if (type < 0x100)
        return 0;
    if (type != 0x100) {
        if (type != 0x200 || (uchar)toupper(code) != ' ')
            return 0;
        obj55_288EC0(PBA(game_player_slot) + 0xa2, 0);      /* space released */
        return 1;
    }
    if (hud_38FC80 && cmd_25F3B8(code))
        return 1;
    k = (ulong)toupper(code);
    switch (k) {
    case 0x0d: case 0x200d:
        if (!hud_38FD03)
            cmd_25F138();
        return 1;
    case 0x08:
        hud_268B30();
        return 1;
    case 0x09:
        if (hud_38B9AD != 3) {
            if (tnm_frozen) {
                hud_drag_widget = 0;
                MouseIntDrawOn();
                MouseUnfreeze();
                MouseSetCursor(0x1f90016);
            }
            hud_35AEF0 ^= 1;
            helm_263BE4();
        }
        return 1;
    case 0x83b: case 0x83c: case 0x83d:                    /* F1-F3: squad member */
        if (!hud_38FC80 && game_391E4C > (long)(k - 0x83a))
            cmd_25F0C0(k - 0x83a, 1);
        return 1;
    case 0x83e:                                             /* F4 */
        if (!hud_38FC80 && game_391E4C > 1)
            cmd_25F0C0((short)game_391E4C, 1);
        return 1;
    case 0x21: case 0x23: case 0x24: case 0x31: case 0x32: case 0x33: case 0x34: case 0x40:
        return weapon_key(code);
    case 0x25: case 0x35: case 0x1035:
        return fire_key();
    case 0x20:                                              /* space pressed */
        obj55_288EC0(PBA(game_player_slot) + 0xa2, 1);
        control_2D8568();
        return 1;
    case 0x3f: case 0x48:
        options_2CCC28();
        return 1;
    case 0x42:
        hudview_26C820();
        return 1;
    case 0x47: case 0x247:
        if (game_359708 == 0x2e || game_359708 == 0x2f)
            return 1;
        return hud_26AC98(hud_helmet == 0);
    case 0x49:
        if (game_359708 != 0x27)
            fire_292180();
        return 1;
    case 0x4c:
        helm_262DB8();
        return 1;
    case 0x4d:
        fire_2921A8();
        return 1;
    case 0x4e:
        hudcb_26485C(1);
        return 0;
    case 0x2b: case 0x3d: case 0x202b:
#ifndef TN_WATCOM_ABI
        if (port_mouselook && game_camera_38FB20) {     /* Advanced: the full map's zoom, */
            hud_26AFB0(1);                              /* as its zoom buttons do */
            return 1;
        }
#endif
        hud_26B800(1);
        hudcb_264D68();
        return 1;
    case 0x2d: case 0x5f: case 0x202d:
#ifndef TN_WATCOM_ABI
        if (port_mouselook && game_camera_38FB20) {
            hud_26AFB0(-1);
            return 1;
        }
#endif
        hud_26B800(-1);
        hudcb_264C38();
        return 1;
    case 0x2c: case 0x3c:
        hudcb_264A84(0);
        return 0;
    case 0x2e: case 0x3e:
        hudcb_264A84(1);
        return 0;
    case 0x28: case 0x39:
        if (game_38FC12[0][0] >= 2 && game_38FC12[0][0] <= 5) {
            SndPlay(0x7e8, 0, 0x28, 1, 0x40, 0x40, 0);
            hudview_26C360(0, 1, 0);
        }
        return 1;
    case 0x29: case 0x30:
        SndPlay(0x7e8, 0, 0x28, 1, 0x40, 0x40, 0);
        hudview_26C360(2, 1, 0);
        return 1;
    case 0x54: case 0x5b:
        targview_292FC4(0xff);
        return 1;
    case 0x59: case 0x5d:
        targview_292FC4(1);
        return 1;
    case 0x60:
        if (PBA(ENT_SLOT(game_player_entity))[0x39d])
            biped_27EA60(game_player_entity);
        else
            biped_27E8FC(game_player_entity, 0xccc, 0x8000);
        return 0;
    case 0x46a:
        joy_center();
        return 0;
    case 0x46: case 0x84c:
        hmd_recenter = 1;
        return 1;
    case 0xa49: case 0x1a49: case 0x2a49: case 0x3a49:
        air_alt += 0x4000;
        if (air_alt > 0x40000)
            air_alt = 0x40000;
        return 1;
    case 0xa51: case 0x1a51: case 0x2a51: case 0x3a51:
        air_alt -= 0x4000;
        if (air_alt < 0x8000)
            air_alt = 0x8000;
        return 1;
    case 0x2a47: case 0x3a47:
        aircraft_283AFC();
        return 1;
    case 0x842: case 0x1842:
        hud_268414();
        return 1;
    case 0x843: case 0x1843:
        return map_layer(1);
    case 0x844: case 0x1844:
        return map_layer(2);
    case 0x857: case 0x1857:
        return map_layer(4);
    case 0x858: case 0x1858:
        if (game_359708 == 0x2e || game_359708 == 0x2f)
            return 1;
        return map_layer(8);
    }
    if (cmd_2613E0(code))
        return 1;
    return play_27696C(code);
}

/* The movement keys (scan codes play_move_keys, bit i for key i) held this frame: alt turns
 * the turn keys (bits 7 and 8) into strafes (bits 11 and 12).  With ctrl, the first two
 * matching entries of play_move_table give the aircraft's controls; with shift, a movement key
 * jumps the player sideways / forwards (once until the footstep counter allows it again). */
void play_2765F4(void)
{
    uchar *ks = tn_replay_state_ptr;
    short buf[6];                      /* esp+0x00: damage_27C8B8 output */
    long val[2];                       /* esp+0x0C */
    fix s, c;
    ushort bits = 0, ang;
    long i, n = 0, ctrl;
    PlayMove *m;
    uchar *e, *p;

#define KEY(sc) (ks[(sc) >> 3] & (1 << ((sc) & 7)))
    for (i = 0; play_move_keys[i]; i++)
        if (KEY(play_move_keys[i]))
            bits |= 1 << i;
    if ((bits & 0x80) && (ks[7] & 1 || ks[0x17] & 1))
        bits = (bits & 0xf77f) | 0x800;
    if ((bits & 0x100) && (ks[7] & 1 || ks[0x17] & 1))
        bits = (bits & ~0x1100) | 0x1000;
    ctrl = (ks[3] & 0x20) || (ks[0x13] & 0x20);
    val[1] = play_move_default;
    if (bits) {
        m = play_move_table;
        for (; m->mask; m++) {
            if (m->mask & bits) {
                val[n++] = m->val;
                bits &= ~m->clear;
                if (n == 2)
                    break;
            }
        }
        if (!m->mask && n == 0)
            bits = 0;
    }
    if (play_last_bits || (bits && ctrl)) {
        long v = bits && ctrl ? val[0] : play_move_default;
#ifndef TN_WATCOM_ABI
        /* G47: the game finds two held keys and uses the first only, so the drone did not turn
         * while it flew forward or back.  A turn key (its point on the middle row) held with a
         * moving key: the moving key's control, and the turn key's turn put into it below.  (The
         * keys stand for points of the game's steering square: the middle row turns, the middle
         * column flies forward or back, the lower corners strafe.) */
        long turn = 0, slant = 0;
        if (bits && ctrl && n == 2) {
            int t0 = (val[0] >> 16) == (play_move_default >> 16), t1 = (val[1] >> 16) == (play_move_default >> 16);
            if (t0 != t1) {
                turn = t0 ? val[0] : val[1];
                v = t0 ? val[1] : val[0];
            } else if (!t0) {
                /* and a strafe key (its point beside the middle column) with a forward or back
                 * key: the forward or back key's control, flown 45 degrees to the strafe's side */
                int m0 = (short)val[0] == (short)play_move_default, m1 = (short)val[1] == (short)play_move_default;
                if (m0 != m1) {
                    long sv = m0 ? val[1] : val[0];
                    v = m0 ? val[0] : val[1];
                    slant = (short)sv > (short)play_move_default ? 0x2000 : -0x2000;
                }
            }
        }
#endif
        if (game_396868) {
            damage_27C8B8(v, play_move_size, buf);
            damage_27CB58(buf, (fix *)(game_cls3[ENT_SLOT(game_396868)] + 0x132));
#ifndef TN_WATCOM_ABI
            if (turn) {
                fix t[2];
                damage_27C8B8(turn, play_move_size, buf);
                damage_27CB58(buf, t);
                ((short *)(game_cls3[ENT_SLOT(game_396868)] + 0x132))[2] = ((short *)t)[2];
            }
            if (slant) {                /* (flying back, the speed is negative: the other way) */
                fix *o = (fix *)(game_cls3[ENT_SLOT(game_396868)] + 0x132);
                ((ushort *)o)[3] = (ushort)(o[0] < 0 ? -slant : slant);
            }
#endif
            ailow_24BB1C(game_entities[game_396868].f21, 0);
        }
        ks = tn_replay_state_ptr;
        play_last_bits = ctrl ? bits : 0;
    }
    tn_replay_state_ptr = ks;
    if (!(bits & 0x1e75) || !(ks[5] & 4 || ks[6] & 0x40))
        return;
    if (game_441359 != 0 || game_time <= (ulong)game_44135D || game_441361 < 4)
        return;
    p = PBA(ENT_SLOT(game_player_entity));
    if (*(long *)(p + 0x3c8) >= 0x30000)
        return;
    if (bits & 0x205)
        ang = 0;
    else if (bits & 0x1040)
        ang = 0x4000;
    else if (bits & 0x420)
        ang = 0x8000;
    else
        ang = 0xc000;
    e = (uchar *)&game_entities[game_player_entity];
    ang += game_entities[game_player_entity].heading;
    fix_sincos(ang, &s, &c);
    EDMS_marble_add_I14(e[0x22], (fix)((ulong)c * 120), (fix)((ulong)s * 120), 0x500000);
    game_441359 = 2;
    game_44135D = game_time + 0xfa;
#undef KEY
}

/* ctrl-escape stops a replay; Alt-5 / 6 / 7 drop into the debugger (int 3 with the key's name
 * in eax and ds in edx: the original's debug hooks) */
uchar play_27696C(ulong code)
{
    char *s;

    if (code == 0x61b) {
        if (tn_replay_playing)
            replay_29A40C();
        return 1;
    }
    if (code == 0x635)
        s = play_str_alt5;
    else if (code == 0x636)
        s = play_str_alt6;
    else if (code == 0x637)
        s = play_str_alt7;
    else
        return 0;
    __asm__ volatile("mov %%ds, %%edx\n\tint3" : : "a"(s) : "edx", "memory");
    return 1;
}
