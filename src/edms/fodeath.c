/* fodeath.c -- x:\prj\tech\libsrc\tn_edms\models\fodeath.cc (Terra Nova v1.09): the interface
 * of the DEATH model (projectiles and debris).  Shock: src/Libraries/EDMS/Source/MODELS/FODEATH.CC.
 * Decompiled from __FF.EXE.  The code sits in the middle of game code (0x2FD8E8-0x2FDE00).
 *
 *   0x2FDC7C EDMS_make_death   0x2FDD10 EDMS_set_death_parameters   0x2FDD5C EDMS_get_death_parameters
 *
 * Quirks kept: EDMS_make_death leaves params[4..9] and the acceleration column of init_state
 * uninitialised (stack garbage reaches I[24..29]); set/get don't check the handle.
 */
#include "edms_a.h"

/* Creates a DEATH object and returns its physics handle.  params: mass, size, gravity, 1/mass;
 * the other 6 are not set.  1/mass faults (#DE) for mass 0 or |mass| <= 1/65536, as in the
 * original. */
physics_handle EDMS_make_death(Death *d, State *s)
{
    Q params[10];
    Q init_state[6][3];
    object_number on;

    init_state[0][0] = s->X;  init_state[0][1] = s->X_dot;
    init_state[1][0] = s->Y;  init_state[1][1] = s->Y_dot;
    init_state[2][0] = s->Z;  init_state[2][1] = s->Z_dot;

    params[0] = d->mass;
    params[1] = d->size;
    params[2] = d->gravity;
    params[3] = edms_q_div(0x10000, d->mass);
    on = make_death(init_state, params);
    return EDMS_bind_object_number(on);
}

/* I[20..22] = mass, size, gravity; I[23] = 1/mass.  Unreferenced. */
void EDMS_set_death_parameters(physics_handle ph, Death *d)
{
    Q mass = d->mass, size = d->size, gravity = d->gravity;
    Q *I = EDMS_I_ROW(EDMS_PH2ON(ph));

    I[20] = mass;
    I[21] = size;
    I[22] = gravity;
    I[23] = edms_q_div(0x10000, mass);
}

/* Reads back mass, size, gravity; the fourth field is set to 0.  Unreferenced. */
void EDMS_get_death_parameters(physics_handle ph, Death *d)
{
    Q *I = EDMS_I_ROW(EDMS_PH2ON(ph));

    d->mass = I[20];
    d->size = I[21];
    d->gravity = I[22];
    d->drag = 0;
}
