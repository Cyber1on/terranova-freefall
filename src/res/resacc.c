/* resacc.c -- x:\prj\tech\libsrc\res\resacc.c (Terra Nova v1.09): access to resources by id.
 * Decompiled from __FF.EXE.
 *
 *   0x2EB380 ResLock   0x2EB3F4 ResUnlock   0x2EB460 ResGet   0x2EB4EC ResExtract
 *   0x2EB504 ResDrop   0x2EB55C ResDelete   0x2EB5C8 ResCheckId
 *
 * Loaded, unlocked resources sit in an LRU chain (ID_HEAD .. ID_TAIL) so the pager can free the
 * least recently used ones; locking takes a resource out of the chain.
 */
#include "res.h"

/* Locks a resource (loading it if needed) and returns a pointer to it. */
void *ResLock(Id id)
{
    ResDesc *prd = RESDESC(id);

    if (prd->ptr == NULL) {
        if (ResLoadResource(id) == NULL)
            return NULL;
    } else if (prd->lock == 0) {
        ResRemoveFromLRU(prd);
    }
    prd->lock++;
    return prd->ptr;
}

/* Unlocks a resource; when the last lock goes it joins the tail of the LRU chain. */
void ResUnlock(Id id)
{
    ResDesc *prd = RESDESC(id);

    if (prd->lock) {
        prd->lock--;
        if (prd->lock == 0)
            ResAddToTail(prd);
    }
}

/* Gets a pointer to a resource without locking it (it may be paged out later), and moves it
 * to the tail of the LRU chain. */
void *ResGet(Id id)
{
    ResDesc *prd = RESDESC(id);

    if (prd->ptr == NULL) {
        if (ResLoadResource(id) == NULL)
            return NULL;
        ResAddToTail(prd);
    } else if (prd->lock == 0 && prd->next != ID_TAIL) {
        ResRemoveFromLRU(prd);
        ResAddToTail(prd);
    }
    return prd->ptr;
}

/* Reads a resource into a caller-supplied buffer.  Returns buffer, or NULL on failure. */
void *ResExtract(Id id, void *buffer)
{
    if (ResRetrieve(id, buffer))
        return buffer;
    return NULL;
}

/* Drops a resource from memory (it stays available on disk). */
void ResDrop(Id id)
{
    ResDesc *prd = RESDESC(id);

    if (prd->lock == 0)
        ResRemoveFromLRU(prd);
    if (prd->ptr) {
        Free(prd->ptr);
        prd->ptr = NULL;
    }
}

/* Deletes a resource forever (frees memory and clears its descriptor). */
void ResDelete(Id id)
{
    ResDesc *prd = RESDESC(id);

    if (ResInUse(id)) {
        if (prd->ptr) {
            Free(prd->ptr);
            if (prd->lock == 0)
                ResRemoveFromLRU(prd);
        }
        memset(prd, 0, sizeof(ResDesc));
    }
}

/* Checks that an id is in the valid range. */
bool ResCheckId(Id id)
{
    if (id < ID_MIN)
        return FALSE;
    if (id > resDescMax)
        return FALSE;
    return TRUE;
}
