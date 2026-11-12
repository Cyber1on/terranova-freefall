/* orig_crt.c -- the last original routines of the C runtime and the HMD drivers, for the
 * standalone build (the verification build still runs the original code for them).
 *
 *   0x313F6B __prtf          Watcom's printf engine, used by the game's sprintf: the host's
 *                            vsnprintf (the game's formats are plain %d %s %x %02d ...)
 *   0x317DB5 rand            Watcom's rand / srand (0x317DD9), with the original's seed (0x3654F0):
 *                            the star field is made with it, so the host's rand would give
 *                            other stars
 *   0x2FB71F crt_2FB71F      fscanf (haze.c's camera path playback): the host's vfscanf on the
 *                            port's FILE
 *   0x2FD8E0 hmd_null_init   the "no head-mounted display" driver: init fails (-1)
 *   0x2FD8E8 hmd_null_func   returns 0
 *   0x2FDB80 hmd_2FDB80      TE6 driver: recentre (the current angles become the reference)
 *   0x2FDC60 te6_2FDC60      TE6 driver: enable tracking if the driver has the capability
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"

#define TN_DATA(a) (tn_data_obj + ((a) - TN_DATA_BASE))

/* putc(specs, c) stores c at specs->ptr (+0) and counts in specs->count (+0x10) */
int __prtf(void *dest, const char *fmt, char **args, void (*putc_fn)(void *specs, int c))
{
    struct { char *ptr; long r1[3]; long count; long r2[11]; } specs;
    char small[256], *s = small;
    va_list ap;
    int n, i;

    memset(&specs, 0, sizeof specs);
    specs.ptr = dest;
    va_copy(ap, *(va_list *)args);
    n = vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof small) {
        s = malloc(n + 1);
        if (!s)
            return 0;
        va_copy(ap, *(va_list *)args);
        vsnprintf(s, n + 1, fmt, ap);
        va_end(ap);
    }
    for (i = 0; i < n; i++)
        putc_fn(&specs, (unsigned char)s[i]);
    if (s != small)
        free(s);
    return (int)specs.count;
}

int tn_rand(void)
{
    unsigned long *seed = (unsigned long *)TN_DATA(0x3654F0);
    *seed = *seed * 0x41C64E6Dul + 0x3039;
    return (int)((*seed >> 16) & 0x7FFF);
}

void tn_srand(unsigned seed)
{
    *(unsigned long *)TN_DATA(0x3654F0) = seed;
}

int crt_2FB71F(FILE *fp, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfscanf(fp, fmt, ap);
    va_end(ap);
    return n;
}

long hmd_null_init(void)
{
    return -1;
}

long hmd_null_func(void)
{
    return 0;
}

void hmd_2FDB80(void)
{
    memcpy(TN_DATA(0x3645AC), TN_DATA(0x3645B8), 6);
}

void te6_2FDC60(int on)
{
    unsigned char *drv = *(unsigned char **)TN_DATA(0x3645A0);      /* hmd_name: the driver record */

    *TN_DATA(0x3645D2) = (unsigned char)on && drv[0x1C] ? 1 : 0;
}
