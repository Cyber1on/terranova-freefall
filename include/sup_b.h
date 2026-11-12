/* Worker sup-b (INPUT + PALETTE): globals and helpers not declared in support.h.
 * Addresses are bound through symbols-sup-b.csv. */
#ifndef TN_SUP_B_H
#define TN_SUP_B_H
#include "support.h"

/* ------------------------------------------------------------------ PALETTE */
extern uchar c_off_stack[3];                /* 0x448A72 one colour sent by a SHIFT step */
extern uchar local_smap[768];               /* 0x448A75 CBANK rotation buffer */

/* ------------------------------------------------------------------ INPUT: mouse */
typedef struct { short x, y; short butts; } mouse_state;
extern short mouseQueueSize;                /* 0x3644CC 32 */
extern uchar mouseXshift, mouseYshift;      /* 0x3644CE, 0x3644CF extra bits of driver resolution */
extern uchar mouseMask;                     /* 0x3644D0 events allowed into the queue */
extern uchar mouseLefty;                    /* 0x3644D1 */
extern short mouseCalls;                    /* 0x3644D2 */
extern ulong default_mouse_ticks;           /* 0x3644D8 */
extern ulong mouseHandlerSize;              /* 0x3654F4 0x147 */
extern mouse_callfunc mouseCall[16];        /* 0x44CE80 */
extern void *mouseCallData[16];             /* 0x44CE40 */
extern short mouseInstantButts;             /* 0x44D0C2 */
extern long mouse_vel_yfrac, mouse_vel_xfrac; /* 0x44D0D0, 0x44D0D4 (names ours) */
extern char MouseHandler[];                 /* 0x319530 asm entry (ours: in mouse.c) */
#ifdef TN_LINK_ORIGINAL_DATA
extern char mouse_handler_isr[];            /* = 0x319530: the handler mouse_init installs */
#endif
struct SREGS;
union REGS;
int int386x(int intno, union REGS *in, union REGS *out, struct SREGS *s); /* 0x31950D */
void _mouse_update_vel(void);
void ReadMouseState(mouse_state *pMouseState);
short shifted_button_state(short bstate);
void mouse_handler_body(ulong eax, ulong ebx, ulong ecx, ulong edx);

/* ------------------------------------------------------------------ INPUT: kb, kblayout, kbcook, joystick */
typedef struct { ulong eax, ebx, ecx, edx, esi, edi; } dpmi_regs;
int dpmi_int31(dpmi_regs *r);               /* inline int 0x31 in the original; returns carry */
#ifdef TN_WATCOM_ABI
static inline void outb(int port, int v) { __asm__ volatile("outb %b0, %w1" : : "a"(v), "Nd"(port)); }
static inline uchar inb(int port)
{
    uchar v;
    __asm__ volatile("inb %w1, %b0" : "=a"(v) : "Nd"(port));
    return v;
}
#else
#define outb port_outb
#define inb port_inb
#endif
/* pointer to a fixed linear address (BIOS data area, DOS memory) that gcc can't see through */
static inline void *linear(ulong a) { __asm__("" : "+r"(a)); return (void *)a; }
#define BIOS_B(a) (*(volatile uchar *)linear(a))
#define BIOS_W(a) (*(volatile ushort *)linear(a))
extern ulong kb_conv_selector;              /* 0x3652C8 DOS block selector (DPMI 0x100) */
extern ulong kb_conv_linear;                /* 0x3652CC DOS block linear address; 0 = not started */
extern ulong kb_isr_size;                   /* 0x36C99C 0x15C bytes locked at kb_pm_isr */
extern ulong kb_ring_start;                 /* 0x45DAF0 buffer + 0x10 */
extern ulong kb_old_rm_vector;              /* 0x45DB90 seg:off */
extern ulong kb_stub_rm_ptr;                /* 0x45DB94 seg:off of the real-mode stub */
extern ulong kb_old_pm_offset;              /* 0x45DB9C */
extern ushort kb_old_pm_selector;           /* 0x45DBA0 */
extern ulong kb_buf_base;                   /* 0x45DBA4 */
extern ulong kb_state_ptr;                  /* 0x45DBA8 buffer + 0x410 */
extern ulong kbd_modifier_state;            /* 0x44D290 KBM_* bits: 1 scroll, 2 num, 4 caps, 8/0x10 shift,
                                               0x20/0x40 ctrl, 0x80 alt, 0x100 right alt */
extern uchar kb_signal_ctx[0x40];           /* 0x44D774 interrupted eip (+0x20), cs (+0x1C), eflags (+0x3C) */
extern ushort kb_cnv_table[][3];            /* 0x365708 unshifted, shifted, AltGr per scan code */
extern short *kb_layout_tables[8];          /* 0x36CD5C patch lists {code, 3 codes} of layouts 1-6 */
extern char kb_pm_isr[];                    /* 0x31AAE0 asm entry (ours: in kb.c) */
#ifdef TN_LINK_ORIGINAL_DATA
extern char kb_pm_isr_addr[];               /* = 0x31AAE0: the handler kb_startup installs */
#endif
int kb_isr_body(ulong *frame);
int kb_set_leds(int mask);                  /* 0x31A210 */
void joy_read_raw(short *out);              /* 0x30415C */

#endif
