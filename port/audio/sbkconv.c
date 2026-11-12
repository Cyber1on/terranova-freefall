/* sbkconv.c -- the game's AWE32 bank (SOUND\FF.SBK, "TerraNova GM" by Eric Brosius, 1995) as a
 * SoundFont 2 file in memory, for the SoundFont music (sfsynth.c).
 *
 * FF.SBK is a SoundFont 1 bank (ifil 1.0, for the EMU8000 of the Sound Blaster AWE32).  Its
 * preset and instrument lists are laid out as in SoundFont 2, but:
 *   - the sample headers are 16 bytes (start, end, loop start, loop end), their names in a
 *     separate snam chunk; the samples before the first one at address 0 are the card's ROM
 *     samples (the 1 MB General MIDI ROM, "irom 1MGM"; their names start with '*'), the others
 *     are in the bank's own smpl chunk.  Every sample is taken as 44.1 kHz, pitch 60;
 *   - the loop points are one and two words earlier than SoundFont 2's;
 *   - most generators are in the EMU8000's own units (times in milliseconds, levels 0-127 ...),
 *     a zone's generators replace the instrument's global zone's (in SoundFont 2 those are
 *     defaults too), and an unset sustain level is no sustain;
 *   - generator 55 is the sample's pitch (in cents: 6000 = key 60), and coarse and fine tune
 *     follow the zone's scale tuning.
 * The conversion follows the rules Takashi Iwai worked out for the Linux AWE32 driver's
 * sfxload (awesfx: SBKtoSF2.txt and awelib/sbkconv.c, parsesf.c, sample.c; GPL-2.0-or-later).
 *
 * The ROM samples are not in the file.  With a dump of the ROM (rom, 512K words) they are taken
 * from it; without one only the presets whose samples are all the bank's own are made (and the
 * General MIDI SoundFont under this bank plays the others). */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NGEN 60                         /* SoundFont 1 has a generator 59; it is dropped */
enum { G_KEYRANGE = 43, G_VELRANGE = 44, G_INSTRUMENT = 41, G_SAMPLEID = 53, G_SAMPLEMODES = 54,
       G_SAMPLEPITCH = 55, G_SCALETUNING = 56, G_ROOTKEY = 58, G_COARSE = 51, G_FINE = 52 };

typedef struct { const uint8_t *p; uint32_t n; } Chunk;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* the sub-chunk id in the LIST lt of the RIFF (any depth of LISTs) */
static int find_chunk(const uint8_t *d, uint32_t n, const char *lt, const char *id, Chunk *c)
{
    uint32_t o = 12;
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "sfbk", 4))
        return 0;
    while (o + 12 <= n) {
        uint32_t sz = rd32(d + o + 4), e, q;
        /* P115: a size past the end of the file is cut to it (o + 8 + sz went round past 4 GB: the
         * step forward was 0 and the loop never ended, or it went back to an earlier chunk) */
        if (sz > n - o - 8)
            sz = n - o - 8;
        if (!memcmp(d + o, "LIST", 4) && !memcmp(d + o + 8, lt, 4)) {
            e = o + 8 + sz;
            for (q = o + 12; q + 8 <= e;) {
                uint32_t s2 = rd32(d + q + 4);
                if (s2 > e - q - 8)     /* runs past its list (P115: q + 8 + s2 could go round) */
                    break;
                if (!memcmp(d + q, id, 4)) {
                    c->p = d + q + 8;
                    c->n = s2;
                    return 1;
                }
                q += 8 + s2 + (s2 & 1);
            }
        }
        o += 8 + sz + (sz & 1);
    }
    return 0;
}

/* ---- the output, a growing byte buffer */
typedef struct { uint8_t *p; size_t n, cap; int bad; } Buf;

static void put(Buf *b, const void *s, size_t n)
{
    if (b->bad)
        return;
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 65536;
        uint8_t *q;
        while (c < b->n + n)
            c *= 2;
        q = realloc(b->p, c);
        if (!q) {
            b->bad = 1;
            return;
        }
        b->p = q;
        b->cap = c;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
}

static void put16(Buf *b, int v) { uint8_t t[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; put(b, t, 2); }
static void put32(Buf *b, uint32_t v) { uint8_t t[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; put(b, t, 4); }
static void put_name(Buf *b, const char *s)
{
    char t[20] = { 0 };
    size_t l = strlen(s);
    memcpy(t, s, l > 19 ? 19 : l);      /* a name of up to 19 characters and its 0 */
    put(b, t, 20);
}
/* a chunk header whose size is filled in by end_chunk */
static size_t begin_chunk(Buf *b, const char *id, const char *list)
{
    size_t at = b->n;
    put(b, id, 4);
    put32(b, 0);
    if (list)
        put(b, list, 4);
    return at;
}
static void end_chunk(Buf *b, size_t at)
{
    uint32_t sz = (uint32_t)(b->n - at - 8);
    if (b->bad)
        return;
    b->p[at + 4] = (uint8_t)sz;
    b->p[at + 5] = (uint8_t)(sz >> 8);
    b->p[at + 6] = (uint8_t)(sz >> 16);
    b->p[at + 7] = (uint8_t)(sz >> 24);
    if (sz & 1)
        put(b, "", 1);
}

/* ---- SoundFont 1 units to SoundFont 2's (awesfx sbkconv.c) */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int ms_to_tc(int ms) { return (int)(log((ms <= 0 ? 1 : ms) / 1000.0) / log(2.0) * 1200.0); }

static int convert(int g, int v)
{
    switch (g) {
    case 8:  return clampi(v == 127 ? 13500 : 59 * v + 4366, 1500, 13500);      /* filter cutoff */
    case 9:  return clampi(v * 3 / 2, 0, 960);                                   /* filter Q */
    case 15: case 16: return clampi(v * 1000 / 256, 0, 1000);                   /* chorus, reverb */
    case 17: return clampi(v * 1000 / 127 - 500, -500, 500);                     /* pan */
    case 48: return v <= 0 ? 1440 : clampi((int)(-2000.0 * log10(v / 127.0)), 0, 1440); /* attenuation */
    case 56: return v ? 50 : 100;                                                /* scale tuning */
    case 21: case 23: case 25: case 26: case 27: case 28: case 30:
    case 33: case 34: case 35: case 36: case 38:
        return clampi(ms_to_tc(v), -12000, 8000);                               /* times */
    case 31: case 32: case 39: case 40: return clampi((int)(v * 5.55), -1200, 1200);  /* per key */
    case 22: return v > 0 ? clampi((int)(1200 * log10((double)v) / log10(2.0) - 7925.0), -16000, 4500) : -725;
    case 24: return v > 0 ? clampi((int)(1200 * log10((double)v) / log10(2.0) - 7925.0), -16000, 4500) : -15600;
    case 5: case 6: case 7: return clampi((1200 * v / 64 + 1) / 2, -12000, 12000);  /* to pitch */
    case 10: return clampi(1200 * 3 * v / 64, -12000, 12000);                   /* mod LFO to filter */
    case 11: return clampi(1200 * 6 * v / 64, -12000, 12000);                   /* mod env to filter */
    case 13: return clampi(120 * v / 64, -960, 960);                            /* tremolo */
    case 29: return v < 96 ? 1000 * (96 - v) / 96 : 0;                           /* mod env sustain */
    case 37: return v < 96 ? (2000 - 21 * v) / 2 : 0;                           /* vol env sustain */
    default: return v;
    }
}

typedef struct { int16_t val[NGEN]; uint8_t set[NGEN]; } Zone;

/* an instrument's number in the output, -1 if it is left out or not one of the bank's (P115) */
static int inst_out(const int *map, int n, int k)
{
    return k >= 0 && k < n ? map[k] : -1;
}

/* the zone's generators bag z of the instrument (or preset) generator list */
static void read_zone(const Chunk *bag, const Chunk *gen, int z, Zone *t)
{
    uint32_t a, e, i;
    memset(t, 0, sizeof *t);
    if (z < 0 || 4 * (uint32_t)z + 8 > bag->n)
        return;                         /* past the bag list (a damaged bank, P115): no generators */
    a = rd16(bag->p + 4 * z);
    e = rd16(bag->p + 4 * (z + 1));
    for (i = a; i < e && 4 * i + 4 <= gen->n; i++) {
        int op = rd16(gen->p + 4 * i), am = (int16_t)rd16(gen->p + 4 * i + 2);
        if (op < NGEN) {
            t->val[op] = (int16_t)am;
            t->set[op] = 1;
        }
    }
}

/* the instrument generators SoundFont 2 is given for an SBK zone (its global zone g merged in) */
static int sf2_zone(const Zone *gz, const Zone *lz, int16_t *out_op, int16_t *out_am, int *sample)
{
    Zone z = *lz;
    int g, n = 0, root, tune, scale, hi;
    if (gz)
        for (g = 0; g < NGEN; g++)
            if (gz->set[g] && !z.set[g]) {
                z.val[g] = gz->val[g];
                z.set[g] = 1;
            }
    if (!z.set[G_SAMPLEID])
        return -1;
    *sample = z.val[G_SAMPLEID];
    /* the pitch (awesfx parsesf.c set_rootkey): SBK sample pitch, the root key over it, then
     * the tunings, which follow the scale tuning */
    scale = z.set[G_SCALETUNING] ? convert(G_SCALETUNING, z.val[G_SCALETUNING]) : 100;
    root = 60;
    tune = 0;
    if (z.set[G_SAMPLEPITCH]) {
        root = z.val[G_SAMPLEPITCH] / 100;
        tune = -(z.val[G_SAMPLEPITCH] % 100);
        if (tune <= -50) {
            root++;
            tune += 100;
        }
        if (scale == 50)
            tune /= 2;
    }
    if (z.set[G_ROOTKEY])
        root += z.val[G_ROOTKEY] - 60;
    tune += z.val[G_COARSE] * scale + z.val[G_FINE] * scale / 100;
    hi = z.set[G_KEYRANGE] ? ((uint16_t)z.val[G_KEYRANGE]) >> 8 : 127;
    if (root >= hi + 60)
        root -= 60;
    /* ranges first, the sample last */
    if (z.set[G_KEYRANGE]) {
        out_op[n] = G_KEYRANGE; out_am[n++] = z.val[G_KEYRANGE];
    }
    if (z.set[G_VELRANGE]) {
        out_op[n] = G_VELRANGE; out_am[n++] = z.val[G_VELRANGE];
    }
    for (g = 0; g < NGEN; g++) {
        int v;
        if (g == G_KEYRANGE || g == G_VELRANGE || g == G_SAMPLEID || g == G_SAMPLEPITCH || g == G_ROOTKEY ||
            g == G_COARSE || g == G_FINE || g == G_SCALETUNING || g == G_INSTRUMENT ||
            g == 14 || g == 18 || g == 19 || g == 20 || g == 42 || g == 49 || g >= 59)
            continue;
        if (z.set[g])
            v = convert(g, z.val[g]);
        else if (g == 29 || g == 37)
            v = 1000;                   /* SBK: no sustain level is none */
        else if (g == 22)
            v = -725;                   /* SBK's LFO defaults */
        else if (g == 24)
            v = -15600;
        else
            continue;
        out_op[n] = (int16_t)g;
        out_am[n++] = (int16_t)v;
    }
    out_op[n] = G_SCALETUNING; out_am[n++] = (int16_t)scale;
    out_op[n] = G_ROOTKEY; out_am[n++] = (int16_t)clampi(root, 0, 127);
    out_op[n] = G_COARSE; out_am[n++] = (int16_t)clampi(tune / 100, -120, 120);
    out_op[n] = G_FINE; out_am[n++] = (int16_t)(tune % 100);
    return n;
}

uint8_t *sbk_to_sf2(const uint8_t *d, size_t len, const int16_t *rom, size_t rom_words, size_t *out_len,
                    int *made, int *total)
{
    Chunk snam, smpl, phdr, pbag, pgen, inst, ibag, igen, shdr;
    int nsmp, ninst, npre, i, z, k, first_ram = -1, nout_pre = 0;
    uint8_t *have = NULL, *inst_used = NULL, *smp_used = NULL;
    int *inst_map = NULL, *smp_map = NULL, ninst_out = 0, nsmp_out = 0;
    uint32_t *smp_at = NULL;
    Buf b = { 0 };
    size_t riff, list, ck;
    int nbag;

    *made = *total = 0;
    if (len > 0x7FFFFFFF || !find_chunk(d, (uint32_t)len, "sdta", "smpl", &smpl) ||
        !find_chunk(d, (uint32_t)len, "pdta", "phdr", &phdr) || !find_chunk(d, (uint32_t)len, "pdta", "pbag", &pbag) ||
        !find_chunk(d, (uint32_t)len, "pdta", "pgen", &pgen) || !find_chunk(d, (uint32_t)len, "pdta", "inst", &inst) ||
        !find_chunk(d, (uint32_t)len, "pdta", "ibag", &ibag) || !find_chunk(d, (uint32_t)len, "pdta", "igen", &igen) ||
        !find_chunk(d, (uint32_t)len, "pdta", "shdr", &shdr))
        return NULL;
    if (!find_chunk(d, (uint32_t)len, "sdta", "snam", &snam))
        snam.p = NULL, snam.n = 0;
    nsmp = (int)(shdr.n / 16);
    ninst = (int)(inst.n / 22) - 1;
    npre = (int)(phdr.n / 38) - 1;
    if (nsmp <= 0 || ninst <= 0 || npre <= 0)
        return NULL;
    have = calloc((size_t)nsmp, 1);
    smp_used = calloc((size_t)nsmp, 1);
    smp_map = calloc((size_t)nsmp, sizeof *smp_map);
    smp_at = calloc((size_t)nsmp, sizeof *smp_at);
    inst_used = calloc((size_t)ninst, 1);
    inst_map = calloc((size_t)ninst, sizeof *inst_map);
    if (!have || !smp_used || !smp_map || !smp_at || !inst_used || !inst_map)
        goto fail;
    /* which samples are there: the ROM's (before the first at address 0) only with the dump */
    for (i = 0; i < nsmp; i++) {
        const uint8_t *h = shdr.p + 16 * i;
        uint32_t s = rd32(h), e = rd32(h + 4);
        if (first_ram < 0 && s == 0)
            first_ram = i;
        if (first_ram >= 0 && i >= first_ram)
            have[i] = e > s && e <= smpl.n / 2;
        else
            have[i] = rom && e > s && e <= rom_words;
    }
    /* the presets whose instruments' samples are all there */
    for (i = 0; i < npre; i++) {
        const uint8_t *p = phdr.p + 38 * i;
        int b0 = rd16(p + 24), b1 = rd16(p + 38 + 24), ok = 1, any = 0;
        for (z = b0; z < b1 && ok; z++) {
            Zone pz;
            read_zone(&pbag, &pgen, z, &pz);
            if (!pz.set[G_INSTRUMENT])
                continue;
            k = pz.val[G_INSTRUMENT];
            if (k < 0 || k >= ninst) {
                ok = 0;
                break;
            }
            {
                int a = rd16(inst.p + 22 * k + 20), e = rd16(inst.p + 22 * (k + 1) + 20), zz;
                for (zz = a; zz < e; zz++) {
                    Zone iz;
                    read_zone(&ibag, &igen, zz, &iz);
                    if (iz.set[G_SAMPLEID]) {
                        any = 1;
                        if (iz.val[G_SAMPLEID] < 0 || iz.val[G_SAMPLEID] >= nsmp || !have[iz.val[G_SAMPLEID]])
                            ok = 0;
                    }
                }
            }
        }
        (*total)++;
        if (!ok || !any)
            continue;
        nout_pre++;
        for (z = b0; z < b1; z++) {
            Zone pz;
            read_zone(&pbag, &pgen, z, &pz);
            if (pz.set[G_INSTRUMENT]) {
                int a, e, zz;
                k = pz.val[G_INSTRUMENT];
                inst_used[k] = 1;
                a = rd16(inst.p + 22 * k + 20);
                e = rd16(inst.p + 22 * (k + 1) + 20);
                for (zz = a; zz < e; zz++) {
                    Zone iz;
                    read_zone(&ibag, &igen, zz, &iz);
                    if (iz.set[G_SAMPLEID])
                        smp_used[iz.val[G_SAMPLEID]] = 1;
                }
            }
        }
    }
    *made = nout_pre;
    if (!nout_pre)
        goto fail;
    for (k = 0; k < ninst; k++)
        inst_map[k] = inst_used[k] ? ninst_out++ : -1;

    riff = begin_chunk(&b, "RIFF", "sfbk");
    list = begin_chunk(&b, "LIST", "INFO");
    ck = begin_chunk(&b, "ifil", NULL); put16(&b, 2); put16(&b, 1); end_chunk(&b, ck);
    ck = begin_chunk(&b, "isng", NULL); put(&b, "EMU8000", 8); end_chunk(&b, ck);
    ck = begin_chunk(&b, "INAM", NULL); put(&b, "TerraNova GM (FF.SBK)", 22); end_chunk(&b, ck);
    end_chunk(&b, list);
    /* the samples, each with the 46 zero words SoundFont 2 wants after it */
    list = begin_chunk(&b, "LIST", "sdta");
    ck = begin_chunk(&b, "smpl", NULL);
    for (i = 0; i < nsmp; i++) {
        const uint8_t *h = shdr.p + 16 * i;
        uint32_t s = rd32(h), e = rd32(h + 4), w;
        if (!smp_used[i]) {
            smp_map[i] = -1;
            continue;
        }
        smp_map[i] = nsmp_out++;
        smp_at[i] = (uint32_t)((b.n - ck - 8) / 2);
        if (first_ram >= 0 && i >= first_ram)
            put(&b, smpl.p + 2 * s, 2 * (size_t)(e - s));
        else
            for (w = s; w < e; w++)
                put16(&b, rom[w]);
        for (w = 0; w < 46; w++)
            put16(&b, 0);
    }
    end_chunk(&b, ck);
    end_chunk(&b, list);

    list = begin_chunk(&b, "LIST", "pdta");
    /* presets: their zones are just instruments (FF.SBK's preset layer has nothing else) */
    ck = begin_chunk(&b, "phdr", NULL);
    nbag = 0;
    for (i = 0; i < npre; i++) {
        const uint8_t *p = phdr.p + 38 * i;
        int b0 = rd16(p + 24), b1 = rd16(p + 38 + 24), keep = 1, nz = 0;
        for (z = b0; z < b1; z++) {
            Zone pz;
            read_zone(&pbag, &pgen, z, &pz);
            if (pz.set[G_INSTRUMENT]) {
                if (inst_out(inst_map, ninst, pz.val[G_INSTRUMENT]) < 0)
                    keep = 0;
                nz++;
            }
        }
        if (!keep || !nz)
            continue;
        put(&b, p, 20);                 /* name */
        put16(&b, rd16(p + 20));        /* preset */
        put16(&b, rd16(p + 22));        /* bank */
        put16(&b, nbag);
        put32(&b, 0); put32(&b, 0); put32(&b, 0);
        nbag += nz;
    }
    put_name(&b, "EOP"); put16(&b, 0); put16(&b, 0); put16(&b, nbag); put32(&b, 0); put32(&b, 0); put32(&b, 0);
    end_chunk(&b, ck);
    ck = begin_chunk(&b, "pbag", NULL);
    for (k = 0; k <= nbag; k++) {
        put16(&b, k);
        put16(&b, 0);
    }
    end_chunk(&b, ck);
    ck = begin_chunk(&b, "pmod", NULL); put(&b, "\0\0\0\0\0\0\0\0\0\0", 10); end_chunk(&b, ck);
    ck = begin_chunk(&b, "pgen", NULL);
    for (i = 0; i < npre; i++) {
        const uint8_t *p = phdr.p + 38 * i;
        int b0 = rd16(p + 24), b1 = rd16(p + 38 + 24), keep = 1;
        for (z = b0; z < b1; z++) {
            Zone pz;
            read_zone(&pbag, &pgen, z, &pz);
            if (pz.set[G_INSTRUMENT] && inst_out(inst_map, ninst, pz.val[G_INSTRUMENT]) < 0)
                keep = 0;
        }
        if (!keep)
            continue;
        for (z = b0; z < b1; z++) {
            Zone pz;
            read_zone(&pbag, &pgen, z, &pz);
            if (!pz.set[G_INSTRUMENT])
                continue;
            if (pz.set[G_KEYRANGE]) {
                put16(&b, G_KEYRANGE);
                put16(&b, pz.val[G_KEYRANGE]);
            }
            if (pz.set[G_VELRANGE]) {
                put16(&b, G_VELRANGE);
                put16(&b, pz.val[G_VELRANGE]);
            }
            put16(&b, G_INSTRUMENT);
            put16(&b, inst_map[pz.val[G_INSTRUMENT]]);
        }
    }
    put32(&b, 0);
    end_chunk(&b, ck);
    /* the pbag's generator indexes: rewrite now that the counts are known */
    {
        size_t pb = 0, pg = 0, o;
        int gi = 0, zi = 0;
        for (o = list + 12; o + 8 < b.n; o += 8 + rd32(b.p + o + 4) + (rd32(b.p + o + 4) & 1)) {
            if (!memcmp(b.p + o, "pbag", 4))
                pb = o + 8;
            else if (!memcmp(b.p + o, "pgen", 4))
                pg = o + 8;
        }
        for (i = 0; i < npre && pb && pg; i++) {
            const uint8_t *p = phdr.p + 38 * i;
            int b0 = rd16(p + 24), b1 = rd16(p + 38 + 24), keep = 1;
            for (z = b0; z < b1; z++) {
                Zone pz;
                read_zone(&pbag, &pgen, z, &pz);
                if (pz.set[G_INSTRUMENT] && inst_out(inst_map, ninst, pz.val[G_INSTRUMENT]) < 0)
                    keep = 0;
            }
            if (!keep)
                continue;
            for (z = b0; z < b1; z++) {
                Zone pz;
                read_zone(&pbag, &pgen, z, &pz);
                if (!pz.set[G_INSTRUMENT])
                    continue;
                b.p[pb + 4 * zi] = (uint8_t)gi;
                b.p[pb + 4 * zi + 1] = (uint8_t)(gi >> 8);
                zi++;
                gi += 1 + pz.set[G_KEYRANGE] + pz.set[G_VELRANGE];
            }
        }
        if (pb && !b.bad) {
            b.p[pb + 4 * zi] = (uint8_t)gi;
            b.p[pb + 4 * zi + 1] = (uint8_t)(gi >> 8);
        }
    }
    /* instruments: every local zone with its global zone merged in and converted */
    {
        Buf ibagb = { 0 }, igenb = { 0 };
        int zone_n = 0, gen_n = 0;
        ck = begin_chunk(&b, "inst", NULL);
        for (k = 0; k < ninst; k++) {
            const uint8_t *ip = inst.p + 22 * k;
            int a = rd16(ip + 20), e = rd16(ip + 22 + 20), zz;
            Zone gz, *gzp = NULL;
            if (inst_map[k] < 0)
                continue;
            put(&b, ip, 20);
            put16(&b, zone_n);
            for (zz = a; zz < e; zz++) {
                Zone iz;
                int16_t op[NGEN + 8], am[NGEN + 8];
                int n, s, j;
                read_zone(&ibag, &igen, zz, &iz);
                if (!iz.set[G_SAMPLEID]) {
                    if (zz == a) {
                        gz = iz;
                        gzp = &gz;
                    }
                    continue;
                }
                n = sf2_zone(gzp, &iz, op, am, &s);
                if (n < 0 || s < 0 || s >= nsmp || smp_map[s] < 0)
                    continue;
                put16(&ibagb, gen_n);
                put16(&ibagb, 0);
                for (j = 0; j < n; j++) {
                    put16(&igenb, op[j]);
                    put16(&igenb, am[j]);
                }
                put16(&igenb, G_SAMPLEID);
                put16(&igenb, smp_map[s]);
                gen_n += n + 1;
                zone_n++;
            }
        }
        put_name(&b, "EOI");
        put16(&b, zone_n);
        end_chunk(&b, ck);
        put16(&ibagb, gen_n);
        put16(&ibagb, 0);
        put32(&igenb, 0);
        ck = begin_chunk(&b, "ibag", NULL); put(&b, ibagb.p, ibagb.n); end_chunk(&b, ck);
        ck = begin_chunk(&b, "imod", NULL); put(&b, "\0\0\0\0\0\0\0\0\0\0", 10); end_chunk(&b, ck);
        ck = begin_chunk(&b, "igen", NULL); put(&b, igenb.p, igenb.n); end_chunk(&b, ck);
        b.bad |= ibagb.bad | igenb.bad;
        free(ibagb.p);
        free(igenb.p);
    }
    /* sample headers: 44.1 kHz, pitch 60; SBK loop points + 1 and + 2 (awesfx sample.c) */
    ck = begin_chunk(&b, "shdr", NULL);
    for (i = 0; i < nsmp; i++) {
        const uint8_t *h = shdr.p + 16 * i;
        uint32_t s = rd32(h), e = rd32(h + 4), ls = rd32(h + 8) + 1, le = rd32(h + 12) + 2, at = smp_at[i];
        char name[21] = { 0 };
        if (smp_map[i] < 0)
            continue;
        if (snam.p && (uint32_t)(20 * i + 20) <= snam.n)
            memcpy(name, snam.p + 20 * i, 20);
        if (name[0] == '*')
            memmove(name, name + 1, 20);
        if (le > e)
            le = e;
        if (ls < s || ls >= le) {
            ls = s;
            le = e;
        }
        put_name(&b, name);
        put32(&b, at);
        put32(&b, at + (e - s));
        put32(&b, at + (ls - s));
        put32(&b, at + (le - s));
        put32(&b, 44100);
        put(&b, "\x3C\0", 2);           /* pitch 60, no correction */
        put16(&b, 0);
        put16(&b, 1);                   /* mono */
    }
    put_name(&b, "EOS");
    for (k = 0; k < 26; k++)
        put(&b, "", 1);
    end_chunk(&b, ck);
    end_chunk(&b, list);
    end_chunk(&b, riff);
    if (b.bad)
        goto fail;
    free(have); free(smp_used); free(smp_map); free(smp_at); free(inst_used); free(inst_map);
    *out_len = b.n;
    return b.p;
fail:
    free(have); free(smp_used); free(smp_map); free(smp_at); free(inst_used); free(inst_map);
    free(b.p);
    return NULL;
}
