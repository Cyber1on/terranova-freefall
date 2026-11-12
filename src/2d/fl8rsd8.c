/* fl8rsd8.c -- Terra Nova v1.09 flat8 canvas: RSD8 (run-length) bitmaps and the RSD8 texture
 * mapper/perspective inits that unpack into grd_unpack_buf.  Decompiled from __FF.EXE.
 *
 *   0x331F50 gri_flat8_rsd8_ubitmap      0x331F94 gri_flat8_rsd8_bitmap
 *   0x332590 gri_rsd8_tm_init            0x3325E4 gri_rsd8_pm_init
 *
 * RSD8 codes: 00 n c = run of n c; 01..7F = literal of that many bytes; 81..FF = skip n&7F;
 * 80 w: w = 0 end, w < 8000 skip w, w < C000 literal w&7FFF, else run (w&3FFF) of the next byte.
 */
#include "2d_tmap.h"

int gr_rsd8_convert(grs_bitmap *sbm, grs_bitmap *dbm);                /* 0x2E0F70 (2d-prim csv, orig) */
long gri_rsd8_blit(uchar *src, uchar *dst, ulong row, int w);         /* 0x34E6E0 (2d-core) */

#define DRAW_UBITMAP 142                    /* canvas table: + 2*bm->type */

long gri_flat8_rsd8_ubitmap(grs_bitmap *bm, short x, short y)
{
    uchar *d = grd_bm.bits + (long)y * (ushort)grd_bm.row + x;
    return gri_rsd8_blit(bm->bits, d, (ushort)grd_bm.row, bm->w);
}

enum { OP_RUN, OP_SKIP, OP_LIT };

/* reads the next code; returns 0 at the end of the data */
static int rsd_op(uchar **ps, short *len, int *op)
{
    uchar *s = *ps;
    uchar c = *s;
    if (c == 0) {
        *op = OP_RUN;
        *len = s[1];
        *ps = s + 2;                        /* at the run's byte */
        return 1;
    }
    if (c < 0x80) {
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
        if (w >= 0x8000) {
            if (w >= 0xc000) {
                *op = OP_RUN;
                *len = w & 0x3fff;
            } else {
                *op = OP_LIT;
                *len = w & 0x7fff;
            }
        } else {
            if (w == 0)
                return 0;
            *op = OP_SKIP;
            *len = w;
        }
    }
    return 1;
}

/* function table GRC_STENCIL_BITMAP for BMT_RSD8 (the stencil argument is ignored) */
int gri_flat8_rsd8_bitmap(grs_bitmap *bm, short x, short y)
{
    short x_off = 0, y_off = 0;
    short xc = x, yc = y;
    short x_right = x + bm->w, y_bot = y + bm->h;
    short len = 0;
    int op = OP_RUN;
    int code = CLIP_NONE;
    uchar *src = bm->bits, *d;

    if (x > grd_gc.clip.i.right || x_right <= grd_gc.clip.i.left
        || y > grd_gc.clip.i.bot || y_bot <= grd_gc.clip.i.top)
        return CLIP_ALL;
    if (x < grd_gc.clip.i.left) {
        x_off = grd_gc.clip.i.left - x;
        xc = grd_gc.clip.i.left;
        code = 1;
    }
    if (y < grd_gc.clip.i.top) {
        yc = grd_gc.clip.i.top;
        code |= 2;
        y_off = grd_gc.clip.i.top - y;
    }
    if (x_right >= grd_gc.clip.i.right) {
        x_right = grd_gc.clip.i.right;
        code |= 4;
    }
    if (y_bot > grd_gc.clip.i.bot) {
        y_bot = grd_gc.clip.i.bot;
        code |= 8;
    }
    if (code == CLIP_NONE) {
        WATCOM_CALL((void *)grd_canvas_table[DRAW_UBITMAP + 2 * bm->type], (long)bm, (long)x, (long)y);
        return CLIP_NONE;
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

    d = grd_bm.bits + (long)yc * (ushort)grd_bm.row + xc;
    if (yc >= y_bot)
        return code;
    do {
        int xl = x + x_off;              /* int compare (not short) */
        /* left part (and the previous row's right part) */
        while (xc < xl) {
            if (len == 0 && !rsd_op(&src, &len, &op))
                return code;
            if (xc + len <= xl) {
                if (op == OP_RUN)
                    src++;
                else if (op == OP_LIT)
                    src += len;
                xc += len;
                len = 0;
            } else {
                short k = xl - xc;
                if (op == OP_LIT)
                    src += k;
                len -= k;
                xc += k;
            }
        }
        /* visible part */
        while (xc < x_right) {
            short k;
            if (len == 0 && !rsd_op(&src, &len, &op))
                return code;
            if (xc + len <= x_right) {
                k = len;
                if (op == OP_RUN) {
                    memset(d, *src, k);
                    src++;
                } else if (op == OP_LIT) {
                    memcpy(d, src, k);
                    src += k;
                }
                len = 0;
            } else {
                k = x_right - xc;
                if (op == OP_RUN)
                    memset(d, *src, k);
                else if (op == OP_LIT) {
                    memcpy(d, src, k);
                    src += k;
                }
                len -= k;
            }
            xc += k;
            d += k;
        }
        xc -= bm->w;
        d += (ushort)grd_bm.row - (x_right - x) + x_off;
        yc++;
    } while (yc < y_bot);
    return code;
}

/* texture mapper init for BMT_RSD8: unpack and run the init of the unpacked type */
long gri_rsd8_tm_init(grs_tmap_loop_info *ti)
{
    grs_bitmap bm;
    int r;
    if (grd_unpack_buf == NULL)
        return (long)ti;
    r = gr_rsd8_convert(&ti->bm, &bm);
    if (r != 0)
        return r;
    ti->bm.bits = bm.bits;
    ti->n += (bm.type - BMT_RSD8) * 65;
    return WATCOM_CALL(grd_function_table[ti->n], (long)ti);
}

/* perspective init for BMT_RSD8 (ps +10 holds the function index at this point) */
long gri_rsd8_pm_init(grs_bitmap *b, long *ps)
{
    grs_bitmap bm;
    int r;
    if (grd_unpack_buf == NULL)
        return (long)b;
    r = gr_rsd8_convert(b, &bm);
    if (r != 0)
        return r;
    b->bits = bm.bits;
    ps[4] += (bm.type - BMT_RSD8) * 65;
    return WATCOM_CALL(grd_function_table[ps[4]], (long)b, (long)ps);
}
