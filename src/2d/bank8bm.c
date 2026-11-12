/* bank8bm.c -- Terra Nova v1.09 8-bit banked VESA canvas (bank8): bitmap blits and the solid
 * horizontal line.  Decompiled from __FF.EXE (Watcom C); names ours.
 *
 *   0x33EE20 bank8_flat8_ubitmap        (function table BMT_FLAT8 GRC_BITMAP, FILL_NORM)
 *   0x33F910 bank8_xor_flat8_ubitmap    (function table BMT_FLAT8 GRC_BITMAP, FILL_XOR)
 *   0x340060 bank8_get_flat8_ubitmap    (canvas GET_FLAT8_UBITMAP: screen -> flat8 bitmap)
 *   0x33FFF0 bank8_draw_device_ubitmap  (canvas DRAW_DEVICE_UBITMAP)
 *   0x340250 bank8_h_double_ubitmap     (canvas table entry 246)
 *   0x3405A0 bank8_hv_double_ubitmap    (canvas table entry 270)
 *   0x340B00 bank8_uhline_fill          (line table GRL_UHLINE, FILL_NORM and FILL_SOLID)
 *
 * Each row is copied through the 64K window; a row that crosses the end of the window is split
 * and the next bank selected, and when the row step would leave the window the next bank is
 * selected in advance (this assumes the row is shorter than 64K).  The blits put the bank that
 * was current on entry back at the end.
 * Quirks kept: bank8_uhline_fill with x1 < x0 fills |x1 - x0 + 1| bytes from x1 (one short of
 * the span); FILL_SOLID's line still draws c; the fill dword is ((c & 0xffff00ff) |
 * (c & 0xff) << 8) twice.  bank8_draw_device_ubitmap reads the device bitmap into a temporary
 * flat8 canvas and draws that with bank8_flat8_ubitmap (silently nothing when the allocation
 * fails).  bank8_xor_flat8_ubitmap xors every source byte (BMF_TRANS ignored, zero bytes are
 * no-ops anyway); its byte loops are do-whiles, so a zero-width bitmap runs 4G bytes (crashes).
 * The double blits write every source pixel twice (and every row twice for hv) at x * 2 but
 * y * 1, through the asm row doublers at 0x34EA70 / 0x34EAD4 (self-modifying, outside this
 * range; copied here as dbl / dbl2 with their exact write order) and do not restore the entry
 * bank.  Their translucent paths only advance the destination for non-zero pixels (the row is
 * compacted), and a row crossing the window end draws `room` source pixels (not room / 2)
 * before switching, running past the window.
 */
#include "2d_tmap.h"

#define SET_BANK() WATCOM_CALL((void (*)())grd_device_table[GRT_SET_BANK], (long)(short)grd_bank8_bank)

static void bank8_next(void)
{
    grd_bank8_bank++;
    grd_bank8_lock++;
    SET_BANK();
    grd_bank8_lock--;
}

/* the initial bank of a blit (the cached bank is checked against the one saved on entry) */
static void bank8_first(long addr, ushort save)
{
    long bank = addr >> 16;

    if (grd_bank8_lock != 0 || save != bank) {
        int l = grd_bank8_lock;
        grd_bank8_bank = (ushort)bank;
        grd_bank8_lock = l + 1;
        SET_BANK();
        grd_bank8_lock--;
    }
}

static void bank8_restore(ushort save)
{
    if (save != grd_bank8_bank) {
        grd_bank8_bank = save;
        grd_bank8_lock++;
        SET_BANK();
        grd_bank8_lock--;
    }
}

static void copy(uchar *d, uchar *s, long n)
{
    ulong i;

    for (i = 0; i < (ulong)n; i++)
        d[i] = s[i];
}

void bank8_flat8_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *src = bm->bits, *win = grd_cap->vbase, *d;
    short w = bm->w, h = bm->h;
    ushort save = grd_bank8_bank;
    long addr = (long)grd_bm.bits + y * (ulong)(ushort)grd_bm.row + x, room;
    int i;

    bank8_first(addr, save);
    if (bm->flags & BMF_TRANS) {
        while (h-- != 0) {
            room = 0x10000 - (addr & 0xffff);
            d = win + (addr & 0xffff);
            if (room >= w) {
                for (i = 0; i < w; i++)
                    if (src[i] != 0)
                        d[i] = src[i];
                if (room <= (ushort)grd_bm.row)
                    bank8_next();
            } else {
                for (i = 0; i < room; i++)
                    if (src[i] != 0)
                        d[i] = src[i];
                bank8_next();
                for (d = win; i < w; i++, d++)
                    if (src[i] != 0)
                        *d = src[i];
            }
            src += (ushort)bm->row;
            addr += (ushort)grd_bm.row;
        }
    } else {
        while (h-- != 0) {
            room = 0x10000 - (addr & 0xffff);
            d = win + (addr & 0xffff);
            if (room >= w) {
                copy(d, src, w);
                if (room <= (ushort)grd_bm.row)
                    bank8_next();
            } else {
                copy(d, src, room);
                bank8_next();
                copy(win, src + room, w - room);
            }
            src += (ushort)bm->row;
            addr += (ushort)grd_bm.row;
        }
    }
    bank8_restore(save);
}

static void xor_bytes(uchar *d, uchar *s, ulong n)
{
    do
        *d++ ^= *s++;
    while (--n != 0);
}

void bank8_xor_flat8_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *src = bm->bits, *win = grd_cap->vbase;
    short w = bm->w, h = bm->h;
    ushort save = grd_bank8_bank;
    long addr = (long)grd_bm.bits + y * (ulong)(ushort)grd_bm.row + x, room;

    bank8_first(addr, save);
    while (h-- != 0) {
        room = 0x10000 - (addr & 0xffff);
        if (room >= w) {
            xor_bytes(win + (addr & 0xffff), src, w);
            if (room <= (ushort)grd_bm.row)
                bank8_next();
        } else {
            xor_bytes(win + (addr & 0xffff), src, room);
            bank8_next();
            xor_bytes(win, src + room, w - room);
        }
        src += (ushort)bm->row;
        addr += (ushort)grd_bm.row;
    }
    bank8_restore(save);
}

/* 0x34EA70: d[2i] = d[2i+1] = s[i], i < n (a word loop, aligned on d) */
static void dbl(uchar *d, uchar *s, ulong n)
{
    ulong k;

    for (k = 0; k < n; k++)
        d[2 * k] = d[2 * k + 1] = s[k];
}

/* 0x34EAD4: dbl into two rows; with equal alignment the two rows are written step by step */
static void dbl2(uchar *d0, uchar *d1, uchar *s, ulong n)
{
    ulong k;

    if (n == 0)
        return;
    if ((((ulong)d0 ^ (ulong)d1) & 1) != 0) {
        dbl(d0, s, n);
        dbl(d1, s, n);
    } else if ((ulong)d0 & 1) {
        d0[0] = s[0];
        d1[0] = s[0];
        for (k = 1; k < n; k++) {
            d0[2 * k - 1] = s[k - 1]; d0[2 * k] = s[k];
            d1[2 * k - 1] = s[k - 1]; d1[2 * k] = s[k];
        }
        d0[2 * n - 1] = s[n - 1];
        d1[2 * n - 1] = s[n - 1];
    } else
        for (k = 0; k < n; k++) {
            d0[2 * k] = d0[2 * k + 1] = s[k];
            d1[2 * k] = d1[2 * k + 1] = s[k];
        }
}

/* translucent doubled run: non-zero pixels only, the destination advancing only for them */
static uchar *tdbl(uchar *d, uchar *s, int from, int to)
{
    int i;

    for (i = from; i < to; i++)
        if (s[i] != 0) {
            d[0] = d[1] = s[i];
            d += 2;
        }
    return d;
}

static void tdbl_row(uchar *win, uchar *src, int w, long addr)
{
    long room = 0x10000 - (addr & 0xffff);
    uchar *d = win + (addr & 0xffff);

    if (room >= 2 * w) {
        tdbl(d, src, 0, w);
        if (room <= (ushort)grd_bm.row)
            bank8_next();
    } else {
        tdbl(d, src, 0, room);
        bank8_next();
        tdbl(win, src, room, w);
    }
}

static void bank8_select_first(long addr)
{
    int l = grd_bank8_lock;
    long bank = addr >> 16;

    if (l != 0 || grd_bank8_bank != bank) {
        grd_bank8_bank = (ushort)bank;
        grd_bank8_lock = l + 1;
        SET_BANK();
        grd_bank8_lock--;
    }
}

void bank8_h_double_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *src = bm->bits, *win = grd_cap->vbase, *d;
    int w = bm->w, w2 = 2 * w, h = bm->h;
    long addr = (long)grd_bm.bits + y * (ulong)(ushort)grd_bm.row + 2 * x, room, n1, n2;

    bank8_select_first(addr);
    if (bm->flags & BMF_TRANS) {
        while (h-- != 0) {
            tdbl_row(win, src, w, addr);
            src += (ushort)bm->row;
            addr += (ushort)grd_bm.row;
        }
        return;
    }
    while (h-- != 0) {
        room = 0x10000 - (addr & 0xffff);
        d = win + (addr & 0xffff);
        if (room >= w2) {
            dbl(d, src, w);
            if (room <= (ushort)grd_bm.row)
                bank8_next();
        } else {
            n1 = room >> 1;
            n2 = (w2 - room) >> 1;
            dbl(d, src, n1);
            if (room & 1) {
                win[0xffff] = src[n1];
                bank8_next();
                win[0] = src[n1];
                dbl(win + 1, src + n1 + 1, n2);
            } else {
                bank8_next();
                dbl(win, src + n1, n2);
            }
        }
        src += (ushort)bm->row;
        addr += (ushort)grd_bm.row;
    }
}

void bank8_hv_double_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *src = bm->bits, *win = grd_cap->vbase, *mid;
    int w = bm->w, w2 = 2 * w, h = bm->h, k;
    long row2 = 2 * (ushort)grd_bm.row;
    long addr = (long)grd_bm.bits + y * (ulong)(ushort)grd_bm.row + 2 * x, room, row, n1;

    bank8_select_first(addr);
    if (bm->flags & BMF_TRANS) {
        while (h-- != 0) {
            for (k = 0; k < 2; k++) {
                tdbl_row(win, src, w, addr);
                addr += (ushort)grd_bm.row;
            }
            src += (ushort)bm->row;
        }
        return;
    }
    while (h-- != 0) {
        room = 0x10000 - (addr & 0xffff);
        row = (ushort)grd_bm.row;
        if (room >= w2 + row) {                 /* both rows in this bank */
            dbl2(win + (addr & 0xffff), win + ((addr + row) & 0xffff), src, w);
            if (room <= row2)
                bank8_next();
        } else if (room >= row) {               /* the second row crosses the window end */
            long r2 = room - row;

            n1 = r2 >> 1;
            mid = src + n1;
            dbl2(win + (addr & 0xffff), win + ((addr + row) & 0xffff), src, n1);
            if (r2 & 1) {
                dbl(win + ((addr + r2 - 1) & 0xffff), mid, w - n1);
                win[0xffff] = *mid;
                bank8_next();
                win[0] = *mid;
                dbl(win + 1, mid + 1, (w2 - r2) >> 1);
            } else {
                dbl(win + ((addr + r2) & 0xffff), mid, w - n1);
                bank8_next();
                dbl(win, mid, w - n1);
            }
        } else {                                /* the first row crosses, the second is past it */
            n1 = room >> 1;
            mid = src + n1;
            dbl(win + (addr & 0xffff), src, n1);
            if (room & 1) {
                win[0xffff] = *mid;
                bank8_next();
                win[0] = *mid;
                dbl(win + ((row + addr) & 0xffff), src, n1 + 1);
                dbl2(win + 1, win + ((addr + row + room + 1) & 0xffff), mid + 1, (w2 - room) >> 1);
            } else {
                bank8_next();
                dbl(win + ((row + addr) & 0xffff), src, n1);
                dbl2(win, win + row, mid, w - n1);
            }
        }
        addr += row2;
        src += (ushort)bm->row;
    }
}

void bank8_get_flat8_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *win = grd_cap->vbase, *dst = bm->bits;
    short w = bm->w, h = bm->h;
    ushort save = grd_bank8_bank;
    long addr = (long)grd_bm.bits + y * (ulong)(ushort)grd_bm.row + x, room;

    bank8_first(addr, save);
    while (h-- != 0) {
        room = 0x10000 - (addr & 0xffff);
        if (room >= w) {
            copy(dst, win + (addr & 0xffff), w);
            if (room <= (ushort)grd_bm.row)
                bank8_next();
        } else {
            copy(dst, win + (addr & 0xffff), room);
            bank8_next();
            copy(dst + room, win, w - room);
        }
        addr += (ushort)grd_bm.row;
        dst += (ushort)bm->row;
    }
    bank8_restore(save);
}

void bank8_draw_device_ubitmap(grs_bitmap *bm, short x, short y)
{
    grs_canvas cnv;
    grs_canvas *tmp = gr_alloc_canvas(BMT_FLAT8, bm->w, bm->h);

    if (tmp == NULL)
        return;
    gr_make_canvas(bm, &cnv);
    gr_push_canvas(&cnv);
    bank8_get_flat8_ubitmap(&tmp->bm, 0, 0);
    gr_pop_canvas();
    bank8_flat8_ubitmap(&tmp->bm, x, y);
    gr_free_canvas(tmp);
}

static void fill(uchar *p, long c, long n)
{
    ulong a = ((ulong)c & 0xffff00ff) | (((ulong)c & 0xff) << 8);
    ulong d = (a << 16) | a, i;

    for (i = 0; i < ((ulong)n >> 2); i++, p += 4)
        *(ulong *)p = d;
    for (i = 0; i < ((ulong)n & 3); i++)
        *p++ = (uchar)c;
}

void bank8_uhline_fill(short x0, short y, short x1, long c, long parm)
{
    uchar *win = grd_cap->vbase;
    long n = x1 - x0 + 1, room;
    long addr = (long)grd_bm.bits + y * (ulong)(ushort)grd_bm.row;
    int l = grd_bank8_lock;
    long bank;

    if (n <= 0)
        x0 = x1;
    addr += x0;
    bank = addr >> 16;
    if (l != 0 || grd_bank8_bank != bank) {
        grd_bank8_bank = (ushort)bank;
        grd_bank8_lock = l + 1;
        SET_BANK();
        grd_bank8_lock--;
    }
    if (n < 0)
        n = -n;
    room = 0x10000 - (addr & 0xffff);
    if (n <= room)
        fill(win + (addr & 0xffff), c, n);
    else {
        fill(win + (addr & 0xffff), c, room);
        bank8_next();
        fill(win, c, n - room);
    }
}
