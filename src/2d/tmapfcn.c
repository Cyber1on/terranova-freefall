/* tmapfcn.c -- texture mapper edge functions (Terra Nova v1.09; System Shock 2d tmapfcn.c
 * family, names partly Shock's, partly ours).
 *
 *   0x34B410 gri_x_edge      0x34B474 gri_ix_edge     0x34B510 gri_rgbx_edge
 *   0x34B610 gri_scale_edge  0x34B69C gri_uvx_grad_edge (ours, unreferenced)
 *   0x34B77C gri_uvix_grad_edge (ours, unreferenced)  0x34B890 gri_uvx_edge
 *   0x34B960 gri_uvix_edge   0x34BA60 gri_uvwx_edge   0x34BC14 gri_uviwx_edge
 *   0x34BE68 gri_uvwy_edge   0x34C01C gri_uviwy_edge  (the last five unreferenced)
 *
 * Edge functions are called (info, p_next, p_prev, side): side 1 = left/top edge at info+0x1C,
 * else right/bottom at +0x40.  Walking down (x edges): e.x = prev.x stepped to the first pixel
 * row ceil(prev.y), e.y = fix_cint(next.y) (as a signed short), e.dx = (next.x - prev.x) /
 * (next.y - prev.y); the other components likewise.  y edges (v scanning) swap x and y.
 * The "uvw" edges are for floors/walls (w linear along the scan direction): a component that
 * changes by less than 1/16 texel along the edge is taken as prev.c * info->w with step
 * prev.c * info->dw, nudged by +-1 towards the nearest texel; otherwise prev.c*prev.w with
 * a step to next.c*next.w.  All divisions fault on zero / overflow like the original.
 */
#include "2d.h"

typedef grs_tmap_loop_info TI;
typedef grs_tmap_edge TE;

#define FIX_CEIL(a) ((fix)(((a) + 0xFFFF) & 0xFFFF0000))
static long cint(fix a) { return (short)((a + 0xFFFF) >> 16); }
static long iabs(long a) { return a < 0 ? -a : a; }

static TE *edge_of(TI *ti, int side) { return side == 1 ? &ti->left : &ti->right; }

/* x along a downward edge; returns the y fraction to the first pixel row */
static fix x_part(TE *e, grs_vertex *n, grs_vertex *p, fix d)
{
    fix f;
    e->x = p->x;
    e->y = cint(n->y);
    e->dx = fix_div(n->x - e->x, d);
    f = FIX_CEIL(p->y) - p->y;
    e->x += fix_mul(f, e->dx);
    return f;
}

static void lin_comp(fix *c, fix *dc, fix pc, fix nc, fix d, fix f)
{
    *c = pc;
    *dc = fix_div(nc - *c, d);
    *c += fix_mul(f, *dc);
}

/* floor/wall component (perspective-correct along w) */
static void w_comp(TI *ti, fix *c, fix *dc, fix pc, fix pw, fix nc, fix nw, fix d, fix f)
{
    if (iabs(nc - pc) < 0x1000) {
        *c = fix_mul(pc, ti->w);
        *dc = fix_mul(pc, ti->dw);
        if ((pc & 0xFFFF) < 0x8000) {
            (*c)++;
            (*dc)++;
        } else {
            (*c)--;
            (*dc)--;
        }
    } else {
        *c = fix_mul(pc, pw);
        *dc = fix_div(fix_mul(nc, nw) - *c, d);
        *c += fix_mul(*dc, f);
    }
}

void gri_x_edge(TI *ti, grs_vertex **pn, grs_vertex **pp, int side)
{
    grs_vertex *n = *pn, *p = *pp;
    x_part(edge_of(ti, side), n, p, n->y - p->y);
}

void gri_ix_edge(TI *ti, grs_vertex **pn, grs_vertex **pp, int side)
{
    grs_vertex *n = *pn, *p = *pp;
    TE *e = edge_of(ti, side);
    fix d = n->y - p->y, f = x_part(e, n, p, d);
    lin_comp(&e->i, &e->di, p->i, n->i, d, f);
}

/* r, g, b in the vertex u, v, w */
void gri_rgbx_edge(TI *ti, grs_vertex **pn, grs_vertex **pp, int side)
{
    grs_vertex *n = *pn, *p = *pp;
    TE *e = edge_of(ti, side);
    fix d = n->y - p->y, f = x_part(e, n, p, d);
    lin_comp(&e->u, &e->du, p->u, n->u, d, f);
    lin_comp(&e->v, &e->dv, p->v, n->v, d, f);
    lin_comp(&e->i, &e->di, p->w, n->w, d, f);
}

/* Scaler: both edges vertical (left x = prev.x, right x = next.x, dx 0), u fixed per edge,
 * v stepped down the left edge (and copied to the right). */
void gri_scale_edge(TI *ti, grs_vertex **pn, grs_vertex **pp)
{
    grs_vertex *p = *pp, *n = *pn;
    fix f;

    ti->left.u = p->u;
    ti->left.x = p->x;
    ti->right.u = n->u;
    ti->right.dx = 0;
    ti->right.x = n->x;
    ti->left.dx = ti->right.dx;
    ti->left.v = p->v;
    ti->right.y = ti->left.y = cint(n->y);
    f = FIX_CEIL(p->y) - p->y;
    ti->right.dv = ti->left.dv = fix_div(n->v - p->v, n->y - p->y);
    ti->left.v += fix_mul(ti->left.dv, f);
}

/* Left edge of a linear mapper whose u, v gradients sit in the right edge slots
 * (right.u = du/dx, right.v = dv/dx, right.du = du/dy, right.dv = dv/dy). */
void gri_uvx_grad_edge(TI *ti, grs_vertex **pn, grs_vertex **pp)
{
    grs_vertex *p = *pp, *n = *pn;
    fix fy, fx;

    ti->left.x = p->x;
    ti->left.y = cint(n->y);
    ti->left.dx = fix_div(n->x - ti->left.x, n->y - p->y);
    fy = FIX_CEIL(p->y) - p->y;
    ti->left.x += fix_mul(fy, ti->left.dx);
    fx = FIX_CEIL(ti->left.x) - p->x;
    ti->left.u = p->u + (fix_mul(fx, ti->right.u) + fix_mul(fy, ti->right.du));
    ti->left.v = p->v + (fix_mul(fx, ti->right.v) + fix_mul(fy, ti->right.dv));
    ti->left.du = (ti->left.dx >> 16) * ti->right.u + ti->right.du;
    ti->left.dv = (ti->left.dx >> 16) * ti->right.v + ti->right.dv;
}

void gri_uvix_grad_edge(TI *ti, grs_vertex **pn, grs_vertex **pp)
{
    grs_vertex *p = *pp, *n = *pn;
    fix d, fy, fx;

    d = n->y - p->y;
    ti->left.x = p->x;
    ti->left.y = cint(n->y);
    ti->left.dx = fix_div(n->x - ti->left.x, d);
    fy = FIX_CEIL(p->y) - p->y;
    ti->left.x += fix_mul(fy, ti->left.dx);
    lin_comp(&ti->left.i, &ti->left.di, p->i, n->i, d, fy);
    fx = FIX_CEIL(ti->left.x) - p->x;
    ti->left.u = p->u + (fix_mul(fx, ti->right.u) + fix_mul(fy, ti->right.du));
    ti->left.v = p->v + (fix_mul(fx, ti->right.v) + fix_mul(fy, ti->right.dv));
    ti->left.du = (ti->left.dx >> 16) * ti->right.u + ti->right.du;
    ti->left.dv = (ti->left.dx >> 16) * ti->right.v + ti->right.dv;
}

void gri_uvx_edge(TI *ti, grs_vertex **pn, grs_vertex **pp, int side)
{
    grs_vertex *n = *pn, *p = *pp;
    TE *e = edge_of(ti, side);
    fix d = n->y - p->y, f = x_part(e, n, p, d);
    lin_comp(&e->u, &e->du, p->u, n->u, d, f);
    lin_comp(&e->v, &e->dv, p->v, n->v, d, f);
}

void gri_uvix_edge(TI *ti, grs_vertex **pn, grs_vertex **pp, int side)
{
    grs_vertex *n = *pn, *p = *pp;
    TE *e = edge_of(ti, side);
    fix d = n->y - p->y, f = x_part(e, n, p, d);
    lin_comp(&e->u, &e->du, p->u, n->u, d, f);
    lin_comp(&e->v, &e->dv, p->v, n->v, d, f);
    lin_comp(&e->i, &e->di, p->i, n->i, d, f);
}

void gri_uvwx_edge(TI *ti, grs_vertex **pn, grs_vertex **pp, int side)
{
    grs_vertex *n = *pn, *p = *pp;
    TE *e = edge_of(ti, side);
    fix d = n->y - p->y, f = x_part(e, n, p, d);
    w_comp(ti, &e->u, &e->du, p->u, p->w, n->u, n->w, d, f);
    w_comp(ti, &e->v, &e->dv, p->v, p->w, n->v, n->w, d, f);
}

void gri_uviwx_edge(TI *ti, grs_vertex **pn, grs_vertex **pp, int side)
{
    grs_vertex *n = *pn, *p = *pp;
    TE *e = edge_of(ti, side);
    fix d = n->y - p->y, f = x_part(e, n, p, d);
    w_comp(ti, &e->u, &e->du, p->u, p->w, n->u, n->w, d, f);
    w_comp(ti, &e->v, &e->dv, p->v, p->w, n->v, n->w, d, f);
    w_comp(ti, &e->i, &e->di, p->i, p->w, n->i, n->w, d, f);
}

/* y along a rightward edge (vertical scanning); the step goes in e->dx */
static fix y_part(TE *e, grs_vertex *n, grs_vertex *p, fix d)
{
    fix f;
    e->y = p->y;
    e->x = cint(n->x);
    e->dx = fix_div(n->y - e->y, d);
    f = FIX_CEIL(p->x) - p->x;
    e->y += fix_mul(f, e->dx);
    return f;
}

void gri_uvwy_edge(TI *ti, grs_vertex **pn, grs_vertex **pp, int side)
{
    grs_vertex *n = *pn, *p = *pp;
    TE *e = edge_of(ti, side);
    fix d = n->x - p->x, f = y_part(e, n, p, d);
    w_comp(ti, &e->u, &e->du, p->u, p->w, n->u, n->w, d, f);
    w_comp(ti, &e->v, &e->dv, p->v, p->w, n->v, n->w, d, f);
}

void gri_uviwy_edge(TI *ti, grs_vertex **pn, grs_vertex **pp, int side)
{
    grs_vertex *n = *pn, *p = *pp;
    TE *e = edge_of(ti, side);
    fix d = n->x - p->x, f = y_part(e, n, p, d);
    w_comp(ti, &e->u, &e->du, p->u, p->w, n->u, n->w, d, f);
    w_comp(ti, &e->v, &e->dv, p->v, p->w, n->v, n->w, d, f);
    w_comp(ti, &e->i, &e->di, p->i, p->w, n->i, n->w, d, f);
}
