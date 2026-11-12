/* ragdoll.c -- Terra Nova soldier ragdolls (module M050; source name unknown).
 * Decompiled from __FF.EXE v1.09.
 *
 * When a soldier (PBA) dies, vec3_282CFC replaces its body with a class 6 object of type 0x21
 * and a ragdoll record: the 11 limb segments of its skeleton, each a pair of joints with a
 * velocity per joint.  vec3_2825DC moves the joints every frame (gravity, damping, a length
 * constraint per segment, terrain contact) until every joint rests; the player's own ragdoll
 * also steers the death camera (ragdoll_cam / ragdoll_camdir, read by 0x269694).
 *
 *   0x2822F0 vec3_2822F0  death camera position from the player's ragdoll head segment
 *   0x28236C vec3_28236C  death camera pitch and heading from ragdoll_camdir
 *   0x2823DC vec3_2823DC  allocate a ragdoll record (recycles the oldest when all are used)
 *   0x282450 vec3_282450  camera direction from a segment and its normal
 *   0x2824C8 vec3_2824C8  length constraint of the current segment
 *   0x2825DC vec3_2825DC  simulate one ragdoll for a time step
 *   0x282CBC vec3_282CBC  kill callback of the ragdoll object: frees its record
 *   0x282CFC vec3_282CFC  turn a dead soldier into a ragdoll
 *   0x283240 vec3_283240  reset (falls through into 0x283260)
 *   0x283260 vec3_283260  no-op
 */
#include "g3_a.h"
#include "g6_c.h"     /* added at the G5-G8 merge */
#include "g7_b.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
uchar ragdoll_used[RAGDOLL_N];
uchar ragdoll_recs[RAGDOLL_N][RAGDOLL_SIZE];
fix *ragdoll_cam;
uchar *ragdoll_cur;
fix ragdoll_camdir[3];
fix *ragdoll_p0, *ragdoll_p1;
#endif

#define RD_JOINT(r, j)  ((fix *)((r) + 0xD1 + (j) * 0xC))
#define RD_NORMAL(r, k) ((fix *)((r) + 0x1D9 + (k) * 0xC))
#define RD_MOTION(r, k) ((r) + 0x25D + (k) * 0x1E)

void vec3_2822F0(fix *out)
{
    fix *a = ragdoll_cam, *b = ragdoll_cam + 3;

    out[0] = a[0] + (a[0] - b[0]) * 3 / 5;
    out[1] = a[1] + (a[1] - b[1]) * 3 / 5;
    out[2] = a[2] + (a[2] - b[2]) * 3 / 5;
}

void vec3_28236C(short *pitch, short *heading)
{
    fix h = fix_sqrt(G3A_FIXMUL(ragdoll_camdir[1], ragdoll_camdir[1]) +
                     G3A_FIXMUL(ragdoll_camdir[0], ragdoll_camdir[0]));

    *pitch = (short)fix_atan2(ragdoll_camdir[2], h);
    *pitch = -*pitch;
    *heading = (short)(fix_atan2(ragdoll_camdir[1], ragdoll_camdir[0]) + 0x8000);
}

long vec3_2823DC(void)
{
    long i, best, bi;

    for (i = 0; i < RAGDOLL_N && ragdoll_used[i]; i++)
        ;
    if (i != RAGDOLL_N) {
        ragdoll_used[i] = 1;
        return i;
    }
    best = 0;
    bi = 0;
    for (i = 0; i < RAGDOLL_N; i++) {
        if (best < *(long *)&ragdoll_recs[i][5]) {
            best = *(long *)&ragdoll_recs[i][5];
            bi = i;
        }
    }
    KillObjMark(*(short *)&ragdoll_recs[bi][0x3A7], 3);
    return bi;
}

void vec3_282450(fix *out, uchar *rec, long seg)
{
    g3s_vector d;
    fix *n = RD_NORMAL(rec, seg);

    g3_vec_sub(&d, (g3s_vector *)RD_JOINT(rec, seg * 2 + 1), (g3s_vector *)RD_JOINT(rec, seg * 2));
    g3_cross_product((g3s_vector *)out, &d, (g3s_vector *)n);
    g3_cross_product((g3s_vector *)n, (g3s_vector *)out, &d);
    g3_vec_normalize((g3s_vector *)n);
}

void vec3_2824C8(long seg)
{
    uchar *m = RD_MOTION(ragdoll_cur, seg);
    fix d[3], len, mag, f;

    len = *(fix *)m;
    g3_vec_sub((g3s_vector *)d, (g3s_vector *)ragdoll_p0, (g3s_vector *)ragdoll_p1);
    mag = g3_vec_mag((g3s_vector *)d);
    if (mag == 0)
        return;
    f = (G3A_FIXDIV(len, mag) - 0x10000) / 2;
    if (f == 0)
        return;
    g3_vec_scale((g3s_vector *)d, (g3s_vector *)d, f);
    ragdoll_p0[0] += d[0];
    ragdoll_p0[1] += d[1];
    ragdoll_p1[0] -= d[0];
    ragdoll_p1[1] -= d[1];
    if (m[0x1C] && !m[0x1D]) {
        ragdoll_p1[2] -= d[2] * 2;
    } else if (m[0x1D] && !m[0x1C]) {
        ragdoll_p0[2] += d[2] * 2;
    } else {
        ragdoll_p1[2] -= d[2];
        ragdoll_p0[2] += d[2];
    }
}

/* joint and velocity of one end of a segment after the terrain test */
static void ground(fix *p, terrain_ff *t, fix h, uchar *flag, uchar *resting)
{
    fix a, b;

    if (h >= p[2]) {
        p[2] = h - 4;
        a = t->g_dx < 0 ? -t->g_dx : t->g_dx;
        b = t->g_dy < 0 ? -t->g_dy : t->g_dy;
        if ((a > b ? a : b) > 0x9000) {
            p[0] -= (long)((ulong)t->g_dx << 10) >> 16;
            p[1] -= (long)((ulong)t->g_dy << 10) >> 16;
            *resting = 0;
        }
        *flag = 1;
    } else {
        *flag = 0;
    }
}

static void damp(fix *v, uchar grounded, fix dt, fix g, uchar *resting)
{
    if (grounded) {
        v[0] -= v[0] >> 3;
        v[1] -= v[1] >> 3;
    } else {
        v[0] -= v[0] >> 6;
        v[1] -= v[1] >> 6;
        v[2] -= G3A_FIXMUL(dt, g);
        v[2] -= v[2] >> 6;
        *resting = 0;
    }
}

void vec3_2825DC(short slot, fix dt)
{
    uchar *obj = game_cls6[slot];
    uchar *rec, *m;
    GameEntity *e;
    terrain_ff tff;
    fix old0[3], old1[3], d[3], sum[3];
    fix grav, inv, h;
    long i, count;
    uchar resting;

    if (dt == 0)
        return;
    rec = ragdoll_recs[*(long *)(obj + 0xA)];
    (*(long *)(rec + 5))++;
    ragdoll_cur = rec;
    if (obj[0xE])
        return;
    grav = (long)game_442439 * 10 / 6;
    count = 0;
    e = &game_entities[G3A_IDX15(*(short *)obj)];
    inv = G3A_FIXDIV(0x10000, dt);
    resting = 1;
    sum[0] = sum[1] = sum[2] = 0;
    for (i = 0; i < rec[0xE]; i++) {
        if (rec[0x24 + i * 0x10] & 0x10)   /* flags of segment i at its fixed place */
            continue;
        tff.caller = G3A_IDX15(*(short *)obj);
        ragdoll_p0 = RD_JOINT(rec, i * 2);
        ragdoll_p1 = RD_JOINT(rec, i * 2 + 1);
        memcpy(old0, ragdoll_p0, 12);
        memcpy(old1, ragdoll_p1, 12);
        m = RD_MOTION(rec, i);
        ragdoll_p0[0] += G3A_FIXMUL(dt, *(fix *)(m + 4));
        ragdoll_p0[1] += G3A_FIXMUL(dt, *(fix *)(m + 8));
        ragdoll_p0[2] += G3A_FIXMUL(dt, *(fix *)(m + 0xC));
        ragdoll_p1[0] += G3A_FIXMUL(dt, *(fix *)(m + 0x10));
        ragdoll_p1[1] += G3A_FIXMUL(dt, *(fix *)(m + 0x14));
        ragdoll_p1[2] += G3A_FIXMUL(dt, *(fix *)(m + 0x18));
        g3_vec_add((g3s_vector *)sum, (g3s_vector *)sum, (g3s_vector *)RD_JOINT(rec, i * 2));
        vec3_2824C8(i);
        count++;
        FF_terrain(ragdoll_p0[0] * 6, ragdoll_p0[1] * 6, ragdoll_p0[2] * 6, 0, &tff);
        h = tff.g_height / 6;
        /* the height under the first joint is used for both */
        ground(ragdoll_p0, &tff, h, &m[0x1C], &resting);
        ground(ragdoll_p1, &tff, h, &m[0x1D], &resting);
        if (m[0x1C] && ragdoll_p0[2] - old0[2] > 0x2AAA) {
            memcpy(ragdoll_p0, old0, 12);
            m[0x1C] = 0;
        }
        if (m[0x1D] && ragdoll_p1[2] - old1[2] > 0x2AAA) {
            memcpy(ragdoll_p1, old1, 12);
            m[0x1D] = 0;
        }
        g3_vec_sub((g3s_vector *)d, (g3s_vector *)ragdoll_p0, (g3s_vector *)old0);
        g3_vec_scale((g3s_vector *)(m + 4), (g3s_vector *)d, inv);
        if (m[0x1C] && *(fix *)(m + 0xC) > 0x8000)
            *(fix *)(m + 0xC) = 0;
        g3_vec_sub((g3s_vector *)d, (g3s_vector *)ragdoll_p1, (g3s_vector *)old1);
        g3_vec_scale((g3s_vector *)(m + 0x10), (g3s_vector *)d, inv);
        if (m[0x1D] && *(fix *)(m + 0x18) > 0x8000)
            *(fix *)(m + 0x18) = 0;
        if (tff.DATA1)
            rec[0x24 + i * 0x10] |= 0x10;
        if (m[0x1C]) {
            *(fix *)(m + 4) -= *(fix *)(m + 4) >> 3;
            *(fix *)(m + 8) -= *(fix *)(m + 8) >> 3;
            if (helm_263410(RD_JOINT(rec, i * 2)[0], RD_JOINT(rec, i * 2)[1])) {
                sfx_28DC50(RD_JOINT(rec, i * 2), 1);
                rec[0x24 + i * 0x10] |= 0x10;
            }
        } else {
            damp((fix *)(m + 4), 0, dt, grav, &resting);
        }
        damp((fix *)(m + 0x10), m[0x1D], dt, grav, &resting);
    }
    if (RD_JOINT(rec, 10) == ragdoll_cam) {
        vec3_282450(ragdoll_camdir, rec, 5);
        resting = 0;
    }
    if (count)
        g3_vec_scale((g3s_vector *)&e->x, (g3s_vector *)sum, 0x10000 / count);
    else
        memcpy(&e->x, RD_JOINT(rec, 11), 12);
    if (resting) {
        obj[0xE] = 1;
        if (count == 0)
            KillObjMark(G3A_IDX15(*(short *)obj), 1);
    }
}

void vec3_282CBC(short ent, uchar reason)
{
    if (reason == 3)
        return;
    ragdoll_used[*(long *)(game_cls6[G3A_ENT(ent).slot] + 0xA)] = 0;
}

/* The unused register / stack arguments are those of the caller's death handler signature. */
void vec3_282CFC(short ent, long unused1, long unused2, long unused3, long unused4)
{
    GameEntity *e = &G3A_ENT(ent);
    uchar *biped = (uchar *)&game_pba[e->slot] + 0xDE;
    uchar *skel = biped + 0xD;
    uchar *rec, *obj, *seg, *bseg, *m;
    GridObjDesc desc;   /* only ent and x, y, z are set: the rest of pos is stack garbage */
    short newent, newslot;
    fix vel[3], at[3], d[3];
    long r, i;
    fix *bjoints, *bnormals;

    if (*(long *)biped == 0xB)
        return;
    *(fix *)&desc.pos[0] = e->x;
    *(fix *)&desc.pos[4] = e->y;
    *(fix *)&desc.pos[8] = e->z;
#ifndef TN_WATCOM_ABI
    port_sfx_kill = 1;                  /* G19: the body of the dead soldier */
    r = sfx_28D0F4(&newent, &newslot, 0x21, 0, &desc);
    port_sfx_kill = 0;
    if (!r)
        return;
#else
    if (!sfx_28D0F4(&newent, &newslot, 0x21, 0, &desc))
        return;
#endif
    r = vec3_2823DC();
    if (r == -1)
        return;
    obj = game_cls6[newslot];
    at[0] = e->x + 0x1555;
    at[1] = e->y;
    at[2] = e->z - 0x2AAA;
    BipedComputePose(biped, 100);
    physics_2D3E74(ent, &vel[0], &vel[1], &vel[2]);
    bjoints = (fix *)(biped + 0xD1);
    bnormals = bjoints + skel[2] * 3;
    *(long *)(obj + 0xA) = r;
    obj[0xE] = 0;
    rec = ragdoll_recs[r];
    *(long *)rec = *(long *)biped;
    rec[4] = 3;
    *(long *)(rec + 5) = 0;
    *(short *)(rec + 0x3A7) = newent;
    memcpy(rec + 0xD, skel, skel[1] * 16 + 0x14);
    rec[0xF] = 0x16;
    rec[0x10] = 0xB;
    *(uchar **)(rec + 0x15) = rec + 0x21;
    for (i = 0; i < skel[1]; i++) {
        bseg = *(uchar **)(skel + 8) + i * 16;
        seg = *(uchar **)(rec + 0x15) + i * 16;
        seg[0] = (uchar)(i * 2);
        seg[1] = (uchar)(i * 2 + 1);
        seg[2] = (uchar)i;
        memcpy(RD_JOINT(rec, seg[0]), bjoints + bseg[0] * 3, 12);
        memcpy(RD_JOINT(rec, seg[1]), bjoints + bseg[1] * 3, 12);
        memcpy(RD_NORMAL(rec, seg[2]), bnormals + bseg[2] * 3, 12);
        m = RD_MOTION(rec, i);
        *(fix *)m = biped_281750(skel, i, 0x10000);
        m[0x1C] = 0;
        m[0x1D] = 0;
        memcpy(m + 4, vel, 12);
        memcpy(m + 0x10, vel, 12);
        /* each joint is pushed 1.5 away from a point at the soldier's feet */
        g3_vec_sub((g3s_vector *)d, (g3s_vector *)RD_JOINT(rec, seg[0]), (g3s_vector *)at);
        g3_vec_scale((g3s_vector *)d, (g3s_vector *)d, G3A_FIXDIV(0x18000, g3_vec_mag((g3s_vector *)d)));
        g3_vec_add((g3s_vector *)(m + 4), (g3s_vector *)(m + 4), (g3s_vector *)d);
        g3_vec_sub((g3s_vector *)d, (g3s_vector *)RD_JOINT(rec, seg[1]), (g3s_vector *)at);
        g3_vec_scale((g3s_vector *)d, (g3s_vector *)d, G3A_FIXDIV(0x18000, g3_vec_mag((g3s_vector *)d)));
        g3_vec_add((g3s_vector *)(m + 0x10), (g3s_vector *)(m + 0x10), (g3s_vector *)d);
    }
    if (ent == game_player_entity) {
        ragdoll_cam = RD_JOINT(rec, 10);
        vec3_282450(ragdoll_camdir, rec, 5);
    }
    killobj_289554(G3A_IDX15(*(short *)obj), WATCOM_FN(vec3_282CBC), -1);
}

void vec3_283240(void)
{
    lg_memset(ragdoll_used, 0, 0, RAGDOLL_N);
    ragdoll_cam = 0;
}

void vec3_283260(void)
{
}
