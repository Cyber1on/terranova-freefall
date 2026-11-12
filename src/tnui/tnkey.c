/* tnkey.c -- Terra Nova's key handler layer ("tnui", no Shock counterpart: TN does not link
 * the LG UI library; names ours except KeyAddHandler/KeyRemoveHandler, from the error string).
 * Decompiled from __FF.EXE.
 *
 *   0x2E3690 tnkey_init          0x2E3730 tnkey_shutdown      0x2E3748 tnkey_poll
 *   0x2E38AC tnkey_wait          0x2E38D8 KeyAddHandler       0x2E3900 KeyRemoveHandler
 *   0x2E3950 tnkey_dispatch_code 0x2E3984 tnkey_dispatch      0x2E39C4 tnkey_null_handler
 *   0x2E39D0 tnkey_set_hook
 *
 * Handlers live in an LLIST (tnkey_handlers, 0x11-byte nodes).  KeyRemoveHandler only swaps
 * the handler for tnkey_null_handler; tnkey_poll frees such nodes after dispatching, so a
 * handler may remove itself.  Key-down events (cooked code with KB_FLAG_DOWN, flipped to bit 8
 * clear) go to every handler, key-up events only to handlers added with all = TRUE.
 * tnkey_poll is the game's per-frame task 8 (tn_task_func[8], enable bit 0x100).
 * The tn_replay_* game hooks feed recorded keys / record keys (record-replay of input).
 */
#include "sup_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
LlistHead tnkey_handlers;
ushort tnkey_removed;
void *tnkey_hook;
#endif

static void dispatch(short code)
{
    tnkey_event ev;
    ev.type = (code & 0x100) ? TNKEY_UP : TNKEY_DOWN;
    ev.code = code;
    ev.pad = 0;
#ifndef TN_WATCOM_ABI
    /* G28: the base screens' handlers take a key they do not know for a click at the event's
     * point (+8: the same place as a mouse event's), which the original never set: the stack's
     * leftovers, so now and then a key pressed a button somewhere on the screen (and read past
     * the port's smaller event).  Off the screen: such a key does nothing */
    ev.pt.x = ev.pt.y = -1;
#endif
    tnkey_dispatch(&ev);
}

void tnkey_init(void)
{
    int i;
    kb_startup(tnkey_init_buf);
    for (i = 0; i < 256; i++)
        kb_set_state(i, KBA_REPEAT);
    kb_set_state(0x54, KBA_SIGNAL);                     /* SysRq */
    memset(tn_replay_state, 0, sizeof(tn_replay_state));
    tn_task_func[5] = WATCOM_FN(tnui_replay_task);
    tn_replay_state_ptr = tn_replay_state;
    tn_task_func[8] = WATCOM_FN(tnkey_poll);
    *(ushort *)&tn_task_mask |= TN_TASK_REPLAY | TN_TASK_KEY;
    AtExit(WATCOM_FN(tnkey_shutdown));
    LlistInit(&tnkey_handlers, sizeof(tnkey_node), 8);
}

void tnkey_shutdown(void)
{
    kb_shutdown();
    ((uchar *)&tn_task_mask)[1] &= ~(TN_TASK_KEY >> 8);
    LlistDestroy(&tnkey_handlers);
}

void tnkey_poll(void)
{
    ulong cooked;               /* kb_cook writes the low word */
    bool results;
    short *keys;
    int n, i;

    kb_flush_bios();
    if (tn_replay_playing) {
        game_replay_get_keys(&keys, &n);
        for (i = 0; i < n; i++)
            if (game_replay_key_ok(keys[i] ^ 0x100))
                dispatch(keys[i] ^ 0x100);
    }
    while ((uchar)kb_look_next() != KBC_NONE) {
        ushort k = kb_next();
        kbs_event ev;
        memcpy(&ev, &k, sizeof(ev));
        kb_cook(ev, (ushort *)&cooked, &results, 0);
        if (!results)
            continue;
        if (tn_replay_playing && !game_replay_filter_key((short)(cooked ^ 0x100)))
            continue;
        if (tn_replay_recording)
            game_replay_record_key((short)cooked);
        dispatch((short)(cooked ^ 0x100));
    }
    if (tnkey_removed) {
        llist *p, *next;
        for (p = tnkey_handlers.head.pnext; p != &tnkey_handlers.tail; p = next) {
            next = p->pnext;
            if (((tnkey_node *)p)->handler == WATCOM_FN(tnkey_null_handler))
                LlistFree(&tnkey_handlers, p);
        }
        tnkey_removed = 0;
    }
}

/* Waits for a key press; returns the cooked code with the down flag cleared.  Unreferenced. */
uint tnkey_wait(void)
{
    ulong k;
    kb_flush_bios();
    do {
        while (!kb_get_cooked((ushort *)&k))
            ;
    } while (!(k & 0x100));
    return (ushort)(k ^ 0x100);
}

void KeyAddHandler(void *handler, void *data, bool all, bool tail)
{
    tnkey_node *n = tail ? LlistAddTail(&tnkey_handlers) : LlistAddHead(&tnkey_handlers);
    n->data = data;
    n->all = all;
    n->handler = handler;
}

void KeyRemoveHandler(void *handler, void *data)
{
    llist *p;
    for (p = tnkey_handlers.head.pnext; p != &tnkey_handlers.tail; p = p->pnext) {
        tnkey_node *n = (tnkey_node *)p;
        if (n->handler == handler && n->data == data) {
            n->handler = WATCOM_FN(tnkey_null_handler);
            tnkey_removed++;
            return;
        }
    }
    DbgReportError(1, "KeyRemoveHandler: Can't find key handler routine $%x with id $%x\n", handler, data);
}

void tnkey_dispatch_code(short code)
{
    dispatch(code);
}

void tnkey_dispatch(tnkey_event *e)
{
    bool all = e->type == TNKEY_DOWN;
    llist *p;
    for (p = tnkey_handlers.head.pnext; p != &tnkey_handlers.tail; p = p->pnext) {
        tnkey_node *n = (tnkey_node *)p;
        if ((all || n->all) && n->handler(e, n))
            break;
    }
}

bool tnkey_null_handler(void)
{
    return FALSE;
}

void tnkey_set_hook(void *f)
{
    tnkey_hook = f;
}
