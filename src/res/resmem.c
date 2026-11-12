/* resmem.c -- x:\prj\tech\libsrc\res\resmem.c (Terra Nova v1.09): RES memory management.
 * Decompiled from __FF.EXE.
 *
 *   0x2FCD80 ResMalloc        0x2FCD9C ResRealloc       0x2FCE4C ResFree
 *   0x2FCE54 ResInstallPager  0x2FCE6C ResPage          0x2FCE7C ResDefaultPager
 *
 * When malloc fails, the pager frees unlocked resources, oldest first (the LRU chain), and
 * retries.  ResInit installs these three functions as the LG allocators, so the whole game
 * allocates through them.
 */
#include "resmem.h"

#ifndef TN_LINK_ORIGINAL_DATA
ResPagerFunc f_pager = WATCOM_FN(ResDefaultPager);
long resPagedBytes;
#endif

/* Usable size of a Watcom heap block: the word before it is the block size (header
 * included) with bit 0 set while the block is in use. */
#ifdef TN_WATCOM_ABI
#define HEAP_BLOCK_SIZE(p) ((((ulong *)(p))[-1] & ~1UL) - sizeof(ulong))
#elif defined(_WIN32)
#include <malloc.h>
#define HEAP_BLOCK_SIZE(p) _msize(p)    /* the host heap keeps no size before the block */
#else
size_t port_heap_size(void *p);         /* malloc_usable_size, or lockstep.c's own heap */
#define HEAP_BLOCK_SIZE(p) port_heap_size(p)
#endif

/* Allocates memory, paging resources out if the heap is full. */
void *ResMalloc(size_t size)
{
    void *p = malloc(size);
    if (p == NULL && (long)size > 0)
        p = f_pager(size);
    return p;
}

/* Reallocates memory, paging resources out if the heap is full.  When even that fails, the
 * old block is freed first and the allocation tried again, so the data survives only because
 * free() leaves a freed block's contents in place. */
void *ResRealloc(void *p, size_t newsize)
{
    size_t oldsize = HEAP_BLOCK_SIZE(p);
    size_t n;
    void *pnew;

    pnew = malloc(newsize);
    if (pnew == NULL)
        pnew = ResPage(newsize);
    if (pnew == NULL && (long)newsize > 0)
        pnew = f_pager(newsize);

    if (pnew != NULL) {
        n = newsize < oldsize ? newsize : oldsize;
        memcpy(pnew, p, n);
        free(p);
    } else {
        free(p);
        pnew = malloc(newsize);
        if (pnew == NULL)
            pnew = ResPage(newsize);
        if (pnew != NULL && pnew != p) {
            n = newsize < oldsize ? newsize : oldsize;
            memmove(pnew, p, n);
        }
    }
    return pnew;
}

/* Frees memory. */
void ResFree(void *p)
{
    free(p);
}

/* Installs a pager; NULL restores the default one. */
void ResInstallPager(ResPagerFunc f)
{
    if (f)
        f_pager = f;
    else
        f_pager = WATCOM_FN(ResDefaultPager);
}

/* Calls the pager for a block of size bytes. */
void *ResPage(long size)
{
    if (size <= 0)
        return NULL;
    return f_pager(size);
}

/* Default pager: drops resources from the head of the LRU chain until 10000 bytes more than
 * needed have been released and malloc succeeds, or the chain is empty. */
void *ResDefaultPager(long size)
{
    long freed = 0;
    Id id, next;
    void *p;

    next = gResDesc[ID_HEAD].next;
    while (next != ID_TAIL) {
        id = next;
        next = gResDesc[id].next;
        freed += gResDesc[id].size;
        ResDrop(id);
        if (freed > size + 10000 && (p = malloc(size)) != NULL)
            goto done;
    }
    p = malloc(size);
done:
    resPagedBytes += freed;
    return p;
}
