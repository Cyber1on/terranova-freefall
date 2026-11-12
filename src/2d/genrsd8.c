/* genrsd8.c -- Terra Nova v1.09: RSD8 (run-length) bitmaps drawn through the generic fill
 * functions (function table GRC_BITMAP / GRC_STENCIL_BITMAP of BMT_RSD8 for the fill types
 * other than FILL_NORM).  Decompiled from __FF.EXE (Watcom C).
 *
 *   0x333900 gen_rsd8_ubitmap        0x333B9C gen_rsd8_bitmap
 *   0x33BCA0 gen_rsd8_solid_ubitmap  0x33BEF8 gen_rsd8_solid_bitmap
 *            (canvas tables 0x36808C / 0x36A558 SOLID_RSD8_UBITMAP / SOLID_RSD8_BITMAP, Shock's
 *             names (not implemented there): the extra colour argument goes to grd_gc.fcolor and every run and literal
 *             span is drawn as a uhline of that colour)
 *
 * Runs go to the current uhline fill (grd_uline_fill_vector[GRL_UHLINE]) after the run's byte
 * is stored in grd_gc.fcolor; literal bytes go one by one to gen_fill_pixel.  RSD8 codes as in
 * fl8rsd8.c.
 *
 * Quirks kept:
 *  - gen_rsd8_bitmap has no unclipped shortcut, and clips the right edge only when
 *    x + w > clip right (gri_flat8_rsd8_bitmap: >=).
 *  - grd_gc.fcolor is left set to the last run's byte.
 *  - gen_rsd8_ubitmap returns nothing meaningful (eax is left over); here void.
 */
#include "2d_tmap.h"

extern void (**grd_uline_fill_vector)();    /* 0x44C5AC */
int gen_fill_pixel(long color, short x, short y);   /* 0x2FAD20 */
#define GRL_UHLINE 2

enum { OP_RUN, OP_SKIP, OP_LIT };

/* reads the next code; returns 0 at the end of the data */
static int rsd_op(uchar **ps, short *len, int *op)
{
    uchar *s = *ps;
    uchar c = *s;
    if (c == 0) {
        *op = OP_RUN;
        *len = s[1];
        *ps = s + 2;
    } else if (c < 0x80) {
        *op = OP_LIT;
        *len = c;
        *ps = s + 1;
    } else if (c > 0x80) {
        *op = OP_SKIP;
        *len = c & 0x7f;
        *ps = s + 1;
    } else {
        ushort w = *(ushort *)(s + 1);
        *ps = s + 3;
        if (w >= 0xc000) {
            *op = OP_RUN;
            *len = w & 0x3fff;
        } else if (w >= 0x8000) {
            *op = OP_LIT;
            *len = w & 0x7fff;
        } else {
            if (w == 0)
                return 0;
            *op = OP_SKIP;
            *len = w;
        }
    }
    return 1;
}

#define UHLINE(x, y, k) WATCOM_CALL(grd_uline_fill_vector[GRL_UHLINE], (long)(x), (long)(y), \
        (long)(short)((x) + (k) - 1), (long)grd_gc.fcolor, (long)grd_gc.fill_parm)

/* draws k pixels of the current code at (x, y) */
static void draw(uchar **ps, int op, short k, short x, short y, int whole, int solid)
{
    uchar *s = *ps;
    short i;
    if (op == OP_RUN) {
        if (!solid)
            grd_gc.fcolor = *s;
        UHLINE(x, y, k);
        if (whole)
            *ps = s + 1;
    } else if (op == OP_LIT) {
        if (solid)
            UHLINE(x, y, k);
        else
            for (i = 0; i < k; i++)
                gen_fill_pixel(s[i], (short)(x + i), y);
        *ps = s + k;
    }
}

static void rsd8_ubitmap(grs_bitmap *bm, short x, short y, int solid)
{
    uchar *src = bm->bits;
    short len = 0, xe = x + bm->w, ye = y + bm->h;
    int op = OP_RUN;

    for (; y < ye; y++) {
        while (x < xe) {
            if (len == 0 && !rsd_op(&src, &len, &op))
                return;
            if (x + len > xe) {
                short k = xe - x;
                draw(&src, op, k, x, y, 0, solid);
                len -= k;
                x += k;
            } else {
                draw(&src, op, len, x, y, 1, solid);
                x += len;
                len = 0;
            }
        }
        x -= bm->w;
    }
}

static int rsd8_bitmap(grs_bitmap *bm, short x, short y, int solid, long color)
{
    short x_off = 0, y_off = 0, xc = x, yc = y;
    short x_right = x + bm->w, y_bot = y + bm->h;
    short len = 0;
    int op = OP_RUN, code = CLIP_NONE;
    uchar *src = bm->bits;

    if (x > grd_gc.clip.i.right || x_right <= grd_gc.clip.i.left
        || y > grd_gc.clip.i.bot || y_bot <= grd_gc.clip.i.top)
        return CLIP_ALL;
    if (solid)
        grd_gc.fcolor = color;
    if (x < grd_gc.clip.i.left) {
        xc = grd_gc.clip.i.left;
        x_off = grd_gc.clip.i.left - x;
        code = 1;
    }
    if (y < grd_gc.clip.i.top) {
        y_off = grd_gc.clip.i.top - y;
        yc = grd_gc.clip.i.top;
        code |= 2;
    }
    if (x_right > grd_gc.clip.i.right) {
        code |= 4;
        x_right = grd_gc.clip.i.right;
    }
    if (y_bot > grd_gc.clip.i.bot) {
        code |= 8;
        y_bot = grd_gc.clip.i.bot;
    }

    /* skip the clipped rows and the first row's left part */
    if (y_off > 0 || x_off > 0) {
        ulong target = (ulong)(ushort)bm->row * y_off + x_off;
        ulong n = 0;
        while (n < target) {
            if (len == 0 && !rsd_op(&src, &len, &op))
                return code;
            if ((ulong)len + n <= target) {
                if (op == OP_RUN)
                    src++;
                else if (op == OP_LIT)
                    src += len;
                n += len;
                len = 0;
            } else {
                short k = (short)(target - n);
                if (op == OP_LIT)
                    src += k;
                n += k;
                len -= k;
            }
        }
    }

    for (; yc < y_bot; yc++) {
        int xl = x + x_off;
        while (xc < xl) {
            if (len == 0 && !rsd_op(&src, &len, &op))
                return code;
            if (xc + len > xl) {
                short k = (short)(xl - xc);
                if (op == OP_LIT)
                    src += k;
                len -= k;
                xc += k;
            } else {
                if (op == OP_RUN)
                    src++;
                else if (op == OP_LIT)
                    src += len;
                xc += len;
                len = 0;
            }
        }
        while (xc < x_right) {
            if (len == 0 && !rsd_op(&src, &len, &op))
                return code;
            if (xc + len > x_right) {
                short k = x_right - xc;
                draw(&src, op, k, xc, yc, 0, solid);
                xc += k;
                len -= k;
            } else {
                draw(&src, op, len, xc, yc, 1, solid);
                xc += len;
                len = 0;
            }
        }
        xc -= bm->w;
    }
    return code;
}

void gen_rsd8_ubitmap(grs_bitmap *bm, short x, short y)
{
    rsd8_ubitmap(bm, x, y, 0);
}

int gen_rsd8_bitmap(grs_bitmap *bm, short x, short y)
{
    return rsd8_bitmap(bm, x, y, 0, 0);
}

void gen_rsd8_solid_ubitmap(grs_bitmap *bm, short x, short y, long color)
{
    grd_gc.fcolor = color;
    rsd8_ubitmap(bm, x, y, 1);
}

int gen_rsd8_solid_bitmap(grs_bitmap *bm, short x, short y, long color)
{
    return rsd8_bitmap(bm, x, y, 1, color);
}
