/* lllist.c -- x:\prj\tech\libsrc\dstruct\lllist.c (Terra Nova v1.09): low-level doubly-linked
 * list routines.  Decompiled from __FF.EXE.
 *
 *   0x313EA0 llist_insert_queue   0x313ED0 llist_move_queue   0x313F50 llist_num_nodes
 *
 * As System Shock.  Queues keep higher priority first; a new node goes after the nodes of
 * equal priority.
 */
#include "sup_a.h"

#define QTAIL(plh) ((queue *)&(plh)->tail)

/* Inserts pq before the first node of lower priority. */
void llist_insert_queue(llist_head *plh, queue *pq)
{
    queue *p = (queue *)plh->head.pnext;

    while (p != QTAIL(plh) && pq->priority <= p->priority)
        p = p->pnext;
    pq->pprev = p->pprev;
    pq->pnext = p;
    p->pprev->pnext = pq;
    p->pprev = pq;
}

/* Re-sorts pq after its priority changed.  Returns TRUE if it moved. */
bool llist_move_queue(llist_head *plh, queue *pq)
{
    bool move = FALSE;

    if ((llist_head *)pq->pprev != plh && pq->priority > pq->pprev->priority)
        move = TRUE;
    else if (pq->pnext != QTAIL(plh) && pq->priority < pq->pnext->priority)
        move = TRUE;
    if (move) {
        pq->pprev->pnext = pq->pnext;
        pq->pnext->pprev = pq->pprev;
        llist_insert_queue(plh, pq);
    }
    return move;
}

int llist_num_nodes(llist_head *plh)
{
    llist *p;
    int n = 0;

    for (p = plh->head.pnext; p != &plh->tail; p = p->pnext)
        n++;
    return n;
}
