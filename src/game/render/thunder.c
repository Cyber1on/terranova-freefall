/* thunder.c -- Terra Nova module M094 (0x2B2BF0-0x2B397F; r:\prj\tn\v109\tn\src\tn\thunder.c):
 * lightning bolts.  Decompiled from __FF.EXE v1.09.
 *
 *   0x2B2BF0 thunder_2B2BF0 reset the pools       0x2B2C38 bolt_find_free (no callers)
 *   0x2B2C60 bolt_seg_alloc                       0x2B2CB8 bolt_pt_alloc
 *   0x2B2D10 bolt_pt_release (no callers)         0x2B2D3C bolt_seg_release
 *   0x2B2DBC thunder_2B2DBC free a bolt           0x2B2E34 bolt_seg_unlink
 *   0x2B2E80 bolt_seg_extend                      0x2B2ED4 bolt_seg_new
 *   0x2B2F04 bolt_seg_branch                      0x2B2F44 bolt_dir_set (no callers)
 *   0x2B2F78 bolt_dir_normalize                   0x2B300C bolt_dir_jitter
 *   0x2B3098 bolt_seg_step                        0x2B3170 bolt_seg_life
 *   0x2B31DC bolt_branch                          0x2B3244 bolt_pt_underground
 *   0x2B32BC thunder_2B32BC set scale / step      0x2B32C8 bolt_grow
 *   0x2B33E4 thunder_2B33E4 grow n steps          0x2B3404 thunder_2B3404 new bolt
 *   0x2B3628 bolt_draw_line (callback)            0x2B365C bolt_draw_wide
 *   0x2B36A4 bolt_walk                            0x2B372C bolt_draw_simple
 *   0x2B3788 bolt_draw_far (callback)             0x2B3824 bolt_draw_near (callback)
 *   0x2B3898 thunder_2B3898 draw a bolt
 *
 * A bolt (slot 1..7, bolt_head[b] = first segment) starts as one segment from a point `from`
 * aimed at `to`; each growth step (bolt_grow) adds a point to every segment that has not
 * reached its life length, bolt_step further along its current direction with a random jitter
 * (bolt_dir_jitter: +-0.25 per axis before normalising); a segment whose new point is under
 * the terrain stops, and with a probability that rises with the steps since its last branch a
 * segment forks a new branch (generation + 1, shorter life: bolt_seg_life) from its tip.
 * New segments go to the head of the bolt's list.  Drawing walks every segment from tip to base
 * and draws each piece with the drawobj 3D line functions (drawobj_29CFC4 thin, drawobj_29D2B0
 * wide when the view is closer than 30 units), skipping pieces whose end is hidden behind the
 * terrain silhouette (ybuf).  The colours come from two fade tables indexed by the bolt's age.
 *
 * Quirks kept:
 *  - bolt_seg_alloc never hands out segment 0xFF, bolt_pt_alloc never point 0x1FF.
 *  - bolt_seg_unlink loops for ever when the segment is not in the bolt's list.
 *  - bolt_walk skips the branches when the branch colour is 0x2C.
 *  - bolt_draw_simple adds 0xF to the fade table colours, thunder_2B3898 does not.
 *  - thunder_2B3404's failure path frees the bolt slot twice (inline, then thunder_2B2DBC).
 *  - An age below 0 indexes the fade tables past their end (as the original).
 */
#include "g6_a.h"
#include "g5_a.h"

#define THUNDER_FILE "r:\\prj\\tn\\v109\\tn\\src\\tn\\thunder.c"

void thunder_2B2BF0(void)
{
    bolt_pt_next = 0;
    bolt_seg_next = 1;
    bolt_seg_free = 0;
    bolt_pt_free = -1;
    bolt_seg_nfree = 0;
    bolt_pt_nfree = 0;
    lg_memset(bolt_head, 0, 1, 8);
}

long bolt_find_free(void)
{
    long b;
    for (b = 1; b < 8; b++)
        if (bolt_head[b] == 0)
            return b;
    return 0;
}

long bolt_seg_alloc(void)
{
    long s = bolt_seg_free;
    if (s != 0) {
        bolt_seg_free = bolt_seg[s].next;
        bolt_seg_nfree--;
        return s;
    }
    if (bolt_seg_next + 1 == 0x100)
        return 0;
    return bolt_seg_next++;
}

long bolt_pt_alloc(void)
{
    long p = bolt_pt_free;
    if (p != -1) {
        bolt_pt_free = bolt_pt[p].link;
        bolt_pt_nfree--;
        return p;
    }
    if (bolt_pt_next + 1 == 0x200)
        return p;                       /* -1 */
    return bolt_pt_next++;
}

void bolt_pt_release(long p)
{
    bolt_pt[p].link = (short)bolt_pt_free;
    bolt_pt_nfree++;
    bolt_pt_free = p;
}

/* frees the segment and its points from the tip back to (not including) the base */
void bolt_seg_release(long s)
{
    BoltSeg *g = &bolt_seg[s];
    long nfree = bolt_pt_nfree;
    long p = g->tip;
    bolt_seg_nfree++;
    while (p != g->base) {
        short old = (short)bolt_pt_free;
        long q = p;
        nfree++;
        bolt_pt_free = q;
        p = bolt_pt[p].link;
        bolt_pt[q].link = old;
    }
    g->next = (uchar)bolt_seg_free;
    bolt_seg_free = s;
    bolt_pt_nfree = nfree;
}

void thunder_2B2DBC(long b)
{
    long s = bolt_head[b], p;
    while (s != 0) {
        long t = s;
        s = bolt_seg[s].next;
        bolt_seg_release(t);
    }
    p = bolt_root[b];
    if (p != -1) {
        bolt_pt[p].link = (short)bolt_pt_free;
        bolt_pt_nfree++;
        bolt_pt_free = p;
    }
    bolt_head[b] = 0;
    bolt_root[b] = -1;
}

void bolt_seg_unlink(long b, long s)
{
    long prev = bolt_head[b];
    if (prev == s)
        bolt_head[b] = bolt_seg[s].next;
    else {
        while (bolt_seg[prev].next != s)
            prev = bolt_seg[prev].next;
        bolt_seg[prev].next = bolt_seg[s].next;
    }
    bolt_seg_release(s);
}

long bolt_seg_extend(long s)
{
    long p = bolt_pt_alloc(), tip;
    if (p == -1)
        return p;
    tip = bolt_seg[s].tip;
    bolt_pt[p].seg = (uchar)s;
    bolt_pt[p].link = (short)tip;
    if (tip != -1)
        bolt_pt[p].count = bolt_pt[tip].count + 1;
    bolt_seg[s].tip = (short)p;
    return p;
}

long bolt_seg_new(long b)
{
    long s = bolt_seg_alloc();
    if (s != 0) {
        bolt_seg[s].cnt = 0;
        bolt_seg[s].next = bolt_head[b];
        bolt_head[b] = (uchar)s;
    }
    return s;
}

long bolt_seg_branch(long b, long parent)
{
    long s = bolt_seg_new(b);
    if (s != 0) {
        short t = bolt_seg[parent].tip;
        bolt_seg[s].base = t;
        bolt_seg[s].tip = t;
        bolt_seg[s].gen = bolt_seg[parent].gen + 1;
    }
    return s;
}

void bolt_dir_set(fix *a, fix *b)
{
    bolt_dir[0] = b[0] - a[0];
    bolt_dir[1] = b[1] - a[1];
    bolt_dir[2] = b[2] - a[2];
}

void bolt_dir_normalize(void)
{
    fix d = fix_safe_pyth_dist(bolt_dir[0], bolt_dir[1], THUNDER_FILE, 0x15D), k;
    d = fix_safe_pyth_dist(d, bolt_dir[2], THUNDER_FILE, 0x15D);
    bolt_dist = d;
    if (d < 0x10)
        k = 0x10000;
    else
        k = fix_div(0x10000, d);
    bolt_dir[0] = fix_mul(bolt_dir[0], k);
    bolt_dir[1] = fix_mul(bolt_dir[1], k);
    bolt_dir[2] = fix_mul(bolt_dir[2], k);
}

void bolt_dir_jitter(void)
{
    long i;
    for (i = 0; i < 3; i++)
        bolt_dir[i] += ((RndRange(game_rnd_text, 0, 0x7FFF) & 0x1FF) << 6) - 0x4000;
}

/* adds a point to segment s: from the segment's tip (or `to` when it has none) along the
 * direction from -> to (jittered), len further */
long bolt_seg_step(long s, long from, long to, fix len, uchar jitter)
{
    long tip = bolt_seg[s].tip;
    long p = bolt_seg_extend(s);
    BoltPt *a, *q;
    if (p == -1)
        return p;
    a = &bolt_pt[from];
    q = &bolt_pt[to];
    bolt_dir[0] = q->x - a->x;
    bolt_dir[1] = q->y - a->y;
    bolt_dir[2] = q->z - a->z;
    bolt_dir_normalize();
    if (jitter) {
        bolt_dir_jitter();
        bolt_dir_normalize();
    }
    if (tip == -1)
        tip = to;
    bolt_pt[p].x = fix_mul(bolt_dir[0], len) + bolt_pt[tip].x;
    bolt_pt[p].y = fix_mul(bolt_dir[1], len) + bolt_pt[tip].y;
    bolt_pt[p].z = fix_mul(bolt_dir[2], len) + bolt_pt[tip].z;
    return p;
}

long bolt_seg_life(long s)
{
    long len = bolt_len, lo, r;
    uchar gen = bolt_seg[s].gen;
    if (gen > 1)
        len >>= (gen - 1) & 31;
    lo = len / 2;
    if (lo < 3)
        lo = 3;
    if (len < lo)
        len = lo + 1;
    r = RndRange(game_rnd_text, 0, 0x7FFF);
    return (r >> 2) % (len - lo + 1) + lo;
}

long bolt_branch(long b, long parent, fix len)
{
    long s = bolt_seg_branch(b, parent), tip;
    if (s == 0)
        return 0;
    tip = bolt_seg[parent].tip;
    bolt_seg[s].life = (uchar)bolt_seg_life(parent);
    if (bolt_seg_step(s, bolt_pt[tip].link, tip, len, 1) == -1) {
        bolt_seg_unlink(b, s);
        return 0;
    }
    return s;
}

uchar bolt_pt_underground(long p)
{
    uchar *m = game_map_35BFB4;
    fix ox = (fix)(*(short *)(m + 8)) << 16;
    fix oy = (fix)(*(short *)(m + 0xA)) << 16;
    fix x = (bolt_pt[p].x >> 2) + ox;
    fix y = (bolt_pt[p].y >> 2) + oy;
    fix h;
    if (x > 0 && y > 0 && x < 0x1000000 && y < 0x1000000)
        h = map_2949C4(bolt_pt[p].x, bolt_pt[p].y);
    else
        h = 0;
    return h > bolt_pt[p].z;
}

void thunder_2B32BC(fix scale, fix step)
{
    bolt_scale = scale;
    bolt_step = step;
}

uchar bolt_grow(long b)
{
    long s = bolt_head[b];
    uchar done = 1;
    while (s != 0) {
        BoltSeg *g = &bolt_seg[s];
        long tip = g->tip;
        long main = g->gen == 0;
        if (g->life > bolt_pt[tip].count) {
            long p = bolt_seg_step(s, g->base, tip, bolt_step, 1);
            if (p == -1)
                return 1;
            done = 0;
            if (bolt_pt_underground(p))
                bolt_pt[p].count = g->life;
            else {
                long lim = (g->cnt >> (main ? 0 : 1)) + 20 - (main ? 0 : 10);
                if ((RndRange(game_rnd_text, 0, 0x7FFF) & 0x7F) < lim) {
                    bolt_branch(b, s, bolt_scale);
                    g->cnt = 0;
                } else
                    g->cnt++;
            }
        }
        s = bolt_seg[s].next;
    }
    return done;
}

uchar thunder_2B33E4(long b, long n)
{
    while (n-- != 0)
        if (bolt_grow(b))
            return 1;
    return 0;
}

long thunder_2B3404(fix *from, fix *to)
{
    long b = bolt_find_free(), p, s, q, k, len;
    if (b == 0)
        return 0;
    p = bolt_pt_alloc();
    s = bolt_seg_alloc();
    if (s != 0) {
        bolt_seg[s].tip = -1;
        bolt_seg[s].base = -1;
        bolt_seg[s].cnt = 0;
    }
    if (p == -1 || s == 0) {
        bolt_head[b] = 0;
        bolt_root[b] = -1;
        if (p != -1) {
            bolt_pt[p].link = (short)bolt_pt_free;
            bolt_pt_nfree++;
            bolt_pt_free = p;
        }
        if (s != 0)
            bolt_seg_release(s);
        thunder_2B2DBC(b);
        return 0;
    }
    bolt_pt[p].x = from[0];
    bolt_pt[p].y = from[1];
    bolt_pt[p].z = from[2];
    bolt_seg[s].tip = (short)p;
    bolt_pt[p].count = 0;
    bolt_seg[s].base = (short)p;
    bolt_pt[p].link = -1;
    bolt_seg[s].gen = 0;
    bolt_dir[0] = to[0] - from[0];
    bolt_dir[1] = to[1] - from[1];
    bolt_dir[2] = to[2] - from[2];
    bolt_dir_normalize();
    len = bolt_dist / bolt_step;
    len = len * 3 / 2;
    if (len < 10)
        len = 10;
    if (len > 0xFF)
        len = 0xFF;
    bolt_len = len;
    bolt_seg[s].life = (uchar)len;
    bolt_head[b] = (uchar)s;
    bolt_seg[s].next = 0;
    bolt_root[b] = (short)p;
    q = bolt_seg_extend(s);
    if (q == -1) {
        thunder_2B2DBC(b);
        return 0;
    }
    bolt_pt[q].x = fix_mul(bolt_dir[0], bolt_scale) + from[0];
    bolt_pt[q].y = fix_mul(bolt_dir[1], bolt_scale) + from[1];
    bolt_pt[q].z = fix_mul(bolt_dir[2], bolt_scale) + from[2];
    for (k = 0; k < 4; k++)
        if (bolt_grow(b))
            break;
    return b;
}

/* ---------------------------------------------------------------- drawing */

void bolt_draw_line(BoltPt *a, BoltPt *b, uchar gen)
{
    uchar c = gen ? bolt_col_branch : bolt_col_main;
    drawobj_29CFC4(a->x, a->y, a->z, b->x, b->y, b->z, c);
}

void bolt_draw_wide(BoltPt *a, BoltPt *b, uchar gen)
{
    uchar c = gen ? bolt_col_branch : bolt_col_main;
    fix w = gen ? bolt_width_branch : bolt_width_main;
    drawobj_29D2B0(a->x, a->y, a->z, b->x, b->y, b->z, w, c);
}

void bolt_walk(long b, BoltDrawFn fn)
{
    long s = bolt_head[b];
    while (s != 0) {
        uchar gen = bolt_seg[s].gen;
        if (!gen || bolt_col_branch != 0x2C) {
            long p = bolt_seg[s].tip, q;
            do {
                q = p;
                p = bolt_pt[q].link;
                WATCOM_CALL(fn, (long)&bolt_pt[p], (long)&bolt_pt[q], gen);
            } while (p != bolt_seg[s].base);
        }
        s = bolt_seg[s].next;
    }
}

/* colour index by age: 0x20 - age * 32 / 1024, not below 0 (0x20 when not close) */
static long bolt_age_index(long close, long age)
{
    long i;
    if (close == 0)
        return 0x20;
    i = 0x20 - (age << 5) / 1024;
    if (i < 0)
        i = 0;
    return i;
}

void bolt_draw_simple(long b, long close, long age)
{
    long i = bolt_age_index(close, age);
    bolt_col_main = bolt_fade_main[i] + 0xF;
    bolt_col_branch = bolt_fade_branch[i] + 0xF;
    bolt_walk(b, (BoltDrawFn)WATCOM_FN(bolt_draw_line));
}

/* the piece is skipped when its end projects inside the canvas below the terrain top */
static uchar bolt_hidden(BoltPt *b)
{
    long sx, sy, x;
    if (!drawobj_29DA60(b->x, b->y, b->z, NULL, NULL, NULL, &sx, &sy) || sx < 0)
        return 0;
    x = sx >> 16;
    if (x >= grd_canvas->bm.w)
        return 0;
    return (sy >> 16) > bolt_ybuf[x];
}

void bolt_draw_far(BoltPt *a, BoltPt *b, uchar gen)
{
    uchar c = gen ? bolt_col_branch : bolt_col_main;
    if (!bolt_hidden(b))
        drawobj_29CFC4(a->x, a->y, a->z, b->x, b->y, b->z, c);
}

void bolt_draw_near(BoltPt *a, BoltPt *b, uchar gen)
{
    if (!bolt_hidden(b))
        bolt_draw_wide(a, b, gen);
}

void thunder_2B3898(long bolt, long close, ulong age, long *ybuf)
{
    long saved = 0, i;
    BoltPt *r;
    fix d;

    bolt_ybuf = ybuf;
    if (grd_canvas->gc.fill_type != 0) {
        saved = grd_canvas->gc.fill_parm;
        grd_canvas->gc.fill_parm = (long)planet_tab3e;
    }
    i = bolt_age_index(close, (long)age);
    bolt_col_branch = bolt_fade_branch[i];
    bolt_width_branch = 0x2492;
    r = &bolt_pt[bolt_seg[bolt_head[bolt]].base];
    bolt_col_main = bolt_fade_main[i];
    bolt_width_main = 0x4000;
    d = fix_safe_pyth_dist(ff_view.x - r->x, ff_view.y - r->y, THUNDER_FILE, 0x2CD);
    if (d < 0x1E0000)
        bolt_walk(bolt, (BoltDrawFn)WATCOM_FN(bolt_draw_near));
    else
        bolt_walk(bolt, (BoltDrawFn)WATCOM_FN(bolt_draw_far));
    if (saved)
        grd_canvas->gc.fill_parm = saved;
}
