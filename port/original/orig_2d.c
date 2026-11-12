/* orig_2d.c -- C versions of 2D routines that the decompilation kept as machine code, written
 * for the standalone build (the verification build still runs the original code for them).
 * Each follows the original instruction by instruction; the address is the original's.
 *
 *   0x2E0F70 gr_rsd8_convert   RSD8 bitmap -> flat bitmap in grd_unpack_buf (RSDCVT.C)
 *   0x317DF0 water_left_edge   water renderer edge scanners (self-modifying asm)
 *   0x317EA0 water_right_edge
 *   0x2FB3A0 crt_2FB3A0        water texture mapper (lit floor mapper with the reflection)
 *   0x324FF0 gr_null           empty function of the 2D function tables
 */
#include "2d.h"

/* ---------------------------------------------------------------- RSDCVT.C */

/* System Shock's rsdcvt.c (the same in Terra Nova): skips and padding are written as 0.
 * A token that runs past the right edge is split; the remainder continues on the next row. */
int gr_rsd8_convert(grs_bitmap *sbm, grs_bitmap *dbm)
{
    short x_right, y_bot, x, y, rsd_count, op_count;
    int rsd_code = 0, over_run;
    uchar *p_dst, *rsd_src;

    if (grd_unpack_buf == NULL)
        return 1;
    if (sbm->type != BMT_RSD8)
        return 2;
    *dbm = *sbm;
    dbm->type = sbm->flags & BMF_TLUC8 ? BMT_TLUC8 : BMT_FLAT8;
    dbm->bits = grd_unpack_buf;
    if (dbm->w == dbm->row) {
        p_dst = gr_rsd8_unpack(sbm->bits, dbm->bits);
        goto done;
    }
    rsd_src = sbm->bits;
    x = y = rsd_count = 0;
    x_right = sbm->w;
    y_bot = sbm->h;
    p_dst = dbm->bits;
    over_run = dbm->row - sbm->w;
    while (y < y_bot) {
        while (x < x_right) {
            if (rsd_count == 0) {
                uchar c = *rsd_src;
                if (c == 0) {
                    rsd_code = 0;
                    rsd_count = rsd_src[1];
                    rsd_src += 2;
                } else if (c < 0x80) {
                    rsd_code = 2;
                    rsd_count = c;
                    rsd_src++;
                } else if (c != 0x80) {
                    rsd_code = 1;
                    rsd_count = c & 0x7F;
                    rsd_src++;
                } else {
                    ushort w = rsd_src[1] | rsd_src[2] << 8;
                    rsd_src += 3;
                    if (w >= 0xC000) {
                        rsd_code = 0;
                        rsd_count = w & 0x3FFF;
                    } else if (w >= 0x8000) {
                        rsd_code = 2;
                        rsd_count = w & 0x7FFF;
                    } else if (w != 0) {
                        rsd_code = 1;
                        rsd_count = w;
                    } else
                        goto done;
                }
            }
            if (x + rsd_count <= x_right) {
                if (rsd_code == 0)
                    memset(p_dst, *rsd_src++, rsd_count);
                else if (rsd_code == 1)
                    memset(p_dst, 0, rsd_count);
                else {
                    memcpy(p_dst, rsd_src, rsd_count);
                    rsd_src += rsd_count;
                }
                x += rsd_count;
                p_dst += rsd_count;
                rsd_count = 0;
            } else {
                op_count = x_right - x;
                if (rsd_code == 0)
                    memset(p_dst, *rsd_src, op_count);
                else if (rsd_code == 1)
                    memset(p_dst, 0, op_count);
                else {
                    memcpy(p_dst, rsd_src, op_count);
                    rsd_src += op_count;
                }
                x += op_count;
                p_dst += op_count;
                rsd_count -= op_count;
            }
        }
        x -= sbm->w;
        if (over_run) {
            memset(p_dst, 0, over_run);
            p_dst += over_run;
        }
        y++;
    }
done:
    over_run = (int)(dbm->bits + dbm->row * dbm->h - p_dst);
    if (over_run)
        memset(p_dst, 0, over_run);
    return 0;
}

/* ---------------------------------------------------------------- water edges */

extern uchar **water_clut_row;     /* 0x44CE30 */
extern long *water_tab;            /* 0x44CE34 */
extern long water_src_off;         /* 0x44CE38 */

/* (edx:eax) idiv d, as the original: a quotient that does not fit traps like the CPU does
 * (the port's divide-error handler then gives the original handler's result) */
static long idiv64(long long n, long d)
{
    long q, hi = (long)(n >> 32);
    __asm__ volatile("idivl %3" : "=a"(q), "+d"(hi) : "0"((long)n), "rm"(d) : "cc");
    return q;
}

#define CEIL16(v) ((long)((ulong)(v) + 0xFFFF) >> 16)

/* The edge from vertex *v0 to the lower vertex *v1 (each [0] = scan coordinate, [1] = x):
 * the rounded-up x of every scan line it crosses, as the high dword of a 48.16 accumulator,
 * for scan lines first .. first + count - 1.  Returns count (<= 0: nothing to do). */
static long water_edge(fix **v1, fix **v0, long *first, unsigned long long *acc,
                       unsigned long long *step)
{
    fix *a = *v1, *b = *v0;
    long y_end = CEIL16(a[0]), y0 = CEIL16(b[0]);
    long dy = a[0] - b[0], frac = -b[0] & 0xFFFF, count = y_end - y0;
    long x = b[1], dx = a[1] - b[1], slope = 0;

    if (count <= 0)
        return count;
    if (dx != 0) {
        long bias = dx >= 0;
        slope = idiv64((long long)dx << 16, dy) + bias;
        x += bias;
    }
    x += 0xFFFF;
    x += (long)(((long long)slope * frac) >> 16);
    *step = (unsigned long long)((long long)slope << 16);
    *acc = (unsigned long long)((long long)x << 16) - *step;
    *first = y0;
    return count;
}

/* 1 if a scan line's entry in buf lies right of the edge */
int water_left_edge(fix **v1, fix **v0, long *buf)
{
    unsigned long long acc, step;
    long y, n = water_edge(v1, v0, &y, &acc, &step);

    for (; n > 0; n--, y++) {
        acc += step;
        if (buf[y] > (long)(acc >> 32))
            return 1;
    }
    return 0;
}

/* Moves buf's entries left onto the edge where they lie right of it (once one has moved, an
 * entry equal to the edge is stored too, until an entry lies left of it); 1 if any moved. */
int water_right_edge(fix **v1, fix **v0, long *buf)
{
    unsigned long long acc, step;
    long y, n = water_edge(v1, v0, &y, &acc, &step), x;
    int ret = 0, follow = 0;

    for (; n > 0; n--, y++) {
        acc += step;
        x = (long)(acc >> 32);
        if (follow ? buf[y] >= x : buf[y] > x) {
            buf[y] = x;
            ret = 1;
            follow = 1;
        } else
            follow = 0;
    }
    return ret;
}

/* ---------------------------------------------------------------- water mapper */

/* refl: [0] reflection top row, [1] colour table per row, [2] per-texel water entries.
 * Only flat 8-bit textures without the transparency flag whose row is a power of two;
 * they are drawn as type 1 (the lit floor mapper's water variant). */
void crt_2FB3A0(grs_bitmap *bm, long n, grs_vertex **pts, long *refl)
{
    grs_tmap_info ti;
    long ymin, i;
    short y;

    if (bm->type != BMT_FLAT8 || (bm->flags & 1) || bm->row != 1L << bm->wlog)
        return;
    bm->type = 1;
    ymin = pts[0]->y;
    for (i = 1; i < n; i++)
        if (ymin > pts[i]->y)
            ymin = pts[i]->y;
    y = (short)CEIL16(ymin);
    water_src_off = (refl[0] - y) * 2 * grd_canvas->bm.row;
    water_clut_row = (uchar **)refl[1] + y;
    water_tab = (long *)refl[2];
    ti.tmap_type = 0x25;
    ti.flags = 4;
    ti.clut = NULL;                 /* not set by the original (stack garbage); not read */
    h_umap(bm, n, pts, &ti);
    bm->type = BMT_FLAT8;
}

void gr_null(void)
{
}
