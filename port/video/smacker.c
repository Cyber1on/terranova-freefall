/* smacker.c -- Smacker movie playback for the standalone build: a decoder for the Smacker
 * format (version 2 and 4 video, Huffman-packed DPCM audio) written from the format's public
 * description, and the parts of the RAD Smacker 2.x DOS library and its SVGA helper that the
 * game calls (src/game/main/movie.c).  Those entry points keep the original's addresses and
 * argument order (the game calls them with the arguments reversed from the SDK's C order).
 *
 * The Smack structure keeps the fields the game reads at their original offsets: Width +4,
 * Height +8, Frames +0xC, NewPalette +0x68, Palette +0x6C (256 x 6-bit RGB, as for the DAC).
 * Movie sound goes straight to an SDL audio stream.
 *
 * With "Smooth videos" (videosmooth.c) a second decoder of the same file runs one picture ahead
 * of the game, video only, and hands each picture to the RIFE worker, so the in-between picture
 * of the one on screen and the next is ready by the time it is due.  The game's own decoder and
 * what it sees stay as they are. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "port.h"
#include "videosmooth.h"

/* ------------------------------------------------------------------ bit reader (LSB first) */
typedef struct {
    const uint8_t *p, *end;
    uint32_t bit;                       /* bit position in *p */
} bits;

static void bits_init(bits *b, const uint8_t *p, size_t n)
{
    b->p = p;
    b->end = p + n;
    b->bit = 0;
}

static int get_bit(bits *b)
{
    int v;
    if (b->p >= b->end)
        return 0;
    v = (*b->p >> b->bit) & 1;
    if (++b->bit == 8) {
        b->bit = 0;
        b->p++;
    }
    return v;
}

static unsigned get_bits(bits *b, int n)
{
    unsigned v = 0;
    int i;
    for (i = 0; i < n; i++)
        v |= (unsigned)get_bit(b) << i;
    return v;
}

/* ------------------------------------------------------------------ Huffman trees
 * Stored as an array: a node is {-1, index of the "1" child} followed by its "0" subtree;
 * a leaf is {value, 0}. */
typedef struct {
    int (*n)[2];
    int count, cap;
} tree;

static int tree_add(tree *t, int a, int b)
{
    if (t->count == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 256;
        t->n = realloc(t->n, t->cap * sizeof *t->n);
    }
    t->n[t->count][0] = a;
    t->n[t->count][1] = b;
    return t->count++;
}

static void tree_free(tree *t)
{
    free(t->n);
    memset(t, 0, sizeof *t);
}

/* an 8-bit tree: "1" = node (0 subtree, then 1 subtree), "0" = leaf with an 8-bit value */
static void read_small(bits *b, tree *t, int depth)
{
    if (depth > 32 || b->p >= b->end) {
        tree_add(t, 0, 0);
        return;
    }
    if (get_bit(b)) {
        int node = tree_add(t, -1, 0);
        read_small(b, t, depth + 1);
        t->n[node][1] = t->count;
        read_small(b, t, depth + 1);
    } else
        tree_add(t, (int)get_bits(b, 8), 0);
}

static int decode(bits *b, const tree *t)
{
    int i = 0;
    if (!t->count)
        return 0;
    while (t->n[i][0] == -1)
        i = get_bit(b) ? t->n[i][1] : i + 1;
    return t->n[i][0];
}

/* a 16-bit "big" tree: leaves are a low byte and a high byte from two small trees; three
 * escape values mark the leaves that hold the last three values decoded (a cache). */
typedef struct {
    tree t;
    int last[3];                        /* leaf index of the three cache entries */
} bigtree;

static void read_big_nodes(bits *b, bigtree *bt, const tree *lo, const tree *hi, const int *esc, int depth)
{
    if (depth > 64 || b->p >= b->end) {
        tree_add(&bt->t, 0, 0);
        return;
    }
    if (get_bit(b)) {
        int node = tree_add(&bt->t, -1, 0);
        read_big_nodes(b, bt, lo, hi, esc, depth + 1);
        bt->t.n[node][1] = bt->t.count;
        read_big_nodes(b, bt, lo, hi, esc, depth + 1);
    } else {
        int v = decode(b, lo) | decode(b, hi) << 8, i;
        for (i = 0; i < 3; i++)
            if (v == esc[i]) {
                bt->last[i] = bt->t.count;
                v = 0;
            }
        tree_add(&bt->t, v, 0);
    }
}

static void read_big(bits *b, bigtree *bt)
{
    tree lo = { 0 }, hi = { 0 };
    int esc[3], i;

    memset(bt, 0, sizeof *bt);
    if (!get_bit(b)) {
        tree_add(&bt->t, 0, 0);
        bt->last[0] = bt->last[1] = bt->last[2] = tree_add(&bt->t, 0, 0);
        return;
    }
    if (get_bit(b)) {
        read_small(b, &lo, 0);
        get_bit(b);
    }
    if (get_bit(b)) {
        read_small(b, &hi, 0);
        get_bit(b);
    }
    for (i = 0; i < 3; i++)
        esc[i] = (int)get_bits(b, 16);
    bt->last[0] = bt->last[1] = bt->last[2] = -1;
    read_big_nodes(b, bt, &lo, &hi, esc, 0);
    get_bit(b);
    for (i = 0; i < 3; i++)
        if (bt->last[i] < 0)
            bt->last[i] = tree_add(&bt->t, 0, 0);
    tree_free(&lo);
    tree_free(&hi);
}

static void big_reset(bigtree *bt)
{
    bt->t.n[bt->last[0]][0] = bt->t.n[bt->last[1]][0] = bt->t.n[bt->last[2]][0] = 0;
}

static int big_decode(bits *b, bigtree *bt)
{
    int i = 0, v;
    while (bt->t.n[i][0] == -1)
        i = get_bit(b) ? bt->t.n[i][1] : i + 1;
    v = bt->t.n[i][0];
    if (v != bt->t.n[bt->last[0]][0]) {
        bt->t.n[bt->last[2]][0] = bt->t.n[bt->last[1]][0];
        bt->t.n[bt->last[1]][0] = bt->t.n[bt->last[0]][0];
        bt->t.n[bt->last[0]][0] = v;
    }
    return v;
}

/* ------------------------------------------------------------------ the Smack structure */
#pragma pack(push, 1)
typedef struct Smack {
    uint32_t Version;                   /* +0x00 */
    uint32_t Width, Height, Frames;     /* +0x04 +0x08 +0x0C */
    uint32_t MSPerFrame;                /* +0x10 */
    uint32_t SmackerType;               /* +0x14 */
    uint32_t LargestInTrack[7];         /* +0x18 */
    uint32_t tablesize, codesize, absize, detailsize, typesize;   /* +0x34 */
    uint32_t TrackType[7];              /* +0x48 */
    uint32_t extra;                     /* +0x64 */
    uint32_t NewPalette;                /* +0x68 */
    uint8_t Palette[772];               /* +0x6C */
    uint32_t PalType;
    uint32_t FrameNum;                  /* current frame (0-based) */
    /* ---- ours */
    SDL_IOStream *io;
    int version4;
    uint32_t flags;
    uint32_t audio_rate[7];
    uint32_t *frame_size;
    uint8_t *frame_type;
    uint64_t *frame_pos;
    bigtree mmap, mclr, full, type;
    uint8_t *chunk;
    uint32_t chunk_len, chunk_cap;
    uint32_t video_off;                 /* where the video data starts in chunk */
    uint8_t *image;                     /* Width x Height, the decoded picture */
    uint8_t *dest;                      /* SmackToScreen target */
    uint32_t dest_pitch;
    double ms_per_frame;
    uint64_t start_us;                  /* when frame 0 was due */
    uint64_t pause_us;
    SDL_AudioStream *audio;
    int audio_on;
    int volume;                         /* 0..0x8000 (32768 = full) */
    /* ---- Smooth videos */
    struct Smack *ahead;                /* the decoder one picture ahead (video only) */
    vsmooth *vs;
    uint8_t *mid_rgb;
    uint32_t shown;                     /* the picture SmackDoFrame put up last */
    uint32_t mid_handled;               /* in-between picture before this one: dealt with */
    uint32_t dest_off;                  /* where the frames go in video memory */
} Smack;
#pragma pack(pop)

/* ------------------------------------------------------------------ audio */
static void open_audio(Smack *s)
{
    SDL_AudioSpec spec;
    uint32_t a = s->audio_rate[0];

    if (!(a & 0x40000000) || !(SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO)))
        return;
    spec.freq = (int)(a & 0xFFFFFF);
    spec.channels = (a & 0x10000000) ? 2 : 1;
    spec.format = (a & 0x20000000) ? SDL_AUDIO_S16LE : SDL_AUDIO_U8;
    s->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (s->audio) {
        SDL_SetAudioStreamGain(s->audio, s->volume / 32768.0f);
        SDL_ResumeAudioStreamDevice(s->audio);
        s->audio_on = 1;
    }
}

static void play_audio(Smack *s, const uint8_t *p, uint32_t n)
{
    uint32_t a = s->audio_rate[0], unp;
    int stereo = (a & 0x10000000) != 0, b16 = (a & 0x20000000) != 0;
    uint8_t *out;

    if (!s->audio || !s->audio_on || n < 4)
        return;
    if (!(a & 0x80000000)) {
        SDL_PutAudioStreamData(s->audio, p, (int)n);
        return;
    }
    unp = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
    if (unp > 16 << 20)
        return;
    out = malloc(unp + 4);
    {
        bits b;
        tree t[4] = { { 0 } };
        int i, nt = 1 << (b16 + stereo), pred[2] = { 0, 0 };
        uint32_t k;

        bits_init(&b, p + 4, n - 4);
        if (!get_bit(&b)) {
            free(out);
            return;
        }
        if (get_bit(&b) != stereo || get_bit(&b) != b16) {
            free(out);
            return;
        }
        for (i = 0; i < nt; i++) {
            if (get_bit(&b))
                read_small(&b, &t[i], 0);
            get_bit(&b);
        }
        if (b16) {
            int16_t *o = (int16_t *)out;
            uint32_t total = unp / 2;
            for (i = stereo; i >= 0; i--) {
                unsigned v = get_bits(&b, 16);
                pred[i] = (int16_t)((v >> 8) | (v << 8));
            }
            for (k = 0; k <= (uint32_t)stereo && k < total; k++)
                o[k] = (int16_t)pred[k];
            for (; k < total; k++) {
                int ch = (int)(k & (uint32_t)stereo);
                int v = decode(&b, &t[ch * 2]) | decode(&b, &t[ch * 2 + 1]) << 8;
                pred[ch] = (int16_t)(pred[ch] + (int16_t)v);
                o[k] = (int16_t)pred[ch];
            }
        } else {
            for (i = stereo; i >= 0; i--)
                pred[i] = (int)get_bits(&b, 8);
            for (k = 0; k <= (uint32_t)stereo && k < unp; k++)
                out[k] = (uint8_t)pred[k];
            for (; k < unp; k++) {
                int ch = (int)(k & (uint32_t)stereo);
                pred[ch] = (uint8_t)(pred[ch] + (int8_t)decode(&b, &t[ch]));
                out[k] = (uint8_t)pred[ch];
            }
        }
        for (i = 0; i < 4; i++)
            tree_free(&t[i]);
    }
    SDL_PutAudioStreamData(s->audio, out, (int)unp);
    free(out);
}

/* ------------------------------------------------------------------ frames */
static const uint8_t pal_scale[64] = { 0 };     /* unused: the game wants 6-bit values */

/* loads frame FrameNum's chunk and applies its palette (NewPalette tells the game) */
static int load_frame(Smack *s)
{
    uint32_t n, off = 0;
    int i;

    (void)pal_scale;
    if (s->FrameNum >= s->Frames)
        return 0;
    n = s->frame_size[s->FrameNum] & ~3u;
    if (n > s->chunk_cap) {
        s->chunk_cap = n;
        s->chunk = realloc(s->chunk, n);
    }
    SDL_SeekIO(s->io, (Sint64)s->frame_pos[s->FrameNum], SDL_IO_SEEK_SET);
    s->chunk_len = (uint32_t)SDL_ReadIO(s->io, s->chunk, n);
    s->NewPalette = 0;
    if ((s->frame_type[s->FrameNum] & 1) && s->chunk_len) {
        uint32_t len = s->chunk[0] * 4u, p = 1;
        uint8_t old[768];
        memcpy(old, s->Palette, 768);
        i = 0;
        while (i < 256 && p < len && p < s->chunk_len) {
            uint8_t t = s->chunk[p++];
            if (t & 0x80) {
                int c = (t & 0x7F) + 1;
                i += c;                 /* keep these entries */
            } else if (t & 0x40) {
                int c = (t & 0x3F) + 1, src = p < s->chunk_len ? s->chunk[p++] : 0;
                while (c-- && i < 256) {
                    memcpy(s->Palette + i * 3, old + (src & 0xFF) * 3, 3);
                    i++;
                    src++;
                }
            } else {
                if (p + 2 > s->chunk_len)
                    break;
                s->Palette[i * 3] = t & 0x3F;
                s->Palette[i * 3 + 1] = s->chunk[p++] & 0x3F;
                s->Palette[i * 3 + 2] = s->chunk[p++] & 0x3F;
                i++;
            }
        }
        s->NewPalette = 1;
        off = len;
    }
    s->video_off = off;
    return 1;
}

static const int block_runs[64] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
    51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048
};

static void decode_video(Smack *s, const uint8_t *p, uint32_t n)
{
    bits b;
    uint32_t bw = s->Width / 4, bh = s->Height / 4, total = bw * bh, blk = 0, w = s->Width;

    bits_init(&b, p, n);
    big_reset(&s->mmap);
    big_reset(&s->mclr);
    big_reset(&s->full);
    big_reset(&s->type);
    while (blk < total) {
        int t = big_decode(&b, &s->type), run = block_runs[(t >> 2) & 0x3F];
        switch (t & 3) {
        case 0:                         /* two colours and a bit map */
            while (run-- && blk < total) {
                int clr = big_decode(&b, &s->mclr), map = big_decode(&b, &s->mmap), y, x;
                uint8_t lo = (uint8_t)clr, hi = (uint8_t)(clr >> 8);
                uint8_t *o = s->image + (blk / bw) * 4 * w + (blk % bw) * 4;
                for (y = 0; y < 4; y++, o += w)
                    for (x = 0; x < 4; x++, map >>= 1)
                        o[x] = (map & 1) ? hi : lo;
                blk++;
            }
            break;
        case 1:                         /* full block */
            while (run-- && blk < total) {
                uint8_t *o = s->image + (blk / bw) * 4 * w + (blk % bw) * 4;
                int mode = 0, y, v;
                if (s->version4) {
                    if (get_bit(&b))
                        mode = 1;
                    else if (get_bit(&b))
                        mode = 2;
                }
                if (mode == 0) {
                    for (y = 0; y < 4; y++, o += w) {
                        v = big_decode(&b, &s->full);
                        o[2] = (uint8_t)v;
                        o[3] = (uint8_t)(v >> 8);
                        v = big_decode(&b, &s->full);
                        o[0] = (uint8_t)v;
                        o[1] = (uint8_t)(v >> 8);
                    }
                } else if (mode == 1) { /* double: 2x2 pixels */
                    for (y = 0; y < 2; y++, o += 2 * w) {
                        v = big_decode(&b, &s->full);
                        o[0] = o[1] = o[w] = o[w + 1] = (uint8_t)v;
                        o[2] = o[3] = o[w + 2] = o[w + 3] = (uint8_t)(v >> 8);
                    }
                } else {                /* half: rows doubled */
                    for (y = 0; y < 2; y++, o += 2 * w) {
                        int v1 = big_decode(&b, &s->full), v2 = big_decode(&b, &s->full);
                        o[0] = o[w] = (uint8_t)v1;
                        o[1] = o[w + 1] = (uint8_t)(v1 >> 8);
                        o[2] = o[w + 2] = (uint8_t)v2;
                        o[3] = o[w + 3] = (uint8_t)(v2 >> 8);
                    }
                }
                blk++;
            }
            break;
        case 2:                         /* unchanged */
            blk += run;
            break;
        case 3: {                       /* solid colour */
            uint8_t c = (uint8_t)(t >> 8);
            while (run-- && blk < total) {
                uint8_t *o = s->image + (blk / bw) * 4 * w + (blk % bw) * 4;
                int y;
                for (y = 0; y < 4; y++, o += w)
                    memset(o, c, 4);
                blk++;
            }
            break;
        }
        }
    }
}

/* ------------------------------------------------------------------ SVGA helper (RAD's) */
static uint32_t svga_w, svga_h;

/* 0x2FE1C0 SVGADetect(mode): 1 = a card is there */
long smacker_2FE1C0(long a) { (void)a; return 1; }
/* 0x2FDF50 SVGASetup(h, w): picks the mode; 1 if it exists */
long smacker_2FDF50(long h, long w)
{
    svga_w = (uint32_t)w;
    svga_h = (uint32_t)h;
    return 1;
}
long smacker_2FDDA0(void) { return (long)svga_h; }              /* height */
long smacker_2FDDB0(void) { return 0; }                         /* bank switch routine */
long smacker_2FDDC0(void) { return (long)svga_w; }              /* bytes per line */
/* 0x2FDE80 SVGASetGraph: sets the mode */
long smacker_2FDE80(void)
{
    port_video_set_mode((int)svga_w, (int)svga_h, (int)svga_w);
    return 0;
}
/* 0x2FDDD0 SVGAClear(colour) */
long smacker_2FDDD0(long colour)
{
    memset(port_video.vram, (int)colour, (size_t)svga_w * svga_h);
    return 0;
}
/* 0x2FED30 the palette to the DAC (256 x 6-bit RGB) */
long smacker_2FED30(const uint8_t *pal)
{
    memcpy(port_video.dac, pal, 768);
    port_video.pal_serial++;
    return 0;
}
/* 0x2FDD94 wait ms */
long smacker_2FDD94(long ms)
{
    if (ms > 0)
        SDL_Delay((Uint32)ms);
    return 0;
}

/* ------------------------------------------------------------------ Smacker API */
/* 0x2FE610 SmackSoundUseMSS-like set-up (rate / digital driver): sound goes to SDL here */
long smacker_2FE610(long a, long drv) { (void)a; (void)drv; return 1; }

static void smooth_open(Smack *s, const char *host);
static void smooth_ahead(Smack *s);
static void smooth_poll(Smack *s);

/* opens the movie at host path `host` (`name`: the game's name for it, for the log) */
static Smack *smk_open(const char *host, const char *name, int sound)
{
    Smack *s;
    uint8_t h[104];
    uint32_t i, trees;
    uint8_t *tb;
    uint64_t pos;
    bits b;

    s = calloc(1, sizeof *s);
    s->io = SDL_IOFromFile(host, "rb");
    if (!s->io || SDL_ReadIO(s->io, h, 104) != 104 || memcmp(h, "SMK", 3) != 0 || (h[3] != '2' && h[3] != '4')) {
        port_log("Smacker: cannot open %s (%s)\n", name, host);
        if (s->io)
            SDL_CloseIO(s->io);
        free(s);
        return NULL;
    }
#define U32(o) ((uint32_t)h[o] | (uint32_t)h[o + 1] << 8 | (uint32_t)h[o + 2] << 16 | (uint32_t)h[o + 3] << 24)
    s->Version = U32(0);
    s->version4 = h[3] == '4';
    s->Width = U32(4);
    s->Height = U32(8);
    s->Frames = U32(12);
    {
        int32_t r = (int32_t)U32(16);
        s->ms_per_frame = r > 0 ? r : r < 0 ? -r / 100.0 : 100.0;
    }
    s->MSPerFrame = (uint32_t)s->ms_per_frame;
    s->flags = U32(20);
    for (i = 0; i < 7; i++)
        s->LargestInTrack[i] = U32(24 + i * 4);
    trees = U32(52);
    s->tablesize = trees;
    for (i = 0; i < 7; i++)
        s->audio_rate[i] = s->TrackType[i] = U32(72 + i * 4);
#undef U32
    if (s->flags & 1)
        port_log("Smacker: %s has a ring frame (not supported)\n", name);
    s->frame_size = calloc(s->Frames + 1, 4);
    s->frame_type = calloc(s->Frames + 1, 1);
    s->frame_pos = calloc(s->Frames + 1, 8);
    SDL_ReadIO(s->io, s->frame_size, s->Frames * 4);
    SDL_ReadIO(s->io, s->frame_type, s->Frames);
    tb = malloc(trees + 8);
    memset(tb, 0, trees + 8);
    SDL_ReadIO(s->io, tb, trees);
    bits_init(&b, tb, trees);
    read_big(&b, &s->mmap);
    read_big(&b, &s->mclr);
    read_big(&b, &s->full);
    read_big(&b, &s->type);
    free(tb);
    pos = (uint64_t)SDL_TellIO(s->io);
    for (i = 0; i < s->Frames; i++) {
        s->frame_pos[i] = pos;
        pos += s->frame_size[i] & ~3u;
    }
    s->image = calloc(s->Width * s->Height + 16, 1);
    s->volume = 32768;
    if (sound)
        open_audio(s);
    s->FrameNum = 0;
    load_frame(s);
    s->start_us = 0;
    return s;
}

int port_movies_open;                   /* SmackOpen'd and not yet closed: the first start's question
                                         * waits for the menu (options.c) */
/* the movie's place in the game's video memory, for "Movie size: Fill screen" (platform.c) */
uint32_t port_movie_off, port_movie_pitch;
int port_movie_w, port_movie_h;

/* 0x3007A0 SmackOpen(extrabuf, flags, name) */
void *smacker_3007A0(long extrabuf, long flags, const char *name)
{
    char host[1024];
    Smack *s;

    (void)extrabuf;
    port_path(name, host, sizeof host);
    s = smk_open(host, name, flags != 0);
    port_movies_open += s != NULL;
    /* in-between pictures for movies of up to 20 pictures a second */
    if (s && port_smooth_videos && s->ms_per_frame >= 50 && vsmooth_start(0, -1) == 1)
        smooth_open(s, host);
    return s;
}

/* 0x2FF820 SmackToScreen(buf, height, pitch, y, x, smk): where the frames go */
long smacker_2FF820(long bank, long height, long pitch, long y, long x, Smack *s)
{
    (void)bank;
    (void)height;
    s->dest = port_video.vram + y * pitch + x;
    s->dest_pitch = (uint32_t)pitch;
    s->dest_off = (uint32_t)(y * pitch + x);
    port_movie_off = s->dest_off;
    port_movie_pitch = s->dest_pitch;
    port_movie_w = (int)s->Width;
    port_movie_h = (int)s->Height;
    return 0;
}

/* 0x2FEF20 SmackVolumePan(pan, volume, tracks, smk) */
long smacker_2FEF20(long pan, long volume, long tracks, Smack *s)
{
    (void)pan;
    (void)tracks;
    s->volume = (int)volume;
    if (s->audio)
        SDL_SetAudioStreamGain(s->audio, volume / 32768.0f);
    return 0;
}

/* decodes the loaded frame into s->image, playing its sound if `sound` */
static void decode_frame(Smack *s, int sound)
{
    uint32_t off = s->video_off, i;
    uint8_t type = s->frame_type[s->FrameNum];

    for (i = 0; i < 7; i++)
        if (type & (2 << i)) {
            uint32_t len;
            if (off + 4 > s->chunk_len)
                break;
            len = s->chunk[off] | s->chunk[off + 1] << 8 | s->chunk[off + 2] << 16 | (uint32_t)s->chunk[off + 3] << 24;
            if (len < 4 || off + len > s->chunk_len)
                break;
            if (i == 0 && sound)
                play_audio(s, s->chunk + off + 4, len - 4);
            off += len;
        }
    if (off <= s->chunk_len)
        decode_video(s, s->chunk + off, s->chunk_len - off);
}

/* 0x300040 SmackDoFrame: decodes the current frame, plays its sound, shows it */
long smacker_300040(Smack *s)
{
    uint32_t y;

    if (s->start_us == 0)
        s->start_us = port_now_us() - (uint64_t)(s->FrameNum * s->ms_per_frame * 1000);
    decode_frame(s, 1);
    s->shown = s->FrameNum;
    if (s->dest)
        for (y = 0; y < s->Height; y++)
            memcpy(s->dest + y * s->dest_pitch, s->image + y * s->Width, s->Width);
    if (s->vs)
        vsmooth_hide();                 /* the real picture is up: no in-between over it */
    return 0;
}

/* 0x2FFD70 SmackNextFrame */
long smacker_2FFD70(Smack *s)
{
    if (s->FrameNum + 1 < s->Frames) {
        s->FrameNum++;
        load_frame(s);
        if (s->vs)
            smooth_ahead(s);
    }
    return 0;
}

/* 0x2FEDF0 SmackWait: nonzero while it is not yet time for the next frame */
long smacker_2FEDF0(Smack *s)
{
    uint64_t due;
    if (!s->start_us)
        return 0;
    if (s->vs)
        smooth_poll(s);
    due = s->start_us + (uint64_t)((s->FrameNum) * s->ms_per_frame * 1000);
    if (port_now_us() < due) {
        SDL_Delay(1);
        return 1;
    }
    return 0;
}

/* 0x300560 SmackSoundOnOff(on, smk) */
long smacker_300560(long on, Smack *s)
{
    s->audio_on = on != 0;
    if (s->audio && !on)
        SDL_ClearAudioStream(s->audio);
    return 0;
}

static long smk_close(Smack *s);

/* 0x300420 SmackClose */
long smacker_300420(Smack *s)
{
    if (s && port_movies_open > 0)
        port_movies_open--;
    if (!port_movies_open)
        port_movie_w = port_movie_h = 0;
    return smk_close(s);
}

static long smk_close(Smack *s)
{
    if (!s)
        return 0;
    if (s->vs) {
        vsmooth_hide();
        vsmooth_end(s->vs);
    }
    if (s->ahead)
        smk_close(s->ahead);
    free(s->mid_rgb);
    if (s->audio)
        SDL_DestroyAudioStream(s->audio);
    if (s->io)
        SDL_CloseIO(s->io);
    tree_free(&s->mmap.t);
    tree_free(&s->mclr.t);
    tree_free(&s->full.t);
    tree_free(&s->type.t);
    free(s->frame_size);
    free(s->frame_type);
    free(s->frame_pos);
    free(s->chunk);
    free(s->image);
    free(s);
    return 0;
}

/* ------------------------------------------------------------------ Smooth videos */
static void smooth_open(Smack *s, const char *host)
{
    s->ahead = smk_open(host, host, 0);
    s->vs = s->ahead ? vsmooth_begin((int)s->Width, (int)s->Height) : NULL;
    if (!s->vs) {
        if (s->ahead)
            smk_close(s->ahead);
        s->ahead = NULL;
        return;
    }
    s->mid_rgb = malloc((size_t)s->Width * s->Height * 3);
    s->mid_handled = 0;
    smooth_ahead(s);                    /* pictures 0 and 1 */
}

/* the ahead decoder decodes up to the picture after the next one the game shows */
static void smooth_ahead(Smack *s)
{
    Smack *a = s->ahead;
    while (a->FrameNum < a->Frames && a->FrameNum <= s->FrameNum + 1) {
        decode_frame(a, 0);
        vsmooth_push(s->vs, a->FrameNum, a->image, a->Palette);
        a->FrameNum++;
        load_frame(a);
    }
}

static int smooth_wait(void)
{
    static int w = -1;
    if (w < 0)
        w = SDL_getenv("FREEFALL_SMOOTH_WAIT") != NULL;
    return w;
}

/* while the game waits for the next picture: halfway there, the in-between picture is due */
static void smooth_poll(Smack *s)
{
    uint32_t next = s->shown + 1;
    uint64_t now, half;

    if (next >= s->Frames || s->mid_handled >= next)
        return;
    now = port_now_us();
    half = s->start_us + (uint64_t)((s->shown + 0.5) * s->ms_per_frame * 1000);
    if (now < half)
        return;
    /* FREEFALL_SMOOTH_WAIT=1 (tests on a slow card): the movie waits for each in-between picture */
    if (smooth_wait()) {
        int r = vsmooth_mid(s->vs, next, s->mid_rgb, 5000);
        s->start_us += port_now_us() - now;
        if (r == 1 && s->dest)
            vsmooth_show(s->vs, s->mid_rgb, s->dest_off, (int)s->dest_pitch);
        s->mid_handled = next;
        return;
    }
    switch (vsmooth_mid(s->vs, next, s->mid_rgb, 0)) {
    case 1:
        if (s->dest)
            vsmooth_show(s->vs, s->mid_rgb, s->dest_off, (int)s->dest_pitch);
        s->mid_handled = next;
        break;
    case -1:                            /* none for this pair: the picture stays */
        s->mid_handled = next;
        break;
    default:                            /* not ready: no use once a quarter frame late */
        if (now >= half + (uint64_t)(s->ms_per_frame * 250)) {
            vsmooth_count_late(s->vs);
            s->mid_handled = next;
        }
    }
}

/* ---- for "tnfree --smooth-test" (smoothtest.c): the movie at a host path, video only */
void *port_smk_open(const char *host, int *w, int *h, uint32_t *frames, double *ms_per_frame)
{
    Smack *s = smk_open(host, host, 0);
    if (s) {
        s->shown = UINT32_MAX;          /* nothing decoded yet */
        *w = (int)s->Width;
        *h = (int)s->Height;
        *frames = s->Frames;
        *ms_per_frame = s->ms_per_frame;
    }
    return s;
}

/* missions.c (the Mission Selector's movies): the movie at a host path with its sound, at gain
 * 0..1 (the game's own movies: SmackVolumePan from the sound effects volume, movie.c) */
void *port_smk_open_sound(const char *host, float gain, int *w, int *h, uint32_t *frames, double *ms_per_frame)
{
    Smack *s = smk_open(host, host, 1);
    if (s) {
        s->shown = UINT32_MAX;
        s->volume = (int)(gain * 32768);
        if (s->audio)
            SDL_SetAudioStreamGain(s->audio, gain);
        *w = (int)s->Width;
        *h = (int)s->Height;
        *frames = s->Frames;
        *ms_per_frame = s->ms_per_frame;
    }
    return s;
}

/* decodes the next picture: 1 with its pixels and 6-bit palette (valid until the next call:
 * loading a picture applies its palette change), 0 at the end; its sound plays when the movie
 * was opened with it */
int port_smk_next(void *m, const uint8_t **pixels, const uint8_t **pal6)
{
    Smack *s = m;
    if (s->shown != UINT32_MAX) {
        s->FrameNum++;
        load_frame(s);
    }
    if (s->FrameNum >= s->Frames)
        return 0;
    decode_frame(s, s->audio != NULL);
    s->shown = s->FrameNum;
    *pixels = s->image;
    *pal6 = s->Palette;
    return 1;
}

void port_smk_close(void *m)
{
    smk_close(m);
}
