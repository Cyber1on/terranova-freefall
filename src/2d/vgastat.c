/* vgastat.c -- Terra Nova v1.09: VGA input status 1 (port 0x3DA) retrace bits, the stat_htrace /
 * stat_vtrace entries of the VGA and VESA device tables.  Decompiled from __FF.EXE (asm);
 * names as in 2d-core's csv.
 *
 *   0x34328C vga_stat_htrace   bit 0 (display disabled / horizontal retrace)
 *   0x343298 vga_stat_vtrace   bit 3 (vertical retrace)
 */
#include "2d_tmap.h"

int vga_stat_htrace(void)
{
    return tm_inb(0x3DA) & 1;
}

int vga_stat_vtrace(void)
{
    return (tm_inb(0x3DA) & 8) >> 3;
}
