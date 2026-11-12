/* detail.c -- texture-map detail settings of the 3D library (Terra Nova v1.09):
 *  - a small asm block after star.c (no RCS string, 0x2FA080..0x2FA0F2) with the detail-level
 *    setters (all names ours, none in System Shock);
 *  - x:\prj\tech\libsrc\3d\detail.c (Watcom C, 0x3150A0..0x3150D6).
 * Decompiled from __FF.EXE.
 *
 *   0x2FA080 g3_set_detail_params    0x2FA094 g3_set_detail_dist
 *   0x2FA09C g3_set_detail_level     0x2FA0CC g3_set_detail_entry
 *   0x3150A0 g3_set_tmaps_linear     0x3150BC g3_reset_tmaps
 *
 * g3d_detail_a (0x36444C) is the shift of tmap's perspective test (a polygon whose depth range
 * is small next to (min z >> a) is drawn with the linear mapper); 2D reads a (0x34FC70) and
 * g3d_detail_dist (0x327870) too.  The detail table (0x364454) has 6-byte entries {a, b, dist}.
 * All four asm setters use Watcom registers (eax, edx, ebx, ecx); g3_set_detail_level keeps edx.
 *
 * g3_tmap_func / g3_lit_tmap_func (0x365310 / 0x365314, fill.asm's data) are called by the
 * object interpreter with the tmap register convention, so in the verification build they get
 * the original entry addresses of the tmap routines (our C versions take cdecl arguments).
 */
#include "3d_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
g3s_detail_entry g3d_detail_table[3] = { { 0, 0, 0x40000 }, { 3, 4, 0x20000 }, { 4, 5, 0 } };
#endif

#ifdef TN_WATCOM_ABI
#define TMAP_ENTRY(f, addr) ((void (*)())(addr))
#else
#define TMAP_ENTRY(f, addr) ((void (*)())(f))
#endif

void g3_set_detail_params(uchar a, uchar b)
{
    g3d_detail_a = a & 7;
    g3d_detail_b = b & 7;
}

void g3_set_detail_dist(long d)
{
    g3d_detail_dist = d;
}

void g3_set_detail_level(int level)
{
    g3s_detail_entry *d = &g3d_detail_table[level];

    g3d_detail_a = d->a;
    g3d_detail_b = d->b;
    g3d_detail_dist = d->dist;
}

void g3_set_detail_entry(uchar a, uchar b, long dist, int index)
{
    g3s_detail_entry *d = &g3d_detail_table[index];

    d->a = a & 7;
    d->b = b & 7;
    d->dist = dist;
}

void g3_set_tmaps_linear(void)
{
    g3_tmap_func = TMAP_ENTRY(g3_draw_lmap, G3_DRAW_LMAP_ADDR);
    g3_lit_tmap_func = TMAP_ENTRY(g3_light_lmap, G3_LIGHT_LMAP_ADDR);
}

void g3_reset_tmaps(void)
{
    g3_tmap_func = TMAP_ENTRY(g3_draw_tmap, G3_DRAW_TMAP_ADDR);
    g3_lit_tmap_func = TMAP_ENTRY(g3_light_tmap, G3_LIGHT_TMAP_ADDR);
}
