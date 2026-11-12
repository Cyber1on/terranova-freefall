/* mapann.c -- Terra Nova module M069 (0x295180-0x295320; source file name unknown): map
 * annotations (the labelled areas of the tactical map, resource 0xAF items, 0x37 bytes each)
 * and the annotation icon name table.  Decompiled from __FF.EXE v1.09; map names kept.
 *
 *   0x295180 mapann_295180   init the annotation list (LlistInit, nodes of 0x41 bytes)
 *   0x29519C mapann_29519C   reset: destroy and init the list again
 *   0x2951C0 MapAnnIconName  add an icon name and its value to the icon table (no callers)
 *   0x295224 mapann_295224   add a copy of one annotation record, sorted by -y
 *   0x295268 mapann_295268   nearest annotation to (x, y) whose flags match a mask (no callers)
 *
 * Callers: misobj_2C940C (G9c, mission object creation) inits / resets the list and adds
 * every item of resource 0xAF.
 * Quirks kept:
 *  - MapAnnIconName reports "%s too long" without passing the string (the format reads a
 *    stray argument), and checks the table size only after storing (entry 0x1F can be written).
 *  - mapann_295268 tests the mask as a 16-bit value against the byte flags; the distance is
 *    max(dx, dy) + min(dx, dy) / 2 with C (towards zero) halving of signed values.
 */
#include "g5_b.h"
#include "support.h"
#include "t1_c.h"

void mapann_295180(void)
{
    LlistInit(&mapann_list, sizeof(MapAnnNode), 4);
}

void mapann_29519C(void)
{
    LlistDestroy(&mapann_list);
    LlistInit(&mapann_list, sizeof(MapAnnNode), 4);
}

void MapAnnIconName(char *name, long val)
{
    long n;
    if (strlen(name) > 0x1D)
        DbgReportError(1, "MapAnnIconName: %s too long\n");
    name = strpool_dup(name);
    n = mapann_icon_count;
    mapann_icon_vals[n] = val;
    mapann_icon_names[n] = name;
    mapann_icon_count = n + 1;
    if (n + 1 >= 0x1F)
        DbgReportError(1, "MapAnnIconName: too many annotation icon names\n");
}

MapAnnNode *mapann_295224(MapAnn *rec)
{
    MapAnnNode *node = LlistAddQueue(&mapann_list, (short)-((rec->y + 0x8000) >> 16));
    memcpy(&node->ann, rec, sizeof(MapAnn));
    return node;
}

MapAnnNode *mapann_295268(fix x, fix y, ulong mask)
{
    long best = 0x7FFF0000;
    MapAnnNode *found = 0;
    MapAnnNode *node;
    for (node = (MapAnnNode *)mapann_list.head.pnext; (llist *)node != &mapann_list.tail;
         node = (MapAnnNode *)node->q.pnext) {
        long dx, dy, d;
        if (!(node->ann.flags & (ushort)mask))
            continue;
        dx = x - (node->ann.x + node->ann.w / 2);
        if (dx < 0) dx = -dx;
        dy = y - (node->ann.y + node->ann.h / 2);
        if (dy < 0) dy = -dy;
        if (dx > dy)
            d = dx + dy / 2;
        else
            d = dy + dx / 2;
        if (d < best) {
            best = d;
            found = node;
        }
    }
    return found;
}
