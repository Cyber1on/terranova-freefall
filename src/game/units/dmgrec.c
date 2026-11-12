/* dmgrec.c -- Terra Nova module M047 (one function; source name unknown).
 * Decompiled from __FF.EXE v1.09.
 *
 *   0x27E1E0 damage_27E1E0  store three words and a dword into a 10-byte record (HUD callers)
 */
#include "g3_a.h"

void damage_27E1E0(short *rec, short a, short b, short c, long d)
{
    rec[0] = a;
    rec[1] = b;
    rec[2] = c;
    *(long *)(rec + 3) = d;
}
