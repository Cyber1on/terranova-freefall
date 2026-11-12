/* fmsynth.c -- the Miles AdLib / OPL3 MIDI driver (ADLIB.MDI, SBPRO2.MDI ...) on an emulated
 * OPL3 (Nuked OPL3).  The driver logic -- virtual and physical FM voices, priorities, the note
 * to frequency tables, volume and panning -- follows ScummVM's reimplementation of the Miles
 * AdLib driver (audio/miles_adlib.cpp, GPL-3.0-or-later, which dumped its tables from ADLIB.MDI).
 * Added here: the OPL3 driver's four-operator timbres (25-byte entries of the .OPL timbre file),
 * played on the OPL3's four-operator channel pairs, the pairs handled as the game's OPL3 driver
 * (SBPRO2.MDI, its register writes recorded in DOSBox) does it (P102): given in turn, kept
 * four-operator through a note's release, split only when a two-operator note takes one of
 * their channels, and taken from another note by priority when none is free.  A channel's pitch
 * bend reaches 12 semitones, as with both of the game's drivers (P104: the base music's doubled
 * lines are detuned by small bends).  Played twice as loud as the chip puts it out (P106: the
 * emulated OPL3's own level, the card's, was very quiet next to the General MIDI and AWE32
 * music), a peak limiter keeping the few loudest moments under full scale.
 *
 * Timbre files (Miles "global timbre library", e.g. FF.AD / FF.OPL): a directory of
 * (patch, bank, offset) entries ending in patch = bank = 0xFF; each timbre is
 *   u16 size, s8 transposition, operator 1: 20 40 60 80 E0, C0, operator 2: 20 40 60 80 E0
 * and for a four-operator timbre (size 25) operator 3: 20 40 60 80 E0, a byte (0 in all of
 * FF.OPL), operator 4: 20 40 60 80 E0.  Every timbre of size 25 is a four-operator one, and
 * bit 7 of its C0 byte is the second channel's connection bit (P99: the operators each
 * algorithm puts out are the loud ones of the timbres).  Bank 127 holds the percussion
 * (patch = note). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miles_int.h"
#include "port.h"
#include "../../third_party/nuked-opl3/opl3.h"

#define VOICES_MAX 20               /* virtual FM voices */
#define PHYS_MAX 18                 /* OPL3 channels */
#define PERC_BANK 127

typedef struct {
    uint8_t bank, patch;
    int8_t transpose;
    uint8_t four_op;
    uint8_t op[4][5];               /* per operator: 20 40 60 80 E0 */
    uint8_t c0, c0b;                /* c0b: the second channel's connection bit (four_op) */
} Timbre;

typedef struct {
    uint8_t bank;
    const Timbre *timbre;
    uint16_t bender;
    uint8_t range, protect, volume, expression, pan, modulation, sustain, active;
} Chan;

typedef struct {
    uint8_t used, chan, physical, phys, orig_note, note, velocity, sustained;
    const Timbre *timbre;
    int16_t transpose;
    uint16_t priority;
} Voice;

typedef struct {
    uint8_t used, voice, b0, four;  /* four: this channel and channel+3 play a 4-op voice */
} Phys;

static const uint16_t op1_reg[PHYS_MAX] = {
    0x000, 0x001, 0x002, 0x008, 0x009, 0x00A, 0x010, 0x011, 0x012,
    0x100, 0x101, 0x102, 0x108, 0x109, 0x10A, 0x110, 0x111, 0x112 };
static const uint16_t op2_reg[PHYS_MAX] = {
    0x003, 0x004, 0x005, 0x00B, 0x00C, 0x00D, 0x013, 0x014, 0x015,
    0x103, 0x104, 0x105, 0x10B, 0x10C, 0x10D, 0x113, 0x114, 0x115 };
static const uint16_t chan_reg[PHYS_MAX] = {
    0x000, 0x001, 0x002, 0x003, 0x004, 0x005, 0x006, 0x007, 0x008,
    0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108 };

/* note -> F-number (ADLIB.MDI) */
static const uint16_t freq_table[] = {
    0x02B2, 0x02B4, 0x02B7, 0x02B9, 0x02BC, 0x02BE, 0x02C1, 0x02C3, 0x02C6, 0x02C9, 0x02CB, 0x02CE,
    0x02D0, 0x02D3, 0x02D6, 0x02D8, 0x02DB, 0x02DD, 0x02E0, 0x02E3, 0x02E5, 0x02E8, 0x02EB, 0x02ED,
    0x02F0, 0x02F3, 0x02F6, 0x02F8, 0x02FB, 0x02FE, 0x0301, 0x0303, 0x0306, 0x0309, 0x030C, 0x030F,
    0x0311, 0x0314, 0x0317, 0x031A, 0x031D, 0x0320, 0x0323, 0x0326, 0x0329, 0x032B, 0x032E, 0x0331,
    0x0334, 0x0337, 0x033A, 0x033D, 0x0340, 0x0343, 0x0346, 0x0349, 0x034C, 0x034F, 0x0352, 0x0356,
    0x0359, 0x035C, 0x035F, 0x0362, 0x0365, 0x0368, 0x036B, 0x036F, 0x0372, 0x0375, 0x0378, 0x037B,
    0x037F, 0x0382, 0x0385, 0x0388, 0x038C, 0x038F, 0x0392, 0x0395, 0x0399, 0x039C, 0x039F, 0x03A3,
    0x03A6, 0x03A9, 0x03AD, 0x03B0, 0x03B4, 0x03B7, 0x03BB, 0x03BE, 0x03C1, 0x03C5, 0x03C8, 0x03CC,
    0x03CF, 0x03D3, 0x03D7, 0x03DA, 0x03DE, 0x03E1, 0x03E5, 0x03E8, 0x03EC, 0x03F0, 0x03F3, 0x03F7,
    0x03FB, 0x03FE, 0xFE01, 0xFE03, 0xFE05, 0xFE07, 0xFE08, 0xFE0A, 0xFE0C, 0xFE0E, 0xFE10, 0xFE12,
    0xFE14, 0xFE16, 0xFE18, 0xFE1A, 0xFE1C, 0xFE1E, 0xFE20, 0xFE21, 0xFE23, 0xFE25, 0xFE27, 0xFE29,
    0xFE2B, 0xFE2D, 0xFE2F, 0xFE31, 0xFE34, 0xFE36, 0xFE38, 0xFE3A, 0xFE3C, 0xFE3E, 0xFE40, 0xFE42,
    0xFE44, 0xFE46, 0xFE48, 0xFE4A, 0xFE4C, 0xFE4F, 0xFE51, 0xFE53, 0xFE55, 0xFE57, 0xFE59, 0xFE5C,
    0xFE5E, 0xFE60, 0xFE62, 0xFE64, 0xFE67, 0xFE69, 0xFE6B, 0xFE6D, 0xFE6F, 0xFE72, 0xFE74, 0xFE76,
    0xFE79, 0xFE7B, 0xFE7D, 0xFE7F, 0xFE82, 0xFE84, 0xFE86, 0xFE89, 0xFE8B, 0xFE8D, 0xFE90, 0xFE92,
    0xFE95, 0xFE97, 0xFE99, 0xFE9C, 0xFE9E, 0xFEA1, 0xFEA3, 0xFEA5, 0xFEA8, 0xFEAA, 0xFEAD, 0xFEAF };

/* velocity / 8 -> level (ADLIB.MDI) */
static const uint8_t velocity_table[16] = {
    82, 85, 88, 91, 94, 97, 100, 103, 106, 109, 112, 115, 118, 121, 124, 127 };

/* OPL3 four-operator channel pairs: first channel of each (the second is +3) and its 0x104 bit */
static const uint8_t pair_first[6] = { 0, 1, 2, 9, 10, 11 };

enum { UPD_20 = 1, UPD_40 = 2, UPD_60 = 4, UPD_C0 = 8, UPD_E0 = 16, UPD_A0 = 32, UPD_ALL = 63 };

static opl3_chip chip;
static int is_open, opl3, nphys, nvoices;
static Timbre *timbres;
static int ntimbres;
static Chan chans[16];
static Voice voices[VOICES_MAX];
static Phys phys[PHYS_MAX];
static uint8_t last_phys = PHYS_MAX;
static uint8_t last_pair = 5;           /* the four-operator pair last given (pair_first) */
static uint8_t reg104;

static void wr(int reg, int val)
{
    if (!opl3 && (reg & 0x100))
        return;
    OPL3_WriteReg(&chip, (uint16_t)reg, (uint8_t)val);
}

static const Timbre *find_timbre(int bank, int patch)
{
    int i;
    for (i = 0; i < ntimbres; i++)
        if (timbres[i].bank == bank && timbres[i].patch == patch)
            return &timbres[i];
    return NULL;
}

static int pair_index(int ch)
{
    int i;
    for (i = 0; i < 6; i++)
        if (pair_first[i] == ch)
            return i;
    return -1;
}

/* a channel that is the second half of a four-operator pair playing a note (P102: once the note
 * is let go a two-operator voice may take either half, as with the OPL3 driver) */
static int is_pair_second(int ch)
{
    return ch >= 3 && pair_index(ch - 3) >= 0 && phys[ch - 3].four && phys[ch - 3].used;
}

static int free_phys(int want_four)
{
    int n, p = last_phys;

    if (want_four) {
        /* P102: circular too, from the last pair used, as the OPL3 driver (from the first pair
         * each time, the pairs of the first half of the chip were taken over and over and a
         * second four-operator note often found none free) */
        int i, k = last_pair;
        for (i = 0; i < 6; i++) {
            int a;
            if (++k >= 6)
                k = 0;
            a = pair_first[k];
            if (!phys[a].used && !phys[a + 3].used && !is_pair_second(a)) {
                last_pair = (uint8_t)k;
                return a;
            }
        }
        return -1;
    }
    /* circular search from the last channel used (newer Miles drivers) */
    for (n = 0; n < nphys; n++) {
        if (++p >= nphys)
            p = 0;
        if (!phys[p].used && !is_pair_second(p)) {
            last_phys = (uint8_t)p;
            return p;
        }
    }
    return -1;
}

static void set_four(int ch, int on)
{
    int i = pair_index(ch);
    uint8_t v;
    if (i < 0)
        return;
    v = on ? (uint8_t)(reg104 | (1 << i)) : (uint8_t)(reg104 & ~(1 << i));
    phys[ch].four = (uint8_t)on;
    if (on)
        phys[ch + 3].used = 1;
    else if (phys[ch + 3].used && phys[ch + 3].voice == 0xFF)
        phys[ch + 3].used = 0;
    if (v != reg104) {
        reg104 = v;
        wr(0x104, v);
    }
}

/* P102: the four-operator pair of first channel ch made two channels again for a two-operator
 * voice on channel keep (ch or ch + 3), as the OPL3 driver does it: the other channel's
 * operators let go at once and its note cut (the pair's last note ends there) */
static void pair_off(int ch, int keep)
{
    int o = keep == ch ? ch + 3 : ch;
    set_four(ch, 0);
    wr(0x80 + op1_reg[o], 0x0F);
    wr(0x80 + op2_reg[o], 0x0F);
    wr(0xB0 + chan_reg[o], 0);
    phys[o].b0 = 0;
}

/* scale an operator's total level by vol (0..127) */
static uint8_t scaled40(uint8_t r40, int vol)
{
    int v = (~r40) & 0x3F;
    v = v * vol / 127;
    return (uint8_t)((r40 & 0xC0) | ((~v) & 0x3F));
}

static void update(int vi, int key_on, int flags)
{
    Voice *v = &voices[vi];
    Chan *c = &chans[v->chan];
    const Timbre *t = v->timbre;
    const uint8_t (*op)[5] = t->op;
    int p, four, vol = 0, c0 = t->c0;
    uint16_t o[4];

    if (!v->physical)
        return;
    p = v->phys;
    four = phys[p].four;
    if (!four && t->four_op) {          /* a four-operator timbre on a lone channel (an OPL2
                                           with a .OPL file; P102: on the OPL3 these always
                                           get a pair): operators 3 and 4 as a two-operator
                                           voice (operator 4 puts out in every algorithm,
                                           operator 2 in only one) */
        op = t->op + 2;
        c0 = t->c0b;
    }
    o[0] = op1_reg[p];
    o[1] = op2_reg[p];
    o[2] = four ? op1_reg[p + 3] : 0;
    o[3] = four ? op2_reg[p + 3] : 0;

    if (flags & UPD_40) {
        vol = c->volume * c->expression * 2;
        vol >>= 8;
        if (vol)
            vol++;
        vol = vol * v->velocity * 2;
        vol >>= 8;
        if (vol)
            vol++;
        if (vol > 127)
            vol = 127;
    }
    if (flags & UPD_20) {
        int k, n = four ? 4 : 2;
        for (k = 0; k < n; k++)
            wr(0x20 + o[k], op[k][0] | (c->modulation >= 64 ? 0x40 : 0));
    }
    if (flags & UPD_40) {
        if (!four) {
            wr(0x40 + o[0], (c0 & 1) ? scaled40(op[0][1], vol) : op[0][1]);
            wr(0x40 + o[1], scaled40(op[1][1], vol));
        } else {
            /* scale the carriers of the four-operator algorithm */
            int a = t->c0 & 1, b = t->c0b & 1;
            int car[4] = { a, !a && b, a && b, 1 };
            int k;
            for (k = 0; k < 4; k++)
                wr(0x40 + o[k], car[k] ? scaled40(op[k][1], vol) : op[k][1]);
        }
    }
    if (flags & UPD_60) {
        int k, n = four ? 4 : 2;
        for (k = 0; k < n; k++) {
            wr(0x60 + o[k], op[k][2]);
            wr(0x80 + o[k], op[k][3]);
        }
    }
    if (flags & UPD_E0) {
        int k, n = four ? 4 : 2;
        for (k = 0; k < n; k++)
            wr(0xE0 + o[k], op[k][4]);
    }
    if (flags & UPD_C0) {
        int out = 0;
        if (opl3)
            out = c->pan <= 27 ? 0x20 : c->pan >= 100 ? 0x10 : 0x30;
        wr(0xC0 + chan_reg[p], (c0 & 0x0F) | out);
        if (four)
            wr(0xC0 + chan_reg[p + 3], t->c0b | out);
    }
    if (flags & UPD_A0) {
        if (!key_on) {
            wr(0xB0 + chan_reg[p], phys[p].b0 & 0x1F);
        } else {
            int bend = (int)c->bender - 0x2000;
            int note = v->note, pnote;
            unsigned freq;
            int octave;
            uint8_t b0;

            bend >>= 5;
            bend *= c->range;
            note += v->transpose;
            note -= 24;
            do note += 12; while (note < 0);
            note += 12;
            do note -= 12; while (note > 95);
            note = (note << 8) + bend + 8;
            note >>= 4;
            note -= 12 * 16;
            do note += 12 * 16; while (note < 0);
            note += 12 * 16;
            do note -= 12 * 16; while (note > 96 * 16 - 1);
            pnote = note >> 4;
            freq = freq_table[((pnote % 12) << 4) + (note & 0x0F)];
            octave = pnote / 12 - 1;
            if (freq & 0x8000)
                octave++;
            if (octave & 0x80) {
                octave++;
                freq >>= 1;
            }
            b0 = (uint8_t)(((freq >> 8) & 3) | ((octave & 7) << 2) | 0x20);
            wr(0xA0 + chan_reg[p], freq & 0xFF);
            wr(0xB0 + chan_reg[p], b0);
            phys[p].b0 = b0;
        }
    }
}

static void assign(int vi, int p)
{
    Voice *v = &voices[vi];
    chans[v->chan].active++;
    v->physical = 1;
    v->phys = (uint8_t)p;
    phys[p].used = 1;
    phys[p].voice = (uint8_t)vi;
    if (opl3 && v->timbre->four_op && pair_index(p) >= 0 && !phys[p + 3].used) {
        phys[p + 3].voice = 0xFF;
        set_four(p, 1);
    } else if (phys[p].four)
        pair_off(p, p);
    else if (p >= 3 && pair_index(p - 3) >= 0 && phys[p - 3].four)
        pair_off(p - 3, p);
    update(vi, 1, UPD_ALL);
}

static void release(int vi)
{
    Voice *v = &voices[vi];
    int p;
    if (!v->physical) {
        v->used = 0;
        return;
    }
    p = v->phys;
    update(vi, 0, UPD_A0);
    v->physical = 0;
    v->used = 0;
    phys[p].used = 0;
    /* P102: a four-operator pair stays one through the note's release, as with the OPL3 driver
     * (it is made two channels again only when a two-operator voice takes it): switched at the
     * note's end, the pair's modulators played on as two-operator voices, a crack each time */
    if (phys[p].four && phys[p + 3].voice == 0xFF)
        phys[p + 3].used = 0;
    if (chans[v->chan].active)
        chans[v->chan].active--;
}

/* the four-operator pair (its first channel) a voice on channel c would leave free if let go:
 * c is in a pair and the pair's other channel is free or c's own voice's, else -1 */
static int pair_of(int c)
{
    int f = pair_index(c) >= 0 ? c : c >= 3 && pair_index(c - 3) >= 0 ? c - 3 : -1;
    if (f < 0)
        return -1;
    if (phys[f].four)
        return c == f ? f : -1;
    return phys[c == f ? f + 3 : f].used ? -1 : f;
}

static void priority_sort(void)
{
    uint16_t pri[VOICES_MAX];
    int i, n = 0;

    memset(pri, 0, sizeof pri);
    for (i = 0; i < nvoices; i++)
        if (voices[i].used) {
            Chan *c = &chans[voices[i].chan];
            unsigned p = c->protect >= 64 ? 0xFFFF : voices[i].priority;
            p = p >= c->active ? p - c->active : 0;
            pri[i] = (uint16_t)p;
            n++;
        }
    while (n) {
        unsigned hi = 0, lo = 0xFFFF;
        int hv = 0, lv = -1, p, pair;
        for (i = 0; i < nvoices; i++)
            if (voices[i].used && !voices[i].physical && pri[i] >= hi) {
                hi = pri[i];
                hv = i;
            }
        /* P102: a four-operator note takes a pair, as with the OPL3 driver (never a lone
         * channel, where it played as its second half only): a voice's on a pair whose other
         * channel is free, or a four-operator voice's */
        if (hi == 0)
            break;
        pair = opl3 && voices[hv].timbre && voices[hv].timbre->four_op;
        for (i = 0; i < nvoices; i++)
            if (voices[i].used && voices[i].physical && pri[i] <= lo && (!pair || pair_of(voices[i].phys) >= 0)) {
                lo = pri[i];
                lv = i;
            }
        if (lv < 0 || hi < lo || hi == 0 || !voices[lv].physical || voices[hv].physical)
            break;
        p = pair ? pair_of(voices[lv].phys) : voices[lv].phys;
        release(lv);
        assign(hv, p);
        n--;
    }
}

static void note_off(int ch, int note)
{
    int i;
    for (i = 0; i < nvoices; i++)
        if (voices[i].used && voices[i].chan == ch && voices[i].orig_note == note) {
            if (chans[ch].sustain >= 64) {
                voices[i].sustained = 1;
                continue;
            }
            release(i);
        }
}

static void note_on(int ch, int note, int vel)
{
    const Timbre *t;
    Voice *v;
    int i, p;

    if (vel == 0) {
        note_off(ch, note);
        return;
    }
    t = ch == 9 ? find_timbre(PERC_BANK, note) : chans[ch].timbre;
    if (!t)
        return;
    for (i = 0; i < nvoices && voices[i].used; i++)
        ;
    if (i == nvoices)
        return;
    v = &voices[i];
    if (ch != 9) {
        v->note = (uint8_t)note;
        v->transpose = t->transpose;
    } else {
        v->note = (uint8_t)t->transpose;
        v->transpose = 0;
    }
    v->used = 1;
    v->chan = (uint8_t)ch;
    v->orig_note = (uint8_t)note;
    v->timbre = t;
    v->velocity = velocity_table[(vel & 0x7F) >> 3];
    v->physical = 0;
    v->sustained = 0;
    v->priority = 32767;
    p = opl3 && t->four_op ? free_phys(1) : free_phys(0);
    if (p < 0) {
        priority_sort();
        return;
    }
    assign(i, p);
}

static void release_sustain(int ch)
{
    int i;
    for (i = 0; i < nvoices; i++)
        if (voices[i].used && voices[i].chan == ch && voices[i].sustained)
            note_off(ch, voices[i].orig_note);
}

static void control(int ch, int cc, int val)
{
    Chan *c = &chans[ch];
    int flags = 0, i;

    switch (cc) {
    case 114: c->bank = (uint8_t)val; break;            /* Miles: patch bank select */
    case 112: c->protect = (uint8_t)val; break;         /* Miles: voice protect */
    case 1: c->modulation = (uint8_t)val; flags = UPD_20; break;
    case 7: c->volume = (uint8_t)val; flags = UPD_40; break;
    case 11: c->expression = (uint8_t)val; flags = UPD_40; break;
    case 10:
        c->pan = (uint8_t)val;
        if (opl3)
            flags = UPD_C0;
        break;
    case 64:
        c->sustain = (uint8_t)val;
        if (val < 64)
            release_sustain(ch);
        break;
    case 6: c->range = (uint8_t)val; break;             /* data entry: Miles 3 bend range */
    case 121:
        c->sustain = 0;
        release_sustain(ch);
        c->modulation = 0;
        c->expression = 127;
        c->bender = 0x2000;
        flags = UPD_20 | UPD_40 | UPD_A0;
        break;
    case 123:
        for (i = 0; i < nvoices; i++)
            if (voices[i].used && voices[i].chan == ch)
                note_off(ch, voices[i].orig_note);
        break;
    }
    if (flags)
        for (i = 0; i < nvoices; i++)
            if (voices[i].used && voices[i].chan == ch)
                update(i, 1, flags);
}

void fm_msg(int status, int d1, int d2)
{
    int ch = status & 15, i;

    if (!is_open)
        return;
    switch (status & 0xF0) {
    case 0x80: note_off(ch, d1); break;
    case 0x90: note_on(ch, d1, d2); break;
    case 0xB0: control(ch, d1, d2); break;
    case 0xC0: {
        const Timbre *t = find_timbre(chans[ch].bank, d1);
        if (t)
            chans[ch].timbre = t;
        break;
    }
    case 0xE0:
        chans[ch].bender = (uint16_t)((d1 & 0x7F) | ((d2 & 0x7F) << 7));
        for (i = 0; i < nvoices; i++)
            if (voices[i].used && voices[i].chan == ch)
                update(i, 1, UPD_A0);
        break;
    }
}

/* P106: the FM music's level: twice the chip's, through a peak limiter (instant attack, 80 ms
 * release) that holds it under 29,000.  Over all 92 pieces the music is 6 dB louder (5.4 dB the
 * least); the limiter turns it down by more than 1 dB 0.7 % of the time (OPL3; OPL2 0.2 %), by
 * 4.4 dB at the most, for a moment. */
#define FM_GAIN 2.0f
#define FM_LIMIT 29000.0f
static float lim_gain = 1.0f;

void fm_render(int32_t *mix, int frames)
{
    const float release = 1.0f / (0.08f * MIX_RATE);
    int i;
    int16_t s[2];
    if (!is_open)
        return;
    for (i = 0; i < frames; i++) {
        float l, r, pk;
        OPL3_GenerateResampled(&chip, s);
        l = s[0] * FM_GAIN;
        r = s[1] * FM_GAIN;
        pk = l < 0 ? -l : l;
        if ((r < 0 ? -r : r) > pk)
            pk = r < 0 ? -r : r;
        if (pk * lim_gain > FM_LIMIT)
            lim_gain = FM_LIMIT / pk;
        mix[2 * i] += (int32_t)(l * lim_gain);
        mix[2 * i + 1] += (int32_t)(r * lim_gain);
        lim_gain += (1.0f - lim_gain) * release;
    }
}

static int load_timbres(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t *d;
    long size;
    int n, i, o;

    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = malloc(size + 32);
    if (!d || fread(d, 1, size, f) != (size_t)size) {
        fclose(f);
        free(d);
        return -1;
    }
    fclose(f);
    for (n = 0, o = 0; o + 2 <= size && !(d[o] == 0xFF && d[o + 1] == 0xFF); o += 6)
        n++;
    free(timbres);
    timbres = calloc(n ? n : 1, sizeof *timbres);
    ntimbres = 0;
    for (i = 0, o = 0; i < n; i++, o += 6) {
        Timbre *t = &timbres[ntimbres];
        uint32_t off = d[o + 2] | d[o + 3] << 8 | d[o + 4] << 16 | (uint32_t)d[o + 5] << 24;
        const uint8_t *e;
        int len;
        if (off + 14 > (uint32_t)size)
            continue;
        e = d + off;
        len = e[0] | e[1] << 8;
        if (len != 14 && (len != 25 || off + 25 > (uint32_t)size))
            continue;
        t->patch = d[o];
        t->bank = d[o + 1];
        t->transpose = (int8_t)e[2];
        memcpy(t->op[0], e + 3, 5);
        t->c0 = e[8];
        memcpy(t->op[1], e + 9, 5);
        if (len == 25) {
            memcpy(t->op[2], e + 14, 5);
            memcpy(t->op[3], e + 20, 5);
            /* P99: bit 7 of the C0 byte is the second channel's connection bit, not a mark of
             * the four-operator timbres (read so, a quarter of them played as their two quiet
             * modulators and most of the rest as four operators in a row) */
            t->c0b = (uint8_t)(t->c0 >> 7);
            t->four_op = 1;
        }
        ntimbres++;
    }
    free(d);
    return ntimbres ? 0 : -1;
}

static void reset_chip(void)
{
    int i, k;
    static const int op_bases[] = { 0x20, 0x40, 0x60, 0x80, 0xE0 };
    static const int op_vals[] = { 1, 0x3F, 0xFF, 0x0F, 0 };

    OPL3_Reset(&chip, MIX_RATE);
    reg104 = 0;
    if (opl3) {
        wr(0x105, 1);
        wr(0x104, 0);
    }
    wr(0x01, opl3 ? 0 : 0x20);
    wr(0x02, 0);
    wr(0x03, 0);
    wr(0x04, 0x60);
    wr(0x04, 0x80);
    wr(0x08, 0);
    wr(0xBD, 0xC0);
    for (k = 0; k < 5; k++)
        for (i = 0; i < nphys; i++) {
            wr(op_bases[k] + op1_reg[i], op_vals[k]);
            wr(op_bases[k] + op2_reg[i], op_vals[k]);
        }
    for (i = 0; i < nphys; i++) {
        wr(0xA0 + chan_reg[i], 0);
        wr(0xB0 + chan_reg[i], 0);
        wr(0xC0 + chan_reg[i], 0);
    }
}

int fm_open(const char *timbre_file, int want_opl3)
{
    int i;

    if (load_timbres(timbre_file) != 0) {
        port_log("FM music: cannot read the timbre file %s\n", timbre_file);
        return -1;
    }
    opl3 = want_opl3;
    nphys = opl3 ? 18 : 9;
    nvoices = opl3 ? 20 : 16;
    memset(chans, 0, sizeof chans);
    memset(voices, 0, sizeof voices);
    memset(phys, 0, sizeof phys);
    last_phys = PHYS_MAX;
    last_pair = 5;
    for (i = 0; i < 16; i++) {
        chans[i].volume = 0x7F;
        chans[i].pan = 0x40;
        chans[i].expression = 127;
        chans[i].bender = 0x2000;
        chans[i].range = 12;                /* P104: 12 semitones, as SBPRO2.MDI (DOSBox) */
    }
    reset_chip();
    lim_gain = 1.0f;
    is_open = 1;
    port_log("FM music: %s, %d timbres from %s\n", opl3 ? "OPL3" : "OPL2", ntimbres, timbre_file);
    return 0;
}

void fm_close(void)
{
    is_open = 0;
    free(timbres);
    timbres = NULL;
    ntimbres = 0;
}
