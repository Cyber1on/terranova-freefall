/* fl8hvlin.c -- fl8hlin.c / fl8vlin.c / gruilin.c (Terra Nova v1.09): integer unclipped
 * horizontal and vertical lines on a flat8 canvas (pixel table entries GRL_UHLINE and
 * GRL_UVLINE, one per fill type) and the integer line that converts to fix and calls the
 * current uline (GRL_ILINE, every fill type).  Decompiled from __FF.EXE (Watcom C); the
 * sources are Shock's.
 *
 *   0x335610 gri_all_uiline_fill
 *   0x335660 gri_flat8_uhline_ns    0x3356D4 gri_flat8_uhline_clut
 *   0x335730 gri_flat8_uhline_xor   0x335770 gri_flat8_uhline_blend
 *   0x335800 gri_flat8_uvline_ns    0x335878 gri_flat8_uvline_clut
 *   0x3358E8 gri_flat8_uvline_xor   0x335954 gri_flat8_uvline_blend
 *
 * Differences from Shock / quirks kept: LG_memset is inlined as rep stosd of a dword built
 * as (c | (c & 0xff) << 8) twice, so colour bits above 7 leak into bytes 2 and 3 of each
 * dword (ns and blend; clut colours are clean bytes).  The uhline blend is TN's own: it
 * uses tluc8tab[c] (indexed with the whole colour) as a translucency table and memsets c
 * when there is none.  The uvline blend just stores c, and neither xor maps c through parm.
 */
#include "2d_tmap.h"

extern void (**grd_uline_fill_vector)();    /* 0x44C5AC */

/* rep stosd / rep stosb with the colour dword built from (c & ~0xff00) | (c & 0xff) << 8 */
static void fill_quirk(uchar *q, long c, long n)
{
    ulong a = ((ulong)c & 0xffff00ff) | (((ulong)c & 0xff) << 8);
    ulong d = (a << 16) | a, i;

    for (i = 0; i < ((ulong)n >> 2); i++, q += 4)
        *(ulong *)q = d;
    for (i = 0; i < ((ulong)n & 3); i++)
        *q++ = (uchar)c;
}

#define ROW() ((ulong)(ushort)grd_bm.row)

void gri_all_uiline_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1)
{
    grs_vertex u0, u1;

    u0.x = (v0->x << 16) | 0x8000;
    u0.y = (v0->y << 16) | 0x8000;
    u1.x = (v1->x << 16) | 0x8000;
    u1.y = (v1->y << 16) | 0x8000;
    WATCOM_CALL(grd_uline_fill_vector[0], c, parm, (long)&u0, (long)&u1);
}

void gri_flat8_uhline_ns(short x0, short y0, short x1, long c, long parm)
{
    short t;

    if (x0 > x1) {
        t = x0; x0 = x1; x1 = t;
    }
    if (grd_gc.fill_type == FILL_SOLID)
        c = (uchar)parm;
    fill_quirk(grd_bm.bits + y0 * ROW() + x0, c, x1 - x0 + 1);
}

void gri_flat8_uhline_clut(short x0, short y0, short x1, long c, long parm)
{
    short t;

    if (x0 > x1) {
        t = x0; x0 = x1; x1 = t;
    }
    c = ((uchar *)parm)[c];
    fill_quirk(grd_bm.bits + y0 * ROW() + x0, c, x1 - x0 + 1);
}

void gri_flat8_uhline_xor(short x0, short y0, short x1, long c, long parm)
{
    uchar *p;
    short t;

    if (x0 > x1) {
        t = x0; x0 = x1; x1 = t;
    }
    for (p = grd_bm.bits + y0 * ROW() + x0; x0 <= x1; p++, x0++)
        *p ^= (uchar)c;
}

void gri_flat8_uhline_blend(short x0, short y0, short x1, long c, long parm)
{
    uchar *p, *e, *tab = tluc8tab_p[c];
    short t;

    if (x0 > x1) {
        t = x0; x0 = x1; x1 = t;
    }
    p = grd_bm.bits + y0 * ROW() + x0;
    if (tab == NULL) {
        fill_quirk(p, c, x1 - x0 + 1);
        return;
    }
    for (e = p + (x1 - x0); p <= e; p++)
        *p = tab[*p];
}

enum { V_STORE, V_XOR };

static void uvline(int mode, short x0, short y0, short y1, long c)
{
    uchar *p;
    short t;

    if (y0 > y1) {
        t = y0; y0 = y1; y1 = t;
    }
    for (p = grd_bm.bits + y0 * ROW() + x0; y0 <= y1; y0++, p += ROW()) {
        if (mode == V_XOR)
            *p ^= (uchar)c;
        else
            *p = (uchar)c;
    }
}

void gri_flat8_uvline_ns(short x0, short y0, short y1, long c, long parm)
{
    if (grd_gc.fill_type == FILL_SOLID)
        c = (uchar)parm;
    uvline(V_STORE, x0, y0, y1, c);
}

void gri_flat8_uvline_clut(short x0, short y0, short y1, long c, long parm)
{
    uvline(V_STORE, x0, y0, y1, ((uchar *)parm)[c]);
}

void gri_flat8_uvline_xor(short x0, short y0, short y1, long c, long parm)
{
    uvline(V_XOR, x0, y0, y1, c);
}

void gri_flat8_uvline_blend(short x0, short y0, short y1, long c, long parm)
{
    uvline(V_STORE, x0, y0, y1, c);
}
