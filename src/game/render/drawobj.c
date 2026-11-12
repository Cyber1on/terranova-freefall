/* drawobj.c -- Terra Nova module M076 (0x29AC50-0x29E11F; r:\prj\tn\v109\tn\src\tn\drawobj.c):
 * drawing the map objects (entities) in the 3D view.  Decompiled from __FF.EXE v1.09.
 *
 *   0x29AC50 drawobj_29AC50 unlock the model tables   0x29ACA8 drawobj_29ACA8 draw setup (DrawInfo)
 *   0x29AED0 drawobj_29AED0 (no refs) model object?   0x29AF44 ff_image_model_width
 *   0x29AF98 drawobj_29AF98 draw an entity            0x29B170 drawobj_29B170 model offset
 *   0x29B234 drawobj_29B234 draw a pose skeleton      0x29B2C0 drawobj_29B2C0 PBA hand position
 *   0x29B3AC drawobj_29B3AC start the entity object   0x29B43C drawobj_29B43C (no refs) end object
 *   0x29B448 drawobj_29B448 lock / unlock a texture   0x29B488 drawobj_29B488 a model's textures
 *   0x29B4D0 drawobj_29B4D0 highlight colour          0x29B54C drawobj_29B54C draw a 3D model
 *   0x29B694 drawobj_29B694 debris textures           0x29B730 drawobj_29B730 textured plate
 *   0x29BA3C drawobj_29BA3C nav marker                0x29BC60 drawobj_29BC60 grow a rect by a point
 *   0x29BCF0 drawobj_29BCF0 model screen rect         0x29BE28 drawobj_29BE28 model bounding box
 *   0x29BE78 drawobj_29BE78 draw a sprite             0x29C114 drawobj_29C114 limb vector
 *   0x29C1DC drawobj_29C1DC bend a limb               0x29C348 drawobj_29C348 (no refs) vec add
 *   0x29C36C drawobj_29C36C half angle around 0xC000  0x29C39C drawobj_29C39C half angle
 *   0x29C3AC drawobj_29C3AC joints -> skeleton        0x29C448 drawobj_29C448 one bone -> skeleton
 *   0x29C508 drawobj_29C508 pose a PBA                0x29C864 drawobj_29C864 draw a PBA
 *   0x29CA48 drawobj_29CA48 draw a ragdoll            0x29CBA4 drawobj_29CBA4 entity screen point
 *   0x29CCE0 drawobj_29CCE0 entity screen rect        0x29CF2C drawobj_29CF2C (no refs) rect grow
 *   0x29CF68 drawobj_29CF68 (no refs) rect scale      0x29CFC4 drawobj_29CFC4 3D line
 *   0x29D238 drawobj_29D238 clip intersection         0x29D284 drawobj_29D284 (no refs) outcode
 *   0x29D2B0 drawobj_29D2B0 wide 3D line              0x29DA60 drawobj_29DA60 world -> view/screen
 *   0x29DC20 drawobj_29DC20 vehicle pitch             0x29DD64 drawobj_29DD64 model pitch
 *   0x29DE8C drawobj_29DE8C beam (class 6 type 0x1C)  0x29DF38 drawobj_29DF38 particles (0x1D)
 *   0x29E0DC drawobj_29E0DC (no refs) bolt
 *
 * The DrawInfo filled by drawobj_29ACA8 (callers keep it as 12 bytes on their stack):
 *   +0 Ref of the model or bitmap, +4 sprite width (fix), +8 byte: 1 = draw it as a resource.
 *
 * Quirks kept:
 *  - drawobj_29ACA8 returns 0 for a class 0 entity without a model, before setting DrawInfo.draw;
 *    drawobj_29B694 ignores the return value and reads draw (and drawobj_29BE28 the Ref of a
 *    class 6 effect) uninitialised.  Our C reads its own uninitialised local there.
 *  - drawobj_29B730 takes its plate size from the entity's address ((e >> 4) and (e >> 7)).
 *  - drawobj_29BA3C sets fill type 1 (clut) afterwards whatever fill type it found.
 *  - drawobj_29BA3C: the heading of the marker is the word at 0x41E228 before its high byte is
 *    incremented (it spins by 256 per call).
 *  - drawobj_29B54C's "light afterwards" flag (ffpipe_29E5B8) is never set.
 *  - Divisions written as (a << 16) / b or a * b / c with a 64-bit dividend fault (#DE) on
 *    overflow in the original; the C divides in 64 bits and truncates.
 *  - drawobj_29DA60 stores the view coordinates only when the first pointer is not NULL, and
 *    the screen ones only when the first screen pointer is not NULL.
 *  - drawobj_29CCE0 passes the same pointer for the view x and y of drawobj_29DA60.
 */
#include "g6_a.h"
#include "g5_a.h"
#include "g5_b.h"
#include "g6_b.h"
#include "g6_c.h"
#include "g3_a.h"
#include "g3_b.h"
#include "g2_c.h"
#include "g2_b.h"
#include "g1_b.h"
#include "g4_a.h"
#include "res.h"
#include "support.h"
#include "3d_a.h"
#include "3d_b.h"
#include "3d_c.h"

#define DRAWOBJ_FILE "r:\\prj\\tn\\v109\\tn\\src\\tn\\drawobj.c"
#define FIX_POLY 26                      /* canvas table: fix-point polygon (c, n, vertex list) */

#define D32(p, o) (*(long *)((uchar *)(p) + (o)))
#define U16(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define S16(p, o) (*(short *)((uchar *)(p) + (o)))
#define U8(p, o) (*((uchar *)(p) + (o)))
#define ENT_INDEX(e) ((short)(((uchar *)(e) - (uchar *)game_entities) / (long)sizeof(GameEntity)))
#define PBA(slot) ((uchar *)&game_pba[(short)(slot)])
#define CLS6(slot) (game_cls6[(short)(slot)])
#define RES_TYPE(ref) (gResDesc2[(ulong)(ref) >> 16].type)
#ifndef TN_WATCOM_ABI
/* a sprite's width in the world; "Tree size" in the sharp view (port/video/sharp3d.c) */
#define IMAGE_WIDTH(e, ref) port_tree_scale(ref, (e)->x, (e)->y, ff_image_model_width(e))
#else
#define IMAGE_WIDTH(e, ref) ff_image_model_width(e)
#endif

/* (a << 16) / b and a * b / c with 64-bit dividends, as the asm's idiv */
static fix fdiv(fix a, fix b) { return (fix)(((long long)a * 65536) / b); }
static long muldiv(long a, long b, long c) { return (long)(((long long)a * b) / c); }
static long half(long a) { return a / 2; }               /* sar with rounding toward 0 */
static long iabs(long a) { return a < 0 ? -a : a; }

static void fill_type_0(void)
{
    grd_canvas->gc.fill_type = 0;
    grd_function_table = grd_function_fill_table;
    grd_uline_fill_vector = grd_uline_fill_table;
}

static void fill_type_1(void)
{
    grd_canvas->gc.fill_type = 1;
    grd_function_table = grd_function_fill_table + 390;
    grd_uline_fill_vector = grd_uline_fill_table + 9;
}

static long light_parm(g3s_vector *norm, g3s_vector *pos)
{
    fix l = g3_light((g3s_point *)norm, (g3s_point *)pos);
    return ((l >> (g3d_light_shift & 31)) & ~0xFF) + (long)g3d_light_tab;
}

/* ------------------------------------------------------------------ model tables, DrawInfo */

void drawobj_29AC50(void)
{
    if (drawobj_cls0_models != NULL) {
        ResUnlock(0x55B);
        ResUnlock(0x55C);
        ResUnlock(0x55D);
        ResUnlock(0x55E);
        drawobj_cls0_models = NULL;
    }
}

uchar drawobj_29ACA8(GameEntity *e, uchar *buf)
{
    DrawInfo *d = (DrawInfo *)buf;
    long ref;
    uchar *r;

    if (drawobj_cls0_models == NULL) {
        drawobj_cls0_models = RefLock(0x55B0000);
        drawobj_cls2_models = RefLock(0x55C0000);
        drawobj_cls3_models = RefLock(0x55D0000);
        game_41E218 = RefLock(0x55E0000);
    }
#ifndef TN_WATCOM_ABI
    if (e == &port_burn_ent) {          /* "Tree explosion": the plant's picture (port/mods/burn.c) */
        if (!port_burn_model(&d->ref, &d->scale))
            return 0;
        d->draw = 1;
        return 1;
    }
#endif
    switch (e->cls) {
    case 0:
#ifndef TN_WATCOM_ABI
        if (port_s3d_on && port_burn_covers(e))     /* "Tree explosion": broke, behind its burst */
            return 0;
#endif
        ref = D32(drawobj_cls0_models + e->type * 8, game_cls0[e->slot][6] * 4);
        d->ref = ref;
        if (ref == 0)
            return 0;
        if (RES_TYPE(ref) == 2) {
            d->scale = IMAGE_WIDTH(e, ref);
            d->draw = 1;
            return 1;
        }
        break;
    case 1:
        d->ref = 1;
        d->draw = 1;
        return 1;
    case 2:
        d->ref = D32(drawobj_cls2_models, e->type * 4);
        d->draw = 1;
        return 1;
    case 3:
        d->ref = D32(drawobj_cls3_models, e->type * 4);
        d->scale = 0x2AAA;
        if (RES_TYPE(d->ref) == 2) {
            d->scale = IMAGE_WIDTH(e, d->ref);
            d->draw = 1;
            return 1;
        }
        break;
    case 4:
        ref = D32(game_41E218 + e->type * 8, game_cls4[e->slot][0x78] * 4);
        d->ref = ref;
        if (RES_TYPE(ref) == 2) {
            d->scale = IMAGE_WIDTH(e, ref);
            d->draw = 1;
            return 1;
        }
        break;
    case 5:
        if (e->type == 0xA) {
            d->ref = 0x53F0001;
            d->draw = 1;
            return 1;
        }
        d->ref = 0x48C0000;
        d->scale = 0x1555;
        d->draw = 1;
        return 1;
    case 6:
        if (e->type == 0x1C || e->type == 0x1D || (e->type >= 0x1F && e->type <= 0x21)) {
            d->draw = 0;
            return 1;
        }
        r = *(uchar **)(CLS6(e->slot) + 6);
        if (r == NULL || !(r[0x76] & 0x10))
            return 0;
        d->ref = D32(r, 0x2F);
        d->scale = ff_image_model_width(e);
        break;
    }
    d->draw = 1;
    return 1;
}

/* (no callers) whether the entity is drawn as a 3D model (not a PBA) */
uchar drawobj_29AED0(long ent)
{
    DrawInfo d;
    ulong t;
    if (!drawobj_29ACA8(ENT(ent), (uchar *)&d) || !d.draw)
        return 0;
    t = RES_TYPE(d.ref);
    if (t != 0 && t != 0xF)
        return 0;
    return ENT(ent)->cls != 1;
}

fix ff_image_model_width(GameEntity *e)
{
    ShapeItem *s = interact_2973E8(e->cls, e->type);
    if ((ushort)s->type == 2 || (ushort)s->type == 3)
        return D32(s, (long)s->npts * 8 + 4) * 2;
    DbgReportError(1, "ff_image_model_width: wrong shape image, class %d subclass %d\n", e->cls, e->type);
    return 0;
}

/* ------------------------------------------------------------------ drawing an entity */

#ifndef TN_WATCOM_ABI
/* "Tree explosion" (port/mods/burn.c): the plant drawn through the colour tables drawobj_29BE78 would
 * use, its own (the light of its place; the infrared's, not hot) and an effect's (full light; the
 * infrared's hot one), and the blast's a little darker */
static void burn_draw(GameEntity *e, fix scale, long shade)
{
    long lit, glow, dim;
    if (grd_canvas->gc.fill_type == 1)
        lit = glow = dim = grd_canvas->gc.fill_parm;
    else {
        lit = ((15 - shade) << 8) + (long)grd_screen->ltab;
        glow = (15 << 8) + (long)grd_screen->ltab;
        dim = (13 << 8) + (long)grd_screen->ltab;
    }
    if (lit == (long)planet_tab3b) {
        lit = (long)port_ir_obj_clut(e, planet_tab3b + 0x100);
        glow = dim = (long)port_ir_obj_clut(NULL, planet_tab3b + 0x300);
    }
    port_burn_draw(scale, (const uchar *)lit, (const uchar *)glow, (const uchar *)dim);
}
#endif

void drawobj_29AF98(GameEntity *e, uchar *buf)
{
    DrawInfo *d = (DrawInfo *)buf;
    uchar *p, *r;

    if (e->cls == 6 && (CLS6(e->slot)[0x2A] & 0x40))
        drawobj_flat = 1;
    else
        drawobj_flat = 0;
    if (wx_in_center && wx_grid_mode)
        drawobj_flat = 1;
    if (d->draw) {
        ulong t = RES_TYPE(d->ref);
        if (t == 2) {
            long shade;
            p = RefLock(d->ref);
            D32(p, 0) = (long)(p + 0x1C);
            shade = drawobj_flat ? 0 : map_294B18(e->x, e->y);
#ifndef TN_WATCOM_ABI
            {   /* "Effects pack" and "Objects pack", in the sharp view: the pack's picture for the
                 * frame (port/mods/effects.c), a death's at its size (none for a burning plant's);
                 * "Tree explosion" (port/mods/burn.c) */
                uchar hdr[0x1C];
                fix sc = d->scale;
                int r = 0;
                if (port_s3d_on && e == &port_burn_ent) {
                    burn_draw(e, d->scale, shade);
                    r = -1;
                } else if (port_s3d_on)
                    r = e->cls == 6 ? port_fx_frame(*(void **)(CLS6(e->slot) + 6), e->slot, d->ref, p, hdr, &sc) :
                                      port_obj_frame(d->ref, p, hdr);
                if (r) {
                    if (r > 0)
                        drawobj_29BE78(e, hdr, sc, shade);
                    ResUnlock((Id)((ulong)d->ref >> 16));
                    return;
                }
            }
#endif
            drawobj_29BE78(e, p, d->scale, shade);
            ResUnlock((Id)((ulong)d->ref >> 16));
        } else if (t == 0 || t == 0xF) {
            if (e->cls == 1) {
                if (viewdist_2A20C4(&e->x))
                    drawobj_29C864(e);
                return;
            }
            p = RefLock(d->ref);
            drawobj_29B54C(e, p);
            ResUnlock((Id)((ulong)d->ref >> 16));
        }
        return;
    }
    if (e->cls != 6)
        return;
    switch (e->type) {
    case 0x1C:
        drawobj_29DE8C(e);
        break;
    case 0x1D:
        drawobj_29DF38(e);
        break;
    case 0x1F:
        r = CLS6(e->slot);
        if (D32(r, 0xA) != 0)
            bolt_draw_simple(D32(r, 0xA), D32(r, 0xE), D32(r, 0xE) << 8);
        break;
    case 0x20:
        drawobj_29B730(e, D32(CLS6(e->slot), 0x26));
        break;
    case 0x21:
        drawobj_29CA48(e);
        break;
    }
}

/* the offset of a 3D model's sub-object point (vector 0 of op 0x2C) for the entity type ent:
 * (z, x, -y) of the model, 0 = all zero */
uchar drawobj_29B170(long ent, uchar *buf, fix *a, fix *b, fix *c)
{
    ulong ref = *(ulong *)buf;
    uchar *obj = RefLock(ref);
    long parms = objtype_293AA0((short)ent);
    uchar ok = 0;

    if (parms != 0) {
        fix x, y, z;
        g3d_interp_vecs[0].z = 0;
        g3d_interp_vecs[0].y = 0;
        g3d_interp_vecs[0].x = 0;
        g3_interpret_sub_object(obj, (void *)parms);
        z = g3d_interp_vecs[0].z;
        x = g3d_interp_vecs[0].x;
        y = -g3d_interp_vecs[0].y;
        *a = z;
        *b = x;
        *c = y;
        ok = (*a != 0 || *b != 0 || *c != 0);
    }
    ResUnlock((Id)(*(ulong *)buf >> 16));
    return ok;
}

/* builds the skeleton of the PBA entity e in pose 3 (no drawing) */
void drawobj_29B234(GameEntity *e)
{
    uchar *pba = PBA(e->slot);
    uchar *sk = pba + 0xEB;
    uchar *buf;
    skel_2F6A64((Skel *)sk);
    buf = ffmem_2C4F40((sk[2] + sk[3]) * 12);
    D32(sk, 0xC) = (long)buf;
    D32(sk, 0x10) = (long)(buf + sk[2] * 12);
    drawobj_29C448(pba + 0xDE, sk, 3);
    skel_2F7B1C((Skel *)sk);
    skel_2F6AD0((Skel *)sk);
    ffmem_2C4FE4((void *)D32(sk, 0xC));
}

void drawobj_29B2C0(long slot, long kind, fix *x, fix *y, fix *z)
{
    uchar *pba = PBA(slot);
    uchar *bp = pba + 0xDE;
    long t = D32(bp, 5), i;
    if (t == 0)
        t = 1;
    drawobj_29C508((short)slot, t);
    if (D32(bp, 0) == 0xB) {
        drawobj_29B234(ENT((short)(U16(pba, 0) << 1) >> 1));
        i = kind != 2;
        *x = g3d_interp_vecs[i].z;
        *y = g3d_interp_vecs[i].x;
        *z = -g3d_interp_vecs[i].y;
    } else {
        i = (kind != 2) + 0xE;
        *x = D32(bp, i * 12 + 0xD1);
        *y = D32(bp, i * 12 + 0xD5);
        *z = D32(bp, i * 12 + 0xD9);
    }
}

/* starts the 3D object of the entity: position (y, -z, x) (+ body offset), heading, pitch
 * (vehicles and installation type 0x2F: from the terrain under the model's bounding box bb) */
void drawobj_29B3AC(GameEntity *e, uchar *bb)
{
    g3s_vector v;
    ushort pitch = e->f1d;
    v.x = e->y;
    v.y = -e->z;
    v.z = e->x;
    if (e->cls == 2 || (e->cls == 4 && e->type == 0x2F))
        pitch = drawobj_29DD64(e, bb);
    if (e->body)
        v.y += physics_2D45CC(ENT_INDEX(e));
    g3_start_object_angles_xyz(&v, pitch, e->heading, 0, 1);
}

void drawobj_29B43C(void)
{
    g3_end_object();
}

void drawobj_29B448(long idx, long res, uchar lock)
{
    if (lock) {
        uchar *p = RefLock(ff_res_tab[res]);
        D32(p, 0) = (long)(p + 0x1C);
        tmap_tab[idx] = (grs_bitmap *)p;
    } else
        ResUnlock((Id)((ulong)ff_res_tab[res] >> 16));
}

/* locks (lock = 1) or unlocks the textures of a 3D model */
void drawobj_29B488(uchar *obj, uchar lock)
{
    uchar *t = obj + 0x5E + obj[0x47] * 16;
    long n = obj[0x49], i;
    for (i = 0; i < n; i++, t += 10)
        if (t[0] == 0)
            drawobj_29B448(t[1], D32(t, 2), lock);
}

/* sets the model highlight colour (vcolor_tab[2]) of entity types 0x38 / 0x39; 1 = set */
uchar drawobj_29B4D0(GameEntity *e)
{
    if (e->type == 0x38) {
        vcolor_tab[2] = 0x4B;
        return 1;
    }
    if (e->type != 0x39)
        return 0;
    if (aimain_24A760() || objmap_find(ENT_INDEX(e))) {
        vcolor_tab[2] = 0x4B;
        return 1;
    }
    vcolor_tab[2] = 0;
    return 0;
}

void drawobj_29B54C(GameEntity *e, uchar *obj)
{
    long parms = vd_draw_arg ? objtype_293AA0(vd_draw_arg) : 0;

    drawobj_29B488(obj, 1);
    if (grd_canvas->gc.fill_type != 0) {
        long saved;
        ffpipe_29E4F4();
        drawobj_29B3AC(e, obj);
        saved = grd_canvas->gc.fill_parm;
        if (saved == (long)planet_tab3b) {
            if (e->cls == 3 && e->type == 0xA && drawobj_35B1A0 == 0)
                grd_canvas->gc.fill_parm = (long)planet_tab3c;
            else
                grd_canvas->gc.fill_parm = (long)planet_tab3b + (((drawobj_flat != 0) + 2) << 8);
#ifndef TN_WATCOM_ABI
            if (port_s3d_on)            /* the sharp view: the fog (infrared.c) */
                grd_canvas->gc.fill_parm = (long)port_ir_obj_clut(e, (const uchar *)grd_canvas->gc.fill_parm);
#endif
        } else
            saved = 0;
        g3_interpret_object(obj, (void *)parms, 0);
        if (saved)
            grd_canvas->gc.fill_parm = saved;
    } else {
        fix shade = map_294B18(e->x, e->y);
        if (drawobj_flat || drawobj_29B4D0(e))
            ffpipe_29E598();
        else if (shade == map_shade_max * 2)
            ffpipe_29E544();
        else
            ffpipe_29E500();
        drawobj_29B3AC(e, obj);
        g3_interpret_object(obj, (void *)parms, 0);
    }
    g3_end_object();
    drawobj_29B488(obj, 0);
}

/* collects the model's textures of at least 16 x 16 into sfx_debris_img (at most 32) */
long drawobj_29B694(GameEntity *e)
{
    DrawInfo d;
    uchar *obj, *t;
    long n, i, k = 0, cnt = 0;

    drawobj_29ACA8(e, (uchar *)&d);
    if (!d.draw)
        return 0;
    obj = RefLock(d.ref);
    t = obj + 0x5E + obj[0x47] * 16;
    n = obj[0x49];
    for (i = 0; i < n; i++, t += 10) {
        if (t[0] == 0) {
            grs_bitmap *bm = RefGet(ff_res_tab[D32(t, 2)]);
            if (bm->w >= 16 && bm->h >= 16) {
                sfx_debris_img[k++] = D32(t, 2);
                cnt++;
                if (k == 32)
                    break;
            }
        }
    }
    ResUnlock((Id)((ulong)d.ref >> 16));
    return cnt;
}

static void plate_uv(g3s_phandle *p, long a, long b)
{
    p[0]->u = 0;
    p[0]->v = 0;
    p[1]->u = (sfix)(a >> 6);
    p[1]->v = 0;
    p[2]->u = (sfix)(a >> 6);
    p[2]->v = (sfix)(b >> 6);
    p[3]->u = 0;
    p[3]->v = (sfix)(b >> 6);
}

/* a flat square (both faces), textured with ff_res_tab[tex] or colour 0x25 */
void drawobj_29B730(GameEntity *e, long tex)
{
    grs_bitmap *bm = NULL;
    g3s_phandle p[4], q;
    g3s_vector norm;
    long lit, a, b;

    if (tex >= 0) {
        bm = RefGet(ff_res_tab[tex]);
        bm->bits = (uchar *)bm + 0x1C;
    }
    lit = grd_canvas->gc.fill_type == 0;
    ffpipe_29E500();
    drawobj_29B3AC(e, NULL);
    a = ((((long)e >> 4) + 4) & 7) << 11;
    b = ((((long)e >> 7) + 4) & 7) << 11;
    drawobj_plate[3].x = a;
    drawobj_plate[0].x = a;
    drawobj_plate[2].x = -a;
    drawobj_plate[1].x = -a;
    drawobj_plate[3].y = -b;
    drawobj_plate[1].y = b;
    drawobj_plate[2].y = -b;
    drawobj_plate[0].y = b;
    g3_transform_list(4, p, drawobj_plate);
    plate_uv(p, a, b);
    if (lit) {
        norm.x = 0;
        norm.y = 0x10000;
        norm.z = 0;
        fill_type_1();
        grd_canvas->gc.fill_parm = light_parm(&norm, drawobj_plate);
    }
    if (bm)
        g3_draw_tmap(4, p, bm);
    else
        g3_draw_poly(0x25, 4, p);
    plate_uv(p, a, b);
    q = p[3]; p[3] = p[0]; p[0] = q;
    q = p[1]; p[1] = p[2]; p[2] = q;
    if (lit) {
        norm.y = -norm.y;
        fill_type_1();
        grd_canvas->gc.fill_parm = light_parm(&norm, drawobj_plate);
    }
    if (bm)
        g3_draw_tmap(4, p, bm);
    else
        g3_draw_poly(0x25, 4, p);
    g3_free_list(4, p);
    g3_end_object();
    if (lit)
        fill_type_0();
}

/* the nav point marker at (ffpipe_38FFF0, ffpipe_38FFF4): a spinning pyramid and the distance
 * from the camera's entity in metres */
void drawobj_29BA3C(void)
{
    GameEntity m;
    g3s_phandle p[5];
    char text[0x20];
    long saved;

    if (grd_canvas->gc.fill_type != 0) {
        saved = grd_canvas->gc.fill_parm;
        fill_type_0();
    } else
        saved = 0;
    m.x = ffpipe_38FFF0;
    m.y = ffpipe_38FFF4;
    m.z = map_2949C4(m.x, m.y) + 0x10000;
    m.f1d = 0;
    m.body = 0;
    m.cls = 6;
    m.heading = drawobj_nav_turn;
    drawobj_nav_turn = (ushort)(drawobj_nav_turn + 0x100);
    grd_canvas->gc.fcolor = 0x9C;
    drawobj_29B3AC(&m, NULL);
    g3_transform_list(5, p, drawobj_nav_shape);
    g3_draw_line(p[0], p[1]);
    g3_draw_line(p[0], p[2]);
    g3_draw_line(p[0], p[3]);
    g3_draw_line(p[1], p[2]);
    g3_draw_line(p[2], p[3]);
    g3_draw_line(p[3], p[1]);
    if (p[4]->codes == 0 && (p[4]->p3_flags & 8)) {
        GameEntity *c;
        fix d;
        drawobj_nav_sx = p[4]->sx >> 16;
        drawobj_nav_sy = p[4]->sy >> 16;
        c = ENT(S16(game_camera, 0xBC));
        d = fix_safe_pyth_dist(ffpipe_38FFF0 - c->x, ffpipe_38FFF4 - c->y, DRAWOBJ_FILE, 0x40D);
        lg_sprintf(text, "%d M", (d * 6) >> 16);
#ifndef TN_WATCOM_ABI
        if (port_s3d_on && (port_s3d_kx > 1 || port_s3d_ky > 1))  /* the sharp view: at the game's size */
            port_s3d_text(0x1F7, text, drawobj_nav_sx + 0xF * port_s3d_kx, drawobj_nav_sy - 3 * port_s3d_ky);
        else
#endif
        GfxDrawText(0x1F7, text, drawobj_nav_sx + 0xF, drawobj_nav_sy - 3);
    } else
        drawobj_nav_sx = -1;
    g3_free_list(5, p);
    g3_end_object();
    if (saved) {
        fill_type_1();
        grd_canvas->gc.fill_parm = saved;
    }
}

/* grows the screen rectangle r (x0, y0, x1, y1) by point p (or to the canvas edges it is
 * off-screen past) */
void drawobj_29BC60(g3s_point *p, short *r)
{
    if (p->p3_flags & 8) {
        long x = p->sx >> 16, y = p->sy >> 16;
        if (x < r[0])
            r[0] = (short)x;
        if (x > r[2])
            r[2] = (short)x;
        if (y < r[1])
            r[1] = (short)y;
        if (y > r[3])
            r[3] = (short)y;
        return;
    }
    if (p->codes & 1)
        r[0] = 0;
    if (p->codes & 2)
        r[2] = (short)(ff_cam.half_w * 2);
    if (p->codes & 8)
        r[1] = 0;
    if (p->codes & 4)
        r[3] = (short)(ff_cam.half_h * 2);
}

/* the screen rectangle of the bounding box (+0x08 max, +0x14 min) of the model buf's Ref */
void drawobj_29BCF0(GameEntity *e, uchar *buf, short *r)
{
    uchar *bb = RefGet(*(Ref *)buf);
    g3s_phandle p[8];
    fix dx, dy, dz;
    long i;

    drawobj_29B3AC(e, bb);
    dx = D32(bb, 8) - D32(bb, 0x14);
    dy = D32(bb, 0xC) - D32(bb, 0x18);
    dz = D32(bb, 0x10) - D32(bb, 0x1C);
    p[0] = g3_transform_point((g3s_vector *)(bb + 0x14));
    p[1] = g3_copy_add_delta_x(p[0], dx);
    p[2] = g3_copy_add_delta_y(p[1], dy);
    p[3] = g3_copy_add_delta_y(p[0], dy);
    p[4] = g3_copy_add_delta_z(p[0], dz);
    p[5] = g3_copy_add_delta_x(p[4], dx);
    p[6] = g3_copy_add_delta_y(p[5], dy);
    p[7] = g3_copy_add_delta_y(p[4], dy);
    g3_project_list(8, p);
    g3_end_object();
    r[0] = (short)(ff_cam.half_w * 2);
    r[2] = 0;
    r[1] = (short)(ff_cam.half_h * 2);
    r[3] = 0;
    for (i = 0; i < 8; i++)
        drawobj_29BC60(p[i], r);
    g3_free_list(8, p);
}

/* the bounding box of the entity's model: a = (min z, min x, -max y)... as (+0x1C, +0x14,
 * -+0x0C), b = (+0x10, +0x08, -+0x18) */
uchar drawobj_29BE28(GameEntity *e, fix *a, fix *b)
{
    DrawInfo d;
    uchar *m;
    if (!drawobj_29ACA8(e, (uchar *)&d))
        return 0;
    m = RefGet(d.ref);
    a[0] = D32(m, 0x1C);
    a[1] = D32(m, 0x14);
    a[2] = -D32(m, 0xC);
    b[0] = D32(m, 0x10);
    b[1] = D32(m, 8);
    b[2] = -D32(m, 0x18);
    return 1;
}

/* draws the bitmap bm (with a frame anchor at +0x10) scaled to width `scale` at the entity,
 * shaded by `shade` (0..15) */
void drawobj_29BE78(GameEntity *e, uchar *bm, fix scale, long shade)
{
    long sx, sy, vz, vy, vx;
    long w, h, x0, y0, clut, restore;
    grs_bitmap *b = (grs_bitmap *)bm;

    drawobj_29DA60(e->x, e->y, e->z, &vx, &vy, &vz, &sx, &sy);
    if (iabs(vy) > iabs(vz) * 10 || iabs(vx) > iabs(vz) * 10)
        return;
    sx >>= 16;
    sy >>= 16;
    w = muldiv(ff_cam.zx, scale, vz);
    h = muldiv(ff_cam.zy, (b->h * scale) / b->w, vz) >> 16;
    w >>= 16;
    if (S16(bm, 0x10) != 0 || S16(bm, 0x12) != 0) {
        x0 = sx - (S16(bm, 0x10) * w) / b->w;
        y0 = sy - (S16(bm, 0x12) * h) / b->h;
    } else {
        x0 = sx - half(w);
        y0 = sy - h;
    }
    if (grd_canvas->gc.fill_type == 1)
        clut = grd_canvas->gc.fill_parm;
    else
        clut = ((15 - shade) << 8) + (long)grd_screen->ltab;
    restore = clut;
    if (clut == (long)planet_tab3b) {
        if (e->cls == 3)
            clut = (long)planet_tab3e;
        else
            clut = (long)planet_tab3b + ((drawobj_flat ? 3 : 1) << 8);
#ifndef TN_WATCOM_ABI
        if (port_s3d_on)                /* the sharp view: Sci-fi's trees, the fog (infrared.c) */
            clut = (long)port_ir_obj_clut(e, (const uchar *)clut);
#endif
        grd_canvas->gc.fill_parm = clut;
        restore = (long)planet_tab3b;
    }
    y0++;
#ifndef TN_WATCOM_ABI
    /* "Effects pack" (port/mods/effects.c): its picture see-through by its alpha (a translucent frame's
     * as the game's, without the light table) */
    if (port_s3d_on && port_fx_blit(b->bits, x0, y0, w, h, b->type == 5 && grd_canvas->gc.fill_type != 1 ?
                                    NULL : (const uchar *)clut)) {
        if (restore == (long)planet_tab3b)
            grd_canvas->gc.fill_parm = restore;
        return;
    }
#endif
    if (b->type == 5) {
        if (ff_cam.probe != 0 && *ff_cam.probe_ptr == 0xFF) {
            *ff_cam.probe_ptr = ff_cam.probe_save;
            WC5(grd_canvas_table[SCALE_DEVICE_BITMAP + 2 * b->type], b, (short)x0, (short)y0, (short)w, (short)h);
            ff_cam.probe_save = *ff_cam.probe_ptr;
            *ff_cam.probe_ptr = 0xFF;
        } else
            WC5(grd_canvas_table[SCALE_DEVICE_BITMAP + 2 * b->type], b, (short)x0, (short)y0, (short)w, (short)h);
    } else
        WC6(grd_canvas_table[CLUT_SCALE_DEVICE_BITMAP + 2 * b->type], b, (short)x0, (short)y0, (short)w, (short)h, clut);
    if (restore == (long)planet_tab3b)
        grd_canvas->gc.fill_parm = restore;
}

/* ------------------------------------------------------------------ PBA skeletons */

/* out = |a - b| * (cos d sin (c - e), cos d cos (c - e), sin d) */
void drawobj_29C114(fix *out, fix *a, fix *b, long c, long d, long e)
{
    fix dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    fix len = fix_sqrt(fix_mul(dx, dx) + fix_mul(dy, dy) + fix_mul(dz, dz));
    fix s1, c1, s2, c2;
    fix_sincos((fixang)(c - e), &s1, &c1);
    fix_sincos((fixang)d, &s2, &c2);
    out[0] = fix_mul(fix_mul(c2, s1), len);
    out[1] = fix_mul(fix_mul(c2, c1), len);
    out[2] = fix_mul(s2, len);
}

/* blends the limb vector v (from b) toward a - b by t, keeping its length; v += b - a */
void drawobj_29C1DC(fix *v, fix *a, fix *b, fix t)
{
    if (t != 0x10000) {
        fix dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        fix len = fix_sqrt(fix_mul(dx, dx) + fix_mul(dy, dy) + fix_mul(dz, dz));
        fix nx = dx + fix_mul(v[0] - dx, t);
        fix ny = dy + fix_mul(v[1] - dy, t);
        fix nz = dz + fix_mul(v[2] - dz, t);
        fix l2 = fix_sqrt(fix_mul(nx, nx) + fix_mul(ny, ny) + fix_mul(nz, nz));
        v[0] = muldiv(nx, len, l2);
        v[1] = muldiv(ny, len, l2);
        v[2] = muldiv(nz, len, l2);
    }
    v[0] += b[0] - a[0];
    v[1] += b[1] - a[1];
    v[2] += b[2] - a[2];
}

void drawobj_29C348(fix *a, fix *b)
{
    a[0] += b[0];
    a[1] += b[1];
    a[2] += b[2];
}

long drawobj_29C36C(long a)
{
    long x = (ushort)a - 0xC000;
    if (x < 0)
        x += 0x10000;
    return half(x) + 0xC000;
}

long drawobj_29C39C(long a)
{
    return half((short)a);
}

/* joint (x, y, z) -> skeleton point (y, -z, x) */
static void joint_to_skel(fix *d, fix *s)
{
    d[2] = s[0];
    d[0] = s[1];
    d[1] = -s[2];
}

/* copies the biped's joints (bp + 0xD1, 12 bytes each) into the skeleton's point arrays */
void drawobj_29C3AC(uchar *bp, uchar *sk)
{
    fix *s = (fix *)(bp + 0xD1);
    fix *d;
    long i;
    d = (fix *)D32(sk, 0xC);
    for (i = 0; i < sk[2]; i++, s += 3)
        joint_to_skel(d + 3 * i, s);
    d = (fix *)D32(sk, 0x10);
    for (i = 0; i < sk[3]; i++, s += 3)
        joint_to_skel(d + 3 * i, s);
}

/* copies the joints of bone k (its 3 point indices) into the skeleton */
void drawobj_29C448(uchar *bp, uchar *sk, long k)
{
    fix *s = (fix *)(bp + 0xD1);
    fix *s2 = s + 3 * sk[2];
    uchar *rec = (uchar *)D32(sk, 8) + k * 16;
    joint_to_skel((fix *)D32(sk, 0xC) + 3 * rec[0], s + 3 * rec[0]);
    joint_to_skel((fix *)D32(sk, 0xC) + 3 * rec[1], s + 3 * rec[1]);
    joint_to_skel((fix *)D32(sk, 0x10) + 3 * rec[2], s2 + 3 * rec[2]);
}

/* poses the PBA in slot at time t (BipedComputePose) and bends its arms toward their aim */
void drawobj_29C508(long slot, long t)
{
    uchar *pba = PBA(slot);
    GameEntity *e = ENT((short)(U16(pba, 0) << 1) >> 1);
    uchar *bp = pba + 0xDE;
    fix v[3], w[3];
    long hd;

    if (t <= D32(pba, 0xE3))
        return;
    BipedComputePose(bp, t);
    if (D32(bp, 0) == 0xB) {
        long A = D32(pba, 0x3FE), B = D32(pba, 0x3FA);
        drawobj_pose[2] = 0;
        drawobj_pose[1] = 0;
        drawobj_pose[0] = 0;
        if (A > B && A > 0) {
            drawobj_pose[0] = -(long)U16(pba, 0x3F6);
            drawobj_pose[1] = U16(pba, 0x3F8);
            drawobj_pose[2] = U16(pba, 0x3F4);
        } else if (B > 0) {
            drawobj_pose[0] = -(long)U16(pba, 0x3F2);
            drawobj_pose[1] = U16(pba, 0x3F8);
            drawobj_pose[2] = U16(pba, 0x3F4);
        }
        D32(D32(bp, 0x15), 0x3C) = (long)drawobj_pose;
        return;
    }
    if (D32(pba, 0x3FE) != 0) {
        fix *p1 = (fix *)(bp + 0x149), *p2 = (fix *)(bp + 0x161), *p3 = (fix *)(bp + 0x179);
        hd = (ushort)(e->heading + 0xC000);
        drawobj_29C114(v, p2, p1, drawobj_29C39C(U16(pba, 0x3F6)) & 0xFFFF,
                       drawobj_29C36C(U16(pba, 0x3F8)) & 0xFFFF, hd);
        drawobj_29C1DC(v, p2, p1, D32(pba, 0x3FE));
        p2[0] += v[0]; p2[1] += v[1]; p2[2] += v[2];
        p3[0] += v[0]; p3[1] += v[1]; p3[2] += v[2];
        drawobj_29C114(v, p3, p2, U16(pba, 0x3F6), U16(pba, 0x3F8), hd);
        drawobj_29C1DC(v, p3, p2, D32(pba, 0x3FE));
        p3[0] += v[0]; p3[1] += v[1]; p3[2] += v[2];
    }
    if (D32(pba, 0x3FA) != 0) {
        fix *p1 = (fix *)(bp + 0x155), *p2 = (fix *)(bp + 0x16D), *p3 = (fix *)(bp + 0x185);
        hd = (ushort)(e->heading + 0xC000);
        drawobj_29C114(w, p2, p1, drawobj_29C39C(U16(pba, 0x3F2)) & 0xFFFF,
                       drawobj_29C36C(U16(pba, 0x3F4)) & 0xFFFF, hd);
        drawobj_29C1DC(w, p2, p1, D32(pba, 0x3FA));
        p2[0] += w[0]; p2[1] += w[1]; p2[2] += w[2];
        p3[0] += w[0]; p3[1] += w[1]; p3[2] += w[2];
        drawobj_29C114(w, p3, p2, U16(pba, 0x3F2), U16(pba, 0x3F4), hd);
        drawobj_29C1DC(w, p3, p2, D32(pba, 0x3FA));
        p3[0] += w[0]; p3[1] += w[1]; p3[2] += w[2];
    }
}

static void skel_points(uchar *sk)
{
    uchar *buf;
    skel_2F6A64((Skel *)sk);
    buf = ffmem_2C4F40((sk[2] + sk[3]) * 12);
    D32(sk, 0xC) = (long)buf;
    D32(sk, 0x10) = (long)(buf + sk[2] * 12);
}

static void biped_textures(uchar *sk, uchar lock)
{
    uchar *m = (uchar *)D32(sk, 8);
    drawobj_29B488((uchar *)D32(m, 8), lock);
    drawobj_29B488((uchar *)D32(m, 0x18), lock);
    drawobj_29B488((uchar *)D32(m, 0x28), lock);
    drawobj_29B488((uchar *)D32(m, 0x38), lock);
}

/* draws a PBA (class 1 entity) */
void drawobj_29C864(GameEntity *e)
{
    uchar *pba = PBA(e->slot);
    uchar *bp = pba + 0xDE, *sk = pba + 0xEB;

    if (e->slot == game_player_slot && ragdoll_cam != NULL)
        return;
    skel_points(sk);
    drawobj_29C508(e->slot, biped_2820A4(ENT_INDEX(e)));
    drawobj_29C3AC(bp, sk);
    if (D32(bp, 0) == 0xB)
        biped_textures(sk, 1);
    if (grd_canvas->gc.fill_type != 0) {
        ffpipe_29E4F4();
        if (grd_canvas->gc.fill_parm == (long)planet_tab3b) {
            grd_canvas->gc.fill_parm = (long)planet_tab3e;
#ifndef TN_WATCOM_ABI
            if (port_s3d_on)            /* the sharp view: the fog (infrared.c) */
                grd_canvas->gc.fill_parm = (long)port_ir_obj_clut(e, planet_tab3e);
#endif
            skel_2F74F0((Skel *)sk, 2);
            grd_canvas->gc.fill_parm = (long)planet_tab3b;
        } else
            skel_2F74F0((Skel *)sk, 2);
    } else {
        if (map_294B18(e->x, e->y) == map_shade_max * 2)
            ffpipe_29E544();
        else if (D32(bp, 0) == 0xB)
            ffpipe_29E500();
        else
            ffpipe_29E564();
        g3_eval_light_obj_cen(NULL);
        skel_2F74F0((Skel *)sk, 2);
    }
    if (D32(bp, 0) == 0xB)
        biped_textures(sk, 0);
    skel_2F6AD0((Skel *)sk);
    ffmem_2C4FE4((void *)D32(sk, 0xC));
}

/* draws a ragdoll (class 6 type 0x21) */
void drawobj_29CA48(GameEntity *e)
{
    uchar *rd = ragdoll_recs[D32(CLS6(e->slot), 0xA)];
    uchar *sk = rd + 0xD;
    uchar mine = 0;

    if (drawobj_38FB12 == game_player_entity && (uchar *)ragdoll_cam == rd + 0x149)
        mine = 1;
    D32(rd, 5) = 0;
    skel_points(sk);
    drawobj_29C3AC(rd, sk);
    if (mine)
        U8(D32(sk, 8), 0x53) |= 0x10;
    if (grd_canvas->gc.fill_type != 0) {
        ffpipe_29E4F4();
        if (grd_canvas->gc.fill_parm == (long)planet_tab3b) {
            grd_canvas->gc.fill_parm = (long)planet_tab3e;
#ifndef TN_WATCOM_ABI
            if (port_s3d_on)            /* the sharp view: the fog (infrared.c) */
                grd_canvas->gc.fill_parm = (long)port_ir_obj_clut(e, planet_tab3e);
#endif
            skel_2F74F0((Skel *)sk, 2);
            grd_canvas->gc.fill_parm = (long)planet_tab3b;
        } else
            skel_2F74F0((Skel *)sk, 2);
    } else {
        if (map_294B18(e->x, e->y) == map_shade_max * 2)
            ffpipe_29E544();
        else
            ffpipe_29E564();
        g3_eval_light_obj_cen(NULL);
        skel_2F74F0((Skel *)sk, 2);
    }
    if (mine)
        U8(D32(sk, 8), 0x53) &= ~0x10;
    skel_2F6AD0((Skel *)sk);
    ffmem_2C4FE4((void *)D32(sk, 0xC));
}

/* ------------------------------------------------------------------ screen positions */

/* the screen point of entity ent as a rectangle of one point (unchanged when not drawn) */
#ifndef TN_WATCOM_ABI
extern int port_hud_noclamp;            /* port/video/sharp3d.c */
#endif

void drawobj_29CBA4(long ent, short *r)
{
    GameEntity *e = ENT(ent);
    DrawInfo d;
    fix sx, sy;

    if (e->cls == 1) {
        biped_27EAF4((short)ent, &sx, &sy);
        r[2] = r[0] = (short)(sx >> 16);
        r[3] = r[1] = (short)(sy >> 16);
        return;
    }
    if (drawobj_29ACA8(e, (uchar *)&d) && d.draw) {
        ulong t = RES_TYPE(d.ref);
        fix o[3];
        g3s_vector v;
        g3s_phandle p;
        long x, y;
        if (t != 0 && t != 0xF)
            return;
        interact_298864((short)ent, o);
        v.z = o[0];
        v.x = o[1];
        v.y = -o[2];
        p = g3_transform_point(&v);
        x = p->sx;
        y = p->sy;
        g3_free_point(p);
        x = (x + 0x8000) >> 16;
        y = (y + 0x8000) >> 16;
#ifndef TN_WATCOM_ABI
        if (port_hud_noclamp) {         /* widescreen: also beyond the game's view (hudmain.c) */
            r[2] = r[0] = (short)x;
            r[3] = r[1] = (short)y;
            return;
        }
#endif
        if (x < 0)
            x = 0;
        else if (x >= grd_canvas->bm.w)
            x = grd_canvas->bm.w - 1;
        if (y < 0)
            y = 0;
        else if (y >= grd_canvas->bm.h)
            y = grd_canvas->bm.h - 1;
        r[2] = r[0] = (short)x;
        r[3] = r[1] = (short)y;
    }
}

static void rect_scale(short *r)
{
    long f0 = disp_flags & 1, f1 = (disp_flags >> 1) & 1;
    r[2] = (short)(r[2] << f0);
    r[0] = (short)(r[0] << f0);
    r[1] = (short)(r[1] << f1);
    r[3] = (short)(r[3] << f1);
}

/* the screen rectangle of the entity, clipped to the canvas; 1 = not empty */
uchar drawobj_29CCE0(GameEntity *e, short *r)
{
    DrawInfo d;
    fix z = e->z;
    long vxy, vz, sx, sy;
    ulong t;
#ifndef TN_WATCOM_ABI
    uchar hdr[0x1C];
#endif

    if (!drawobj_29ACA8(e, (uchar *)&d))
        return 0;
    if (!drawobj_29DA60(e->x, e->y, e->z, &vxy, &vxy, &vz, &sx, &sy))
        return 0;
    if (!d.draw)
        return 0;
    t = RES_TYPE(d.ref);
    if (t == 2) {
        grs_bitmap *b;
        long w, h, x, y;
        if (e->body)
            z -= physics_2D45CC(ENT_INDEX(e));
        drawobj_29DA60(e->x, e->y, z, &vxy, &vxy, &vz, &sx, &sy);
        x = (short)(sx >> 16);
        y = sy >> 16;
        b = RefGet(d.ref);
#ifndef TN_WATCOM_ABI
        /* "Effects pack" in the sharp view: the box of the pack's picture, a death's at its size
         * (port/mods/effects.c), so the view does not cut it off */
        if (port_s3d_on && e == &port_burn_ent) {   /* "Tree explosion": the burn's box */
            port_burn_box(hdr, &d.scale);
            b = (grs_bitmap *)hdr;
        } else if (port_s3d_on && e->cls == 6) {
            int k = port_fx_frame(*(void **)(CLS6(e->slot) + 6), e->slot, d.ref, (uchar *)b, hdr, &d.scale);
            if (k < 0)
                return 0;
            if (k > 0)
                b = (grs_bitmap *)hdr;
        }
#endif
        w = muldiv(ff_cam.zx, d.scale, vz) >> 16;
        h = muldiv(ff_cam.zy, (b->h * d.scale) / b->w, vz) >> 16;
        r[0] = (short)(x - half((short)w));
        r[1] = (short)(y - h);
        r[2] = (short)(r[0] + w);
        r[3] = (short)(r[1] + h);
#ifndef TN_WATCOM_ABI
        if (port_s3d_on && e == &port_burn_ent)     /* and what the burn draws below its foot */
            r[3] = (short)(r[3] + port_burn_below(h));
#endif
        rect_scale(r);
    } else if (t == 0 || t == 0xF) {
        if (e->cls != 1)
            drawobj_29BCF0(e, (uchar *)&d, r);
        else
            biped_28213C(ENT_INDEX(e), r);
    } else
        return 0;
    if (r[0] < 0)
        r[0] = 0;
    if (r[1] < 0)
        r[1] = 0;
    if (r[2] >= grd_canvas->bm.w)
        r[2] = grd_canvas->bm.w - 1;
    if (r[3] >= grd_canvas->bm.h)
        r[3] = grd_canvas->bm.h - 1;
    return r[0] < r[2] && r[1] < r[3];
}

void drawobj_29CF2C(short *r, fix x, fix y)
{
    x >>= 16;
    y >>= 16;
    if (x < r[0])
        r[0] = (short)x;
    if (x > r[2])
        r[2] = (short)x;
    if (y < r[1])
        r[1] = (short)y;
    if (y > r[3])
        r[3] = (short)y;
}

void drawobj_29CF68(short *r)
{
    rect_scale(r);
}

/* ------------------------------------------------------------------ 3D lines */

#define NEAR_Z 0x28F

static fix proj_x(fix v, fix z) { return fdiv(v, z) * ff_cam.half_w + (ff_cam.half_w << 16); }
static fix proj_y(fix v, fix z) { return fdiv(v, z) * ff_cam.half_h + (ff_cam.half_h << 16); }

/* a line between two world points (the near end clipped to the view plane) */
void drawobj_29CFC4(fix ax, fix ay, fix az, fix bx, fix by, fix bz, uchar color)
{
    long avx, avy, avz, asx, asy, bvx, bvy, bvz, bsx, bsy;
    uchar ra = drawobj_29DA60(ax, ay, az, &avx, &avy, &avz, &asx, &asy);
    uchar rb = drawobj_29DA60(bx, by, bz, &bvx, &bvy, &bvz, &bsx, &bsy);

    if (!ra && !rb)
        return;
    if (rb && !ra) {
        fix t = fdiv(avz - NEAR_Z, avz - bvz);
        avx += fix_mul(bvx - avx, t);
        avy += fix_mul(bvy - avy, t);
        asx = fdiv(avx, NEAR_Z) * ff_cam.half_w + (ff_cam.half_w << 16);
        asy = fdiv(avy, NEAR_Z) * ff_cam.half_h + (ff_cam.half_h << 16);
    } else if (!rb && ra) {
        fix t = fdiv(bvz - NEAR_Z, bvz - avz);
        bvx += fix_mul(avx - bvx, t);
        bvy += fix_mul(avy - bvy, t);
        bsx = fdiv(bvx, NEAR_Z) * ff_cam.half_w + (ff_cam.half_w << 16);
        bsy = fdiv(bvy, NEAR_Z) * ff_cam.half_h + (ff_cam.half_h << 16);
    }
#ifndef TN_WATCOM_ABI
    if (port_s3d_on) {                  /* the sharp view: limits and width of the game's view */
        long lim = 0x400 * (port_s3d_kx > port_s3d_ky ? port_s3d_kx : port_s3d_ky), n, i;
        int across;
        if ((iabs(asx) >> 16) > lim || (iabs(asy) >> 16) > lim || (iabs(bsx) >> 16) > lim
            || (iabs(bsy) >> 16) > lim)
            return;
        grd_canvas->gc.fcolor = color;
        across = iabs(bsx - asx) < iabs(bsy - asy);     /* steep: widen across x */
        n = ((across ? port_s3d_kx : port_s3d_ky) + 1) / 2;
        for (i = 0; i < n; i++) {
            fix o = (fix)((2 * i - (n - 1)) << 15);
            if (across)
                gr_fix_line(asx + o, asy, bsx + o, bsy);
            else
                gr_fix_line(asx, asy + o, bsx, bsy + o);
        }
        return;
    }
#endif
    if ((iabs(asx) >> 16) > 0x400 || (iabs(asy) >> 16) > 0x400 || (iabs(bsx) >> 16) > 0x400
        || (iabs(bsy) >> 16) > 0x400)
        return;
    grd_canvas->gc.fcolor = color;
    gr_fix_line(asx, asy, bsx, bsy);
}

/* moves (p, q) toward (d, e) to where the clip plane (a -> b) is crossed */
void drawobj_29D238(long a, fix *p, fix *q, long b, fix d, fix e)
{
    fix t = fdiv(*q - a, b - a - e + *q);
    *p += fix_mul(d - *p, t);
    *q += fix_mul(e - *q, t);
}

long drawobj_29D284(fix x, fix y, fix w)
{
    long c = 0;
    if (x < -w)
        c = 1;
    if (x > w)
        c |= 2;
    if (y < -w)
        c |= 4;
    if (y > w)
        c |= 8;
    return c;
}

static long outcode(fix x, fix y, fix z)
{
    long c = 0;
    if (x < -z)
        c = 1;
    if (x > z)
        c |= 2;
    if (y < -z)
        c |= 4;
    if (y > z)
        c |= 8;
    return c;
}

/* the half width of the band at depth z, clamped */
static void band_offsets(fix ux, fix uy, fix width, fix z, fix *px, fix *py)
{
    long w = muldiv(ff_cam.zx, width, z) + 2;
#ifndef TN_WATCOM_ABI
    if (port_s3d_on) {                  /* the sharp view: the game's view's limit, k times */
        if (w > 0x800000 * port_s3d_kx)
            w = 0x800000 * port_s3d_kx;
    } else
#endif
    if (w > 0x800000)
        w = 0x800000;
    *px = fix_mul(uy, w);
    *py = fix_mul(ux, w);
}

/* a band of the given world width between two world points, clipped to the view pyramid,
 * drawn as three polygons (end caps and the middle) with colour color */
void drawobj_29D2B0(fix ax, fix ay, fix az, fix bx, fix by, fix bz, fix width, uchar color)
{
    long xa, ya, za, xb, yb, zb;
    long ca, cb;
    fix sxa, sya, sxb, syb, dx, dy, len, ux, uy, px, py, s;
    grs_vertex tmp;
    uchar ra = drawobj_29DA60(ax, ay, az, &xa, &ya, &za, NULL, NULL);
    uchar rb = drawobj_29DA60(bx, by, bz, &xb, &yb, &zb, NULL, NULL);

    if (!ra && !rb)
        return;
    ca = outcode(xa, ya, za);
    cb = outcode(xb, yb, zb);
    if (ca & cb)
        return;
    if (ca | cb) {
        if (ca & 1) {
            drawobj_29D238(-xa, &ya, &za, -xb, yb, zb);
            xa = -za;
            ca = outcode(xa, ya, za);
        } else if (cb & 1) {
            drawobj_29D238(-xb, &yb, &zb, -xa, ya, za);
            xb = -zb;
            cb = outcode(xb, yb, zb);
        }
        if (ca & cb)
            return;
        if (ca & 2) {
            drawobj_29D238(xa, &ya, &za, xb, yb, zb);
            xa = za;
            ca = outcode(xa, ya, za);
        } else if (cb & 2) {
            drawobj_29D238(xb, &yb, &zb, xa, ya, za);
            xb = zb;
            cb = outcode(xb, yb, zb);
        }
        if (ca & cb)
            return;
        if (ca & 4) {
            drawobj_29D238(-ya, &xa, &za, -yb, xb, zb);
            ya = -za;
            ca = outcode(xa, ya, za);
        } else if (cb & 4) {
            drawobj_29D238(-yb, &xb, &zb, -ya, xa, za);
            yb = -zb;
            cb = outcode(xb, yb, zb);
        }
        if (ca & cb)
            return;
        if (ca & 8) {
            drawobj_29D238(ya, &xa, &za, yb, xb, zb);
            ya = za;
            ca = outcode(xa, ya, za);
        } else if (cb & 8) {
            drawobj_29D238(yb, &xb, &zb, ya, xa, za);
            yb = zb;
            cb = outcode(xb, yb, zb);
        }
        if (ca & cb)
            return;
    }
    if (ca != 0 || cb != 0)
        return;
    sxa = proj_x(xa, za);
    sya = proj_y(ya, za);
    sxb = proj_x(xb, zb);
    syb = proj_y(yb, zb);
    dx = sxb - sxa;
    dy = syb - sya;
    len = fix_safe_pyth_dist(dx, dy, DRAWOBJ_FILE, 0x7CC);
#ifndef TN_WATCOM_ABI
    if (len == 0 || len > (port_s3d_on ? 0x4000000 * (port_s3d_kx > port_s3d_ky ? port_s3d_kx : port_s3d_ky) : 0x4000000))
        return;
#else
    if (len == 0 || len > 0x4000000)
        return;
#endif
    ux = fdiv(dx, len);
    uy = fdiv(dy, len);

    band_offsets(ux, uy, width, za, &px, &py);
    wl_verts[2].x = sxa + px;
    wl_verts[2].y = sya - py;
    wl_verts[3].x = sxa - px;
    wl_verts[3].y = sya + py;
    wl_verts[1].x = sxa + 3 * (px - py) / 4;
    s = 3 * (py + px) / 4;
    wl_verts[1].y = sya - s;
    wl_verts[0].x = sxa - s;
    wl_verts[0].y = sya + 3 * (py - px) / 4;
    WC3(grd_canvas_table[FIX_POLY], color, 4, &wl_vlist[0]);

    band_offsets(ux, uy, width, zb, &px, &py);
    wl_verts[5].x = px + sxb;
    wl_verts[5].y = syb - py;
    wl_verts[4].x = sxb - px;
    wl_verts[4].y = syb + py;
    s = 3 * (px + py) / 4;
    wl_verts[6].x = s + sxb;
    wl_verts[6].y = syb - 3 * (py - px) / 4;
    wl_verts[7].x = sxb - 3 * (px - py) / 4;
    wl_verts[7].y = syb + s;
    WC3(grd_canvas_table[FIX_POLY], color, 4, &wl_vlist[4]);

    /* the middle: v2..v5 with the pairs (v2, v3) and (v4, v5) swapped */
    tmp = wl_verts[2]; wl_verts[2] = wl_verts[3]; wl_verts[3] = tmp;
    tmp = wl_verts[4]; wl_verts[4] = wl_verts[5]; wl_verts[5] = tmp;
    WC3(grd_canvas_table[FIX_POLY], color, 4, &wl_vlist[2]);
}

/* world (x, y, h) -> view (ox, oy, oz: x, y, depth; stored when ox != NULL) and screen (sx, sy:
 * 16.16, stored when sx != NULL); 0 = behind the near plane or (with sx) off the screen */
uchar drawobj_29DA60(fix x, fix y, fix h, fix *ox, fix *oy, fix *oz, long *sx, long *sy)
{
    fix dx = x - ff_view.x, dy = y - ff_view.y, dz = h * 24;
    fix *m = ff_view.m;
    fix vx = fix_mul(dy, m[0]) + fix_mul(dz, m[1]) + fix_mul(dx, m[2]) + ff_view.mz[0];
    fix vy = fix_mul(dy, m[3]) + fix_mul(dz, m[4]) + fix_mul(dx, m[5]) + ff_view.mz[1];
    fix vz = fix_mul(dy, m[6]) + fix_mul(dz, m[7]) + fix_mul(dx, m[8]) + ff_view.mz[2];

    if (ox != NULL) {
        *ox = vx;
        *oy = vy;
        *oz = vz;
    }
    if (vz < NEAR_Z)
        return 0;
    if (sx == NULL)
        return 1;
    if ((iabs(ff_cam.half_w * vx) >> 16) >= half(iabs(vz)))
        return 0;
    if ((iabs(ff_cam.half_h * vy) >> 16) >= half(iabs(vz)))
        return 0;
    *sx = proj_x(vx, vz);
    *sy = proj_y(vy, vz);
    return 1;
}

/* ------------------------------------------------------------------ pitch from the terrain */

static fix ground(fix x, fix y, fix z, long caller)
{
    terrain_ff t;
    t.caller = caller;
    physics_2D5C90(x * 6, y * 6, z * 6, 0, &t);
    return *(fix *)&t / 6;
}

/* the pitch of a vehicle from the terrain under it and one unit ahead, faded out above the
 * ground and clamped to +-0x1555 */
fixang drawobj_29DC20(long ent)
{
    GameEntity *e = ENT(ent);
    fix s, c, h0, h1;
    long a, dz;

    fix_sincos(e->heading, &s, &c);
    h0 = ground(e->x, e->y, e->z, (short)ent);
    h1 = ground(c + e->x, s + e->y, e->z, (short)ent);
    a = -(long)fix_atan2((h1 - h0) >> 2, 0x10000);
    dz = e->z - h0;
    if (dz > 0x20000)
        a = 0;
    else {
        if (dz > 0)
            a = muldiv((short)a, 0x20000 - dz, 0x20000);
        if ((short)a > 0x1555)
            return 0x1555;
        if ((short)a < -0x1555)
            return 0xEAAB;
    }
    return (fixang)a;
}

/* the pitch of a model from the terrain at the front and back of its bounding box bb */
fixang drawobj_29DD64(GameEntity *e, uchar *bb)
{
    fix s, c, hl, cx, sy, h0, h1;
    fix_sincos(e->heading, &s, &c);
    hl = half(D32(bb, 0x10) - D32(bb, 0x1C));
    cx = fix_mul(hl, c);
    sy = fix_mul(hl, s);
    h0 = ground(e->x + cx, e->y + sy, e->z, ENT_INDEX(e));
    h1 = ground(e->x - cx, e->y - sy, e->z, ENT_INDEX(e));
    return (fixang)-(long)fix_atan2(h1 - h0, hl * 2);
}

/* ------------------------------------------------------------------ class 6 effects */

/* a beam from the entity to the record's end point: an outer and an inner band */
void drawobj_29DE8C(GameEntity *e)
{
    uchar *r = CLS6(e->slot);
    long w1, w2;
    uchar c1, c2;
    if (r[0x18] == 0x28) {
        c2 = 0x20;
        w2 = 0x50;
        c1 = 0x25;
        w1 = 0xC0;
    } else {
        c2 = r[0x18];
        w2 = 0xC0;
        c1 = (uchar)(r[0x18] + 6);
        w1 = 0x200;
    }
    if (w1 != 0)
        drawobj_29D2B0(e->x, e->y, e->z, D32(r, 0xC), D32(r, 0x10), D32(r, 0x14), w1, c1);
    drawobj_29D2B0(e->x, e->y, e->z, D32(r, 0xC), D32(r, 0x10), D32(r, 0x14), w2, c2);
}

/* the record's particle system, with the view matrix as world -> view transform */
void drawobj_29DF38(GameEntity *e)
{
    uchar *r = CLS6(e->slot);
    fix mt[9], t[3];
    fix *m = ff_view.m;
    long probe;

    if (D32(r, 0xA) == -1)
        return;
    mt[0] = m[2];
    mt[1] = m[0];
    mt[2] = m[1] * 24;
    mt[3] = m[5];
    mt[4] = m[3];
    mt[5] = m[4] * 24;
    mt[6] = m[8];
    mt[7] = m[6];
    mt[8] = m[7] * 24;
    t[0] = ff_view.mz[0] + (-fix_mul(m[0], ff_view.y) - fix_mul(m[2], ff_view.x));
    t[1] = ff_view.mz[1] + (-fix_mul(m[3], ff_view.y) - fix_mul(m[5], ff_view.x));
    t[2] = ff_view.mz[2] + (-fix_mul(m[6], ff_view.y) - fix_mul(m[8], ff_view.x));
    if (ff_cam.probe != 0 && *ff_cam.probe_ptr == 0xFF) {
        *ff_cam.probe_ptr = ff_cam.probe_save;
        probe = 1;
    } else
        probe = 0;
    particle_2A56A8(D32(r, 0xA), mt, t, ff_cam.half_w, ff_cam.half_h, ff_cam.half_w, ff_cam.half_h, 0);
    if (probe) {
        ff_cam.probe_save = *ff_cam.probe_ptr;
        *ff_cam.probe_ptr = 0xFF;
    }
}

void drawobj_29E0DC(GameEntity *e)
{
    uchar *r = CLS6(e->slot);
    if (D32(r, 0xA) != 0)
        bolt_draw_simple(D32(r, 0xA), D32(r, 0xE), D32(r, 0xE) << 8);
}
