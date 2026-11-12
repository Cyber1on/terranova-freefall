/* namegen.c -- version string (M120, no assert path), Terra Nova v1.09.  Decompiled from
 * __FF.EXE.
 *
 *   0x2DBDF0 make_version_string   game_version = "<tag[3]>1.09" ("V1.09"), built with lg_sprintf
 *                                  in game_version_buf (called once by game_main)
 */
#include "g1_c.h"

void make_version_string(void)
{
    lg_sprintf(game_version_buf, "%c%d.%s%d", game_version_tag[3], 1, "0", 9);
    g1c_strcpy(game_version, game_version_buf);
}
