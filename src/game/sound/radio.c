/* radio.c -- Terra Nova radio speech (module M110; source name unknown).  Decompiled from
 * __FF.EXE v1.09; names ours (RadioSpeak / RadioMessage / RadioAck are the entry points other
 * modules call).
 *
 *   0x2CDC10 radio_chance         0x2CDC34 radio_voice_class    0x2CDC4C radio_line_id
 *   0x2CDC78 radio_line_span      0x2CDCBC radio_files_reset    0x2CDCD8 radio_stop_voice
 *   0x2CDD20 radio_init           0x2CDE9C radio_close          0x2CDECC radio_pick_line
 *   0x2CDFD8 radio_snd_done_cb    0x2CE040 radio_busy_prio      0x2CE098 radio_duck_below
 *   0x2CE0FC radio_stop_below     0x2CE144 radio_last_start     0x2CE188 radio_play
 *   0x2CE32C radio_update         0x2CE450 radio_say            0x2CE620 RadioSpeak
 *   0x2CE7B8 radio_flush          0x2CE7F8 radio_msg_group      0x2CE860 radio_msg_done_cb
 *   0x2CE87C radio_msg_play       0x2CE974 radio_msg_update     0x2CEA24 RadioMessage
 *   0x2CEB0C radio_ack_done_cb    0x2CEB20 radio_ack_play       0x2CEBA4 radio_ack_update
 *   0x2CEC20 RadioAck             0x2CECE4 RadioMessageHi       0x2CED40 radio_stop_all
 *
 * Three kinds of radio sound:
 *  - squad chatter: RadioSpeak(speaker, message) looks the message up in resource 0x8AF (per
 *    message: priority, delay, chance, line count, follow-up message ...; the line ranges come
 *    from the +0xA word of consecutive entries, radio_line_span), picks the least used line
 *    (radio_pick_line, usage counters per voice) and says it on the speaker's channel
 *    (radio_say: play now when the channel is idle or the new line has higher priority, else
 *    queue it in one of 3 slots per channel, replacing the lowest priority).  The speech files
 *    are RESBRKnn.RES, one per voice class (radio_init opens one per speaker);
 *    radio_update plays queued lines, at most one line start per 500 ms over all channels;
 *  - command messages (RadioMessage / RadioMessageHi: sound 0xAF0 + id, text 0x1790000 + id,
 *    or for ids with bit 0x1000 sound 0xBA5 + id, text 0x17A0000 + id), 6 queue slots;
 *  - acknowledgements (RadioAck: sound 0xA5A + id, text 0x17B0000 + id), 6 queue slots.
 * Quirks kept:
 *  - radio_busy_prio compares channel i's priority with the best so far but records byte +0x30
 *    of speaker record i (not the priority);
 *  - radio_say counts a line it plays twice in the usage counters (radio_play counts it too);
 *  - radio_pick_line reads the voice of channel `ch` (callers pass the speaker index);
 *  - radio_init stores one file handle per speaker into a 4-entry table and radio_play /
 *    radio_snd_done_cb index radio_talking (4 bytes) by speaker, without bounds;
 *  - radio_line_span returns the whole ecx - edx registers (high words are leftovers; callers
 *    use the low word); radio_voice_class and radio_line_id return the register value with
 *    the input's high bits (callers use the low byte / word);
 *  - radio_msg_play with an id without bit 0x1000 still tests the low word of the id
 *    with the bit cleared only when it is set; the high word of the stack slot is garbage in
 *    the original but only its low word is used.
 */
#include "g1_b.h"
#include "g7_a.h"     /* added at the G5-G8 merge */

#ifndef TN_LINK_ORIGINAL_DATA
int radio_files[4];
RadioChan radio_chans[4];
uchar radio_line_used[4][RADIO_LINES];
uchar radio_voice_used[0x13][RADIO_LINES];
uchar radio_talking[4];
RadioMsgQ radio_ackq[6], radio_msgq[6];
schar radio_nchans;
long radio_msg_handle = -1, radio_msg_cur = -1, radio_msg_last;
uchar radio_msg_47_said;
long radio_ack_handle = -1;
short radio_ack_cur = -1;
#endif

#define CLASS(v) ((schar)radio_voice_class(v))

/* unreferenced: TRUE with probability (pct + 1) / 101 */
bool radio_chance(long pct)
{
    return RndRange(&rnd_stream_35B48C, 0, 100) <= (short)pct;
}

/* voice type -> resbrk file / volume class: below 0x10 itself, 0x10..0x17 -> 0x10,
   0x20 -> 0x12, the rest -> 0x11 */
ulong radio_voice_class(ulong v)
{
    schar c = (schar)v;
    if (c >= 0x10) {
        schar t = c;
        c = 0x10;
        if (t >= 0x18)
            c = 0x11;
        if (t == 0x20)
            c++;
    }
    return (v & 0xFFFF0000) | ((v & 0xFF) << 8) | (uchar)c;
}

/* unreferenced: sound id of line n of a voice: (class - 1) * 0x113 + base + n (register
   arithmetic of the original kept: the high word of the result depends on v's) */
ulong radio_line_id(ulong v, long base, long n)
{
    ulong e = radio_voice_class(v);
    e = (e & 0xFFFF0000) | (ushort)(short)(schar)e;
    e = (e - 1) * 0x113;
    base += e;
    e = (e & 0xFFFF0000) | (ushort)(short)(schar)n;
    return e + base;
}

/* first line of message max(a,b) minus first line of message min(a,b) (resource 0x8AF) */
ushort radio_line_span(long a, long b)
{
    schar lo = (schar)a, hi = (schar)b, t;
    uchar *tbl;
    ushort x, y;

    if (lo > hi) {
        t = lo;
        lo = hi;
        hi = t;
    }
    tbl = ResLock(0x8AF);
    x = *(ushort *)(tbl + *(long *)(tbl + 2 + hi * 4) + 0xA);
    y = *(ushort *)(tbl + *(long *)(tbl + 2 + lo * 4) + 0xA);
    ResUnlock(0x8AF);
    return x - y;
}

void radio_files_reset(void)
{
    memset(radio_files, -1, 0x10);
}

void radio_stop_voice(long v)
{
    long i;
    for (i = 0; i < radio_nchans; i++) {
        RadioChan *c = &radio_chans[i];
        if (c->voice == (schar)v && c->handle != -1) {
            snd_chan_stop(c->handle);
            c->start = 0;
        }
    }
}

void radio_init(void)
{
    long i;

    memset(radio_files, -1, 0x10);
    memset(radio_talking, 0, 4);
    for (i = 0; i < game_391E4C; i++) {
        GameSpeaker *s = &game_391E50[i];
        if (s->active && s->voice)
            radio_files[i] = ResOpenResFile(radio_file_names[CLASS(s->voice)], 0, 0);
    }
    if (game_359708 == 5)
        radio_files[game_391E4C] = ResOpenResFile("resbrk18.res", 0, 0);
    for (i = 0; i < 6; i++) {
        radio_msgq[i].id = -1;
        radio_ackq[i].id = -1;
    }
    radio_nchans = (schar)((uchar)game_391E4C - 1);
    memset(radio_chans, 0, 0xA0);
    for (i = 0; i < radio_nchans; i++)
        radio_chans[i].voice = game_391E50[i + 1].voice;
    for (i = 0; i < 4; i++)
        radio_chans[i].handle = -1;
}

void radio_close(void)
{
    long i;
    for (i = 0; i < 4; i++) {
        if (radio_files[i] != -1)
            ResCloseFile(radio_files[i]);
        radio_files[i] = -1;
    }
}

/* choose one of n lines starting at base, least used first (random among the least used);
   mode bit 0: -1 if that line was used already; n == 1: line 0 */
schar radio_pick_line(long ch, long base, long n_, long mode)
{
    schar n = (schar)n_, voice = radio_chans[(schar)ch].voice;
    uchar *used = &radio_voice_used[0][0] + voice * RADIO_LINES + (short)base;
    long i, cnt = -1, min = -1, v;
    uchar r;

    if ((uchar)n == 1)
        return 0;
    for (i = 0; i < n; i++) {
        v = (schar)used[i];
        if (v > min) {
            cnt = 1;
            min = v;
        } else if (min == v)
            cnt++;
    }
    if (cnt == n) {
        min = 0x1000000;
        cnt = 0;
    }
    r = (uchar)RndRange(&rnd_stream_35B48C, 0, n - cnt - 1);
    for (i = 0; i < n; i++) {
        if ((schar)used[i] < min) {
            if (r == 0)
                break;
            r--;
        }
    }
    if ((mode & 1) && used[i] != 0)
        i = -1;
    return (schar)i;
}

/* end of a line on channel ch */
long radio_snd_done_cb(long h, long ch)
{
    long n = game_391E4C, i;

    radio_chans[ch].handle = -1;
    for (i = 1; i < n; i++) {
        GameSpeaker *s = &game_391E50[i];
        if (s->active && s->chan == ch) {
            radio_talking[i] = 0;
            game_389DF8[0x20] |= 0x20;
        }
    }
    game_391E4C = n;
    return i;
}

/* see the quirk in the header comment */
uchar radio_busy_prio(void)
{
    uchar best = 0;
    long i;
    for (i = 0; i < radio_nchans; i++)
        if (radio_chans[i].handle != -1 && radio_chans[i].prio > (long)best)
            best = game_391E50[i].f30;
    return best;
}

/* unreferenced: turn down the channels below priority p */
void radio_duck_below(long p)
{
    long i;
    for (i = 0; i < radio_nchans; i++) {
        RadioChan *c = &radio_chans[i];
        if (c->handle != -1 && (schar)p > c->prio)
            snd_chan_set_vol(c->handle, radio_class_vol[CLASS(c->voice)] - 10);
    }
}

void radio_stop_below(long p)
{
    long i;
    for (i = 0; i < radio_nchans; i++) {
        RadioChan *c = &radio_chans[i];
        if (c->handle != -1 && (schar)p > c->prio) {
            snd_chan_stop(c->handle);
            c->start = 0;
        }
    }
}

/* latest line start on the other channels */
ulong radio_last_start(long ch)
{
    ulong t = 0;
    long i;
    for (i = 0; i < radio_nchans; i++)
        if (i != (schar)ch && t < radio_chans[i].start)
            t = radio_chans[i].start;
    return t;
}

/* start line snd on channel ch now; FALSE if another line started < 500 ms ago or no sound */
bool radio_play(long ch_, long snd, long prio, long line)
{
    schar ch = (schar)ch_;
    RadioChan *c = &radio_chans[ch];
    long n, i, vol;
    schar cls = CLASS(c->voice);

    if (radio_last_start(ch) + 500 > game_time)
        return FALSE;
    vol = radio_class_vol[cls];
    if ((long)radio_busy_prio() > (schar)prio)
        vol -= 10;
    c->handle = SndPlay((ushort)snd, 4, 0xFF, 1, vol, ch << 5, 0);
    if (c->handle < 0) {
        c->handle = -1;
        return FALSE;
    }
    c->prio = (schar)prio;
    snd_chan_add_callback(c->handle, WATCOM_FN(radio_snd_done_cb), ch);
    radio_line_used[ch][(short)line]++;
    radio_voice_used[c->voice][(short)line]++;
    n = game_391E4C;
    for (i = 1; i < n; i++) {
        GameSpeaker *s = &game_391E50[i];
        if (s->active && s->chan == ch) {
            radio_talking[i] = 1;
            game_389DF8[0x20] |= 0x20;
        }
    }
    c->start = game_time;
    return TRUE;
}

void radio_update(void)
{
    ulong t = game_time;
    long k, i;
    schar best, bi;

    for (k = 0; k < radio_nchans; k++) {
        RadioChan *c = &radio_chans[k];
        for (i = 0; i < 3; i++)
            if (c->q[i].snd != 0 && t > c->q[i].expire) {
                c->q[i].snd = 0;
                c->q[i].expire = 0;
            }
        game_time = t;
        if (c->handle == -1) {
            best = -1;
            bi = -1;
            for (i = 0; i < 3; i++)
                if (c->q[i].snd != 0 && best < c->q[i].prio) {
                    bi = (schar)i;
                    best = c->q[i].prio;
                }
            game_time = t;
            if (bi != -1 && (c->q[bi].wait == 0 || radio_busy_prio() == 0)) {
                RadioQ *q = &c->q[bi];
                if (radio_play(k, q->snd, q->prio, q->line))
                    q->snd = 0;
            }
        }
        t = game_time;
    }
    game_time = t;
    radio_msg_update();
    radio_ack_update();
}

/* say line snd on channel ch (play it or queue it for up to `delay` ms) */
void radio_say(long ch_, long snd, long prio_, long delay, long line_, long wait, long unused)
{
    schar ch = (schar)ch_;
    uchar prio = (uchar)prio_;
    short line = (short)line_;
    RadioChan *c = &radio_chans[ch];
    schar min, slot;
    long i;

    if (c->handle != -1 && c->prio < (long)prio)
        snd_chan_stop(c->handle);
    if (c->handle == -1 && ((uchar)wait == 0 || radio_busy_prio() == 0)
        && radio_play(ch, (ushort)snd, (schar)prio, line)) {
        radio_line_used[ch][line]++;
        radio_voice_used[c->voice][line]++;
        return;
    }
    /* queue: a free slot, else the lowest priority one */
    slot = -1;
    min = 100;
    for (i = 0; i < 3; i++) {
        if (c->q[i].snd == 0) {
            min = -1;
            slot = (schar)i;
            break;
        }
        if (min > c->q[i].prio) {
            min = c->q[i].prio;
            slot = (schar)i;
        }
    }
    if ((long)prio < min)
        return;
    if (min != -1) {
        RadioQ *q = &c->q[slot];
        radio_line_used[ch][q->line]--;
        radio_voice_used[c->voice][q->line]--;
    }
    c->q[slot].snd = (ushort)snd;
    c->q[slot].prio = prio;
    c->q[slot].expire = game_time + delay;
    c->q[slot].line = line;
    c->q[slot].wait = (uchar)wait;
    radio_line_used[ch][line]++;
    radio_voice_used[c->voice][line]++;
}

/* speaker spk says message msg (if the dice allow) on its channel */
void RadioSpeak(long spk_, long msg_)
{
    schar spk = (schar)spk_, msg = (schar)msg_, ch, v;
    uchar *tbl, *e;
    ushort base, id;
    short line;

    ch = game_391E50[spk].chan;
    if (ch < 1 || ch > 3)
        return;
    tbl = ResLock(0x8AF);
    e = tbl + *(long *)(tbl + 2 + msg * 4);
    if (RndRange(&rnd_stream_35B48C, 0, 100) <= (short)(schar)e[3]) {
        if (e[6] && RndRange(&rnd_stream_35B48C, 0, 100) <= (short)(schar)e[7]) {
            msg = (schar)e[6];
            e = tbl + *(long *)(tbl + 2 + msg * 4);
        }
        base = radio_line_span(1, msg);
        v = radio_pick_line(spk, (short)base, (schar)e[5], (schar)e[4]);
        if (v != -1) {
            line = (short)(base + v);
            radio_chans[ch].voice = game_391E50[spk].voice;
            id = (ushort)(((short)(schar)radio_voice_class(game_391E50[spk].voice) - 1) * 0x113
                          + *(ushort *)(e + 0xA) + (short)v);
            radio_say(ch, id, e[0], *(short *)(e + 1), line, (schar)e[8], (schar)e[9]);
        }
    }
    ResUnlock(0x8AF);
}

/* unreferenced: stop the speech and forget the queued lines' expiry times */
void radio_flush(void)
{
    long k, i;
    radio_stop_below(0x65);
    for (k = 0; k < radio_nchans; k++)
        for (i = 0; i < 3; i++)
            radio_chans[k].q[i].expire = 0;
}

/* message g >= 0x3B: *n = number of variants, returns the index of its first variant
   (sum of the variant counts of the groups before it, low byte) */
uchar radio_msg_group(long g_, uchar *n)
{
    schar g = (schar)g_;
    uchar *tbl = ResLock(0x8B0);
    ushort sum = 0;
    long i;

    for (i = 0; i < g - 0x3B; i++)
        sum += tbl[*(long *)(tbl + 2 + i * 4)];
    *n = *(tbl + *(long *)(tbl + 2 + (g - 0x3B) * 4));
    ResUnlock(0x8B0);
    return (uchar)sum;
}

long radio_msg_done_cb(void)
{
    long cur = radio_msg_cur;
    radio_msg_handle = -1;
    radio_msg_last = cur;
    radio_msg_cur = -1;
    return cur;
}

void radio_msg_play(long msg)
{
    short m = (short)msg, alt = -1;

    if ((ushort)m & 0x1000)
        alt = m & ~0x1000;
    if (radio_msg_flags & 2) {
        hud_2688A4(RefGet(alt != -1 ? alt + 0x17A0000 : m + 0x1790000));
        radio_msg_cur = m;
    }
    if (snd_channels && (radio_msg_flags & 1)) {
        if (alt != -1)
            radio_msg_handle = SndPlay((ushort)(alt + 0xBA5), 4, 0xFF, 1, 0x54, 0x40, 0);
        else
            radio_msg_handle = SndPlay((ushort)(m + 0xAF0), 4, 0xFF, 1, 0x54, 0x40, 0);
        radio_msg_cur = m;
        if (radio_msg_handle >= 0)
            snd_chan_add_callback(radio_msg_handle, WATCOM_FN(radio_msg_done_cb), 0);
    }
}

void radio_msg_update(void)
{
    ulong t;
    schar bi = -1;
    long j;

    if (radio_msg_handle != -1)
        return;
    t = game_time;
    for (j = 0; j < 6; j++)
        if (radio_msgq[j].id != -1 && t > radio_msgq[j].expire)
            radio_msgq[j].id = -1;
    for (j = 0; j < 6; j++)
        if (radio_msgq[j].id != -1 && t > radio_msgq[j].start) {
            bi = (schar)j;
            t = radio_msgq[j].start;
        }
    if (bi != -1) {
        radio_msg_play(radio_msgq[bi].id);
        radio_msgq[bi].id = -1;
    }
}

/* queue command message msg (ids >= 0x3B are groups: one variant at random) */
void RadioMessage(long msg)
{
    uchar n, m;
    long delay, j;

    if (game_35BF48 && msg != 0x47)
        return;
    if (game_35B77C || radio_msg_47_said)
        return;
    if (msg == 0x47)
        radio_msg_47_said = 1;
    if (msg >= 0x3B) {
        m = radio_msg_group((schar)msg, &n) + 0x3B;
        m += (uchar)RndRange(&rnd_stream_35B48C, 0, n - 1);
        delay = 3000;
    } else {
        m = (uchar)msg;
        delay = 25000;
    }
    if (m == radio_msg_cur)
        return;
    if (radio_msg_handle == -1) {
        radio_msg_play(m);
        return;
    }
    for (j = 0; j < 6; j++)
        if (radio_msgq[j].id == -1) {
            radio_msgq[j].id = m;
            radio_msgq[j].start = game_time;
            radio_msgq[j].expire = game_time + delay;
            return;
        }
}

void radio_ack_done_cb(void)
{
    radio_ack_handle = -1;
    radio_ack_cur = -1;
}

void radio_ack_play(long id_)
{
    short id = (short)id_;
    if (radio_text_flags & 2)
        hud_268BD4(RefGet(id + 0x17B0000), 100);
    if (snd_channels && (radio_text_flags & 1)) {
        radio_ack_handle = SndPlay((ushort)(id + 0xA5A), 4, 0xFF, 1, 0x4C, 0x40, 0);
        radio_ack_cur = id;
        if (radio_ack_handle >= 0)
            snd_chan_add_callback(radio_ack_handle, WATCOM_FN(radio_ack_done_cb), 0);
    }
}

void radio_ack_update(void)
{
    ulong t;
    schar bi = -1;
    long j;

    if (radio_ack_handle != -1)
        return;
    t = game_time;
    for (j = 0; j < 6; j++)
        if (radio_ackq[j].id != -1 && t > radio_ackq[j].start) {
            bi = (schar)j;
            t = radio_ackq[j].start;
        }
    if (bi != -1) {
        radio_ack_play(radio_ackq[bi].id);
        radio_ackq[bi].id = -1;
    }
}

void RadioAck(long id_)
{
    short id = (short)id_;
    long j;

    if (game_359708 == 0x27 || game_35B77C)
        return;
    if (id >= 0x52 && id < 0x68)
        SndPlay((ushort)(id_ + 0xA7D), 4, 0xFF, 1, 0x4C, 0x40, 0);
    if (id == radio_ack_cur)
        return;
    for (j = 0; j < 6; j++)
        if (id == radio_ackq[j].id)
            return;
    if (radio_ack_handle == -1) {
        radio_ack_play(id);
        return;
    }
    for (j = 0; j < 6; j++)
        if (radio_ackq[j].id == -1) {
            radio_ackq[j].id = id;
            radio_ackq[j].start = game_time;
            return;
        }
}

/* queue message msg with bit 0x1000 (the alternative text / sound) for 30 s */
void RadioMessageHi(long msg)
{
    short m = (short)(msg | 0x1000);
    long j;

    if (radio_msg_handle == -1) {
        radio_msg_play(m);
        return;
    }
    for (j = 0; j < 6; j++)
        if (radio_msgq[j].id == -1) {
            radio_msgq[j].id = m;
            radio_msgq[j].start = game_time;
            radio_msgq[j].expire = game_time + 30000;
            return;
        }
}

void radio_stop_all(void)
{
    long i;

    for (i = 0; i < radio_nchans; i++) {
        RadioChan *c = &radio_chans[i];
        if (c->handle != -1) {
            snd_chan_stop(c->handle);
            c->handle = -1;
            c->start = 0;
        }
    }
    if (radio_ack_handle >= 0) {
        snd_chan_stop(radio_ack_handle);
        radio_ack_handle = -1;
    }
    for (i = 0; i < 6; i++)
        radio_ackq[i].id = -1;
    if (radio_msg_handle >= 0) {
        snd_chan_stop(radio_msg_handle);
        radio_msg_handle = -1;
    }
    for (i = 0; i < 6; i++)
        radio_msgq[i].id = -1;
}
