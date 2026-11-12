/* videosmooth.c -- "Smooth videos": the game's movies run at 15 pictures a second; this makes
 * the picture halfway between each two with RIFE 4.6 (frame interpolation by a neural network)
 * on the graphics card, so they can be shown at 30.  RIFE lives in tnfree-rife.dll
 * (rife/tnfree_rife.cpp, C++ and Vulkan), loaded here only when the option is used: without
 * Vulkan the DLL does not load and the option stays off.  Its model is in RIFE/rife-v4.6 next to
 * tnfree.exe.
 *
 * One worker thread owns the DLL's handle (it loads the model, then makes the pictures), so the
 * game thread never waits for the card: it hands over each pair of pictures as the movie is
 * decoded one picture ahead (smacker.c) and asks for the result when it is due.  Pairs that are
 * a scene cut (or the same picture) get no in-between picture: the earlier one stays up. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "port.h"
#include "tnfree_rife.h"
#include "videosmooth.h"

int port_smooth_videos;

#define SLOTS 4                         /* pairs queued or done at a time */
/* mean difference (0..255 a channel) above which two pictures are a scene cut, and below
 * which they are the same picture */
#define CUT_DIFF 40.0
#define SAME_DIFF 0.25

enum { FREE, QUEUED, RUNNING, DONE };

typedef struct {
    int state;
    uint32_t frame;                     /* in-between of frame-1 and frame */
    uint8_t *a, *b, *out;
    double ms;
} slot;

struct vsmooth {
    int w, h;
    slot s[SLOTS];
    uint8_t *rgb[2];                    /* the last two pictures pushed */
    uint32_t rgb_frame[2];
    int have;                           /* how many of rgb[] hold pictures */
    int closing;
    vsmooth_stats st;
};

static struct {
    int state;                          /* 0 not started, 1 loading, 2 ready, -1 off */
    char status[256];
    SDL_SharedObject *dll;
    tnfree_rife_version_fn version;
    tnfree_rife_gpu_count_fn gpu_count;
    tnfree_rife_default_gpu_fn default_gpu;
    tnfree_rife_gpu_name_fn gpu_name;
    tnfree_rife_open_fn open;
    tnfree_rife_interpolate_fn interpolate;
    tnfree_rife_close_fn close;
    tnfree_rife_shutdown_fn shutdown;
    tnfree_rife *rife;
    int gpu;
    char model_dir[1100];
    char model[64];
    char cards[600];                    /* "  card 0: name (kind)" lines */                     /* folder in RIFE/, "rife-v4.6" unless chosen */
    SDL_Thread *thread;
    SDL_Mutex *mx;
    SDL_Condition *work, *done;
    vsmooth *cur;                       /* the movie playing */
    int quit;
} g;

static void set_off(const char *why)
{
    snprintf(g.status, sizeof g.status, "%s", why);
    port_log("Smooth videos: off (%s)\n", why);
    g.state = -1;
}

static int load_dll(void)
{
    char path[1100];
    const char *base = SDL_GetBasePath();

#ifdef _WIN32
    snprintf(path, sizeof path, "%stnfree-rife.dll", base ? base : "");
#else
    snprintf(path, sizeof path, "%stnfree-rife.so", base ? base : "");
#endif
    if (!g.model[0])
        snprintf(g.model, sizeof g.model, "rife-v4.6");
    snprintf(g.model_dir, sizeof g.model_dir, "%sRIFE/%s", base ? base : "", g.model);
    g.dll = SDL_LoadObject(path);
    if (!g.dll) {
        port_log("Smooth videos: %s: %s\n", path, SDL_GetError());
        set_off("tnfree-rife.dll did not load: it needs Vulkan graphics drivers");
        return 0;
    }
#define FN(name) (g.name = (tnfree_rife_##name##_fn)SDL_LoadFunction(g.dll, "tnfree_rife_" #name)) != NULL
    if (!(FN(version) && FN(gpu_count) && FN(default_gpu) && FN(gpu_name) && FN(open) &&
          FN(interpolate) && FN(close) && FN(shutdown)) || g.version() != TNFREE_RIFE_API_VERSION) {
        set_off("tnfree-rife.dll is not the one of this tnfree.exe");
        return 0;
    }
#undef FN
    return 1;
}

/* The 4x4 blocks (Smacker's blocks) that are the same in both pictures keep their own pixels:
 * RIFE softens still parts a little, which flickered against the sharp real pictures (most of a
 * briefing is still; measured on 5 clips: never worse, briefings much closer to the real
 * in-between picture). */
static void keep_still(int w, int h, const uint8_t *a, const uint8_t *b, uint8_t *out)
{
    size_t pitch = (size_t)w * 3;
    int bx, by, y;
    for (by = 0; by + 4 <= h; by += 4)
        for (bx = 0; bx + 4 <= w; bx += 4) {
            size_t o = by * pitch + (size_t)bx * 3;
            for (y = 0; y < 4; y++)
                if (memcmp(a + o + y * pitch, b + o + y * pitch, 12))
                    break;
            if (y == 4)
                for (y = 0; y < 4; y++)
                    memcpy(out + o + y * pitch, a + o + y * pitch, 12);
        }
}

static int SDLCALL worker(void *arg)
{
    char err[200] = "";
    int n, type = 0;
    (void)arg;

    n = g.gpu_count();
    if (n <= 0) {
        SDL_LockMutex(g.mx);
        set_off("no graphics card with Vulkan");
        SDL_BroadcastCondition(g.done);
        SDL_UnlockMutex(g.mx);
        return 0;
    }
    {
        /* every card in the log (and the test's results); -2 = first integrated, -3 = first discrete */
        static const char *kinds[] = { "discrete", "integrated", "virtual", "CPU" };
        int i, t, want = g.gpu == -2 ? 1 : g.gpu == -3 ? 0 : -1, found = -1;
        size_t len = 0;
        for (i = 0; i < n; i++) {
            const char *name = g.gpu_name(i, &t);
            port_log("Smooth videos: card %d: %s (%s)\n", i, name, t >= 0 && t < 4 ? kinds[t] : "?");
            if (len < sizeof g.cards)
                len += snprintf(g.cards + len, sizeof g.cards - len, "  card %d: %s (%s)\n", i, name,
                                t >= 0 && t < 4 ? kinds[t] : "?");
            if (t == want && found < 0)
                found = i;
        }
        if (want >= 0 && found < 0) {
            SDL_LockMutex(g.mx);
            set_off(want ? "no integrated graphics card with Vulkan" : "no discrete graphics card with Vulkan");
            SDL_BroadcastCondition(g.done);
            SDL_UnlockMutex(g.mx);
            return 0;
        }
        if (want >= 0)
            g.gpu = found;
        else if (g.gpu >= n) {
            SDL_LockMutex(g.mx);
            set_off("there is no card with that number");
            SDL_BroadcastCondition(g.done);
            SDL_UnlockMutex(g.mx);
            return 0;
        } else if (g.gpu < 0)
            g.gpu = g.default_gpu();
    }
    g.rife = g.open(g.model_dir, g.gpu, err, sizeof err);
    SDL_LockMutex(g.mx);
    if (!g.rife) {
        set_off(err[0] ? err : "RIFE did not start");
        SDL_BroadcastCondition(g.done);
        SDL_UnlockMutex(g.mx);
        return 0;
    }
    {
        const char *name = g.gpu_name(g.gpu, &type);
        static const char *kinds[] = { "discrete", "integrated", "virtual", "CPU" };
        snprintf(g.status, sizeof g.status, "RIFE %s on %s (%s)",
                 strncmp(g.model, "rife-v", 6) ? g.model : g.model + 6, name,
                 type >= 0 && type < 4 ? kinds[type] : "?");
        port_log("Smooth videos: %s, %d Vulkan card(s)\n", g.status, n);
    }
    g.state = 2;
    SDL_BroadcastCondition(g.done);
    while (!g.quit) {
        vsmooth *v = g.cur;
        slot *job = NULL;
        int i;
        if (v && !v->closing)
            for (i = 0; i < SLOTS; i++)
                if (v->s[i].state == QUEUED && (!job || v->s[i].frame < job->frame))
                    job = &v->s[i];
        if (!job) {
            SDL_WaitCondition(g.work, g.mx);
            continue;
        }
        job->state = RUNNING;
        SDL_UnlockMutex(g.mx);
        {
            uint64_t t0 = SDL_GetTicksNS();
            int rc = g.interpolate(g.rife, job->a, job->b, v->w, v->h, 0.5f, job->out);
            double ms;
            if (rc == 0)
                keep_still(v->w, v->h, job->a, job->b, job->out);
            ms = (SDL_GetTicksNS() - t0) / 1e6;
            SDL_LockMutex(g.mx);
            job->ms = ms;
            if (rc == 0) {
                job->state = DONE;
                v->st.made++;
                v->st.total_ms += ms;
                if (ms > v->st.max_ms)
                    v->st.max_ms = ms;
            } else {
                job->state = FREE;      /* no picture: the earlier one stays up */
                v->st.dropped++;
            }
        }
        SDL_BroadcastCondition(g.done);
    }
    SDL_UnlockMutex(g.mx);
    return 0;
}

void vsmooth_set_model(const char *name)
{
    if (g.state == 0 && name && name[0] && !strpbrk(name, "/\\:"))
        snprintf(g.model, sizeof g.model, "%s", name);
}

const char *vsmooth_cards(void)
{
    return g.cards;
}

int vsmooth_start(int wait, int gpu)
{
    if (g.state == 0) {
        g.state = 1;
        g.gpu = gpu;
        snprintf(g.status, sizeof g.status, "starting");
        if (!load_dll())
            return -1;
        g.mx = SDL_CreateMutex();
        g.work = SDL_CreateCondition();
        g.done = SDL_CreateCondition();
        g.thread = SDL_CreateThread(worker, "videosmooth", NULL);
        if (!g.thread) {
            set_off("no worker thread");
            return -1;
        }
    }
    if (wait && g.state == 1) {
        SDL_LockMutex(g.mx);
        while (g.state == 1)
            SDL_WaitCondition(g.done, g.mx);
        SDL_UnlockMutex(g.mx);
    }
    return g.state == 2 ? 1 : g.state == 1 ? 0 : -1;
}

const char *vsmooth_status(void)
{
    return g.state == 0 ? "not started" : g.status;
}

void vsmooth_stop(void)
{
    if (!g.thread)
        return;
    SDL_LockMutex(g.mx);
    g.quit = 1;
    SDL_BroadcastCondition(g.work);
    SDL_UnlockMutex(g.mx);
    SDL_WaitThread(g.thread, NULL);
    g.thread = NULL;
    if (g.rife)
        g.close(g.rife);
    g.rife = NULL;
    g.shutdown();
    g.state = -1;
    snprintf(g.status, sizeof g.status, "stopped");
}

vsmooth *vsmooth_begin(int w, int h)
{
    vsmooth *v;
    size_t n = (size_t)w * h * 3;
    int i;

    if (g.state != 2 || w <= 0 || h <= 0)
        return NULL;
    v = calloc(1, sizeof *v);
    v->w = w;
    v->h = h;
    for (i = 0; i < SLOTS; i++) {
        v->s[i].a = malloc(n);
        v->s[i].b = malloc(n);
        v->s[i].out = malloc(n);
    }
    v->rgb[0] = malloc(n);
    v->rgb[1] = malloc(n);
    SDL_LockMutex(g.mx);
    g.cur = v;
    SDL_UnlockMutex(g.mx);
    return v;
}

/* mean difference of two pictures, from every 4th pixel of every 2nd row */
static double picture_diff(const vsmooth *v, const uint8_t *a, const uint8_t *b)
{
    uint64_t sum = 0, count = 0;
    int x, y, c;
    for (y = 0; y < v->h; y += 2) {
        const uint8_t *pa = a + (size_t)y * v->w * 3, *pb = b + (size_t)y * v->w * 3;
        for (x = 0; x < v->w; x += 4)
            for (c = 0; c < 3; c++) {
                int d = pa[x * 3 + c] - pb[x * 3 + c];
                sum += (uint64_t)(d < 0 ? -d : d);
                count++;
            }
    }
    return count ? (double)sum / count : 0;
}

void vsmooth_push(vsmooth *v, uint32_t frame, const uint8_t *pixels, const uint8_t *pal6)
{
    uint8_t lut[256][3], *cur, *prev;
    size_t i, n;
    int k, kfree = -1;
    double diff;

    if (!v)
        return;
    n = (size_t)v->w * v->h;
    for (k = 0; k < 256; k++) {         /* the 6-bit DAC values to 8 bits */
        lut[k][0] = (uint8_t)(pal6[k * 3] << 2 | pal6[k * 3] >> 4);
        lut[k][1] = (uint8_t)(pal6[k * 3 + 1] << 2 | pal6[k * 3 + 1] >> 4);
        lut[k][2] = (uint8_t)(pal6[k * 3 + 2] << 2 | pal6[k * 3 + 2] >> 4);
    }
    /* rgb[0] = the newest picture */
    cur = v->rgb[1];
    prev = v->rgb[0];
    for (i = 0; i < n; i++)
        memcpy(cur + i * 3, lut[pixels[i]], 3);
    v->rgb[1] = prev;
    v->rgb[0] = cur;
    v->rgb_frame[1] = v->rgb_frame[0];
    v->rgb_frame[0] = frame;
    if (v->have < 2)
        v->have++;
    if (v->have < 2 || v->rgb_frame[1] + 1 != frame)
        return;
    diff = picture_diff(v, prev, cur);
    if (diff > CUT_DIFF) {
        v->st.cuts++;
        return;
    }
    if (diff < SAME_DIFF)
        return;
    SDL_LockMutex(g.mx);
    for (k = 0; k < SLOTS; k++) {
        slot *s = &v->s[k];
        if ((s->state == QUEUED || s->state == DONE) && s->frame + 1 < frame) {
            /* its moment has passed (the movie is one picture ahead of the screen) */
            v->st.dropped += s->state == QUEUED;
            s->state = FREE;
        }
        if (s->state == FREE && kfree < 0)
            kfree = k;
    }
    if (kfree < 0)
        v->st.dropped++;                /* the card is behind: this pair gets none */
    else {
        slot *s = &v->s[kfree];
        memcpy(s->a, prev, n * 3);
        memcpy(s->b, cur, n * 3);
        s->frame = frame;
        s->state = QUEUED;
        SDL_SignalCondition(g.work);
    }
    SDL_UnlockMutex(g.mx);
}

int vsmooth_mid(vsmooth *v, uint32_t frame, uint8_t *rgb, int wait_ms)
{
    uint64_t until = SDL_GetTicks() + (wait_ms > 0 ? (uint64_t)wait_ms : 0);
    int k, r = -1;

    if (!v)
        return -1;
    SDL_LockMutex(g.mx);
    for (;;) {
        slot *s = NULL;
        for (k = 0; k < SLOTS; k++)
            if (v->s[k].state != FREE && v->s[k].frame == frame)
                s = &v->s[k];
        if (!s) {
            r = -1;
            break;
        }
        if (s->state == DONE) {
            memcpy(rgb, s->out, (size_t)v->w * v->h * 3);
            s->state = FREE;
            r = 1;
            break;
        }
        {
            uint64_t now = SDL_GetTicks();
            if (g.state != 2 || now >= until) {
                r = 0;
                break;
            }
            SDL_WaitConditionTimeout(g.done, g.mx, (Sint32)(until - now));
        }
    }
    SDL_UnlockMutex(g.mx);
    return r;
}

int vsmooth_rgb(vsmooth *v, uint32_t frame, uint8_t *rgb)
{
    int i;
    if (!v)
        return 0;
    for (i = 0; i < v->have; i++)
        if (v->rgb_frame[i] == frame) {
            memcpy(rgb, v->rgb[i], (size_t)v->w * v->h * 3);
            return 1;
        }
    return 0;
}

/* The in-between picture on the screen: the game thread puts it up, the main thread draws it
 * over the movie's place in the game's screen (vsmooth_overlay) until the movie's next picture
 * is copied to the screen (vsmooth_hide) or the palette changes. */
static struct {
    SDL_SpinLock lock;
    uint8_t *rgb;
    int w, h, cap, on, pitch;
    uint32_t off;                       /* where in the game's video memory */
    unsigned pal;                       /* port_video.pal_serial when put up */
} ov;

void vsmooth_show(vsmooth *v, const uint8_t *rgb, uint32_t vram_off, int pitch)
{
    size_t n;

    if (!v || !port_smooth_videos)
        return;
    n = (size_t)v->w * v->h * 3;
    SDL_LockSpinlock(&ov.lock);
    if (ov.cap < (int)n) {
        free(ov.rgb);
        ov.rgb = malloc(n);
        ov.cap = ov.rgb ? (int)n : 0;
    }
    if (ov.rgb) {
        memcpy(ov.rgb, rgb, n);
        ov.w = v->w;
        ov.h = v->h;
        ov.off = vram_off;
        ov.pitch = pitch;
        ov.pal = port_video.pal_serial;
        ov.on = 1;
        v->st.shown++;
    }
    SDL_UnlockSpinlock(&ov.lock);
}

void vsmooth_hide(void)
{
    if (!ov.on)
        return;
    SDL_LockSpinlock(&ov.lock);
    ov.on = 0;
    SDL_UnlockSpinlock(&ov.lock);
}

int vsmooth_overlay(uint32_t *pixels, int tpitch, uint32_t start, int pitch, int w, int h,
                    void (*grade)(const uint8_t *rgb, uint32_t *out, int n))
{
    int y, x0, y0, rw, rh, drawn = 0;

    if (!ov.on)
        return 0;
    SDL_LockSpinlock(&ov.lock);
    if (ov.on && ov.pal != port_video.pal_serial)
        ov.on = 0;                      /* a new palette (a fade, a new scene): the game's picture */
    if (ov.on && ov.pitch == pitch && ov.off >= start) {
        x0 = (int)((ov.off - start) % (uint32_t)pitch);
        y0 = (int)((ov.off - start) / (uint32_t)pitch);
        rw = x0 + ov.w <= w ? ov.w : w - x0;
        rh = y0 + ov.h <= h ? ov.h : h - y0;
        for (y = 0; y < rh; y++)
            grade(ov.rgb + (size_t)y * ov.w * 3, (uint32_t *)((uint8_t *)pixels + (size_t)(y0 + y) * tpitch) + x0,
                  rw);
        drawn = rw > 0 && rh > 0;
    }
    SDL_UnlockSpinlock(&ov.lock);
    return drawn;
}

int vsmooth_state(void)
{
    return g.state;
}

void vsmooth_get_stats(const vsmooth *v, vsmooth_stats *st)
{
    if (v)
        *st = v->st;
    else
        memset(st, 0, sizeof *st);
}

void vsmooth_count_late(vsmooth *v)
{
    if (v)
        v->st.late++;
}

void vsmooth_end(vsmooth *v)
{
    int i, busy;

    if (!v)
        return;
    SDL_LockMutex(g.mx);
    v->closing = 1;                     /* the worker takes no more of its pairs */
    do {
        busy = 0;
        for (i = 0; i < SLOTS; i++)
            busy |= v->s[i].state == RUNNING;
        if (busy)
            SDL_WaitCondition(g.done, g.mx);
    } while (busy);
    if (g.cur == v)
        g.cur = NULL;
    SDL_UnlockMutex(g.mx);
    if (v->st.made || v->st.cuts)
        port_log("Smooth videos: %u in-between pictures (%.1f ms each, at most %.1f), %u scene cuts, "
                 "%u skipped, %u late, %u shown\n", v->st.made, v->st.made ? v->st.total_ms / v->st.made : 0.0,
                 v->st.max_ms, v->st.cuts, v->st.dropped, v->st.late, v->st.shown);
    for (i = 0; i < SLOTS; i++) {
        free(v->s[i].a);
        free(v->s[i].b);
        free(v->s[i].out);
    }
    free(v->rgb[0]);
    free(v->rgb[1]);
    free(v);
}
