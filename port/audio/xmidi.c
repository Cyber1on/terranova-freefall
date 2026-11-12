/* xmidi.c -- the XMIDI sequencer of the Miles Sound System (AIL 3.x) for the standalone build:
 * the AIL_*_sequence API, playing Extended MIDI (XMIDI) at 120 ticks per second through the FM
 * synthesizer (fmsynth.c) or a General MIDI device (gmout.c).
 *
 * XMIDI: an IFF file ("FORM XDIR" + "CAT XMID", or one "FORM XMID") of sequences, each with an
 * optional TIMB (timbres used), RBRN (branch points: id -> offset into EVNT) and the EVNT
 * stream.  In EVNT, bytes below 0x80 are waits (summed, in 1/120 s), note-ons carry their
 * length (a MIDI variable-length number) instead of a note-off, and controllers 110..120 are
 * Miles's own: 114 patch bank, 115 indirect prefix, 116 FOR, 117 NEXT/BREAK, 118 clear the
 * beat count, 119 callback trigger, 120 branch point.
 *
 * Everything runs at "interrupt time": the sequencer from the sound service (miles.c), the API
 * under the interrupt lock. */
#include <stdlib.h>
#include <string.h>
#include "miles_int.h"
#include "platform.h"
#include "port.h"

#define MAX_SEQS 16
#define MAX_NOTES 64

enum { SEQ_FREE = 1, SEQ_DONE = 2, SEQ_PLAYING = 4, SEQ_STOPPED = 8 };

typedef void (*seq_cb)(void *S);
typedef void (*trigger_cb)(void *S, long channel, long value);
typedef void (*beat_cb)(void *mdi, void *S, long beat, long measure);

typedef struct {
    int used, status;
    const uint8_t *evnt, *ptr, *rbrn;
    int nbranch;
    long wait;
    int loop_count;
    struct { const uint8_t *pos; int count; } loops[4];
    int nloops;
    struct { uint8_t ch, note, vel; long left; } notes[MAX_NOTES];
    int nnotes;
    uint8_t cc7[16];                    /* volume as the sequence set it */
    uint16_t chans;                     /* channels the sequence used */
    long vol, vol_from, vol_to, vol_ticks, vol_t;
    long tempo, tempo_from, tempo_to, tempo_ticks, tempo_t, tempo_acc;
    long tempo_us, beat_us, beat_frac, beat, measure, numer, denom;
    void *prefix, *trigger, *beat_fn, *eos;
    long user[8];
    MpTrack *pack;                      /* music pack file playing instead of the MIDI */
    int entry, num;                     /* music table entry and sequence (0: not named) */
    unsigned long elapsed;              /* 1/120 s ticks since it started (for a pack switch) */
} Seq;

static Seq seqs[MAX_SEQS];
int xmi_output;
long xmi_master = 127;
extern char miles_mdi_driver[];         /* the MDI driver record (miles.c) */

static Seq *seq_of(void *S)
{
    Seq *s = S;
    return s >= seqs && s < seqs + MAX_SEQS && s->used ? s : NULL;
}

static void send(int status, int d1, int d2)
{
    if (xmi_output == MUSIC_FM)
        fm_msg(status, d1, d2);
    else if (xmi_output == MUSIC_GM)
        gm_msg(status, d1, d2);
    else if (xmi_output == MUSIC_SF)
        sf_msg(status, d1, d2);
}

/* what each channel was last told (-1: nothing), so that a new output (the options' Music line,
 * miles.c port_music_reopen) carries on from there: xmi_restore */
static struct { int16_t cc[128], prog, pressure, bend; } chst[16];
static int chst_ready;

static void remember(int status, int d1, int d2)
{
    int ch = status & 15;
    if (!chst_ready) {
        memset(chst, 0xFF, sizeof chst);
        chst_ready = 1;
    }
    switch (status & 0xF0) {
    case 0xB0:
        if (d1 < 120)
            chst[ch].cc[d1] = (int16_t)d2;
        else if (d1 == 121) {           /* reset controllers */
            chst[ch].cc[1] = chst[ch].cc[11] = chst[ch].cc[64] = -1;
            chst[ch].cc[100] = chst[ch].cc[101] = -1;
            chst[ch].pressure = chst[ch].bend = -1;
        }
        break;
    case 0xC0:
        chst[ch].prog = (int16_t)d1;
        break;
    case 0xD0:
        chst[ch].pressure = (int16_t)d1;
        break;
    case 0xE0:
        chst[ch].bend = (int16_t)(d1 | d2 << 7);
        break;
    }
}

static void out(int status, int d1, int d2)
{
    remember(status, d1, d2);
    send(status, d1, d2);
}

/* a sequence with a music pack file plays no notes (controllers and programs still go out, so
 * the MIDI carries on correctly when the pack is switched off mid-piece) */
#define sout(s, a, b, c) do { if (!(s)->pack || ((a) & 0xF0) != 0x90) out(a, b, c); } while (0)

static int scaled_volume(Seq *s, int v)
{
    return (int)(v * s->vol / 127 * xmi_master / 127);
}

static void send_volumes(Seq *s)
{
    int ch;
    for (ch = 0; ch < 16; ch++)
        if (s->chans & (1 << ch))
            sout(s, 0xB0 | ch, 7, scaled_volume(s, s->cc7[ch]));
}

static void flush_notes(Seq *s)
{
    int i, ch;
    for (i = 0; i < s->nnotes; i++)
        sout(s, 0x80 | s->notes[i].ch, s->notes[i].note, 0);
    s->nnotes = 0;
    for (ch = 0; ch < 16; ch++)
        if (s->chans & (1 << ch))
            sout(s, 0xB0 | ch, 64, 0);      /* sustain off */
}

static unsigned long vlq(const uint8_t **p)
{
    unsigned long v = 0;
    uint8_t b;
    do {
        b = *(*p)++;
        v = (v << 7) | (b & 0x7F);
    } while (b & 0x80);
    return v;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* the EVNT (and RBRN) of sequence num; 0 if there is none */
static int find_sequence(const uint8_t *d, int num, const uint8_t **evnt, const uint8_t **rbrn)
{
    const uint8_t *p = d, *end;
    int n = 0;

    *rbrn = NULL;
    if (memcmp(p, "FORM", 4) == 0 && memcmp(p + 8, "XDIR", 4) == 0) {
        p += 8 + ((be32(p + 4) + 1) & ~1u);
        if (memcmp(p, "CAT ", 4) != 0)
            return 0;
        end = p + 8 + be32(p + 4);
        p += 12;                        /* "CAT " len "XMID" */
    } else if (memcmp(p, "FORM", 4) == 0 && memcmp(p + 8, "XMID", 4) == 0) {
        end = p + 8 + be32(p + 4);
    } else
        return 0;
    while (p + 12 <= end) {
        const uint8_t *f, *fend;
        if (memcmp(p, "FORM", 4) != 0 || memcmp(p + 8, "XMID", 4) != 0)
            return 0;
        fend = p + 8 + be32(p + 4);
        if (n++ == num) {
            for (f = p + 12; f + 8 <= fend; f += 8 + ((be32(f + 4) + 1) & ~1u)) {
                if (memcmp(f, "RBRN", 4) == 0)
                    *rbrn = f + 8;
                else if (memcmp(f, "EVNT", 4) == 0) {
                    *evnt = f + 8;
                    return 1;
                }
            }
            return 0;
        }
        p = p + 8 + ((be32(p + 4) + 1) & ~1u);
    }
    return 0;
}

static void restart(Seq *s)
{
    s->ptr = s->evnt;
    s->wait = 0;
    s->nloops = 0;
}

static void end_seq(Seq *s, int callback)
{
    flush_notes(s);
    s->status = SEQ_DONE;
    if (callback && s->eos)
        ((seq_cb)s->eos)(s);
}

static void controller(Seq *s, int ch, int cc, int v)
{
    switch (cc) {
    case 7:
        s->cc7[ch] = (uint8_t)v;
        sout(s, 0xB0 | ch, 7, scaled_volume(s, v));
        return;
    case 115:
        if (s->prefix)
            ((trigger_cb)s->prefix)(s, ch, v);
        return;
    case 116:
        if (s->nloops < 4) {
            s->loops[s->nloops].pos = s->ptr;
            s->loops[s->nloops].count = v;
            s->nloops++;
        }
        return;
    case 117:
        if (s->nloops > 0) {
            int k = s->nloops - 1;
            if (v < 64)
                s->nloops--;                            /* BREAK */
            else if (s->loops[k].count == 0)
                s->ptr = s->loops[k].pos;               /* forever */
            else if (--s->loops[k].count == 0)
                s->nloops--;
            else
                s->ptr = s->loops[k].pos;
        }
        return;
    case 118:
        s->beat = 0;
        s->measure = 0;
        s->beat_frac = 0;
        return;
    case 119:
        if (s->trigger)
            ((trigger_cb)s->trigger)(s, ch, v);
        return;
    case 110: case 111: case 113: case 120:
        return;
    case 112: case 114:                 /* Miles's FM timbre controls: kept for a switch to FM */
        remember(0xB0 | ch, cc, v);
        if (xmi_output == MUSIC_FM)
            sout(s, 0xB0 | ch, cc, v);
        return;
    }
    sout(s, 0xB0 | ch, cc, v);
}

static void set_beat_length(Seq *s, int denom)
{
    s->beat_us = denom >= 2 ? s->tempo_us >> (denom - 2) : s->tempo_us << (2 - denom);
    if (s->beat_us <= 0)
        s->beat_us = 500000;
}

/* events until the next wait; stops when the sequence ends */
static void run_events(Seq *s)
{
    while (s->status == SEQ_PLAYING && s->wait == 0) {
        uint8_t b = *s->ptr;
        int ch;

        if (b < 0x80) {
            long d = 0;
            while (*s->ptr < 0x80)
                d += *s->ptr++;
            s->wait = d;
            continue;
        }
        s->ptr++;
        ch = b & 15;
        switch (b >> 4) {
        case 0x9: {
            uint8_t note = s->ptr[0], vel = s->ptr[1];
            unsigned long len;
            s->ptr += 2;
            len = vlq(&s->ptr);
            s->chans |= 1 << ch;
            sout(s, b, note, vel);
            if (vel && s->nnotes < MAX_NOTES) {
                s->notes[s->nnotes].ch = (uint8_t)ch;
                s->notes[s->nnotes].note = note;
                s->notes[s->nnotes].vel = vel;
                s->notes[s->nnotes].left = (long)len;
                s->nnotes++;
            } else if (vel)
                sout(s, 0x80 | ch, note, 0);
            break;
        }
        case 0x8: case 0xA: case 0xE:
            s->chans |= 1 << ch;
            sout(s, b, s->ptr[0], s->ptr[1]);
            s->ptr += 2;
            break;
        case 0xB:
            s->chans |= 1 << ch;
            controller(s, ch, s->ptr[0], s->ptr[1]);
            s->ptr += 2;
            break;
        case 0xC: case 0xD:
            s->chans |= 1 << ch;
            sout(s, b, s->ptr[0], 0);
            s->ptr += 1;
            break;
        default:
            if (b == 0xFF) {
                uint8_t type = *s->ptr++;
                unsigned long len = vlq(&s->ptr);
                const uint8_t *data = s->ptr;
                s->ptr += len;
                if (type == 0x2F) {
                    if (s->loop_count == 0 || --s->loop_count > 0)
                        restart(s);
                    else
                        end_seq(s, 1);
                    return;
                }
                if (type == 0x51 && len >= 3) {
                    s->tempo_us = data[0] << 16 | data[1] << 8 | data[2];
                    set_beat_length(s, s->denom);
                } else if (type == 0x58 && len >= 2) {
                    s->numer = data[0] ? data[0] : 4;
                    s->denom = data[1];
                    set_beat_length(s, s->denom);
                    s->beat_frac = 0;
                    s->beat = 0;
                }
            } else if (b == 0xF0 || b == 0xF7) {
                unsigned long len = vlq(&s->ptr);
                s->ptr += len;
            }
            break;
        }
    }
}

/* one 1/120 s tick of a playing sequence */
static void step(Seq *s)
{
    int i;

    for (i = 0; i < s->nnotes; ) {
        if (--s->notes[i].left <= 0) {
            sout(s, 0x80 | s->notes[i].ch, s->notes[i].note, 0);
            s->notes[i] = s->notes[--s->nnotes];
        } else
            i++;
    }
    s->beat_frac += 1000000;
    if (s->beat_frac >= s->beat_us * XMI_RATE) {
        s->beat_frac -= s->beat_us * XMI_RATE;
        if (++s->beat >= s->numer) {
            s->beat = 0;
            s->measure++;
        }
        if (s->beat_fn)
            ((beat_cb)s->beat_fn)(miles_mdi_driver, s, s->beat, s->measure);
        if (s->status != SEQ_PLAYING)
            return;
    }
    if (s->wait > 0)
        s->wait--;
    run_events(s);
}

static int xmi_paused;                  /* port_music_pause: the music holds still */

void xmi_tick(void)
{
    int i;
    if (xmi_paused)
        return;
    for (i = 0; i < MAX_SEQS; i++) {
        Seq *s = &seqs[i];
        if (!s->used || s->status != SEQ_PLAYING)
            continue;
        s->elapsed++;
        if (s->vol_ticks) {
            long v = s->vol_from + (s->vol_to - s->vol_from) * ++s->vol_t / s->vol_ticks;
            if (s->vol_t >= s->vol_ticks)
                s->vol_ticks = 0;
            if (v != s->vol) {
                s->vol = v;
                send_volumes(s);
            }
        }
        if (s->tempo_ticks) {
            s->tempo = s->tempo_from + (s->tempo_to - s->tempo_from) * ++s->tempo_t / s->tempo_ticks;
            if (s->tempo_t >= s->tempo_ticks)
                s->tempo_ticks = 0;
        }
        s->tempo_acc += s->tempo;
        while (s->tempo_acc >= 100 && s->status == SEQ_PLAYING) {
            s->tempo_acc -= 100;
            step(s);
        }
    }
}

/* the pack files of the playing sequences (the sound service, as xmi_tick) */
void xmi_render_packs(int32_t *mix, int frames)
{
    int i;
    if (xmi_paused)
        return;
    for (i = 0; i < MAX_SEQS; i++) {
        Seq *s = &seqs[i];
        if (s->used && s->pack && s->status == SEQ_PLAYING)
            mp_render(s->pack, mix, frames, (int)(s->vol * xmi_master * 0x4000 / (127 * 127)));
    }
}

/* 1 when the sequence has a FOR loop with no end (its pack file then loops too) */
static int loops_forever(const uint8_t *p)
{
    for (;;) {
        uint8_t b = *p++;
        if (b < 0x80)
            continue;
        switch (b >> 4) {
        case 0x9:
            p += 2;
            vlq(&p);
            break;
        case 0x8: case 0xA: case 0xE:
            p += 2;
            break;
        case 0xB:
            if (p[0] == 116 && p[1] == 0)
                return 1;
            p += 2;
            break;
        case 0xC: case 0xD:
            p += 1;
            break;
        default:
            if (b == 0xFF) {
                uint8_t type = *p++;
                unsigned long len = vlq(&p);
                if (type == 0x2F)
                    return 0;
                p += len;
            } else if (b == 0xF0 || b == 0xF7) {
                unsigned long len = vlq(&p);
                p += len;
            } else
                return 0;
        }
    }
}

/* the output has just changed (under the interrupt lock): it is told every channel's state, banks
 * and program first, the RPN last, then the notes still held start again */
void xmi_restore(void)
{
    static const uint8_t first[] = { 0, 32, 112, 114 }, last[] = { 101, 100, 6, 38 };
    int ch, k, i;
    if (!chst_ready)
        return;
    for (ch = 0; ch < 16; ch++) {
        for (k = 0; k < 4; k++)
            if (chst[ch].cc[first[k]] >= 0 && (first[k] < 112 || xmi_output == MUSIC_FM))
                send(0xB0 | ch, first[k], chst[ch].cc[first[k]]);
        if (chst[ch].prog >= 0)
            send(0xC0 | ch, chst[ch].prog, 0);
        for (k = 1; k < 120; k++)
            if (chst[ch].cc[k] >= 0 && k != 6 && k != 32 && k != 38 && (k < 98 || k > 101) &&
                k != 112 && k != 114)
                send(0xB0 | ch, k, chst[ch].cc[k]);
        if (chst[ch].cc[101] >= 0 && chst[ch].cc[100] >= 0)
            for (k = 0; k < 4; k++)
                if (chst[ch].cc[last[k]] >= 0)
                    send(0xB0 | ch, last[k], chst[ch].cc[last[k]]);
        if (chst[ch].pressure >= 0)
            send(0xD0 | ch, chst[ch].pressure, 0);
        if (chst[ch].bend >= 0)
            send(0xE0 | ch, chst[ch].bend & 127, chst[ch].bend >> 7);
    }
    for (i = 0; i < MAX_SEQS; i++)
        if (seqs[i].used && seqs[i].status == SEQ_PLAYING && !seqs[i].pack)
            for (k = 0; k < seqs[i].nnotes; k++)
                send(0x90 | seqs[i].notes[k].ch, seqs[i].notes[k].note, seqs[i].notes[k].vel);
}

void xmi_all_off(void)
{
    int i;
    for (i = 0; i < MAX_SEQS; i++)
        if (seqs[i].used && seqs[i].status == SEQ_PLAYING)
            end_seq(&seqs[i], 0);
}

/* ------------------------------------------------------------------ AIL sequence API */
#define LOCK int was_ = port_irq_save()
#define UNLOCK port_irq_restore(was_)

/* the Mission Selector's movies (missions.c): the music stops where it is and goes on from there
 * afterwards. The game still sees its sequences playing; their held notes go off and come back
 * with the channels' state (xmi_restore) */
void port_music_pause(int on)
{
    int i, k;
    LOCK;
    if (on && !xmi_paused) {
        for (i = 0; i < MAX_SEQS; i++) {
            Seq *s = &seqs[i];
            if (!s->used || s->status != SEQ_PLAYING || s->pack)
                continue;
            for (k = 0; k < s->nnotes; k++)
                send(0x80 | s->notes[k].ch, s->notes[k].note, 0);
            for (k = 0; k < 16; k++)
                if (s->chans & (1 << k))
                    send(0xB0 | k, 64, 0);
        }
        xmi_paused = 1;
    } else if (!on && xmi_paused) {
        xmi_paused = 0;
        xmi_restore();
    }
    UNLOCK;
}

void *AIL_allocate_sequence_handle(void *mdi)
{
    int i;
    Seq *r = NULL;
    LOCK;
    (void)mdi;
    for (i = 0; i < MAX_SEQS; i++)
        if (!seqs[i].used) {
            r = &seqs[i];
            memset(r, 0, sizeof *r);
            r->used = 1;
            r->status = SEQ_DONE;
            break;
        }
    UNLOCK;
    return r;
}

void AIL_release_sequence_handle(void *S)
{
    Seq *s;
    MpTrack *pack = NULL;
    LOCK;
    if ((s = seq_of(S)) != NULL) {
        flush_notes(s);
        s->used = 0;
        s->status = SEQ_FREE;
        pack = s->pack;
        s->pack = NULL;
    }
    UNLOCK;
    mp_free(pack);
}

long AIL_init_sequence(void *S, void *start, long num)
{
    Seq *s;
    const uint8_t *evnt, *rbrn;
    long r = 0;
    int ch;
    MpTrack *old_pack = NULL;
    LOCK;
    if ((s = seq_of(S)) != NULL) {
        if (s->status == SEQ_PLAYING || s->status == SEQ_STOPPED)
            flush_notes(s);
        s->status = SEQ_DONE;
        old_pack = s->pack;             /* port_music_attach gives the new piece its file */
        s->pack = NULL;
        s->entry = s->num = 0;
        if (start && find_sequence(start, (int)num, &evnt, &rbrn)) {
            s->evnt = evnt;
            s->rbrn = rbrn;
            s->nbranch = rbrn ? rbrn[0] | rbrn[1] << 8 : 0;
            s->prefix = s->trigger = s->beat_fn = s->eos = NULL;
            s->loop_count = 1;
            s->vol = s->vol_to = 127;
            s->vol_ticks = 0;
            s->tempo = s->tempo_to = 100;
            s->tempo_ticks = 0;
            s->tempo_acc = 0;
            s->tempo_us = 500000;
            s->numer = 4;
            s->denom = 2;
            set_beat_length(s, 2);
            s->beat_frac = s->beat = s->measure = 0;
            s->nnotes = 0;
            s->chans = 0;
            for (ch = 0; ch < 16; ch++)
                s->cc7[ch] = 127;
            restart(s);
            r = 1;
        } else if (start)
            port_log("Music: no sequence %ld in the XMIDI data\n", num);
    }
    UNLOCK;
    mp_free(old_pack);
    return r;
}

void AIL_start_sequence(void *S)
{
    Seq *s;
    LOCK;
    if ((s = seq_of(S)) != NULL && s->evnt) {
        if (s->status == SEQ_PLAYING || s->status == SEQ_STOPPED)
            flush_notes(s);
        restart(s);
        s->beat_frac = s->beat = s->measure = 0;
        s->tempo_acc = 0;
        s->elapsed = 0;
        s->status = SEQ_PLAYING;
        if (s->pack)
            mp_rewind(s->pack);
        if (getenv("FREEFALL_TRACE_SOUND"))
            port_log("Music: sequence %d starts\n", (int)(s - seqs));
    }
    UNLOCK;
}

void AIL_stop_sequence(void *S)
{
    Seq *s;
    LOCK;
    if ((s = seq_of(S)) != NULL && s->status == SEQ_PLAYING) {
        flush_notes(s);
        s->status = SEQ_STOPPED;
    }
    UNLOCK;
}

void AIL_resume_sequence(void *S)
{
    Seq *s;
    LOCK;
    if ((s = seq_of(S)) != NULL && s->status == SEQ_STOPPED) {
        send_volumes(s);
        s->status = SEQ_PLAYING;
    }
    UNLOCK;
}

void AIL_end_sequence(void *S)
{
    Seq *s;
    LOCK;
    if ((s = seq_of(S)) != NULL && (s->status == SEQ_PLAYING || s->status == SEQ_STOPPED))
        end_seq(s, 1);
    UNLOCK;
}

void AIL_set_sequence_tempo(void *S, long tempo, long ms)
{
    Seq *s;
    LOCK;
    if ((s = seq_of(S)) != NULL) {
        long ticks = ms * XMI_RATE / 1000;
        if (ticks <= 0) {
            s->tempo = s->tempo_to = tempo;
            s->tempo_ticks = 0;
        } else {
            s->tempo_from = s->tempo;
            s->tempo_to = tempo;
            s->tempo_ticks = ticks;
            s->tempo_t = 0;
        }
    }
    UNLOCK;
}

void AIL_set_sequence_volume(void *S, long vol, long ms)
{
    Seq *s;
    LOCK;
    if (vol < 0)
        vol = 0;
    if (vol > 127)
        vol = 127;
    if ((s = seq_of(S)) != NULL) {
        long ticks = ms * XMI_RATE / 1000;
        if (ticks <= 0) {
            s->vol = s->vol_to = vol;
            s->vol_ticks = 0;
            if (s->status == SEQ_PLAYING)
                send_volumes(s);
        } else {
            s->vol_from = s->vol;
            s->vol_to = vol;
            s->vol_ticks = ticks;
            s->vol_t = 0;
        }
    }
    UNLOCK;
}

long AIL_sequence_status(void *S)
{
    Seq *s = seq_of(S);
    return s ? s->status : 0;
}

void AIL_branch_index(void *S, unsigned long marker)
{
    Seq *s;
    int i;
    LOCK;
    if ((s = seq_of(S)) != NULL && s->rbrn)
        for (i = 0; i < s->nbranch; i++) {
            const uint8_t *e = s->rbrn + 2 + 6 * i;
            if ((unsigned long)(e[0] | e[1] << 8) == marker) {
                flush_notes(s);
                s->ptr = s->evnt + (e[2] | e[3] << 8 | e[4] << 16 | (uint32_t)e[5] << 24);
                s->wait = 0;
                break;
            }
        }
    UNLOCK;
}

static void *set_cb(void *S, int which, void *cb)
{
    Seq *s;
    void *old = NULL;
    LOCK;
    if ((s = seq_of(S)) != NULL) {
        void **slot = which == 0 ? &s->prefix : which == 1 ? &s->trigger : which == 2 ? &s->eos : &s->beat_fn;
        old = *slot;
        *slot = cb;
    }
    UNLOCK;
    return old;
}

void *AIL_register_prefix_callback(void *S, void *cb) { return set_cb(S, 0, cb); }
void *AIL_register_trigger_callback(void *S, void *cb) { return set_cb(S, 1, cb); }
void *AIL_register_sequence_callback(void *S, void *cb) { return set_cb(S, 2, cb); }
void *AIL_register_beat_callback(void *S, void *cb) { return set_cb(S, 3, cb); }

void AIL_set_sequence_user_data(void *S, unsigned long index, long value)
{
    Seq *s = seq_of(S);
    if (s && index < 8)
        s->user[index] = value;
}

long AIL_sequence_user_data(void *S, unsigned long index)
{
    Seq *s = seq_of(S);
    return s && index < 8 ? s->user[index] : 0;
}

void AIL_set_XMIDI_master_volume(void *mdi, long v)
{
    int i;
    LOCK;
    (void)mdi;
    xmi_master = v < 0 ? 0 : v > 127 ? 127 : v;
    for (i = 0; i < MAX_SEQS; i++)
        if (seqs[i].used && seqs[i].status == SEQ_PLAYING)
            send_volumes(&seqs[i]);
    UNLOCK;
}

long AIL_XMIDI_master_volume(void *mdi)
{
    (void)mdi;
    return xmi_master;
}

/* music packs (musicpack.c): the game's music code (music.c) names the music table entry and
 * sequence number of a sequence it has just prepared; with a pack file for it, the file plays
 * instead of the MIDI */
void port_music_attach(void *S, int entry, int num)
{
    Seq *s = seq_of(S);
    MpTrack *t, *old;

    if (!s || !s->evnt || s->status != SEQ_DONE)
        return;
    s->entry = entry;
    s->num = num;
    t = mp_open(entry, num, loops_forever(s->evnt));
    if (!t)
        return;
    {
        LOCK;
        old = s->pack;
        s->pack = t;
        UNLOCK;
    }
    mp_free(old);
}

/* the player picked another pack (or none) in the options: every prepared piece gets the new
 * pack's file at once, and a playing one jumps to where its MIDI is */
void port_music_pack_changed(void)
{
    int i;
    for (i = 0; i < MAX_SEQS; i++) {
        Seq *s = &seqs[i];
        MpTrack *t = NULL, *old;
        int playing;
        if (!s->used || !s->evnt || !s->entry)
            continue;
        playing = s->status == SEQ_PLAYING || s->status == SEQ_STOPPED;
        if (s->status == SEQ_DONE || playing)
            t = mp_open(s->entry, s->num, loops_forever(s->evnt));
        if (t && playing)
            mp_seek_ms(t, s->elapsed * 1000 / XMI_RATE);
        {
            LOCK;
            if (playing)
                flush_notes(s);         /* the MIDI notes stop; with no pack the next ones sound */
            old = s->pack;
            s->pack = t;
            UNLOCK;
        }
        mp_free(old);
    }
}
