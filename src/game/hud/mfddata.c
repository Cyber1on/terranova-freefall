/* mfddata.c -- Terra Nova module M019 (0x266320-0x267B4F): MFD / comm data.  Decompiled from
 * __FF.EXE v1.09.
 *
 *   0x266320 mfddata_266320 clear the member list    0x266340 mfddata_266340 ... and stop voices
 *   0x266390 mfddata_266390 fill member record idx   0x26645C mfddata_26645C mark ent gone
 *   0x2664AC mfddata_2664AC remove member ent        0x2665B4 mfddata_2665B4 record of ent
 *   0x2665E8 mfddata_2665E8 radio channel of ent     0x266618 .. 0x266860 AI messages
 *   0x266A4C mfddata_266A4C drop all members         0x266AF4 mfddata_266AF4 add a member
 *   0x266C64 mfddata_266C64 recount                  0x266C90 mfddata_266C90 suit figure init
 *   0x266E84 mfddata_266E84 unlock rect resources    0x266EAC mfddata_266EAC build the parts
 *   0x2670B8 mfddata_2670B8 group first / last       0x267158 mfddata_267158 run part callbacks
 *   0x267198 mfddata_267198 suit figure update       0x267224 mfddata_267224 damage roll
 *   0x26747C mfddata_26747C damage level             0x2675B4 mfddata_2675B4 helmet groups follow
 *   0x267690 mfddata_267690 init a part              0x26773C mfddata_26773C mark damaged parts
 *   0x26776C mfddata_26776C start the overlay anim   0x2677B4 mfddata_2677B4 its callback
 *   0x267824 mfddata_267824 stop it                  0x267840 mfddata_267840 blit dirty parts
 *   0x267870 mfddata_267870 blit the figure (falls into the `ret` at 0x2679CC)
 *   0x2679D0 / 267A30 / 267A90 / 267AF0 draw a part with bitmap 0x308 / 0x30A / 0x30C / 0x30E
 *
 * Quirks kept:
 *  - mfddata_266860 allocates a message before the switch: for an order kind it does not
 *    handle the message is neither sent nor freed.
 *  - mfddata_266A4C walks the records up to the count it had on entry and zeroes the active
 *    word of every non-player record (count-- each time), wherever the list ends.
 *  - mfddata_266AF4 reads record [member] (member is the caller's index) for the squad.
 *  - mfddata_266EAC builds group 4 from rectangle resource 0x309 (group 0's) without locking it
 *    and subtracts dx / dy from the resource data in place on every call.
 *  - mfddata_267224 does nothing when first == last; in group 1 it reads the dependency from
 *    group 0's link record, and the random branch copies the rectangle of part [i] (group 0)
 *    into the overlay whatever the group.
 *  - mfddata_2675B4 guards the group 2 / 3 loops with the counts of groups 0 / 1.
 */
#include "g8_b.h"
#include "g8_c.h"     /* added at the G5-G8 merge */
#include "g9_a.h"     /* added at the G5-G8 merge */
#ifndef TN_WATCOM_ABI
#include "cockpitpack.h"                /* cockpit packs: the damaged pieces as drawn (P95) */
#include "visor.h"                      /* ... and the damage on the visor's glass instead */
#endif

#define REC(i) ((uchar *)&game_391E50[i])
#define RS16(r, o) (*(short *)((r) + (o)))
#define REC_END ((uchar *)&game_391E50[4])
#define PBA_P(slot) ((uchar *)&game_pba[slot])
#define PBA_PLAYER PBA_P(game_player_slot)
#define ADDR(a, b) ((ulong)(ushort)(a) | (ulong)(ushort)(b) << 16)
#define LINK(g, j8) (&mfd_38BB00[g][(uchar)(j8)])
#define STATE mfd_38BE20
#define PART mfd_38BCE0
#define NEXT(p) ((SuitPart *)((uchar *)(p) + 0x6F))

typedef void (*CanvasFn)();

/* ------------------------------------------------------------------ squad member list */

void mfddata_266320(void)
{
    memset(game_391E50, 0, 0xFA);
    game_391E4C = 0;
}

void mfddata_266340(void)
{
    long i;
    for (i = 1; i < game_391E4C; i++)
        radio_stop_voice(game_391E50[i].voice);
    memset(game_391E50, 0, 0xFA);
    game_391E4C = 0;
}

static void recount(void)
{
    uchar *r;
    long n = 0;
    for (r = REC(0); r < REC_END; r += 0x32)
        if (RS16(r, 0))
            n++;
    game_391E4C = n;
}

void mfddata_266390(long idx, long voice, long b, uchar *types, long d)
{
    uchar *r = REC(idx);
    long i;

    memset(r, 0, 0x32);
    RS16(r, 0) = 1;
    RS16(r, 2) = 0;
    RS16(r, 4) = 0;
    r[6] = (uchar)voice;
    r[7] = (uchar)idx;
    r[8] = (uchar)b;
    for (i = 0; i < 4; i++)
        r[9 + i] = types[i];
    *(long *)(r + 0x2C) = -1;
    for (i = 0; i < 3; i++)
        RS16(r, 0xE + 10 * i) = 0;
    r[0xD] = (uchar)d;
    r[0x31] = mfd_35B3ED[idx];
    recount();
}

void mfddata_26645C(long ent)
{
    uchar *r;
    for (r = REC(0); r < REC_END; r += 0x32) {
        if (RS16(r, 0) && (short)ent == RS16(r, 2)) {
            if (RS16(r, 2) == mfd_38FC81)
                cmd_25F044();
            RS16(r, 0) = 2;
            hudview_26C600(0);
            return;
        }
    }
}

void mfddata_2664AC(long ent)
{
    uchar *r, *q;
    short e;

    if ((short)ent == mfd_38FC81)
        hudview_26C224(0, 1);
    for (r = REC(0); r < REC_END; r += 0x32) {
        if (!RS16(r, 0) || (short)ent != RS16(r, 2))
            continue;
        e = RS16(r, 2);
        mstat_2C7220(e, (schar)r[7]);
        radio_stop_voice((schar)r[6]);
        if (e == mfd_38FC81)
            cmd_25F044();
        mfd_390349[voice_soldier[r[6]]][0] = 1;
        for (q = r; q < REC(3); q += 0x32)
            memcpy(q, q + 0x32, 0x32);
        RS16(REC(3), 0) = 0;
        game_391E4C--;
        if (mfd_3599D4 >= game_391E4C)
            mfd_3599D4--;
        if (game_391E4C <= 1)
            mfddata_266A4C();
        hudview_26C600(0);
        return;
    }
}

uchar *mfddata_2665B4(long ent)
{
    uchar *r;
    for (r = REC(0); r < REC_END; r += 0x32)
        if (RS16(r, 0) && (short)ent == RS16(r, 2))
            return r;
    return 0;
}

uchar mfddata_2665E8(long ent)
{
    long i;
    for (i = 0; i < 4; i++)
        if (game_391E50[i].active && (short)ent == RS16(REC(i), 2))
            return REC(i)[7];
    return 0xFF;
}

/* ------------------------------------------------------------------ AI messages */

void mfddata_266618(long ent, long a, long b)
{
    Msg *m = MsgAlloc();
    if (!m)
        return;
    m->flag = mfd_35AAC0;
    aiacc_25AC6C(m, ADDR(1, 0), ADDR(0x81, ent), &a, (uchar)b);
    MsgSend(m);
}

void mfddata_266680(long ent, long a, long b)
{
    Msg *m = MsgAlloc();
    if (!m)
        return;
    m->flag = mfd_35AAC0;
    aiacc_25AFFC(m, ADDR(1, 0), ADDR(0x81, ent), (uchar)a, (uchar)b);
    MsgSend(m);
}

void mfddata_2666E4(long ent, long a, long b)
{
    Msg *m = MsgAlloc();
    if (!m)
        return;
    m->flag = mfd_35AAC0;
    aiacc_25B014(m, ADDR(1, 0), ADDR(0x81, ent), (ushort)a, (uchar)b);
    MsgSend(m);
}

void mfddata_266748(long brain, long squad)
{
    Msg *m = MsgAlloc();
    if (!m)
        return;
    m->flag = mfd_35AAC0;
    aiacc_25AC50(m, ADDR(1, 0), ADDR(0x80, (uchar)brain), (uchar)squad, 0x14);
    MsgSend(m);
}

void mfddata_2667AC(long brain, long what)
{
    Msg *m = MsgAlloc();
    ulong from = ADDR(1, 0), to = ADDR(0x80, (uchar)brain);
    if (!m)
        return;
    m->flag = mfd_35AAC0;
    if ((ushort)what == 5)
        aiacc_25AC50(m, from, to,
                     game_brains[game_entities[game_player_entity].f21].f006, 0x14);
    else if ((ushort)what == 6)
        aiacc_25AEE0(m, from, to);
    MsgSend(m);
}

void mfddata_266860(short *ord, long b)
{
    ushort cmd = ord[0];
    ulong from = ADDR(1, 0), to = ADDR(0x81, ord[3]);
    uchar c = (uchar)b;
    Msg *m;

    if (cmd == 0x43) {
        ailow_24B24C();
        return;
    }
    m = MsgAlloc();
    if (!m)
        return;
    m->flag = mfd_35AAC0;
    switch (cmd) {
    case 1:
        aiacc_25AD78(m, from, to, c);
        break;
    case 2:
        aiacc_25ADE0(m, from, to, 0, c);
        break;
    case 3:
        aiacc_25AD88(m, from, to, c);
        break;
    case 7:
        aiacc_25AEF0(m, from, to, game_player_entity, c);
        break;
    case 8:
        aiacc_25AEF0(m, from, to, 0, c);
        break;
    case 0xA:
        aiacc_25AF1C(m, from, to, ((uchar *)ord)[0xC], c);
        break;
    case 0x11:
        aiacc_25AEF0(m, from, to, ord[4], c);
        break;
    case 0x1E:
        aiacc_25ADB8(m, from, to, ((uchar *)ord)[0xC]);
        break;
    default:
        return;
    }
    MsgSend(m);
}

/* ------------------------------------------------------------------ member list (cont.) */

void mfddata_266A4C(void)
{
    long n = game_391E4C, i;
    for (i = 0; i < n; i++) {
        if (game_player_entity != RS16(REC(i), 2)) {
            radio_stop_voice(game_391E50[i].voice);
            game_391E50[i].active = 0;
            game_391E4C--;
        }
    }
    if (hud_38FC80)
        cmd_25F044();
    hudview_26C224(0, 1);
    hudview_26C600(0);
    hudview_26C5B4(2);
    hudview_26C5B4(5);
    hudview_26C5B4(4);
    hudview_26C5B4(3);
}

void mfddata_266AF4(long ent, long b, long member)
{
    uchar types[4];
    short slot = game_entities[(short)ent].slot;
    uchar *p6 = PBA_P(slot) + 6;
    long n, sq;
    uchar brain;
    uchar *ms;
    short k;

    types[0] = p6[0x3F];
    types[1] = p6[0x4E];
    types[2] = p6[0x5D];
    types[3] = p6[0x6C];
    n = (schar)game_391E4C;
    mfddata_266390(n, (schar)b, p6[1], types, (schar)p6[7]);
    brain = game_entities[(short)ent].f21;
    game_brains[brain].f009 = (uchar)b;
    k = (short)(*(short *)&game_pba[RS16(REC((schar)member), 4)] << 1) >> 1;
    sq = game_brains[game_entities[k].f21].f006;
    mfddata_266748(brain, sq);
    ms = ordn_mission_state;
    ms[*(short *)(ms + 0x4A0) * 0x128 + 1] = (uchar)b;
    (*(short *)(ms + 0x4A0))++;
    RS16(REC(n), 4) = slot;
    RS16(REC(n), 2) = (short)ent;
}

void mfddata_266C64(void)
{
    recount();
}

/* ------------------------------------------------------------------ suit-damage figure */

void mfddata_266C90(void)
{
    uchar *suit = *(uchar **)(PBA_PLAYER + 0x9C);
    long g;

    for (g = 0; g < 6; g++)
        ResLock(0x2FF + g);
    for (g = 0; g < 6; g++)
        mfd_35AAC4[g] = *(uchar *)RefGet((0x2FF + g) << 16);
    for (g = 0; g < 6; g++)
        ResUnlock(0x2FF + g);
    mfddata_266EAC();
    ResLock(0x307);
    for (g = 0; g < 4; g++)
        memcpy(mfd_38BB00[g], RefGet(0x3070000 + g), (ulong)mfd_35AAC4[g] * 8);
    ResUnlock(0x307);
    lg_memset(mfd_38BE20, 0, 0, 0x46);
    mfd_35ADD2 = game_44136C;
    mfd_35ADD4 = *(ushort *)(suit + 3);
    mfddata_2670B8();
#ifndef TN_WATCOM_ABI
    port_visor_begin();                 /* "Visor damage": none yet, from this armour (port/mods/visor.c) */
#endif
}

void mfddata_266E84(void)
{
    ResUnlock(0x309);
    ResUnlock(0x30B);
    ResUnlock(0x30D);
    ResUnlock(0x30F);
}

static void build_group(long g, long res, long def_g, long n)
{
    long k;
    uchar *def = mfd_35AAD0[def_g * 15];
    for (k = g * 15; k < g * 15 + n; k++, def += 11) {
        PART[k] = &mfd_38BE68[k];
        mfddata_267690(PART[k], def, RefGet(res << 16 | def[9]));
    }
}

void mfddata_266EAC(void)
{
    mfd_35AACB = *(uchar *)RefGet(0x3050000);
    mfd_35AACC = *(uchar *)RefGet(0x3060000);
    ResLock(0x309);
    build_group(0, 0x309, 0, mfd_35AAC4[0]);
    ResLock(0x30B);
    build_group(1, 0x30B, 1, mfd_35AAC4[1]);
    ResLock(0x30D);
    build_group(2, 0x30D, 2, mfd_35AAC4[2]);
    ResLock(0x30F);
    build_group(3, 0x30F, 3, mfd_35AAC4[3]);
    if (mfd_35AAC4[4])
        build_group(4, 0x309, 4, mfd_35AAC4[4]);
}

void mfddata_2670B8(void)
{
    if (!mfd_35AAC4[0])
        return;
    SP_FIRST(0) = PART[0];
    SP_LAST(0) = PART[mfd_35AAC4[0] - 1];
    SP_FIRST(1) = PART[15];
    SP_LAST(1) = PART[14 + mfd_35AAC4[1]];
    SP_FIRST(2) = PART[30];
    SP_LAST(2) = PART[29 + mfd_35AAC4[2]];
    SP_FIRST(3) = PART[45];
    SP_LAST(3) = PART[44 + mfd_35AAC4[3]];
    SP_FIRST(4) = PART[60];
    SP_LAST(4) = PART[59 + mfd_35AAC4[4]];
}

void mfddata_267158(SuitPart *first, SuitPart *last)
{
    SuitPart *p;
    for (p = first; p <= last; p = NEXT(p)) {
        if (p->draw && (p->flags & 0x80))
            WATCOM_CALL((CanvasFn)p->draw, (long)p);
        if ((p->flags & 0x20) && p->draw2 && p->rect[2])
            WATCOM_CALL((CanvasFn)p->draw2, (long)p);
    }
}

void mfddata_267198(void)
{
    if (!mfd_35AAC4[0])
        return;
    mfddata_26747C();
    if (hud_eye.obj == game_396868 && hud_helmet)
        return;
    if (hud_helmet) {
        mfddata_267158(SP_FIRST(2), SP_LAST(2));
        mfddata_267158(SP_FIRST(3), SP_LAST(3));
    } else {
        mfddata_267158(SP_FIRST(0), SP_LAST(0));
        mfddata_267158(SP_FIRST(1), SP_LAST(1));
        if (mfd_35AAC4[4])
            mfddata_267158(SP_FIRST(4), SP_LAST(4));
    }
}

/* damage part p (index i of group g) and what it links to; ov: the part whose rectangle the
 * overlay takes */
static void spread(SuitPart *p, uchar *st, uchar *l, long g, SuitPart *ov)
{
    uchar k;
    p->flags |= 0x20;
    *st |= 3;
    k = l[4];
    if (k >= 1) {
        STATE[59 + k] |= 2;
        PART[59 + k]->rect = ov->rect;
        mfddata_26776C(PART[59 + l[4]]);
    }
}

static void spread_link(long g, uchar *l)
{
    uchar k = l[3];
    if (k >= 1) {
        STATE[g * 15 + k - 1] |= 3;
        PART[g * 15 + k - 1]->flags |= 0x20;
    }
}

void mfddata_267224(SuitPart *first, SuitPart *last, long g, long level)
{
    SuitPart *p;
    uchar j8 = 0, i = 0, gb = (uchar)g, lv = (uchar)level;
    uchar *st, *l;

    if (first == last)
        return;
    for (p = first; p <= last; p = NEXT(p), j8 += 8, i++) {
        st = &STATE[gb * 15 + i];
        if (*st & 2) {
            p->flags |= 0x20;
            *st |= 1;
            continue;
        }
        if (*st & 1)
            continue;
        if (gb == 1) {
            l = LINK(1, j8);
            if (l[7] >= 1 && lv > l[0] && (STATE[LINK(0, j8)[7]] & 2)) {
                spread(p, st, l, 1, PART[15 + i]);
                spread_link(1, LINK(1, j8));
            }
        }
        l = LINK(gb, j8);
        if (lv > l[0]) {
            if (RndRange(&game_rnd_35B704, 1, 100) < l[1]) {
                spread(p, st, l, gb, PART[i]);
                spread_link(gb, LINK(gb, j8));
            } else
                *st |= 1;
        }
    }
}

void mfddata_26747C(void)
{
    uchar *suit = *(uchar **)(PBA_PLAYER + 0x9C);
    ushort a, lo, s1;
    long v, t;
    uchar lvl;

    if (game_359708 == 0x2E || game_359708 == 0x2F)
        return;
    a = game_44136C;
    if (a < mfd_35ADD2)
        mfd_35ADD2 = a;
    v = game_44136A;
    lo = mfd_35ADD2;
    if (lo > v * 73 / 100)
        return;
    if (lo < v * 37 / 100 && lo != 0)
        lvl = 0x1E;
    else if (mfd_35ADD2 == 0)
        lvl = 0x28;
    else
        lvl = 0x14;
    a = *(ushort *)(suit + 3);
    if (a < mfd_35ADD4)
        mfd_35ADD4 = a;
    s1 = *(ushort *)(suit + 1);
    t = (long)(s1 - mfd_35ADD4) * 60 / (long)s1;
    lvl = (uchar)(lvl + t);
    mfddata_267224(SP_FIRST(0), SP_LAST(0), 0, lvl);
    mfddata_267224(SP_FIRST(1), SP_LAST(1), 1, lvl);
    mfddata_2675B4();
}

void mfddata_2675B4(void)
{
    SuitPart *last2 = SP_LAST(2), *last3 = SP_LAST(3), *p;
    uchar j8, k;

    if (mfd_35AAC4[0]) {
        j8 = 0;
        k = 0;
        for (p = SP_FIRST(2); p <= last2; p = NEXT(p), k++, j8 += 8) {
            if (PART[LINK(2, j8)[6] - 1]->flags & 0x20) {
                p->flags |= 0x20;
                STATE[30 + k] |= 2;
            }
        }
        if (mfd_35AAC4[1]) {
            j8 = 0;
            k = 0;
            for (p = SP_FIRST(3); p <= last3; p = NEXT(p), k++, j8 += 8) {
                if (PART[14 + LINK(3, j8)[6]]->flags & 0x20) {
                    p->flags |= 0x20;
                    STATE[45 + k] |= 2;
                }
            }
        }
    }
    SP_LAST(3) = last3;
    SP_LAST(2) = last2;
}

void mfddata_267690(SuitPart *p, uchar *def, short *rect)
{
    ushort dx = mfd_35AACB, dy = mfd_35AACC;

    p->rect = rect;
    rect[0] -= dx;
    p->rect[1] -= dy;
    p->rect[2] -= dx;
    p->rect[3] -= dy;
    p->draw = *(void **)def;
    p->draw2 = *(void **)(def + 4);
    p->flags = def[8];
    p->idx = def[9];
    p->kind = def[0xA];
    if (def[0xA] == 4)
        return;
    gr_init_sub_bitmap(&tnui_back_canvas.bm, &p->canvas.bm, rect[0], rect[1],
                       (short)(rect[2] - rect[0]), (short)(rect[3] - rect[1]));
    p->canvas.gc = grd_defgc;
    p->canvas.gc.clip.f.right = (long)p->canvas.bm.w << 16;
    p->canvas.gc.clip.f.bot = (long)p->canvas.bm.h << 16;
}

void mfddata_26773C(void)
{
    long i;
    if (!mfd_35AAC4[0])
        return;
    for (i = 0; i < 0x46; i++)
        if (STATE[i] & 2)
            PART[i]->flags |= 0x20;
}

void mfddata_26776C(SuitPart *p)
{
    if (mfd_35ADDC || !mfd_35AAC4[4])
        return;
    AnimGetSize(0x3110000);
    p->anim = anim_2E68A0(0x3110000, p, &p->canvas, WATCOM_FN(mfddata_2677B4));
    mfd_35ADDC = 1;
}

void mfddata_2677B4(uchar *anim, long flags, long c)
{
    SuitPart *p;
    grs_bitmap *bm;

    (void)c;
    if (!mfd_35AAC4[4])
        return;
    p = *(SuitPart **)(anim + 0x10);
    if (flags & 8) {
        helm_262A44();
        Free((*(SuitPart **)(anim + 0x10))->canvas.bm.bits);
    }
    if ((flags & 1) && *(short *)(anim + 0x33) > 1) {
        gr_set_canvas(grd_screen_canvas);
        bm = RefGet(*(Ref *)((uchar *)p->anim + 0x2F));
        bm->bits = (uchar *)bm + 0x1C;
        WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm,
                    p->rect[0], p->rect[1]);
    }
}

void mfddata_267824(SuitPart *p)
{
    if (p->anim)
        anim_2E6A34(p->anim);
    mfd_35ADDC = 0;
}

static inline void blit_parts(SuitPart *first, SuitPart *last)
{
    SuitPart *p;
    if (!mfd_35AAC4[0])
        return;
    for (p = first; p <= last; p = NEXT(p)) {
        if (p->flags & 0x80) {
            GfxBlit((LGRect *)p->rect);
            p->flags &= 0x7F;
        }
    }
}

void mfddata_267840(SuitPart *first, SuitPart *last)
{
    blit_parts(first, last);
}

void mfddata_267870(void)
{
    if (!mfd_35AAC4[0])
        return;
    if (hud_eye.obj == game_396868 && hud_helmet)
        return;
    if (hud_helmet) {
        blit_parts(SP_FIRST(2), SP_LAST(2));
        blit_parts(SP_FIRST(3), SP_LAST(3));
    } else {
        blit_parts(SP_FIRST(0), SP_LAST(0));
        blit_parts(SP_FIRST(1), SP_LAST(1));
        if (mfd_35AAC4[4])
            blit_parts(SP_FIRST(4), SP_LAST(4));
    }
}

static void draw_part(SuitPart *p, ulong res)
{
    grs_bitmap *bm;
    gr_set_canvas(&tnui_back_canvas);
    bm = RefGet(p->idx | res);
    bm->bits = (uchar *)bm + 0x1C;
#ifndef TN_WATCOM_ABI
    if (port_ck_on && bm->w > 0 && bm->h > 0 && bm->w <= 320 && bm->h <= 480) {
        /* a cockpit pack's picture is shown: the piece drawn undamaged (port_ck_whole) */
        static uchar pc[320 * 480];
        grs_canvas cv;
        memset(pc, 0, (size_t)bm->w * bm->h);
        gr_init_canvas(&cv, pc, BMT_FLAT8, bm->w, bm->h);
        gr_set_canvas(&cv);
        WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, 0, 0);
        gr_set_canvas(&tnui_back_canvas);
        if (!port_ck_whole(tnui_back_canvas.bm.bits, tnui_back_canvas.bm.row, pc, bm->w, bm->h, p->rect[0],
                           p->rect[1]))
            WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, p->rect[0],
                        p->rect[1]);
        else {
            /* P121: the jump jet fuel gauge (hudcb_264E70) is a live display in the frame; the
             * art's pixels there are a full gauge, so a piece over it showed the pack's full red
             * gauge until the fuel changed (after a fall, the gauge red where it was empty).  On
             * the gauge the piece's own pixels go, as without a pack */
            int g, x, y;
            for (g = 0; g < 2; g++) {
                const short *r = hud_bar_rects[g];
                for (y = r[1] > p->rect[1] ? r[1] : p->rect[1]; y < r[3] && y < p->rect[1] + bm->h; y++)
                    for (x = r[0] > p->rect[0] ? r[0] : p->rect[0]; x < r[2] && x < p->rect[0] + bm->w; x++) {
                        uchar c = pc[(size_t)(y - p->rect[1]) * bm->w + (x - p->rect[0])];
                        if (c && x >= 0 && y >= 0 && x < tnui_back_canvas.bm.w && y < tnui_back_canvas.bm.h)
                            tnui_back_canvas.bm.bits[(size_t)y * tnui_back_canvas.bm.row + x] = c;
                    }
            }
        }
    } else
#endif
    WATCOM_CALL((CanvasFn)grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, p->rect[0],
                p->rect[1]);
#ifndef TN_WATCOM_ABI
    port_ck_mark(tnui_back_canvas.bm.bits, tnui_back_canvas.bm.row, p->rect[0], p->rect[1],
                 p->rect[0] + bm->w, p->rect[1] + bm->h);
#endif
    p->flags = (p->flags & 0x5F) | 0x80;
}

void mfddata_2679D0(SuitPart *p) { draw_part(p, 0x3080000); }
void mfddata_267A30(SuitPart *p) { draw_part(p, 0x30A0000); }
void mfddata_267A90(SuitPart *p) { draw_part(p, 0x30C0000); }
void mfddata_267AF0(SuitPart *p) { draw_part(p, 0x30E0000); }
void mfddata_2679CC(void) {}
