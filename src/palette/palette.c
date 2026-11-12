/* palette.c -- x:\prj\lib\src\palette\palette.c (Terra Nova v1.09): palette effects
 * (fades/shifts, single-colour cycles, colour banks).  Decompiled from __FF.EXE.
 *
 *   0x2E7220 palette_advance_all_fx    0x2E72C4 palette_advance_effect
 *   0x2E7720 palette_install_effect    0x2E78BC palette_remove_effect
 *   0x2E7900 palette_freeze_effect     0x2E794C palette_unfreeze_effect
 *   0x2E7998 palette_query_effect      0x2E79B8 palette_change_delay
 *   0x2E79E0 palette_set_rate          0x2E79E8 palette_initialize
 *   0x2E7A50 palette_shutdown          0x2E7A5C palette_init_smap
 *
 * Terra Nova links an older revision than System Shock's palette.c (1.34):
 *  - palette_advance_all_fx does not read the palette and never does the final CBANK gr_set_pal;
 *  - a SHIFT step sends every changed colour with its own gr_set_pal(i, 1, c_off_stack);
 *  - a CYCLE wraps when entry_n >= range (Shock: >);
 *  - CBANK rotates the colours itself (two gr_get_pal + one gr_set_pal);
 *  - palette_swap_shadow is compiled out (empty macro), only the search loop before it remains.
 * Quirks kept: palette_init_smap stores the deltas at Delta_Cmap[3*i] while the SHIFT step
 * reads them at Delta_Cmap[3*(i-first)] (same as Shock); the inlined fix_div faults on
 * overflow (num_steps 0).  Effect ids are signed chars (ids >= 0x80 index backwards).
 */
#include "support.h"
#include "sup_b.h"
#include "fix.h"
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
PAL_TABLE_ENTRY *pal_fx_table;          /* Palette_Effects_Table */
short pal_table_size;                   /* Palette_Effects_Table_Size */
long pal_timestamp;                     /* last_timestamp */
short pal_shift_count;                  /* num_active_effects */
uchar pal_shift_count2;                 /* num_installed_shifts */
long pal_remainder;                     /* ts_remainder (static in palette_advance_all_fx) */
short pal_rate;                         /* timestamps_per_step */
long pal_smap_delta[768];               /* Delta_Cmap */
long pal_smap_curr[768];                /* Shadow_Fixed_Cmap */
uchar c_off_stack[3];
uchar local_smap[768];
#endif

/* Shock names */
#define pal_num_active_effects pal_shift_count
#define num_installed_shifts pal_shift_count2
#define pal_ts_remainder pal_remainder
#define timestamps_per_step pal_rate
#define Delta_Cmap pal_smap_delta
#define Shadow_Fixed_Cmap pal_smap_curr

#define PT pal_fx_table

void palette_advance_all_fx(long timestamp)
{
    int i, time_diff, steps_to_do;

    if (timestamps_per_step <= 0)
        return;
    time_diff = timestamp - pal_timestamp;
    pal_timestamp = timestamp;
    steps_to_do = time_diff / timestamps_per_step;
    pal_ts_remainder += time_diff % timestamps_per_step;
    if (pal_ts_remainder > timestamps_per_step) {
        pal_ts_remainder -= timestamps_per_step;
        steps_to_do++;
    }
    if (steps_to_do == 0)
        return;
    for (i = 0; i < pal_table_size; i++)
        if (PT[i].status == ACTIVE)
            palette_advance_effect((uchar)i, steps_to_do);
}

void palette_advance_effect(uchar uid, int steps)
{
    signed char id = (signed char)uid;
    PAL_TABLE_ENTRY *e;
    short e1, en, er, i, j, s;
    uchar *t, *v;
    int add_to_color, add_to_delay, a;
    fix x;

    if (PT[id].mode == STEADY)
        steps = 1;
    en = PT[id].entry_n;
    er = PT[id].range;
    t = PT[id].from_pal;
    v = PT[id].to_pal;
    e1 = PT[id].entry_1;

    add_to_color = steps / (PT[id].dsteps + 1);
    add_to_delay = steps % (PT[id].dsteps + 1);
    e = &PT[id];
    e->curr_dstep += add_to_delay;
    if (e->curr_dstep > e->dsteps) {
        e->curr_dstep -= e->dsteps;
        add_to_color++;
    }
    if (PT[id].effect != SHIFT)
        add_to_color %= er;
    else if (PT[id].curr_stage + add_to_color > 256)
        add_to_color %= 256;
    if (add_to_color == 0)
        return;

    e = &PT[id];
    switch (e->effect) {
    case SHIFT:
        if (e->curr_stage == -1 && grd_pal + 3 * e1 != t + 3 * e1) {
            gr_set_pal(e1, er, t + 3 * e1);
            PT[id].curr_stage = 0;
        }
        e = &PT[id];
        e->curr_stage += add_to_color;
        if (e->curr_stage >= e->stages) {
            gr_set_pal(e1, er, v + 3 * e1);
            /* palette_remove_effect(id), inlined */
            e = &PT[id];
            if (e->status == EMPTY)
                return;
            e->status = EMPTY;
            pal_num_active_effects--;
            if (e->effect == SHIFT)
                num_installed_shifts--;
            return;
        }
        x = add_to_color << 16;
        for (i = e1, j = 0; i <= en; i++, j++) {
            Shadow_Fixed_Cmap[3 * i] += fix_mul(x, Delta_Cmap[3 * j]);
            Shadow_Fixed_Cmap[3 * i + 1] += fix_mul(x, Delta_Cmap[3 * j + 1]);
            Shadow_Fixed_Cmap[3 * i + 2] += fix_mul(x, Delta_Cmap[3 * j + 2]);
            c_off_stack[0] = (uchar)((Shadow_Fixed_Cmap[3 * i] + 0x8000) >> 16);
            c_off_stack[1] = (uchar)((Shadow_Fixed_Cmap[3 * i + 1] + 0x8000) >> 16);
            c_off_stack[2] = (uchar)((Shadow_Fixed_Cmap[3 * i + 2] + 0x8000) >> 16);
            gr_set_pal(i, 1, c_off_stack);
        }
        break;

    case CYCLE:
        /* for cycles entry_n is the current colour, entry_1 the colormap index */
        s = e->entry_n + add_to_color;
        e->entry_n = s;
        if (s >= er)
            e->entry_n = s - er;
        s = PT[id].entry_n;
        if (s < 0)
            break;
        gr_set_pal(e1, 1, t + 3 * s);
        Shadow_Fixed_Cmap[3 * e1] = t[3 * s] << 16;
        Shadow_Fixed_Cmap[3 * e1 + 1] = t[3 * s + 1] << 16;
        Shadow_Fixed_Cmap[3 * e1 + 2] = t[3 * s + 2] << 16;
        break;

    case CBANK:
        a = add_to_color;
        gr_get_pal(e1 + a, er - a, local_smap + 3 * e1);
        gr_get_pal(e1, a, local_smap + 3 * (er + e1 - a));
        gr_set_pal(e1, er, local_smap + 3 * e1);
        if (num_installed_shifts == 0 || num_installed_shifts != 1)
            break;              /* palette_swap_shadow(e1, er, a) compiled out */
        for (i = 0; i < pal_table_size; i++)
            if (PT[i].effect == SHIFT)
                break;          /* the swap that would follow is compiled out too */
        break;
    }
}

uchar palette_install_effect(PAL_TYPE type, PAL_MODE mode, short b1, short b2,
                             short b3, short b4, uchar *ptr1, uchar *ptr2)
{
    signed char i;
    PAL_TABLE_ENTRY *e;

    for (i = 0; i < pal_table_size; i++)
        if (PT[i].status == EMPTY)
            break;
    if (i == pal_table_size)
        return 0xFF;

    e = &PT[i];
    e->effect = type;
    e->mode = mode;
    e->status = ACTIVE;
    switch (type) {
    case SHIFT:
        e->curr_dstep = 0;
        e->curr_stage = -1;
        e->entry_1 = b1;
        e->range = b2 - b1 + 1;
        e->entry_n = b2;
        e->from_pal = ptr1;
        e->to_pal = ptr2;
        e->dsteps = b3;
        e->stages = b4;
        palette_init_smap(b1, b2, ptr1, ptr2, b4);
        num_installed_shifts++;
        break;
    case CYCLE:
        e->entry_n = -1;        /* not begun yet */
        e->curr_dstep = 0;
        e->entry_1 = b1;
        e->range = b2;          /* number of colours */
        e->from_pal = ptr1;
        e->dsteps = b3;
        break;
    case CBANK:
        e->curr_dstep = 0;
        e->entry_1 = b1;
        e->range = b2 - b1 + 1;
        e->entry_n = b2;
        e->dsteps = b3;
        break;
    }
    pal_num_active_effects++;
    return i;
}

int palette_remove_effect(uchar id)
{
    PAL_TABLE_ENTRY *e = &PT[(signed char)id];

    if (e->status == EMPTY)
        return 6;               /* ERR_NOEFFECT */
    pal_num_active_effects--;
    e->status = EMPTY;
    if (e->effect == SHIFT)
        num_installed_shifts--;
    return 0;
}

int palette_freeze_effect(uchar id)
{
    PAL_TABLE_ENTRY *e = &PT[(signed char)id];

    if (e->status == EMPTY)
        return 4;               /* ERR_RANGE */
    if (e->status == FROZEN)
        return 6;               /* ERR_NOEFFECT */
    e->status = FROZEN;
    pal_num_active_effects--;
    return 0;
}

int palette_unfreeze_effect(uchar id)
{
    PAL_TABLE_ENTRY *e = &PT[(signed char)id];

    if (e->status == EMPTY)
        return 4;
    if (e->status != FROZEN)
        return 6;
    e->status = ACTIVE;
    pal_num_active_effects++;
    return 0;
}

PAL_STATUS palette_query_effect(uchar id)
{
    return PT[(signed char)id].status;
}

void palette_change_delay(uchar id, short delay)
{
    if (PT[(signed char)id].status != EMPTY)
        PT[(signed char)id].dsteps = delay;
}

void palette_set_rate(short ts)
{
    timestamps_per_step = ts;
}

void palette_initialize(short tbl_size)
{
    int i;
    PAL_TABLE_ENTRY *t;

    pal_table_size = tbl_size;
    t = Malloc(tbl_size * sizeof(PAL_TABLE_ENTRY));     /* no error check */
    for (i = 0; i < tbl_size; i++)
        t[i].status = EMPTY;
    timestamps_per_step = 1;
    pal_table_size = tbl_size;
    pal_fx_table = t;
}

void palette_shutdown(void)
{
    Free(pal_fx_table);
}

void palette_init_smap(short first, short last, uchar *from, uchar *to, short num_steps)
{
    int i, j;
    fix x0, x1, x2, y;

    for (i = first; i <= last; i++) {
        j = 3 * (i - first);
        Shadow_Fixed_Cmap[3 * i] = from[j] << 16;
        Shadow_Fixed_Cmap[3 * i + 1] = from[j + 1] << 16;
        Shadow_Fixed_Cmap[3 * i + 2] = from[j + 2] << 16;
        x0 = (to[j] << 16) - Shadow_Fixed_Cmap[3 * i];
        x1 = (to[j + 1] << 16) - Shadow_Fixed_Cmap[3 * i + 1];
        x2 = (to[j + 2] << 16) - Shadow_Fixed_Cmap[3 * i + 2];
        y = num_steps << 16;
        Delta_Cmap[3 * i] = fix_div(x0, y);
        Delta_Cmap[3 * i + 1] = fix_div(x1, y);
        Delta_Cmap[3 * i + 2] = fix_div(x2, y);
    }
}
