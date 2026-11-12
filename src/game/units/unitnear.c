/* unitnear.c -- Terra Nova: lists of objects near a unit (module M042; source name unknown).
 * Decompiled from __FF.EXE v1.09.
 *
 *   0x27D140 damage_27D140  init a NearList (radii, size, buffer; empties it)
 *   0x27D158 damage_27D158  the NearList of a PBA (+0xB2), vehicle (+0x4D) or aircraft (+0x62)
 *   0x27D1E4 damage_27D1E4  fill a NearList with the objects around an entity
 *   0x27D368 damage_27D368  merge several NearLists (each entity once, in id order, dist 0)
 *   0x27D458 damage_27D458  map icon of an entity (through the list for unseen ones)
 *   0x27D540 damage_27D540  the "listed until" time of a PBA / vehicle / aircraft / class-4 unit
 *
 * Quirks kept:
 *  - damage_27D1E4 stores vehicles of type < 4 inside the near/far band without checking the
 *    list size (it can write past buf[max - 1]);
 *  - damage_27D1E4 / damage_27D368 index the entity table and the seen[] array with the
 *    entry's signed id as stored.
 */
#include "g3_c.h"
#include "g2_b.h"      /* game_cls5 (owner g2b) */
#include "g4_a.h"      /* AI callees (owners g4a/g4b/g4c1/g4c2; G4 merge) */
#include "g8_b.h"     /* added at the G5-G8 merge */
#include "g8_c.h"     /* added at the G5-G8 merge */

void damage_27D140(NearList *l, fix near_r, fix far_r, long max, NearEntry *buf)
{
    l->near_r = near_r;
    l->far_r = far_r;
    l->max = max;
    l->buf = buf;
    buf->id = 0;
}

NearList *damage_27D158(long ent)
{
    GameEntity *e = &G3C_ENT(ent);

    switch (e->cls) {
    case 1: return (NearList *)((uchar *)&game_pba[e->slot] + 0xB2);
    case 2: return (NearList *)((uchar *)&game_vehicles[e->slot] + 0x4D);
    case 3: return (NearList *)(game_cls3[e->slot] + 0x62);
    }
    return NULL;
}

long damage_27D1E4(long ent, NearList *l)
{
    long n = 0;
    long off = 0;          /* byte offset into buf (ecx) */
    fix near_r, far_r;
    ulong now;
    short me = (short)ent;

    if ((short)aihelp_2D34B8(0) == 0)
        return 0;
    near_r = l->near_r / 6;
    far_r = l->far_r / 6;
    now = game_time;
    for (;;) {
        short id = (short)aihelp_2D34B8(1);
        GameEntity *e;
        uchar cls;
        fix d;

        if (id == 0)
            break;
        e = &G3C_ENT(id);
        cls = e->cls;
        if (cls == 5 || cls == 6 || cls == 8)
            continue;
        d = fix_fast_pyth_dist(e->x - G3C_ENT(me).x, e->y - G3C_ENT(me).y);
        if (d > far_r)
            continue;
        if (d > near_r && cls != 4) {
            if (cls == 2 && e->type < 4)
                goto store;            /* no size check (quirk) */
            {
                ulong *until = damage_27D540(id);
                if (until) {
                    if (now >= *until)
                        continue;
                    if (G3C_ENT(id).cls == 1 && (schar)mfddata_2665E8(id) >= 0) {
                        uchar *p = (uchar *)&game_pba[G3C_ENT(id).slot];
                        if (*(long *)(p + 0xD) == 6 && p[0x11] != 0)
                            continue;
                    }
                }
            }
        }
        if (n >= l->max - 1)
            break;
store:
        *(short *)((uchar *)l->buf + off) = id;
        off += 6;
        *(short *)((uchar *)l->buf + off - 4) = (short)((d * 6) >> 16);
        n++;
    }
    l->buf[n].id = 0;
    return n;
}

long damage_27D368(NearList *out, NearList **lists, long n)
{
    uchar seen[300];
    long i, cnt;
    short id;

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < n; i++) {
        NearList *l = lists[i];
        NearEntry *p = l->buf;
        long j;
        for (j = 0; j < l->max; j++, p++) {
            if (p->id == 0)
                break;
            seen[p->id] = 1;
        }
    }
    cnt = 0;
    for (id = 1; id < 300; id++) {
        if (!seen[id])
            continue;
        if (cnt >= out->max - 1)
            break;
        out->buf[cnt].id = id;
        out->buf[cnt].dist = 0;
        cnt++;
    }
    out->buf[cnt].id = 0;
    return cnt;
}

long damage_27D458(NearList *l, long ent)
{
    short me = (short)ent;
    uchar cls = G3C_ENT(me).cls;
    NearEntry *p;

    if (game_42015A)
        return 0x36;
    switch (cls) {
    case 1:
        if ((schar)mfddata_2665E8(me) != -1)
            return mapdisp_2769E8(me);
        /* fall through */
    case 2:
    case 3:
        for (p = l->buf;; p++) {
            if (p->id == 0)
                return 0;
            if (p->id == me)
                return mapdisp_2769E8(p->id);
        }
    case 5:
        if ((schar)mfddata_2665E8(*(short *)((uchar *)&game_cls5[G3C_ENT(me).slot] + 6)) == -1)
            return 0x1D;
        return 0;
    }
    return 0;
}

ulong *damage_27D540(long ent)
{
    GameEntity *e = &G3C_ENT(ent);

    switch (e->cls) {
    case 1: return (ulong *)((uchar *)&game_pba[e->slot] + 0xD8);
    case 2: return (ulong *)((uchar *)&game_vehicles[e->slot] + 0x11F);
    case 3: return (ulong *)(game_cls3[e->slot] + 0x146);
    case 4: return (ulong *)(game_cls4[e->slot] + 0x6C);
    }
    return NULL;
}
