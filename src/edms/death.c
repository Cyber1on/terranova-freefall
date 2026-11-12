/* death.c -- x:\prj\tech\libsrc\tn_edms\models\death.cc (Terra Nova v1.09): the DEATH model
 * (projectiles and debris), System Shock's MODELS/DEATH.CC cut down to 3 degrees of freedom.
 * Decompiled from __FF.EXE.
 *
 *   0x3198A4 deadly_idof   0x319C87 make_death
 *   0x319DE4 death_stub_neg1_a   0x319DEC death_stub_neg1_b
 *
 * Death I[] slots (make_death copies params[0..9] to I[20..29]; EDMS_make_death fills only
 * params[0..2] = Death mass, size, gravity, so I[23..29] are stack garbage):
 *   I[20] mass (copied to I[36])   I[21] size (collision radius, copied to I[31])
 *   I[22] gravity
 * Differences from Shock: no drag (the accelerations are 0, 0, -gravity), a wall is any
 * non-zero w_x or w_y (Shock: |w_x| + |w_y| > 0), the location passed to the collision callback
 * is a local array, the object test skips partners that are DEATH objects themselves (Shock
 * tested this object's own type), the partner's I[39] raises its collision centre, and the
 * radius test uses the current radii (Shock compared against last time's object13).
 */
#include "edms.h"
#include "edms_c.h"

#define EDMS_BADNESS_DEATH (-9999)

void deadly_idof(int object)
{
    Q *I = edms_I[object];
    Q x, y, z, X, Y, Z;
    Q location[3];
    terrain_ff TFF;
    bool wall, ground;
    unsigned int mask;
    int bit, other;

    x = edms_A[object][0][0];
    y = edms_A[object][1][0];
    z = edms_A[object][2][0];

    TFF.caller = edms_on2ph[object];
    X = x; Y = y; Z = z;
    ff_terrain(&X, &Y, &Z, FALSE, &TFF);

    wall = TFF.w_x != 0 || TFF.w_y != 0;
    ground = TFF.g_height > z - I[21];
    if (wall || ground) {
        physics_handle ph;
        if (I[38] > 0)
            I[38] = EDMS_Q_MINUS_ONE;
        location[0] = x;
        location[1] = y;
        location[2] = z;
        ph = edms_on2ph[object];
        WATCOM_CALL((edms_collision_fn)edms_object_collision, ph, ph, EDMS_BADNESS_DEATH, ground ? TFF.DATA1 : 0,
                    wall ? TFF.DATA2 : 0, EDMS_C_PTRARG(location));
        WATCOM_CALL((edms_ph_fn)edms_autodestruct, edms_on2ph[object]);
    }

    mask = are_you_there(object);
    for (bit = 0; mask != 0; mask >>= 1, bit++) {
        if (!(mask & 1))
            continue;
        for (other = bit; other < EDMS_MAX_OBJ && edms_S[other][0][0] > edms_END; other += 32) {
            Q dx, dy, dz, R, r;
            physics_handle C, V;

            if (other == object || (I[37] >> 16) == other || edms_I[other][30] == edms_DEATH)
                continue;
            dx = x - edms_A[other][0][0];
            dy = y - edms_A[other][1][0];
            dz = z - (edms_A[other][2][0] + edms_I[other][39]);
            R = I[31] + edms_I[other][31];
            if (!(edms_c_abs(dx) < R) || !(edms_c_abs(dy) < R) || !(edms_c_abs(dz) < R))
                continue;
            r = fix_sqrt(edms_c_mul(dx, dx) + edms_c_mul(dy, dy) + edms_c_mul(dz, dz));
            if (!(r < R) || !(r > 0x1999))      /* .1 */
                continue;
            if (I[38] > 0)
                I[38] = EDMS_Q_MINUS_ONE;
            C = edms_on2ph[object];
            V = edms_on2ph[other];
            location[0] = x;
            location[1] = y;
            location[2] = z;
            WATCOM_CALL((edms_collision_fn)edms_object_collision, C, V, EDMS_BADNESS_DEATH, 0, 0, EDMS_C_PTRARG(location));
            WATCOM_CALL((edms_ph_fn)edms_autodestruct, C);
        }
    }

    edms_S[object][0][2] = 0;
    edms_S[object][1][2] = 0;
    edms_S[object][2][2] = -I[22];
}

/* Makes a death object in the first free slot from init_state[dof][pos, vel] (rows of 3 Q,
 * accelerations not copied) and params[10].  Returns the object number or -1.  Unlike
 * make_marble it does not write the collision hash, and it wakes the object. */
int make_death(Q init_state[][3], Q params[10])
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
        for (deriv = 0; deriv < 2; deriv++)
            edms_S[object][coord][deriv] = edms_A[object][coord][deriv] = init_state[coord][deriv];

    I = edms_I[object];
    for (copy = 0; copy < 10; copy++)
        I[copy + 20] = params[copy];
    I[30] = edms_DEATH;
    edms_idof_functions[object] = (void (WATCOMCALL *)(int))WATCOM_FN(deadly_idof);
    I[31] = I[21];
    I[35] = 0;
    I[34] = I[35];
    I[33] = I[34];
    I[32] = I[33];
    I[36] = I[20];
    I[37] = EDMS_Q_MINUS_ONE;
    I[38] = 0;
    I[39] = 0;
    edms_no_no_not_me[object] = 1;
    return object;
}

/* Two unreferenced stubs after make_death (names ours). */
int death_stub_neg1_a(void)
{
    return -1;
}

int death_stub_neg1_b(void)
{
    return -1;
}
