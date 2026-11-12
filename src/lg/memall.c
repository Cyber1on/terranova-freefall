/* memall.c -- x:\prj\tech\libsrc\lg\memall.c (Terra Nova v1.09): installable allocator stack,
 * checked allocators and DOS conventional memory blocks.  Decompiled from __FF.EXE.
 *
 *   0x2DFC30 MemSetAllocator     0x2DFC70 MemPushAllocator    0x2DFCC8 MemPopAllocator
 *   0x2DFD14 Calloc (CallocNorm) 0x2DFD34 MemCheckOn          0x2DFD8C MemCheckOff
 *   0x2DFDCC MallocConvMemBlock  0x2DFE2C ReallocConvMemBlock 0x2DFEC0 FreeConvMemBlock
 *   0x2DFEF8 MallocChecked       0x2DFF28 ReallocChecked
 *
 * Differences from System Shock's memall.c:
 *  - MemCheckOn saves the current allocators into mem_check_saved and the checked versions
 *    call those ("MallocPrev"); it pushes inline (no push when the stack is full, but the
 *    hard flag is still set).  MemCheckOff pops inline.
 *  - The checked allocators only report when the hard flag is set (no warning otherwise).
 *  - The conventional-memory functions return a far pointer (edx:eax = ds:linear address) and
 *    also store the ds selector at ConvMemBlock+8.  Our C returns only the offset; the
 *    selector half is not reproduced in edx.
 */
#include "sup_a.h"

#define MAX_ALLOCATORS 4

#ifndef TN_LINK_ORIGINAL_DATA
MemAllocSet mem_alloc_stack[MAX_ALLOCATORS];
int mem_alloc_index;
MemAllocSet mem_check_saved;
bool mem_check_hard;
#endif

#define INT_DPMI 0x31

/* Sets the current allocator set (and the current stack slot). */
void MemSetAllocator(void *fm, void *fr, void *ff)
{
    MemAllocSet *pmas = &mem_alloc_stack[mem_alloc_index];

    f_free = ff;
    f_malloc = pmas->malloc_f = fm;
    pmas->realloc_f = fr;
    pmas->free_f = ff;
    f_realloc = fr;
}

/* Pushes the old allocators and installs new ones.  Returns 0, or -1 if the stack is full. */
int MemPushAllocator(void *fm, void *fr, void *ff)
{
    if (mem_alloc_index >= MAX_ALLOCATORS - 1)
        return -1;
    ++mem_alloc_index;
    MemSetAllocator(fm, fr, ff);
    return 0;
}

/* Pops the most recent allocators.  Returns 0, or -1 on underflow. */
int MemPopAllocator(void)
{
    MemAllocSet *pmas;

    if (mem_alloc_index <= 0)
        return -1;
    --mem_alloc_index;
    pmas = &mem_alloc_stack[mem_alloc_index];
    f_malloc = pmas->malloc_f;
    f_realloc = pmas->realloc_f;
    f_free = pmas->free_f;
    return 0;
}

/* Allocates with Malloc() and clears to 0 (Shock CallocNorm). */
void *Calloc(size_t size)
{
    void *p = (*f_malloc)(size);
    if (p)
        memset(p, 0, size);
    return p;
}

/* Turns on checked allocation.  hard = report an error when an allocation fails. */
void MemCheckOn(bool hard)
{
    mem_check_saved.malloc_f = f_malloc;
    mem_check_saved.realloc_f = f_realloc;
    mem_check_saved.free_f = f_free;
    if (mem_alloc_index < MAX_ALLOCATORS - 1) {
        ++mem_alloc_index;
        MemSetAllocator(WATCOM_FN(MallocChecked), WATCOM_FN(ReallocChecked), f_free);
    }
    mem_check_hard = hard;
}

/* Turns off checked allocation (pops the allocator stack). */
void MemCheckOff(void)
{
    MemAllocSet *pmas;

    if (mem_alloc_index > 0) {
        --mem_alloc_index;
        pmas = &mem_alloc_stack[mem_alloc_index];
        f_malloc = pmas->malloc_f;
        f_realloc = pmas->realloc_f;
        f_free = pmas->free_f;
    }
}

static inline ushort get_ds(void)
{
    ushort ds;
    __asm__ volatile("movw %%ds, %0" : "=r"(ds));
    return ds;
}

/* Allocates conventional (below 1 MB) memory with DPMI 0x100.
 *     size = size in bytes;  pcmb = filled in on success
 * Returns the linear address of the block, or NULL. */
void *MallocConvMemBlock(ushort size, ConvMemBlock *pcmb)
{
    union REGS regs;

    regs.x.eax = 0x100;
    regs.x.ebx = (size + 15) >> 4;
    int386(INT_DPMI, &regs, &regs);
    if (regs.x.cflag)
        return NULL;
    pcmb->realSeg = regs.w.ax;
    pcmb->selector = regs.w.dx;
    pcmb->pad = get_ds();
    pcmb->ptr = (void *)((ulong)pcmb->realSeg << 4);
    return pcmb->ptr;
}

/* Resizes a conventional block (DPMI 0x102), then rereads its base (DPMI 6).
 * Returns the linear address, or NULL. */
void *ReallocConvMemBlock(ConvMemBlock *pcmb, ushort newsize)
{
    union REGS regs;
    long realAddr;

    regs.x.eax = 0x102;
    regs.w.bx = (newsize + 15) >> 4;
    regs.w.dx = pcmb->selector;
    int386(INT_DPMI, &regs, &regs);
    if (regs.x.cflag)
        return NULL;
    regs.x.eax = 6;
    regs.w.bx = pcmb->selector;
    int386(INT_DPMI, &regs, &regs);
    realAddr = ((long)regs.w.cx << 16) + regs.w.dx;
    pcmb->realSeg = realAddr >> 4;
    pcmb->pad = get_ds();
    pcmb->ptr = (void *)((ulong)pcmb->realSeg << 4);
    return pcmb->ptr;
}

/* Frees a conventional block (DPMI 0x101).  Returns 0, or -1 if the free failed. */
int FreeConvMemBlock(ConvMemBlock *pcmb)
{
    union REGS regs;

    regs.x.eax = 0x101;
    regs.w.dx = pcmb->selector;
    int386(INT_DPMI, &regs, &regs);
    return regs.x.cflag ? -1 : 0;
}

/* Calls the allocator saved by MemCheckOn; reports an error on NULL when checking is hard. */
void *MallocChecked(size_t size)
{
    void *p = (*mem_check_saved.malloc_f)(size);
    if (p == NULL && mem_check_hard)
        DbgReportError(1, "MallocChecked: out of memory allocating %d bytes\n", size);
    return p;
}

void *ReallocChecked(void *p, size_t size)
{
    void *pnew = (*mem_check_saved.realloc_f)(p, size);
    if (pnew == NULL && mem_check_hard)
        DbgReportError(1, "ReallocChecked: out of memory reallocing %d bytes\n", size);
    return pnew;
}
