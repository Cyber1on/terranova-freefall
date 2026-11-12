/* pathheap.c -- Terra Nova path planner priority queue (module M014, 0x25EB60-0x25EF0F).
 * Decompiled from __FF.EXE v1.09.
 *
 * A 4-ary min-heap of {short key, PathNode *node} entries (6 bytes, packed).  Every node keeps
 * its position in the heap in the low 10 bits of its +8 word (the other 6 bits are left alone),
 * so the planner can lower a node's key in place.
 *
 *   0x25EB60 pathheap_25EB60  create a heap of `max` entries (f_malloc; NULL on failure)
 *   0x25EBA0 pathheap_25EBA0  free a heap (unreferenced)
 *   0x25EBB8 pathheap_25EBB8  empty a heap
 *   0x25EBC0 pathheap_25EBC0  insert node with key; when full and `evict`, drop the last entry
 *                             if the new key is smaller; returns the position or -1
 *   0x25ECCC pathheap_25ECCC  lower the key of the entry at position i; returns the new position
 *   0x25EDAC pathheap_25EDAC  remove and return the node with the smallest key (NULL if empty)
 *
 * Quirks kept:
 *  - pathheap_25EB60 leaks the 8-byte header when the entry array cannot be allocated;
 *  - pathheap_25ECCC returns its position argument unchanged (all 32 bits) when the key is not
 *    lower or the entry does not move;
 *  - pathheap_25EDAC does not clear the heap position of the node it returns, and sifts the
 *    moved entry down comparing with its own key (not the key at the current position, which is
 *    the same entry);
 *  - parent positions are (i - 1) / 4 with C division (truncating towards 0).
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_c2.h"

#define SETPOS(n, i) ((n)->bits = (ushort)(((n)->bits & 0xFC00) | ((i) & 0x3FF)))

PathHeap *pathheap_25EB60(short max)
{
    PathHeap *h = (PathHeap *)(*f_malloc)(8);
    if (h == NULL)
        return NULL;
    h->max = max;
    h->count = 0;
    h->ents = (PathHeapEnt *)(*f_malloc)((long)max * 6);
    if (h->ents == NULL)
        return NULL;
    return h;
}

void pathheap_25EBA0(PathHeap *h)
{
    (*f_free)(h->ents);
    (*f_free)(h);
}

void pathheap_25EBB8(PathHeap *h)
{
    h->count = 0;
}

long pathheap_25EBC0(PathHeap *h, PathNode *node, short key, uchar evict)
{
    PathHeapEnt *e = h->ents;
    long i, p;

    if (h->count == h->max) {
        if (!evict || !(key < e[h->count - 1].key))
            return -1;
        h->count--;
        e[h->count].node->bits &= 0xFC00;
    }
    i = h->count++;
    p = (i - 1) / 4;
    while (i > 0 && key < e[p].key) {
        e[i] = e[p];
        SETPOS(e[i].node, i);
        i = p;
        p = (i - 1) / 4;
    }
    e[i].key = key;
    e[i].node = node;
    SETPOS(node, i);
    return i;
}

long pathheap_25ECCC(PathHeap *h, long pos, short key)
{
    PathHeapEnt *e = h->ents;
    short i = (short)pos;
    long p, ret = pos;
    PathNode *node;

    if (!(key < e[i].key))
        return pos;
    p = (i - 1) / 4;
    node = e[i].node;
    while ((short)ret > 0 && key < e[p].key) {
        long c = (short)ret;
        e[c] = e[p];
        SETPOS(e[c].node, ret);
        ret = p;
        p = ((short)p - 1) / 4;
    }
    i = (short)ret;
    e[i].key = key;
    e[i].node = node;
    SETPOS(node, ret);
    return ret;
}

PathNode *pathheap_25EDAC(PathHeap *h)
{
    PathHeapEnt *e, tmp;
    PathNode *top;
    long i, key;
    uchar more = 1;

    if (h->count == 0)
        return NULL;
    e = h->ents;
    top = e[0].node;
    h->count--;
    e[0] = e[h->count];
    if (h->count == 0)
        return top;
    tmp = e[0];
    key = e[0].key;
    i = 0;
    for (;;) {
        long first = i * 4 + 1, end, best, bkey, j;
        if (first >= h->count || !more)
            break;
        best = i;
        bkey = key;
        end = i * 4 + 5;
        if (end >= h->count)
            end = h->count;
        for (j = first; j < end; j++) {
            if (e[j].key < bkey) {
                bkey = e[j].key;
                best = j;
            }
        }
        if (best != i) {
            e[i] = e[best];
            SETPOS(e[i].node, i);
            i = best;
        } else
            more = 0;
    }
    e[i] = tmp;
    SETPOS(e[i].node, i);
    return top;
}
