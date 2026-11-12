/* resbuild.c -- x:\prj\tech\libsrc\res\resbuild.c (Terra Nova v1.09): writing resources to
 * resource files open for editing.  Decompiled from __FF.EXE.
 *
 *   0x2FC620 ResSetComment    0x2FC664 ResWrite         0x2FC9FC ResKill
 *   0x2FCA14 ResPack          0x2FCBA4 ResCopyBytes     0x2FCC58 ResEraseIfInFile
 *
 * Terra Nova's version can use a caller-supplied work buffer (resTempBuff) for compression and
 * copying instead of allocating one; nothing in the game sets it.
 */
#include "resbuild.h"
#include "lzw.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar resPad[4];
void *resTempBuff;
long resTempBuffSize;
#endif

#define CTRL_Z 26                       /* ends the comment, so the header can be TYPEd */
#define EXTRA 250                       /* slack for LZW output that grows */
#define SIZE_RESCOPY 32768

/* Sets the comment in a resource file's header. */
void ResSetComment(int filenum, char *comment)
{
    ResFileHeader *phead = &resFile[filenum].pedit->hdr;

    memset(phead->comment, 0, sizeof(phead->comment));
    strncpy(phead->comment, comment, sizeof(phead->comment) - 2);
    phead->comment[strlen(phead->comment)] = CTRL_Z;
}

/* Writes a resource at the end of the data of its file (replacing any older copy in the
 * directory), compressing it if it is flagged RDF_LZW.  Returns the number of bytes written,
 * or -1 on a write error. */
int ResWrite(Id id)
{
    ResDesc *prd;
    ResDesc2 *prd2;
    ResFile *prf;
    ResDirEntry *pDirEntry;
    uchar *p;
    long size, sizeTable, compsize;
    void *pcompbuff;
    int padBytes, n, total = 0;
    bool freeBuff = FALSE;

    prd = RESDESC(id);
    prd2 = RESDESC2(id);
    prf = &resFile[prd->filenum];

    ResEraseIfInFile(id);

    if (prf->pedit->pdir->numEntries == prf->pedit->numAllocDir) {
        prf->pedit->numAllocDir += DEFAULT_RES_GROWDIRENTRIES;
        prf->pedit->pdir = Realloc(prf->pedit->pdir,
                                   sizeof(ResDirHeader) + sizeof(ResDirEntry) * prf->pedit->numAllocDir);
    }

    prd->offset = prf->pedit->currDataOffset;

    pDirEntry = RESFILE_DIRENTRY(prf->pedit->pdir, prf->pedit->pdir->numEntries);
    pDirEntry->id = id;
    pDirEntry->flags = prd2->flags;
    pDirEntry->type = prd2->type;
    pDirEntry->size = prd->size;

    lseek(prf->fd, prf->pedit->currDataOffset, SEEK_SET);
    p = prd->ptr;
    sizeTable = 0;
    size = prd->size;

    /* a compound resource's ref table is never compressed */
    if (prd2->flags & RDF_COMPOUND) {
        sizeTable = REFTABLESIZE(((RefTable *)p)->numRefs);
        n = write(prf->fd, p, sizeTable);
        total += n;
        if (n != sizeTable)
            return -1;
        p += sizeTable;
        size -= sizeTable;
    }

    if (pDirEntry->flags & RDF_LZW) {
        if (resTempBuff == NULL || size > resTempBuffSize) {
            freeBuff = TRUE;
            pcompbuff = Malloc(size + EXTRA);
        } else {
            pcompbuff = resTempBuff;
        }
        compsize = LzwCompressBuff2Buff(p, size, pcompbuff, size);
        if (compsize < 0) {
            pDirEntry->flags &= ~RDF_LZW;   /* didn't shrink: store it uncompressed */
        } else {
            pDirEntry->csize = sizeTable + compsize;
            n = write(prf->fd, pcompbuff, compsize);
            total += n;
            if (n != compsize)
                return -1;
        }
        if (freeBuff)
            Free(pcompbuff);
    }

    if (!(pDirEntry->flags & RDF_LZW)) {
        pDirEntry->csize = prd->size;
        n = write(prf->fd, p, size);
        total += n;
        if (n != size)
            return -1;
    }

    padBytes = (4 - pDirEntry->csize) & 3;
    if (padBytes) {
        n = write(prf->fd, resPad, padBytes);
        total += n;
        if (n != padBytes)
            return -1;
    }
    tell(prf->fd);                      /* the misalignment warning was compiled out */

    prf->pedit->pdir->numEntries++;
    prf->pedit->currDataOffset = RES_OFFSET_ALIGN(prf->pedit->currDataOffset + pDirEntry->csize);
    return total;
}

/* Removes a resource from memory and from its file. */
void ResKill(Id id)
{
    ResDrop(id);
    ResEraseIfInFile(id);
}

/* Removes the holes left by erased resources from a resource file open for editing.
 * Returns the number of bytes reclaimed. */
long ResPack(int filenum)
{
    ResFile *prf = &resFile[filenum];
    ResDirEntry *pDirEntry, *peWrite;
    long numReclaimed, sizeReclaimed, dataRead, dataWrite;
    int i;

    if (prf->pedit == NULL)
        return 0;

    numReclaimed = sizeReclaimed = 0;
    dataRead = dataWrite = prf->pedit->pdir->dataOffset;

    /* move the data of every live entry down over the holes */
    pDirEntry = RESFILE_DIRENTRY(prf->pedit->pdir, 0);
    for (i = 0; i < prf->pedit->pdir->numEntries; i++, pDirEntry++) {
        if (pDirEntry->id == 0) {
            numReclaimed++;
            sizeReclaimed += pDirEntry->csize;
        } else {
            if (gResDesc[pDirEntry->id].offset > RES_OFFSET_PENDING)
                gResDesc[pDirEntry->id].offset = dataWrite;
            if (dataRead != dataWrite)
                ResCopyBytes(prf->fd, dataWrite, dataRead, pDirEntry->csize);
            dataWrite = RES_OFFSET_ALIGN(dataWrite + pDirEntry->csize);
        }
        dataRead = RES_OFFSET_ALIGN(dataRead + pDirEntry->csize);
    }

    /* then squeeze the empty entries out of the directory */
    pDirEntry = peWrite = RESFILE_DIRENTRY(prf->pedit->pdir, 0);
    for (i = 0; i < prf->pedit->pdir->numEntries; i++, pDirEntry++) {
        if (pDirEntry->id) {
            if (pDirEntry != peWrite)
                *peWrite = *pDirEntry;
            peWrite++;
        }
    }
    prf->pedit->pdir->numEntries -= numReclaimed;

    prf->pedit->currDataOffset = dataWrite;
    lseek(prf->fd, dataWrite, SEEK_SET);
    prf->pedit->flags &= ~RFF_NEEDSPACK;
    chsize(prf->fd, dataWrite);         /* the directory is written again on close */
    return sizeReclaimed;
}

/* Copies size bytes within a file, from readPos to writePos (lower), in 32K chunks. */
void ResCopyBytes(int fd, long writePos, long readPos, long size)
{
    long sizeCopy;
    uchar *buff;
    bool freeBuff = FALSE;

    if (resTempBuff == NULL || resTempBuffSize < SIZE_RESCOPY) {
        buff = Malloc(SIZE_RESCOPY);
        freeBuff = TRUE;
    } else {
        buff = resTempBuff;
    }

    while (size > 0) {
        sizeCopy = size > SIZE_RESCOPY ? SIZE_RESCOPY : size;
        lseek(fd, readPos, SEEK_SET);
        read(fd, buff, sizeCopy);
        lseek(fd, writePos, SEEK_SET);
        size -= sizeCopy;
        write(fd, buff, sizeCopy);
        readPos += sizeCopy;
        writePos += sizeCopy;
    }

    if (freeBuff)
        Free(buff);
}

/* If a resource is in its file's directory, marks the entry empty (and packs the file if it
 * is set to auto-pack).  Returns TRUE if it was there. */
bool ResEraseIfInFile(Id id)
{
    ResDesc *prd = RESDESC(id);
    ResFile *prf = &resFile[prd->filenum];
    ResDirEntry *pDirEntry;
    int i;

    pDirEntry = RESFILE_DIRENTRY(prf->pedit->pdir, 0);
    for (i = 0; i < prf->pedit->pdir->numEntries; i++, pDirEntry++) {
        if (pDirEntry->id == id) {
            pDirEntry->id = 0;
            prf->pedit->flags |= RFF_NEEDSPACK;
            if (prf->pedit->flags & RFF_AUTOPACK)
                ResPack(prd->filenum);
            return TRUE;
        }
    }
    return FALSE;
}
