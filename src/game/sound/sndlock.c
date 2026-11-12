/* sndq.c -- Terra Nova sound callback queues (module M003, between sound.c and noisy.c in
 * __FF.EXE; source file name unknown).  Decompiled from __FF.EXE v1.09; names ours.
 *
 *   0x249170 sndlock_249170            0x24919C sndlock_24919C     0x2491C0 sndlock_2491C0
 *   0x2491E0 sndlock_2491E0       0x2491FC sndlock_2491FC         0x249230 sndlock_249230
 *
 * The callbacks run at interrupt time from the music / digital sound layers (sound.c registers
 * them: sndlock_24919C with snd_set_sequence_count 0x2EFA0C, sndlock_2491C0 with
 * snd_set_finish, sndlock_2491FC with music_set_play_cb, sndlock_2491E0 with
 * music_set_prefix_cb), so they only append to small queues that sound.c drains later;
 * sndlock_249170 / sndlock_249230 page-lock the code and the queues (DPMI).
 * Quirks kept: the queues hold 31 entries (count < 31) in 32-slot arrays; the callbacks return
 * whatever was left in eax (the new count, or their first argument); sndlock_2491E0 divides by
 * its third argument without a zero test.
 */
#include "g1_b.h"
#include "g1_c.h"      /* music_set_volume etc. */

/* The queue variables carry music.c's names (one name per address since the G2 merge; this
 * file had sndq_seq_count / play_word / play_seq / prefix_val / vol / vol_ms and sndq_seq[n] =
 * music_unlock_ids[n + 1]). */
#ifndef TN_LINK_ORIGINAL_DATA
long music_unlock_count;               /* snd_finish_count / snd_finish_queue: sound.c */
ushort music_cur;
char music_cur_seq;
long music_rnd, music_fade_vol, music_fade_time;
long music_unlock_ids[33];
#endif

void sndlock_249170(void)
{
    dpmi_lock_region((void *)sndlock_249170, (void *)sndlock_249230);
    dpmi_lock_region(&music_unlock_ids[1], &music_rnd);
}

/* a sequence ended: queue its value */
void sndlock_24919C(long v)
{
    long n = music_unlock_count;
    if (n < 0x1F) {
        music_unlock_ids[n + 1] = v;
        music_unlock_count = n + 1;
    }
}

/* a digital sample finished: queue the second argument */
void sndlock_2491C0(long a, long chan)
{
    long n = snd_finish_count;
    if (n < 0x1F) {
        snd_finish_queue[n] = chan;
        snd_finish_count = n + 1;
    }
}

long sndlock_2491E0(long a, long b, long c)
{
    long r = music_rnd % c;
    music_rnd = 0;
    return r;
}

/* a sequence started: remember it and apply a pending volume change */
long sndlock_2491FC(long seq, long w)
{
    long r = seq;
    music_cur = (ushort)w;
    music_cur_seq = (char)seq;
    if (music_fade_time)
        r = music_set_volume((char)seq, music_fade_vol, music_fade_time);
    music_fade_time = 0;
    return r;
}

void sndlock_249230(void)
{
    dpmi_unlock_region((void *)sndlock_249170, (void *)sndlock_249230);
    dpmi_unlock_region(&music_unlock_ids[1], &music_rnd);
}
