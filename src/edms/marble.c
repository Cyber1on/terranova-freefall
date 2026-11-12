/* marble.c -- x:\prj\tech\libsrc\tn_edms\models\marble.cc (Terra Nova v1.09): the MARBLE model
 * (vehicles and objects), Terra Nova's own version of System Shock's MODELS/MARBLE.CC.
 * Decompiled from __FF.EXE.
 *
 *   0x312CB8 marble_static_init   0x312CC3 marble_safe_mag     0x312D9B marble_idof
 *   0x313A6E marble_set_control   0x313AFC marble_set_I11      0x313B30 marble_add_I14
 *   0x313B59 make_marble
 *
 * TN rewrote the model.  Shock's marble asks terrain() for height and slopes and uses a spring
 * (I[20] stiffness) against the ground; TN asks the game's FF_terrain (through ff_terrain) and
 * treats the ground and walls as a hard contact: it moves the marble out of the ground itself
 * (writing A and S positions), cancels the incoming normal velocity with a bounce impulse and
 * applies a friction acceleration limited by what would stop the marble.  Objects collide as
 * spheres or, when I[32] is set, as vertical cylinders.
 *
 * Marble I[] slots (make_marble copies params[0..9] to I[20..29], params[10..11] to I[32..33];
 * EDMS_make_marble fills params from the Marble struct p[0..9]):
 *   I[11] I[12] I[13]  external acceleration Z, X, Y (marble_set_I11; wakes the object)
 *   I[14] I[15] I[16]  impulse X, Y, Z (marble_add_I14, added up; applied as I[28]*I/dt and
 *                      cleared by every marble_idof)
 *   I[17] I[18] I[19]  control acceleration Z, X, Y (marble_set_control)
 *   I[20] p0  mass (marble_idof does not read it)
 *   I[21] p1  radius: ground contact distance, and collision radius (copied to I[31])
 *   I[22] p2  gravity (subtracted from Z acceleration; the normal force on the ground is
 *             gravity * cos(slope))
 *   I[23] p3  restitution: the bounce impulse is (1 + I[23]) * normal speed
 *   I[24] p4  not read by the model
 *   I[25] p5  control gain on the ground: control I[18],I[19] is scaled by I[25] times
 *             TFF.terrain_information
 *   I[26] p6  drag on the ground with no horizontal control
 *   I[27] p7  drag on the ground while controlled (I[18] or I[19] effective)
 *   I[28]     1/mass (EDMS_make_marble computes it; I[36] gets a copy)
 *   I[29]     uninitialised (EDMS_make_marble leaves params[9] unset)
 *   I[32] p8  cylinder horizontal radius, 0 = collide as a sphere of radius I[31]
 *   I[33] p9  cylinder vertical half-height
 *   I[37]     excluded collision partner (Q, -1 = none)
 *   I[38]     autodestruct, set to -1 on a collision when armed (> 0)
 *   I[39]     Z offset of the collision centre (EDMS_set_param39)
 */
#include "edms.h"
#include "edms_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
Q edms_marble_one;                  /* 0x45D61C, 1.0 after marble_static_init */
Q edms_marble_last[3];              /* 0x45D620 */
#endif

/* Static initialiser (XI table entry 0x36EFB4): static Q fix_one = 1. */
void marble_static_init(void)
{
    edms_marble_one = 0x10000;
}

/* |(x, y, z)| without overflow (name ours).  Takes absolute values of x, y and z IN PLACE, then
 * halves them (by a common shift) until each is at most 32.0, takes the root and shifts back.
 * The Q result goes through the hidden pointer ret (passed in ecx), which is also returned.
 * Quirk: a component above 0x40000000 makes the limit overflow to 0 and loops forever. */
Q *marble_safe_mag(Q *x, Q *y, Q *z, Q *ret)
{
    Q limit = 0x200000;             /* 32.0 */
    int shift = 0;

    *x = edms_c_abs(*x);
    *y = edms_c_abs(*y);
    *z = edms_c_abs(*z);
    while (limit < *x) { limit = edms_c_mul(limit, 0x20000); shift++; }
    while (limit < *y) { limit = edms_c_mul(limit, 0x20000); shift++; }
    while (limit < *z) { limit = edms_c_mul(limit, 0x20000); shift++; }
    *x >>= (shift & 31);
    *y >>= (shift & 31);
    *z >>= (shift & 31);
    *ret = fix_sqrt(edms_c_mul(*x, *x) + edms_c_mul(*y, *y) + edms_c_mul(*z, *z)) << (shift & 31);
    return ret;
}

/* Collision callback, as marble_idof makes it: location = marble_last (A position). */
static void marble_collision_callback(int object, int other, Q r, Q R)
{
    int badness = edms_c_mul(0x140000, EDMS_Q_ONE - edms_c_div(r, R)) >> 16;  /* 20*(1 - r/R) */
    WATCOM_CALL((edms_collision_fn)edms_object_collision, edms_on2ph[object], edms_on2ph[other], badness, 0, 0,
                EDMS_C_PTRARG(edms_marble_last));
}

/* Moves the marble out of another object by (R - r)/r times the separation (all three axes). */
static void marble_push_out(int object, Q R, Q r, Q dx, Q dy, Q dz)
{
    Q f = edms_c_div(R - r, r);
    edms_A[object][0][0] += edms_c_mul(dx, f);
    edms_S[object][0][0] = edms_A[object][0][0];
    edms_A[object][1][0] += edms_c_mul(dy, f);
    edms_S[object][1][0] = edms_A[object][1][0];
    edms_A[object][2][0] += edms_c_mul(dz, f);
    edms_S[object][2][0] = edms_A[object][2][0];
}

/* The internal degrees of freedom: terrain contact, collisions and the accelerations
 * S[object][dof][2].  Called by soliton_lite through idof_functions with A filled. */
void marble_idof(int object)
{
    Q *I = edms_I[object];
    Q inv_dt, fx, fy, fz, imp_x, imp_y, imp_z;
    Q acc_x = 0, acc_y = 0, acc_z = 0;      /* collision accumulators (never added to in TN) */
    Q normal = 0, nf_x = 0, nf_y = 0, nf_z = 0;  /* normal force (gravity * cos) and components */
    Q bn_x = 0, bn_y = 0, bn_z = 0;         /* bounce */
    Q x, y, z, X, Y, Z;
    Q g_height, slope_x, slope_y, wall[3], traction, friction, dist = 0, depth;
    Q c, nx, ny, mag, cut[3];
    bool wall_hit = 0, contact;
    terrain_ff TFF;
    unsigned int mask;
    int bit, other;

    inv_dt = edms_c_div(EDMS_Q_ONE, edms_timestep);
    fx = I[12];
    fy = I[13];
    fz = I[11] + I[17];
    imp_x = edms_c_mul(I[14], inv_dt);
    imp_y = edms_c_mul(I[15], inv_dt);
    imp_z = edms_c_mul(I[16], inv_dt);

    x = edms_A[object][0][0];
    y = edms_A[object][1][0];
    z = edms_A[object][2][0];
    edms_marble_last[0] = x;
    edms_marble_last[1] = y;
    edms_marble_last[2] = z;

    TFF.caller = edms_on2ph[object];
    X = x; Y = y; Z = z;
    ff_terrain(&X, &Y, &Z, FALSE, &TFF);
    g_height = TFF.g_height;
    slope_x = TFF.g_dx;
    slope_y = TFF.g_dy;
    wall[0] = TFF.w_x;
    wall[1] = TFF.w_y;
    wall[2] = TFF.w_z;
    traction = TFF.terrain_information;
    friction = TFF.friction;
    if (TFF.DATA1 != 0 || TFF.DATA2 != 0)
        WATCOM_CALL((edms_terrain_event_fn)edms_terrain_event, edms_on2ph[object], TFF.DATA1, TFF.DATA2);

    /* A wall: its vector replaces the ground; distance = 0.25 - |wall|. */
    if (wall[0] != 0 || wall[1] != 0 || wall[2] != 0) {
        if (wall[2] == 0)
            wall[2] = 0x28F;        /* .01 */
        slope_x = edms_c_div(-wall[0], wall[2]);
        slope_y = edms_c_div(-wall[1], wall[2]);
        dist = 0x4000 - fix_sqrt(edms_c_mul(wall[0], wall[0]) + edms_c_mul(wall[1], wall[1]) +
                                 edms_c_mul(wall[2], wall[2]));
        g_height = 0;
        wall_hit = 1;
    }

    /* Unit normal (nx, ny, c) of the surface z = g(x, y). */
    c = edms_c_div(edms_marble_one, fix_sqrt(edms_marble_one + edms_c_mul(slope_x, slope_x) +
                                             edms_c_mul(slope_y, slope_y)));
    nx = edms_c_mul(-c, slope_x);
    ny = edms_c_mul(-c, slope_y);
    if (!wall_hit)
        dist = edms_c_mul(z - g_height, c);

    depth = I[21] - dist;
    contact = depth > 0;
    if (contact) {
        Q vn, pen, ctl, c3, drag;

        /* Normal force cancels gravity along the normal. */
        normal = edms_c_mul(I[22], c);
        nf_x = edms_c_mul(normal, nx);
        nf_y = edms_c_mul(normal, ny);
        nf_z = edms_c_mul(normal, c);

        /* Bounce: speed into the surface, times (1 + restitution), per timestep. */
        vn = -(edms_c_mul(edms_A[object][0][1], nx) + edms_c_mul(edms_A[object][1][1], ny) +
               edms_c_mul(edms_A[object][2][1], c));
        if (vn > 0) {
            Q b = edms_c_mul(edms_c_mul(vn, I[23] + EDMS_Q_ONE), inv_dt);
            bn_x = edms_c_mul(b, nx);
            bn_y = edms_c_mul(b, ny);
            bn_z = edms_c_mul(b, c);
        }

        /* Push the marble out of the ground, allowing it to sink 20% of its radius. */
        pen = depth - edms_c_mul(I[21], 0x3333);
        if (pen < 0)
            pen = 0;
        edms_A[object][0][0] += edms_c_mul(nx, pen);
        edms_S[object][0][0] = edms_A[object][0][0];
        edms_A[object][1][0] += edms_c_mul(ny, pen);
        edms_S[object][1][0] = edms_A[object][1][0];
        edms_A[object][2][0] += edms_c_mul(c, pen);
        edms_S[object][2][0] = edms_A[object][2][0];

        /* Control works only on the ground, scaled by traction and by cos^3 of the slope. */
        ctl = edms_c_mul(I[25], traction);
        fx += edms_c_mul(I[18], ctl);
        fy += edms_c_mul(I[19], ctl);
        c3 = edms_c_mul(edms_c_mul(c, c), c);
        fx = edms_c_mul(fx, c3);
        fy = edms_c_mul(fy, c3);
        drag = (fx != 0 || fy != 0) ? I[27] : I[26];
        fx -= edms_c_mul(drag, edms_A[object][0][1]);
        fy -= edms_c_mul(drag, edms_A[object][1][1]);
    }

    /* Collisions with the objects in this object's hash cells. */
    mask = are_you_there(object);
    for (bit = 0; mask != 0; mask >>= 1, bit++) {
        if (!(mask & 1))
            continue;
        for (other = bit; other < EDMS_MAX_OBJ && edms_S[other][0][0] > edms_END; other += 32) {
            Q *J = edms_I[other];
            Q dx, dy, dz, R, r;

            if (other == object || (I[37] >> 16) == other)
                continue;
            dx = x - edms_A[other][0][0];
            dy = y - edms_A[other][1][0];
            dz = (I[39] + z) - (edms_A[other][2][0] + J[39]);

            if (I[32] == 0 && J[32] == 0) {
                /* Two spheres. */
                R = I[31] + J[31];
                if (!(edms_c_abs(dx) < R) || !(edms_c_abs(dy) < R) || !(edms_c_abs(dz) < R))
                    continue;
                r = fix_sqrt(edms_c_mul(dx, dx) + edms_c_mul(dy, dy) + edms_c_mul(dz, dz));
                if (!(r < R))
                    continue;
                if (I[38] > 0)
                    I[38] = EDMS_Q_MINUS_ONE;
                marble_collision_callback(object, other, r, R);
                if (!(r > 0x28F))
                    continue;
                marble_push_out(object, R, r, dx, dy, dz);
            } else {
                /* At least one vertical cylinder (a sphere counts as radius = half-height). */
                Q rh_o = I[32], rv_o = I[33], rh_x, rv_x, Rv;
                if (rh_o == 0)
                    rh_o = rv_o = I[31];
                rh_x = J[32];
                rv_x = J[33];
                if (rh_x == 0)
                    rh_x = rv_x = J[31];
                R = rh_o + rh_x;
                Rv = rv_o + rv_x;
                if (!(edms_c_abs(dx) < R) || !(edms_c_abs(dy) < R) || !(edms_c_abs(dz) < Rv))
                    continue;
                r = fix_sqrt(edms_c_mul(dx, dx) + edms_c_mul(dy, dy));
                if (!(r < R))
                    continue;
                if (I[38] > 0)
                    I[38] = EDMS_Q_MINUS_ONE;
                marble_collision_callback(object, other, r, R);
                if (!(r > 0x28F))
                    continue;
                /* Only a sphere is pushed, and only out of a cylinder.  Quirk: the push uses the
                 * horizontal ratio on dz as well. */
                if (I[32] != 0 || J[32] == 0)
                    continue;
                marble_push_out(object, R, r, dx, dy, dz);
            }
        }
    }

    /* Equations of motion. */
    edms_S[object][0][2] = fx + nf_x + bn_x + edms_c_mul(I[28], imp_x + acc_x);
    edms_S[object][1][2] = edms_c_mul(I[28], imp_y + acc_y) + (fy + nf_y + bn_y);
    edms_S[object][2][2] = (fz + nf_z + bn_z - I[22]) + edms_c_mul(I[28], imp_z + acc_z);

    /* Friction on the ground: friction * normal force, doubled when it exceeds 0.7 of what
     * would stop the marble this step, and never more than that. */
    if (contact) {
        Q need_x, need_y, need_z, ax, ay, az, fr, f, fr_x, fr_y, fr_z;

        need_x = edms_c_mul(-edms_A[object][0][1], inv_dt) - edms_S[object][0][2];
        need_y = edms_c_mul(-edms_A[object][1][1], inv_dt) - edms_S[object][1][2];
        need_z = edms_c_mul(-edms_A[object][2][1], inv_dt) - edms_S[object][2][2];
        ax = edms_c_abs(need_x);
        ay = edms_c_abs(need_y);
        az = edms_c_abs(need_z);
        fr = f = edms_c_mul(friction, normal);
        cut[0] = ax; cut[1] = ay; cut[2] = az;
        marble_safe_mag(&cut[0], &cut[1], &cut[2], &mag);
        if (fr > edms_c_mul(mag, 0xB333))
            f = edms_c_mul(fr, 0x20000);
        if (mag > 0x28F) {
            Q inv = edms_c_div(EDMS_Q_ONE, mag);
            fr_x = edms_c_mul(f, edms_c_mul(ax, inv));
            fr_y = edms_c_mul(f, edms_c_mul(ay, inv));
            fr_z = edms_c_mul(f, edms_c_mul(az, inv));
        } else {
            fr_x = fr_y = fr_z = 0x3E80000;     /* 1000.0 */
        }
        if (fr_x > ax) fr_x = ax;
        if (need_x < 0) fr_x = -fr_x;
        if (fr_y > ay) fr_y = ay;
        if (need_y < 0) fr_y = -fr_y;
        if (fr_z > az) fr_z = az;
        if (need_z < 0) fr_z = -fr_z;
        edms_S[object][0][2] += fr_x;
        edms_S[object][1][2] += fr_y;
        edms_S[object][2][2] += fr_z;
    }

    /* The impulse is used up. */
    I[16] = 0;
    I[15] = I[16];
    I[14] = I[15];
}

/* Control acceleration (Shock scaled it by I[26] and took abs(Z); TN stores it as given).
 * Wakes the object when any control is non-zero. */
void marble_set_control(int object, Q *X, Q *Y, Q *Z)
{
    Q *I = edms_I[object];
    I[18] = *X;
    I[19] = *Y;
    I[17] = *Z;
    edms_no_no_not_me[object] = edms_no_no_not_me[object] != 0 || I[18] != 0 || I[19] != 0 || I[17] != 0;
}

/* External acceleration X, Y, Z into I[12], I[13], I[11]; wakes the object (name ours). */
void marble_set_I11(int object, Q *X, Q *Y, Q *Z)
{
    Q *I = edms_I[object];
    I[12] = *X;
    I[13] = *Y;
    I[11] = *Z;
    edms_no_no_not_me[object] = 1;
}

/* Adds an impulse X, Y, Z to I[14..16] (name ours).  Does not wake the object. */
void marble_add_I14(int object, Q *X, Q *Y, Q *Z)
{
    Q *I = edms_I[object];
    I[14] += *X;
    I[15] += *Y;
    I[16] += *Z;
}

/* Makes a marble in the first free slot from init_state[dof][pos, vel, accel] and params[12].
 * Returns the object number, or -1 when all 96 are used.  Quirk (as in Shock): the free-slot
 * search has no bound, it stops at the first S[o][0][0] <= END even past S[95]. */
int make_marble(Q init_state[3][3], Q params[12])
{
    int object = -1, coord, deriv, copy;
    Q end = edms_END;
    EDMS_ObjState *S = edms_c_launder(edms_S);
    Q *I;

    while (S[++object][0][0] > end)
        ;
    if (object >= EDMS_MAX_OBJ)
        return -1;

    for (coord = 0; coord < 3; coord++)
        for (deriv = 0; deriv < 3; deriv++)
            edms_S[object][coord][deriv] = edms_A[object][coord][deriv] = init_state[coord][deriv];

    I = edms_I[object];
    for (copy = 0; copy < 10; copy++)
        I[copy + 20] = params[copy];
    I[30] = edms_MARBLE;
    I[31] = I[21];
    I[32] = params[10];
    I[33] = params[11];
    I[34] = 0;
    I[35] = 0;
    I[36] = I[28];
    I[37] = EDMS_Q_MINUS_ONE;
    I[38] = 0;
    I[39] = 0;
    I[14] = I[15] = I[16] = 0;
    I[18] = I[19] = I[17] = 0;
    I[12] = I[13] = I[11] = 0;

    edms_idof_functions[object] = (void (WATCOMCALL *)(int))WATCOM_FN(marble_idof);
    generic_write_object(object, edms_A);
    return object;
}
