/* rsdunpck.c -- x:\prj\tech\libsrc\2d\rsdunpck.asm (Terra Nova v1.09): run-length decoders.
 * Decompiled from __FF.EXE (hand-written asm in the original).
 *
 *   0x2FAF1C gr_delta8_unpack (name ours)     0x2FAFC0 gr_rsd8_unpack
 *
 * gr_rsd8_unpack uses the register convention of System Shock's RSDUNPCK.H
 * (#pragma aux parm [esi] [edi] value [edi]): src in esi, dst in edi, returns the end of the
 * output in edi.  The C function takes (src, dst) and returns the end pointer; the verification
 * build adapts the registers in its test.
 *
 * RSD8 codes: 0 n c = n bytes of c; 1..0x7F = copy that many; 0x81..0xFF = (code & 0x7F)
 * zero bytes; 0x80 w: w = 0 end, < 0x8000 w zero bytes, < 0xC000 copy w & 0x7FFF,
 * else (w & 0x3FFF) bytes of the next byte.  Skips are written as zeros.
 *
 * gr_delta8_unpack (dst, src) decodes a delta frame onto an existing image: 1..0x7F = copy
 * that many, 0 n c = n bytes of c, 0x81..0xFF = skip (code & 0x7F), 0x80 w: w > 0 skip w,
 * w = 0 end, else (w & 0x7FFF) < 0x4000 copy that many, otherwise fill (w & 0x3FFF) with the
 * next byte.  Quirk: a long copy/fill of 0 bytes at an odd address runs away (the asm's
 * byte/word split underflows); the C does nothing.
 */
#include "2d.h"

uchar *gr_rsd8_unpack(uchar *src, uchar *dst)
{
    uint n, w;

    for (;;) {
        uchar code = *src;
        if (code == 0) {
            n = src[1];
            memset(dst, src[2], n);
            src += 3;
            dst += n;
        } else if (code == 0x80) {
            w = src[1] | (src[2] << 8);
            if (w < 0x8000) {
                if (w == 0)
                    return dst;
                src += 3;
                memset(dst, 0, w);
                dst += w;
            } else if (w < 0xC000) {
                n = w & 0x7FFF;
                src += 3;
                memcpy(dst, src, n);
                src += n;
                dst += n;
            } else {
                n = w & 0x3FFF;
                memset(dst, src[3], n);
                src += 4;
                dst += n;
            }
        } else if (code > 0x80) {
            n = code & 0x7F;
            src++;
            memset(dst, 0, n);
            dst += n;
        } else {
            src++;
            memcpy(dst, src, code);
            src += code;
            dst += code;
        }
    }
}

void gr_delta8_unpack(uchar *dst, uchar *src)
{
    uint n;
    ushort w;
    signed char code;

    for (;;) {
        code = (signed char)*src++;
        if (code > 0) {
            memcpy(dst, src, code);
            src += code;
            dst += code;
        } else if (code == 0) {
            n = *src++;
            memset(dst, *src++, n);
            dst += n;
        } else if ((uchar)code != 0x80) {
            dst += (uchar)code - 0x80;
        } else {
            w = src[0] | (src[1] << 8);
            src += 2;
            if ((short)w > 0)
                dst += w;
            else if (w == 0)
                return;
            else if ((w & 0x7FFF) < 0x4000) {
                n = w & 0x3FFF;
                memcpy(dst, src, n);
                src += n;
                dst += n;
            } else {
                n = w & 0x3FFF;
                memset(dst, *src++, n);
                dst += n;
            }
        }
    }
}
