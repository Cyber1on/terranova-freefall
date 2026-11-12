/* fl8pix.c -- x:\prj\tech\libsrc\2d\fl8pix.c, fl8gpix.c, fl8p24.c, fl8g24.c, fl8clear.c
 * (Terra Nova v1.09): flat8 pixel set/get for every fill type, 24-bit pixels, canvas clear.
 * Decompiled from __FF.EXE.
 *
 *   0x327220 flat8_set_upixel        0x327248 flat8_clut_set_upixel   0x327274 flat8_xor_set_upixel
 *   0x32729C flat8_blend_set_upixel  0x3272E8 flat8_solid_set_upixel  0x327310 flat8_get_upixel
 *   0x32733C flat8_get_pixel         0x327390 flat8_set_upixel24      0x3273E0 flat8_set_pixel24
 *   0x327450 flat8_get_upixel24      0x32748C flat8_get_pixel24       0x3274F0 flat8_clear
 */
#include "2d_prim.h"

#define PIX(x, y) (grd_bm.bits + grd_bm.row * (y) + (x))

/* draws an unclipped pixel of the given color at (x, y) on the canvas. */
void flat8_set_upixel(long color, short x, short y)
{
    *PIX(x, y) = (uchar)color;
}

void flat8_clut_set_upixel(long color, short x, short y)
{
    *PIX(x, y) = ((uchar *)grd_gc.fill_parm)[color];
}

void flat8_xor_set_upixel(long color, short x, short y)
{
    *PIX(x, y) ^= (uchar)color;
}

/* Terra Nova's blend: tluc8tab_p[color] is a 256-byte translucency table for that color, or
 * NULL for an opaque color. */
void flat8_blend_set_upixel(long color, short x, short y)
{
    uchar *tab = tluc8tab_p[color];
    uchar *p = PIX(x, y);

    if (tab)
        *p = tab[*p];
    else
        *p = (uchar)color;
}

void flat8_solid_set_upixel(long color, short x, short y)
{
    *PIX(x, y) = (uchar)grd_gc.fill_parm;
}

long flat8_get_upixel(short x, short y)
{
    return *PIX(x, y);
}

/* clipped get; returns -1 outside the clip rectangle.  The right and bottom edges count as
 * inside here (all other primitives treat them as outside). */
long flat8_get_pixel(short x, short y)
{
    if (x < grd_clip.left || x > grd_clip.right || y < grd_clip.top || y > grd_clip.bot)
        return -1;
#ifndef TN_WATCOM_ABI
    /* G40: with the edges inside, a point on the right or bottom edge of a canvas the size of its
     * bitmap is one past it: a star rounded onto the bottom edge read the byte after the bitmap's
     * last row (past the end of its memory when the canvas has a buffer of its own) */
    if (x >= grd_bm.w || y >= grd_bm.h)
        return -1;
#endif
    return *PIX(x, y);
}

/* 24-bit color -> 15-bit inverse palette index */
#define IPAL_INDEX(c) ((((c) >> 3) & 0x1f) | (((c) >> 6) & 0x3e0) | (((c) >> 9) & 0x7c00))

void flat8_set_upixel24(long c, short x, short y)
{
    *PIX(x, y) = grd_ipal[IPAL_INDEX(c)];
}

/* quirk: like flat8_get_pixel the right and bottom edges are inside */
int flat8_set_pixel24(long c, short x, short y)
{
    if (x < grd_clip.left || x > grd_clip.right || y < grd_clip.top || y > grd_clip.bot)
        return CLIP_ALL;
    *PIX(x, y) = grd_ipal[IPAL_INDEX(c)];
    return CLIP_NONE;
}

long flat8_get_upixel24(short x, short y)
{
    return *(ulong *)(grd_pal + 3 * *PIX(x, y)) & 0xffffff;
}

/* returns CLIP_ALL (as a "color") outside the clip rectangle */
long flat8_get_pixel24(short x, short y)
{
    if (x < grd_clip.left || x >= grd_clip.right || y < grd_clip.top || y >= grd_clip.bot)
        return CLIP_ALL;
    return *(ulong *)(grd_pal + 3 * *PIX(x, y)) & 0xffffff;
}

/* Fills the whole canvas bitmap with color (rep stosd + rep stosb per row).  Quirk: the dword
 * pattern is built as ((c & 0xffff) | (c & 0xff) << 8) | (that << 16), so color bits 16-31
 * leak into bytes 2 and 3 of every dword (the game only passes 0..255). */
void flat8_clear(long color)
{
    uchar *row = grd_bm.bits;
    short h = grd_bm.h;
    ulong lo = ((ulong)color & 0xffff00ff) | (((ulong)color & 0xff) << 8);
    ulong pat = lo | (lo << 16);

    while (h--) {
        int w = grd_bm.w, i;
        uchar *p = row;
        for (i = (uint)w >> 2; i > 0; i--, p += 4)
            *(ulong *)p = pat;
        for (i = w & 3; i > 0; i--)
            *p++ = (uchar)color;
        row += grd_bm.row;
    }
}
