/* mouse.c -- n:\project\lib\src\input\mouse.c (Terra Nova v1.09): interrupt-driven mouse
 * through the Microsoft mouse driver (int 0x33 via int386/int386x).  Decompiled from __FF.EXE.
 *
 *   0x2FB750 mouse_shutdown          0x2FB7C8 mouse_init             0x2FB8EC mouse_set_screensize
 *   0x2FB96C _mouse_update_vel       0x2FBAB0 mouse_get_xy           0x2FBAE0 mouse_put_xy
 *   0x2FBBD4 mouse_check_btn         0x2FBC00 mouse_look_next        0x2FBC48 mouse_next
 *   0x2FBCB8 mouse_flush             0x2FBCD4 mouse_generate         0x2FBDB8 mouse_set_callback
 *   0x2FBE1C mouse_unset_callback    0x2FBE64 mouse_constrain_xy     0x2FBF44 mouse_set_rate
 *   0x2FBFD0 mouse_get_rate          0x2FC010 mouse_set_timestamp_register
 *   0x2FC018 mouse_get_time          0x2FC020 ReadMouseState         0x2FC05C mouse_extremes
 *   0x2FC0D8 shifted_button_state    0x2FC0EC mouse_set_lefty        0x2FC128 mouse_set_velocity_range
 *   0x2FC144 mouse_set_velocity      0x2FC1D0 mouse_add_velocity     0x2FC1EC mouse_get_velocity
 *   0x319530 MouseHandler (asm, DOS/4GW protected-mode int 0x33 user handler; see below)
 *
 * This is the DOS version that the Mac Shock source keeps in comments, with Terra Nova changes:
 *  - mouse_constrain_xy clamps its arguments (0..10000, high >= low, at most 4000 wide);
 *  - _mouse_update_vel keeps the fractional part of the motion (mouse_vel_xfrac/yfrac), so
 *    slow velocities still move the pointer (Shock drops it);
 *  - mouse_set_screensize uses rates 8/8 below 321 pixels wide (Shock: 8/8 too, via 16/2).
 * Quirks kept: shifted_button_state uses the button numbers as masks (it returns bstate & 2,
 * so mouse_set_lefty only clears bit 0); the ISR wraps the queue with == size while the C code
 * uses <; mouse_put_xy passes an event whose 6 pad bytes are uninitialised stack.
 */
#include "support.h"
#include "sup_a.h"
#include "sup_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
short mouseQueueSize = 32;
uchar mouseXshift = 0, mouseYshift = 1, mouseMask = 0xFF, mouseLefty = 0;
short mouseCalls = 0;
bool mouse_inited = FALSE;
ulong default_mouse_ticks = 0;
ulong *mouse_ticks_ptr = &default_mouse_ticks;
long mouse_xvel, mouse_yvel;
long mouse_vel_xh = 0x7FFFFFFF, mouse_vel_yh = 0x7FFFFFFF;
long mouse_vel_xl = 0x80000000, mouse_vel_yl = 0x80000000;
ulong mouse_last_vel_time;
ulong mouseHandlerSize = 0x147;
mouse_callfunc mouseCall[16];
void *mouseCallData[16];
mouse_event mouse_queue[32];
short mouse_qhead, mouseInstantButts, mouse_qtail;
short mouse_x, mouse_y;
long mouse_vel_yfrac, mouse_vel_xfrac;
#define mouse_handler_isr MouseHandler
#endif

#define INT_MOUSE 0x33

static inline void dpmi_region(ulong fn, void *addr, ulong size)
{
    /* inline int 0x31 ax=0x600 lock / 0x601 unlock linear region bx:cx, size si:di; result ignored */
#ifdef TN_WATCOM_ABI
    __asm__ volatile("int $0x31" : "+a"(fn)
                     : "b"((ulong)addr >> 16), "c"((ulong)addr & 0xFFFF), "S"(size >> 16), "D"(size & 0xFFFF)
                     : "memory", "cc");
#else
    (void)fn; (void)addr; (void)size;   /* nothing is paged out in the standalone build */
#endif
}

int mouse_shutdown(void)
{
    union REGS regs;
    struct SREGS segregs;

    if (mouse_inited) {
        regs.x.eax = 0x000C;
        regs.x.ecx = 0;
        regs.x.edx = 0;
        segregs.es = 0;
        segregs.ds = 0;
        int386x(INT_MOUSE, &regs, &regs, &segregs);
        dpmi_region(0x601, mouse_handler_isr, mouseHandlerSize);
    }
    return 0;
}

int mouse_init(short xsize, short ysize)
{
    union REGS regs;
    struct SREGS segregs;
    ushort cs;

    regs.x.eax = 0x0000;
    int386(INT_MOUSE, &regs, &regs);
    mouse_inited = (regs.w.ax != 0);
    if (mouse_inited) {
        mouse_qhead = 0;
        mouse_qtail = 0;
        mouseCalls = 0;
        regs.x.eax = 0x0003;                    /* ReadMouseState, inlined */
        int386(INT_MOUSE, &regs, &regs);
        mouse_x = regs.w.cx;
        mouse_y = regs.w.dx;
        mouseInstantButts = regs.w.bx;
        __asm__ volatile("movw %%cs, %0" : "=r"(cs));
        dpmi_region(0x600, mouse_handler_isr, mouseHandlerSize);
        regs.x.eax = 0x000C;
        regs.w.cx = 0xFF;                       /* take all mouse events */
        regs.x.edx = (ulong)mouse_handler_isr;
        segregs.es = cs;
        segregs.ds = cs;
        int386x(INT_MOUSE, &regs, &regs, &segregs);
        mouse_set_screensize(xsize, ysize);
    }
    AtExit(WATCOM_FN(mouse_shutdown));
    /* errtype is 16 bits here: the upper half of eax is whatever AtExit returned */
    return mouse_inited ? 0 : 1;               /* OK : ERR_NODEV */
}

int mouse_set_screensize(short x, short y)
{
    short xrate = 8, yrate = 8, t = 100;

    if (x > 320) {
        xrate = yrate = 2;
        mouseXshift = 3;
        mouseYshift = 3;
    } else {
        mouseXshift = 1;
        mouseYshift = 0;
    }
    mouse_set_rate(xrate, yrate, t);
    mouse_constrain_xy(0, 0, x - 1, y - 1);
    mouse_put_xy(mouse_x, mouse_y);
    return 0;
}

/* Moves the pointer by velocity * elapsed ticks (velocity in pixels per 0x10000 ticks),
 * keeping the remainder for the next call. */
void _mouse_update_vel(void)
{
    ulong ticks = *mouse_ticks_ptr;
    short newx, newy, sy;
    long mx, my, ax, ay, sx;
    long dt;

    if (ticks != mouse_last_vel_time && (mouse_xvel != 0 || mouse_yvel != 0)) {
        newx = mouse_x;
        newy = mouse_y;
        dt = ticks - mouse_last_vel_time;
        mx = mouse_xvel * dt + mouse_vel_xfrac;
        my = mouse_yvel * dt + mouse_vel_yfrac;
        ax = mx < 0 ? -mx : mx;
        ay = my < 0 ? -my : my;
        sx = mx > 0 ? 1 : (mx < 0 ? -1 : 0);
        sy = my > 0 ? 1 : (my < 0 ? -1 : 0);
        mouse_vel_xfrac = sx * (ax % 0x10000);
        mouse_vel_yfrac = sy * (ay % 0x10000);
        mouse_put_xy((short)(newx + (ax / 0x10000) * sx), (short)(newy + (ay / 0x10000) * sy));
    }
    mouse_last_vel_time = ticks;
}

int mouse_get_xy(short *x, short *y)
{
    if (!mouse_inited)
        return 1;
    _mouse_update_vel();
    *x = mouse_x;
    *y = mouse_y;
    return 0;
}

int mouse_put_xy(short x, short y)
{
    union REGS regs, ms;
    mouse_event me;

    if (!mouse_inited)
        return 1;
    regs.x.eax = 0x0004;
    regs.x.ecx = (long)x << (mouseXshift & 31);
    regs.x.edx = (long)y << (mouseYshift & 31);
    int386(INT_MOUSE, &regs, &regs);
    ms.x.eax = 0x0003;                          /* ReadMouseState, inlined */
    int386(INT_MOUSE, &ms, &ms);
    mouse_x = (long)(short)ms.w.cx >> (mouseXshift & 31);
    mouse_y = (long)(short)ms.w.dx >> (mouseYshift & 31);
    if (mouseMask & 1) {                        /* MOUSE_MOTION */
        me.x = mouse_x;
        me.y = mouse_y;
        me.type = 1;
        me.timestamp = *mouse_ticks_ptr;
        me.buttons = (uchar)mouseInstantButts;
        mouse_generate(me);                     /* me.pad is never set */
    }
    return 0;
}

int mouse_check_btn(short button, bool *res)
{
    if (!mouse_inited)
        return 1;
    *res = ((long)mouseInstantButts >> (button & 31)) & 1;
    return 0;
}

int mouse_look_next(mouse_event *res)
{
    if (mouse_qtail == mouse_qhead)
        _mouse_update_vel();
    if (mouse_qtail == mouse_qhead)
        return 1;                               /* ERR_NODEV */
    *res = mouse_queue[mouse_qtail];
    return 0;
}

int mouse_next(mouse_event *res)
{
    if (mouse_qtail == mouse_qhead)
        _mouse_update_vel();
    if (mouse_qtail == mouse_qhead)
        return 2;                               /* ERR_DUNDERFLOW */
    *res = mouse_queue[mouse_qtail];
    mouse_qtail = (mouse_qtail + 1 < mouseQueueSize) ? mouse_qtail + 1 : 0;
    return 0;
}

int mouse_flush(void)
{
    mouse_qtail = 0;
    mouse_qhead = mouse_qtail;
    return 0;
}

int mouse_generate(mouse_event e)
{
    short newin = mouse_qhead, newout = mouse_qtail;
    short in = newin;
    int i, result = 0;

    mouse_queue[in] = e;
    newin = (in + 1 < mouseQueueSize) ? in + 1 : 0;
    if (newin == mouse_qtail) {
        newout = (newout + 1 < mouseQueueSize) ? newout + 1 : 0;
        result = 2;                             /* ERR_DUNDERFLOW (sic) */
    }
    mouse_qtail = newout;
    mouse_qhead = newin;
    mouse_x = e.x;
    mouse_y = e.y;
    mouseInstantButts = e.buttons;
    for (i = 0; i < mouseCalls; i++)
        if (mouseCall[i] != NULL)
            WATCOM_CALL(mouseCall[i], (long)&mouse_queue[in], (long)mouseCallData[i]);
    return result;
}

int mouse_set_callback(mouse_callfunc f, void *data, int *id)
{
    short calls = mouseCalls;

    for (*id = 0; *id < calls; *id = *id + 1)
        if (mouseCall[*id] == NULL)
            break;
    if (*id == 16) {
        mouseCalls = calls;
        return 3;                               /* ERR_DOVERFLOW */
    }
    if (*id == calls)
        calls++;
    mouseCall[*id] = f;
    mouseCallData[*id] = data;
    mouseCalls = calls;
    return 0;
}

int mouse_unset_callback(int id)
{
    short calls = mouseCalls;

    if (id >= calls || id < 0) {
        mouseCalls = calls;
        return 4;                               /* ERR_RANGE */
    }
    mouseCall[id] = NULL;
    while (calls > 0 && mouseCall[calls - 1] == NULL)
        calls--;
    mouseCalls = calls;
    return 0;
}

int mouse_constrain_xy(short xl, short yl, short xh, short yh)
{
    union REGS regs;

    if (xl > 10000) xl = 10000;
    if (yl > 10000) yl = 10000;
    if (xl < 0) xl = 0;
    if (yl < 0) yl = 0;
    if (xh < xl) xh = xl;
    if (yh < yl) yh = yl;
    if (xh - xl > 4000) xh = xl + 4000;
    if (yh - yl > 4000) yh = yl + 4000;
    if (!mouse_inited)
        return 1;
    regs.x.eax = 0x0007;
    regs.x.ecx = (long)xl << (mouseXshift & 31);
    regs.x.edx = (long)xh << (mouseXshift & 31);
    int386(INT_MOUSE, &regs, &regs);
    regs.x.eax = 0x0008;
    regs.x.ecx = (long)yl << (mouseYshift & 31);
    regs.x.edx = (long)yh << (mouseYshift & 31);
    int386(INT_MOUSE, &regs, &regs);
    return 0;
}

int mouse_set_rate(short xr, short yr, short thold)
{
    union REGS regs;

    if (!mouse_inited)
        return 1;
    regs.x.eax = 0x000F;
    regs.x.ecx = xr < 1 ? 1 : xr;
    regs.x.edx = yr < 1 ? 1 : yr;
    int386(INT_MOUSE, &regs, &regs);
    regs.x.eax = 0x0013;
    regs.x.edx = thold < 1 ? 1 : thold;
    int386(INT_MOUSE, &regs, &regs);
    return 0;
}

int mouse_get_rate(short *xr, short *yr, short *thold)
{
    union REGS regs;

    regs.x.eax = 0x001B;
    int386(INT_MOUSE, &regs, &regs);
    *xr = regs.x.ebx;
    *yr = regs.x.ecx;
    *thold = regs.x.edx;
    return 0;
}

int mouse_set_timestamp_register(ulong *tstamp)
{
    mouse_ticks_ptr = tstamp;
    return 0;
}

ulong mouse_get_time(void)
{
    return *mouse_ticks_ptr;
}

void ReadMouseState(mouse_state *pMouseState)
{
    union REGS regs;

    regs.x.eax = 0x0003;
    int386(INT_MOUSE, &regs, &regs);
    pMouseState->x = regs.w.cx;
    pMouseState->y = regs.w.dx;
    pMouseState->butts = regs.w.bx;
}

int mouse_extremes(short *xmin, short *ymin, short *xmax, short *ymax)
{
    union REGS regs;

    regs.x.eax = 0x31;
    int386(INT_MOUSE, &regs, &regs);
    *xmin = (long)regs.w.ax >> (mouseXshift & 31);
    *ymin = (long)regs.w.bx >> (mouseYshift & 31);
    *xmax = (long)regs.w.cx >> (mouseXshift & 31);
    *ymax = (long)regs.w.dx >> (mouseYshift & 31);
    return 0;
}

/* MOUSE_RBUTTON 1, MOUSE_LBUTTON 0, MOUSE_CBUTTON 2 are button numbers, used here as masks. */
short shifted_button_state(short bstate)
{
    short tmp = (bstate & 1) >> 1;
    tmp |= (bstate & 0) << 1;
    tmp |= bstate & 2;
    return tmp;
}

int mouse_set_lefty(bool lefty)
{
    if (lefty == mouseLefty)
        return 6;                               /* ERR_NOEFFECT */
    mouseInstantButts = shifted_button_state(mouseInstantButts);   /* inlined in the original */
    mouseLefty = lefty;
    return 0;
}

int mouse_set_velocity_range(long xl, long yl, long xh, long yh)
{
    mouse_vel_xl = xl;
    mouse_vel_yl = yl;
    mouse_vel_xh = xh;
    mouse_vel_yh = yh;
    return 0;
}

int mouse_set_velocity(long x, long y)
{
    long t;

    t = x < mouse_vel_xh ? x : mouse_vel_xh;
    mouse_xvel = t < mouse_vel_xl ? mouse_vel_xl : t;
    t = y < mouse_vel_yh ? y : mouse_vel_yh;
    mouse_yvel = t < mouse_vel_yl ? mouse_vel_yl : t;
    if (mouse_xvel != x || mouse_yvel != y)
        return 4;                               /* ERR_RANGE */
    return 0;
}

int mouse_add_velocity(long x, long y)
{
    return mouse_set_velocity(mouse_xvel + x, mouse_yvel + y);
}

int mouse_get_velocity(long *x, long *y)
{
    *x = mouse_xvel;
    *y = mouse_yvel;
    return 0;
}

/* ------------------------------------------------------------------ interrupt handler
 * MouseHandler is far-called by the DOS/4GW mouse driver extension with ax = event mask,
 * bx = buttons, cx = x, dx = y.  It loads ds/es with the hard-wired flat selector 0x188,
 * queues the event (if mouseMask allows it), and calls the callbacks from the last to the
 * first, with interrupts off around each call.  The asm entry saves the registers and passes
 * them to mouse_handler_body. */
void mouse_handler_body(ulong eax, ulong ebx, ulong ecx, ulong edx)
{
    ushort mask = eax, butts = ebx, x = ecx, y = edx, slot, n, d;
    uchar lo, hi, sx = mouseXshift & 31, sy = mouseYshift & 31;

    x = sx >= 16 ? 0 : x >> sx;
    y = sy >= 16 ? 0 : y >> sy;
    if (mouseLefty) {                           /* swap left/right in the event and the buttons */
        lo = (mask & 6) << 2;
        hi = (mask & 0x18) >> 2;
        mask = (mask & 0xFF00) | (uchar)((mask & 0xE1) | lo | hi);
        butts = (butts & 0xFFFC) | ((butts & 1) << 1) | ((butts & 2) >> 1);
    }
    mouseInstantButts = butts;
    mouse_x = x;
    mouse_y = y;
    slot = mouse_qhead;
    mouse_queue[slot].type = (uchar)mask;
    mouse_queue[slot].x = x;
    mouse_queue[slot].y = y;
    mouse_queue[slot].buttons = (uchar)butts;
    mouse_queue[slot].timestamp = *mouse_ticks_ptr;
    if (mouseMask & mask) {
        n = mouse_qhead + 1;
        if (n == (ushort)mouseQueueSize)
            n = 0;
        mouse_qhead = n;
        if (mouse_qhead == mouse_qtail) {
            n = mouse_qtail + 1;
            if (n == (ushort)mouseQueueSize)
                n = 0;
            mouse_qtail = n;
        }
    }
    for (d = (ushort)mouseCalls << 2; d != 0;) {
        d -= 4;
        if (mouseCall[d >> 2] == NULL)
            continue;
        TN_CLI();
        WATCOM_CALL(mouseCall[d >> 2], (long)&mouse_queue[slot], (long)mouseCallData[d >> 2]);
        TN_STI();
    }
}

/* Watcom-convention entry for mouse_generate: the 16-byte event is passed on the stack and
 * popped by the callee (ret 0x10); all registers are preserved. */
#ifdef TN_WATCOM_ABI
__asm__(
    "\t.text\n"
    "\t.globl mouse_generate_watcom\n"
    "mouse_generate_watcom:\n"
    "\tpush %ecx\n\tpush %edx\n"
    "\tpushl 24(%esp)\n\tpushl 24(%esp)\n\tpushl 24(%esp)\n\tpushl 24(%esp)\n"
    "\tcall mouse_generate\n"
    "\taddl $16, %esp\n"
    "\tpop %edx\n\tpop %ecx\n"
    "\tret $16\n");
#else
#endif

#ifdef TN_WATCOM_ABI
__asm__(
    "\t.text\n"
    "\t.globl MouseHandler\n"
    "MouseHandler:\n"
    "\tpush %es\n\tpush %ds\n\tpush %ebp\n\tpush %esi\n\tpush %edi\n"
    "\tpush %edx\n\tpush %ecx\n\tpush %ebx\n\tpush %eax\n"
    "\tmovw $0x188, %ax\n\tmovw %ax, %ds\n\tmovw %ax, %es\n"
    "\tmovl %esp, %eax\n"
    "\tpushl 12(%eax)\n\tpushl 8(%eax)\n\tpushl 4(%eax)\n\tpushl (%eax)\n"
    "\tcall mouse_handler_body\n"
    "\taddl $16, %esp\n"
    "\tpop %eax\n\tpop %ebx\n\tpop %ecx\n\tpop %edx\n"
    "\tpop %edi\n\tpop %esi\n\tpop %ebp\n\tpop %ds\n\tpop %es\n"
    "\tlret\n");
#else
/* Standalone build: no mouse driver callback (the platform layer calls mouse_handler_body);
 * only the address is taken. */
char MouseHandler[4];
#endif
