/* mode.c -- x:\prj\tech\libsrc\2d\mode.c + smode.asm (Terra Nova v1.09): graphics modes.
 * Decompiled from __FF.EXE.
 *
 *   0x308DF0 gr_set_mode (asm)        0x364DF0 grd_mode_info[77] (data, see 2d.h)
 *
 * gr_set_mode is hand-written asm (SMODE.ASM); the C below does the same.  Quirks kept:
 *  - a mode not in grd_info.modes returns (mode & 0xFFFF0000) | 0xFFFF (the asm only loads
 *    the -1 terminator into ax);
 *  - the device's INIT_DEVICE is called with its own address as argument;
 *  - the aspect ratio is computed unsigned and faults (#DE) for a mode of height 0.
 */
#include "2d.h"

int gr_set_mode(int mode, int clear)
{
    short *m;
    void (**dev)() = grd_device_table;
    grs_mode_info *mi;
    int err;
    ulong w, h;

    for (m = grd_info.modes; *m != -1; m++)
        if ((short)mode == *m)
            goto found;
    return (mode & 0xFFFF0000) | 0xFFFF;
found:
    grd_screen_row = 0;
    if ((err = WATCOM_CALL(GR_IFN(dev[GRT_SET_MODE]), mode, clear)) != 0)
        return err;
    if (dev[GRT_INIT_DEVICE] != NULL)
        WATCOM_CALL(dev[GRT_INIT_DEVICE], (long)dev[GRT_INIT_DEVICE], clear, mode);
    grd_canvas_table = grd_pixel_table = grd_canvas_table_list[BMT_DEVICE];
    grd_mode = mode;
    mi = &grd_mode_info[mode];
    grd_mode_cap.w = mi->w;
    grd_mode_cap.h = mi->h;
    if (grd_screen_row == 0)
        grd_screen_row = WATCOM_CALL(GR_IFN(grd_canvas_table[CALC_ROW]), (ushort)mi->w);
    w = (ushort)mi->w;
    h = (ushort)mi->h;
    grd_mode_cap.aspect = (0xC000 * w) / h;
    return 0;
}
