/* fnsplit.c -- Terra Nova file name split / merge helpers (the map's "mapld" group; the
 * map loader only uses them).  Decompiled from __FF.EXE; names ours.
 *
 *   0x2F5A30 fn_split          0x2F5B94 fn_merge          0x2F5C24 strncpy0
 *
 * FnParts (0x80): drive letter, dir (up to 0x71 chars, keeps its trailing separator), name (8)
 * at +0x73, extension (3) at +0x7C.
 * Quirks kept: fn_split returns -1 both when a part was cut short and when there is no '.'
 * (in which case the extension is empty); the name is everything up to the first '.' after
 * the directory and the extension everything after it (dots included); after a truncated
 * directory the name still starts after the real last separator.  fn_merge adds '/' after a
 * directory that does not end in '/' or '\'.
 */
#include "t2_b.h"

int fn_split(FnParts *parts, char *path)
{
    int ret = 0;
    char *p = path, *e, *dot;
    size_t len;

    if (p[0] != 0 && p[1] == ':') {
        parts->drive = p[0];
        p += 2;
    } else
        parts->drive = 0;
    for (e = p + strlen(p) - 1; e >= p; e--)
        if (*e == '/' || *e == '\\')
            break;
    len = e + 1 - p;
    if (len >= 0x72) {
        len = 0x71;
        ret = -1;
    }
    memcpy(parts->dir, p, len);
    parts->dir[len] = 0;
    p = e + 1;
    dot = t2b_strchr(p, '.');
    if (dot == NULL)
        dot = p + strlen(p);
    len = dot - p;
    if (len >= 9) {
        len = 8;
        ret = -1;
    }
    memcpy(parts->name, p, len);
    parts->name[len] = 0;
    if (*dot == 0) {
        parts->ext[0] = 0;
        return -1;
    }
    p = dot + 1;
    len = strlen(p);
    if (len >= 4) {
        len = 3;
        ret = -1;
    }
    memcpy(parts->ext, p, len);
    parts->ext[len] = 0;
    return ret;
}

char *fn_merge(char *dst, FnParts *parts)
{
    char *d = dst, *s;

    if (parts->drive) {
        *d++ = parts->drive;
        *d++ = ':';
    }
    for (s = parts->dir; *s; )
        *d++ = *s++;
    if (parts->dir[0] != 0 && d[-1] != '/' && d[-1] != '\\')
        *d++ = '/';
    for (s = parts->name; *s; )
        *d++ = *s++;
    *d = 0;
    if (parts->ext[0] != 0) {
        *d++ = '.';
        t2b_strcpy(d, parts->ext);
    }
    return dst;
}

/* strncpy that always terminates (dst needs n + 1 bytes); unreferenced. */
char *strncpy0(char *dst, char *src, size_t n)
{
    char *r = strncpy(dst, src, n);

    dst[n] = 0;
    return r;
}
