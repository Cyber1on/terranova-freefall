/* rescustm.c -- M111 (0x2CEE20-0x2D2BE0): the custom scenario builder (rescustm.res: 0x972
 * bitmaps, 0x973 rectangles, 0x974 texts, 0x960..0x971 tables) and the scenario generator run
 * by MissionSetup (rsb_2D0F90 places the chosen units, rsb_2D2A4C applies the options).
 *
 *   0x2CEE20 / 27 / 35 (data) unit templates          0x2CEE3C open      0x2CEEA4 close
 *   0x2CEF28 per frame   0x2CEF9C defaults   0x2CF048 type -> list index   0x2CF0C8 draw
 *   0x2CF11C hint   0x2CF1A0 every control   0x2CF25C handler   0x2CF6FC hit test
 *   0x2CF754 labels   0x2CF8F0 .. 0x2D01C4 one control each   0x2D02A8 start   0x2D0324 cancel
 *   0x2D0330 quit   0x2D0340 type lists   0x2D04A4 (dead) first empty region
 *   0x2D04D4 .. 0x2D0E68 the weapon grid page   0x2D0F90 place the units
 *   0x2D1788 / 2D1BEC / 2D21B4 / 2D2600 drop-down lists   0x2D2A1C (dead) shade
 *   0x2D2A4C apply   0x2D2B04 clear   0x2D2B44 / 2D2B54 / 2D2B94 mission hooks
 *
 * Quirks kept:
 *  - rsb_2CF048 returns the index within the class's own list, but the slot index of a class
 *    2 / 3 type counts from the start of list 1: defaults of those classes land on the wrong
 *    type, and rsb_2D05C8 / rsb_2D0A20 then read before their tables;
 *  - rsb_2D0E00 compares the address 0x442EE8 with 0xFF, so columns 2 and 3 always copy;
 *  - rsb_2CF25C reads the function of a missed hit at address 0xC on the grid page (here: no
 *    call, as when that word is not 0x2D0C00);
 *  - the lists fetch the slot's rectangle before testing for slot 0xFF;
 *  - rsb_2D0F90 leaves its object descriptors' heading words uninitialised (stack garbage in
 *    the original; zero here), and a slot with a class other than 1..3 ends it early.
 */
#include "g10_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar rsb_2CEE20[7] = { 2, 0x64, 0x64, 0x64, 0, 0, 0 };
uchar rsb_2CEE27[14] = { 2, 0xFF, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0, 0, 0, 0, 0, 0 };
uchar rsb_2CEE35[7] = { 2, 0x64, 0x64, 0x64, 0, 0, 0 };
fix rsb_gravity[9] = { 0x30000, 0x40000, 0x50000, 0x90000, 0xA0000, 0xA0000, 0xE0000,
                       0x1C0000, 0x270000 };
RsbButton rsb_buttons[0x3D];          /* refs / functions / args: see the notes */
RsbButton rsb_grid_buttons[0x37];
long rsb_resfile;
uchar rsb_show_extra;
long rsb_mode;
long rsb_result;
long rsb_dirty;
uchar rsb_sel = 0xFF;
uchar rsb_n1, rsb_n2, rsb_n3;
uchar rsb_nnames;
uchar rsb_pop;
ulong rsb_hint_time;
uchar rsb_hint = 0xFF;
uchar rsb_nregions;
uchar rsb_grid_cur;
short rsb_ent;
long rsb_units;
long rsb_min_squad = 100;
long rsb_last_squad;
ulong rsb_timer;
uchar rsb_timer_on;
uchar rsb_used[0x20];
LGRect rsb_clip;
uchar rsb_grid_save[25];
uchar rsb_list3[15];
uchar rsb_list1[15];
uchar rsb_list2[15];
uchar rsb_grid[25];
uchar rsb_names[0x21];
uchar rsb_side;
uchar rsb_difficulty;
uchar rsb_size;
RsbSlot rsb_slots[5];
#endif

#define FONT  0x164
#define FONT2 0x165
#define BACK  (&tnui_back_canvas)

#ifdef TN_WATCOM_ABI
#define IS_FN(p, f, a) ((void *)(p) == WATCOM_FN(f) || (void *)(p) == (void *)(a))
#else
#define IS_FN(p, f, a) ((void *)(p) == (void *)(f))
#endif

/* a frame drawn unclipped / clipped at (x, y) */
static void ubitmap(long ref, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);

    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * bm->type], (long)bm, x, y);
}

static void bitmap(long ref, long x, long y)
{
    grs_bitmap *bm = RefGet(ref);

    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, x, y);
}

static void fcolor(long c)
{
    grd_canvas->gc.fcolor = c;
}

static void label(long rect, long text, long font)
{
    options_2CAB58(rect, text, font, BACK);
}

/* the text of a type list entry (class 1..3); 0 for another class */
static long type_text(RsbSlot *s, int store)
{
    uchar t;

    if (s->cls == 1)
        t = rsb_list1[s->idx];
    else if (s->cls == 2)
        t = rsb_list2[s->idx - rsb_n1];
    else if (s->cls == 3)
        t = rsb_list3[s->idx - rsb_n1 - rsb_n2];
    else
        return 0;
    if (store)
        s->type = t;
    return (s->cls == 1 ? 0x17E0000 : s->cls == 2 ? 0x17F0000 : 0x1800000) | t;
}

static void set_class(RsbSlot *s)
{
    if (s->idx < rsb_n1)
        s->cls = 1;
    else if (s->idx < rsb_n1 + rsb_n2)
        s->cls = 2;
    else
        s->cls = 3;
}

/* the slot of a control's argument: the bit (of 0x3F) that is the highest set one */
static long slot_of(uchar a)
{
    long i, k = 0;

    for (i = 0; i < 5; i++)
        if (((a & 0x3F) >> i) == 1)
            k = i;
    return k;
}

static void hint(uchar h)
{
    rsb_hint = h;
    rsb_hint_time = tn_ticks + 0xFA;
}

/* ------------------------------------------------------------------ the main page */
void rsb_2CEE3C(void)
{
    rsb_resfile = ResOpenResFile("rescustm.res", 0, 0);
    ResLock(0x972);
    ResLock(0x973);
    ResLock(0x974);
    KeyAddHandler(WATCOM_FN(rsb_2CF25C), 0, 0, 0);
    MouseAddHandler(&tnui_full_rect, 0xFF, WATCOM_FN(rsb_2CF25C));
    rsb_2D0340();
    rsb_2CEF9C();
}

void rsb_2CEEA4(void)
{
    KeyRemoveHandler(WATCOM_FN(rsb_2CF25C), 0);
    MouseRemoveHandler(&tnui_full_rect, 0xFF, WATCOM_FN(rsb_2CF25C));
    ResUnlock(0x972);
    ResUnlock(0x973);
    ResUnlock(0x974);
    ResCloseFile(rsb_resfile);
    rsb_ent = 0;
    game_35ECF2 = 0;
    rsb_timer_on = 0;
    rsb_nnames = 0;
    game_441366 = 0;
    mapd_show_ents = 0;
    rsb_units = 0;
    rsb_timer = 0;
}

void rsb_2CEF28(void)
{
    long i;
    uchar any = 0;

    if (rsb_mode != 0)
        return;
    for (i = 0; i < 5; i++)
        if (rsb_slots[i].count)
            any = 1;
    if (!any)
        rsb_2CFE2C(0x81);
    if (rsb_hint != 0xFF)
        rsb_dirty = 1;
    if (rsb_dirty == 1 && !(rsb_pop & 1)) {
        MouseHide();
        rsb_2CF0C8();
        MouseShow();
        rsb_dirty = 0;
    }
}

void rsb_2CEF9C(void)
{
    uchar *p;
    long i;

    ResLock(0x967);
    rsb_side = 1;
    hud_442F1A = 1;
    game_442F1B = 0x10;
    rsb_difficulty = 5;
    rsb_size = 2;
    game_442F32 = 3;
    rescustm_idx = 0;
    p = RefGet(0x9670000);
    for (i = 0; i < 5; i++, p += 3) {
        rsb_slots[i].cls = p[0];
        rsb_slots[i].type = p[1];
        rsb_slots[i].count = p[2];
        rsb_slots[i].idx = rsb_2CF048(rsb_slots[i].type, rsb_slots[i].cls);
    }
    rsb_2D05C8(rsb_size);
    ResUnlock(0x967);
}

uchar rsb_2CF048(uchar type, uchar cls)
{
    long i;

    if (cls == 1) {
        for (i = 0; i < rsb_n1; i++)
            if (rsb_list1[i] == type)
                return (uchar)i;
    } else if (cls == 2) {
        for (i = 0; i < rsb_n2; i++)
            if (rsb_list2[i] == type)
                return (uchar)i;
    } else if (cls == 3) {
        for (i = 0; i < rsb_n3; i++)
            if (rsb_list3[i] == type)
                return (uchar)i;
    }
    return 0xFF;
}

void rsb_2CF0C8(void)
{
    gr_set_canvas(BACK);
    bitmap(0x9720000, 0, 0);
    rsb_2CF754();
    rsb_2CF1A0();
    rsb_2CF11C();
    GfxBlit(&tnui_full_rect);
}

void rsb_2CF11C(void)
{
    LGRect *r;

    if (tn_ticks > rsb_hint_time) {
        rsb_hint = 0xFF;
        return;
    }
    r = RefGet(0x973001F + rsb_hint);
    ubitmap(rsb_hint % 2 == 0 ? 0x9720016 : 0x9720018, r->ul.x, r->ul.y);
}

void rsb_2CF1A0(void)
{
    rsb_2CF8F0(0x40);
    rsb_2CF9EC(rsb_side);
    rsb_2CFAC0(hud_442F1A);
    rsb_2CFB94(0x41);
    rsb_2CFB94(0x42);
    rsb_2CFB94(0x44);
    rsb_2CFB94(0x48);
    rsb_2CFB94(0x50);
    rsb_2CFE2C(0x41);
    rsb_2CFE2C(0x42);
    rsb_2CFE2C(0x44);
    rsb_2CFE2C(0x48);
    rsb_2CFE2C(0x50);
    rsb_2CFF10(0x40);
    rsb_2D01C4(game_442F32);
    rsb_2D00D8(rsb_size);
    rsb_2CFFEC(0);
    rsb_dirty = 0;
}

/* centre of a rectangle */
static LGPoint centre(LGRect *r)
{
    LGPoint c;

    c.x = r->ul.x + (r->lr.x - r->ul.x) / 2;
    c.y = r->ul.y + (r->lr.y - r->ul.y) / 2;
    return c;
}

/* the open list follows the mouse */
static void popup(LGPoint pt)
{
    if (rsb_pop & 8)
        rsb_2D1BEC(rsb_sel, pt);
    else if (rsb_pop & 4)
        rsb_2D1788(rsb_sel, pt);
    else if (rsb_pop & 0x10)
        rsb_2D21B4(rescustm_idx, pt);
    else
        rsb_2D2600(rsb_difficulty, pt);
}

static void click_sound(void)
{
    SndPlay(0x7CC, 0, 0x28, 1, 0x40, 0x40, 0);
}

static uchar main_key(TnEvent *e, int *handled)
{
    *handled = 1;
    switch (*(ushort *)&e->pad) {
    case 0x4F:
    case 0x6F:
        if (game_paused_menu)
            return 0;
        options_2CA8FC();
        return 1;
    case 0x458:
    case 0x478:
        main_245334();
        return 1;
    case 0x2837:
        pcx_screenshot(0, "nova");
        return 1;
    }
    *handled = 0;
    return 1;
}

uchar rsb_2CF25C(TnEvent *e)
{
    RsbButton *b;
    int handled;
    uchar r;

    if (rsb_mode != 0) {
        if (e->type == 0x100) {
            if (*(ushort *)&e->pad == 0x1B) {
                rsb_2D085C();
                return 1;
            }
            r = main_key(e, &handled);
            if (handled)
                return r;
        } else if (e->type != 1)
            return 1;
        b = rsb_2CF6FC(rsb_grid_buttons, e->pt, 0x37);
        if (!b)
            return 1;
        if (IS_FN(b->fn, rsb_2D0C00, 0x2D0C00) &&
            (rsb_slots[b->arg >> 2].cls == 3 || rsb_slots[b->arg >> 2].cls == 2) && b->arg % 4 >= 2)
            return 1;
        click_sound();
        WATCOM_CALL(b->fn, (long)b->arg);
        return 1;
    }
    switch (e->type) {
    case 0x100:
        if (*(ushort *)&e->pad == 0x1B) {
            rsb_result = 2;
            return 1;
        }
        r = main_key(e, &handled);
        if (handled)
            return r;
        /* fall through */
    case 1:
        b = rsb_2CF6FC(rsb_buttons, e->pt, 0x3D);
        if (!b)
            return 1;
        if (IS_FN(b->fn, rsb_2D1BEC, 0x2D1BEC) || IS_FN(b->fn, rsb_2D1788, 0x2D1788) ||
            IS_FN(b->fn, rsb_2D21B4, 0x2D21B4) || IS_FN(b->fn, rsb_2D2600, 0x2D2600)) {
            MouseHide();
            rsb_pop |= 1;
            if (IS_FN(b->fn, rsb_2D1BEC, 0x2D1BEC))
                rsb_2D1BEC(b->arg, e->pt);
            else if (IS_FN(b->fn, rsb_2D1788, 0x2D1788))
                rsb_2D1788(b->arg, e->pt);
            else if (IS_FN(b->fn, rsb_2D21B4, 0x2D21B4))
                rsb_2D21B4(rescustm_idx, e->pt);
            else
                rsb_2D2600(rsb_difficulty, e->pt);
            MouseConstrain(&rsb_clip);
            GfxBlit(&tnui_full_rect);
            return 1;
        }
        click_sound();
        WATCOM_CALL(b->fn, (long)b->arg);
        return 1;
    case 0x40:
        if (!(rsb_pop & 1))
            return 1;
        popup(e->pt);
        GfxBlit(&tnui_full_rect);
        return 1;
    case 2: {
        LGRect *rr;
        LGPoint c;

        if (!(rsb_pop & 1))
            return 1;
        if (rsb_pop & 8)
            rr = RefGet(0x9730010 + rsb_sel);
        else if (rsb_pop & 4)
            rr = RefGet(0x9730037 + rsb_sel);
        else if (rsb_pop & 0x10)
            rr = RefGet(0x9730002);
        else
            rr = RefGet(0x9730016);
        c = centre(rr);
        MouseClearStencil();
        popup(e->pt);
        GfxBlit(&tnui_full_rect);
        MouseMoveTo(c);
        MouseShow();
        rsb_sel = 0xFF;
        rsb_pop = 0;
        return 1;
    }
    }
    return 1;
}

RsbButton *rsb_2CF6FC(RsbButton *t, LGPoint pt, long n)
{
    long i;

    for (i = 0; i < n; i++, t++) {
        t->rect = *(LGRect *)RefGet(t->ref);
        if (pt.y >= t->rect.ul.y && pt.y < t->rect.lr.y && pt.x >= t->rect.ul.x &&
            pt.x < t->rect.lr.x)
            return t;
    }
    return 0;
}

void rsb_2CF754(void)
{
    LGRect *r;

    fcolor(0xA2);
    label(0x9730000, 0x9740000, FONT2);
    label(0x9730001, 0x9740002, FONT);
    label(0x9730004, 0x974003F, FONT);
    label(0x973000F, 0x9740043, FONT);
    label(0x9730015, 0x9740048, FONT);
    label(0x9730008, 0x9740044, FONT);
    label(0x9730017, 0x9740052, FONT);
    label(0x973003C, 0x974005D, FONT);
    label(0x9730043, 0x974006F, FONT);
    if (rsb_show_extra) {
        r = RefGet(0x9730041);
        bitmap(0x9720022, r->ul.x, r->ul.y);
        label(0x9730042, 0x9740062, FONT);
    }
    label(0x973000C, 0x974005A, FONT);
    label(0x973000D, 0x974005B, FONT);
    label(0x973000E, 0x974005C, FONT);
}

void rsb_2CF8F0(uchar a)
{
    LGRect *r = RefGet(0x9730002);

    if (!(a & 0x40)) {
        if (a & 1) {
            if (++rescustm_idx >= 0xF)
                rescustm_idx = 0;
            rsb_hint = 1;
        } else {
            if (--rescustm_idx == 0xFF)
                rescustm_idx = 0xE;
            rsb_hint = 0;
        }
        rsb_hint_time = tn_ticks + 0xFA;
    }
    fcolor(0xA2);
    ubitmap(0x9720003, r->ul.x, r->ul.y);
    label(0x9730002, 0x9740003 + rescustm_idx, FONT);
    label(0x9730003, 0x9740030 + rescustm_idx, FONT);
    rsb_dirty = 1;
}

/* three radio buttons: refs rect + i, text + i */
static void radio3(uchar a, long rect, long text)
{
    long i;
    LGRect *r;

    gr_set_canvas(BACK);
    for (i = 0; i < 3; i++) {
        r = RefGet(rect + i);
        if ((1 << i) == a) {
            fcolor(0xA2);
            ubitmap(0x972000A, r->ul.x, r->ul.y);
        } else {
            fcolor(0x24);
            ubitmap(0x9720008, r->ul.x, r->ul.y);
        }
        label(rect + i, text + i, FONT);
    }
}

void rsb_2CF9EC(uchar a)
{
    radio3(a, 0x9730005, 0x9740040);
    rsb_side = a;
    rsb_dirty = 1;
}

void rsb_2CFAC0(uchar a)
{
    radio3(a, 0x9730009, 0x9740045);
    hud_442F1A = a;
    rsb_dirty = 1;
}

void rsb_2CFB94(uchar a)
{
    long k = slot_of(a), text;
    RsbSlot *s = &rsb_slots[k];
    LGRect *r = RefGet(0x9730010 + k);

    if (!(a & 0x40)) {
        if (a & 0x80) {
            s->idx++;
            if (s->idx > rsb_n1 + rsb_n2 + rsb_n3 - 1)
                s->idx = 0;
            set_class(s);
            rsb_hint = (uchar)(k * 4 + 3);
        } else {
            if (--s->idx == 0xFF)
                s->idx = (uchar)(rsb_n1 + rsb_n2 + rsb_n3 - 1);
            set_class(s);
            rsb_hint = (uchar)(k * 4 + 2);
        }
        rsb_hint_time = tn_ticks + 0xFA;
    }
    fcolor(0xA2);
    ubitmap(0x9720005, r->ul.x, r->ul.y);
    text = type_text(s, 1);
    if (text)
        label(0x9730010 + k, text, FONT);
    rsb_2D05C8(rsb_size);
    rsb_dirty = 1;
}

void rsb_2CFE2C(uchar a)
{
    long k = slot_of(a);
    RsbSlot *s = &rsb_slots[k];
    LGRect *r = RefGet(0x9730037 + k);
    char buf[4];

    if (!(a & 0x40)) {
        if (a & 0x80) {
            if (++s->count >= 6)
                s->count = 6;
            rsb_hint = (uchar)(k * 4 + 5);
        } else {
            if (--s->count == 0xFF)
                s->count = 0;
            rsb_hint = (uchar)(k * 4 + 4);
        }
        rsb_hint_time = tn_ticks + 0xFA;
    }
    fcolor(0xA2);
    ltoa(s->count, buf, 10);
    options_2CAAC0(r, buf, FONT, BACK);
    rsb_dirty = 1;
}

void rsb_2CFF10(uchar a)
{
    LGRect *r = RefGet(0x9730016);

    if (!(a & 0x40)) {
        if (a == 1) {
            rsb_difficulty += a;
            if (rsb_difficulty >= 9)
                rsb_difficulty = 0;
            rsb_hint = 0x17;
        } else {
            if (--rsb_difficulty == 0xFF)
                rsb_difficulty = 8;
            rsb_hint = 0x16;
        }
        rsb_hint_time = tn_ticks + 0xFA;
    }
    fcolor(0xA2);
    ubitmap(0x9720003, r->ul.x, r->ul.y);
    label(0x9730016, 0x9740049 + rsb_difficulty, FONT);
    rsb_dirty = 1;
}

void rsb_2CFFEC(uchar a)
{
    long i;
    LGRect *r;

    if (a & game_442F1B)
        game_442F1B &= ~a;
    else
        game_442F1B |= a;
    gr_set_canvas(BACK);
    for (i = 0; i < 7; i++) {
        r = RefGet(0x9730018 + i);
        if ((1 << i) & game_442F1B) {
            fcolor(0xA2);
            ubitmap(0x972000A, r->ul.x, r->ul.y);
        } else {
            fcolor(0x24);
            ubitmap(0x9720008, r->ul.x, r->ul.y);
        }
        label(0x9730018 + i, 0x9740053 + i, FONT);
    }
    rsb_dirty = 1;
}

void rsb_2D00D8(uchar a)
{
    long i;
    LGRect *r;

    gr_set_canvas(BACK);
    rsb_show_extra = a == 8;
    for (i = 0; i < 4; i++) {
        r = RefGet(0x973003D + i);
        if ((1 << i) == a) {
            fcolor(0xA2);
            ubitmap(0x972000A, r->ul.x, r->ul.y);
        } else {
            fcolor(0x24);
            ubitmap(0x9720008, r->ul.x, r->ul.y);
        }
        label(0x973003D + i, 0x974005E + i, FONT);
    }
    rsb_size = a;
    rsb_2D05C8(a);
    rsb_dirty = 1;
}

void rsb_2D01C4(uchar a)
{
    long i;
    LGRect *r;
    char buf[4];

    gr_set_canvas(BACK);
    for (i = 0; i < 4; i++) {
        r = RefGet(0x9730044 + i);
        if (i == a) {
            fcolor(0xA2);
            ubitmap(0x9720020, r->ul.x, r->ul.y);
        } else {
            fcolor(0x24);
            ubitmap(0x972001C, r->ul.x, r->ul.y);
        }
        ltoa(i, buf, 10);
        options_2CAAC0(r, buf, FONT, BACK);
    }
    game_442F32 = a;
    rsb_dirty = 1;
}

void rsb_2D02A8(void)
{
    ResLock(0x960);
    rsb_nregions = ((uchar *)RefGet(0x9600000))[rescustm_idx];
    ResUnlock(0x960);
    if (rsb_side & 1)
        game_359708 = 0x2A;
    else if (rsb_side & 2)
        game_359708 = 0x2C;
    else
        game_359708 = 0x2B;
    camplist_2C8E78();
    rsb_2CEEA4();
    rsb_result = 1;
}

void rsb_2D0324(void)
{
    rsb_result = 2;
}

void rsb_2D0330(void)
{
    if (main_245334())
        rsb_2CEEA4();
}

/* a byte count from resource id, entry 0 */
static uchar res_count(Id id)
{
    uchar n;

    ResLock(id);
    n = *(uchar *)RefGet((Ref)id << 16);
    ResUnlock(id);
    return n;
}

static void res_list(Id id, uchar *dst, uchar n)
{
    uchar *p;
    long i;

    ResLock(id);
    p = RefGet((Ref)id << 16);
    for (i = 0; i < n; i++)
        dst[i] = p[i];
    ResUnlock(id);
}

void rsb_2D0340(void)
{
    uchar *p;
    long i;

    rsb_n1 = res_count(0x961);
    rsb_n2 = res_count(0x963);
    rsb_n3 = res_count(0x965);
    res_list(0x962, rsb_list1, rsb_n1);
    res_list(0x964, rsb_list2, rsb_n2);
    res_list(0x966, rsb_list3, rsb_n3);
    ResLock(0x968);
    p = RefGet(0x9680000);
    for (i = 0; i < 0x21; i++)
        if (p[i])
            rsb_names[rsb_nnames++] = (uchar)i;
    ResUnlock(0x968);
}

long rsb_2D04A4(void)
{
    long i;

    for (i = 0; i < 0x20; i++)
        if (*(long *)&game_4413D0[i * 8] == 0 && *(long *)&game_4413D0[i * 8 + 4] == 0)
            return i;
    return 0x1F;
}

/* ------------------------------------------------------------------ the weapon grid page */
static void grid_copy(uchar *dst, const uchar *src)
{
    long r, j;

    for (r = 0; r < 5; r++)
        for (j = 0; j < 4; j++)
            dst[r * 5 + j] = src[r * 5 + j];
}

void rsb_2D04D4(void)
{
    if (!(rsb_size & 8))
        return;
    gr_set_canvas(BACK);
    grid_copy(rsb_grid_save, rsb_grid);
    rsb_mode = 1;
    rsb_dirty = 1;
}

void rsb_2D05C8(uchar a)
{
    static const Id ids[9] = { 0x969, 0x96C, 0x96F, 0x96A, 0x96D, 0x970, 0x96B, 0x96E, 0x971 };
    uchar *p1 = 0, *p2 = 0, *p3 = 0;
    long i, j;

    for (i = 0; i < 9; i++)
        ResLock(ids[i]);
    if (a == 1 || a == 2 || a == 4) {
        long k = a == 1 ? 0 : a == 2 ? 1 : 2;

        p1 = RefGet((Ref)(0x969 + k) << 16);
        p2 = RefGet((Ref)(0x96C + k) << 16);
        p3 = RefGet((Ref)(0x96F + k) << 16);
    }
    for (i = 0; i < 5; i++) {
        RsbSlot *s = &rsb_slots[i];
        uchar *row = &rsb_grid[i * 5];

        if (s->cls == 1) {
            row[4] = 4;
            if (!(rsb_size & 8))
                for (j = 0; j < 4; j++)
                    row[j] = p1[s->idx * 4 + j];
        } else if (s->cls == 2) {
            row[4] = 2;
            if (!(rsb_size & 8))
                for (j = 0; j < 4; j++)
                    row[j] = j < 2 ? p2[((long)s->idx - rsb_n1) * 2 + j] : 0xFF;
        } else if (s->cls == 3) {
            row[4] = 2;
            if (!(rsb_size & 8))
                for (j = 0; j < 4; j++)
                    row[j] = j < 2 ? p3[((long)s->idx - (rsb_n1 + rsb_n2)) * 2 + j] : 0xFF;
        }
    }
    for (i = 0; i < 9; i++)
        ResUnlock(ids[i]);
}

void rsb_2D085C(void)
{
    grid_copy(rsb_grid, rsb_grid_save);
    rsb_mode = 0;
    rsb_dirty = 1;
}

void rsb_2D093C(void)
{
    rsb_mode = 0;
    rsb_dirty = 1;
}

void rsb_2D0954(void)
{
    if (rsb_mode == 1 && rsb_dirty == 1) {
        MouseHide();
        rsb_2D0984();
        MouseShow();
        rsb_dirty = 0;
    }
}

void rsb_2D0984(void)
{
    uchar v;

    gr_set_canvas(BACK);
    bitmap(0x9720021, 0, 0);
    rsb_2D0A20();
    v = rsb_grid_cur;
    rsb_2D0C00(v);
    rsb_2D0E68(rsb_grid[(v >> 2) * 5 + v % 4]);
    GfxBlit(&tnui_full_rect);
}

void rsb_2D0A20(void)
{
    long i, text;

    fcolor(0xA2);
    label(0x9730048, 0x9740069, FONT2);
    label(0x973004E, 0x974006A, FONT2);
    for (i = 0x9730066; i != 0x973006B; i++)
        label(i, 0x974006B, FONT);
    label(0x9730051, 0x974005C, FONT);
    label(0x9730050, 0x974006D, FONT);
    label(0x973004F, 0x974006C, FONT);
    for (i = 0; i < 5; i++) {
        text = type_text(&rsb_slots[i], 1);
        if (text)
            label(0x9730049 + i, text, FONT);
    }
}

void rsb_2D0BA8(void)
{
    uchar v = rsb_grid_cur;

    rsb_2D0C00(v);
    rsb_2D0E68(rsb_grid[(v >> 2) * 5 + v % 4]);
}

void rsb_2D0C00(uchar v)
{
    long row = v >> 2, col = v % 4, r, j;
    LGRect *sel = RefGet(0x9730052 + v), *rr;
    uchar c;

    rsb_grid_cur = v;
    gr_set_canvas(BACK);
    fcolor(0x24);
    for (r = 0; r < 5; r++) {
        uchar cls = rsb_slots[r].cls;

        for (j = 0; j < 4; j++) {
            uchar *cell = &rsb_grid[r * 5 + j];
            long ref = 0x9730052 + r * 4 + j;

            if (*cell == 0xFF)
                *cell = 0;
            if ((cls == 3 || cls == 2) && j >= 2) {
                rr = RefGet(ref);
                bitmap(0x9720025, (short)(rr->ul.x - 1), rr->ul.y);
            } else if (*cell == 0)
                label(ref, 0x974006E, FONT);
            else
                label(ref, 0x16E0000 + rsb_names[*cell], FONT);
        }
    }
    fcolor(0xA2);
    c = rsb_grid[row * 5 + col];
    bitmap(0x9720024, (short)(sel->ul.x - 1), sel->ul.y);
    if (c == 0)
        label(0x9730052 + v, 0x974006E, FONT);
    else
        label(0x9730052 + v, 0x16E0000 + rsb_names[c], FONT);
    rsb_dirty = 1;
}

void rsb_2D0E00(uchar row)
{
    uchar b0 = rsb_grid[row * 5], b1 = rsb_grid[row * 5 + 1];
    uchar b2 = rsb_grid[row * 5 + 2], b3 = rsb_grid[row * 5 + 3];
    long r;

    for (r = 0; r < 5; r++) {
        rsb_grid[r * 5] = b0;
        rsb_grid[r * 5 + 1] = b1;
        rsb_grid[r * 5 + 2] = b2;       /* (the original's test of these is always true) */
        rsb_grid[r * 5 + 3] = b3;
    }
    rsb_dirty = 1;
}

void rsb_2D0E68(uchar w)
{
    long i, ref;
    LGRect *rr;
    char *s;

    rsb_grid[(rsb_grid_cur >> 2) * 5 + rsb_grid_cur % 4] = w;
    for (i = 0, ref = 0x973006B; i < rsb_nnames; i++, ref++) {
        if (i == w) {
            rr = RefGet(ref);
            fcolor(0xA2);
            bitmap(0x9720024, (short)(rr->ul.x - 1), rr->ul.y);
        } else
            fcolor(0x24);
        s = RefGet(0x16E0000 + rsb_names[i]);
        if (strlen(s) == 0)
            label(ref, 0x974006E, FONT);
        else
            label(ref, 0x16E0000 + rsb_names[i], FONT);
    }
    rsb_dirty = 1;
}

/* ------------------------------------------------------------------ the scenario generator */
static fix fmul(fix a, fix b)
{
    return (fix)(((long long)a * b) >> 16);
}

/* an object descriptor with ent and position set: the rest stays as it is (see the quirks) */
static void place(GridObjDesc *d, short ent, fix x, fix y, fix z)
{
    d->ent = ent;
    d->x = x;
    d->y = y;
    d->z = z;
    interact_297400(d, game_entities[ent].cls, game_entities[ent].type);
    grid_29582C(d);
}

static ulong pt_of(fix x, fix y)
{
    return (ushort)(x >> 16) | (ulong)(ushort)(y >> 16) << 16;
}

static GridObjDesc rsb_desc;    /* zeroed: the original's descriptors are uninitialised stack */

void rsb_2D0F90(void)
{
    uchar t1[14], t2[7], t3[7];
    long n, i, pos, step;
    fix fx, fy, x, y;
    uchar sq = 0;
    short state0 = *(short *)tnm_state_ptr;

    game_rnd_35B704.f_Seed(&game_rnd_35B704, (long)state0);
    lg_memset(rsb_used, 0, 0, 0x20);
    for (n = 0; n < 5; n++) {
        RsbSlot *s = &rsb_slots[n];
        uchar *row = &rsb_grid[n * 5];

        pos = RndRange(&game_rnd_35B704, 1, rsb_nregions - 1);
        if (rsb_used[pos])
            for (i = 1; i < rsb_nregions; i++)
                if (!rsb_used[i]) {
                    pos = i;
                    break;
                }
        fx = *(fix *)&game_4413D0[pos * 8];
        fy = *(fix *)&game_4413D0[pos * 8 + 4];
        rsb_used[pos] = 1;
        sq = aiperc_253AE8(0, pt_of(fx, fy), 2);
        if (sq < rsb_min_squad)
            rsb_min_squad = sq;
        if (n == 4 && (rsb_side & 2)) {
            uchar d[7] = { 1, 0x64, 0x64, 0x64, 0, 0, 0 };
            short slot;

            damage_27CF30(&rsb_ent, &slot, 0, 0x3F, d, 0);
            place(&rsb_desc, rsb_ent, fx, fy, aircraft_284A6C(rsb_ent, fx, fy) + 0x1A000);
        }
        for (i = 0; i < s->count; i++) {
            ushort ang;

            step = 0x168 / s->count;
            memcpy(t2, rsb_2CEE20, 7);
            memcpy(t1, rsb_2CEE27, 14);
            rsb_units++;
            memcpy(t3, rsb_2CEE35, 7);
            if (game_442F1B & 0x20) {
                t1[2] = 0;
                t2[1] = 0;
                t3[1] = 0;
            }
            if (i % 6 == 0 && i != 0) {
                sq = aiperc_253AE8(0, pt_of(fx, fy), 2);
                if (sq < rsb_min_squad)
                    rsb_min_squad = sq;
                rsb_units++;
            }
            ang = (ushort)(((step * i) << 15) / 0xB4);
            if (s->cls == 1) {
                t1[8] = rsb_names[row[0]];
                t1[9] = rsb_names[row[1]];
                t1[10] = rsb_names[row[2]];
                t1[11] = rsb_names[row[3]];
                x = fx + fmul(0x20000, fix_cos(ang));
                y = fy + fmul(0x20000, fix_sin(ang));
                ailow_24B8A0(s->type, sq, x, y, t1);
            } else if (s->cls == 2) {
                t2[4] = rsb_names[row[0]];
                t2[5] = rsb_names[row[1]];
                x = fx + fmul(0x30000, fix_cos(ang));
                y = fy + fmul(0x30000, fix_sin(ang));
                ailow_24B8DC(s->type, sq, x, y, t2);
            } else if (s->cls == 3) {
                short a, e;

                t3[4] = rsb_names[row[0]];
                t3[5] = rsb_names[row[1]];
                x = fx + fmul(0x20000, fix_cos(ang));
                y = fy + fmul(0x20000, fix_sin(ang));
                a = ailow_24B924(s->type, sq, x, y, t3);
                e = (short)(*(short *)game_cls3[a] << 1) >> 1;
                game_entities[e].z = aircraft_284A6C(e, x, y) + 0x10000;
                place(&rsb_desc, e, game_entities[e].x, game_entities[e].y, game_entities[e].z);
                physics_2D42E0(e);
            } else
                return;
            if (game_442F1B & 0x40)
                game_squads[sq].f002 = 1;
        }
    }
    rsb_last_squad = sq;
    if (rsb_side & 4) {
        uchar d[7] = { 1, 0x64, 0x64, 0x64, 0, 0, 0 };
        fix x0 = *(fix *)&game_4413D0[0], y0 = *(fix *)&game_4413D0[4];
        schar side = (schar)game_brains[game_entities[game_player_entity].f21].f005;
        uchar q = aiperc_253AE8(0, pt_of(x0, y0), side);

        if (q) {
            short r = AILowInit(4, 0x3F, q, x0, y0, d);

            if (r)
                rsb_ent = (short)(*(short *)game_cls4[r] << 1) >> 1;
        }
        place(&rsb_desc, rsb_ent, x0, y0, aircraft_284A6C(rsb_ent, x0, y0) + 0x1A000);
    }
}

/* ------------------------------------------------------------------ drop-down lists */
/* shade the screen (every other line) */
static void shade(void)
{
    long y;

    fcolor(0);
    for (y = 0; y < 0x1E0; y += 2)
        gen_hline(0, (short)y, 0x27F);
}

/* the first draw of a list: shade, put the mouse on the current entry, play the sound */
static void list_open(LGRect *rect, long top, long cur, LGPoint *pt)
{
    LGPoint c;

    shade();
    rect->ul.y = (short)(top + cur * 20);
    rect->lr.y = (short)(top + cur * 20 + 20);
    c = centre(rect);
    MouseMoveTo(c);
    *pt = c;
    rsb_pop |= 2;
    SndPlay(0x791, 0, 0x28, 1, 0x40, 0x40, 0);
}

/* one entry: selected when pt is on it */
static int entry(LGRect *rect, long x, long xl, long xr, long y, long on_bm, long off_bm,
                 LGPoint pt)
{
    int on = pt.y >= y && pt.y < y + 20;

    if (on) {
        fcolor(0xA2);
        ubitmap(on_bm, x, (short)y);
    } else {
        fcolor(0x24);
        ubitmap(off_bm, x, (short)y);
    }
    ubitmap(0x972000D, xl, (short)y);
    ubitmap(0x972000E, xr, (short)y);
    rect->ul.y = (short)y;
    rect->lr.y = (short)(y + 20);
    return on;
}

static void list_done(LGRect *r, long top, long height, uchar flag)
{
    rsb_clip.ul.x = r->ul.x;
    rsb_clip.lr.x = r->lr.x;
    rsb_clip.ul.y = (short)top;
    rsb_clip.lr.y = (short)(top + height);
    rsb_dirty = 1;
    rsb_pop |= flag;
}

void rsb_2D1788(uchar k, LGPoint pt)
{
    LGRect *r = RefGet(0x9730037 + k), rect;
    long top, i, x, xl;
    char buf[8];

    if (k == 0xFF)
        return;
    {
        long t = r->ul.y - 2;

        if ((tnui_full_rect.lr.y - t) / 20 * 20 + t > 0x1E0)
            return;
    }
    gr_set_canvas(BACK);
    top = r->ul.y - 2 - (r->ul.y - 7) / 20 * 20;
    rect.ul.x = r->ul.x;
    rect.lr.x = r->lr.x;
    if (!(rsb_pop & 2))
        list_open(&rect, top, rsb_slots[k].count, &pt);
    x = r->ul.x;
    xl = (short)(x - 0xD);
    ubitmap(0x9720011, xl, (short)(top - 2));
    for (i = 0; i <= 6; i++) {
        int on = entry(&rect, (short)x, xl, r->lr.x, top + i * 20, 0x972001E, 0x972001A, pt);

        ltoa(i, buf, 10);
        options_2CAAC0(&rect, buf, FONT, BACK);
        if (on)
            rsb_slots[k].count = (uchar)i;
    }
    ubitmap(0x9720010, (short)(x - 0xD), (short)(top + 0x75));
    list_done(r, top, 0x8C, 4);
    rsb_sel = k;
}

void rsb_2D1BEC(uchar k, LGPoint pt)
{
    LGRect *r = RefGet(0x9730010 + k), rect;
    long top, i, n, x, xl;

    if (k == 0xFF)
        return;
    {
        long t = r->ul.y - 2;

        if ((tnui_full_rect.lr.y - t) / 20 * 20 + t > 0x1E0)
            return;
    }
    gr_set_canvas(BACK);
    top = r->ul.y - 2 - (r->ul.y - 7) / 20 * 20;
    rect.ul.x = r->ul.x;
    rect.lr.x = r->lr.x;
    if (!(rsb_pop & 2))
        list_open(&rect, top, rsb_slots[k].idx, &pt);
    x = r->ul.x;
    xl = (short)(x - 0xD);
    ubitmap(0x972000C, xl, (short)(top - 2));
    for (i = 0; i < rsb_n1 + rsb_n2 + rsb_n3; i++) {
        int on = entry(&rect, (short)x, xl, r->lr.x, top + i * 20, 0x972001D, 0x9720019, pt);
        long text;
        uchar cls;

        if (i < rsb_n1) {
            text = 0x17E0000 | rsb_list1[i];
            cls = 1;
        } else if (i < rsb_n1 + rsb_n2) {
            text = 0x17F0000 | rsb_list2[i - rsb_n1];
            cls = 2;
        } else {
            text = 0x1800000 | rsb_list3[i - rsb_n1 - rsb_n2];
            cls = 3;
        }
        options_2CAAC0(&rect, RefGet(text), FONT, BACK);
        if (on) {
            rsb_slots[k].cls = cls;
            rsb_slots[k].idx = (uchar)i;
        }
    }
    n = rsb_n1 + rsb_n2 + rsb_n3;
    ubitmap(0x972000F, xl, (short)((n - 1) * 20 + top - 3));
    list_done(r, top, n * 20, 8);
    rsb_sel = k;
}

void rsb_2D21B4(uchar a, LGPoint pt)
{
    LGRect *r = RefGet(0x9730002), rect;
    long top, i, x, xl;

    {
        long t = r->ul.y - 2;

        if ((tnui_full_rect.lr.y - t) / 20 * 20 + t > 0x1E0)
            return;
    }
    gr_set_canvas(BACK);
    top = r->ul.y - 2 - (r->ul.y - 7) / 20 * 20;
    rect.ul.x = r->ul.x;
    rect.lr.x = r->lr.x;
    if (!(rsb_pop & 2))
        list_open(&rect, top, a, &pt);
    x = r->ul.x;
    xl = (short)(x - 0xD);
    ubitmap(0x9720012, xl, (short)(top - 2));
    for (i = 0; i < 0xF; i++) {
        int on = entry(&rect, (short)x, xl, r->lr.x, top + i * 20, 0x972001F, 0x972001B, pt);

        options_2CAAC0(&rect, RefGet(0x9740003 + i), FONT, BACK);
        if (on)
            rescustm_idx = (uchar)i;
    }
    ubitmap(0x972000B, xl, (short)(top + 0x115));
    list_done(r, top, 0x12C, 0x10);
}

void rsb_2D2600(uchar a, LGPoint pt)
{
    LGRect *r = RefGet(0x9730016), rect;
    long top, i, x, xl;

    top = r->ul.y - 2;
    if ((tnui_full_rect.lr.y - top) / 20 * 20 + top > 0x1E0)
        return;
    gr_set_canvas(BACK);
    rect.ul.x = r->ul.x;
    rect.lr.x = r->lr.x;
    if (!(rsb_pop & 2))
        list_open(&rect, top, a, &pt);
    x = r->ul.x;
    xl = (short)(x - 0xD);
    ubitmap(0x9720009, xl, (short)(top - 2));
    for (i = 0; i < 9; i++) {
        int on = entry(&rect, (short)x, xl, r->lr.x, top + i * 20, 0x972001F, 0x972001B, pt);

        options_2CAAC0(&rect, RefGet(0x9740049 + i), FONT, BACK);
        if (on)
            rsb_difficulty = (uchar)i;
    }
    ubitmap(0x9720007, xl, (short)(top + 0x9D));
    list_done(r, top, 0xB4, 0x20);
}

void rsb_2D2A1C(void)
{
    shade();
}

/* ------------------------------------------------------------------ mission hooks */
void rsb_2D2A4C(void)
{
    weapphys_4463D8 = rsb_gravity[rsb_difficulty];
    game_441366 = (game_442F1B & 1) != 0;
    if (game_442F1B & 8)
        *(long *)((uchar *)&game_pba[game_player_slot] + 0xC6) = 0xC80000;
    mapd_show_ents = (game_442F1B & 0x10) != 0;
    if (hud_442F1A & 2) {
        hud_44244B = 2;
        hud_44244C = 0x32;
    } else if (hud_442F1A & 4) {
        hud_44244B = 1;
        hud_44244C = 0x32;
    } else {
        hud_44244B = 0;
        hud_44244C = 0;
    }
    weather_2AFD4C((schar)(hud_44244B - 1), hud_44244C);
}

void rsb_2D2B04(void)
{
    gr_set_canvas(BACK);
    fcolor(0);
    WATCOM_CALL(grd_canvas_table[16], 0, 0, 0x27F, 0x1DF);
    GfxBlit(&tnui_full_rect);
}

uchar rsb_2D2B44(void)
{
    return (game_35ECF2 & 1) != 0;
}

void rsb_2D2B54(void)
{
    rsb_ent = 0;
    game_35ECF2 = 0;
    rsb_timer = 0;
    rsb_timer_on = 0;
    rsb_nnames = 0;
    game_441366 = 0;
    mapd_show_ents = 0;
    rsb_units = 0;
}

uchar rsb_2D2B94(long t)
{
    if (rsb_timer_on & 1)
        return game_time > rsb_timer;
    rsb_timer_on |= 1;
    rsb_timer = game_time + t;
    return 0;
}
