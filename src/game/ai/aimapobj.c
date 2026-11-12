/* aimapobj.c -- Terra Nova map object loading (module M113, 0x2D3010-0x2D30DF).
 * Decompiled from __FF.EXE v1.09.
 *
 *   0x2D3010 aihelp_2D3010  clear game_39A3F4
 *   0x2D301C aihelp_2D301C  load the map's tile-object table (resource 0x53 into game_3C5E1C)
 *                           and the 30 compound object resources 0x78..0x95: every item is
 *                           extracted into game_442F90 and handed to map_2941E8, then
 *                           tileobj_298D2C starts the next group; tileobj_299004 numbers them
 *   0x2D30D8 aihelp_2D30D8  return -5
 *
 * Quirks kept: only resource 0x78 is tested for being in use; 0x79..0x95 are read
 * unconditionally (ResExtractRefTable of a missing id is left to the resource code); the ref
 * table buffer is 0x48 bytes on the stack, read with a size limit of 0x46.
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_c2.h"
#include "g5_a.h"     /* added at the G5-G8 merge */

void aihelp_2D3010(void)
{
    game_39A3F4 = 0;
}

void aihelp_2D301C(void)
{
    union { RefTable t; uchar b[0x48]; } rt;
    long k;
    ulong id;

    tileobj_298C74();
    if (ResInUse(0x53))
        ResExtract(0x53, game_3C5E1C);
    if (!ResInUse(0x78))
        return;
    for (k = 0, id = 0x78; k < 0x1E; k++, id++) {
        ushort j;
        ResExtractRefTable((Id)id, &rt.t, 0x46);
        for (j = 0; j < rt.t.numRefs; ) {
            RefExtract(&rt.t, ((ulong)(ushort)id << 16) | j, game_442F90);
            j++;
            map_2941E8(game_442F90);
        }
        tileobj_298D2C();
    }
    tileobj_299004();
}

long aihelp_2D30D8(void)
{
    return -5;
}
