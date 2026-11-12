/* particle.c -- Terra Nova module M081 (0x2A3C30-0x2A63BF; source file name unknown): particle
 * systems (smoke, sparks, dust, debris clouds).  Decompiled from __FF.EXE v1.09.
 *
 *   0x2A3C30 particle_2A3C30 report a bad handle     0x2A3C70 particle_2A3C70 free the arrays
 *   0x2A3D10 particle_2A3D10 init                    0x2A3E5C particle_2A3E5C reset
 *   0x2A3EAC particle_2A3EAC free handle             0x2A3EF0 particle_2A3EF0 release handle
 *   0x2A3F08 particle_2A3F08 renumber handles        0x2A3F4C particle_2A3F4C new empty group
 *   0x2A3F94..0x2A402C setters (origin, velocity, draw/mode, scale, type, period)
 *   0x2A403C particle_2A403C delete group            0x2A4114 particle_2A4114 delete by handle
 *   0x2A414C particle_2A414C remove a particle       0x2A41C0 particle_2A41C0 group empty?
 *   0x2A420C particle_2A420C decimate                0x2A42C8 particle_2A42C8 new group of n
 *   0x2A43B0 particle_2A43B0 spawn particles         0x2A44FC particle_2A44FC age a group
 *   0x2A45A0 particle_2A45A0 damping power           0x2A45D8 / 0x2A4800 move particles
 *   0x2A4978 particle_2A4978 update all groups       0x2A4A38..0x2A4FC8 view matrix of a group
 *   0x2A504C / 0x2A5158 / 0x2A5254 projection        0x2A52EC / 0x2A538C draw points / pairs
 *   0x2A56A8 particle_2A56A8 draw a group            0x2A5798 / 0x2A5848 / 0x2A5BB4 pixels
 *   0x2A5914 particle_2A5914 random "blocks a ray"   0x2A59DC particle_2A59DC draw one particle
 *   0x2A5BF4 particle_2A5BF4 draw a line             0x2A5C8C particle_2A5C8C gravity/damping
 *   0x2A5CF8 particle_2A5CF8 update a group          0x2A6000 particle_2A6000 spread along a vector
 *   0x2A6134 particle_2A6134 group from descriptor   0x2A62DC particle_2A62DC respawn callback
 *
 * A group has an origin (PtcGroup, fix world position, a scale and how it is drawn), an
 * orientation (PtcAngles) and a motion record (PtcMotion: origin velocity, type, the step clock).
 * Particles are in group units (short) with byte velocities; time advances in steps of
 * motion.period quarter-ticks.  Types (ptc_types) select gravity, damping, spin and how
 * particles die; descriptors (ptc_descs) are the presets sfx.c spawns.
 *
 * Quirks kept:
 *  - particle_2A3C70 frees the motion array a second time when the handle array exists, never
 *    frees the handle array and leaves its pointer set.
 *  - particle_2A6134 writes handles[-1] when every handle is in use.
 *  - particle_2A538C passes the group's first particle (not the pair) to particle_2A5BF4, so
 *    lines take that particle's colour; its mode 3 (unused: modes are 0/1) uses the x formula
 *    for every coordinate and two never-written locals as w0/w1 (here 0 and 0).
 *  - particle_2A59DC / particle_2A5BF4 draw type 2 re-dispatch on the particle's kind, which
 *    loops forever when kind is 2.
 *  - particle_2A5CF8 adds the spin of axis 1/2/3 to a, b and c (all three tests are made).
 */
#include "g6_c.h"

#ifndef TN_WATCOM_ABI
/* the sharp view (port/video/sharp3d.c): a pixel of the game's view is k x k pixels there */
#define PTC_PIXEL(c, x, y) do { \
        if (port_s3d_on) port_s3d_block((c), (x), (y), 0); else gen_fill_pixel((c), (x), (y)); \
    } while (0)
#else
#define PTC_PIXEL gen_fill_pixel
#endif

void particle_2A3C30(long h)
{
    if (h >= 0 && h < ptc_max_groups)
        DbgReportError(1, ptc_msg_bad_handle2, h, ptc_handles[h]);
    else
        DbgReportError(1, ptc_msg_bad_handle1, h);
}

void particle_2A3C70(void)
{
    if (ptc_parts) Free(ptc_parts);
    if (ptc_groups) Free(ptc_groups);
    if (ptc_angles) Free(ptc_angles);
    if (ptc_motion) Free(ptc_motion);
    if (ptc_handles) Free(ptc_motion);          /* sic: motion again, handles never freed */
    if (ptc_start) Free(ptc_start);
    ptc_parts = NULL;
    ptc_groups = NULL;
    ptc_angles = NULL;
    ptc_motion = NULL;
    ptc_start = NULL;
    ptc_max_groups = 0;
    ptc_ngroups = 0;
}

void particle_2A3D10(long max_parts, long max_groups)
{
    long i;
    if (ptc_parts) {
        particle_2A3C70();
    } else {
        ptc_rnd1.f_Seed(&ptc_rnd1, 1);
        ptc_rnd2.f_Seed(&ptc_rnd2, 1);
    }
    particle_2A5C8C(0x20, 0xFF00);
    ptc_ngroups = 0;
    ptc_nparts = 0;
    ptc_max_parts = 0;
    ptc_max_groups = 0;
    ptc_parts = Malloc(max_parts * 12);
    ptc_groups = Malloc(max_groups << 4);
    ptc_angles = Malloc(max_groups * 8);
    ptc_motion = Malloc(max_groups << 4);
    ptc_handles = Malloc(max_groups * 4);
    ptc_start = Malloc(max_groups * 4 + 4);
    if (ptc_parts && ptc_groups && ptc_angles && ptc_motion && ptc_start) {
        ptc_max_parts = max_parts;
        ptc_start[0] = 0;
        for (i = 0; i < max_groups; i++)
            ptc_handles[i] = -1;
        ptc_max_groups = max_groups;
    }
}

void particle_2A3E5C(void)
{
    long i;
    ptc_ngroups = 0;
    ptc_nparts = 0;
    ptc_start[0] = 0;
    for (i = 0; i < ptc_max_groups; i++)
        ptc_handles[i] = -1;
}

long particle_2A3EAC(void)
{
    long i;
    for (i = 0; i < ptc_max_groups; i++)
        if (ptc_handles[i] == -1) return i;
    return -1;
}

void particle_2A3EF0(long h)
{
    ptc_handles[h] = -1;
}

void particle_2A3F08(long g)
{
    long i;
    for (i = 0; i < ptc_max_groups; i++)
        if (ptc_handles[i] > g) ptc_handles[i]--;
}

long particle_2A3F4C(void)
{
    long n = ptc_ngroups;
    if (n >= ptc_max_groups) return -1;
    ptc_start[n + 1] = ptc_start[n];
    ptc_ngroups = n + 1;
    return n;
}

void particle_2A3F94(long g, fix x, fix y, fix z)
{
    ptc_groups[g].x = x;
    ptc_groups[g].y = y;
    ptc_groups[g].z = z;
}

void particle_2A3FAC(long g, fix vx, fix vy, fix vz)
{
    ptc_motion[g].vx = vx;
    ptc_motion[g].vy = vy;
    ptc_motion[g].vz = vz;
}

void particle_2A3FC4(long g, fix vx, fix vy, fix vz)
{
    ptc_motion[g].vx += vx;
    ptc_motion[g].vy += vy;
    ptc_motion[g].vz += vz;
}

void particle_2A3FF0(long g, long draw, long mode)
{
    ptc_groups[g].draw = (uchar)draw;
    ptc_groups[g].mode = (uchar)mode;
}

void particle_2A4008(long g, long scale)
{
    ptc_groups[g].scale = (ushort)scale;
}

void particle_2A401C(long g, long type)
{
    ptc_motion[g].type = (uchar)type;
}

void particle_2A402C(long g, long period)
{
    ptc_motion[g].period = (uchar)period;
}

void particle_2A403C(long g)
{
    long i, n;
    for (i = 0; i < ptc_max_groups; i++)          /* particle_2A3F08 inline */
        if (ptc_handles[i] > g) ptc_handles[i]--;
    n = --ptc_ngroups - g;
    memmove(&ptc_groups[g], &ptc_groups[g + 1], n << 4);
    memmove(&ptc_angles[g], &ptc_angles[g + 1], (ptc_ngroups - g) << 3);
    memmove(&ptc_motion[g], &ptc_motion[g + 1], (ptc_ngroups - g) << 4);
    memmove(&ptc_start[g], &ptc_start[g + 1], ((ptc_ngroups - g) << 2) + 4);
}

void particle_2A4114(long h)
{
    if (h < 0 || ptc_handles[h] < 0) particle_2A3C30(h);
    particle_2A403C(ptc_handles[h]);
    ptc_handles[h] = -1;
}

/* Removes particle i of group g: every later group gives its last particle to fill the hole
 * left in the group before it, so all groups stay packed. */
long particle_2A414C(long g, long i)
{
    long k, last;
    for (k = g; k < ptc_ngroups; k++) {
        last = --ptc_start[k + 1];
        ptc_parts[i] = ptc_parts[last];
        i = last;
    }
    return ptc_start[g] == ptc_start[g + 1];
}

long particle_2A41C0(long h)
{
    long g;
    if (h < 0) return 1;
    if (ptc_handles[h] < 0) particle_2A3C30(h);
    g = ptc_handles[h];
    return ptc_start[g] == ptc_start[g + 1];
}

/* Keeps only the particles whose index has a bit of mask set (drops 1 in 2^shift - 1 ...). */
void particle_2A420C(long shift)
{
    long mask = (1 << (shift & 31)) - 2;
    long src = 0, dst = 0, k, end = ptc_start[1];
    for (k = 1; k <= ptc_ngroups; k++) {
        for (; src < end; src++)
            if (mask & src) ptc_parts[dst++] = ptc_parts[src];
        ptc_start[k] = dst;
#ifndef TN_WATCOM_ABI
        if (k == ptc_ngroups)           /* the original read one entry past the table (G12) */
            break;
#endif
        end = ptc_start[k + 1];
    }
    ptc_nparts = dst;
}

long particle_2A42C8(long n)
{
    long g = ptc_ngroups;
    PtcAngles *a;
    PtcMotion *m;
    if (g >= ptc_max_groups) return -1;
    ptc_start[g + 1] = ptc_start[g];
    ptc_ngroups = g + 1;
    ptc_nparts = ptc_start[ptc_ngroups];
    if (ptc_max_parts - ptc_nparts < n) {
        particle_2A420C(ptc_gc_shift);
        if (ptc_max_parts - ptc_nparts < n) n = ptc_max_parts - ptc_nparts;
    }
    ptc_start[g + 1] += n;
    a = &ptc_angles[g];
    a->grow = 0;
    a->c = 0;
    a->b = 0;
    a->a = 0;
    m = &ptc_motion[g];
    m->vz = 0;
    m->acc = 0;
    m->period = 8;
    m->type = 0;
    m->vy = 0;
    m->vx = 0;
    return g;
}

void particle_2A43B0(Particle *p, long n, long spread, long size, long z0, long z1,
                     long life0, long life1, long c0, long c1, long kind)
{
    long color = RndRange(&ptc_rnd1, c0, c1);
    long sz = size * (z1 - z0), hi = z1 * size, lo = z0 * size, xy = size * spread;
    while (n--) {
        p->x = (short)RndRange(&ptc_rnd2, -spread * size, xy);
        p->y = (short)RndRange(&ptc_rnd2, -spread * size, xy);
        if (z0 > 0 && z1 > 0)
            p->z = (short)RndRange(&ptc_rnd2, 0, sz);
        else
            p->z = (short)RndRange(&ptc_rnd2, lo, hi);
        p->vx = (schar)RndRange(&ptc_rnd2, -spread, spread);
        p->vy = (schar)RndRange(&ptc_rnd2, -spread, spread);
        p->vz = (schar)RndRange(&ptc_rnd2, z0, z1);
        p->life = (uchar)RndRange(life0 ? &ptc_rnd2 : &ptc_rnd1, life0, life1);
        p->color = (uchar)color;
        p->kind = (schar)kind;
        p++;
    }
}

/* Ages group g by dec steps; a particle that runs out of life (or, with below, goes under 0)
 * survives only when cb keeps it (respawned in place). */
void particle_2A44FC(long g, long dec, PtcKillFn cb, uchar below)
{
    long i = ptc_start[g], end = ptc_start[g + 1];
    Particle *p;
    while (i < end) {
        p = &ptc_parts[i];
        if ((long)p->life > dec && !(below && p->z < 0)) {
            p->life -= (uchar)dec;
            i++;
        } else if (cb && (uchar)WATCOM_CALL(cb, g, (long)&ptc_parts[i])) {
            i++;
        } else {
            particle_2A414C(g, i);
            end--;
        }
    }
}

fix particle_2A45A0(fix f, long n)
{
    if (n >= 8) f = fix_mul(f, f);
    if (n >= 4) f = fix_mul(f, f);
    if (n >= 2) f = fix_mul(f, f);
    return f;
}

static void ptc_damp(Particle *p, long n, fix k)
{
    long i;
    for (i = 0; i < n; i++, p++) {
        p->vx = (schar)fix_mul(k, p->vx);
        p->vy = (schar)fix_mul(k, p->vy);
        p->vz = (schar)fix_mul(k, p->vz);
    }
}

void particle_2A45D8(Particle *p, long n, fix grav, fix damp, long steps)
{
    long i;
    fix k;
    schar old, nw, d;
    if (damp != 0x10000) {
        k = particle_2A45A0(damp, steps);
        ptc_damp(p, n, k);
        if (grav != 0 && steps > 1) {
            k += damp;
            if (steps > 4) k += fix_mul(damp, damp);
            grav += fix_mul(k, grav);
        }
    } else {
        grav = grav * steps;
    }
    if (grav < 0) {
        d = (schar)((grav - 0xFF) / 256);
        for (i = 0; i < n; i++) {
            old = p[i].vz;
            nw = (schar)(old + d);
            if (old >= 0 || nw < 0) p[i].vz = nw;   /* never pushed through 0 */
        }
    } else if (grav > 0) {
        d = (schar)((grav + 0xFF) >> 8);
        for (i = 0; i < n; i++) {
            old = p[i].vz;
            nw = (schar)(old + d);
            if (old <= 0 || nw > 0) p[i].vz = nw;
        }
    }
    for (i = 0; i < n; i++, p++) {
        p->x += p->vx * steps;
        p->y += p->vy * steps;
        p->z += p->vz * steps;
    }
}

void particle_2A4800(Particle *p, long n, fix damp, long steps)
{
    long i;
    if (damp != 0x10000) ptc_damp(p, n, particle_2A45A0(damp, steps));
    for (i = 0; i < n; i++, p++) {
        p->vz += (schar)(p->kind * steps);
        p->x += p->vx * steps;
        p->y += p->vy * steps;
        p->z += p->vz * steps;
    }
}

void particle_2A4978(long dt)
{
    long q = dt >> 2, g, t, period, steps;
    PtcMotion *m;
    for (g = 0; g < ptc_ngroups; g++) {
        m = &ptc_motion[g];
        ptc_groups[g].x += m->vx;
        ptc_groups[g].y += m->vy;
        ptc_groups[g].z += m->vz;
        period = m->period;
        t = m->acc + q;
        if (t < period) {
            m->acc = (uchar)t;
            steps = 0;
        } else if (t < period * 2) {
            steps = 1;
            m->acc = (uchar)(t - period);
        } else {
            steps = t / period;
            m->acc = (uchar)(t - period * steps);
        }
        particle_2A5CF8(g, q, steps);
    }
}

void particle_2A4A38(fix *m, long h, long p, long b)
{
    fix sh, ch, sp, cp, sb, cb, spsb, spcb;
    fix_sincos((fixang)h, &sh, &ch);
    fix_sincos((fixang)p, &sp, &cp);
    fix_sincos((fixang)b, &sb, &cb);
    spsb = fix_mul(sp, sb);
    spcb = fix_mul(sp, cb);
    m[0] = fix_mul(ch, cb) + fix_mul(sh, spsb);
    m[1] = fix_mul(sh, cb) + fix_mul(ch, spsb);
    m[2] = -fix_mul(cp, sb);
    m[3] = -fix_mul(sh, cp);
    m[4] = fix_mul(ch, cp);
    m[5] = sp;
    m[6] = fix_mul(ch, sb) + fix_mul(sh, spcb);
    m[7] = fix_mul(sh, sb) + fix_mul(ch, spcb);
    m[8] = fix_mul(cp, cb);
}

fix particle_2A4B74(fix *a, fix *b)
{
    return fix_mul(a[0], b[0]) + fix_mul(a[1], b[1]) + fix_mul(a[2], b[2]);
}

void particle_2A4BA8(fix *dst, fix *a, fix *b)
{
    long r, j;
    for (r = 0; r < 3; r++)
        for (j = 0; j < 3; j++)
            dst[3 * r + j] = fix_mul(a[j], b[3 * r]) + fix_mul(a[3 + j], b[3 * r + 1])
                           + fix_mul(a[6 + j], b[3 * r + 2]);
}

long particle_2A4C98(fix *m, long g)
{
    PtcAngles *a = &ptc_angles[g];
    if (a->a == 0 && a->b == 0 && a->c == 0) return 0;
    particle_2A4A38(m, (ushort)a->c, (ushort)a->a, (ushort)a->b);
    return 1;
}

void particle_2A4CE8(fix *dst, long g, fix *v, fix *t)
{
    fix tmp[9];
    fix *pos = &ptc_groups[g].x;
    dst[9] = t[0] + particle_2A4B74(pos, v);
    dst[10] = t[1] + particle_2A4B74(pos, v + 3);
    dst[11] = t[2] + particle_2A4B74(pos, v + 6);
    if (particle_2A4C98(tmp, g))
        particle_2A4BA8(dst, tmp, v);
    else
        memcpy(dst, v, 0x24);
}

void particle_2A4D80(long g)
{
    long i;
    for (i = 0; i < 9; i++)
        ptc_mat[i] = fix_mul(ptc_mat[i], (fix)ptc_groups[g].scale << 8);
}

/* No callers: an orthographic variant of particle_2A4D80 (screen scale and centre folded in). */
void particle_2A4E70(long g)
{
    fix iz = fix_div(0x10000, ptc_mat[11]), iz2 = fix_div(0x10000, ptc_mat[11]), kx, ky;
    long i;
    kx = fix_mul(iz, (fix)ptc_groups[g].scale << 8);
    ky = fix_mul(iz2, (fix)ptc_groups[g].scale << 8);
    ptc_inv_z = fix_div(0x10000, ptc_mat[11]);
    for (i = 0; i < 3; i++) ptc_mat[i] = fix_mul(kx, ptc_mat[i]);
    for (i = 3; i < 6; i++) ptc_mat[i] = fix_mul(ky, ptc_mat[i]);
    ptc_mat[9] = fix_mul(iz, ptc_mat[9]) + ptc_cx / ptc_sx;
    ptc_mat[10] = fix_mul(iz2, ptc_mat[10]) + ptc_cy / ptc_sy;
}

long particle_2A4FC8(long g, fix *v, fix *t)
{
    fix r;
    particle_2A4CE8(ptc_mat, g, v, t);
    r = (v[6] < 0 ? -v[6] : v[6]) + (v[7] < 0 ? -v[7] : v[7]) + (v[8] < 0 ? -v[8] : v[8]);
    r = fix_mul(r, (fix)ptc_groups[g].scale << 8);
    particle_2A4D80(g);
    return r + (r >> 2) <= ptc_mat[11];
}

#define PTC_ROW(p, a, b, c, t) ((((p)->x * (a) + (p)->y * (b) + (p)->z * (c)) >> 8) + (t))

long particle_2A504C(Particle *p, long *sx, long *sy, fix *w)
{
    fix z = PTC_ROW(p, ptc_mat[6], ptc_mat[7], ptc_mat[8], ptc_mat[11]), k;
    if (z < ptc_near) return 1;
    *w = k = fix_div(0x10000, z);
    *sx = (fix_mul(PTC_ROW(p, ptc_mat[0], ptc_mat[1], ptc_mat[2], ptc_mat[9]), k) * ptc_sx + ptc_cx) >> 16;
    *sy = (fix_mul(PTC_ROW(p, ptc_mat[3], ptc_mat[4], ptc_mat[5], ptc_mat[10]), k) * ptc_sy + ptc_cy) >> 16;
    return 0;
}

void particle_2A5158(Particle *p, long *sx, long *sy, fix *w)
{
    fix z = PTC_ROW(p, ptc_mat[6], ptc_mat[7], ptc_mat[8], ptc_mat[11]), k;
    *w = k = fix_div(0x10000, z);
    *sx = (fix_mul(PTC_ROW(p, ptc_mat[0], ptc_mat[1], ptc_mat[2], ptc_mat[9]), k) * ptc_sx + ptc_cx) >> 16;
    *sy = (fix_mul(PTC_ROW(p, ptc_mat[3], ptc_mat[4], ptc_mat[5], ptc_mat[10]), k) * ptc_sy + ptc_cy) >> 16;
}

void particle_2A5254(Particle *p, long *sx, long *sy)
{
    *sx = (PTC_ROW(p, ptc_mat[0], ptc_mat[1], ptc_mat[2], ptc_mat[9]) * ptc_sx) >> 16;
    *sy = (PTC_ROW(p, ptc_mat[3], ptc_mat[4], ptc_mat[5], ptc_mat[10]) * ptc_sy) >> 16;
}

void particle_2A52EC(long type, Particle *p, long n, uchar unclipped)
{
    long sx, sy;
    fix w;
    if (unclipped == 0) {
        while (n--) {
            if (!particle_2A504C(p, &sx, &sy, &w)) particle_2A59DC(type, p, sx, sy, w);
            p++;
        }
    } else if (unclipped == 1) {
        while (n--) {
            particle_2A5158(p, &sx, &sy, &w);
            particle_2A59DC(type, p, sx, sy, w);
            p++;
        }
    } else if (unclipped == 3) {
        particle_2A5BB4(type, p, n);
    }
}

/* Lines from particle i (even) to a random odd particle r. */
void particle_2A538C(long type, Particle *p, long n, uchar unclipped)
{
    long half = n / 2, lim = n - 1, i = 0, r, x0, y0, x1, y1;
    fix w0, w1;
    if (unclipped == 0) {
        for (; i < lim; i += 2) {
            r = RndRange(game_rnd_text, 0, 0x7FFF) % half * 2 + 1;
            if (i <= r) i++;
            if (particle_2A504C(&p[i], &x0, &y0, &w0)) continue;
            if (particle_2A504C(&p[r], &x1, &y1, &w1)) continue;
            particle_2A5BF4(type, p, x0, y0, w0, x1, y1, w1);
        }
    } else if (unclipped == 1) {
        for (; i < lim; ) {
            r = RndRange(game_rnd_text, 0, 0x7FFF) % half * 2 + 1;
            if (i <= r) i++;
            particle_2A5158(&p[i], &x0, &y0, &w0);
            particle_2A5158(&p[r], &x1, &y1, &w1);
            i += 2;
            particle_2A5BF4(type, p, x0, y0, w0, x1, y1, w1);
        }
    } else if (unclipped == 3) {
        for (; i < lim; ) {
            r = RndRange(game_rnd_text, 0, 0x7FFF) % half * 2 + 1;
            if (r >= i) i++;
            x0 = (PTC_ROW(&p[i], ptc_mat[0], ptc_mat[1], ptc_mat[2], ptc_mat[9]) * ptc_sx) >> 16;
            x1 = (PTC_ROW(&p[r], ptc_mat[0], ptc_mat[1], ptc_mat[2], ptc_mat[9]) * ptc_sx) >> 16;
            i += 2;
            particle_2A5BF4(type, p, x0, x0, 0, x1, x1, 0);   /* sic (see the file header) */
        }
    }
}

void particle_2A56A8(long h, fix *v, fix *t, fix sx, fix sy, long cx, long cy, long shift)
{
    long g, vis, first, n;
    PtcGroup *grp;
    if (h < 0 || ptc_handles[h] < 0) particle_2A3C30(h);
    g = ptc_handles[h];
    grp = &ptc_groups[g];
    ptc_sy = sy;
    ptc_cx = cx << 16;
    ptc_cy = cy << 16;
    ptc_sx = sx;
    vis = (uchar)particle_2A4FC8(g, v, t);
    ptc_cur_group = g;
    first = ptc_start[g];
    n = (ptc_start[g + 1] - first) >> (shift & 31);
    if (grp->mode == 0)
        particle_2A52EC(grp->draw, &ptc_parts[first], n, (uchar)vis);
    else if (grp->mode == 1)
        particle_2A538C(grp->draw, &ptc_parts[first], n, (uchar)vis);
}

void particle_2A5798(Particle *p, long n)
{
    long x, y;
    while (n--) {
        x = (PTC_ROW(p, ptc_mat[0], ptc_mat[1], ptc_mat[2], ptc_mat[9]) * ptc_sx) >> 16;
        y = (PTC_ROW(p, ptc_mat[3], ptc_mat[4], ptc_mat[5], ptc_mat[10]) * ptc_sy) >> 16;
        PTC_PIXEL(p->color, (short)x, (short)y);
        p++;
    }
}

void particle_2A5848(Particle *p, long n, long color)
{
    long x, y;
    while (n--) {
        x = (PTC_ROW(p, ptc_mat[0], ptc_mat[1], ptc_mat[2], ptc_mat[9]) * ptc_sx) >> 16;
        y = (PTC_ROW(p, ptc_mat[3], ptc_mat[4], ptc_mat[5], ptc_mat[10]) * ptc_sy) >> 16;
        PTC_PIXEL(color, (short)x, (short)y);
        p++;
    }
}

/* Smoke-like groups (draw 0, 1, 8) block with a chance of a quarter of their particle count. */
uchar particle_2A5914(long h)
{
    long g = ptc_handles[h], draw = ptc_groups[g].draw;
    if (draw == 0 || draw == 1 || draw == 8) {
        if (RndRange(&ptc_rnd1, 0, 99) < (ptc_start[g + 1] - ptc_start[g]) >> 2) return 1;
    }
    return 0;
}

void particle_2A59DC(long type, Particle *p, long x, long y, fix w)
{
    long size = w * 50;
    for (;;) {
        switch ((ulong)type) {
        case 0:
            if (w > 0xA3D) rndsky_2A6AF8(x, y, size, p->color);
            else rndsky_2A6478(x, y, p->color);
            return;
        case 1:
            if (w > 0xA3D) rndsky_2A6AF8(x, y, size, ptc_motion[ptc_cur_group].color);
            else rndsky_2A6478(x, y, ptc_motion[ptc_cur_group].color);
            return;
        case 2:
            type = p->kind;
            if ((ulong)type <= 11) continue;
            break;
        case 5:
            rndsky_2A6478(x, y, p->color);
            return;
        case 6:
            rndsky_2A6478(x, y, ptc_motion[ptc_cur_group].color);
            return;
        case 7:
            PTC_PIXEL(p->color + ((p->kind * p->life) >> 8), (short)x, (short)y);
            return;
        case 8:
            if (w > 0x1999) rndsky_2A6AF8(x, y, w * 20, p->color);
            else rndsky_2A6478(x, y, p->color);
            return;
        case 9:
            if (w > 0x4000) rndsky_2A6AF8(x, y, w << 3, p->color);
            else rndsky_2A6478(x, y, p->color);
            return;
        case 10:
            if (w > 0x10000) rndsky_2A6AF8(x, y, w * 2, p->color);
            else rndsky_2A6478(x, y, p->color);
            return;
        case 11:
            rndsky_2A6B44(x, y, size, p->color);
            return;
        default:                /* 3, 4 and > 11 */
            break;
        }
        PTC_PIXEL(0xFE + (w & 1), (short)x, (short)y);
        return;
    }
}

void particle_2A5BB4(long type, Particle *p, long n)
{
    if (type == 0)
        particle_2A5798(p, n);
    else if (type == 1)
        particle_2A5848(p, n, ptc_motion[ptc_cur_group].color);
    else
        particle_2A5848(p, n, 0xFF);
}

void particle_2A5BF4(long type, Particle *p, long x0, long y0, fix w0, long x1, long y1, fix w1)
{
    for (;;) {
        if ((ulong)type == 0) {
            grd_canvas->gc.fcolor = p->color;
        } else if ((ulong)type == 1) {
            grd_canvas->gc.fcolor = ptc_motion[ptc_cur_group].color;
        } else if ((ulong)type == 2) {
            type = p->kind;
            continue;
        } else {
            grd_canvas->gc.fcolor = 0xFE + (w0 != w1);
        }
        break;
    }
    gr_iline((short)x0, (short)y0, (short)x1, (short)y1);
}

void particle_2A5C8C(fix grav, fix k)
{
    long i;
    ptc_gravity = grav;
    ptc_pow[0] = 0x10000;
    for (i = 1; i <= 32; i++)
        ptc_pow[i] = fix_mul(k, ptc_pow[i - 1]);
}

void particle_2A5CF8(long g, long dt, long steps)
{
    PtcType *T = &ptc_types[ptc_motion[g].type];
    long flags, q, n, axis, first, cnt;
    fix f, damp, grav;
    PtcAngles *a;
    PtcGroup *grp;
    PtcMotion *m;
    ptc_cur_group = g;
    flags = T->flags;
    q = dt >> 2;
    n = (flags & 0x80) ? steps : q;
    a = &ptc_angles[g];
    if (a->grow) {
        grp = &ptc_groups[g];
        grp->scale += a->grow * n;
        if (grp->scale > 0x14) grp->scale = 0x14;
    }
    axis = flags & 3;
    if (axis) {
        n <<= 4;
        if (axis == 1) a->a += (short)(n * T->spin);
        if (axis == 2) a->b += (short)(n * T->spin);
        if (axis == 3) a->c += (short)(n * T->spin);
    }
    m = &ptc_motion[g];
    if (flags & 4)
        m->vz += (T->grav * ptc_gravity * q) >> 8;
    if (flags & 8) {
        f = (T->damp * ptc_pow[q > 0x20 ? 0x20 : q]) >> 8;
        m->vx = fix_mul(m->vx, f);
        m->vy = fix_mul(m->vy, f);
        m->vz = fix_mul(m->vz, f);
    }
    if (steps == 0) return;
    damp = (flags & 0x20) ? (T->damp * ptc_pow[1]) / 256 : 0x10000;
    first = ptc_start[g];
    cnt = ptc_start[g + 1] - first;
    if (flags & 0x40) {
        particle_2A4800(&ptc_parts[first], cnt, damp, steps);
    } else {
        grav = (flags & 0x10) ? (T->grav * ptc_gravity) / 256 : 0;
        particle_2A45D8(&ptc_parts[first], cnt, grav, damp, steps);
    }
    particle_2A44FC(g, steps, ((flags >> 8) & 1) ? (PtcKillFn)WATCOM_FN(particle_2A62DC) : NULL,
                    ((flags >> 8) & 4) != 0);
}

/* Spreads the particles of h along (ax, ay, az): each moves by a random fraction 0..n. */
void particle_2A6000(long h, fix n, fix ax, fix ay, fix az)
{
    long g = ptc_handles[h], cnt, i;
    Particle *p = &ptc_parts[ptc_start[g]];
    fix inv, r;
    cnt = ptc_start[g + 1] - ptc_start[g];
    inv = fix_div(0x10000, ptc_groups[g].scale);
    ax = fix_mul(ax, inv);
    ay = fix_mul(ay, inv);
    az = fix_mul(az, inv);
    for (i = 0; i < cnt; i++, p++) {
        r = RndRange(&ptc_rnd1, 0, n);
        p->x += fix_mul(r, ax) >> 16;
        p->y += fix_mul(r, ay) >> 16;
        p->z += fix_mul(r, az) >> 16;
    }
}

long particle_2A6134(long idx, fix x, fix y, fix z)
{
    PtcDesc *D = &ptc_descs[idx];
    long g, h, i, life0, c0, c1, first;
    PtcGroup *grp;
    PtcMotion *m;
    g = particle_2A42C8(D->count >> ((uchar)ptc_desc_shift & 31));   /* mov cl, byte [shift] */
    if (g < 0) return g;
    h = -1;
    for (i = 0; i < ptc_max_groups; i++)
        if (ptc_handles[i] == -1) { h = i; break; }
    ptc_handles[h] = g;                         /* sic: handles[-1] when none is free */
    grp = &ptc_groups[g];
    grp->scale = (ushort)D->scale;
    grp->x = x;
    grp->y = y;
    grp->z = z;
    grp->draw = (uchar)D->draw;
    grp->mode = (uchar)D->mode;
    m = &ptc_motion[g];
    m->period = (uchar)D->period;
    m->type = (uchar)D->type;
    if (ptc_types[D->type].flags & 0x200) {
        life0 = 0;
    } else {
        life0 = D->life0;
        RndRange(&ptc_rnd1, 0, D->rnd);
        RndRange(&ptc_rnd1, 0, D->rnd);
    }
    if (D->draw == 7) {
        c0 = c1 = ptc_color7;
    } else {
        c0 = D->color0;
        c1 = D->color1;
    }
    first = ptc_start[g];
    particle_2A43B0(&ptc_parts[first], ptc_start[g + 1] - first, D->spread, D->size, D->z0, D->z1,
                    life0, D->life1, c0, c1, D->kind);
    return h;
}

uchar particle_2A62DC(long g, Particle *p)
{
    PtcDesc *D = &ptc_descs[ptc_types[ptc_motion[g].type].next];
    if (D->chance < 100 && ptc_start[g + 1] - ptc_start[g] < 10) return 0;
    if (RndRange(&ptc_rnd1, 0, 99) >= D->chance) return 0;
    particle_2A43B0(p, 1, D->spread, D->size, D->z0, D->z1, D->life0, D->life1,
                    D->color0, D->color1, D->kind);
    return 1;
}
