/* memcpy and strlen for the C build.  The original code has no such functions: Watcom inlined
 * them (rep movsd / rep movsb, repne scasb).  This is support code, not part of the game. */
#include "lg.h"

void *memcpy(void *d, const void *s, size_t n)
{
    uchar *dp = d;
    const uchar *sp = s;
    while (n--)
        *dp++ = *sp++;
    return d;
}

/* strlen: likewise inlined (repne scasb) in the original. */
size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}
