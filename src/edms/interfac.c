/* interfac.c -- x:\prj\tech\libsrc\tn_edms\interfac.cc (Terra Nova v1.09): the EDMS interface
 * layer (Shock: src/Libraries/EDMS/Source/INTERFAC.CC), plus the fix-to-Q projectile wrapper in
 * front of it and the TN marble API (Shock MODELS/MARBFACE.CC style).  Decompiled from __FF.EXE.
 *
 *   0x2F074C EDMS_cast_projectile (name ours)     0x2F0810 interfac_static_init
 *   0x2F081B terrain               0x2F0829 ff_terrain              0x2F0847 ff_raycast
 *   0x2F086B EDMS_startup          0x2F08A2 EDMS_set_workspace      0x2F08A8 EDMS_set_autodestruct
 *   0x2F08D1 EDMS_defuse_autodestruct                               0x2F08FA EDMS_kill_object
 *   0x2F092D EDMS_get_state        0x2F097C EDMS_get_state_accel (name ours)
 *   0x2F09E3 EDMS_holistic_teleport                                 0x2F0A66 EDMS_ignore_collisions
 *   0x2F0A83 EDMS_obey_collisions  0x2F0A94 EDMS_mprint_state       0x2F0AA0 EDMS_inventory_and_statistics
 *   0x2F0AA5 EDMS_sanity_check     0x2F0AAA EDMS_soliton_vector     0x2F0ABB EDMS_init_handles
 *   0x2F0ADD EDMS_bind_object_number                                0x2F0AFA EDMS_remap_object_number
 *   0x2F0B29 EDMS_release_object   0x2F0B48 EDMS_get_free_ph        0x2F0B66 EDMS_set_param39 (name ours)
 *   0x2F0B88 EDMS_set_X_dot        0x2F0BA8 EDMS_set_Y_dot          0x2F0BC8 EDMS_set_Z_dot (names ours)
 *   0x2F0BE8 EDMS_get_marble_parameters                             0x2F0C55 EDMS_control_marble
 *   0x2F0C7D EDMS_marble_set_I11 (name ours)                        0x2F0CA5 EDMS_marble_add_I14 (name ours)
 *   0x2F0CCD EDMS_make_marble      0x2F0DBC EDMS_set_marble_parameters
 *
 * The Q helpers 0x2F07E0-0x2F080C (compiler-generated, reached only from the C++ runtime) stay
 * original.  Physics handles (ph) are the game's names for objects; object numbers (on) are
 * indices into S/I and change when soliton packs the arrays (EDMS_kill).
 *
 * Quirks kept: most functions don't check the handle (ph2on[-1] reads the terrain-event callback
 * pointer at 0x44B9E0); EDMS_cast_projectile looks up ph2on[exclude] even for exclude = -1;
 * EDMS_make_marble leaves params[9] (-> I[29]) uninitialised; EDMS_make_marble/make_death bind a
 * handle even when the model returns -1 (ph2on[ph] = -1, on2ph[-1] = ph, i.e. 0x44BB60).
 */
#include "edms_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
void *edms_object_collision;
void *edms_autodestruct;
void *edms_off_playfield;
void *edms_sleepy_snoozy;
void *edms_terrain_event;
object_number edms_ph2on[EDMS_MAX_OBJ];
physics_handle edms_on2ph[EDMS_MAX_OBJ];
Q edms_fix_zero;
#endif

/* File-scope constructor (XI table): fix_zero = 0. */
void interfac_static_init(void)
{
    edms_fix_zero = 0;
}

/* FF_FACE-style wrapper: fix arguments in, Q arguments out to EDMS_FF_cast_projectile.  The
 * direction is copied to a local Q[3]; X is passed as is.  exclude is a physics handle and is
 * translated without a check (exclude = -1 reads ph2on[-1]).  The original then copies X[1] and
 * X[2] onto themselves (Q -> fix conversion of an aliased array), a no-op. */
physics_handle EDMS_cast_projectile(fix X[3], fix D[3], fix speed, fix mass, fix size, fix range,
                                    physics_handle exclude, physics_handle shooter, long *g_info,
                                    long *w_info, bool *hit)
{
    Q dir[3];
    Q q_speed = speed, q_mass = mass, q_size = size, q_range = range;

    dir[0] = D[0];
    dir[1] = D[1];
    dir[2] = D[2];
    return EDMS_FF_cast_projectile(X, dir, &q_speed, &q_mass, &q_size, &q_range,
                                   EDMS_PH2ON(exclude), shooter, g_info, w_info, hit);
}

/* Q terrain(Q X, Q Y, int deriv): the game's height function.  Unreferenced. */
Q *terrain(Q *X, Q *Y, int deriv, Q *ret)
{
    *ret = Terrain(*X, *Y, deriv);
    return ret;
}

/* Freefall terrain query: the game fills tff for the point (X, Y, Z). */
bool ff_terrain(Q *X, Q *Y, Q *Z, bool fast, terrain_ff *tff)
{
    return FF_terrain(*X, *Y, *Z, fast, tff);
}

/* Ray cast against the terrain (for EDMS_FF_cast_projectile). */
bool ff_raycast(Q *x, Q *y, Q *z, Q vec[3], Q *range, Q where_hit[3], terrain_ff *tff)
{
    return FF_raycast(*x, *y, *z, vec, *range, where_hit, tff);
}

/* Starts EDMS: soliton's initialisation, the handle tables, then the 5 game callbacks. */
void EDMS_startup(EDMS_data *d)
{
    EDMS_initialize(d);
    EDMS_init_handles();
    edms_object_collision = d->collision_callback;
    edms_autodestruct = d->autodestruct_callback;
    edms_off_playfield = d->awol_callback;
    edms_sleepy_snoozy = d->snooz_callback;
    edms_terrain_event = d->terrain_event_callback;
}

/* Tells EDMS what space to use for A.  Unreferenced. */
void EDMS_set_workspace(void *place)
{
    edms_A = (EDMS_Argblock_Pointer)place;
}

/* Autodestruct: the object dies at its next impact (deadly_idof sets I[38] < 0). */
void EDMS_set_autodestruct(physics_handle ph)
{
    if (ph > -1)
        EDMS_I_ROW(EDMS_PH2ON(ph))[EDMS_IDOF_AUTODESTRUCT] = 0x10000;
}

/* Unreferenced. */
void EDMS_defuse_autodestruct(physics_handle ph)
{
    if (ph > -1)
        EDMS_I_ROW(EDMS_PH2ON(ph))[EDMS_IDOF_AUTODESTRUCT] = 0;
}

/* Removes an object: soliton packs the arrays down over it, so every later object number
 * moves down by one.  TN drops Shock's "killed while integrating" message. */
void EDMS_kill_object(physics_handle ph)
{
    object_number on, i;

    if (ph > -1) {
        on = EDMS_PH2ON(ph);
        EDMS_kill(on);
        EDMS_release_object(ph);
        for (i = on + 1; i < EDMS_MAX_OBJ; i++)
            EDMS_remap_object_number(i, i - 1);
    }
}

/* Position and velocity of an object (TN State has no angles).  Nothing is written for an
 * unbound handle. */
void EDMS_get_state(physics_handle ph, State *s)
{
    object_number on = EDMS_PH2ON(ph);

    if (on > -1 && on < EDMS_MAX_OBJ) {
        s->X = edms_S[on][0][0];  s->X_dot = edms_S[on][0][1];
        s->Y = edms_S[on][1][0];  s->Y_dot = edms_S[on][1][1];
        s->Z = edms_S[on][2][0];  s->Z_dot = edms_S[on][2][1];
    }
}

/* TN: EDMS_get_state plus the accelerations (name ours). */
void EDMS_get_state_accel(physics_handle ph, StateAccel *s)
{
    object_number on = EDMS_PH2ON(ph);

    if (on > -1 && on < EDMS_MAX_OBJ) {
        s->X = edms_S[on][0][0];  s->X_dot = edms_S[on][0][1];  s->X_ddot = edms_S[on][0][2];
        s->Y = edms_S[on][1][0];  s->Y_dot = edms_S[on][1][1];  s->Y_ddot = edms_S[on][1][2];
        s->Z = edms_S[on][2][0];  s->Z_dot = edms_S[on][2][1];  s->Z_ddot = edms_S[on][2][2];
    }
}

/* Moves an object: out of the collision grid, new position/velocity with zero accelerations,
 * back into the grid. */
void EDMS_holistic_teleport(physics_handle ph, State *s)
{
    object_number on;

    if (ph > -1) {
        on = EDMS_PH2ON(ph);
        generic_delete_object(on, edms_S);
        EDMS_S_ROW(on)[0][0] = s->X;
        EDMS_S_ROW(on)[0][1] = s->X_dot;
        EDMS_S_ROW(on)[1][0] = s->Y;
        EDMS_S_ROW(on)[1][1] = s->Y_dot;
        EDMS_S_ROW(on)[2][0] = s->Z;
        EDMS_S_ROW(on)[2][1] = s->Z_dot;
        EDMS_S_ROW(on)[0][2] = 0;
        EDMS_S_ROW(on)[1][2] = 0;
        EDMS_S_ROW(on)[2][2] = 0;
        generic_write_object(on, edms_S);
    }
}

/* The two objects stop colliding with each other (one partner per object). */
void EDMS_ignore_collisions(physics_handle ph1, physics_handle ph2)
{
    if (ph1 > -1 && ph2 > -1)
        exclude_from_collisions(EDMS_PH2ON(ph1), EDMS_PH2ON(ph2));
}

/* Unreferenced. */
void EDMS_obey_collisions(physics_handle ph)
{
    if (ph > -1)
        reset_collisions(EDMS_PH2ON(ph));
}

/* Unreferenced; no handle check. */
void EDMS_mprint_state(physics_handle ph)
{
    mprint_state(EDMS_PH2ON(ph));
}

/* Unreferenced. */
void EDMS_inventory_and_statistics(int show_sleepers)
{
    inventory_and_statistics(show_sleepers);
}

/* 0 = fine, else an error code (see sanity_check). */
int EDMS_sanity_check(void)
{
    return sanity_check();
}

/* The per-frame entry: advance the simulation by timestep seconds (fix). */
void EDMS_soliton_vector(fix timestep)
{
    Q t = timestep;

    soliton_vector(&t);
}

/* Every handle and object number unbound. */
void EDMS_init_handles(void)
{
    int i;

    for (i = 0; i < EDMS_MAX_OBJ; ++i)
        edms_ph2on[i] = edms_on2ph[i] = -1;
}

/* Gives object number on a free physics handle; -1 if none is left. */
physics_handle EDMS_bind_object_number(object_number on)
{
    physics_handle ph = EDMS_get_free_ph();

    if (ph != -1) {
        EDMS_PH2ON(ph) = on;
        EDMS_ON2PH(on) = ph;
    }
    return ph;
}

/* The handle of object #old now refers to object #nu.  Nothing happens if old is unbound. */
void EDMS_remap_object_number(object_number old, object_number nu)
{
    physics_handle ph = EDMS_ON2PH(old);

    if (ph == -1)
        return;
    EDMS_ON2PH(nu) = ph;
    EDMS_ON2PH(old) = -1;
    EDMS_PH2ON(ph) = nu;
}

/* Unbinds a handle (no check: ph must be valid). */
void EDMS_release_object(physics_handle ph)
{
    object_number on = EDMS_PH2ON(ph);

    EDMS_ON2PH(on) = -1;
    EDMS_PH2ON(ph) = -1;
}

/* First unbound handle >= min_physics_handle, or -1. */
physics_handle EDMS_get_free_ph(void)
{
    int i;

    for (i = edms_min_physics_handle; i < EDMS_MAX_OBJ; ++i)
        if (EDMS_PH2ON(i) == -1)
            return i;
    return -1;
}

/* TN (name ours): I[39] (marble: collision-centre Z offset).  No handle check. */
void EDMS_set_param39(physics_handle ph, fix v)
{
    EDMS_I_ROW(EDMS_PH2ON(ph))[EDMS_IDOF_PARAM39] = v;
}

/* TN (names ours): set one velocity component.  No handle check. */
void EDMS_set_X_dot(physics_handle ph, fix v)
{
    EDMS_S_ROW(EDMS_PH2ON(ph))[0][1] = v;
}

void EDMS_set_Y_dot(physics_handle ph, fix v)
{
    EDMS_S_ROW(EDMS_PH2ON(ph))[1][1] = v;
}

void EDMS_set_Z_dot(physics_handle ph, fix v)
{
    EDMS_S_ROW(EDMS_PH2ON(ph))[2][1] = v;
}

/* Reads back the TN marble parameters (not 1/mass).  Unreferenced; no handle check. */
void EDMS_get_marble_parameters(physics_handle ph, Marble *m)
{
    Q *I = EDMS_I_ROW(EDMS_PH2ON(ph));
    int i;

    for (i = 0; i < 8; i++)
        m->p[i] = I[20 + i];
    m->p[8] = I[32];
    m->p[9] = I[33];
}

/* Marble control (I[17..19]); wakes the marble if any is non-zero.  No handle check. */
void EDMS_control_marble(physics_handle ph, fix X, fix Y, fix Z)
{
    Q x = X, y = Y, z = Z;

    marble_set_control(EDMS_PH2ON(ph), &x, &y, &z);
}

/* TN (name ours): I[11..13], wakes the marble.  No handle check. */
void EDMS_marble_set_I11(physics_handle ph, fix X, fix Y, fix Z)
{
    Q x = X, y = Y, z = Z;

    marble_set_I11(EDMS_PH2ON(ph), &x, &y, &z);
}

/* TN (name ours): I[14..16] += (X, Y, Z).  No handle check. */
void EDMS_marble_add_I14(physics_handle ph, fix X, fix Y, fix Z)
{
    Q x = X, y = Y, z = Z;

    marble_add_I14(EDMS_PH2ON(ph), &x, &y, &z);
}

/* Creates a marble and returns its physics handle.  params: m->p[0..7], 1/mass, [9] left
 * uninitialised (the original does not set it; it ends up in I[29]), m->p[8], m->p[9].
 * 1/mass faults (#DE) for mass 0 or |mass| <= 1/65536, as in the original. */
physics_handle EDMS_make_marble(Marble *m, State *s)
{
    Q params[12];
    Q init_state[3][3];
    object_number on;

    init_state[0][0] = s->X;  init_state[0][1] = s->X_dot;
    init_state[1][0] = s->Y;  init_state[1][1] = s->Y_dot;
    init_state[2][0] = s->Z;  init_state[2][1] = s->Z_dot;
    init_state[0][2] = init_state[1][2] = init_state[2][2] = 0;

    params[0] = m->p[0];
    params[1] = m->p[1];
    params[2] = m->p[2];
    params[3] = m->p[3];
    params[4] = m->p[4];
    params[5] = m->p[5];
    params[6] = m->p[6];
    params[7] = m->p[7];
    params[8] = edms_q_div(0x10000, m->p[0]);
    params[10] = m->p[8];
    params[11] = m->p[9];
    on = make_marble(init_state, params);
    return EDMS_bind_object_number(on);
}

/* Changes a marble's parameters: I[20..27] = p[0..7], I[28] = 1/mass, I[32] = p[8],
 * I[33] = p[9].  No handle check.  Note: I[31] (collision radius) is not updated. */
void EDMS_set_marble_parameters(physics_handle ph, Marble *m)
{
    Q p[10];
    Q *I;
    int i;

    for (i = 0; i < 10; i++)
        p[i] = m->p[i];
    I = EDMS_I_ROW(EDMS_PH2ON(ph));
    for (i = 0; i < 8; i++)
        I[20 + i] = p[i];
    I[28] = edms_q_div(0x10000, p[0]);
    I[32] = p[8];
    I[33] = p[9];
}
