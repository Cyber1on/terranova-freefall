/* grd.c -- x:\prj\tech\libsrc\2d\grd.c (Terra Nova v1.09): 2D global data.
 * Data only: in the verification build (TN_LINK_ORIGINAL_DATA) every global below is the
 * original one, bound by symbols-2d-core.csv.
 *
 * Data layout (data object): 0x35F8BC grd_screen, 0x35F8C0 grd_pal, 0x35F8C4 grd_bpal,
 * 0x35F8C8 grd_ipal, 0x35F8CC grd_cap, 0x35F8D0 grd_mode, 0x35F8D4 grd_interrupt.
 * BSS: 0x449E90 grd_default_bpal, 0x44AE90 grd_default_pal, 0x44B190 grd_detect_func,
 * 0x44B194 grd_canvas_table, 0x44B198 grd_pixel_table, 0x44B19C grd_device_table,
 * 0x44B1A0 grd_canvas, 0x44B1A4 grd_visible_canvas, 0x44B1A8 grd_screen_canvas,
 * 0x44B1AC grd_mode_cap, 0x44B1B8 grd_info, 0x44B23C grd_canvas_index,
 * 0x44B23E grd_pixel_index, 0x44B240 grd_screen_row, 0x44B242 chn_flags.
 */
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
grs_screen *grd_screen = NULL;
uchar grd_default_pal[768];
uchar *grd_pal = grd_default_pal;
grs_rgb grd_default_bpal[1024];
grs_rgb *grd_bpal = grd_default_bpal;
uchar *grd_ipal = NULL;
grs_canvas *grd_screen_canvas;
grs_canvas *grd_visible_canvas;
grs_canvas *grd_canvas;
grs_sys_info grd_info;
grs_drvcap grd_mode_cap;
grs_drvcap *grd_cap = &grd_mode_cap;
void (**grd_device_table)();
void (**grd_pixel_table)();
void (**grd_canvas_table)();
int grd_mode = -1;
bool grd_interrupt = 0;
short grd_pixel_index, grd_canvas_index;
short grd_screen_row;
uchar chn_flags;
int (*grd_detect_func)(grs_sys_info *info);
grs_context grd_defgc = {0xF};      /* 0x360484 (CONTEXT.C): fcolor 15, rest 0 */
void *(WATCOMCALL *gr_malloc_p)(size_t n) = (void *(WATCOMCALL *)(size_t))malloc;   /* 0x360470 (GRMALLOC.C) */
void (WATCOMCALL *gr_free_p)(void *p) = (void (WATCOMCALL *)(void *))free;        /* 0x360474 */
uchar *tluc8tab[256];               /* 0x44C180 */
uchar **tluc8tab_p = tluc8tab;      /* 0x360478 */
/* table lists and sets (filled by gr_init / the mode setters) */
void (**grd_canvas_table_list[BMT_TYPES])();          /* 0x365194 */
void (**grd_function_table_list[BMT_TYPES])();        /* 0x3604B0 */
void (**grd_uline_fill_table_list[BMT_TYPES])();      /* 0x3604D4 */
void (**grd_canvas_table_set[GRD_TABLE_SETS])();      /* 0x368680 */
void (**grd_function_table_set[GRD_TABLE_SETS])();    /* 0x368650 */
void (**grd_uline_fill_table_set[GRD_TABLE_SETS])();  /* 0x3686B0 */
void (**grd_function_fill_table)();                   /* 0x44C5A0 */
void (**grd_function_table)();                        /* 0x44C5A4 */
void (**grd_uline_fill_table)();                      /* 0x44C5A8 */
void (**grd_uline_fill_vector)();                     /* 0x44C5AC */
/* 0x364DF0 MODE.C: mode, BIOS/VESA mode number, w, h, bits, flags (VESA detection fills in
 * vmode, flags and win for the SVGA modes); 77 entries up to 0x36518C */
grs_mode_info grd_mode_info[GRD_MODES] = {
    {0, 0x13, 320, 200, 8, 1}, {1, -1, 320, 200, 8, 1}, {2, -1, 320, 400, 8, 1},
    {3, -1, 320, 240, 8, 1}, {4, -1, 320, 480, 8, 1}, {5, 0, 640, 350, 4, 0},
    {6, 0, 640, 400, 4, 0}, {7, 0, 640, 480, 4, 0}, {8, 0, 800, 600, 4, 0},
    {9, 0, 1024, 768, 4, 0}, {10, 0, 1280, 1024, 4, 0}, {11, 0, 1600, 1200, 4, 0},
    {12, 0, 320, 200, 8, 1}, {13, 0, 320, 240, 8, 1}, {14, 0, 320, 400, 8, 1},
    {15, 0, 360, 200, 8, 1}, {16, 0, 360, 240, 8, 1}, {17, 0, 360, 400, 8, 1},
    {18, 0, 640, 350, 8, 1}, {19, 0, 640, 400, 8, 1}, {20, 0, 640, 480, 8, 1},
    {21, 0, 800, 600, 8, 1}, {22, 0, 1024, 768, 8, 1}, {23, 0, 1280, 1024, 8, 1},
    {24, 0, 1600, 1200, 8, 1}, {25, 0, 320, 200, 15, 0}, {26, 0, 320, 240, 15, 0},
    {27, 0, 320, 400, 15, 0}, {28, 0, 360, 200, 15, 0}, {29, 0, 360, 240, 15, 0},
    {30, 0, 360, 400, 15, 0}, {31, 0, 640, 350, 15, 0}, {32, 0, 640, 400, 15, 0},
    {33, 0, 640, 480, 15, 0}, {34, 0, 800, 600, 15, 0}, {35, 0, 1024, 768, 15, 0},
    {36, 0, 1280, 1024, 15, 0}, {37, 0, 1600, 1200, 15, 0}, {38, 0, 320, 200, 16, 1},
    {39, 0, 320, 240, 16, 1}, {40, 0, 320, 400, 16, 1}, {41, 0, 360, 200, 16, 1},
    {42, 0, 360, 240, 16, 1}, {43, 0, 360, 400, 16, 1}, {44, 0, 640, 350, 16, 1},
    {45, 0, 640, 400, 16, 1}, {46, 0, 640, 480, 16, 1}, {47, 0, 800, 600, 16, 1},
    {48, 0, 1024, 768, 16, 1}, {49, 0, 1280, 1024, 16, 1}, {50, 0, 1600, 1200, 16, 1},
    {51, 0, 320, 200, 24, 1}, {52, 0, 320, 240, 24, 1}, {53, 0, 320, 400, 24, 1},
    {54, 0, 360, 200, 24, 1}, {55, 0, 360, 240, 24, 1}, {56, 0, 360, 400, 24, 1},
    {57, 0, 640, 350, 24, 1}, {58, 0, 640, 400, 24, 1}, {59, 0, 640, 480, 24, 1},
    {60, 0, 800, 600, 24, 1}, {61, 0, 1024, 768, 24, 1}, {62, 0, 1280, 1024, 24, 1},
    {63, 0, 1600, 1200, 24, 1}, {64, 0, 320, 200, 32, 0}, {65, 0, 320, 240, 32, 0},
    {66, 0, 320, 400, 32, 0}, {67, 0, 360, 200, 32, 0}, {68, 0, 360, 240, 32, 0},
    {69, 0, 360, 400, 32, 0}, {70, 0, 640, 350, 32, 0}, {71, 0, 640, 400, 32, 0},
    {72, 0, 640, 480, 32, 0}, {73, 0, 800, 600, 32, 0}, {74, 0, 1024, 768, 32, 0},
    {75, 0, 1280, 1024, 32, 0}, {76, 0, 1600, 1200, 32, 0}
};
#endif

