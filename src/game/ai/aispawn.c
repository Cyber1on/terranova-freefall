/* aispawn.c -- Terra Nova timed mission spawns (module M112, 0x2D2BE0-0x2D3005).
 * Decompiled from __FF.EXE v1.09.
 *
 * game_442F44 points at the mission's spawn table: a dword bit mask (bit i = spawn i in use),
 * then 0x32-byte records from +4 (+0 kind, +6 a bit index tested against game_442420, +7 the
 * squad to put there, +8/+0xC 16.16 position, +0x1B a byte set from game_44127A for kind 1).
 * Spawn i appears at region centre game_4413D0[i] whenever its mission timer (game_442F40, G4c1)
 * runs out, once the square is free and the player cannot see it.
 *
 *   0x2D2BE0 aihelp_2D2BE0  can spawn i appear now? (square free of anything but class 4/6
 *                           objects, no unit within 5, the carrier not within 5, and not in
 *                           the camera's view cone when the camera entity is within 15)
 *   0x2D2E9C aihelp_2D2E9C  place spawn i: set its position and send its squad there
 *   0x2D2F5C aihelp_2D2F5C  per frame (dt in ms): count the spawn timers down and fire them
 *
 * Quirks kept:
 *  - aihelp_2D2BE0 looks the square up in the spatial grid as grid_debug_key does: an empty
 *    bucket uses hash entry 0 (the free-list header) as the chain;
 *  - the view-cone test compares (angle - (heading - fov / 2)) with the full fov as unsigned
 *    16-bit numbers, fov being the camera's culling half-angle word;
 *  - aihelp_2D2F5C reloads both table pointers for every bit and leaves the timer at 0 when
 *    the spawn could not appear (so it is retried every frame).
 */
#include "g4_a.h"      /* the AI header: includes g4_b.h, g4_c1.h, g4_c2.h */
#include "g4_c2.h"
#include "g9_c.h"     /* added at the G9/G10 merge */

#define SPAWN(i) (game_442F44 + 4 + (long)(i) * 0x32)
#define CAM_ENT  (*(short *)(game_camera + GAME_CAM_BC))     /* 0x38FBE0 */
#define CAM_FOV  (*(ushort *)(game_camera + GAME_CAM_CULL_HALF)) /* 0x38FB3C */
#define REGION_X(i) (*(fix *)(game_4413D0 + (long)(i) * 8))
#define REGION_Y(i) (*(fix *)(game_4413D0 + (long)(i) * 8 + 4))

static fix ent_dist(short e, fix x, fix y)
{
    return fix_fast_pyth_dist(game_entities[e].x - x, game_entities[e].y - y);
}

uchar aihelp_2D2BE0(long i)
{
    fix x, y;
    GridXY key;
    short e, s;
    fixang a;

    if (game_442F44 == NULL || !(*(ulong *)game_442F44 & (1UL << (i & 31))))
        return 0;
    if (!((1UL << (SPAWN(i)[6] & 31)) & game_442420))
        return 0;
    x = REGION_X(i);
    y = REGION_Y(i);
    key.x = (short)(x >> 16);
    key.y = (short)(y >> 16);
    grid_hash_cur = GRID_HASH(key.x, key.y);
    if (grid_hash[grid_hash_cur].sq == 0)
        e = 0;
    else if (*(ulong *)&grid_squares[grid_hash[grid_hash_cur].sq] == *(ulong *)&key)
        e = grid_hash_cur;
    else
        e = grid_295CFC(key, 0, grid_hash_cur);
    for (s = grid_hash[e].sq; s; s = grid_squares[s].next) {
        short ent = grid_squares[s].ent;
        uchar cls = game_entities[ent].cls;
        if (cls != 6 && cls != 4)
            return 0;
        if (cls == 4 && instsim_288260(game_entities[ent].type))
            return 0;
    }
    e = aihelp_2D30E0(x, y, (AihelpIter)WATCOM_FN(aihelp_2D31D8));
    if (ent_dist(e, x, y) < 0x50000)
        return 0;
    if (game_396868 != 0 && ent_dist(game_396868, x, y) < 0x50000)
        return 0;
    if (ent_dist(CAM_ENT, x, y) >= 0xF0000)
        return 1;
    a = fix_atan2(y - game_entities[CAM_ENT].y, x - game_entities[CAM_ENT].x);
    if ((ushort)(a - (ushort)(game_38FAC4 - CAM_FOV / 2)) >= CAM_FOV)
        return 1;
    return 0;
}

uchar aihelp_2D2E9C(long i)
{
    uchar *r;
    ulong pt;

    if (game_442F44 == NULL || !(*(ulong *)game_442F44 & (1UL << (i & 31))))
        return 0;
    r = SPAWN(i);
    *(fix *)(r + 8) = (REGION_X(i) & 0xFFFF0000) | 0x8000;
    *(fix *)(r + 0xC) = (REGION_Y(i) & 0xFFFF0000) | 0x8000;
    pt = G4C2_PT((*(fix *)(r + 8) + 0x8000) >> 16, (*(fix *)(r + 0xC) + 0x8000) >> 16);
    if (r[0] == 1)
        r[0x1B] = game_44127A;
    if (aimain_24A7B8(r[7], pt, 2))
        misobj_2C9898(r, r[7]);
    return 1;
}

void aihelp_2D2F5C(ulong dt)
{
    uchar i;

    for (i = 0; i < 0x20; i++) {
        ulong bit = 1UL << i;
        ulong *t;
        if (game_442F44 == NULL || !(*(ulong *)game_442F44 & bit))
            continue;
        if (game_442F40 == NULL || !(*(ulong *)game_442F40 & bit))
            continue;
        t = (ulong *)game_442F40;
        if (dt < t[0x21 + i]) {
            t[0x21 + i] -= dt;
            continue;
        }
        t[0x21 + i] = 0;
        if (aihelp_2D2BE0(i) && aihelp_2D2E9C(i))
            ((ulong *)game_442F40)[0x21 + i] = ((ulong *)game_442F40)[1 + i];
    }
}
