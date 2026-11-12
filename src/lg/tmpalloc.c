/* tmpalloc.c -- x:\prj\tech\libsrc\lg\tmpalloc.c (Terra Nova v1.09): temporary memory buffer
 * (a MemStack).  Decompiled from __FF.EXE.
 *
 *   0x309110 temp_mem_get_stack   0x309118 temp_mem_init   0x309164 temp_mem_uninit
 *   0x30918C temp_malloc          0x3091B4 temp_realloc    0x3091CC temp_free
 *
 * TN's buffer is 48 KB (Shock 16 KB), allocated with Malloc() but freed with the CRT free()
 * (quirk kept).
 */
#include "sup_a.h"

#define TEMP_BUF_SIZE 0xC000

#ifndef TN_LINK_ORIGINAL_DATA
MemStack *temp_mem_stack;
bool temp_mem_own;
#endif

void *temp_mem_get_stack(void)
{
    return temp_mem_stack;
}

/* Sets the memstack used for temporary memory; NULL allocates a dynamic one.
 * Returns 0, or -1 if the allocation failed. */
int temp_mem_init(void *stack)
{
    MemStack *ms = stack;

    if (ms == NULL) {
        ms = (*f_malloc)(sizeof(MemStack) + TEMP_BUF_SIZE);
        if (ms == NULL)
            return -1;
        temp_mem_own = TRUE;
        ms->baseptr = ms + 1;
        ms->sz = TEMP_BUF_SIZE;
        MemStackInit(ms);
    }
    temp_mem_stack = ms;
    return 0;
}

int temp_mem_uninit(void)
{
    if (temp_mem_own == TRUE) {
        free(temp_mem_stack);
        temp_mem_own = FALSE;
    }
    temp_mem_stack = NULL;
    return 0;
}

void *temp_malloc(size_t n)
{
    if (temp_mem_stack == NULL && temp_mem_init(NULL) != 0)
        return NULL;
    return MemStackAlloc(temp_mem_stack, n);
}

void *temp_realloc(void *p, size_t n)
{
    return MemStackRealloc(temp_mem_stack, p, n);
}

/* Returns 0, or 1 if p was freed out of order. */
int temp_free(void *p)
{
    return MemStackFree(temp_mem_stack, p) == FALSE;
}
