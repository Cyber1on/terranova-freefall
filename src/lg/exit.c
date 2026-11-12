/* exit.c -- LG exit-function list (x:\prj\tech\libsrc\lg; no System Shock counterpart).
 * Decompiled from __FF.EXE (Terra Nova v1.09).
 *
 *   0x2DE815 AtExitRun   0x2DE841 AtExit
 *
 * AtExit installs AtExitRun into the Watcom CRT exit hook (crt_exit_hook, called by exit())
 * and appends the function to a 32-entry list; AtExitRun calls the list newest first.
 */
#include "sup_a.h"

#define ATEXIT_MAX 32

#ifndef TN_LINK_ORIGINAL_DATA
int atexit_count;
void *atexit_funcs[ATEXIT_MAX];
void *crt_exit_hook;
#endif

/* Runs the exit functions in reverse order of registration.  The count is set to the maximum
 * first, so AtExit calls made from an exit function fail instead of growing the list.
 * Each function is called with its own address in eax (the original does `call eax`). */
void AtExitRun(void)
{
    int n = atexit_count;

    atexit_count = ATEXIT_MAX;
    while (n > 0) {
        void *f = atexit_funcs[--n];
        WATCOM_CALL(f, (long)f);
    }
}

/* Adds func to the exit list.  Returns 0, or -1 if the list is full.  The CRT hook is set
 * on every call, even when the list is full. */
int AtExit(void *func)
{
    crt_exit_hook = WATCOM_FN(AtExitRun);
    if (atexit_count >= ATEXIT_MAX)
        return -1;
    atexit_funcs[atexit_count++] = func;
    return 0;
}
