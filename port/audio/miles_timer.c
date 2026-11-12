/* miles_timer.c -- the timer services of the Miles Sound System (AIL 3.x) for the standalone
 * build.  On DOS, Miles drove the PIT (8253) and ran each registered callback at its own rate
 * from the timer interrupt; here a thread does the same, at interrupt time (holding the
 * interrupt lock).  The game's 250 Hz tick (tn_timer_tick) and the sound services run on it. */
#include <SDL3/SDL.h>
#include "platform.h"
#include "port.h"

#define MAX_TIMERS 16
#define PIT_HZ 1193180.0

typedef void (*timer_cb)(long user);

static struct {
    int used, running;
    timer_cb cb;
    long user;
    uint64_t period_ns;
    uint64_t due_ns;
} timers[MAX_TIMERS];

static volatile int ail_lock_count;
static SDL_Thread *timer_thread;
static volatile int timer_stop;
static uint64_t start_ns;

static uint64_t now_ns(void) { return SDL_GetTicksNS() - start_ns; }

static int SDLCALL timer_main(void *unused)
{
    (void)unused;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    while (!timer_stop) {
        uint64_t t = now_ns(), next = t + 2000000;
        int i;

        port_irq_enter();
        if (!ail_lock_count)
            for (i = 0; i < MAX_TIMERS; i++) {
                int n = 0;
                if (!timers[i].used || !timers[i].running || !timers[i].period_ns)
                    continue;
                /* callbacks that fell behind catch up, but not forever (after a pause) */
                while (timers[i].due_ns <= t && n < 8 && timers[i].running) {
                    timers[i].due_ns += timers[i].period_ns;
                    timers[i].cb(timers[i].user);
                    n++;
                }
                if (timers[i].due_ns <= t)
                    timers[i].due_ns = t + timers[i].period_ns;
                if (timers[i].running && timers[i].due_ns < next)
                    next = timers[i].due_ns;
            }
        port_irq_leave();
        t = now_ns();
        if (next > t)
            SDL_DelayPrecise(next - t);
    }
    return 0;
}

void port_timer_init(void)
{
    if (timer_thread || port_lockstep)  /* lockstep runs: no timer interrupt (lockstep.c) */
        return;
    start_ns = SDL_GetTicksNS();
    timer_thread = SDL_CreateThread(timer_main, "timer", NULL);
}

void port_timer_shutdown(void)
{
    if (timer_thread) {
        timer_stop = 1;
        SDL_WaitThread(timer_thread, NULL);
        timer_thread = NULL;
    }
}

/* ------------------------------------------------------------------ AIL timer API */
long AIL_register_timer(void *cb)
{
    int i, was = port_irq_save();
    for (i = 0; i < MAX_TIMERS; i++)
        if (!timers[i].used) {
            timers[i].used = 1;
            timers[i].running = 0;
            timers[i].cb = (timer_cb)cb;
            timers[i].user = 0;
            timers[i].period_ns = 0;
            break;
        }
    port_irq_restore(was);
    port_timer_init();
    return i < MAX_TIMERS ? i : -1;
}

static int valid(long h) { return h >= 0 && h < MAX_TIMERS && timers[h].used; }

long AIL_set_timer_user(long h, long user)
{
    long old = 0;
    if (valid(h)) {
        old = timers[h].user;
        timers[h].user = user;
    }
    return old;
}

long AIL_set_timer_period(long h, long us)
{
    if (valid(h))
        timers[h].period_ns = (uint64_t)(us > 0 ? us : 1) * 1000;
    return 0;
}

long AIL_set_timer_frequency(long h, long hz)
{
    if (valid(h))
        timers[h].period_ns = (uint64_t)(1e9 / (hz > 0 ? hz : 1));
    return 0;
}

long AIL_set_timer_divisor(long h, long divisor)
{
    if (valid(h))
        timers[h].period_ns = (uint64_t)((divisor & 0xFFFF ? divisor & 0xFFFF : 65536) * 1e9 / PIT_HZ);
    return 0;
}

long AIL_start_timer(long h)
{
    if (valid(h) && !timers[h].running) {
        timers[h].due_ns = now_ns() + timers[h].period_ns;
        timers[h].running = 1;
    }
    return 0;
}

long AIL_stop_timer(long h)
{
    if (valid(h))
        timers[h].running = 0;
    return 0;
}

long AIL_release_timer_handle(long h)
{
    if (valid(h)) {
        int was = port_irq_save();
        timers[h].running = 0;
        timers[h].used = 0;
        port_irq_restore(was);
    }
    return 0;
}

/* AIL_lock / AIL_unlock: keep the timer callbacks out (they run later, catching up) */
long AIL_lock(void)
{
    ail_lock_count++;
    return 0;
}

long AIL_unlock(void)
{
    if (ail_lock_count > 0)
        ail_lock_count--;
    return 0;
}
