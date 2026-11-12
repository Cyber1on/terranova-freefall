/* audiocfg.c -- Terra Nova's sound card setup: audio.cfg, MIDI and digital driver install,
 * the sequence and sample handle pools, and stereo sample pairs (names ours, no assert path).
 * Decompiled from __FF.EXE.  Miles AIL stays original; so do the sound card config reader
 * (sndcfg_*, T2b), the digital layer (digi_*, T2b) and the music layer (music_*, T2a).
 *
 *   0x2EF750 snd_cfg_driver_name    0x2EF798 snd_read_audio_cfg     0x2EF858 snd_cfg_clear
 *   0x2EF890 snd_sbk_template (32 zero bytes, data in code)
 *   0x2EF8B0 snd_music_slots_init   0x2EF8EC snd_midi_install       0x2EFA0C snd_set_sequence_count
 *   0x2EFB2C snd_midi_uninstall     0x2EFB8C snd_load_sbk           0x2EFC1C snd_unload_sbk
 *   0x2EFC30 snd_dig_install        0x2EFD0C snd_set_sample_count   0x2EFE30 snd_samples_reset_user
 *   0x2EFE6C snd_dig_is_stereo      0x2EFE74 snd_dig_uninstall      0x2EFEA0 snd_sample_tables_init
 *   0x2EFF50 snd_pair_lock          0x2EFF7C snd_pair_play          0x2F00E8 snd_pair_set
 *   0x2F02A8 snd_pair_get           0x2F0304 snd_pair_start         0x2F03DC snd_pair_end
 *   0x2F0444 snd_pair_stop          0x2F0488 snd_pair_resume        0x2F04CC snd_pair_done
 *   0x2F0570 snd_pair_unlock
 *
 * snd_read_audio_cfg returns 0x1000 when there is no MIDI driver, 0x2000 when there is no
 * digital driver (0x3000 when audio.cfg is missing).  The install functions return 0, or
 * 0x100 already installed, 0x101 no driver, 0x102 AIL install failed, 0x103 no card answered.
 * A stereo pair plays one sound on two samples; snd_pair_set/get replace the samples' set/get
 * callbacks (snd_sample_cbs) and split pan and volume over the two halves with snd_pan_table.
 *
 * Quirks kept:
 *  - snd_pair_play returns the channel in al only (the rest of eax is what digi_2EBDA0
 *    returned); with fewer than two free samples it returns digi_2EB87C's whole eax.
 *  - snd_set_sample_count does not release samples whose status is 1.
 *  - snd_pair_done clears the done callback with "xor edi, ebx" of two equal values.
 */
#include "t2_c.h"

void *lg_memset(void *dst, ulong pattern, int unused, ulong n);   /* 0x2F0E80 (T1c) */

#ifndef TN_LINK_ORIGINAL_DATA
const char snd_sbk_template[32];
#endif

typedef long (*cfn1)(void *);
typedef void (*cfn2)(void *, long);

static void str_copy(char *d, const char *s)
{
    while ((*d++ = *s++) != 0)
        ;
}

char *snd_cfg_driver_name(SndCard *c, long base)
{
    if (c->drv)
        return c->drv;
    if (c->idx == -1)
        return NULL;
    if (c->idx < 0 || c->idx >= 20)
        return NULL;
    c->drvflags = snd_drv_flags[base + c->idx];
    return snd_drv_names[c->idx + base];
}

static void cfg_clear(SndCard *c)
{
    c->unk5 = 0;
    c->drv = NULL;
    c->idx = -1;
    lg_memset(c->io, 0xFFFFFFFF, 0, 10);
}

ulong snd_read_audio_cfg(SndCard *dig, SndCard *midi)
{
    ulong res = 0, r;

    if (midi)
        cfg_clear(midi);
    if (dig)
        cfg_clear(dig);
    if (sndcfg_310260(snd_cfg_name, dig, midi) == 0x104)
        return 0x3000;
    r = res | 0x1000;
    if (!midi || !(midi->drv = snd_cfg_driver_name(midi, 0)))
        res = r;
    r = res | 0x2000;
    if (!dig || !(dig->drv = snd_cfg_driver_name(dig, 20)))
        res = r;
    return res;
}

void snd_cfg_clear(SndCard *c)
{
    if (c) {
        c->idx = -1;
        c->drv = NULL;
        c->unk5 = 0;
        lg_memset(c->io, 0xFFFFFFFF, 0, 10);
    }
}

void snd_music_slots_init(void)
{
    int i;
    uchar *p;

    for (i = 0, p = (uchar *)music_seq; i < SND_MAX_SEQS; i++, p += 0x1D) {
        p[1] = 0x3F;
        p[0] = 0x7F;
        *(long *)(p + 3) = 100;
        p[0x17] = 0;
        p[0x1C] = 0xFF;
        *(long *)(snd_music_slots2 + 9 * (i + 1)) = 0;
        *(long *)(snd_music_slots2 + 9 * (i + 1) + 4) = 0;
    }
}

long snd_midi_install(SndCard *c)
{
    short iobuf[0xC];       /* 0x18 bytes */
    void *io;

    if (snd_mdi_driver)
        return 0x100;
    if (!c->drv)
        return 0x101;
    sndcfg_30FF90(c, iobuf);
    io = sndcfg_30FFC0(c, iobuf);
    snd_mdi_driver = AIL_install_MDI_driver_file(sndcfg_310170(c->drv), io);
    if (!snd_mdi_driver)
        return 0x102;
    if (!sndcfg_310100(io)) {
        music_check_all();
        if (snd_mdi_type != -1 && snd_sbk_wanted == 1)
            snd_unload_sbk();
        AIL_uninstall_MDI_driver(snd_mdi_driver);
        snd_mdi_driver = NULL;
        music_unlock();
        dpmi_lock_region(&snd_mdi_lock, &snd_mdi_type);
        return 0x103;
    }
    snd_music_slots_init();
    music_seq_count = 0;
    if (c->io10e == 1)
        snd_sbk_wanted = 1;
    snd_load_sbk(c->idx);
    music_lock();
    dpmi_lock_region(&snd_mdi_lock, &snd_mdi_type);
    return 0;
}

long snd_set_sequence_count(long n)
{
    long i;
    void *h;

    if (!snd_mdi_driver)
        return 3;
    snd_mdi_lock = snd_mdi_driver;
    if (n > SND_MAX_SEQS)
        return 4;
    if (n < music_seq_count) {
        i = 0;
        if (music_seq_count <= 0)
            return 0;
        while (n < music_seq_count) {
            if (AIL_sequence_status(music_handle[i]) == 2) {
                AIL_release_sequence_handle(music_handle[i]);
                music_seq_count--;
                if (i < music_seq_count)
                    music_handle[i] = music_handle[music_seq_count];
            } else
                i++;
            if (i >= music_seq_count)
                break;
        }
        return 0;
    }
    while (n > music_seq_count) {
        h = music_handle[music_seq_count] = AIL_allocate_sequence_handle(snd_mdi_lock);
        if (!h)
            return 2;
        AIL_set_sequence_user_data(h, 0, music_seq_count);
        music_seq_count++;
    }
    return 0;
}

long snd_midi_uninstall(void)
{
    music_check_all();
    if (snd_mdi_type != -1 && snd_sbk_wanted == 1 && snd_mdi_type == SND_DRV_AWE32)
        sbk_unload(snd_mdi_driver);
    AIL_uninstall_MDI_driver(snd_mdi_driver);
    snd_mdi_driver = NULL;
    music_unlock();
    dpmi_lock_region(&snd_mdi_lock, &snd_mdi_type);
    return 0;
}

void snd_load_sbk(long type)
{
    char name[32];
    char *e;
    int i;

    for (i = 0; i < 32; i++)
        name[i] = snd_sbk_template[i];
    str_copy(name, snd_gtl_prefix);
    snd_mdi_type = type;
    if (type == SND_DRV_AWE32) {
        for (e = name; *e; e++)
            ;
        str_copy(e, snd_sbk_ext);
        sbk_load(snd_mdi_driver, sndcfg_310170(name));
    }
}

void snd_unload_sbk(void)
{
    if (snd_mdi_type == SND_DRV_AWE32)
        sbk_unload(snd_mdi_driver);
}

long snd_dig_install(SndCard *c)
{
    short iobuf[0xC];       /* 0x18 bytes */
    void *io;

    snd_sample_tables_init();
    if (snd_dig_driver)
        return 0x100;
    if (!c->drv)
        return 0x101;
    c->drvflags = 1;
    sndcfg_30FF90(c, iobuf);
    io = sndcfg_30FFC0(c, iobuf);
    snd_dig_driver = AIL_install_DIG_driver_file(sndcfg_310170(c->drv), io);
    if (!snd_dig_driver)
        return 0x102;
    if (!sndcfg_310100(io)) {
        digi_2EB67C();
        AIL_uninstall_DIG_driver(snd_dig_driver);
        snd_dig_driver = NULL;
        digi_2EB5E0();
        return 0x103;
    }
    snd_dig_stereo = *(long *)((uchar *)snd_dig_driver + 0x3C) == 2;
    digi_2EB5E0();
    return 0;
}

long snd_set_sample_count(long n)
{
    long i;
    void *h;

    if (!snd_dig_driver)
        return 3;
    snd_dig_lock = snd_dig_driver;
    if (n > SND_MAX_SAMPLES)
        return 4;
    if (n < snd_sample_count) {
        for (i = snd_sample_count - 1; n <= i; i--) {
            switch (AIL_sample_status(snd_sample_handles[i])) {
            case 1:
                break;
            case 2:
                AIL_release_sample_handle(snd_sample_handles[i]);
                break;
            default:
                digi_2EBBE0((signed char)i);
                AIL_release_sample_handle(snd_sample_handles[i]);
                break;
            }
        }
    } else {
        while (n > snd_sample_count) {
            h = snd_sample_handles[snd_sample_count] = AIL_allocate_sample_handle(snd_dig_lock);
            if (!h)
                return 2;
            AIL_init_sample(h);
            AIL_set_sample_user_data(snd_sample_handles[snd_sample_count], 0, snd_sample_count);
            snd_sample_count++;
        }
    }
    snd_sample_count = n;
    return 0;
}

void snd_samples_reset_user(void)
{
    long i;

    for (i = 0; i < snd_sample_count; i++)
        AIL_set_sample_user_data(snd_sample_handles[i], 0, i);
}

uchar snd_dig_is_stereo(void)
{
    return snd_dig_stereo;
}

long snd_dig_uninstall(void)
{
    digi_2EB67C();
    AIL_uninstall_DIG_driver(snd_dig_driver);
    snd_dig_driver = NULL;
    digi_2EB5E0();
    return 0;
}

void snd_sample_tables_init(void)
{
    int k;
    uchar *p;

    for (k = 0; k < SND_MAX_SAMPLES; k++) {
        snd_sample_cbs[k].set = WATCOM_FN(digi_2EBE24);
        snd_sample_cbs[k].fns = &snd_ail_vtbl;
        snd_sample_cbs[k].get = WATCOM_FN(digi_2EBE44);
        p = (uchar *)&snd_sample_info[k];
        *(long *)(p + 0xA) = 0;
        p[0] = 0x40;
        p[1] = 0x7F;
        snd_sample_cbs[k].done = NULL;
        *(long *)(p + 0x12) = 1;
        p[2] = 0x3F;
        snd_sample_cbs[k].done_user = 0;
        p[5] = 0;
        p[3] = 0;
        *(long *)(p + 6) = 0x562200;
        *(long *)(snd_sample_info2 + 17 * (k + 1)) = 0;
    }
}

void snd_pair_lock(void)
{
    dpmi_lock_region((void *)snd_pair_lock, (void *)snd_pair_unlock);
    dpmi_lock_region(snd_pairs, snd_lock_data_end);
}

long snd_pair_play(void *snd0, void *snd1, long a, long chan_req, long c, ulong flags)
{
    long n, r;
    signed char ch0, ch1;

    n = digi_2EBC40((uchar)chan_req);
    if (n == 0 || (n == 1 && (flags & 0x100))) {
        snd_last_error = 0x200;
        return 0xFF;
    }
    /* digi_2EB87C (t2_b.h) returns the channel in al; with fewer than two free samples this
     * function hands back its whole eax, so it is read as a long here (as before the G2 merge,
     * when this file declared digi_2EB87C long). */
    {
        long (*start_l)(void *, long, uchar, long, uchar) = (void *)digi_2EB87C;
        r = start_l(snd0, a, (uchar)chan_req, c, (uchar)flags);
    }
    ch0 = (signed char)r;
    if (n <= 1)
        return r;
    ch1 = (signed char)digi_2EB87C(snd1, a, (uchar)chan_req, c, (uchar)flags);
    snd_sample_cbs[ch0].get = WATCOM_FN(snd_pair_get);
    snd_sample_cbs[ch0].set = WATCOM_FN(snd_pair_set);
    snd_sample_cbs[ch0].fns = snd_pair_fns;
    snd_pairs[ch0].partner = ch1;
    snd_pairs[ch0].flags = flags;
    snd_pairs[ch1].flags = flags | 0x400;
    snd_pairs[ch1].partner = ch0;
    snd_sample_cbs[ch1].get = WATCOM_FN(snd_pair_get);
    snd_sample_cbs[ch1].fns = snd_pair_fns;
    snd_sample_cbs[ch1].set = WATCOM_FN(snd_pair_set);
    snd_pairs[ch0].vol = 0x7F;
    snd_pairs[ch0].pan = 0x3F;
    digi_2EBDA0(ch0, 0, 0x40);
    digi_2EBDA0(ch0, 1, 0x7F);
    return (uchar)ch0;
}

static inline long clamp127(long v)
{
    if (v < 0)
        return 0;
    if (v > 127)
        return 127;
    return v;
}

void snd_pair_set(long chan, long what, long v)
{
    signed char ch = (signed char)chan;
    void *h0 = snd_sample_handles[ch];
    void *h1 = snd_sample_handles[snd_pairs[ch].partner];
    long l, r;
    SndPanEntry *e;

    switch ((uchar)what) {
    case 0:                                 /* pan */
        v = clamp127(v);
        e = &snd_pan_table[v];
        r = (e->rvol * snd_pairs[ch].vol) >> 7;
        l = (snd_pairs[ch].vol * e->lvol) >> 7;
        if (r < 0) r = 0;
        if (l < 0) l = 0;
        ((cfn2)snd_ail_setters[1])(h1, r);
        ((cfn2)snd_ail_setters[1])(h0, l);
        ((cfn2)snd_ail_setters[0])(h1, e->rpan);
        ((cfn2)snd_ail_setters[0])(h0, e->lpan);
        snd_pairs[ch].pan = v;
        break;
    case 1:                                 /* volume */
        v = clamp127(v);
        e = &snd_pan_table[snd_pairs[ch].pan];
        l = (e->lvol * v) >> 7;
        r = (e->rvol * v) >> 7;
        if (l < 0) l = 0;
        if (r < 0) r = 0;
        ((cfn2)snd_ail_setters[1])(h1, r);
        ((cfn2)snd_ail_setters[1])(h0, l);
        snd_pairs[ch].vol = v;
        break;
    default:
        ((cfn2)snd_ail_setters[(uchar)what])(h1, v);
        ((cfn2)snd_ail_setters[(uchar)what])(h0, v);
        break;
    }
}

long snd_pair_get(long chan, long what)
{
    signed char ch = (signed char)chan;

    if ((uchar)what == 0)
        return snd_pairs[ch].pan;
    if ((uchar)what == 1)
        return snd_pairs[ch].vol;
    return ((cfn1)snd_ail_getters[(uchar)what])(snd_sample_handles[ch]);
}

long snd_pair_start(long chan)
{
    signed char ch = (signed char)chan;
    signed char p = snd_pairs[ch].partner;

    AIL_lock();
    if ((snd_pairs[ch].flags & 0x200) && ((cfn1)snd_ail_getters[1])(snd_sample_handles[p]) == 0) {
        snd_sample_info[p].flags = 0;
        snd_sample_cbs[ch].get = WATCOM_FN(digi_2EBE44);
        snd_sample_cbs[ch].set = WATCOM_FN(digi_2EBE24);
        snd_sample_cbs[ch].fns = &snd_ail_vtbl;
    } else
        AIL_start_sample(snd_sample_handles[p]);
    AIL_start_sample(snd_sample_handles[ch]);
    AIL_unlock();
    return 0;
}

long snd_pair_end(long chan)
{
    signed char ch = (signed char)chan;

    if (!(snd_sample_info[ch].flags & 2))
        return -1;
    AIL_end_sample(snd_sample_handles[snd_pairs[ch].partner]);
    AIL_end_sample(snd_sample_handles[ch]);
    return 0;
}

long snd_pair_stop(long chan)
{
    signed char ch = (signed char)chan;

    AIL_stop_sample(snd_sample_handles[snd_pairs[ch].partner]);
    AIL_stop_sample(snd_sample_handles[ch]);
    return 0;
}

long snd_pair_resume(long chan)
{
    signed char ch = (signed char)chan;

    AIL_resume_sample(snd_sample_handles[snd_pairs[ch].partner]);
    AIL_resume_sample(snd_sample_handles[ch]);
    return 0;
}

long snd_pair_done(long chan)
{
    signed char ch = (signed char)chan;
    uchar *info = (uchar *)&snd_sample_info[ch];
    SndSampleCb *cb;

    if ((info[3] & 2) && !(snd_pairs[ch].flags & 0x400)) {
        if (snd_sample_done_hook)
            WATCOM_CALL(snd_sample_done_hook, ch, *(long *)(info + 0x1A));
        cb = &snd_sample_cbs[ch];
        if (cb->done) {
            WATCOM_CALL(cb->done, ch, cb->done_user);
            cb->done = NULL;
        }
    }
    info[3] = 0;
    return 0;
}

void snd_pair_unlock(void)
{
    dpmi_unlock_region((void *)snd_pair_lock, (void *)snd_pair_unlock);
    dpmi_unlock_region(snd_pairs, snd_lock_data_end);
}
