/* objring.c -- Terra Nova module M032 (0x279E80-0x27A0D0; source file name unknown): creation
 * of the class-8 "timer" object and a 5-entry ring of timed records.  Decompiled from __FF.EXE
 * v1.09; map names (timers_xxxxxx) kept.
 *
 *   0x279E80 timers_279E80 create a class-8 entity     0x279F94 timers_279F94 ring fill count
 *   0x279FAC timers_279FAC drop expired entries        0x27A014 timers_27A014 n-th entry
 *   0x27A048 timers_27A048 push an entry               0x27A0C0 timers_27A0C0 (empty)
 *
 * Ring: objring[5], tail = oldest entry, head = next free entry.  An entry expires 500 game time
 * units after it was pushed.
 * Quirks kept:
 *  - timers_27A048: when the ring overflows the tail is advanced with "tail < 5 ? tail + 1 : 0",
 *    so the tail can become 5 (one past the ring).
 *  - timers_279FAC walks with a 16-bit index from tail until it meets head; with head outside
 *    0..4 it never returns (the original loops too).  It writes game_time and head back unchanged.
 *  - timers_279E80 clears objring[0..4] but leaves head / tail alone.
 */
#include "g2_a.h"

/* class 8 pool record (12 bytes, pool at 0x39BB30) */
typedef struct {
    GamePoolRec h;      /* +0 */
    short w6;           /* +6 */
    uchar g8[2];
    uchar bA, bB;       /* +A, +B */
} TimerRec;

bool timers_279E80(short *ent, short *slot, uchar type)
{
    TimerRec *t;
    int i;
    if (!grid_295588(8, ent, slot))
        return 0;
    game_entities[*ent].type = type;
    game_entities[*ent].active = 1;
    game_entities[*ent].f23 = 0;
    t = (TimerRec *)game_pools[8].base + *slot;
    t->w6 = 0;
    t->bA = 0;
    t->bB = 1;
    for (i = 0; i < 5; i++)
        memset(&objring[i], 0, sizeof(ObjRingEntry));
    return 1;
}

long timers_279F94(void)
{
    long h = objring_head;
    if (h < objring_tail)
        h += 5;
    return h - objring_tail;
}

void timers_279FAC(void)
{
    long head = objring_head;
    ulong now = game_time;
    short i;
    if (objring_tail == head)
        return;
    i = (short)objring_tail;
    while (i != head) {
        if (now >= objring[i].time)
            objring_tail = i + 1;
        if (objring_tail >= 5)
            objring_tail = 0;
        if (++i >= 5)
            i = 0;
    }
    game_time = now;          /* written back unchanged */
    objring_head = head;
}

ObjRingEntry *timers_27A014(long n)
{
    long i = n + objring_tail;
    if (i - 1 < 5)
        i = i - 1;
    else
        i -= 6;
    if (i < 0 || i >= 5)
        return NULL;
    return &objring[i];
}

long timers_27A048(long a, long b, long c)
{
    long head = objring_head, tail = objring_tail;
    objring[head].a = a;
    objring[head].b = b;
    objring[head].c = c;
    objring[head].time = game_time + 500;
    objring[head].done = 0;
    if (++head >= 5)
        head = 0;
    if (head == tail) {
        if (tail >= 5)
            tail = 0;
        else
            tail++;
    }
    objring_tail = tail;
    objring_head = head;
    return 1;
}

void timers_27A0C0(void)
{
}
