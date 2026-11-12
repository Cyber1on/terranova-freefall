/* msgpool.c -- TN message pool (Terra Nova v1.09; names ours, no assert path).  Decompiled
 * from __FF.EXE.
 *
 *   0x2EC5B0 MsgPoolInit    0x2EC610 MsgPoolShutdown  0x2EC630 MsgAlloc     0x2EC65C MsgFree
 *   0x2EC678 MsgSend        0x2EC708 MsgReceive       0x2EC820 MsgFlush     0x2EC850 MsgDump
 *   0x2EC86C MsgFormat
 *
 * Fixed pool of 32-byte messages (mostly AI orders).  Entry 0 is not a message: its next field
 * heads the free list.  Sent messages are queued FIFO in one of 16 hash buckets chosen by the
 * destination address (a*7 + b*3) & 15.  MsgReceive is an iterator: it remembers the
 * destination it was last asked for and where it stopped in that bucket, so repeated calls with
 * the same destination walk the queue once.
 * Quirks kept: MsgSend to the iterator's destination only clears the low word (a) of the
 * remembered key, so with a == 0 the iterator is not reset; MsgPoolInit leaves the last
 * entry's next as Calloc's zero; MsgDump formats into a stack buffer and drops it.
 */
#include "t1_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
MsgHash msg_hash;
long msg_pool_count;
Msg *msg_pool;
static ulong msg_time_zero;
ulong *msg_time_ptr = &msg_time_zero;
void *msg_dump_func = WATCOM_FN(MsgFormat);
#endif

#define MSG_INDEX(m) ((ushort)(((long)((char *)(m) - (char *)msg_pool)) >> 5))
#define KEY(addr) (*(ulong *)&(addr))

/* Allocates the pool (once) and builds the free list. */
void MsgPoolInit(long count)
{
    long i;

    if (msg_pool != NULL)
        return;
    msg_pool_count = count;
    msg_pool = Calloc(count << 5);
    for (i = 0; i < msg_pool_count - 1; i++)
        msg_pool[i].next = (ushort)(i + 1);
    memset(&msg_hash, 0, sizeof(msg_hash));
    AtExit(WATCOM_FN(MsgPoolShutdown));
}

/* Frees the pool (AtExit). */
void MsgPoolShutdown(void)
{
    if (msg_pool != NULL) {
        (*f_free)(msg_pool);
        msg_pool = NULL;
    }
}

/* Takes a message off the free list; NULL when the pool is exhausted. */
Msg *MsgAlloc(void)
{
    ushort i = msg_pool[0].next;
    Msg *m;

    if (i == 0)
        return NULL;
    m = &msg_pool[i];
    msg_pool[0].next = m->next;
    m->flag = 0;
    return m;
}

/* Returns a message to the free list. */
void MsgFree(Msg *m)
{
    m->next = msg_pool[0].next;
    msg_pool[0].next = MSG_INDEX(m);
}

/* Stamps the message with the current time and queues it for its destination. */
void MsgSend(Msg *m)
{
    MsgBucket *b;
    ushort i;

    m->time = *msg_time_ptr;
    b = &msg_hash.bucket[MSG_HASH(m->to.a, m->to.b)];
    if (KEY(m->to) == msg_hash.key)
        *(ushort *)&msg_hash.key = 0;          /* only the low word (quirk) */
    i = MSG_INDEX(m);
    if (b->tail != 0)
        msg_pool[b->tail].next = i;
    else
        b->head = i;
    b->tail = i;
    m->next = 0;
}

/* Unlinks and returns the next queued message for 'to', or NULL. */
Msg *MsgReceive(MsgAddr *to)
{
    Msg *pool = msg_pool;
    Msg *m;

    if (KEY(*to) != msg_hash.key) {
        msg_hash.key = KEY(*to);
        msg_hash.cur_bucket = &msg_hash.bucket[MSG_HASH(to->a, to->b)];
        msg_hash.cur = msg_hash.cur_bucket->head;
        msg_hash.prev = 0;
    }
    while (msg_hash.cur != 0) {
        m = &pool[msg_hash.cur];
        if (KEY(m->to) == msg_hash.key) {
            if (msg_hash.prev != 0) {
                pool[msg_hash.prev].next = m->next;
                if (msg_hash.cur == msg_hash.cur_bucket->tail)
                    msg_hash.cur_bucket->tail = msg_hash.prev;
            } else {
                msg_hash.cur_bucket->head = m->next;
                if (m->next == 0)
                    msg_hash.cur_bucket->tail = 0;
            }
            msg_hash.cur = m->next;
            return m;
        }
        msg_hash.prev = msg_hash.cur;
        msg_hash.cur = m->next;
    }
    return NULL;
}

/* Discards every queued message for 'to'. */
void MsgFlush(MsgAddr *to)
{
    Msg *m;

    if (msg_pool == NULL)
        return;
    while ((m = MsgReceive(to)) != NULL)
        MsgFree(m);
}

/* Debug dump (unreferenced; the output is never printed). */
void MsgDump(Msg *m)
{
    char buf[0x50];

    if (msg_dump_func != NULL)
        WATCOM_CALL(msg_dump_func, (long)buf, (long)m);
}

/* Formats one message (default msg_dump_func). */
int MsgFormat(char *buf, Msg *m)
{
    return lg_sprintf(buf, (char *)"[%d] %d.%d -> %d.%d: [%x %x %x %x %x %x %x %x]\n",
                      (long)((char *)m - (char *)msg_pool) >> 5,
                      m->from.a, m->from.b, m->to.a, m->to.b,
                      m->data[0], m->data[1], m->data[2], m->data[3],
                      m->data[4], m->data[5], m->data[6], m->data[7]);
}
