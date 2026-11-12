/* resmake.c -- x:\prj\tech\libsrc\res\resmake.c (Terra Nova v1.09): making resources from data
 * in memory.  Decompiled from __FF.EXE.
 *
 *   0x2FC200 ResMake    0x2FC2C4 ResMakeCompound    0x2FC30C ResAddRef    0x2FC5F0 ResUnmake
 */
#include "resbuild.h"

/* Makes a resource from a block of memory (not copied: the block becomes the resource).
 * The resource is locked, so it can't be paged out, and marked as not yet written. */
void ResMake(Id id, void *ptr, long size, uchar type, int filenum, uchar flags)
{
    ResDesc *prd;
    ResDesc2 *prd2;

    ResExtendDesc(id);
    prd = RESDESC(id);
    if (prd->offset)
        ResDelete(id);

    prd->ptr = ptr;
    prd->size = size;
    prd->filenum = filenum;
    prd->offset = 0;
    prd->lock = 0;
    prd->lock |= 1;
    prd->offset |= RES_OFFSET_PENDING;

    prd2 = RESDESC2(id);
    prd2->flags = flags;
    prd2->type = type;
}

/* Makes an empty compound resource. */
void ResMakeCompound(Id id, uchar type, int filenum, uchar flags)
{
    RefTable *prt;

    prt = Malloc(REFTABLESIZE(0));
    prt->numRefs = 0;
    prt->offset[0] = REFTABLESIZE(0);
    ResMake(id, prt, REFTABLESIZE(0), type, filenum, flags | RDF_COMPOUND);
}

/* Adds an item to a compound resource, or replaces one (the item is copied).  An index past
 * the end extends the table; skipped items get size 0. */
void ResAddRef(Ref ref, void *pitem, long itemSize)
{
    ResDesc *prd;
    RefTable *prt;
    RefIndex index, i;
    long oldSize, sizeDiff;

    prd = RESDESC(REFID(ref));
    prt = (RefTable *)prd->ptr;
    if (prt == NULL)
        prt = (RefTable *)RefGet(ref);   /* sic: returns the item, not the table */

    index = REFINDEX(ref);
    if (index < prt->numRefs) {
        oldSize = REFSIZE(prt, index);
        if (itemSize == oldSize) {
            memcpy(REFPTR(prt, index), pitem, itemSize);
        } else if (itemSize < oldSize) {
            /* smaller: pull the following items down, copy, shrink the block */
            sizeDiff = oldSize - itemSize;
            for (i = index + 1; i <= prt->numRefs; i++)
                prt->offset[i] -= sizeDiff;
            prd->size -= sizeDiff;
            memmove(REFPTR(prt, index + 1), REFPTR(prt, index + 1) + sizeDiff,
                    prt->offset[prt->numRefs] - prt->offset[index + 1]);
            memcpy(REFPTR(prt, index), pitem, itemSize);
            prd->ptr = Realloc(prd->ptr, prd->size);
        } else {
            /* larger: grow the block, push the following items up, copy */
            sizeDiff = itemSize - oldSize;
            prd->size += sizeDiff;
            prd->ptr = prt = Realloc(prd->ptr, prd->size);
            memmove(REFPTR(prt, index + 1) + sizeDiff, REFPTR(prt, index + 1),
                    prt->offset[prt->numRefs] - prt->offset[index + 1]);
            for (i = index + 1; i <= prt->numRefs; i++)
                prt->offset[i] += sizeDiff;
            memcpy(REFPTR(prt, index), pitem, itemSize);
        }
    } else {
        /* past the end: make room for the new offsets, then append the item */
        long sizeOffsets = ((index + 1) - prt->numRefs) * sizeof(long);
        prd->size += itemSize + sizeOffsets;
        prd->ptr = prt = Realloc(prd->ptr, prd->size);
        memmove(REFPTR(prt, 0) + sizeOffsets, REFPTR(prt, 0), prd->size - REFTABLESIZE(index + 1));
        for (i = 0; i <= prt->numRefs; i++)
            prt->offset[i] += sizeOffsets;
        for (i = prt->numRefs + 1; i <= index; i++)
            prt->offset[i] = prt->offset[prt->numRefs];
        prt->offset[index + 1] = prt->offset[index] + itemSize;
        memcpy(REFPTR(prt, index), pitem, itemSize);
        prt->numRefs = index + 1;
    }
}

/* Makes RES forget a resource without freeing its data (the caller owns the memory again). */
void ResUnmake(Id id)
{
    memset(RESDESC(id), 0, sizeof(ResDesc));
}
