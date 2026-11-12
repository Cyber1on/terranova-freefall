/* crtconv.c -- Watcom C runtime number conversions and sprintf as linked into Terra Nova v1.09
 * (utoa.c / itoa.c, ultoa.c / ltoa.c, sprintf.c).  Decompiled from __FF.EXE.
 *
 *   0x2F315E utoa_alphabet (data in code)  0x2F3183 utoa   0x2F31CB itoa
 *   0x2F327E ultoa_alphabet (data in code) 0x2F32A3 ultoa  0x2F32E9 ltoa
 *   0x2F3304 mem_putc                      0x2F3312 sprintf (cdecl)
 *
 * Digits are built backwards in a 40-byte stack buffer (a radix below 2 is the caller's
 * problem: divide error or overflow).  Only radix 10 gets a '-' sign.  sprintf formats with the
 * CRT's __prtf (original code), mem_putc storing into the buffer.
 */
#include "t1_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
const char utoa_alphabet[37] = "0123456789abcdefghijklmnopqrstuvwxyz";
const char ultoa_alphabet[37] = "0123456789abcdefghijklmnopqrstuvwxyz";
#endif

char *utoa(unsigned value, char *buf, int radix)
{
    char tmp[40], *q = tmp + 1, *p = buf;

    tmp[0] = '\0';
    do {
        *q++ = utoa_alphabet[value % (unsigned)radix];
        value /= (unsigned)radix;
    } while (value != 0);
    do
        *p = *--q;
    while (*p++ != '\0');
    return buf;
}

char *itoa(int value, char *buf, int radix)
{
    char *p = buf;

    if (radix == 10 && value < 0) {
        value = -value;
        *p++ = '-';
    }
    utoa(value, p, radix);
    return buf;
}

char *ultoa(ulong value, char *buf, int radix)
{
    char tmp[40], *q = tmp + 1, *p = buf;

    tmp[0] = '\0';
    do {
        *q++ = ultoa_alphabet[value % (ulong)radix];
        value /= (ulong)radix;
    } while (value != 0);
    do
        *p = *--q;
    while (*p++ != '\0');
    return buf;
}

char *ltoa(long value, char *buf, int radix)
{
    char *p = buf;

    if (radix == 10 && value < 0) {
        value = -value;
        *p++ = '-';
    }
    ultoa(value, p, radix);
    return buf;
}

/* __prtf output callback: specs->ptr (+0) gets the character, specs->count (+0x10) counts. */
void mem_putc(void *specs, int c)
{
    char **pp = specs;

    *(*pp)++ = (char)c;
    ++*(long *)((char *)specs + 0x10);
}

int sprintf(char *buf, const char *fmt, ...)
{
    __builtin_va_list args;
    int n;

    __builtin_va_start(args, fmt);
    n = __prtf(buf, fmt, &args, WATCOM_FN(mem_putc));
    buf[n] = '\0';
    return n;
}
