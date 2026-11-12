/* kbcook.c -- r:\prj\lib\src\input\kbcook.c, DOS version (Terra Nova v1.09): converts raw
 * keyboard events into cooked codes (ascii + KB_FLAG_* bits).  Decompiled from __FF.EXE.
 *
 *   0x301CE0 kb_cook    0x301F80 kb_get_cooked    0x31A210 kb_set_leds (asm, name ours)
 *
 * The DOS code that the Mac Shock source keeps in comments, with Terra Nova additions:
 *  - a third table column: with right Alt (AltGr, kbd_modifier_state bit 8) held, a non-zero
 *    third code is used, and right Alt then does not set KB_FLAG_ALT;
 *  - a 4th argument 'raw': the cooked code is the scan code with SPECIAL|2ND|ALT|CTRL
 *    (0x1E00) allowed, and the shift flag is shift ^ capslock.  Both callers pass FALSE.
 * kb_get_cooked returns its bool in al; when kb_next gives code 0xFF, ah keeps the event's state.
 * Quirk kept: scan code 0x7A returns early, so on a key-up it leaves the modifier state
 * inverted (same as Shock).
 */
#include "support.h"
#include "sup_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
ulong kbd_modifier_state;
#endif

#define KBM_LED_MASK 7

bool kb_cook(kbs_event ev, ushort *cooked, bool *results, bool raw)
{
    uchar code = ev.code, state = ev.state;
    ushort flags = kb_cnv_table[code][0], cnv = 0;
    ulong old_mods = kbd_modifier_state;
    uchar shifted = ((old_mods >> 4) | (old_mods >> 3)) & 1;      /* either shift */
    uchar capslock = (flags >> 15) & (old_mods >> 2) & 1;         /* CNV_CAPS and caps lock */
    uchar altgr = 0;

    if (!raw) {
        if ((old_mods >> 8) & 1) {
            cnv = kb_cnv_table[code][2];
            altgr = (cnv != 0);
        }
        if (!altgr)
            cnv = kb_cnv_table[code][shifted ^ capslock];
    } else {
        cnv = code | 0x1E00;
        shifted ^= capslock;
    }
    *cooked = cnv & 0x28FF;                 /* CNV_SPECIAL | CNV_2ND | 0xFF */
    *results = FALSE;

    /* if an up event, use negative logic */
    if (state == 0)
        kbd_modifier_state = ~kbd_modifier_state;
    switch (code) {
    case 0x7A: return 0;
    case 0x2A: kbd_modifier_state |= 0x08; break;     /* left shift */
    case 0x36: kbd_modifier_state |= 0x10; break;     /* right shift */
    case 0x1D: kbd_modifier_state |= 0x20; break;     /* left ctrl */
    case 0x9D: kbd_modifier_state |= 0x40; break;     /* right ctrl */
    case 0x3A: if (state == 1) kbd_modifier_state ^= 4; break;    /* caps lock */
    case 0x45: if (state == 1) kbd_modifier_state ^= 2; break;    /* num lock */
    case 0x46: if (state == 1) kbd_modifier_state ^= 1; break;    /* scroll lock */
    case 0x38: kbd_modifier_state |= 0x80; break;     /* left alt */
    case 0xB8: kbd_modifier_state |= 0x100; break;    /* right alt */
    default: *results = TRUE; break;
    }
    if (state == 0)
        kbd_modifier_state = ~kbd_modifier_state;
    if ((kbd_modifier_state & KBM_LED_MASK) != (old_mods & KBM_LED_MASK))
        kb_set_leds(kbd_modifier_state & KBM_LED_MASK);
    if (!*results)
        return 0;

    if ((cnv & 0x4000) && !(kbd_modifier_state & 2))   /* CNV_NUM without num lock */
        *cooked = code | 0x0800;                        /* | KB_FLAG_SPECIAL */
    *cooked |= state << 8;
    *cooked |= ((kbd_modifier_state << 4) | (kbd_modifier_state << 3)) & 0x200 & cnv;   /* ctrl */
    *cooked |= ((kbd_modifier_state << 3) | ((kbd_modifier_state << 2) ^ (altgr << 10))) & 0x400 & cnv; /* alt */
    *cooked |= (shifted << 12) & cnv;                  /* shift */
    return 0;
}

bool kb_get_cooked(ushort *key)
{
    bool res = FALSE;
    ushort e = kb_next();
    kbs_event ev;

    if ((uchar)e == KBC_NONE)
        return res;
    ev.code = e;
    ev.state = e >> 8;
    kb_cook(ev, key, &res, FALSE);
    return res;
}

/* Sends "set LEDs" (0xED, mask) to the 8042, waiting up to 0x10000 polls of port 0x64 for
 * the input buffer before each byte.  Returns 1 if a wait timed out.  The keyboard's 0xFA
 * acknowledges are seen by kb_pm_isr. */
static int kb_8042_send(uchar v)
{
    ulong n = 0x10000;

    while (inb(0x64) & 2)
        if (--n == 0)
            return 1;
    outb(0x60, v);
    return 0;
}

int kb_set_leds(int mask)
{
    int r = kb_8042_send(0xED);
    return kb_8042_send(mask & 7) | r;
}
