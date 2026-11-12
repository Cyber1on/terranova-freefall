/* bank8pix.c -- Terra Nova v1.09 8-bit banked VESA canvas (Shock's "bank8": canvas table
 * 0x36A558, function table 0x3686E0, line table 0x36AA68): pixels, clear and state.
 * Decompiled from __FF.EXE (Watcom C); Shock declares the bank8 tables but has no code, so the
 * names are ours.  The canvas bitmap's bits are an offset into the linear frame buffer; each
 * access selects 64K bank (offset >> 16) through grd_device_table[GRT_SET_BANK] unless it is
 * already current, and goes through the bank window grd_cap->vbase.
 *
 *   0x33E150 bank8_set_upixel8 (canvas SET_UPIXEL8, function table FILL_NORM GRC_PIXEL)
 *   0x33E1E0 bank8_set_upixel8_interrupt (identical code)
 *   0x33E270 bank8_clut_fill_upixel  0x33E310 bank8_xor_fill_upixel
 *   0x33E3A4 bank8_blend_fill_upixel 0x33E434 bank8_solid_fill_upixel
 *   0x33FB00 bank8_get_upixel8   0x33FB84 bank8_get_pixel8
 *   0x33FC30 bank8_set_upixel24  0x33FCE4 bank8_set_pixel24
 *   0x33FDB0 bank8_get_upixel24  0x33FE50 bank8_get_pixel24
 *   0x33FF10 bank8_clear         0x33FF50 bank8_push_state  0x33FF80 bank8_pop_state
 *
 * Quirks kept: bank8_get_pixel8 and bank8_set_pixel24 clip with x <= right and y <= bot (one
 * pixel past the clip rectangle); bank8_get_pixel24 clips correctly.  The 24-bit routines'
 * inverse-palette index is ((c >> 3) & 0x1f) | ((c >> 6) & 0x3e0) | ((c >> 9) & 0x7c00), which
 * takes the top 5 bits of the low byte but bits 9..13 and 18..22 of the others (ModeX uses
 * the proper 5:5:5 of each byte).  The blend fill pixel just stores c.  The bank state shares
 * the ModeX state stack (0x36EF4C / 0x4608E8), one word per entry.
 */
#include "2d_tmap.h"

extern int grd_mx_state_depth;            /* 0x36EF4C */
extern uchar grd_mx_state_stack[10][2];   /* 0x4608E8 */

#ifndef DRAW_URECT
#define DRAW_URECT 15                     /* canvas table entry */
#endif
#define ADDR(x, y) ((long)grd_bm.bits + (short)(y) * (ulong)(ushort)grd_bm.row + (short)(x))
#define IPAL24(c) (((c) >> 3 & 0x1f) | ((c) >> 6 & 0x3e0) | ((c) >> 9 & 0x7c00))
#define INCLIP(x, y) ((x) >= grd_clip.left && (x) <= grd_clip.right && (y) >= grd_clip.top && (y) <= grd_clip.bot)

void bank8_set_upixel8(long c, short x, short y)
{
    int lock_;
    uchar *p = BANK8_PTR(ADDR(x, y));

    *p = (uchar)c;
    grd_bank8_lock = lock_;
}

void bank8_set_upixel8_interrupt(long c, short x, short y)
{
    int lock_;
    uchar *p = BANK8_PTR(ADDR(x, y));

    *p = (uchar)c;
    grd_bank8_lock = lock_;
}

void bank8_clut_fill_upixel(long c, short x, short y)
{
    int lock_;
    uchar *p = BANK8_PTR(ADDR(x, y));

    *p = ((uchar *)grd_gc.fill_parm)[c];
    grd_bank8_lock = lock_;
}

void bank8_xor_fill_upixel(long c, short x, short y)
{
    int lock_;
    uchar *p = BANK8_PTR(ADDR(x, y));

    *p ^= (uchar)c;
    grd_bank8_lock = lock_;
}

void bank8_blend_fill_upixel(long c, short x, short y)
{
    int lock_;
    uchar *p = BANK8_PTR(ADDR(x, y));

    *p = (uchar)c;
    grd_bank8_lock = lock_;
}

void bank8_solid_fill_upixel(long c, short x, short y)
{
    int lock_;
    uchar *p = BANK8_PTR(ADDR(x, y));

    *p = (uchar)grd_gc.fill_parm;
    grd_bank8_lock = lock_;
}

long bank8_get_upixel8(short x, short y)
{
    int lock_;
    uchar *p = BANK8_PTR(ADDR(x, y));
    long r = *p;

    grd_bank8_lock = lock_;
    return r;
}

long bank8_get_pixel8(short x, short y)
{
    int lock_ = grd_bank8_lock;
    long r = -1;

    if (INCLIP(x, y))
        r = *BANK8_PTR(ADDR(x, y));
    grd_bank8_lock = lock_;
    return r;
}

void bank8_set_upixel24(long c, short x, short y)
{
    int lock_;
    uchar *p = BANK8_PTR(ADDR(x, y));

    *p = grd_ipal[IPAL24(c)];
    grd_bank8_lock = lock_;
}

int bank8_set_pixel24(long c, short x, short y)
{
    int lock_ = grd_bank8_lock, r = CLIP_ALL;

    if (INCLIP(x, y)) {
        uchar *p = BANK8_PTR(ADDR(x, y));
        *p = grd_ipal[IPAL24(c)];
        r = 0;
    }
    grd_bank8_lock = lock_;
    return r;
}

long bank8_get_upixel24(short x, short y)
{
    int lock_;
    uchar *p = BANK8_PTR(ADDR(x, y));
    long r = *(ulong *)(grd_pal + 3 * *p) & 0xffffff;

    grd_bank8_lock = lock_;
    return r;
}

long bank8_get_pixel24(short x, short y)
{
    int lock_ = grd_bank8_lock;
    long r = -1;

    if (x >= grd_clip.left && x < grd_clip.right && y >= grd_clip.top && y < grd_clip.bot) {
        uchar *p = BANK8_PTR(ADDR(x, y));
        r = *(ulong *)(grd_pal + 3 * *p) & 0xffffff;
    }
    grd_bank8_lock = lock_;
    return r;
}

void bank8_clear(long c)
{
    long fc = grd_gc.fcolor;

    grd_gc.fcolor = c;
    WATCOM_CALL((void (*)())grd_canvas_table[DRAW_URECT], 0L, 0L, (long)grd_bm.w, (long)grd_bm.h);
    grd_gc.fcolor = fc;
}

int bank8_push_state(void)
{
    int d = grd_mx_state_depth, r;

    if (d >= 10)
        r = -1;
    else {
        d++;
        *(ushort *)grd_mx_state_stack[d - 1] = grd_bank8_bank;
        r = 0;
    }
    grd_mx_state_depth = d;
    return r;
}

int bank8_pop_state(void)
{
    ushort b;

    if (grd_mx_state_depth <= 0)
        return -1;
    grd_mx_state_depth--;
    b = *(ushort *)grd_mx_state_stack[grd_mx_state_depth];
    if (grd_bank8_bank != b) {
        grd_bank8_bank = b;
        grd_bank8_lock++;
        WATCOM_CALL((void (*)())grd_device_table[GRT_SET_BANK], (long)(short)grd_bank8_bank);
        grd_bank8_lock--;
    }
    return 0;
}
