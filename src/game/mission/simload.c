/* simload.c -- Terra Nova module M051 (source name unknown): loads the sim type tables from
 * ressim.res at startup.  Decompiled from __FF.EXE v1.09.
 *
 *   0x283270 simload_283270
 *
 * Every table is one compound resource whose items are the records of one entity type; each
 * item is extracted into its slot (RefExtract), between a ResLock and a ResUnlock of the
 * resource.  The four subsystem tables are cleared first.
 * Map correction: resource 0x39A goes to game_395728 (class 4 meter values) and resource 0x39C
 * to 0x396404 (game.h's game_cls4_types: blast types of class 8, not class 4).
 */
#include "g9_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar simload_39634E[0x20];
#endif

/* One table: resource id, items, record size, destination, bytes cleared first (0 = none). */
typedef struct {
    ushort id;
    uchar n;
    uchar size;
    void *dst;
    ushort clear;
} SimTab;

static const SimTab simtabs_a[] = {
    { 0x385, 0x12, 3, game_hp_cls1, 0 },
    { 0x386, 0x0C, 3, game_hp_cls2, 0 },
    { 0x387, 0x11, 3, game_hp_cls3, 0 },
    { 0x388, 0x4A, 3, game_hp_cls4, 0 },
};

static const SimTab simtabs_b[] = {
    { 0x38E, 0x12, 0x10, game_sys_cls1, 0x120 },
    { 0x38F, 0x0C, 0x04, game_sys_cls2, 0x30 },
    { 0x390, 0x11, 0x06, game_sys_cls3, 0x66 },
    { 0x391, 0x4A, 0x02, game_sys_cls4, 0x94 },
    { 0x396, 0x3C, 0x03, game_cls0_types, 0 },
    { 0x397, 0x12, 0x46, game_pba_types, 0 },
    { 0x398, 0x0C, 0x36, game_veh_types, 0 },
    { 0x399, 0x11, 0x37, game_cls3_types, 0 },
    { 0x39A, 0x4A, 0x04, game_395728, 0 },
    { 0x39C, 0x13, 0x09, game_cls4_types, 0 },
    { 0x39B, 0x22, 0x24, ord_table, 0 },
};

static void simload_tab(const SimTab *t)
{
    RefTable *rt;
    int i;

    if (t->clear)
        memset(t->dst, 0, t->clear);
    rt = ResLock(t->id);
    for (i = 0; i < t->n; i++)
        RefExtract(rt, MKREF(t->id, i), (uchar *)t->dst + i * t->size);
    ResUnlock(t->id);
}

void simload_283270(void)
{
    static uchar *const skill[4] = { skill_cls1, skill_cls2, skill_cls3, skill_cls4 };
    static schar *const mfd[3] = { mfd_395AD8, mfd_395AEC, mfd_395B00 };
    int fd = ResOpenResFile("ressim.res", 0, 0);
    RefTable *rt;
    unsigned i;

    for (i = 0; i < sizeof(simtabs_a) / sizeof(simtabs_a[0]); i++)
        simload_tab(&simtabs_a[i]);
    rt = ResLock(0x3A7);
    for (i = 0; i < 4; i++)
        RefExtract(rt, MKREF(0x3A7, i + 1), skill[i]);
    ResUnlock(0x3A7);
    for (i = 0; i < sizeof(simtabs_b) / sizeof(simtabs_b[0]); i++)
        simload_tab(&simtabs_b[i]);
    for (i = 0; i < 3; i++) {
        rt = ResLock(0x3A9 + i);
        RefExtract(rt, MKREF(0x3A9 + i, 0), mfd[i]);
        ResUnlock(0x3A9 + i);
    }
    rt = ResLock(0x3A6);
    RefExtract(rt, MKREF(0x3A6, 0), simload_39634E);
    ResUnlock(0x3A6);
    ResCloseFile(fd);
}
