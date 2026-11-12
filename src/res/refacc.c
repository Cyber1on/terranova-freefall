/* refacc.c -- x:\prj\tech\libsrc\res\refacc.c (Terra Nova v1.09): access to the items (refs) of
 * compound resources.  Decompiled from __FF.EXE.
 *
 *   0x2E3110 RefLock           0x2E3194 RefGet             0x2E3230 ResReadRefTable
 *   0x2E32C0 ResExtractRefTable 0x2E3348 ResNumRefs        0x2E33C4 RefExtract
 *   0x2E3510 RefInject         0x2E3628 RefCheckRef
 *
 * A compound resource starts with a RefTable: numRefs, then numRefs+1 offsets from the start
 * of the resource (the last one is the end of the last item).
 */
#include "res.h"
#include "lzw.h"

/* Locks a compound resource and returns a pointer to one of its items. */
void *RefLock(Ref ref)
{
    Id id = REFID(ref);
    ResDesc *prd = RESDESC(id);
    RefTable *prt;

    if (prd->ptr == NULL) {
        if (ResLoadResource(id) == NULL)
            return NULL;
    } else if (prd->lock == 0) {
        ResRemoveFromLRU(prd);
    }
    prd->lock++;

    prt = (RefTable *)prd->ptr;
    return REFPTR(prt, REFINDEX(ref));
}

/* Gets a pointer to an item of a compound resource without locking (see ResGet). */
void *RefGet(Ref ref)
{
    Id id = REFID(ref);
    ResDesc *prd = RESDESC(id);
    RefTable *prt;

    if (prd->ptr == NULL) {
        if (ResLoadResource(id) == NULL)
            return NULL;
        ResAddToTail(prd);
    } else if (prd->lock == 0 && prd->next != ID_TAIL) {
        ResRemoveFromLRU(prd);
        ResAddToTail(prd);
    }

    prt = (RefTable *)prd->ptr;
    return REFPTR(prt, REFINDEX(ref));
}

/* Allocates and reads the ref table of a compound resource.  NULL if not compound. */
RefTable *ResReadRefTable(Id id)
{
    ResDesc *prd = RESDESC(id);
    int fd = resFile[prd->filenum].fd;
    RefIndex numRefs;
    RefTable *prt;

    if (!(gResDesc2[id].flags & RDF_COMPOUND))
        return NULL;

    lseek(fd, prd->offset, SEEK_SET);
    read(fd, &numRefs, sizeof(RefIndex));
    prt = Malloc(REFTABLESIZE(numRefs));
    prt->numRefs = numRefs;
    read(fd, &prt->offset[0], sizeof(long) * (numRefs + 1));
    return prt;
}

/* Reads the ref table of a compound resource into a buffer of the given size.
 * Returns 0, or -1 if not compound or the buffer is too small. */
int ResExtractRefTable(Id id, RefTable *prt, long size)
{
    ResDesc *prd = RESDESC(id);
    int fd = resFile[prd->filenum].fd;

    if (!(gResDesc2[id].flags & RDF_COMPOUND))
        return -1;

    lseek(fd, prd->offset, SEEK_SET);
    read(fd, &prt->numRefs, sizeof(RefIndex));
    if ((ulong)REFTABLESIZE(prt->numRefs) > (ulong)size)
        return -1;
    read(fd, &prt->offset[0], sizeof(long) * (prt->numRefs + 1));
    return 0;
}

/* Returns the number of items in a compound resource (reading the count from disk if it is
 * not loaded), or -1 if it isn't compound. */
int ResNumRefs(Id id)
{
    ResDesc *prd;
    int fd;
    RefIndex numrefs;

    if (!(gResDesc2[id].flags & RDF_COMPOUND))
        return -1;

    prd = RESDESC(id);
    if (prd->ptr)
        return ((RefTable *)prd->ptr)->numRefs;

    fd = resFile[prd->filenum].fd;
    lseek(fd, prd->offset, SEEK_SET);
    read(fd, &numrefs, sizeof(RefIndex));
    return numrefs;
}

/* Reads one item of a compound resource into buff.  prt may be NULL, in which case the
 * needed parts of the ref table are read from disk.  Returns buff. */
void *RefExtract(RefTable *prt, Ref ref, void *buff)
{
    Id id = REFID(ref);
    RefIndex index = REFINDEX(ref);
    ResDesc *prd = RESDESC(id);
    int fd = resFile[prd->filenum].fd;
    long refsize, offset;
    RefIndex numrefs;

    if (prt) {
        refsize = prt->offset[index + 1] - prt->offset[index];
        numrefs = prt->numRefs;
        offset = prt->offset[index];
    } else {
        lseek(fd, prd->offset, SEEK_SET);
        read(fd, &numrefs, sizeof(RefIndex));
        lseek(fd, sizeof(long) * index, SEEK_CUR);
        read(fd, &offset, sizeof(long));
        read(fd, &refsize, sizeof(long));
        refsize -= offset;
    }

    lseek(fd, prd->offset + REFTABLESIZE(numrefs), SEEK_SET);
    if (gResDesc2[id].flags & RDF_LZW) {
        LzwExpandFd2Buff(fd, buff, offset - REFTABLESIZE(numrefs), refsize);
    } else {
        lseek(fd, offset - REFTABLESIZE(numrefs), SEEK_CUR);
        read(fd, buff, refsize);
    }
    return buff;
}

/* Writes an item of a compound resource back to its file (same size).  Not possible for
 * compressed resources (returns 0); otherwise returns write()'s result. */
int RefInject(RefTable *prt, Ref ref, void *buff)
{
    Id id = REFID(ref);
    RefIndex index;
    ResDesc *prd;
    int fd;
    long refsize, offset;
    RefIndex numrefs;

    if (gResDesc2[id].flags & RDF_LZW)
        return 0;

    prd = RESDESC(id);
    fd = resFile[prd->filenum].fd;
    index = REFINDEX(ref);
    if (prt) {
        refsize = prt->offset[index + 1] - prt->offset[index];
        numrefs = prt->numRefs;
        offset = prt->offset[index];
    } else {
        lseek(fd, prd->offset, SEEK_SET);
        read(fd, &numrefs, sizeof(RefIndex));
        lseek(fd, sizeof(long) * index, SEEK_CUR);
        read(fd, &offset, sizeof(long));
        read(fd, &refsize, sizeof(long));
        refsize -= offset;
    }

    lseek(fd, prd->offset + REFTABLESIZE(numrefs), SEEK_SET);
    lseek(fd, offset - REFTABLESIZE(numrefs), SEEK_CUR);
    return write(fd, buff, refsize);
}

/* Checks that a ref names a valid compound resource (the index is not checked). */
bool RefCheckRef(Ref ref)
{
    Id id = REFID(ref);

    if (!ResCheckId(id))
        return FALSE;
    if (!(gResDesc2[id].flags & RDF_COMPOUND))
        return FALSE;
    return TRUE;
}
