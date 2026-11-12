/* memprobe.c -- free memory probes (M100, unreferenced code; names are the map's), Terra Nova
 * v1.09.  Decompiled from __FF.EXE.
 *
 *   0x2C5600 dead_2C5600   total of a descending series of malloc() probes (freed again)
 *   0x2C567C dead_2C567C   DPMI 0x500 free memory info, page counts scaled to bytes
 *   0x2C5700 dead_2C5700   DOS IOCTL 0x440D lock (0x4A) / unlock (0x6A) logical volume
 *   0x2C5720 dead_2C5720   dead_2C567C on mem_info, smaller of the first two fields
 *   0x2C57CC dead_2C57CC   same, falling back to dead_2C5600 (cached in mem_free_estimate)
 *   0x2C5804 dead_2C5804   f_malloc probe for `want` bytes in 8 MB / 2 MB / ... blocks
 *
 * The DPMI and DOS calls are inline asm with the registers the original sets.
 * Quirks kept: dead_2C5720 reads mem_info even when DPMI reports no page size; dead_2C5600 keeps
 * probing with the same size after a success (so it allocates up to 25 blocks of the largest
 * size that fits).
 */
#include "g1_c.h"

/* DPMI 0x604: page size (BX:CX); the original keeps only CX. */
static ulong dpmi_page_size(void)
{
    ulong cx;

    __asm__ volatile("xor %%cx, %%cx\n\tmov $0x604, %%ax\n\tint $0x31"
                     : "=c"(cx) : : "eax", "ebx", "edx", "memory", "cc");
    return cx & 0xFFFF;
}

/* DPMI 0x500: free memory information into es:edi. */
static void dpmi_free_info(ulong *info)
{
    __asm__ volatile("push %%es\n\tpush %%ds\n\tpop %%es\n\tmov $0x500, %%ax\n\tint $0x31\n\tpop %%es"
                     : "+D"(info) : : "eax", "ebx", "ecx", "edx", "memory", "cc");
}

ulong dead_2C5600(void)
{
    void *p[25];
    int n = 0, i;
    long size = 0x7A6020;
    ulong total = 0;

    lg_memset32((ulong *)p, 0, 0, 25);
    do {
        if (n >= 25)
            break;
        p[n] = malloc(size);
        if (!p[n])
            size -= 20000;
        else {
            n++;
            total += size;
        }
    } while (size > 100000);
    for (i = n - 1; i >= 0; i--)
        if (p[i])
            free(p[i]);
    return total;
}

ulong dead_2C567C(ulong *info)
{
    ulong ps = dpmi_page_size();

    if (ps == 0)
        return (ulong)-1;
    dpmi_free_info(info);
    info[3] *= ps;
    info[7] *= ps;
    info[6] *= ps;
    info[5] *= ps;
    info[8] *= ps;
    info[1] *= ps;
    info[2] *= ps;
    info[4] *= ps;
    return info[0];
}

long dead_2C5700(long drive)
{
    __asm__ volatile("mov %%al, %%bl\n\tmov $1, %%bh\n\tmov $0x440D, %%ax\n\tmov $8, %%ch\n\t"
                     "mov $0x4A, %%cl\n\tmov $3, %%dx\n\tint $0x21\n\tmov $0x6A, %%cl\n\tint $0x21"
                     : "+a"(drive) : : "ebx", "ecx", "edx", "memory", "cc");
    return drive;
}

ulong dead_2C5720(void)
{
    ulong ps = dpmi_page_size();

    if (ps != 0) {
        dpmi_free_info(mem_info);
        mem_info[3] *= ps;
        mem_info[7] *= ps;
        mem_info[6] *= ps;
        mem_info[5] *= ps;
        mem_info[8] *= ps;
        mem_info[1] *= ps;
        mem_info[2] *= ps;
        mem_info[4] *= ps;
    }
    return mem_info[0] < mem_info[1] ? mem_info[0] : mem_info[1];
}

ulong dead_2C57CC(void)
{
    ulong m;

    dead_2C567C(mem_info);
    m = mem_info[0] < mem_info[1] ? mem_info[0] : mem_info[1];
    if (m != 0)
        return m;
    return mem_free_estimate = dead_2C5600();
}

ulong dead_2C5804(long want)
{
    void *p[32];
    long block = 0x800000, total = 0;
    int n = 0;

    if (mem_free_estimate)
        return mem_free_estimate;
    while (total < want && block >= 0x8000) {
        while (want - total > block && n < 32) {
            p[n] = (*f_malloc)(block);
            if (!p[n])
                break;
            n++;
            total += block;
        }
        block >>= 2;
    }
    if (total < want && block > want - total && n < 32) {
        p[n] = (*f_malloc)(block);
        if (p[n]) {
            n++;
            total += block;
        }
    }
    while (--n >= 0)
        (*f_free)(p[n]);
    return total;
}
