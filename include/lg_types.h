/* Basic Looking Glass types, as used by the 1995 LG tech libraries in Terra Nova.
 * The game was built with Watcom C/386: int and long are 32 bits, pointers 32 bits. */
#ifndef LG_TYPES_H
#define LG_TYPES_H

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;     /* 32 bits on the target (and with gcc -m32) */
typedef uchar bool;              /* LG bool is a byte (returned in al) */
#define TRUE 1
#define FALSE 0
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Calling-convention bridge.
 * The game was compiled with Watcom's register convention (args in eax, edx, ebx, ecx).  In the
 * verification build (TN_WATCOM_ABI) our C is linked next to the original machine code, so:
 *  - WATCOMCALL marks pointers to functions that may be original Watcom code (allocator hooks,
 *    pagers, callbacks).  gcc's regparm(2) passes the first two args in eax, edx exactly like
 *    Watcom, so it is only used for callbacks with at most two arguments.
 *  - WATCOM_FN(f) is the address to hand to code that will call f back with the Watcom
 *    convention (the generated W_f wrapper); in a plain C build it is just f.
 *  - WATCOM_CALL3(f, a, b, c) calls a 3-argument callback (eax, edx, ebx), which regparm
 *    can't express.
 */
#ifdef TN_WATCOM_ABI
#define WATCOMCALL __attribute__((regparm(2)))
#define WATCOM_FN(f) ((void *)W_##f)
#include "watcom_thunks.h"
static inline long watcom_call3(void *f, long a, long b, long c)
{
    __asm__ volatile("call *%3" : "+a"(a), "+d"(b), "+b"(c) : "r"(f) : "ecx", "memory", "cc");
    return a;
}
#define WATCOM_CALL3(f, a, b, c) watcom_call3((void *)(f), (long)(a), (long)(b), (long)(c))
/* Any number of arguments: WATCOM_CALL(f, a, b, c, d, e...) (up to 8).  Reads args[0..3]
 * even when fewer are given, so the array is always padded to 8. */
long watcom_callv(void *f, int n, const long *args);
#define WATCOM_NARGS_(a, b, c, d, e, f, g, h, n, ...) n
#define WATCOM_CALL(f, ...) watcom_callv((void *)(f), WATCOM_NARGS_(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0), \
                                         (const long[8]){ __VA_ARGS__ })
#else
/* Standalone (port) build: everything is our own C with one calling convention, so a "Watcom
 * call" is a plain call.  The pointer is cast because the game keeps many callbacks in long or
 * void * slots; every argument is a 32-bit int or pointer, like on the original. */
typedef long (*watcom_any_fn)();
#define WATCOM_CALL(f, ...) ((watcom_any_fn)(f))(__VA_ARGS__)
#define WATCOMCALL
#define WATCOM_FN(f) ((void *)(f))
#define WATCOM_CALL3(f, a, b, c) ((watcom_any_fn)(f))(a, b, c)
#endif

/* Interrupt flag and I/O ports.  The verification build keeps the original instructions; the
 * port build calls the platform layer (port/core/platform.c), where "interrupts" are the timer,
 * sound and input threads: TN_CLI() keeps them out until TN_STI(), and the ports the game
 * touches (VGA status and DAC, keyboard, PIC, game port) are emulated.  The _M forms are the
 * ones with a "memory" clobber. */
#ifdef TN_WATCOM_ABI
#define TN_CLI() __asm__ volatile("cli")
#define TN_STI() __asm__ volatile("sti")
#define TN_CLI_M() __asm__ volatile("cli" ::: "memory")
#define TN_STI_M() __asm__ volatile("sti" ::: "memory")
#else
void port_cli(void);
void port_sti(void);
unsigned char port_inb(int port);
void port_outb(int port, int v);
void port_outw(int port, int v);
#define TN_CLI() port_cli()
#define TN_STI() port_sti()
#define TN_CLI_M() port_cli()
#define TN_STI_M() port_sti()
#endif

/* Pointers the game reads while they are still 0.  Under DOS/4GW linear address 0 is
 * conventional memory, so such reads (and writes) went to low memory; the port build gives
 * them a zeroed block of its own (port_low_mem, 1 MB).  TN_LOWMEM(p) is p, or that block. */
#ifdef TN_WATCOM_ABI
#define TN_LOWMEM(p) (p)
#else
extern unsigned char port_low_mem[];
#define TN_LOWMEM(p) ((p) ? (p) : (void *)port_low_mem)
#endif

/* Compile-time layout checks against the original binary. */
#define LG_STATIC_ASSERT(c, msg) typedef char static_assert_##msg[(c) ? 1 : -1]

#endif
