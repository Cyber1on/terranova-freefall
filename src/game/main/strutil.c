/* strutil.c -- string helpers (M119, no assert path; names are the map's), Terra Nova v1.09.
 * Decompiled from __FF.EXE.
 *
 *   0x2DBCE0 strutil_2DBCE0   strncpy(d, s, n - 1) and terminate; returns d
 *   0x2DBCF8 strutil_2DBCF8   draw a string resource, replacing letters at random (garble / 256
 *                             chance per character) with random letters (HUD noise effect)
 *
 * Quirk kept: the string is copied into a 0x28-byte buffer without a length check.
 */
#include "g1_c.h"

char *strutil_2DBCE0(char *d, char *s, int n)
{
    strncpy(d, s, n - 1);
    d[n - 1] = 0;
    return d;
}

void strutil_2DBCF8(ushort font, Ref ref, int x, int y, int garble)
{
    char buf[0x28];
    int i, n, base;

    g1c_strcpy(buf, RefLock(ref));
    ResUnlock(REFID(ref));
    if (garble) {
        n = g1c_strlen(buf);
        for (i = 0; i < n; i++) {
            if ((RndRange((void *)game_rnd_text, 0, 0x7FFF) & 0xFF) < garble) {
                base = (RndRange((void *)game_rnd_text, 0, 0x7FFF) & 1) ? 'A' : 'a';
                buf[i] = RndRange((void *)game_rnd_text, 0, 0x7FFF) % 25 + base;
            }
        }
    }
    GfxDrawText(font, buf, x, y);
}
