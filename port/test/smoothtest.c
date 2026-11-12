/* smoothtest.c -- "tnfree --smooth-test [--gpu N|integrated|discrete] [--model M] [--out DIR] [MOVIE ...]": smooths some of the
 * game's movies to 30 pictures a second with RIFE (videosmooth.c) and writes each as an AVI file
 * to look at: on the left the movie as the game shows it, on the right smoothed (at the movie's
 * own size: a player shows it bigger; JPEG pictures twice as big took 4 times as long).  MOVIE is a name of the game's MOVIE folder (OPENERA) or the path of a .smk file;
 * without any, a few typical ones.  Out goes to the SmoothTest folder next to tnfree.exe, with
 * results.txt: the graphics card and how long it took for each in-between picture.
 *
 * The AVI files are Motion JPEG (no sound), which VLC and the Windows players play. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "port.h"
#include "videosmooth.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

/* ------------------------------------------------------------------ Motion JPEG AVI */
typedef struct {
    FILE *f;
    int w, h, fps;
    uint32_t frames, max_chunk;
    long movi;                          /* file offset of the "movi" list's fourcc */
    uint32_t *idx;                      /* offset, size per frame */
    uint32_t idx_cap;
    uint8_t *jpg;
    size_t jpg_len, jpg_cap;
} avi;

static void put32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}

static void put16(FILE *f, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    fwrite(b, 1, 2, f);
}

static void fourcc(FILE *f, const char *c) { fwrite(c, 1, 4, f); }

static void avi_headers(avi *a)
{
    FILE *f = a->f;
    fseek(f, 0, SEEK_SET);
    fourcc(f, "RIFF"); put32(f, 0); fourcc(f, "AVI ");
    fourcc(f, "LIST"); put32(f, 4 + 8 + 56 + 8 + 4 + 8 + 56 + 8 + 40); fourcc(f, "hdrl");
    fourcc(f, "avih"); put32(f, 56);
    put32(f, 1000000 / a->fps);         /* microseconds a frame */
    put32(f, a->max_chunk * a->fps);
    put32(f, 0);
    put32(f, 0x10);                     /* has an index */
    put32(f, a->frames);
    put32(f, 0);
    put32(f, 1);                        /* streams */
    put32(f, a->max_chunk);
    put32(f, a->w); put32(f, a->h);
    put32(f, 0); put32(f, 0); put32(f, 0); put32(f, 0);
    fourcc(f, "LIST"); put32(f, 4 + 8 + 56 + 8 + 40); fourcc(f, "strl");
    fourcc(f, "strh"); put32(f, 56);
    fourcc(f, "vids"); fourcc(f, "MJPG");
    put32(f, 0); put16(f, 0); put16(f, 0); put32(f, 0);
    put32(f, 1); put32(f, a->fps);      /* scale, rate */
    put32(f, 0); put32(f, a->frames);
    put32(f, a->max_chunk);
    put32(f, 0xFFFFFFFF); put32(f, 0);
    put16(f, 0); put16(f, 0); put16(f, (uint16_t)a->w); put16(f, (uint16_t)a->h);
    fourcc(f, "strf"); put32(f, 40);
    put32(f, 40); put32(f, a->w); put32(f, a->h);
    put16(f, 1); put16(f, 24);
    fourcc(f, "MJPG"); put32(f, a->w * a->h * 3);
    put32(f, 0); put32(f, 0); put32(f, 0); put32(f, 0);
    fourcc(f, "LIST"); put32(f, 0);
    a->movi = ftell(f);
    fourcc(f, "movi");
}

static int avi_open(avi *a, const char *path, int w, int h, int fps)
{
    memset(a, 0, sizeof *a);
    a->f = fopen(path, "wb");
    if (!a->f)
        return 0;
    a->w = w;
    a->h = h;
    a->fps = fps;
    avi_headers(a);
    return 1;
}

static void jpg_out(void *ctx, void *data, int size)
{
    avi *a = ctx;
    if (a->jpg_len + (size_t)size > a->jpg_cap) {
        a->jpg_cap = (a->jpg_len + size) * 2;
        a->jpg = realloc(a->jpg, a->jpg_cap);
    }
    memcpy(a->jpg + a->jpg_len, data, (size_t)size);
    a->jpg_len += (size_t)size;
}

static void avi_frame(avi *a, const uint8_t *rgb)
{
    uint32_t n;
    a->jpg_len = 0;
    stbi_write_jpg_to_func(jpg_out, a, a->w, a->h, 3, rgb, 92);
    n = (uint32_t)a->jpg_len;
    if (a->frames * 2 + 2 > a->idx_cap) {
        a->idx_cap = a->idx_cap ? a->idx_cap * 2 : 1024;
        a->idx = realloc(a->idx, a->idx_cap * sizeof *a->idx);
    }
    a->idx[a->frames * 2] = (uint32_t)(ftell(a->f) - a->movi);
    a->idx[a->frames * 2 + 1] = n;
    fourcc(a->f, "00dc");
    put32(a->f, n);
    fwrite(a->jpg, 1, n, a->f);
    if (n & 1)
        fputc(0, a->f);
    if (n > a->max_chunk)
        a->max_chunk = n;
    a->frames++;
}

static void avi_close(avi *a)
{
    long end;
    uint32_t i;
    if (!a->f)
        return;
    end = ftell(a->f);
    fourcc(a->f, "idx1");
    put32(a->f, a->frames * 16);
    for (i = 0; i < a->frames; i++) {
        fourcc(a->f, "00dc");
        put32(a->f, 0x10);              /* key frame */
        put32(a->f, a->idx[i * 2]);
        put32(a->f, a->idx[i * 2 + 1]);
    }
    {
        long total = ftell(a->f);
        avi_headers(a);                 /* now with the frame count and sizes */
        fseek(a->f, a->movi - 8, SEEK_SET);
        fourcc(a->f, "LIST");
        put32(a->f, (uint32_t)(end - a->movi));
        fseek(a->f, 4, SEEK_SET);
        put32(a->f, (uint32_t)(total - 8));
    }
    fclose(a->f);
    free(a->idx);
    free(a->jpg);
    memset(a, 0, sizeof *a);
}

/* ------------------------------------------------------------------ the test */
/* src (w x h RGB) into dst at column x0 of a picture dw wide */
static void put(uint8_t *dst, int dw, int x0, const uint8_t *src, int w, int h)
{
    int y;
    for (y = 0; y < h; y++)
        memcpy(dst + ((size_t)y * dw + x0) * 3, src + (size_t)y * w * 3, (size_t)w * 3);
}

static int file_exists(const char *p)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(p, &info) && info.type == SDL_PATHTYPE_FILE;
}

/* a movie name of the game's MOVIE folder (on disk or the CD) or a path: its host path */
static int find_movie(const char *arg, char *host, int size)
{
    char dos[300];
    static const char *where[] = { "C:\\TNOVA\\MOVIE\\%s%s", "D:\\MOVIE\\%s%s", "D:\\TNOVA\\MOVIE\\%s%s" };
    const char *ext = SDL_strcasestr(arg, ".smk") ? "" : ".SMK";
    int i;

    if (file_exists(arg)) {
        snprintf(host, size, "%s", arg);
        return 1;
    }
    for (i = 0; i < 3; i++) {
        snprintf(dos, sizeof dos, where[i], arg, ext);
        port_path(dos, host, size);
        if (file_exists(host))
            return 1;
    }
    return 0;
}

static void base_name(const char *path, char *out, int size)
{
    const char *b = path, *p;
    char *dot;
    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            b = p + 1;
    snprintf(out, size, "%s", b);
    if ((dot = strrchr(out, '.')) != NULL)
        *dot = 0;
}

static int test_movie(const char *arg, const char *out_dir, FILE *res)
{
    char host[1100], name[256], path[1400];
    int w, h, W, H, ok = 1;
    uint32_t frames, n = 0;
    double ms;
    void *m;
    vsmooth *v;
    avi a;
    uint8_t *prev, *cur, *mid, *pic;
    const uint8_t *px, *pal;
    vsmooth_stats st;
    uint64_t t0, t1, write_ns = 0;

    if (!find_movie(arg, host, sizeof host)) {
        fprintf(res, "%s: not found (a name of the game's MOVIE folder, or a .smk file)\n", arg);
        return 0;
    }
    m = port_smk_open(host, &w, &h, &frames, &ms);
    if (!m) {
        fprintf(res, "%s: not a Smacker movie\n", host);
        return 0;
    }
    v = vsmooth_begin(w, h);
    if (!v) {
        port_smk_close(m);
        fprintf(res, "%s: smoothing is not ready\n", arg);
        return 0;
    }
    base_name(host, name, sizeof name);
    snprintf(path, sizeof path, "%s/%s-compare.avi", out_dir, name);
    W = w * 2 + 4;                      /* two pictures, a gap between */
    H = h;
    if (!avi_open(&a, path, W, H, (int)(1000.0 / ms * 2 + 0.5))) {
        vsmooth_end(v);
        port_smk_close(m);
        fprintf(res, "%s: cannot write %s\n", arg, path);
        return 0;
    }
    prev = malloc((size_t)w * h * 3);
    cur = malloc((size_t)w * h * 3);
    mid = malloc((size_t)w * h * 3);
    pic = calloc((size_t)W * H, 3);
    t0 = SDL_GetTicks();
    while (port_smk_next(m, &px, &pal)) {
        vsmooth_push(v, n, px, pal);
        vsmooth_rgb(v, n, cur);
        if (n > 0) {
            /* the in-between picture: the game shows the earlier picture meanwhile */
            int r = vsmooth_mid(v, n, mid, 20000);
            if (r == 0)
                ok = 0;
            put(pic, W, 0, prev, w, h);
            put(pic, W, w + 4, r == 1 ? mid : prev, w, h);
            t1 = SDL_GetTicksNS();
            avi_frame(&a, pic);
            write_ns += SDL_GetTicksNS() - t1;
        }
        put(pic, W, 0, cur, w, h);
        put(pic, W, w + 4, cur, w, h);
        t1 = SDL_GetTicksNS();
        avi_frame(&a, pic);
        write_ns += SDL_GetTicksNS() - t1;
        memcpy(prev, cur, (size_t)w * h * 3);
        n++;
    }
    avi_close(&a);
    vsmooth_get_stats(v, &st);
    vsmooth_end(v);
    port_smk_close(m);
    fprintf(res, "%-10s %3dx%-3d %5u pictures at %.0f fps: %5u in-between made, %.1f ms each "
            "(at most %.1f), %u scene cuts, %u skipped; %.1f s in all (%.1f s writing the AVI) -> %s\n",
            name, w, h, frames, 1000.0 / ms, st.made, st.made ? st.total_ms / st.made : 0.0,
            st.max_ms, st.cuts, st.dropped, (SDL_GetTicks() - t0) / 1000.0, write_ns / 1e9, path);
    fflush(res);
    free(prev);
    free(cur);
    free(mid);
    free(pic);
    return ok;
}

int vsmooth_test_main(int argc, char **argv)
{
    static const char *defaults[] = { "OPENERA", "SCEN05A", "SCEN20A", "BRIEF01A", "NIKDIE" };
    char out_dir[1100], res_path[1200];
    const char *base = SDL_GetBasePath();
    int gpu = -1, i, count = 0, ok = 1;
    const char *list[64];
    FILE *res;

    snprintf(out_dir, sizeof out_dir, "%sSmoothTest", base ? base : "");
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--gpu") && i + 1 < argc) {
            i++;
            gpu = !SDL_strcasecmp(argv[i], "integrated") ? -2 : !SDL_strcasecmp(argv[i], "discrete") ? -3 : atoi(argv[i]);
        } else if (!SDL_strcasecmp(argv[i], "integrated") || !SDL_strcasecmp(argv[i], "discrete"))
            gpu = argv[i][0] == 'i' || argv[i][0] == 'I' ? -2 : -3;
        else if (!strcmp(argv[i], "--model") && i + 1 < argc)
            vsmooth_set_model(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            snprintf(out_dir, sizeof out_dir, "%s", argv[++i]);
        else if (count < 64)
            list[count++] = argv[i];
    }
    if (!count)
        for (; count < (int)SDL_arraysize(defaults); count++)
            list[count] = defaults[count];
    SDL_CreateDirectory(out_dir);
    snprintf(res_path, sizeof res_path, "%s/results.txt", out_dir);
    res = fopen(res_path, "w");
    if (!res) {
        port_log("smooth test: cannot write %s\n", res_path);
        return 1;
    }
    fprintf(res, "Terra Nova: Freefall smoothing test (15 -> 30 pictures a second)\n");
    if (vsmooth_start(1, gpu) != 1) {
        fprintf(res, "Smoothing does not work here: %s\n%s", vsmooth_status(), vsmooth_cards());
        fclose(res);
        return 1;
    }
    fprintf(res, "Vulkan cards:\n%s", vsmooth_cards());
    fprintf(res, "%s\nIn the AVI files: left the movie as the game shows it, right smoothed.\n"
            "Real time needs each in-between picture in under 66 ms.\n\n", vsmooth_status());
    fflush(res);
    for (i = 0; i < count; i++)
        ok &= test_movie(list[i], out_dir, res);
    fclose(res);
    vsmooth_stop();
    port_log("smooth test: results in %s\n", res_path);
    return ok ? 0 : 1;
}
