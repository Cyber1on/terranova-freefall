/* edms_b.h -- extras for the EDMS soliton/ff_ftl decompilation (worker edms-b).
 * Types, globals and prototypes are in edms.h; this only adds the FIXPP operator helpers
 * used by src/edms/soliton.c and src/edms/ff_ftl.c.
 *
 * The original is C++ with the FIXPP class Q, whose operators are all inlined:
 *   Q * Q   imul; shrd eax,edx,16    -> (fix)(((int64)a * b) >> 16)   (truncates toward -inf)
 *   Q / Q   edx = a>>16, eax = a<<16; idiv b -> ((int64)a << 16) / b (truncates toward 0;
 *           faults on b == 0 or quotient overflow, like the original: no check)
 *   to_int  sar 16
 */
#ifndef TN_EDMS_B_H
#define TN_EDMS_B_H
#include "edms.h"

static inline Q qb_mul(Q a, Q b)
{
    return (Q)(((long long)a * b) >> 16);
}

static inline Q qb_div(Q a, Q b)
{
    return (Q)((((long long)a) << 16) / b);
}

static inline int qb_to_int(Q a)
{
    return a >> 16;
}

#endif
