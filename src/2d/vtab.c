/* vtab.c -- row offset table for the texture mappers (Terra Nova v1.09).
 * Decompiled from __FF.EXE (hand-written asm with self-modifying code; this C does the same).
 *
 *   0x34C640 gri_init_vtab (2d-tmap's name)
 *
 * Returns temp_malloc(bm->h * 4) filled with vtab[i] = i * bm->row (h, row unsigned 16 bit).
 * Quirks kept: the result is not checked for NULL, and the loop is a do-while on a negative
 * byte counter, so h == 0 stores vtab[0] = 0 into the empty block and then runs on for 2^30
 * entries (the original patches the store address into the loop instruction).
 */
#include "2d.h"

long *gri_init_vtab(grs_bitmap *bm)
{
    long *vtab = temp_malloc((ushort)bm->h * 4);
    long *end = vtab + (ushort)bm->h;
    long off = -(long)(ushort)bm->h, v = 0;

    do {
        end[off] = v;
        v += (ushort)bm->row;
    } while (++off != 0);
    return vtab;
}
