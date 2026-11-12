/* edms_a.h -- helpers for the EDMS interface/collide/phy_tool C (worker edms-a).
 *
 * FIXPP class Q operators are inlined in the original; these reproduce the exact machine code:
 *   a*b  : imul + shrd 16              -> low 32 bits of (int64)a*b >> 16
 *   a/b  : edx = a>>16, eax = a<<16, idiv b  (faults (#DE) on b = 0 or overflow, like the original)
 *   to_int: sar 16
 */
#ifndef TN_EDMS_A_H
#define TN_EDMS_A_H
#include "edms.h"

static inline Q edms_q_mul(Q a, Q b)
{
    return (Q)(((long long)a * b) >> 16);
}

static inline Q edms_q_div(Q a, Q b)
{
    Q r;
    int hi = a >> 16;
    __asm__ volatile("idivl %3" : "=a"(r), "+d"(hi) : "0"((int)((unsigned)a << 16)), "rm"(b) : "cc");
    return r;
}

static inline int edms_q_to_int(Q a)
{
    return a >> 16;
}

/* Hides where a pointer comes from, so gcc cannot reason about array bounds when the original
 * indexes past the end of an array (sanity_check's unbounded object scan, ph2on[-1]...). */
static inline void *edms_launder(void *p)
{
    __asm__("" : "+r"(p));
    return p;
}

/* S[object] / I[object] rows through a laundered base: the original indexes with any int. */
#define EDMS_S_ROW(o) (((EDMS_ObjState *)edms_launder(edms_S))[o])
#define EDMS_I_ROW(o) (((Q (*)[EDMS_DOF_MAX])edms_launder(edms_I))[o])
#define EDMS_PH2ON(ph) (((object_number *)edms_launder(edms_ph2on))[ph])
#define EDMS_ON2PH(on) (((physics_handle *)edms_launder(edms_on2ph))[on])

#endif
