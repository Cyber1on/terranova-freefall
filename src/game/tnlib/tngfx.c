/* tngfx.c -- TN graphics glue: mode setting with a full-screen back canvas, save/restore of
 * the mode, text and image drawing, translucent rectangles, back canvas blits (Terra Nova
 * v1.09).  Names from the error strings (GfxInit, GfxSetModeAndStuff, GfxSaveAndSetMode,
 * GfxRestoreMode), the rest ours.  Decompiled from __FF.EXE.
 *
 *   0x2E3660 GfxInit2           0x2E3DF0 gfx_check_config   0x2E3E78 gfx_init_once
 *   0x2E3EB0 GfxInit            0x2E3EE0 GfxSetModeAndStuff 0x2E3F90 GfxSetMode
 *   0x2E40C0 GfxSaveAndSetMode  0x2E4144 GfxRestoreMode     0x2E41EC GfxShutdown
 *   0x2E421C gfx_2E421C       0x2E4238 gfx_2E4238       0x2E4304 GfxDrawText
 *   0x2E4324 GfxRectVerts       0x2E4370 GfxTlucRect        0x2E43D0 GfxBlit
 *   0x2E444C GfxBlitHFlip       0x2E44C8 GfxBlitMask
 *
 * The game draws into tnui_back_canvas (a flat8 canvas of the screen size, allocated by
 * GfxSetMode) and copies rectangles of it to the screen with GfxBlit, through gfx_blit_bm,
 * a bitmap header whose bits point into the back canvas.
 */
#include "t2_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
grs_bitmap gfx_blit_bm = { 0, BMT_FLAT8 };
grs_screen *gfx_screen;
uchar gfx_alt_lowres = 1, gfx_alt_medres = 1, gfx_linear_ok = 1, gfx_inited;
grs_canvas gfx_saved_canvas;
grs_bitmap gfx_saved_blit_bm;
LGRect gfx_saved_rect;
void *gfx_canvas_buf;
int gfx_saved;
short gfx_saved_mode, gfx_mode;
uchar gfx_mode_set;
#endif

/* Same as GfxInit without cursor_lock_a (and its own copy of the message). */
void GfxInit2(int mode)
{
    if (gr_init())
        DbgReportError(1, "GfxInit: gr_tnova_init() failed!\n");
    gfx_init_once();
    GfxSetMode((short)mode);
}

/* Config switches against broken video cards. */
void gfx_check_config(void)
{
    if (vesa_version >= 0x102 && CfgLookup("badpaldac", 0, 0))
        vesa_reset_dac();
#ifdef TN_WATCOM_ABI
    if (!(grd_mode_info[GRM_320x200x8V].flags & 1) || CfgLookup("badlowres", 0, 0))
        gfx_alt_lowres = 0;
    if (!(grd_mode_info[GRM_320x400x8V].flags & 1) || CfgLookup("badmedres", 0, 0))
        gfx_alt_medres = 0;
#else
    /* badlowres / badmedres asked for the VGA's planar modes instead of the card's; Freefall's
     * display has the card's modes only, so they are ignored (docs/BUGS.md P13) */
    if (!(grd_mode_info[GRM_320x200x8V].flags & 1))
        gfx_alt_lowres = 0;
    if (!(grd_mode_info[GRM_320x400x8V].flags & 1))
        gfx_alt_medres = 0;
#endif
    if (CfgLookup("badlinmodes", 0, 0))
        gfx_linear_ok = 0;
}

void gfx_init_once(void)
{
    if (!gfx_inited) {
        gr_set_malloc(WATCOM_FN(ResMalloc));
        gr_set_free(WATCOM_FN(free));
        gfx_check_config();
        AtExit(WATCOM_FN(GfxShutdown));
        gfx_inited = 1;
    }
}

void GfxInit(int mode)
{
    if (gr_init())
        DbgReportError(1, "GfxInit: gr_tnova_init() failed!\n");
    cursor_lock_a();
    gfx_init_once();
    GfxSetMode((short)mode);
}

/* Modes 0 and 2 go to their VESA equivalents unless the config says the card can't. */
void GfxSetModeAndStuff(short mode)
{
    int flags;
    if (mode == GRM_320x200x8 && gfx_alt_lowres)
        mode = GRM_320x200x8V;
    if (mode == GRM_320x400x8 && gfx_alt_medres)
        mode = GRM_320x400x8V;
    flags = ((grd_mode_info[mode].flags & 2) && gfx_linear_ok) ? 3 : 1;
    gfx_mode = mode;
    if (gr_set_mode(mode, flags))
        DbgReportError(1, "GfxSetModeAndStuff: gr_set_mode() failed!\n");
#ifndef TN_WATCOM_ABI
    /* G23: the mouse cursor's save-unders keep the canvas the pixels under it go back to, and
     * MouseSaveUnderAt (the HUD's) fills them again without changing it: after a change of
     * mode the next move of the cursor drew through the old screen's freed canvas (into the
     * freed picture, unless the allocator gave the new screen the same blocks).  They move to
     * the new screen's canvas; the old screen's pixels are gone with it: nothing is put back */
    int su_at = -1, su2_at = -1;
    if (gfx_screen) {
        if (tnm_su.canvas >= gfx_screen->c && tnm_su.canvas <= gfx_screen->c + 1)
            su_at = (int)(tnm_su.canvas - gfx_screen->c);
        if (tnm_su2.canvas >= gfx_screen->c && tnm_su2.canvas <= gfx_screen->c + 1)
            su2_at = (int)(tnm_su2.canvas - gfx_screen->c);
    }
#endif
    if (gfx_screen)
        gr_free_screen(gfx_screen);
    gfx_screen = gr_alloc_screen(grd_cap->w, grd_cap->h);
    gr_set_screen(gfx_screen);
#ifndef TN_WATCOM_ABI
    if (su_at >= 0) {
        tnm_su.canvas = gfx_screen->c + su_at;
        tnm_su.drawn = 0;
    }
    if (su2_at >= 0) {
        tnm_su2.canvas = gfx_screen->c + su2_at;
        tnm_su2.drawn = 0;
    }
#endif
}

/* Sets the mode and makes the back canvas the size of the screen (reused when the size
 * did not change; gfx_canvas_buf, when set, is used instead of a new allocation). */
void GfxSetMode(short mode)
{
    int w, h;
    GfxSetModeAndStuff(mode);
    gfx_mode_set = 1;
    tnui_full_rect.lr.x = grd_screen_canvas->bm.w;
    tnui_full_rect.lr.y = grd_screen_canvas->bm.h;
    w = tnui_full_rect.lr.x;
    h = tnui_full_rect.lr.y;
    if (tnui_back_canvas.bm.bits && tnui_back_canvas.bm.w == w && tnui_back_canvas.bm.h == h)
        return;
    if (tnui_back_canvas.bm.bits)
        Free(tnui_back_canvas.bm.bits);
    if (gfx_canvas_buf)
        tnui_back_canvas.bm.bits = gfx_canvas_buf;
    else
        tnui_back_canvas.bm.bits = Malloc(w * h);
    memset(tnui_back_canvas.bm.bits, 0, w * h);
    tnui_back_canvas.bm.type = BMT_FLAT8;
    tnui_back_canvas.bm.w = w;
    tnui_back_canvas.bm.flags = 0;
    tnui_back_canvas.bm.h = h;
    tnui_back_canvas.bm.row = w;
    gfx_blit_bm.row = w;
    tnui_back_canvas.bm.align = 0;
    tnui_back_canvas.gc = grd_defgc;
    tnui_back_canvas.gc.clip.f.right = (long)(short)w << 16;
    tnui_back_canvas.gc.clip.f.bot = (long)(short)h << 16;
}

void GfxSaveAndSetMode(short mode, void *buf)
{
    if (gfx_saved)
        DbgReportError(1, "Tried to nest GfxSaveAndSetMode.\n");
    gfx_saved = 1;
    gfx_saved_rect = tnui_full_rect;
    gfx_saved_canvas = tnui_back_canvas;
    gfx_canvas_buf = buf;
    gfx_saved_blit_bm = gfx_blit_bm;
    gfx_saved_mode = gfx_mode;
    tnui_back_canvas.bm.bits = 0;
    GfxSetMode(mode);
    gfx_canvas_buf = 0;
}

/* Frees the temporary back canvas unless it is 'keep' (f_free(NULL) when there is none). */
void GfxRestoreMode(void *keep)
{
    if (gfx_saved != 1)
        DbgReportError(1, "Tried to GfxRestoreMode without GfxSaveAndSetMode first.\n");
    gfx_saved = 0;
    GfxSetModeAndStuff(gfx_saved_mode);
    if (keep != tnui_back_canvas.bm.bits)
        Free(tnui_back_canvas.bm.bits);
    tnui_back_canvas = gfx_saved_canvas;
    tnui_full_rect = gfx_saved_rect;
    gfx_blit_bm = gfx_saved_blit_bm;
    tnui_back_canvas.gc = grd_defgc;
    tnui_back_canvas.gc.clip.f.right = (long)tnui_back_canvas.bm.w << 16;
    tnui_back_canvas.gc.clip.f.bot = (long)tnui_back_canvas.bm.h << 16;
}

void GfxShutdown(void)
{
#ifndef TN_WATCOM_ABI
    /* quitting while a screen that saved the mode is up (the options screen): the back canvas
     * is that screen's buffer from the game's memory stack, not a heap block (docs/BUGS.md G13) */
    if (gfx_saved)
        ;
    else
#endif
    if (tnui_back_canvas.bm.bits)
        Free(tnui_back_canvas.bm.bits);
    if (gfx_mode_set) {
        gr_close();
        gfx_mode_set = 0;
        cursor_unlock_a();
    }
}

/* Waits for the start of the next vertical retrace. */
void gfx_2E421C(void)
{
    while (((long (WATCOMCALL *)(void))grd_device_table[GRT_STAT_VTRACE])())
        ;
    while (!((long (WATCOMCALL *)(void))grd_device_table[GRT_STAT_VTRACE])())
        ;
}

/* Tiles a bitmap (a RES frame: header + bits at +0x1C) over the clip rectangle of the
 * current canvas, the grid aligned on (x, y). */
void gfx_2E4238(Ref ref, int x, int y)
{
    grs_bitmap *bm = RefGet(ref);
    short x0;
    bm->bits = (uchar *)bm + 0x1C;
    while (y + bm->h < grd_canvas->gc.clip.i.top)
        y += bm->h;
    while (x + bm->w < grd_canvas->gc.clip.i.left)
        x += bm->w;
    while (y < grd_canvas->gc.clip.i.bot) {
        x0 = x;
        while (x < grd_canvas->gc.clip.i.right) {
            WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, (short)x, (short)y);
            x += bm->w;
        }
        y += bm->h;
        x = x0;
    }
}

/* Draws a string with a font resource (canvas table entry 315 = font string). */
void GfxDrawText(int font, char *s, int x, int y)
{
    void (**tab)() = grd_canvas_table;
    void *f = ResGet((Id)font);
    WATCOM_CALL(tab[315], (long)f, (long)s, (short)x, (short)y);
}

/* The 4 corners (x, y only) of a rectangle as a vertex list. */
void GfxRectVerts(grs_vertex *v, grs_vertex **vp, long x0, long y0, long x1, long y1)
{
    v[0].x = x0 << 16;
    v[0].y = y0 << 16;
    vp[0] = &v[0];
    v[1].x = x1 << 16;
    v[1].y = y0 << 16;
    vp[1] = &v[1];
    v[2].x = x1 << 16;
    v[2].y = y1 << 16;
    vp[2] = &v[2];
    v[3].x = x0 << 16;
    v[3].y = y1 << 16;
    vp[3] = &v[3];
}

/* Fills a rectangle with colour 255 made translucent through 'tab'. */
void GfxTlucRect(uchar *tab, long x0, long y0, long x1, long y1)
{
    grs_vertex v[4];
    grs_vertex *vp[4];
    GfxRectVerts(v, vp, x0, y0, x1, y1);
    tluc8tab_p[255] = tab;
    WATCOM_CALL(grd_canvas_table[31 /* poly */], 0xFF, 4, (long)vp);
    tluc8tab_p[255] = 0;
}

/* Points gfx_blit_bm at rectangle r of the back canvas and selects the screen canvas. */
static void gfx_blit_setup(LGRect *r)
{
    gfx_blit_bm.w = r->lr.x - r->ul.x;
    gfx_blit_bm.h = r->lr.y - r->ul.y;
    gfx_blit_bm.bits = tnui_back_canvas.bm.bits + tnui_back_canvas.bm.row * (long)r->ul.y + r->ul.x;
    gr_set_canvas(grd_screen_canvas);
}

/* Copies rectangle r of the back canvas to the same place on the screen. */
#ifndef TN_WATCOM_ABI
void port_s3d_blit(const void *rect);   /* port/video/sharp3d.c */
#endif

void GfxBlit(LGRect *r)
{
    gfx_blit_setup(r);
    WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * gfx_blit_bm.type], (long)&gfx_blit_bm, r->ul.x, r->ul.y);
#ifndef TN_WATCOM_ABI
    port_s3d_blit(r);                   /* the sharp 3D view goes with the screen copy */
#endif
}

void GfxBlitHFlip(LGRect *r)
{
    gfx_blit_setup(r);
    WATCOM_CALL(grd_canvas_table[218 /* hflip ubitmap */ + 2 * gfx_blit_bm.type], (long)&gfx_blit_bm, r->ul.x, r->ul.y);
}

/* Canvas entry 195 (+2*type) at (0, 0); the original passes its caller's edx through. */
void GfxBlitMask(LGRect *r, long x)
{
    gfx_blit_setup(r);
    WATCOM_CALL(grd_canvas_table[195 + 2 * gfx_blit_bm.type], (long)&gfx_blit_bm, x, 0, 0);
}
