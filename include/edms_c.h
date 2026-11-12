/* edms_c.h -- helpers for the EDMS model C (worker edms-c: marble.c, death.c).
 *
 * FIXPP class Q operators are inlined in the original; these reproduce the machine code:
 *   a*b    : imul + shrd 16                  -> low 32 bits of ((int64)a*b) >> 16
 *   a/b    : edx = a>>16, eax = a<<16, idiv b -> faults (#DE) on b = 0 or overflow, like the original
 *   to_int : sar 16
 *   abs    : CRT abs (0x32510A); abs(INT_MIN) = INT_MIN
 */
#ifndef TN_EDMS_C_H
#define TN_EDMS_C_H
#include "edms.h"

static inline Q edms_c_mul(Q a, Q b)
{
    return (Q)(((long long)a * b) >> 16);
}

static inline Q edms_c_div(Q a, Q b)
{
    Q r;
    int hi = a >> 16;
    __asm__ volatile("idivl %3" : "=a"(r), "+d"(hi) : "0"((int)((unsigned)a << 16)), "rm"(b) : "cc");
    return r;
}

static inline Q edms_c_abs(Q a)
{
    return a < 0 ? -a : a;          /* -fwrapv: abs(INT_MIN) stays INT_MIN, like the CRT */
}

/* Hides where a pointer comes from, so gcc cannot use the array bound (the free-slot search in
 * make_marble/make_death runs past S[95] when all slots are used). */
static inline void *edms_c_launder(void *p)
{
    __asm__("" : "+r"(p));
    return p;
}

/* Game callbacks as the models call them (stored as void * in edms.h). */
typedef void (*edms_collision_fn)(physics_handle caller, physics_handle victim, int badness,
                                  long DATA1, long DATA2, fix *location);
typedef void (*edms_ph_fn)(physics_handle ph);
typedef void (*edms_terrain_event_fn)(physics_handle ph, long DATA1, long DATA2);
#ifdef TN_WATCOM_ABI
#define EDMS_C_PTRARG(p) ((long)(p))    /* WATCOM_CALL takes longs */
#else
#define EDMS_C_PTRARG(p) (p)
#endif

#define EDMS_Q_ONE   0x10000        /* 1.0 */
#define EDMS_Q_MINUS_ONE ((Q)0xFFFF0000)   /* -1.0 */

#endif
