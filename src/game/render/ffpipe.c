/* ffpipe.c -- Terra Nova module M077 (0x29E120-0x29EEF0; r:\prj\tn\v109\tn\src\tn\ffpipe.c):
 * the render pipeline: camera set-up, view matrix, sun light, and the frame (sky, terrain
 * strips, haze, objects, weather, lightning) drawn into the camera's canvas.  Decompiled from
 * __FF.EXE v1.09; map names kept (FfCameraSetup is the community name).
 *
 *   0x29E120 ffpipe_29E120  probe the pixel at (x, y) of the camera's canvas
 *   0x29E160 ffpipe_29E160  probe off
 *   0x29E180 ffpipe_29E180  choose the weather routine (no callers; not in the map)
 *   0x29E19C ffpipe_29E19C  lightning visibility (no callers)
 *   0x29E1BC ffpipe_29E1BC  distance view - lightning
 *   0x29E1F4 ffpipe_29E1F4  column buffer = canvas height (no callers)
 *   0x29E234 ffpipe_29E234  column buffer from an array
 *   0x29E26C ffpipe_29E26C  renderer init          0x29E2B8 ffpipe_29E2B8 renderer shutdown
 *   0x29E2D8 ffpipe_29E2D8  draw the scene (between g3_start_frame and g3_end_frame)
 *   0x29E4F4..0x29E598      3D light modes (no light, sun, ambient + diffuse variants)
 *   0x29E5B8 ffpipe_29E5B8  sun as the light source
 *   0x29E5C8 ffpipe_29E5C8  light from a point towards another (no callers)
 *   0x29E5FC ffpipe_29E5FC  sun light from the map (levels and direction)
 *   0x29E6F8 ffpipe_29E6F8  3D light table
 *   0x29E700 ffpipe_29E700  draw a frame: tables, 3D view, scene, strip messages
 *   0x29E830 FfCameraSetup  camera for a w x h canvas at a zoom
 *   0x29EB04 ffpipe_29EB04  make a view/camera current (no callers)
 *   0x29EB54 ffpipe_29EB54  copy +0xA5 of the current camera (no callers)
 *   0x29EB64 ffpipe_29EB64  view matrix from the angles
 *   0x29EDB4 ffpipe_29EDB4  render a view (clamps the pitch)
 *   0x29EE80 ffpipe_29EE80  resource 0x5A0 of resmap.res into ff_res_tab
 *
 * Quirks kept:
 *  - FfCameraSetup halves grd_cap->aspect around the set-up for "half width" cameras and
 *    doubles it back (so it is not restored exactly for odd values).  When the camera has
 *    DivTables it compares itself with cams[count - 1], which reads before the list when the
 *    count is 0.
 *  - ffpipe_29EB64 stores m[1], m[4], m[7] before scaling them, so mz[] uses the unscaled
 *    values (the port build: from the scaled ones, G22).
 *  - The probe: before the scene's final passes the pixel is set to 0xFF; afterwards it is
 *    put back only when it is still 0xFF (whatever drew there stays).
 */
#include "g5_b.h"
#include "g6_b.h"
#include "g1_b.h"
#include "g1_c.h"
#include "g2_b.h"
#include "g3_c.h"
#include "g5_a.h"
#include "g5_c.h"
#include "3d_a.h"
#include "3d_b.h"
#include "res.h"
#include "g6_a.h"     /* added at the G5-G8 merge */
#include "g6_b.h"     /* added at the G5-G8 merge */

#define HVLIST ((HviewList *)game_438C20)

void ffpipe_29E120(FfCamera *cam, long x, long y)
{
    grs_canvas *cv;
    cam->probe = 1;
    cv = cam->canvas;
    cam->probe_x = (short)x;
    cam->probe_y = (short)y;
    cam->probe_hit = 0;
    cam->probe_ptr = cv->bm.bits + y * (long)cv->bm.row + x;
#ifndef TN_WATCOM_ABI
    /* the HUD probes the pixel under the mouse every frame, also when the mouse is outside the
     * view: the original then marked (and restored) a byte outside the canvas, wherever it was
     * in memory.  Here a spare byte stands in; nothing is drawn there, so nothing is hit, as in
     * the original (docs/BUGS.md G10) */
    if (x < 0 || y < 0 || x >= cv->bm.w || y >= cv->bm.h) {
        static uchar outside;
        cam->probe_ptr = &outside;
    }
#endif
}

void ffpipe_29E160(FfCamera *cam)
{
    cam->probe = 0;
    cam->probe_hit = 0;
}

void ffpipe_29E180(long mode)
{
#ifdef TN_WATCOM_ABI
    if (mode == 1)
        ffpipe_35C0E4 = 0x2B2340;
    else
        ffpipe_35C0E4 = 0x2B2418;
#else
    /* standalone build: our functions for weather_2B2340 / weather_2B2418 */
    if (mode == 1)
        ffpipe_35C0E4 = (ulong)weather_2B2340;
    else
        ffpipe_35C0E4 = (ulong)weather_2B2418;
#endif
}

void ffpipe_29E19C(void)
{
    ff_bolt_vis = viewdist_2A213C(ff_bolt_pos[0], ff_bolt_pos[1], ff_bolt_pos[2]);
}

void ffpipe_29E1BC(void)
{
    ff_bolt_dist = fix_safe_pyth_dist(ff_bolt_pos[0] - ff_view.x, ff_bolt_pos[1] - ff_view.y,
                                      "r:\\prj\\tn\\v109\\tn\\src\\tn\\ffpipe.c", 0x1C8);
}

void ffpipe_29E1F4(void)
{
    grs_canvas *cv = grd_canvas;
    long i;
    for (i = 0; i < cv->bm.w; i++)
        ff_col_ybuf[i] = cv->bm.h;
    ff_ybuf_set = 1;
    grd_canvas = cv;              /* written back unchanged */
}

void ffpipe_29E234(long *src)
{
    grs_canvas *cv = grd_canvas;
    long i;
    for (i = 0; i < cv->bm.w; i++)
        ff_col_ybuf[i] = *src++;
    ff_ybuf_set = 0;
    grd_canvas = cv;
}

void ffpipe_29E26C(void)
{
    ffpipe_43CC11 = 1;
    g3_star_alloc(0x100);
    g3_star_random(0x1D, 8);
    memstack_2ADA2C();
    ralloc_2ADEB0();
    hviewdiv_2AB100();
    rndsky_2A8344();
    ffpipe_29EE80();
    viewdist_2A0D70();
    g3_set_detail_level(2);
}

void ffpipe_29E2B8(void)
{
    rndsky_2A844C();
    hviewdiv_2AB100();
    ralloc_2ADEEC();
    memstack_2ADB80();
    hviewdiv_2AB480();
    g3_star_free();
}

static int ffpipe_sky_on(void)
{
    return ffpipe_35C100 && !FF_CAM_NOSKY(&ff_cam) && !game_41DE34;
}

void ffpipe_29E2D8(FfCamera *cam, FfView *view)
{
    uchar strips[0x40];
    terrrast_2ADF00();
    hviewdiv_2AB650();
    terrstrip_2AB0D4();
    viewdist_2A0EC8();
#ifndef TN_WATCOM_ABI
    if (port_s3d_on)
        port_water_frame();             /* "Water reflection: High": the objects' reflections (rndsky.c) */
#endif
    weather_2B1694();
    TerrainStripBuild(cam, view, strips);
    ffpipe_mesh = terrmesh_29EEF0(view, strips);
    weather_2B1814();
    if (ffpipe_mesh != -1) {
        weather_2B16B0(ffpipe_mesh, view->x, view->y);
        terrmesh_29F2B4();
    }
    if (ff_bolt) {
        ff_bolt_vis = viewdist_2A213C(ff_bolt_pos[0], ff_bolt_pos[1], ff_bolt_pos[2]);
        if (ff_bolt_vis) {
            long i;
            ffpipe_29E1BC();
            for (i = 0; i < grd_canvas->bm.w; i++)
                ff_col_ybuf[i] = grd_canvas->bm.h;
            ff_ybuf_set = 1;
        }
    } else
        ff_bolt_vis = 0;
    if (ffpipe_sky_on())
        rndsky_2A84A4();
    hviewdiv_2AB6C4(cam, view, strips);
    rndsky_2A7A6C();
    if (ff_bolt_vis)
        thunder_2B3898(ff_bolt, game_35CADC < 0xF, game_time - ff_bolt_time, ff_col_ybuf);
    if (game_35CADA != 0xFF)
        weather_2AFA5C(ffpipe_41EB60, 3);
#ifndef TN_WATCOM_ABI
    if (port_s3d_on && port_water_full && ffpipe_sky_on())
        port_water_sky_keep();          /* "Water reflection: Medium" (rndsky.c) */
#endif
    if (ff_cam.probe) {
        uchar *p = ff_cam.probe_ptr;
        ff_cam.probe_hit = 0;
        ff_cam.probe_save = *p;
        *p = 0xFF;
    }
    hviewdiv_2AB750();
    if (ffpipe_sky_on()) {
        if (ff_cam.probe && *ff_cam.probe_ptr == 0xFF) {
            *ff_cam.probe_ptr = ff_cam.probe_save;
            rndsky_2A8658();
            ff_cam.probe_save = *ff_cam.probe_ptr;
            *ff_cam.probe_ptr = 0xFF;
        } else
            rndsky_2A8658();
    }
    if (ffpipe_mesh != -1)
        weather_2B1F24();
    else
        weather_2B1DCC();
    if (ff_cam.probe && *ff_cam.probe_ptr == 0xFF)
        *ff_cam.probe_ptr = ff_cam.probe_save;
#ifndef TN_WATCOM_ABI
    if (port_s3d_on && port_water_full && ffpipe_sky_on())
        port_water_full_pass();         /* "Water reflection: Medium / High" (rndsky.c) */
    if (port_fog_on && ffpipe_sky_on())
        port_fog_water();               /* "Fog": the water too (rndsky.c) */
    if (port_s3d_on && port_water_full && port_water_fx && ffpipe_sky_on())
        port_water_after();             /* "Water effects": shallows, splashes (rndsky.c) */
#endif
    if (ffpipe_38FFF0)
        drawobj_29BA3C();
    weather_2B16AC();
    viewdist_2A0F48();
    terrstrip_2AB0E8();
    hviewdiv_2AB6B8();
    terrrast_2ADF34();
}

void ffpipe_29E4F4(void)
{
    _g3d_light_type = 0;
}

void ffpipe_29E500(void)
{
    _g3d_light_type = 6;
    _g3d_amb_light = ff_amb;
    _g3d_diff_light = ff_diff;
    _g3d_spec_light = ff_spec;
}

void ffpipe_29E528(void)
{
    _g3d_light_type = 2;
    _g3d_amb_light = ff_amb;
    _g3d_diff_light = ff_diff;
}

void ffpipe_29E544(void)
{
    _g3d_light_type = 2;
    _g3d_amb_light = ff_amb;
    _g3d_diff_light = ff_diff >> 1;
}

void ffpipe_29E564(void)
{
    _g3d_light_type = 2;
    _g3d_amb_light = (ff_amb + 0x10000) / 2;
    _g3d_diff_light = ff_diff;
}

void ffpipe_29E598(void)
{
    _g3d_light_type = 2;
    _g3d_amb_light = 0x10000;
    _g3d_diff_light = 0;
}

void ffpipe_29E5B8(void)
{
    g3_set_light_src(&ff_light);
}

void ffpipe_29E5C8(fix x0, fix y0, fix z0, fix x1, fix y1, fix z1)
{
    g3s_vector v;
    v.x = x1 - x0;
    v.y = y1 - y0;
    v.z = z1 - z0;
    g3_vec_normalize(&v);
    g3_set_light_src(&v);
}

void ffpipe_29E5FC(void)
{
    fix s1, c1, s0, c0;
    uchar *sun = *(uchar **)(game_map_35BFB4 + 0x3A);
    ff_amb = *(fix *)(sun + 8);
    ff_diff = *(fix *)(sun + 4);
    if (ff_amb + ff_diff > 0x10000)
        ff_amb = 0x10000 - ff_diff;
    ff_spec = ff_diff;
    fix_sincos(*(ushort *)(sun + 2), &s1, &c1);
    fix_sincos(*(ushort *)(sun + 0), &s0, &c0);
    ff_light.x = fix_mul(s0, c1);
    ff_light.y = -s1;
    ff_light.z = fix_mul(c0, c1);
    _g3d_light_type = 6;
    _g3d_amb_light = ff_amb;
    _g3d_diff_light = ff_diff;
    _g3d_spec_light = ff_spec;
    _g3d_flash = 0;
    g3_set_light_src(&ff_light);
}

void ffpipe_29E6F8(uchar *tab)
{
    g3d_light_tab = tab;
}

void ffpipe_29E700(FfCamera *cam, FfView *view)
{
    struct { uchar what; FfView *view; } __attribute__((packed)) msg;
    g3s_vector pos;
    g3s_angvec ang;
    ffpipe_29E5FC();
    if (HVLIST->dirty) {
        HVLIST->dirty = 0;
        hviewdiv_2AB480();
    }
    if (!cam->hview_ok)
        hviewdiv_2AB4E8(cam);
    msg.what = 0;
    msg.view = view;
    strip_2ABB10(&msg, NULL, NULL);
    if (cam->display)
        display_2AD19C();
    g3_start_frame();
    pos.x = ff_view.y;
    pos.y = -ff_view.z;
    pos.z = ff_view.x;
    ang.tx = -ff_view.pitch;
    ang.ty = ff_view.heading;
    ang.tz = 0;
    g3_set_view_angles(&pos, &ang, 1, fix_div(0x10000, fix_mul(ff_cam.zoom_y, 0x11999)));
    grd_screen->ltab = planet_light;
    g3d_light_tab = planet_light;
    ffpipe_29E2D8(cam, view);
    g3_end_frame();
    msg.what = 1;
    strip_2ABB10(&msg, NULL, NULL);
    if (cam->display)
        display_2AD1DC();
}

void FfCameraSetup(void *pcam, long w, long h, long zoom)
{
    FfCamera *cam = pcam;
    fix s, s0;
    long i;
    ushort k;
    s = 0x20000 - fix_exp(0x10000 - zoom);
    if (cam->zoom == 0)
        s0 = 0x10000;
    else
        s0 = 0x20000 - fix_exp(0x10000 - cam->zoom);
    if ((cam->flags & 1) && !(cam->flags & 2))
        grd_cap->aspect >>= 1;
    w >>= cam->flags & 1;
    h >>= (cam->flags >> 1) & 1;
    cam->half_w = w >> 1;
    cam->half_h = h >> 1;
    cam->zoom = zoom;
    cam->fov_x = fix_atan2(0x10000, zoom);
    for (k = 0; (long)k < cam->band_end; k++) {
        FfBand *b = &cam->band[k];
        b->a = (ushort)(((long)b->d * s + 0x8000) >> 16);
        b->b = (ushort)(((long)b->e * s + 0x8000) >> 16);
    }
    for (i = 0; i < 7; i++) {
        long q = fix_div((long)cam->dist[i] << 16, s0);
        q = (q + 0x8000) >> 16;
        cam->dist[i] = (short)((q * s + 0x8000) >> 16);
    }
    cam->zoom_y = fix_div(zoom * w, h * grd_cap->aspect);
    cam->fov_y = fix_atan2(0x10000, cam->zoom_y);
    cam->zx = cam->half_w * cam->zoom;
    cam->zy = cam->half_h * cam->zoom_y;
    cam->a5 = 0x50;
    if (cam->hview_ok) {
        if (cam == HVLIST->cams[(long)(ushort)HVLIST->count - 1]) {
            hviewdiv_2AB44C(cam);
            HVLIST->count--;
            HVLIST->cur = HVLIST->cams2[(ushort)HVLIST->count];
        } else
            HVLIST->dirty = 1;
    }
    if ((cam->flags & 1) && !(cam->flags & 2))
        grd_cap->aspect += grd_cap->aspect;
}

void ffpipe_29EB04(FfView *view, FfCamera *cam)
{
    if (view != &ff_view)
        memcpy(&ff_view, view, sizeof(FfView));
    if (cam != &ff_cam)
        memcpy(&ff_cam, cam, sizeof(FfCamera));
}

void ffpipe_29EB54(FfCamera *cam)
{
    cam->a5 = ff_cam.a5;
}

void ffpipe_29EB64(FfView *view, FfCamera *cam)
{
    fix sx = fix_mul(cam->zoom, 0x11999);
    fix sy = fix_mul(cam->zoom_y, 0x11999);
    fix sh, ch, sp, cp, sb, cb;
    fix_sincos(view->heading, &sh, &ch);
    fix_sincos(view->pitch, &sp, &cp);
    fix_sincos(view->bank, &sb, &cb);
    view->m[0] = fix_mul(sx, fix_mul(ch, cb) + fix_mul(sb, fix_mul(sp, sh)));
    view->m[1] = -fix_mul(sx, fix_mul(sb, cp));
    view->m[2] = fix_mul(sx, fix_mul(sb, fix_mul(sp, ch)) - fix_mul(cb, sh));
    view->m[3] = fix_mul(sy, fix_mul(ch, sb) - fix_mul(cb, fix_mul(sp, sh)));
    view->m[4] = fix_mul(sy, fix_mul(cb, cp));
    view->m[5] = fix_mul(sy, -fix_mul(sb, sh) - fix_mul(cb, fix_mul(sp, ch)));
    view->m[6] = fix_mul(sh, cp);
    view->m[7] = sp;
    view->m[8] = fix_mul(cp, ch);
    view->cos_h = ch;
    view->sin_h = sh;
    view->cos_p = cp;
    view->sin_p = sp;
    view->cos_b = cb;
    view->sin_b = sb;
    view->mz[0] = fix_mul(view->z, view->m[1]);
    view->mz[1] = fix_mul(view->z, view->m[4]);
    view->mz[2] = fix_mul(view->z, view->m[7]);
    view->m[1] = -fix_mul(view->m[1], 0xAAA);
    view->m[4] = -fix_mul(view->m[4], 0xAAA);
    view->m[7] = -fix_mul(view->m[7], 0xAAA);
#ifndef TN_WATCOM_ABI
    /* G22: the land's and the sprites' heights go through the scaled m[1], m[4], m[7] (1/24 of
     * the column, rounded down to whole units), the eye's height through the unscaled ones: the
     * eye stood off the ground under it by up to 1/2000 of its height (0.03 squares at 58 squares
     * down), by another amount at every change of the pitch, and the near ground shook as the
     * view followed a slope.  The eye's terms from the scaled column, as the heights' */
    view->mz[0] = fix_mul(view->z, view->m[1] * -24);
    view->mz[1] = fix_mul(view->z, view->m[4] * -24);
    view->mz[2] = fix_mul(view->z, view->m[7] * -24);
#endif
}

void ffpipe_29EDB4(FfView *view, FfCamera *cam)
{
    if (view->pitch < 0x8000) {
        if ((long)view->pitch + cam->fov_y > 0x3C71)
            view->pitch = 0x3C71 - cam->fov_y;
    } else {
        if ((long)view->pitch - cam->fov_y < 0xC38E)
            view->pitch = 0xC38E + cam->fov_y;
    }
    ffpipe_29EB64(view, cam);
    gr_set_canvas(cam->canvas);
    if (view != &ff_view)
        memcpy(&ff_view, view, sizeof(FfView));
    if (cam != &ff_cam)
        memcpy(&ff_cam, cam, sizeof(FfCamera));
    ffpipe_29E700(cam, view);
    cam->a5 = ff_cam.a5;
}

void ffpipe_29EE80(void)
{
    int f;
    uchar *p;
    long n, i;
    long *e;
    memset(ff_res_tab, 0, sizeof(ff_res_tab));
    f = ResOpenResFile("resmap.res", 0, 0);
    p = ResLock(0x5A0);
    n = (ulong)(*(long *)(p + 6) - *(long *)(p + 2)) >> 3;
    e = (long *)(p + *(long *)(p + 2));
    for (i = 0; i < n; i++, e += 2)
        ff_res_tab[e[0]] = e[1];
    ResUnlock(0x5A0);
    ResCloseFile(f);
}
