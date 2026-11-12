/* clprect.c -- clip/clprect.c (Terra Nova v1.09): rectangle clipper.
 * Decompiled from __FF.EXE.
 *
 *   0x34A150 gr_clip_rect
 *
 * Clips the rectangle [*left,*right) x [*top,*bot) to the canvas int clip rectangle in place.
 * Returns CLIP_ALL when it lies wholly outside (right <= clip.left, left >= clip.right,
 * bot <= clip.top or top >= clip.bot; nothing is changed then), else the OR of CLIP_LEFT/TOP/
 * RIGHT/BOT for each edge that was moved.  As in System Shock.
 */
#include "2d.h"

int gr_clip_rect(short *left, short *top, short *right, short *bot)
{
    int code = CLIP_NONE;

    if (*right <= grd_int_clip.left || *left >= grd_int_clip.right ||
        *bot <= grd_int_clip.top || *top >= grd_int_clip.bot)
        return CLIP_ALL;
    if (*left < grd_int_clip.left) {
        *left = grd_int_clip.left;
        code = CLIP_LEFT;
    }
    if (*right > grd_int_clip.right) {
        *right = grd_int_clip.right;
        code |= CLIP_RIGHT;
    }
    if (*top < grd_int_clip.top) {
        *top = grd_int_clip.top;
        code |= CLIP_TOP;
    }
    if (*bot > grd_int_clip.bot) {
        *bot = grd_int_clip.bot;
        code |= CLIP_BOT;
    }
    return code;
}
