/* resload.c -- x:\prj\tech\libsrc\res\resload.c (Terra Nova v1.09): loading resources from disk.
 * Decompiled from __FF.EXE.
 *
 *   0x304400 ResLoadResource   0x304460 ResRetrieve
 */
#include "res.h"
#include "lzw.h"

#ifndef TN_LINK_ORIGINAL_DATA
Id resLoadingId;
#endif

/* Allocates memory for a resource and loads it.  Returns the pointer or NULL. */
void *ResLoadResource(Id id)
{
    ResDesc *prd = RESDESC(id);
    void *p;

    if (prd->size == 0)
        return NULL;

    /* Tell the pager which resource is being loaded, so it isn't the one paged out. */
    resLoadingId = id;
    p = Malloc(prd->size);
    resLoadingId = 0;
    prd->ptr = p;
    if (prd->ptr == NULL)
        return NULL;

    ResRetrieve(id, prd->ptr);
    return prd->ptr;
}

/* Reads a resource from its file into buffer (a compound resource keeps its ref table at the
 * start of the buffer).  Always returns TRUE (Freefall: FALSE for a damaged file's ref table). */
bool ResRetrieve(Id id, void *buffer)
{
    ResDesc *prd = RESDESC(id);
    ResDesc2 *prd2 = RESDESC2(id);
    int fd = resFile[prd->filenum].fd;
    uchar *p = buffer;
    long size;
    long tableSize;

    lseek(fd, prd->offset, SEEK_SET);
    size = prd->size;

    if (prd2->flags & RDF_COMPOUND) {
#ifndef TN_WATCOM_ABI
        /* G27: a damaged file (a saved game) can say anything: a ref table bigger than the
         * resource was read past the buffer */
        if (size < (long)sizeof(RefIndex))
            return FALSE;
#endif
        read(fd, p, sizeof(RefIndex));
        tableSize = ((RefTable *)buffer)->numRefs * sizeof(long);
#ifndef TN_WATCOM_ABI
        if (tableSize + sizeof(long) + sizeof(RefIndex) > (unsigned long)size)
            return FALSE;
#endif
        p += sizeof(RefIndex);
        read(fd, p, tableSize + sizeof(long));
        p += tableSize + sizeof(long);
        size -= tableSize + sizeof(long) + sizeof(RefIndex);
    }

#ifndef TN_WATCOM_ABI
    /* G27: the expansion stops at the resource's size (it went on as long as the data, which
     * in a damaged file is anything) */
    if (prd2->flags & RDF_LZW) {
        if (size > 0)
            LzwExpandFd2Buff(fd, p, 0, size);
    } else
#else
    if (prd2->flags & RDF_LZW)
        LzwExpandFd2Buff(fd, p, 0, 0);
    else
#endif
        read(fd, p, size);

    if ((prd2->flags & RDF_CDSPOOF) && resCDSpoofFunc)
        (*resCDSpoofFunc)(size, id);

    return TRUE;
}
