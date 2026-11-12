/* detect.c -- x:\prj\tech\libsrc\2d\detect.c (Terra Nova v1.09): video hardware detection.
 * Decompiled from __FF.EXE.
 *
 *   0x31AA30 gr_detect
 *
 * Unlike System Shock's (which reads video.cfg), Terra Nova asks the VESA BIOS
 * (vesa_get_info) and falls back to plain VGA with the five 320-wide modes.
 */
#include "2d.h"

extern void (*vga_device_table[])();     /* 0x36DEEC */
extern void (*vesa_device_table[])();    /* 0x36D10C */

/* Fills info and selects the device driver.  Always returns 0. */
int gr_detect(grs_sys_info *info)
{
    if (vesa_get_info(info) == 0) {
        info->id_maj = 0;
        info->id_min = 0;
        info->memory = 256;
        info->modes[0] = GRM_320x200x8;
        info->modes[1] = GRM_320x200x8X;
        info->modes[2] = GRM_320x240x8;
        info->modes[3] = GRM_320x400x8;
        info->modes[4] = GRM_320x480x8;
        info->modes[5] = -1;
        grd_device_table = vga_device_table;
    } else
        grd_device_table = vesa_device_table;
    return 0;
}
