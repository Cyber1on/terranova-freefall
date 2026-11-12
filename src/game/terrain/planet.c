/* planet.c -- Terra Nova module M074 (0x299590-0x299E6C; source file name unknown, the error
 * strings say PlanetUse): loading a planet file.  Decompiled from __FF.EXE v1.09.
 *
 *   0x299590 planet_299590 start an allocation   0x2995DC planet_2995DC forget it
 *   0x2995E8 planet_2995E8 take bytes            0x299624 PlanetUse
 *   0x299C34 planet_299C34 shaded light tables   0x299CC4 planet_299CC4 (unused) bright rows
 *   0x299E04 planet_299E04 set palette (callback) 0x299E4C planet_299E4C forget the file
 *
 * A planet file (named by the map record, MapLoad 0x293F50 calls PlanetUse) holds a world's
 * terrain textures (res 0x30, 0x1000 bytes each; res 0x31, 0x100 bytes each; res 0x2D maps
 * texture numbers to both), the palette (0x32, with an optional patch in ref 0x42:0), shading
 * and light tables and a few small records.  The variable-sized resources are carved out of
 * one static heap (planet_heap, 0x45948 bytes) sized for their total first.
 *
 * Quirks kept:
 *  - planet_299590 tests the size signed; a size above 0x45948 reports an error and continues
 *    with a NULL allocation pointer (the original then extracts to low memory).
 *  - planet_2995E8 reports running out of room but hands out the bytes anyway (planet_mem_left
 *    wraps).
 *  - PlanetUse reads the palette patch into an uninitialised stack buffer and copies entries
 *    0xB0..0xE6 of it to planet_pal_part, also when there is no patch or it does not cover
 *    them (stale stack bytes).
 *  - planet_299E4C and the reopen path forget the heap without freeing anything (static).
 */
#include "g5_c.h"
#include "g1_c.h"
#include "res.h"
#include "2d.h"
#include "g1_b.h"
#include "t2_a.h"

void planet_299590(long size)
{
    if (size <= 0x45948)
        planet_mem_base = planet_mem_ptr = planet_heap;
    else
        planet_mem_ptr = NULL;
    if (planet_mem_ptr == NULL)
        DbgReportError(1, "PlanetUse: cannot allocate enough memory\n");
    planet_mem_left = size;
}

void planet_2995DC(void)
{
    planet_mem_left = 0;
}

uchar *planet_2995E8(ulong size)
{
    uchar *p;
    if (size > planet_mem_left)
        DbgReportError(1, "PlanetUse: Ran out of allocated planet memory\n");
    p = planet_mem_ptr;
    planet_mem_ptr = p + size;
    planet_mem_left -= size;
    return p;
}

long PlanetUse(void *fname)
{
    uchar pal[0x300];                       /* never initialised (see the quirk above) */
    uchar row[0x100];
    uchar hdr[4];
    uchar *idx, *a, *b;
    int fd, i;

    fd = ResOpenResFile(fname, 0, 0);
    if (fd < 0)
        return -1;
    if (ResSize(0x28) != 2 || ResSize(0x29) != 1 || ResSize(0x2A) != 1) {
        ResCloseFile(fd);
        return -1;
    }
    if (planet_res_file >= 0)
        planet_2995DC();
    planet_res_file = fd;
    ResExtract(0x29, &planet_tex_count);
    if (planet_tex_count > 0x40) {
        ResCloseFile(planet_res_file);
        planet_res_file = -1;
        return -1;
    }
    ResExtract(0x2A, &planet_res2a);

    planet_299590(ResSize(0x2B) + ResSize(0x2C) + ResSize(0x30) + ResSize(0x31)
                  + planet_tex_count * 4 + ResSize(0x2E) + ResSize(0x2F) + ResSize(0x34)
                  + ResSize(0x46) + ResSize(0x47));
    if (planet_mem_ptr == NULL)
        DbgReportError(1, "PlanetUse: cannot allocate enough memory\n");
    planet_mem_base = planet_mem_ptr;

    ResExtract(0x28, hdr);
    game_41DE34 = hdr[0];
    game_41DE33 = hdr[1];
    game_41DD58 = planet_2995E8(ResSize(0x2B));
    ResExtract(0x2B, game_41DD58);
    planet_res2c = planet_2995E8(ResSize(0x2C));
    ResExtract(0x2C, planet_res2c);
    planet_res30 = planet_2995E8(ResSize(0x30));
    map_293EFC(0x30, planet_res30, 0x8000);

    idx = ffmem_2C4F40(ResSize(0x2D));
    ResExtract(0x2D, idx);
    for (i = 0; i < planet_tex_count; i++)
        planet_tex[i] = planet_res30 + (idx[i] << 12);
    planet_res31 = planet_2995E8(ResSize(0x31));
    ResExtract(0x31, planet_res31);
    planet_tex_rows = (uchar **)planet_2995E8(planet_tex_count * 4);
    for (i = 0; i < planet_tex_count; i++)
        planet_tex_rows[i] = planet_res31 + (idx[i] << 8);
    ffmem_2C4FE4(idx);

    planet_pal_count = ResSize(0x32) / 3;
    ResExtract(0x32, planet_pal);
    ResExtract(0x41, planet_tab41);
    ResExtract(0x3B, planet_tab3b);
    ResExtract(0x3C, planet_tab3c);
    ResExtract(0x3D, planet_tab3d);
    ResExtract(0x3E, planet_tab3e);
    ResExtract(0x40, planet_tab40);
    ResExtract(0x3F, planet_tab3f);
    {
        uchar *item = RefGet(MKREF(0x42, 0));
        if (*(long *)(item + 0x18) != 0) {
            uchar *patch = (uchar *)ResGet(0x42) + *(long *)(item + 0x18);
            short start = *(short *)patch;
            long n = *(long *)patch >> 16;
            memcpy(pal + start * 3, patch + 4, n * 3);
        }
    }
    memcpy(planet_pal_part, pal + 0xB0 * 3, 0xA5);

    planet_res2e = planet_2995E8(ResSize(0x2E));
    ResExtract(0x2E, planet_res2e);
    planet_res2f = planet_2995E8(ResSize(0x2F));
    ResExtract(0x2F, planet_res2f);
    ResExtract(0x35, planet_light);
    ResExtract(0x36, planet_tab36);
    ResExtract(0x37, planet_tab37);
    ResExtract(0x38, planet_tab38);
    ResExtract(0x3A, planet_tab3a);
    rndsky_2A6BC4(0xF7, planet_tab3a);
    planet_shade = planet_tab37;
    terrain_cornerbits = planet_2995E8(ResSize(0x34));
    ResExtract(0x34, terrain_cornerbits);
    planet_res46 = planet_2995E8(ResSize(0x46));
    ResExtract(0x46, planet_res46);
    planet_res47 = planet_2995E8(ResSize(0x47));
    ResExtract(0x47, planet_res47);
    ResExtract(0x43, game_41DA28);
    ResExtract(0x44, terrain_typebits);
    ResExtract(0x45, terrain_kindtab);
    ResExtract(0x48, &map_vary_mask);
    ResExtract(0x49, terrain_kinds);
    ResCloseFile(planet_res_file);

    /* flip the 16 light rows upside down, then repeat the last one into rows 16..23 */
    for (a = planet_light, b = planet_light + 0xF00; a != planet_light + 0x800; a += 0x100, b -= 0x100) {
        memcpy(row, a, 0x100);
        memcpy(a, b, 0x100);
        memcpy(b, row, 0x100);
    }
    for (a = planet_light + 0x1000; a != planet_light + 0x1800; a += 0x100)
        memcpy(a, planet_light + 0xF00, 0x100);
    planet_299C34();
    return planet_res_file;
}

void planet_299C34(void)
{
    int k, j;
    for (k = 0; k < 3; k++) {
        uchar *lut = planet_shade + 0x200 * (k + 1);
        for (j = 0; j < 0x1800; j++)
            planet_shaded[k][j] = lut[planet_light[j]];
    }
}

void planet_299CC4(void)
{
    signed char pal[0x300];
    uchar *ipal = grd_ipal;
    int i, k;
    gr_get_pal(0, 0x100, (uchar *)pal);
    for (i = 0; i < 0x100; i++) {
        long r = pal[i * 3], g = pal[i * 3 + 1], b = pal[i * 3 + 2];
        long R = r << 4, G = g << 4, B = b << 4;
        for (k = 1; k <= 8; k++) {
            long rr, gg, bb;
            R += r; G += g; B += b;
            rr = R < 0x1000 ? R : 0xFFF;
            gg = G < 0x1000 ? G : 0xFFF;
            bb = B < 0x1000 ? B : 0xFFF;
            R = rr; G = gg; B = bb;
            planet_light[0xF00 + k * 0x100 + i] = ipal[(bb >> 7) << 10 | (gg >> 7) << 5 | (rr >> 7)];
        }
    }
}

int planet_299E04(void)
{
    grd_gamma_on = 1;
    gr_set_pal(0x100 - planet_pal_count, planet_pal_count, planet_pal);
    if (grd_ipal)
        ipal_update();
    else
        ipal_alloc();
    rndsky_2A83BC();
    display_2AD150();
    return 0;
}

void planet_299E4C(void)
{
    if (planet_res_file >= 0) {
        planet_mem_left = 0;
        planet_res_file = -1;
    }
}
