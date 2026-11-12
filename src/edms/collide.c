/* collide.c -- x:\prj\tech\libsrc\tn_edms\collide.cc (Terra Nova v1.09): the collision hash
 * ("terrain database" of object presences).  Shock: src/Libraries/EDMS/Source/COLLIDE.CC.
 * Decompiled from __FF.EXE.
 *
 *   0x3126C8 generic_write_object   0x3127C8 generic_delete_object   0x3128C5 object_check_hash
 *   0x312948 reset_collisions       0x3129AB exclude_from_collisions 0x312A3A are_you_there
 *
 * The playfield is divided into a 100 x 100 grid (hash = hash_scale * position, hash_scale =
 * 100 / playfield size).  data[x][y] has bit (1 << (object & 31)) set when object may be in that
 * cell; an object writes a 2x2 block of cells around itself.  Objects n, n+32 and n+64 share a
 * bit, which object_check_hash resolves.
 *
 * Quirks kept: are_you_there indexes data[][] without a bounds check; an object at hash 1 or 98
 * is "off the playfield" (awol callback) even though cells 0..99 exist.
 */
#include "edms_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
unsigned int edms_data[EDMS_DATA_SIZE][EDMS_DATA_SIZE];
unsigned int edms_test_bitmask;
#endif

#define COLLISION_MAX  (EDMS_DATA_SIZE - 1)
#define DELTA_BY_TWO   0x8000               /* .5 */
#define COLLIDE_NONE   (-0x10000)           /* -1.0 in I[o][IDOF_COLLIDE] */

/* data[][] through a laundered pointer: are_you_there's index is not bounds checked. */
#define DATA(x, y) (((unsigned int (*)[EDMS_DATA_SIZE])edms_launder(edms_data))[x][y])

/* Sets (write) or clears (delete) the object's bit in the 2x2 cell block nearest its position
 * in state; an object outside cells 2..98 is reported to the game's awol callback instead. */
static inline void hash_object(int object, EDMS_Argblock_Pointer state, int write)
{
    Q q_hash_x = edms_q_mul(edms_hash_scale, state[object][0][0]);
    Q q_hash_y = edms_q_mul(edms_hash_scale, state[object][1][0]);
    unsigned int obit = 1u << (object & 31);
    int hash_x = edms_q_to_int(q_hash_x);
    int hash_y = edms_q_to_int(q_hash_y);

    if (hash_x > 1 && hash_y > 1 && hash_x < COLLISION_MAX && hash_y < COLLISION_MAX) {
        /* Upper left corner of the 2x2 block, by which half of the cell we are in. */
        if (q_hash_x - (hash_x << 16) < DELTA_BY_TWO)
            hash_x--;
        if (q_hash_y - (hash_y << 16) < DELTA_BY_TWO)
            hash_y--;
        if (write) {
            DATA(hash_x, hash_y) |= obit;
            DATA(hash_x, hash_y + 1) |= obit;
            DATA(hash_x + 1, hash_y) |= obit;
            DATA(hash_x + 1, hash_y + 1) |= obit;
        } else {
            DATA(hash_x, hash_y) &= ~obit;
            DATA(hash_x, hash_y + 1) &= ~obit;
            DATA(hash_x + 1, hash_y) &= ~obit;
            DATA(hash_x + 1, hash_y + 1) &= ~obit;
        }
    } else {
        /* Inform the world that something stinks in Denmark... */
        WATCOM_CALL(edms_off_playfield, EDMS_ON2PH(object));
    }
}

void generic_write_object(int object, EDMS_Argblock_Pointer state)
{
    hash_object(object, state, 1);
}

void generic_delete_object(int object, EDMS_Argblock_Pointer state)
{
    hash_object(object, state, 0);
}

/* Could object be the one in hash cell (hx, hy)?  Uses A while integrating (A_is_active) if the
 * object is awake, else S.  abs is the CRT's (0x325111). */
bool object_check_hash(int object, int hx, int hy)
{
    EDMS_Argblock_Pointer state = (edms_A_is_active && edms_no_no_not_me[object]) ? edms_A : edms_S;
    int my_hx = edms_q_to_int(edms_q_mul(edms_hash_scale, state[object][0][0]));
    int my_hy = edms_q_to_int(edms_q_mul(edms_hash_scale, state[object][1][0]));

    return crt_labs(my_hx - hx) <= 1 && crt_labs(my_hy - hy) <= 1;
}

/* Clears the object's collision exclusion, on both sides. */
void reset_collisions(int object)
{
    Q *I = EDMS_I_ROW(object);

    if (I[EDMS_IDOF_COLLIDE] > COLLIDE_NONE) {
        EDMS_I_ROW(edms_q_to_int(I[EDMS_IDOF_COLLIDE]))[EDMS_IDOF_COLLIDE] = COLLIDE_NONE;
        I[EDMS_IDOF_COLLIDE] = COLLIDE_NONE;
    }
}

/* guy_1 and guy_2 ignore each other; any previous exclusion of either is reset first. */
void exclude_from_collisions(int guy_1, int guy_2)
{
    if (EDMS_I_ROW(guy_1)[EDMS_IDOF_COLLIDE] > COLLIDE_NONE ||
        EDMS_I_ROW(guy_2)[EDMS_IDOF_COLLIDE] > COLLIDE_NONE) {
        reset_collisions(guy_1);
        reset_collisions(guy_2);
    }
    EDMS_I_ROW(guy_1)[EDMS_IDOF_COLLIDE] = (Q)((unsigned)guy_2 << 16);
    EDMS_I_ROW(guy_2)[EDMS_IDOF_COLLIDE] = (Q)((unsigned)guy_1 << 16);
}

/* Bits of every object that may share the hash cell of object's A position (test_bitmask). */
unsigned int are_you_there(int object)
{
    return edms_test_bitmask = DATA(edms_q_to_int(edms_q_mul(edms_hash_scale, edms_A[object][0][0])),
                                    edms_q_to_int(edms_q_mul(edms_hash_scale, edms_A[object][1][0])));
}
