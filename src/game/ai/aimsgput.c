/* aimsgput.c -- Terra Nova AI message packers / unpackers, second half of module M011
 * (0x25AF34-0x25B372; the first half 0x25AB8C-0x25AF1C is worker g4c1's).
 * Decompiled from __FF.EXE v1.09.
 *
 * Every AI message is a Msg of the T1c message pool (t1_c.h, 32 bytes).  A packer stores the
 * message type in byte +2, the sender (MsgAddr as one dword) at +8, the receiver at +0xC and the
 * type's arguments from +0x10 on; an unpacker copies the arguments of a received message out.
 * The packers of types 0x64 / 0x6C / 0x6D / 0x6E write a fixed receiver {2, 0} (the script
 * system, T1a).  The map names (aiacc_XXXXXX) are kept because other batches call them.
 *
 *   0x25AF34 aiacc_25AF34  type 0x17: words a, b, c at +10/+12/+14
 *   0x25AF58 aiacc_25AF58  unpack three words +10/+12/+14
 *   0x25AF70 aiacc_25AF70  type 0x18: bytes a, b, c at +10/+11/+12
 *   0x25AF90 aiacc_25AF90  unpack three bytes +10/+11/+12
 *   0x25AFA4 aiacc_25AFA4  type 0x19: bytes a, b, c (unreferenced)
 *   0x25AFC4 aiacc_25AFC4  type 0x1A: byte a
 *   0x25AFD4 aiacc_25AFD4  type 0x1B: bytes a, b
 *   0x25AFEC aiacc_25AFEC  type 0x1C: byte a (unreferenced)
 *   0x25AFFC aiacc_25AFFC  type 0x1D: bytes a, b
 *   0x25B014 aiacc_25B014  type 0x1E: word a, word (uchar b)
 *   0x25B034 aiacc_25B034  unpack word +10, byte +12
 *   0x25B044 aiacc_25B044  type 0x1F: words a, b
 *   0x25B060 aiacc_25B060  unpack word +10, byte +12 zero-extended into a word
 *   0x25B074 aiacc_25B074  type 0x20: no arguments
 *   0x25B084 aiacc_25B084  type 0x22: words a, b, c
 *   0x25B0A8 aiacc_25B0A8  type 0x23: bytes a, b, c, d
 *   0x25B0D0 aiacc_25B0D0  unpack four bytes +10..+13 (unreferenced)
 *   0x25B0F0 aiacc_25B0F0  type 0x24: 16 bytes copied from a buffer
 *   0x25B11C aiacc_25B11C  unpack 16 bytes (unreferenced)
 *   0x25B140 .. 0x25B1AC   types 0x25 0x26 0x28 0x2C 0x2D 0x2E(no arg) 0x2F 0x30: byte a
 *   0x25B1BC aiacc_25B1BC  type 0x2A: word a
 *   0x25B1CC aiacc_25B1CC  unpack word +10
 *   0x25B1D4 aiacc_25B1D4  type 0x2B: no arguments
 *   0x25B1E0 aiacc_25B1E0  type 0x27: no arguments
 *   0x25B1EC aiacc_25B1EC  type 0x64 to {2,0}: bytes a, b (unreferenced)
 *   0x25B208 aiacc_25B208  type 0x65 from {0x86, (uchar)c}: 16 bytes copied from a buffer
 *   0x25B244 aiacc_25B244  unpack byte +A (sender .b low byte) and 16 bytes
 *   0x25B26C aiacc_25B26C  type 0x66: dword a
 *   0x25B27C aiacc_25B27C  unpack dword +10
 *   0x25B284 aiacc_25B284  type 0x67: dword a
 *   0x25B294 aiacc_25B294  type 0x68: dword a
 *   0x25B2A4 aiacc_25B2A4  type 0x69: no arguments
 *   0x25B2B0 aiacc_25B2B0  type 0x6A: no arguments
 *   0x25B2BC aiacc_25B2BC  type 0x6B: no arguments
 *   0x25B2C8 aiacc_25B2C8  type 0x6C to {2,0}: dword a
 *   0x25B2E0 aiacc_25B2E0  type 0x6D to {2,0}: dword a
 *   0x25B2F8 aiacc_25B2F8  type 0x6E to {2,0}: bytes a, b, c, d
 *   0x25B324 aiacc_25B324  type 0x6F: no arguments (unreferenced)
 *   0x25B330 aiacc_25B330  type 0x70: dwords a, b (unreferenced)
 *   0x25B348 aiacc_25B348  unpack dwords +10/+14 (unreferenced)
 *   0x25B358 aiacc_25B358  type 0x40: no arguments
 *   0x25B364 aiacc_25B364  type 0x41: byte a
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_c2.h"

#define MB(m, o) (((uchar *)(m))[o])
#define MW(m, o) (*(ushort *)((uchar *)(m) + (o)))
#define ML(m, o) (*(ulong *)((uchar *)(m) + (o)))

static void head(Msg *m, uchar type, ulong from, ulong to)
{
    MB(m, 2) = type;
    ML(m, 8) = from;
    ML(m, 0xC) = to;
}

static void head_script(Msg *m, uchar type, ulong from)
{
    MB(m, 2) = type;
    MW(m, 0xC) = 2;
    MW(m, 0xE) = 0;
    ML(m, 8) = from;
}

static void copy16(void *d, const void *s)
{
    uchar *dd = d;
    const uchar *ss = s;
    int i;
    for (i = 0; i < 16; i++)
        dd[i] = ss[i];
}

void aiacc_25AF34(Msg *m, ulong from, ulong to, long a, long b, long c)
{
    head(m, 0x17, from, to);
    MW(m, 0x10) = (ushort)a;
    MW(m, 0x12) = (ushort)b;
    MW(m, 0x14) = (ushort)c;
}

void aiacc_25AF58(Msg *m, short *a, short *b, short *c)
{
    *a = MW(m, 0x10);
    *b = MW(m, 0x12);
    *c = MW(m, 0x14);
}

static void put3b(Msg *m, uchar type, ulong from, ulong to, long a, long b, long c)
{
    head(m, type, from, to);
    MB(m, 0x10) = (uchar)a;
    MB(m, 0x11) = (uchar)b;
    MB(m, 0x12) = (uchar)c;
}

void aiacc_25AF70(Msg *m, ulong from, ulong to, long a, long b, long c)
{
    put3b(m, 0x18, from, to, a, b, c);
}

void aiacc_25AF90(Msg *m, uchar *a, uchar *b, uchar *c)
{
    *a = MB(m, 0x10);
    *b = MB(m, 0x11);
    *c = MB(m, 0x12);
}

void aiacc_25AFA4(Msg *m, ulong from, ulong to, long a, long b, long c)
{
    put3b(m, 0x19, from, to, a, b, c);
}

static void put1b(Msg *m, uchar type, ulong from, ulong to, long a)
{
    head(m, type, from, to);
    MB(m, 0x10) = (uchar)a;
}

static void put2b(Msg *m, uchar type, ulong from, ulong to, long a, long b)
{
    head(m, type, from, to);
    MB(m, 0x10) = (uchar)a;
    MB(m, 0x11) = (uchar)b;
}

void aiacc_25AFC4(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x1A, from, to, a); }
void aiacc_25AFD4(Msg *m, ulong from, ulong to, long a, long b) { put2b(m, 0x1B, from, to, a, b); }
void aiacc_25AFEC(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x1C, from, to, a); }
void aiacc_25AFFC(Msg *m, ulong from, ulong to, long a, long b) { put2b(m, 0x1D, from, to, a, b); }

void aiacc_25B014(Msg *m, ulong from, ulong to, long a, long b)
{
    head(m, 0x1E, from, to);
    MW(m, 0x10) = (ushort)a;
    MW(m, 0x12) = (uchar)b;
}

void aiacc_25B034(Msg *m, short *a, uchar *b)
{
    *a = MW(m, 0x10);
    *b = MB(m, 0x12);
}

void aiacc_25B044(Msg *m, ulong from, ulong to, long a, long b)
{
    head(m, 0x1F, from, to);
    MW(m, 0x10) = (ushort)a;
    MW(m, 0x12) = (ushort)b;
}

void aiacc_25B060(Msg *m, short *a, short *b)
{
    *a = MW(m, 0x10);
    *b = MB(m, 0x12);
}

void aiacc_25B074(Msg *m, ulong from, ulong to) { head(m, 0x20, from, to); }

void aiacc_25B084(Msg *m, ulong from, ulong to, long a, long b, long c)
{
    head(m, 0x22, from, to);
    MW(m, 0x10) = (ushort)a;
    MW(m, 0x12) = (ushort)b;
    MW(m, 0x14) = (ushort)c;
}

void aiacc_25B0A8(Msg *m, ulong from, ulong to, long a, long b, long c, long d)
{
    put3b(m, 0x23, from, to, a, b, c);
    MB(m, 0x13) = (uchar)d;
}

void aiacc_25B0D0(Msg *m, uchar *a, uchar *b, uchar *c, uchar *d)
{
    *a = MB(m, 0x10);
    *b = MB(m, 0x11);
    *c = MB(m, 0x12);
    *d = MB(m, 0x13);
}

void aiacc_25B0F0(Msg *m, ulong from, ulong to, const void *buf)
{
    head(m, 0x24, from, to);
    copy16((uchar *)m + 0x10, buf);
}

void aiacc_25B11C(Msg *m, void *buf)
{
    copy16(buf, (uchar *)m + 0x10);
}

void aiacc_25B140(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x25, from, to, a); }
void aiacc_25B150(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x26, from, to, a); }
void aiacc_25B160(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x28, from, to, a); }
void aiacc_25B170(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x2C, from, to, a); }
void aiacc_25B180(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x2D, from, to, a); }
void aiacc_25B190(Msg *m, ulong from, ulong to) { head(m, 0x2E, from, to); }
void aiacc_25B19C(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x2F, from, to, a); }
void aiacc_25B1AC(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x30, from, to, a); }

void aiacc_25B1BC(Msg *m, ulong from, ulong to, long a)
{
    head(m, 0x2A, from, to);
    MW(m, 0x10) = (ushort)a;
}

void aiacc_25B1CC(Msg *m, short *a) { *a = MW(m, 0x10); }
void aiacc_25B1D4(Msg *m, ulong from, ulong to) { head(m, 0x2B, from, to); }
void aiacc_25B1E0(Msg *m, ulong from, ulong to) { head(m, 0x27, from, to); }

void aiacc_25B1EC(Msg *m, ulong from, long a, long b)
{
    head_script(m, 0x64, from);
    MB(m, 0x10) = (uchar)a;
    MB(m, 0x11) = (uchar)b;
}

void aiacc_25B208(Msg *m, long unused, ulong to, long id, const void *buf)
{
    (void)unused;
    MB(m, 2) = 0x65;
    MW(m, 8) = 0x86;
    ML(m, 0xC) = to;
    MW(m, 0xA) = (uchar)id;
    copy16((uchar *)m + 0x10, buf);
}

void aiacc_25B244(Msg *m, uchar *id, void *buf)
{
    *id = MB(m, 0xA);
    copy16(buf, (uchar *)m + 0x10);
}

void aiacc_25B26C(Msg *m, ulong from, ulong to, long a)
{
    head(m, 0x66, from, to);
    ML(m, 0x10) = a;
}

void aiacc_25B27C(Msg *m, long *a) { *a = ML(m, 0x10); }

void aiacc_25B284(Msg *m, ulong from, ulong to, long a)
{
    head(m, 0x67, from, to);
    ML(m, 0x10) = a;
}

void aiacc_25B294(Msg *m, ulong from, ulong to, long a)
{
    head(m, 0x68, from, to);
    ML(m, 0x10) = a;
}

void aiacc_25B2A4(Msg *m, ulong from, ulong to) { head(m, 0x69, from, to); }
void aiacc_25B2B0(Msg *m, ulong from, ulong to) { head(m, 0x6A, from, to); }
void aiacc_25B2BC(Msg *m, ulong from, ulong to) { head(m, 0x6B, from, to); }

void aiacc_25B2C8(Msg *m, ulong from, long a)
{
    head_script(m, 0x6C, from);
    ML(m, 0x10) = a;
}

void aiacc_25B2E0(Msg *m, ulong from, long a)
{
    head_script(m, 0x6D, from);
    ML(m, 0x10) = a;
}

void aiacc_25B2F8(Msg *m, ulong from, long a, long b, long c, long d)
{
    head_script(m, 0x6E, from);
    MB(m, 0x10) = (uchar)a;
    MB(m, 0x11) = (uchar)b;
    MB(m, 0x12) = (uchar)c;
    MB(m, 0x13) = (uchar)d;
}

void aiacc_25B324(Msg *m, ulong from, ulong to) { head(m, 0x6F, from, to); }

void aiacc_25B330(Msg *m, ulong from, ulong to, long a, long b)
{
    head(m, 0x70, from, to);
    ML(m, 0x10) = a;
    ML(m, 0x14) = b;
}

void aiacc_25B348(Msg *m, long *a, long *b)
{
    *a = ML(m, 0x10);
    *b = ML(m, 0x14);
}

void aiacc_25B358(Msg *m, ulong from, ulong to) { head(m, 0x40, from, to); }
void aiacc_25B364(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x41, from, to, a); }
