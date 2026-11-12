/* hash.c -- n:/project/lib/src/dstruct/hash.c (Terra Nova v1.09): open-addressing hash tables.
 * Decompiled from __FF.EXE; identical to System Shock's HASH.C (rev 1.5).
 *
 *   0x3045D0 hashlog2        0x3045EC expmod          0x304644 is_fermat_prime
 *   0x3046A0 hash_init       0x30473C hash_copy       0x3047C0 find_elem (static; here hash_find_elem)
 *   0x304858 find_index (static; here hash_find_index)                    0x3048CC grow (hash_grow)
 *   0x3049B4 hash_set        0x304A44 hash_insert     0x304AB4 hash_delete
 *   0x304AE0 hash_lookup     0x304B14 hash_iter       0x304B54 hash_step
 *   0x304B94 hash_destroy
 *
 * The only client in the game is the config-file parser (0x2E1DF0-0x2E2EB0).
 * Original quirks kept:
 *  - hash_init/hash_copy/grow leak the first block when the second allocation fails
 *    (grow frees its new vector, hash_init/hash_copy don't).
 *  - hash_set/hash_insert ignore hash_grow()'s result and write at index -1 when the table is full
 *    (find_index returns INDEX_NOT_FOUND).
 *  - The probe step 1 << (hash % sizelog2) uses a negative remainder for negative hashes;
 *    shl only looks at the low 5 bits (written & 31 here).
 *  - hash_step reads statvec[*index] before checking *index < size.
 *  - The element copies are Watcom's inline memcpy (rep movsd, then rep movsb for n & 3), kept
 *    as movs_copy: a negative probe index (negative hash) can make the destination overlap the
 *    caller's element, and then dword-wise copying gives a different result than bytes.
 */
#include "sup_c.h"

#define HASH_EMPTY 0
#define HASH_TOMBSTONE 1
#define HASH_FULL 2
#define INDEX_NOT_FOUND -1
#define FULLNESS_THRESHHOLD_PERCENT 80
#define OK 0
#define ERR_NOEFFECT 6
#define ERR_NOMEM 7

#define ELEM(tbl, i) ((void *)((tbl)->vec + (i) * (tbl)->elemsize))

int hash_grow(Hashtable *h, int newsize);   /* static in the original; global so it can be tested */

/* rep movsd of n >> 2 dwords, rep movsb of n & 3 bytes, forward */
static void movs_copy(void *d, const void *s, int n)
{
    ulong *dw = d;
    const ulong *sw = s;
    uchar *db;
    const uchar *sb;
    uint i;
    for (i = (uint)n >> 2; i; i--)
        *dw++ = *sw++;
    db = (uchar *)dw;
    sb = (const uchar *)sw;
    for (i = n & 3; i; i--)
        *db++ = *sb++;
}

int hashlog2(int x)
{
    if (x < 2)
        return 0;
    return 1 + hashlog2(x / 2);
}

int expmod(int b, int e, uint m)
{
    if (e == 0)
        return 1;
    if (e % 2 == 0) {
        int tmp = expmod(b, e / 2, m);
        return (uint)(tmp * tmp) % m;
    } else {
        int tmp = expmod(b, e - 1, m);
        return (uint)(b * tmp) % m;
    }
}

bool is_fermat_prime(uint n, uint numtests)
{
    uint i;
    if (n < 3)
        return FALSE;
    for (i = 0; i < numtests; i++) {
        int a = (uint)rand() % (n - 2) + 2;
        if (expmod(a, n, n) != a)
            return FALSE;
    }
    return TRUE;
}

int hash_init(Hashtable *h, int elemsize, int vecsize, Hashfunc hfunc, Equfunc efunc)
{
    int i;
    while (!is_fermat_prime(vecsize, 2))
        vecsize++;
    h->elemsize = elemsize;
    h->size = vecsize;
    h->sizelog2 = hashlog2(vecsize);
    h->fullness = 0;
    h->hfunc = hfunc;
    h->efunc = efunc;
    h->statvec = Malloc(vecsize);
    if (h->statvec == NULL)
        return ERR_NOMEM;
    for (i = 0; i < vecsize; i++)
        h->statvec[i] = HASH_EMPTY;
    h->vec = Malloc(elemsize * vecsize);
    if (h->vec == NULL)
        return ERR_NOMEM;
    return OK;
}

int hash_copy(Hashtable *t, Hashtable *s)
{
    *t = *s;
    t->statvec = Malloc(t->size);
    if (t->statvec == NULL)
        return ERR_NOMEM;
    t->vec = Malloc(t->elemsize * t->size);
    if (t->vec == NULL)
        return ERR_NOMEM;
    movs_copy(t->vec, s->vec, t->size * t->elemsize);
    movs_copy(t->statvec, s->statvec, t->size);
    return OK;
}

bool hash_find_elem(Hashtable *h, void *elem, int *idx)
{
    bool found = FALSE;
    int hash = h->hfunc(elem);
    int index, j;
    for (j = 0, index = hash % h->size; j < h->size && h->statvec[index] != HASH_EMPTY;
         j++, index = (index + (1 << ((hash % h->sizelog2) & 31))) % h->size) {
        void *myelem = ELEM(h, index);
        if (h->statvec[index] == HASH_FULL && h->efunc(elem, myelem) == 0) {
            found = TRUE;
            break;
        }
    }
    *idx = index;
    return found;
}

int hash_find_index(Hashtable *h, void *elem)
{
    int hash = h->hfunc(elem);
    int j, index;
    for (j = 0, index = hash % h->size; j < h->size && h->statvec[index] == HASH_FULL;
         j++, index = (index + (1 << ((hash % h->sizelog2) & 31))) % h->size)
        ;
    if (j >= h->size)
        index = INDEX_NOT_FOUND;
    return index;
}

int hash_grow(Hashtable *h, int newsize)
{
    char *oldvec = h->vec;
    char *oldstat = h->statvec;
    char *newvec, *newstat;
    int oldsize = h->size;
    int i;
    for (; !is_fermat_prime(newsize, 2); newsize++)
        ;
    newvec = Malloc(newsize * h->elemsize);
    if (newvec == NULL)
        return ERR_NOMEM;
    newstat = Malloc(newsize);
    if (newstat == NULL) {
        Free(newvec);
        return ERR_NOMEM;
    }
    h->vec = newvec;
    h->statvec = newstat;
    h->size = newsize;
    h->sizelog2 = hashlog2(newsize);
    h->fullness = 0;
    for (i = 0; i < newsize; i++)
        newstat[i] = HASH_EMPTY;
    for (i = 0; i < oldsize; i++)
        if (oldstat[i] == HASH_FULL)
            hash_insert(h, oldvec + i * h->elemsize);
    Free(oldvec);
    Free(oldstat);
    return OK;
}

int hash_set(Hashtable *h, void *elem)
{
    int i;
    if (h->fullness * 100 / h->size > FULLNESS_THRESHHOLD_PERCENT)
        hash_grow(h, h->size * 2);
    if (!hash_find_elem(h, elem, &i))
        i = hash_find_index(h, elem);
    movs_copy(ELEM(h, i), elem, h->elemsize);
    h->statvec[i] = HASH_FULL;
    h->fullness++;
    return OK;
}

int hash_insert(Hashtable *h, void *elem)
{
    int i;
    if (h->fullness * 100 / h->size > FULLNESS_THRESHHOLD_PERCENT)
        hash_grow(h, h->size * 2);
    i = hash_find_index(h, elem);
    movs_copy(ELEM(h, i), elem, h->elemsize);
    h->statvec[i] = HASH_FULL;
    h->fullness++;
    return OK;
}

int hash_delete(Hashtable *h, void *elem)
{
    int i;
    if (hash_find_elem(h, elem, &i)) {
        h->statvec[i] = HASH_TOMBSTONE;
        return OK;
    }
    return ERR_NOEFFECT;
}

int hash_lookup(Hashtable *h, void *elem, void **result)
{
    int i;
    if (hash_find_elem(h, elem, &i))
        *result = ELEM(h, i);
    else
        *result = NULL;
    return OK;
}

int hash_iter(Hashtable *h, HashIterFunc ifunc, void *data)
{
    int i;
    for (i = 0; i < h->size; i++)
        if (h->statvec[i] == HASH_FULL)
            if (((bool (WATCOMCALL *)(void *, void *))ifunc)(ELEM(h, i), data))
                break;
    return OK;
}

int hash_step(Hashtable *h, void **result, int *index)
{
    while (h->statvec[*index] != HASH_FULL && *index < h->size)
        (*index)++;
    if (*index == h->size)
        *result = NULL;
    else
        *result = ELEM(h, *index);
    (*index)++;
    return OK;
}

int hash_destroy(Hashtable *h)
{
    h->size = 0;
    h->fullness = 0;
    Free(h->statvec);
    Free(h->vec);
    return OK;
}
