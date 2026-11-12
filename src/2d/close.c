/* close.c -- x:\prj\tech\libsrc\2d\close.c (Terra Nova v1.09): shutting down the 2D system.
 *
 *   0x3090D0 gr_close
 *
 * Terra Nova restores the saved video state only if a mode was ever set.
 */
#include "2d.h"

int gr_close(void)
{
    if (grd_active == 0)
        return 0;
    if (grd_mode != -1)
        gr_pop_video_state(TRUE);
    if (grd_device_table[GRT_CLOSE_DEVICE] != NULL)
        WATCOM_CALL(grd_device_table[GRT_CLOSE_DEVICE], (long)&grd_info);
    grd_active = 0;
    return 0;
}
