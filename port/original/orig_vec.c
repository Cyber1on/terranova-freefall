/* orig_vec.c -- the 2D library's vector picture interpreter (unreferenced by System Shock's 2D
 * code; used by the game's scripts, op 0x62), a C version for the standalone build (the
 * verification build still runs the original code).  It follows the original instruction by
 * instruction; the address is the original's.
 *
 *   0x3091F0 gr_int_3091F0   draw a picture at (x, y) in its own size
 *   0x309264 gr_int_309264   draw a picture into a rectangle (x0 y0 x1 y1), scaled to it
 *
 * A picture: short w, h, then opcodes (a byte, 0 ends; bytes above 0x10 are skipped).
 * Coordinates are shorts in picture units, mapped to fix as x * scale + origin (rounded to the
 * nearest pixel for the integer primitives):
 *    1 c           fill the whole rectangle in colour c (unscaled; the colour is restored)
 *    2 c / 3 c     foreground / background colour
 *    4 x y         point
 *    5 x0 y x1     horizontal line      6 x y0 y1      vertical line
 *    7 x0 y0 x1 y1 line (fix)
 *    8 / 9 x0 y0 x1 y1             box / filled rectangle
 *    10 / 11 x y r                 circle / disk (fix; r gets the x origin added)
 *    12 / 13 n (x y)*n             polyline / filled polygon (n <= 32, else skipped)
 *    14 ref x y                    bitmap (scaled to the picture's scale; the width and height
 *                                  passed get the origin added); continues at y (!)
 *    15 font x y string            text at (x, y), unscaled
 *    16 ref x0 y0 x1 y1            another picture, into that rectangle
 */
#include "2d.h"
#include "res.h"

extern fix gr_vec_pts[32][2];      /* 0x44DB20 polygon vertices */
extern fix *gr_vec_plist[32];      /* 0x44DC20 pointers to them */
extern fix gr_vec_oy;              /* 0x44DCA0 origin */
extern fix gr_vec_ox;              /* 0x44DCA4 */
extern fix gr_vec_sy;              /* 0x44DCA8 scale */
extern fix gr_vec_sx;              /* 0x44DCAC */

#define DRAW_POINT 10
#define DRAW_RECT 16
#define DRAW_BOX 18
#define FIX_POLY 26
#define FIX_CIRCLE 133
#define FIX_DISK 137
#define DRAW_STRING 315

#define S16(o) (*(short *)(p + (o)))
#define FX(v) (fmul((fix)(v) << 16, gr_vec_sx) + gr_vec_ox)
#define FY(v) (fmul((fix)(v) << 16, gr_vec_sy) + gr_vec_oy)
#define RND(f) ((short)((fix)((f) + 0x8000) >> 16))

static fix fmul(fix a, fix b)
{
    return (fix)(((long long)a * b) >> 16);
}

/* (a << 16 as 48 bits) idiv b; a quotient that does not fit traps like the CPU does */
static fix fdiv(fix a, fix b)
{
    long q, hi = (short)a;          /* edx = (a << 16) >> 16, eax = a << 32 (= 0) */
    __asm__ volatile("idivl %3" : "=a"(q), "+d"(hi) : "0"(0L), "rm"(b) : "cc");
    return q;
}

void gr_int_309264(uchar *pic, short *r)
{
    uchar *p = pic + 4, op, scaled, save;
    short x, y, x1, y1, n, i;
    fix fx, fy, fx1, fy1;
    grs_bitmap *bm;

    if (r[2] - r[0] == ((short *)pic)[0] && r[3] - r[1] == ((short *)pic)[1]) {
        scaled = 0;
        gr_vec_sx = gr_vec_sy = 0x10000;
    } else {
        scaled = 1;
        gr_vec_sx = fdiv(r[2] - r[0], (fix)((short *)pic)[0] << 16);
        gr_vec_sy = fdiv(r[3] - r[1], (fix)((short *)pic)[1] << 16);
    }
    gr_vec_ox = (fix)r[0] << 16;
    gr_vec_oy = (fix)r[1] << 16;
    for (;;) {
        do
            op = *p++;
        while (op > 0x10);
        switch (op) {
        case 0:
            return;
        case 1:
            save = (uchar)grd_canvas->gc.fcolor;
            grd_canvas->gc.fcolor = p[0];
            WATCOM_CALL(grd_canvas_table[DRAW_RECT], (long)r[0], (long)r[1], (long)r[2], (long)r[3]);
            grd_canvas->gc.fcolor = save;
            p += 1;
            break;
        case 2:
            grd_canvas->gc.fcolor = p[0];
            p += 1;
            break;
        case 3:
            grd_canvas->gc.bcolor = p[0];
            p += 1;
            break;
        case 4:
            x = RND(FX(S16(0)));
            y = RND(FY(S16(2)));
            WATCOM_CALL(grd_canvas_table[DRAW_POINT], (long)x, (long)y);
            p += 4;
            break;
        case 5:
        case 6:
            x = RND(FX(S16(0)));
            y = RND(FY(S16(2)));
            if (op == 5)
                gen_hline(x, y, RND(FX(S16(4))));
            else
                gen_vline(x, y, RND(FY(S16(4))));
            p += 6;
            break;
        case 7:
            gr_fix_line(FX(S16(0)), FY(S16(2)), FX(S16(4)), FY(S16(6)));
            p += 8;
            break;
        case 8:
        case 9:
            x = RND(FX(S16(0)));
            y = RND(FY(S16(2)));
            x1 = RND(FX(S16(4)));
            y1 = RND(FY(S16(6)));
            WATCOM_CALL(grd_canvas_table[op == 8 ? DRAW_BOX : DRAW_RECT], (long)x, (long)y, (long)x1, (long)y1);
            p += 8;
            break;
        case 10:
        case 11:
            fx = FX(S16(0));
            fy = FY(S16(2));
            fx1 = FX(S16(4));
            WATCOM_CALL(grd_canvas_table[op == 10 ? FIX_CIRCLE : FIX_DISK], fx, fy, fx1);
            p += 6;
            break;
        case 12:
        case 13:
            n = *p++;
            if (n > 32) {
                p += n * 8;
                break;
            }
            for (i = 0; i < n; i++) {
                gr_vec_plist[i] = gr_vec_pts[i];
                gr_vec_pts[i][0] = FX(S16(0));
                gr_vec_pts[i][1] = FY(S16(2));
                p += 4;
                if (op == 12 && i > 0)
                    gr_fix_line(gr_vec_pts[i - 1][0], gr_vec_pts[i - 1][1], gr_vec_pts[i][0], gr_vec_pts[i][1]);
            }
            if (op == 13)
                WATCOM_CALL(grd_canvas_table[FIX_POLY], grd_canvas->gc.fcolor, (long)n, (long)gr_vec_plist);
            break;
        case 14: {
            Ref ref = *(Ref *)p;
            x = RND(FX(S16(4)));
            y = RND(FY(S16(6)));
            if (scaled) {
                bm = RefGet(ref);
                x1 = RND(FX(bm->w));
                y1 = RND(FY(bm->h));
                bm = RefGet(ref);
                bm->bits = (uchar *)bm + 0x1C;
                WATCOM_CALL(grd_canvas_table[SCALE_DEVICE_BITMAP + 2 * bm->type], (long)bm, (long)x,
                            (long)y, (long)x1, (long)y1);
            } else {
                bm = RefGet(ref);
                bm->bits = (uchar *)bm + 0x1C;
                WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, (long)x, (long)y);
            }
            p += 6;
            break;
        }
        case 15: {
            ushort id = (ushort)S16(0);
            x = S16(2);
            y = S16(4);
            grd_canvas->gc.font = ResLock(id);
            WATCOM_CALL(grd_canvas_table[DRAW_STRING], (long)grd_canvas->gc.font, (long)(p + 6), (long)x, (long)y);
            ResUnlock(id);
            p += 6 + strlen((char *)p + 6) + 1;
            break;
        }
        case 16: {
            Ref ref = *(Ref *)p;
            short sr[4];
            fix sx = gr_vec_sx, sy = gr_vec_sy, ox = gr_vec_ox, oy = gr_vec_oy;
            uchar fc = (uchar)grd_canvas->gc.fcolor, bc = (uchar)grd_canvas->gc.bcolor;

            sr[0] = RND(FX(S16(4)));
            sr[1] = RND(FY(S16(6)));
            sr[2] = RND(FX(S16(8)));
            sr[3] = RND(FY(S16(10)));
            gr_int_309264(RefLock(ref), sr);
            ResUnlock((ushort)(ref >> 16));
            gr_vec_sx = sx;
            gr_vec_sy = sy;
            gr_vec_ox = ox;
            gr_vec_oy = oy;
            grd_canvas->gc.fcolor = fc;
            grd_canvas->gc.bcolor = bc;
            p += 12;
            break;
        }
        }
    }
}

void gr_int_3091F0(uchar *pic, short x, short y)
{
    short r[4];

    r[0] = x;
    r[1] = y;
    r[2] = ((short *)pic)[0] + x;
    r[3] = ((short *)pic)[1] + y;
    gr_int_309264(pic, r);
}
