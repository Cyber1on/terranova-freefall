/* mapsharp.c -- Freefall: the marks of the squad map and the full-screen map at the 3D
 * resolution.
 *
 * port_s3d_map (sharp3d.c) runs the game's map drawing (mapdisp_276EF8) a second time on a
 * canvas kx x ky times the map's size, its scale divided by kx (a picture pixel as tall as it is
 * wide: in 320x400 kx = 2 ky, the y shift taken out).  The terrain, the blast rings and the view
 * cone scale by themselves; what the game draws a pixel at a time comes here while
 * port_map_kx > 1 (mapdisp.c), at the picture's scale and at its exact place (no longer on the
 * game's pixels): dots, text and small bitmaps as kx x ky blocks of the original's pixels, lines,
 * crosses, boxes and circles half a game pixel thick.  Colours as the game's: through the clut
 * while the map draws with one (fill type 1).
 */
#include <math.h>
#include <string.h>
#include "2d.h"
#include "port.h"
#include "platform.h"

void GfxDrawText(int font, char *s, int x, int y);

int port_map_kx = 1, port_map_ky = 1;   /* > 1: the map is drawn sharp (sharp3d.c) */
int port_map_sh;                        /* the game's y shift (hud_35A544) meanwhile */
int port_map_low_h;                     /* the game's map canvas height (rndsky's discs) */
int port_map_pass;                      /* the terrain drawn for the sharp map (mapdisp.c) */
int port_map_smooth;                    /* "Map smoothing" On (mapdisp.c, smooth_terrain) */

/* half a game pixel, at least one */
static int thick(void)
{
    return port_map_kx / 2 > 1 ? port_map_kx / 2 : 1;
}

static uchar map_colour(long c)
{
    grs_canvas *cv = grd_canvas;
    if (cv->gc.fill_type == 1)
        return ((uchar *)cv->gc.fill_parm)[(uchar)c];
    return (uchar)c;
}

/* [x0, x1) x [y0, y1), clipped */
static void fill(long x0, long y0, long x1, long y1, uchar c)
{
    grs_canvas *cv = grd_canvas;
    long y;
    if (x0 < cv->gc.clip.i.left)
        x0 = cv->gc.clip.i.left;
    if (y0 < cv->gc.clip.i.top)
        y0 = cv->gc.clip.i.top;
    if (x1 > cv->gc.clip.i.right)
        x1 = cv->gc.clip.i.right;
    if (y1 > cv->gc.clip.i.bot)
        y1 = cv->gc.clip.i.bot;
    if (x1 <= x0)
        return;
    for (y = y0; y < y1; y++)
        memset(cv->bm.bits + y * cv->bm.row + x0, c, x1 - x0);
}

/* a run of n pixels around pixel p's centre (n and the line's thickness alike odd or even, so
 * crossing bars share their middle) */
static void run(long p, long n, long *a, long *b)
{
    *a = p - (n - 1) / 2;
    *b = *a + n;
}

static long even_as(long n, long t)
{
    return ((n ^ t) & 1) ? n + 1 : n;
}

/* a game pixel: a kx x ky block on pixel (x, y) */
void port_map_point(long x, long y)
{
    long x0, x1, y0, y1;
    run(x, port_map_kx, &x0, &x1);
    run(y, port_map_ky, &y0, &y1);
    fill(x0, y0, x1, y1, map_colour(grd_canvas->gc.fcolor));
}

/* the game's cross: 3 game pixels across, 2 ay + 1 down */
void port_map_cross(long x, long y, int ay)
{
    long t = thick(), a, b, c, d;
    uchar col = map_colour(grd_canvas->gc.fcolor);
    run(x, even_as(3 * port_map_kx, t), &a, &b);
    run(y, t, &c, &d);
    fill(a, c, b, d, col);
    run(y, even_as((2 * ay + 1) * port_map_ky, t), &c, &d);
    run(x, t, &a, &b);
    fill(a, c, b, d, col);
}

/* an outline [x0, x1) x [y0, y1) */
static void box(long x0, long y0, long x1, long y1, uchar col)
{
    long t = thick();
    if (x1 - x0 <= 2 * t || y1 - y0 <= 2 * t) {
        fill(x0, y0, x1, y1, col);
        return;
    }
    fill(x0, y0, x1, y0 + t, col);
    fill(x0, y1 - t, x1, y1, col);
    fill(x0, y0 + t, x0 + t, y1 - t, col);
    fill(x1 - t, y0 + t, x1, y1 - t, col);
}

/* the game's box (DRAW_BOX) from (x0, y0) to (x1, y1) exclusive, in the picture's pixels */
void port_map_box(long x0, long y0, long x1, long y1)
{
    box(x0, y0, x1, y1, map_colour(grd_canvas->gc.fcolor));
}

/* a nav marker's box: the game's 3 x 3 pixels from (x - 2, y - 2) */
void port_map_navbox(long x, long y)
{
    long a, b, c, d;
    run(x - port_map_kx, even_as(3 * port_map_kx, thick()), &a, &b);
    run(y - port_map_ky, even_as(3 * port_map_ky, thick()), &c, &d);
    box(a, c, b, d, map_colour(grd_canvas->gc.fcolor));
}

/* a line between two points of the picture (fix, pixel centres at .5) */
static void line(double x0, double y0, double x1, double y1, uchar col)
{
    double dx = x1 - x0, dy = y1 - y0, len = sqrt(dx * dx + dy * dy), t = thick(), h;
    long i, a, b;
    if (fabs(dx) >= fabs(dy)) {         /* a run down each column */
        if (x0 > x1) {
            double s = x0; x0 = x1; x1 = s;
            s = y0; y0 = y1; y1 = s;
            dx = -dx;
            dy = -dy;
        }
        h = dx > 0 ? t * len / dx : t;
        for (i = (long)floor(x0); i <= (long)floor(x1); i++) {
            double xc = i + 0.5, y;
            if (xc < x0)
                xc = x0;
            if (xc > x1)
                xc = x1;
            y = dx > 0 ? y0 + (xc - x0) * dy / dx : y0;
            a = (long)floor(y - h / 2 + 0.5);
            b = (long)floor(y + h / 2 + 0.5);
            fill(i, a, i + 1, b > a ? b : a + 1, col);
        }
    } else {                            /* a run across each row */
        if (y0 > y1) {
            double s = x0; x0 = x1; x1 = s;
            s = y0; y0 = y1; y1 = s;
            dx = -dx;
            dy = -dy;
        }
        h = t * len / dy;
        for (i = (long)floor(y0); i <= (long)floor(y1); i++) {
            double yc = i + 0.5, x;
            if (yc < y0)
                yc = y0;
            if (yc > y1)
                yc = y1;
            x = x0 + (yc - y0) * dx / dy;
            a = (long)floor(x - h / 2 + 0.5);
            b = (long)floor(x + h / 2 + 0.5);
            fill(a, i, b > a ? b : a + 1, i + 1, col);
        }
    }
}

void port_map_line(fix x0, fix y0, fix x1, fix y1)
{
    line(x0 / 65536.0, y0 / 65536.0, x1 / 65536.0, y1 / 65536.0,
         map_colour(grd_canvas->gc.fcolor));
}

/* gr_iline: between two pixels */
void port_map_iline(long x0, long y0, long x1, long y1)
{
    line(x0 + 0.5, y0 + 0.5, x1 + 0.5, y1 + 0.5, map_colour(grd_canvas->gc.fcolor));
}

/* gr_wire_poly: the closed outline through n points (grs_vertex: fix x, y first) */
void port_map_poly(long c, long n, void **vp)
{
    long i;
    uchar col = map_colour(c);
    for (i = 0; i < n; i++) {
        const fix *p = vp[i], *q = vp[(i + 1) % n];
        line(p[0] / 65536.0, p[1] / 65536.0, q[0] / 65536.0, q[1] / 65536.0, col);
    }
}

/* the game's circle of radius r around pixel (x, y), as the game's pixels show it: kx x ky its
 * size (an ellipse in 320x400, as there) */
void port_map_circle(long x, long y, long r)
{
    double rx = r, ry = (double)r * port_map_ky / port_map_kx, t = thick();
    double rxo = rx + t / 2, ryo = ry + t / 2, rxi = rx - t / 2, ryi = ry - t / 2;
    uchar col = map_colour(grd_canvas->gc.fcolor);
    long j;
    if (r <= 0) {
        port_map_point(x, y);
        return;
    }
    for (j = (long)floor(-ryo); j <= (long)ceil(ryo); j++) {
        double dy = j, xo, xi;
        long a, b;
        if (fabs(dy) > ryo)
            continue;
        xo = rxo * sqrt(1 - (dy / ryo) * (dy / ryo));
        xi = ryi > 0 && fabs(dy) < ryi ? rxi * sqrt(1 - (dy / ryi) * (dy / ryi)) : -1;
        if (xi < 0.5) {
            a = (long)ceil(x - xo);
            b = (long)floor(x + xo) + 1;
            fill(a, y + j, b, y + j + 1, col);
            continue;
        }
        a = (long)ceil(x - xo);
        b = (long)floor(x - xi) + 1;
        fill(a, y + j, b > a ? b : a + 1, y + j + 1, col);
        a = (long)ceil(x + xi);
        b = (long)floor(x + xo) + 1;
        fill(b > a ? a : b - 1, y + j, b, y + j + 1, col);
    }
}

/* text and bitmaps: drawn as the game draws them on a canvas of their own, then each pixel as
 * a kx x ky block from (x, y) of the picture */
#define TW 640
#define TH 96
static uchar tbuf[TW * TH];

static void blocks(long x, long y, int w, int h)
{
    grs_canvas *cv = grd_canvas;
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            uchar v = tbuf[j * TW + i];
            if (v)
                fill(x + i * port_map_kx, y + j * port_map_ky, x + (i + 1) * port_map_kx,
                     y + (j + 1) * port_map_ky,
                     cv->gc.fill_type == 1 ? ((uchar *)cv->gc.fill_parm)[v] : v);
        }
}

static void own_canvas(grs_canvas *tc)
{
    memset(tbuf, 0, sizeof tbuf);
    gr_init_canvas(tc, tbuf, BMT_FLAT8, TW, TH);
    tc->gc.fcolor = grd_canvas->gc.fcolor;
    gr_push_canvas(tc);
}

void port_map_text(long font, char *s, long x, long y)
{
    grs_canvas tc;
    own_canvas(&tc);
    GfxDrawText((int)font, s, 0, 0);
    gr_pop_canvas();
    blocks(x, y, TW, TH);
}

void port_map_bitmap(grs_bitmap *bm, long x, long y)
{
    grs_canvas tc;
    if (bm->w > TW || bm->h > TH)
        return;
    own_canvas(&tc);
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, 0L, 0L);
    gr_pop_canvas();
    blocks(x, y, bm->w, bm->h);
}

/* a blast's translucent disc (rndsky_2A6AF8: r its radius in fix pixels) at the picture's size:
 * as wide as the game's (up to 64 of its pixels), as tall as it shows (rows doubled on a
 * canvas over 200 rows high, as rndsky_2A6564 does) */
void port_sky_disc(long x, long y, long w, long h, long color);

void port_map_disc(long x, long y, fix r)
{
    long w = (r + 0x8000) / 0x8000, h;
    if (w < port_map_kx)
        w = port_map_kx;
    if (w > 0x40 * port_map_kx)
        w = 0x40 * port_map_kx;
    h = w * port_map_ky / port_map_kx * (port_map_low_h > 200 ? 2 : 1);
    port_sky_disc(x, y, w, h, 0xF7);
}
