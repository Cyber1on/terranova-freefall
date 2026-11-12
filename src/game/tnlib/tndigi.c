/* tndigi.c -- Terra Nova digital sample layer over Miles AIL (the DOS version of the LG "snd"
 * library: x:\prj\tech\libsrc\snd\dig_ops.c & co.; System Shock's Mac source has the same
 * entry points over the Sound Manager).  Decompiled from __FF.EXE; names ours unless they are
 * Shock's (snd_sample_parms, snd_find_free_handle, snd_kill_all_samples, snd_sample_play,
 * snd_sample_reload_parms, snd_sample_info/snd_sample_handles/snd_sample_count, snd_last_error, snd_sample_done_hook).
 * The functions T2c's audio code calls keep their map names (meaning in brackets).
 *
 *   0x2EB5E0 digi_2EB5E0 (snd_lock_digi)       0x2EB60C snd_sample_handle
 *   0x2EB614 snd_sample_parms                  0x2EB62C snd_set_finish
 *   0x2EB644 snd_eos_callback                  0x2EB67C digi_2EB67C (snd_kill_all_samples)
 *   0x2EB6DC snd_find_free_handle              0x2EB7EC snd_sample_reload_parms
 *   0x2EB87C digi_2EB87C (snd_sample_load)     0x2EB9F0 snd_sample_play
 *   0x2EBBC4 snd_start_sample                  0x2EBBE0 digi_2EBBE0 (snd_end_sample)
 *   0x2EBC00 snd_stop_sample                   0x2EBC20 snd_resume_sample
 *   0x2EBC40 digi_2EBC40 (snd_count_free)      0x2EBCD8 snd_foreach_playing
 *   0x2EBD34 snd_default_parms                 0x2EBD60 snd_copy_parms
 *   0x2EBDA0 digi_2EBDA0 (snd_set_parm)        0x2EBDF0 snd_get_parm
 *   0x2EBE24 digi_2EBE24 (snd_ail_set_parm)    0x2EBE44 digi_2EBE44 (snd_ail_get_parm)
 *   0x2EBE6C snd_ail_set_pri                   0x2EBE98 snd_ail_get_pri
 *   0x2EBEC4 snd_ail_set_user                  0x2EBEF0 snd_ail_get_user
 *   0x2EBF18 snd_sample_flags                  0x2EBF34 snd_fade_tick
 *   0x2EC0E4 snd_fade                          0x2EC1A0 snd_ail_start
 *   0x2EC1E0 snd_ail_end                       0x2EC1FC snd_ail_stop
 *   0x2EC218 snd_ail_resume                    0x2EC234 snd_ail_eos
 *
 * 16 sample handles (allocated by the AIL startup code, user data 0 = index).  Each has a
 * parameter block (snd_sample_info) and an ops record (snd_sample_cbs: parameter setter / getter and a
 * vtable of start / end / stop / resume / end-of-sample); digi_2EB87C / snd_sample_play
 * install the AIL ones.  Parameters (pan, volume, rate, loops, position, priority, user) go
 * through the setter / getter tables at 0x35F91C / 0x35F900 (AIL calls, or the parameter
 * block for priority and user).  snd_fade ramps the volume from a 128 ms AIL timer.
 * Handles are passed around as signed chars.
 * Quirks kept: snd_fade_tick's "target reached" test compiles to two jumps to the same place,
 * so a fade always runs its full time; digi_2EB5E0 locks the code range [itself, itself];
 * snd_sample_play without parms leaves the user field and type of the block as they were;
 * table indexes (parameter ids) are not checked; the priority / user setters leave index * 19
 * in eax, which digi_2EBE24 (and so digi_2EBDA0) returns.
 */
#include "t2_b.h"

/* cdecl call into original code that may clobber ebx (see t2_b.h) */
#ifdef TN_WATCOM_ABI
__asm__(".text\n"
        ".globl t2b_cdecl\n"
        "t2b_cdecl:\n"
        "\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\tpushl %ebp\n"
        "\tpushl 32(%esp)\n\tpushl 32(%esp)\n\tpushl 32(%esp)\n"
        "\tcall *32(%esp)\n"
        "\taddl $12, %esp\n"
        "\tpopl %ebp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n"
        "\tret\n");
#else
/* Standalone build: the callees are our own C, a plain call does. */
long t2b_cdecl(void *f, long a, long b, long c) { return ((long (*)())f)(a, b, c); }
#endif

#ifndef TN_LINK_ORIGINAL_DATA
void *snd_sample_done_hook;
long snd_finish_data;
void *snd_ail_getters[7] = { 0 };
void *snd_ail_setters[7] = { 0 };
snd_chan_vtbl snd_ail_vtbl = { WATCOM_FN(snd_ail_start), WATCOM_FN(snd_ail_end), WATCOM_FN(snd_ail_stop),
                               WATCOM_FN(snd_ail_resume), WATCOM_FN(snd_ail_eos) };
long snd_sample_count;
uchar snd_dig_stereo = 1;
long snd_fade_timer = -1;
void *snd_dig_driver;
void *snd_finish_parms;
long snd_last_error;
uchar snd_stereo_reverse;
snd_fade_info snd_fade_tab[SND_MAX_SAMPLES];
snd_digi_parms snd_sample_info[SND_MAX_SAMPLES];
void *snd_sample_handles[SND_MAX_SAMPLES];
snd_chan_ops snd_sample_cbs[SND_MAX_SAMPLES];
#endif

#define AIL(f, a, b, c) T2B_CDECL(f, a, b, c)
#define END(h) WATCOM_CALL(snd_sample_cbs[(int)h].vt->end, (long)(h))

void digi_2EB5E0(void)
{
    AIL(dpmi_lock_region, snd_ail_getters, &snd_fade_timer, 0);
    AIL(dpmi_lock_region, digi_2EB5E0, digi_2EB5E0, 0);
}

void *snd_sample_handle(int i)
{
    return snd_sample_handles[i];
}

snd_digi_parms *snd_sample_parms(int i)
{
    return &snd_sample_info[i];
}

/* Sets the end-of-sample callback fn(handle, user); returns the old one. */
void *snd_set_finish(void *fn, long data)
{
    void *old = snd_sample_done_hook;

    snd_finish_data = data;
    snd_sample_done_hook = fn;
    return old;
}

/* AIL end-of-sample callback (cdecl). */
void snd_eos_callback(void *s)
{
    long i = AIL(AIL_sample_user_data, s, 0, 0);
    snd_chan_vtbl *vt = snd_sample_cbs[i].vt;
    char h = (char)AIL(AIL_sample_user_data, s, 0, 0);

    WATCOM_CALL(vt->eos, (long)h);
}

void digi_2EB67C(void)
{
    int i;

    for (i = 0; i < snd_sample_count; i++)
        if (AIL(AIL_sample_status, snd_sample_handles[i], 0, 0) != SMP_DONE)
            END((char)i);
}

/* A free handle, else the lowest-priority one if pri beats it (ended unless check_only);
 * -1 if none. */
char snd_find_free_handle(uchar pri, bool check_only)
{
    int i;
    char lo;

    AIL(AIL_lock, 0, 0, 0);
    for (i = 0; i < snd_sample_count; i++)
        if (AIL(AIL_sample_status, snd_sample_handles[i], 0, 0) == SMP_DONE && snd_sample_info[i].flags == 0) {
            AIL(AIL_unlock, 0, 0, 0);
            return (char)i;
        }
    lo = 0;
    for (i = 1; i < snd_sample_count; i++)
        if (snd_sample_info[i].pri < snd_sample_info[(int)lo].pri)
            lo = (char)i;
    if (pri > snd_sample_info[(int)lo].pri) {
        if (!check_only)
            END(lo);
        AIL(AIL_unlock, 0, 0, 0);
        return lo;
    }
    AIL(AIL_unlock, 0, 0, 0);
    return -1;
}

void snd_sample_reload_parms(snd_digi_parms *p)
{
    void *s = p->hnd;
    long pan = p->pan;

    if (!(p->flags & SND_FLG_INUSE) || s == NULL)
        return;
    if (snd_stereo_reverse)
        pan = 0x7F - pan;
    AIL(AIL_set_sample_volume, s, p->vol, 0);
    AIL(AIL_set_sample_pan, s, pan, 0);
    AIL(AIL_set_sample_loop_count, s, p->loops, 0);
    if (p->pitch)
        AIL(AIL_set_sample_playback_rate, s,
            (long)(((long long)(p->pitch >> 8) * p->rate) >> 8) >> 8, 0);
}

static void set_ail_ops(char h)
{
    snd_sample_cbs[(int)h].vt = &snd_ail_vtbl;
    snd_sample_cbs[(int)h].set_parm = WATCOM_FN(digi_2EBE24);
    snd_sample_cbs[(int)h].get_parm = WATCOM_FN(digi_2EBE44);
}

/* Sets up a handle for sample data (not started; the vtable start does that). */
char digi_2EB87C(void *data, long len, uchar pri, long user, uchar flags)
{
    char h = snd_find_free_handle(pri, FALSE);
    snd_digi_parms *p;
    void *s;

    if (h == -1) {
        snd_last_error = 0x200;              /* SND_NO_HANDLE */
        return -1;
    }
    p = &snd_sample_info[(int)h];
    s = snd_sample_handles[(int)h];
    p->pan = 0x40;
    p->vol = 0x7F;
    p->pri = 0x3F;
    p->loops = 1;
    p->type = 0;
    p->pitch = 0;
    p->rate = 22050 << 8;
    p->flags = 0;
    p->len = len;
    p->user = user;
    p->hnd = s;
    p->data = data;
    p->pri = pri;
    p->flags = flags | SND_FLG_INUSE;
    AIL(AIL_init_sample, s, 0, 0);
    set_ail_ops(h);
    if (!(p->flags & SND_FLG_RAWDATA) && AIL(AIL_set_sample_file, s, p->data, -1) == 0)
        p->flags |= SND_FLG_RAWDATA;
    if (p->flags & SND_FLG_RAWDATA) {
        AIL(AIL_set_sample_type, s, p->type, 0);
        AIL(AIL_set_sample_address, s, p->data, p->len);
    } else
        p->rate = WATCOM_CALL(snd_sample_cbs[(int)h].get_parm, (long)h, SND_PARM_RATE) << 8;
    AIL(AIL_register_EOS_callback, s, snd_eos_callback, 0);
    digi_2EBDA0(h, SND_PARM_LOOPS, 1);
    return h;
}

/* Plays sample data with optional parameters (unreferenced). */
char snd_sample_play(long snd_ref, long len, void *data, snd_digi_parms *dprm)
{
    char h = snd_find_free_handle(dprm != NULL ? dprm->pri : 0x3F, FALSE);
    snd_digi_parms *p;
    void *s;

    if (h == -1) {
        snd_last_error = 0x200;
        return -1;
    }
    p = &snd_sample_info[(int)h];
    s = snd_sample_handles[(int)h];
    if (dprm != NULL)
        snd_copy_parms(p, dprm);
    else {
        p->pan = 0x40;
        p->vol = 0x7F;
        p->pri = 0x3F;
        p->loops = 1;
        p->type = 0;
        p->rate = 22050 << 8;
        p->flags = 0;
        p->pitch = 0;
    }
    if (data != NULL) {
        p->data = data;
        p->len = len;
    } else
        p->flags |= SND_FLG_NODATA;
    p->snd_ref = snd_ref;
    set_ail_ops(h);
    AIL(AIL_init_sample, s, 0, 0);
    p->hnd = s;
    AIL(AIL_set_sample_loop_count, s, p->loops, 0);
    p->flags |= SND_FLG_INUSE;
    if (p->flags & SND_FLG_NODATA) {
        snd_sample_reload_parms(p);
        AIL(AIL_register_EOS_callback, s, snd_eos_callback, 0);
        return h;
    }
    if (!(p->flags & SND_FLG_RAWDATA) && AIL(AIL_set_sample_file, s, p->data, -1) == 0)
        p->flags |= SND_FLG_RAWDATA;
    if (p->flags & SND_FLG_RAWDATA) {
        AIL(AIL_set_sample_type, s, p->type, 0);
        AIL(AIL_set_sample_address, s, p->data, p->len);
    } else
        p->rate = AIL(AIL_sample_playback_rate, s, 0, 0) << 8;
    AIL(AIL_register_EOS_callback, s, snd_eos_callback, 0);
    snd_sample_reload_parms(p);
    AIL(AIL_start_sample, s, 0, 0);
    return h;
}

long snd_start_sample(char h)
{
    return WATCOM_CALL(snd_sample_cbs[(int)h].vt->start, (long)h);
}

long digi_2EBBE0(char h)
{
    return WATCOM_CALL(snd_sample_cbs[(int)h].vt->end, (long)h);
}

long snd_stop_sample(char h)
{
    return WATCOM_CALL(snd_sample_cbs[(int)h].vt->stop, (long)h);
}

long snd_resume_sample(char h)
{
    return WATCOM_CALL(snd_sample_cbs[(int)h].vt->resume, (long)h);
}

/* Handles a sample of priority pri could get; pri -1: handles not playing (AIL's count). */
long digi_2EBC40(long pri)
{
    long n = 0;
    int i;

    if (pri == -1)
        return snd_sample_count - AIL(AIL_active_sample_count, snd_dig_driver, 0, 0);
    AIL(AIL_lock, 0, 0, 0);
    for (i = 0; i < snd_sample_count; i++)
        if (AIL(AIL_sample_status, snd_sample_handles[i], 0, 0) == SMP_DONE || (uchar)pri > snd_sample_info[i].pri)
            n++;
    AIL(AIL_unlock, 0, 0, 0);
    return n;
}

/* fn(handle, arg) for every playing sample. */
void snd_foreach_playing(void *fn, long arg)
{
    int i;

    for (i = 0; i < snd_sample_count; i++)
        if (AIL(AIL_sample_status, snd_sample_handles[i], 0, 0) == SMP_PLAYING)
            WATCOM_CALL(fn, (long)(char)i, arg);
}

void snd_default_parms(snd_digi_parms *p)
{
    p->pan = 0x40;
    p->vol = 0x7F;
    p->pri = 0x3F;
    p->loops = 1;
    p->type = 0;
    p->pitch = 0;
    p->rate = 22050 << 8;
    p->flags = 0;
}

void snd_copy_parms(snd_digi_parms *d, snd_digi_parms *s)
{
    d->pan = s->pan;
    d->vol = s->vol;
    d->pri = s->pri;
    d->snd_ref = s->snd_ref;
    d->loops = s->loops;
    d->rate = s->rate;
    d->pitch = s->pitch;
    d->type = s->type;
    d->user = s->user;
    d->flags = s->flags;
}

/* Sets a parameter through the handle's ops; pan only when stereo is on (reversed if asked).
 * h_ is the whole register: the early return hands it back unchanged. */
long digi_2EBDA0(long h_, uchar parm, long val)
{
    char h = (char)h_;

#ifndef TN_WATCOM_ABI
    if (h < 0)                          /* G39: no handle; the game calls through the slot */
        return h_;                      /* before snd_sample_cbs (a crash) */
#endif
    if (parm == SND_PARM_PAN) {
        if (!snd_dig_stereo)
            return h_;
        if (snd_stereo_reverse)
            val = 0x7F - val;
    }
    return WATCOM_CALL(snd_sample_cbs[(int)h].set_parm, (long)h, (long)parm, val);
}

long snd_get_parm(char h, uchar parm)
{
#ifndef TN_WATCOM_ABI
    if (h < 0)                          /* G39: as digi_2EBDA0 */
        return 0;
#endif
    return WATCOM_CALL(snd_sample_cbs[(int)h].get_parm, (long)h, (long)parm);
}

long digi_2EBE24(char h, uchar parm, long val)
{
    return AIL(snd_ail_setters[parm], snd_sample_handles[(int)h], val, 0);
}

long digi_2EBE44(char h, uchar parm)
{
    return AIL(snd_ail_getters[parm], snd_sample_handles[(int)h], 0, 0);
}

/* setter / getter table entries for the values kept in the parameter block (cdecl) */
long snd_ail_set_pri(void *s, long pri)
{
    long i = AIL(AIL_sample_user_data, s, 0, 0);

    snd_sample_info[i].pri = (uchar)pri;
    return i * 19;              /* left in eax (index scaled for the 0x26 stride) */
}

long snd_ail_get_pri(void *s)
{
    return snd_sample_info[AIL(AIL_sample_user_data, s, 0, 0)].pri;
}

long snd_ail_set_user(void *s, long user)
{
    long i = AIL(AIL_sample_user_data, s, 0, 0);

    snd_sample_info[i].user = user;
    return i * 19;              /* as snd_ail_set_pri */
}

long snd_ail_get_user(void *s)
{
    return snd_sample_info[AIL(AIL_sample_user_data, s, 0, 0)].user;
}

uchar snd_sample_flags(char h)
{
    return snd_sample_info[(int)h].flags;
}

/* AIL timer callback (cdecl): one step of every active volume fade. */
void snd_fade_tick(void)
{
    snd_fade_info *f;
    long steps, cur;
    int i, any = 0;

    for (i = 0; i < snd_sample_count; i++) {
        f = &snd_fade_tab[i];
        if (!(f->flags & 0x1000))
            continue;
        steps = f->steps;
        if (AIL(AIL_sample_status, snd_sample_handles[i], 0, 0) == SMP_DONE) {
            f->flags = 0;
            f->steps = 0;
            continue;
        }
        if (steps > 0x100) {
            any = 1;
            steps -= 0x100;
            cur = f->cur + (long)((ulong)(f->target - f->cur) << 8) / steps;
            /* the binary tests cur against the target here, but every outcome continues
             * the fade (no early end) */
            digi_2EBDA0((char)i, SND_PARM_VOL, cur);
            f->cur = cur;
            f->steps = steps;
        } else {
            digi_2EBDA0((char)i, SND_PARM_VOL, f->target);
            f->steps = 0;
            f->cur = f->target;
            if (f->flags & 1)
                END((char)i);
            f->flags = 0;
        }
    }
    if (!any) {
        AIL(AIL_stop_timer, snd_fade_timer, 0, 0);
        AIL(AIL_release_timer_handle, snd_fade_timer, 0, 0);
        snd_fade_timer = -1;
    }
}

/* Fades the volume to target over steps/256 timer ticks; flags bit 0 ends the sample after.
 * Returns 0, or 0x4000 if no timer could be registered. */
long snd_fade(char h, long target, long steps, long flags)
{
    long cur = WATCOM_CALL(snd_sample_cbs[(int)h].get_parm, (long)h, SND_PARM_VOL);
    snd_fade_info *f;

    if (cur == target) {
        if (flags & 1)
            END(h);
        return 0;
    }
    f = &snd_fade_tab[(int)h];
    f->cur = cur;
    f->target = target;
    f->steps = steps;
    f->flags = flags | 0x1000;
    if (snd_fade_timer == -1) {
        snd_fade_timer = AIL(AIL_register_timer, snd_fade_tick, 0, 0);
        if (snd_fade_timer == -1)
            return 0x4000;
        AIL(AIL_set_timer_period, snd_fade_timer, 128000, 0);
        AIL(AIL_start_timer, snd_fade_timer, 0, 0);
    }
    return 0;
}

/* the AIL vtable (snd_ail_vtbl) */
long snd_ail_start(char h)
{
    if (snd_sample_info[(int)h].flags & SND_FLG_INUSE) {
        AIL(AIL_start_sample, snd_sample_handles[(int)h], 0, 0);
        return 0;
    }
    return -1;
}

long snd_ail_end(char h)
{
    AIL(AIL_end_sample, snd_sample_handles[(int)h], 0, 0);
    return 0;
}

long snd_ail_stop(char h)
{
    AIL(AIL_stop_sample, snd_sample_handles[(int)h], 0, 0);
    return 0;
}

long snd_ail_resume(char h)
{
    AIL(AIL_resume_sample, snd_sample_handles[(int)h], 0, 0);
    return 0;
}

/* End of sample: the finish callbacks, then the handle is free. */
long snd_ail_eos(char h)
{
    snd_digi_parms *p = &snd_sample_info[(int)h];

    if (p->flags & SND_FLG_INUSE) {
        if (snd_sample_done_hook != NULL)
            WATCOM_CALL(snd_sample_done_hook, (long)h, p->user);
        if (snd_finish_parms != NULL && !(p->flags & SND_FLG_NODATA))
            WATCOM_CALL(snd_finish_parms, (long)p);
    }
    p->flags = 0;
    return 0;
}
