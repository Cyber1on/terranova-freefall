/* partgroup.c -- Terra Nova multi-part object groups (module M039; source name unknown).
 * Decompiled from __FF.EXE v1.09; names are the map's placeholders.
 *
 *   0x27C3F0 damage_27C3F0  create a group (class 7) and its parts (class 4) from a layout
 *                           resource item; returns the group entity or 0
 *   0x27C5A4 damage_27C5A4  place the parts of a group on the map (grid footprints)
 *   0x27C70C damage_27C70C  unlink one part from its group (optionally kill it) -- no callers
 *   0x27C7CC damage_27C7CC  kill every part of a group and remove the group -- no callers
 *
 * A group resource item is a list of 0x12-byte PartItems {kind, x, y, z offsets, heading}.
 * The group's class 7 record holds the part count (+6) and the last part's class 4 slot (+8);
 * the parts form a circular list through class 4 record +8 (each part points to the one created
 * before it; the first part points to the last).
 */
#include "g3_b.h"
#include "g2_a.h"      /* grid_29582C (owner g2a) */
#include "g5_a.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
PartGroup game_cls7[32];
#endif

short damage_27C3F0(Ref key, fix x, fix y, ushort heading)
{
    short grp, gslot, part, pslot;
    short first = 0, prev = 0;   /* QUIRK: the original reads these uninitialized when the
                                    item has no parts (we use 0) */
    RefTable *t;
    PartItem *it;
    long count, i;

    if (!grid_295588(7, &grp, &gslot))
        return 0;
    G3B_ENT(grp).type = (uchar)key;
    G3B_ENT(grp).f23 = 0;
    G3B_ENT(grp).active = 1;
    t = (RefTable *)ResLock(REFID(key));
    count = (ulong)(t->offset[REFINDEX(key) + 1] - t->offset[REFINDEX(key)]) / sizeof(PartItem);
    it = (PartItem *)RefGet(key);
    for (i = 0; i < count; i++, it++) {
        if (!damage_27CF30(&part, &pslot, 0, it->kind, game_35B610, 0)) {
            ResUnlock(REFID(key));
            return 0;
        }
        /* QUIRK: the owner group is stored in the record of the GROUP's slot number in the
         * class 4 table (not in the part's record), every iteration. */
        CLS4_W(gslot, 6) = grp;
        if (i == 0)
            first = pslot;
        else
            CLS4_W(pslot, 8) = prev;
        prev = pslot;
    }
    CLS4_W(first, 8) = prev;
    game_cls7[gslot].count = (short)count;
    game_cls7[gslot].last = prev;
    damage_27C5A4(grp, key, x, y, heading);
    ResUnlock(REFID(key));
    return grp;
}

void damage_27C5A4(short grp, Ref key, fix x, fix y, ushort unused)
{
    ObjFoot foot;
    RefTable *t;
    PartItem *it;
    ulong n;
    short p;
    fix h;

    t = (RefTable *)ResLock(REFID(key));
    it = (PartItem *)RefGet(key);
    n = (ulong)(t->offset[REFINDEX(key) + 1] - t->offset[REFINDEX(key)]) / sizeof(PartItem);
    it += n - 1;                 /* the last part was created last: walk backwards */
    p = game_cls7[G3B_ENT(grp).slot].last;
    h = map_2949C4(x + it->x, y + it->y);
    while (n-- != 0) {
        memset(&foot, 0, sizeof(foot));
        foot.obj = G2C_IDX15(CLS4_W(p, 0));
        foot.x = x + it->x;
        foot.y = y + it->y;
        foot.z = h + it->z;
        foot.heading = it->heading;
        interact_297400(&foot, G3B_ENT(foot.obj).cls, G3B_ENT(foot.obj).type);
        it--;
        grid_29582C(&foot);
        p = CLS4_W(p, 8);
    }
    ResUnlock(REFID(key));
}

void damage_27C70C(short ent, short slot, uchar kill)
{
    short prev = slot, n;
    PartGroup *g;

    /* find the part that points to this one (loops forever if it is not in a cycle) */
    while ((n = CLS4_W(prev, 8)) != slot)
        prev = n;
    CLS4_W(prev, 8) = CLS4_W(slot, 8);
    g = &game_cls7[G3B_ENT(ent).slot];
    g->count--;
    if (g->last == slot)
        g->last = prev;
    if (kill)
        KillObjMark(G2C_IDX15(CLS4_W(slot, 0)), 2);
}

void damage_27C7CC(short ent)
{
    short g = G3B_ENT(ent).slot;
    short p = game_cls7[g].last;

    do {
        KillObjMark(G2C_IDX15(CLS4_W(p, 0)), 2);
        p = CLS4_W(p, 8);
    } while (p != game_cls7[g].last);
    grid_295730(ent);
}
