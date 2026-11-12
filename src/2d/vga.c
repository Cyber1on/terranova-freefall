/* vga.c -- Terra Nova v1.09 VGA / Mode X device driver (DOS; no System Shock counterpart,
 * names ours).  Hardware access: VGA ports and video BIOS calls through DPMI int 31h.
 *
 *   0x31D120 vga_set_crtc_regs     0x31D1F0 vga_set_pal         0x31D440 vga_get_pal
 *   0x31D470 vga_set_mode          0x31D574 vga_set_focus
 *   0x349C70 vga_save_bios_state   0x349E30 vga_save_vesa_state 0x349ED4 vga_save_state
 *   0x349F00 vga_restore_bios_state 0x34A0B0 vga_restore_vesa_state 0x34A118 vga_restore_state
 *   0x34A13C vga_get_focus         0x34A140 vga_set_width
 *   data 0x36DEEC vga_device_table, 0x36D0C8 / 0x36D0EA vga_regs_a / vga_regs_b,
 *        0x36D150 vga_crtc_x (CRTC values of the 240/480-line Mode X modes)
 *
 * Modes 0..4 (grd_mode_info): 0 = BIOS mode 13h, 1..4 = Mode X (unchained) 320x200 / 400 /
 * 240 / 480.  The state routines (device table GRT_SAVE_STATE / GRT_RESTORE_STATE, used by
 * gr_push/pop_video_state) keep a 790-byte block: +0 kind (0 text/BIOS, 1 graphics), +2 BIOS
 * mode or GRM_ mode, +4 768 palette bytes, +0x304 flags, +0x305 17 attribute registers.
 * The int 31h / port sequences are the inline (#pragma aux) code of the original, written
 * here as inline asm.
 */
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
dpmi_rmregs vesa_rm;               /* 0x45E318 */
uchar vga_regs_a[34] = {           /* 0x36D0C8: misc, -, -, -, -, seq 1..4, CRTC 0.. */
    0x27, 0x01, 0x10, 0x31, 0x03, 0x09, 0x0f, 0x00, 0x0e, 0x6b, 0x4f, 0x50, 0x8e, 0x5c, 0x84,
    0x04, 0x11, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xdf, 0xa2, 0xc7, 0x28, 0x40,
    0xdc, 0xf0, 0xa3, 0xff
};
uchar vga_regs_b[34] = {           /* 0x36D0EA */
    0x63, 0x00, 0x10, 0x05, 0x03, 0x01, 0x0f, 0x00, 0x0e, 0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80,
    0xbf, 0x1f, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9c, 0x8e, 0x8f, 0x28, 0x40,
    0x96, 0xb9, 0xa3, 0xff
};
ushort vga_crtc_x[8] = {           /* 0x36D150 */
    0x2011, 0x3e07, 0xe312, 0xea15, 0xf010, 0x2211, 0x0d16, 0x1306
};
extern int vga_stat_htrace(void);  /* 0x34328C (2d-tmap range) */
extern int vga_stat_vtrace(void);  /* 0x343298 (2d-tmap range) */
void (*vga_device_table[GRD_DEVICE_FUNCS])() = {
    0, 0, (void (*)())vga_set_mode, 0, (void (*)())vga_save_state, (void (*)())vga_restore_state,
    (void (*)())vga_stat_htrace, (void (*)())vga_stat_vtrace, (void (*)())vga_set_pal,
    (void (*)())vga_get_pal, (void (*)())vga_set_width, 0, (void (*)())vga_set_focus,
    (void (*)())vga_get_focus, 0, 0, 0
};
#else
extern uchar vga_regs_a[34], vga_regs_b[34];
extern ushort vga_crtc_x[8];
#endif

/* ---------------------------------------------------------------- port / DPMI primitives */
#ifdef TN_WATCOM_ABI
static inline void outb(int port, int v) { __asm__ volatile("outb %b0, %w1" : : "a"(v), "Nd"(port)); }
static inline void outw(int port, int v) { __asm__ volatile("outw %w0, %w1" : : "a"(v), "Nd"(port)); }
static inline uchar inb(int port)
{
    uchar v;
    __asm__ volatile("inb %w1, %b0" : "=a"(v) : "Nd"(port));
    return v;
}
#else
#define outb port_outb
#define outw port_outw
#define inb port_inb
#endif

/* int 31h ax=0300h: real-mode interrupt intno with the registers in vesa_rm (bl = intno,
 * bh = 0, cx = 0 words to copy); 0 or the DPMI error */
static inline long dpmi_rm_int(int intno)
{
    long r;
    __asm__ volatile("movb %b2, %%bl\n\txorb %%bh, %%bh\n\tint $0x31\n\tjc 1f\n\txorl %%eax, %%eax\n1:"
                     : "=a"(r) : "a"(0x300), "q"(intno), "c"(0), "D"(&vesa_rm) : "ebx", "memory", "cc");
    return r;
}

/* int 31h ax=0100h: allocate paras paragraphs of DOS memory; blk[0] = selector,
 * blk[1] = linear address; 0 or the DPMI error */
static inline long dpmi_alloc_dos(long paras, ulong *blk)
{
    long r;
    __asm__ volatile("int $0x31\n\tjc 1f\n\tmovl %%edx, (%%ecx)\n\tandl $0xffff, %%eax\n\tshll $4, %%eax\n\t"
                     "movl %%eax, 4(%%ecx)\n\txorl %%eax, %%eax\n1:"
                     : "=a"(r) : "a"(0x100), "b"(paras), "c"(blk) : "edx", "memory", "cc");
    return r;
}

/* int 31h ax=0101h: free DOS memory */
static inline void dpmi_free_dos(ulong sel)
{
    long r;
    __asm__ volatile("int $0x31" : "=a"(r) : "a"(0x101), "d"(sel) : "memory", "cc");
}

static void copy(uchar *d, const uchar *s, int n)
{
    while (n-- > 0)
        *d++ = *s++;
}

/* ---------------------------------------------------------------- registers, palette */
/* Programs the misc output, sequencer and CRT controller registers from one of two tables
 * (which != 0: vga_regs_a).  Quirks kept: the CRTC write-protect bit is cleared with a word
 * write to 3D5h (0 to index 11h, FFh to 3D6h); CRTC 1, 2 and 8 are skipped.  Keeps eax. */
int vga_set_crtc_regs(int which)
{
    uchar *p = which ? vga_regs_a : vga_regs_b;
    int i;

    outb(0x3C2, p[0]);
    outb(0x3C4, 0);
    outb(0x3C5, 0);
    p += 5;
    for (i = 1; i <= 4; i++) {
        outb(0x3C4, i);
        outb(0x3C5, *p++);
    }
    outb(0x3C4, 0);
    outb(0x3C5, 3);
    outb(0x3D4, 0x11);
    outw(0x3D5, 0xFF00);
    outb(0x3D4, 0);
    outb(0x3D5, *p);
    p += 3;
    for (i = 3; i <= 7; i++) {
        outb(0x3D4, i);
        outb(0x3D5, *p++);
    }
    outb(0x3D4, 0x11);
    outb(0x3D5, 0x80);
    p++;
    for (i = 9; i < 0x17; i++) {
        outb(0x3D4, i);
        outb(0x3D5, *p++);
    }
    return which;
}

/* Sends n 8-bit RGB triples (as 6-bit DAC values) starting at colour start. */
void vga_set_pal(int start, int n, uchar *pal)
{
    int c;

    if (n <= 0)
        return;
    outb(0x3C8, start);
    for (c = n * 3; c; c--)
        outb(0x3C9, *pal++ >> 2);
}

/* Reads n RGB triples starting at colour start (shifted to 8 bits). */
void vga_get_pal(int start, int n, uchar *pal)
{
    int c;

    if (n <= 0)
        return;
    outb(0x3C7, start);
    for (c = n * 3; c; c--)
        *pal++ = inb(0x3C9) << 2;
}

/* ---------------------------------------------------------------- mode, focus, width */
static void set_tables(int set)
{
    grd_canvas_table_list[BMT_DEVICE] = grd_canvas_table_set[set];
    grd_function_table_list[BMT_DEVICE] = grd_function_table_set[set];
    grd_uline_fill_table_list[BMT_DEVICE] = grd_uline_fill_table_set[set];
}

/* Sets mode 0..4 through BIOS mode 13h (flags bit 0 = clear the screen; else BIOS bit 7
 * "keep memory"), then unchains it for the Mode X modes.  Returns 0, 1 for another mode. */
int vga_set_mode(int mode, int flags)
{
    if ((ulong)mode >= 5)
        return 1;
    flags ^= 1;
    grd_mode_cap.vbase = (uchar *)0xA0000;
    grd_valloc_mode = 0;
    vesa_rm.eax = (vesa_rm.eax & 0xFFFF0000) | (ushort)(0x13 | (flags << 7));
    dpmi_rm_int(0x10);
    if (mode == 0) {
        set_tables(2);
        return 0;
    }
    outw(0x3C4, 0x0604);                  /* chain 4 off */
    outb(0x3D4, 0x17);
    outb(0x3D5, inb(0x3D5) | 0x40);       /* byte mode */
    if (!(flags & 1)) {
        ulong *d = (ulong *)0xA0000;
        int i;
        for (i = 0; i < 0x4000; i++)
            d[i] = 0;
    }
    outw(0x3D4, 0x0014);                  /* doubleword mode off */
    if (mode != 1) {
        if (mode != 2) {
            int i;
            outb(0x3C2, 0xE3);
            for (i = 0; i < 8; i++)
                outw(0x3D4, vga_crtc_x[i]);
            if (mode == 3)
                goto done;
        }
        outw(0x3D4, 0x4009);              /* double scan off / 400 lines */
    }
done:
    set_tables(10);
    return 0;
}

/* Moves the visible canvas to (x, y) of the screen and sets the CRTC start address. */
void vga_set_focus(int x, int y)
{
    ulong a;

    grd_screen->x = x;
    grd_screen->y = y;
    a = (ulong)grd_screen_canvas->bm.bits + (ulong)(ushort)grd_screen_canvas->bm.row * (ulong)y + ((ulong)x >> 2);
    grd_visible_canvas->bm.bits = (uchar *)a;
    a -= 0xA0000;
    outw(0x3D4, (a & 0xFF00) | 0x0C);
    outw(0x3D4, ((a & 0xFF) << 8) | 0x0D);
}

void vga_get_focus(void)
{
}

/* CRTC offset register = w / 8 (the word w << 5 with the index in the low byte) */
void vga_set_width(int w)
{
    outw(0x3D4, ((w << 5) & 0xFF00) | 0x13);
}

/* ---------------------------------------------------------------- state save / restore */
#define ST_KIND(b) (*(short *)(b))
#define ST_MODE(b) (*(short *)((b) + 2))
#define ST_PAL(b) ((b) + 4)
#define ST_FLAGS(b) ((b)[0x304])
#define ST_ATTR(b) ((b) + 0x305)

/* Text / BIOS mode: the BIOS mode, 50-line font flag, DAC paging, DAC and attribute regs. */
int vga_save_bios_state(uchar *buf)
{
    ulong blk[2];

    ST_KIND(buf) = 0;
    vesa_rm.eax = 0x0F00;                 /* get video mode */
    dpmi_rm_int(0x10);
    ST_MODE(buf) = (uchar)vesa_rm.eax;
    ST_FLAGS(buf) = 0;
    vesa_rm.eax = 0x1130;                 /* font information: DL = rows - 1 */
    dpmi_rm_int(0x10);
    if ((vesa_rm.edx & 0xFF) == 0x31)
        ST_FLAGS(buf) |= 1;
    vesa_rm.eax = 0x101A;                 /* DAC paging state */
    dpmi_rm_int(0x10);
    if (vesa_rm.ebx & 0xFF)
        ST_FLAGS(buf) |= 2;
    ST_FLAGS(buf) = ST_FLAGS(buf) | (uchar)((long)(vesa_rm.ebx & 0xFF00) >> 4);
    if (dpmi_alloc_dos(0x30, blk))
        return 0;
    vesa_rm.ebx = 0;
    vesa_rm.ecx = 0x100;
    vesa_rm.eax = 0x1017;                 /* read DAC block */
    vesa_rm.edx = 0;
    vesa_rm.es = blk[1] >> 4;
    dpmi_rm_int(0x10);
    copy(ST_PAL(buf), (uchar *)blk[1], 0x300);
    vesa_rm.eax = 0x1009;                 /* read attribute registers + overscan */
    vesa_rm.edx = 0;
    vesa_rm.es = blk[1] >> 4;
    dpmi_rm_int(0x10);
    copy(ST_ATTR(buf), (uchar *)blk[1], 0x11);
    dpmi_free_dos(blk[0]);
    return 1;
}

/* Graphics mode: grd_mode (or the BIOS mode if none was set) and the palette. */
int vga_save_vesa_state(uchar *buf)
{
    ST_KIND(buf) = 1;
    buf[0x305] = 0xFF;
    ST_FLAGS(buf) = grd_mode != -1;
    if (grd_mode != -1)
        ST_MODE(buf) = grd_mode;
    else {
        vesa_rm.eax = 0x0F00;
        dpmi_rm_int(0x10);
        ST_MODE(buf) = (uchar)vesa_rm.eax;
    }
    vga_get_pal(0, 0x100, ST_PAL(buf));
    *(long *)(buf + 0x306) = 0;
    buf[0x312] = 6;
    *(long *)(buf + 0x30A) = 0;
    *(long *)(buf + 0x30E) = 0;
    return 1;
}

/* GRT_SAVE_STATE: graphics controller misc register bit 0 tells graphics from text. */
int vga_save_state(uchar *buf)
{
    outb(0x3CE, 6);
    if (inb(0x3CF) & 1)
        return vga_save_vesa_state(buf);
    return vga_save_bios_state(buf);
}

/* Quirk kept: the "select DAC page" call (1013h bl=1) is set up but never issued. */
int vga_restore_bios_state(uchar *buf)
{
    ulong blk[2];

    if (dpmi_alloc_dos(0x30, blk))
        return 0;
    vesa_rm.eax = (uchar)ST_MODE(buf);   /* set mode */
    dpmi_rm_int(0x10);
    if (ST_FLAGS(buf) & 1) {
        vesa_rm.ebx = 0;
        vesa_rm.eax = 0x1112;             /* 8x8 font: 50 lines */
        dpmi_rm_int(0x10);
    }
    copy((uchar *)blk[1], ST_PAL(buf), 0x300);
    vesa_rm.eax = 0x1012;                 /* set DAC block */
    vesa_rm.ebx = 0;
    vesa_rm.ecx = 0x100;
    vesa_rm.edx = 0;
    vesa_rm.es = blk[1] >> 4;
    dpmi_rm_int(0x10);
    vesa_rm.eax = 0x1013;                 /* DAC paging mode */
    vesa_rm.ebx = (ST_FLAGS(buf) & 2) ? 0x100 : 0;
    dpmi_rm_int(0x10);
    vesa_rm.eax = 0x1013;
    vesa_rm.ebx = 1;
    copy((uchar *)blk[1], ST_ATTR(buf), 0x11);
    vesa_rm.eax = 0x1002;                 /* set attribute registers */
    vesa_rm.edx = 0;
    vesa_rm.es = blk[1] >> 4;
    dpmi_rm_int(0x10);
    dpmi_free_dos(blk[0]);
    return 1;
}

int vga_restore_vesa_state(uchar *buf)
{
    if (ST_FLAGS(buf))
        gr_set_mode((uchar)ST_MODE(buf), 1);
    else {
        vesa_rm.eax = (uchar)ST_MODE(buf);
        dpmi_rm_int(0x10);
    }
    vga_set_pal(0, 0x100, ST_PAL(buf));
    return 1;
}

/* GRT_RESTORE_STATE */
int vga_restore_state(uchar *buf)
{
    if (ST_KIND(buf) == 0)
        return vga_restore_bios_state(buf);
    if (ST_KIND(buf) == 1)
        return vga_restore_vesa_state(buf);
    return 0;
}
