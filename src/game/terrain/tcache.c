/* tcache.c -- Terra Nova modules M089 (0x2AD9F0-0x2ADEB0) and M090 (0x2ADEB0-0x2ADF00; source
 * file names unknown): the terrain cache memory stack (a 0x4240-byte MemStack), the strip
 * pattern tables read from resource 0x55F, and circle outlines built on that stack.
 * Decompiled from __FF.EXE v1.09; map names kept (memstack_xxxxxx, ralloc_xxxxxx).
 *
 *   0x2AD9F0 tcache_dir9 (data, 9 bytes)  0x2AD9FC tcache_2AD9FC use a given buffer
 *   0x2ADA2C memstack_2ADA2C init         0x2ADB80 memstack_2ADB80 free
 *   0x2ADB8C memstack_2ADB8C pop records  0x2ADBD8 memstack_2ADBD8 circle step choice
 *   0x2ADC84 memstack_2ADC84 circle step  0x2ADD14 memstack_2ADD14 circle outline
 *   0x2ADEB0 ralloc_2ADEB0 alloc record   0x2ADEEC ralloc_2ADEEC free record
 *
 * Resource 0x55F holds 0x31 patterns; pattern i (1..0x31) is a ushort count, count bytes and
 * i byte pairs.  tcache_recs[i] = {i, count, the count bytes}; tcache_tab[i] = the pairs, the
 * second byte of each pair made cumulative at load.  The record list of game_438C20 (+0x24
 * first, +0x28 end) continues after record 49 (index 50 on) with circle outlines made by
 * memstack_2ADD14: one byte per two steps (direction codes from tcache_dir9 in bits 0-2 and
 * 4-6; bit 3 marks the first byte, bit 7 the last).
 * Notes:
 *  - The map lists 0x2AD9F0 as a function: it is the 9-byte direction table; the function
 *    is 0x2AD9FC (unreferenced).  memstack_2ADB8C and memstack_2ADD14 (with their helpers
 *    2ADBD8 / 2ADC84) are unreferenced too.
 *  - memstack_2ADB8C sums the record sizes in a register whose high half holds pointer bits;
 *    only the low 16 bits are used (as here).
 */
#include "g5_a.h"

void tcache_2AD9FC(void *buf)
{
    tcache_ms.baseptr = buf;
    tcache_ms.sz = 0x4240;
    MemStackInit(&tcache_ms);
    memset(tcache_ms.baseptr, 0, 0x4240);
}

void memstack_2ADA2C(void)
{
    uchar **tab = tcache_tab;
    uchar *res, *p;
    ushort i, j;
    tcache_ms.baseptr = Malloc(0x4240);
    tcache_ms.sz = 0x4240;
    MemStackInit(&tcache_ms);
    memset(tcache_ms.baseptr, 0, 0x4240);
    tab = (uchar **)MemStackAlloc(&tcache_ms, 0xC4) - 1;
    tcache_tab = tab;
    tcache_recs = (TcRec *)MemStackAlloc(&tcache_ms, 0x640) - 1;
    res = MemStackAlloc(&tcache_ms, ResSize(0x55F));
    ResExtract(0x55F, res);
    p = res;
    for (i = 1; i <= 0x31; i++) {
        ushort cnt = *(ushort *)p;
        TcRec *r = &tcache_recs[i];
        p += 2;
        r->id = i;
        r->data = p;
        r->size = cnt;
        p += cnt + i * 2;
    }
    p = res;
    for (i = 1; i <= 0x31; i++) {
        p += *(ushort *)p + 2;
        tab[i] = p;
        for (j = 0; j < i; j++)
            tab[i][j * 2 + 1] += tab[i][j * 2];
        p += i * 2;
    }
    tcache_tab = tab;
}

void memstack_2ADB80(void)
{
    Free(tcache_ms.baseptr);
}

void memstack_2ADB8C(void *obj)
{
    TcRec *r = (TcRec *)(((TcList *)game_438C20)->end) - 1;
    TcRec *stop = *(TcRec **)((uchar *)obj + 0x44);
    ushort total = 0;
    while (r >= stop) {
        total += r->size;
        MemStackFree(&tcache_ms, r->data);
        r--;
    }
    stop = *(TcRec **)((uchar *)obj + 0x44);
    memset(stop->data, 0, total);
}

long memstack_2ADBD8(long err, short x, short y, short *sx, short *sy)
{
    long a = err + 2 * *sx * x;
    long b = err + 2 * *sy * y;
    long c1 = a + 1, c2 = b + 1, c3 = 2 * *sy * y + err + 2 * *sx * x + 2;
    long best, r = c3;
    long abs1 = c1 < 0 ? -c1 : c1, abs2 = c2 < 0 ? -c2 : c2, abs3 = c3 < 0 ? -c3 : c3;
    if (abs1 < abs2)
        best = abs1 < abs3 ? c1 : c3;
    else
        best = abs2 < abs3 ? c2 : c3;
    if (best == c1) {
        *sy = 0;
        r = c1;
    } else if (best == c2) {
        r = c2;
        *sx = 0;
    }
    return r;
}

long memstack_2ADC84(long err, short *px, short *py)
{
    short sx, sy;
    long r;
    if (*px >= 0) {
        if (*py >= 0) { sx = -1; sy = 1; }
        else { sx = 1; sy = 1; }
    } else {
        if (*py >= 0) { sx = -1; sy = -1; }
        else { sx = 1; sy = -1; }
    }
    r = memstack_2ADBD8(err, *px, *py, &sx, &sy);
    *px += sx;
    *py += sy;
    return r;
}

void *memstack_2ADD14(long rad)
{
    uchar dir9[9];
    TcList *l = game_438C20;
    TcRec *r;
    uchar *p;
    short x, y, px, py;
    long err;
    memcpy(dir9, tcache_dir9, 9);
    for (r = l->first; r < l->end; r++)
        if ((short)rad == r->id) {
            if (r->data != NULL)
                return r;
            break;
        }
    p = MemStackAlloc(&tcache_ms, (ushort)rad * 4);
    r->data = p;
    x = 0;
    y = -(short)rad;
    px = 0;
    *p |= 8;
    err = 0;
    do {
        py = y;
        err = memstack_2ADC84(err, &x, &y);
        *p = (*p & 0xF8) | (dir9[(x - px + 1) * 3 + (y - py) + 1] & 7);
        px = x;
        py = y;
        err = memstack_2ADC84(err, &x, &y);
        *p = (*p & 0x8F) | ((dir9[(x - px + 1) * 3 + (y - py) + 1] & 7) << 4);
        p++;
        px = x;
    } while (x != 0 || y != -(long)(ushort)rad);
    p[-1] |= 0x80;
    r->id = (short)rad;
    r->size = (ushort)((ulong)p - (ulong)r->data);
    r->data = MemStackRealloc(&tcache_ms, r->data, r->size);
    if (r == l->end)
        l->end = r + 1;
    return r;
}

void ralloc_2ADEB0(void)
{
    game_438C20 = Malloc(0x30);
    memset(game_438C20, 0, 0x30);
    ((TcList *)game_438C20)->first = &tcache_recs[50];
    ((TcList *)game_438C20)->end = &tcache_recs[50];
}

void ralloc_2ADEEC(void)
{
    Free(game_438C20);
}
