/* image.c -- loads the original program's code and data objects from the player's __FF.EXE.
 *
 * __FF.EXE is a DOS/4GW "LE" executable: a DOS stub followed by an LE header, an object table,
 * a page map, the pages of each object and fixup records (places in an object that hold an
 * address, and what they point at).  We copy objects 1 (code) and 3 (data) into tn_code_obj /
 * tn_data_obj and patch every 32-bit address they hold so it points into the port instead:
 *   - into the data object -> tn_data_obj + offset
 *   - into the code object -> our function when a routine starts there (port_funcs), else
 *                             tn_code_obj + offset (data kept in the code segment)
 * The decompiled C then finds the game's initialised tables exactly where it expects them.
 * Same parsing as tools/le_image.py (verification side).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"

/* The program inside the English v1.09 __FF.EXE (GOG / Steam): the code and data objects as
 * stored (before fixups) and the fixup tables.  The whole file is not checked, because the DOS
 * extender bound in front of the program is not the same in every release. */
#define FF_CODE_CRC 0x9428E73Cul
#define FF_DATA_CRC 0x3E99893Eul
#define FF_FIXUP_CRC 0xB490F87Eul

static unsigned long u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned long)p[3] << 24; }
static unsigned u16(const unsigned char *p) { return p[0] | p[1] << 8; }

static unsigned long crc32(const unsigned char *p, unsigned long n)
{
    unsigned long c = 0xFFFFFFFFul;
    int k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320ul & (0ul - (c & 1)));
    }
    return c ^ 0xFFFFFFFFul;
}

/* ---------------------------------------------------------------- address translation */

void *port_func_at(unsigned long a)
{
    int lo = 0, hi = port_nfuncs - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (port_funcs[m].addr == a)
            return port_funcs[m].fn;
        if (port_funcs[m].addr < a)
            lo = m + 1;
        else
            hi = m - 1;
    }
    return 0;
}

const char *port_func_name(unsigned long a)
{
    int i;
    for (i = 0; i < port_nfuncs; i++)
        if (port_funcs[i].addr == a)
            return port_funcs[i].name;
    return 0;
}

static const port_func **by_fn;
static int cmp_fn(const void *a, const void *b)
{
    const char *x = (*(const port_func **)a)->fn, *y = (*(const port_func **)b)->fn;
    return x < y ? -1 : x > y;
}

unsigned long port_orig_addr(const void *fn)
{
    int lo = 0, hi = port_nfuncs - 1, i;
    if (!by_fn) {
        by_fn = malloc(port_nfuncs * sizeof *by_fn);
        if (!by_fn)
            return 0;
        for (i = 0; i < port_nfuncs; i++)
            by_fn[i] = &port_funcs[i];
        qsort(by_fn, port_nfuncs, sizeof *by_fn, cmp_fn);
    }
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if ((const char *)by_fn[m]->fn == (const char *)fn)
            return by_fn[m]->addr;
        if ((const char *)by_fn[m]->fn < (const char *)fn)
            lo = m + 1;
        else
            hi = m - 1;
    }
    return 0;
}

void *port_ptr(unsigned long a)
{
    if (a >= TN_CODE_BASE && a < TN_CODE_BASE + TN_CODE_SIZE)
        return tn_code_obj + (a - TN_CODE_BASE);
    if (a >= TN_DATA_BASE && a < TN_DATA_BASE + TN_DATA_SIZE)
        return tn_data_obj + (a - TN_DATA_BASE);
    return 0;
}

/* ---------------------------------------------------------------- loader */

static unsigned char *read_file(const char *path, unsigned long *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = n > 0 ? malloc(n) : 0;
    if (buf && fread(buf, 1, n, f) != (size_t)n) {
        free(buf);
        buf = 0;
    }
    fclose(f);
    *size = buf ? (unsigned long)n : 0;
    return buf;
}

/* The LE header: an "MZ" header whose new-header offset (+0x3C) points at "LE\0\0" with the
 * game's layout.  In the GOG / Steam file it is the second MZ, after the DOS/4GW loader. */
static int find_le(const unsigned char *d, unsigned long size, unsigned long *base, unsigned long *le)
{
    unsigned long pos, at;

    for (pos = 0; pos + 0x40 <= size; pos++) {
        if (d[pos] != 'M' || d[pos + 1] != 'Z')
            continue;
        at = pos + u32(d + pos + 0x3C);
        if (at < pos || at > size || size - at < 0xB0 || memcmp(d + at, "LE\0\0", 4) != 0)
            continue;
        if (u32(d + at + 0x28) != 4096 || u32(d + at + 0x44) != 3)
            continue;
        *base = pos;
        *le = at;
        return 0;
    }
    return 1;
}

const char *port_load_image(const char *exe_path)
{
    static char msg[300];
    unsigned long size, base, le, page_size, last_page, total_pages, nobj, i, j, fix_end, fix_crc;
    unsigned long unmapped = 0;
    int k;
    unsigned char *d = read_file(exe_path, &size);
    unsigned char *img[3] = { tn_code_obj, 0, tn_data_obj };
    unsigned long img_size[3] = { TN_CODE_SIZE, 0, TN_DATA_SIZE };

    if (!d) {
        snprintf(msg, sizeof msg, "cannot read %s", exe_path);
        return msg;
    }
    if (find_le(d, size, &base, &le)) {
        free(d);
        snprintf(msg, sizeof msg, "%s: no DOS/4GW program found in it (%lu bytes)", exe_path, size);
        return msg;
    }
    page_size = u32(d + le + 0x28);
    last_page = u32(d + le + 0x2C);
    total_pages = u32(d + le + 0x14);
    nobj = u32(d + le + 0x44);
    fix_end = le + u32(d + le + 0x6C);
    if (le + u32(d + le + 0x68) + 4 * (total_pages + 1) <= size)
        fix_end += u32(d + le + u32(d + le + 0x68) + 4 * total_pages);
    if (page_size != 4096 || nobj != 3 || fix_end > size || le + u32(d + le + 0x40) + 3 * 24 > size ||
        le + u32(d + le + 0x48) + 4 * total_pages > size) {
        free(d);
        snprintf(msg, sizeof msg, "%s: unexpected LE layout (%lu bytes)", exe_path, size);
        return msg;
    }
    fix_crc = crc32(d + le + u32(d + le + 0x68), fix_end - (le + u32(d + le + 0x68)));

    /* pass 1: copy the pages of objects 1 and 3.  The page offsets count from the start of the
     * bound program in the DOS/4GW layout, from the start of the file in others: try both. */
    for (k = 0; k < 2; k++) {
        unsigned long pages = (k ? 0 : base) + u32(d + le + 0x80);
        int ok = 1;
        for (i = 0; i < nobj && ok; i++) {
            const unsigned char *o = d + le + u32(d + le + 0x40) + i * 24;
            unsigned long osize = u32(o), index = u32(o + 12), count = u32(o + 16);
            if (!img[i])
                continue;
            if (osize != img_size[i]) {
                ok = 0;
                break;
            }
            memset(img[i], 0, osize);
            for (j = 0; j < count; j++) {
                const unsigned char *pm = d + le + u32(d + le + 0x48) + 4 * (index + j - 1);
                unsigned long pn = (unsigned long)pm[0] << 16 | pm[1] << 8 | pm[2];
                unsigned long n = pn == total_pages ? last_page : 4096, at;
                if (j * 4096 >= osize)
                    break;
                if (n > osize - j * 4096)
                    n = osize - j * 4096;
                at = pages + (pn - 1) * 4096;
                if (at > size || n > size - at) {
                    ok = 0;
                    break;
                }
                memcpy(img[i] + j * 4096, d + at, n);
            }
        }
        if (ok && crc32(tn_code_obj, TN_CODE_SIZE) == FF_CODE_CRC && crc32(tn_data_obj, TN_DATA_SIZE) == FF_DATA_CRC)
            break;
        if (!k)
            port_log("image: code %08lX data %08lX fixups %08lX\n", crc32(tn_code_obj, TN_CODE_SIZE),
                     crc32(tn_data_obj, TN_DATA_SIZE), fix_crc);
    }
    if (k == 2 || fix_crc != FF_FIXUP_CRC) {
        snprintf(msg, sizeof msg, "%s is not the English v1.09 __FF.EXE (the GOG / Steam version): %lu bytes, "
                 "CRC %08lX", exe_path, size, crc32(d, size));
        free(d);
        return msg;
    }

    /* pass 2: fixups (after all pages are in, as records may straddle pages) */
    for (i = 0; i < nobj; i++) {
        const unsigned char *o = d + le + u32(d + le + 0x40) + i * 24;
        unsigned long index = u32(o + 12), count = u32(o + 16);
        if (!img[i])
            continue;
        for (j = 0; j < count; j++) {
            unsigned long f0 = u32(d + le + u32(d + le + 0x68) + 4 * (index + j - 1));
            unsigned long f1 = u32(d + le + u32(d + le + 0x68) + 4 * (index + j));
            const unsigned char *p = d + le + u32(d + le + 0x6C) + f0;
            const unsigned char *end = d + le + u32(d + le + 0x6C) + f1;
            while (p < end) {
                unsigned st = p[0], fl = p[1], typ = st & 15, target;
                long off = (short)u16(p + 2);
                unsigned long dest = 0, value;
                target = p[4];
                p += 5;
                if (typ != 2) {
                    if (fl & 0x10) {
                        dest = u32(p);
                        p += 4;
                    } else {
                        dest = u16(p);
                        p += 2;
                    }
                }
                off += (long)(j * 4096);
                if (typ != 7 || off < 0 || (unsigned long)off + 4 > img_size[i])
                    continue;            /* selectors (2) and relative calls (8) are code-only */
                if (target == 3)
                    value = (unsigned long)(tn_data_obj + dest);
                else if (target == 1) {
                    void *fn = port_func_at(TN_CODE_BASE + dest);
                    value = fn ? (unsigned long)fn : (unsigned long)(tn_code_obj + dest);
                } else {
                    value = 0;
                    unmapped++;
                }
                img[i][off] = (unsigned char)value;
                img[i][off + 1] = (unsigned char)(value >> 8);
                img[i][off + 2] = (unsigned char)(value >> 16);
                img[i][off + 3] = (unsigned char)(value >> 24);
            }
        }
    }
    free(d);
    if (unmapped)
        port_log("image: %lu pointers into the DOS/4GW stub object left 0\n", unmapped);
    return 0;
}

/* ---------------------------------------------------------------- static initialisers */

/* The Watcom start-up code runs the routines of the XI table (entries {u8 type, u8 priority,
 * u32 routine}, 0x36EF78-0x36EFDE) before main, lowest priority first (in table order within a
 * priority).  The ones the port has are the C++ file-scope initialisers of the EDMS physics
 * (priority 64: constants such as the integration slice) and the argv builder; the others are
 * C runtime internals (FPU, files, heap) that the host's runtime does itself. */
#define XI_START 0x36EF78ul
#define XI_END 0x36EFDEul

void port_static_init(void)
{
    unsigned char *t = tn_data_obj + (XI_START - TN_DATA_BASE);
    int n = (int)((XI_END - XI_START) / 6), done[32] = { 0 }, k, i, best;

    for (k = 0; k < n; k++) {
        best = -1;
        for (i = 0; i < n; i++)
            if (!done[i] && (best < 0 || t[i * 6 + 1] < t[best * 6 + 1]))
                best = i;
        done[best] = 1;
        {
            unsigned char *e = t + best * 6;
            unsigned long fn = e[2] | e[3] << 8 | e[4] << 16 | (unsigned long)e[5] << 24;
            if (fn && (fn < (unsigned long)tn_code_obj || fn >= (unsigned long)tn_code_obj + TN_CODE_SIZE))
                ((void (*)(void))fn)();
        }
    }
}
