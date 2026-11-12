/* rsd8.c -- RSD8 (run/skip/dump) unpack into a flat8 bitmap (Terra Nova v1.09, asm original;
 * probably x:\prj\tech\libsrc\2d\rsdunpck.asm's row-aware blitter).
 *   0x34E6E0 gri_rsd8_blit (name from 2d-tmap; caller fl8rsd8.c)
 *
 * Stream codes (one byte op):
 *   0x00 n v        run: n copies of v
 *   0x01..0x7F      dump: that many literal bytes follow
 *   0x81..0xFF      skip (op & 0x7F) pixels
 *   0x80 w16        w == 0: end; w < 0x8000: skip w; w < 0xC000: dump (w & 0x7FFF) literal bytes;
 *                   else run of (w & 0x3FFF) copies of the following byte
 * row == w takes a contiguous path that ignores row ends.  Otherwise pixels wrap at w and the
 * destination steps by row - w at each row end.
 * Quirks kept: the short skip tests "remaining > count" unsigned (ja), the long skip tests
 * signed (jle); with w == 0 and row != 0 a skip loops forever (as the original).
 * The asm returns with popal, so eax is the src pointer unchanged.
 */
#include "2d.h"

long gri_rsd8_blit(uchar *src, uchar *dst, ulong row, int w)
{
    uchar *s = src, *d = dst;
    long skip = (long)row - w;     /* ebx */
    long rem = w;                  /* edx: pixels left in this row */
    ulong n;
    int lit, v = 0;

    if (skip == 0) {
        for (;;) {
            uchar op = *s;
            if (op == 0) { n = s[1]; v = s[2]; s += 3; while (n--) *d++ = (uchar)v; }
            else if (op < 0x80) { n = op; s++; while (n--) *d++ = *s++; }
            else if (op > 0x80) { d += op & 0x7F; s++; }
            else {
                n = s[1] | (s[2] << 8);
                if (n < 0x8000) { if (n == 0) return (long)src; s += 3; d += n; }
                else if (n < 0xC000) { n &= 0x7FFF; s += 3; while (n--) *d++ = *s++; }
                else { n &= 0x3FFF; v = s[3]; s += 4; while (n--) *d++ = (uchar)v; }
            }
        }
    }
    for (;;) {
        uchar op = *s;
        if (op == 0) { n = s[1]; v = s[2]; s += 3; lit = 0; }
        else if (op < 0x80) { n = op; s++; lit = 1; }
        else if (op > 0x80) {
            n = op & 0x7F; s++; d += n;
            if ((ulong)rem > n) { rem -= n; continue; }
            rem -= n;
            do { d += skip; rem += w; } while (rem <= 0);
            continue;
        } else {
            n = s[1] | (s[2] << 8);
            if (n < 0x8000) {
                if (n == 0) return (long)src;
                s += 3; d += n; rem -= n;
                if (rem <= 0) do { d += skip; rem += w; } while (rem <= 0);
                continue;
            }
            if (n < 0xC000) { n &= 0x7FFF; s += 3; lit = 1; }
            else { n &= 0x3FFF; v = s[3]; s += 4; lit = 0; }
        }
        /* write n pixels (literal or run), wrapping at row ends */
        rem -= (long)n;
        if (rem > 0) {
            if (lit) while (n--) *d++ = *s++; else while (n--) *d++ = (uchar)v;
            continue;
        }
        {
            long c = (long)n + rem;          /* the part that fits in this row */
            for (;;) {
                if (lit) while (c-- > 0) *d++ = *s++; else while (c-- > 0) *d++ = (uchar)v;
                d += skip;
                c = -rem;                    /* what is left */
                rem += w;
                if (rem > 0) break;
                c += rem;                    /* a whole row (rem <= 0 again) */
            }
            if (lit) while (c-- > 0) *d++ = *s++; else while (c-- > 0) *d++ = (uchar)v;
        }
    }
}
