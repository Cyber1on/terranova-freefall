/* LZW.C  New improved super-duper LZW compressor & decompressor (Greg Travis, Rex Bradford)
 * x:\prj\tech\libsrc\res\lzw.c as linked into Terra Nova v1.09, __FF.EXE 0x2E0250..0x2E0F61.
 * Decompiled from the binary; structure and names follow System Shock's LZW.C (rcs 1.4).
 *
 * Differences from the Shock (Mac) source, all read from the binary:
 *  - LzwInit() really calls AtExit(LzwTerm) (commented out in the Mac source).
 *  - Malloc/Free (LG allocator hooks) instead of NewPtr/DisposePtr.
 *  - The fd and FILE * sources/destinations exist (commented out in the Mac source); the
 *    LzwNullDest functions do not exist in this build.
 *  - Warning() calls are compiled out (LzwSetBuffer, LzwMallocBuffer, LzwDecodeString).
 *  - LzwInputCode and LzwDecodeString are inlined into LzwExpand, but out-of-line copies are
 *    still present (0x2E0938, 0x2E0F1C, never called).
 * Quirks kept on purpose (they are in Shock too):
 *  - LzwSetBuffer computes lzwFdWriteBuff from its own previous value (+512 every call, so the
 *    first call makes it 0x200, an absolute low-memory address) and puts lzwCodeValue at
 *    buff + 512, overlapping the decode stack and the fd read buffer.  Compressing from an fd
 *    source therefore scribbles into the code value table.
 *  - LzwCompress reads srcSize + 1 bytes from its source (and 2 when srcSize is 0), LzwExpand
 *    reads up to 3 bytes beyond the end-of-data code.
 */
#include "lzw.h"

/* Watcom stdio (not in lg.h yet) */
int fgetc(FILE *fp);                    /* 0x3042C0 */
int fputc(int c, FILE *fp);             /* 0x3036B7 */

/* Important constants */
#define MAX_VALUE ((1 << LZW_BITS) - 1) /* end-of-compress-data code (0x3FFF) */
#define MAX_CODE (MAX_VALUE - 2)        /* maximum real code allowed (0x3FFD) */
#define FLUSH_CODE (MAX_VALUE - 1)      /* code to flush lzw string table (0x3FFE) */
#define HASHING_SHIFT (LZW_BITS - 8)    /* # bits to shift when hashing */
#define FLUSH_PAUSE 1000                /* wait on full table before flush */

#ifndef TN_LINK_ORIGINAL_DATA
LzwE lzwe;
LzwC lzwc;
int lzwWriteBuffIndex;
FILE *lzwFpDest;
int lzwFdDest;
int lzwReadBuffIndex;
uchar *lzwBuffDestPtr;
uchar *lzwBuffSrcPtr;
FILE *lzwFpSrc;
int lzwFdSrc;
uchar *lzwDecodeStack;
uchar *lzwFdWriteBuff;
uchar *lzwFdReadBuff;
uchar *lzwAppendChar;
void *lzwBuffer;
short *lzwCodeValue;
ushort *lzwPrefixCode;
bool lzwBufferMalloced;
#endif

/* ---------------- initialization and termination ---------------- */

/* 0x2E0250 */
void LzwInit(void)
{
    AtExit(WATCOM_FN(LzwTerm));
}

/* 0x2E025C (LzwFreeBuffer inlined) */
void LzwTerm(void)
{
    LzwFreeBuffer();
}

/* ---------------- buffer setting ---------------- */

/* 0x2E0284  Returns 0 if ok, -1 if buffer too small (unsigned compare: LZW_BUFF_SIZE is size_t) */
int LzwSetBuffer(void *buff, long buffSize)
{
    if (buffSize < LZW_BUFF_SIZE)
        return -1;
    LzwFreeBuffer();
    lzwBuffer = buff;
    lzwDecodeStack = (uchar *)lzwBuffer;
    lzwFdReadBuff = lzwDecodeStack + LZW_DECODE_STACK_SIZE;
    lzwFdWriteBuff = lzwFdWriteBuff + LZW_FD_READ_BUFF_SIZE;         /* sic */
    lzwCodeValue = (short *)(lzwDecodeStack + LZW_FD_WRITE_BUFF_SIZE); /* sic */
    lzwPrefixCode = (ushort *)((uchar *)lzwCodeValue + LZW_TABLE_SIZE * sizeof(ushort));
    lzwAppendChar = (uchar *)lzwPrefixCode + LZW_TABLE_SIZE * sizeof(ushort);
    lzwBufferMalloced = FALSE;
    return 0;
}

/* 0x2E02F0  Returns 0 if success, -1 if error */
int LzwMallocBuffer(void)
{
    void *buff;

    if (lzwBuffer == NULL || !lzwBufferMalloced) {
        buff = Malloc(LZW_BUFF_SIZE);
        if (buff == NULL)
            return -1;
        LzwSetBuffer(buff, LZW_BUFF_SIZE);
        lzwBufferMalloced = TRUE;
    }
    return 0;
}

/* 0x2E0330 */
void LzwFreeBuffer(void)
{
    if (lzwBufferMalloced) {
        Free(lzwBuffer);
        lzwBuffer = NULL;
        lzwBufferMalloced = FALSE;
    }
}

/* ---------------- compression ---------------- */

/* Accumulate an output code into the bit buffer and put whole bytes to the destination.  If the
 * output exceeds destSizeMax, shut down source and destination and return -1. */
#define LzwOutputCode(code) {                                                           \
    lzwc.lzwOutputBitBuffer |= ((ulong)(code)) << (32 - LZW_BITS - lzwc.lzwOutputBitCount); \
    lzwc.lzwOutputBitCount += LZW_BITS;                                                 \
    while (lzwc.lzwOutputBitCount >= 8) {                                               \
        (*f_DestPut)(lzwc.lzwOutputBitBuffer >> 24);                                    \
        if (++lzwc.lzwOutputSize > destSizeMax) {                                       \
            (*f_SrcCtrl)(srcLoc, END);                                                  \
            (*f_DestCtrl)(destLoc, END);                                                \
            return -1L;                                                                 \
        }                                                                               \
        lzwc.lzwOutputBitBuffer <<= 8;                                                  \
        lzwc.lzwOutputBitCount -= 8;                                                    \
    }                                                                                   \
}

/* 0x2E0358  Returns the compressed size, -1 if it would exceed destSizeMax, 0 if no buffer */
long LzwCompress(LzwCtrlFunc f_SrcCtrl, LzwGetFunc f_SrcGet, long srcLoc, long srcSize,
                 LzwCtrlFunc f_DestCtrl, LzwPutFunc f_DestPut, long destLoc, long destSizeMax)
{
    if (lzwBuffer == NULL) {
        if (LzwMallocBuffer() < 0)
            return 0;
    }

    lzwc.next_code = 256;               /* skip over real 256 char values */
    memset(lzwCodeValue, -1, sizeof(short) * LZW_TABLE_SIZE);
    lzwc.lzwOutputSize = 0;
    lzwc.lzwOutputBitCount = 0;
    lzwc.lzwOutputBitBuffer = 0;

    (*f_SrcCtrl)(srcLoc, BEGIN);
    (*f_DestCtrl)(destLoc, BEGIN);

    lzwc.string_code = (*f_SrcGet)();
    lzwc.lzwInputCharCount = 1;

    while (TRUE) {
        /* get next input char; the char is read before the count check */
        lzwc.character = (*f_SrcGet)();
        if (lzwc.lzwInputCharCount++ >= srcSize)
            break;

        /* string in table: take its code */
        lzwc.index = LzwFindMatch(lzwc.string_code, lzwc.character);
        if (lzwCodeValue[lzwc.index] != -1)
            lzwc.string_code = lzwCodeValue[lzwc.index];

        /* not in table: add it if there is room */
        else if (lzwc.next_code <= MAX_CODE) {
            lzwCodeValue[lzwc.index] = lzwc.next_code++;
            lzwPrefixCode[lzwc.index] = lzwc.string_code;
            lzwAppendChar[lzwc.index] = lzwc.character;
            LzwOutputCode(lzwc.string_code);
            lzwc.string_code = lzwc.character;
        }

        /* table full for a while: flush it */
        else if (lzwc.next_code > MAX_CODE + FLUSH_PAUSE) {
            LzwOutputCode(lzwc.string_code);
            LzwOutputCode(FLUSH_CODE);
            memset(lzwCodeValue, -1, sizeof(short) * LZW_TABLE_SIZE);
            lzwc.string_code = lzwc.character;
            lzwc.next_code = 256;
        }

        /* can't add but not time to flush yet: just output the code */
        else {
            lzwc.next_code++;
            LzwOutputCode(lzwc.string_code);
            lzwc.string_code = lzwc.character;
        }
    }

    /* output current code, end-of-data code, and a final 0 to flush the bit buffer */
    LzwOutputCode(lzwc.string_code);
    LzwOutputCode(MAX_VALUE);
    LzwOutputCode(0);

    (*f_SrcCtrl)(srcLoc, END);
    (*f_DestCtrl)(destLoc, END);
    return lzwc.lzwOutputSize;
}

/* ---------------- expansion ---------------- */

/* 0x2E0938 (static in the original; inlined into LzwExpand, this copy is never called) */
unsigned int LzwInputCode(LzwGetFunc f_SrcGet)
{
    unsigned int return_value;

    while (lzwe.lzwInputBitCount <= 24) {
        lzwe.lzwInputBitBuffer |= ((ulong)(*f_SrcGet)()) << (24 - lzwe.lzwInputBitCount);
        lzwe.lzwInputBitCount += 8;
    }
    return_value = lzwe.lzwInputBitBuffer >> (32 - LZW_BITS);
    lzwe.lzwInputBitBuffer <<= LZW_BITS;
    lzwe.lzwInputBitCount -= LZW_BITS;
    return return_value;
}

/* 0x2E09B0  Returns # bytes of uncompressed output (counting only stored ones), 0 if no buffer */
long LzwExpand(LzwCtrlFunc f_SrcCtrl, LzwGetFunc f_SrcGet, long srcLoc,
               LzwCtrlFunc f_DestCtrl, LzwPutFunc f_DestPut, long destLoc,
               long destSkip, long destSize)
{
    if (lzwBuffer == NULL) {
        if (LzwMallocBuffer() < 0)
            return 0;
    }

    lzwe.lzwInputBitCount = 0;
    lzwe.lzwInputBitBuffer = 0;
    lzwe.next_code = 256;               /* next available char after regular 256 chars */
    lzwe.outputSize = 0;
    lzwe.destSkip = destSkip;
    lzwe.destSize = destSize ? destSize : LZW_MAXSIZE;

    (*f_SrcCtrl)(srcLoc, BEGIN);
    (*f_DestCtrl)(destLoc, BEGIN);

    /* get first code & output it (not checked against destSize) */
    lzwe.old_code = LzwInputCode(f_SrcGet);
    lzwe.character = lzwe.old_code;
#ifndef TN_WATCOM_ABI
    /* G27: a damaged file (a saved game): the data ends at the first code that cannot be
     * there.  A string's first code is a character, and a code is never one not made yet
     * (but the next one): then the string table's codes all lead to smaller ones, made in this
     * expansion, and no string can loop for ever */
    if (lzwe.old_code > 255)
        goto DONE_EXPAND;
#endif
    if (--lzwe.destSkip < 0) {
        (*f_DestPut)(lzwe.old_code);
        lzwe.outputSize++;
    }

    while ((lzwe.new_code = LzwInputCode(f_SrcGet)) != MAX_VALUE) {

        /* flush code: reset the string table and restart */
        if (lzwe.new_code == FLUSH_CODE) {
            lzwe.next_code = 256;
            lzwe.old_code = LzwInputCode(f_SrcGet);
            lzwe.character = lzwe.old_code;
#ifndef TN_WATCOM_ABI
            if (lzwe.old_code > 255)    /* G27 */
                break;
#endif
            if (--lzwe.destSkip < 0) {
                if (lzwe.outputSize++ >= lzwe.destSize)
                    break;
                (*f_DestPut)(lzwe.old_code);
            }
            continue;
        }

#ifndef TN_WATCOM_ABI
        if (lzwe.new_code > lzwe.next_code)     /* G27 */
            break;
#endif
        /* STRING+CHARACTER+STRING+CHARACTER+STRING case: undefined code, decode the last
         * code with the last character appended */
        if (lzwe.new_code >= lzwe.next_code) {
            *lzwDecodeStack = lzwe.character;
            lzwe.string = LzwDecodeString(lzwDecodeStack + 1, lzwe.old_code);
        } else
            lzwe.string = LzwDecodeString(lzwDecodeStack, lzwe.new_code);

        /* output the decoded string, in reverse order */
        lzwe.character = *lzwe.string;
        while (lzwe.string >= lzwDecodeStack) {
            if (--lzwe.destSkip < 0) {
                if (lzwe.outputSize++ >= lzwe.destSize)
                    goto DONE_EXPAND;
                (*f_DestPut)(*lzwe.string);
            }
            --lzwe.string;
        }

        /* if possible, add a new code to the string table */
        if (lzwe.next_code <= MAX_CODE) {
            lzwPrefixCode[lzwe.next_code] = lzwe.old_code;
            lzwAppendChar[lzwe.next_code] = lzwe.character;
            lzwe.next_code++;
        }
        lzwe.old_code = lzwe.new_code;
    }

DONE_EXPAND:
    (*f_SrcCtrl)(srcLoc, END);
    (*f_DestCtrl)(destLoc, END);
    return lzwe.outputSize;
}

/* ---------------- standard sources ---------------- */

/* 0x2E0D5C, 0x2E0D68  memory buffer source */
void WATCOMCALL LzwBuffSrcCtrl(long srcLoc, LzwCtrl ctrl)
{
    if (ctrl == BEGIN)
        lzwBuffSrcPtr = (uchar *)srcLoc;
}

uchar WATCOMCALL LzwBuffSrcGet(void)
{
    return *lzwBuffSrcPtr++;
}

/* 0x2E0D7C, 0x2E0D94  file descriptor source (fd = open(), already positioned) */
void WATCOMCALL LzwFdSrcCtrl(long srcLoc, LzwCtrl ctrl)
{
    if (ctrl == BEGIN) {
        lzwFdSrc = (int)srcLoc;
        lzwReadBuffIndex = LZW_FD_READ_BUFF_SIZE;
    }
}

uchar WATCOMCALL LzwFdSrcGet(void)
{
    if (lzwReadBuffIndex == LZW_FD_READ_BUFF_SIZE) {
        read(lzwFdSrc, lzwFdReadBuff, LZW_FD_READ_BUFF_SIZE);
        lzwReadBuffIndex = 0;
    }
    return lzwFdReadBuff[lzwReadBuffIndex++];
}

/* 0x2E0DDC, 0x2E0DE8  file pointer source (fp = fopen(), already positioned) */
void WATCOMCALL LzwFpSrcCtrl(long srcLoc, LzwCtrl ctrl)
{
    if (ctrl == BEGIN)
        lzwFpSrc = (FILE *)srcLoc;
}

uchar WATCOMCALL LzwFpSrcGet(void)
{
    return fgetc(lzwFpSrc);
}

/* ---------------- standard destinations ---------------- */

/* 0x2E0DF4, 0x2E0E00  memory buffer destination */
void WATCOMCALL LzwBuffDestCtrl(long destLoc, LzwCtrl ctrl)
{
    if (ctrl == BEGIN)
        lzwBuffDestPtr = (uchar *)destLoc;
}

void WATCOMCALL LzwBuffDestPut(uchar byte)
{
    *lzwBuffDestPtr++ = byte;
}

/* 0x2E0E14, 0x2E0E4C  file descriptor destination */
void WATCOMCALL LzwFdDestCtrl(long destLoc, LzwCtrl ctrl)
{
    if (ctrl == BEGIN) {
        lzwFdDest = (int)destLoc;
        lzwWriteBuffIndex = 0;
    } else if (ctrl == END) {
        if (lzwWriteBuffIndex)
            write(lzwFdDest, lzwFdWriteBuff, lzwWriteBuffIndex);
    }
}

void WATCOMCALL LzwFdDestPut(uchar byte)
{
    lzwFdWriteBuff[lzwWriteBuffIndex++] = byte;
    if (lzwWriteBuffIndex == LZW_FD_WRITE_BUFF_SIZE) {
        write(lzwFdDest, lzwFdWriteBuff, LZW_FD_WRITE_BUFF_SIZE);
        lzwWriteBuffIndex = 0;
    }
}

/* 0x2E0E88, 0x2E0E94  file pointer destination */
void WATCOMCALL LzwFpDestCtrl(long destLoc, LzwCtrl ctrl)
{
    if (ctrl == BEGIN)
        lzwFpDest = (FILE *)destLoc;
}

void WATCOMCALL LzwFpDestPut(uchar byte)
{
    fputc(byte, lzwFpDest);
}

/* ---------------- internal routines ---------------- */

/* 0x2E0EAC  Hashing routine: returns the string table index of prefix+char, or the first free
 * index on its probe sequence if the string is not in the table. */
int LzwFindMatch(int hash_prefix, unsigned int hash_character)
{
    int index;
    int offset;

    index = (hash_character << HASHING_SHIFT) ^ hash_prefix;
    if (index == 0)
        offset = 1;
    else
        offset = LZW_TABLE_SIZE - index;
    while (1) {
        if (lzwCodeValue[index] == -1)
            return index;
        if (lzwPrefixCode[index] == hash_prefix && lzwAppendChar[index] == hash_character)
            return index;
        index -= offset;
        if (index < 0)
            index += LZW_TABLE_SIZE;
    }
}

/* 0x2E0F1C (inlined into LzwExpand, this copy is never called)  Decodes a string from the string
 * table into buffer, last char first; returns a pointer to its last stored (= first) char.
 * Nothing limits the length to the 4000-byte decode stack. */
uchar *LzwDecodeString(uchar *buffer, unsigned int code)
{
    while (code > 255) {
        *buffer++ = lzwAppendChar[code];
        code = lzwPrefixCode[code];
    }
    *buffer = code;
    return buffer;
}

#ifdef TN_WATCOM_ABI
/* Test entry points (cdecl), for verify/test_lzw.py: run our LzwCompress / LzwExpand with our
 * own standard callbacks.  kind: 0 buffer, 1 fd, 2 FILE *. */
static const LzwCtrlFunc lzwTestSrcCtrl[3] = {LzwBuffSrcCtrl, LzwFdSrcCtrl, LzwFpSrcCtrl};
static const LzwGetFunc lzwTestSrcGet[3] = {LzwBuffSrcGet, LzwFdSrcGet, LzwFpSrcGet};
static const LzwCtrlFunc lzwTestDestCtrl[3] = {LzwBuffDestCtrl, LzwFdDestCtrl, LzwFpDestCtrl};
static const LzwPutFunc lzwTestDestPut[3] = {LzwBuffDestPut, LzwFdDestPut, LzwFpDestPut};

long LzwTestCompress(int srcKind, long srcLoc, long srcSize, int destKind, long destLoc, long destSizeMax)
{
    return LzwCompress(lzwTestSrcCtrl[srcKind], lzwTestSrcGet[srcKind], srcLoc, srcSize,
                       lzwTestDestCtrl[destKind], lzwTestDestPut[destKind], destLoc, destSizeMax);
}

long LzwTestExpand(int srcKind, long srcLoc, int destKind, long destLoc, long destSkip, long destSize)
{
    return LzwExpand(lzwTestSrcCtrl[srcKind], lzwTestSrcGet[srcKind], srcLoc,
                     lzwTestDestCtrl[destKind], lzwTestDestPut[destKind], destLoc, destSkip, destSize);
}
#endif
