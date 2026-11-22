/* mfdpanel.c -- Terra Nova module M022 (0x26CB50-0x27161F): MFD squad panels, member pages,
 * order menu, descrambler and scanner dialogs, and the squad-order helpers that turn an MFD
 * command into AI messages.  Decompiled from __FF.EXE v1.09.
 *
 * View modes (hud_mode_fns 0x35AEF4: activate, event, update, draw):
 *   mode 1  squad list      0x26CB50 / 0x26CB78 / 0x26CC48 / 0x26CD00
 *   mode 2  squad member    0x26D398 / 0x26D3DC / 0x26CC48 / 0x26D52C
 *   mode 3  member pages    0x26D618 / 0x26D630 / 0x26D748 / 0x26D754 (pages 0x26D764 status,
 *                           0x26DC8C systems, 0x26DCC0 weapons, 0x26DEE8 other systems)
 *   mode 6  draw 0x26DEE4 (empty)       mode 7  order menu event 0x26DF20, draw 0x26E208
 *   mode 23 charge dialog   0x26F504 / 0x26F5AC / 0x26D748 / 0x26F8A8
 *   mode 26 descrambler     0x26F088 / 0x26EFAC / 0x26F094 / 0x26E834
 *   mode 27 choice dialog   0x26F4F8 / 0x26F2C8 / 0x26D748 / 0x26F13C
 *
 *   0x26CFCC mfd_26CFCC portraits column     0x26D194 mfd_26D194 redraw after a hit
 *   0x26D1BC mfd_26D1BC point in name bar    0x26D1F4 mfd_26D1F4 name bar
 *   0x26D300 mfd_26D300 next / previous member
 *   0x26DEF8 / 0x26DF10 order menu open / close
 *   0x26E724 mfd_26E724 descrambler wave     0x26FAB4 mfd_26FAB4 repeat the last order
 *   0x26FB38 mfd_26FB38 order a member (AI message)       0x26FD8C mfd_26FD8C reset charges
 *   0x26FDA8 mfd_26FDA8 order "use charge"   0x26FF38 mfd_26FF38 charge used: repair systems
 *   0x270090 mfd_270090 weapon orders        0x27016C mfd_27016C damage bar
 *   0x27022C mfd_27022C systems bar          0x27047C mfd_27047C order timing value
 *   0x27065C mfd_27065C order done (radio / mission events)
 *   0x270930 mfd_270930 target reached       0x270CE0 mfd_270CE0 order "go to target"
 *   0x270E70 / 0x270F0C flag tests           0x270E80 mfd_270E80 entity in a member's range
 *   0x270F1C mfd_270F1C mission 5 squad flag 0x270FB0 mfd_270FB0 order description text
 *   0x271428 mfd_271428 a voice 0x20 member  0x271470 mfd_271470 order "attack"
 *   0x2715A8 mfd_2715A8 squad button click
 *
 * Watcom merged code: 0x26DEE4 is the bare `ret` of 0x26DCC0; 0x26DEE8 jumps to 0x2717F0.
 *
 * Quirks kept:
 *  - mfd_26D52C / mfd_26DF20 / mfd_26E208 / the order helpers index the member records with -1
 *    when the entity is not a member (reads the bytes before game_391E50).
 *  - mfd_26CB78: with more than 2 members, a click on row 4 or below picks "all" (index = count).
 *  - mfd_26DF20: a menu line with a plain command runs it and keeps scanning the list, so a
 *    following line can run too when the picked line's text is empty.
 *  - mfd_26E208: the 0x35A038 list with member byte +0xD == -1 draws an uninitialised buffer;
 *    the ltoa of a value <= -100 writes its 5th byte over the member index (emulated).
 *  - mfd_26FB38: when the channel already has a target entity the AI message gets the
 *    position of an uninitialised local (only the target is meaningful then).
 *  - mfd_27065C (0x34): compares mfd_38FCF4 indexed by the member, everywhere else by channel.
 *  - mfd_26EFAC: the click rectangle's far corner is (x0 + rect.x0 + rect.x1, ...).
 *  - mfd_27047C: the per-skill default case indexes soldier_skill by the member index, while
 *    the other cases go through the member's voice (voice_soldier).
 */
#include "g8_a.h"
#include "g2_b2.h"     /* objdmg_hit_flag */
#include "g6_a.h"      /* drawobj_35B1A0 */
#include "g6_b.h"      /* flash_28E630 */
#include "g4_a.h"      /* aimain_24A6A0, ailow_24AE4C, ailow_24B9B8 */
#include "t1_c.h"      /* MsgAlloc, MsgSend, ltoa, lg_memset, sprintf */
#include "g8_c.h"     /* added at the G5-G8 merge */

/* callees owned by other G8 / later workers (no header yet) */
void cmd_25F0C0(long member, long b);                   /* 0x25F0C0 */
void cmd_25F7E8(MfdItem *it);                           /* 0x25F7E8 */
long cmd_25FFC8(long arg);                              /* 0x25FFC8 */
uchar cmd_26016C(long v);                               /* 0x26016C */
uchar cmd_26018C(void);                                 /* 0x26018C */
void mfddata_266AF4(long ent, long b, long member);     /* 0x266AF4 */
void simtype_27AE24(long a, long b, long chan);         /* 0x27AE24 */
uchar simtype_27B200(void);                             /* 0x27B200 */
long simtype_27B298(long ent);                          /* 0x27B298 (full eax) */
schar simtype_27B368(long chan);                        /* 0x27B368 */
void simtype_27B3C8(long ent);                          /* 0x27B3C8 */
void simtype_27B4AC(long ent);                          /* 0x27B4AC */
void options_2CAAC0(LGRect *r, char *text, long font, grs_canvas *c);  /* 0x2CAAC0 */

#define DRAW_CLEAR 8
#define DRAW_POINT 10
#define DRAW_URECT 15
#define DRAW_RECT 16
#define DRAW_BOX 18
#define FONT_STRING 315

#define HI (game_35A540 >> 1)
#define FCOLOR(c) (grd_canvas->gc.fcolor = (c))
#define DIRTY(w) ((w)->flags |= 0x20)
#define EV_X(ev) (*(short *)((uchar *)(ev) + 8))
#define EV_Y(ev) (*(short *)((uchar *)(ev) + 0xA))
#define EV_PT(ev) (*(LGPoint *)((uchar *)(ev) + 8))

#define SM(i) ((uchar *)game_391E50 + (long)(i) * 0x32)
#define SM_KIND(i) (*(short *)SM(i))
#define SM_ENT(i) (*(short *)(SM(i) + 2))
#define SM_SLOT(i) (*(short *)(SM(i) + 4))
#define SM_VOICE(i) (SM(i)[6])
#define SM_CHAN(i) (*(schar *)(SM(i) + 7))
#define PBA(slot) ((uchar *)&game_pba[slot])
#define ALIVE(slot) (PBA(slot)[0xCF] & 1)
#define BRAIN(b) ((uchar *)&game_brains[b])
#define SQUAD(s) ((uchar *)&game_squads[s])
#define SQ_ADDR(brain) (*(ulong *)(SQUAD(BRAIN(brain)[6]) + 3))
#define MS16(p) (*(short *)(p))
#define MU16(p) (*(ushort *)(p))

static void rect(long a, long b, long c, long d) { WATCOM_CALL(grd_canvas_table[DRAW_RECT], a, b, c, d); }
static void urect(long a, long b, long c, long d) { WATCOM_CALL(grd_canvas_table[DRAW_URECT], a, b, c, d); }
static void box(long a, long b, long c, long d) { WATCOM_CALL(grd_canvas_table[DRAW_BOX], a, b, c, d); }
static void rect_r(short *r) { rect(r[0], r[1], r[2], r[3]); }

/* A FrameDesc resource (bitmap + 0x1C byte header) drawn with the canvas ubitmap / bitmap. */
static void draw_ufr(Ref ref, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);
    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, x, y);
}

static void draw_fr(Ref ref, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);
    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, x, y);
}

/* index of the member record of ent, -1 if none (inlined everywhere) */
static schar find_member(short ent)
{
    schar k = 0;

    while (k < game_391E4C && SM_ENT(k) != ent)
        k++;
    if (k == game_391E4C)
        k = -1;
    return k;
}

/* the AI message every order helper sends: from the player's squad to member k's squad */
static void send_order(Msg *m, long k, void *pos, long a, long c, long d)
{
    uchar b = game_entities[SM_ENT(k)].f21;

    aiacc_25ACE0(m, SQ_ADDR(game_entities[SM_ENT(0)].f21), SQ_ADDR(b), pos, a, b, c, d);
    MsgSend(m);
}

/* Watcom inlined strcpy / strcat */
static char *s_strcpy(char *d, const char *s)
{
    char *p = d;
    while ((*p++ = *s++) != 0)
        ;
    return d;
}

static char *s_strcat(char *d, const char *s)
{
    s_strcpy(d + strlen(d), s);
    return d;
}

static long iabs(long a)                 /* cdq / xor / sub */
{
    long s = a >> 31;
    return (a ^ s) - s;
}

static int in_rect(short *r, short x, short y)
{
    return y >= r[1] && y < r[3] && x >= r[0] && x < r[2];
}

static void click_sound(void)
{
    SndPlay(0x7E2, 0, 0x28, 1, 0x40, *(schar *)RefGet(0x89C0076), 0);
}

/* ------------------------------------------------------------------ mode 1: squad list */
void mfd_26CB50(HudWidget *w, uchar on)
{
    if (!on) {
        hud_views[0].mode_mask &= ~2;
        return;
    }
    if (game_391E4C > 1)
        hud_views[w->id].mode_mask |= 0x3C;
}

void mfd_26CB78(HudWidget *w, HudEvent *ev)
{
    long i, cnt = game_391E4C;

    if (cnt <= 1)
        return;
    i = (EV_Y(ev) - w->y0) / (HI ? 0x1C : 0xE) + 1;
    if (i < cnt || cnt <= 2) {
        if (SM_KIND(i) != 1)
            return;
    } else {
        if (i != 4)
            return;
        i = cnt;
    }
    if (ev->type != 1)
        return;
    if (i != game_391E4C) {
        if (!ALIVE(SM_SLOT(i)))
            return;
        if (!ALIVE(game_player_slot))
            return;
    }
    cmd_25F0C0((short)i, 1);
}

void mfd_26CC48(long view)
{
    uchar hit, flag;
    long i;

    if (!ALIVE(game_player_slot))
        cmd_25F044();
    flag = (game_392490 & 1) | ((game_392490 >> 1) & 1);
    hit = objdmg_hit_flag;
    for (i = 0; i < game_391E4C; i++)
        if (!ALIVE(SM_SLOT(i)) || hit) {
            flag = 1;
            hit = 0;
            break;
        }
    if (flag)
        DIRTY(HUD_VIEW_W(view));
    objdmg_hit_flag = hit;
}

void mfd_26CD00(void)
{
    long i, y = 2;
    Ref id;

    draw_ufr(0x23D0009, 0, 0);
    mfd_26CFCC();
    for (i = 1; i < game_391E4C; i++) {
        FCOLOR(SM(i)[0x31]);
        strutil_2DBCF8(0x1F7, 0x16C0000 + SM_VOICE(i), 0x18, y, 0);
        if (SM_KIND(i) == 1) {
            mfd_27022C((short)i, 0x51, (short)(y + 1), 2, 10);
            mfd_27016C((short)i, 0x51, (short)(y + 5), 1, 10);
        }
        y += HI ? 0xC : 6;
        FCOLOR(0x1D);
        if (SM_KIND(i) == 1) {
            if (SM_VOICE(i) == 0x20)
                id = 0x16E0068;
            else
                id = 0x16E0068 + game_39034D[voice_soldier[SM_VOICE(i)] * 0x1EE];
            strutil_2DBCF8(0x1F7, id, 0x18, y, 0);
        } else if (SM_KIND(i) == 2) {
            FCOLOR(0x4E);
            GfxDrawText(0x1F7, RefGet(0x16E00C1), 0x18, y);
        } else {
            GfxDrawText(0x1F7, RefGet(0x16E00C2), 0x18, y);
        }
        y += HI ? 0x10 : 8;
    }
    if (game_391E4C > 2) {
        LGRect r = *(LGRect *)&hud_w_389E44->x0;
        char *s = RefLock(0x16F0046);
        long e = (HI ? 0x1C : 0xE) * 3 + 1;
        short tw, th;

        FCOLOR(0x84);
        box(0, (short)e, (short)(r.lr.x - r.ul.x), (short)(r.lr.y - r.ul.y));
        gr_font_string_size(ResGet(0x1F7), s, &tw, &th);
        GfxDrawText(0x1F7, s, (r.lr.x - r.ul.x - tw) / 2, e + (r.lr.y - r.ul.y - e - th) / 2);
        ResUnlock(0x16F);
    }
}

void mfd_26CFCC(void)
{
    long i, y = 1;

    draw_ufr(0x23D0001, 0, 0);
    for (i = 1; i < game_391E4C; i++) {
        if (ALIVE(SM_SLOT(i)) && ALIVE(game_player_slot) && SM_KIND(i) == 1) {
            draw_ufr(0x21A0000 + SM_VOICE(i), 1, (short)y);
            if (((uchar *)radio_talking)[i])
                draw_ufr(0x23D0006, 0, (short)(y - 1));
        } else {
            grs_canvas *c = grd_canvas;
            short l, t, r, b;

            c->gc.clip.i.sten = NULL;
            l = c->gc.clip.i.left;
            c->gc.clip.i.left = 1;
            t = c->gc.clip.i.top;
            c->gc.clip.i.top = (short)y;
            r = c->gc.clip.i.right;
            c->gc.clip.i.right = 0x16;
            b = c->gc.clip.i.bot;
            grd_canvas->gc.clip.i.bot = (short)((HI ? 0x1C : 0xE) + y - 1);
            hudview_26C50C(0);
            c = grd_canvas;
            c->gc.clip.i.sten = NULL;
            c->gc.clip.i.left = l;
            c->gc.clip.i.top = t;
            c->gc.clip.i.right = r;
            c->gc.clip.i.bot = b;
        }
        y += HI ? 0x1C : 0xE;
    }
}

void mfd_26D194(long view)
{
    if (objdmg_hit_flag) {
        objdmg_hit_flag = 0;
        DIRTY(HUD_VIEW_W(view));
    }
}

uchar mfd_26D1BC(HudWidget *w, long y)
{
    return y - w->y0 >= w->canvas.bm.h - (HI ? 16 : 8);
}

/* the name bar under the portrait: current member, or the squad name in group mode */
void mfd_26D1F4(void)
{
    schar cur = (schar)mfd_3599D4;
    long color = SM(cur)[0x31];
    Ref text;
    HudWidget *w;
    long h;

    if ((hud_389BC2 & 0x200) && hud_views[0].mode == 2) {
        text = 0x16F0046;
        color = 0x84;
    } else
        text = 0x16C0000 + SM_VOICE(cur);
    w = HUD_VIEW_W(0);
    h = w->canvas.bm.h;
    FCOLOR(0);
    rect(0, (short)(h - (HI ? 16 : 8)), w->canvas.bm.w, (short)h);
    FCOLOR(color);
    strutil_2DBCF8(0x1F7, text, 3, h - ((HI ? 16 : 8) - 1), 0);
}

/* select the next (dir 1) / previous (dir -1) living soldier */
uchar mfd_26D300(long dir)
{
    schar cur = (schar)mfd_3599D4;
    long last = game_391E4C - 1;
    long i = cur + dir;

    if (i <= 0)
        i = last;
    else if (i >= game_391E4C)
        i = 1;
    for (;;) {
        if (i == cur)
            return 0;
        if (ALIVE(SM_SLOT(i)) && SM_KIND(i) == 1) {
            cmd_25F0C0((short)i, 0);
            mfd_38FC81 = SM_ENT(i);
            mfd_3599D4 = (short)i;
            DIRTY(HUD_VIEW_W(0));
            return 1;
        }
        i += dir;
        if (i <= 0)
            i = last;
        else if (i >= game_391E4C)
            i = 1;
    }
}

/* ------------------------------------------------------------------ mode 2: squad member */
void mfd_26D398(HudWidget *w, uchar on)
{
    long v;

    if (!on)
        return;
    v = w->id;
    hud_views[v].mode_mask |= 4;
    if (hud_views[1].mode != 7)
        cmd_25F0C0(mfd_3599D4, 0);
    hud_views[v].timer = game_time + 0x190;
}

void mfd_26D3DC(HudWidget *w, HudEvent *ev)
{
    long dy = EV_Y(ev) - w->y0;
    long i;

    if (dy >= w->canvas.bm.h - (HI ? 16 : 8)) {
        if (ev->type == 1)
            mfd_26D300(1);
        else if (ev->type == 4)
            mfd_26D300(-1);
        else
            return;
        DIRTY(w);
        return;
    }
    if (EV_X(ev) > w->x0 + 0x17)
        return;
    i = dy / (HI ? 0x1C : 0xE) + 1;
    if (i >= game_391E4C || SM_KIND(i) != 1 || ev->type != 1)
        return;
    if (hud_389BC2 & 4) {
        hud_389BC8 = BRAIN(game_entities[SM_ENT(i)].f21)[6];
        cmd_2601D8();
        cmd_25F0AC(0x2EE);
        return;
    }
    if (!ALIVE(SM_SLOT(i)))
        return;
    cmd_25F0C0((short)i, 0);
    DIRTY(w);
}

void mfd_26D52C(void)
{
    grs_bitmap *bm;

    WATCOM_CALL(grd_canvas_table[DRAW_CLEAR], 0);
    if (!(hud_389BC2 & 0x200)) {
        schar k = find_member(mfd_38FC81);
        Ref ref = (0x21C + SM_VOICE(k)) << 16;

        mfd_26CFCC();
        bm = RefGet(ref);
    } else {
        mfd_26CFCC();
        bm = RefGet(0x21B0000);
    }
    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, 0x17, 1);
    mfd_26D1F4();
}

/* ------------------------------------------------------------------ mode 3: member pages */
void mfd_26D618(HudWidget *w, uchar on)
{
    if (on) {
        mfd_38FD02 = 0;
        DIRTY(HUD_VIEW_W(0));
    }
}

void mfd_26D630(HudWidget *w, HudEvent *ev)
{
    LGPoint org = *(LGPoint *)RefGet(0x2400000);
    LGPoint pt;
    short dx, dy;
    long t = ev->type, i;

    if (t != 1 && t != 4)
        return;
    if (mfd_38FD02) {
        mfd_38FD02 = 0;
        DIRTY(HUD_VIEW_W(0));
        return;
    }
    dx = EV_X(ev) - w->x0;
    dy = EV_Y(ev) - w->y0;
    pt.x = org.x + dx;
    pt.y = org.y + dy;
    if (dy >= w->canvas.bm.h - (HI ? 16 : 8) && dx < 0x32) {
        mfd_26D300(t == 1 ? 1 : -1);
        return;
    }
    for (i = 0; i < 3; i++)
        if (RectTestPt(RefGet(mfd_35B154[i]), pt)) {
            mfd_38FD02 = (uchar)(i + 1);
            DIRTY(HUD_VIEW_W(0));
            return;
        }
}

void mfd_26D748(long view)
{
    DIRTY(HUD_VIEW_W(view));
}

void mfd_26D754(void)
{
    WATCOM_CALL(mfd_35B160[mfd_38FD02], mfd_38FD02);
}

/* page 0: status (helmet view of the member, meter and systems bars, order text) */
void mfd_26D764(void)
{
    char buf[0x100];
    schar cur = (schar)mfd_3599D4;
    short *r0, *r;
    LGRect rc;
    short slot, tw, th;
    long bar, v;
    uchar *pba, *sys;
    HudWidget *w0;

    mfd_38FC81 = SM_ENT(mfd_3599D4);
    ResLock(0x240);
    r0 = RefGet(0x2400000);
    draw_ufr(0x23D0007, 0, 0);
    grd_canvas->gc.clip.i.sten = NULL;
    grd_canvas->gc.clip.i.left = 1;
    grd_canvas->gc.clip.i.top = 0;
    grd_canvas->gc.clip.i.right = 0x32;
    grd_canvas->gc.clip.i.bot = HUD_VIEW_W(0)->canvas.bm.h - 1;
    slot = SM_SLOT(cur);
    rc = *(LGRect *)RefGet(0x2400005);
    rc.ul.x -= r0[0];
    rc.ul.y -= r0[1];
    rc.lr.x -= r0[0];
    rc.lr.y -= r0[1];
    helm_2635C8(slot, &rc);
    mfd_26D1F4();
    w0 = HUD_VIEW_W(0);
    grd_canvas->gc.clip.i.sten = NULL;
    grd_canvas->gc.clip.i.left = 0;
    grd_canvas->gc.clip.i.top = 0;
    grd_canvas->gc.clip.i.right = w0->canvas.bm.w;
    grd_canvas->gc.clip.i.bot = w0->canvas.bm.h;

    pba = PBA(slot);
    bar = (fix_mul(*(fix *)(pba + 0x86), fix_div(0x230000, *(fix *)(pba + 0x8A))) + 0x8000) >> 16;
    FCOLOR(0);
    r = RefGet(0x2400003);
    urect((short)(r[0] - r0[0] + bar), (short)(r[1] - r0[1]), (short)(r[2] - r0[0]),
          (short)(r[3] - r0[1]));
    FCOLOR(0x36);
    box((short)(r[0] - 1 - r0[0]), (short)(r[1] - 1 - r0[1]), (short)(r[2] - r0[0]),
        (short)(r[3] + 1 - r0[1]));

    sys = *(uchar **)(pba + 0x9C);
    v = MU16(sys + 3) * 35 / MU16(sys + 1);
    if (MU16(sys + 3) && !(short)v)
        v = 1;
    FCOLOR(0);
    r = RefGet(0x2400002);
    urect((short)(v + r[0] - r0[0]), (short)(r[1] - r0[1]), (short)(r[2] - r0[0]),
          (short)(r[3] - r0[1]));
    FCOLOR(0x36);
    box((short)(r[0] - 1 - r0[0]), (short)(r[1] - 1 - r0[1]), (short)(r[2] - r0[0]),
        (short)(r[3] + 1 - r0[1]));

    r = RefGet(0x2400006);
    mfd_270FB0(game_entities[SM_ENT(cur)].f21, buf);
    grd_canvas->gc.font = ResLock(0x1F7);
    gr_font_string_wrap(grd_canvas->gc.font, buf, (short)(r[2] - r[0]));
    gr_font_string_size(grd_canvas->gc.font, buf, &tw, &th);
    FCOLOR(0x84);
    WATCOM_CALL(grd_canvas_table[FONT_STRING], (long)grd_canvas->gc.font, (long)buf,
                (short)((r[2] - r[0] - tw) / 2 + (r[0] - r0[0])), (short)(r[1] - r0[1]));
    ResUnlock(0x1F7);
    ResUnlock(0x240);
}

/* page 1: systems of the member shown */
void mfd_26DC8C(void)
{
    mfdpage_27172C(PBA(game_entities[mfd_38FC81].slot) + 6);
}

/* page 2: weapons of the member shown */
void mfd_26DCC0(void)
{
    char b0[0x28], b28[0x28];
    uchar *pba, *wp;
    long i, y = 1, yy;
    uchar r, col;

    WATCOM_CALL(grd_canvas_table[DRAW_CLEAR], 0);
    pba = PBA(game_entities[mfd_38FC81].slot);
    wp = pba + 0x45;
    for (i = 0; i < 4; i++, wp += 0xF, y += HI ? 0x1A : 0xD) {
        uchar *o;

        if (!wp[0])
            continue;
        FCOLOR(0x1D);
        GfxDrawText(0x1F7, mfd_38FD10[wp[0]], 4, y);
        yy = y + (HI ? 0xC : 6);
        r = weapon_ready(wp, pba + 0x86);
        col = 0;
        if (r == 0) {
            o = (uchar *)&ord_table[wp[0]];
            if (MS16(wp + 2) < MS16(o + 0xC)) {
                strutil_2DBCE0(b28, RefGet(0x1710028), 0x28);
                lg_sprintf(b0, b28, MS16(wp + 2) * 100 / MS16(o + 0xC));
                col = 0x7E;
            } else if (MU16(o + 0xA) > 1) {
                strutil_2DBCE0(b28, RefGet(0x1710027), 0x28);
                lg_sprintf(b0, b28, MU16(wp + 4));
                col = 0x84;
            } else {
                strutil_2DBCE0(b0, RefGet(0x16F0047), 0x28);
                col = 0x84;
            }
        } else if (r == 3 || r == 5) {
            strutil_2DBCE0(b0, RefGet(0x1710029), 0x28);
            col = 0x84;
        }
        if (col) {
            FCOLOR(col);
            GfxDrawText(0x1F7, b0, 0xA, yy);
        } else {
            FCOLOR(mfd_35B170[r]);
            strutil_2DBCF8(0x1F7, 0x16F0047 + r, 0xA, yy, 0);
        }
    }
}

void mfd_26DEE4(void)
{
}

/* page 3: systems of another entity */
void mfd_26DEE8(void)
{
    mfdpage_2717F0(mfd_38FC81);
}

/* ------------------------------------------------------------------ mode 7: order menu */
void mfd_26DEF8(void)
{
    hud_38FD03 = 1;
    hudcb_264680(0);
    hudcb_2646D8(1);
}

void mfd_26DF10(void)
{
    hud_38FD03 = 0;
    hudcb_2646D8(0);
}

void mfd_26DF20(HudWidget *w, HudEvent *ev)
{
    char buf[0x50];
    schar k = find_member(mfd_38FC81);
    MfdItem *it;
    long row, n, r;

    if (ev->type == 4) {
        cmd_25F044();
        return;
    }
    if (ev->type != 1)
        return;
    row = (EV_Y(ev) - w->y0 - 2) / (HI ? 0xE : 7);
    it = mfd_38FC87->items;
    for (n = 0; it->fn; it++, n++) {
        if (n == row) {
            if (SM_VOICE(k) == 1 && game_359708 == 0x1F && it->cmd == 0x19) {
                if (game_35B150 & 0x10)
                    return;
                r = cmd_25FFC8(it->arg);
                if (!(short)r) {
                    RadioSpeak(k, 0xC);
                    return;
                }
                *(short *)(mfd_392141 + SM_CHAN(k) * 12) = (short)r;
                mfd_26FB38(1, 3, *(schar *)(SM(k) + 2), 0x42);
                simtype_27B3C8((short)r);
                cmd_25F044();
                return;
            }
            if (!simtype_27B368(SM_CHAN(k)) && it->cmd == 0x19)
                return;
            if (!simtype_27B200() && it->cmd == 0x1A)
                return;
            if (it->cmd == 0x42) {
                if (!cmd_26016C((uchar)it->arg)) {
                    mfd_38FCFC = row;
                    cmd_25F7E8(it);
                    return;
                }
            } else if (it->cmd == 0x3D) {
                if (!cmd_26018C()) {
                    mfd_38FCFC = row;
                    cmd_25F7E8(it);
                    return;
                }
            } else if (mfd_38FC87->items != mfd_35A090 || row < 2
                       || (game_359708 == 0x1E && SM_VOICE(k) == 1)) {
                mfd_38FCFC = row;
                cmd_25F7E8(it);
            }
        }
        if (it->flags & 1) {
            if (!*(char *)RefGet(it->arg))
                row++;
        } else {
            WATCOM_CALL(it->fn, it->arg, (long)k, (long)buf);
            if (!buf[0])
                row++;
        }
    }
}

void mfd_26E208(void)
{
    char b0[0x3C], b3c[0x3C], num[12];
    schar k = find_member(mfd_38FC81);
    MfdItem *it;
    long cnt = 0, y = 2, yb;
    char *text = NULL;

    WATCOM_CALL(grd_canvas_table[DRAW_CLEAR], 0);
    FCOLOR(0x1D);
    for (it = mfd_38FC87->items; it->fn; it++) {
        FCOLOR(0x1D);
        if (SM_VOICE(k) == 1 && game_359708 == 0x1F
            && (it->arg == 0x16F0034 || it->arg == 0x16F0035))
            continue;
        if (!simtype_27B368(SM_CHAN(k)) && it->cmd == 0x19 && it->arg == 0x16F0034)
            FCOLOR(0x26);
        if (!simtype_27B200() && it->cmd == 0x1A && it->arg == 0x16F0035)
            FCOLOR(0x26);
        if (it->flags & 1) {
            text = RefGet(it->arg);
            if (!*text)
                continue;
        } else {
            if (it->cmd == 0x42)
                FCOLOR(cmd_26016C((uchar)it->arg) ? 0x26 : 0x1D);
            else if (it->cmd == 0x3D)
                FCOLOR(cmd_26018C() ? 0x26 : 0x1D);
            WATCOM_CALL(it->fn, it->arg, (long)k, (long)b3c);
            if (!b3c[0])
                continue;
        }
        if (SM_VOICE(k) == 1 && game_359708 == 0x1F && it->cmd == 0x42 && (game_35B150 & 0x10))
            FCOLOR(0x26);
        mfd_38FCC0[cnt++] = it->arg;
        memset(mfd_38FD04, 0, 3);
        if (it->key >= 0x83B && it->key <= 0x843) {
            mfd_38FD04[0] = 'F';
            mfd_38FD04[1] = (char)(it->key - 10);
        } else
            mfd_38FD04[0] = (char)it->key;
        GfxDrawText(0x1F7, mfd_38FD04, 3, y);
        GfxDrawText(0x1F7, (it->flags & 1) ? text : b3c, 0x10, y);
        y += HI ? 0xE : 7;
    }
    FCOLOR(0x1D);
    yb = HI ? 0x5C : 0x2E;
    if (mfd_38FC87->items == mfd_35A038) {
        schar c = *(schar *)(SM(k) + 0xD);

        if (c != -1)
            s_strcpy(b0, RefGet(0x16E0030 + c));
        GfxDrawText(0x1F7, b0, 3, yb);      /* b0 uninitialised when c == -1 (original too) */
    }
    if (!(SM_VOICE(k) == 1 && game_359708 == 0x1F) && mfd_38FC87->items == mfd_35A090) {
        s_strcpy(b0, RefGet(0x2430023));
        ltoa((schar)simtype_27B368(SM_CHAN(k)), num, 10);
        if (strlen(num) >= 4)               /* the original's 4-byte buffer overflows into k */
            k = num[4];
        s_strcat(b0, num);
        GfxDrawText(0x1F7, b0, 3, yb);
    }
    if (mfd_38FC87->items == mfd_35A2FC) {
        s_strcpy(b0, RefGet(0x2430024));
        ltoa(mfd_38FCF8[SM_CHAN(k)], num, 10);
        s_strcat(b0, num);
        GfxDrawText(0x1F7, b0, 3, yb);
    }
}

/* ------------------------------------------------------------------ mode 26: descrambler */
/* the signal line: flat, or noise of amplitude noise */
void mfd_26E724(LGRect *r, long noise)
{
    long mid = ((r->lr.y - r->ul.y) >> 1) + 3;
    long n = r->lr.x - r->ul.x - 2;
    long i;

    FCOLOR(0x1D);
    if (!noise) {
        gen_hline((short)(r->ul.x + 1), (short)mid, (short)(r->lr.x - 2));
        return;
    }
    for (i = 0; i < n; i++) {
        long c = RndRange(&game_rnd_35B704, 1, 3), d;

        if (c == 1)
            FCOLOR(0x1D);
        else if (c == 2)
            FCOLOR(0x84);
        else if (c == 3)
            FCOLOR(0x36);
        d = RndRange(&game_rnd_35B704, 0, noise * 2);
        WATCOM_CALL(grd_canvas_table[DRAW_POINT], (short)(r->ul.x + i + 1), (short)(d + mid - noise));
    }
}

void MfdDescramblerPanel(void)
{
    GameEntity *e = &game_entities[game_player_entity];
    fix q, s;
    short *r0, *r1, *r2, *r3, *r4, *r5, *r6, *r8;
    long noise, rnd;

    q = fix_div(0x500000 - fix_fast_pyth_dist(e->x - mfd_4413D8, e->y - mfd_4413DC), 0x500000);
    s = q;
    ResLock(0x23F);
    r0 = RefGet(0x23F0000);
    r1 = RefGet(0x23F0001);
    r2 = RefGet(0x23F0002);
    r3 = RefGet(0x23F0003);
    r4 = RefGet(0x23F0004);
    r5 = RefGet(0x23F0005);
    r6 = RefGet(0x23F0006);
    r8 = RefGet(0x23F0008);
    mfd_35B176 = *(LGRect *)RefGet(0x23F0007);
    draw_ufr(0x23E0000, 0, 0);
    if (game_35B175 & 1) {
        rnd = RndRange(&game_rnd_35B704, 0x28, 0x3C);
        FCOLOR(0x85);
        options_2CAAC0(&mfd_35B176, RefGet(0x16F00A2), 0x1F7, &hud_w_389E44->canvas);
        if (q < 0)
            s = 0;
        noise = fix_mul(((r0[3] - r0[1] - 1) / 2) << 16, s) >> 16;
        if (game_35B188 && s > 0xE000) {
            long hh;

            FCOLOR(0x1D);
            hh = HI ? 6 : 3;
            GfxDrawText(0x1F7, RefGet(0x16F00A4), r8[0], r8[1] - hh);
            hh = HI ? 6 : 3;
            GfxDrawText(0x1F7, RefGet(0x16F00A5), r8[0], r8[1] + hh);
        }
        mfd_26E724((LGRect *)r0, noise);
        FCOLOR(0x84);
        if (s > 0) {
            char *t;
            long i, j;

            (void)fix_mul(rnd << 16, s);        /* computed and dropped by the original */
            t = RefGet(0x16F00A0);
            if (s > 0xEA00) {
                j = mfd_35B180++;
                if ((ulong)(strlen(t) - 1) <= (ulong)mfd_35B180) {
                    mfd_35B180 = 0;
                    j = 0;
                }
                mfd_35B184 |= 1;
                for (i = 0; i < 0x1A; i++, j++) {
                    if (t[j])
                        mfd_38FC98[i] = t[j];
                    else {
                        mfd_38FC98[i] = t[0];
                        j = 0;
                    }
                }
                mfd_38FC98[i] = 0;
            } else if (s > 0x8000) {
                for (i = 0; i < 0x1A; i += 2) {
                    mfd_38FC98[i] = t[RndRange(&game_rnd_35B704, 0, strlen(t) - 1)];
                    mfd_38FC98[i + 1] = t[RndRange(&game_rnd_35B704, 0, strlen(t) - 1)];
                }
                mfd_35B184 = 0;
                mfd_38FC98[i] = 0;
            } else {
                s_strcpy(mfd_38FC98, RefGet(0x16F00A1));
                mfd_35B184 = 0;
            }
            GfxDrawText(0x1F7, mfd_38FC98, 2, HI ? 0x5C : 0x2E);
        }
    } else {
        FCOLOR(0x8A);
        options_2CAAC0(&mfd_35B176, RefGet(0x16F00A3), 0x1F7, &hud_w_389E44->canvas);
    }
    FCOLOR(0x84);
    box(mfd_35B176.ul.x, mfd_35B176.ul.y, mfd_35B176.lr.x, mfd_35B176.lr.y);
    rect((short)(mfd_35B176.ul.x - 3), mfd_35B176.ul.y, mfd_35B176.ul.x, mfd_35B176.lr.y);
    rect(mfd_35B176.lr.x, mfd_35B176.ul.y, (short)(mfd_35B176.lr.x + 2), mfd_35B176.lr.y);
    FCOLOR(0x89);
    rect((short)(mfd_35B176.ul.x - 1), (short)(mfd_35B176.ul.y + 1), (short)(mfd_35B176.ul.x + 1),
         (short)(mfd_35B176.lr.y - 1));
    rect((short)(mfd_35B176.lr.x - 1), (short)(mfd_35B176.ul.y + 1), (short)(mfd_35B176.lr.x + 1),
         (short)(mfd_35B176.lr.y - 1));
    FCOLOR(0x84);
    box(r0[0], r0[1], r0[2], r0[3]);
    FCOLOR(0x89);
    box((short)(r0[0] - 1), (short)(r0[1] - 1), (short)(r0[2] + 1), (short)(r0[3] + 1));
    FCOLOR(0x89);
    rect_r(r1);
    rect_r(r2);
    rect_r(r3);
    rect_r(r4);
    rect_r(r5);
    rect_r(r6);
    if (s > 0xEA00 && (game_35B175 & 1)) {
        if (game_time > mfd_35B190) {
            mfd_35B190 = game_time + 0xFA;
            if (mfd_35B18C >= 2)
                mfd_35B18C = 0;
            else
                mfd_35B18C++;
        }
        FCOLOR(0x82);
        r1 = RefGet(0x23F0001 + mfd_35B18C * 2);
        r2 = RefGet(0x23F0002 + mfd_35B18C * 2);
        rect_r(r1);
        rect_r(r2);
    }
    ResUnlock(0x23F);
}

void mfd_26EFAC(HudWidget *w, HudEvent *ev)
{
    LGRect r;
    HudWidget *m;

    if (ev->type != 1)
        return;
    m = hud_w_389E44;
    r.ul.x = m->x0 + mfd_35B176.ul.x;
    r.ul.y = m->y0 + mfd_35B176.ul.y;
    r.lr.x = r.ul.x + mfd_35B176.lr.x;
    r.lr.y = r.ul.y + mfd_35B176.lr.y;
    if (!RectTestPt(&r, EV_PT(ev)))
        return;
    game_35B175 ^= 1;
    SndPlay(0x7E1, 0, 0x28, 1, 0x40, 0x40, 0);
    if (game_35B175 & 1)
        mfd_35B194 = SndPlay(0x7F8, 0, 0xFF, 0, -1, 0x40, 0);
    else {
        snd_chan_stop(mfd_35B194);
        mfd_35B194 = -1;
    }
}

void mfd_26F088(HudWidget *w, uchar on)
{
    if (!on)
        hud_views[0].mode_mask &= ~(1UL << 26);
}

void mfd_26F094(long view)
{
    uchar f = mfd_35B184;

    if (!(game_35B175 & 1))
        f = 0;
    else if ((f & 1) && !(f & 2)) {
        mfd_35B198 = game_time + 0xC350;
        f |= 2;
    } else if ((f & 1) && game_time > mfd_35B198 && !(f & 4)) {
        mfd_35B184 = f;
        ailow_24B9B8(0);
        game_35B188 = 1;
        f = mfd_35B184 | 4;
        *(ulong *)(PBA(game_player_slot) + 0xD8) = game_time + 0x1388;
    }
    DIRTY(HUD_VIEW_W(view));
    mfd_35B184 = f;
}

/* ------------------------------------------------------------------ mode 27: choice dialog */
void mfd_26F13C(void)
{
    char buf[0x40];     /* 0x14 in the original */
    short r[4];

    MouseHide();
    FCOLOR(0);
    memcpy(r, RefGet(0x2420000), 8);
    rect(0, 0, r[2], r[3]);
    memcpy(r, RefGet(0x2420007), 8);
    draw_fr(0x2410001, r[0], r[1]);
    FCOLOR(0x1D);
    sprintf(buf, "%s: %i", (char *)RefGet(0x2430013), mfd_38FD01);
    GfxDrawText(0x1F7, buf, 5, HI ? 0xA : 5);
    sprintf(buf, "%s: %i", (char *)RefGet(0x2430014), mfd_38FD00);
    GfxDrawText(0x1F7, buf, 5, HI ? 0x1E : 0xF);
    memcpy(r, RefGet(0x2420006), 8);
    GfxDrawText(0x1F7, RefLock(0x243000D), r[0], r[1]);
    ResUnlock(0x243);
    MouseShow();
}

void mfd_26F2C8(HudWidget *w, HudEvent *ev)
{
    short r[4], dx, dy;
    short *m;

    if (ev->type == 4) {
        click_sound();
        cmd_25F044();
        return;
    }
    if (ev->type != 1)
        return;
    m = tnm_state_ptr;
    dx = m[0] - w->x0;
    dy = m[1] - w->y0;
    memcpy(r, RefGet(0x2420007), 8);
    if (in_rect(r, dx, dy)) {
        mfd_3599EA = 0;
        click_sound();
        mfd_26FB38(mfd_38FD01, (schar)mfd_3599EA, (schar)hud_389BC4, 0x42);
        cmd_25F044();
    }
    memcpy(r, RefGet(0x2420008), 8);
    if (in_rect(r, dx, dy)) {
        mfd_3599EA = 1;
        click_sound();
        mfd_26FB38(mfd_38FD01, (schar)mfd_3599EA, (schar)hud_389BC4, 0x42);
        cmd_25F044();
    }
    memcpy(r, RefGet(0x2420009), 8);
    if (in_rect(r, dx, dy)) {
        mfd_3599EA = 2;
        click_sound();
        mfd_26FB38(mfd_38FD01, (schar)mfd_3599EA, (schar)hud_389BC4, 0x42);
        cmd_25F044();
    }
}

void mfd_26F4F8(HudWidget *w, uchar on)
{
    if (!on)
        hud_views[0].mode_mask &= ~(1UL << 26);
}

/* ------------------------------------------------------------------ mode 23: charge dialog */
void mfd_26F504(HudWidget *w, uchar on)
{
    schar k;
    long a;

    if (!on || !w)
        return;
    k = find_member(hud_389BC4);
    mfd_38FD00 = simtype_27B368(SM_CHAN(k));
    a = simtype_27B298((schar)hud_389BC4);
    if (mfd_38FD00 >= a)        /* the value: the smaller of the two */
        mfd_38FD01 = (schar)simtype_27B298((schar)hud_389BC4);
    else
        mfd_38FD01 = mfd_38FD00;
}

void mfd_26F5AC(HudWidget *w, HudEvent *ev)
{
    short r[4], dx, dy;
    short *m;
    long v;

    if (ev->type == 4) {
        click_sound();
        hudview_26C224(w->id, 0x16);
        return;
    }
    if (ev->type != 1)
        return;
    m = tnm_state_ptr;
    dx = m[0] - w->x0;
    dy = m[1] - w->y0;
    memcpy(r, RefGet(0x2420002), 8);
    if (in_rect(r, dx, dy)) {
        v = mfd_38FD01 - 1;
        if (v < 0)
            v = 0;
        mfd_38FD01 = (schar)v;
        click_sound();
    }
    memcpy(r, RefGet(0x2420004), 8);
    if (in_rect(r, dx, dy)) {
        v = mfd_38FD01 + 1;
        if (mfd_38FD00 < v)
            v = mfd_38FD00;
        mfd_38FD01 = (schar)v;
        click_sound();
    }
    memcpy(r, RefGet(0x2420007), 8);
    if (in_rect(r, dx, dy)) {
        mfd_26FB38(mfd_38FD01, 0, (schar)hud_389BC4, 0x19);
        click_sound();
        cmd_25F044();
    }
    memcpy(r, RefGet(0x2420008), 8);
    if (in_rect(r, dx, dy)) {
        mfd_26FB38(mfd_38FD01, 1, (schar)hud_389BC4, 0x19);
        click_sound();
        cmd_25F044();
    }
    memcpy(r, RefGet(0x2420009), 8);
    if (in_rect(r, dx, dy)) {
        mfd_26FB38(mfd_38FD01, 2, (schar)hud_389BC4, 0x19);
        click_sound();
        cmd_25F044();
    }
}

void mfd_26F8A8(void)
{
    char buf[0x40];     /* 0x14 in the original */
    short r[4];

    MouseHide();
    FCOLOR(0);
    memcpy(r, RefGet(0x2420000), 8);
    rect(0, 0, r[2], r[3]);
    memcpy(r, RefGet(0x2420002), 8);
    draw_fr(0x2410000, r[0], r[1]);
    memcpy(r, RefGet(0x2420007), 8);
    draw_fr(0x2410001, r[0], r[1]);
    memcpy(r, RefGet(0x2420001), 8);
    FCOLOR(0x1D);
    GfxDrawText(0x1F7, RefLock(0x243000C), r[0], r[1]);
    ResUnlock(0x243);
    memcpy(r, RefGet(0x2420006), 8);
    GfxDrawText(0x1F7, RefLock(0x243000D), r[0], r[1]);
    ResUnlock(0x243);
    memcpy(r, RefGet(0x2420003), 8);
    sprintf(buf, "%i", mfd_38FD01);
    GfxDrawText(0x1F7, buf, r[0], r[1]);
    memcpy(r, RefGet(0x2420005), 8);
    sprintf(buf, "%s: %i", (char *)RefGet(0x243000E), mfd_38FD00);
    GfxDrawText(0x1F7, buf, r[0], r[1]);
    MouseShow();
}

/* ------------------------------------------------------------------ squad orders */
void mfd_26FAB4(long ent)
{
    schar k = find_member((short)ent);
    long c = SM_CHAN(k);

    simtype_27AE24(mfd_38FCE8[c * 2 + 1], mfd_38FCE8[c * 2], c);
    RadioSpeak(k, 0x56);
}

/* order member ent: radio acknowledgement and an AI message (type 0x31) to its squad */
void mfd_26FB38(long a, long b, long ent, long cmd)
{
    schar k = find_member((short)ent);
    short c = SM_CHAN(k);
    uchar *o;
    short pt[2];
    Msg *m;
    long v;

    mfd_38FCE8[c * 2] = (schar)b;
    mfd_38FCE8[c * 2 + 1] = (schar)a;
    RadioSpeak(k, (uchar)b == 3 ? 1 : (schar)((uchar)b + 0x58));
    m = MsgAlloc();
    if (!m)
        return;
    o = mfd_392141 + c * 12;
    if (MS16(o) == 0) {
        pt[0] = MS16(o + 4);
        pt[1] = MS16(o + 8);
        v = mfd_27047C(k, 3, (uchar)cmd);
        send_order(m, k, pt, 0, (uchar)cmd, (uchar)v);
    } else {
        v = mfd_27047C(k, 3, (uchar)cmd);
        send_order(m, k, pt, MS16(o), (uchar)cmd, (uchar)v);   /* pt uninitialised (original) */
    }
}

void mfd_26FD8C(void)
{
    lg_memset(mfd_38FCF8, 0x05050505, 0, 4);
}

void mfd_26FDA8(long ent, long target, long c)
{
    schar k = find_member((short)ent);
    Msg *m;
    long v;

    if (!mfd_270E80((short)target)) {
        RadioSpeak(k, 0xC);
        return;
    }
    RadioSpeak(k, 1);
    if (!mfd_38FCF8[SM_CHAN(k)]) {
        RadioSpeak(k, 0x5C);
        return;
    }
    mfd_38FCF4[SM_CHAN(k)] = (uchar)target;
    m = MsgAlloc();
    if (!m)
        return;
    v = mfd_27047C(k, 5, (uchar)c);
    send_order(m, k, NULL, (short)target, 0x34, (uchar)v);
}

/* a charge was used on ent2: repair 30% of each of its systems but the first */
void mfd_26FF38(long ent, long ent2)
{
    schar k = find_member((short)ent), j;
    uchar *p;
    long n, i;

    objdmg_hit_flag = 1;
    mfd_38FCF8[SM_CHAN(k)]--;
    j = find_member((short)ent2);
    p = PBA(SM_SLOT(j)) + 6;
    n = (short)ent2 == game_player_entity ? 8 : 4;
    for (i = 0; i < n; i++)
        if (i) {
            uchar *rec = *(uchar **)(p + 0x96) + i * 5;
            ushort v = MU16(rec + 3) + MU16(rec + 1) * 30 / 100;

            MU16(rec + 3) = v < MU16(rec + 1) ? v : MU16(rec + 1);
        }
    RadioSpeak(k, 0x49);
}

#ifndef TN_WATCOM_ABI
extern int port_ai_fix;                 /* port/mods/ai.c: "AI: Fixed", a bit for each of G49 to G60 */
#define AI_FIX(g) (port_ai_fix >> ((g) - 49) & 1)
#endif
void mfd_270090(long ent, long what)
{
    uchar b = game_entities[(short)ent].f21;
    uchar *br, *p;

    if (!b)
        return;
    br = BRAIN(b);
    if (br[0xD] != 1 || br[5] != 1)
        return;
    p = PBA(game_entities[(short)ent].slot) + 9;
    switch ((ushort)what) {
    case 0x28:
        br[0x2F] = 2;
        aimain_24A6A0(b, *(long *)p == 0 ? 0x5C : 1);
        break;
    case 0x29:
        br[0x2F] = 0;
        aimain_24A6A0(b, 1);
        break;
    case 0x2A:
        if (*(long *)p > 0) {
#ifndef TN_WATCOM_ABI
            /* G51 ("AI: Fixed"): "Use ASF Now" sets the mode to "now" for the one use, and the use
             * sets it to "Don't use ASF" whatever it was: a squad mate on "Use ASF at Will" (the
             * mission's start) no longer uses its ASF by itself.  Fixed: back to the mode before */
            uchar before = br[0x2F];
#endif
            br[0x2F] = 1;
            ailow_24AE4C(b, *(long *)(p + 4), 1);
#ifndef TN_WATCOM_ABI
            if (AI_FIX(51) && before == 2)
                br[0x2F] = 2;
#endif
        }
        aimain_24A6A0(b, *(long *)p == 0 ? 0x5C : 1);
        break;
    }
}

/* damage bar of member i: grows right from x - w, colour by damage */
void mfd_27016C(long i, long x, long y, long h, long w)
{
    uchar d = damage_27DF14(SM_ENT((short)i), 0);
    long len = (short)d * (short)w / 100;
    long x0;

    if ((short)d > 0)
        len = (short)len < 1 ? 1 : (short)len;
    x0 = x - w;
    if ((short)d < 0x32)
        FCOLOR(0x41);
    else if ((short)d < 0x5A)
        FCOLOR(0x4D);
    else
        FCOLOR(0x82);
    rect((short)x0, (short)y, (short)(len + x0), (short)(h + y));
}

/* systems bar of member i: four segments by system condition */
void mfd_27022C(long i, long x, long y, long h, long w)
{
    uchar *p = PBA(SM_SLOT((short)i)) + 6, *end = p + 0x1C;
    long a = 0, b = 0, c = 0, d = 0;
    fix unit, pos;
    short x0, x1, y0 = (short)y, y1;

    for (; p != end; p += 7) {
        short cur = MS16(p + 0xF), mx = MS16(p + 0x11);

        if (cur > mx * 0x49 / 100)
            a++;
        else if (cur < mx * 0x25 / 100 && cur > 0)
            b++;
        else if (cur == 0)
            c++;
        else
            d++;
    }
    unit = ((long)(short)w << 16) / 4;
    x0 = (short)(x - w);
    pos = (long)x0 << 16;
    x1 = (short)(x0 + w);
    y1 = (short)(y0 + h);
    if (a > 0) {
        FCOLOR(0x82);
        rect((short)(pos >> 16), y0, x1, y1);
    }
    pos += a * unit;
    if (d > 0) {
        FCOLOR(0x4D);
        rect((short)(pos >> 16), y0, x1, y1);
    }
    pos += d * unit;
    if (b > 0) {
        FCOLOR(0x41);
        rect((short)(pos >> 16), y0, x1, y1);
    }
    pos += b * unit;
    if (c > 0) {
        FCOLOR(0x25);
        rect((short)(pos >> 16), y0, x1, y1);
    }
}

/* timing value for an order of member k (per mission tables and the soldier's skills) */
long mfd_27047C(long k, long what, long cmd)
{
    long v = 0, i;
    long rec;
    uchar s;

    switch ((uchar)((uchar)what - 1)) {
    case 0:
    case 1:
        return 0;
    case 2:
        for (i = 0; i < 16; i += 2)
            if (mfd_395AEC[i] == game_359708)
                v = mfd_395AEC[i + 1];
        if (!v)
            v = mfd_395AEC[1];
        rec = voice_soldier[SM_VOICE((schar)k)] * 0x1EE;
        s = soldier_skill[rec + 4];
        return s ? v * 10 / s : v;
    case 3:
        if ((uchar)cmd == 0x3C)
            v = mfd_395AD8[1];
        else
            for (i = 0; i < 16; i += 2)
                if (mfd_395AD8[i] == game_359708)
                    v = mfd_395AD8[i + 1];
        if (!v)
            v = mfd_395AD8[1];
        rec = voice_soldier[SM_VOICE((schar)k)] * 0x1EE;
        s = soldier_skill[rec + 3];
        return s ? v * 10 / s : v;
    case 4:
        if ((uchar)cmd == 0x40) {
            for (i = 0; i < 4; i += 2)
                if (mfd_395B00[i] == game_359708)
                    v = mfd_395B00[i + 1];
            return v;
        }
        return mfd_395B00[1];
    default:            /* indexed by the member index itself, not its soldier record */
        return 10 - soldier_skill[(schar)k * 0x1EE + (schar)what];
    }
}

/* an order of member ent finished (what = command) */
void mfd_27065C(long ent, long what)
{
    schar k = find_member((short)ent);

    switch ((uchar)what) {
    case 9:
        if (game_35B19C && game_359708 == 0x1E) {
            pbasim_28A140((short)ent, 0);
            drawobj_35B1A0 = 1;
            mfd_35B6E5 |= 1;
        }
        return;
    case 0x19:
    case 0x42:
        mfd_26FAB4((short)ent);
        if (game_359708 == 0x1F && SM_VOICE(k) == 1)
            game_35B150 |= 0x10;
        return;
    case 0x34:
        k = find_member((short)ent);
        if (game_359708 == 5 && (game_3599E8 & 4) && mfd_38FCF4[k] == (ushort)hud_389BDC
            && !mfd_271428()) {
            mfd_270F1C(0);
            game_3599E8 |= 1;
            mfddata_266AF4(hud_389BDC, 0x20, k);
            RadioSpeak(k, 0x49);
            mfd_38FCF8[SM_CHAN(k)]--;
            return;
        }
        if ((game_3599E8 & 8) && mfd_38FCF4[k] == (ushort)hud_389BDC && !mfd_271428()) {
            game_3599E8 |= 2;
            RadioSpeak(k, 0x49);
            mfd_38FCF8[SM_CHAN(k)]--;
            return;
        }
        mfd_26FF38((short)ent, (short)mfd_38FCF4[SM_CHAN(k)]);
        return;
    case 0x3C:
    case 0x3D:
        k = find_member((short)ent);
        mfd_270930((short)ent, mfd_38FC90[SM_CHAN(k)]);
        return;
    }
}

/* member ent reached its target entity: mission events */
void mfd_270930(long ent, long target)
{
    char buf[0x50];
    schar k;
    short c;
    GameEntity *e;

    simtype_27B4AC((short)target);
    k = find_member((short)ent);
    c = SM_CHAN(k);
    e = &game_entities[(short)target];
    if (e->cls == 4 && (e->type == 0x45 || e->type == 0x46)) {
        instsim_288E40((short)target);
        RadioSpeak(k, 0x54);
        mfd_38FC90[c] = 0;
        return;
    }
    switch (game_359708) {
    case 5:
        if (mfd_271428())
            return;
        if (game_3599E8 & 2) {
            mfd_270F1C(0);
            game_3599E8 |= 1;
            mfddata_266AF4((short)target, 0x20, k);
        } else
            game_3599E8 |= 4;
        RadioSpeak(k, 0x4C);
        break;
    case 0x15:
        inst_siren_flags |= 2;
        RadioSpeak(k, 0x4D);
        break;
    case 0x18:
        game_35B1A4 = 1;
        RadioSpeak(k, 0x4E);
        break;
    case 0x19:
        game_35B1A8 = 1;
        RadioSpeak(k, 0x4F);
        break;
    case 0x1E:
        game_35B19C = 1;
        RadioSpeak(k, 0x50);
        break;
    case 0x20: {
        long i;

        if (game_35B1AC)
            return;
        for (i = 0; i < 0x14; i += 5) {
            uchar *sys = *(uchar **)(PBA(game_entities[(short)ent].slot) + 0x9C);
            ushort cur = MU16(sys + i + 3);

            MU16(sys + i + 3) = (ushort)(cur * RndRange(&rnd_stream_35B48C, 10, 20) / 100);
        }
        s_strcpy(buf, RefGet(0x2440003));     /* copied and never used */
        RadioSpeak(k, 0x5D);
        game_35B1AC = 1;
        instsim_288554((short)target, 1);
        static_287168((short)ent, (short)target, 3, 1, 0x320);
        break;
    }
    case 0x21:
        flash_28E630();
        game_35B1B0 = 1;
        RadioSpeak(k, 0x51);
        break;
    default:
        return;
    }
    mfd_38FC90[c] = 0;
}

/* order member ent to go to entity target */
void mfd_270CE0(long ent, long target, long cmd)
{
    schar k = find_member((short)ent);
    GameEntity *e;
    Msg *m;
    long v;

    mfd_38FC90[SM_CHAN(k)] = (short)target;
    if ((uchar)cmd == 0x3C) {
        e = &game_entities[(short)target];
        if (!(e->cls == 4 && (e->type == 0x45 || e->type == 0x46))) {
            RadioSpeak(k, 0x53);
            return;
        }
    }
    m = MsgAlloc();
    if (!m)
        return;
    v = mfd_27047C(k, 4, (uchar)cmd);
    send_order(m, k, NULL, (short)target, (uchar)cmd, (uchar)v);
}

long mfd_270E70(void)
{
    return game_3599E8 & 1;
}

/* is entity ent in the range of any member? */
uchar mfd_270E80(long ent)
{
    long i;

    for (i = 0; i < game_391E4C; i++) {
        fix range = *(fix *)(PBA(SM_SLOT(i)) + 0xB2);
        GameEntity *me = &game_entities[SM_ENT(i)], *e = &game_entities[(short)ent];

        if (fix_fast_pyth_dist(me->x - e->x, me->y - e->y) < range)
            return 1;
    }
    return 0;
}

uchar mfd_270F0C(void)
{
    return (game_35B150 & 0x10) != 0;
}

void mfd_270F1C(long v)
{
    short e = hud_389BDC;
    long i;

    if (game_359708 == 5) {
        for (i = 0; i < 60; i++) {
            short s = (short)(MS16(PBA(i)) << 1) >> 1;

            if (game_entities[s].f23 == 1)
                e = s;
        }
        SQUAD(BRAIN(game_entities[e].f21)[6])[2] = (uchar)v;
    }
    hud_389BDC = e;
}

/* the text describing what brain is doing */
void mfd_270FB0(long brain, char *out)
{
    char b0[0x50], b50[0x50], ba0[0x50];
    uchar bi = (uchar)brain;
    uchar *br = BRAIN(bi);
    uchar sq = br[6], *s, a;
    schar k;
    short c;
    Ref text = 0;

    out[0] = 0;
    k = 0;
    while (k < game_391E4C && SM_ENT(k) != MS16(br + 0xB))
        k++;
    if (k == game_391E4C)
        k = -1;
    c = SM_CHAN(k);
    ResLock(0x16F);
    s = SQUAD(sq);
    if (s[0x157] != 0 && s[0x157] != 4 && s[0x157] != 2) {
        switch (s[0x162]) {
        case 1:
            text = 0x16F00B3;
            goto found;
        case 0x19:
        case 0x42:
            text = 0x16F00B0;
            goto found;
        case 0x34: {
            long j;

            for (j = 0; j < game_391E4C; j++)
                if (SM_ENT(j) == MS16(br + 0xB))
                    break;
            if (j == game_391E4C)
                j = -1;
            hud_26A6D4(mfd_38FCF4[SM_CHAN(j)], ba0);
            lg_sprintf(out, RefGet(0x16F00B1), ba0);
            goto done;
        }
        case 0x3C:
        case 0x3D: {
            short t = mfd_38FC90[c];

            text = mfd_35B1B4[game_359708];
            if (!text && t && game_entities[t].cls == 4 && game_entities[t].type == 0x45)
                text = 0x16F00AF;
            break;
        }
        }
    }
    if (text)
        goto found;
    a = br[0x30];
    switch (a) {
    case 0:
    case 1:
        text = 0x16F00B3;
        break;
    case 2:
    case 4: {
        short tx = MS16(br + 0x33), ty = MS16(br + 0x35), mx = MS16(br + 0xE), my = MS16(br + 0x10);
        short t = MS16(s + 0xA9);

        if (t) {
            hud_26A6D4(t, b50);
            lg_sprintf(out, RefGet(0x16F00A6), b50);
            goto done;
        }
        if (iabs(mx - tx) > 1 || iabs(my - ty) > 1) {
            long ang = (ushort)(fix_atan2(ty - my, tx - mx) + 0x1000) >> 13;

            lg_sprintf(out, RefGet(0x16F00A8), RefGet(0x16F008A + ang));
            goto done;
        }
        text = 0x16F00B3;
        break;
    }
    case 8:
    case 0x10:
        if (br[0xCA]) {
            hud_26A6D4(br[0xCA], b0);
            lg_sprintf(out, RefGet(0x16F00A7), b0);
        } else
            lg_sprintf(out, RefGet(0x16F00A7), mfd_3558C8);
        goto done;
    }
found:
    if (!text)
        text = 0x16F00B2;
    s_strcpy(out, RefGet(text));
done:
    ResUnlock(0x16F);
}

/* is any member a voice 0x20 record that is set? */
long mfd_271428(void)
{
    long i, n = game_391E4C;

    for (i = 0; i < n; i++)
        if (SM_VOICE(i) == 0x20 && SM_KIND(i) != 0)
            return 1;
    return 0;
}

/* order member ent to attack entity target */
void mfd_271470(long ent, long target)
{
    schar k = find_member((short)ent);
    Msg *m;
    long v;

    if (!mfd_270E80((short)target)) {
        RadioSpeak(k, 0xC);
        return;
    }
    m = MsgAlloc();
    if (!m)
        return;
    v = mfd_27047C(k, 5, 0x41);
    send_order(m, k, NULL, (short)target, 9, (uchar)v);
}

/* the squad button: switch the view to the squad list, or select all */
void mfd_2715A8(HudWidget *w, HudEvent *ev)
{
    uchar press = ev->type == 1;
    long v = w->id;

    if (!press || game_391E4C <= 1)
        return;
    SndPlay(0x7EA, 0, 0x40, 1, -1, 0x40, 0);
    if (hud_views[v].mode != 1) {
        hudview_26C224(v, 1);
        cmd_25F044();
        return;
    }
    cmd_25F0C0(mfd_3599D4, 1);
}
