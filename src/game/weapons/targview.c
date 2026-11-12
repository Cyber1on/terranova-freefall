/* targview.c -- Terra Nova module M066 (0x292E10-0x2933B8; source file name unknown): stepping
 * the selected target through the list of visible targets.  Decompiled from __FF.EXE v1.09.
 *
 *   0x292E10 targ_cmp        qsort compare by entity id
 *   0x292E20 targ_ok         may this entity be targeted at this distance?
 *   0x292FC4 targview_292FC4 select the next (dir != 0xFF) / previous (dir == 0xFF) target
 *
 * targ_list[targ_count] (10-byte records: distance, 6 bytes, entity) is filled elsewhere.
 * targview_292FC4 sorts it by entity, drops repeated entities and unusable entries (the first
 * entry is kept unchecked, then replaced by the last one while it is unusable), finds the current
 * target (targ_current) and the nearest / farthest entries, then picks the entry next in distance
 * order (ties broken by entity id) and hands it to helm_262D4C.
 * Quirks kept:
 *  - targ_ok checks class 1 type 1 by comparing the type with the class byte.
 *  - the "previous" search compares an equal-distance candidate's distance with the best
 *    distance so far, the "next" search with the best entry's distance; with no best entry yet
 *    (best = -1) the entity comparisons read the word before targ_list (targ_list[-1].ent).
 *  - the previous search starts with best distance 0, the next search with the view range.
 */
#include "g2_a.h"
#include "g7_b.h"     /* added at the G5-G8 merge */

long targ_cmp(TargRec *a, TargRec *b)
{
    return (long)a->ent - (long)b->ent;
}

bool targ_ok(short ent, short dist)
{
    GameEntity *e = &game_entities[ent], *pl;
    uchar cls = e->cls;
    if (cls != 1 && cls != 4 && cls != 2 && cls != 3)
        return 0;
    if (e->cls == 1) {
        uchar t = e->type;
        if (t == 0 || t == 1 || t == 2 || t == 0xD)
            return 0;
    }
    if (e->cls == 3) {
        uchar t = e->type;
        if (t == 0 || t == 1 || t == 7)
            return 0;
    }
    if (e->cls == 2 && e->type == 2)
        return 0;
    if (e->cls == 4) {
        uchar t = e->type;
        if (t != 0x45 && t != 0x46 && t != 0x47)
            return 0;
    }
    if (dist < 0 || dist > *(short *)(targ_view + 8))
        return 0;
    pl = &game_entities[game_player_entity];
    return fix_fast_pyth_dist(e->x - pl->x, e->y - pl->y) <
           *(long *)((uchar *)&game_pba[game_player_slot] + 0xB6) / 6;
}

void targview_292FC4(uchar dir)
{
    TargRec *L = targ_list;
    long cur = -1, best = -1, i, last, kept;
    short cur_dist = -1, min_dist, max_dist = 0, min_i = 0, max_i = 0, bd;

    min_dist = *(short *)(targ_view + 8);
    if (targ_count == 0)
        return;
    lg_qsort(L, targ_count, sizeof(TargRec), WATCOM_FN(targ_cmp));

    /* drop repeated entities and unusable entries (entry 0 is kept here) */
    kept = 0;
    last = 0;
    for (i = 1; i < targ_count; i++) {
        if (L[i].ent == L[last].ent)
            continue;
        if (!targ_ok(L[i].ent, L[i].dist))
            continue;
        if (L[i].dist <= 0 || L[i].dist >= *(short *)(targ_view + 8))
            continue;
        L[++last] = L[i];
        kept++;
    }
    targ_count = kept + 1;
    while (!targ_ok(L[0].ent, L[0].dist)) {
        if (targ_count == 1) {
            targ_count = 0;
            break;
        }
        targ_count--;
        L[0] = L[targ_count];
    }
    if (targ_count == 0)
        return;

    for (i = 0; i < targ_count; i++) {
        short d = L[i].dist;
        if (targ_current == L[i].ent) {
            cur = i;
            cur_dist = d;
        }
        if (min_dist > d) {
            min_dist = d;
            min_i = (short)i;
        } else if (min_dist == d && L[i].ent < L[min_i].ent) {
            min_i = (short)i;
        }
        if (max_dist < d) {
            max_dist = d;
            max_i = (short)i;
        } else if (max_dist == d && L[i].ent > L[max_i].ent) {
            max_i = (short)i;
        }
    }

    if ((signed char)dir == -1) {
        /* previous: the farthest entry nearer than the current one */
        if (cur_dist == -1 || cur_dist < min_dist || min_i == cur) {
            helm_262D4C(L[max_i].ent);
            return;
        }
        bd = 0;
        for (i = 0; i < targ_count; i++) {
            short d = L[i].dist;
            if (d < cur_dist) {
                if (bd < d) {
                    best = i;
                    bd = d;
                } else if (bd == d && L[i].ent > L[best].ent) {
                    best = i;
                }
            } else if (d == cur_dist && L[i].ent < L[cur].ent) {
                if (best == -1) {
                    bd = cur_dist;
                    best = i;
                } else if (bd == d && L[i].ent > L[best].ent) {
                    best = i;
                }
            }
        }
    } else {
        /* next: the nearest entry farther than the current one */
        if (cur_dist == -1 || cur_dist > max_dist || max_i == cur) {
            helm_262D4C(L[min_i].ent);
            return;
        }
        bd = *(short *)(targ_view + 8);
        for (i = 0; i < targ_count; i++) {
            short d = L[i].dist;
            if (d > cur_dist) {
                if (bd > d) {
                    best = i;
                    bd = d;
                } else if (bd == d && L[i].ent < L[best].ent) {
                    best = i;
                }
            } else if (d == cur_dist && L[i].ent > L[cur].ent) {
                if (best == -1) {
                    bd = cur_dist;
                    best = i;
                } else if (d == L[best].dist && L[i].ent < L[best].ent) {
                    best = i;
                }
            }
        }
    }
    if (best == -1)
        return;
    helm_262D4C(L[best].ent);
}
