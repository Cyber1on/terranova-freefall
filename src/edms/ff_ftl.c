/* ff_ftl.c -- x:\prj\tech\libsrc\tn_edms\ff_ftl.cc (Terra Nova v1.09): the instantaneous
 * projectile raycast ("faster than light" weapons).  Decompiled from __FF.EXE.
 * Reference: System Shock src/Libraries/EDMS/Source/MODELS/FF_FTL.CC (an older copy;
 * TN's EDMS_FF_cast_projectile is Shock's EDMS_FF_cast_projectile_new, and FF_object_check is
 * FF_object_check_new).
 *
 *   0x311244 ff_ftl_static_init   0x311283 EDMS_FF_cast_projectile   0x311848 FF_object_check
 *
 * The ray is first cast against the terrain by the game (ff_raycast -> FF_raycast 0x2DA678);
 * the hash cells along the ray up to that point are then collected (16 projectile diameters per
 * step) and the objects in them tested against the ray.
 *
 * TN differences from the Shock source:
 *  - a range below .1 returns -1 at once, with hit = FALSE and both infos 0;
 *  - the direction is reset to (0,0,1) and the range to 10 only on a non-unit D (the
 *    "range < 1" test is gone);
 *  - object positions are tested at Z + I[o][39] (also when stepping back from a victim);
 *  - the knock-back is gone: iota_c is computed (20 * I[36] * mass * speed) but nothing is
 *    written; only no_no_not_me[victim] = 1 remains;
 *  - FF_object_check gets an 8th argument, the terrain-hit flag; it only guards a dead
 *    sampling_period * max_step multiply;
 * Quirks kept: max_step = range / sampling_period is computed and thrown away (the division
 * still faults on a zero size); g_info is set by reading w_info back (g_info = w_info = 0);
 * FF_object_check's scale loop doubles a 32-bit int (scale * 16.0 wraps for |d| > 16384).
 */
#include "edms_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
Q edms_initial_X[3];
Q edms_final_X[3];
Q edms_sampling_period;
Q edms_D_old[3];
#endif

#define S edms_S
#define I edms_I
#define END edms_END

#define SMALL_ENOUGH 16

#ifndef TN_WATCOM_ABI
extern int port_highfps_fix;            /* port/core/platform.c */
#endif

/* CRT abs (0x32510A); abs(INT_MIN) stays INT_MIN. */
static inline int ff_abs(int x)
{
    return x < 0 ? -x : x;
}

static inline int ff_hash(Q x)
{
    return qb_to_int(qb_mul(edms_hash_scale, x));
}

/* File-scope constructors (XI table entry 0x36EF8A).  D_old's __ctor_array is a no-op. */
void ff_ftl_static_init(void)
{
    edms_initial_X[0] = edms_initial_X[1] = edms_initial_X[2] = 0;
    edms_final_X[0] = edms_final_X[1] = edms_final_X[2] = 0;
}

physics_handle EDMS_FF_cast_projectile(Q *X,     /* source (moved to the end point) */
                                       Q D[3],   /* unit vector of ray (scaled to the step) */
                                       Q *speed, /* for knockback (unused in TN) */
                                       Q *mass,  /* for knockback (unused in TN) */
                                       Q *size,  /* radius */
                                       Q *range, /* how far it can reach */
                                       int exclude, /* what object it cannot hit (object number) */
                                       int shooter, /* not used */
                                       long *g_info, long *w_info, bool *hit)
{
    int stepper, object_pointer = 0, max_step, victim_on = 0, i;
    hash_info must_check_objects[EDMS_MAX_OBJ];
    unsigned int test_data, last_test_data = 0;
    physics_handle victim = -1;
    int hx, hy;
    Q anus, where_hit[3], dx, dy, dz, planar_dist, dist;
    Q x_arg, y_arg, z_arg, range_arg, size_arg;
    terrain_ff TFF;
    bool hit_terrain;
    volatile Q discard;

    edms_sampling_period = qb_mul(*size, 0x20000);      /* diameter of projectile */
    discard = qb_div(*range, edms_sampling_period);     /* Shock's max_step; unused */

    /* Save the initial vectors for the line finder... */
    edms_initial_X[0] = X[0];
    edms_initial_X[1] = X[1];
    edms_initial_X[2] = X[2];
    hx = ff_hash(X[0]);
    hy = ff_hash(X[1]);

    if (*range < 0x1999) {                              /* .1 */
        *hit = FALSE;
        *w_info = 0;
        *g_info = *w_info;
        return -1;
    }

    /* Ensure that D is a unit vector */
    anus = fix_sqrt(qb_mul(D[0], D[0]) + qb_mul(D[1], D[1]) + qb_mul(D[2], D[2]));
    if (anus < 0xFD70 || anus > 0x1028F) {              /* .99, 1.01 */
        D[2] = 0x10000;
        D[0] = 0;
        D[1] = D[0];
        *range = 0xA0000;                               /* 10 */
    }

    edms_D_old[0] = D[0];
    edms_D_old[1] = D[1];
    edms_D_old[2] = D[2];

    /* go nuts now that Freefall is doing the terrain raycast */
    edms_sampling_period = qb_mul(edms_sampling_period, 0x100000);

    /* Turn D into the stepping vector */
    D[0] = qb_mul(D[0], edms_sampling_period);
    D[1] = qb_mul(D[1], edms_sampling_period);
    D[2] = qb_mul(D[2], edms_sampling_period);

    /* Make sure the game doesn't take this too seriously... */
    TFF.caller = 0;
    TFF.my_size = *size;

    /* Make sure we're on the map. */
    if (hx < 0 || hy < 0 || hx > EDMS_DATA_SIZE - 1 || hy > EDMS_DATA_SIZE - 1)
        return victim;

    range_arg = *range;
    z_arg = X[2];
    y_arg = X[1];
    x_arg = X[0];
    hit_terrain = ff_raycast(&x_arg, &y_arg, &z_arg, edms_D_old, &range_arg, where_hit, &TFF);

    /* Set the range to where we hit terrain, then look for objects the old way. */
    dx = where_hit[0] - X[0];
    dy = where_hit[1] - X[1];
    dz = where_hit[2] - X[2];
#ifdef TN_WATCOM_ABI
    planar_dist = fix_safe_pyth_dist(dx, dy, (char *)0x3587BC, 159);  /* __FILE__, __LINE__ */
    dist = fix_safe_pyth_dist(planar_dist, dz, (char *)0x3587E1, 160);
#else
    planar_dist = fix_safe_pyth_dist(dx, dy, "ff_ftl.c", 159);
    dist = fix_safe_pyth_dist(planar_dist, dz, "ff_ftl.c", 160);
#endif
    max_step = qb_to_int(qb_div(dist, edms_sampling_period));
#ifndef TN_WATCOM_ABI
    /* High frame rates (--fps above 25): a projectile moves less than one step (16 diameters)
     * in a frame, max_step is then 0 and no object is ever tested, so shots never hit.  The
     * option counts the last part step as a step.  Off at the original's frame rate. */
    if (port_highfps_fix && (qb_div(dist, edms_sampling_period) & 0xFFFF))
        max_step++;
#endif

    /* Collect the hash cells along the ray... */
    for (stepper = 0; stepper < max_step; stepper++) {
        test_data = edms_data[hx][hy];
        if (test_data != last_test_data && test_data != 0) {
            if (object_pointer < EDMS_MAX_OBJ) {
                must_check_objects[object_pointer].hx = hx;
                must_check_objects[object_pointer].hy = hy;
                must_check_objects[object_pointer].bits = test_data;
                object_pointer++;
            }
            last_test_data = test_data;
        }

        /* Move the check point... */
        X[0] += D[0];
        X[1] += D[1];
        X[2] += D[2];
        hx = ff_hash(X[0]);
        hy = ff_hash(X[1]);
        if (hx < 0 || hy < 0 || hx > EDMS_DATA_SIZE - 1 || hy > EDMS_DATA_SIZE - 1)
            break;                                      /* off map */
    }

    /* Save the final point of the line segment... */
    edms_final_X[0] = X[0];
    edms_final_X[1] = X[1];
    edms_final_X[2] = X[2];

    /* Now check for objects */
    for (i = 0; i < object_pointer; i++) {
        size_arg = *size;
        range_arg = *range;
        victim = FF_object_check(must_check_objects[i].bits, must_check_objects[i].hx,
                                 must_check_objects[i].hy, &size_arg, &range_arg, exclude,
                                 max_step, hit_terrain);
        if (victim != -1) {
            /* We hit someone! */
            hit_terrain = FALSE;
            victim_on = edms_ph2on[victim];
            /* Q iota_c = 20 * I[victim_on][36] * mass * speed: computed, never used in TN */
            discard = qb_mul(qb_mul(qb_mul(0x140000, I[victim_on][36]), *mass), *speed);
            edms_no_no_not_me[victim_on] = 1;           /* make sure we're up... */
            break;
        }
    }

    if (victim > -1) {
        /* Simplest thing for now... */
        X[0] = S[victim_on][0][0] - edms_D_old[0];
        X[1] = S[victim_on][1][0] - edms_D_old[1];
        X[2] = S[victim_on][2][0] + I[victim_on][EDMS_IDOF_PARAM39] - edms_D_old[2];
    } else {
        X[0] = where_hit[0];
        X[1] = where_hit[1];
        X[2] = where_hit[2];
    }

    /* Did we hit a wall, or did we hit range out? */
    *hit = FALSE;
    *w_info = 0;
    *g_info = *w_info;

    if (hit_terrain) {
        *hit = TRUE;
        *g_info = TFF.DATA1;
    } else if (stepper == max_step) {
        /* Check for rangeout... */
        *hit = FALSE;
        *w_info = 0;
        *g_info = *w_info;
    }

    /* Did we hit somebody? */
    if (victim != -1) {
        *hit = TRUE;
        *w_info = 0;
        *g_info = *w_info;
    }
    return victim;
}

/* Tests the objects in data_word (a hash cell's object bits) against the ray initial_X ->
 * final_X; returns the physics handle of the nearest one hit, or -1. */
physics_handle FF_object_check(unsigned int data_word, int hx, int hy, Q *size, Q *range,
                               int exclude, int max_step, int hit_terrain)
{
    int object;
    physics_handle victim = -1;
    Q kzdisto = 10000 << 16;
    Q dx, dy, dz, par_component, dist_squared, perp_component_squared, kill_zone, kzdist;
    int scale, scale_shift;
    unsigned int bit = 0;
    volatile Q discard;

    while (data_word != 0) {
        if (data_word & 1) {
            /* Object bit number 'bit' is on: check all objects which have that bit */
            for (object = bit; object < EDMS_MAX_OBJ && S[object][0][0] > END; object += 32) {
                if (object == exclude)
                    continue;
                if (!object_check_hash(object, hx, hy))
                    continue;                           /* didn't mean this one */

                /* Distances of potentially hit object to beginning of ray */
                dx = S[object][0][0] - edms_initial_X[0];
                dy = S[object][1][0] - edms_initial_X[1];
                dz = S[object][2][0] + I[object][EDMS_IDOF_PARAM39] - edms_initial_X[2];

                /* Scale <dx dy dz> down so that all are <= SMALL_ENOUGH */
                scale = 1;
                scale_shift = 0;
                while (ff_abs(dx) > (scale << 16) * SMALL_ENOUGH) { scale += scale; scale_shift++; }
                while (ff_abs(dy) > (scale << 16) * SMALL_ENOUGH) { scale += scale; scale_shift++; }
                while (ff_abs(dz) > (scale << 16) * SMALL_ENOUGH) { scale += scale; scale_shift++; }
                dx >>= scale_shift & 31;
                dy >>= scale_shift & 31;
                dz >>= scale_shift & 31;

                /* Component of the vector to the object along the (unit) ray direction */
                par_component = qb_mul(edms_D_old[0], dx) + qb_mul(edms_D_old[1], dy)
                              + qb_mul(edms_D_old[2], dz);
                if (par_component < 0)
                    continue;                           /* object is behind us */

                dist_squared = qb_mul(dx, dx) + qb_mul(dy, dy) + qb_mul(dz, dz);
                perp_component_squared = dist_squared - qb_mul(par_component, par_component);

                /* Distance of the center of the beam to the center of the object */
                kill_zone = fix_sqrt(perp_component_squared) << (scale_shift & 31);

                if (kill_zone < I[object][EDMS_IDOF_RADIUS] + *size) {
                    kzdist = fix_sqrt(dist_squared) << (scale_shift & 31);
                    if (hit_terrain)                    /* TN leftover, result unused */
                        discard = qb_mul(edms_sampling_period, max_step << 16);
                    /* in front of whatever stopped the beam, and the closest so far */
                    if (kzdist - I[object][EDMS_IDOF_RADIUS] < qb_mul(edms_sampling_period, max_step << 16)
                        && kzdist < kzdisto) {
                        victim = edms_on2ph[object];
                        kzdisto = kzdist;
                    }
                }
            }
        }
        /* Shift over the mask so we're testing the next object bit */
        data_word >>= 1;
        bit++;
    }
    return victim;
}
