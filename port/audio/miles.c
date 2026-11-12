/* miles.c -- the Miles Sound System (AIL 3.x) for the standalone build: driver installation,
 * the digital sample API with its mixer, and the sound output (SDL3).  The XMIDI sequencer is
 * in xmidi.c, the FM synthesizer in fmsynth.c, General MIDI output in gmout.c, the timer
 * services in miles_timer.c.
 *
 * The sound service runs as a Miles timer (at DIG_SERVICE_RATE, 250 Hz for the game), so at
 * interrupt time like the DOS drivers: it keeps about 60 ms of sound queued for the output, and
 * while mixing it steps the music at exactly 120 ticks per second of sound.  End-of-sample
 * callbacks happen there too.  Mixing follows the Miles mixer: volume (0..127) times the master
 * volume / 127, pan (0..127) through the same left / right table. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miles_int.h"
#include "platform.h"
#include "port.h"

#define MAX_SAMPLES 32
#define QUEUE_FRAMES (MIX_RATE * 60 / 1000)

enum { SMP_FREE = 1, SMP_DONE = 2, SMP_PLAYING = 4, SMP_STOPPED = 8 };
enum { F_MONO8, F_MONO16, F_STEREO8, F_STEREO16 };
#define DIG_PCM_SIGN 1

typedef void (*eos_cb)(void *S);

typedef struct {
    int used, status;
    const uint8_t *start;               /* the buffer playing */
    uint32_t frames;                    /* its length in frames */
    uint32_t pos, frac;                 /* position: frame + 16-bit fraction */
    const uint8_t *file_first;          /* VOC: first sound block, and what follows the current */
    const uint8_t *voc_next;
    const uint8_t *voc_repeat;
    int voc_repeat_count;
    int format, flags;
    long rate, vol, pan, loop_count, loops_done;
    void *eos;
    long user[8];
} Sample;

/* driver records: the game reads the DIG driver's +0x3C (2: stereo) */
static long dig_driver[64];
char miles_mdi_driver[256];
#define DIG_HW_FORMAT dig_driver[0x18 / 4]
#define DIG_CHANNELS dig_driver[0x3C / 4]
#define DIG_MASTER dig_driver[0x68 / 4]

static Sample samples[MAX_SAMPLES];
static long prefs[32];
static int started, dig_installed, mdi_installed;
static long service_timer = -1;
static SDL_AudioStream *stream;

/* miles_timer.c */
long AIL_register_timer(void *cb);
long AIL_set_timer_frequency(long h, long hz);
long AIL_start_timer(long h);
long AIL_release_timer_handle(long h);
static uint64_t clock_start_us, frames_made;
static uint32_t tick_frac;              /* frames to the next music tick, in 1/120 frames */
static char gtl_prefix[256];
static char mdi_name[256];              /* the game's music driver file */
char port_music_mode[16] = "auto";

/* Miles's pan table: gain (of 128) for pan p on the right, 127 - p on the left */
static const uint8_t pan_table[128] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
    32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62,
    64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 94,
    96, 98, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128 };

extern short ail_io_parms[4];

#define LOCK int was_ = port_irq_save()
#define UNLOCK port_irq_restore(was_)

static Sample *smp(void *S)
{
    Sample *s = S;
    return s >= samples && s < samples + MAX_SAMPLES && s->used ? s : NULL;
}

static int frame_bytes(int format)
{
    return format == F_MONO8 ? 1 : format == F_STEREO16 ? 4 : 2;
}

/* ------------------------------------------------------------------ VOC files */
/* moves to the next sound block from p; 0 at the end of the file */
static int voc_block(Sample *s, const uint8_t *p)
{
    for (;;) {
        int type = p[0];
        uint32_t len;
        if (type == 0)
            return 0;
        len = p[1] | p[2] << 8 | p[3] << 16;
        switch (type) {
        case 1:                         /* sound data: time constant, packing, data */
            if (p[5] == 0) {
                s->rate = 1000000L / (256 - p[4]);
                s->format = F_MONO8;
                s->flags = 0;
                s->start = p + 6;
                s->frames = len - 2;
                s->voc_next = p + 4 + len;
                return 1;
            }
            break;
        case 2:                         /* more data, same format */
            s->start = p + 4;
            s->frames = len / frame_bytes(s->format);
            s->voc_next = p + 4 + len;
            return 1;
        case 6:                         /* repeat start */
            s->voc_repeat = p + 4 + len;
            s->voc_repeat_count = p[4] | p[5] << 8;
            break;
        case 7:                         /* repeat end */
            if (s->voc_repeat && s->voc_repeat_count != 0) {
                if (s->voc_repeat_count != 0xFFFF)
                    s->voc_repeat_count--;
                p = s->voc_repeat;
                continue;
            }
            break;
        case 9: {                       /* new format: rate, bits, channels, codec */
            int bits = p[8], ch = p[9], codec = p[10] | p[11] << 8;
            if (codec == 0 || codec == 4) {
                s->rate = p[4] | p[5] << 8 | p[6] << 16 | (long)p[7] << 24;
                s->format = (bits == 16 ? 1 : 0) + (ch == 2 ? 2 : 0);
                s->flags = bits == 16 ? DIG_PCM_SIGN : 0;
                s->start = p + 16;
                s->frames = (len - 12) / frame_bytes(s->format);
                s->voc_next = p + 4 + len;
                return 1;
            }
            break;
        }
        }
        p += 4 + len;
    }
}

static int load_file(Sample *s, const uint8_t *d)
{
    s->file_first = NULL;
    s->voc_next = NULL;
    s->voc_repeat = NULL;
    if (memcmp(d, "Creative Voice File", 19) == 0) {
        const uint8_t *first = d + (d[0x14] | d[0x15] << 8);
        if (!voc_block(s, first))
            return 0;
        s->file_first = first;
        return 1;
    }
    if (memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WAVE", 4) == 0) {
        uint32_t size = d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24;
        const uint8_t *p = d + 12, *end = d + 8 + size;
        int ch = 1, bits = 8, ok = 0;
        while (p + 8 <= end) {
            uint32_t len = p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24;
            if (memcmp(p, "fmt ", 4) == 0) {
                if ((p[8] | p[9] << 8) != 1)
                    return 0;           /* PCM only */
                ch = p[10] | p[11] << 8;
                s->rate = p[12] | p[13] << 8 | p[14] << 16 | (long)p[15] << 24;
                bits = p[22] | p[23] << 8;
                ok = 1;
            } else if (memcmp(p, "data", 4) == 0 && ok) {
                s->format = (bits == 16 ? 1 : 0) + (ch == 2 ? 2 : 0);
                s->flags = bits == 16 ? DIG_PCM_SIGN : 0;
                s->start = p + 8;
                s->frames = len / frame_bytes(s->format);
                return 1;
            }
            p += 8 + ((len + 1) & ~1u);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ mixer */
static void get_frame(const Sample *s, uint32_t i, int *l, int *r)
{
    const uint8_t *p = s->start + i * frame_bytes(s->format);
    int sign = s->flags & DIG_PCM_SIGN;
    switch (s->format) {
    case F_MONO8:
        *l = *r = sign ? (int8_t)p[0] << 8 : (p[0] - 128) << 8;
        break;
    case F_STEREO8:
        *l = sign ? (int8_t)p[0] << 8 : (p[0] - 128) << 8;
        *r = sign ? (int8_t)p[1] << 8 : (p[1] - 128) << 8;
        break;
    case F_MONO16:
        *l = *r = (int16_t)(p[0] | p[1] << 8);
        break;
    default:
        *l = (int16_t)(p[0] | p[1] << 8);
        *r = (int16_t)(p[2] | p[3] << 8);
        break;
    }
}

/* the buffer ran out: next VOC block, next loop, or the end (1 = keep playing) */
static int buffer_end(Sample *s)
{
    s->pos = 0;
    s->frac = 0;
    if (s->voc_next && voc_block(s, s->voc_next))
        return 1;
    s->loops_done++;
    if (s->loop_count == 0 || s->loops_done < s->loop_count) {
        if (s->file_first)
            voc_block(s, s->file_first);
        return 1;
    }
    s->status = SMP_DONE;
    if (s->eos)
        ((eos_cb)s->eos)(s);
    return 0;
}

static void mix_sample(Sample *s, int32_t *mix, int frames)
{
    int v = (int)(s->vol * DIG_MASTER / 127);
    int gl, gr, i;
    uint32_t step;

    if (v < 0)
        v = 0;
    if (v > 127)
        v = 127;
    if (v)
        v++;
    gl = pan_table[127 - (s->pan & 127)] * v;     /* of 128 * 128 */
    gr = pan_table[s->pan & 127] * v;
    for (i = 0; i < frames; i++) {
        int l0, r0, l1, r1, l, r;
        if (s->status != SMP_PLAYING)
            return;
        if (s->pos >= s->frames) {
            Sample *self = s;
            if (!buffer_end(s) || self->status != SMP_PLAYING || !s->frames)
                return;
        }
        step = (uint32_t)(((uint64_t)(s->rate > 0 ? s->rate : 11025) << 16) / MIX_RATE);
        get_frame(s, s->pos, &l0, &r0);
        if (s->pos + 1 < s->frames)
            get_frame(s, s->pos + 1, &l1, &r1);
        else {
            l1 = l0;
            r1 = r0;
        }
        l = l0 + (int)(((int64_t)(l1 - l0) * s->frac) >> 16);
        r = r0 + (int)(((int64_t)(r1 - r0) * s->frac) >> 16);
        mix[2 * i] += (l * gl) >> 14;
        mix[2 * i + 1] += (r * gr) >> 14;
        s->frac += step;
        s->pos += s->frac >> 16;
        s->frac &= 0xFFFF;
    }
}

/* the Mission Selector's way to the outfitting (missions.c): the game's sound goes silent while
 * its screens are passed under black (the game goes on as usual underneath) */
volatile int port_audio_mute;
static int mute_gain = 4096;            /* 4096 = full */
#define MUTE_STEP (4096 / (MIX_RATE * 15 / 100) + 1)

static void render(int16_t *outbuf, int frames)
{
    static int32_t mix[2 * 8192];
    int done = 0, i, k;

    memset(mix, 0, sizeof(int32_t) * 2 * frames);
    while (done < frames) {
        uint32_t to_tick = (tick_frac + XMI_RATE - 1) / XMI_RATE;
        int n = frames - done;
        if (to_tick == 0) {
            if (mdi_installed)
                xmi_tick();
            tick_frac += MIX_RATE;
            continue;
        }
        if ((uint32_t)n > to_tick)
            n = (int)to_tick;
        for (k = 0; k < MAX_SAMPLES; k++)
            if (samples[k].used && samples[k].status == SMP_PLAYING)
                mix_sample(&samples[k], mix + 2 * done, n);
        if (xmi_output == MUSIC_FM)
            fm_render(mix + 2 * done, n);
        else if (xmi_output == MUSIC_SF)
            sf_render(mix + 2 * done, n);
        if (mdi_installed)
            xmi_render_packs(mix + 2 * done, n);
        done += n;
        tick_frac = tick_frac > (uint32_t)n * XMI_RATE ? tick_frac - (uint32_t)n * XMI_RATE : 0;
    }
    for (i = 0; i < 2 * frames; i++) {
        int32_t v = mix[i];
        if (mute_gain < 4096) {
            v = (int32_t)((int64_t)v * mute_gain >> 12);
        }
        if (i & 1) {                    /* port_audio_mute: to silence (and back) in 0.15 s */
            if (port_audio_mute && mute_gain > 0)
                mute_gain -= mute_gain < MUTE_STEP ? mute_gain : MUTE_STEP;
            else if (!port_audio_mute && mute_gain < 4096)
                mute_gain += 4096 - mute_gain < MUTE_STEP ? 4096 - mute_gain : MUTE_STEP;
        }
        outbuf[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

/* the sound service (a Miles timer: interrupt time) */
static void service(long user)
{
    static int16_t buf[2 * 8192];
    long want;

    (void)user;
    if (stream)
        want = QUEUE_FRAMES - SDL_GetAudioStreamQueued(stream) / 4;
    else
        want = (long)((port_now_us() - clock_start_us) * MIX_RATE / 1000000 - frames_made);
    if (want <= 0)
        return;
    if (want > 8192)
        want = 8192;
    render(buf, (int)want);
    frames_made += want;
    {                                   /* FREEFALL_SOUND_DUMP=file: the mix as raw 44.1 kHz s16 stereo */
        static FILE *dump;
        static int tried;
        if (!tried) {
            const char *p = getenv("FREEFALL_SOUND_DUMP");
            tried = 1;
            dump = p && p[0] ? fopen(p, "wb") : NULL;
        }
        if (dump) {
            fwrite(buf, 4, (size_t)want, dump);
            fflush(dump);
        }
    }
    if (stream)
        SDL_PutAudioStreamData(stream, buf, (int)want * 4);
}

static void output_open(void)
{
    SDL_AudioSpec spec;

    if (service_timer != -1)
        return;
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = 2;
    spec.freq = MIX_RATE;
    if ((SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO)) &&
        (stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL)) != NULL)
        SDL_ResumeAudioStreamDevice(stream);
    else
        port_log("Sound: no audio output (%s); playing silently\n", SDL_GetError());
    clock_start_us = port_now_us();
    frames_made = 0;
    tick_frac = 0;
    service_timer = AIL_register_timer((void *)service);
    AIL_set_timer_frequency(service_timer, prefs[0] > 0 ? prefs[0] : 200);
    AIL_start_timer(service_timer);
}

static void output_close(void)
{
    if (service_timer != -1) {
        AIL_release_timer_handle(service_timer);
        service_timer = -1;
    }
    if (stream) {
        SDL_DestroyAudioStream(stream);
        stream = NULL;
    }
}

/* ------------------------------------------------------------------ startup, drivers */
long AIL_startup(void)
{
    LOCK;
    if (!started) {
        memset(prefs, 0, sizeof prefs);
        prefs[0] = 200;                 /* DIG_SERVICE_RATE */
        prefs[1] = 11025;               /* DIG_HARDWARE_SAMPLE_RATE */
        prefs[3] = 100;                 /* DIG_LATENCY */
        prefs[4] = 16;                  /* DIG_MIXER_CHANNELS */
        prefs[5] = 100;                 /* DIG_DEFAULT_VOLUME */
        prefs[11] = 120;                /* MDI_SERVICE_RATE */
        prefs[12] = 8;                  /* MDI_SEQUENCES */
        prefs[13] = 127;                /* MDI_DEFAULT_VOLUME */
        started = 1;
    }
    UNLOCK;
    return 1;
}

void AIL_uninstall_DIG_driver(void *dig);
void AIL_uninstall_MDI_driver(void *mdi);

void AIL_shutdown(void)
{
    if (!started)
        return;
    if (mdi_installed)
        AIL_uninstall_MDI_driver(miles_mdi_driver);
    if (dig_installed)
        AIL_uninstall_DIG_driver(dig_driver);
    output_close();
    started = 0;
}

void port_audio_init(void)
{
}

void port_audio_shutdown(void)
{
    AIL_shutdown();
}

long AIL_set_preference(unsigned long number, long value)
{
    long old = 0;
    if (number < 32) {
        old = prefs[number];
        prefs[number] = value;
    }
    return old;
}

void AIL_set_GTL_filename_prefix(char *prefix)
{
    snprintf(gtl_prefix, sizeof gtl_prefix, "%s", prefix ? prefix : "");
}

void *miles_find_file(const char *dos_name, char *out, int size)
{
    FILE *f;
    port_path(dos_name, out, size);
    f = fopen(out, "rb");
    if (!f)
        return NULL;
    fclose(f);
    return out;
}

static void base_name(const char *path, char *out, int size)
{
    const char *b = path;
    int i;
    for (; *path; path++)
        if (*path == '\\' || *path == '/' || *path == ':')
            b = path + 1;
    for (i = 0; b[i] && i < size - 1; i++)
        out[i] = (char)SDL_tolower((unsigned char)b[i]);
    out[i] = 0;
}

static void set_io(short *io)
{
    int k;
    if (io)
        for (k = 0; k < 4; k++)
            ail_io_parms[k] = io[k];
}

/* Miles loads the driver file: without it there is no driver (the game then runs silent) */
static int driver_file_exists(const char *name)
{
    char host[1100];
    return name && *name && miles_find_file(name, host, sizeof host) != NULL;
}

void *AIL_install_DIG_driver_file(char *name, void *io)
{
    if (!driver_file_exists(name))
        return NULL;
    LOCK;
    if (!dig_installed) {
        memset(dig_driver, 0, sizeof dig_driver);
        DIG_HW_FORMAT = F_STEREO16;
        DIG_CHANNELS = 2;
        DIG_MASTER = 127;
        dig_installed = 1;
        set_io(io);
        output_open();
        port_log("Sound: digital driver %s\n", name ? name : "?");
    }
    UNLOCK;
    return dig_driver;
}

void AIL_uninstall_DIG_driver(void *dig)
{
    int i;
    LOCK;
    (void)dig;
    for (i = 0; i < MAX_SAMPLES; i++)
        samples[i].used = 0;
    dig_installed = 0;
    if (!mdi_installed)
        output_close();
    UNLOCK;
}

/* the game's bank or timbre file of that extension next to the GTL prefix (e.g. ff.opl), else in
 * the sound folder: its host path in host, or NULL */
static const char *prefix_file(const char *ext, char *host, int size)
{
    char dos[300], b[64];
    snprintf(dos, sizeof dos, "%s%s", gtl_prefix, ext);
    if (miles_find_file(dos, host, size))
        return host;
    base_name(gtl_prefix, b, sizeof b);
    snprintf(dos, sizeof dos, "sound\\%s%s", b, ext);
    return miles_find_file(dos, host, size);
}

/* which music the driver file stands for: OPL2 FM, OPL3 FM or General MIDI; or the SoundFont
 * music (an AWE32's: the game's bank over a SoundFont) */
static int open_music(const char *drv)
{
    static const char *const gm_drivers[] = { "mpu401.mdi", "mt32mpu.mdi", "sbawe32.mdi",
                                              "ultra.mdi", "sndscape.mdi", "umid.mdi", NULL };
    static const char *const opl3_drivers[] = { "sbpro2.mdi", "opl3.mdi", "esfm.mdi", NULL };
    char base[64], host[1100];
    int k, gm = 0, opl3 = 0;
    const char *mode = port_music_mode;

    int is_auto = strcmp(mode, "auto") == 0;

    base_name(drv, base, sizeof base);
    for (k = 0; gm_drivers[k]; k++)
        gm |= strcmp(base, gm_drivers[k]) == 0;
    for (k = 0; opl3_drivers[k]; k++)
        opl3 |= strcmp(base, opl3_drivers[k]) == 0;
    if (strcmp(mode, "off") == 0 || (is_auto && strcmp(base, "null.mdi") == 0)) {
        if (is_auto)                    /* "auto": the options' Music line shows what it chose */
            snprintf(port_music_mode, sizeof port_music_mode, "off");
        return MUSIC_NONE;
    }
    if (strcmp(mode, "soundfont") == 0) {
        char sbk[1100];
        if (sf_open(prefix_file(".sbk", sbk, sizeof sbk)) == 0)
            return MUSIC_SF;
        opl3 = 1;                       /* no SoundFont: FM, as General MIDI without a synthesizer */
    }
    if (strcmp(mode, "gm") == 0 || (is_auto && gm)) {
        if (gm_open() == 0) {
            if (is_auto)
                snprintf(port_music_mode, sizeof port_music_mode, "gm");
            return MUSIC_GM;
        }
        opl3 = 1;
    }
    if (strcmp(mode, "fm") == 0 || strcmp(mode, "opl3") == 0)
        opl3 = 1;
    else if (strcmp(mode, "opl2") == 0)
        opl3 = 0;
    /* the timbre file next to the GTL prefix (e.g. ff.opl), else in the sound folder */
    if (prefix_file(opl3 ? ".opl" : ".ad", host, sizeof host) && fm_open(host, opl3) == 0) {
        if (is_auto || strcmp(mode, "fm") == 0)
            snprintf(port_music_mode, sizeof port_music_mode, "%s", opl3 ? "opl3" : "opl2");
        return MUSIC_FM;
    }
    port_log("FM music: no timbre file for prefix \"%s\"\n", gtl_prefix);
    return MUSIC_NONE;
}

static void close_music(int o)
{
    if (o == MUSIC_FM)
        fm_close();
    else if (o == MUSIC_GM)
        gm_close();
    else if (o == MUSIC_SF)
        sf_close();
}

static const char *music_name(int o)
{
    return o == MUSIC_FM ? "FM (OPL emulator)" : o == MUSIC_GM ? "General MIDI" :
           o == MUSIC_SF ? "SoundFont (FluidSynth)" : "nothing";
}

void *AIL_install_MDI_driver_file(char *name, void *io)
{
    if (!driver_file_exists(name))
        return NULL;
    LOCK;
    if (!mdi_installed) {
        memset(miles_mdi_driver, 0, sizeof miles_mdi_driver);
        snprintf(mdi_name, sizeof mdi_name, "%s", name ? name : "");
        xmi_output = open_music(mdi_name);
        mdi_installed = 1;
        set_io(io);
        output_open();
        port_log("Music: driver %s, playing %s\n", name ? name : "?", music_name(xmi_output));
    }
    UNLOCK;
    return miles_mdi_driver;
}

/* the options' Music line: the music moves to port_music_mode at once and the pieces playing
 * carry on.  Nothing plays while the outputs change, so the opening (the system's MIDI device can
 * take a moment) is outside the interrupt lock; then the new output hears every channel's state
 * and the notes held (xmidi.c xmi_restore). */
void port_music_reopen(void)
{
    int old, now, taken = 0;
    {
        LOCK;
        old = mdi_installed ? xmi_output : -1;
        if (old >= 0)
            xmi_output = MUSIC_NONE;
        UNLOCK;
    }
    if (old < 0)
        return;                         /* no music driver yet: it opens as chosen */
    close_music(old);
    now = open_music(mdi_name);
    {
        LOCK;
        if (mdi_installed) {
            xmi_output = now;
            xmi_restore();
            taken = 1;
        }
        UNLOCK;
    }
    if (!taken) {                       /* the driver went meanwhile (the game quit) */
        close_music(now);
        return;
    }
    port_log("Music: now playing %s\n", music_name(now));
}

void AIL_uninstall_MDI_driver(void *mdi)
{
    LOCK;
    (void)mdi;
    xmi_all_off();
    close_music(xmi_output);
    xmi_output = MUSIC_NONE;
    mdi_installed = 0;
    if (!dig_installed)
        output_close();
    UNLOCK;
}

/* AWE32 sound banks: the SoundFont music loads the game's bank itself (open_music, sfsynth.c) */
long sbk_load(void *mdi, char *name)
{
    (void)mdi;
    (void)name;
    return 0;
}

long sbk_unload(void *mdi)
{
    (void)mdi;
    return 0;
}

void AIL_set_digital_master_volume(void *dig, long v)
{
    (void)dig;
    DIG_MASTER = v < 0 ? 0 : v > 127 ? 127 : v;
}

long AIL_digital_master_volume(void *dig)
{
    (void)dig;
    return DIG_MASTER;
}

/* ------------------------------------------------------------------ samples */
void AIL_init_sample(void *S);

void *AIL_allocate_sample_handle(void *dig)
{
    int i, n = prefs[4] > 0 && prefs[4] < MAX_SAMPLES ? (int)prefs[4] : MAX_SAMPLES;
    Sample *r = NULL;
    LOCK;
    (void)dig;
    for (i = 0; i < n; i++)
        if (!samples[i].used) {
            r = &samples[i];
            memset(r, 0, sizeof *r);
            r->used = 1;
            AIL_init_sample(r);
            break;
        }
    UNLOCK;
    return r;
}

void AIL_release_sample_handle(void *S)
{
    Sample *s;
    LOCK;
    if ((s = smp(S)) != NULL) {
        s->used = 0;
        s->status = SMP_FREE;
    }
    UNLOCK;
}

void AIL_init_sample(void *S)
{
    Sample *s;
    LOCK;
    if ((s = smp(S)) != NULL) {
        s->status = SMP_DONE;
        s->start = NULL;
        s->frames = s->pos = s->frac = 0;
        s->file_first = s->voc_next = s->voc_repeat = NULL;
        s->loop_count = 1;
        s->loops_done = 0;
        s->format = F_MONO8;
        s->flags = 0;
        s->vol = prefs[5];
        s->rate = 11025;
        s->pan = DIG_HW_FORMAT >= F_STEREO8 ? 64 : 0;
        s->eos = NULL;
    }
    UNLOCK;
}

long AIL_set_sample_file(void *S, void *image, long block)
{
    Sample *s;
    long r = 0;
    LOCK;
    (void)block;
    if ((s = smp(S)) != NULL && image) {
        r = load_file(s, image);
        s->pos = s->frac = 0;
        if (!r) {
            static int told;
            const uint8_t *d = image;
            s->start = NULL;
            if (!told++)
                port_log("Sound: a sample file of unknown type (%02X %02X %02X %02X)\n", d[0], d[1], d[2], d[3]);
        }
    }
    UNLOCK;
    return r;
}

void AIL_set_sample_address(void *S, void *start, unsigned long len)
{
    Sample *s;
    LOCK;
    if ((s = smp(S)) != NULL) {
        s->start = start;
        s->frames = (uint32_t)(len / frame_bytes(s->format));
        s->pos = s->frac = 0;
        s->file_first = s->voc_next = NULL;
    }
    UNLOCK;
}

void AIL_set_sample_type(void *S, long format, unsigned long flags)
{
    Sample *s;
    LOCK;
    if ((s = smp(S)) != NULL) {
        uint32_t bytes = s->frames * frame_bytes(s->format);
        s->format = (int)(format & 3);
        s->flags = (int)flags;
        s->frames = bytes / frame_bytes(s->format);
    }
    UNLOCK;
}

void AIL_start_sample(void *S)
{
    Sample *s;
    LOCK;
    if ((s = smp(S)) != NULL && s->start) {
        if (s->file_first)
            voc_block(s, s->file_first);
        s->pos = s->frac = 0;
        s->loops_done = 0;
        s->status = SMP_PLAYING;
        if (getenv("FREEFALL_TRACE_SOUND"))
            port_log("Sound: sample %d starts: %u frames, format %d, %ld Hz, volume %ld, pan %ld, loops %ld\n",
                     (int)(s - samples), s->frames, s->format, s->rate, s->vol, s->pan, s->loop_count);
    }
    UNLOCK;
}

void AIL_stop_sample(void *S)
{
    Sample *s;
    LOCK;
    if ((s = smp(S)) != NULL && s->status == SMP_PLAYING)
        s->status = SMP_STOPPED;
    UNLOCK;
}

void AIL_resume_sample(void *S)
{
    Sample *s;
    LOCK;
    if ((s = smp(S)) != NULL && s->status == SMP_STOPPED)
        s->status = SMP_PLAYING;
    UNLOCK;
}

/* ends the sample now; the end-of-sample callback runs as at its natural end */
void AIL_end_sample(void *S)
{
    Sample *s;
    LOCK;
    if ((s = smp(S)) != NULL && s->status != SMP_FREE && s->status != SMP_DONE) {
        s->status = SMP_DONE;
        if (s->eos)
            ((eos_cb)s->eos)(s);
    }
    UNLOCK;
}

void AIL_set_sample_playback_rate(void *S, long rate)
{
    Sample *s = smp(S);
    if (s)
        s->rate = rate;
}

void AIL_set_sample_volume(void *S, long vol)
{
    Sample *s = smp(S);
    if (s)
        s->vol = vol < 0 ? 0 : vol > 127 ? 127 : vol;
}

void AIL_set_sample_pan(void *S, long pan)
{
    Sample *s = smp(S);
    if (s)
        s->pan = pan < 0 ? 0 : pan > 127 ? 127 : pan;
}

void AIL_set_sample_loop_count(void *S, long n)
{
    Sample *s = smp(S);
    if (s)
        s->loop_count = n;
}

void AIL_set_sample_position(void *S, unsigned long pos)
{
    Sample *s;
    LOCK;
    if ((s = smp(S)) != NULL) {
        s->pos = (uint32_t)(pos / frame_bytes(s->format));
        s->frac = 0;
    }
    UNLOCK;
}

unsigned long AIL_sample_status(void *S)
{
    Sample *s = smp(S);
    return s ? (unsigned long)s->status : 0;
}

long AIL_sample_playback_rate(void *S) { Sample *s = smp(S); return s ? s->rate : 0; }
long AIL_sample_volume(void *S) { Sample *s = smp(S); return s ? s->vol : 0; }
long AIL_sample_pan(void *S) { Sample *s = smp(S); return s ? s->pan : 0; }
long AIL_sample_loop_count(void *S) { Sample *s = smp(S); return s ? s->loop_count : 0; }
unsigned long AIL_sample_position(void *S)
{
    Sample *s = smp(S);
    return s ? s->pos * frame_bytes(s->format) : 0;
}

void *AIL_register_EOS_callback(void *S, void *cb)
{
    Sample *s;
    void *old = NULL;
    LOCK;
    if ((s = smp(S)) != NULL) {
        old = s->eos;
        s->eos = cb;
    }
    UNLOCK;
    return old;
}

void AIL_set_sample_user_data(void *S, unsigned long index, long v)
{
    Sample *s = smp(S);
    if (s && index < 8)
        s->user[index] = v;
}

long AIL_sample_user_data(void *S, unsigned long index)
{
    Sample *s = smp(S);
    return s && index < 8 ? s->user[index] : 0;
}

long AIL_active_sample_count(void *dig)
{
    int i;
    long n = 0;
    (void)dig;
    for (i = 0; i < MAX_SAMPLES; i++)
        if (samples[i].used && samples[i].status == SMP_PLAYING)
            n++;
    return n;
}
