/* classdesc.c -- Terra Nova module M070 (source name unknown): the object class descriptor
 * table and a small id counter.  Decompiled from __FF.EXE v1.09; names ours.
 *
 *   0x295320 game_pools (data in the code segment, 9 x 8 bytes; GamePool, game.h)
 *   0x295368 objid_reset      0x29537C objid_next      0x2953DC pool_2953DC
 *
 * game_pools[cls] = {record count, record size, table} for each entity class (the grid
 * and interaction code walks it).  The map listed the table as a function (pool_295320) and
 * missed the two functions after it (no references: dead code).
 * objid_next hands out ids {index 0..0x1FF, page} from objid_index / objid_page (0x3BA980,
 * 0x3BA984, the dwords just after class 0's 256 9-byte records) until page 0x200.
 */
#include "g2_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
long objid_index, objid_page;
/* (this file called the table obj_class_desc; one name since the G2 merge) */
GamePool game_pools[9] = {
    { 0x100, 0x009, (uchar *)0x3BA080 },     /* 0 */
    {  0x3C, 0x582, (uchar *)0x3A5608 },     /* 1 PBA / soldier (game_pba) */
    {  0x34, 0x128, (uchar *)0x3A19E8 },     /* 2 vehicle (game_vehicles) */
    {  0x28, 0x14F, (uchar *)0x39E590 },     /* 3 */
    {  0x40, 0x079, (uchar *)0x39C750 },     /* 4 */
    {  0x50, 0x01E, (uchar *)0x39BDF0 },     /* 5 */
    {  0x80, 0x02C, (uchar *)0x39A530 },     /* 6 */
    {  0x20, 0x00A, (uchar *)0x39BCB0 },     /* 7 */
    {  0x20, 0x00C, (uchar *)0x39BB30 },     /* 8 */
};
#endif

/* unreferenced */
void objid_reset(void)
{
    objid_page = 0;
    objid_index = 0;
}

/* unreferenced */
uchar objid_next(short *id)
{
    long i;
    if (objid_page == 0x200)
        return 0;
    i = objid_index;
    id[0] = (short)objid_index;
    objid_index = i + 1;
    id[1] = (short)objid_page;
    if (i + 1 == 0x200) {
        objid_index = 0;
        objid_page++;
    }
    return 1;
}

void pool_2953DC(uchar *id)
{
    id[0] = 0;
    id[1] = 0;
}
