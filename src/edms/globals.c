/* globals.c -- x:\prj\tech\libsrc\tn_edms\globals.cc (Terra Nova v1.09): EDMS model type codes
 * and END.  Shock: src/Libraries/EDMS/Source/GLOBALS.CC.  Decompiled from __FF.EXE.
 *
 *   0x312C48 globals_static_init
 *
 * The Q globals are set by a file-scope constructor (Q has a constructor, so they are not static
 * data).  TN adds a ninth type (8, unused by the linked models).  min_physics_handle
 * (0x45D618) is plain data (0), set by EDMS_initialize.
 */
#include "edms_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
Q edms_END, edms_VACUUM, edms_MARBLE, edms_FIELD_POINT, edms_ROBOT, edms_BIPED, edms_PELVIS,
  edms_DEATH, edms_D_FRAME, edms_TYPE8;
int edms_min_physics_handle;
#endif

void globals_static_init(void)
{
    edms_END = -9999 * 0x10000;
    edms_VACUUM = 0;
    edms_MARBLE = 0x10000;
    edms_FIELD_POINT = 0x20000;
    edms_ROBOT = 0x30000;
    edms_BIPED = 0x40000;
    edms_PELVIS = 0x50000;
    edms_DEATH = 0x60000;
    edms_D_FRAME = 0x70000;
    edms_TYPE8 = 0x80000;
}
