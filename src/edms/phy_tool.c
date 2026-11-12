/* phy_tool.c -- x:\prj\tech\libsrc\tn_edms\phy_tool.cc (Terra Nova v1.09): debugging tools.
 * Shock: src/Libraries/EDMS/Source/PHY_TOOL.CC.  Decompiled from __FF.EXE.
 *
 *   0x312A98 phy_tool_static_init   0x312ACB mprint_state   0x312B2E inventory_and_statistics
 *   0x312B60 sanity_check           0x312C0E EDMS_frere_jaques   0x312C2D EDMS_crystal_meth
 *
 * TN compiled the printing out (mout), so mprint_state and inventory_and_statistics only run
 * their empty loops; settle_object/snobby_soliton_lite and the Euler-angle tools are not linked.
 */
#include "edms_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
Q edms_pt_consts[5];
#endif

/* File-scope constructor: phy_tool's own copies of the soliton constants
 * (static Q one_sixth = .1666, point_five = .5, point_one_two_five = .125, two = 2, point_1 = .1). */
void phy_tool_static_init(void)
{
    edms_pt_consts[0] = 0x2AAA;
    edms_pt_consts[1] = 0x8000;
    edms_pt_consts[2] = 0x2000;
    edms_pt_consts[3] = 0x20000;
    edms_pt_consts[4] = 0x1999;
}

/* Marc LeBlanc's state printer, with the printing gone: the loops are all that is left. */
void mprint_state(int object)
{
    volatile int coord, deriv;      /* empty loops kept (no effect) */

    if (EDMS_S_ROW(object)[0][0] > edms_END) {
        for (coord = 0; coord < EDMS_DOF && EDMS_S_ROW(object)[coord][0] > edms_END; coord++)
            for (deriv = 0; deriv < 3; deriv++)
                ;
    }
}

/* Inventory of everything in the system, printing compiled out (show_sleepers unused). */
void inventory_and_statistics(int show_sleepers)
{
    volatile int object;            /* empty loop kept (no effect) */

    for (object = 0; object < EDMS_MAX_OBJ && edms_S[object][0][0] > edms_END; object++)
        ;
}

#define EDMS_TOO_MANY_OBJECTS        1
#define EDMS_PHYSICS_HANDLES_CORRUPT 2
#define EDMS_OBJECT_HANDLES_CORRUPT  3

/* EDMS' sanity checker: 0 = fine, else the last problem found.  Quirks kept: the object count
 * scan has no bound (it runs past S into the following globals until a value <= END), and the
 * handle check skips object number 0 (ph2on[ph] > 0). */
int sanity_check(void)
{
    int object;
    physics_handle ph;
    int return_value = 0;

    for (object = 0; EDMS_S_ROW(object)[0][0] > edms_END; object++)
        ;
    if (object > EDMS_MAX_OBJ)
        return_value = EDMS_TOO_MANY_OBJECTS;

    for (object = 0; EDMS_S_ROW(object)[0][0] > edms_END; object++)
        if (EDMS_PH2ON(EDMS_ON2PH(object)) != object)
            return_value = EDMS_PHYSICS_HANDLES_CORRUPT;

    for (ph = 0; ph < EDMS_MAX_OBJ; ph++)
        if (edms_ph2on[ph] > 0 && edms_ph2on[ph] < EDMS_MAX_OBJ)
            if (EDMS_ON2PH(edms_ph2on[ph]) != ph)
                return_value = EDMS_OBJECT_HANDLES_CORRUPT;

    return return_value;
}

/* Is the object awake?  Unreferenced. */
bool EDMS_frere_jaques(physics_handle ph)
{
    bool awake = FALSE;

    if (ph > -1 && edms_no_no_not_me[EDMS_PH2ON(ph)] == 1)
        awake = TRUE;
    return awake;
}

/* Wakes the object.  Unreferenced. */
void EDMS_crystal_meth(physics_handle ph)
{
    if (ph > -1)
        ((int *)edms_launder(edms_no_no_not_me))[EDMS_PH2ON(ph)] = 1;
}
