/* wpnfire.c -- Terra Nova special weapon use (module M065; source name unknown; the binary
 * names WeaponFire).  Decompiled from __FF.EXE v1.09; other names are the map's.
 *
 *   0x292180 fire_292180  show / hide the weapon display (bit 1 of 0x35ADE0 + 1)
 *   0x2921A8 fire_2921A8  weapon reset + sound 0x7E1 + HUD refresh
 *   0x2921F0 fire_2921F0  radio acknowledgement 0x52 + n
 *   0x292200 fire_292200  radio acknowledgement 0x5D + n
 *   0x292210 fire_292210  radio acknowledgement 0x6A + n
 *   0x292248 WeaponFire   use the selected special weapon of a PBA
 *   0x292970 fire_292970  select / deselect it (timed and energy weapons)
 *   0x292A68 fire_292A68  per-frame drain of a selected timed / energy / designator weapon
 *   0x292C2C fire_292C2C  lose charges (damage)
 *   0x292C98 fire_292C98  TRUE when no other object is within 0.5 of (x, y) (mine placing)
 * Data: 0x292220 the WeaponFire jump table (types 2..11).
 *
 * Weapon block (PbaWeapon, PBA record + 9): count, type, sel, ready; the PBA record also holds
 * the reload records (pointer at +0x9C, count word at +0xA0; 5 bytes {?, ushort max, ushort cur})
 * and the energy meter (ObjMeter at +0x86).
 * Types: 2 reload (first record + max/4), 5 reload all (others + max/5), 6 energy weapon,
 * 7 / 9 mines (9 on mission 0x2F refills to 10 and clears type-0x2E objects), 8 timed (one
 * charge per 20 time units), 10 air strike call, 11 designator.
 */
#include "g2_c.h"
#include "g2_a.h"      /* grid (owner g2a) */
#include "g2_b.h"      /* game_cls5, game_cls4 (owner g2b) */
#include "g1_c.h"      /* game_rnd_text (owner g1c) */
#include "g3_a.h"      /* G3a callees (owner g3a) */
#include "g4_a.h"      /* AI callees (owners g4a/g4b/g4c1/g4c2; G4 merge) */
#include "g6_b.h"     /* added at the G5-G8 merge */
#include "g7_a.h"     /* added at the G5-G8 merge */
#include "g7_c.h"     /* added at the G5-G8 merge */
#include "g8_b.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
ulong fire_next_time;
short fire_397C9C;
uchar fire_35BF30;
uchar fire_35BF68;
short fire_35BF6A;
ulong fire_35BF6C;
ulong fire_35BF70, fire_35BF74, fire_35BF78, fire_35BF7C;
#endif

#define IS_PLAYER(o) ((short)(o) == (short)game_player_entity)
#define PBA_OF(o) ((uchar *)&game_pba[G2C_ENT(o).slot])

void fire_292180(void)
{
    long v = !((uchar)(game_35ADE0 + 1) & 2);
    hud_26B970(v);
    hview3d_274510(v);
}

void fire_2921A8(void)
{
    if (game_359708 == 0x2E)
        return;
    weapons_290858();
    SndPlay(0x7E1, 0, 0x40, 1, -1, 0x40, 0);
    if (game_camera_38FB20)
        hud_26AF24();
    else
        hud_26AE98();
}

void fire_2921F0(long n)
{
    RadioAck((short)((n & 0xFF) + 0x52));
}

void fire_292200(long n)
{
    RadioAck((short)((n & 0xFF) + 0x5D));
}

void fire_292210(long n)
{
    RadioAck((short)((n & 0xFF) + 0x6A));
}

/* lay a mine (types 7 and 9): create it in front of obj */
static uchar fire_lay_mine(short obj, long kind, uchar *desc, long *l2, uchar ack1, uchar ack2,
                           PbaWeapon *w)
{
    ObjFoot fp;
    long mine = 0;
    fix nx = 0, ny = 0;

    memset(&fp, 0, sizeof(fp));
    if (!damage_27CF30(&mine, l2, 0, kind, desc, obj))
        return 0;
    fp.obj = (short)mine;
    memcpy(&fp.x, &G2C_ENT(obj).x, 0x12);
    path_25BE1C(0x10000, 0, fp.heading, fp.x, fp.y, &nx, &ny);
    fp.x = nx;
    fp.y = ny;
    interact_297400(&fp, G2C_ENT(fp.obj).cls, G2C_ENT(fp.obj).type);
    grid_29582C(&fp);
    physics_2D43DC((short)mine);
    if (IS_PLAYER(obj)) {
        RadioAck(ack1);
        if (w->count == 1)
            RadioAck(ack2);
    }
    return 1;
}

uchar WeaponFire(long obj)
{
    short o = (short)obj;
    uchar *pba = PBA_OF(o);
    PbaWeapon *w = (PbaWeapon *)(pba + 9);

    if (!w->count || !w->ready)
        return 0;
    if (IS_PLAYER(o) && game_time < fire_next_time)
        return 0;
    if (w->count <= 1 && w->type == 9 && game_359708 == 0x2F) {
        short i;
        ushort flag;
        w->count = 10;
        i = G2C_IDX15(*(short *)game_cls4[0]);
        flag = *(ushort *)game_cls4[i] >> 15;
        while (i) {
            short e = G2C_IDX15(*(short *)game_cls4[i]);
            if (!flag && G2C_ENT(e).active && G2C_ENT(e).type == 0x2E)
                KillObjMark(e, 3);
            i = *(short *)&game_cls4[i][2];
            flag = *(ushort *)game_cls4[i] >> 15;
        }
    }
    switch (w->type) {
    case 7: {
        uchar d[7] = {0, 100, 100, 100, 0, 0, 0};
        long l = 0;
        if (!fire_lay_mine(o, 0x2F, d, &l, 0x59, 0x71, w))
            return 0;
        break;
    }
    case 9: {
        uchar d[7] = {0, 100, 100, 100, 6, 0, 0};
        long l = 0;
        if (!fire_lay_mine(o, 0x2E, d, &l, 0x5B, 0x73, w))
            return 0;
        break;
    }
    case 2: {
        uchar *r = *(uchar **)(pba + 0x9C);
        ushort max = *(ushort *)(r + 1), cur = *(ushort *)(r + 3);
        long v;
        if (cur == max)
            return 0;
        v = (max >> 2) + cur;
        *(ushort *)(r + 3) = (ushort)(max < v ? max : v);
        if (IS_PLAYER(o))
            RadioAck(0x54);
        break;
    }
    case 5: {
        uchar *r = *(uchar **)(pba + 0x9C);
        long i;
        int need = 0;
        for (i = 0; i < *(ushort *)(pba + 0xA0); i++)
            if (i && *(ushort *)(r + i * 5 + 3) < *(ushort *)(r + i * 5 + 1)) {
                need = 1;
                break;
            }
        if (!need)
            return 0;
        for (i = 0; i < *(ushort *)(pba + 0xA0); i++)
            if (i) {
                uchar *p = r + i * 5;
                long max = *(ushort *)(p + 1);
                long v = max / 5 + *(ushort *)(p + 3);
                *(ushort *)(p + 3) = (ushort)(v >= max ? max : v);
            }
        if (IS_PLAYER(o))
            RadioAck(0x57);
        break;
    }
    case 10:
        if (game_396868)
            return 0;
        if (!aircraft_2837B4(1, o))
            return 0;
        fire_35BF68 = 1;
        if (IS_PLAYER(o)) {
            game_3968B5 = 1;
            hudview_26C224(2, 0x13);
            hudview_26C598(0x13);
            hudview_26C5B4(0x10);
        }
        if (IS_PLAYER(o))
            RadioAck(0x5C);
        break;
    case 11: {
        ObjFoot fp;
        uchar d[3] = {0, 0, 0};
        long des = 0, l = 0;
        memset(&fp, 0, sizeof(fp));
        SndPlay(0xAD6, 4, 0xFF, 1, 0x46, 0x40, 0);
        fire_397C9C = o;
        if (!damage_27D610((short *)&des, (short *)&l, 0x35, d))
            return 0;
        fp.obj = (short)des;
        fire_35BF6A = (short)des;
        memcpy(&fp.x, &G2C_ENT(o).x, 0x12);
        fp.z = aircraft_284A6C(o, G2C_ENT(o).x, G2C_ENT(o).y);
        interact_297400(&fp, G2C_ENT(fp.obj).cls, G2C_ENT(fp.obj).type);
        grid_29582C(&fp);
        fire_292970(o, 1);
        fire_35BF6C = game_time + 0x270F;
        fire_35BF30 = 0;
        break;
    }
    }
    w->count--;
    game_389E4C[0x20] |= 0x20;
    if (IS_PLAYER(o))
        fire_next_time = game_time + 0x2EE;
    return 1;
}

uchar fire_292970(long obj, long sel)
{
    short o = (short)obj;
    PbaWeapon *w = (PbaWeapon *)(PBA_OF(o) + 9);
    uchar s = (uchar)sel;

    if ((long)s == (long)(schar)w->sel)
        return 1;
    if (s && (!w->count || !w->ready))
        return 0;
    if (w->type == 8) {
        if (s) {
            fire_35BF70 = 0;
            fire_35BF74 = 0;
            if (IS_PLAYER(o))
                RadioAck(0x5A);
        } else if (IS_PLAYER(o))
            RadioAck(0x65);
    } else if (w->type == 6) {
        if (s) {
            fire_35BF78 = 0;
            fire_35BF7C = 0;
            if (IS_PLAYER(o))
                RadioAck(0x58);
        } else if (IS_PLAYER(o))
            RadioAck(0x63);
    }
    w->sel = s;
    hudview_26C5F4(0xC);
    return 1;
}

void fire_292A68(long obj)
{
    short o = (short)obj;
    uchar *pba = PBA_OF(o);
    PbaWeapon *w = (PbaWeapon *)(pba + 9);

    if (!w->sel)
        return;
    switch (w->type) {
    case 8: {
        ulong el = fire_35BF70 ? game_time - fire_35BF70 + fire_35BF74 : 0;
        short q = (short)(el / 20);
        fire_35BF74 = el - q * 20;
        if (q < w->count)
            w->count -= q;
        else
            w->count = 0;
        if (w->count <= 0)
            fire_292970(o, 0);
        fire_35BF70 = game_time;
        hudview_26C5F4(0xC);
        break;
    }
    case 6: {
        long el = fire_35BF78 ? (long)(game_time - fire_35BF78 + fire_35BF7C) : 0;
        long t = el / 10;
        ObjMeter *m = (ObjMeter *)(pba + 0x86);
        objacc_28B854(m, t << 16, 0, 1);
        if (objacc_28B870(m) <= 0)
            fire_292970(o, 0);
        fire_35BF78 = game_time;
        fire_35BF7C = el - t * 10;
        hudview_26C5F4(0xC);
        break;
    }
    case 0xB:
        hudview_26C5F4(0xC);
        if (fire_35BF6A && flash_28E350(fire_35BF6A, 5))
            fire_292970(o, 0);
        break;
    }
}

void fire_292C2C(long amount, long obj)
{
    PbaWeapon *w = (PbaWeapon *)(PBA_OF((short)obj) + 9);
    short h;

    if (!amount)
        return;
    h = (short)(amount / 2);
    if (h == 0)
        h = 1;
    if (h < w->count) {
        w->count -= h;
        return;
    }
    w->count = 0;
    fire_292970((short)obj, 0);
}

uchar fire_292C98(long obj, fix x, fix y)
{
    long cx, cy;
    long x0 = x >> 16, y0 = y >> 16;

    for (cx = x0 - 1; cx <= x0 + 1; cx++)
        for (cy = y0 - 1; cy <= y0 + 1; cy++) {
            short key[2];
            short b, i;
            key[0] = (short)cx;
            key[1] = (short)cy;
            grid_hash_cur = (short)((((ushort)(cx * 4 + cy)) & 0x1FF) + 0x200);
            if (grid_hash[grid_hash_cur].sq == 0)
                b = 0;
            else if (*(ulong *)&grid_squares[(short)grid_hash[grid_hash_cur].sq] == *(ulong *)key)
                b = grid_hash_cur;
            else
                b = (short)grid_295CFC(*(GridXY *)key, 0, grid_hash_cur);
            for (i = (short)grid_hash[b].sq; i; i = grid_squares[i].next) {
                short e = grid_squares[i].ent;
                if (e != (short)obj &&
                    fix_fast_pyth_dist(x - G2C_ENT(e).x, y - G2C_ENT(e).y) < 0x8000)
                    return 0;
            }
        }
    return 1;
}
