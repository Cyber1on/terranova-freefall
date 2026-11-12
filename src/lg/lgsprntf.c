/* lgsprntf.c -- x:\prj\tech\libsrc\lg\lgsprntf.c (Terra Nova v1.09): "no-floats baby sprintf".
 * Decompiled from __FF.EXE.
 *
 *   0x2DEBE0 lg_sprintf (cdecl)   0x2DEC08 lg_vsprintf   0x2DF418 lg_sprintf_install_stringfunc
 *   0x2DF420 lgs_int_to_str       0x2DF468 lgs_uint_to_str   (static in the original, names ours)
 *
 * Same as System Shock's lgsprntf.c 1.12 (no LGSPF_SMALLINT_OPT), plus:
 *  - %q and %Q are synonyms of %f and %F.
 *  - The case of the conversion picks the format: upper case (%F, %Q) is a 16.16 fix, lower
 *    case (%f, %q) a 24.8 fix24 (the reverse of the Shock comment).
 * Quirks kept from the original:
 *  - %o without '#' reads a second argument and prints that one (unmasked), Shock bug.
 *  - %b reads only the low byte of its argument.
 *  - newchars is not reset between conversions: a conversion that sets no length (%s with a
 *    NULL string, an unknown letter) reuses the previous length.  Before the first conversion
 *    it is whatever the caller left in ecx (the variable lives in ecx and is never
 *    initialized).  We model that as a 4th argument, ecx_in: lg_sprintf passes the caller's
 *    ecx (we pass 0), DbgHandleC passes strlen(prefix) (what its inlined strlen leaves in ecx).
 *  - 'big' (h/l) persists across conversions; it is only set at the start of the call.
 *  - %S without an installed string function writes a 0 and returns -1.
 * isalpha/isupper are Watcom's ctype table (_IsTable).
 */
#include "sup_a.h"

#define MAX_FIX_PRECIS 4

#ifndef TN_LINK_ORIGINAL_DATA
LgsStringFunc lgs_string_func;
#endif

static char *const boolstring[] = { "FALSE", "TRUE" };                 /* 0x35F404 */
static const int pten[MAX_FIX_PRECIS + 1] = { 1, 10, 100, 1000, 10000 }; /* 0x35F40C */

enum { STAGE_TEXT, STAGE_FLAGS, STAGE_FWID, STAGE_PRECISION };
enum { SMALL, DEFAULT, BIG };

#define CTYPE(c) (_IsTable[(uchar)((c) + 1)])

int lg_sprintf(char *buf, char *fmt, ...)
{
    char *va[1];
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    va[0] = ap;  /* i386: va_list is the argument pointer */
    return lg_vsprintf(buf, fmt, va, 0);
}

int lg_vsprintf(char *buf, char *format, void *args, int ecx_in)
{
    char **ap = args;
    char *arg_str;
    uint arg_uint;
    long arg_fix;
    bool arg_bool;
    char fix_frac_buf[5];
    bool ladjust = 0, altform = 0, pspec = 0, this_is_len;
    uchar big, stage;
    char pad_char = ' ', src_char;
    int dest_ind, src_ind, len, int_part, frac_part, mult;
    int fwid = 0, precis = 0, newchars = ecx_in, fshift, shift_ind, prefix;

    if (buf == NULL)
        return 0;
    dest_ind = src_ind = 0;
    stage = STAGE_TEXT;
    big = DEFAULT;

    while ((src_char = format[src_ind++]) != 0) {
        this_is_len = FALSE;
        if (stage == STAGE_TEXT) {
            if (src_char == '%') {
                stage = STAGE_FLAGS;
                pad_char = ' ';
                fwid = precis = 0;
                ladjust = altform = pspec = FALSE;
            } else
                buf[dest_ind++] = src_char;
            continue;
        }
        switch (src_char) {
        case '.':
            if (stage < STAGE_PRECISION)
                stage = STAGE_PRECISION;
            break;
        case '-':
            if (stage == STAGE_FLAGS)
                ladjust = TRUE;
            break;
        case '#':
            if (stage == STAGE_FLAGS)
                altform = TRUE;
            break;
        case '0':
            if (stage == STAGE_FLAGS) {
                pad_char = '0';
                break;
            }
            /* fall through */
        case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            if (stage < STAGE_FWID)
                stage = STAGE_FWID;
            if (stage == STAGE_FWID)
                fwid = fwid * 10 + (src_char - '0');
            else {
                pspec = TRUE;
                precis = precis * 10 + (src_char - '0');
            }
            break;
        case '%':
            buf[dest_ind++] = '%';
            stage = STAGE_TEXT;
            break;
        case 'h':
            this_is_len = TRUE;
            big = SMALL;
            break;
        case 'L':
        case 'l':
            this_is_len = TRUE;
            big = BIG;
            break;
        case 'n':
            newchars = fwid = 0;
            *LG_VA_ARG(ap, int *) = dest_ind;
            break;
        case 'i':
        case 'd':
            newchars = lgs_int_to_str(buf + dest_ind, LG_VA_ARG(ap, int));
            break;
        case 'u':
            arg_uint = LG_VA_ARG(ap, uint);
            if (big == SMALL)
                arg_uint &= 0xFFFF;
            newchars = lgs_uint_to_str(buf + dest_ind, arg_uint, 10, 0);
            break;
        case 'B':
            arg_uint = LG_VA_ARG(ap, uint);
            buf[dest_ind] = '0';
            buf[dest_ind + 1] = 'b';
            newchars = 2 + lgs_uint_to_str(buf + dest_ind + 2, arg_uint, 2, 0);
            break;
        case 'p':
            src_char = 'X';
            /* fall through */
        case 'x':
        case 'X':
            arg_uint = LG_VA_ARG(ap, uint);
            if (big == SMALL)
                arg_uint &= 0xFFFF;
            if (altform && arg_uint != 0) {
                buf[dest_ind] = '0';
                buf[dest_ind + 1] = src_char;
                newchars = 2 + lgs_uint_to_str(buf + dest_ind + 2, arg_uint, 16, src_char - 'X' + 'A');
            } else
                newchars = lgs_uint_to_str(buf + dest_ind, arg_uint, 16, src_char - 'X' + 'A');
            break;
        case 'o':
            arg_uint = LG_VA_ARG(ap, uint);
            if (big == SMALL)
                arg_uint &= 0xFFFF;
            if (altform) {
                buf[dest_ind] = '0';
                newchars = 1 + lgs_uint_to_str(buf + dest_ind + 1, arg_uint, 8, 0);
            } else /* bug: prints the next argument instead */
                newchars = lgs_uint_to_str(buf + dest_ind, LG_VA_ARG(ap, uint), 8, 0);
            break;
        case 'c':
            buf[dest_ind] = (char)LG_VA_ARG(ap, int);
            newchars = 1;
            break;
        case 'b':
            arg_bool = LG_VA_ARG(ap, uchar) != 0;
            if (altform) {
                buf[dest_ind] = boolstring[arg_bool][0];
                newchars = 1;
            } else {
                lg_strcpy(buf + dest_ind, boolstring[arg_bool]);
                newchars = 5 - arg_bool;
            }
            break;
        case 'S':
            if (lgs_string_func == NULL) {
                buf[dest_ind] = 0;
                return -1;
            }
            arg_str = (char *)WATCOM_CALL(lgs_string_func, LG_VA_ARG(ap, ulong));
            goto string_copy;
        case 's':
            arg_str = LG_VA_ARG(ap, char *);
        string_copy:
            if (arg_str) {
                if (pspec)
                    strncpy(buf + dest_ind, arg_str, precis);
                else
                    lg_strcpy(buf + dest_ind, arg_str);
                newchars = lg_strlen(arg_str);
                if (pspec && newchars > precis)
                    newchars = precis;
            }
            break;
        case 'F':
        case 'Q':
        case 'f':
        case 'q':
            if (!pspec || precis > MAX_FIX_PRECIS)
                precis = MAX_FIX_PRECIS;
            mult = pten[precis];
            arg_fix = LG_VA_ARG(ap, long);
            if (CTYPE(src_char) & LG_CT_UPPER) {        /* fix 16.16 */
                int_part = arg_fix >> 16;
                frac_part = ((arg_fix & 0xFFFF) * mult + 0x8000) >> 16;
            } else {                                     /* fix24 24.8 */
                int_part = arg_fix >> 8;
                frac_part = ((arg_fix & 0xFF) * mult + 0x80) >> 8;
            }
            if (frac_part >= mult) {
                frac_part = 0;
                int_part++;
            }
            if (int_part < 0 && frac_part != 0) {
                int_part++;
                frac_part = mult - frac_part;
                if (int_part == 0)
                    buf[dest_ind++] = '-';
            }
            newchars = lgs_int_to_str(buf + dest_ind, int_part);
            if (precis != 0) {
                buf[dest_ind + newchars] = '.';
                len = lgs_int_to_str(fix_frac_buf, frac_part);
                memset(buf + dest_ind + newchars + 1, '0', precis);
                lg_strcpy(buf + dest_ind + newchars + precis + 1 - len, fix_frac_buf);
                newchars += precis + 1;
            } else if (altform) {
                buf[dest_ind + newchars] = '.';
                newchars++;
            }
            break;
        }

        if ((CTYPE(src_char) & (LG_CT_UPPER | LG_CT_LOWER)) && !this_is_len) {
            stage = STAGE_TEXT;
            if (newchars < fwid) {
                fshift = fwid - newchars;
                if (ladjust)
                    memset(buf + dest_ind + newchars, ' ', fshift);
                else {
                    /* non-numeric fields are never 0-padded */
                    if (src_char == 's' || src_char == 'S' || src_char == 'c' || src_char == 'b')
                        pad_char = ' ';
                    prefix = 0;
                    if (pad_char == '0') {
                        if (buf[dest_ind] == '-')
                            prefix = 1;
                        else if (altform) {
                            if (src_char == 'o')
                                prefix = 1;
                            else if (src_char == 'x' || src_char == 'X')
                                prefix = 2;
                        }
                    }
                    for (shift_ind = dest_ind + fwid - 1; shift_ind >= dest_ind + fshift + prefix; shift_ind--)
                        buf[shift_ind] = buf[shift_ind - fshift];
                    memset(buf + dest_ind + prefix, pad_char, fshift);
                }
                dest_ind += fwid;
            } else
                dest_ind += newchars;
        }
    }
    buf[dest_ind] = '\0';
    return dest_ind;
}

/* Installs the string function used by %S (e.g. RefGet-like: string number -> char *). */
void lg_sprintf_install_stringfunc(LgsStringFunc f)
{
    lgs_string_func = f;
}

/* Writes val in decimal; returns the length. */
int lgs_int_to_str(char *buf, int val)
{
    int len;

    if (val < 0) {
        buf[0] = '-';
        if (val == (int)0x80000000) {
            len = lgs_int_to_str(buf + 1, 214748364);
            buf[1 + len] = '8';
            buf[2 + len] = '\0';
            return 2 + len;
        }
        return 1 + lgs_int_to_str(buf + 1, -val);
    }
    return lgs_uint_to_str(buf, (uint)val, 10, 0);
}

/* Writes val in the given base; alph is the first letter for digits above 9 ('a' or 'A').
 * Base 8 and 16 use shifts.  Digits are compared as signed chars. */
int lgs_uint_to_str(char *buf, uint val, int base, char alph)
{
    int ind, rev;
    char tmp;

    alph -= 10;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    ind = 0;
    switch ((uint)base) {
    case 8:
        while (val > 0) {
            buf[ind++] = (val & 7) + '0';
            val >>= 3;
        }
        break;
    case 16:
        while (val > 0) {
            tmp = val & 0xF;
            buf[ind++] = tmp + (tmp < 10 ? '0' : alph);
            val >>= 4;
        }
        break;
    default:
        while (val > 0) {
            tmp = val % (uint)base;
            buf[ind++] = tmp + (tmp < 10 ? '0' : alph);
            val /= (uint)base;
        }
        break;
    }
    for (rev = 0; rev < (ind >> 1); rev++) {
        tmp = buf[rev];
        buf[rev] = buf[ind - rev - 1];
        buf[ind - rev - 1] = tmp;
    }
    buf[ind] = '\0';
    return ind;
}
