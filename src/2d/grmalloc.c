/* grmalloc.c -- x:\prj\tech\libsrc\2d\grmalloc.c (Terra Nova v1.09): the 2D library's
 * allocator hooks (gr_malloc_p / gr_free_p, defined in grd.c).  Decompiled from __FF.EXE.
 *
 *   0x2F1B70 gr_set_malloc     0x2F1B78 gr_set_free
 */
#include "2d.h"

void gr_set_malloc(void *(WATCOMCALL *malloc_func)(size_t n))
{
    gr_malloc_p = malloc_func;
}

void gr_set_free(void (WATCOMCALL *free_func)(void *p))
{
    gr_free_p = free_func;
}
