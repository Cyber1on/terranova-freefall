/* LZW: 14-bit LZW compressor/expander of the RES library (x:\prj\tech\libsrc\res\lzw.c, by Greg
 * Travis and Rex Bradford), as linked into Terra Nova v1.09 (__FF.EXE 0x2E0250..0x2E0F61).
 * Decompiled from the binary; names follow System Shock's LZW.H / LZW.C.
 *
 * Data is read from a "source" and written to a "destination", each given as a control function
 * (called with BEGIN before and END after the run) plus a get/put function.  Standard sources:
 * memory buffer, file descriptor, FILE *.  Standard destinations: the same three.  Terra Nova's
 * build has no LzwNullDest (bit bucket) functions, so the ...2Null macros of Shock are absent.
 */
#ifndef LZW_H
#define LZW_H
#include "lg.h"

#define LZW_BITS 14                     /* # bits in compress codes */
#define LZW_TABLE_SIZE 18041            /* prime somewhat larger than 2**LZW_BITS */
#define LZW_FD_READ_BUFF_SIZE 512
#define LZW_FD_WRITE_BUFF_SIZE 512
#define LZW_DECODE_STACK_SIZE 4000

/* LzwSetBuffer() requires a buffer of at least this size (0x173FD = 95229 bytes).  Note it is a
 * size_t expression, so comparisons against it are unsigned. */
#define LZW_BUFF_SIZE (LZW_DECODE_STACK_SIZE + LZW_FD_READ_BUFF_SIZE + LZW_FD_WRITE_BUFF_SIZE + \
    (LZW_TABLE_SIZE * (sizeof(short) + sizeof(ushort) + sizeof(uchar))))

/* LzwCtrl's used to start/stop lzw sources and destinations.  Watcom stores this enum in a byte
 * (the callbacks test dl). */
typedef uchar LzwCtrl;
enum { BEGIN, END };

#define LZW_MAXSIZE 0x7FFFFFFFL         /* maximum output size */

/* Source/destination callbacks.  They may be original Watcom code (RES passes the addresses of
 * the standard ones), so they use the Watcom register convention (at most two args). */
typedef void (WATCOMCALL *LzwCtrlFunc)(long loc, LzwCtrl ctrl);
typedef uchar (WATCOMCALL *LzwGetFunc)(void);
typedef void (WATCOMCALL *LzwPutFunc)(uchar byte);

typedef struct FILE FILE;               /* Watcom stdio stream */

/* ---------------- public routines ---------------- */
void LzwInit(void);                     /* registers LzwTerm with AtExit */
void LzwTerm(void);                     /* frees the buffer if malloced */
int LzwSetBuffer(void *buff, long buffSize);
int LzwMallocBuffer(void);
void LzwFreeBuffer(void);

long LzwCompress(
    LzwCtrlFunc f_SrcCtrl,              /* func to control source */
    LzwGetFunc f_SrcGet,                /* func to get bytes from source */
    long srcLoc,                        /* source "location" (ptr, fd, FILE *, ...) */
    long srcSize,                       /* size of source in bytes */
    LzwCtrlFunc f_DestCtrl,             /* func to control dest */
    LzwPutFunc f_DestPut,               /* func to put bytes to dest */
    long destLoc,                       /* dest "location" */
    long destSizeMax);                  /* max size of dest (or LZW_MAXSIZE) */

long LzwExpand(
    LzwCtrlFunc f_SrcCtrl, LzwGetFunc f_SrcGet, long srcLoc,
    LzwCtrlFunc f_DestCtrl, LzwPutFunc f_DestPut, long destLoc,
    long destSkip,                      /* # dest bytes to skip over (or 0) */
    long destSize);                     /* # dest bytes to capture (if 0, all) */

/* standard sources and destinations */
void WATCOMCALL LzwBuffSrcCtrl(long srcLoc, LzwCtrl ctrl);
uchar WATCOMCALL LzwBuffSrcGet(void);
void WATCOMCALL LzwFdSrcCtrl(long srcLoc, LzwCtrl ctrl);
uchar WATCOMCALL LzwFdSrcGet(void);
void WATCOMCALL LzwFpSrcCtrl(long srcLoc, LzwCtrl ctrl);
uchar WATCOMCALL LzwFpSrcGet(void);
void WATCOMCALL LzwBuffDestCtrl(long destLoc, LzwCtrl ctrl);
void WATCOMCALL LzwBuffDestPut(uchar byte);
void WATCOMCALL LzwFdDestCtrl(long destLoc, LzwCtrl ctrl);
void WATCOMCALL LzwFdDestPut(uchar byte);
void WATCOMCALL LzwFpDestCtrl(long destLoc, LzwCtrl ctrl);
void WATCOMCALL LzwFpDestPut(uchar byte);

/* internal routines (out-of-line copies exist in the binary, both are also inlined) */
unsigned int LzwInputCode(LzwGetFunc f_SrcGet);
int LzwFindMatch(int hash_prefix, unsigned int hash_character);
uchar *LzwDecodeString(uchar *buffer, unsigned int code);

/* ---------------- convenience macros ---------------- */
#define LzwBuffSrcC(psrc, srcSize) LzwBuffSrcCtrl, LzwBuffSrcGet, (long)(psrc), srcSize
#define LzwFdSrcC(fdSrc, srcSize) LzwFdSrcCtrl, LzwFdSrcGet, (long)(fdSrc), srcSize
#define LzwFpSrcC(fpSrc, srcSize) LzwFpSrcCtrl, LzwFpSrcGet, (long)(fpSrc), srcSize
#define LzwBuffDestC(pdest, destSizeMax) LzwBuffDestCtrl, LzwBuffDestPut, (long)(pdest), destSizeMax
#define LzwFdDestC(fdDest) LzwFdDestCtrl, LzwFdDestPut, (long)(fdDest), LZW_MAXSIZE
#define LzwFpDestC(fpDest) LzwFpDestCtrl, LzwFpDestPut, (long)(fpDest), LZW_MAXSIZE

#define LzwBuffSrcE(psrc) LzwBuffSrcCtrl, LzwBuffSrcGet, (long)(psrc)
#define LzwFdSrcE(fdSrc) LzwFdSrcCtrl, LzwFdSrcGet, (long)(fdSrc)
#define LzwFpSrcE(fpSrc) LzwFpSrcCtrl, LzwFpSrcGet, (long)(fpSrc)
#define LzwBuffDestE(pdest, destSkip, destSize) LzwBuffDestCtrl, LzwBuffDestPut, (long)(pdest), destSkip, destSize
#define LzwFdDestE(fdDest, destSkip, destSize) LzwFdDestCtrl, LzwFdDestPut, (long)(fdDest), destSkip, destSize
#define LzwFpDestE(fpDest, destSkip, destSize) LzwFpDestCtrl, LzwFpDestPut, (long)(fpDest), destSkip, destSize

#define LzwCompressBuff2Buff(psrc, srcSize, pdest, destSizeMax) \
    LzwCompress(LzwBuffSrcC(psrc, srcSize), LzwBuffDestC(pdest, destSizeMax))
#define LzwCompressBuff2Fd(psrc, srcSize, fdDest) LzwCompress(LzwBuffSrcC(psrc, srcSize), LzwFdDestC(fdDest))
#define LzwCompressBuff2Fp(psrc, srcSize, fpDest) LzwCompress(LzwBuffSrcC(psrc, srcSize), LzwFpDestC(fpDest))
#define LzwCompressBuff2User(psrc, srcSize, f_destCtrl, f_destPut, destLoc, destSizeMax) \
    LzwCompress(LzwBuffSrcC(psrc, srcSize), f_destCtrl, f_destPut, destLoc, destSizeMax)
#define LzwCompressFd2Buff(fdSrc, srcSize, pdest, destSizeMax) \
    LzwCompress(LzwFdSrcC(fdSrc, srcSize), LzwBuffDestC(pdest, destSizeMax))
#define LzwCompressFd2Fd(fdSrc, srcSize, fdDest) LzwCompress(LzwFdSrcC(fdSrc, srcSize), LzwFdDestC(fdDest))
#define LzwCompressFd2Fp(fdSrc, srcSize, fpDest) LzwCompress(LzwFdSrcC(fdSrc, srcSize), LzwFpDestC(fpDest))
#define LzwCompressFp2Buff(fpSrc, srcSize, pdest, destSizeMax) \
    LzwCompress(LzwFpSrcC(fpSrc, srcSize), LzwBuffDestC(pdest, destSizeMax))
#define LzwCompressFp2Fd(fpSrc, srcSize, fdDest) LzwCompress(LzwFpSrcC(fpSrc, srcSize), LzwFdDestC(fdDest))
#define LzwCompressFp2Fp(fpSrc, srcSize, fpDest) LzwCompress(LzwFpSrcC(fpSrc, srcSize), LzwFpDestC(fpDest))

#define LzwExpandBuff2Buff(psrc, pdest, destSkip, destSize) \
    LzwExpand(LzwBuffSrcE(psrc), LzwBuffDestE(pdest, destSkip, destSize))
#define LzwExpandBuff2Fd(psrc, fdDest, destSkip, destSize) \
    LzwExpand(LzwBuffSrcE(psrc), LzwFdDestE(fdDest, destSkip, destSize))
#define LzwExpandBuff2Fp(psrc, fpDest, destSkip, destSize) \
    LzwExpand(LzwBuffSrcE(psrc), LzwFpDestE(fpDest, destSkip, destSize))
#define LzwExpandFd2Buff(fdSrc, pdest, destSkip, destSize) \
    LzwExpand(LzwFdSrcE(fdSrc), LzwBuffDestE(pdest, destSkip, destSize))
#define LzwExpandFd2Fd(fdSrc, fdDest, destSkip, destSize) \
    LzwExpand(LzwFdSrcE(fdSrc), LzwFdDestE(fdDest, destSkip, destSize))
#define LzwExpandFd2Fp(fdSrc, fpDest, destSkip, destSize) \
    LzwExpand(LzwFdSrcE(fdSrc), LzwFpDestE(fpDest, destSkip, destSize))
#define LzwExpandFp2Buff(fpSrc, pdest, destSkip, destSize) \
    LzwExpand(LzwFpSrcE(fpSrc), LzwBuffDestE(pdest, destSkip, destSize))
#define LzwExpandFp2Fd(fpSrc, fdDest, destSkip, destSize) \
    LzwExpand(LzwFpSrcE(fpSrc), LzwFdDestE(fdDest, destSkip, destSize))
#define LzwExpandFp2Fp(fpSrc, fpDest, destSkip, destSize) \
    LzwExpand(LzwFpSrcE(fpSrc), LzwFpDestE(fpDest, destSkip, destSize))

/* ---------------- module state (0x446B50..0x446BD5) ---------------- */
typedef struct {                        /* compress state, 0x446B78 */
    unsigned int next_code;             /* next available string code */
    unsigned int character;             /* current character read from source */
    unsigned int string_code;           /* current string compress code */
    unsigned int index;                 /* index into string table */
    long lzwInputCharCount;             /* input character count */
    long lzwOutputSize;                 /* current size of output */
    int lzwOutputBitCount;              /* current bit location in output */
    ulong lzwOutputBitBuffer;           /* 32-bit buffer holding output bits */
} LzwC;
LG_STATIC_ASSERT(sizeof(LzwC) == 32, LzwC_size);

typedef struct {                        /* expand state, 0x446B50 */
    int lzwInputBitCount;
    ulong lzwInputBitBuffer;
    unsigned int next_code;             /* next available string code */
    unsigned int new_code;              /* next code from source */
    unsigned int old_code;              /* last code gotten from source */
    unsigned int character;             /* current char for string stack */
    uchar *string;                      /* used to output string in reverse order */
    long outputSize;                    /* size of uncompressed data */
    long destSkip;                      /* # bytes to skip over */
    long destSize;                      /* destination size */
} LzwE;
LG_STATIC_ASSERT(sizeof(LzwE) == 40, LzwE_size);

extern LzwE lzwe;                       /* 0x446B50 */
extern LzwC lzwc;                       /* 0x446B78 */
extern int lzwWriteBuffIndex;           /* 0x446B98 */
extern FILE *lzwFpDest;                 /* 0x446B9C */
extern int lzwFdDest;                   /* 0x446BA0 */
extern int lzwReadBuffIndex;            /* 0x446BA4 */
extern uchar *lzwBuffDestPtr;           /* 0x446BA8 */
extern uchar *lzwBuffSrcPtr;            /* 0x446BAC */
extern FILE *lzwFpSrc;                  /* 0x446BB0 */
extern int lzwFdSrc;                    /* 0x446BB4 */
extern uchar *lzwDecodeStack;           /* 0x446BB8 decoded string */
extern uchar *lzwFdWriteBuff;           /* 0x446BBC buffer for file descriptor dest */
extern uchar *lzwFdReadBuff;            /* 0x446BC0 buffer for file descriptor source */
extern uchar *lzwAppendChar;            /* 0x446BC4 appended chars array */
extern void *lzwBuffer;                 /* 0x446BC8 total buffer */
extern short *lzwCodeValue;             /* 0x446BCC code value array */
extern ushort *lzwPrefixCode;           /* 0x446BD0 prefix code array */
extern bool lzwBufferMalloced;          /* 0x446BD4 buffer malloced? */

#endif
