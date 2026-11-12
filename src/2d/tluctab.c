/* tluctab.c -- x:\prj\tech\libsrc\2d\tluctab.c (Terra Nova v1.09): translucency tables.
 * Decompiled from __FF.EXE.
 *
 *   0x2F1B80 gr_init_blend_table (name ours; not in System Shock)
 *   0x2F1C64 gr_init_translucency_table          0x2F1DB4 gr_init_lit_translucency_table
 *   0x2F1F50 gr_init_lit_translucency_tables     0x2F1FD8 gr_dump_tluc8_table
 *   0x2F2098 gr_read_tluc8_table
 *   data: 0x44C180 tluc8tab[256] (via tluc8tab_p 0x360478, grd.c), 0x44BD80 tluc8ltab[256]
 *   (via tluc8ltab_p 0x36047C), 0x360480 tluc8nstab, 0x44C580 tluc8stab
 *
 * Only gr_init_translucency_table is called by the game.  The table routines are System
 * Shock's (see its TLUCTAB.C for the colour model); the tables are reached through pointers.
 */
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar *tluc8ltab[256];              /* 0x44BD80 */
uchar **tluc8ltab_p = tluc8ltab;    /* 0x36047C */
int tluc8nstab = 0;                 /* 0x360480 */
uchar *tluc8stab;                   /* 0x44C580 */
#endif

/* glomped rgb (r<<2 | g<<13 | b<<24, 10 bits each) components as fixed point */
#define rtof(b) ((fix)(((b) & 0x3ff) << 12))
#define gtof(b) ((fix)(((b) & 0x1ff800) << 1))
#define btof(b) ((fix)(((b) & 0xffc00000) >> 10))
/* inverse palette index of a fix colour << 2 (gr_index_rgb) */
#define tluc_index(r, g, b) ((((r) << 2) >> 19 & 0x1f) | (((g) << 2) >> 14 & 0x3e0) | (((b) << 2) >> 9 & 0x7c00))

/* For each of the 256 colours of pal (6-bit RGB triples) the colour blended toward color by
 * frac (0 = pal, 0x10000 = color); color's components are taken as their low 8 bits. */
uchar *gr_init_blend_table(uchar *p, fix frac, grs_rgb color, uchar *pal)
{
    long cr = ((color >> 2) & 0xff) * frac;
    long cg = ((color >> 13) & 0xff) * frac;
    long cb = ((color >> 24) & 0xff) * frac;
    fix inv = 0x10000 - frac;
    uchar *end = pal + 3 * 256, *q = p;

    do {
        long r = cr + pal[0] * inv;
        long g = cg + pal[1] * inv;
        long b = cb + pal[2] * inv;
        *q++ = grd_ipal[(r >> 19 & 0x1f) | (g >> 14 & 0x3e0) | (b >> 9 & 0x7c00)];
        pal += 3;
    } while (pal != end);
    return p;
}

uchar *gr_init_translucency_table(uchar *p, fix opacity, fix purity, grs_rgb color)
{
    grs_rgb bg;
    int i;
    fix baser = fix_mul(rtof(color), opacity);
    fix baseg = fix_mul(gtof(color), opacity);
    fix baseb = fix_mul(btof(color), opacity);
    fix filter = fix_mul(0x10000 - opacity, purity);
    fix clarity = 0x10000 - opacity - filter;
    fix filterr = clarity + (fix_mul(rtof(color), filter) >> 6);
    fix filterg = clarity + (fix_mul(gtof(color), filter) >> 6);
    fix filterb = clarity + (fix_mul(btof(color), filter) >> 6);

    for (i = 0; i < 256; i++) {
        fix r, g, b;
        bg = grd_bpal[i];
        r = fix_mul(rtof(bg), filterr) + baser;
        g = fix_mul(gtof(bg), filterg) + baseg;
        b = fix_mul(btof(bg), filterb) + baseb;
        p[i] = ((uchar *)TN_LOWMEM(grd_ipal))[tluc_index(r, g, b)];   /* 0 before the first palette */
    }
    return p;
}

uchar *gr_init_lit_translucency_table(uchar *p, fix opacity, fix purity, grs_rgb color, grs_rgb light)
{
    grs_rgb bg;
    int i;
    fix filter = fix_mul(0x10000 - opacity, purity);
    fix clarity = 0x10000 - opacity - filter;
    fix filterr = clarity + (fix_mul(rtof(color), filter) >> 6);
    fix filterg = clarity + (fix_mul(gtof(color), filter) >> 6);
    fix filterb = clarity + (fix_mul(btof(color), filter) >> 6);
    fix baser = fix_mul(fix_mul(rtof(color), rtof(light)), opacity) >> 6;
    fix baseg = fix_mul(fix_mul(gtof(color), gtof(light)), opacity) >> 6;
    fix baseb = fix_mul(fix_mul(btof(color), btof(light)), opacity) >> 6;

    for (i = 0; i < 256; i++) {
        fix r, g, b;
        bg = grd_bpal[i];
        r = fix_mul(rtof(bg), filterr) + baser;
        g = fix_mul(gtof(bg), filterg) + baseg;
        b = fix_mul(btof(bg), filterb) + baseb;
        p[i] = grd_ipal[tluc_index(r, g, b)];
    }
    return p;
}

/* n tables, lit by the n light levels of white in the screen's light table. */
uchar *gr_init_lit_translucency_tables(uchar *p, fix opacity, fix purity, grs_rgb color, int n)
{
    int k;

    if (n == 0)
        return NULL;
    for (k = 0; k < n; k++)
        gr_init_lit_translucency_table(p + 256 * k, opacity, purity, color,
                                       grd_bpal[grd_screen->ltab[k * 256 + grd_ipal[0x7fff]]]);
    return p;
}

/* Writes the translucency tables to buf: total size, nlit, tluc8nstab, the shaded tables,
 * then for every colour with a table: colour, table, nlit light tables.  Returns the size. */
int gr_dump_tluc8_table(uchar *buf, int nlit)
{
    uchar *p = buf + 4;
    int k, lsize = nlit << 8;

    *p++ = (uchar)nlit;
    *p++ = (uchar)tluc8nstab;
    memcpy(p, tluc8stab, tluc8nstab << 8);
    /* quirk: p is not advanced past the shaded tables (they are overwritten by what follows) */
    for (k = 0; k < 256; k++) {
        if (tluc8tab_p[k] != NULL) {
            *p++ = (uchar)k;
            memcpy(p, tluc8tab_p[k], 256);
            p += 256;
            memcpy(p, tluc8ltab_p[k], lsize);
            p += lsize;
        }
    }
    *(int *)buf = p - buf;
    return p - buf;
}

void gr_read_tluc8_table(uchar *buf)
{
    uchar *p, *end;
    int lsize, k;

    end = buf + *(int *)buf;
    p = buf + 4;
    lsize = *p++ << 8;
    tluc8nstab = *p++;
    tluc8stab = p;
    p += tluc8nstab << 8;
    while (p < end) {
        k = *p++;
        tluc8tab_p[k] = p;
        p += 256;
        if (lsize)
            tluc8ltab_p[k] = p;
        p += lsize;
    }
}
