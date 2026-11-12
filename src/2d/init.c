/* init.c -- x:\prj\tech\libsrc\2d\init.c + initint.c (Terra Nova v1.09): starting the 2D
 * system.  Decompiled from __FF.EXE.
 *
 *   0x304BE0 gr_init        0x31AA90 gri_init
 *
 * gr_init ("reworked to avoid linking in unused function tables", INIT.H 1.2) installs the
 * detection function and the function tables this program links -- flat8 (linear 8-bit),
 * bank8 (banked 8-bit VESA) and Mode X -- then calls gri_init.
 */
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
int grd_active = 0;                 /* 0x36C994 */
#endif

/* the linked function tables (data, other workers' modules) */
extern void (*flat8_canvas_table[])();          /* 0x365C50 */
extern void (*flat8_function_table[])();        /* 0x366160 */
extern void (*flat8_uline_fill_table[])();      /* 0x367FD8 */
extern void (*modex_canvas_table[])();          /* 0x36808C */
extern void (*modex_uline_fill_table[])();      /* 0x36859C */
extern void (*bank8_function_table[])();        /* 0x3686E0 */
extern void (*bank8_canvas_table[])();          /* 0x36A558 */
extern void (*bank8_uline_fill_table[])();      /* 0x36AA68 */
extern void (*modex_function_table[])();        /* 0x36AB1C */

/* Starts up the 2D system with the VGA/VESA drivers.  Returns gri_init's result. */
int gr_init(void)
{
    grd_detect_func = (int (*)(grs_sys_info *))WATCOM_FN(gr_detect);
    grd_canvas_table_list[BMT_FLAT8] = flat8_canvas_table;
    grd_function_table_list[BMT_FLAT8] = flat8_function_table;
    grd_uline_fill_table_list[BMT_FLAT8] = flat8_uline_fill_table;
    grd_function_table_set[GRD_TS_FLAT8] = flat8_function_table;
    grd_canvas_table_set[GRD_TS_FLAT8] = flat8_canvas_table;
    grd_uline_fill_table_set[GRD_TS_FLAT8] = flat8_uline_fill_table;
    grd_canvas_table_set[GRD_TS_MODEX] = modex_canvas_table;
    grd_uline_fill_table_set[GRD_TS_MODEX] = modex_uline_fill_table;
    grd_function_table_set[GRD_TS_BANK8] = bank8_function_table;
    grd_canvas_table_set[GRD_TS_BANK8] = bank8_canvas_table;
    grd_uline_fill_table_set[GRD_TS_BANK8] = bank8_uline_fill_table;
    grd_function_table_set[GRD_TS_MODEX] = modex_function_table;
    return gri_init();
}

/* Starts up the 2D system: temporary memory, detection of the video hardware, saving the
 * video state.  Returns 0 or an error code; does nothing if already active. */
int gri_init(void)
{
    int err;

    if (grd_active != 0)
        return 0;
    if (temp_mem_get_stack() == NULL)
        if ((err = temp_mem_init(NULL)) != 0)
            return err;
    if ((err = WATCOM_CALL(GR_IFN(grd_detect_func), (long)&grd_info)) != 0)
        return err;
    gr_push_video_state(1);
    grd_active = 1;
    init_inverse_table();
    return 0;
}
