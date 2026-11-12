/* strpool.c -- small-string allocator (Terra Nova v1.09; names ours, no assert path).
 * Decompiled from __FF.EXE.
 *
 *   0x2F2F20 strpool_dup   0x2F2FF0 strpool_free
 *
 * Copies of strings up to 128 bytes (with the NUL) come from five DSTRUCT pools of 8, 16, 32,
 * 64 and 128-byte elements, longer ones from f_malloc.  The pool is chosen from the length
 * alone, so strpool_free needs the string unchanged.  Lengths are compared as signed 16-bit
 * values (quirk: a string of 32 KB or more would go to the 8-byte pool).  No NULL checks.
 */
#include "t1_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
short strpool_sizes[STRPOOL_N] = { 8, 16, 32, 64, 128 };
short strpool_counts[STRPOOL_N] = { 64, 32, 32, 16, 8 };
uchar strpool_need_init = 1;
Pool *strpool_ptrs[STRPOOL_N];
Pool strpool_pools[STRPOOL_N];
#endif

/* Returns a copy of s. */
char *strpool_dup(char *s)
{
    long i;
    short len;
    char *p;

    if (strpool_need_init) {
        for (i = 0; i < STRPOOL_N; i++) {
            strpool_ptrs[i] = &strpool_pools[i];
            PoolInit(&strpool_pools[i], strpool_sizes[i], strpool_counts[i], TRUE);
        }
        strpool_need_init = FALSE;
    }
    len = (short)(strlen(s) + 1);
    for (i = 0; i < STRPOOL_N; i++)
        if (len <= strpool_sizes[i])
            break;
    if (i < STRPOOL_N)
        p = PoolAlloc(strpool_ptrs[i]);
    else
        p = (*f_malloc)(len);
    memcpy(p, s, len);
    return p;
}

/* Frees a copy made by strpool_dup. */
void strpool_free(char *s)
{
    long i;
    short len = (short)(strlen(s) + 1);

    for (i = 0; i < STRPOOL_N; i++) {
        if (len <= strpool_sizes[i]) {
            PoolFree(strpool_ptrs[i], s);
            return;
        }
    }
    (*f_free)(s);
}
