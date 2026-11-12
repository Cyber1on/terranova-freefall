/* kb.c -- x:\prj\tech\libsrc\input\kb*.c, DOS version (Terra Nova v1.09): low-level keyboard.
 * Decompiled from __FF.EXE.
 *
 *   0x304C80 kb_startup      0x304EC0 kb_set_state     0x304ED0 kb_shutdown
 *   0x305000 kb_flush_bios   0x305020 kb_look_next     0x30504C kb_next
 *   0x31AAE0 kb_pm_isr (asm IRQ1 handler, see below)
 *
 * kb_startup allocates a DOS memory block (DPMI 0x100) holding the event buffer shared with
 * the interrupt handlers (kb_lowmem: write index, scan-code history, flags, a 512-entry ring of
 * kbs_event, per-scan-code KBA_* bits) followed by a real-mode IRQ1 stub, copied from the
 * executable's second object (0x72000, kb_stub_size bytes).  It locks the protected-mode
 * handler and the block, hooks IRQ1 in protected mode (kb_pm_isr) and real mode (the stub),
 * and copies the BIOS lock-key state into kbd_modifier_state.  All DPMI calls are inline
 * int 0x31.  A call that sets carry but returns ax = 0 counts as success (the code tests eax).
 *
 * kb_look_next / kb_next return the event in ax (code 0xFF = none); if bit 0 of the buffer
 * flags is set they wait for an event instead (nothing in the game sets it).
 */
#ifndef TN_WATCOM_ABI
#include <stdlib.h>
#endif
#include "support.h"
#include "sup_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
ulong kb_conv_selector, kb_conv_linear;
ulong kb_isr_size = 0x15C;
ulong kb_ring_start, kb_old_rm_vector, kb_stub_rm_ptr, kb_old_pm_offset, kb_buf_base, kb_state_ptr;
ushort kb_old_pm_selector;
uchar kb_signal_ctx[0x40];
#define kb_pm_isr_addr kb_pm_isr
#endif

/* inline int 0x31; returns the carry flag */
int dpmi_int31(dpmi_regs *r)
{
    ulong a = r->eax, b = r->ebx, c = r->ecx, d = r->edx, s = r->esi, di = r->edi;
    uchar cf;

    __asm__ volatile("int $0x31\n\tsetc %6"
                     : "+a"(a), "+b"(b), "+c"(c), "+d"(d), "+S"(s), "+D"(di), "=m"(cf)
                     : : "memory", "cc");
    r->eax = a; r->ebx = b; r->ecx = c; r->edx = d; r->esi = s; r->edi = di;
    return cf;
}

/* DPMI 0x600 lock / 0x601 unlock linear region; returns 0 or the error (eax) */
static ulong kb_dpmi_region(dpmi_regs *r, ulong fn, ulong addr, ulong size)
{
    r->eax = fn;
    r->ebx = addr >> 16;
    r->ecx = addr & 0xFFFF;
    r->esi = size >> 16;
    r->edi = size & 0xFFFF;
    return dpmi_int31(r) ? r->eax : 0;
}

/* DPMI 0x400: the master PIC base is in dh; returns the IRQ1 vector */
static uchar kb_irq1_vector(void)
{
    dpmi_regs r = { 0 };

    r.eax = 0x400;
    dpmi_int31(&r);
    return (uchar)(r.edx >> 8) + 1;
}

#ifdef TN_WATCOM_ABI
int kb_startup(void *init_buf)
{
    /* one register set flows through the int 0x31 calls, as in the original: a call that
     * fails leaves the previous call's outputs in the registers that are stored */
    dpmi_regs r = { 0 };
    ulong err, buf;
    kb_lowmem *b;
    uchar vec;
    ushort cs;

    if (kb_conv_linear != 0)
        return 0;
    r.eax = 0x100;
    r.ebx = (kb_stub_size + 0x61F) >> 4;
    err = 0;
    if (dpmi_int31(&r))
        err = r.eax;
    else {
        kb_conv_selector = r.edx;
        kb_conv_linear = (r.eax & 0xFFFF) << 4;
    }
    if (err)
        return -1;

    buf = kb_conv_linear;
    kbd_lowmem_start = (kb_lowmem *)buf;
    if (kb_dpmi_region(&r, 0x600, (ulong)kb_pm_isr_addr, kb_isr_size) != 0
        || kb_dpmi_region(&r, 0x600, buf, kb_stub_size + 0x610) != 0)
        return -1;

    /* real-mode far pointer of the stub, which sits after the 0x610-byte buffer */
    kb_stub_rm_ptr = (((buf & 0xFFFF0) << 12) | (buf & 0xF)) + 0x610;
    memcpy((void *)(buf + 0x610), linear(0x72000), kb_stub_size);
    r.ebx = buf & 0xF;                      /* register contents left by the original's code */
    r.ecx = 0;                              /* (rep movs) that a failing call leaves in place */

    r.eax = 0x400;                          /* DPMI version: dh = master PIC base */
    dpmi_int31(&r);
    vec = (uchar)(r.edx >> 8) + 1;          /* IRQ1 */
    r.eax = 0x204;                          /* old protected-mode vector */
    r.ebx = (r.ebx & ~0xFF) | vec;
    dpmi_int31(&r);
    kb_old_pm_selector = r.ecx;
    kb_old_pm_offset = r.edx;
    r.eax = 0x200;                          /* old real-mode vector */
    dpmi_int31(&r);
    kb_old_rm_vector = (r.ecx << 16) | (r.edx & 0xFFFF);

    kbd_modifier_state = (BIOS_B(0x417) >> 4) & 7;     /* BIOS scroll/num/caps lock */
    b = (kb_lowmem *)buf;
    kb_buf_base = buf;
    b->last = 0;
    b->flags = 0;
    b->unk8 = kb_old_rm_vector;             /* the real-mode stub chains to it */
    kb_ring_start = buf + 0x10;
    kb_state_ptr = buf + 0x410;
    kb_read_index = 0x10;
    b->head = 0x10;
    if (init_buf == NULL)
        memset(b->state, 0, 0x100);
    else
        memcpy(b->state, init_buf, 0x100);

    __asm__ volatile("movw %%cs, %0" : "=r"(cs));
    r.eax = 0x205;
    r.ebx = vec;
    r.ecx = cs;
    r.edx = (ulong)kb_pm_isr_addr;
    if (dpmi_int31(&r) && r.eax != 0)
        return -1;
    r.eax = 0x201;
    r.ecx = kb_stub_rm_ptr >> 16;
    r.edx = kb_stub_rm_ptr;
    dpmi_int31(&r);                         /* result ignored */
    kb_layout_init();
    return 0;
}

#else
/* Standalone build: the buffer is ordinary memory and the platform layer calls kb_isr_body for
 * every byte the keyboard sends (port/core/platform.c).  Same buffer set-up as above. */
int kb_startup(void *init_buf)
{
    kb_lowmem *b;

    if (kb_conv_linear != 0)
        return 0;
    b = (kb_lowmem *)calloc(1, 0x610);
    if (b == NULL)
        return -1;
    kbd_modifier_state = 0;
    kbd_lowmem_start = b;
    kb_buf_base = (ulong)b;
    b->last = 0;
    b->flags = 0;
    kb_ring_start = (ulong)b + 0x10;
    kb_state_ptr = (ulong)b + 0x410;
    kb_read_index = 0x10;
    b->head = 0x10;
    if (init_buf == NULL)
        memset(b->state, 0, 0x100);
    else
        memcpy(b->state, init_buf, 0x100);
    kb_layout_init();
    kb_conv_linear = (ulong)b;
    return 0;
}
#endif

void kb_set_state(int code, uchar bits)
{
    ((uchar *)kbd_lowmem_start)[0x410 + code] |= bits;
}

#ifdef TN_WATCOM_ABI
int kb_shutdown(void)
{
    dpmi_regs r = { 0 };
    uchar vec;

    if (kb_conv_linear == 0)
        return 0;
    vec = kb_irq1_vector();
    r.eax = 0x205;
    r.ebx = vec;
    r.ecx = kb_old_pm_selector;
    r.edx = kb_old_pm_offset;
    if (dpmi_int31(&r) && r.eax != 0)
        return -1;
    r.eax = 0x201;
    r.ebx = vec;
    r.ecx = kb_old_rm_vector >> 16;
    r.edx = kb_old_rm_vector;
    dpmi_int31(&r);                         /* result ignored */
    if (kb_dpmi_region(&r, 0x601, (ulong)kb_pm_isr_addr, kb_isr_size) != 0
        || kb_dpmi_region(&r, 0x601, kb_conv_linear, kb_stub_size + 0x610) != 0)
        return -1;
    r.eax = 0x101;
    r.edx = kb_conv_selector;
    if (dpmi_int31(&r) && r.eax != 0)
        return -1;
    /* give the lock-key state back to the BIOS */
    BIOS_B(0x417) = (BIOS_B(0x417) & 0x80) | ((kbd_modifier_state & 7) << 4);
    kb_conv_linear = 0;
    return 0;
}

void kb_flush_bios(void)
{
    TN_CLI_M();
    BIOS_W(0x41A) = BIOS_W(0x41C);   /* BIOS buffer head = tail */
    BIOS_B(0x418) &= 0;
    TN_STI_M();
}

#else
int kb_shutdown(void)
{
    kb_conv_linear = 0;         /* the buffer stays: the game may still read it */
    return 0;
}

void kb_flush_bios(void)
{
}
#endif

ushort kb_look_next(void)
{
    volatile kb_lowmem *b = kbd_lowmem_start;
    ulong i = kb_read_index;
    ushort flags = b->flags;

    while ((ushort)i == (ushort)b->head)
        if (!(flags & 1))
            return KBC_NONE;
    return *(volatile ushort *)((char *)b + i);
}

ushort kb_next(void)
{
    volatile kb_lowmem *b = kbd_lowmem_start;
    ulong i = kb_read_index;
    ushort flags = b->flags, ev;

    while ((ushort)i == (ushort)b->head)
        if (!(flags & 1))
            return KBC_NONE;
    ev = *(volatile ushort *)((char *)b + i);
    i = (i & 0xFFFF0000) | (ushort)(i + 2);
    if (i >= 0x410)
        i = 0x10;
    kb_read_index = i;
    return ev;
}

/* ------------------------------------------------------------------ IRQ1 handler
 * kb_pm_isr: protected-mode keyboard interrupt.  Reads the scan code from port 0x60 and turns
 * it into a kbs_event (code with bit 7 = E0-extended key, state 1 = down), using the history
 * of scan codes in b->last to decode E0/E1 prefixes (E0 2A / E0 AA fake shifts are dropped,
 * the E1 1D 45 pause sequence becomes code 0x7F).  Key repeats are dropped unless the key's
 * state has KBA_REPEAT; a key with KBA_SIGNAL redirects the interrupted code to the game's
 * handler at 0x180:0x3417E0, saving eip/cs/eflags in kb_signal_ctx.  0xFA (ack) and 0xFE
 * (resend) set flags bits 3 and 4.  If flags bit 1 is set the old handler gets the
 * interrupt, else the handler sends EOI itself.
 * Quirk kept: on the signal path the event stored is the low word of kb_signal_ctx's address
 * (0xD774), because eax was reused for it.
 * The asm entry saves the registers, loads ds with the hard-wired flat selector 0x188 and
 * calls kb_isr_body with the address of the interrupt frame (eip, cs, eflags). */
int kb_isr_body(ulong *frame)
{
    kb_lowmem *b = kbd_lowmem_start;
    ulong last = b->last, idx;
    uchar sc = inb(0x60), cl;
    ushort ev;

    if (sc == 0xFA) {
        b->flags |= 8;
        goto done;
    }
    if (sc == 0xFE) {
        b->flags |= 0x10;
        goto done;
    }
    if (sc == 0xE0 || sc == 0xE1)
        goto save;
    if ((uchar)last == 0xE0) {
        if (sc == 0x2A || sc == 0xAA)
            goto save;
        ev = ((sc & 0x80) ? 0 : 0x100) | (uchar)(sc | 0x80);
    } else {
        if ((uchar)last == 0xE1)
            goto save;
        if ((ushort)last == 0xE19D || (ushort)last == 0xE11D)
            sc = (sc & 0x80) | 0x7F;
        ev = ((sc & 0x80) ? 0 : 0x100) | (sc & 0x7F);
    }
    sc = (uchar)ev;
    if (ev & 0x100) {                       /* make */
        cl = b->state[sc];
        if (!(cl & KBA_STATE))
            b->state[sc] = cl | KBA_STATE;
        else if (!(cl & KBA_REPEAT))
            goto save;                      /* repeat dropped */
        if (cl & KBA_SIGNAL) {
            *(ulong *)(kb_signal_ctx + 0x20) = frame[0];
            *(ushort *)(kb_signal_ctx + 0x1C) = frame[1];
            *(ulong *)(kb_signal_ctx + 0x3C) = frame[2];
            frame[0] = 0x3417E0;
            frame[1] = 0x180;
            ev = (ushort)(ulong)kb_signal_ctx;
            sc = (uchar)ev;
        }
    } else
        b->state[sc] &= ~KBA_STATE;
    idx = b->head & 0xFFFF;
    *(ushort *)((char *)b + idx) = ev;
    idx += 2;
    if (idx >= 0x410)
        idx = 0x10;
    b->head = idx;
save:
    b->last = (last << 8) | sc;
done:
    if (b->flags & 2)
        return 1;                           /* chain to the old handler */
    TN_CLI();
    outb(0x20, 0x20);
    return 0;
}

#ifdef TN_WATCOM_ABI
__asm__(
    "\t.text\n"
    "\t.globl kb_pm_isr\n"
    "kb_pm_isr:\n"
    "\tsti\n"
    "\tpush %eax\n\tpush %ebx\n\tpush %ds\n\tpush %ecx\n\tpush %edx\n\tpush %esi\n"
    "\tmovw $0x188, %bx\n\tmovw %bx, %ds\n"
    "\tleal 24(%esp), %eax\n"
    "\tpush %eax\n\tcall kb_isr_body\n\taddl $4, %esp\n"
    "\tpop %esi\n\tpop %edx\n\tpop %ecx\n"
    "\ttestl %eax, %eax\n\tjnz 1f\n"
    "\tpop %ds\n\tpop %ebx\n\tpop %eax\n\tiret\n"
    "1:\tmovzwl kb_old_pm_selector, %eax\n\tmovl kb_old_pm_offset, %ebx\n"
    "\tpop %ds\n\txchgl %ebx, (%esp)\n\txchgl %eax, 4(%esp)\n\tcli\n\tlret\n");
#else
/* Standalone build: no IRQ1 handler (the platform layer feeds key events); only the address is
 * taken, for the DPMI calls that are stubs there. */
char kb_pm_isr[4];
#endif
