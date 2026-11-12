/* unittab.c -- Terra Nova: the 12-byte unit table at 0x442870 (module M048; source name
 * unknown).  Decompiled from __FF.EXE v1.09.
 *
 *   0x27E200 damage_27E200  copy record idx (12 bytes) to dst
 *   0x27E234 damage_27E234  first dword of record idx
 */
#include "g3_c.h"

void damage_27E200(UnitTabRec *dst, long idx)
{
    *dst = unit_tab[idx];
}

long damage_27E234(long idx)
{
    return unit_tab[idx].f0;
}
