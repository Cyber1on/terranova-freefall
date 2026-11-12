/* resexblk.c -- x:\prj\tech\libsrc\res\resexblk.c (Terra Nova v1.09): extracting a resource
 * in blocks, handing each block to a callback.  Decompiled from __FF.EXE.
 *
 *   0x2F5C40 ResExtractInBlocks   0x2F5D24 RefExtractInBlocks   0x2F5E38 LzwBlockDestCtrl
 *   0x2F5E6C LzwBlockDestPut      0x2F5EF4 ResReadBlocks
 *
 * The callback gets (buff, numBytes, iBlock) and returns the size of the next block (<= 0 to
 * keep the current size).  Compressed data is expanded through an LZW destination that
 * calls the callback each time the buffer fills.  A compound resource that is also
 * compressed is silently skipped by ResExtractInBlocks.  The game uses it once (0x293F3D).
 */
#include "res.h"
#include "lzw.h"

/* Called with the Watcom convention (three register args), hence WATCOM_CALL3. */
typedef long (*ResBlockFunc)(void *buff, long numBytes, long iBlock);

#ifndef TN_LINK_ORIGINAL_DATA
static uchar *lzwBlockBuff;            /* 0x44C6D0 */
static long lzwBlockSize;              /* 0x44C6D4 */
static long lzwBlockNumPassed;         /* 0x44C6D8 */
static long lzwBlockTotal;             /* 0x44C6DC */
static ResBlockFunc lzwBlockFunc;      /* 0x44C6E0 */
static long lzwBlockIndex;             /* 0x44C6E4: block number */
static long lzwBlockBuffIndex;         /* 0x44C6E8: bytes in the buffer */
#else
extern uchar *lzwBlockBuff;
extern long lzwBlockSize, lzwBlockNumPassed, lzwBlockTotal, lzwBlockIndex, lzwBlockBuffIndex;
extern ResBlockFunc lzwBlockFunc;
#endif

void WATCOMCALL LzwBlockDestCtrl(long destLoc, LzwCtrl ctrl);
void WATCOMCALL LzwBlockDestPut(uchar b);
void ResReadBlocks(int fd, void *buff, long blockSize, long size, ResBlockFunc f_ProcBlock);

/* Extracts a resource in blocks of blockSize bytes (the callback may change the size). */
void ResExtractInBlocks(Id id, void *buff, long blockSize, ResBlockFunc f_ProcBlock)
{
    ResDesc *prd = RESDESC(id);
    ResDesc2 *prd2;
    int fd;

    if (prd->size == 0 || blockSize == 0)
        return;

    prd2 = RESDESC2(id);
    fd = resFile[prd->filenum].fd;
    lseek(fd, prd->offset, SEEK_SET);

    if (!(prd2->flags & RDF_LZW)) {
        ResReadBlocks(fd, buff, blockSize, prd->size, f_ProcBlock);
    } else if (!(prd2->flags & RDF_COMPOUND)) {
        lzwBlockBuff = buff;
        lzwBlockTotal = prd->size;
        lzwBlockFunc = f_ProcBlock;
        lzwBlockSize = blockSize;
        LzwExpand(LzwFdSrcCtrl, LzwFdSrcGet, fd, LzwBlockDestCtrl, LzwBlockDestPut, (long)buff, 0, 0);
    }
}

/* Extracts one item of a compound resource in blocks. */
void RefExtractInBlocks(RefTable *prt, Ref ref, void *buff, long blockSize, ResBlockFunc f_ProcBlock)
{
    Id id = REFID(ref);
    RefIndex index = REFINDEX(ref);
    ResDesc *prd = RESDESC(id);
    int fd = resFile[prd->filenum].fd;
    long size, skip;

    size = REFSIZE(prt, index);
    if (size == 0 || blockSize == 0)
        return;

    /* the ref table is stored uncompressed; the items follow it */
    lseek(fd, prd->offset + REFTABLESIZE(prt->numRefs), SEEK_SET);
    skip = prt->offset[index] - REFTABLESIZE(prt->numRefs);

    if (!(RESDESC2(id)->flags & RDF_LZW)) {
        lseek(fd, skip, SEEK_CUR);
        ResReadBlocks(fd, buff, blockSize, size, f_ProcBlock);
    } else {
        lzwBlockBuff = buff;
        lzwBlockTotal = size;
        lzwBlockFunc = f_ProcBlock;
        lzwBlockSize = blockSize;
        LzwExpand(LzwFdSrcCtrl, LzwFdSrcGet, fd, LzwBlockDestCtrl, LzwBlockDestPut, (long)buff, skip, size);
    }
}

/* LZW destination: start of expansion. */
void WATCOMCALL LzwBlockDestCtrl(long destLoc, LzwCtrl ctrl)
{
    if (ctrl == BEGIN) {
        lzwBlockIndex = 0;
        lzwBlockNumPassed = 0;
        lzwBlockBuffIndex = 0;
        if (lzwBlockSize > lzwBlockTotal)
            lzwBlockSize = lzwBlockTotal;
    }
}

/* LZW destination: one expanded byte; hands the buffer to the callback when it is full. */
void WATCOMCALL LzwBlockDestPut(uchar b)
{
    long next, remain;

    lzwBlockBuff[lzwBlockBuffIndex++] = b;
    if (lzwBlockBuffIndex >= lzwBlockSize) {
        next = WATCOM_CALL3(lzwBlockFunc, lzwBlockBuff, lzwBlockSize, lzwBlockIndex);
        lzwBlockNumPassed += lzwBlockSize;
        remain = lzwBlockTotal - lzwBlockNumPassed;
        if (next > 0)
            lzwBlockSize = next;
        if (lzwBlockSize > remain)
            lzwBlockSize = remain;
        lzwBlockBuffIndex = 0;
        lzwBlockIndex++;
    }
}

/* Reads size bytes from the current file position in blocks. */
void ResReadBlocks(int fd, void *buff, long blockSize, long size, ResBlockFunc f_ProcBlock)
{
    long numRead = 0, n, next;
    long iBlock = 0;

    if (size <= 0)
        return;
    do {
        n = size - numRead;
        if (blockSize <= n)
            n = blockSize;
        read(fd, buff, n);
        next = WATCOM_CALL3(f_ProcBlock, buff, n, iBlock);
        if (next > 0)
            blockSize = next;
        numRead += n;
        iBlock++;
    } while (numRead < size);
}
