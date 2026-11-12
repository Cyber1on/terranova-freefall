/* dosemu.c -- the DOS, DPMI and mouse driver services the game asks for through the Watcom
 * library's int386 / int386x, and the small DOS/4GW and LG helpers of the original that only
 * made sense on DOS.
 *
 * The mouse is a Microsoft-compatible int 33h driver: the platform layer reports the pointer in
 * game pixels, the driver keeps it in its own (virtual) coordinates, which are the game pixels
 * shifted by mouseXshift / mouseYshift (the game asks for them that way), and calls the game's
 * event handler (function 0Ch) as the DOS driver did, at interrupt time. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "port.h"

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long ulong;
/* Watcom union REGS / struct SREGS (as in include/sup_a.h) */
union REGS {
    struct { ulong eax, ebx, ecx, edx, esi, edi, cflag; } x;
    struct { ushort ax, _1, bx, _2, cx, _3, dx, _4, si, _5, di, _6; ulong cflag; } w;
};
struct SREGS { ushort es, cs, ss, ds, fs, gs; };

extern uchar mouseXshift, mouseYshift;
extern void mouse_handler_body(ulong eax, ulong ebx, ulong ecx, ulong edx);
void port_mouse_warp(int gx, int gy);

/* ------------------------------------------------------------------ int 33h */
static struct {
    int x, y;                           /* driver coordinates */
    int xmin, xmax, ymin, ymax;
    int buttons;
    int handler_mask;
    void *handler;
    int xrate, yrate, thold;
    int ok;
} ms = { 0, 0, 0, 639, 0, 199, 0, 0, NULL, 8, 16, 64, 0 };

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void mouse_int(union REGS *r)
{
    switch (r->w.ax) {
    case 0x00:                          /* reset */
    case 0x21:
        ms.ok = 1;
        ms.handler = NULL;
        ms.handler_mask = 0;
        ms.xmin = 0; ms.xmax = 639; ms.ymin = 0; ms.ymax = 199;
        ms.x = 320; ms.y = 100;
        r->x.eax = 0xFFFF;
        r->x.ebx = 3;
        break;
    case 0x01: case 0x02:               /* show / hide the driver's pointer: the game draws its own */
        break;
    case 0x03:
        r->x.ebx = ms.buttons;
        r->x.ecx = ms.x;
        r->x.edx = ms.y;
        break;
    case 0x04:
        ms.x = clampi((short)r->w.cx, ms.xmin, ms.xmax);
        ms.y = clampi((short)r->w.dx, ms.ymin, ms.ymax);
        port_mouse_warp(ms.x >> (mouseXshift & 15), ms.y >> (mouseYshift & 15));
        break;
    case 0x07:
        ms.xmin = (short)r->w.cx;
        ms.xmax = (short)r->w.dx;
        if (ms.xmin > ms.xmax) { int t = ms.xmin; ms.xmin = ms.xmax; ms.xmax = t; }
        ms.x = clampi(ms.x, ms.xmin, ms.xmax);
        break;
    case 0x08:
        ms.ymin = (short)r->w.cx;
        ms.ymax = (short)r->w.dx;
        if (ms.ymin > ms.ymax) { int t = ms.ymin; ms.ymin = ms.ymax; ms.ymax = t; }
        ms.y = clampi(ms.y, ms.ymin, ms.ymax);
        break;
    case 0x0B:                          /* motion counters */
        r->x.ecx = 0;
        r->x.edx = 0;
        break;
    case 0x0C:
        ms.handler_mask = r->w.cx;
        ms.handler = (void *)r->x.edx;
        break;
    case 0x0F:
        ms.xrate = r->w.cx;
        ms.yrate = r->w.dx;
        break;
    case 0x13:
        ms.thold = r->w.dx;
        break;
    case 0x1B:
        r->x.ebx = ms.xrate;
        r->x.ecx = ms.yrate;
        r->x.edx = ms.thold;
        break;
    default:
        port_log("int 33h: function %04X not emulated\n", r->w.ax);
        break;
    }
}

/* from the platform layer (main thread): pointer at game pixel (gx, gy); moved 2: a motion
 * event even if the position is the same (widescreen: the pointer went beside the screen or
 * came back, so the game draws or takes away its pointer) */
void port_mouse_event(int gx, int gy, int buttons, int moved, int changed)
{
    int mask = 0;

    if (!ms.ok)
        return;
    port_irq_enter();
    if (moved) {
        int nx = clampi(gx << (mouseXshift & 15), ms.xmin, ms.xmax);
        int ny = clampi(gy << (mouseYshift & 15), ms.ymin, ms.ymax);
        if (nx != ms.x || ny != ms.y || moved == 2)
            mask |= 1;
        ms.x = nx;
        ms.y = ny;
    }
    if (changed & 1) mask |= (buttons & 1) ? 2 : 4;
    if (changed & 2) mask |= (buttons & 2) ? 8 : 0x10;
    if (changed & 4) mask |= (buttons & 4) ? 0x20 : 0x40;
    ms.buttons = buttons & 7;
    if (mask && ms.handler && (mask & ms.handler_mask)) {
        port_game_pause();              /* the handler draws the cursor */
        mouse_handler_body(mask & ms.handler_mask, ms.buttons, ms.x, ms.y);
        port_game_resume();
    }
    port_irq_leave();
}

/* lockstep.c: the mouse moved to driver position (x, y) (x < 0: not moved) and / or the
 * buttons changed (buttons < 0: unchanged), reported the way verify/frame.py's mouse() does */
void port_mouse_raw(int x, int y, int buttons)
{
    int ev = 0;

    if (x >= 0 && (x != ms.x || y != ms.y)) {
        ms.x = x;
        ms.y = y;
        ev |= 1;
    }
    if (buttons >= 0) {
        if ((buttons & 1) && !(ms.buttons & 1)) ev |= 2;
        if (!(buttons & 1) && (ms.buttons & 1)) ev |= 4;
        if ((buttons & 2) && !(ms.buttons & 2)) ev |= 8;
        if (!(buttons & 2) && (ms.buttons & 2)) ev |= 0x10;
        ms.buttons = buttons;
    }
    if (ev & ms.handler_mask && ms.handler)
        mouse_handler_body(ev, ms.buttons, ms.x, ms.y);
}

void port_mouse_click(void)
{
    port_mouse_event(0, 0, 1, 0, 1);
    port_mouse_event(0, 0, 0, 0, 1);
}

void port_input_init(void)
{
}

/* ------------------------------------------------------------------ DPMI (int 31h) */
static void dpmi_int(union REGS *r)
{
    r->x.cflag = 0;
    switch (r->w.ax) {
    case 0x0100: {                      /* DOS memory: a normal block here */
        ulong size = (ulong)r->w.bx << 4;
        void *p = calloc(1, size ? size : 16);
        if (!p) {
            r->x.cflag = 1;
            r->x.eax = 8;
            break;
        }
        /* ax = "segment" (the game turns it back into an address only for its own records),
         * dx = "selector": both are the pointer's halves, kept in a table below */
        r->x.eax = (ulong)p >> 4;
        r->x.edx = (ulong)p;
        break;
    }
    case 0x0101:
        free((void *)r->x.edx);
        break;
    case 0x0204:                        /* get protected-mode vector */
        r->x.ecx = 0;
        r->x.edx = 0;
        break;
    case 0x0205:                        /* set it: interrupts are the platform layer's */
    case 0x0201:
    case 0x0600: case 0x0601:           /* lock / unlock: nothing is paged out */
        break;
    case 0x0400:
        r->x.eax = 0x5A;
        r->x.edx = 0x0870;
        break;
    case 0x0500: {                      /* free memory information */
        ulong *info = (ulong *)r->x.edi;
        int i;
        for (i = 0; i < 12; i++)
            info[i] = 0xFFFFFFFF;
        info[0] = 64u << 20;
        break;
    }
    default:
        port_log("int 31h: DPMI function %04X not emulated\n", r->w.ax);
        r->x.cflag = 1;
        break;
    }
}

int int386(int intno, union REGS *in, union REGS *out)
{
    if (out != in)
        *out = *in;
    switch (intno) {
    case 0x31:
        dpmi_int(out);
        break;
    case 0x33:
        mouse_int(out);
        break;
    default:
        port_log("int %02Xh (ax %04X) not emulated\n", intno, out->w.ax);
        break;
    }
    return out->x.eax;
}

int int386x(int intno, union REGS *in, union REGS *out, struct SREGS *s)
{
    (void)s;
    return int386(intno, in, out);
}

void segread(struct SREGS *s)
{
    memset(s, 0, sizeof *s);
}

void lg_sidt(void)
{
}

/* DPMI page locking (the interrupt handlers' code and data): nothing to do */
long dpmi_lock_region(void *p, long n) { (void)p; (void)n; return 0; }
long dpmi_unlock_region(void *p, long n) { (void)p; (void)n; return 0; }
long dpmi_lock_data(void *p, long n) { (void)p; (void)n; return 0; }
long dpmi_unlock_data(void *p, long n) { (void)p; (void)n; return 0; }

/* The divide-error handler of the original (int 0 through DPMI) skipped a faulting divide in
 * the registered code ranges.  The port's C does its divisions in C; nothing to install. */
long g3a_div0_install(long flags) { (void)flags; return 0; }
long g3a_div0_add_range(void *start, void *end) { (void)start; (void)end; return 0; }
long crt_2DF9E9(void) { return 0; }

/* ------------------------------------------------------------------ exit */
extern void *crt_exit_hook;
extern void AtExitRun(void);
void port_finish(int code);
static char *exit_msg;

/* 0x2DF720: at exit, writes the message lg_exit_msg left (to stdout, as the original) */
long crt_2DF720(void)
{
    if (exit_msg) {
        fputs(exit_msg, stdout);
        fflush(stdout);
        port_log("%s\n", exit_msg);
    }
    return 0;
}

/* Watcom exit(): the C library's exit hooks (LG's AtExitRun), then the end of the process. */
void port_exit(int code)
{
    static int exiting;
    if (!exiting++ && crt_exit_hook) {
        port_game_ending();             /* the window stops drawing from the game's data (P113) */
        AtExitRun();
    }
    fflush(stdout);
    port_log("The game ended (exit code %d).\n", code);
    port_finish(code);
    exit(code);
}

void lg_exit_msg(int code, char *msg)
{
    exit_msg = msg;
    port_exit(code);
}

/* ------------------------------------------------------------------ the Watcom C library */
long crt_abs(long v) { return v < 0 ? -v : v; }
long crt_labs(long v) { return v < 0 ? -v : v; }
void *crt_memcpy(void *d, const void *s, unsigned long n) { return memcpy(d, s, n); }
/* Watcom C++ array constructor helper (array, count, signature): Q's constructor is empty */
void *wcpp_ctor_array(void *a, long n, void *sig) { (void)n; (void)sig; return a; }
/* the C++ helpers of class Q (FIX++): an empty constructor, a copy constructor and the
 * deleting destructor helper */
void *Q_ctor(void *q) { return q; }
void *Q_copy_ctor(void *q, const void *src) { *(long *)q = *(const long *)src; return q; }
void *Q_delete_helper(void *q, long flags)
{
    if (flags & 4)
        free(q);                        /* original: operator delete */
    return q;
}

/* critical error handler (int 24h): the port has none */
void _harderr(void *handler) { (void)handler; }
