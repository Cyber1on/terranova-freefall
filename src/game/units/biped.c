/* biped.c -- Terra Nova biped (soldier) walker: legs, steps, body sway and arms (module M049;
 * source name unknown).  Decompiled from __FF.EXE v1.09.
 *
 * Every soldier PBA record holds a biped record at PBA + 0xDE (BP_* offsets below, all relative
 * to that record).  Once per frame BipedIdleTurn (through biped_2814E4, which cuts long frames
 * into steps of at most 0.1 s) follows the entity's position and heading: it moves the pelvis,
 * computes the speed and gait, finishes steps and starts new ones (biped_27F658 picks the leg).
 * When the soldier is drawn, BipedComputePose interpolates the feet of the moving legs, bends the
 * knees (inverse kinematics, biped_27F2A4), places the toes (biped_27FB28) and the arms.
 *
 * Joints (bp_joints, 16 x fix[3] at biped + 0xD1): 0/1 toes, 2/3 feet, 4/5 knees, 6/7 hips,
 * 8 pelvis, 9 head (top of the spine), 10/11 shoulders, 12/13 elbows, 14/15 hands.
 * Legs (0x44 bytes at biped + 0x235): +0 heading word, +2 cos, +6 sin, +0xA planted flag,
 * +0xB knee plane normal, +0x17 ground kind under the foot, +0x18 step record (STEP_*).
 *
 * The module keeps the biped being worked on and its frame values in statics (bp_*), set by
 * BipedIdleTurn, BipedComputePose and biped_27EE34; the helpers read them.
 */
#include "g3_b.h"
#include "g1_c.h"      /* game_rnd_text (owner g1c) */
#include "3d_a.h"
#include "g6_c.h"     /* added at the G5-G8 merge */
#include "g7_b.h"     /* added at the G5-G8 merge */
#include "g8_a.h"     /* added at the G5-G8 merge */

/* ------------------------------------------------------------------ module statics */
#ifndef TN_LINK_ORIGINAL_DATA
fix bp_cs[2];            /* 0x3924B0 cos, sin of the heading */
fix bp_mv[2];            /* 0x3924B8 unit direction of motion (0 when standing) */
fix bp_p[2];             /* 0x3924C0 x, y of the point the steps are placed around */
fix bp_fwd;              /* 0x3924C8 forward part of the motion direction */
fix bp_halfw;            /* 0x3924CC stance width (halved where the feet are placed) */
fix bp_lift;             /* 0x3924D0 foot lift of the next step */
long bp_now;             /* 0x3924D4 time of the update */
uchar *bp_cur;           /* 0x3924D8 the biped record */
fix bp_stride;           /* 0x3924DC */
fix bp_dt;               /* 0x3924E0 frame time */
fix *bp_joints;          /* 0x3924E4 bp_cur + 0xD1 */
fix bp_lead;             /* 0x3924E8 same value as bp_stride */
fix bp_v[3];             /* 0x3924EC x, y velocity, 0x3924F4 vertical velocity of the pelvis */
ushort bp_heading;       /* 0x3924F8 */
long game_441361;        /* 0x441361 footstep counter of the player */
uchar game_44659C;       /* 0x44659C last ground byte read by biped_27E250 */
#endif

#define BP_L(b, o) (*(long *)((uchar *)(b) + (o)))
#define BP_W(b, o) (*(short *)((uchar *)(b) + (o)))
#define BP_UW(b, o) (*(ushort *)((uchar *)(b) + (o)))
#define BP_B(b, o) (*((uchar *)(b) + (o)))
#define BP_V(b, o) ((fix *)((uchar *)(b) + (o)))
#define BP_ENT(b) BP_W(b, 0x21B)                 /* the entity of the biped */
#define BIPED_OF(e) ((uchar *)&game_pba[G3B_ENT(e).slot] + 0xDE)
#define J(i) (bp_joints + 3 * (i))
#define LEG(b, i) ((uchar *)(b) + 0x235 + 0x44 * (i))
#define STEP(b, i) (LEG(b, i) + 0x18)
/* step record (0x2C bytes) */
#define ST_START 0x00    /* long start time */
#define ST_DUR 0x04      /* long duration */
#define ST_LIFT 0x08     /* fix foot lift */
#define ST_SVEC 0x0C     /* fix[3] start position */
#define ST_EVEC 0x18     /* fix[3] end position */
#define ST_TARGET 0x24   /* fix * the joint moved */
#define ST_STATE 0x28    /* 0 waiting, 1 moving, 2 done */
#define ST_SREL 0x29     /* start position relative to the pelvis */
#define ST_EREL 0x2A     /* end position relative to the pelvis */
#define ST_KIND 0x2B
#define ST_TGT(s) (*(fix **)((s) + ST_TARGET))
#define V3(p) ((g3s_vector *)(p))
#define MUL6(x) ((long)((ulong)(x) * 6))

/* ------------------------------------------------------------------ small helpers */

/* ground kind under (x, y): 5 on a class 4 object, else the kind table entry of the map square
 * (or of physics_2D637C for the squares marked in the bit table) */
uchar biped_27E250(fix x, fix y, fix z, terrain_ff *tff)
{
    terrain_ff local;
    uchar k, b;

    if (tff == 0) {
        tff = &local;
        local.caller = BP_ENT(bp_cur);
        FF_terrain(MUL6(x), MUL6(y), MUL6(z), 0, &local);
    }
    if (G3B_ENT(tff->DATA1 & 0xFFF).cls == 4)
        return 5;
    if (x < 0 || y < 0)
        k = 0;
#ifndef TN_WATCOM_ABI
    /* P155: beyond the map's 512 squares there is no row (the game tests only the near edges
     * and reads past the row table) */
    else if (x >= 0x2000000 || y >= 0x2000000)
        k = 0;
#endif
    else {
        uchar *row = *(uchar **)(game_map_35BFB4 + 0x84E + (y >> 16) * 4);
        b = row[(x >> 16) * 3] & 0x3F;
        game_44659C = b;
        if (game_41DA28[b >> 3] & (1 << (b & 7)))
            k = game_41DD58[b];
        else
            k = physics_2D637C(x, y, b);
    }
    return game_41DB58[k * 12];
}

void biped_27E354(fix *out, fix *a, fix *b)                  /* no callers */
{
    out[0] = a[0] + b[0];
    out[1] = a[1] + b[1];
}

void biped_27E370(fix *out, fix *a, fix *b)
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
}

void biped_27E38C(fix *out, fix *a, fix s)                   /* no callers */
{
    out[0] = G3B_FIXMUL(a[0], s);
    out[1] = G3B_FIXMUL(a[1], s);
}

fix biped_27E3B0(fix *a, fix *b)                             /* 2D cross product */
{
    return G3B_FIXMUL(a[0], b[1]) - G3B_FIXMUL(a[1], b[0]);
}

fix biped_27E3D8(fix *a, fix *b)                             /* 2D dot product */
{
    return G3B_FIXMUL(a[0], b[0]) + G3B_FIXMUL(a[1], b[1]);
}

/* pelvis-relative position (forward, left, up) to world */
void biped_27E400(fix *out, fix *v)
{
    out[0] = J(8)[0] + G3B_FIXMUL(v[0], bp_cs[0]) + G3B_FIXMUL(v[1], bp_cs[1]);
    out[1] = J(8)[1] + G3B_FIXMUL(v[0], bp_cs[1]) + G3B_FIXMUL(v[1], -bp_cs[0]);
    out[2] = J(8)[2] + v[2];
}

/* plant the foot of a leg where it is */
void biped_27E470(long leg)
{
    uchar *st = STEP(bp_cur, leg);
    fix *foot = J(leg + 2);

    memcpy(st + ST_EVEC, foot, 12);
    BP_L(st, ST_DUR) = 0;
    BP_B(st, ST_EREL) = 0;
    BP_B(st, ST_STATE) = 2;
    ST_TGT(st) = foot;
    BP_L(st, ST_START) = bp_now;
    biped_27FB28(leg, (uchar)BP_L(bp_cur, 0x201));
    BP_B(LEG(bp_cur, leg), 0xA) = 0;
}

/* advance a step in time; 1 when the foot has just landed (footstep sounds) */
uchar biped_27E514(uchar *st, long now, long leg)
{
    long d;
    uchar *b;

    if (BP_B(st, ST_STATE) == 2 && BP_B(st, ST_EREL) != 1)
        return 0;
    d = now - BP_L(st, ST_START);
    if (d >= BP_L(st, ST_DUR)) {
        if (BP_B(st, ST_EREL) == 0)
            memcpy(ST_TGT(st), st + ST_EVEC, 12);
        else
            biped_27E400(ST_TGT(st), (fix *)(st + ST_EVEC));
        BP_B(st, ST_STATE) = 2;
        BP_L(st, ST_LIFT) = 0;
        if (BP_B(st, ST_EREL) == 1)
            return 0;
        biped_27FB28(leg, 1);
        if (BP_B(st, ST_KIND) != 4) {
            b = bp_cur;
            if (game_player_entity == BP_ENT(b)) {
                game_441361++;
                snd_play_step(leg, BP_B(LEG(b, leg), 0x17), BP_L(b, 0x2EA));
                b = bp_cur;
                if (BP_L(b, 0x2EA) >= BP_L(b, 0x207) && BP_B(LEG(b, leg), 0x17) == 3)
                    sfx_28DC50(ST_TGT(st), 0);
            } else if (BP_L(b, 0x2EA) >= BP_L(b, 0x207)) {
                fix *t = ST_TGT(st);
                if (biped_27E250(t[0], t[1], t[2], 0) == 3)
                    sfx_28DC50(ST_TGT(st), 1);
            }
        }
        return 1;
    }
    if (d < 0 || BP_B(st, ST_STATE) == 1)
        return 0;
    if (game_player_entity == BP_ENT(bp_cur) && BP_B(st, ST_KIND) == 5)
        snd_null_248FA8();                    /* called with (leg, speed); does nothing */
    BP_B(st, ST_STATE) = 1;
    return 0;
}

/* move the target joint of a step to its position at time now (foot raised on a parabola) */
void biped_27E678(uchar *st, long now)
{
    fix v[3], s[3], e[3], *sp, *ep, t;
    long d;

    if (BP_B(st, ST_STATE) == 2 && BP_B(st, ST_EREL) != 1)
        return;
    d = now - BP_L(st, ST_START);
    if (d >= BP_L(st, ST_DUR)) {
        if (BP_B(st, ST_EREL) == 0)
            memcpy(ST_TGT(st), st + ST_EVEC, 12);
        else
            biped_27E400(ST_TGT(st), (fix *)(st + ST_EVEC));
        return;
    }
    if (d < 0)
        return;
    t = G3B_FIXDIV(d, BP_L(st, ST_DUR));
    sp = (fix *)(st + ST_SVEC);
    if (BP_B(st, ST_SREL)) {
        biped_27E400(s, sp);
        sp = s;
    }
    ep = (fix *)(st + ST_EVEC);
    if (BP_B(st, ST_EREL)) {
        biped_27E400(e, ep);
        ep = e;
    }
    g3_vec_sub(V3(v), V3(ep), V3(sp));
    g3_vec_scale(V3(v), V3(v), t);
    if (BP_L(st, ST_LIFT) > 0)
        v[2] += 4 * G3B_FIXMUL(BP_L(st, ST_LIFT), 0x4000 - G3B_FIXMUL(t - 0x8000, t - 0x8000));
    g3_vec_add(V3(ST_TGT(st)), V3(sp), V3(v));
}

/* step length, lift and stance width for a speed */
void biped_27E778(fix speed)
{
    uchar *b = bp_cur;
    fix a, c, t, e;

    if (speed > BP_L(b, 0x20B)) {
        c = (long)((long long)BP_L(b, 0x20F) * speed / BP_L(b, 0x20B));
        t = G3B_FIXDIV(speed - BP_L(b, 0x20B), BP_L(b, 0x20B));
        if (t > 0x10000)
            t = 0x10000;
        a = G3B_FIXMUL(BP_L(b, 0x1E9), t + 0x4000);
    } else if (speed > BP_L(b, 0x207)) {
        a = BP_L(b, 0x1E9) / 4;
        c = BP_L(b, 0x20F);
    } else {
        a = BP_L(b, 0x1ED);
        c = 0;
    }
    bp_lift = a;
    b = bp_cur;
    if (BP_L(b, 0x2E6) < 1 || BP_B(b, 4) != 0)
        a = (BP_L(b, 0x1D9) + BP_L(b, 0x1D5)) / 2;
    else {
        e = G3B_FIXMUL(bp_cs[0], bp_mv[0]) + G3B_FIXMUL(bp_cs[1], bp_mv[1]);
        bp_fwd = e;
        if (e < 0)
            e = 0;
        c = G3B_FIXMUL(c, e + 0x10000) / 2;
        a = BP_L(bp_cur, 0x217);
    }
    bp_halfw = a;
    bp_lead = bp_stride = (long)((ulong)c * 8) / 16;
}

fix biped_27E8C0(fix *a, fix *b, fix *c)                     /* no callers */
{
    return -(G3B_FIXMUL(a[0] - b[0], c[1] - b[1]) - G3B_FIXMUL(a[1] - b[1], c[0] - b[0]));
}

/* undo a posture set by biped_27E8FC / biped_27E99C */
static void bp_restore(uchar *b)
{
    BP_B(b, 0x2BF) = 0;
    BP_B(b, 4) = 1;
    BP_W(b, 0x21F) = 0;
    BP_L(b, 0x213) = BP_L(b, 0x2C0);
    BP_W(b, 0x21D) = BP_W(b, 0x2C4);
}

/* lean (angle) and crouch (height factor) */
void biped_27E8FC(short ent, ushort ang, fix rate)
{
    uchar *b = BIPED_OF(ent);

    if (BP_B(b, 0x2BF))
        bp_restore(b);
    BP_B(b, 0x2BF) = 1;
    BP_B(b, 4) = 1;
    BP_W(b, 0x2C4) = BP_W(b, 0x21D);
    BP_W(b, 0x21F) = 0;
    BP_UW(b, 0x21D) = ang;
    BP_L(b, 0x2C0) = BP_L(b, 0x213);
    BP_L(b, 0x213) = rate;
}

void biped_27E99C(short ent, uchar low)
{
    uchar *b = BIPED_OF(ent);

    if (BP_B(b, 0x2BF))
        bp_restore(b);
    BP_W(b, 0x2C4) = BP_W(b, 0x21D);
    BP_L(b, 0x2C0) = BP_L(b, 0x213);
    if (low) {
        BP_B(b, 0x2BF) = 2;
        BP_W(b, 0x21D) = 0x1999;
        BP_L(b, 0x213) = 0x6666;
    } else {
        BP_B(b, 0x2BF) = 1;
        BP_W(b, 0x21D) = 0x33D;
        BP_L(b, 0x213) = 0x9999;
    }
    BP_B(b, 4) = 1;
    BP_W(b, 0x21F) = 0;
}

void biped_27EA60(short ent)
{
    bp_restore(BIPED_OF(ent));
}

uchar biped_27EAB8(short ent)                               /* no callers */
{
    if (G3B_ENT(ent).cls != 1)
        return 0;
    return BP_L(BIPED_OF(ent), 0) == 0xB;
}

/* screen position of the chest */
void biped_27EAF4(short ent, fix *sx, fix *sy)
{
    fix *j = (fix *)game_pba[G3B_ENT(ent).slot].joints;
    fix c[3], v[3];
    g3s_phandle p;

    c[0] = (3 * j[24] + j[27]) / 4;
    c[1] = (3 * j[25] + j[28]) / 4;
    c[2] = (3 * j[26] + j[29]) / 4;
    v[2] = c[0];
    v[0] = c[1];
    v[1] = -c[2];
    p = g3_transform_point(V3(v));
    *sx = p->sx;
    *sy = p->sy;
    g3_free_point(p);
}

void biped_27EBD4(short ent, fix *out)                       /* no callers: the chest */
{
    fix *j = (fix *)game_pba[G3B_ENT(ent).slot].joints;

    out[0] = (3 * j[24] + j[27]) / 4;
    out[1] = (3 * j[25] + j[28]) / 4;
    out[2] = (3 * j[26] + j[29]) / 4;
}

/* the top of the head: the spine extended by a factor of the type */
void biped_27EC90(fix *out, short ent)
{
    uchar *pba = (uchar *)&game_pba[G3B_ENT(ent).slot];
    fix v[3], s;

    g3_vec_sub(V3(v), V3(pba + 0x21B), V3(pba + 0x20F));
    switch ((ulong)BP_L(pba, 0xDE)) {
    case 0: s = 0x4D65; break;
    case 1: s = 0x5D17; break;
    case 2: s = 0x4771; break;
    case 10: s = 0x50D7; break;
    default: s = 0x5174; break;
    }
    g3_vec_scale(V3(v), V3(v), s);
    g3_vec_add(V3(out), V3(v), V3(pba + 0x21B));
}

/* height of the chest above the entity */
fix biped_27ED34(long ent)
{
    uchar *pba;

    if (G3B_ENT(ent).cls != 1)
        return 0;
    pba = (uchar *)&game_pba[G3B_ENT(ent).slot];
    return (3 * BP_L(pba, 0x217) + BP_L(pba, 0x223)) / 4 - G3B_ENT(ent).z;
}

/* standing still with both feet down */
bool biped_27EDA0(short entity)
{
    uchar *b = BIPED_OF(entity);

    if (BP_B(b, 4) == 2 || BP_B(b, 4) == 1 || BP_B(b, 0x275) == 1 || BP_B(b, 0x2B9) == 1)
        return 0;
    return 1;
}

uchar biped_27EDF4(short ent)
{
    uchar *b = BIPED_OF(ent);

    if (BP_L(b, 5) >= 10 && BP_B(b, 4) == 2)
        return 0;
    return 1;
}

/* reset the pose to standing at the entity (IDLE_RESET) */
void biped_27EE34(short ent)
{
    GameEntity *e = &G3B_ENT(ent);
    uchar *b = BIPED_OF(ent);
    fix s, c;

    bp_heading = e->heading;
    BP_UW(b, 0x227) = e->heading;
    bp_cs[0] = fix_cos(bp_heading);
    bp_cs[1] = fix_sin(bp_heading);
    bp_joints = (fix *)(b + 0xD1);
    BP_L(b, 0x2D2) = e->z - BP_L(b, 0x221);
    bp_cur = b;
    BP_L(b, 0x2C6) = G3B_FIXMUL(BP_L(b, 0x213), BP_L(b, 0x2F6));
    BP_L(b, 0x2CA) = e->z + (BP_L(b, 0x2C6) - BP_L(bp_cur, 0x221));
    J(8)[0] = e->x;
    J(8)[1] = e->y;
    J(8)[2] = BP_L(b, 0x2CA);
    BP_L(bp_cur, 0x2DA) = 0;
    BP_L(bp_cur, 0x2DE) = 0;
    BP_L(bp_cur, 0x2E2) = BP_L(b, 0x1CD);
    g3_vec_add(V3(J(9)), V3(J(8)), V3(bp_cur + 0x2DA));
    bp_p[0] = J(8)[0];
    bp_p[1] = J(8)[1];
    bp_halfw = BP_L(bp_cur, 0x217);
    s = G3B_FIXMUL(-bp_cs[1], BP_L(b, 0x1D9)) / 2;
    c = G3B_FIXMUL(bp_cs[0], BP_L(b, 0x1D9)) / 2;
    J(6)[0] = J(8)[0] - s;
    J(6)[1] = J(8)[1] - c;
    J(7)[0] = J(8)[0] + s;
    J(7)[1] = J(8)[1] + c;
    J(7)[2] = J(6)[2] = J(8)[2];
    biped_27FFDC();
    BP_L(bp_cur, 5) = 0;
}

ushort biped_27EFE4(uchar *b)                                /* no callers: skeleton resource */
{
    long t = BP_L(b, 0);

    if (t == 0xB)
        return 0x376;
    if (t == 6 || t == 7 || t == 9)
        return 0x375;
    return 0x374;
}

long biped_27F00C(short ent)                                 /* body size */
{
    long t = BP_L(BIPED_OF(ent), 0), a, s;

    if (t == 0xB)
        return 8;
    if (t >= 6 && t <= 8)
        return 0xB;
    a = RndRange((RndStream *)game_rnd_text, 0, 0x7FFF) % 16;
    s = a + RndRange((RndStream *)game_rnd_text, 0, 0x7FFF) % 16;
    return (RndRange((RndStream *)game_rnd_text, 0, 0x7FFF) % 16 + s) / 4 + 6;
}

uchar biped_27F0D0(long i)
{
    return game_35B680[i];
}

/* resize the bipeds of the speaker records */
void biped_27F0DC(void)
{
    long i;
    uchar *rec;
    long k;
    short e;

    for (i = 0, rec = (uchar *)game_391E50; i < game_391E4C; i++, rec += 0x32) {
        k = biped_27F0D0(rec[6]);
        e = G2C_IDX15(*(short *)&game_pba[*(short *)(rec + 4)]);
        biped_2817B0(BIPED_OF(e), k);
    }
}

/* create the biped of a soldier entity */
void biped_27F150(long ent_, long type)
{
    short ent = (short)ent_;
    uchar *b = BIPED_OF(ent), *en;
    long i, j, v;
    ushort id;

    BP_L(b, 0) = type;
    BP_W(b, 0x21B) = ent;
    BP_B(b, 4) = 0;
    if (BP_L(b, 0) == 0xB)
        id = 0x376;
    else if (BP_L(b, 0) == 6 || BP_L(b, 0) == 7 || BP_L(b, 0) == 9)
        id = 0x375;
    else
        id = 0x374;
    ResExtract(id, b + 0xD);
    BP_L(b, 0x15) = (long)(b + 0x21);
    for (i = 0; i < BP_B(b, 0xE); i++) {
        en = *(uchar **)(b + 0x15) + 16 * i;
        BP_L(en, 4) = (BP_L(b, 0) * 7 + 0x320 + BP_L(en, 4)) << 16;
    }
    biped_2817B0(b, biped_27F00C(ent));
    if (type == 9 && ent != game_player_entity) {
        /* random heads of type 9: entries 3, 4 and 9, 10 */
        for (j = 0; j != 4; j += 2) {
            v = RndRange((RndStream *)game_rnd_text, 0, 0x7FFF) % 13 >= 5 ? 9 : 10;
            en = *(uchar **)(b + 0x15) + 0x30 * j;
            BP_L(en, 0x34) = (v * 7 + 0x323) << 16;
            en = *(uchar **)(b + 0x15) + 0x30 * j;
            BP_L(en, 0x44) = (v * 7 + 0x320 + j + 4) << 16;
        }
    }
    if (BP_L(b, 0) == 0xB)
        biped_27E99C(ent, 0);
}

/* ------------------------------------------------------------------ legs */

/* knee of a leg from the foot and the hip (inverse kinematics); sign selects the bend */
void biped_27F2A4(long sign, long leg)
{
    uchar *b = bp_cur;
    fix *foot = J(leg + 2), *knee = J(leg + 4), *hip = J(leg + 6);
    fix *vec = BP_V(b, leg == 0 ? 0x1B5 : 0x19D);
    fix l[3], c[3], p[3], maxlen, d2 = 0, d = 0, t, h, s;

    if (BP_L(b, 5) > 6) {
        maxlen = G3B_FIXMUL(0xF333, BP_L(b, 0x2FE));
        g3_vec_sub(V3(l), V3(foot), V3(hip));
        d2 = g3_vec_dotprod(V3(l), V3(l));
        d = fix_sqrt(d2);
        if (d < 0x5F9) {
            foot[2] -= 0x5F9;
            d2 = G3B_FIXMUL(0x5F9, 0x5F9);
            d = 0x5F9;
            BP_B(LEG(bp_cur, leg), 0xA) = 0;
        } else if (d > maxlen) {
            g3_vec_scale(V3(l), V3(l), G3B_FIXDIV(maxlen, d));
            g3_vec_add(V3(foot), V3(hip), V3(l));
            BP_B(LEG(bp_cur, leg), 0xA) = 0;
            d2 = g3_vec_dotprod(V3(l), V3(l));
            d = fix_sqrt(d2);
        }
    }
    b = bp_cur;
    if (BP_L(b, 5) > 0xF) {
        p[0] = foot[0] + BP_L(LEG(b, leg), 2) / 16;
        p[1] = foot[1] + BP_L(LEG(b, leg), 6) / 16;
        p[2] = foot[2];
        g3_compute_normal(V3(vec), V3(hip), V3(foot), V3(p));
        t = (G3B_FIXDIV(BP_L(bp_cur, 0x2EE), d2) + 0x10000) >> 1;
        knee[0] = hip[0] + G3B_FIXMUL(l[0], t);
        knee[1] = hip[1] + G3B_FIXMUL(l[1], t);
        knee[2] = hip[2] + G3B_FIXMUL(l[2], t);
        g3_cross_product(V3(c), V3(l), V3(vec));
        t = (G3B_FIXDIV(BP_L(bp_cur, 0x2EE), d) + d) >> 1;
        h = fix_sqrt(BP_L(bp_cur, 0x2F2) - G3B_FIXMUL(t, t));
        s = -G3B_FIXDIV(h, d);
        if (sign < 0)
            s = -s;
        knee[0] += G3B_FIXMUL(c[0], s);
        knee[1] += G3B_FIXMUL(c[1], s);
        knee[2] += G3B_FIXMUL(c[2], s);
    } else {
        memcpy(vec, b + 0x191, 12);
        knee[0] = (foot[0] + hip[0]) / 2;
        knee[1] = (foot[1] + hip[1]) / 2;
        knee[2] = (foot[2] + hip[2]) / 2;
    }
}

void biped_27F620(fix *dest, uchar *st)                      /* no callers: step end position */
{
    if (BP_B(st, ST_EREL) == 0)
        memcpy(dest, st + ST_EVEC, 12);
    else
        biped_27E400(dest, (fix *)(st + ST_EVEC));
}

/* choose the leg to move and start its step */
void biped_27F658(void)
{
    uchar *b = bp_cur;
    fix d[2], v[2], a[2], c[2], m, t;
    short w[2];
    uchar back;
    long i;

    if (BP_B(b, 4) == 2 || (BP_B(b, 4) == 1 && BP_B(b, 0x2BF) == 0)) {
        biped_27FFDC();
        return;
    }
    if (BP_L(bp_cur, 0x2E6) >= 1) {
        /* walking / running: the trailing leg steps in the direction of motion */
        for (i = 0; i < 2; i++) {
            fix *e = (fix *)(STEP(bp_cur, i) + ST_EVEC);
            v[0] = e[0] - bp_p[0];
            v[1] = e[1] - bp_p[1];
            d[i] = biped_27E3D8(v, bp_mv);
        }
        back = d[0] < d[1];
        m = back ? d[0] : d[1];
        b = bp_cur;
        if (BP_B(STEP(b, 0), ST_KIND) == 4) {
            biped_280364(0, d[1], bp_now - (BP_L(STEP(b, 0), ST_START) + BP_L(STEP(b, 0), ST_DUR)), 5);
            return;
        }
        if (BP_B(STEP(b, 1), ST_KIND) == 4) {
            biped_280364(1, d[0], bp_now - (BP_L(STEP(b, 1), ST_START) + BP_L(STEP(b, 1), ST_DUR)), 5);
            return;
        }
        m = -m;
        if (m <= bp_stride)
            return;
        t = G3B_FIXDIV(m - bp_stride, BP_L(b, 0x2EA));
        if (t > bp_dt)
            t = bp_dt;
        b = bp_cur;
        if (BP_L(b, 0x20B) / 2 + BP_L(b, 0x20B) > BP_L(b, 0x2EA) || bp_fwd < 0xCCCC)
            biped_280364(1 - back, d[back], t, 3);
        else
            biped_280364(1 - back, d[back], t, 4);
        return;
    }
    /* standing / turning: keep the feet under the body */
    for (i = 0; i < 2; i++) {
        fix *e = (fix *)(STEP(bp_cur, i) + ST_EVEC);
        v[0] = e[0] - bp_p[0];
        v[1] = e[1] - bp_p[1];
        d[i] = biped_27E3D8(v, bp_cs);
        /* (the original also computes end - hip into an unused local here) */
    }
    back = d[0] < d[1];
    b = bp_cur;
    for (i = 0; i < 2; i++)
        if (BP_B(STEP(b, i), ST_KIND) == 1) {
            bp_lift = 0;
            biped_280254(i);
            return;
        }
    for (i = 0; i < 2; i++)
        if (BP_B(LEG(b, i), 0xA) == 0) {
            bp_lift = 0;
            biped_280254(i);
            return;
        }
    biped_27E370(a, J(1), BP_V(b, 0x284));
    biped_27E370(c, bp_p, BP_V(b, 0x284));
    if (-biped_27E3B0(a, c) < 0) {
        biped_280254(1);
        return;
    }
    b = bp_cur;
    biped_27E370(a, J(0), BP_V(b, 0x240));
    biped_27E370(c, bp_p, BP_V(b, 0x240));
    if (biped_27E3B0(a, c) < 0) {
        biped_280254(0);
        return;
    }
    for (i = 0; i < 2; i++) {
        w[i] = bp_heading - BP_W(LEG(bp_cur, i), 0);
        if (i)
            w[i] = -w[i];
        if (w[i] < -0x1000) {
            biped_280254(i);
            return;
        }
    }
    for (i = 0; i < 2; i++)
        if (w[i] > 0x2000) {
            biped_280254(i);
            return;
        }
    biped_27E370(a, J(0), J(1));
    biped_27E370(c, bp_p, J(1));
    if (-biped_27E3B0(a, c) < 0) {
        biped_280254(1 - back);
        return;
    }
    b = bp_cur;
    biped_27E370(a, BP_V(b, 0x240), BP_V(b, 0x284));
    biped_27E370(c, bp_p, BP_V(b, 0x284));
    if (biped_27E3B0(a, c) < 0)
        biped_280254(back);
}

/* toe of a leg and the knee plane; full = the toe follows the foot direction */
void biped_27FB28(long leg, uchar flag)
{
    uchar *b = bp_cur;
    fix *foot = J(leg + 2), *toe = J(leg), *knee = J(leg + 4);
    fix *vec = BP_V(LEG(b, leg), 0xB);
    fix c = BP_L(LEG(b, leg), 2), s = BP_L(LEG(b, leg), 6);
    fix S = BP_L(b, 0x1F1) + BP_L(b, 0x1F5);
    fix tmp[3], f, dz, n, q;
    uchar full = flag != 0 || BP_L(b, 5) < 0xF;

    if (full) {
        tmp[0] = G3B_FIXMUL(c, BP_L(b, 0x1F1));
        tmp[1] = G3B_FIXMUL(s, BP_L(bp_cur, 0x1F1));
        tmp[2] = -BP_L(bp_cur, 0x1ED);
        g3_vec_add(V3(toe), V3(foot), V3(tmp));
        vec[0] = toe[0] - G3B_FIXMUL(c, S);
        vec[1] = toe[1] - G3B_FIXMUL(s, S);
        vec[2] = toe[2];
        return;
    }
    f = (G3B_FIXMUL(c, foot[0] - knee[0]) + G3B_FIXMUL(s, foot[1] - knee[1])) / 3;
    dz = foot[2] - knee[2];
    n = fix_sqrt(G3B_FIXMUL(f, f) + G3B_FIXMUL(dz, dz));
    if (n == 0) {
        dz = (fix)0xFFFF0000;
        f = 0;
    } else {
        f = G3B_FIXDIV(f, n);
        dz = G3B_FIXDIV(dz, n);
    }
    b = bp_cur;
    tmp[0] = G3B_FIXMUL(BP_L(b, 0x1ED), f) - G3B_FIXMUL(BP_L(b, 0x1F1), dz);
    tmp[2] = G3B_FIXMUL(BP_L(b, 0x1ED), dz) + G3B_FIXMUL(BP_L(b, 0x1F1), f);
    tmp[1] = G3B_FIXMUL(tmp[0], s);
    tmp[0] = G3B_FIXMUL(tmp[0], c);
    g3_vec_add(V3(toe), V3(foot), V3(tmp));
    q = -G3B_FIXMUL(S, dz);
    tmp[2] = G3B_FIXMUL(S, f);
    tmp[1] = G3B_FIXMUL(q, s);
    tmp[0] = G3B_FIXMUL(q, c);
    g3_vec_sub(V3(vec), V3(toe), V3(tmp));
}

/* leg heading (body heading + turn + splay) and the foot offset from the hip line */
void biped_27FE10(fix *out, long leg, ushort turn)
{
    uchar *b = bp_cur, *L = LEG(b, leg);
    long a = leg == 0 ? (long)BP_UW(b, 0x205) : -(long)BP_UW(b, 0x205);

    BP_UW(L, 0) = (ushort)(bp_heading + turn + a);
    BP_L(L, 2) = fix_cos(BP_UW(L, 0));
    BP_L(L, 6) = fix_sin(BP_UW(L, 0));
    if (out) {
        out[0] = G3B_FIXMUL(BP_L(L, 2), BP_L(bp_cur, 0x1F5) - BP_L(bp_cur, 0x1F1));
        out[1] = G3B_FIXMUL(BP_L(L, 6), BP_L(bp_cur, 0x1F5) - BP_L(bp_cur, 0x1F1));
        out[2] = BP_L(bp_cur, 0x1ED);
    }
}

/* where a foot goes after time rate: heading turned by the turn rate, position moved by the
 * velocity, the foot to the side (left = flag 1) by half the stance width */
void biped_27FED8(short *ang, fix *cs, fix *xy, fix rate, long left)
{
    fix h = (left ? bp_halfw : -bp_halfw) / 2;
    ushort a;

    if (rate) {
        *ang = (short)G3B_FIXMUL(BP_L(bp_cur, 0x229), rate);
        a = bp_heading + *ang;
        cs[0] = fix_cos(a);
        cs[1] = fix_sin(a);
        xy[0] = bp_p[0] + G3B_FIXMUL(-cs[1], h);
        xy[1] = bp_p[1] + G3B_FIXMUL(cs[0], h);
        xy[0] += G3B_FIXMUL(bp_v[0], rate);
        xy[1] += G3B_FIXMUL(bp_v[1], rate);
    } else {
        *ang = 0;
        cs[0] = bp_cs[0];
        cs[1] = bp_cs[1];
        xy[0] = bp_p[0] + G3B_FIXMUL(-cs[1], h);
        xy[1] = bp_p[1] + G3B_FIXMUL(cs[0], h);
    }
}

/* put both feet down under the body now (step kind 6); returns 6 */
long biped_27FFDC(void)
{
    fix xy[3], off[3], cs[2];
    short ang;
    long leg;

    for (leg = 0; leg < 2; leg++) {
        biped_27FED8(&ang, cs, xy, 0, leg);
        xy[2] = J(8)[2] - BP_L(bp_cur, 0x2C6);
        biped_27FE10(off, leg, (ushort)ang);
        if (xy[2] < BP_L(bp_cur, 0x2D2))
            xy[2] = BP_L(bp_cur, 0x2D2);
        g3_vec_add(V3(J(leg + 2)), V3(xy), V3(off));
        biped_27E470(leg);
        BP_B(STEP(bp_cur, leg), ST_KIND) = 6;
    }
    if (game_player_entity == BP_ENT(bp_cur))
        game_441361 = 0;
    return 6;
}

/* ground height for a foot position; on a big height difference the step is halved (up to 6
 * times) with its duration and lift */
void biped_2800BC(fix *pos, fix *dur, long leg)
{
    terrain_ff tff;
    fix dx = pos[0] - bp_p[0], dy = pos[1] - bp_p[1], h = 0;
    long i, a;
    uchar *b = bp_cur;

    if (BP_L(b, 5) > 4 || game_player_entity == BP_ENT(b)) {
        for (i = 0; i < 6; i++) {
            tff.caller = BP_ENT(bp_cur);
            FF_terrain(MUL6(pos[0]), MUL6(pos[1]), MUL6(J(8)[2]), 0, &tff);
            h = tff.g_height / 6;
            a = h - BP_L(bp_cur, 0x2D2);
            if (a < 0)
                a = -a;
            if (a <= BP_L(bp_cur, 0x2FE))
                break;
            dx /= 2;
            dy /= 2;
            *dur /= 2;
            bp_lift /= 2;
            pos[0] = bp_p[0] + dx;
            pos[1] = bp_p[1] + dy;
        }
        if (i == 6) {
            b = bp_cur;
            if (h > BP_L(b, 0x2D2))
                h = BP_L(b, 0x2D2) + BP_L(b, 0x2FE);
            else
                h = BP_L(b, 0x2D2);
        }
        pos[2] = h;
    } else
        pos[2] = BP_L(b, 0x2D2);
    if (game_player_entity == BP_ENT(bp_cur))
        BP_B(LEG(bp_cur, leg), 0x17) = biped_27E250(pos[0], pos[1], pos[2], &tff);
}

/* start a step of a standing leg (kind 2) */
uchar biped_280254(long leg)
{
    uchar *st = STEP(bp_cur, leg);
    fix *foot = J(leg + 2);
    fix t, cs[2], xy[3], off[3];
    short ang;

    t = G3B_FIXDIV(BP_L(bp_cur, 0x20F), BP_L(bp_cur, 0x207)) / 8;
    biped_27FED8(&ang, cs, xy, t, leg);
    biped_27FE10(off, leg, (ushort)ang);
    biped_2800BC(xy, &t, leg);
    BP_L(st, ST_START) = bp_now;
    BP_B(st, ST_SREL) = 0;
    BP_L(st, ST_DUR) = t;
    if (BP_B(st, ST_EREL) == 0)
        memcpy(st + ST_SVEC, st + ST_EVEC, 12);
    else
        biped_27E400((fix *)(st + ST_SVEC), (fix *)(st + ST_EVEC));
    g3_vec_add(V3(st + ST_EVEC), V3(xy), V3(off));
    BP_B(st, ST_STATE) = 0;
    BP_B(st, ST_KIND) = 2;
    ST_TGT(st) = foot;
    BP_L(st, ST_LIFT) = bp_lift;
    return BP_B(st, ST_KIND);
}

/* start a walking step (kind 3 walk, 4 run, 5 end of a run step) that began t ago; d is the
 * position of the other foot along the motion */
uchar biped_280364(long leg, fix d, fix t, long kind)
{
    uchar *b = bp_cur, *st = STEP(b, leg);
    fix *foot = J(leg + 2);
    fix dur = 0, ox = 0, oy = 0, cs[2], xy[3], off[3], p[3];
    short ang;

    /* (dur, ox and oy are uninitialized in the original for other kinds; there are none) */
    if (kind == 4) {
        dur = G3B_FIXDIV(BP_L(b, 0x1E9) / 3 + d, BP_L(b, 0x2EA));
        ox = oy = 0;
    } else if (kind == 5 || kind == 3) {
        dur = G3B_FIXDIV(bp_stride + d, BP_L(bp_cur, 0x2EA));
        ox = G3B_FIXMUL(bp_mv[0], bp_lead);
        oy = G3B_FIXMUL(bp_mv[1], bp_lead);
    }
    if (kind == 5)
        bp_lift = 0;
    biped_27FED8(&ang, cs, xy, dur, leg);
    biped_27FE10(off, leg, (ushort)ang);
    p[0] = xy[0] + ox;
    p[1] = xy[1] + oy;
    biped_2800BC(p, &dur, leg);
    if (kind == 4) {
        bp_lift /= 2;
        p[2] += BP_L(bp_cur, 0x1E9);
    }
    BP_L(st, ST_START) = bp_now - t;
    BP_B(st, ST_SREL) = 0;
    BP_L(st, ST_DUR) = dur + t;
    if (BP_B(st, ST_EREL) == 0)
        memcpy(st + ST_SVEC, st + ST_EVEC, 12);
    else
        biped_27E400((fix *)(st + ST_SVEC), (fix *)(st + ST_EVEC));
    g3_vec_add(V3(st + ST_EVEC), V3(p), V3(off));
    BP_B(st, ST_STATE) = 0;
    ST_TGT(st) = foot;
    BP_L(st, ST_LIFT) = bp_lift;
    BP_B(st, ST_KIND) = (uchar)kind;
    return BP_B(st, ST_KIND);
}

/* ------------------------------------------------------------------ arms */

/* type 0xB (no arms): only the arm plane normals */
void biped_280544(void)
{
    uchar *b = bp_cur;
    fix *v;
    long i;

    for (i = 0; i < 2; i++) {
        v = BP_V(b, i == 0 ? 0x1C1 : 0x1A9);
        v[0] = BP_L(LEG(b, i), 6);
        v[2] = 0;
        v[1] = -BP_L(LEG(b, i), 2);
    }
}

/* shoulders, elbows and hands; the arms swing against the opposite foot */
void biped_28059C(void)
{
    uchar inhelm = helm_263410(J(8)[0], J(8)[1]);
    fix o[3], up[3], tmp[3], *sh, *el, *hd, *v, *f, dxy, s1;
    ushort a, a2;
    long i, r;

    o[2] = 0;
    o[0] = G3B_FIXMUL(-bp_cs[1], BP_L(bp_cur, 0x1D5)) / 2;
    o[1] = G3B_FIXMUL(bp_cs[0], BP_L(bp_cur, 0x1D5)) / 2;
    g3_vec_scale(V3(up), V3(bp_cur + 0x2DA), BP_L(bp_cur, 0x1D1));
    for (i = 0; i < 2; i++) {
        sh = J(10 + i);
        el = J(12 + i);
        hd = J(14 + i);
        v = BP_V(bp_cur, i == 0 ? 0x1C1 : 0x1A9);
        if (i == 0)
            g3_vec_sub(V3(sh), V3(J(8)), V3(o));
        else
            g3_vec_add(V3(sh), V3(J(8)), V3(o));
        v[0] = bp_cs[1];
        v[1] = -bp_cs[0];
        v[2] = 0;
        g3_vec_add(V3(sh), V3(sh), V3(up));
        if (BP_L(bp_cur, 5) > 0xF) {
            f = J(i == 0 ? 3 : 2);
            dxy = G3B_FIXMUL(f[0] - J(8)[0], bp_cs[0]) + G3B_FIXMUL(f[1] - J(8)[1], bp_cs[1]);
            a = fix_atan2(dxy * 2, BP_L(bp_cur, 0x2FE)) - 0x1000;
            tmp[2] = G3B_FIXMUL(-BP_L(bp_cur, 0x1DD), fix_cos(a));
            s1 = G3B_FIXMUL(BP_L(bp_cur, 0x1DD), fix_sin(a));
            tmp[0] = G3B_FIXMUL(s1, bp_cs[0]);
            tmp[1] = G3B_FIXMUL(s1, bp_cs[1]);
            g3_vec_add(V3(el), V3(sh), V3(tmp));
            if (BP_B(bp_cur, 0x2BF) == 2)
                r = 0;
            else if (!inhelm && game_player_entity != BP_ENT(bp_cur) && BP_B(bp_cur, 0x2BF) != 1) {
                r = BP_L(bp_cur, 0x2EA) / 4 + 0x2AAA;
                if (r > 0x5555)
                    r = 0x5555;
            } else
                r = 0x5555;
            a2 = (ushort)(a + r);
            tmp[2] = G3B_FIXMUL(-BP_L(bp_cur, 0x1E1), fix_cos(a2));
            s1 = G3B_FIXMUL(BP_L(bp_cur, 0x1E1), fix_sin(a2));
            tmp[0] = G3B_FIXMUL(s1, bp_cs[0]);
            tmp[1] = G3B_FIXMUL(s1, bp_cs[1]);
            g3_vec_add(V3(hd), V3(tmp), V3(el));
        } else {
            memcpy(el, sh, 12);
            el[2] -= BP_L(bp_cur, 0x1DD);
            tmp[0] = G3B_FIXMUL(bp_cs[0], BP_L(bp_cur, 0x1E1));
            tmp[1] = G3B_FIXMUL(bp_cs[1], BP_L(bp_cur, 0x1E1));
            tmp[2] = 0;
            g3_vec_add(V3(hd), V3(tmp), V3(el));
        }
    }
}

/* ------------------------------------------------------------------ per frame */

/* pose for drawing at time (detail): moving feet, knees, toes, arms */
void BipedComputePose(void *biped, long time)
{
    uchar *b = biped;
    long leg;

    if (BP_B(b, 4) == 3 || time <= BP_L(b, 5))
        return;
    bp_joints = (fix *)(b + 0xD1);
    bp_heading = BP_UW(b, 0x227);
    bp_cur = b;
    BP_L(b, 5) = time;
    bp_cs[0] = fix_cos(bp_heading);
    bp_cs[1] = fix_sin(bp_heading);
    BP_L(b, 0x191) = bp_cs[1];
    BP_L(b, 0x199) = 0;
    BP_L(b, 0x195) = -bp_cs[0];
    for (leg = 0; leg < 2; leg++) {
        b = bp_cur;
        if (BP_B(STEP(b, leg), ST_STATE) == 1 || BP_B(STEP(b, leg), ST_EREL) == 1) {
            biped_27E678(STEP(bp_cur, leg), bp_now);
            biped_27F2A4(BP_L(bp_cur, 0x1FD), leg);
            if (BP_B(STEP(bp_cur, leg), ST_EREL) == 1)
                biped_27FE10(0, leg, 0);
            biped_27FB28(leg, BP_B(bp_cur, 0x201));
        } else {
            biped_27F2A4(BP_L(b, 0x1FD), leg);
            if (BP_B(LEG(bp_cur, leg), 0xA) == 0)
                biped_27FB28(leg, BP_B(bp_cur, 0x201));
        }
    }
    if (BP_L(bp_cur, 0) == 0xB)
        biped_280544();
    else
        biped_28059C();
}

/* follow the entity for dt (the community's IDLE_* / POSE_DAMP logic): heading (idle turns of
 * AI soldiers), pelvis height (springy when falling), speed and gait, steps */
void BipedIdleTurn(short ent, fix dt, long now)
{
    uchar *b = BIPED_OF(ent);
    GameEntity *e;
    terrain_ff tff;
    fix g, top, q, w, s, c, dz, v, dx, dy;
    long oldgait, i;
    short d;
    uchar flag;

    bp_now = now;
    bp_dt = dt;
    if (ent != game_player_entity && BP_L(b, 5) < 4) {
        /* not drawn in detail since the last update: just reset */
        biped_27EE34(ent);
        return;
    }
    bp_joints = (fix *)(b + 0xD1);
    e = &G3B_ENT(ent);
    bp_heading = e->heading;
    bp_cur = b;
    bp_v[0] = G3B_FIXDIV(e->x - J(8)[0], dt);
    bp_v[1] = G3B_FIXDIV(e->y - J(8)[1], dt);
    BP_L(bp_cur, 0x2EA) = fix_sqrt(G3B_FIXMUL(bp_v[0], bp_v[0]) + G3B_FIXMUL(bp_v[1], bp_v[1]));
    if (G3B_ENT(BP_ENT(bp_cur)).type == 0xD && mfd_270E70() == 0)
        biped_27E8FC(BP_ENT(bp_cur), 0x800, 0xCCCC);
    if (G3B_ENT(BP_ENT(bp_cur)).type != 0xD && game_player_entity != BP_ENT(bp_cur)
        && BP_L(bp_cur, 0) != 0xB
        && (BP_B(bp_cur, 0x2BF) != 0 || BP_L(bp_cur, 0x2EA) < 0x64)) {
        /* standing AI soldier: now and then pick a new look direction, turn slowly towards it */
        if (RndRange((RndStream *)game_rnd_text, 0, 0x7FFF) % 32 < 1)
            BP_W(bp_cur, 0x21F) = (short)(((RndRange((RndStream *)game_rnd_text, 0, 0x7FFF) & 0xFFFF0FFF)
                                           - 0x800) * 2);
        d = (short)(bp_heading + BP_W(bp_cur, 0x21F) - BP_W(bp_cur, 0x227));
        i = d;
        if (i > 0x222)
            i = 0x222;
        else if (i < -0x222)
            i = -0x222;
        BP_L(b, 0x229) = 0;
        bp_heading = (ushort)(BP_UW(b, 0x227) + i);
    } else
        BP_L(b, 0x229) = G3B_FIXDIV((short)(bp_heading - BP_W(b, 0x227)), dt);
    BP_UW(b, 0x227) = bp_heading;
    bp_cs[0] = fix_cos(bp_heading);
    bp_cs[1] = fix_sin(bp_heading);

    /* pelvis height */
    tff.caller = ent;
    FF_terrain(MUL6(e->x), MUL6(e->y), MUL6(e->z), 0, &tff);
    g = tff.g_height / 6;
    BP_L(b, 0x2D2) = g;
    top = e->z - g - 0xE60;
    if (top < BP_L(bp_cur, 0x221))
        top = BP_L(bp_cur, 0x221);
    BP_L(b, 0x2C6) = G3B_FIXMUL(BP_L(b, 0x213), BP_L(b, 0x2F6));
    top = top + g + (BP_L(b, 0x2C6) - BP_L(bp_cur, 0x221));
    if (BP_B(b, 4) == 1) {
        /* falling / landing: damped spring */
        bp_v[2] = BP_L(b, 0x2CE);
        BP_L(b, 0x2CA) += G3B_FIXMUL(dt, BP_L(b, 0x2CE));
        if (BP_L(b, 0x2CA) - g < BP_L(b, 0x2C6) / 2)
            BP_L(b, 0x2CA) = g + BP_L(b, 0x2C6) / 2;
        else if (BP_L(b, 0x2CA) - top > 0x444)
            BP_L(b, 0x2CA) = top + 0x444;
        BP_L(b, 0x2CE) += G3B_FIXMUL(dt, top - BP_L(b, 0x2CA)) << 7;
        BP_L(b, 0x2CE) -= BP_L(b, 0x2CE) >> 3;
    } else {
        v = G3B_FIXDIV(top - BP_L(b, 0x2CA), dt);
        BP_L(b, 0x2CA) = top;
        bp_v[2] = v;
        BP_L(b, 0x2CE) = v;
    }
    if (BP_L(b, 0x2CA) - g > BP_L(bp_cur, 0x2F6))
        BP_B(bp_cur, 4) = 2;                  /* in the air */
    else if (BP_B(bp_cur, 4) == 2) {
        if (ent == game_player_entity && BP_L(bp_cur, 0x2D6) < 0)
            snd_play_step(2, biped_27E250(e->x, e->y, e->z, 0), -BP_L(bp_cur, 0x2D6));
        if (BP_L(bp_cur, 0x2D6) < -0x20000)
            BP_B(bp_cur, 4) = 1;              /* hard landing */
        else
            BP_B(bp_cur, 4) = 0;
    } else if (BP_B(bp_cur, 4) == 1) {
        if (bp_v[2] >= 0) {
            v = BP_L(b, 0x2CA) - top;
            if (v < 0)
                v = -v;
            if (v < 0xCCC)
                BP_B(bp_cur, 4) = 0;
        }
    }

    /* pelvis and hips */
    J(8)[0] = e->x;
    J(8)[1] = e->y;
    s = G3B_FIXMUL(-bp_cs[1], BP_L(b, 0x1D9)) / 2;
    c = G3B_FIXMUL(bp_cs[0], BP_L(b, 0x1D9)) / 2;
    J(6)[0] = J(8)[0] - s;
    J(6)[1] = J(8)[1] - c;
    J(7)[0] = J(8)[0] + s;
    J(7)[1] = J(8)[1] + c;
    if (BP_B(bp_cur, 4) == 0 && BP_L(bp_cur, 0x2E6) <= 1) {
        /* lower the pelvis so the planted leg reaches the ground */
        i = BP_B(b, 0x2B9) != 1;
        dx = J(i + 6)[0] - J(i + 2)[0];
        dy = J(i + 6)[1] - J(i + 2)[1];
        v = fix_sqrt(BP_L(b, 0x2FA) - (G3B_FIXMUL(dx, dx) + G3B_FIXMUL(dy, dy)));
        dz = BP_L(b, 0x2F6) - v;
        if (dz > BP_L(b, 0x1E9))
            dz = BP_L(b, 0x1E9);
        else if (dz < 0)
            dz = 0;
        BP_L(b, 0x2CA) -= dz / 4;
    }
    J(8)[2] = J(7)[2] = J(6)[2] = BP_L(b, 0x2CA);

    /* speed and gait */
    if (BP_L(bp_cur, 0x2EA) > 0) {
        bp_mv[0] = G3B_FIXDIV(bp_v[0], BP_L(bp_cur, 0x2EA));
        bp_mv[1] = G3B_FIXDIV(bp_v[1], BP_L(bp_cur, 0x2EA));
    } else
        bp_mv[0] = bp_mv[1] = 0;
    oldgait = BP_L(bp_cur, 0x2E6);
    if (BP_L(bp_cur, 0x2EA) > BP_L(bp_cur, 0x20B))
        BP_L(bp_cur, 0x2E6) = 2;
    else if (BP_L(bp_cur, 0x2EA) > BP_L(bp_cur, 0x207))
        BP_L(bp_cur, 0x2E6) = 1;
    else
        BP_L(bp_cur, 0x2E6) = 0;
    if (BP_L(bp_cur, 0x2E6) != 0 && BP_B(bp_cur, 0x2BF) != 0)
        bp_restore(BIPED_OF(BP_ENT(bp_cur)));         /* moving: stand up */

    /* body lean (with speed) and the spine */
    q = G3B_FIXMUL(bp_cs[0], bp_mv[0]) + G3B_FIXMUL(bp_cs[1], bp_mv[1]);
    w = BP_L(bp_cur, 0x2EA) / 50;
    i = G3B_FIXMUL(q, w) + BP_W(bp_cur, 0x21D) - BP_W(bp_cur, 0x225);
    if (i > 0x333)
        i = 0x333;
    else if (i < -0x333)
        i = -0x333;
    BP_UW(bp_cur, 0x225) += (ushort)i;
    if (BP_UW(bp_cur, 0x225) != 0) {
        s = G3B_FIXMUL(fix_sin(BP_UW(bp_cur, 0x225)), BP_L(bp_cur, 0x1CD));
        BP_L(bp_cur, 0x2DA) = G3B_FIXMUL(s, bp_cs[0]);
        BP_L(bp_cur, 0x2DE) = G3B_FIXMUL(s, bp_cs[1]);
        BP_L(bp_cur, 0x2E2) = G3B_FIXMUL(fix_cos(BP_UW(bp_cur, 0x225)), BP_L(bp_cur, 0x1CD));
    } else {
        BP_L(bp_cur, 0x2DA) = 0;
        BP_L(bp_cur, 0x2DE) = 0;
        BP_L(bp_cur, 0x2E2) = BP_L(b, 0x1CD);
    }
    BP_L(bp_cur, 0x2D6) = BP_L(b, 0x2CE);
    g3_vec_add(V3(J(9)), V3(J(8)), V3(bp_cur + 0x2DA));

    /* steps */
    if (biped_27E514(b + 0x24D, bp_now, 0))
        BP_B(b, 0x23F) = 1;
    if (biped_27E514(b + 0x291, bp_now, 1))
        BP_B(b, 0x283) = 1;
    for (i = 0; i < 2; i++) {
        uchar *st = STEP(b, i);
        if (BP_B(st, ST_STATE) != 1)
            continue;
        flag = 0;
        if (oldgait - BP_L(bp_cur, 0x2E6) > 0)
            flag = 1;                         /* slowed down: land the moving foot now */
        else if (BP_L(bp_cur, 0x2E6) > 0) {
            fix vv[2];
            vv[0] = ((fix *)(st + ST_EVEC))[0] - J(8)[0];
            vv[1] = ((fix *)(st + ST_EVEC))[1] - J(8)[1];
            if (BP_B(st, ST_KIND) != 4 && biped_27E3D8(vv, bp_mv) < 0)
                flag = 1;                     /* the step goes backwards */
        }
        if (flag) {
            biped_27E678(st, bp_now);
            biped_27E470(i);
            BP_B(st, ST_KIND) = 1;
        }
    }
    b = bp_cur;
    if (BP_B(b, 4) == 2 || BP_B(b, 4) == 1 || (BP_B(b, 0x275) != 1 && BP_B(b, 0x2B9) != 1)) {
        bp_p[0] = G3B_FIXMUL(BP_L(bp_cur, 0x2DA), BP_L(bp_cur, 0x1F9)) + J(8)[0];
        bp_p[1] = G3B_FIXMUL(BP_L(bp_cur, 0x2DE), BP_L(bp_cur, 0x1F9)) + J(8)[1];
        biped_27E778(BP_L(bp_cur, 0x2EA));
        biped_27F658();
    }
    BP_L(bp_cur, 5) = 0;
}

/* out = a + (b - a) * t */
void biped_281488(fix *out, fix *a, fix *b, fix t)
{
    out[0] = a[0] + G3B_FIXMUL(t, b[0] - a[0]);
    out[1] = a[1] + G3B_FIXMUL(t, b[1] - a[1]);
    out[2] = a[2] + G3B_FIXMUL(t, b[2] - a[2]);
}

/* BipedIdleTurn in steps of at most 0.1 s (when drawn in detail), with the entity position and
 * heading interpolated from the last update */
void biped_2814E4(short ent, fix dt, long now)
{
    uchar *b = BIPED_OF(ent);
    GameEntity *e = &G3B_ENT(ent);
    fix z = e->z, nw[3], old[3], step, t, acc;
    ushort heading = e->heading, oldh;
    short dh;
    long n, i, keep, t0;

    if (dt > 0x1999 && BP_L(b, 5) > 2) {
        n = (short)((G3B_FIXDIV(dt, 0x1999) + 0xFFFF) >> 16);
        step = dt / n;
        old[0] = BP_L(b, 0x131);
        old[1] = BP_L(b, 0x135);
        old[2] = BP_L(b, 0x310);
        oldh = BP_UW(b, 0x30E);
        nw[0] = e->x;
        nw[1] = e->y;
        nw[2] = e->z;
        keep = BP_L(b, 5);
        dh = (short)(heading - oldh);
        t0 = now - dt;
        acc = step;
        for (i = 0; i < n - 1; i++) {
            t = G3B_FIXDIV(acc, dt);
            biped_281488(&G3B_ENT(ent).x, old, nw, t);
            G3B_ENT(ent).heading = (ushort)(G3B_FIXMUL(t, dh) + oldh);
            BipedIdleTurn(ent, step, t0 + acc);
            acc += step;
            BP_L(b, 5) = keep;
        }
        memcpy(&G3B_ENT(ent).x, nw, 12);
        G3B_ENT(ent).heading = heading;
        BipedIdleTurn(ent, step, now);
    } else
        BipedIdleTurn(ent, dt, now);
    BP_UW(b, 0x30E) = heading;
    BP_L(b, 0x310) = z;
}

/* ------------------------------------------------------------------ skeleton */

/* skeleton = biped + 0xD (header extracted from resource 0x374..0x376): +1 segment count,
 * +8 pointer to the 16-byte segment entries (Ref at +4) */
static uchar *bp_seg(void *skel, long seg)
{
    return RefGet(*(Ref *)(*(uchar **)((uchar *)skel + 8) + 16 * seg + 4));
}

fix biped_281728(void *skel, long seg, fix scale)            /* no callers */
{
    uchar *r = bp_seg(skel, seg);
    return G3B_FIXMUL(scale, BP_L(r, 0xC) - BP_L(r, 0x18));
}

fix biped_281750(void *skel, long seg, fix scale)
{
    return G3B_FIXMUL(scale, BP_L(bp_seg(skel, seg), 8));
}

long biped_28176C(void *skel, long seg)                      /* no callers */
{
    return BP_L(bp_seg(skel, seg), 4);
}

/* body dimensions of a biped from its skeleton, type and size k */
void biped_2817B0(uchar *b, long k)
{
    uchar *skel = b + 0xD;
    long t = BP_L(b, 0), dv = 0, q;
    fix len0, s1, s2, *r;

    if (t == 4)
        k += 10;
    else if (t == 3)
        k += 2;
    else if (t == 1)
        k -= 2;
    else if (t == 2 || t == 5)
        k += 5;
    if (BP_L(b, 0) != 0xB) {
        dv = BP_L(bp_seg(skel, 0), 4);
        q = (long)((ulong)(k - 8) << 16) / 64;
        s1 = q + 0x10000;
        s2 = (q + 0x20000) / 2;
        len0 = G3B_FIXMUL(0x10000, BP_L(bp_seg(skel, 0), 8));
        BP_L(b, 0x1E9) = G3B_FIXMUL(s1, BP_L(bp_seg(skel, 1), 8));
        BP_L(b, 0x1E5) = G3B_FIXMUL(s1, BP_L(bp_seg(skel, 2), 8));
        BP_L(b, 0x1DD) = G3B_FIXMUL(s2, BP_L(bp_seg(skel, 3), 8));
        BP_L(b, 0x1E1) = G3B_FIXMUL(s2, BP_L(bp_seg(skel, 4), 8));
        q = G3B_FIXMUL(s2, BP_L(bp_seg(skel, 5), 8));
        BP_L(b, 0x1FD) = 1;
        BP_L(b, 0x201) = 0;
    } else {
        r = (fix *)bp_seg(skel, 1);
        BP_L(b, 0x1E9) = G3B_FIXMUL(0xCCCC, r[3] - r[6]);
        r = (fix *)bp_seg(skel, 2);
        BP_L(b, 0x1E5) = G3B_FIXMUL(0xCCCC, r[3] - r[6]);
        r = (fix *)bp_seg(skel, 3);
        q = G3B_FIXMUL(0xCCCC, r[3] - r[6]);
        BP_L(b, 0x1FD) = -1;
        BP_L(b, 0x201) = 1;
        len0 = 0x392B;
    }
    BP_L(b, 0x1CD) = q;
    BP_L(b, 0x20F) = G3B_FIXMUL(BP_L(b, 0x1E5) + BP_L(b, 0x1E9), 0xE666);
    /* per type: 0x1D5 shoulder width, 0x1D9 hip width, 0x1ED foot height, 0x1F5 foot length
     * (constants divided by the size of segment 0), 0x1D1 arm raise, 0x213 crouch, 0x1F9 sway,
     * 0x21D lean, 0x205 leg splay */
#define DIMS(w1, w2, w3, w4) do { BP_L(b, 0x1D5) = (long)(w1) / dv; BP_L(b, 0x1D9) = (long)(w2) / dv; \
        BP_L(b, 0x1ED) = (long)(w3) / dv; q = (long)(w4) / dv; } while (0)
    switch ((ulong)BP_L(b, 0)) {
    case 0: case 1: case 3: case 4:
        DIMS(0x280000, 0x100000, 0xACCCC, 0x89999);
        BP_L(b, 0x1D1) = 0xDE50; BP_L(b, 0x213) = 0xE666; BP_L(b, 0x1F9) = 0x4CCC;
        BP_W(b, 0x21D) = 0x333; BP_UW(b, 0x205) = 0xF800;
        BP_L(b, 0x1F5) = q;
        break;
    case 2:
        DIMS(0x2D0000, 0x100000, 0xACCCC, 0x89999);
        BP_L(b, 0x1D1) = 0xDE50; BP_L(b, 0x213) = 0xE666; BP_L(b, 0x1F9) = 0x4CCC;
        BP_W(b, 0x21D) = 0x333; BP_UW(b, 0x205) = 0xF800;
        BP_L(b, 0x1F5) = q;
        break;
    case 5:
        DIMS(0x2D0000, 0x100000, 0xB6666, 0x12CCCC);
        BP_L(b, 0x1D1) = 0xEBCA; BP_L(b, 0x213) = 0xE666; BP_L(b, 0x1F9) = 0x4CCC;
        BP_W(b, 0x21D) = 0; BP_UW(b, 0x205) = 0xF800;
        BP_L(b, 0x1F5) = q;
        break;
    case 6:
        DIMS(0x2A0000, 0xF0000, 0xA0000, (long)0xFFF46667);
        BP_L(b, 0x1D1) = 0xD794; BP_L(b, 0x213) = 0xB333; BP_L(b, 0x1F9) = 0xCCCC;
        BP_W(b, 0x21D) = 0x1000; BP_UW(b, 0x205) = 0xF000;
        BP_L(b, 0x1F5) = q;
        break;
    case 7:
        DIMS(0x2A0000, 0x100000, 0xFCCCC, 0x23333);
        BP_L(b, 0x1D1) = 0xD794; BP_L(b, 0x213) = 0xB333; BP_L(b, 0x1F9) = 0xCCCC;
        BP_W(b, 0x21D) = 0x1000; BP_UW(b, 0x205) = 0xF000;
        BP_L(b, 0x1F5) = q;
        break;
    case 8:
        DIMS(0x3D0000, 0x1B0000, 0xECCCC, 0x153333);
        BP_L(b, 0x1D1) = 0xC35E; BP_L(b, 0x213) = 0xE666; BP_L(b, 0x1F9) = 0x8000;
        BP_W(b, 0x21D) = 0x800; BP_UW(b, 0x205) = 0xF800;
        BP_L(b, 0x1F5) = q;
        break;
    case 9:
        DIMS(0x210000, 0x100000, 0x60000, 0xA9999);
        goto common;
    case 10:
        DIMS(0x240000, 0xA0000, 0x89999, 0x7CCCC);
        goto common;
    case 11:
        r = (fix *)bp_seg(skel, 4);
        BP_L(b, 0x1D9) = BP_L(b, 0x1D5) = G3B_FIXMUL(0xFB9F, r[3] - r[6]);
        r = (fix *)bp_seg(skel, 0);
        q = G3B_FIXMUL(0x539C, r[3] - r[6]);
        BP_L(b, 0x213) = 0xCCCC; BP_L(b, 0x1F9) = 0x4CCC;
        BP_W(b, 0x21D) = 0; BP_UW(b, 0x205) = 0;
        BP_L(b, 0x1ED) = q;
        break;
    default:
        DIMS(0x2A0000, 0x100000, 0x60000, 0xA9999);
    common:
        BP_L(b, 0x1D1) = 0xC35E; BP_L(b, 0x213) = 0xE666; BP_L(b, 0x1F9) = 0x4CCC;
        BP_W(b, 0x21D) = 0; BP_UW(b, 0x205) = 0xF800;
        BP_L(b, 0x1F5) = q;
        break;
    }
#undef DIMS
    BP_L(b, 0x1F1) = fix_sqrt(G3B_FIXMUL(len0, len0) - G3B_FIXMUL(BP_L(b, 0x1ED), BP_L(b, 0x1ED)));
    if (BP_L(b, 0) == 0xB)
        BP_L(b, 0x1F5) = BP_L(b, 0x1F1);
    BP_L(b, 0x217) = BP_L(b, 0x1D9);
    BP_L(b, 0x2F2) = G3B_FIXMUL(BP_L(b, 0x1E5), BP_L(b, 0x1E5));
    BP_L(b, 0x2EE) = BP_L(b, 0x2F2) - G3B_FIXMUL(BP_L(b, 0x1E9), BP_L(b, 0x1E9));
    BP_L(b, 0x2F6) = BP_L(b, 0x1E9) + BP_L(b, 0x1E5) + BP_L(b, 0x1ED);
    BP_L(b, 0x2FA) = G3B_FIXMUL(BP_L(b, 0x2F6), BP_L(b, 0x2F6));
    BP_L(b, 0x2FE) = BP_L(b, 0x1E9) + BP_L(b, 0x1E5);
    BP_L(b, 0x2C6) = G3B_FIXMUL(BP_L(b, 0x213), BP_L(b, 0x2F6));
    BP_L(b, 0x221) = PBAT_L(G3B_ENT(BP_ENT(b)).type, 0x22) / 6;
    BP_L(b, 0x30A) = G3B_FIXMUL(BP_L(b, 0x1DD), BP_L(b, 0x1DD));
    BP_L(b, 0x306) = BP_L(b, 0x1DD) + BP_L(b, 0x1E1);
    BP_L(b, 0x302) = BP_L(b, 0x30A) - G3B_FIXMUL(BP_L(b, 0x1E1), BP_L(b, 0x1E1));
    BP_L(b, 0x2CA) = BP_L(b, 0x2C6) + G3B_ENT(BP_ENT(b)).z - BP_L(b, 0x221);
    BP_L(b, 0x2CE) = 0;
    q = G3B_FIXMUL(0x17190, fix_sqrt(BP_L(b, 0x2F6)));
    BP_L(b, 0x20B) = q;
    BP_L(b, 0x20B) = G3B_FIXMUL(fix_sqrt(game_442439), q);
    BP_L(b, 0x207) = BP_L(b, 0x20B) / 6;
    if (BP_L(b, 0) == 0xB)
        BP_L(b, 0x20B) *= 2;
    BP_B(b, 0x275) = 2;
    BP_B(b, 0x278) = 0;
    BP_B(b, 0x2B9) = 2;
    BP_B(b, 4) = 0;
    BP_B(b, 0x2BF) = 0;
    BP_W(b, 0x225) = BP_W(b, 0x21D);
    BP_B(b, 0x278) = 0;
    if (game_low_mem) {
        uchar *en = *(uchar **)(skel + 8);
        ResDrop(REFID(BP_L(en, 0x04)));
        ResDrop(REFID(BP_L(en, 0x14)));
        ResDrop(REFID(BP_L(en, 0x24)));
        ResDrop(REFID(BP_L(en, 0x34)));
        ResDrop(REFID(BP_L(en, 0x44)));
        ResDrop(REFID(BP_L(en, 0x54)));
        if (skel[1] == 7)
            ResDrop(REFID(BP_L(*(uchar **)(skel + 8), 0x64)));
    }
}

/* ------------------------------------------------------------------ screen */

/* screen radius (pixels, at least 1) of the biped */
long biped_2820A4(short ent)
{
    uchar *b = BIPED_OF(ent);
    fix v[3];
    g3s_phandle p;
    long n;

    v[2] = BP_L(b, 0x131);
    v[0] = BP_L(b, 0x135);
    v[1] = -BP_L(b, 0x139);
    p = g3_transform_point(V3(v));
    n = (g3_screen_size_y(BP_L(b, 0x1CD) + BP_L(b, 0x2F6), p->z) + 0x8000) >> 16;
    if (n == 0)
        n = 1;
    g3_free_point(p);
    return n;
}

/* screen box (left, top, right, bottom) of the biped from the feet level to above the head */
void biped_28213C(short ent, short *box)
{
    uchar *pba = (uchar *)&game_pba[G3B_ENT(ent).slot], *b = pba + 0xDE;
    fix v[3], sz, sx1, lo, hi, q;
    g3s_phandle p;

    v[2] = BP_L(pba, 0x20F);
    v[0] = BP_L(pba, 0x213);
    v[1] = -(BP_L(pba, 0x217) - BP_L(b, 0x2F6));
    p = g3_transform_point(V3(v));
    box[3] = (short)(p->sy >> 16);
    sz = g3_screen_size_x(BP_L(b, 0x2F6) + BP_L(b, 0x1F5), p->z);
    sx1 = p->sx;
    g3_free_point(p);
    q = 3 * BP_L(b, 0x1CD) / 4;
    BP_L(pba, 0x223) += q;
    v[2] = BP_L(pba, 0x21B);
    v[0] = BP_L(pba, 0x21F);
    v[1] = -BP_L(pba, 0x223);
    BP_L(pba, 0x223) -= 3 * BP_L(b, 0x1CD) / 4;
    p = g3_transform_point(V3(v));
    box[1] = (short)(p->sy >> 16);
    if (p->sx > sx1) {
        lo = sx1;
        hi = p->sx;
    } else {
        lo = p->sx;
        hi = sx1;
    }
    box[0] = (short)((lo - sz) >> 16);
    box[2] = (short)((hi + sz) >> 16);
    g3_free_point(p);
}
