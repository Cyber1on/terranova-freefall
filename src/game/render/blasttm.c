/* blasttm.c -- Terra Nova module M086 (0x2AC530-0x2AC79F; source file name unknown, the error
 * string says BlastFindTmap): blasted terrain textures.  Decompiled from __FF.EXE v1.09.
 *
 *   0x2AC530 blast_2AC530 set up the cache      0x2AC5B0 blast_2AC5B0 free it
 *   0x2AC5BC BlastFindTmap texture with blast marks  0x2AC6D0 blast_2AC6D0 noise table
 *
 * A small cache of 4 blasted copies (0x1000 bytes, 64 x 64) of planet textures: slot k of
 * blast_cache (1..4; slot 0 unused) holds the planet texture blast_cache[k].owner with one of
 * the four delta frames 0x48C0001..4 (by texture number mod 4) painted over it; blast_map gives
 * the slot of each texture (0 none).  The least recently used slot is replaced.
 * blast_2AC6D0 fills blast_noise with 256 random pixels of the frame 0x48C0002.
 *
 * Quirks kept:
 *  - BlastFindTmap reports "internal error 1" when no slot is older than 0xFFFFFFFF but goes on
 *    with slot 0 (NULL buffer).
 *  - blast_2AC6D0 points the frame's bitmap at the malloc'd unpack buffer and frees it, leaving
 *    the resource's bits pointer dangling.
 *
 * The port: port_blast_live says the copies' block is allocated; the sharp view (sharp3d.c)
 * keeps it over its passes, which put back blast_cache and blast_map with the data object.  The
 * effects pack's BlastMarks (marks.c) are told of a new map and of blast_2AC6D0's picks.
 */
#include "g5_c.h"
#include "res.h"
#include "2d.h"
#include "g1_c.h"

#ifndef TN_WATCOM_ABI
int port_blast_live;
void port_marks_map(void);
void port_marks_pick(int i, long at, int w, int h);
#endif

void blast_2AC530(void)
{
    uchar *buf = Malloc(0x4000);
    memset(blast_cache, 0, sizeof(blast_cache));
    blast_cache[1].buf = buf;
    blast_cache[1].owner = 0xFF;
    blast_cache[2].owner = 0xFF;
    blast_cache[3].owner = 0xFF;
    blast_cache[2].buf = buf + 0x1000;
    blast_cache[4].owner = 0xFF;
    blast_cache[3].buf = buf + 0x2000;
    blast_cache[4].buf = buf + 0x3000;
    memset(blast_map, 0, sizeof(blast_map));
    blast_clock = 0;
#ifndef TN_WATCOM_ABI
    port_blast_live = 1;
    port_marks_map();
#endif
}

void blast_2AC5B0(void)
{
    Free(blast_cache[1].buf);
#ifndef TN_WATCOM_ABI
    port_blast_live = 0;
#endif
}

uchar *BlastFindTmap(long tex)
{
    BlastTex *b;
    ulong oldest = 0xFFFFFFFF;
    long k, best = 0;
    uchar *fr;

    if (blast_map[tex]) {
        b = &blast_cache[blast_map[tex]];
        b->stamp = blast_clock++;
        return b->buf;
    }
    for (k = 1; k <= 4; k++)
        if (oldest > blast_cache[k].stamp) {
            oldest = blast_cache[k].stamp;
            best = k;
        }
    if (best == 0)
        DbgReportError(1, "BlastFindTmap: internal error 1\n");
    b = &blast_cache[best];
    if (b->owner != 0xFF)
        blast_map[b->owner] = 0;
    blast_map[tex] = best;
    b->owner = tex;
    b->stamp = blast_clock++;
    memcpy(b->buf, planet_tex[tex], 0x1000);
    fr = RefGet(0x48C0001 + tex % 4);
    *(uchar **)fr = fr + 0x1C;
    gr_delta8_unpack(b->buf, fr + 0x1C);
    return b->buf;
}

void blast_2AC6D0(void)
{
    grs_bitmap *bm = RefGet(0x48C0002);
    long size = (long)bm->w * bm->h, i;
    uchar *src = (uchar *)bm + 0x1C;

    bm->bits = Malloc(size);
    gr_rsd8_unpack(src, bm->bits);
#ifndef TN_WATCOM_ABI
    for (i = 0; i < 0x100; i++) {       /* the same picks, one at a time */
        long at = RndRange(game_rnd_text, 0, 0x7FFF) % size;
        blast_noise_tmp[i] = bm->bits[at];
        port_marks_pick((int)i, at, bm->w, bm->h);
    }
#else
    for (i = 0; i < 0x100; i += 2) {
        blast_noise_tmp[i] = bm->bits[RndRange(game_rnd_text, 0, 0x7FFF) % size];
        blast_noise_tmp[i + 1] = bm->bits[RndRange(game_rnd_text, 0, 0x7FFF) % size];
    }
#endif
    memcpy(blast_noise, blast_noise_tmp, 0x100);
    Free(bm->bits);
}
