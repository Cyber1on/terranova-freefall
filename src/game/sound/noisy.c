/* noisy.c -- Terra Nova ambient object sounds (r:\prj\tn\v109\tn\src\tn\noisy.c, module M004).
 * Decompiled from __FF.EXE v1.09; function names ours.
 *
 *   0x249260 noisy_kill_cb        0x2492C4 noisy_2492C4           0x249320 noisy_249320
 *   0x249340 noisy_remove         0x249388 noisy_add            0x249474 noisy_snd_done_cb
 *   0x249488 noisy_249488         0x2495A0 noisy_2495A0   0x2495F8 noisy_loudest
 *   0x249680 noisy_sort           0x2496FC noisy_sort_vol       0x249784 noisy_calc_vol
 *   0x249874 noisy_select         0x249928 noisy_oneshot
 *
 * Up to 64 objects carry a looping sound (noisy_tab, 13 bytes each).  noisy_order keeps the
 * entries sorted by sound id (insertion sort, noisy_sort) and, within one id, by volume
 * (noisy_sort_vol).  Every frame noisy_249488 computes each entry's 3D volume and pan
 * (noisy_calc_vol), lets the two loudest entries of each sound id play (noisy_select: volume
 * above 6), starts sounds that became audible (after 750 ms of silence), stops the others
 * after 2 s, and fades restarted sounds in over 32 frames.  Sound ids 0x815..0x81E are
 * one-shots: they play once when the player gets within 2.0 (1.0 for 0x819 and up) and the
 * entry is freed (noisy_oneshot); noisy_add picks one of 2 or 3 variants at random.
 * Quirks kept:
 *  - noisy_remove and noisy_kill_cb stop the sound but leave the channel field; the entry is
 *    reused only once the sound's end callback has set it to -1 (noisy_add needs both).
 *  - noisy_loudest returns the whole ecx register (high word = its caller's ecx); callers use
 *    the low word.  When the scan reaches entry 0 it reads noisy_order[-1] (the dword before
 *    the table, music_unlock_ids[32]) as an index.
 *  - noisy_oneshot's random rate offset is (r % 4) * -128 + 256 (0x100, 0x80, 0, -0x80).
 *  - noisy_oneshot frees the entry whether the sound could play or not (but not when it is
 *    out of range or sound is off), and does not reset the audible flag.
 */
#include "g1_b.h"

#ifndef TN_LINK_ORIGINAL_DATA
long noisy_count;
long noisy_order[NOISY_MAX];
NoisyRec noisy_tab[NOISY_MAX];
#endif

#define REC(k) (&noisy_tab[noisy_order[k]])

/* the object died: drop its entries (killobj callback) */
void noisy_kill_cb(long obj)
{
    long i;
    for (i = 0; i < NOISY_MAX; i++) {
        NoisyRec *r = &noisy_tab[i];
        if ((short)obj != r->obj)
            continue;
        if (r->chan >= 0)
            snd_chan_stop(r->chan);
        r->snd = 0;
        r->fade = 0;
        noisy_count--;
        r->obj = 0;
        r->time = 0;
    }
    noisy_sort();
}

void noisy_2492C4(void)
{
    long i;
    for (i = 0; i < NOISY_MAX; i++) {
        NoisyRec *r = &noisy_tab[i];
        r->chan = -1;
        r->time = 0;
        r->snd = 0;
        r->pan = 0;
        noisy_order[i] = i;
        r->vol = 0;
        r->audible = 0;
        r->fade = 0;
    }
    noisy_count = 0;
}

void noisy_249320(void)
{
    long i;
    for (i = 0; i < NOISY_MAX; i++)
        noisy_remove(i);
    noisy_2492C4();
    noisy_count = 0;
}

void noisy_remove(long i)
{
    NoisyRec *r = &noisy_tab[i];
    if (r->chan >= 0)
        snd_chan_stop(r->chan);
    r->snd = 0;
    noisy_sort();
    noisy_count--;
}

/* attach looping sound snd to object obj; returns the entry or -1 */
long noisy_add(long obj, long snd)
{
    long i, slot = -1, rnd;
    ushort s = (ushort)snd;
    NoisyRec *r;

    if (!snd_channels)
        return -1;
    for (i = 0; i < NOISY_MAX; i++)
        if (noisy_tab[i].snd == 0 && noisy_tab[i].chan == -1) {
            slot = i;
            break;
        }
    if (slot == -1)
        return -1;
    r = &noisy_tab[slot];
    r->obj = (short)obj;
    r->time = 0;
    if (s >= 0x815 && s <= 0x81E) {
        rnd = RndRange(&snd_rnd, 0, 0x7FFF);
        if (s >= 0x819)
            s += rnd % 3;
        else
            s += rnd & 1;
    }
    killobj_289554((short)obj, WATCOM_FN(noisy_kill_cb), -1);
    r->snd = s;
    noisy_sort();
    noisy_count++;
    return slot;
}

/* end of a noisy sound (sound.c callback, user data = the entry) */
ulong noisy_snd_done_cb(long h, NoisyRec *r)
{
    r->chan = -1;
    r->fade = 0x20;
    r->time = game_time;
    return game_time;
}

void noisy_249488(void)
{
    long k, h;
    NoisyRec *r;

    if (!noisy_count)
        return;
    noisy_select();
    for (k = 0; k < NOISY_MAX; k++) {
        r = REC(k);
        if (r->snd == 0)
            continue;
        if (r->audible) {
            if (r->chan >= 0 || game_time - r->time <= 750)
                continue;
            h = SndPlay(r->snd, 5, 0x40, 0, r->obj, 0, 0);
            r = REC(k);
            r->chan = (schar)h;
            if (h < 0)
                continue;
            r->time = game_time;
            if (r->snd == 0x7FA)
                snd_chans[h].no_doppler |= 1;
            snd_chan_add_callback(h, WATCOM_FN(noisy_snd_done_cb), (long)REC(k));
        } else {
            if (r->chan < 0 || game_time - r->time <= 2000)
                continue;
            snd_chan_stop(r->chan);
        }
    }
}

/* stop one channel playing the loudest noisy sound unless that is snd; TRUE if stopped */
bool noisy_2495A0(ushort snd)
{
    ushort s = noisy_loudest();
    long k;
    NoisyRec *r;

    if (s == 0 || s == snd)
        return FALSE;
    for (k = 0; k < NOISY_MAX; k++) {
        r = REC(k);
        if (s == r->snd && r->chan >= 0) {
            snd_chan_stop(r->chan);
            return TRUE;
        }
    }
    return FALSE;
}

/* the noisy sound id with the most channels playing (0 if none) */
ushort noisy_loudest(void)
{
    ushort best = 0, s;
    long max = 0, k = 63, n;
    NoisyRec *r;

    s = REC(63)->snd;
    while (s != 0 && k >= 0) {
        n = snd_count_playing(s);
        if (n > max) {
            max = n;
            best = s;
        }
        for (;;) {                      /* skip the rest of this id (k = 0 reads order[-1]) */
            r = &noisy_tab[noisy_order[k - 1]];
            k--;
            if (s != r->snd)
                break;
            if (k == 0)
                return best;
        }
        if (k < 0)
            break;
        s = r->snd;
    }
    return best;
}

/* insertion sort of noisy_order by sound id */
void noisy_sort(void)
{
    long i, j, v;
    for (i = 1; i < NOISY_MAX; i++) {
        v = noisy_order[i];
        for (j = i - 1; j >= 0; j--) {
            if (!(noisy_tab[v].snd < noisy_tab[noisy_order[j]].snd))
                break;
            noisy_order[j + 1] = noisy_order[j];
        }
        noisy_order[j + 1] = v;
    }
}

/* insertion sort by volume within runs of the same sound id */
void noisy_sort_vol(void)
{
    long i, j, v;
    for (i = 1; i < NOISY_MAX; i++) {
        v = noisy_order[i];
        for (j = i - 1; j >= 0; j--) {
            NoisyRec *o = &noisy_tab[noisy_order[j]];
            if (noisy_tab[v].vol >= o->vol || noisy_tab[v].snd != o->snd)
                break;
            noisy_order[j + 1] = noisy_order[j];
        }
        noisy_order[j + 1] = v;
    }
}

/* volume and pan of every entry from its object's position; fade restarted sounds in */
void noisy_calc_vol(void)
{
    long k, vol, pan;
    NoisyRec *r;
    GameEntity *e;

    for (k = 0; k < NOISY_MAX; k++) {
        r = REC(k);
        if (r->snd == 0)
            continue;
        e = &game_entities[r->obj];
        snd_spatial_vol(r->snd, e->x, e->y, e->z, &vol, &pan);
        r = REC(k);
        r->pan = (uchar)pan;
        r->vol = (uchar)vol;
        if (r->chan < 0)
            continue;
        if (r->fade) {
            r->fade--;
            vol = (((0x20 - r->fade) * vol) >> 5) & 0xFF;
        }
        snd_chan_set_vol_pan(REC(k)->chan, vol, pan);
    }
}

/* per sound id (loudest first) only the two loudest entries with volume > 6 are audible */
void noisy_select(void)
{
    long k, n;
    ushort s;
    NoisyRec *r;

    noisy_calc_vol();
    noisy_sort_vol();
    k = 63;
    for (;;) {
        s = REC(k)->snd;
        if (s == 0)
            return;
        n = 0;
        for (; k >= 0; k--, n++) {
            r = REC(k);
            if (s != r->snd)
                break;
            if (r->snd >= 0x815 && r->snd <= 0x81E) {
                r->audible = 0;
                noisy_oneshot(r);
            } else {
                REC(k)->audible = (n < 2 && REC(k)->vol > 6) ? 1 : 0;
            }
        }
        if (k < 0)
            return;
    }
}

/* one-shot entry: play it once the player is close enough, then free the entry */
void noisy_oneshot(NoisyRec *r)
{
    GameEntity *o = &game_entities[r->obj];
    GameEntity *p = &game_entities[game_player_entity];
    fix dz = o->z - p->z, d;
    long h, rnd, rate;

    d = fix_safe_pyth_dist(o->x - p->x, o->y - p->y, "r:\\prj\\tn\\v109\\tn\\src\\tn\\noisy.c", 0x202);
    d = fix_safe_pyth_dist(d, dz, "r:\\prj\\tn\\v109\\tn\\src\\tn\\noisy.c", 0x202);
    if (!snd_channels)
        return;
    if (d > (r->snd < 0x819 ? 0x20000 : 0x10000))
        return;
    if (d == 0)
        h = -1;
    else
        h = SndPlay(r->snd, 2, 0x40, 1, r->obj, 0, 0);
    if (h >= 0) {
        rnd = RndRange(&snd_rnd, 0, 0x7FFF);
        rate = 0x100 - ((rnd % 4) << 7) + snd_chans[h].rate;
        digi_2EBDA0(snd_chans[h].hnd, 2, rate);
        snd_chans[h].no_doppler |= 1;
    }
    r->snd = 0;
    r->chan = -1;
    r->obj = 0;
    noisy_count--;
}
