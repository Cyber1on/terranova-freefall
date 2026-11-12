/* sfsynth.c -- SoundFont music: the game's General MIDI music played by FluidSynth (2.6, LGPL-2.1)
 * as a Sound Blaster AWE32 played it.  The AWE32 had General MIDI sounds of its own (in its ROM) and
 * the game put the bank it brought for the card over them (SOUND\FF.SBK, "TerraNova GM" by Eric
 * Brosius; audiocfg.c snd_load_sbk).  Here the General MIDI sounds come from a SoundFont the
 * player chooses (a .sf2 or .dls file in the Music folder, or Windows' own gm.dls) and the game's
 * bank goes over it (sbkconv.c makes a SoundFont 2 of it in memory).
 *
 * Most of FF.SBK's sounds are made from the card's ROM samples, which no file of the game has.
 * With a dump of the ROM in the Music folder (1 MB, as emulators use: awe32.raw) every one of its
 * 58 sounds and its drum kit plays; without one only the two made from its own samples (Dulcimer,
 * Overdrive guitar) do, and the SoundFont plays the rest.
 *
 * FluidSynth is built without threads (its "embedded" system layer): every call here comes under
 * the sound interrupt lock (miles.c render and the music sequencer), or, opening and closing,
 * while xmi_output is MUSIC_NONE and render does not call in. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miles_int.h"
#include "platform.h"
#include "port.h"

char port_soundfont[128];               /* the SoundFont file in Music/ ("" = the first; ini soundfont=) */
static char sf_note[64];                /* how much of the AWE32 bank plays (the options' help line) */

/* the files the SoundFont line offers: .sf2, .dls and .sbk files in Music/, then Windows' gm.dls */
static int sf_ext(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && (!SDL_strcasecmp(n + l - 4, ".sf2") || !SDL_strcasecmp(n + l - 4, ".dls") ||
                     !SDL_strcasecmp(n + l - 4, ".sbk"));
}

static int windows_gm(char *out, size_t size)
{
#ifdef _WIN32
    /* a 32-bit program's System32 is SysWOW64: Sysnative is the real one */
    static const char *const where[] = { "Sysnative", "System32", "SysWOW64" };
    const char *root = SDL_getenv("SystemRoot");
    int k;
    for (k = 0; k < 3; k++) {
        SDL_PathInfo info;
        snprintf(out, size, "%s\\%s\\drivers\\gm.dls", root ? root : "C:\\Windows", where[k]);
        if (SDL_GetPathInfo(out, &info) && info.type == SDL_PATHTYPE_FILE)
            return 1;
    }
#endif
    (void)out;
    (void)size;
    return 0;
}

int port_soundfonts(char out[][64], int max)
{
    char **list, gm[300];
    int n = 0, i, count = 0, have_gm = 0;
    if (port_music_dir[0]) {
        list = SDL_GlobDirectory(port_music_dir, NULL, 0, &count);
        for (i = 0; list && i < count && n < max; i++) {
            char p[1200];
            SDL_PathInfo info;
            if (strchr(list[i], '/') || strchr(list[i], '\\') || strlen(list[i]) >= 64 || !sf_ext(list[i]))
                continue;
            snprintf(p, sizeof p, "%s/%s", port_music_dir, list[i]);
            if (!SDL_GetPathInfo(p, &info) || info.type != SDL_PATHTYPE_FILE)
                continue;
            snprintf(out[n++], 64, "%s", list[i]);
            have_gm |= !SDL_strcasecmp(list[i], "gm.dls");
        }
        SDL_free(list);
    }
    for (i = 1; i < n; i++) {           /* sorted by name */
        int j;
        for (j = i; j > 0 && SDL_strcasecmp(out[j - 1], out[j]) > 0; j--) {
            char tmp[64];
            memcpy(tmp, out[j], 64);
            memcpy(out[j], out[j - 1], 64);
            memcpy(out[j - 1], tmp, 64);
        }
    }
    if (n < max && !have_gm && windows_gm(gm, sizeof gm))
        snprintf(out[n++], 64, "gm.dls");
    return n;
}

/* the file the SoundFont line names (or the first there is): its path (*win: Windows' own gm.dls);
 * NULL for none */
static const char *sf_path(char *out, size_t size, char *name, size_t nsize, int *win)
{
    static char l[32][64];
    int n = port_soundfonts(l, 32), k, pick = n ? 0 : -1;
    SDL_PathInfo info;
    for (k = 0; k < n; k++)
        if (port_soundfont[0] && !SDL_strcasecmp(l[k], port_soundfont))
            pick = k;
    *win = 0;
    if (pick < 0)
        return NULL;
    snprintf(name, nsize, "%s", l[pick]);
    snprintf(out, size, "%s/%s", port_music_dir, l[pick]);
    if (SDL_GetPathInfo(out, &info) && info.type == SDL_PATHTYPE_FILE)
        return out;
    *win = 1;
    return windows_gm(out, size) ? out : NULL;
}

/* what the SoundFont line shows: the file playing or to play (the folder is looked at once a
 * second at most: the line is drawn every frame) */
void port_soundfont_name(char *buf, size_t n)
{
    static char shown[64], was[128];
    static Uint64 at;
    char p[1200], name[64];
    int win;
    if (!at || SDL_GetTicks() - at > 1000 || strcmp(was, port_soundfont)) {
        if (!sf_path(p, sizeof p, name, sizeof name, &win))
            snprintf(shown, sizeof shown, "None in the Music folder");
        else if (win)
            snprintf(shown, sizeof shown, "Windows' gm.dls");
        else
            snprintf(shown, sizeof shown, "%.30s", name);
        snprintf(was, sizeof was, "%s", port_soundfont);
        at = SDL_GetTicks() | 1;
    }
    snprintf(buf, n, "%s", shown);
}

const char *port_soundfont_note(void) { return sf_note; }

#ifdef FREEFALL_FLUIDSYNTH
#include <fluidsynth.h>

uint8_t *sbk_to_sf2(const uint8_t *d, size_t len, const int16_t *rom, size_t rom_words, size_t *out_len,
                    int *made, int *total);

static fluid_settings_t *settings;
static fluid_synth_t *synth;
static char load_err[200];              /* FluidSynth's last complaint while a file loads */
static int loading, logged;

static void sf_log(int level, const char *msg, void *data)
{
    (void)data;
    if (loading) {                      /* kept: told only if no loader takes the file */
        if (level <= FLUID_ERR)
            snprintf(load_err, sizeof load_err, "%s", msg);
    } else if (level <= FLUID_ERR && logged < 10) {
        logged++;
        port_log("FluidSynth: %s\n", msg);
    }
}

/* ---- files through SDL (UTF-8 names on Windows too), and the converted bank from memory */
#define MEM_NAME "<FF.SBK>"
static uint8_t *mem_sf2;
static size_t mem_len;

static void *cb_open(const char *path)
{
    if (!strcmp(path, MEM_NAME))
        return mem_sf2 ? SDL_IOFromConstMem(mem_sf2, mem_len) : NULL;
    return SDL_IOFromFile(path, "rb");
}

static int cb_read(void *buf, fluid_long_long_t count, void *h)
{
    return count <= 0 || SDL_ReadIO(h, buf, (size_t)count) == (size_t)count ? FLUID_OK : FLUID_FAILED;
}

static int cb_seek(void *h, fluid_long_long_t off, int origin)
{
    SDL_IOWhence w = origin == SEEK_SET ? SDL_IO_SEEK_SET : origin == SEEK_CUR ? SDL_IO_SEEK_CUR : SDL_IO_SEEK_END;
    return SDL_SeekIO(h, off, w) < 0 ? FLUID_FAILED : FLUID_OK;
}

static fluid_long_long_t cb_tell(void *h)
{
    Sint64 p = SDL_TellIO(h);
    return p < 0 ? FLUID_FAILED : p;
}

static int cb_close(void *h)
{
    return SDL_CloseIO(h) ? FLUID_OK : FLUID_FAILED;
}

/* a dump of the AWE32's 1 MB General MIDI ROM in Music/ (its words: "M1MG" at byte 4, or at
 * byte 6 in dumps that start two bytes early); NULL for none */
static int16_t *find_rom(size_t *words)
{
    char **list;
    int i, count = 0;
    int16_t *rom = NULL;
    *words = 0;
    if (!port_music_dir[0])
        return NULL;
    list = SDL_GlobDirectory(port_music_dir, NULL, 0, &count);
    for (i = 0; list && i < count && !rom; i++) {
        char p[1200];
        SDL_PathInfo info;
        size_t len;
        uint8_t *d;
        if (strchr(list[i], '/') || strchr(list[i], '\\'))
            continue;
        snprintf(p, sizeof p, "%s/%s", port_music_dir, list[i]);
        if (!SDL_GetPathInfo(p, &info) || info.type != SDL_PATHTYPE_FILE || info.size < 0x100000 ||
            info.size > 0x100010)
            continue;
        d = SDL_LoadFile(p, &len);
        if (!d)
            continue;
        if (len >= 0x100000 && (!memcmp(d + 4, "M1MG", 4) || !memcmp(d + 6, "M1MG", 4))) {
            int skip = memcmp(d + 4, "M1MG", 4) ? 2 : 0;
            rom = SDL_malloc(0x100000);
            if (rom) {
                memcpy(rom, d + skip, 0x100000 - skip);
                memset((uint8_t *)rom + 0x100000 - skip, 0, skip);
                *words = 0x80000;
                port_log("SoundFont music: AWE32 ROM dump %s\n", list[i]);
            }
        }
        SDL_free(d);
    }
    SDL_free(list);
    return rom;
}

/* a SoundFont 1 bank (.sbk) as a SoundFont 2 in memory, loaded; the sfont id or -1 */
static int load_sbk(const char *path, const int16_t *rom, size_t words, int *made, int *total)
{
    size_t len, out;
    uint8_t *d = SDL_LoadFile(path, &len);
    int id = -1;
    *made = *total = 0;
    if (!d)
        return -1;
    mem_sf2 = sbk_to_sf2(d, len, rom, words, &out, made, total);
    SDL_free(d);
    if (mem_sf2) {
        mem_len = out;
        id = fluid_synth_sfload(synth, MEM_NAME, 0);
        free(mem_sf2);                  /* FluidSynth keeps its own copy */
        mem_sf2 = NULL;
    }
    return id;
}

static int ends_sbk(const char *p)
{
    size_t l = strlen(p);
    return l > 4 && !SDL_strcasecmp(p + l - 4, ".sbk");
}

int sf_open(const char *sbk_path)
{
    fluid_sfloader_t *loader;
    char path[1200], name[64];
    int16_t *rom;
    size_t words;
    int base, ch, made = 0, total = 0, win;

    if (synth)
        return 0;
    sf_note[0] = 0;
    fluid_set_log_function(FLUID_PANIC, sf_log, NULL);
    fluid_set_log_function(FLUID_ERR, sf_log, NULL);
    fluid_set_log_function(FLUID_WARN, NULL, NULL);
    fluid_set_log_function(FLUID_INFO, NULL, NULL);
    fluid_set_log_function(FLUID_DBG, NULL, NULL);
    if (!sf_path(path, sizeof path, name, sizeof name, &win)) {
        port_log("SoundFont music: no .sf2 or .dls file in %s\n", port_music_dir);
        return -1;
    }
    settings = new_fluid_settings();
    if (!settings)
        return -1;
    fluid_settings_setnum(settings, "synth.sample-rate", MIX_RATE);
    fluid_settings_setint(settings, "synth.threadsafe-api", 0);
    fluid_settings_setint(settings, "synth.polyphony", 128);
    fluid_settings_setint(settings, "synth.cpu-cores", 1);
    fluid_settings_setnum(settings, "synth.gain", 0.35);   /* about the FM music's loudness */
    synth = new_fluid_synth(settings);
    if (!synth) {
        delete_fluid_settings(settings);
        settings = NULL;
        return -1;
    }
    loader = new_fluid_defsfloader(settings);   /* .sf2 files and the bank in memory, through SDL */
    if (loader) {
        fluid_sfloader_set_callbacks(loader, cb_open, cb_read, cb_seek, cb_tell, cb_close);
        fluid_synth_add_sfloader(synth, loader);
    }
    rom = find_rom(&words);
    loading = 1;
    load_err[0] = 0;
    base = ends_sbk(path) ? load_sbk(path, rom, words, &made, &total) : fluid_synth_sfload(synth, path, 0);
    if (base < 0) {
        loading = 0;
        port_log("SoundFont music: %s did not load%s%s\n", path, load_err[0] ? ": " : "", load_err);
        SDL_free(rom);
        sf_close();
        return -1;
    }
    port_log("SoundFont music: %s\n", path);
    if (sbk_path && sbk_path[0] && load_sbk(sbk_path, rom, words, &made, &total) >= 0) {
        port_log("SoundFont music: the game's AWE32 bank over it, %d of its %d sounds%s\n", made, total,
                 rom ? "" : " (the others need the AWE32's ROM)");
        if (rom && made >= total)       /* the owner's wording */
            snprintf(sf_note, sizeof sf_note, "AWE32.RAW installed, all %d sounds in use", total);
        else if (rom)
            snprintf(sf_note, sizeof sf_note, "AWE32.RAW installed, %d of %d sounds in use", made, total);
        else
            snprintf(sf_note, sizeof sf_note, "No AWE32.RAW: %d of %d sounds in use", made, total);
    }
    loading = 0;
    SDL_free(rom);
    fluid_synth_program_reset(synth);
    for (ch = 0; ch < 16; ch++) {
        sf_msg(0xB0 | ch, 121, 0);      /* as General MIDI (gmout.c) */
        sf_msg(0xB0 | ch, 7, 127);
        sf_msg(0xB0 | ch, 10, 64);
    }
    return 0;
}

void sf_close(void)
{
    if (synth)
        delete_fluid_synth(synth);
    if (settings)
        delete_fluid_settings(settings);
    synth = NULL;
    settings = NULL;
}

void sf_msg(int status, int d1, int d2)
{
    int ch = status & 15;
    if (!synth)
        return;
    d1 &= 0x7F;
    d2 &= 0x7F;
    switch (status & 0xF0) {
    case 0x80: fluid_synth_noteoff(synth, ch, d1); break;
    case 0x90: if (d2) fluid_synth_noteon(synth, ch, d1, d2); else fluid_synth_noteoff(synth, ch, d1); break;
    case 0xA0: fluid_synth_key_pressure(synth, ch, d1, d2); break;
    case 0xB0: fluid_synth_cc(synth, ch, d1, d2); break;
    case 0xC0: fluid_synth_program_change(synth, ch, d1); break;
    case 0xD0: fluid_synth_channel_pressure(synth, ch, d1); break;
    case 0xE0: fluid_synth_pitch_bend(synth, ch, d1 | d2 << 7); break;
    }
}

/* adds frames (stereo) to the mix, at the FM music's loudness */
void sf_render(int32_t *mix, int frames)
{
    static float l[4096], r[4096];
    int done = 0, i;
    if (!synth)
        return;
    while (done < frames) {
        int n = frames - done > 4096 ? 4096 : frames - done;
        if (fluid_synth_write_float(synth, n, l, 0, 1, r, 0, 1) != FLUID_OK)
            return;
        for (i = 0; i < n; i++) {
            mix[2 * (done + i)] += (int32_t)(l[i] * 32767.0f);
            mix[2 * (done + i) + 1] += (int32_t)(r[i] * 32767.0f);
        }
        done += n;
    }
}

#else  /* built without FluidSynth */

int sf_open(const char *sbk_path)
{
    (void)sbk_path;
    port_log("SoundFont music: this build has no FluidSynth\n");
    return -1;
}

void sf_close(void)
{
}

void sf_msg(int status, int d1, int d2)
{
    (void)status;
    (void)d1;
    (void)d2;
}

void sf_render(int32_t *mix, int frames)
{
    (void)mix;
    (void)frames;
}

#endif
