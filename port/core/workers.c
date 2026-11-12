/* workers.c -- threads that share the heavy loops of the sharp 3D view (the water reflection,
 * rndsky.c).  port_parallel(fn, arg, n) runs fn(arg, part, thread) for each part 0 .. n - 1 on
 * the game thread (thread 0) and the worker threads (1 ..), handing the parts out one at a time
 * so uneven parts balance, and returns when all are done.  The parts must not depend on each
 * other.  One worker per logical core but one (the game thread is the other), at most
 * PORT_THREADS - 1; FREEFALL_THREADS=n sets how many threads in all (1: none but the game's).
 * The workers start at the first call and then wait on a semaphore between calls.
 *
 * port_stream_begin(fn, arg): the workers run fn(arg, part, thread) for the parts the game thread
 * hands out with port_stream_ready(n) (parts 0 .. n - 1 ready) while the game thread goes on with
 * its own work; port_stream_end(n) runs what is left on the game thread too and returns when all
 * n are done (haze.c: the far land's pixels drawn while the game walks the land).  The parts
 * must not depend on each other nor on what the game thread does meanwhile.  port_parallel while
 * a stream runs runs its parts on the game thread alone; one stream at a time.
 *
 * port_parallel_display: the same for the window's thread (platform.c), with workers of its own
 * (at most 3, at high priority like the window's thread): laying a new 3D picture into the
 * screen's colours took the window's thread a whole refresh or more at 4K, while the game's
 * workers had every core (P59) */
#include <SDL3/SDL.h>
#include <stdlib.h>
#include "port.h"

typedef struct {
    SDL_Semaphore *go, *done;
    SDL_AtomicInt next;
    int nw, total;
    void (*job)(void *, int, int);
    void *job_arg;
} Pool;

static Pool game = { NULL, NULL, { 0 }, -1, 0, NULL, NULL };
static Pool disp = { NULL, NULL, { 0 }, -1, 0, NULL, NULL };

static void run_parts(Pool *p, int thread)
{
    int i;
    while ((i = SDL_AddAtomicInt(&p->next, 1)) < p->total)
        p->job(p->job_arg, i, thread);
}

typedef struct {
    Pool *p;
    int thread;
} Worker;

static int worker(void *arg)
{
    Worker *w = arg;
    if (w->p == &disp)
        SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    for (;;) {
        SDL_WaitSemaphore(w->p->go);
        run_parts(w->p, w->thread);
        SDL_SignalSemaphore(w->p->done);
    }
    return 0;
}

static void pool_start(Pool *p, int n, const char *name)
{
    static Worker ws[2][PORT_THREADS];
    int i;
    p->nw = 0;
    if (n > PORT_THREADS - 1)
        n = PORT_THREADS - 1;
    if (n > 0 && (p->go = SDL_CreateSemaphore(0)) && (p->done = SDL_CreateSemaphore(0)))
        for (i = 0; i < n; i++) {
            Worker *w = &ws[p == &disp][i];
            SDL_Thread *t;
            w->p = p;
            w->thread = i + 1;
            t = SDL_CreateThread(worker, name, w);
            if (!t)
                break;
            SDL_DetachThread(t);
            p->nw++;
        }
}

int port_threads(void)
{
    if (game.nw < 0) {
        const char *e = getenv("FREEFALL_THREADS");
        pool_start(&game, e ? atoi(e) - 1 : SDL_GetNumLogicalCPUCores() - 1, "tnfree worker");
        port_log("Drawing threads: %d\n", game.nw + 1);
    }
    return game.nw + 1;
}

static void pool_run(Pool *p, void (*fn)(void *arg, int part, int thread), void *arg, int n)
{
    int i;
    if (p->nw == 0 || n <= 1) {
        for (i = 0; i < n; i++)
            fn(arg, i, 0);
        return;
    }
    p->job = fn;
    p->job_arg = arg;
    p->total = n;
    SDL_SetAtomicInt(&p->next, 0);
    for (i = 0; i < p->nw; i++)
        SDL_SignalSemaphore(p->go);
    run_parts(p, 0);
    for (i = 0; i < p->nw; i++)
        SDL_WaitSemaphore(p->done);
}

static struct {
    SDL_Mutex *m;
    SDL_Condition *c;
    int ready, next, done, on;          /* (ready, next and done under m while on) */
    void (*fn)(void *, int, int);
    void *arg;
} st;

void port_parallel(void (*fn)(void *arg, int part, int thread), void *arg, int n)
{
    int i;
    port_threads();
    if (st.on) {                        /* (the workers are the stream's) */
        for (i = 0; i < n; i++)
            fn(arg, i, 0);
        return;
    }
    pool_run(&game, fn, arg, n);
}

/* the next ready part, -1 none (wait: for one, or the stream's end) */
static int stream_take(int wait)
{
    int i = -1;
    SDL_LockMutex(st.m);
    while (wait && st.next >= st.ready && !st.done)
        SDL_WaitCondition(st.c, st.m);
    if (st.next < st.ready)
        i = st.next++;
    SDL_UnlockMutex(st.m);
    return i;
}

static void stream_job(void *arg, int part, int thread)
{
    int i;
    (void)arg;
    (void)part;
    while ((i = stream_take(1)) >= 0)
        st.fn(st.arg, i, thread);
}

void port_stream_begin(void (*fn)(void *arg, int part, int thread), void *arg)
{
    int i;
    port_threads();
    st.fn = fn;
    st.arg = arg;
    st.ready = st.next = st.done = 0;
    if (game.nw == 0 || (!st.m && !(st.m = SDL_CreateMutex())) ||
        (!st.c && !(st.c = SDL_CreateCondition())))
        return;                         /* (all at port_stream_end) */
    st.on = 1;
    game.job = stream_job;
    game.job_arg = NULL;
    game.total = game.nw;
    SDL_SetAtomicInt(&game.next, 0);
    for (i = 0; i < game.nw; i++)
        SDL_SignalSemaphore(game.go);
}

void port_stream_ready(int n)
{
    int more = n - st.ready;            /* (st.ready changes only here and at the end) */
    if (!st.on || more <= 0)
        return;
    SDL_LockMutex(st.m);
    st.ready = n;
    SDL_UnlockMutex(st.m);
    if (more == 1)
        SDL_SignalCondition(st.c);
    else
        SDL_BroadcastCondition(st.c);
}

void port_stream_end(int n)
{
    int i;
    if (!st.on) {
        for (i = 0; i < n; i++)
            st.fn(st.arg, i, 0);
        return;
    }
    SDL_LockMutex(st.m);
    if (n > st.ready)
        st.ready = n;
    st.done = 1;
    SDL_UnlockMutex(st.m);
    SDL_BroadcastCondition(st.c);
    while ((i = stream_take(0)) >= 0)
        st.fn(st.arg, i, 0);
    for (i = 0; i < game.nw; i++)
        SDL_WaitSemaphore(game.done);
    st.on = 0;
}

void port_parallel_display(void (*fn)(void *arg, int part, int thread), void *arg, int n)
{
    if (disp.nw < 0) {
        const char *e = getenv("FREEFALL_THREADS");
        int k = e ? atoi(e) - 1 : SDL_GetNumLogicalCPUCores() - 1;
        pool_start(&disp, k < 3 ? k : 3, "tnfree display");
    }
    pool_run(&disp, fn, arg, n);
}
