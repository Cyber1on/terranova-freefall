/* ressimld.c -- Terra Nova module M109 (source name unknown): unit table of a skill level.
 * Decompiled from __FF.EXE v1.09.
 *
 *   0x2CDBB0 ressim_2CDBB0 (script native)
 *
 * Sets the skill level (game_442900) and extracts item <skill> of resource 0x3A5 of
 * ressim.res into unit_tab.  As a native its eax goes onto the script stack: the original
 * returns what ResCloseFile left in eax (no value), so does this one.
 */
#include "g9_a.h"

void ressim_2CDBB0(long skill)
{
    int fd = ResOpenResFile("ressim.res", 0, 0);
    RefTable *rt;

    game_442900 = (uchar)skill;
    rt = ResLock(0x3A5);
    RefExtract(rt, MKREF(0x3A5, game_442900), unit_tab);
    ResUnlock(0x3A5);
    ResCloseFile(fd);
}
