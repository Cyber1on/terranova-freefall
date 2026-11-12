/* video.c -- the 2D library's VESA driver for the standalone build: a VBE 2.0 card with a linear
 * frame buffer (port_video.vram), shown by the platform layer.  Same routine names and device
 * table use as src/2d/vesa.c (the DOS driver, which is only compiled for the verification build),
 * so the rest of the 2D library and the game run unchanged.
 *
 * What the card offers: every 8-bit mode of grd_mode_info that is 320 or more wide (linear), plus
 * mode 0 (BIOS mode 13h, 320x200, linear as on a real VGA).  Mode X (modes 1..4) and the high
 * colour modes are not offered, like on a card without them.  The palette goes to the DAC with
 * 6 bits per colour, as on the original. */
#include <string.h>
#include "2d.h"
#include "lg.h"
#include "platform.h"

/* ---------------------------------------------------------------- state */
static void card_set_mode(int w, int h)
{
    port_video_set_mode(w, h, w);
    vesa_bytes_per_line = w;
}

/* ---------------------------------------------------------------- protected mode interface */
void *vesa_get_pm_func(int which)
{
    (void)which;
    return NULL;
}

ushort vesa_alloc_selector(ulong base, ulong limit)
{
    (void)base;
    (void)limit;
    return 0;
}

void vesa_check_logical(int w, int h)
{
    vesa_logical_ok = (long)w * h <= PORT_VRAM_KB * 1024L;
}

char vesa_get_mode_info(long *w, long *bytes_per_line)
{
    *w = port_video.w;
    *bytes_per_line = port_video.pitch;
    return 1;
}

/* logical line width w: the card keeps one byte per pixel; lines = what fits in video memory */
int vesa_set_logical_width(int w, long *lines)
{
    if (w <= 0)
        return 0;
    vesa_bytes_per_line = w;
    grd_screen_row = (short)w;
    port_video.pitch = w;
    *lines = PORT_VRAM_KB * 1024L / w;
    if (*lines > 0x7FFF)
        *lines = 0x7FFF;
    return w;
}

long vesa_set_dac_width(long bits)
{
    (void)bits;
    vesa_dac_width = 6;
    return 6;
}

long vesa_get_dac_width(void)
{
    return 6;
}

long vesa_reset_dac(void)
{
    vesa_dac_switchable = 0;
    vesa_dac_width = 6;
    return 6;
}

/* ---------------------------------------------------------------- palette (8-bit RGB in) */
void vesa_set_pal(int start, int n, uchar *pal)
{
    int i;
    if (n <= 0 || start < 0)
        return;
    for (i = 0; i < n && start + i < 256; i++) {
        port_video.dac[(start + i) * 3] = pal[i * 3] >> 2;
        port_video.dac[(start + i) * 3 + 1] = pal[i * 3 + 1] >> 2;
        port_video.dac[(start + i) * 3 + 2] = pal[i * 3 + 2] >> 2;
    }
    port_video.pal_serial++;
}

void vesa_get_pal(int start, int n, uchar *pal)
{
    int i;
    if (n <= 0 || start < 0)
        return;
    for (i = 0; i < n && start + i < 256; i++) {
        pal[i * 3] = port_video.dac[(start + i) * 3] << 2;
        pal[i * 3 + 1] = port_video.dac[(start + i) * 3 + 1] << 2;
        pal[i * 3 + 2] = port_video.dac[(start + i) * 3 + 2] << 2;
    }
}

void vesa_pm_set_pal(int sub, int start, int n, uchar *buf)
{
    (void)sub;
    (void)start;
    (void)n;
    (void)buf;
}

/* ---------------------------------------------------------------- modes */
int vesa_set_mode_tables(int mode, uchar linear)
{
    grs_mode_info *mi = &grd_mode_info[mode];

    (void)linear;
    vesa_linear = 1;
    vesa_check_logical(mi->w, mi->h);
    card_set_mode(mi->w, mi->h);
    grd_screen_row = (short)mi->w;
    grd_uline_fill_table_list[BMT_DEVICE] = grd_uline_fill_table_set[GRD_TS_FLAT8];
    grd_function_table_list[BMT_DEVICE] = grd_function_table_set[GRD_TS_FLAT8];
    grd_canvas_table_list[BMT_DEVICE] = grd_canvas_table_set[GRD_TS_FLAT8];
    vesa_device_table[GRT_CANVAS_TABLE] = (void (*)())grd_canvas_table_set[GRD_TS_FLAT8];
    grd_mode_cap.vbase = vesa_lfb;
    grd_valloc_mode = 0;
    vesa_pm_bank = vesa_pm_start = vesa_pm_pal = NULL;
    vesa_gran_shift = mi->win;
    vesa_device_table[GRT_SET_BANK] = NULL;
    return 0;
}

/* GRT_SET_MODE.  flags bit 0: clear the screen. */
int vesa_set_mode(int mode, int flags)
{
    if (mode < 0 || mode >= GRD_MODES || !(grd_mode_info[mode].flags & 1) || grd_mode_info[mode].b != 8)
        return -1;
    if (flags & 1)
        memset(port_video.vram, 0, PORT_VRAM_KB * 1024);
    vesa_dac_width = 6;
    return vesa_set_mode_tables(mode, 1);
}

int vesa_get_mode(void)
{
    return grd_mode;
}

int vesa_set_width(int w)
{
    long lines;
    return vesa_set_logical_width(w, &lines);
}

int vesa_get_width(void)
{
    return port_video.pitch;
}

int vesa_get_bytes_per_line(void)
{
    return port_video.pitch;
}

int vesa_max_lines(void)
{
    if (port_video.pitch <= 0)
        return 0;
    return PORT_VRAM_KB * 1024L / port_video.pitch;
}

void vesa_get_focus(long *x, long *y)
{
    if (port_video.pitch <= 0) {
        *x = *y = -1;
        return;
    }
    *x = port_video.start % port_video.pitch;
    *y = port_video.start / port_video.pitch;
}

int vesa_set_start(int x, int y, int wait)
{
    (void)wait;
    port_video.start = (uint32_t)(y * port_video.pitch + x);
    return x;
}

void vesa_set_focus(int x, int y)
{
    vesa_set_start(x, y, 0);
    grd_screen->y = y;
    grd_screen->x = x;
    grd_visible_canvas->bm.bits = grd_screen_canvas->bm.bits + y * vesa_bytes_per_line + x;
}

int vesa_set_bank_pm(int bank) { return bank; }
int vesa_set_bank_pm2(int bank) { return bank; }
int vesa_set_bank_rm(int bank) { return bank; }
int vesa_set_bank_rm2(int bank) { return bank; }

/* video state save / restore (gr_push_video_state / gr_pop_video_state): nothing to keep */
int vesa_save_state(uchar *buf)
{
    (void)buf;
    return 0;
}

int vesa_restore_state(uchar *buf)
{
    (void)buf;
    return 0;
}

/* ---------------------------------------------------------------- detection */
int vesa_get_info(grs_sys_info *info)
{
    grs_mode_info *e;
    short *p = info->modes;

    vesa_version = 0x200;
    vesa_pal_blank = 0;
    vesa_dac_switchable = 0;
    vesa_lfb = vesa_lfb_addr = port_video.vram;
    vesa_lfb_phys = 0xE0000000u;
    info->id_maj = 1;
    info->id_min = 0;
    info->memory = PORT_VRAM_KB;
    vesa_device_table[GRT_SET_PAL] = (void (*)())vesa_set_pal;
    vesa_device_table[GRT_GET_PAL] = (void (*)())vesa_get_pal;
    *p++ = GRM_320x200x8;
    for (e = &grd_mode_info[5]; e < grd_mode_info + GRD_MODES; e++) {
        if (e->b == 8 && e->w >= 320 && (long)e->w * e->h <= PORT_VRAM_KB * 1024L &&
            p < info->modes + 63) {
            e->vmode = 0x100 + (short)(e - grd_mode_info);
            e->flags = (e->flags & ~4) | 1 | 2;
            e->win = 0;
            *p++ = e->mode;
        } else
            e->flags &= ~1;
    }
    *p = -1;
    grd_mode_info[0].flags |= 1 | 2;
    return 1;
}

void vesa_close(void)
{
    vesa_lfb = NULL;
    vesa_version = 0;
    vesa_pm_bank = vesa_pm_start = vesa_pm_pal = NULL;
}
