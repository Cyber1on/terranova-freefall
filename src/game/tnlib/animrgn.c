/* animrgn.c -- animations played in a tnui region of their own (names ours, no assert path;
 * "tnui extras" in the map).  Decompiled from __FF.EXE.
 *
 *   0x2FD330 animrgn_back_on        0x2FD338 animrgn_back_off
 *   0x2FD344 tnui2_2FD344 (start)   0x2FD4AC animrgn_callback      0x2FD64C animrgn_draw
 *   0x2FD710 animrgn_rect           0x2FD7C4 animrgn_start_rect    0x2FD88C animrgn_scale
 *
 * tnui2_2FD344(ref, pos) opens a region over the animation's first frame (hot spot at pos.pt)
 * with priority pos.z and starts the delta animation player (anim_2E68A0, T2b) on it with
 * animrgn_callback.  With animrgn_use_back the region is a type 1 region drawing straight
 * into the back buffer (frames drawn in place), else a type 3 region with its own bitmap
 * that animrgn_draw copies.  When the header's base_z is set, sizes and hot spots scale by
 * base_z / z (16.16).  The callback moves/resizes the region (flags 0x20 / 0x80 / 0x42),
 * updates the frame rectangle (flag 1) and destroys the region at the end (flag 8).
 *
 * Quirks kept:
 *  - animrgn_rect and animrgn_callback scale with the region's priority as z.
 *  - With callback flag 0x80 and not 0x20 the move uses dx, dy and the priority change left
 *    over from earlier (uninitialised stack / ecx in the original); ours pass 0.
 */
#include "t2_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar animrgn_use_back;
#endif

void animrgn_back_on(void)
{
    animrgn_use_back = 1;
}

void animrgn_back_off(void)
{
    animrgn_use_back = 0;
}

long animrgn_scale(short z, short base_z, LGPoint pt, LGPoint *out)
{
    long s;

    if (z <= 0) {
        out->x = 0;
        out->y = 0;
        return 0;
    }
    s = ((long)base_z << 16) / z;
    out->x = (pt.x * s) >> 16;
    out->y = (pt.y * s) >> 16;
    return s;
}

void animrgn_start_rect(Ref ref, AnimPos *pos, LGRect *out)
{
    AnimHdr *h = RefLock(ref);
    LGPoint sz;
    long s;

    if (h->base_z) {
        s = animrgn_scale(pos->z, h->base_z, *(LGPoint *)&h->w, &sz);
        out->ul.x = pos->pt.x - ((h->hotx * s) >> 16);
        out->ul.y = pos->pt.y - ((h->hoty * s) >> 16);
        out->lr.x = out->ul.x + sz.x;
        out->lr.y = out->ul.y + sz.y;
    } else {
        out->ul.x = pos->pt.x - h->hotx;
        out->ul.y = pos->pt.y - h->hoty;
        out->lr.x = out->ul.x + h->w;
        out->lr.y = out->ul.y + h->h;
    }
    ResUnlock(ref >> 16);
}

void animrgn_rect(TnRegion *r, short base_z, LGPoint size, LGPoint hot, LGRect *out)
{
    LGPoint sz;
    long s;

    if (base_z) {
        s = animrgn_scale(r->prio, base_z, size, &sz);
        out->ul.x = r->rect.ul.x - ((hot.x * s) >> 16);
        out->ul.y = r->rect.ul.y - ((hot.y * s) >> 16);
        out->lr.x = out->ul.x + sz.x;
        out->lr.y = out->ul.y + sz.y;
    } else {
        out->ul.x = r->rect.ul.x - hot.x;
        out->ul.y = r->rect.ul.y - hot.y;
        out->lr.x = out->ul.x + size.x;
        out->lr.y = out->ul.y + size.y;
    }
}

void *tnui2_2FD344(Ref ref, AnimPos *pos)
{
    AnimHdr hdr;
    TnRegion *r;
    grs_canvas *c;

    animrgn_start_rect(ref, pos, &animrgn_params.rect);
    animrgn_params.prio = pos->z;
    hdr = *(AnimHdr *)RefGet(ref);
    animrgn_params.flags = (hdr.flags & 1) != 0;
    animrgn_params.type = animrgn_use_back ? TNR_BACK : TNR_DRAWN;
    r = viewport_2E55D8(&animrgn_params);
    ANIMRGN_OWN(r) = animrgn_use_back == 0;
    if (hdr.flags & 4)
        c = NULL;
    else {
        c = ANIMRGN_CNV(r);
        if (ANIMRGN_OWN(r)) {
            c->bm.row = hdr.w;
            c->bm.w = hdr.w;
            c->bm.h = hdr.h;
            c->bm.type = BMT_FLAT8;
            c->bm.align = 0;
            c->bm.bits = (*f_malloc)(c->bm.h * (ulong)c->bm.row);
        } else
            gr_init_sub_bitmap(&tnui_back_canvas.bm, &c->bm, pos->pt.x, pos->pt.y, hdr.w, hdr.h);
        c->bm.flags = (hdr.flags & 1) != 0;
        c->gc = grd_defgc;
        c->gc.clip.f.right = c->bm.w << 16;
        c->gc.clip.f.bot = c->bm.h << 16;
    }
    r->user = anim_2E68A0(ref, r, c, WATCOM_FN(animrgn_callback));
    viewport_2E57F0(r);
    return r->user;
}

void animrgn_callback(void *anim, uchar flags, AnimFrameInfo *fi)
{
    TnRegion *r = ANIM_REGION(anim);
    AnimHdr *h;
    LGPoint size, sz;
    LGRect t;
    long dx = 0, dy = 0, dprio = 0, s;

    if (flags & 8) {
        if (ANIMRGN_OWN(r) && ANIM_BM(anim))
            (*f_free)(ANIM_BM(anim)->bits);
        viewport_2E575C(r);
        return;
    }
    if (flags & 0xE2) {
        if (flags & 0xA0) {
            size = fi->size.x ? fi->size : *(LGPoint *)&ANIM_HDR(anim)->w;
            if (flags & 0x20) {
                h = ANIM_HDR(anim);
                animrgn_rect(r, h->base_z, size, *(LGPoint *)&h->hotx, &t);
                dx = t.ul.x - r->rect.ul.x + fi->x;
                dy = t.ul.y - r->rect.ul.y + fi->y;
                dprio = fi->dprio;
                size.x = t.lr.x - t.ul.x;
                size.y = t.lr.y - t.ul.y;
            }
            viewport_2E5954(r, dx, dy, dprio, size.x, size.y);
        } else
            viewport_2E5954(r, fi->x + fi->x2, fi->y + fi->y2, fi->dprio, 0, 0);
    }
    if (flags & 1) {
        h = ANIM_HDR(anim);
        if (h->base_z) {
            s = animrgn_scale(r->prio, h->base_z, *(LGPoint *)&h->w, &sz);
            t.ul.x = (fi->upd.ul.x * s) >> 16;
            t.ul.y = (fi->upd.ul.y * s) >> 16;
            t.lr.x = (fi->upd.lr.x * s) >> 16;
            t.lr.y = (s * fi->upd.lr.y) >> 16;
            viewport_2E5BC8(r, &t);
        } else
            viewport_2E5BC8(r, &fi->upd);
    }
}

void animrgn_draw(TnRegion *r)
{
    void *anim = r->user;
    AnimHdr *h = ANIM_HDR(anim);
    grs_bitmap *bm;

    if (h->flags & 4) {
        bm = RefGet(ANIM_FRAME(anim));
        bm->bits = (uchar *)bm + 0x1C;
    } else
        bm = &ANIMRGN_CNV(r)->bm;
    if (h->base_z)
        WATCOM_CALL(grd_canvas_table[SCALE_DEVICE_BITMAP + 2 * bm->type], (long)bm, 0, 0,
                    r->cnv.bm.w, r->cnv.bm.h);
    else
        WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_BITMAP + 2 * bm->type], (long)bm, 0, 0);
}
