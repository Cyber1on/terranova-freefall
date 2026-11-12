/* aimkmsg.c -- Terra Nova AI message packers / unpackers, first half of module M011
 * (0x25AC50-0x25AF33; the second half 0x25AF34-0x25B372 is worker g4c2's aimsgput.c).
 * Decompiled from __FF.EXE v1.09.
 *
 * Every AI message is a Msg of the T1c message pool (t1_c.h, 32 bytes).  A packer stores the
 * message type in byte +2, the sender (MsgAddr as one dword) at +8, the receiver at +0xC and the
 * type's arguments from +0x10 on; an unpacker copies the arguments of a received message out.
 * The map names (aiacc_XXXXXX) are kept because other batches call them.
 *
 *   0x25AC50 aiacc_25AC50  type given by the caller: byte a at +10
 *   0x25AC64 aiacc_25AC64  unpack byte +10
 *   0x25AC6C aiacc_25AC6C  type 0x00: position (2 words) +10/+12, byte b as a word +14
 *   0x25AC98 aiacc_25AC98  unpack position +10/+12, byte +14
 *   0x25ACB0 aiacc_25ACB0  type 0x29: position +10/+12
 *   0x25ACCC aiacc_25ACCC  unpack position +10/+12
 *   0x25ACE0 aiacc_25ACE0  type 0x31: word a +10, position +12/+14 (0, 0 without one), byte b,
 *                          words c, d as words +16/+18/+1A
 *   0x25AD34 aiacc_25AD34  unpack type 0x31 (bytes of +16/+18/+1A)
 *   0x25AD68 .. 0x25ADD4   types 4, 5 (byte a), 6 (byte a), 7, 8, 0x34 (byte a), 0x33, 0x32
 *   0x25ADE0 aiacc_25ADE0  type 0x0B: bytes a, b +10/+11
 *   0x25ADF8 aiacc_25ADF8  unpack bytes +10/+11
 *   0x25AE08 .. 0x25AE60   types 0x0C (byte a), 0x0D/0x0E/0x0F (bytes a, b), 0x10 (byte a)
 *   0x25AE70 aiacc_25AE70  type 0x11: dword a +10, words b, c, d +14/+16/+18
 *   0x25AE98 aiacc_25AE98  unpack type 0x11 and the sender's .b
 *   0x25AEC8 aiacc_25AEC8  type 0x12: bytes a, b
 *   0x25AEE0 aiacc_25AEE0  type 0x13 (falls through into 0x25AEEC)
 *   0x25AEEC aiacc_25AEEC  empty function (a bare ret)
 *   0x25AEF0 aiacc_25AEF0  type 0x15: word a +10, byte b +13
 *   0x25AF0C aiacc_25AF0C  unpack word +10, byte +13
 *   0x25AF1C aiacc_25AF1C  type 0x21: bytes a, b
 * The code-segment bytes 0x25AB8C-0x25AC4F that the map listed as six functions (aimsg_25AB88,
 * aiacc_25AB8C/90/94, aiacc_25AC18/1C) are the squad formation table (g4a's ai_formations
 * 0x25AB90, read with base-relative indexes), not code.
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_c1.h"

#define MB(m, o) (((uchar *)(m))[o])
#define MW(m, o) (*(ushort *)((uchar *)(m) + (o)))
#define ML(m, o) (*(ulong *)((uchar *)(m) + (o)))

static void head(Msg *m, uchar type, ulong from, ulong to)
{
    MB(m, 2) = type;
    ML(m, 8) = from;
    ML(m, 0xC) = to;
}

/* 0x25AC50: the type is the fifth (stack) argument; the original stores it last */
void aiacc_25AC50(Msg *m, ulong from, ulong to, long a, long type)
{
    ML(m, 8) = from;
    ML(m, 0xC) = to;
    MB(m, 0x10) = (uchar)a;
    MB(m, 2) = (uchar)type;
}

void aiacc_25AC64(Msg *m, uchar *a) { *a = MB(m, 0x10); }

void aiacc_25AC6C(Msg *m, ulong from, ulong to, void *pos, long b)
{
    head(m, 0, from, to);
    MW(m, 0x10) = ((ushort *)pos)[0];
    MW(m, 0x12) = ((ushort *)pos)[1];
    MW(m, 0x14) = (uchar)b;
}

void aiacc_25AC98(Msg *m, void *pos, uchar *b)
{
    ((ushort *)pos)[0] = MW(m, 0x10);
    ((ushort *)pos)[1] = MW(m, 0x12);
    *b = MB(m, 0x14);
}

void aiacc_25ACB0(Msg *m, ulong from, ulong to, void *pos)
{
    head(m, 0x29, from, to);
    MW(m, 0x10) = ((ushort *)pos)[0];
    MW(m, 0x12) = ((ushort *)pos)[1];
}

void aiacc_25ACCC(Msg *m, void *pos)
{
    ((ushort *)pos)[0] = MW(m, 0x10);
    ((ushort *)pos)[1] = MW(m, 0x12);
}

void aiacc_25ACE0(Msg *m, ulong from, ulong to, void *pos, long a, long b, long c, long d)
{
    head(m, 0x31, from, to);
    MW(m, 0x10) = (ushort)a;
    if (pos) {
        MW(m, 0x12) = ((ushort *)pos)[0];
        MW(m, 0x14) = ((ushort *)pos)[1];
    } else {
        MW(m, 0x12) = 0;
        MW(m, 0x14) = 0;
    }
    MW(m, 0x16) = (uchar)b;
    MW(m, 0x18) = (ushort)c;
    MW(m, 0x1A) = (ushort)d;
}

void aiacc_25AD34(Msg *m, void *pos, short *a, uchar *b, uchar *c, uchar *d)
{
    *a = MW(m, 0x10);
    ((ushort *)pos)[0] = MW(m, 0x12);
    ((ushort *)pos)[1] = MW(m, 0x14);
    *b = MB(m, 0x16);
    *c = MB(m, 0x18);
    *d = MB(m, 0x1A);
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

void aiacc_25AD68(Msg *m, ulong from, ulong to) { head(m, 4, from, to); }
void aiacc_25AD78(Msg *m, ulong from, ulong to, long a) { put1b(m, 5, from, to, a); }
void aiacc_25AD88(Msg *m, ulong from, ulong to, long a) { put1b(m, 6, from, to, a); }
void aiacc_25AD98(Msg *m, ulong from, ulong to) { head(m, 7, from, to); }
void aiacc_25ADA8(Msg *m, ulong from, ulong to) { head(m, 8, from, to); }
void aiacc_25ADB8(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x34, from, to, a); }
void aiacc_25ADC8(Msg *m, ulong from, ulong to) { head(m, 0x33, from, to); }
void aiacc_25ADD4(Msg *m, ulong from, ulong to) { head(m, 0x32, from, to); }
void aiacc_25ADE0(Msg *m, ulong from, ulong to, long a, long b) { put2b(m, 0x0B, from, to, a, b); }

void aiacc_25ADF8(Msg *m, uchar *a, uchar *b)
{
    *a = MB(m, 0x10);
    *b = MB(m, 0x11);
}

void aiacc_25AE08(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x0C, from, to, a); }
void aiacc_25AE18(Msg *m, ulong from, ulong to, long a, long b) { put2b(m, 0x0D, from, to, a, b); }
void aiacc_25AE30(Msg *m, ulong from, ulong to, long a, long b) { put2b(m, 0x0E, from, to, a, b); }
void aiacc_25AE48(Msg *m, ulong from, ulong to, long a, long b) { put2b(m, 0x0F, from, to, a, b); }
void aiacc_25AE60(Msg *m, ulong from, ulong to, long a) { put1b(m, 0x10, from, to, a); }

void aiacc_25AE70(Msg *m, ulong from, ulong to, long a, long b, long c, long d)
{
    head(m, 0x11, from, to);
    ML(m, 0x10) = a;
    MW(m, 0x14) = (ushort)b;
    MW(m, 0x16) = (ushort)c;
    MW(m, 0x18) = (ushort)d;
}

/* type as declared by g2_c.h; only the low word of *ent, *c, *d and *e is written (the
 * sender's .b, then the words +0x18, +0x14, +0x16), all of *target (dword +0x10) */
void aiacc_25AE98(void *ai, long *ent, long *target, long *c, long *d, long *e)
{
    Msg *m = ai;
    *(ushort *)ent = MW(m, 0xA);
    *target = ML(m, 0x10);
    *(ushort *)d = MW(m, 0x14);
    *(ushort *)e = MW(m, 0x16);
    *(ushort *)c = MW(m, 0x18);
}

void aiacc_25AEC8(Msg *m, ulong from, ulong to, long a, long b) { put2b(m, 0x12, from, to, a, b); }
void aiacc_25AEE0(Msg *m, ulong from, ulong to) { head(m, 0x13, from, to); }
void aiacc_25AEEC(void) { }

void aiacc_25AEF0(Msg *m, ulong from, ulong to, long a, long b)
{
    head(m, 0x15, from, to);
    MW(m, 0x10) = (ushort)a;
    MB(m, 0x13) = (uchar)b;
}

void aiacc_25AF0C(Msg *m, short *a, uchar *b)
{
    *a = MW(m, 0x10);
    *b = MB(m, 0x13);
}

void aiacc_25AF1C(Msg *m, ulong from, ulong to, long a, long b) { put2b(m, 0x21, from, to, a, b); }
