/* mxpix.c -- Terra Nova v1.09 ModeX (unchained VGA) canvas: pixels and clear.  Decompiled from
 * __FF.EXE (Watcom C); Shock dropped the ModeX driver, so the names are ours.  A ModeX row is
 * row bytes per plane: pixel x lives at byte x >> 2 of plane x & 3.  The write planes are set
 * through the sequencer map mask (3C4 index 2) and the read plane through the graphics
 * controller read map select (3CE index 4); both are cached in grd_mx_wmask / grd_mx_rplane.
 *
 *   canvas table 0x36808C:
 *   0x33A8E0 modex_set_upixel8 (also fill table NORM/DEVICE/GRC_PIXEL)
 *   0x33A950 modex_set_upixel8_interrupt   (always reprograms the map mask)
 *   0x33ABD0 modex_get_upixel8   0x33AC28 modex_get_pixel8
 *   0x33ACB0 modex_set_upixel24  0x33AD40 modex_set_pixel24
 *   0x33AE00 modex_get_upixel24  0x33AE70 modex_get_pixel24
 *   0x33AF00 modex_clear
 *   fill table 0x36AB1C DEVICE/GRC_PIXEL:
 *   0x33A9BC modex_clut_fill_upixel  0x33AA28 modex_xor_fill_upixel
 *   0x33AAC8 modex_blend_fill_upixel 0x33AB60 modex_solid_fill_upixel
 *
 * Quirks kept: the set/fill pixels add bm.align to x, the get pixels and the 24-bit pixels do
 * not; modex_get_pixel8 selects the read plane with the plane MASK (1 << (x & 3)) instead of
 * the plane number; the blend fill pixel just stores c (after selecting the read plane);
 * modex_clear's single-column case indexes grd_mx_rmask with the unmasked last column.
 */
#include "2d_tmap.h"

#define MX_PTR(x, y) (grd_bm.bits + (y) * (ulong)(ushort)grd_bm.row + ((x) >> 2))
#define RGB15(c) (((c) >> 5 & 0x1f) | ((c) >> 11 & 0x3e0) | ((c) >> 17 & 0x7c00))
#define CLIPPED(x, y) ((x) < grd_clip.left || (x) >= grd_clip.right || (y) < grd_clip.top || (y) >= grd_clip.bot)

void modex_set_upixel8(long c, short x, short y)
{
    int xa = x + grd_bm.align;
    uchar *p = MX_PTR(xa, y);

    MX_WMASK(1 << (xa & 3));
    *p = (uchar)c;
}

void modex_set_upixel8_interrupt(long c, short x, short y)
{
    int xa = x + grd_bm.align;
    uchar *p = MX_PTR(xa, y);
    int m = 1 << (xa & 3);

    grd_mx_wmask = 0xff;                    /* an interrupt may have changed the mask */
    if (m != 0xff) {
        grd_mx_wmask = (uchar)m;
        tm_outw(0x3C4, (uchar)m << 8 | 2);
    }
    *p = (uchar)c;
}

void modex_clut_fill_upixel(long c, short x, short y)
{
    int xa = x + grd_bm.align;
    uchar *p = MX_PTR(xa, y);

    MX_WMASK(1 << (xa & 3));
    *p = ((uchar *)grd_gc.fill_parm)[c];
}

void modex_xor_fill_upixel(long c, short x, short y)
{
    int xa = x + grd_bm.align;
    uchar *p = MX_PTR(xa, y);
    int m = 1 << (xa & 3);

    MX_RPLANE(xa & 3);
    c ^= *p;
    MX_WMASK(m);
    *p = (uchar)c;
}

void modex_blend_fill_upixel(long c, short x, short y)
{
    int xa = x + grd_bm.align;
    uchar *p = MX_PTR(xa, y);
    int m = 1 << (xa & 3);

    MX_RPLANE(xa & 3);
    MX_WMASK(m);
    *p = (uchar)c;
}

void modex_solid_fill_upixel(long c, short x, short y)
{
    int xa = x + grd_bm.align;
    uchar *p = MX_PTR(xa, y);

    MX_WMASK(1 << (xa & 3));
    *p = (uchar)grd_gc.fill_parm;
}

long modex_get_upixel8(short x, short y)
{
    uchar *p = MX_PTR(x, y);

    MX_RPLANE((short)(x & 3));
    return *p;
}

long modex_get_pixel8(short x, short y)
{
    uchar *p;

    if (CLIPPED(x, y))
        return -1;
    p = MX_PTR(x, y);
    MX_RPLANE(grd_mx_lmask[x & 3] & grd_mx_rmask[x & 3]);
    return *p;
}

void modex_set_upixel24(long c, short x, short y)
{
    uchar *p = MX_PTR(x, y);

    MX_WMASK(grd_mx_lmask[x & 3] & grd_mx_rmask[x & 3]);
    *p = grd_ipal[RGB15(c)];
}

int modex_set_pixel24(long c, short x, short y)
{
    uchar *p;

    if (CLIPPED(x, y))
        return CLIP_ALL;
    p = MX_PTR(x, y);
    MX_WMASK(grd_mx_lmask[x & 3] & grd_mx_rmask[x & 3]);
    *p = grd_ipal[RGB15(c)];
    return 0;
}

long modex_get_upixel24(short x, short y)
{
    uchar *p = MX_PTR(x, y);

    MX_RPLANE((short)(x & 3));
    return *(ulong *)(grd_pal + 3 * *p) & 0xffffff;
}

long modex_get_pixel24(short x, short y)
{
    uchar *p;

    if (CLIPPED(x, y))
        return -1;
    p = MX_PTR(x, y);
    MX_RPLANE((short)(x & 3));
    return *(ulong *)(grd_pal + 3 * *p) & 0xffffff;
}

/* rep stosd / rep stosb of n bytes of c, with the dword built as (c | (c & 0xff) << 8) twice */
static void mx_fill(uchar *q, long c, long n)
{
    ulong a = ((ulong)c & 0xffff00ff) | (((ulong)c & 0xff) << 8);
    ulong d = (a << 16) | a, i;

    for (i = 0; i < ((ulong)n >> 2); i++, q += 4)
        *(ulong *)q = d;
    for (i = 0; i < ((ulong)n & 3); i++)
        *q++ = (uchar)c;
}

void modex_clear(long c)
{
    int al = grd_bm.align;
    int last = grd_bm.w + al - 1;           /* last column, in pixels */
    short n = (short)((last >> 2) + 1);     /* columns of 4 */
    uchar *p = grd_bm.bits, *q;
    int h, m;

    if (n == 1) {
        m = grd_mx_rmask[last] & grd_mx_lmask[al];
        h = grd_bm.h;
        MX_WMASK(m);
        while (h-- != 0) {
            *p = (uchar)c;
            p += (ushort)grd_bm.row;
        }
        return;
    }
    m = grd_mx_lmask[al];
    if (m != 0xf) {                         /* partial first column */
        h = grd_bm.h;
        MX_WMASK(m);
        for (q = p; h-- != 0; q += (ushort)grd_bm.row)
            *q = (uchar)c;
        n--;
        p++;
    }
    m = grd_mx_rmask[last & 3];
    if (m != 0xf) {                         /* partial last column */
        q = p + n - 1;
        h = grd_bm.h;
        MX_WMASK(m);
        for (; h-- != 0; q += (ushort)grd_bm.row)
            *q = (uchar)c;
        n--;
    }
    if (n > 0) {
        h = grd_bm.h;
        MX_WMASK(0xf);
        for (; h-- != 0; p += (ushort)grd_bm.row)
            mx_fill(p, c, n);
    }
}
