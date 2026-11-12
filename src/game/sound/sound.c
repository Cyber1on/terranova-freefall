/* sound.c -- the game's sound effects layer (r:\prj\tn\v109\tn\src\tn\sound.c, module M002,
 * Terra Nova v1.09).  Decompiled from __FF.EXE; names ours (SndPlay from the map).
 *
 *   0x246C60 snd_cfg_template (data)  0x246CF0 snd_music_curve (data)
 *   0x246D24 snd_config_load        0x246EA0 snd_config_save        0x246F24 SndInit
 *   0x247134 SndShutdown            0x2471B0 snd_chan_spatialize    0x24733C snd_chan_add_callback
 *   0x24739C snd_chan_clear_callbacks 0x2473BC snd_finish_pending   0x247574 snd_update_channels
 *   0x24765C snd_kill_low_priority  0x2476F8 snd_kill_entity_cb     0x247744 snd_chan_stop
 *   0x247780 snd_stop_entity        0x2477D0 snd_stop_looping       0x247848 snd_chan_start_sample
 *   0x247978 snd_chan_alloc         0x247A08 snd_chan_update_dist   0x247B60 SndPlay
 *   0x247E30 snd_chan_set_handle_ptr 0x247E58 snd_chan_set_vol      0x247EE8 snd_chan_set_vol_pan
 *   0x247F50 snd_chan_get_vol       0x247F90 snd_res_volume         0x247FD0 snd_chan_fade
 *   0x248018 snd_chan_doppler       0x2480A4 snd_chans_init         0x248124 snd_dig_setup
 *   0x24817C snd_midi_setup         0x2481E0 snd_audible            0x24831C snd_spatial_vol
 *   0x248530 snd_play_2d            0x248548 snd_play_2d_once       0x24856C snd_chan_fade_io
 *   0x2485F4 snd_chan_crossfade     0x2486A4 snd_null_2486A4        0x2486A8 SndUpdateHook
 *   0x2486D0 snd_chan_get_id        0x2486FC snd_play_if_idle       0x24872C snd_set_speech_volume
 *   0x248760 snd_preview_sfx_volume 0x2487F4 snd_set_sfx_volume     0x24883C snd_set_sfx_volume_test
 *   0x2488C0 snd_set_qsound         0x2488F8 snd_stereo_test_cb     0x24892C snd_stereo_test
 *   0x2489B0 snd_set_stereo_reverse 0x2489C4 snd_set_channel_level  0x248A5C snd_set_music_volume
 *   0x248AD0 snd_chan_active        0x248B04 snd_sched_init         0x248B40 snd_sched_pop
 *   0x248B5C snd_sched_run          0x248C10 snd_sched_alloc        0x248CA0 SndSchedule
 *   0x248CE4 snd_count_playing      0x248D1C snd_speech_duck        0x248E00 snd_set_type_volume
 *   0x248EBC snd_play_step          0x248FA8 snd_null_248FA8        0x248FBC snd_play_hit
 *   0x249080 snd_loop_end_cb        0x249094 snd_loop_ctl           0x249120 snd_radio_mode
 *
 * Up to snd_channels (4, 8 or 16) game channels (SndChan) sit over the samples of the T2b
 * digital layer (or T2c's stereo sample pairs for the qsound sounds 0x808..0x851, whose right
 * half is the resource id + 0x4A).  SndPlay picks a free channel (or ends the least important
 * one), locks the sound resource, sets the volume from the sound's own volume (byte item
 * id - 0x76C of resource 0x89C), the distance and direction to the player entity (types 1, 2,
 * 5), the global effects / speech volume, and starts it.  The frame hook (slot 1) re-places
 * positional sounds, applies doppler and starts scheduled sounds (SndSchedule).  When a sample
 * ends, the digital layer's finish callback (sndlock_2491C0, M003) queues the channel;
 * snd_finish_pending runs its callbacks and unlocks the resources.
 *
 * Quirks kept: SndPlay uses the channel -2 when the second allocation attempt returns it;
 * snd_sched_run pushes finished events one slot too high on the free stack (it overwrites the
 * first pool record's time when the stack is full); snd_set_music_volume reads 2 bytes past its
 * 50-entry curve for 100% (stack garbage in the original); snd_chan_doppler divides by zero /
 * overflows (a CPU fault in the original) for extreme distances; several functions don't check
 * the channel's upper bound.
 */
#include "g1_a.h"
#include "g1_c.h"      /* music_*, replay_* (owner g1c) */

#ifndef TN_LINK_ORIGINAL_DATA
uchar snd_busy, snd_channels, snd_flags, snd_sfx_vol, snd_speech_vol, snd_vol_359831, snd_music_vol;
RndStream snd_rnd = { 0, WATCOM_FN(RndLc16), WATCOM_FN(RndLc16Seed) };
RndStream snd_rnd2 = { 0, WATCOM_FN(RndLc16), WATCOM_FN(RndLc16Seed) };
long snd_finishing;
fix snd_sound_speed = 0xBB8;
SndEvent *snd_sched_cur;
short snd_hit_ids[0x22] = {
    0, 0x7D5, 0x7D5, 0x7D5, 0x7CF, 0x7CF, 0x7CF, 0x7CF, 0x7D5, 0x7DB, 0x7D5, 0x7CF, 0, 0x7CF, 0x7CF, 0x7CF, 0x7CF,
    0x7D5, 0x7CF, 0x7CF, 0x7CF, 0x7D5, 0x7CF, 0x7CF, 0x7DB, 0x7CF, 0x7CF, 0x7CF, 0x7D5, 0x7D5, 0x7D5, 0x7D5, 0x7D5, 0x7D5
};
SndLoop snd_loops[4] = { { 0x7B6, 0, -1 }, { 0x7B3, 1, -1 }, { 0x7B4, 0x12, -1 }, { 0x7B5, 3, -1 } };
long snd_finish_count;
SndEvent *snd_sched_list;
uchar snd_dist_vol[32] = { 100, 80, 64, 54, 46, 40, 34, 30, 26, 23, 21, 18, 16, 14, 13, 12,
                           11, 10, 9, 8, 7, 6, 6, 5, 5, 4, 3, 3, 2, 2, 1, 1 };
uchar snd_pan_vol[64] = { 128, 128, 128, 128, 128, 128, 127, 127, 127, 127, 126, 126, 126, 125, 125, 125,
                          124, 124, 123, 122, 122, 121, 121, 120, 120, 120, 119, 118, 117, 116, 116, 115,
                          115, 114, 113, 112, 111, 111, 110, 109, 109, 108, 107, 106, 106, 105, 104, 103,
                          102, 102, 101, 100, 99, 99, 98, 97, 96, 96, 95, 94, 94, 93, 92, 91 };
SndEvent *snd_sched_free[8];
SndEvent snd_sched_pool[8];
long snd_duck_music, snd_music_cur, snd_duck_sfx, snd_sfx_base;
SndChan snd_chans[SND_MAX_CHANS];
long snd_stereo_flip, snd_sched_free_count;
long snd_finish_queue[31];
#endif

/* music volume curve by percent / 2 (0x246CF0, copied to the stack by snd_set_music_volume) */
static const uchar music_curve[0x32] = {
    0x00, 0x04, 0x08, 0x0C, 0x0F, 0x12, 0x15, 0x18, 0x1B, 0x1E, 0x21, 0x24, 0x27, 0x2A, 0x2D, 0x30,
    0x34, 0x38, 0x3B, 0x3E, 0x41, 0x44, 0x47, 0x4A, 0x4D, 0x50, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x5C,
    0x5D, 0x5E, 0x5F, 0x60, 0x60, 0x61, 0x61, 0x62, 0x62, 0x63, 0x63, 0x63, 0x63, 0x64, 0x64, 0x64,
    0x64, 0x64
};

static char sound_c[] = "r:\\prj\\tn\\v109\\tn\\src\\tn\\sound.c";

/* a sound's own volume: signed byte item (id - 0x76C) of resource 0x89C, 0 outside the range */
#define SND_RES_VOL(id) ((ushort)(id) >= 0x76C && (ushort)(id) <= 0x851 \
    ? *(signed char *)RefGet(MKREF(0x89C, (ushort)(id) - 0x76C)) : 0)
/* the stereo pair sounds (right half id + 0x4A) */
#define SND_IS_PAIR(id) ((ushort)(id) >= 0x808 && (ushort)(id) <= 0x851 && (ushort)(id) != 0x811 \
    && (snd_flags & SNDF_PAIRS))
#define PLAYER (&game_entities[game_player_entity])
#define CHAN(i) (&snd_chans[i])
#define ABS(v) ((v) < 0 ? -(v) : (v))

#ifndef TN_WATCOM_ABI
/* G39 (Freefall): the channel SndPlay has taken while it makes room for its sound
 * (snd_kill_low_priority, noisy_2495A0).  Stopping a sound runs its end callbacks, and one of
 * them can play another sound: in the game that one took the same channel, still free, and the
 * first sound then wrote its own handle (-1 when it could not start) over it.  The channel
 * looked free with a sample playing on it; when that sample was later taken for a new sound on
 * the channel, the new sound's own start ended it, and noisy_oneshot set the rate of handle -1
 * (a call through the slot before snd_sample_cbs: a crash).  A reserved channel is neither free
 * nor stolen. */
static uchar snd_reserved[SND_MAX_CHANS];
#define SND_RESERVED(i) (snd_reserved[i])
#else
#define SND_RESERVED(i) 0
#endif

/* ------------------------------------------------------------------ configuration */

void snd_config_load(void)
{
    long v;

    snd_channels = 0;
    snd_flags = SNDF_PAIRS | SNDF_PAIRFLAG;
    snd_sfx_vol = snd_speech_vol = snd_vol_359831 = snd_music_vol = 100;
    if (config_get_single_value("digi_channels", 1, &v)) {
        if (v > 0x10)
            v = 0x10;
        else if (v < 0)
            v = 0;
        snd_channels = (uchar)v;
    }
    if (config_get_single_value("qsound", 1, &v)) {
        if (v > 1)
            v = 1;
        else if (v < 0)
            v = 0;
        snd_flags = (snd_flags & ~SNDF_PAIRS) | ((v & 1) << 5);
    }
    if (config_get_single_value("sfx_vol", 1, &v)) {
        if (v > 0x7F)
            v = 0x7F;
        else if (v < 0)
            v = 0;
        snd_sfx_vol = (uchar)v;
    }
    if (config_get_single_value("speech_vol", 1, &v)) {
        if (v > 0x7F)
            v = 0x7F;
        else if (v < 0)
            v = 0;
        snd_speech_vol = (uchar)v;
    }
    if (config_get_single_value("music_vol", 1, &v)) {
        if (v > 0x7F)
            v = 0x7F;
        else if (v < 0)
            v = 0;
        snd_music_vol = (uchar)v;
    }
}

void snd_config_save(void)
{
    config_set_single_value("digi_channels", 1, snd_channels);
    config_set_single_value("qsound", 1, (snd_flags >> 5) & 1);
    config_set_single_value("sfx_vol", 1, snd_sfx_vol);
    config_set_single_value("speech_vol", 1, snd_speech_vol);
    config_set_single_value("music_vol", 1, snd_music_vol);
}

/* ------------------------------------------------------------------ init / shutdown */

void SndInit(bool read_cfg)
{
    SndCard dig, midi;
    char gtl[0x90] = { 0 };     /* copied from the zeros at 0x246C60 */
    ulong cfg;
    char *p;

    snd_rnd.f_Seed(&snd_rnd, 0);
    snd_rnd2.f_Seed(&snd_rnd2, 0);  /* (the original also clears ebx) */
    ResOpenResFile("ressound.res", ROM_READ, FALSE);
    DatapathAdd(&gDatapath, "sound");
    if (!DatapathFind(&gDatapath, "ff.ad", gtl))
        memcpy(gtl, "ff", 3);
    else if ((p = strrchr(gtl, '.')) == NULL)
        memcpy(gtl, "ff", 3);
    else
        *p = 0;
    snd_ail_startup();
    snd_ail_setup((char *)&gDatapath, gtl);
    AtExit(WATCOM_FN(SndShutdown));
    dig.idx = -1;
    midi.idx = -1;
    cfg = 0x3000;
    if (read_cfg)
        cfg = snd_read_audio_cfg(&dig, &midi);
    snd_chans_init();
    music_246428();
    sndlock_249170();
    if (!(cfg & 0x1000) && snd_midi_install(&midi) == 0) {
        snd_flags = (snd_flags & ~SNDF_GM) | ((midi.drvflags & 4) ? SNDF_GM : 0);
        snd_flags = (snd_flags | 8) & ~7;
        snd_set_sequence_count(snd_flags & 8);
        music_end_cb = WATCOM_FN(sndlock_24919C);
    }
    if (!(cfg & 0x2000) && snd_dig_install(&dig) == 0)
        snd_dig_setup(&dig);
    if (snd_channels || (snd_flags & SNDF_MUSIC)) {
        game_hooks[1] = (GameHook)WATCOM_FN(SndUpdateHook);
        *(uchar *)&game_hook_mask |= 2;
    }
    if (snd_channels)
        periodic_add(WATCOM_FN(snd_finish_pending), 10, 0);
    snd_flags |= SNDF_INIT;
    noisy_2492C4();
    snd_sched_init();
    if (snd_flags & SNDF_MUSIC)
        music_246380(midi.idx);
}

void SndShutdown(void)
{
    if (!(snd_flags & SNDF_INIT))
        return;
    if (snd_channels) {
        digi_2EB67C();
        periodic_remove(WATCOM_FN(snd_finish_pending));
        snd_finish_parms = NULL;
    }
    if (snd_flags & SNDF_MUSIC) {
        music_check_all();
        music_end_cb = NULL;
    }
    snd_ail_shutdown();
    snd_channels = 0;
    *(uchar *)&game_hook_mask &= ~2;
    snd_flags &= 0x70;
    sndlock_249230();
    snd_pair_unlock();
}

/* ------------------------------------------------------------------ positional sound */

/* Volume and pan from the distance (c->dist) and the direction to the player entity. */
void snd_chan_spatialize(SndChan *c)
{
    fix x, y;
    long v, i, pan, deg;
    GameEntity *pe;
    ushort a;

    switch (c->type) {
    case SND_T_ENTITY:
    case SND_T_ENTITY2:
        x = game_entities[(short)c->u.ent].x;
        y = game_entities[(short)c->u.ent].y;
        break;
    case SND_T_POS:
        x = c->u.pos[0];
        y = c->u.pos[1];
        break;
    default:
        return;
    }
    if (c->dist == SND_FAR)
        v = 0;
    else {
        i = c->dist >> 16;
        v = i > 0x1F ? 0 : snd_dist_vol[i];
    }
    if (v == 0) {
        c->vol = 0;
        c->pan = 0x40;
        return;
    }
    pe = PLAYER;
    a = (ushort)(fix_atan2(y - pe->y, x - pe->x) - pe->heading);
    pan = ((fix_sin(a) * 63) >> 16) + 0x40;
    deg = (long)a * 180 / 0x8000;
    i = 0x7F - pan;
    if (i >= pan)
        i = pan;
    v = (v * snd_pan_vol[i]) >> 7;
    if (deg > 90 && deg < 270)          /* behind the player */
        v = (v * 3) >> 2;
    if (v < 0)
        v = 0;
    if (c->id != 0)
        v = (v * SND_RES_VOL(c->id)) >> 6;
    c->vol = (uchar)v;
    c->pan = (uchar)pan;
}

/* c->dist = distance to the player, SND_FAR beyond 31 units on an axis */
void snd_chan_update_dist(SndChan *c)
{
    GameEntity *pe;
    fix dx, dy, dz;

    switch (c->type) {
    case SND_T_ENTITY:
    case SND_T_ENTITY2: {
        GameEntity *e = &game_entities[(short)c->u.ent];
        pe = PLAYER;
        dx = e->x - pe->x;
        dy = e->y - pe->y;
        dz = e->z - pe->z;
        break;
    }
    case SND_T_POS:
        pe = PLAYER;
        dx = c->u.pos[0] - pe->x;
        dy = c->u.pos[1] - pe->y;
        dz = c->u.pos[2] - pe->z;
        break;
    default:
        c->dist = SND_FAR;
        return;
    }
    if (ABS(dx) > 0x1F0000 || ABS(dy) > 0x1F0000 || ABS(dz) > 0x1F0000)
        c->dist = SND_FAR;
    else
        c->dist = fix_safe_pyth_dist(fix_safe_pyth_dist(dx, dy, sound_c, 0x3E2), dz, sound_c, 0x3E2);
}

/* Doppler: playback rate from the change of distance since the last update. */
void snd_chan_doppler(SndChan *c)
{
    fix rate = c->rate << 16;
    fix old = c->dist;
    ulong t0 = c->last;
    long dt, speed, f;

    c->last = tn_ticks;
    dt = tn_ticks - t0;
    snd_chan_update_dist(c);
    dt <<= 16;
    if (c->dist == SND_FAR || dt <= 0 || old == c->dist)
        return;
    speed = (long)(((long long)dt * snd_sound_speed) >> 16);
    f = (long)(((long long)(long)(old - c->dist) << 16) / speed) + 0x10000;
    if (f == 0)
        return;
    digi_2EBDA0(c->hnd, SND_PARM_RATE, (long)(((long long)rate * f) >> 16) >> 16);
}

/* TRUE when a sound at (x, y, z) would be heard above volume 8 */
bool snd_audible(fix x, fix y, fix z, ushort id)
{
    GameEntity *pe;
    long d, v;

    if (!snd_channels)
        return FALSE;
    pe = PLAYER;
    d = fix_safe_pyth_dist(fix_safe_pyth_dist(x - pe->x, y - pe->y, sound_c, 0x591), z - pe->z, sound_c, 0x591) >> 16;
    if (d > 0x1F)
        v = 0;
    else {
        pe = PLAYER;
        v = snd_dist_vol[fix_safe_pyth_dist(fix_safe_pyth_dist(x - pe->x, y - pe->y, sound_c, 0x591),
                                            z - pe->z, sound_c, 0x591) >> 16];
    }
    if (id != 0)
        v = (v * SND_RES_VOL(id)) >> 6;
    return (uchar)((snd_sfx_vol * v) >> 7) > 8;
}

/* volume and pan a sound at (x, y, z) would get (*pan untouched when out of range) */
void snd_spatial_vol(ushort id, fix x, fix y, fix z, long *vol, long *pan)
{
    GameEntity *pe;
    fix dx, dy, dz;
    long d;
    ushort a;

    if (!snd_channels)
        return;
    pe = PLAYER;
    dx = x - pe->x;
    dy = y - pe->y;
    dz = z - pe->z;
    if (ABS(dx) > 0x1F0000 || ABS(dy) > 0x1F0000 || ABS(dz) > 0x1F0000) {
        *vol = 0;
        return;
    }
    d = fix_safe_pyth_dist(fix_safe_pyth_dist(dx, dy, sound_c, 0x5AF), dz, sound_c, 0x5AF) >> 16;
    *vol = d > 0x1F ? 0 : snd_dist_vol[fix_safe_pyth_dist(fix_safe_pyth_dist(dx, dy, sound_c, 0x5AF), dz, sound_c, 0x5AF) >> 16];
    if (id != 0)
        *vol = (SND_RES_VOL(id) * *vol) >> 6;
    *vol = ((snd_sfx_vol * *vol) >> 7) & 0xFF;
    if (*vol == 0) {
        *pan = 0x40;
        return;
    }
    pe = PLAYER;
    a = (ushort)(fix_atan2(dy, dx) - pe->heading);
    *pan = ((fix_sin(a) * 63) >> 16) + 0x40;
    d = (long)a * 180 / 0x8000;
    if (d > 90 && d < 270)
        *vol = (*vol * 3) >> 2;
    d = 0x7F - *pan;
    if (d >= *pan)
        d = *pan;
    *vol = (snd_pan_vol[d] * *vol) >> 7;
}

/* ------------------------------------------------------------------ channels */

long snd_chan_add_callback(long chan, void *fn, long arg)
{
    SndChan *c;
    long n;

    if (!snd_channels || chan < 0 || chan > snd_channels - 1)
        return -1;
    c = CHAN(chan);
    n = c->ncb;
    if (n > 3)
        return -1;
    c->cb[n].fn = fn;
    c->cb[n].arg = arg;
    c->ncb++;
    return n;
}

void snd_chan_clear_callbacks(long chan)
{
    if (CHAN(chan)->hnd >= 0)
        CHAN(chan)->ncb = 0;
}

/* Runs the end of every channel queued by the finish callback (FINISH_SOUNDS): its callbacks
 * (last added first), clears it and unlocks the sound resource(s). */
void snd_finish_pending(void)
{
    SndChan *list[32];
    SndChan *c;
    long n = 0, k, j;
    ResDesc *rd;
    ushort id, id2;

    if (snd_finishing)
        return;
    snd_finishing = 1;
    snd_lock();
    for (k = snd_finish_count; k > 0; k--) {
        list[n++] = CHAN(snd_finish_queue[snd_finish_count - 1]);
        snd_finish_count--;
    }
    snd_unlock();
    while (n > 0) {
        c = list[--n];
        for (j = c->ncb; j > 0; j--)
            WATCOM_CALL(c->cb[j - 1].fn, c->id, c->cb[j - 1].arg);
        if (c->handle_ptr) {
            *c->handle_ptr = -1;
            c->handle_ptr = NULL;
        }
        if (c->type == SND_T_SPEECH)
            snd_speech_duck(FALSE);
        id = c->id;
        c->ncb = 0;
        c->type = -1;
        c->hnd = -1;
        rd = &gResDesc[id];
        if (rd->lock == 1) {
            long size = rd->size;
            dpmi_unlock_data(ResGet(id), size);
        }
        ResUnlock(id);
        id = c->id;
        if (SND_IS_PAIR(id)) {
            id2 = id + 0x4A;
            rd = &gResDesc[id2];
            if (rd->lock == 1) {
                long size = rd->size;
                dpmi_unlock_data(ResGet(id2), size);
            }
            ResUnlock(id2);
        }
    }
    snd_finishing = 0;
}

/* frame update: positional sounds follow the player, doppler; then the scheduled sounds */
void snd_update_channels(void)
{
    SndChan *c = snd_chans;
    long i;

    for (i = 0; i < snd_channels; i++, c++) {
        if (c->hnd < 0)
            continue;
        if (c->type == SND_T_ENTITY || c->type == SND_T_POS) {
            snd_chan_spatialize(c);
            digi_2EBDA0(c->hnd, SND_PARM_VOL, (uchar)((snd_sfx_vol * c->vol) >> 7));
            digi_2EBDA0(c->hnd, SND_PARM_PRI, c->vol + 0x10);
            digi_2EBDA0(c->hnd, SND_PARM_PAN, c->pan);
            if (!c->no_doppler && c->vol > 0)
                snd_chan_doppler(c);
        } else if (c->type == SND_T_ENTITY2) {
            if (!c->no_doppler && c->vol > 0)
                snd_chan_doppler(c);
            digi_2EBDA0(c->hnd, SND_PARM_PRI, c->vol + 0x10);
        }
    }
    snd_sched_run();
}

/* Ends a channel to make room: a SND_T_LOW one, else the last quiet (< 0x20) one, else the
 * last one with a start time; never speech, nor samples of priority >= 0xFF. */
bool snd_kill_low_priority(void)
{
    long any = -1, quiet = -1, i;
    SndChan *c = snd_chans;

    for (i = 0; i < snd_channels; i++, c++) {
        if (c->hnd < 0 || c->type == SND_T_SPEECH)
            continue;
        if (c->type == SND_T_LOW) {
            snd_chan_stop(i);
            return TRUE;
        }
        if (snd_get_parm(c->hnd, SND_PARM_PRI) >= 0xFF)
            continue;
        if (c->start != 0xFFFFFFFF)
            any = i;
        if (c->vol < 0x20)
            quiet = i;
    }
    if (quiet != -1) {
        snd_chan_stop(quiet);
        return TRUE;
    }
    if (any == quiet)
        return FALSE;
    snd_chan_stop(any);
    return TRUE;
}

/* kill-list callback: the entity died, end its sounds */
void snd_kill_entity_cb(short ent)
{
    long i;

    for (i = 0; i < snd_channels; i++)
        if (CHAN(i)->u.ent == ent && CHAN(i)->hnd >= 0 && CHAN(i)->type == SND_T_ENTITY)
            snd_chan_stop(i);
}

void snd_chan_stop(long chan)
{
    if (chan < 0 || chan > snd_channels - 1)
        return;
    if (CHAN(chan)->hnd >= 0)
        digi_2EBBE0(CHAN(chan)->hnd);
    snd_finish_pending();
}

void snd_stop_entity(short ent)
{
    long i;

    for (i = 0; i < snd_channels; i++)
        if (CHAN(i)->type == SND_T_ENTITY && CHAN(i)->u.ent == ent && CHAN(i)->hnd >= 0)
            snd_chan_stop(i);
}

/* ends the looping sounds and speech (and the music) */
void snd_stop_looping(void)
{
    long i;

    if (snd_channels) {
        snd_busy = 1;
        noisy_249320();
        for (i = 0; i < snd_channels; i++) {
            if (CHAN(i)->hnd < 0)
                continue;
            if (snd_get_parm(CHAN(i)->hnd, SND_PARM_LOOPS) != 1 || CHAN(i)->type == SND_T_SPEECH)
                snd_chan_stop(i);
        }
        snd_busy = 0;
    }
    if (snd_flags & SNDF_MUSIC)
        music_2467B8(0);
}

/* Locks sound id (and its right half for a stereo pair) and starts a sample on it; returns
 * the digital layer handle (-1: none). */
signed char snd_chan_start_sample(ushort id, long chan, uchar pri)
{
    long id2 = 0;
    void *data, *data2 = NULL;
    long size;
    signed char h;
    ResDesc *rd;

    data = ResLock(id);
    size = gResDesc[id].size;
    if (SND_IS_PAIR(id)) {
        id2 = id + 0x4A;
        data2 = ResLock((ushort)id2);
        h = (signed char)snd_pair_play(data, data2, size, pri, chan, (snd_flags & SNDF_PAIRFLAG) ? 0x100 : 0);
    } else
        h = digi_2EB87C(data, size, pri, chan, 0);
    if (h == -1) {
        ResUnlock(id);
        if ((ushort)id2)
            ResUnlock((ushort)id2);
    } else {
        rd = &gResDesc[id];
        if (rd->lock == 1) {
            dpmi_lock_data(data, rd->size);
            if (data2)
                dpmi_lock_data(data2, gResDesc[(ushort)id2].size);
        }
    }
    return h;
}

/* a free channel, else ends the one with the lowest sample priority; -2 while finishing */
long snd_chan_alloc(void)
{
    long i, best = -1, minp = 0x100, p;

    snd_finish_pending();
    for (i = 0; i < snd_channels; i++)
        if (CHAN(i)->hnd < 0 && !SND_RESERVED(i))
            return i;
    if (snd_finishing)
        return -2;
    for (i = 0; i < snd_channels; i++) {
        if (SND_RESERVED(i))
            continue;
        p = snd_get_parm(CHAN(i)->hnd, SND_PARM_PRI);
        if (p < minp) {
            minp = p;
            best = i;
        }
    }
    if (best != -1)
        snd_chan_stop(best);
    return best;
}

/* Plays sound id; type SND_T_*, pri sample priority (replaced by the volume + 0x10 for
 * positional types), a1..a3 type arguments (volume/pan, position or entity).  Returns the
 * channel, -1 / -2 (or the digital layer's negative result) when nothing plays. */
long SndPlay(ushort id, long type, long pri, long loops, long a1, long a2, long a3)
{
    long base = snd_sfx_vol;
    long chan;
    SndChan *c;
    signed char h;

    if (!snd_channels || snd_busy)
        return -2;
    chan = snd_chan_alloc();
    if (chan == -1) {
        if (!noisy_2495A0(id) && !snd_kill_low_priority())
            return -1;
        chan = snd_chan_alloc();
        if (chan == -1)
            return -1;
    } else if (chan == -2)
        return -1;
    c = CHAN(chan);
    c->type = type;
    c->loops = (uchar)loops;
    c->no_doppler = 0;
    c->last = c->start = tn_ticks;
    c->id = id;
    switch ((ulong)type) {
    case SND_T_PLAIN:
        c->vol = a1 >= 0 ? (uchar)a1 : (uchar)SND_RES_VOL(id);
        c->pan = (uchar)a2;
        break;
    case SND_T_ENTITY:
        if (loops != 1)
            killobj_289554((short)a1, WATCOM_FN(snd_kill_entity_cb), -1);
        /* fall through */
    case SND_T_ENTITY2:
        c->u.ent = (short)a1;
        snd_chan_update_dist(c);
        snd_chan_spatialize(c);
        pri = c->vol + 0x10;
        break;
    case SND_T_POS:
        c->u.pos[0] = a1;
        c->u.pos[1] = a2;
        c->u.pos[2] = a3;
        snd_chan_update_dist(c);
        snd_chan_spatialize(c);
        pri = c->vol + 0x10;
        break;
    case SND_T_SPEECH:
        c->vol = (uchar)a1;
        base = snd_speech_vol;
        c->pan = (uchar)a2;
        break;
    case SND_T_LOW:
        c->vol = (uchar)SND_RES_VOL(id);
        c->pan = (uchar)a2;
        break;
    default:
        return -1;
    }
    if (c->vol == 0 && c->loops == 1)
        return -1;
#ifndef TN_WATCOM_ABI
    if (chan >= 0)
        snd_reserved[chan] = 1;
#endif
    if (digi_2EBC40(pri) == 0 && pri > 0x10)
        if (!noisy_2495A0(id))
            snd_kill_low_priority();      /* result not used */
#ifndef TN_WATCOM_ABI
    if (chan >= 0)
        snd_reserved[chan] = 0;
#endif
    h = snd_chan_start_sample(id, chan, (uchar)pri);
    c->hnd = h;
    if (h < 0)
        return h;
    snd_busy = 1;
    snd_finish_pending();
    snd_busy = 0;
    digi_2EBDA0(h, SND_PARM_LOOPS, loops);
    if (type == SND_T_ENTITY2)
        digi_2EBDA0(h, SND_PARM_VOL, 0);
    else
        digi_2EBDA0(h, SND_PARM_VOL, (uchar)((c->vol * base) >> 7));
    digi_2EBDA0(h, SND_PARM_PAN, c->pan);
    c->rate = snd_get_parm(h, SND_PARM_RATE);
    if (type == SND_T_SPEECH)
        snd_speech_duck(TRUE);
    snd_start_sample(h);
    return chan;
}

void snd_chan_set_handle_ptr(long chan, long *p)
{
    if (chan >= 0 && chan <= snd_channels - 1)
        CHAN(chan)->handle_ptr = p;
}

/* sets the channel volume (before the global volume); returns the old one, -1 if none */
long snd_chan_set_vol(long chan, uchar vol)
{
    SndChan *c;
    signed char h;
    long old;
    uchar base;

    if (!snd_channels || chan < 0)
        return -1;
    c = CHAN(chan);
    h = c->hnd;
    if (h == -1)
        return -1;
    base = c->type == SND_T_SPEECH ? snd_speech_vol : snd_sfx_vol;
    old = c->vol;
    c->vol = vol;
    digi_2EBDA0(h, SND_PARM_VOL, (uchar)((vol * base) >> 7));
    return old;
}

void snd_chan_set_vol_pan(long chan, long vol, long pan)
{
    SndChan *c;
    long h;

    if (!snd_channels || chan < 0)
        return;
    c = CHAN(chan);
    h = c->hnd;
    if (h == -1)
        return;
    c->vol = (uchar)vol;
    c->pan = (uchar)pan;
    digi_2EBDA0(h, SND_PARM_VOL, vol);
    digi_2EBDA0(h, SND_PARM_PAN, pan);
}

long snd_chan_get_vol(long chan)
{
    if (chan < 0 || CHAN(chan)->hnd < 0)
        return -1;
    return snd_get_parm(CHAN(chan)->hnd, SND_PARM_VOL);
}

uchar snd_res_volume(ushort id)
{
    return (uchar)((SND_RES_VOL(id) * snd_sfx_vol) >> 7);
}

void snd_chan_fade(long chan, long vol, long steps, uchar end)
{
    signed char h;

    if (chan < 0)
        return;
    h = CHAN(chan)->hnd;
    if (h < 0)
        return;
    snd_fade(h, (uchar)((vol * snd_sfx_vol) >> 7), steps, end != 0);
}

void snd_chans_init(void)
{
    long i;

    for (i = 0; i < SND_MAX_CHANS; i++) {
        snd_chans[i].hnd = -1;
        snd_chans[i].start = 0xFFFFFFFF;
        snd_chans[i].ncb = 0;
        snd_chans[i].handle_ptr = NULL;
    }
}

/* after the digital driver is installed: 8 channels on driver 1 (no pairs), else 16 */
void snd_dig_setup(SndCard *c)
{
    if (c->idx == 1) {
        snd_channels = 8;
        snd_flags &= ~SNDF_PAIRS;
    } else
        snd_channels = 16;
    if (snd_set_sample_count(snd_channels) != 0)
        snd_channels = 0;
    snd_set_finish(WATCOM_FN(sndlock_2491C0), 0);
    snd_pair_lock();
}

/* after the MIDI driver is installed (unreferenced; SndInit has the same code inline) */
void snd_midi_setup(SndCard *c)
{
    snd_flags = (snd_flags & ~SNDF_GM) | ((c->drvflags & 4) ? SNDF_GM : 0);
    snd_flags = (snd_flags | 8) & ~7;
    snd_set_sequence_count(snd_flags & 8);
    music_end_cb = WATCOM_FN(sndlock_24919C);
}

/* ------------------------------------------------------------------ playing */

long snd_play_2d(ushort id, long loops, long pri)
{
    return SndPlay(id, SND_T_PLAIN, pri, loops, -1, 0x40, 0);
}

long snd_play_2d_once(ushort id)
{
    return SndPlay(id, SND_T_PLAIN, 0x40, 1, -1, 0x40, 0);
}

/* fade a channel in (from 0 to its own volume) or out */
void snd_chan_fade_io(long chan, long in, long steps, uchar end)
{
    long vol = 0;

    if (!snd_channels || chan < 0)
        return;
    if (in) {
        snd_chan_set_vol(chan, 0);
        vol = (uchar)((SND_RES_VOL(CHAN(chan)->id) * snd_sfx_vol) >> 7);
    }
    snd_chan_fade(chan, vol, steps, end);
}

void snd_chan_crossfade(long out, long in, long steps, uchar end)
{
    if (!snd_channels || out < 0 || in < 0)
        return;
    snd_chan_fade(out, 0, steps, end);
    snd_chan_set_vol(in, 0);
    snd_chan_fade(in, (uchar)((SND_RES_VOL(CHAN(in)->id) * snd_sfx_vol) >> 7), steps, 0);
}

long snd_null_2486A4(void)
{
    return 0;
}

/* frame hook (slot 1) */
void SndUpdateHook(void)
{
    if (snd_channels) {
        weapons_29079C();
        noisy_249488();
        snd_update_channels();
    }
    if (snd_flags & SNDF_MUSIC)
        music_24648C();
}

ushort snd_chan_get_id(long chan)
{
    if (chan < 0 || CHAN(chan)->hnd < 0)
        return 0;
    return CHAN(chan)->id;
}

/* plays id unless it is already playing; returns the number playing or SndPlay's result */
long snd_play_if_idle(ushort id, long vol)
{
    long r = snd_count_playing(id);

    if (r == 0)
        r = SndPlay(id, SND_T_PLAIN, 0x3F, 1, vol, 0x3F, 0);
    return r;
}

void snd_set_speech_volume(long pct)
{
    long v = pct * 127 / 100;

    if (v > 0x7F)
        v = 0x7F;
    if (v < 0)
        v = 0;
    snd_speech_vol = (uchar)v;
}

/* plays the test sound 0x7B7 at the new effects volume (the volume itself is not changed) */
void snd_preview_sfx_volume(long pct)
{
    uchar old = snd_sfx_vol;
    long v = pct * 127 / 100, k;

    if (v > 0x7F)
        v = 0x7F;
    if (v < 0)
        v = 0;
    for (k = 0; k < 3; k++)
        if (snd_count_playing(0x7B7 + k))
            return;
    snd_sfx_vol = (uchar)v;
    if (!snd_count_playing(0x7B7))
        SndPlay(0x7B7, SND_T_PLAIN, 0x3F, 1, 0x40, 0x3F, 0);
    snd_sfx_vol = old;
}

void snd_set_sfx_volume(long pct)
{
    long v = pct * 127 / 100;

    if (v > 0x7F)
        v = 0x7F;
    else if (v < 0)
        v = 0;
    snd_set_type_volume(SND_T_PLAIN, v);
    snd_sfx_base = v;
    snd_sfx_vol = (uchar)v;
}

/* sets the plain sounds' volume and plays the test sound 0x7E1 at it (snd_sfx_vol unchanged) */
void snd_set_sfx_volume_test(long pct)
{
    uchar old = snd_sfx_vol;
    long v = pct * 127 / 100;

    if (v > 0x7F)
        v = 0x7F;
    else if (v < 0)
        v = 0;
    snd_set_type_volume(SND_T_PLAIN, v);
    snd_sfx_vol = (uchar)v;
    if (!snd_count_playing(0x7E1))
        SndPlay(0x7E1, SND_T_PLAIN, 0x3F, 1, 0x64, 0x3F, 0);
    snd_sfx_vol = old;
}

void snd_set_qsound(long on)
{
    if (snd_channels >= 8)
        snd_flags = (snd_flags & ~SNDF_PAIRS) | ((on == 1) << 5);
    else
        snd_flags &= ~SNDF_PAIRS;
}

/* end of the left test sound: the right one */
void snd_stereo_test_cb(void)
{
    SndPlay(0x793, SND_T_PLAIN, 0x3F, 1, 0x6E, snd_stereo_flip ? 0 : 0x7F, 0);
}

void snd_stereo_test(long mode)
{
    long r;

    if (!snd_channels)
        return;
    snd_stereo_flip = mode - 1;
    if (snd_stereo_reverse)
        snd_stereo_flip = snd_stereo_flip == 0;
    if (snd_count_playing(0x792) || snd_count_playing(0x793))
        return;
    r = SndPlay(0x792, SND_T_PLAIN, 0x3F, 1, 0x6E, snd_stereo_flip ? 0x7F : 0, 0);
    if (r >= 0)
        snd_chan_add_callback(r, WATCOM_FN(snd_stereo_test_cb), 0);
}

void snd_set_stereo_reverse(uchar v)
{
    if (snd_channels)
        snd_stereo_reverse = v - 1;
}

/* 0, 1, 2: 4, 8 or 16 channels */
void snd_set_channel_level(long level)
{
    uchar old = snd_channels;

    if (level > 2)
        level = 2;
    if (level < 0)
        level = 0;
    if (!snd_channels)
        return;
    snd_channels = 4 << level;
    if (old == snd_channels)
        return;
    switch (snd_channels) {
    case 4:
        snd_flags &= ~(SNDF_PAIRS | SNDF_PAIRFLAG);
        break;
    case 8:
        snd_flags |= SNDF_PAIRS;
        snd_flags &= ~SNDF_PAIRFLAG;
        break;
    case 16:
        snd_flags |= SNDF_PAIRS | SNDF_PAIRFLAG;
        break;
    }
    if (snd_channels > 0x10)
        snd_channels = 0x10;
    snd_set_sample_count(snd_channels);
}

/* percent; non-GM drivers get a curve.  Quirk: pct 100/101 read 2 bytes past the curve. */
void snd_set_music_volume(long pct)
{
    long v;

    if (!(snd_flags & SNDF_MUSIC))
        return;
    if (!(snd_flags & SNDF_GM))
        v = music_curve[pct / 2] * 127;
    else
        v = pct * 0x73;
    snd_music_vol = (uchar)(v / 100);
    snd_music_cur = snd_music_vol;
    snd_set_midi_volume(snd_music_cur);
}

long snd_chan_active(long chan)
{
    if (chan < 0)
        return -2;
    return CHAN(chan)->hnd >= 0;
}

/* ------------------------------------------------------------------ scheduled sounds */

void snd_sched_init(void)
{
    long i;

    for (i = 0; i < 8; i++) {
        snd_sched_free[i] = &snd_sched_pool[i];
        snd_sched_pool[i].next = NULL;
    }
    snd_sched_free_count = 8;
}

SndEvent *snd_sched_pop(void)
{
    SndEvent *e = snd_sched_list;

    if (e == NULL)
        return NULL;
    snd_sched_list = e->next;
    return e;
}

/* Plays the events that are due.  Quirk: a played event goes back on the free stack at
 * [count + 1] (snd_sched_alloc takes [count - 1]). */
void snd_sched_run(void)
{
    SndEvent *e = snd_sched_cur;
    SndEvent *list = snd_sched_list;

    if (e == NULL) {
        e = list;
        if (list)
            list = list->next;
    }
    while (e != NULL && game_time >= e->time) {
        snd_sched_cur = e;
        snd_sched_list = list;
        SndPlay(e->id, e->type, e->pri, e->loops, e->a1, e->a2, e->a3);
#ifndef TN_WATCOM_ABI
        /* G26: the quirk put a played event one place above the top of the free stack (the
         * first one past its end), and left the place below it holding an event still in use:
         * when a nearer thunder clap played before a farther one still on its way, the next two
         * claps were given the same event and the first was never heard */
        snd_sched_free[snd_sched_free_count++] = snd_sched_cur;
#else
        snd_sched_free[++snd_sched_free_count] = snd_sched_cur;
#endif
        list = snd_sched_list;
        e = list;
        if (list)
            list = list->next;
    }
    snd_sched_list = list;
    snd_sched_cur = e;
}

/* takes a free event, sets its time to game_time + delay and inserts it in the sorted list;
 * returns its argument block (&e->id) or NULL */
ushort *snd_sched_alloc(ulong delay)
{
    long n = snd_sched_free_count;
    SndEvent *e, *p, *q;

    if (n > 0)
        e = snd_sched_free[--n];
    else
        e = NULL;
    p = snd_sched_list;
    if (e == NULL) {
        snd_sched_free_count = n;
        return NULL;
    }
    e->time = game_time + delay;
    if (p == NULL || e->time < p->time) {
        snd_sched_list = e;
        e->next = p;
    } else {
        while ((q = p->next) != NULL) {
            if (e->time <= q->time) {
                e->next = q;
                p->next = e;
                break;
            }
            p = q;
        }
        if (p->next == NULL) {
            p->next = e;
            e->next = NULL;
        }
    }
    snd_sched_free_count = n;
    return &e->id;
}

/* plays a sound (SndPlay's arguments) after delay units of game time */
bool SndSchedule(ulong delay, ushort id, long type, uchar pri, long loops, long a1, long a2, long a3)
{
    SndEvent *e;
    ushort *p;

    if (!snd_channels)
        return FALSE;
    p = snd_sched_alloc(delay);
    if (p == NULL)
        return FALSE;
    e = (SndEvent *)((char *)p - 4);
    e->type = type;
    e->id = id;
    e->pri = pri;
    e->loops = loops;
    e->a1 = a1;
    e->a2 = a2;
    e->a3 = a3;
    return TRUE;
}

/* ------------------------------------------------------------------ volumes */

long snd_count_playing(ushort id)
{
    long i, k = 0;

    for (i = 0; i < snd_channels; i++)
        if (CHAN(i)->hnd >= 0 && CHAN(i)->id == id)
            k++;
    return k;
}

/* While speech plays (when the first one starts / the last one ends), lower the effects
 * and the music. */
void snd_speech_duck(bool start)
{
    long s = snd_speech_vol, i, k = 0;

    snd_duck_sfx = s / 2;
    snd_duck_music = s * 3 / 5;
    if (snd_duck_sfx > snd_sfx_base)
        snd_duck_sfx = snd_sfx_base;
    if (snd_duck_music > snd_music_cur)
        snd_duck_music = snd_music_cur;
    for (i = 0; i < snd_channels; i++)
        if (CHAN(i)->type == SND_T_SPEECH && CHAN(i)->hnd >= 0)
            k++;
    if (k != 1)
        return;
    if (start) {
        snd_sfx_vol = (uchar)snd_duck_sfx;
        snd_set_type_volume(SND_T_PLAIN, snd_duck_sfx);
        snd_set_midi_volume(snd_duck_music);
    } else {
        snd_sfx_vol = (uchar)snd_sfx_base;
        snd_set_type_volume(SND_T_PLAIN, snd_sfx_base);
        snd_set_midi_volume(snd_music_cur);
    }
}

/* new volume for the playing sounds of one type (those with a volume of their own) */
void snd_set_type_volume(long type, long vol)
{
    SndChan *c = snd_chans;
    long i;

    for (i = 0; i < snd_channels; i++, c++) {
        if (c->hnd < 0 || type != c->type)
            continue;
        if (SND_RES_VOL(c->id) == 0)
            continue;
        c->vol = (uchar)((SND_RES_VOL(c->id) * vol) >> 7);
        digi_2EBDA0(c->hnd, SND_PARM_VOL, c->vol);
    }
}

/* footstep-like sounds: kind 0/1 (left/right) pick one of 4 variants of set n with a volume
 * from speed; kind 2 the set's single sound in the middle */
void snd_play_step(long kind, long n, fix speed)
{
    long n5, r, vol, pan;
    ushort id;

    if (!snd_channels)
        return;
    n5 = n * 5;
    if (kind != 2) {
        r = RndRange(&snd_rnd, 0, 0x7FFF) >> 4;
        pan = (kind << 6) + 0x20;
        vol = ((fix)(((long long)speed * 0x100000) >> 16) >> 16) + r % 10;
        id = 0x795 + r % 4 + n5;
    } else {
        id = 0x799 + n5;
        pan = 0x40;
        vol = (uchar)((SND_RES_VOL(id) * snd_sfx_vol) >> 7);
    }
    if (vol != 0)
        SndPlay(id, SND_T_PLAIN, 4, 1, vol, pan, 0);
}

void snd_null_248FA8(void)
{
}

/* hit sound idx (0..0x21) from one side (0 front, 1 left, 2 back?, 3 right); a variant set
 * when the player is badly damaged */
void snd_play_hit(long idx, long unused, uchar side)
{
    long vol = 0x78, pan;
    ushort id;

    if (!snd_channels || idx > 0x21 || (id = snd_hit_ids[idx]) == 0)
        return;
    if (damage_27DF14(game_player_entity, 0) < 0x28) {
        vol = 0x7F;
        pan = 0x40;
        id += 3;
    } else
        switch (side) {
        case 1:
            pan = 0;
            break;
        case 2:
            pan = 0x40;
            vol = 0x64;
            break;
        case 3:
            pan = 0x7F;
            break;
        default:
            pan = 0x40;
            break;
        }
    SndPlay((ushort)(id + RndRange(&snd_rnd, 0, 0x7FFF) % 3), SND_T_PLAIN, vol + 0x10, 1, vol, pan, 0);
}

/* channel callback of the looping sounds */
void snd_loop_end_cb(ushort id, SndLoop *l)
{
    if ((l->flags & 0xF) != 3)
        l->chan = -1;
}

void snd_loop_ctl(long idx, long on)
{
    SndLoop *l;

    if (idx == -1)
        return;
    l = &snd_loops[idx];
    if (on) {
        if (l->chan >= 0)
            return;
        l->chan = SndPlay(l->id, SND_T_PLAIN, 0x40, 1, -1, 0x40, 0);
        if (l->chan >= 0)
            snd_chan_add_callback(l->chan, WATCOM_FN(snd_loop_end_cb), (long)l);
    } else if ((l->flags & 0xF) == 3)
        l->chan = -1;
    else if (!(l->flags & 0xF0) && l->chan >= 0)
        snd_chan_stop(l->chan);
}

/* radio view: music to 3/5 and the samples to half */
void snd_radio_mode(bool on)
{
    if (on) {
        weapons_290858();
        snd_set_midi_volume(snd_music_cur * 3 / 5);
        snd_set_digital_volume(0x40);
    } else {
        snd_set_midi_volume(snd_music_cur);
        snd_set_digital_volume(0x7F);
    }
}
