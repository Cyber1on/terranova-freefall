/* vesa.c -- Terra Nova v1.09 VESA BIOS Extensions device driver (DOS, through DPMI; no
 * System Shock counterpart, names ours).  Hardware access: video BIOS (int 10h AX=4Fxxh)
 * through DPMI int 31h, the VBE 2.0 protected mode interface, DPMI selectors and physical
 * mapping of the linear frame buffer.
 *
 *   0x307910 vesa_get_pm_func       0x3079F4 vesa_alloc_selector   0x307AC0 vesa_check_logical
 *   0x307B64 vesa_get_mode_info     0x307C80 vesa_set_logical_width 0x307D54 vesa_set_dac_width
 *   0x307E20 vesa_get_dac_width     0x307E94 vesa_reset_dac        0x307EA8 vesa_set_pal
 *   0x308040 vesa_get_pal           0x3081F0 vesa_set_mode_tables  0x308490 vesa_set_mode
 *   0x3085BC vesa_get_mode          0x308640 vesa_set_width        0x308650 vesa_get_width
 *   0x3086DC vesa_get_bytes_per_line (unreferenced)                0x308768 vesa_max_lines
 *   0x30880C vesa_get_focus         0x308880 vesa_set_focus        0x3088DC vesa_get_info
 *   0x308D4C vesa_close
 *   asm helpers: 0x31D220 vesa_set_bank_pm  0x31D240 vesa_set_bank_pm2  0x31D269 vesa_set_bank_rm
 *   0x31D2E7 vesa_set_bank_rm2  0x31D383 vesa_set_start  0x31D418 vesa_pm_set_pal
 *   data 0x36D10C vesa_device_table, 0x44DB10.. / 0x364DCC.. driver state (see 2d.h)
 *
 * vesa_get_info (from gr_detect) reads the VBE info block and the mode info of every mode
 * 100h..1FFh, fills vmode/flags/win of the matching grd_mode_info entries and the mode list of
 * grd_info, and maps the linear frame buffer (VBE 2.0) once.  vesa_set_mode sets a mode, then
 * vesa_set_mode_tables picks the canvas/function tables of the bit depth (linear or banked),
 * a selector for the frame buffer or the A000h window, the protected mode entry points and
 * the bank switch routine (device table slot 16).  Modes 0..4 go to the VGA driver.
 * The DPMI calls are the inline (#pragma aux) code of the original, written as inline asm.
 */
#include "2d.h"
#include "lg.h"

#ifndef TN_LINK_ORIGINAL_DATA
void *vesa_pm_info;                /* 0x44DB10 */
long vesa_bytes_per_line;          /* 0x44DB14 */
ushort vesa_selector;              /* 0x44DB18 */
uchar vesa_gran_shift;             /* 0x44DB1A */
uchar vesa_dac_switchable;         /* 0x44DB1B */
uchar vesa_pal_blank;              /* 0x44DB1C */
short vesa_version = -1;           /* 0x364DCC */
long vesa_dac_width = 6;           /* 0x364DD0 */
uchar vesa_logical_ok = 1;         /* 0x364DD4 */
uchar vesa_linear;                 /* 0x364DD5 */
void *vesa_pm_bank;                /* 0x364DD8 */
void *vesa_pm_start;               /* 0x364DDC */
void *vesa_pm_pal;                 /* 0x364DE0 */
uchar *vesa_lfb;                   /* 0x364DE4 */
uchar *vesa_lfb_addr;              /* 0x364DE8 */
ulong vesa_lfb_phys;               /* 0x364DEC */
extern int vga_stat_htrace(void);  /* 0x34328C (2d-tmap range) */
extern int vga_stat_vtrace(void);  /* 0x343298 (2d-tmap range) */
extern int vesa_save_state(uchar *buf);    /* 0x3436BC (2d-tmap range) */
extern int vesa_restore_state(uchar *buf); /* 0x3439D8 (2d-tmap range) */
void (*vesa_device_table[GRD_DEVICE_FUNCS])() = {
    0, (void (*)())vesa_close, (void (*)())vesa_set_mode, (void (*)())vesa_get_mode,
    (void (*)())vesa_save_state, (void (*)())vesa_restore_state, (void (*)())vga_stat_htrace,
    (void (*)())vga_stat_vtrace, 0, 0, (void (*)())vesa_set_width, (void (*)())vesa_get_width,
    (void (*)())vesa_set_focus, (void (*)())vesa_get_focus, 0, 0, 0
};
#endif

/* The standalone build has no VESA BIOS: port/video/video.c implements these routines as a
 * VBE 2.0 card with a linear frame buffer that the platform layer shows in its window. */
#ifdef TN_WATCOM_ABI
#define RM_AX() (vesa_rm.eax & 0xFFFF)
#define RMW(f) ((ushort)vesa_rm.f)
#define SETW(f, v) (vesa_rm.f = (vesa_rm.f & 0xFFFF0000) | (ushort)(v))  /* the asm helpers write only low words */
#define VESA_OK() (RM_AX() == 0x4F)

/* ---------------------------------------------------------------- DPMI primitives */
/* int 31h ax=0300h: real-mode int intno with vesa_rm; 0 or the DPMI error */
static inline long dpmi_rm_int(int intno)
{
    long r;
    __asm__ volatile("movb %b2, %%bl\n\txorb %%bh, %%bh\n\tint $0x31\n\tjc 1f\n\txorl %%eax, %%eax\n1:"
                     : "=a"(r) : "a"(0x300), "q"(intno), "c"(0), "D"(&vesa_rm) : "ebx", "memory", "cc");
    return r;
}

/* int 31h ax=0100h: DOS memory; blk[0] = selector, blk[1] = linear address */
static inline long dpmi_alloc_dos(long paras, ulong *blk)
{
    long r;
    __asm__ volatile("int $0x31\n\tjc 1f\n\tmovl %%edx, (%%ecx)\n\tandl $0xffff, %%eax\n\tshll $4, %%eax\n\t"
                     "movl %%eax, 4(%%ecx)\n\txorl %%eax, %%eax\n1:"
                     : "=a"(r) : "a"(0x100), "b"(paras), "c"(blk) : "edx", "memory", "cc");
    return r;
}

static inline void dpmi_free_dos(ulong sel)
{
    long r;
    __asm__ volatile("int $0x31" : "=a"(r) : "a"(0x101), "d"(sel) : "memory", "cc");
}

/* int 31h ax=0000h cx=1: one LDT descriptor; the selector, or -1 */
static inline long dpmi_alloc_ldt(void)
{
    long r;
    __asm__ volatile("int $0x31\n\tjnc 1f\n\tsbbl %%eax, %%eax\n1:" : "=a"(r) : "a"(0), "c"(1) : "memory", "cc");
    return r;
}

/* int 31h ax=0007h: set segment base.  Quirk of the original pragma: the result is
 * computed with "xor eax,eax; jae" (xor clears CF), so it is always 0. */
static inline long dpmi_set_base(ulong sel, ulong base)
{
    long r;
    __asm__ volatile("movl %%edx, %%ecx\n\tshrl $16, %%ecx\n\tint $0x31\n\txorl %%eax, %%eax\n\tjae 1f\n\tdecl %%eax\n1:"
                     : "=a"(r) : "a"(7), "b"(sel), "d"(base) : "ecx", "memory", "cc");
    return r;
}

/* int 31h ax=0008h: set segment limit; 0 or the error */
static inline long dpmi_set_limit(ulong sel, ulong limit)
{
    long r;
    __asm__ volatile("movl %%edx, %%ecx\n\tshrl $16, %%ecx\n\tint $0x31\n\tjc 1f\n\txorl %%eax, %%eax\n1:"
                     : "=a"(r) : "a"(8), "b"(sel), "d"(limit) : "ecx", "memory", "cc");
    return r;
}

/* int 31h ax=0001h: free a selector, after clearing any segment register that holds it */
static inline void dpmi_free_sel(ulong sel)
{
    long r;
    __asm__ volatile("xorl %%eax, %%eax\n\t"
                     "movl %%ds, %%ecx\n\tcmpw %%bx, %%cx\n\tjne 1f\n\tmovl %%eax, %%ds\n1:\t"
                     "movl %%es, %%ecx\n\tcmpw %%bx, %%cx\n\tjne 2f\n\tmovl %%eax, %%es\n2:\t"
                     "movl %%fs, %%ecx\n\tcmpw %%bx, %%cx\n\tjne 3f\n\tmovl %%eax, %%fs\n3:\t"
                     "movl %%gs, %%ecx\n\tcmpw %%bx, %%cx\n\tjne 4f\n\tmovl %%eax, %%gs\n4:\t"
                     "movb $1, %%al\n\tint $0x31"
                     : "=a"(r) : "b"(sel) : "ecx", "memory", "cc");
}

/* int 31h ax=0800h: map size bytes of physical memory at phys; *lin = linear address */
static inline long dpmi_map_physical(ulong phys, ulong size, uchar **lin)
{
    long r;
    __asm__ volatile("movl %%ecx, %%ebx\n\tmovl %%edi, %%esi\n\tshrl $16, %%ebx\n\tshrl $16, %%esi\n\t"
                     "movl $0x800, %%eax\n\tint $0x31\n\tjc 1f\n\tshll $16, %%ebx\n\tmovw %%cx, %%bx\n\t"
                     "movl %%ebx, (%%edx)\n\txorl %%eax, %%eax\n1:"
                     : "=a"(r) : "c"(phys), "D"(size), "d"(lin) : "ebx", "esi", "memory", "cc");
    return r;
}

/* int 31h ax=0801h */
static inline void dpmi_unmap_physical(ulong lin)
{
    long r;
    __asm__ volatile("movl %%ebx, %%ecx\n\tshrl $16, %%ebx\n\tint $0x31"
                     : "=a"(r), "+b"(lin) : "a"(0x801), "d"(0) : "ecx", "memory", "cc");
}

/* far call of a VBE 2.0 protected mode routine with gs = vesa_selector; all registers kept */
static inline void pm_call(void *fn, ulong eax, ulong ebx, ulong ecx, ulong edx, ulong edi)
{
    __asm__ volatile("pushal\n\tpushl %%gs\n\tmovw %6, %%gs\n\tcall *%%esi\n\tpopl %%gs\n\tpopal"
                     : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx), "D"(edi), "S"(fn), "m"(vesa_selector)
                     : "memory", "cc");
}

static void copy(uchar *d, const uchar *s, ulong n)
{
    while (n--)
        *d++ = *s++;
}

static int pick_ok(void) { return dpmi_rm_int(0x10) == 0 && VESA_OK(); }

/* ---------------------------------------------------------------- protected mode interface */
/* which 1/2/3: the bank switch / display start / palette entry of the VBE 2.0 protected mode
 * interface (4F0Ah, copied to f_malloc memory the first time); NULL if not available. */
void *vesa_get_pm_func(int which)
{
    uchar *pm = vesa_pm_info;
    ulong len;

    if (pm == NULL) {
        vesa_rm.ebx = 0;
        vesa_rm.eax = 0x4F0A;
        if (!pick_ok())
            return NULL;
        len = vesa_rm.ecx & 0xFFFF;
        pm = (*f_malloc)(len);
        vesa_pm_info = pm;
        if (pm == NULL)
            return NULL;
        copy(pm, (uchar *)(((ulong)vesa_rm.es << 4) + (vesa_rm.edi & 0xFFFF)), len);
    }
    pm = vesa_pm_info;
    switch ((ulong)which) {
    case 1:
        return pm + *(short *)pm;
    case 2:
        return pm + *(short *)(pm + 2);
    case 3:
        return pm + *(short *)(pm + 4);
    }
    return NULL;
}

/* An LDT selector for base..base+limit; 0 if none. */
ushort vesa_alloc_selector(ulong base, ulong limit)
{
    ulong sel = dpmi_alloc_ldt() & 0xFFFF;

    if ((short)sel < 0)
        return 0;
    if (dpmi_set_base(sel, base) != 0) {          /* never true, see dpmi_set_base */
        dpmi_free_sel(sel);
        return 0;
    }
    if (dpmi_set_limit(sel, limit) != 0) {
        dpmi_free_sel(sel);
        return 0;
    }
    return sel;
}

/* ---------------------------------------------------------------- logical width, mode info */
/* vesa_logical_ok = 4F06h works and allows w x h.  Quirk kept: clears 0x34 bytes of the
 * 0x32-byte register block. */
void vesa_check_logical(int w, int h)
{
    uchar *p = (uchar *)&vesa_rm;
    int i;

    for (i = 0; i < 0x34; i++)
        p[i] = 0;
    vesa_rm.eax = 0x4F06;
    vesa_rm.ebx = 1;
    if (!pick_ok() || RMW(ebx) == 0 || (short)RMW(ecx) < (short)w || (short)RMW(edx) < (short)h)
        vesa_logical_ok = 0;
    else
        vesa_logical_ok = 1;
}

/* Current mode's x resolution and bytes per line from 4F03h + 4F01h; 1 or 0. */
char vesa_get_mode_info(long *w, long *bytes_per_line)
{
    ulong blk[2];
    uchar *mib;

    if (dpmi_alloc_dos(0x10, blk))
        return 0;
    vesa_rm.eax = 0x4F03;
    if (!pick_ok()) {
        dpmi_free_dos(blk[0]);
        return 0;
    }
    vesa_rm.eax = 0x4F01;
    vesa_rm.ecx = vesa_rm.ebx;
    vesa_rm.edi = 0;
    vesa_rm.es = blk[1] >> 4;
    if (!pick_ok()) {
        dpmi_free_dos(blk[0]);
        return 0;
    }
    mib = (uchar *)blk[1];
    *bytes_per_line = *(short *)(mib + 0x10);
    *w = *(short *)(mib + 0x12);
    dpmi_free_dos(blk[0]);
    return 1;
}

/* Sets the logical line width (pixels) with 4F06h: returns the pixels per line it got (0 on
 * failure) and the number of lines in *lines; vesa_bytes_per_line / grd_screen_row follow.
 * Without 4F06h: the current bytes per line, lines from the video memory size. */
int vesa_set_logical_width(int w, long *lines)
{
    long tmp;

    if (vesa_logical_ok) {
        vesa_rm.ecx = w;
        vesa_rm.ebx = 0;
        vesa_rm.eax = 0x4F06;
        if (!pick_ok())
            return 0;
        vesa_bytes_per_line = vesa_rm.ebx & 0xFFFF;
        grd_screen_row = (short)vesa_bytes_per_line;
        *lines = vesa_rm.edx & 0xFFFF;
        return vesa_rm.ecx & 0xFFFF;
    }
    if (!vesa_get_mode_info(&tmp, &vesa_bytes_per_line))
        return 0;
    *lines = ((long)grd_info.memory << 10) / vesa_bytes_per_line;
    return vesa_bytes_per_line;
}

/* ---------------------------------------------------------------- DAC width */
/* Asks for bits per DAC colour (4F08h, VBE 1.2+); falls back to 6.  Returns the width. */
long vesa_set_dac_width(long bits)
{
    long r;

    if (vesa_version < 0x102)
        return 6;
    for (;;) {
        vesa_rm.eax = 0x4F08;
        vesa_rm.ebx = bits << 8;
        if (!pick_ok()) {
            r = 6;
            break;
        }
        r = (long)(vesa_rm.ebx & 0xFF00) >> 8;
        if (r == bits || r == 6)
            break;
        if (bits == 6) {
            vesa_dac_width = 6;
            return 6;
        }
        bits = 6;
        vesa_dac_width = r;
        if (vesa_version < 0x102)
            return 6;
    }
    vesa_dac_width = r;
    return r;
}

long vesa_get_dac_width(void)
{
    if (vesa_version < 0x102)
        return 6;
    vesa_rm.ebx = 1;
    vesa_rm.eax = 0x4F08;
    if (!pick_ok())
        return 6;
    return (long)(vesa_rm.ebx & 0xFF00) >> 8;
}

/* back to a 6-bit DAC; returns the width with bits 8..15 cleared, as the original */
long vesa_reset_dac(void)
{
    long r = vesa_set_dac_width(6);

    vesa_dac_switchable = 0;
    return r & 0xFFFF00FF;
}

/* ---------------------------------------------------------------- palette */
/* VBE palette entries are B, G, R, 0.  Quirk kept: the conversion loops run at least once
 * (n <= 0 converts one entry). */
static void to_vbe(uchar *d, const uchar *s, int n)
{
    if (vesa_dac_width != 6) {
        do {
            d[2] = s[0];
            d[1] = s[1];
            d[0] = s[2];
            d += 4;
            s += 3;
        } while (--n > 0);
    } else {
        do {
            d[2] = s[0] >> 2;
            d[1] = s[1] >> 2;
            d[0] = s[2] >> 2;
            d += 4;
            s += 3;
        } while (--n > 0);
    }
}

static void from_vbe(uchar *d, const uchar *s, int n)
{
    if (vesa_dac_width != 6) {
        do {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d += 3;
            s += 4;
        } while (--n > 0);
    } else {
        do {
            d[0] = s[2] << 2;
            d[1] = s[1] << 2;
            d[2] = s[0] << 2;
            d += 3;
            s += 4;
        } while (--n > 0);
    }
}

/* GRT_SET_PAL of VBE 2.0 (set by vesa_get_info): VGA ports in modes 0..4, else 4F09h through
 * the protected mode entry or the BIOS. */
void vesa_set_pal(int start, int n, uchar *pal)
{
    uchar buf[0x400];
    ulong blk[2];

    if (grd_mode < 5) {
        vga_set_pal(start, n, pal);
        return;
    }
    if (vesa_pm_pal) {
        to_vbe(buf, pal, n);
        vesa_pm_set_pal(0, start, n, buf);
        return;
    }
    if (dpmi_alloc_dos(0x40, blk))
        return;
    to_vbe((uchar *)blk[1], pal, n);
    vesa_rm.eax = 0x4F09;
    vesa_rm.ebx = 0;
    if (vesa_pal_blank)
        vesa_rm.ebx = 0x80;
    vesa_rm.ecx = n;
    vesa_rm.edx = start;
    vesa_rm.edi = 0;
    vesa_rm.es = blk[1] >> 4;
    dpmi_rm_int(0x10);
    dpmi_free_dos(blk[0]);
}

/* GRT_GET_PAL.  Quirk kept: the BIOS path reads all 256 entries and takes n from start. */
void vesa_get_pal(int start, int n, uchar *pal)
{
    uchar buf[0x400];
    ulong blk[2];

    if (grd_mode < 5) {
        vga_get_pal(start, n, pal);
        return;
    }
    if (vesa_pm_pal) {
        vesa_pm_set_pal(1, start, n, buf);
        from_vbe(pal, buf, n);
        return;
    }
    if (dpmi_alloc_dos(0x40, blk))
        return;
    vesa_rm.eax = 0x4F09;
    vesa_rm.ebx = 1;
    vesa_rm.ecx = 0x100;
    vesa_rm.edx = 0;
    vesa_rm.edi = 0;
    vesa_rm.es = blk[1] >> 4;
    if (pick_ok())
        from_vbe(pal, (uchar *)blk[1] + start * 4, n);
    dpmi_free_dos(blk[0]);
}

/* ---------------------------------------------------------------- modes */
/* After a mode set: logical width, DAC, frame buffer selector, function tables by bit depth
 * (linear or banked), protected mode entries, bank switch routine.  0, or -1 if no selector
 * or no tables for the depth. */
int vesa_set_mode_tables(int mode, uchar linear)
{
    grs_mode_info *mi = &grd_mode_info[mode];
    long tmp;
    int idx;

    vesa_linear = linear;
    if (vesa_pm_info) {
        (*f_free)(vesa_pm_info);
        vesa_pm_info = NULL;
    }
    if (vesa_selector) {
        dpmi_free_sel(vesa_selector);
        vesa_selector = 0;
    }
    vesa_check_logical(mi->w, mi->h);
    if (vesa_dac_switchable)
        vesa_set_dac_width(8);
    if (vesa_version >= 0x200) {
        if (linear)
            vesa_selector = vesa_alloc_selector((ulong)vesa_lfb, 0x7FFFFF);
        else
            vesa_selector = vesa_alloc_selector(0xA0000, 0xFFFF);
        if (vesa_selector == 0)
            return -1;
    }
    vesa_set_logical_width(mi->w, &tmp);
    switch (mi->b) {
    case 4:
        idx = 0;
        break;
    case 8:
        idx = 2;
        break;
    case 15:
    case 16:
        idx = 4;
        break;
    case 24:
        idx = 6;
        break;
    case 32:
        idx = 8;
        break;
    default:
        idx = 0;                          /* the original uses whatever esi held */
        break;
    }
    if (!linear)
        idx++;
    if (grd_function_table_set[idx] == NULL)
        return -1;
    grd_uline_fill_table_list[BMT_DEVICE] = grd_uline_fill_table_set[idx];
    grd_function_table_list[BMT_DEVICE] = grd_function_table_set[idx];
    grd_canvas_table_list[BMT_DEVICE] = grd_canvas_table_set[idx];
    vesa_device_table[GRT_CANVAS_TABLE] = (void (*)())grd_canvas_table_set[idx];
    if (linear) {
        grd_mode_cap.vbase = vesa_lfb;
        grd_valloc_mode = 0;
    } else {
        grd_mode_cap.vbase = (uchar *)0xA0000;
        grd_valloc_mode = 1;
    }
    if (vesa_version >= 0x200) {
        vesa_pm_bank = vesa_get_pm_func(1);
        vesa_pm_start = vesa_get_pm_func(2);
        vesa_pm_pal = vesa_get_pm_func(3);
    } else
        vesa_pm_bank = vesa_pm_start = vesa_pm_pal = NULL;
    vesa_gran_shift = mi->win;
    if (mi->flags & 4) {                  /* one read/write window */
        if (vesa_version >= 0x200 && vesa_pm_bank)
            vesa_device_table[GRT_SET_BANK] = (void (*)())WATCOM_FN(vesa_set_bank_pm);
        else
            vesa_device_table[GRT_SET_BANK] = (void (*)())WATCOM_FN(vesa_set_bank_rm);
    } else {
        if (vesa_version >= 0x200 && vesa_pm_bank)
            vesa_device_table[GRT_SET_BANK] = (void (*)())WATCOM_FN(vesa_set_bank_pm2);
        else
            vesa_device_table[GRT_SET_BANK] = (void (*)())WATCOM_FN(vesa_set_bank_rm2);
    }
    return 0;
}

/* GRT_SET_MODE.  flags bit 0: clear the screen, bit 1: linear frame buffer.  Clears the old
 * linear frame buffer first if the current mode used one.  0, or -1 on failure. */
int vesa_set_mode(int mode, int flags)
{
    ushort f = 0;

    if (vesa_linear) {
        uchar *d = vesa_lfb;
        ulong n = (ulong)((long)grd_info.memory << 10);
        while (n--)
            *d++ = 0;
    }
    if (mode < 5) {
        vesa_linear = 0;
        vesa_dac_width = 6;
        return vga_set_mode(mode, flags);
    }
    if (!(grd_mode_info[mode].flags & 1))
        return -1;
    if (!(flags & 1))
        f |= 0x8000;                      /* keep video memory */
    if (flags & 2)
        f |= 0x4000;                      /* linear frame buffer */
    vesa_rm.eax = 0x4F02;
    vesa_rm.ebx = (long)(short)f | (long)grd_mode_info[mode].vmode;
    if (!pick_ok())
        return -1;
    vesa_dac_width = 6;
    return vesa_set_mode_tables(mode, (f & 0x4000) != 0);
}

/* GRT_GET_MODE: the GRM_ mode of the current VESA mode, -1 if none.  Quirk kept: clears
 * vesa_linear (the linear bit is masked with 0). */
int vesa_get_mode(void)
{
    grs_mode_info *mi = grd_mode_info;
    ushort m;

    vesa_rm.eax = 0x4F03;
    if (!pick_ok())
        return -1;
    m = RMW(ebx);
    vesa_linear = 0;
    m &= 0x3FFF;
    for (; mi < grd_mode_info + GRD_MODES; mi++)
        if (m == (ushort)mi->vmode)
            return mi->mode;
    return -1;
}

int vesa_set_width(int w)
{
    long lines;

    return vesa_set_logical_width(w, &lines);
}

/* GRT_GET_WIDTH: pixels per line */
int vesa_get_width(void)
{
    long w, bpl;

    if (vesa_logical_ok) {
        vesa_rm.ebx = 1;
        vesa_rm.eax = 0x4F06;
        if (!pick_ok())
            return -1;
        return vesa_rm.ecx & 0xFFFF;
    }
    if (!vesa_get_mode_info(&w, &bpl))
        return -1;
    return w;
}

int vesa_get_bytes_per_line(void)
{
    long w, bpl;

    if (vesa_logical_ok) {
        vesa_rm.ebx = 1;
        vesa_rm.eax = 0x4F06;
        if (!pick_ok())
            return -1;
        return vesa_rm.ebx & 0xFFFF;
    }
    if (!vesa_get_mode_info(&w, &bpl))
        return -1;
    return bpl;
}

/* lines of the logical screen */
int vesa_max_lines(void)
{
    long w;

    if (vesa_logical_ok) {
        vesa_rm.eax = 0x4F06;
        vesa_rm.ebx = 1;
        if (!pick_ok())
            return -1;
        return vesa_rm.edx & 0xFFFF;
    }
    if (!vesa_get_mode_info(&w, &vesa_bytes_per_line))
        return -1;
    return ((long)grd_info.memory << 10) / vesa_bytes_per_line;
}

/* GRT_GET_FOCUS: display start (4F07h bl=1); -1, -1 on failure */
void vesa_get_focus(long *x, long *y)
{
    vesa_rm.eax = 0x4F07;
    vesa_rm.ebx = 1;
    if (!pick_ok()) {
        *x = -1;
        *y = -1;
        return;
    }
    *x = vesa_rm.ecx & 0xFFFF;
    *y = vesa_rm.edx & 0xFFFF;
}

/* GRT_SET_FOCUS */
void vesa_set_focus(int x, int y)
{
    if (grd_mode < 5 && grd_mode >= 0) {
        vga_set_focus(x, y);
        return;
    }
    vesa_set_start(x, y, 0);
    grd_screen->y = y;
    grd_screen->x = x;
    grd_visible_canvas->bm.bits = grd_screen_canvas->bm.bits + y * vesa_bytes_per_line + x;
}

/* ---------------------------------------------------------------- detection */
/* Fills info (id 1.0, memory, the VGA modes 0..4 and the usable VESA modes, -1 terminated)
 * and grd_mode_info.  1 if VBE 1.1+ is there, else 0 (version -1, empty mode list).
 * Quirks kept: no "VESA" signature returns 0 without freeing the DOS block or marking the
 * version; every listed mode counts against the limit of 64 (also the skipped ones); a
 * window granularity that is not 64 >> k (k < 32) or 0 hangs in the shift search; modes with
 * vmode 0 are marked unavailable only at the end. */
int vesa_get_info(grs_sys_info *info)
{
    ulong blk1[2], blk2[2];
    uchar *vib, *mib;
    ushort *modelist;
    short *p, *first, *end;
    grs_mode_info *e;
    int count, k;

    blk1[1] = 0;
    blk2[1] = 0;
    if (dpmi_alloc_dos(0x20, blk1))
        goto fail;
    vesa_rm.edi = 0;
    vesa_rm.es = blk1[1] >> 4;
    vesa_rm.eax = 0x4F00;
    *(ulong *)blk1[1] = 0x32454256;       /* "VBE2" */
    if (!pick_ok())
        goto fail;
    vib = (uchar *)blk1[1];
    if (*(ulong *)vib != 0x41534556)      /* "VESA" */
        return 0;
    vesa_version = *(short *)(vib + 4);
    if (vesa_version < 0x101)
        goto fail;
    vesa_pal_blank = vib[0xA] & 4;
    info->id_maj = 1;
    info->id_min = 0;
    info->memory = *(ushort *)(vib + 0x12) << 6;
    vesa_dac_switchable = vib[0xA] & 1;
    modelist = (ushort *)(((ulong)*(ushort *)(vib + 0x10) << 4) + *(ushort *)(vib + 0xE));
    if (dpmi_alloc_dos(0x10, blk2))
        goto fail;
    if (vesa_version >= 0x200) {
        vesa_device_table[GRT_SET_PAL] = (void (*)())WATCOM_FN(vesa_set_pal);
        vesa_device_table[GRT_GET_PAL] = (void (*)())WATCOM_FN(vesa_get_pal);
    } else {
        vesa_device_table[GRT_SET_PAL] = (void (*)())WATCOM_FN(vga_set_pal);
        vesa_device_table[GRT_GET_PAL] = (void (*)())WATCOM_FN(vga_get_pal);
    }
    end = (short *)((uchar *)info + 0x82);
    count = 0;
    p = info->modes;
    *p++ = 0;
    *p++ = 1;
    *p++ = 2;
    *p++ = 3;
    *p++ = 4;
    first = p;
    for (;;) {
        short m = *(short *)modelist;
        if (m == -1 || (ulong)p >= (ulong)end || count >= 0x40)
            break;
        count++;
        if (m < 0x100 || m >= 0x200)
            goto next;
        vesa_rm.ecx = m;
        vesa_rm.eax = 0x4F01;
        vesa_rm.edi = 0;
        vesa_rm.es = blk2[1] >> 4;
        if (!pick_ok()) {
            if ((ulong)p > (ulong)first)
                break;
            goto fail;
        }
        mib = (uchar *)blk2[1];
        if (!(mib[0] & 0x10))             /* graphics mode */
            goto next;
        for (e = &grd_mode_info[GRD_MODES - 1]; e >= grd_mode_info; e--)
            if (e->w == *(short *)(mib + 0x12) && e->h == *(short *)(mib + 0x14) &&
                (long)e->b == (long)(signed char)mib[0x19])
                break;
        if (e < grd_mode_info || !(e->flags & 1))
            goto next;
        e->vmode = *(short *)modelist;
        if (!(mib[0] & 1)) {              /* not supported by the hardware */
            e->flags &= ~1;
            goto next;
        }
        for (k = 0; (0x40 >> (k & 31)) != *(short *)(mib + 4); k = (uchar)(k + 1))
            ;
        *(ushort *)&e->win = (uchar)k;
        if ((mib[2] & 7) == 7)
            e->flags |= 4;
        if (vesa_version >= 0x200 && (mib[0] & 0x80)) {
            if (vesa_lfb == NULL) {
                if (dpmi_map_physical(*(ulong *)(mib + 0x28), 0x800000, &vesa_lfb) == 0) {
                    e->flags |= 2;
                    vesa_lfb_addr = vesa_lfb;
                    vesa_lfb_phys = *(ulong *)(mib + 0x28);
                }
            } else
                e->flags |= 2;
        }
        *p++ = e->mode;
next:
        modelist++;
    }
    *p = -1;
    dpmi_free_dos(blk1[0]);
    dpmi_free_dos(blk2[0]);
    for (e = &grd_mode_info[5]; e < grd_mode_info + GRD_MODES; e++)
        if (e->vmode == 0)
            e->flags &= ~1;
    return 1;
fail:
    if (blk1[1])
        dpmi_free_dos(blk1[0]);
    if (blk2[1])
        dpmi_free_dos(blk2[0]);
    vesa_version = -1;
    info->modes[0] = -1;
    return 0;
}

/* GRT_CLOSE_DEVICE */
void vesa_close(void)
{
    dpmi_unmap_physical((ulong)vesa_lfb);
    vesa_lfb = NULL;
    vesa_version = 0;
    if (vesa_pm_info)
        (*f_free)(vesa_pm_info);
    vesa_pm_info = NULL;
    vesa_pm_bank = vesa_pm_start = vesa_pm_pal = NULL;
    if (vesa_selector)
        dpmi_free_sel(vesa_selector);
    vesa_selector = 0;
}

/* ---------------------------------------------------------------- asm helpers */
/* Bank switch routines (device table slot 16, called by the banked primitives with the bank
 * in eax).  The originals keep every register; they return the bank so eax is kept too.
 * pm: VBE 2.0 entry (dx = bank << shift in its low byte only, as the original's "shl dl,cl");
 * rm: 4F05h through the BIOS, saving and restoring vesa_rm.  "2": windows A and B (the second
 * call increments whatever bx the first left). */
int vesa_set_bank_pm(int bank)
{
    ulong d = (bank & ~0xFF) | (uchar)(bank << (vesa_gran_shift & 31));

    pm_call(vesa_pm_bank, bank, 0, vesa_gran_shift, d, 0);
    return bank;
}

int vesa_set_bank_pm2(int bank)
{
    ulong d = (bank & ~0xFF) | (uchar)(bank << (vesa_gran_shift & 31));

    pm_call(vesa_pm_bank, bank, 0, vesa_gran_shift, d, 0);
    pm_call(vesa_pm_bank, bank, 1, vesa_gran_shift, d, 0);
    return bank;
}

int vesa_set_bank_rm(int bank)
{
    dpmi_rmregs save = vesa_rm;

    SETW(eax, 0x4F05);
    SETW(ebx, 0);
    SETW(edx, (ushort)(bank << (vesa_gran_shift & 31)));
    dpmi_rm_int(0x10);
    vesa_rm = save;
    return bank;
}

int vesa_set_bank_rm2(int bank)
{
    dpmi_rmregs save = vesa_rm;

    SETW(eax, 0x4F05);
    SETW(ebx, 0);
    SETW(edx, (ushort)(bank << (vesa_gran_shift & 31)));
    dpmi_rm_int(0x10);
    SETW(eax, 0x4F05);
    SETW(ebx, RMW(ebx) + 1);
    dpmi_rm_int(0x10);
    vesa_rm = save;
    return bank;
}

/* Display start at (x, y); wait (VBE 2.0 only) = during vertical retrace.  Through the
 * protected mode entry (offset in pixels, or in 4-pixel units unless the mode is 4-bit:
 * cx = low word, dx = high word, the high word of ecx is the entry's address as in the
 * original) or 4F07h. */
int vesa_set_start(int x, int y, int wait)
{
    if (vesa_version < 0x200)
        wait = 0;
    wait <<= 7;
    if (vesa_pm_start) {
        ulong off = y * (ulong)(ushort)vesa_bytes_per_line + x;
        if (grd_mode_info[grd_mode].b != 4)
            off >>= 2;
        pm_call(vesa_pm_start, x, wait, ((ulong)vesa_pm_start & 0xFFFF0000) | (off & 0xFFFF), off >> 16,
                (ulong)vesa_pm_start);
    } else {
        SETW(eax, 0x4F07);
        SETW(ebx, wait);
        SETW(ecx, x);
        SETW(edx, y);
        dpmi_rm_int(0x10);
    }
    return x;
}

/* 4F09h through the protected mode entry: sub 0 set (with the blank bit 80h if the DAC needs
 * it), 1 get; buf holds n B,G,R,0 entries.  Register routine in the original (bl, dx, cx,
 * es:edi); like it this leaves gs = vesa_selector. */
void vesa_pm_set_pal(int sub, int start, int n, uchar *buf)
{
    if (sub == 0 && vesa_pal_blank)
        sub |= 0x80;
    __asm__ volatile("pushal\n\tmovw %5, %%gs\n\tcall *%%esi\n\tpopal"
                     : : "b"(sub), "c"(n), "d"(start), "D"(buf), "S"(vesa_pm_pal), "m"(vesa_selector)
                     : "memory", "cc");
}
#endif /* TN_WATCOM_ABI */
