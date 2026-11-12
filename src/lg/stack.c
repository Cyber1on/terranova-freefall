/* stack.c -- x:\prj\tech\libsrc\lg\stack.c (Terra Nova v1.09): memory stacks.
 * Decompiled from __FF.EXE.
 *
 *   0x2FB1C0 MemStackInit   0x2FB1C8 MemStackAlloc   0x2FB1EC MemStackRealloc   0x2FB208 MemStackFree
 *
 * As System Shock, without the warnings.  Pointer comparisons are unsigned.
 */
#include "sup_a.h"

void MemStackInit(MemStack *ms)
{
    ms->topptr = ms->baseptr;
}

void *MemStackAlloc(MemStack *ms, long size)
{
    char *oldptr = ms->topptr;
    char *newptr = oldptr + size;

    if (newptr > (char *)ms->baseptr + ms->sz)
        return NULL;
    ms->topptr = newptr;
    return oldptr;
}

void *MemStackRealloc(MemStack *ms, void *ptr, long newsize)
{
    char *newptr = (char *)ptr + newsize;

    if (newptr > (char *)ms->baseptr + ms->sz)
        return NULL;
    ms->topptr = newptr;
    return ptr;
}

bool MemStackFree(MemStack *ms, void *ptr)
{
    if ((char *)ptr > (char *)ms->topptr)
        return FALSE;
    ms->topptr = ptr;
    return TRUE;
}
