/* dos.c -- LG DOS handlers (no System Shock counterpart; names ours).  Decompiled from __FF.EXE.
 *
 *   0x2DFF60 lg_ctrlbreak_install   0x2DFFE4 lg_ctrlbreak_restore
 *   0x2E0024 lg_divtrap_install     0x2E0084 lg_divtrap_restore
 *
 * The ctrl-break handler replaces protected-mode vector 0x23 (DPMI 0x204/0x205) with the asm
 * stub at 0x30404C, which only counts the breaks in lg_ctrlbreak_count.  The divide trap
 * patches IDT gate 0 directly (linear IDT base from sidt) to point at the stub 0x30405D.
 * Both register their restore function with AtExit.
 */
#include "sup_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
ushort lg_ctrlbreak_count;
ulong lg_old_int23_off;
ushort lg_old_int23_sel;
uchar lg_idt_save[8];
uchar lg_idtr[6];
uchar lg_divtrap_gate[8] = { 0, 0, 0, 0, 0, 0xEE, 0, 0 };
#endif

static inline ushort get_cs(void)
{
    ushort cs;
    __asm__ volatile("movw %%cs, %0" : "=r"(cs));
    return cs;
}

static inline void copy8(void *d, const void *s)
{
    ((ulong *)d)[0] = ((const ulong *)s)[0];
    ((ulong *)d)[1] = ((const ulong *)s)[1];
}

void lg_ctrlbreak_install(void)
{
    union REGS regs;

    regs.x.eax = 0x204;                 /* get protected-mode vector 0x23 */
    regs.x.ebx = 0x23;
    int386(0x31, &regs, &regs);
    lg_old_int23_off = regs.x.edx;
    lg_old_int23_sel = regs.w.cx;
    regs.x.eax = 0x205;                 /* set it to the counting stub */
    regs.x.ebx = 0x23;
    regs.w.cx = get_cs();
    regs.x.edx = (ulong)lg_int23_stub;
    int386(0x31, &regs, &regs);
    AtExit(WATCOM_FN(lg_ctrlbreak_restore));
}

void lg_ctrlbreak_restore(void)
{
    union REGS regs;

    regs.x.eax = 0x205;
    regs.x.ebx = 0x23;
    regs.w.cx = lg_old_int23_sel;
    regs.x.edx = lg_old_int23_off;
    int386(0x31, &regs, &regs);
}

void lg_divtrap_install(void)
{
    struct SREGS sregs;
    uchar *idt;

    lg_sidt();
    segread(&sregs);
    *(ushort *)(lg_divtrap_gate + 2) = sregs.cs;
    *(ushort *)(lg_divtrap_gate + 0) = (ulong)lg_divtrap_stub;
    *(ushort *)(lg_divtrap_gate + 6) = (ulong)lg_divtrap_stub >> 16;
    idt = *(uchar **)(lg_idtr + 2);
    TN_CLI_M();
    copy8(lg_idt_save, idt);
    copy8(idt, lg_divtrap_gate);
    TN_STI_M();
    AtExit(WATCOM_FN(lg_divtrap_restore));
}

void lg_divtrap_restore(void)
{
    uchar *idt = *(uchar **)(lg_idtr + 2);

    TN_CLI_M();
    copy8(idt, lg_idt_save);
    TN_STI_M();
}
