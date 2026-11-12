/* soliton.c -- x:\prj\tech\libsrc\tn_edms\soliton.cc (Terra Nova v1.09): the EDMS integrator,
 * object killing/packing and the fixed-step clock.  Decompiled from __FF.EXE.
 * Reference: System Shock src/Libraries/EDMS/Source/SOLITON.CC.
 *
 *   0x311B10 soliton_static_init   0x311B9F soliton_lite        0x311EF8 soliton_vector
 *   0x311F90 EDMS_initialize       0x3120A0 EDMS_kill           0x3123EC collision_wakeup
 *   0x3126AC soliton_set_alarm
 *
 * Terra Nova cut Shock's 7-dof, up-to-4-stage integrator down to 3 degrees of freedom (X, Y, Z)
 * and a single stage: soliton_lite copies S into the argument block A, calls every object's
 * idof function once (which writes the accelerations S[o][c][2]), then steps
 *   k = dt * a;   x += dt * (v + k/2);   v += k
 * There is no sleeping (no frequency check, no snooze callback), no industrial-strength path
 * and no D_FRAME multiplier.  The objects are packed: the first S[o][0][0] == END ends the list.
 *
 * Quirks kept on purpose:
 *  - soliton_lite's loop variable is a global (0x4534EC) that the idof functions can see; after
 *    an autodestruct kill it is decremented so the object packed into this slot is stepped too
 *    (it gets its position update with its own k from the first loop).
 *  - soliton_lite never sets A_is_active (only clears it), so EDMS_kill always takes its
 *    "S" branches when called from the game.
 *  - soliton_vector adds the leftover time to the caller's timestep copy (the argument is a Q
 *    by value = pointer to the caller's copy) and, when the leftover is > .01 and gets
 *    integrated, does NOT reset the static remainder: the same leftover is added again next
 *    frame.  The remainder is only replaced when the leftover is <= .01.  (Freefall resets it
 *    and spends every leftover of a millisecond or more at once: G17 and P54 in docs/BUGS.md.
 *    The verification build keeps the original.)
 *  - EDMS_kill / collision_wakeup fix the partner of I[37] with I[(int)I[37]][37] = new number,
 *    without checking that the index is in range.
 *  - collision_wakeup (dead code) stores ph2on[ph] even for ph == -1 (writes 0x44B9E0).
 */
#include "edms_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
Q edms_soliton_remainder;
uchar edms_soliton_remainder_guard;
EDMS_ObjState edms_S[EDMS_MAX_OBJ];
int edms_integrating;
Q edms_timestep;
EDMS_Argblock_Pointer edms_A;
Q edms_I[EDMS_MAX_OBJ][EDMS_DOF_MAX];
Q edms_k[EDMS_MAX_OBJ][EDMS_DOF];
void (WATCOMCALL *edms_idof_functions[EDMS_MAX_OBJ])(int object);
int edms_soliton_object;
Q edms_hash_scale;
Q edms_one_sixth, edms_point_five, edms_point_one_two_five, edms_two, edms_point_1;
int edms_no_no_not_me[EDMS_MAX_OBJ];
int edms_alarm_clock[EDMS_MAX_OBJ];
int edms_industrial_strength[EDMS_MAX_OBJ];
int edms_wakeup_pending;
bool edms_A_is_active;
Q edms_min_scale_slice;
#endif

#define S edms_S
#define I edms_I
#define A edms_A
#define END edms_END

/* File-scope constructors (XI table entry 0x36EF90).  The three __ctor_array calls for S, I
 * and k construct Q elements whose constructor is empty: nothing to do in C. */
void soliton_static_init(void)
{
    edms_timestep = 0;
    edms_hash_scale = 0x10000;          /* 1. */
    edms_one_sixth = 0x2AAA;            /* .1666666666667 */
    edms_point_five = 0x8000;
    edms_point_one_two_five = 0x2000;
    edms_two = 0x20000;
    edms_point_1 = 0x1999;
    edms_min_scale_slice = 0x7AE;       /* .03 */
}

/* One integration step of every object (TN single-stage version). */
void soliton_lite(Q *timestep)
{
    int o, c;

    edms_timestep = *timestep;
    edms_wakeup_pending = 0;

    /* Copy the state vector into the argument vector. */
    memcpy(A, S, sizeof(edms_S));

    /* Leading term: the idof function fills in the accelerations. */
    for (edms_soliton_object = 0; S[edms_soliton_object][0][0] > END; edms_soliton_object++) {
        o = edms_soliton_object;
        edms_idof_functions[o](o);
        o = edms_soliton_object;
        for (c = 0; c < EDMS_DOF; c++)
            edms_k[o][c] = qb_mul(*timestep, S[o][c][2]);
    }

    /* Assemble the solution. */
    for (edms_soliton_object = 0; S[edms_soliton_object][0][0] > END; edms_soliton_object++) {
        o = edms_soliton_object;
        for (c = 0; c < EDMS_DOF; c++) {
            S[o][c][0] += qb_mul(*timestep, S[o][c][1] + (edms_k[o][c] >> 1));
            S[o][c][1] += edms_k[o][c];
        }

        /* Update the collision table... */
        generic_delete_object(o, A);
        generic_write_object(o, S);

        if (I[o][EDMS_IDOF_AUTODESTRUCT] < 0) {
            EDMS_kill_object(edms_on2ph[o]);      /* You deserve to die! */
            edms_soliton_object--;
        }
    }
    edms_A_is_active = FALSE;
}

/* Integrates timestep (plus the leftover of earlier frames) in fixed .03 steps. */
void soliton_vector(Q *timestep)
{
    Q slice, step, rest;

    edms_integrating = 1;

    if (!(edms_soliton_remainder_guard & 1)) {
        edms_soliton_remainder_guard |= 1;
        edms_soliton_remainder = 0;
    }
    *timestep += edms_soliton_remainder;

    slice = edms_min_scale_slice;
    while (*timestep > slice) {
        step = slice;
        soliton_lite(&step);
        *timestep -= edms_min_scale_slice;
    }

    rest = *timestep;
#ifndef TN_WATCOM_ABI
    /* Freefall: every frame spends all its time down to the last millisecond, and the leftover
     * is not added again next frame (G17: with frames of 31 to 40 ms, Freefall's 25 frames a
     * second among them, every frame got up to 10 ms more physics time than it took, units
     * walked about a third faster).  Keeping a leftover of .01 or less for the next frame made
     * 40 ms frames move 30 and 50 ms in turn once the leftover was cleared (P54: the player's
     * view shook while walking).  Below a millisecond it is still kept (no tiny steps). */
    if (rest >= 0x42) {                 /* 1 ms */
        step = rest;
        soliton_lite(&step);
        edms_soliton_remainder = 0;
    } else
        edms_soliton_remainder = rest;
#else
    if (rest > 0x28F) {                 /* .01 */
        step = rest;
        soliton_lite(&step);            /* remainder not updated (see header) */
    } else
        edms_soliton_remainder = rest;
#endif

    edms_integrating = 0;
}

/* Initialises the state stream and the collision grid. */
void EDMS_initialize(EDMS_data *d)
{
    int object, coord, deriv;

    edms_min_physics_handle = d->min_physics_handle;
    A = (EDMS_Argblock_Pointer)d->argblock_pointer;

    /* hash_scale = collision_size / playfield_size */
    edms_hash_scale = d->playfield_size;
    edms_hash_scale = qb_div(EDMS_DATA_SIZE << 16, edms_hash_scale);

    for (coord = 0; coord < EDMS_DATA_SIZE; coord++)
        for (deriv = 0; deriv < EDMS_DATA_SIZE; deriv++)
            edms_data[coord][deriv] = 0;

    for (object = 0; object < EDMS_MAX_OBJ; object++) {
        for (coord = 0; coord < EDMS_DOF; coord++)
            for (deriv = 0; deriv < 3; deriv++)
                S[object][coord][deriv] = END;          /* set solver bounds */
        for (coord = 0; coord < EDMS_DOF_MAX; coord++)
            I[object][coord] = END;
        I[object][EDMS_IDOF_MODEL] = edms_VACUUM;       /* look, Ma, no object! */
        I[object][EDMS_IDOF_AUTODESTRUCT] = 0;          /* don't kill */
    }

    for (object = 0; object < EDMS_MAX_OBJ; object++) {
        edms_no_no_not_me[object] = 1;
        edms_alarm_clock[object] = 0;
    }
}

/* Kills object deadguy and packs the objects after it down by one.
 * Returns 0, or -1 when there is no object there. */
int EDMS_kill(int deadguy)
{
    int error_code = -1;
    int object, coord, deriv, number;

    if (S[deadguy][0][0] > END) {
        /* Release from any contractual involvements with other objects... */
        reset_collisions(deadguy);

        for (object = deadguy + 1; object < EDMS_MAX_OBJ && S[object][0][0] > END; object++) {
            if (edms_A_is_active && edms_no_no_not_me[object - 1] == 1)
                generic_delete_object(object - 1, A);
            else
                generic_delete_object(object - 1, S);      /* for collisions */

            edms_idof_functions[object - 1] = edms_idof_functions[object];

            for (coord = 0; coord < EDMS_DOF; coord++) {
                for (deriv = 0; deriv < 3; deriv++) {
                    S[object - 1][coord][deriv] = S[object][coord][deriv];
                    if (edms_A_is_active)
                        A[object - 1][coord][deriv] = A[object][coord][deriv];
                }
                edms_k[object - 1][coord] = edms_k[object][coord];
            }

            for (number = 0; number < EDMS_DOF_MAX; number++)
                I[object - 1][number] = I[object][number];

            /* Fix the excluded collision information (I[37] > -1.0). */
            if (I[object - 1][EDMS_IDOF_COLLIDE] > (Q)0xFFFF0000)
                I[qb_to_int(I[object - 1][EDMS_IDOF_COLLIDE])][EDMS_IDOF_COLLIDE] = (object - 1) << 16;

            edms_no_no_not_me[object - 1] = edms_no_no_not_me[object];
            edms_alarm_clock[object - 1] = edms_alarm_clock[object];
            edms_industrial_strength[object - 1] = edms_industrial_strength[object];

            if (edms_A_is_active && edms_no_no_not_me[object - 1] == 1)
                generic_write_object(object - 1, A);
            else
                generic_write_object(object - 1, S);
        }

        /* Now kill the old last object... */
        if (edms_A_is_active && edms_no_no_not_me[object - 1] == 1)
            generic_delete_object(object - 1, A);
        else
            generic_delete_object(object - 1, S);

        for (coord = 0; coord < EDMS_DOF; coord++)
            for (deriv = 0; deriv < 3; deriv++)
                S[object - 1][coord][deriv] = END;
        for (number = 0; number < EDMS_DOF_MAX; number++)
            I[object - 1][number] = END;
        I[object - 1][EDMS_IDOF_MODEL] = edms_VACUUM;
        I[object - 1][EDMS_IDOF_AUTODESTRUCT] = 0;

        edms_idof_functions[object - 1] = NULL;
        error_code = 0;

        edms_no_no_not_me[object - 1] = 1;
        edms_industrial_strength[object - 1] = 0;
        edms_alarm_clock[object - 1] = 0;
    }
    return error_code;
}

/* Kills an object and re-creates it at the end of the list, awake and "hot" (dead code). */
void collision_wakeup(int object)
{
    Q idof_state[EDMS_DOF_MAX], state[EDMS_DOF][EDMS_DOF_DERIVS], arg[EDMS_DOF][EDMS_DOF_DERIVS];
    int coord, deriv, new_object = 0;
    physics_handle ph;
    void (WATCOMCALL *idof_function_save)(int);
    Q end;

    /* Save me, save me... */
    for (coord = 0; coord < EDMS_DOF; coord++) {
        for (deriv = 0; deriv < 3; deriv++) {
            state[coord][deriv] = S[object][coord][deriv];
            if (edms_no_no_not_me[object])
                arg[coord][deriv] = A[object][coord][deriv];
            else
                arg[coord][deriv] = S[object][coord][deriv];
        }
    }
    for (coord = 0; coord < EDMS_DOF_MAX; coord++)
        idof_state[coord] = I[object][coord];
    idof_function_save = edms_idof_functions[object];

    /* Now kill the offender... */
    EDMS_kill(object);

    /* Simulate the action of soliton packing... */
    ph = edms_on2ph[object];
    EDMS_release_object(ph);
    for (coord = object + 1; coord < EDMS_MAX_OBJ; coord++)
        EDMS_remap_object_number(coord, coord - 1);

    /* Where do I store the copy... */
    end = END;
    while (S[new_object++][0][0] > end)
        ;
    new_object -= 1;

    /* Create the duplicate... */
    for (coord = 0; coord < EDMS_DOF; coord++) {
        for (deriv = 0; deriv < 3; deriv++) {
            A[new_object][coord][deriv] = arg[coord][deriv];
            S[new_object][coord][deriv] = state[coord][deriv];
        }
    }
    for (coord = 0; coord < EDMS_DOF_MAX; coord++)
        I[new_object][coord] = idof_state[coord];

    edms_idof_functions[new_object] = idof_function_save;

    /* Set the physics handles right... */
    edms_on2ph[new_object] = ph;
    edms_ph2on[ph] = new_object;

    generic_write_object(new_object, S);        /* this is ALWAYS DURING integration!! */
    edms_no_no_not_me[new_object] = 1;          /* wake up!!! */
    edms_industrial_strength[new_object] = 1;   /* wake up HOT! */

    if (I[new_object][EDMS_IDOF_COLLIDE] > (Q)0xFFFF0000)
        I[qb_to_int(I[new_object][EDMS_IDOF_COLLIDE])][EDMS_IDOF_COLLIDE] = new_object << 16;
}

/* TN (name ours, dead code): wakes an object at the next alarm check. */
void soliton_set_alarm(int object)
{
    edms_alarm_clock[object] = 1;
    edms_wakeup_pending = 1;
}
