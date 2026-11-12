/* aiiter.c -- Terra Nova AI entity iterators (module M114, 0x2D30E0-0x2D367F).
 * Decompiled from __FF.EXE v1.09.
 *
 * The iterators are called with first = 0 to restart (they return -1) and then with first != 0
 * (only the low byte is tested) to get the next matching entity id, 0 at the end.  Each keeps
 * its cursor in its own global (aihelp_cur_*).
 *
 *   0x2D30E0 aihelp_2D30E0  entity of an iterator nearest to (x, y) (0 if none)
 *   0x2D313C aihelp_2D313C  PBAs whose brain is active and on side 2
 *   0x2D31D8 aihelp_2D31D8  PBAs whose brain is active (or alt_think) and not on side 2
 *   0x2D3298 aihelp_2D3298  PBAs with a speaker record and bit 0 of PBA +0xCF set
 *   0x2D3388 aihelp_2D3388  active entities of class 1..4 (entity list)
 *   0x2D3410 aihelp_2D3410  same, without class 1 units whose brain is on side 1
 *   0x2D34B8 aihelp_2D34B8  same as aihelp_2D3388 (own cursor)
 *   0x2D3540 aihelp_2D3540  entries of the 0xC0 x 6 table game_3885D1 up to the first 0
 *   0x2D35A0 aihelp_2D35A0  entries of game_3885D1 of class 1..4
 *   0x2D35FC aihelp_2D35FC  same, without units whose brain is on side 1
 *
 * Lists: the PBA list starts at game_pba[0].f000 and goes through f002; a PBA's entity is f000
 * with bit 15 dropped (sign-extended from bit 14).  The entity list starts at
 * game_entities[0].f05 and goes through f07.
 *
 * Quirks kept:
 *  - the returned ids are 16-bit: the original leaves garbage in the upper half of eax (caller
 *    registers or address arithmetic); ours sign-extends;
 *  - aihelp_2D3298 with a negative speaker count accepts every PBA whose +0xCF bit 0 is set
 *    (the search loop does not run and its index 0 is not -1), and writes the count back;
 *  - aihelp_2D3540 reads the table entry before testing the cursor against 0xC0, and stops
 *    at the first 0 entry without advancing the cursor;
 *  - aihelp_2D35FC applies the brain side test to every class 1..4 entity, not only units;
 *  - aihelp_2D30E0 starts with distance 0x7FFF0000: entities at least that far are ignored.
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_c2.h"

#define PBA_ENT(p)  ((short)((short)(game_pba[p].f000 << 1) >> 1))
#define PBA_NEXT(p) ((short)game_pba[p].f002)
#define ENT_BRAIN(e) (&game_brains[game_entities[e].f21])
#define TAB_ENT(i)  (*(short *)(game_3885D1 + (long)(i) * 6))

short aihelp_2D30E0(fix x, fix y, AihelpIter it)
{
    fix best = 0x7FFF0000;
    short best_e = 0, e;

    WATCOM_CALL(it, 0);
    while ((e = (short)WATCOM_CALL(it, 1)) != 0) {
        fix d = fix_fast_pyth_dist(game_entities[e].x - x, game_entities[e].y - y);
        if (d < best) {
            best = d;
            best_e = e;
        }
    }
    return best_e;
}

long aihelp_2D313C(long first)
{
    short p = aihelp_cur_2D313C;
    if (!(uchar)first) {
        aihelp_cur_2D313C = 0;
        return -1;
    }
    for (p = p ? PBA_NEXT(p) : (short)game_pba[0].f000; p; p = PBA_NEXT(p)) {
        GameBrain *b = ENT_BRAIN(PBA_ENT(p));
        if (b->active && b->f005 == 2) {
            aihelp_cur_2D313C = p;
            return PBA_ENT(p);
        }
    }
    aihelp_cur_2D313C = 0;
    return 0;
}

long aihelp_2D31D8(long first)
{
    short p = aihelp_cur_2D31D8;
    if (!(uchar)first) {
        aihelp_cur_2D31D8 = 0;
        return -1;
    }
    for (p = p ? PBA_NEXT(p) : (short)game_pba[0].f000; p; p = PBA_NEXT(p)) {
        GameBrain *b = ENT_BRAIN(PBA_ENT(p));
        if ((b->active || b->alt_think) && b->f005 != 2) {
            aihelp_cur_2D31D8 = p;
            return PBA_ENT(p);
        }
    }
    aihelp_cur_2D31D8 = 0;
    return 0;
}

long aihelp_2D3298(long start)
{
    short p = aihelp_cur_2D3298;
    long n = game_391E4C;
    if (!(uchar)start) {
        game_391E4C = n;
        aihelp_cur_2D3298 = 0;
        return -1;
    }
    for (p = p ? PBA_NEXT(p) : (short)game_pba[0].f000; p; p = PBA_NEXT(p)) {
        long j = 0;
        if (n > 0) {
            for (; j < n; j++)
                if (PBA_ENT(p) == *(short *)&game_391E50[j].g_02[0])
                    break;
        }
        if (j == n)
            j = -1;
        if (j != -1 && (((uchar *)&game_pba[p])[0xCF] & 1)) {
            game_391E4C = n;
            aihelp_cur_2D3298 = p;
            return PBA_ENT(p);
        }
    }
    game_391E4C = n;
    aihelp_cur_2D3298 = 0;
    return 0;
}

static long ent_iter(short *cur, long first, int mode)
{
    short e = *cur;
    if (!(uchar)first) {
        *cur = 0;
        return -1;
    }
    for (e = e ? (short)game_entities[e].f07 : (short)game_entities[0].f05; e;
         e = (short)game_entities[e].f07) {
        GameEntity *g = &game_entities[e];
        if (!g->active || g->cls < 1 || g->cls > 4)
            continue;
        if (mode && g->cls == 1 && ENT_BRAIN(e)->f005 == 1)
            continue;
        *cur = e;
        return e;
    }
    *cur = 0;
    return 0;
}

long aihelp_2D3388(long first)
{
    return ent_iter(&aihelp_cur_2D3388, first, 0);
}

long aihelp_2D3410(long first)
{
    return ent_iter(&aihelp_cur_2D3410, first, 1);
}

long aihelp_2D34B8(long first)
{
    return ent_iter(&aihelp_cur_2D34B8, first, 0);
}

long aihelp_2D3540(long first)
{
    uchar c = aihelp_cur_2D3540;
    short e;
    if (!(uchar)first) {
        aihelp_cur_2D3540 = 0;
        return -1;
    }
    if (TAB_ENT(c) == 0 || c >= 0xC0) {
        aihelp_cur_2D3540 = c;
        return 0;
    }
    e = TAB_ENT(c);
    aihelp_cur_2D3540 = (uchar)(c + 1);
    return e;
}

static long tab_iter(uchar *cur, long first, int mode)
{
    uchar c = *cur;
    if (!(uchar)first) {
        *cur = 0;
        return -1;
    }
    while (c < 0xC0) {
        short e = TAB_ENT(c);
        uchar cls;
        c++;
        if (e == 0) {
            *cur = c;
            return 0;
        }
        cls = game_entities[e].cls;
        if (cls < 1 || cls > 4)
            continue;
        if (mode && ENT_BRAIN(e)->f005 == 1)
            continue;
        *cur = c;
        return e;
    }
    *cur = c;
    return 0;
}

long aihelp_2D35A0(long first)
{
    return tab_iter(&aihelp_cur_2D35A0, first, 0);
}

long aihelp_2D35FC(long first)
{
    return tab_iter(&aihelp_cur_2D35FC, first, 1);
}
