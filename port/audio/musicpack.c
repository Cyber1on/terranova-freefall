/* musicpack.c -- music packs (Terra Nova: Freefall quality of life, off by default).
 *
 * A pack is a folder in Music/ next to tnfree.exe (with --user, in that folder), or a zip file
 * there (Music/MyPack.zip, read as it is through modzip.c: the files at the zip's top or in one
 * folder in it; a folder of the same name comes first), holding OGG or
 * MP3 files named after the game's music table entries (track07.ogg, track08.mp3, ...) or after
 * the composer's MIDI soundtrack files (Nh1drop.mp3, Main.ogg, ...; the list is in
 * docs/modding/MUSIC-PACKS.md).  Names are matched without regard to case, spaces, '-' or '_'.
 * tnfree.ini music_pack=<folder> (or the options screen) chooses the pack; empty is off.  A
 * new choice applies at once: the pieces playing switch, at the point they have reached.
 *
 * The game's own music logic is not changed.  Each piece it starts is still an XMIDI sequence
 * run by xmidi.c, so its beats, triggers and end come at the same moments and the game moves
 * between pieces exactly as before; when the pack has a file for the piece, the sequence's
 * MIDI output is muted and the file plays instead, from the start when the piece starts, at the
 * piece's volume (with its fades), paused and resumed with it, and cut when it ends.  A file
 * loops only where the original piece loops forever.  A piece without a file plays as before.
 *
 * Decoding happens in the sound service (miles.c render), from the whole compressed file kept
 * in memory, with linear resampling to the output rate.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "miles_int.h"
#include "port.h"
#include "sharp3d.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis/stb_vorbis.c"
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_libs/dr_mp3.h"

char port_music_pack[128];              /* the pack's name (its folder, or its zip without .zip), "" = off */
static char scanned[128];               /* the pack whose files are in names[] */
static char (*names)[256];              /* its files (in a zip: with the folder they are in) */
static int nnames;
static char pack_path[1300];            /* the pack's folder or zip */
static int pack_zip;                    /* it is a zip */
#define ZIP_FILES 1024                  /* files looked at in a pack's zip */
char port_music_dir[1024];              /* the Music folder */

#define CHUNK 2048                      /* frames decoded at a time */

struct MpTrack {
    void *file;                         /* the compressed file */
    stb_vorbis *ogg;
    drmp3 mp3;
    int is_mp3, channels, rate, loop, ended;
    int16_t buf[2 * (CHUNK + 1)];       /* decoded frames (stereo); buf[0] carries the last one */
    int n, pos;                         /* frames in buf, the current one */
    uint32_t frac, step;                /* position between pos and pos + 1, step (16.16) */
};

static int decode(MpTrack *t, int16_t *out, int frames)
{
    if (t->is_mp3) {
        int16_t tmp[2 * CHUNK];
        int got, i;
        if (t->channels == 2)
            return (int)drmp3_read_pcm_frames_s16(&t->mp3, (drmp3_uint64)frames, out);
        got = (int)drmp3_read_pcm_frames_s16(&t->mp3, (drmp3_uint64)(frames > CHUNK ? CHUNK : frames), tmp);
        for (i = 0; i < got; i++)       /* mono (anything else is refused at open) */
            out[2 * i] = out[2 * i + 1] = tmp[i];
        return got;
    }
    return stb_vorbis_get_samples_short_interleaved(t->ogg, 2, out, frames * 2);
}

static void seek_start(MpTrack *t)
{
    if (t->is_mp3)
        drmp3_seek_to_pcm_frame(&t->mp3, 0);
    else
        stb_vorbis_seek_start(t->ogg);
}

/* the next chunk after the frame at pos; 0 at the end of a file that does not loop */
static int refill(MpTrack *t)
{
    int got, tries, skip = 0;

    if (t->n > 0 && t->pos < t->n) {    /* keep the current frame for the interpolation */
        t->buf[0] = t->buf[2 * t->pos];
        t->buf[1] = t->buf[2 * t->pos + 1];
        t->n = 1;
    } else {
        /* P65: a file above 44100 Hz steps more than a frame at a time and can step past the
         * last one: the current frame is then in the next chunk */
        if (t->n > 0)
            skip = t->pos - t->n;
        t->n = 0;
    }
    t->pos = skip;
    for (tries = 0; tries < 2; tries++) {
        got = decode(t, t->buf + 2 * t->n, CHUNK);
        if (got > 0) {
            t->n += got;
            return 1;
        }
        if (!t->loop)
            break;
        seek_start(t);
    }
    t->ended = 1;
    return 0;
}

static MpTrack *open_file(const char *path)
{
    size_t len;
    void *data = port_mod_load(path, &len);     /* from the disk or the pack's zip */
    MpTrack *t;
    int err = 0;

    if (!data)
        return NULL;
    t = SDL_calloc(1, sizeof *t);
    t->file = data;
    if ((t->ogg = stb_vorbis_open_memory(data, (int)len, &err, NULL)) != NULL) {
        stb_vorbis_info i = stb_vorbis_get_info(t->ogg);
        t->channels = i.channels;
        t->rate = (int)i.sample_rate;
    } else if (drmp3_init_memory(&t->mp3, data, len, NULL)) {
        t->is_mp3 = 1;
        t->channels = (int)t->mp3.channels;
        t->rate = (int)t->mp3.sampleRate;
        if (t->channels != 1 && t->channels != 2) {
            port_log("Music pack: %s has %d channels (1 or 2 only)\n", path, t->channels);
            drmp3_uninit(&t->mp3);
            SDL_free(data);
            SDL_free(t);
            return NULL;
        }
    } else {
        port_log("Music pack: cannot decode %s\n", path);
        SDL_free(data);
        SDL_free(t);
        return NULL;
    }
    if (t->rate < 4000 || t->rate > 192000)
        t->rate = MIX_RATE;
    t->step = (uint32_t)(((uint64_t)t->rate << 16) / MIX_RATE);
    return t;
}

/* the composer's MIDI soundtrack names of the music table entries (as the database viewer) */
static const char *const set_prefix[5] = { "NH", "Tha", "Sar", "Kar", "Sim" };
static const char *const set_piece[8] = { "1drop", "2explor", "3peril", "4battle",
    "6peril-explor trans", "5battle-peril trans", "7vict", "8fail" };
/* the NH set's soundtrack files are numbered differently (NH1explor, Nh2peril) */
static const char *const nh_piece[8] = { "Nh1drop", "NH1explor", "Nh2peril", "NH4battle",
    "NH6peril-explor trans", "NH5battle-peril trans", "Nh7vict", "Nh8fail" };

static int soundtrack_name(int entry, int num, char *out, int size)
{
    static const char *const shell[7] = { NULL, NULL, "Main", NULL, "Base", "Load1intro", "Load2loop" };
    if (entry >= 1 && entry <= 6) {
        if (!shell[entry])
            return 0;
        snprintf(out, size, "%s", shell[entry]);
    } else if (entry >= 7 && entry <= 46) {
        if (entry == 8 && num == 8)     /* the night missions' Explore piece */
            snprintf(out, size, "NH1night");
        else if (entry < 15)
            snprintf(out, size, "%s", nh_piece[entry - 7]);
        else
            snprintf(out, size, "%s%s", set_prefix[(entry - 7) / 8], set_piece[(entry - 7) % 8]);
    } else
        return 0;
    return 1;
}

/* lower case letters and digits only */
static void squash(const char *s, char *out, int size)
{
    int n = 0;
    for (; *s && n < size - 1; s++)
        if ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z'))
            out[n++] = *s;
        else if (*s >= 'A' && *s <= 'Z')
            out[n++] = (char)(*s - 'A' + 'a');
    out[n] = 0;
}

/* the file name's extension if it is .ogg or .mp3 (the name without it into base) */
static int audio_file(const char *name, char *base, int size)
{
    const char *dot, *sl = pack_zip ? strrchr(name, '/') : NULL;
    int n;
    if (sl)                             /* in a zip: the name without its folder */
        name = sl + 1;
    dot = strrchr(name, '.');
    if (!dot || (SDL_strcasecmp(dot, ".ogg") && SDL_strcasecmp(dot, ".mp3")))
        return 0;
    n = (int)(dot - name);
    if (n >= size)
        n = size - 1;
    memcpy(base, name, n);
    base[n] = 0;
    return 1;
}

static void scan_pack(void)
{
    char dir[1200], **list;
    int count = 0, i;

    if (!strcmp(scanned, port_music_pack))
        return;
    snprintf(scanned, sizeof scanned, "%s", port_music_pack);
    SDL_free(names);
    names = NULL;
    nnames = 0;
    if (!port_music_pack[0])
        return;
    snprintf(dir, sizeof dir, "%s/%s", port_music_dir, port_music_pack);
    {
        SDL_PathInfo info;
        pack_zip = !SDL_GetPathInfo(dir, &info) || info.type != SDL_PATHTYPE_DIRECTORY;
    }
    if (pack_zip) {                     /* Music/<pack>.zip: its files at the top or in one folder */
        int n;
        snprintf(pack_path, sizeof pack_path, "%s.zip", dir);
        names = SDL_calloc(ZIP_FILES, sizeof *names);
        n = port_zip_files(pack_path, names, ZIP_FILES);
        for (i = 0; i < n; i++) {
            char base[256];
            const char *sl = strchr(names[i], '/');
            if (audio_file(names[i], base, sizeof base) && (!sl || !strchr(sl + 1, '/')))
                memmove(names[nnames++], names[i], 256);
        }
        if (n < 0)
            port_log("Music pack %s: no folder or zip of that name in %s\n", port_music_pack, port_music_dir);
    } else {
        snprintf(pack_path, sizeof pack_path, "%s", dir);
        list = SDL_GlobDirectory(dir, NULL, 0, &count);
        names = SDL_calloc(count > 0 ? count : 1, sizeof *names);
        for (i = 0; list && i < count; i++) {
            char base[256];
            if (audio_file(list[i], base, sizeof base))
                snprintf(names[nnames++], 256, "%s", list[i]);
        }
        SDL_free(list);
    }
    {                                   /* which tracks the pack has (tnfree.log) */
        int e, found = 0, k;
        char used[512] = "";
        for (k = 0; k < nnames; k++) {
            char base[256], a[256], b[256], t[64];
            int hit = 0;
            audio_file(names[k], base, sizeof base);
            squash(base, a, sizeof a);
            for (e = 1; e <= 46 && !hit; e++) {
                snprintf(t, sizeof t, "track%02d", e);
                squash(t, b, sizeof b);
                hit = !strcmp(a, b);
                if (!hit && soundtrack_name(e, e == 8 ? 1 : 0, t, sizeof t)) {
                    squash(t, b, sizeof b);
                    hit = !strcmp(a, b);
                }
                if (!hit && e >= 7 && e < 15) {
                    snprintf(t, sizeof t, "NH%s", set_piece[e - 7]);
                    squash(t, b, sizeof b);
                    hit = !strcmp(a, b);
                }
            }
            if (!hit && !strcmp(a, "nh1night"))
                hit = 1;
            if (hit)
                found++;
            else if (strlen(used) + strlen(names[k]) + 3 < sizeof used)
                snprintf(used + strlen(used), sizeof used - strlen(used), "%s%s", used[0] ? ", " : "", names[k]);
        }
        port_log("Music pack %s%s: %d music files%s%s\n", port_music_pack, pack_zip ? " (zip)" : "", found,
                 used[0] ? "; names not known (see docs/modding/MUSIC-PACKS.md): " : "", used);
    }
}

/* the pack's file for music table entry `entry` (sequence num), or NULL (no pack, no file) */
MpTrack *mp_open(int entry, int num, int loop)
{
    char want[3][64], path[1400], a[256], b[256];
    MpTrack *t = NULL;
    int k, w, nw = 1;

    if (!port_music_pack[0] || !port_music_dir[0])
        return NULL;
    scan_pack();
    snprintf(want[0], sizeof want[0], "track%02d", entry);
    if (soundtrack_name(entry, num, want[1], sizeof want[1]))
        nw = 2;
    if (entry >= 7 && entry < 15 && !(entry == 8 && num == 8))    /* NH named like the other sets */
        snprintf(want[nw++], sizeof want[0], "NH%s", set_piece[entry - 7]);
    for (w = 0; w < nw && !t; w++) {
        squash(want[w], b, sizeof b);
        for (k = 0; k < nnames && !t; k++) {
            char base[256];
            audio_file(names[k], base, sizeof base);
            squash(base, a, sizeof a);
            if (!strcmp(a, b)) {
                snprintf(path, sizeof path, "%s/%s", pack_path, names[k]);
                t = open_file(path);
            }
        }
    }
    if (!t)
        return NULL;
    t->loop = loop;
    if (getenv("FREEFALL_TRACE_SOUND"))
        port_log("Music pack: track %02d from %s (%d Hz, %s)\n", entry, path, t->rate,
                 loop ? "loops" : "once");
    return t;
}

/* jumps to ms milliseconds into the file (wrapping if it loops; past the end it is silent) */
void mp_seek_ms(MpTrack *t, unsigned long ms)
{
    uint64_t frame = (uint64_t)ms * (uint64_t)t->rate / 1000, len;

    len = t->is_mp3 ? drmp3_get_pcm_frame_count(&t->mp3) : stb_vorbis_stream_length_in_samples(t->ogg);
    mp_rewind(t);
    if (len > 0 && frame >= len) {
        if (!t->loop) {
            t->ended = 1;
            return;
        }
        frame %= len;
    }
    if (t->is_mp3)
        drmp3_seek_to_pcm_frame(&t->mp3, frame);
    else
        stb_vorbis_seek(t->ogg, (unsigned)frame);
}

void mp_free(MpTrack *t)
{
    if (!t)
        return;
    if (t->is_mp3)
        drmp3_uninit(&t->mp3);
    else if (t->ogg)
        stb_vorbis_close(t->ogg);
    SDL_free(t->file);
    SDL_free(t);
}

void mp_rewind(MpTrack *t)
{
    seek_start(t);
    t->n = t->pos = 0;
    t->frac = 0;
    t->ended = 0;
}

/* adds frames of the track to mix (stereo) at gain (of 0x4000) */
void mp_render(MpTrack *t, int32_t *mix, int frames, int gain)
{
    int i;

    for (i = 0; i < frames && !t->ended; i++) {
        int l, r;
        while (t->pos + 1 >= t->n)
            if (!refill(t))
                return;
        l = t->buf[2 * t->pos] + (int)(((int64_t)(t->buf[2 * t->pos + 2] - t->buf[2 * t->pos]) * t->frac) >> 16);
        r = t->buf[2 * t->pos + 1] + (int)(((int64_t)(t->buf[2 * t->pos + 3] - t->buf[2 * t->pos + 1]) * t->frac) >> 16);
        mix[2 * i] += (l * gain) >> 14;
        mix[2 * i + 1] += (r * gain) >> 14;
        t->frac += t->step;
        t->pos += t->frac >> 16;
        t->frac &= 0xFFFF;
    }
}

/* the packs in Music/: its folders and zips (names into out, a zip's without .zip, a folder and
 * a zip of the same name once; up to max); returns how many */
int port_music_packs(char out[][64], int max)
{
    char **list;
    int n = 0, i, count = 0;

    if (!port_music_dir[0])
        return 0;
    list = SDL_GlobDirectory(port_music_dir, NULL, 0, &count);
    for (i = 0; list && i < count && n < max; i++) {
        char p[1200];
        SDL_PathInfo info;
        snprintf(p, sizeof p, "%s/%s", port_music_dir, list[i]);
        if (strchr(list[i], '/') || strchr(list[i], '\\'))     /* the glob goes into subfolders too (P47) */
            continue;
        if (!SDL_GetPathInfo(p, &info))
            continue;
        if (info.type == SDL_PATHTYPE_DIRECTORY && strlen(list[i]) < 64)
            snprintf(out[n++], 64, "%s", list[i]);
        else if (info.type == SDL_PATHTYPE_FILE && strlen(list[i]) > 4 && strlen(list[i]) < 68 &&
                 !SDL_strcasecmp(list[i] + strlen(list[i]) - 4, ".zip"))
            snprintf(out[n++], 64, "%.*s", (int)strlen(list[i]) - 4, list[i]);
    }
    for (i = 0; i < n; i++) {           /* a folder and a zip of the same name: once */
        int j;
        for (j = i + 1; j < n; j++)
            if (!SDL_strcasecmp(out[i], out[j])) {
                memmove(out[j], out[j + 1], (size_t)(n - j - 1) * 64);
                n--;
                j--;
            }
    }
    SDL_free(list);
    for (i = 1; i < n; i++) {           /* sorted by name */
        int j;
        for (j = i; j > 0 && SDL_strcasecmp(out[j - 1], out[j]) > 0; j--) {
            char tmp[64];
            memcpy(tmp, out[j], 64);
            memcpy(out[j], out[j - 1], 64);
            memcpy(out[j - 1], tmp, 64);
        }
    }
    return n;
}
