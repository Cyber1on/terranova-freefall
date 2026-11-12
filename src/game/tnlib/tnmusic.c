/* tnmusic.c -- TN music layer over the Miles AIL 3 XMIDI sequence API, plus a small list of
 * periodic calls (Terra Nova v1.09; names ours, no assert path).  Decompiled from __FF.EXE.
 *
 *   0x2EE510 music_lock          0x2EE53C music_get_handle    0x2EE548 music_get_seq
 *   0x2EE564 music_end           0x2EE580 music_check_all     0x2EE60C music_count_playing
 *   0x2EE65C music_find_free     0x2EE6B0 music_play          0x2EE784 music_start
 *   0x2EE82C music_start_exclusive 0x2EE8B4 music_pause      0x2EE8E8 music_resume
 *   0x2EE91C music_set_volume    0x2EE978 music_get_volume    0x2EE994 music_set_user
 *   0x2EE9AC music_get_user      0x2EE9C4 music_set_tempo     0x2EEA14 music_get_tempo
 *   0x2EEA2C music_branch        0x2EEA48 music_set_trigger_cb 0x2EEA58 music_trigger_hook
 *   0x2EEA80 music_set_beat_cb   0x2EEA90 music_beat_hook     0x2EEACC music_set_prefix_cb
 *   0x2EEADC music_prefix_hook   0x2EEB00 music_set_1c        0x2EEB18 music_set_mark
 *   0x2EEB40 music_seq_hook      0x2EEBE0 music_register_hooks 0x2EEC48 music_play_ex
 *   0x2EED58 music_default_settings 0x2EED6C music_set_start_cb 0x2EED74 music_current_playing
 *   0x2EEDA4 music_set_play_cb   0x2EEDAC music_set_next      0x2EEE20 music_end_beat_hook
 *   0x2EEE34 music_end_trigger_hook 0x2EEE4C music_end_at_beat 0x2EEE6C music_end_at_trigger
 *   0x2EEE8C music_unlock        0x2EEEC0 periodic_clear      0x2EEED0 periodic_add
 *   0x2EEF20 periodic_remove     0x2EEF84 periodic_run        0x2EEFF4 periodic_update
 *
 * Up to 8 sequences (music_seq_count handles allocated elsewhere, music_handle[]).  The AIL
 * user data word 0 of each handle holds its index; the *_hook functions are the AIL (cdecl)
 * callbacks, which pass the index on to the game's Watcom callbacks.  A sequence can name a
 * successor (next) that music_seq_hook starts when it ends.
 * Map note: 0x2EE53C/548/564 and 0x2EEEC0 are separate functions (the map counted them in
 * music_2EE510 / music_2EEE8C).
 */
#include "t2_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
MusicSeq music_seq[MUSIC_SEQS];
HSEQUENCE music_handle[MUSIC_SEQS];
MusicMark music_mark[MUSIC_SEQS];
int music_seq_count;
MusicBeat music_beat;
Periodic periodic_tab[8];
ulong periodic_next_due;
void *music_beat_cb, *music_trigger_cb, *music_prefix_cb, *music_1c;
long music_20;
void *music_play_cb, *music_end_cb, *music_start_cb;
signed char music_current = -1;
int periodic_count;
long snd_last_error;
#endif

#define H(i) music_handle[(signed char)(i)]
#define S(i) music_seq[(signed char)(i)]

/* DPMI-locks the callback pointers and this module's code (the AIL callbacks run in the
 * timer interrupt). */
void music_lock(void)
{
    dpmi_lock_region(&music_beat_cb, &music_current);
    dpmi_lock_region((void *)music_lock, (void *)music_unlock);
}

HSEQUENCE music_get_handle(char i)
{
    return H(i);
}

MusicSeq *music_get_seq(char i)
{
    return &S(i);
}

void music_end(char i)
{
    AIL_end_sequence(H(i));
}

/* Ends every sequence that is not done (and drops its successor); reports the done ones
 * still marked playing to music_end_cb. */
void music_check_all(void)
{
    int k;
    for (k = 0; k < music_seq_count; k++) {
        if (AIL_sequence_status(music_handle[k]) != SEQ_DONE) {
            music_seq[k].next = -1;
            AIL_end_sequence(music_handle[k]);
        } else if (music_seq[k].playing) {
            if (music_end_cb)
                WATCOM_CALL(music_end_cb, music_seq[k].id);
            music_seq[k].playing = 0;
        }
    }
}

int music_count_playing(void)
{
    int k, n = 0;
    for (k = 0; k < music_seq_count; k++)
        if (AIL_sequence_status(music_handle[k]) == SEQ_PLAYING)
            n++;
    return n;
}

/* First handle that is done and not in use, or -1.  (Callers pass a priority in al; it is
 * not used.) */
char music_find_free(void)
{
    signed char k;
    for (k = 0; k < music_seq_count; k++)
        if (AIL_sequence_status(H(k)) == SEQ_DONE && !S(k).playing)
            return k;
    return -1;
}

/* Prepares a sequence (not started).  id -1 means "use the index". */
char music_play(void *data, long id, long num, uchar prio)
{
    signed char i = music_find_free();
    long r;
    MusicSeq *s;
    if (i == -1) {
        snd_last_error = 0x200;
        return -1;
    }
    r = AIL_init_sequence(H(i), data, num);
    AIL_set_sequence_user_data(H(i), 0, i);
    if (r == 0 || r == -1)
        return -1;
    s = &S(i);
    s->playing = 1;
    s->tempo = 100;
    s->vol = 0x7F;
    s->next = -1;
    s->id = id != -1 ? id : i;
    s->num = num;
    s->data = data;
    music_register_hooks(i);
    return i;
}

int music_start(char i)
{
    MusicSeq *s = &S(i);
    HSEQUENCE h = H(i);
    music_current = i;
    if (!s->playing)
        return -1;
    if (music_start_cb)
        WATCOM_CALL(music_start_cb, (signed char)i, (long)s);
    AIL_set_sequence_volume(h, s->vol, 0);
    AIL_set_sequence_tempo(h, s->tempo, 0);
    AIL_start_sequence(h);
    if (music_play_cb)
        WATCOM_CALL(music_play_cb, (signed char)i, s->id);
    return 0;
}

/* Starts sequence i and ends every other playing one. */
int music_start_exclusive(char i)
{
    int k;
    if (music_start(i) == -1)
        return -1;
    for (k = 0; k < music_seq_count; k++)
        if ((signed char)i != k && AIL_sequence_status(music_handle[k]) == SEQ_PLAYING)
            AIL_end_sequence(H(k));
    return 0;
}

void music_pause(char i)
{
    if (AIL_sequence_status(H(i)) == SEQ_PLAYING)
        AIL_stop_sequence(H(i));
}

void music_resume(char i)
{
    if (AIL_sequence_status(H(i)) == SEQ_STOPPED)
        AIL_resume_sequence(H(i));
}

/* Returns the old volume, -1 for a bad index. */
int music_set_volume(char i, long vol, long ms)
{
    int old;
    if ((signed char)i < 0 || (signed char)i >= music_seq_count)
        return -1;
    AIL_set_sequence_volume(H(i), vol, ms);
    old = S(i).vol;
    S(i).vol = vol;
    return old;
}

int music_get_volume(char i)
{
    return S(i).vol;
}

void music_set_user(char i, long v)
{
    S(i).user = v;
}

long music_get_user(char i)
{
    return S(i).user;
}

long music_set_tempo(char i, long tempo, long ms)
{
    long old;
    if ((signed char)i < 0 || (signed char)i >= music_seq_count)
        return -1;
    AIL_set_sequence_tempo(H(i), tempo, ms);
    old = S(i).tempo;
    S(i).tempo = tempo;
    return old;
}

long music_get_tempo(char i)
{
    return S(i).tempo;
}

void music_branch(char i, ulong marker)
{
    AIL_branch_index(H(i), marker);
}

void *music_set_trigger_cb(void *f)
{
    void *old = music_trigger_cb;
    music_trigger_cb = f;
    return old;
}

void music_trigger_hook(HSEQUENCE S, long channel, long value)
{
    if (music_trigger_cb)
        WATCOM_CALL(music_trigger_cb, (signed char)AIL_sequence_user_data(S, 0), value);
}

void *music_set_beat_cb(void *f)
{
    void *old = music_beat_cb;
    music_beat_cb = f;
    return old;
}

void music_beat_hook(void *mdi, HSEQUENCE S, long beat, long measure)
{
    music_beat.beat = beat;
    music_beat.measure = measure;
    if (music_beat_cb)
        WATCOM_CALL(music_beat_cb, (signed char)AIL_sequence_user_data(S, 0), (long)&music_beat);
}

void *music_set_prefix_cb(void *f)
{
    void *old = music_prefix_cb;
    music_prefix_cb = f;
    return old;
}

/* Only registered while music_prefix_cb is set (no NULL test). */
void music_prefix_hook(HSEQUENCE S, long a, long b)
{
    WATCOM_CALL(music_prefix_cb, (signed char)AIL_sequence_user_data(S, 0), a, b);
}

void *music_set_1c(void *f, long v)
{
    void *old = music_1c;
    music_20 = v;
    music_1c = f;
    return old;
}

long music_set_mark(char i, long a, long b)
{
    MusicMark *m = &music_mark[(signed char)i];
    long old;
    m->b = b;
    old = m->a;
    m->a = a;
    return old;
}

/* End of a sequence: report it, then start its successor if that one is prepared. */
void music_seq_hook(HSEQUENCE S)
{
    MusicSeq *s = &music_seq[AIL_sequence_user_data(S, 0)];
    signed char n;
    if (music_end_cb)
        WATCOM_CALL(music_end_cb, s->id);
    n = s->next;
    s->playing = 0;
    if (n != -1 && music_seq[n].playing == 1) {
        AIL_start_sequence(music_handle[n]);
        if (music_play_cb)
            WATCOM_CALL(music_play_cb, s->next, music_seq[s->next].id);
    }
}

void music_register_hooks(char i)
{
    HSEQUENCE h = H(i);
    if (music_prefix_cb)
        AIL_register_prefix_callback(h, (void *)music_prefix_hook);
    if (music_trigger_cb)
        AIL_register_trigger_callback(h, (void *)music_trigger_hook);
    if (music_beat_cb)
        AIL_register_beat_callback(h, (void *)music_beat_hook);
    AIL_register_sequence_callback(h, (void *)music_seq_hook);
}

/* Prepares and starts a sequence with the given settings (NULL: defaults). */
char music_play_ex(long id, void *data, long num, MusicSeq *settings)
{
    signed char i = music_find_free();
    MusicSeq *s;
    HSEQUENCE h;
    long r;
    if (i == -1) {
        snd_last_error = 0x200;
        return -1;
    }
    s = &S(i);
    if (settings) {
        *s = *settings;
    } else {
        s->vol = 0x7F;
        s->prio = 0x3F;
        s->tempo = 100;
        s->next = -1;
    }
    h = H(i);
    AIL_set_sequence_user_data(h, 0, i);
    r = AIL_init_sequence(h, data, num);
    if (r == 0 || r == -1)
        return -1;
    s->playing = 1;
    s->num = num;
    s->id = id;
    s->data = data;
    music_register_hooks(i);
    AIL_set_sequence_volume(h, s->vol, 0);
    AIL_start_sequence(h);
    return i;
}

void music_default_settings(MusicSeq *s)
{
    s->vol = 0x7F;
    s->prio = 0x3F;
    s->tempo = 100;
    s->next = -1;
}

void music_set_start_cb(void *f)
{
    music_start_cb = f;
}

/* The last started sequence, -1 once it is done. */
char music_current_playing(void)
{
    if (AIL_sequence_status(H(music_current)) == SEQ_DONE)
        return -1;
    return music_current;
}

void music_set_play_cb(void *f)
{
    music_play_cb = f;
}

/* Sets the successor of i; the old successor is unprepared (and reported as ended). */
void music_set_next(char i, char next)
{
    signed char n = S(i).next;
    if (n != -1) {
        music_seq[n].playing = 0;
        if (music_end_cb)
            WATCOM_CALL(music_end_cb, music_seq[S(i).next].id);
    }
    S(i).next = next;
}

void music_end_beat_hook(void *mdi, HSEQUENCE S, long beat, long measure)
{
    AIL_end_sequence(S);
}

void music_end_trigger_hook(HSEQUENCE S, long channel, long value)
{
    AIL_register_beat_callback(S, (void *)music_end_beat_hook);
}

/* Ends sequence i at its next beat. */
void music_end_at_beat(char i)
{
    AIL_register_beat_callback(H(i), (void *)music_end_beat_hook);
}

/* Ends sequence i at the first beat after its next trigger. */
void music_end_at_trigger(char i)
{
    AIL_register_trigger_callback(H(i), (void *)music_end_trigger_hook);
}

void music_unlock(void)
{
    dpmi_unlock_region(&music_beat_cb, &music_current);
    dpmi_unlock_region((void *)music_lock, (void *)music_unlock);
}

/* ------------------------------------------------------------------ periodic calls */

void periodic_clear(void)
{
    periodic_count = 0;
    periodic_update();
}

/* FALSE when the list (8) is full.  The first call happens at the next periodic_run. */
bool periodic_add(void *fn, ulong interval, uchar repeat)
{
    int n = periodic_count;
    if (n == 8)
        return 0;
    periodic_tab[n].fn = fn;
    periodic_tab[n].interval = interval;
    periodic_tab[n].repeat = repeat;
    periodic_count = n + 1;
    periodic_tab[n].next = 0;
    periodic_update();
    return 1;
}

/* Removes the first entry calling fn (the last entry takes its place). */
void periodic_remove(void *fn)
{
    int n = periodic_count;
    Periodic *p;
    if (n > 0)
        for (p = periodic_tab; p < periodic_tab + n; p++)
            if (p->fn == fn) {
                periodic_count--;
                *p = periodic_tab[periodic_count];
                break;
            }
    periodic_update();
}

/* Calls every entry that is due (fn gets the entry's byte offset in eax). */
void periodic_run(ulong now)
{
    int k;
    for (k = 0; k < periodic_count; k++) {
        Periodic *p = &periodic_tab[k];
        if (now >= p->next) {
            WATCOM_CALL(p->fn, k * (long)sizeof(Periodic));
            if (p->repeat) {
                if (p->next == 0)
                    p->next = now;
                p->next += p->interval;
            } else {
                p->next = now + p->interval;
            }
        }
    }
    periodic_update();
}

void periodic_update(void)
{
    ulong m;
    int k;
    if (periodic_count == 0) {
        m = 0xFFFFFFFF;
    } else {
        m = periodic_tab[0].next;
        for (k = 1; k < periodic_count; k++)
            if (periodic_tab[k].next < m)
                m = periodic_tab[k].next;
    }
    periodic_next_due = m;
}
