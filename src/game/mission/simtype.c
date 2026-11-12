/* simtype.c -- Terra Nova module M035 (source name unknown; the map called it "sim type data"):
 * nav markers and remote charges per radio channel.  Decompiled from __FF.EXE v1.09; names are
 * the map names other modules call.
 *
 *   0x27ACC0 simtype_27ACC0 init           0x27ADAC simtype_27ADAC set target / mouse position
 *   0x27AE24 simtype_27AE24 place marker   0x27AEEC simtype_27AEEC fire one marker
 *   0x27B144 simtype_27B144 fire a group   0x27B200 simtype_27B200 any marker placed
 *   0x27B240 simtype_27B240 mouse -> world 0x27B298 simtype_27B298 charges needed (channel)
 *   0x27B300 simtype_27B300 charges needed (entity, no caller)
 *   0x27B368 simtype_27B368 free markers   0x27B398 simtype_27B398 in list 5a
 *   0x27B3C8 simtype_27B3C8 add to list 5a 0x27B3F4 simtype_27B3F4 add to list 5b
 *   0x27B420 simtype_27B420 in list 5b     0x27B450 simtype_27B450 fire markers on an entity
 *   0x27B4AC simtype_27B4AC add to list 8  0x27B4D8 simtype_27B4D8 in list 8
 *   0x27B508 simtype_27B508 groups in use (bits)
 *
 * Quirks kept:
 *  - simtype_27AE24 marks the count-1 entries after the new one as used (0xFE) without a
 *    bounds check (a big count runs into the next channel and beyond);
 *  - the free-entry search of simtype_27AE24 looks at entry 10 too, but entry 10 is never
 *    used (index 10 = none);
 *  - simtype_27B144's "same position" flag is kept across channels and starts at 1;
 *  - simtype_27AEEC subtracts the charge count from cmd_39238A[type] only when that is
 *    nonzero (no lower clamp: the byte wraps).
 */
#include "g9_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar mapd_nav[4][0x84];
NavMark nav_sel[4];
short simtype_list8[8];
short simtype_list5b[5];
short simtype_list5a[5];
uchar cmd_39238A[0x4A];
#endif

void simtype_27ACC0(void)
{
    int fd = ResOpenResFile("ressim.res", 0, 0);
    uchar *r = RefGet(MKREF(0x3A8, 0));
    int c, k;

    memset(mapd_nav, 0, sizeof(mapd_nav));
    for (c = 0; c < 4; c++)
        for (k = 0; k < 11; k++)
            NAV(c, k)->colour = -1;
    lg_memset(simtype_list5a, 0xFFFFFFFF, 0, 10);
    lg_memset(simtype_list5b, 0xFFFFFFFF, 0, 10);
    lg_memset(simtype_list8, 0xFFFFFFFF, 0, 0x10);
    for (k = 0; k < 0x4A; k++)
        cmd_39238A[k] = r[k];
    ResCloseFile(fd);
}

/* Mouse position over view widget w -> world x, y (map view data: +0 x0, +4 y0, +0xA scale). */
static void mouse_to_world(HudWidget *w, fix *x, fix *y)
{
    short *m = tnm_state_ptr;
    /* a click in the main view in the frame the map opens there: the view has no map record
     * yet (0), and the original read DOS low memory (docs/BUGS.md G16) */
    uchar *v = TN_LOWMEM(hud_views[w->id].f10);
    short dx = (short)(m[0] - w->x0);
    short dy = (short)(m[1] - w->y0);

    *x = *(fix *)v + dx * *(long *)(v + 0xA);
    *y = *(fix *)(v + 4) + ((long)dy >> (hud_35A544 & 31)) * *(long *)(v + 0xA);
}

void simtype_27ADAC(HudWidget *w, long ent, long chan)
{
    NavMark *s = &nav_sel[(schar)chan];

    s->ent = (short)ent;
    if ((short)ent == 0)
        mouse_to_world(w, &s->x, &s->y);
}

void simtype_27AE24(long a, long b, long chan)
{
    schar c = (schar)chan;
    NavMark *s = &nav_sel[c];
    long k, i;

    s->colour = (schar)b;
    s->count = (uchar)a;
    for (k = 0; NAV(c, k)->colour != -1 && k < 10; k++)
        ;
    if (k == 10)
        return;
    *NAV(c, k) = *s;
    for (i = k + 1; i < (schar)a + k; i++)
        NAV(c, i)->colour = (schar)0xFE;
}

/* Is ent in a list of n shorts? */
static uchar in_list(short *list, int n, short ent)
{
    int i;

    for (i = 0; i < n; i++)
        if (list[i] == ent)
            return 1;
    return 0;
}

/* Put ent into the first free (-1) slot of a list of n shorts. */
static void add_list(short *list, int n, short ent)
{
    int i;

    for (i = 0; i < n; i++)
        if (list[i] == -1) {
            list[i] = ent;
            return;
        }
}

void simtype_27AEEC(long chan, long k, long flag)
{
    schar c = (schar)chan;
    short member = *(short *)&game_391E50[c].g_02[0];   /* +2 member entity */
    NavMark *m = NAV(c, (schar)k);
    short ent, slot, t;

    if (!timers_279E80(&ent, &slot, 0xE))
        return;
    if (m->ent == 0) {
        ENT(ent)->x = m->x;
        ENT(ent)->y = m->y;
        ENT(ent)->z = map_2949C4(m->x, m->y);
    } else {
        ENT(ent)->x = ENT(m->ent)->x;
        ENT(ent)->y = ENT(m->ent)->y;
        ENT(ent)->z = ENT(m->ent)->z;
    }
    *(short *)&game_cls8[slot][6] = member;
    *(short *)&game_cls8[slot][8] = 0;
    game_cls8[slot][0xB] = m->count;
    if ((uchar)flag)
        sfx_28DDD8(ent, 0);
    t = m->ent;
    if (t != 0 && ENT(t)->active && ENT(t)->cls == 4) {
        ENT(ent)->type = 0xF;
        if (in_list(simtype_list5a, 5, t)) {
            if (ENT(t)->cls == 4) {
                instsim_288554(t, 1);
                simtype_27B3F4(t);
            }
        } else if (cmd_39238A[ENT(t)->type] != 0)
            cmd_39238A[ENT(t)->type] -= m->count;
        else
            instsim_288554(t, 1);
    }
    m->colour = (schar)0xFE;
}

void simtype_27B144(long i)
{
    uchar flag = 1;
    fix px = 0, py = 0;
    int c, k;

    for (c = 0; c < 4; c++)
        for (k = 0; k < 10; k++) {
            NavMark *m = NAV(c, k);
            if (m->colour != (schar)i)
                continue;
            simtype_27AEEC(c, k, flag);
            flag = !(m->x == px && m->y == py);
            px = m->x;
            py = m->y;
        }
}

uchar simtype_27B200(void)
{
    int c, k;

    for (c = 0; c < 4; c++)
        for (k = 0; k < 10; k++)
            if (NAV(c, k)->colour != -1 && NAV(c, k)->colour != -2)
                return 1;
    return 0;
}

void simtype_27B240(void *w, fix *x, fix *y)
{
    mouse_to_world(w, x, y);
}

/* Charges needed against an entity: 1000 hp each for the ones in list 5a, else 200. */
static long charges_for(short ent)
{
    if (simtype_27B398(ent))
        return ((long)objdmg_get_hp(ent) + 1000) / 1000;
    return ((long)objdmg_get_hp(ent) + 200) / 200;
}

long simtype_27B298(long ent)
{
    short e = nav_sel[(schar)ent].ent;

    if (e == 0)
        return 1;
    return charges_for(e);
}

long simtype_27B300(long ent)
{
    return charges_for((short)ent);
}

schar simtype_27B368(long chan)
{
    schar n = 0;
    int k;

    for (k = 0; k < 10; k++)
        if (NAV((schar)chan, k)->colour == -1)
            n++;
    return n;
}

uchar simtype_27B398(long ent)
{
    return in_list(simtype_list5a, 5, (short)ent);
}

void simtype_27B3C8(long ent)
{
    add_list(simtype_list5a, 5, (short)ent);
}

void simtype_27B3F4(long ent)
{
    add_list(simtype_list5b, 5, (short)ent);
}

uchar simtype_27B420(long ent)
{
    return in_list(simtype_list5b, 5, (short)ent);
}

void simtype_27B450(long ent)
{
    uchar flag = 1;
    int c, k;

    for (c = 0; c < 4; c++)
        for (k = 0; k < 10; k++)
            if (NAV(c, k)->ent == (short)ent) {
                simtype_27AEEC(c, k, flag);
                flag = 0;
            }
}

void simtype_27B4AC(long ent)
{
    add_list(simtype_list8, 8, (short)ent);
}

uchar simtype_27B4D8(long ent)
{
    return in_list(simtype_list8, 8, (short)ent);
}

uchar simtype_27B508(void *p)
{
    uchar bits = 0;
    int c, k;

    (void)p;
    for (c = 0; c < 4; c++)
        for (k = 0; k < 10; k++)
            switch ((uchar)NAV(c, k)->colour) {
            case 0: bits |= 1; break;
            case 1: bits |= 2; break;
            case 2: bits |= 4; break;
            }
    return bits;
}
