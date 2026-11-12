/* tnanim.c -- Terra Nova delta animation player (the LG "anim" player of System Shock's
 * AnimPlayRegion / ActAnim, DOS version; no source).  Decompiled from __FF.EXE.
 *
 *   0x2E66B0 AnimInit            0x2E66D4 AnimSetTimePtr      0x2E66E4 AnimShutdown
 *   0x2E66F0 anim_2E66F0 (AnimRecur)                          0x2E68A0 anim_2E68A0 (AnimPlay)
 *   0x2E695C anim_2E695C (AnimChangeRef)                      0x2E6A34 anim_2E6A34 (AnimKill)
 *   0x2E6A64 AnimKillAll         0x2E6AAC anim_2E6AAC (AnimSetVelocity)
 *   0x2E6AF8 AnimMoveBy          0x2E6B3C anim_2E6B3C (AnimSetNotify)
 *   0x2E6B48 anim_2E6B48 (AnimSetPalette)                     0x2E6B98 anim_2E6B98 (AnimLoadFrames)
 *   0x2E6C08 AnimGetSize         0x2E6C10 AnimRestart         0x2E6CC0 AnimFirstFrameRef
 *   0x2E6D54 AnimDoCodes         0x2E7044 AnimMove            0x2E7124 AnimShowFrame
 * (anim_XXXXXX names were given by batch T1, whose script code calls them; kept.)
 *
 * Every animation is an ActAnim node (0x80 bytes) in anim_list.  anim_2E66F0 (called each
 * frame) runs the op codes of every animation whose time has come: ops draw frames of an
 * RSD8 compound resource onto the anim's canvas (as a delta frame when the canvas is a flat8
 * bitmap of the same size), wait, loop, move, set the palette, and end or kill the anim.
 * Events are collected in ActAnim.pending + data and handed to two callbacks at the end of
 * the anim's turn.  Killed anims are freed (and their header unlocked) after the loop.
 *
 * Quirks kept: anim_2E68A0 frees the node but leaves the header locked when its size is 0;
 * AnimRestart always reads the time through anim_time_ptr (even when anim_use_time_ptr is
 * 0); anim_2E66F0 calls the notify callback without checking it for NULL; op codes above 12
 * are skipped as one byte by AnimDoCodes but by anim_op_len[op] (reading past the 13-entry
 * table) by AnimFirstFrameRef; AnimShowFrame leaves grd_canvas set to the anim's canvas.
 */
#include "t2_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
LlistHead anim_list;
short anim_kill_count;
ulong *anim_time_ptr;
uchar anim_use_time_ptr;
uchar anim_op_len[13] = { 4, 2, 1, 2, 4, 2, 0, 3, 12, 4, 3, 2, 1 };
#endif

#define FIRST(l) ((ActAnim *)(l).head.pnext)
#define TAIL(l) ((ActAnim *)&(l).tail)

/* round((v << 16) * scale) as the fixed-point code does it (32-bit intermediates) */
static long scale_time(long v, fix scale)
{
    long a = (long)((ulong)v << 16);

    a = (long)(((long long)a * scale) >> 16);
    return (a + 0x8000) >> 16;
}

void AnimInit(void)
{
    LlistInit(&anim_list, sizeof(ActAnim), 4);
    AtExit(WATCOM_FN(AnimShutdown));
}

void AnimSetTimePtr(ulong *p)
{
    anim_time_ptr = p;
    anim_use_time_ptr = TRUE;
}

void AnimShutdown(void)
{
    LlistDestroy(&anim_list);
}

/* Runs every animation (AnimRecur). */
void anim_2E66F0(void)
{
    ActAnim *pa, *next;
    AnimCodeData copy;
    ulong now;
    uchar pend;
    short f;

    if (FIRST(anim_list) == TAIL(anim_list))
        return;
    now = anim_use_time_ptr ? *anim_time_ptr : tn_ticks;
    for (pa = FIRST(anim_list); pa != TAIL(anim_list); pa = (ActAnim *)pa->link.pnext) {
        if (!(pa->flags & AF_PAUSED) && now >= pa->next_time) {
            if (pa->frame >= 0) {
                f = pa->frame + 1;
                pa->frame = f;
                if (f <= pa->last_frame) {
                    pa->frame_ref = (pa->frame_ref & 0xFFFF0000) | (ulong)(long)pa->frame;
                    AnimShowFrame(pa);
                    if (pa->flags & AF_ACCUM)
                        pa->next_time += pa->frame_time;
                    else
                        pa->next_time = now + pa->frame_time;
                } else {
                    pa->frame = -1;
                    AnimDoCodes(pa, now);
                }
            } else
                AnimDoCodes(pa, now);
        }
        if (pa->flags & AF_MOVING)
            AnimMove(pa, now - pa->last_time);
        pa->last_time = now;
        if (pa->pending) {
            pend = pa->pending;
            copy = pa->data;
            pa->pending = 0;
            memset(&pa->data, 0, sizeof(pa->data));
            if ((pend & 0xEB) && pa->code_cb)
                WATCOM_CALL(pa->code_cb, (long)pa, (long)pend, (long)&copy);
            if (pend & pa->notify_mask)
                WATCOM_CALL(pa->notify_cb, (long)pa, (long)pend, (long)&copy);
        }
    }
    if (anim_kill_count) {
        for (pa = FIRST(anim_list); pa != TAIL(anim_list); pa = next) {
            next = (ActAnim *)pa->link.pnext;
            if (pa->flags & AF_DEAD) {
                ResUnlock(REFID(pa->ref));
                LlistFree(&anim_list, pa);
            }
        }
        anim_kill_count = 0;
    }
}

/* Starts an animation (header ref) drawing onto cnv; NULL if it can't. */
ActAnim *anim_2E68A0(Ref ref, void *user, grs_canvas *cnv, void *code_cb)
{
    ActAnim *pa = LlistAddTail(&anim_list);

    if (pa == NULL)
        return NULL;
    pa->ref = ref;
    pa->head = RefLock(ref);
    if (pa->head == NULL) {
        LlistFree(&anim_list, pa);
        return NULL;
    }
    if (pa->head->size.x == 0 || pa->head->size.y == 0) {
        LlistFree(&anim_list, pa);          /* quirk: the header stays locked */
        return NULL;
    }
    pa->notify_data = NULL;
    pa->notify_mask = 0;
    pa->notify_cb = NULL;
    pa->pos[2] = 0;
    pa->pending = 0;
    pa->buf = NULL;
    pa->cnv = cnv;
    pa->pos[1] = pa->pos[0] = pa->pos[2];
    pa->code_cb = code_cb;
    pa->flags &= ~AF_INBUF;
    memset(&pa->data, 0, sizeof(pa->data));
    pa->user = user;
    AnimRestart(pa);
    return pa;
}

/* Switches the anim to another header and restarts it (AnimChangeRef); offset, depth and
 * size changes are reported to the callbacks. */
void anim_2E695C(void *anim, long ref)
{
    ActAnim *pa = anim;
    AnimHead *h = pa->head;
    LGPoint old_off = h->offset, old_size = h->size, new_off, new_size;
    short old_depth = h->depth;

    ResUnlock(REFID(pa->ref));
    pa->ref = ref;
    h = pa->head = RefLock(ref);
    new_off = h->offset;
    new_size = h->size;
    pa->flags &= ~AF_INBUF;
    if (new_off.x != old_off.x || new_off.y != old_off.y) {
        pa->data.doff.x += new_off.x - old_off.x;
        pa->data.doff.y += new_off.y - old_off.y;
        pa->pending |= AN_OFFSET;
    }
    if (old_depth != h->depth)
        pa->pending |= AN_DEPTH;
    if (new_size.x != old_size.x || new_size.y != old_size.y) {
        pa->data.size = new_size;
        pa->pending |= AN_SIZE;
    }
    AnimRestart(pa);
}

/* Marks the anim dead (AnimKill); anim_2E66F0 frees it. */
void anim_2E6A34(void *anim)
{
    ActAnim *pa = anim;

    pa->pending |= AN_KILL;
    pa->flags = (pa->flags | AF_DEAD | AF_PAUSED) & ~AF_MOVING;
    anim_kill_count++;
}

void AnimKillAll(void)
{
    ActAnim *pa;

    for (pa = FIRST(anim_list); pa != TAIL(anim_list); pa = (ActAnim *)pa->link.pnext) {
        pa->pending |= AN_KILL;
        pa->flags = (pa->flags | AF_DEAD | AF_PAUSED) & ~AF_MOVING;
        anim_kill_count++;
    }
}

/* Velocity in units per 1000 ticks (AnimSetVelocity). */
void anim_2E6AAC(void *anim, long vx, long vy, long vz)
{
    ActAnim *pa = anim;

    pa->vel[0] = vx / 1000;
    pa->vel[1] = vy / 1000;
    pa->vel[2] = vz / 1000;
    if (pa->vel[0] != 0 || pa->vel[1] != 0 || pa->vel[2] != 0)
        pa->flags |= AF_MOVING;
    else
        pa->flags &= ~AF_MOVING;
}

/* Reports a move to the callbacks (unreferenced). */
void AnimMoveBy(ActAnim *pa, short dx, short dy, short dz)
{
    if (pa->head->depth != 0 && dz != 0)
        pa->pending |= AN_DEPTH;
    pa->data.dx += dx;
    pa->data.dz += dz;
    pa->data.dy += dy;
    pa->pending |= AN_MOVE;
}

/* AnimSetNotify: cb(anim, pending, data) for the pending bits in mask. */
void anim_2E6B3C(void *anim, void *data, uchar mask, void *cb)
{
    ActAnim *pa = anim;

    pa->notify_data = data;
    pa->notify_cb = cb;
    pa->notify_mask = mask;
}

/* Sets the palette stored with the anim's frames, if any (AnimSetPalette). */
void anim_2E6B48(Ref ref)
{
    AnimHead *h = RefGet(ref);
    AnimFrame *f = RefGet(MKREF(h->frames, 0));
    short *pal;

    if (f->pal_off) {
        pal = (short *)((uchar *)ResGet(h->frames) + f->pal_off);
        gr_set_pal(pal[0], pal[1], (uchar *)(pal + 2));
    }
}

/* Makes the frames resource available (AnimLoadFrames): with a buffer, extracts it there
 * once (FALSE if it doesn't fit); otherwise just loads it.  anim NULL: load the frames of
 * header ref; ref 0: the anim's own header. */
bool anim_2E6B98(void *anim, Ref ref)
{
    ActAnim *pa = anim;
    AnimHead *h;

    if (pa == NULL) {
        h = RefGet(ref);
        ResGet(h->frames);
        return TRUE;
    }
    if (ref == 0)
        ref = pa->ref;
    h = RefGet(ref);
    if (pa->buf == NULL) {
        ResGet(h->frames);
        return TRUE;
    }
    if (pa->flags & AF_INBUF)
        return TRUE;
    if (ResSize(h->frames) > pa->bufsize)
        return FALSE;
    ResExtract(h->frames, pa->buf);
    pa->flags |= AF_INBUF;
    return TRUE;
}

/* Header size as an LGPoint in a long. */
long AnimGetSize(Ref ref)
{
    return *(long *)RefGet(ref);
}

/* Back to the first op code. */
void AnimRestart(ActAnim *pa)
{
    ulong now;

    pa->loop_pc = NULL;
    pa->loop_count = 0;
    pa->frame = -1;
    pa->pc = pa->head->codes;
    pa->time_scale = 0;
    now = *anim_time_ptr;
    pa->last_time = pa->next_time = now;
    pa->flags = (pa->flags & ~AF_ACCUM) | ((pa->head->flags >> 1) & 1);
    pa->flags = (pa->flags & ~0x1E) | 0x10;
    pa->frame_ref = AnimFirstFrameRef(pa);
}

/* The first frame the codes will show (to start with). */
Ref AnimFirstFrameRef(ActAnim *pa)
{
    ushort id = pa->head->frames;
    uchar *p = pa->pc;
    uchar op;

    for (;;) {
        op = *p++;
        switch (op) {
        case 0:
            return *(Ref *)p;
        case 1:
            id = *(ushort *)p;
            break;
        case 2:
        case 4:
            return ((ulong)id << 16) | *p;
        case 12:
            return (ulong)id << 16;
        }
        p += anim_op_len[op];
    }
}

/* Runs op codes until one waits (3), starts a frame run (4) or ends the anim (12). */
void AnimDoCodes(ActAnim *pa, ulong now)
{
    uchar *p = pa->pc;
    uchar op, sub;
    long t;
    short dx, dy, dz, v;

    for (;;) {
        op = *p++;
        if (op > 12)
            continue;
        switch (op) {
        case 0:                         /* frame ref (for AnimFirstFrameRef) */
            p += 4;
            break;
        case 1:                         /* frames resource id */
            pa->frame_ref = ((ulong)*(ushort *)p << 16) | (pa->frame_ref & 0xFFFF);
            p += 2;
            break;
        case 2:                         /* show one frame */
            pa->frame_ref = (pa->frame_ref & 0xFFFF0000) | *p;
            p++;
            AnimShowFrame(pa);
            break;
        case 3:                         /* wait */
            t = *(ushort *)p;
            p += 2;
            if (pa->time_scale)
                t = scale_time(t, pa->time_scale);
            if (pa->flags & AF_ACCUM)
                pa->next_time += t;
            else
                pa->next_time = now + t;
            pa->pc = p;
            return;
        case 4:                         /* frame run: first, last, ticks per frame */
            pa->frame = p[0];
            pa->last_frame = p[1];
            p += 2;
            pa->frame_time = *(short *)p;
            p += 2;
            if (pa->time_scale)
                pa->frame_time = (short)scale_time(pa->frame_time, pa->time_scale);
            pa->frame_ref = (pa->frame_ref & 0xFFFF0000) | (ulong)(long)pa->frame;
            AnimShowFrame(pa);
            if (pa->flags & AF_ACCUM)
                pa->next_time += pa->frame_time;
            else
                pa->next_time = now + pa->frame_time;
            pa->pc = p;
            return;
        case 5:                         /* loop start */
            p += 2;
            pa->loop_pc = p;
            pa->loop_count = *(short *)(p - 2);
            break;
        case 6:                         /* loop end (count 0 = forever) */
            if (pa->loop_count != 0) {
                pa->loop_count--;
                if (pa->loop_count <= 0)
                    break;
            }
            p = pa->loop_pc;
            break;
        case 7:                         /* move by signed bytes */
            dx = (signed char)p[0];
            dy = (signed char)p[1];
            dz = (signed char)p[2];
            p += 3;
            if (pa->head->depth != 0 && dz != 0)
                pa->pending |= AN_DEPTH;
            pa->data.dx += dx;
            pa->data.dy += dy;
            pa->data.dz += dz;
            pa->pending |= AN_MOVE;
            break;
        case 8:                         /* velocity (per 1000 ticks) */
            pa->vel[0] = ((long *)p)[0] / 1000;
            pa->vel[1] = ((long *)p)[1] / 1000;
            pa->vel[2] = ((long *)p)[2] / 1000;
            p += 12;
            if (pa->vel[0] != 0 || pa->vel[1] != 0 || pa->vel[2] != 0)
                pa->flags |= AF_MOVING;
            else
                pa->flags &= ~AF_MOVING;
            break;
        case 9:                         /* one palette entry */
            gr_set_pal(p[0], 1, p + 1);
            p += 4;
            break;
        case 10:
            p += 3;
            break;
        case 11:                        /* user value */
            v = *(short *)p;
            p += 2;
            if (pa->notify_mask & AN_USER) {
                pa->data.user = v;
                pa->pending |= AN_USER;
            }
            break;
        case 12:                        /* 0 end (pause), 1 kill, 2 restart codes */
            sub = *p++;
            if (sub == 0) {
                pa->flags |= AF_PAUSED;
                pa->pending |= AN_END;
                return;
            }
            if (sub == 1) {
                pa->flags = (pa->flags | AF_DEAD | AF_PAUSED) & ~AF_MOVING;
                pa->pending |= AN_KILL;
                anim_kill_count++;
                return;
            }
            if (sub == 2)
                p = pa->head->codes;
            break;
        }
    }
}

/* Integrates the velocity over dt ticks and reports whole-unit moves. */
void AnimMove(ActAnim *pa, ulong dt)
{
    long m = (long)(dt << 16);
    short ix, iy, iz;

    if (pa->vel[0])
        pa->pos[0] += (long)(((long long)pa->vel[0] * m) >> 16);
    if (pa->vel[1])
        pa->pos[1] += (long)(((long long)pa->vel[1] * m) >> 16);
    if (pa->vel[2])
        pa->pos[2] += (long)(((long long)pa->vel[2] * m) >> 16);
    ix = pa->pos[0] >> 16;
    iy = pa->pos[1] >> 16;
    iz = pa->pos[2] >> 16;
    if (ix != 0 || iy != 0 || iz != 0) {
        if (pa->head->depth != 0 && iz != 0)
            pa->pending |= AN_DEPTH;
        pa->data.dx += ix;
        pa->data.dy += iy;
        pa->data.dz += iz;
        pa->pending |= AN_MOVE;
        pa->pos[0] -= (long)((ulong)(long)ix << 16);
        pa->pos[1] -= (long)((ulong)(long)iy << 16);
        pa->pos[2] -= (long)((ulong)(long)iz << 16);
    }
}

/* Draws the current frame and adds its update rectangle. */
void AnimShowFrame(ActAnim *pa)
{
    AnimFrame *f;
    grs_canvas *c;

    if (pa->buf != NULL) {
        if (!(pa->flags & AF_INBUF) && !anim_2E6B98(pa, 0))
            return;
        f = (AnimFrame *)(pa->buf + *(long *)(pa->buf + REFINDEX(pa->frame_ref) * 4 + 2));
    } else
        f = RefLock(pa->frame_ref);
    if (f == NULL)
        return;
    c = pa->cnv;
    if (c != NULL && f->bm.type == BMT_RSD8) {
        if (c->bm.row == f->bm.row && c->bm.h == f->bm.h && c->bm.type == BMT_FLAT8)
            gr_delta8_unpack(c->bm.bits, f->bits);
        else {
            gr_set_canvas(c);
            f->bm.bits = f->bits;
            WATCOM_CALL(grd_canvas_table[DRAW_DEVICE_UBITMAP + 2 * BMT_RSD8 + 1], (long)f, 0, 0);
        }
    }
    if (pa->data.update.lr.x == 0)
        pa->data.update = f->update;
    else
        RectUnion(&pa->data.update, &f->update, &pa->data.update);
    pa->pending |= AN_FRAME;
    if (pa->buf == NULL)
        ResUnlock(REFID(pa->frame_ref));
}
