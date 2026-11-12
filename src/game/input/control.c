/* control.c -- player controls (M116, no assert path), Terra Nova v1.09.  Decompiled from
 * __FF.EXE.
 *
 *   0x2D7910 CtrlClearSlots    0x2D7960 CtrlApplySlots     0x2D7E38 control_2D7E38
 *   0x2D7F2C CtrlKeyboard      0x2D8098 control_2D8098     0x2D80C4 joy_axis
 *   0x2D8138 CtrlJoystick      0x2D828C CtrlHmd            0x2D8568 control_2D8568
 *
 * Every frame tnui_replay_task (0x2F2C2C) clears the control slots and lets the keyboard,
 * joystick and head tracker fill them: game_ctl_slots[0..2] for the soldier, game_ctl_ctrl[0..2]
 * when Ctrl (or joystick button 4) is held.  The physics frame then adds the slots up and applies
 * them: CtrlApplySlots to the player's PBA (move through EDMS, turn, look, crouch/jump, weapon
 * and HUD functions via mis103_2C9180), control_2D7E38 the Ctrl slots to the aircraft code.
 * Slot values are 16.16 in [-1, 1]:
 *   v[0] forward  v[1] strafe  v[2] turn  v[4] look up/down  v[5], v[6], v[7] mis103 kinds 3, 5, 6
 *   v[8] turn (head tracker, added to v[2] after scaling)
 *   flags: 1 look centre, 2 / 4 / 8 mis103 kind 2, 3 = both, 0x20 / 0x40 / 0x80 jump state,
 *   0x100 (byte 1 bit 0) HUD button held.
 * Map note: 0x2D828C is a function of its own (the map counted it in CtrlJoystick).
 * Quirks kept: some keys add to the high word of a value (inc word), which wraps at 16 bits;
 * CtrlApplySlots / control_2D7E38 clear only the nine values of game_ctl_sum, not its flags,
 * which they overwrite.
 */
#include "g1_c.h"
#include "g2_b.h"      /* obj55_288EC0 (owner g2b) */
#include "g7_a.h"     /* added at the G5-G8 merge */
#include "g9_b.h"     /* added at the G9/G10 merge */

#define HIW(f) (((short *)&(f))[1])
#define FLAGS0(s) (((uchar *)&(s)->flags)[0])
#define FLAGS1(s) (((uchar *)&(s)->flags)[1])
#define PBA_B(slot) ((uchar *)&game_pba[slot])
#define HMD_B ((uchar *)&hmd_data)    /* HmdData (t2_a.h) read as raw bytes */
#define HD16(off) (*(ushort *)(HMD_B + (off)))
#define HS16(off) (*(short *)(HMD_B + (off)))

static inline fix fmul(fix a, fix b)
{
    return (fix)(((long long)a * b) >> 16);
}

#ifndef TN_WATCOM_ABI
/* mouselook (port/core/platform.c): the mouse's turn and look since the last frame (angle units);
 * cx, cy: the centre of the main view, where the captured pointer aims; map: the map fills the
 * main view (M puts the 3D view, mode 8, in a side view), mouselook waits */
int port_mouselook_take(int *turn, int *look, int cx, int cy, int map);
extern int port_gimbal;

/* Advanced controls: F turns the player toward the view while the head turns back the same
 * amount, so the view stays put (like torso alignment in MechWarrior); 0x20 angle units a tick,
 * 90 degrees in half a second.  Gimbal mode (port_gimbal) keeps doing it, as if F were held. */
static int ml_align;
#define ML_ALIGN_RATE 0x20

/* The head's whirr with the mouse (owner): the game starts the servo loop in any
 * frame the head turns and stops it in the first frame it doesn't, so with the mouse every
 * small aiming touch made a blip and a slow move stuttered.  For the mouse the loop starts once
 * the head has turned more than ML_WHIRR_ANGLE within the last ML_WHIRR_SLOTS slots (0.2 s of
 * game time) and keeps on until the head has been still ML_WHIRR_HOLD ms.  One for turning, one
 * for looking up / down; the keys (Q / E / R / V) keep the game's own rule. */
#define ML_WHIRR_ANGLE 0x2D8            /* 4 degrees (0x10000 = a full turn; owner, 2 then 3 before) */
#define ML_WHIRR_SLOT 20                /* ms of game time a slot */
#define ML_WHIRR_SLOTS 10
#define ML_WHIRR_HOLD 150               /* ms still before it stops */
typedef struct {
    long slot[ML_WHIRR_SLOTS];          /* how far the head turned in each slot */
    ulong cur, last;                    /* the current slot; when the head last moved */
    int on;
} MlWhirr;
static MlWhirr ml_whirr[2];             /* turning, looking up / down */

/* moved: how far the mouse just turned the head (angle units); 1 while the loop should play */
static int ml_whirr_gate(MlWhirr *w, long moved)
{
    ulong now = game_time, k = now / ML_WHIRR_SLOT;
    long sum = 0;
    int i;

    if (k - w->cur >= ML_WHIRR_SLOTS)   /* (also game time started again) */
        memset(w->slot, 0, sizeof w->slot);
    else
        while (w->cur != k)
            w->slot[++w->cur % ML_WHIRR_SLOTS] = 0;
    w->cur = k;
    if (moved < 0)
        moved = -moved;
    if (moved) {
        w->slot[k % ML_WHIRR_SLOTS] += moved;
        w->last = now;
    }
    for (i = 0; i < ML_WHIRR_SLOTS; i++)
        sum += w->slot[i];
    if (w->on && now - w->last >= ML_WHIRR_HOLD)
        w->on = 0;
    else if (!w->on && moved && sum > ML_WHIRR_ANGLE)
        w->on = 1;
    return w->on;
}

/* the pause (playkey.c): the mouse's turning and looking loops stop, and stay off after it */
void port_ml_whirr_stop(void)
{
    ml_whirr[0].on = ml_whirr[1].on = 0;
    snd_loop_ctl(0, 0);
    snd_loop_ctl(1, 0);
}

/* Freefall option (port_auto_targcam): the left screen (view 0) shows the target camera
 * (mode 0x15, as the target button does) when a new target is picked, and again when a squad
 * screen (F1-F4 comms or orders) closes back to the squad list (mode 1) while a target is still
 * picked; the game itself goes back to the squad list when the target is gone.  Not while the
 * screen shows an order menu or a dialog. */
extern int port_auto_targcam;
extern short targ_current;
void hudview_26C224(long view, long mode);
#define DIRTY(w) ((w)->flags |= 0x20)

static int targcam_busy(uchar m)
{
    return m == 0x15 || m == 0 || m == 7 || m == 0x17 || m == 0x1A || m == 0x1B;
}

static void auto_target_camera(void)
{
    static short last;
    static uchar prev = 1;
    uchar m = hud_views[0].mode, was = prev;
    int fresh = targ_current != last;

    last = targ_current;
    prev = m;
    if (!port_auto_targcam || !targ_current || targcam_busy(m))
        return;
    /* a new target, or a squad screen just closed (not the player leaving the target camera) */
    if (fresh || (m == 1 && was != 1 && !targcam_busy(was))) {
        hudview_26C224(0, 0x15);
        DIRTY(hud_widgets[0x14]);
        prev = 0x15;
    }
}
#endif

static inline fix clamp1(fix v)
{
    if (v > 0x10000)
        return 0x10000;
    if (v < -0x10000)
        return -0x10000;
    return v;
}

void CtrlClearSlots(void)
{
    int k;

    for (k = 0; k < 3; k++) {
        memset(&game_ctl_slots[k], 0, 0x24);
        memset(&game_ctl_ctrl[k], 0, 0x24);
    }
    memset(&game_ctl_sum, 0, 0x24);
}

void CtrlApplySlots(void)
{
    fix speed, sinv, cosv, mx, my;
    long newflags = 0, snd;
    uchar changed = 0;
    ulong turn, look, ramp, old;
    int i, k;
#ifndef TN_WATCOM_ABI
    int ml_turn, ml_look, ml_head;
    HudWidget *mw = hud_w_389DF4;
    int ml_map = hud_views[1].mode == 8 || hud_views[2].mode == 8;
    static int gimbal_shown;

    if (gimbal_shown != port_gimbal) {   /* the label (helmet.c) */
        gimbal_shown = !gimbal_shown;
        if (HUD_VIEW_W(1) && !(hud_helmet && (hud_35AEF0 & 1)))    /* (P105: not while hidden;
                                                                      Tab redraws it) */
            DIRTY(HUD_VIEW_W(1));
    }
    auto_target_camera();

    if (mw)
        ml_head = port_mouselook_take(&ml_turn, &ml_look, (mw->x0 + mw->x1) / 2,
                                      (mw->y0 + mw->y1) / 2 - (hud_helmet ? 0 : 10), ml_map);
    else
        ml_head = port_mouselook_take(&ml_turn, &ml_look, 160, 100, ml_map);
    if (game_35B77C)                    /* P100: the mission is over (the player died, ran off the
                                           map, ended it): the game's keys are off, so is the
                                           mouse's look, and with it the head's whirr */
        ml_turn = ml_look = 0;
#endif

    speed = fmul(*(fix *)(PBA_B(game_player_slot) + 0xC6), 0x20000) >> 1;
    for (i = 0; i < 9; i++) {
        for (k = 0; k < 3; k++)
            game_ctl_sum.v[i] += game_ctl_slots[k].v[i];
        game_ctl_sum.v[i] = clamp1(game_ctl_sum.v[i]);
    }
    game_ctl_sum.flags |= game_ctl_slots[0].flags;
    game_ctl_sum.flags |= game_ctl_slots[1].flags;
    game_ctl_sum.flags |= game_ctl_slots[2].flags;
#ifndef TN_WATCOM_ABI
    if (!ml_head)
        ml_align = 0;
    else if ((game_ctl_sum.flags & 3) || port_gimbal) {  /* F: align the player, keep the view */
        ml_align = 1;
        game_ctl_sum.flags &= ~3;
    }
#endif
    game_ctl_sum.v[0] = fmul(game_ctl_sum.v[0], speed);
    game_ctl_sum.v[1] = fmul(game_ctl_sum.v[1], speed);
    game_ctl_sum.v[2] = fmul(game_ctl_sum.v[2], 0x800);
    game_ctl_sum.v[4] = fmul(game_ctl_sum.v[4], 10);
    game_ctl_sum.v[5] = fmul(game_ctl_sum.v[5], 10);

    /* turning: the keyboard/joystick rate accelerates from 0.375 to 1.25 while held */
    turn = ((ulong)((long)(short)game_ctl_sum.v[2] * (long)game_frame_dt) >> 6);
    turn = (turn & 0xFFFF0000) | (ushort)((ushort)turn + (ushort)game_ctl_sum.v[8]);
    if ((ushort)turn == 0) {
        ctl_turn_on = 0;
    } else {
        if (!ctl_turn_on) {
            ctl_turn_start = game_time;
            ctl_turn_on = 1;
        }
        ramp = (game_time - ctl_turn_start) * 0x40 + 0x6000;
        if ((long)ramp > 0x14000)
            ramp = 0x14000;
        turn = fmul((short)turn, ramp);
    }
    game_entities[game_player_entity].heading += (ushort)turn;
    fix_sincos(game_entities[game_player_entity].heading, &sinv, &cosv);
    mx = fmul(game_ctl_sum.v[0], cosv);
    my = fmul(game_ctl_sum.v[0], sinv);
    mx += fmul(game_ctl_sum.v[1], -sinv);
    my += fmul(game_ctl_sum.v[1], cosv);

    switch (game_ctl_sum.flags & 0xA0) {
    case 0x20:
        if (!(game_ctl_sum.flags & 0x40)) {
            obj55_288EC0(PBA_B(game_player_slot) + 0xA2, 1);
            control_2D8568();
        }
        newflags |= 0x40;
        break;
    case 0x80:
        obj55_288EC0(PBA_B(game_player_slot) + 0xA2, 0);
        break;
    }
    old = *(ulong *)(PBA_B(game_player_slot) + 0xAE);
    *(ulong *)(PBA_B(game_player_slot) + 0xAE) = 0;
    PbaPlayerMove(PBA_B(game_player_slot) + 6, &mx, &my);
    EDMS_control_marble(game_entities[game_player_entity].body, mx, my, old);

    snd = -1;
    if (game_ctl_sum.v[5]) {
        snd = mis103_2C9180(3, game_ctl_sum.v[5] * (long)game_frame_dt, &changed);
    } else if (game_ctl_sum.flags & 0xC) {
        long d = 0;
        switch (game_ctl_sum.flags & 0xC) {
        case 4: d = -0x2000; break;
        case 8: d = 0x2000; break;
        }
        if (d) {
            snd = mis103_2C9180(2, d, &changed);
            newflags |= 2;
        }
    } else if (game_ctl_sum.flags & 2) {
        snd = mis103_2C9180(2, 0, &changed);
        if (changed)
            newflags |= 2;
    }
    if (game_ctl_sum.v[7])
        snd = mis103_2C9180(6, game_ctl_sum.v[7], &changed);
#ifndef TN_WATCOM_ABI
    if (snd == -1) {                    /* mouselook turns the view as Q / E do (90 degrees) */
        short before = hud_441355;

        if (ml_turn)
            mis103_2C9180(3, ml_turn, &changed);
        snd = 0;                        /* the turning loop, by the mouse's rule */
        changed = ml_whirr_gate(&ml_whirr[0], (short)(hud_441355 - before));
    } else
        ml_whirr[0].on = 0;             /* the keys turn: the game's rule */
    if (ml_align) {                     /* the player turns toward the view, the head back */
        short off = (short)hud_441355;
        long step = ML_ALIGN_RATE * (long)game_frame_dt;

        if (step > (off < 0 ? -off : off))
            step = off < 0 ? -off : off;
        if (off < 0)
            step = -step;
        if (step) {
            short before = hud_441355;
            uchar dummy;
            mis103_2C9180(3, -step, &dummy);
            game_entities[game_player_entity].heading += (ushort)(short)(before - hud_441355);
        }
        if ((short)hud_441355 == 0)
            ml_align = 0;
    }
#endif
    if (snd != -1)
        snd_loop_ctl(snd, changed);
    else
        snd_loop_ctl(0, 0);

    /* looking up / down accelerates the same way */
    snd = -1;
    changed = 0;
    look = (ulong)((long)(short)game_ctl_sum.v[4] * (long)game_frame_dt) >> 1;
    if ((ushort)look == 0) {
        ctl_look_on = 0;
    } else {
        if (!ctl_look_on) {
            ctl_look_start = game_time;
            ctl_look_on = 1;
        }
        ramp = 0x7000 + (game_time - ctl_look_start) * 0x60;
        if ((long)ramp > 0x14000)
            ramp = 0x14000;
        look = fmul((short)look, ramp);
    }
    if (game_ctl_sum.v[4]) {
        snd = mis103_2C9180(1, (ushort)look, &changed);
    } else if (game_ctl_sum.flags & 1) {
        snd = mis103_2C9180(0, 0, &changed);
        if (changed)
            newflags |= 1;
    }
    if (game_ctl_sum.v[6])
        snd = mis103_2C9180(5, game_ctl_sum.v[6], &changed);
#ifndef TN_WATCOM_ABI
    if (snd == -1) {                    /* mouselook: look up / down as R / V do */
        short before = hud_441357;

        if (ml_look)
            mis103_2C9180(1, ml_look, &changed);
        snd = 1;                        /* the looking loop, by the mouse's rule */
        changed = ml_whirr_gate(&ml_whirr[1], (short)(hud_441357 - before));
    } else
        ml_whirr[1].on = 0;
#endif
    if (snd != -1)
        snd_loop_ctl(snd, changed);
    else
        snd_loop_ctl(1, 0);
    game_ctl_sum.flags = newflags;
    memset(&game_ctl_sum, 0, 0x24);
}

/* The Ctrl slots go to the aircraft code (look value as a 16-bit rate). */
void control_2D7E38(void)
{
    int i, k;

    for (i = 0; i < 9; i++) {
        for (k = 0; k < 3; k++)
            game_ctl_sum.v[i] += game_ctl_ctrl[k].v[i];
        game_ctl_sum.v[i] = clamp1(game_ctl_sum.v[i]);
    }
    game_ctl_sum.flags |= game_ctl_ctrl[0].flags;
    game_ctl_sum.flags |= game_ctl_ctrl[1].flags;
    game_ctl_sum.flags |= game_ctl_ctrl[2].flags;
    game_ctl_sum.v[4] = fmul(game_ctl_sum.v[4], 10);
    if (game_ctl_sum.v[4])
        aircraft_284670(0, 0, 0, (ushort)((short)game_ctl_sum.v[4] * (short)game_frame_dt));
    else if (game_ctl_sum.flags & 1)
        aircraft_284670(0, 0, 0, 0x8000);
    memset(&game_ctl_sum, 0, 0x24);
}

/* Key bitmap bits: byte = scan code / 8, bit = scan code % 8.  Alt (0x38 / 0xB8) switches the
 * cursor keys from turning to strafing; Ctrl (0x1D / 0x9D) selects the Ctrl slot. */
void CtrlKeyboard(void)
{
    uchar *k = (uchar *)tn_replay_state_ptr;
    GameControlSlot *s;
    int alt;

    alt = (k[7] & 1) || (k[0x17] & 1);
    s = ((k[3] & 0x20) || (k[0x13] & 0x20)) ? &game_ctl_ctrl[0] : &game_ctl_slots[0];
    s->flags &= 0x20;
    if (((k[2] & 2) || (k[0x19] & 1)) && !alt)
        HIW(s->v[0])++;
    else if ((k[3] & 0x80) && !alt)
        s->v[0] += 0x8000;
    if (((k[5] & 0x20) || (k[0x1A] & 1)) && !alt)
        s->v[0] -= 0x10000;
    if (((k[5] & 0x10) && !alt) || ((k[0x19] & 8) && alt))
        s->v[1] -= 0x10000;
    if (((k[5] & 0x40) && !alt) || ((k[0x19] & 0x20) && alt))
        HIW(s->v[1])++;
    if (((k[3] & 0x40) && !alt) || ((k[0x19] & 8) && !alt))
        s->v[2] += (fix)0xFFFF8000;
    if (((k[4] & 1) && !alt) || ((k[0x19] & 0x20) && !alt))
        s->v[2] += 0x8000;
    if (((k[2] & 8) && !alt) || (k[9] & 1))
        s->v[4] -= 0x10000;
    if (((k[5] & 0x80) && !alt) || (k[0xA] & 1))
        HIW(s->v[4])++;
    if (((k[2] & 1) && !alt) || (k[9] & 8))
        FLAGS0(s) |= 4;
    if (((k[2] & 4) && !alt) || (k[9] & 0x20))
        FLAGS0(s) |= 8;
    if (((k[4] & 2) && !alt) || (k[9] & 0x10))
        FLAGS0(s) |= 3;
    tn_replay_state_ptr = k;
}

void control_2D8098(fix sens, long dead)
{
    joy_sens = sens;
    joy_dead = fmul(0x800000, dead) >> 16;
    joy_range = (0x80 - joy_dead) << 16;
}

/* Joystick axis (-128..127) to [-1, 1] with a dead zone and a sensitivity. */
fix joy_axis(char v)
{
    long a = v < 0 ? -v : v;
    fix r;

    if (a < joy_dead) {
        r = 0;
    } else {
        if (v > 0)
            r = (v - joy_dead) << 16;
        else
            r = -((-v - joy_dead) << 16);
        r = (fix)(((long long)r << 16) / joy_range);
        r = fmul(joy_sens, r);
    }
    return clamp1(r);
}

/* Joystick (or, with ctl_hmd_mode, the head tracker's axes and buttons) into slot 1 (2). */
void CtrlJoystick(void)
{
    char *axes;
    uchar btn;
    int k;
    GameControlSlot *s;
    fix r;

    if (ctl_hmd_mode) {
        axes = ctl_hmd_axes;
        k = 2;
        btn = ctl_hmd_buttons;
    } else {
        axes = tn_joy_axes;
        k = 1;
        btn = tn_joy_buttons;
    }
    s = (btn & 8) ? &game_ctl_ctrl[k] : &game_ctl_slots[k];
    s->flags &= 0x120;
    s->v[0] = -joy_axis(axes[1]);
    s->v[2] = joy_axis(axes[0]);
    s->v[1] = joy_axis(axes[2]);
    if (ctl_hmd_mode) {
        s->v[4] = joy_axis(axes[3]);
        s->v[5] = joy_axis(axes[4]);
    } else {
        r = joy_axis(axes[3]);
        if (r)
            s->v[4] = r;
    }
    switch (btn & 0x70) {
    case 0x10: s->v[4] -= 0x10000; break;
    case 0x20: HIW(s->v[5])++; break;
    case 0x30: HIW(s->v[4])++; break;
    case 0x40: s->v[5] -= 0x10000; break;
    }
    if (btn & 1) {
        if (!(FLAGS1(s) & 1)) {
            hud_268E98();
            FLAGS1(s) |= 1;
        }
    } else if (FLAGS1(s) & 1) {
        FLAGS1(s) &= ~1;
        weapons_290858();
    }
    if (btn & 2) {
        if (!(FLAGS0(s) & 0x20))
            FLAGS0(s) |= 0x20;
    } else if (FLAGS0(s) & 0x20) {
        FLAGS0(s) = (FLAGS0(s) & 0x5F) | 0x80;
    }
    if (btn & 4)
        FLAGS0(s) |= 3;
}

/* Head tracker: trackers with buttons (types 1, 2, 3, 4, 9) drive the joystick code; type 9
 * (buttons rotated, hat bits) gets its own dead zone.  Types 3, 5-8 (and others) turn and look
 * through slot 2: v[6] yaw, v[7] / v[8] pitch. */
void CtrlHmd(void)
{
    int t = hmd_type;
    uchar bl;
    GameControlSlot *s;
    uchar *k;

    if (t == 0)
        return;
    if (t == 1 || t == 4 || t == 2 || t == 3 || t == 9) {
        ushort b0;

        ctl_hmd_axes[0] = -(long)(HD16(0xC) >> 8);
        ctl_hmd_axes[1] = HS16(4) >> 8;
        ctl_hmd_axes[2] = HS16(2) >> 8;
        ctl_hmd_axes[3] = HD16(8) >> 8;
        ctl_hmd_axes[4] = 0;
        ctl_hmd_axes[5] = HD16(0xA) >> 8;
        b0 = HD16(0);
        bl = (uchar)b0;
        if (hmd_type == 9) {
            short d;

            bl = (uchar)((b0 << 1) | ((short)b0 < 0)) & 0xF;
            switch (HD16(0) & 0x1E0) {
            case 0x100: bl |= 0x30; break;
            case 0x80: bl |= 0x10; break;
            case 0x40: bl |= 0x20; break;
            case 0x20: bl |= 0x40; break;
            }
            d = (short)(ctl_hmd_axes[0] * 3);
            if (d < -0x78)
                d = -0x78;
            else if (d > 0x78)
                d = 0x78;
            ctl_hmd_axes[0] = (char)d;
            joy_sens = 0x20000;
            joy_dead = fmul(0x800000, 0x1A80) >> 16;
            joy_range = (0x80 - joy_dead) << 16;
            ctl_hmd_axes[3] = 0;
        }
        ctl_hmd_buttons = bl;
        if (HS16(6) > 0x3000)
            ctl_hmd_buttons = bl | 2;
        ctl_hmd_mode = 1;
        if (hmd_type == 3) {
            ctl_hmd_axes[5] = 0;
            ctl_hmd_axes[3] = 0;
            ctl_hmd_axes[0] = 0;
        }
        CtrlJoystick();
        ctl_hmd_mode = 0;
        if (hmd_type == 9) {
            joy_sens = 0x10000;
            joy_dead = fmul(0x800000, 0x3500) >> 16;
            joy_range = (0x80 - joy_dead) << 16;
            if (!(bl & 8))
                game_ctl_slots[2].v[6] = HD16(8) >> 2;
        }
        if (hmd_type != 3)
            return;
    }
    k = (uchar *)tn_replay_state_ptr;
    s = ((k[3] & 0x20) || (k[0x13] & 0x20)) ? &game_ctl_ctrl[2] : &game_ctl_slots[2];
    s->v[6] = -(long)HD16(8);
    if (HMD_B[0x10] & 1) {
        if (hmd_recenter) {
            s->v[8] = -(long)HS16(0xC);
            hmd_2FDB80();
            hmd_recenter = 0;
            s->v[7] = 1;
        } else {
            s->v[7] = -(long)HD16(0xC);
        }
    } else {
        s->v[8] = -(long)HS16(0xC) - ctl_hmd_prev;
        ctl_hmd_prev = -HD16(0xC);
        if (hmd_recenter) {
            hmd_2FDB80();
            hmd_recenter = 0;
        }
    }
}

/* Pushes the player's marble in the direction of the move keys (jump start). */
void control_2D8568(void)
{
    ushort a;
    fix sinv, cosv;

    if (!biped_27EDA0(game_player_entity))
        return;
    if (game_ctl_sum.v[0] == 0 && game_ctl_sum.v[1] == 0)
        return;
    a = 0;
    if (game_ctl_sum.v[0] > 0)
        a = 0;
    else if (game_ctl_sum.v[0] < 0)
        a = 0x8000;
    if (game_ctl_sum.v[1] > 0)
        a = 0x4000;
    else if (game_ctl_sum.v[1] < 0)
        a = 0xC000;
    a += game_entities[game_player_entity].heading;
    fix_sincos(a, &sinv, &cosv);
    EDMS_marble_add_I14(game_entities[game_player_entity].body, cosv * 60, sinv * 60, 0x280000);
}
