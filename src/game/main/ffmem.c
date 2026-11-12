/* ffmem.c -- Terra Nova "ff memory system" (module M099; source name unknown, the error
 * strings say ffFree).  Decompiled from __FF.EXE v1.09; names ours except ffFree.
 *
 *   0x2C4EE0 ffmem_stack_init     0x2C4F14 ffmem_stack_peak_update  0x2C4F40 ffmem_2C4F40
 *   0x2C4F90 ffmem_stack_realloc  0x2C4FE4 ffmem_2C4FE4     0x2C5038 ffmem_pages_init
 *   0x2C5084 ffmem_page_alloc     0x2C5130 ffmem_page_alloc_res 0x2C5150 ffmem_page_free
 *   0x2C5188 ffmem_out_of_memory  0x2C51A4 ffMalloc             0x2C5300 ffFree
 *   0x2C5414 ffRealloc            0x2C5468 ffmem_init           0x2C559C ffmem_page_purge
 *
 * ffmem_init installs ffMalloc / ffRealloc / ffFree as the LG allocator (MemPushAllocator).
 * ffMalloc serves, in this order:
 *  - small requests (<= 0xC00 bytes, not while a resource loads) from a pool of fixed blocks
 *    (ffmem_spec: 13 size classes {min, size, count}, carved from one Malloc);
 *  - resources 0x76C..0x95F and 0x3E8..0x47D (resLoadingId) from a page heap of 0x80 (0x20 in
 *    low-memory mode) 8 KB pages (first fit on a page map; when full, other resources of those
 *    ranges are dropped from the LRU list, ffmem_page_purge, then ResMalloc);
 *  - sizes >= 0x493E0 from a big static buffer (game_big_buf): whole (when game_35F3B8 is set)
 *    or as three 0x4B000-byte slots;
 *  - everything else with ResMalloc (which pages resources out when the heap is full).
 * A failure is reported with DbgReportError unless game_35F3B8 is set.  The ffmem_stack_*
 * functions are a separate stack allocator (0x4E000 bytes) with a high-water mark.
 * Quirks kept:
 *  - ffFree of a pool pointer that is not found clears the used flag of the entry after the
 *    last block (error only when 256 blocks exist); an unknown pointer inside the big buffer
 *    falls through to the other tests (and finally ResFree);
 *  - ffRealloc of a pool block copies `size` bytes (the new size) from the old block;
 *  - ffmem_init adds the block count to ffmem_nblocks without resetting it;
 *  - ffmem_stack_peak_update resets the peak to 0 whenever the stack is empty.
 */
#include "g1_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar ffmem_stack_on;
long ffmem_stack_peak;
FfPoolSpec ffmem_spec[13];
FfBlock ffmem_blocks[256];
long ffmem_big_used[3];
uchar *ffmem_pool_start, *ffmem_pool_end;
long ffmem_nblocks, ffmem_npages;
uchar *ffmem_pages_end, *ffmem_pages;
MemStack ffmem_stack;
void *ffmem_stack_buf;
uchar ffmem_page_map[0x81];
#endif

#define PAGE_SHIFT 13
#define PAGE_SIZE 0x2000
#define BIG_SLOT 0x4B000

void ffmem_stack_init(void)
{
    void *p = Malloc(0x4E000);
    ffmem_stack_buf = p;
    ffmem_stack.baseptr = p;
    ffmem_stack.sz = 0x4E000;
    MemStackInit(&ffmem_stack);
    ffmem_stack_on = 1;
}

long ffmem_stack_peak_update(void)
{
    long d = (uchar *)ffmem_stack.topptr - (uchar *)ffmem_stack.baseptr;
    if (d > ffmem_stack_peak)
        ffmem_stack_peak = d;
    if (d == 0)
        ffmem_stack_peak = 0;
    return d;
}

void *ffmem_2C4F40(size_t size)
{
    void *p;
    if (ffmem_stack_on)
        p = MemStackAlloc(&ffmem_stack, size);
    else
        p = Malloc(size);
    ffmem_stack_peak_update();
    return p;
}

void *ffmem_stack_realloc(void *p, size_t size)
{
    void *q;
    if (ffmem_stack_on)
        q = MemStackRealloc(&ffmem_stack, p, size);
    else
        q = Realloc(p, size);
    ffmem_stack_peak_update();
    return q;
}

bool ffmem_2C4FE4(void *p)
{
    bool r;
    if (!ffmem_stack_on) {
        Free(p);
        return TRUE;
    }
    r = MemStackFree(&ffmem_stack, p);
    ffmem_stack_peak_update();
    return r;
}

void ffmem_pages_init(void)
{
    ffmem_npages = game_low_mem ? 0x20 : 0x80;
    ffmem_pages = Malloc(ffmem_npages << PAGE_SHIFT);
    ffmem_pages_end = ffmem_pages + (ffmem_npages << PAGE_SHIFT);
    ffmem_page_map[ffmem_npages] = 0xFF;
}

/* first fit of size bytes in whole pages; NULL if there is no room */
void *ffmem_page_alloc(size_t size)
{
    long n = (ulong)(size + 0x1FFF) >> PAGE_SHIFT;
    long i = 0, k;

    if (size == 0)
        return NULL;
    for (;;) {
        if (ffmem_page_map[i] != 0) {
            if (i == ffmem_npages)
                return NULL;
            i++;
            continue;
        }
        for (k = 1; k < n && ffmem_page_map[i + k] == 0; k++)
            ;
        if (k == n)
            break;
        i += k;
    }
    ffmem_page_map[i] = (uchar)n;
    for (k = 1; k < n; k++)
        ffmem_page_map[i + k] = 0xFF;
    return ffmem_pages + (i << PAGE_SHIFT);
}

void *ffmem_page_alloc_res(size_t size)
{
    void *p = ffmem_page_alloc(size);
    if (p)
        return p;
    p = ffmem_page_purge(size);
    if (p)
        return p;
    return ResMalloc(size);
}

long ffmem_page_free(void *p)
{
    long i = ((uchar *)p - ffmem_pages) / PAGE_SIZE;
    ulong n = ffmem_page_map[i];
    while (n--)
        ffmem_page_map[i++] = 0;
    return i;
}

void ffmem_out_of_memory(size_t size)
{
    if (!game_35F3B8)
        DbgReportError(1, "Terra Nova has run out of memory trying to allocate %d bytes.\n", size);
}

void *ffMalloc(size_t size)
{
    long i;
    ushort id;
    void *p;

    if (resLoadingId == 0 && size <= 0xC00 && ffmem_nblocks > 0) {
        for (i = 0; i < ffmem_nblocks; i++) {
            FfBlock *b = &ffmem_blocks[i];
            if (!b->used && size >= b->min && size <= b->max) {
                b->used = 1;
                return b->ptr;
            }
        }
    }
    id = resLoadingId;
    if ((id >= 0x76C && id < 0x960) || (id >= 0x3E8 && id < 0x47E)) {
        p = ffmem_page_alloc_res(size);
        if (!p)
            ffmem_out_of_memory(size);
        return p;
    }
    if (size >= 0x493E0) {
        if (game_35F3B8 && size < 0xF2B34) {
            if (game_big_free) {
                game_big_free = 0;
                return game_big_buf;
            }
        } else if (size == BIG_SLOT && game_big_free) {
            for (i = 0; i < 3; i++)
                if (ffmem_big_used[i] == 0) {
                    ffmem_big_used[i] = 1;
                    return game_big_buf + i * BIG_SLOT;
                }
        }
    }
    p = ResMalloc(size);
    if (!p)
        ffmem_out_of_memory(size);
    return p;
}

void ffFree(void *p)
{
    uchar *q = p;
    long i;

    if (q >= game_big_buf && q < game_big_buf + 0xEF420) {
        if (q == game_big_buf && game_35F3B8) {
            game_big_free = 1;
            return;
        }
        for (i = 0; i < 3; i++)
            if (game_big_buf + i * BIG_SLOT == q) {
                ffmem_big_used[i] = 0;
                return;
            }
    }
    if (q >= ffmem_pages && q < ffmem_pages_end) {
        ffmem_page_free(q);
        return;
    }
    if (q >= ffmem_pool_start && q < ffmem_pool_end) {
        for (i = 0; i < ffmem_nblocks; i++)
            if (ffmem_blocks[i].ptr == q)
                break;
        if (ffmem_nblocks <= 0)
            i = 0;
        if (i == 0x100)
            DbgReportError(1, "Internal error--ffFree'd a ptr that wasn't ff managed.\n");
        ffmem_blocks[i].used = 0;
        return;
    }
    ResFree(p);
}

void *ffRealloc(void *p, size_t size)
{
    void *q;
    if ((uchar *)p >= ffmem_pool_start && (uchar *)p < ffmem_pool_end) {
        q = ffMalloc(size);
        if (q) {
            memcpy(q, p, size);
            ffFree(p);
        }
        return q;
    }
    return ResRealloc(p, size);
}

void ffmem_init(void)
{
    long s, j, b, total = 0;
    uchar *p;

    for (s = 0; s < 13; s++) {
        total += ffmem_spec[s].size * ffmem_spec[s].count;
        ffmem_nblocks += ffmem_spec[s].count;
    }
    p = Malloc(total);
    if (!p)
        DbgReportError(1, "No memory for ff memory system\n");
    ffmem_pool_start = p;
    ffmem_pool_end = p + total;
    b = 0;
    for (s = 0; s < 13; s++)
        for (j = 0; j < ffmem_spec[s].count; j++, b++) {
            ffmem_blocks[b].ptr = p;
            ffmem_blocks[b].max = ffmem_spec[s].size;
            ffmem_blocks[b].used = 0;
            p += ffmem_blocks[b].max;
            ffmem_blocks[b].min = ffmem_spec[s].min;
        }
    ffmem_pages_init();
    MemPushAllocator(WATCOM_FN(ffMalloc), WATCOM_FN(ffRealloc), WATCOM_FN(ffFree));
}

/* drop page-heap resources from the LRU list until size bytes fit */
void *ffmem_page_purge(size_t size)
{
    Id id = gResDesc[ID_HEAD].next, next;
    void *p;

    while (id != ID_TAIL) {
        next = gResDesc[id].next;
        if ((id >= 0x76C && id < 0x960) || (id >= 0x3E8 && id < 0x47E)) {
            ResDrop(id);
            p = ffmem_page_alloc(size);
            if (p)
                return p;
        }
        id = next;
    }
    return NULL;
}
