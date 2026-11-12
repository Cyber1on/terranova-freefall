/* llist.c -- x:\prj\tech\libsrc\dstruct\llist.c (Terra Nova v1.09): linked lists with node
 * blocks.  Decompiled from __FF.EXE.
 *
 *   0x2F2C60 LlistInit      0x2F2CAC LlistAddHead    0x2F2D00 LlistAddTail   0x2F2D58 LlistAddQueue
 *   0x2F2DB0 LlistMoveQueue 0x2F2DBC LlistFree       0x2F2DD8 LlistFreeAll   0x2F2E50 LlistDestroy
 *   0x2F2E7C LlistGrowList  0x2F2EDC LlistInitNodeBlock
 *
 * As System Shock.  The list has two sentinels: head (+0, its pnext is the first node) and
 * tail (+8, its pprev is the last node); only those two words are ever written.  Nodes come
 * from blocks of numNodesPerBlock nodes (block = link word + 4 unused bytes + nodes); the
 * block size is computed in 16 bits.  Allocations are not checked.
 * Quirk: LlistFreeAll rebuilds the free list of every block in turn, so only the last block's
 * nodes end up on the free list (the others are lost until LlistDestroy).
 */
#include "sup_a.h"

#define NODE(p) ((llist *)(p))

static inline ushort block_size(LlistHead *plh)
{
    return (ushort)(plh->nodeSize * plh->numNodesPerBlock);
}

/* Threads the nodes of a block into the free list (pfree = first node, last node -> NULL). */
void LlistInitNodeBlock(LlistHead *plh, llist *pNodeBlock, ushort blockSize)
{
    char *p = (char *)pNodeBlock + 8;

    plh->pfree = (llist *)p;
    while ((ulong)p < (ulong)((char *)plh->pfree + ((int)blockSize - (int)plh->nodeSize))) {
        NODE(p)->pnext = (llist *)(p + plh->nodeSize);
        p += plh->nodeSize;
    }
    NODE(p)->pnext = NULL;
}

static void llist_new_block(LlistHead *plh)
{
    ushort size = block_size(plh);
    llist *pblock = (*f_malloc)(size + 8);

    pblock->pnext = plh->pNodeStore;
    plh->pNodeStore = pblock;
    LlistInitNodeBlock(plh, pblock, size);
}

void LlistInit(LlistHead *plh, ushort nodeSize, short numNodesPerBlock)
{
    plh->nodeSize = nodeSize;
    plh->numNodesPerBlock = numNodesPerBlock;
    plh->head.pnext = &plh->tail;
    plh->pNodeStore = NULL;
    plh->tail.pprev = &plh->head;
    llist_new_block(plh);
}

static inline llist *llist_get_node(LlistHead *plh)
{
    llist *pnode;

    if (plh->pfree == NULL)
        llist_new_block(plh);
    pnode = plh->pfree;
    plh->pfree = pnode->pnext;
    return pnode;
}

void *LlistAddHead(LlistHead *plh)
{
    llist *pnode = llist_get_node(plh);

    pnode->pnext = plh->head.pnext;
    pnode->pprev = &plh->head;
    plh->head.pnext->pprev = pnode;
    plh->head.pnext = pnode;
    return pnode;
}

void *LlistAddTail(LlistHead *plh)
{
    llist *pnode = llist_get_node(plh);

    pnode->pnext = &plh->tail;
    pnode->pprev = plh->tail.pprev;
    plh->tail.pprev->pnext = pnode;
    plh->tail.pprev = pnode;
    return pnode;
}

/* Adds a node in priority order (queue nodes: priority at +8, higher first). */
void *LlistAddQueue(LlistHead *plh, short prior)
{
    queue *pq = (queue *)llist_get_node(plh);

    pq->priority = prior;
    llist_insert_queue((llist_head *)plh, pq);
    return pq;
}

bool LlistMoveQueue(LlistHead *plh, void *pnode, short newprior)
{
    ((queue *)pnode)->priority = newprior;
    return llist_move_queue((llist_head *)plh, pnode);
}

/* Unlinks a node and puts it on the free list. */
void LlistFree(LlistHead *plh, void *pnode)
{
    llist *p = pnode;

    p->pprev->pnext = p->pnext;
    p->pnext->pprev = p->pprev;
    p->pnext = plh->pfree;
    plh->pfree = p;
}

/* Empties the list (see the quirk above). */
void LlistFreeAll(LlistHead *plh)
{
    ushort size;
    llist *pblock;

    plh->tail.pprev = &plh->head;
    plh->head.pnext = &plh->tail;
    size = block_size(plh);
    for (pblock = plh->pNodeStore; pblock; pblock = pblock->pnext)
        LlistInitNodeBlock(plh, pblock, size);
}

/* Frees all node blocks and clears the header. */
void LlistDestroy(LlistHead *plh)
{
    llist *pblock = plh->pNodeStore, *next;

    while (pblock) {
        next = pblock->pnext;
        (*f_free)(pblock);
        pblock = next;
    }
    memset(plh, 0, sizeof(*plh));
}

void LlistGrowList(LlistHead *plh)
{
    llist_new_block(plh);
}
