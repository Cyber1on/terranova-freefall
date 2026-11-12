/* kbflush.c -- keyboard flush (Terra Nova v1.09, 0x2FCF0C, between Watcom CRT objects; name ours).
 * Decompiled from __FF.EXE.
 *
 *   0x2FCF0C kb_flush
 *
 * Drops every pending key event: the read index of the INPUT library's keyboard ring
 * (kb_read_index) catches up with the ISR's write index (low word of kbd_lowmem_start->head).
 * Called by the shell/mission code before waiting for a key (0x2C8578, 0x2DC57B, 0x2DD81B, 0x2DD8BA).
 */
#include "t1_b.h"

void kb_flush(void)
{
    kb_read_index = *(ushort *)&kbd_lowmem_start->head;
}
