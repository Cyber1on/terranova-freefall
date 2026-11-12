/* timer.c -- Terra Nova's own timer layer (no Shock counterpart; names ours).  Decompiled from
 * __FF.EXE.  The game tick (tn_ticks, 250 Hz; also the mouse timestamp register) is kept by a
 * callback of the Miles AIL timer services, reached through a small driver table so another
 * timer back end could be plugged in.  The Miles core itself (0x31D678-0x31E06F, AIL_API_*)
 * and the AIL_* API wrappers are third-party code and stay original.  (Freefall: the tick is
 * the real milliseconds, port_tick_sync, P55.)
 *
 *   0x2E0098 tn_get_ticks          0x2E00A0 tn_timer_startup      0x2EB220 tn_timer_tick
 *   0x2EB264 tn_timer_set_rate     0x2EB378 tn_timer_get_rate     0x304200 tnt_register
 *   0x304228 tnt_release           0x30423C tnt_start             0x304250 tnt_stop
 *   0x304264 tnt_set_divisor       0x304274 tnt_set_frequency     0x304284 tnt_set_period
 *   0x304294 tn_timer_install_ail  0x3042A4 tn_timer_shutdown     0x309B00 tn_timer_drv_open
 *
 * Quirks kept:
 *  - tnt_register ignores the user argument (AIL_set_timer_user is never called).
 *  - tn_timer_set_rate(0, hz2, cb) registers cb alone; with hz2 == hz it chains cb into the
 *    tick callback instead of registering a second timer.  Nothing ever releases a second timer
 *    (tn_timer_shutdown releases tn_timer_handle only, and only when it is > 0).
 *  - fix_div overflow / hz >= 0x8000 make the original fault (#DE); not reproduced.
 */
#include "sup_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
ulong tn_ticks, tn_ticks2, tn_ticks2_step;
void (*tn_timer_user_cb)(void);
long tn_timer_hz, tn_tick_acc, tn_timer_handle, tn_tick_step;
TnTimerDriver *tn_timer_driver;
TnTimerDriver tn_timer_ail_driver = {
    (void *)tnt_register, (void *)tnt_release, (void *)tnt_start, (void *)tnt_stop,
    (void *)tnt_set_divisor, (void *)tnt_set_frequency, (void *)tnt_set_period
};
#endif

#define PIT_HZ 1193180  /* 0x1234DC */

ulong tn_get_ticks(void)
{
    return tn_ticks;
}

void tn_timer_startup(void)
{
    tn_timer_install_ail();
    tn_timer_set_rate(250, 250, NULL);
    AtExit(WATCOM_FN(tn_timer_shutdown));
}

#ifndef TN_WATCOM_ABI
unsigned long long port_now_us(void);   /* port/core/platform.c */
static ulong tick_base;
static unsigned long long tick_us0;
static int tick_started;

/* Freefall: tn_ticks = the milliseconds since the timer started, set by the tick and at the start
 * of each frame (port_frame_limit) and after the sharp 3D view (sharp3d.c, whose restore of the
 * game's data puts the tick back), always with interrupts off.  The game times each frame by
 * the tick: counted in steps of 4 ms, a 60 Hz frame (16.7 ms) took 16 ms four times and 20 the
 * fifth, so everything moved a quarter further every fifth frame (P55). */
void port_tick_sync(void)
{
    if (!tick_started) {
        tick_started = 1;
        tick_base = tn_ticks;
        tick_us0 = port_now_us();
    }
    tn_ticks = tick_base + (ulong)((port_now_us() - tick_us0) / 1000);
}
#endif

/* Miles timer callback (cdecl, interrupt time). */
void tn_timer_tick(long user)
{
#ifndef TN_WATCOM_ABI
    port_tick_sync();
#else
    tn_tick_acc += tn_tick_step;
    tn_ticks += tn_tick_acc >> 16;
    ((ushort *)&tn_tick_acc)[1] = 0;            /* keep the fraction */
#endif
    tn_ticks2 += tn_ticks2_step;
    if (tn_timer_user_cb)
        tn_timer_user_cb();
}

/* hz = game tick rate (0: only register cb at hz2), hz2/cb = optional user callback rate. */
int tn_timer_set_rate(long hz, long hz2, void (*cb)(void))
{
    long h;

    if (hz == 0) {
        if (hz2 == 0 || cb == NULL)
            return 0;
        h = tn_timer_drv_open(cb, 0, (ulong)PIT_HZ / (ulong)hz2);
        if (h < 0)
            return -1;
        WATCOM_CALL(tn_timer_driver->start, h);
        return 0;
    }
    tn_timer_hz = hz;
    tn_tick_step = (long)(((long long)1000 << 32) / (long)(hz << 16));      /* fix_div(1000, hz) */
    tn_ticks2_step = (long)(((long long)1 << 32) / (long)(tn_timer_hz << 16)); /* fix_div(1, hz) */
    h = tn_timer_handle = tn_timer_drv_open(tn_timer_tick, 0, PIT_HZ / tn_timer_hz);
    if (h < 0)
        return -1;
    if (hz2 == 0 || cb == NULL) {
        WATCOM_CALL(tn_timer_driver->start, tn_timer_handle);
        return 0;
    }
    if (hz2 == hz) {
        tn_timer_user_cb = cb;
        WATCOM_CALL(tn_timer_driver->start, h);
        return 0;
    }
    WATCOM_CALL(tn_timer_driver->start, h);
    h = tn_timer_drv_open(cb, 0, (ulong)PIT_HZ / (ulong)hz2);
    if (h < 0)
        return -1;
    WATCOM_CALL(tn_timer_driver->start, h);
    return 0;
}

long tn_timer_get_rate(void)
{
    return tn_timer_hz;
}

/* ---- driver table entries for the Miles back end ---- */

long tnt_register(void *cb, long user, long divisor)
{
    long h = AIL_register_timer(cb);
    if (h != -1)
        AIL_set_timer_divisor(h, divisor);
    return h;
}

long tnt_release(long h)
{
    AIL_release_timer_handle(h);
    return 0;
}

long tnt_start(long h)
{
    AIL_start_timer(h);
    return 0;
}

long tnt_stop(long h)
{
    AIL_stop_timer(h);
    return 0;
}

long tnt_set_divisor(long h, long d)
{
    return AIL_set_timer_divisor(h, d);
}

long tnt_set_frequency(long h, long hz)
{
    return AIL_set_timer_frequency(h, hz);
}

long tnt_set_period(long h, long us)
{
    return AIL_set_timer_period(h, us);
}

long tn_timer_install_ail(void)
{
    tn_timer_driver = &tn_timer_ail_driver;
    return 0;
}

long tn_timer_shutdown(void)
{
    if (tn_timer_handle > 0)
        AIL_release_timer_handle(tn_timer_handle);
    return 0;
}

/* Registers a timer with the installed driver; -1 when there is none. */
long tn_timer_drv_open(void *cb, long user, long divisor)
{
    if (tn_timer_driver == NULL)
        return -1;
    return WATCOM_CALL(tn_timer_driver->reg, (long)cb, user, divisor);
}
