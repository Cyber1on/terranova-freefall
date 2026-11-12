/* hviewdiv.c -- Terra Nova module M084 (0x2AB100-0x2AB800; source file name unknown, the
 * error strings call it "hview"): per-camera tables for the terrain strip renderer -- row
 * offsets, the two DivTables (half width / height divided by 1 + i/256) and a log2 table --,
 * a list of the cameras that have them (game_438C20), the per-column buffer of a frame and
 * the memory trash check.  Decompiled from __FF.EXE v1.09; map names kept.
 *
 *   0x2AB100 hviewdiv_2AB100 (empty)                0x2AB104 hviewdiv_2AB104 count band steps
 *   0x2AB174 hviewdiv_2AB174 row offset table       0x2AB1CC hviewdiv_2AB1CC sum of rows (unused)
 *   0x2AB200 hviewdiv_2AB200 rows not increasing?   0x2AB244 hviewdiv_2AB244 DivTable sum (unused)
 *   0x2AB274 hviewdiv_2AB274 DivTables increasing?  0x2AB2B0 hviewdiv_2AB2B0 build DivTables
 *   0x2AB3CC hviewdiv_2AB3CC 1 - exp(-(a-20)/b)     0x2AB3F4 hviewdiv_2AB3F4 build all tables
 *   0x2AB448 hviewdiv_2AB448 (empty)                0x2AB44C hviewdiv_2AB44C free a camera's tables
 *   0x2AB480 hviewdiv_2AB480 free every camera      0x2AB4E8 hviewdiv_2AB4E8 add a camera
 *   0x2AB508 hviewdiv_2AB508 project x (unused)     0x2AB5AC hviewdiv_2AB5AC project y (unused)
 *   0x2AB650 hviewdiv_2AB650 column buffer          0x2AB6B8 hviewdiv_2AB6B8 free it
 *   0x2AB6C4 hviewdiv_2AB6C4 strips + haze          0x2AB750 hviewdiv_2AB750 flush the strip list
 *   0x2AB768 StripCheckForMemoryTrash
 *
 * Quirks kept:
 *  - hviewdiv_2AB3F4 falls through into 0x2AB448 (its own ret).
 *  - hviewdiv_2AB104 compares the running total as 16 bits but keeps it in 32; a band step of
 *    0 never ends (as in the original).
 *  - StripCheckForMemoryTrash: for the first camera the row sum loop is empty (compiled out),
 *    only the DivTable sum is checked; for the others a row table that is not increasing skips
 *    the DivTable check.
 */
#include "g5_b.h"
#include "g1_b.h"
#include "g5_a.h"     /* added at the G5-G8 merge */
#include "g5_c.h"     /* added at the G5-G8 merge */

#define HVLIST ((HviewList *)game_438C20)

void hviewdiv_2AB100(void)
{
}

short hviewdiv_2AB104(FfCamera *cam)
{
    long cnt = 0;
    ulong d;
    ushort i;
    ushort a = cam->band[cam->band0].a;
    d = a < 10 ? a : 10;
    for (i = cam->band0; (long)i < cam->band_end; i++) {
        FfBand *b = &cam->band[i];
        while ((ushort)d < b->b) {
            d += b->step;
            cnt++;
        }
    }
    return (short)cnt;
}

void hviewdiv_2AB174(grs_canvas *cv, long **rows)
{
    short h = cv->bm.h;
    ushort row = cv->bm.row;
    long *p = ffmem_2C4F40((long)h * 4);
    long sum = 0, off = 0;
    short i;
    *rows = p;
    for (i = 0; i < h; i++) {
        sum += off;
        *p++ = off;
        off += row;
    }
    hview_rows_sum = sum;
}

long hviewdiv_2AB1CC(FfCamera *cam)
{
    long sum = 0, i;
    for (i = 0; i < cam->canvas->bm.h; i++)
        sum += cam->rows[i];
    return sum;
}

uchar hviewdiv_2AB200(FfCamera *cam)
{
    long i;
    for (i = 0; i < cam->canvas->bm.h - 1; i++) {
        long *p = &cam->rows[i];
        if (p[0] < 0)
            return 1;
        if (!((ulong)p[0] < (ulong)p[1]))
            return 1;
    }
    return 0;
}

long hviewdiv_2AB244(FfCamera *cam)
{
    long sum = 0, i;
    for (i = 0; i < 0x100; i++)
        sum += cam->div_x[i] + cam->div_y[i];
    return sum;
}

uchar hviewdiv_2AB274(FfCamera *cam)
{
    long i;
    for (i = 0; i < 0xFF; i++) {
        if (cam->div_x[i] < cam->div_x[i + 1])
            return 1;
        if (cam->div_y[i] < cam->div_y[i + 1])
            return 1;
    }
    return 0;
}

void hviewdiv_2AB2B0(FfCamera *cam)
{
    long *dx, *dy, i, k, n, sum;
    uchar *lg;
    fix a, b, d;
    dx = ffmem_2C4F40(0x400);
    cam->div_x = dx;
    dy = ffmem_2C4F40(0x400);
    cam->div_y = dy;
    lg = ffmem_2C4F40(0x100);
    cam->log2 = lg;
    a = cam->half_w << 16;
    b = cam->half_h << 16;
    for (i = 0, d = 0x10000; i < 0x100; i++, d += 0x100) {
        dx[i] = fix_div(a, d);
        dy[i] = fix_div(b, d);
    }
    sum = 0;
    for (i = 0; i < 0x100; i++)
        sum += cam->div_x[i] + cam->div_y[i];
    hview_div_sum = sum;
    *lg++ = 0;
    for (k = 0; k < 8; k++)
        for (n = 1 << k; n > 0; n--)
            *lg++ = (uchar)k;
}

fix hviewdiv_2AB3CC(long a, long b)
{
    fix r = 0x10000 - fix_exp(-((a - 0x14) << 16) / b);
    if (r < 0)
        r = 0;
    return r;
}

void hviewdiv_2AB3F4(FfCamera *cam)
{
    hviewdiv_2AB174(cam->canvas, &cam->rows);
    hviewdiv_2AB2B0(cam);
    if (!hview_first)
        hview_first = cam;
    if (cam == hview_first) {
        hview_first_div_sum = hview_div_sum;
        hview_first_rows_sum = hview_rows_sum;
    }
    cam->hview_ok = 1;
    cam->nbands = hviewdiv_2AB104(cam);
}

void hviewdiv_2AB448(void)
{
}

void hviewdiv_2AB44C(FfCamera *cam)
{
    ffmem_2C4FE4(cam->log2);
    ffmem_2C4FE4(cam->div_y);
    ffmem_2C4FE4(cam->div_x);
    ffmem_2C4FE4(cam->rows);
    cam->hview_ok = 0;
    cam->rows = 0;
}

void hviewdiv_2AB480(void)
{
    short k;
    for (k = HVLIST->count - 1; k >= 0; k--) {
        FfCamera *cam = HVLIST->cams[k];
        ffmem_2C4FE4(cam->log2);
        ffmem_2C4FE4(cam->div_y);
        ffmem_2C4FE4(cam->div_x);
        ffmem_2C4FE4(cam->rows);
        cam->hview_ok = 0;
        cam->rows = 0;
    }
    HVLIST->cur = 0;
    HVLIST->count = 0;
}

void hviewdiv_2AB4E8(FfCamera *cam)
{
    HVLIST->cams[(ushort)HVLIST->count] = cam;
    HVLIST->count++;
    hviewdiv_2AB3F4(cam);
}

long hviewdiv_2AB508(fix x, fix y, fix z, FfCamera *cam, FfView *view)
{
    fix dy = y - view->y, dx = x - view->x;
    fix num = fix_mul(dy, view->m[0]) + fix_mul(z, view->m[1]) + fix_mul(dx, view->m[2]) + ff_view.mz[0];
    fix den = fix_mul(dy, view->m[6]) + fix_mul(z, view->m[7]) + fix_mul(dx, view->m[8]) + ff_view.mz[2];
    return ((fix_div(num, den) + 0x10000) * cam->half_w + 0x8000) >> 16;
}

long hviewdiv_2AB5AC(fix x, fix y, fix z, FfCamera *cam, FfView *view)
{
    fix dy = y - view->y, dx = x - view->x;
    fix num = fix_mul(dy, view->m[3]) + fix_mul(z, view->m[4]) + fix_mul(dx, view->m[5]) + ff_view.mz[1];
    fix den = fix_mul(dy, view->m[6]) + fix_mul(z, view->m[7]) + fix_mul(dx, view->m[8]) + ff_view.mz[2];
    return ((fix_div(num, den) + 0x10000) * cam->half_h + 0x8000) >> 16;
}

void hviewdiv_2AB650(void)
{
    long w = grd_canvas->bm.w;
    long *p = ffmem_2C4F40(w << 2);
    long h, i;
    w = grd_canvas->bm.w;
    h = grd_canvas->bm.h;
    hview_cols = p;
    for (i = 0; i < w; i++)
        p[i] = h;
}

void hviewdiv_2AB6B8(void)
{
    ffmem_2C4FE4(hview_cols);
}

void hviewdiv_2AB6C4(FfCamera *cam, FfView *view, void *strips)
{
    long *s = (long *)(strip_427DE0 - 0x20);
    long w, i, any = 0;
    ((uchar *)s)[0x1E] = 0;
    s[0] = 0;
    w = grd_canvas->bm.w;
    hview_strip = s;
    for (i = 0; i < w; i++)
        if ((any = hview_cols[i]) != 0)
            break;
    if (any) {
        strip_2AC4E4(cam);
        haze_2B5484(cam, view, strips);
    }
}

void hviewdiv_2AB750(void)
{
    if (*hview_strip)
        viewdist_2A250C(hview_strip, 3);
}

void StripCheckForMemoryTrash(FfCamera *cam)
{
    long i, sum;
    if (cam != hview_first) {
        if (hviewdiv_2AB200(cam))
            return;
        if (hviewdiv_2AB274(cam))
            DbgReportError(1, "hview DivTable trashed in StripCheckForMemoryTrash\n");
        return;
    }
    sum = 0;
    for (i = 0; i < 0x100; i++)
        sum += cam->div_x[i] + cam->div_y[i];
    if (sum != hview_first_div_sum)
        DbgReportError(1, "DivTable trashed in StripCheckForMemoryTrash\n");
    strip_2AC124();
}
