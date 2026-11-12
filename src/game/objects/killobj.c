/* killobj.c -- Terra Nova deferred object destruction (module M056; source name unknown, the
 * binary's own string names KillObjMark).  Decompiled from __FF.EXE v1.09.
 *
 *   0x2892D0 killobj_2892D0  KillObjInit: reset the callback lists and the kill list
 *   0x28934C killobj_28934C  KillObjProcess: destroy every marked object
 *   0x2894A0 KillObjMark     mark an object for destruction (reason 1 ordnance, 2 strike, 3 ...)
 *   0x28952C killobj_28952C  add a per-class kill callback
 *   0x289554 killobj_289554  add a per-object kill callback (only for active entities)
 *   0x289588 killobj_289588  remove a per-object kill callback
 *
 * A kill callback is a Watcom fn(obj, reason) called when the bit `reason` of its mask is set.
 * Per-object callbacks are freed after the object is processed (whatever the mask).
 * Quirks kept:
 *  - KillObjMark does not stop at a full list: it reports "couldn't grow list!" and appends
 *    anyway (past killobj_max);
 *  - KillObjProcess calls per-class callbacks only when fn is non-NULL, per-object callbacks
 *    without that test;
 *  - killobj_289554 / killobj_289588 return garbage in eax (void here).
 */
#include "g2_c.h"
#include "g2_a.h"      /* grid_295730 (owner g2a) */
#include "g3_a.h"      /* G3a callees (owner g3a) */

#ifndef TN_LINK_ORIGINAL_DATA
KillEntry killobj_buf[KILLOBJ_BUF_MAX];
LlistHead killobj_class_cbs[9];
long killobj_count;
long killobj_max;
KillEntry *killobj_list;
LlistHead killobj_obj_cbs;
#endif

void killobj_2892D0(void)
{
    uchar c;

    LlistDestroy(&killobj_obj_cbs);
    LlistInit(&killobj_obj_cbs, 0x12, 0x20);
    for (c = 0; c < 9; c++) {
        LlistDestroy(&killobj_class_cbs[c]);
        LlistInit(&killobj_class_cbs[c], 0x12, 0x10);
    }
    if (killobj_list == NULL) {
        killobj_max = KILLOBJ_BUF_MAX;
        killobj_list = killobj_buf;
    }
    killobj_count = 0;
}

void killobj_28934C(void)
{
    KillEntry *e;

    /* list and count are re-read every iteration (callbacks may mark more objects) */
    for (e = killobj_list; e < killobj_list + killobj_count; e++) {
        GameEntity *ent = &G2C_ENT(e->obj);
        llist *end, *n, *next;
        short obj;

        if (ent->f23)
            ordnance_291754(ent->f23);
        ordnance_291754((ushort)(G2C_ENT(e->obj).cls | 0x100));

        end = &killobj_class_cbs[G2C_ENT(e->obj).cls].tail;
        for (n = end[-1].pnext; n != end; n = next) {
            KillCb *cb = (KillCb *)n;
            next = n->pnext;
            if (cb->fn && (cb->mask & (1u << (e->reason & 31))))
                WATCOM_CALL(cb->fn, (long)e->obj, (long)e->reason);
        }

        end = &killobj_obj_cbs.tail;
        for (n = killobj_obj_cbs.head.pnext; n != end; n = next) {
            KillCb *cb = (KillCb *)n;
            next = n->pnext;
            if (cb->obj == e->obj) {
                if (cb->mask & (1u << (e->reason & 31)))
                    WATCOM_CALL(cb->fn, (long)e->obj, (long)e->reason);
                LlistFree(&killobj_obj_cbs, cb);
            }
        }

        if (G2C_ENT(e->obj).body)
            physics_2D3F20(e->obj);
        if (G2C_ENT(e->obj).f21 && G2C_ENT(e->obj).type)
            music_246BD0(e->obj);
        static_288040(e->obj);
        physics_2D65F8(e->obj);
        obj = e->obj;
        grid_295730(obj);
    }
    killobj_count = 0;
}

void KillObjMark(long obj, long reason)
{
    KillEntry *l = killobj_list, *e;
    long n = killobj_count;

    for (e = l; e < l + n; e++)
        if (e->obj == (short)obj) {
            killobj_list = l;
            return;
        }
    killobj_list = l;
    if (killobj_count == killobj_max)
        DbgReportError(1, "KillObjMark: couldn't grow list!\n");
    e = &killobj_list[killobj_count++];
    e->obj = (short)obj;
    e->reason = (uchar)reason;
}

#ifndef TN_WATCOM_ABI
/* "Tree explosion" (sfx.c fx_death): obj goes at the end of the frame */
int port_killobj_marked(long obj)
{
    long i;
    for (i = 0; i < killobj_count; i++)
        if (killobj_list[i].obj == (short)obj)
            return 1;
    return 0;
}
#endif

void *killobj_28952C(long cls, void *fn, ulong mask)
{
    KillCb *cb = LlistAddHead(&killobj_class_cbs[(uchar)cls]);
    cb->mask = mask;
    cb->fn = fn;
    return cb;
}

void killobj_289554(long obj, void *fn, long mask)
{
    if (G2C_ENT(obj).active) {
        KillCb *cb = LlistAddHead(&killobj_obj_cbs);
        cb->obj = (short)obj;
        cb->mask = (ulong)mask;
        cb->fn = fn;
    }
}

void killobj_289588(long obj, void *fn)
{
    llist *n;

    for (n = killobj_obj_cbs.head.pnext; n != &killobj_obj_cbs.tail; n = n->pnext) {
        KillCb *cb = (KillCb *)n;
        if (cb->obj == (short)obj && cb->fn == fn) {
            LlistFree(&killobj_obj_cbs, cb);
            return;
        }
    }
}
