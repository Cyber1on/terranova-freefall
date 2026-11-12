/* statestk.c -- x:\prj\tech\libsrc\2d\stastk.c (Terra Nova v1.09): video state stack.
 * Decompiled from __FF.EXE.
 *
 *   0x31D5D0 gr_push_video_state     0x31D628 gr_pop_video_state
 *
 * Terra Nova keeps up to 5 states of 790 bytes in a fixed array (System Shock: a 2K byte
 * stack with variable-size entries).  The device's save routine returns nonzero on success.
 * The flags/clear arguments are not used.
 */
#include "2d.h"

#define GRD_STATE_SIZE 790
#define GRD_STATE_MAX 5

#ifndef TN_LINK_ORIGINAL_DATA
uchar grd_state_stack[GRD_STATE_MAX][GRD_STATE_SIZE];     /* 0x45E34C */
int grd_state_depth = 0;                                   /* 0x36D160 */
#else
extern uchar grd_state_stack[GRD_STATE_MAX][GRD_STATE_SIZE];
extern int grd_state_depth;
#endif

/* Saves the video state.  Returns 0, or 1 if the stack is full or the save failed. */
int gr_push_video_state(int flags)
{
    if (grd_state_depth == GRD_STATE_MAX)
        return 1;
    if (WATCOM_CALL(GR_IFN(grd_device_table[GRT_SAVE_STATE]), (long)grd_state_stack[grd_state_depth]) == 0)
        return 1;
    grd_state_depth++;
    return 0;
}

/* Restores the last saved video state.  Returns 1 if the device's restore returned 0
 * (failure), else 0; 0 if nothing was saved. */
int gr_pop_video_state(int clear)
{
    if (grd_state_depth == 0)
        return 0;
    grd_state_depth--;
    return WATCOM_CALL(GR_IFN(grd_device_table[GRT_RESTORE_STATE]), (long)grd_state_stack[grd_state_depth]) == 0;
}
