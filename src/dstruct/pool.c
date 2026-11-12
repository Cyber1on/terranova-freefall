/* pool.c -- fixed-size element pools (Terra Nova v1.09; LG dstruct code with no System Shock
 * counterpart, names ours).  Decompiled from __FF.EXE.
 *
 *   0x2F5FA0 PoolInit   0x2F5FEC PoolFreeAll   0x2F6020 PoolAlloc   0x2F605C PoolCalloc
 *   0x2F60B0 PoolFree   0x2F6154 PoolGrow (static)
 *
 * A pool is a list of blocks (newest first), each a 10-byte header {llist link; ushort used}
 * followed by perblock elements of elemsize (>= 8) bytes.  Free elements form one list
 * through the whole pool; a free element holds {next, block}.
 * Quirks: PoolGrow with perblock 0 writes one element past the block; PoolFree of a pointer
 * outside every block does nothing (count unchanged).
 */
#include "sup_a.h"

#define BLOCK_HDR 10
#define POOL_TAIL(p) (&(p)->blocks_tail)

void PoolInit(Pool *p, ushort elemsize, ushort perblock, bool freeEmpty)
{
    p->count = 0;
    p->freelist = NULL;
    p->blocks_tail.pprev = &p->blocks;
    p->blocks.pnext = &p->blocks_tail;
    p->elemsize = elemsize > 8 ? elemsize : 8;
    p->perblock = perblock;
    p->freeEmpty = freeEmpty;
    p->blocksize = (long)p->elemsize * p->perblock + BLOCK_HDR;
}

/* Frees every block. */
void PoolFreeAll(Pool *p)
{
    llist *b = p->blocks.pnext, *next;

    while (b != POOL_TAIL(p)) {
        next = b->pnext;
        (*f_free)(b);
        b = next;
    }
    p->count = 0;
    p->freelist = NULL;
    p->blocks.pnext = POOL_TAIL(p);
    p->blocks_tail.pprev = &p->blocks;
}

/* Adds a block at the head of the block list and puts its elements on the free list. */
void PoolGrow(Pool *p)
{
    PoolBlock *b = (*f_malloc)(p->blocksize);
    PoolFreeElem *e;
    int i;

    if (b == NULL)
        return;
    b->link.pnext = p->blocks.pnext;
    b->link.pprev = &p->blocks;
    p->blocks.pnext->pprev = &b->link;
    p->blocks.pnext = &b->link;
    b->used = 0;
    e = (PoolFreeElem *)((char *)b + BLOCK_HDR);
    for (i = 0; i < (int)p->perblock - 1; i++) {
        e->block = b;
        e->next = (PoolFreeElem *)((char *)e + p->elemsize);
        e = e->next;
    }
    e->next = p->freelist;
    e->block = b;
    p->freelist = (PoolFreeElem *)((char *)b + BLOCK_HDR);
}

void *PoolAlloc(Pool *p)
{
    PoolFreeElem *e;

    if (p->freelist == NULL) {
        PoolGrow(p);
        if (p->freelist == NULL)
            return NULL;
    }
    e = p->freelist;
    e->block->used++;
    p->freelist = e->next;
    p->count++;
    return e;
}

void *PoolCalloc(Pool *p)
{
    void *e = PoolAlloc(p);

    if (e)
        memset(e, 0, p->elemsize);
    return e;
}

/* Returns an element to the pool; frees its block when it becomes empty and freeEmpty is set
 * (after taking the block's elements off the free list). */
void PoolFree(Pool *p, void *elem)
{
    llist *bl;
    PoolBlock *b;
    PoolFreeElem *e = elem, *f, *g;

    for (bl = p->blocks.pnext; bl != POOL_TAIL(p); bl = bl->pnext) {
        b = (PoolBlock *)bl;
        if ((char *)e < (char *)b || (char *)e >= (char *)b + p->blocksize)
            continue;
        e->next = p->freelist;
        e->block = b;
        p->freelist = e;
        if (--b->used == 0 && p->freeEmpty) {
            bl->pprev->pnext = bl->pnext;
            bl->pnext->pprev = bl->pprev;
            f = p->freelist;
            while (f && f->block == b)
                f = f->next;
            p->freelist = f;
            while (f) {
                g = f->next;
                while (g && g->block == b)
                    g = g->next;
                f->next = g;
                f = g;
            }
            (*f_free)(b);
        }
        p->count--;
        return;
    }
}
