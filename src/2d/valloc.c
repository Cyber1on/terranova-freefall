/* valloc.c -- x:\prj\tech\libsrc\2d\valloc.c (Terra Nova v1.09): video memory "allocation".
 *
 *   0x309AE0 valloc        0x309AEC vfree
 *
 * vfree is the `ret` inside valloc's code (the two functions overlap in the binary).
 * valloc never fails, so callers' checks for -1 never trigger.
 */
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar grd_valloc_mode = 0;          /* 0x3651B8: nonzero = banked mode, no linear frame buffer */
#endif

/* Returns the start of video memory (NULL in banked modes). */
uchar *valloc(short w, short h)
{
    if (grd_valloc_mode)
        return NULL;
    return grd_cap->vbase;
}

void vfree(uchar *p)
{
}
