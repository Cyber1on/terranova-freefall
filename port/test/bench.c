/* bench.c -- the built-in benchmark: tnfree --bench (tnfree-benchmark.bat in the release folder).
 *
 * A fixed course through mission 3, drawn with the player's own settings and timed frame by
 * frame.  It runs as the frame comparison runs do (lockstep.c): no timer interrupt and no frame
 * limit, the game's clock 10 ticks on at every frame (40 ms, a frame at the original's 25 a
 * second), the keys put straight into the keyboard handler's buffer, the mission started from
 * the shell at once (no movies, no briefing, no sound).  So every run plays exactly the same
 * game, on any PC and with any settings, and each frame is drawn as fast as the PC can.
 *
 * The course: four scenes, in each the player put in its place (sharp3d.c), then walking forward
 * (W held) for SETTLE frames (the far land and the caches filled) and TIMED frames timed, then
 * stopping for REST frames:
 *   Open ground, cockpit   where mission 3 starts
 *   Lake, goggles          by the lake, the 3D view the whole screen (the heaviest)
 *   Lake, cockpit          the same walk in the cockpit
 *   Forest, cockpit        among the trees
 * For each scene: a frame's time on average and the frames a second that makes, the time 95%
 * of the frames stay under, and the main 3D view's drawing (sharp3d.c; only when a setting draws
 * it again).  The results go to the end of tnfree-bench.txt next to tnfree.exe (in --user's
 * folder with --user), with the date, the version, the CPU, the window's size and the settings
 * changed from the original so that runs can be compared, to tnfree.log, and into a message box
 * when the window has closed (platform.c).  Esc or closing the window stops it (no results). */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif
#include "platform.h"
#include "port.h"
#include "screen.h"
#include "sharp3d.h"
#include "version.h"

void port_exit(int code);

#define BENCH_MISSION   3
#define BENCH_TICKS     10              /* the clock's ticks a frame (250 a second) */
#define START           25              /* frames into the mission before the first scene */
#define SETTLE          50              /* frames from a move to the timing: the walk under way, the
                                           far land and the caches filled */
#define TIMED           200
#define REST            25              /* frames to stop walking before the next move */
#define PERIOD          (SETTLE + TIMED + REST)
#define KEY_W           0x11            /* walk forward (set-1 scan codes) */
#define KEY_G           0x22            /* goggles on / off */

typedef struct {
    const char *name;
    long x, y;                          /* where the player is put, 16.16 */
    unsigned heading;                   /* 0x10000 = a full turn */
    int goggles;
} Scene;

static const Scene scenes[] = {
    { "Open ground, cockpit", 0x012F0000, 0x00D40000, 0x0000, 0 },     /* where the mission starts */
    { "Lake, goggles", 0x01220000, 0x00F80000, 0x8000, 1 },
    { "Lake, cockpit", 0x01220000, 0x00F80000, 0x8000, 0 },
    { "Forest, cockpit", 0x01438000, 0x00C28000, 0xC000, 0 },
};
#define NSCENES ((int)(sizeof scenes / sizeof scenes[0]))

int port_bench;
char port_bench_path[1100];
char port_bench_report[2048];

static long frame;                      /* frames into the mission */
static int timing = -1;                 /* the scene the frame now drawn is timed for, or -1 */
static uint64_t last_us;
static unsigned frame_us[NSCENES][TIMED], view_us[NSCENES][TIMED];
static int ntimed[NSCENES], nview[NSCENES], goggles_off[NSCENES];
static char label[64];

void port_bench_init(long *mission, long *ticks)
{
    *mission = BENCH_MISSION;
    *ticks = BENCH_TICKS;
    port_log("Benchmark: mission %d, %d scenes of %d frames (%d timed)\n", BENCH_MISSION, NSCENES,
             PERIOD, TIMED);
}

const char *port_bench_label(void)
{
    return label;
}

static int cmp_us(const void *a, const void *b)
{
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
    return x < y ? -1 : x > y;
}

static void cpu_name(char *out, size_t n)
{
    snprintf(out, n, "unknown");
#if defined(__i386__) || defined(__x86_64__)
    if (__get_cpuid_max(0x80000000, NULL) >= 0x80000004) {
        unsigned r[13];
        char *s = (char *)r;
        int i;
        for (i = 0; i < 3; i++)
            __get_cpuid(0x80000002 + i, &r[4 * i], &r[4 * i + 1], &r[4 * i + 2], &r[4 * i + 3]);
        r[12] = 0;
        while (*s == ' ')
            s++;
        if (*s)
            snprintf(out, n, "%s", s);
    }
#endif
}

typedef struct {
    int n, nv;                          /* frames timed, of them with the 3D view drawn again */
    double ms, fps, p95, view;          /* a frame on average, frames a second, 95% under, 3D view */
} Stats;

static Stats stats_of(const unsigned *us, int k, const unsigned *view, int nv)
{
    static unsigned sorted[NSCENES * TIMED];
    Stats st = { k, nv, 0, 0, 0, 0 };
    uint64_t sum = 0, vsum = 0;
    int i;
    if (k <= 0)
        return st;
    for (i = 0; i < k; i++) {
        sum += us[i];
        sorted[i] = us[i];
    }
    for (i = 0; i < nv; i++)
        vsum += view[i];
    qsort(sorted, (size_t)k, sizeof *sorted, cmp_us);
    st.ms = sum / 1000.0 / k;
    st.fps = sum ? k * 1e6 / sum : 0;
    st.p95 = sorted[(k * 95 - 1) / 100] / 1000.0;
    st.view = nv ? vsum / 1000.0 / nv : 0;
    return st;
}

static void report(const char *note)
{
    static unsigned all[NSCENES * TIMED], allv[NSCENES * TIMED];
    char text[4096], box[1536], cpu[64], when[32], path[sizeof port_bench_path];
    size_t len = 0, blen = 0, table;
    int s, n = 0, nv = 0;
    time_t t = time(NULL);
    Stats st[NSCENES + 1];
    FILE *f;

#define ADD(...) do { int k_ = snprintf(text + len, sizeof text - len, __VA_ARGS__); \
                      if (k_ > 0) len = len + k_ < sizeof text ? len + k_ : sizeof text - 1; } while (0)
#define BOX(...) do { int k_ = snprintf(box + blen, sizeof box - blen, __VA_ARGS__); \
                      if (k_ > 0) blen = blen + k_ < sizeof box ? blen + k_ : sizeof box - 1; } while (0)
    for (s = 0; s < NSCENES; s++) {
        st[s] = stats_of(frame_us[s], ntimed[s], view_us[s], nview[s]);
        memcpy(all + n, frame_us[s], ntimed[s] * sizeof *all);
        memcpy(allv + nv, view_us[s], nview[s] * sizeof *allv);
        n += ntimed[s];
        nv += nview[s];
    }
    st[NSCENES] = stats_of(all, n, allv, nv);
    cpu_name(cpu, sizeof cpu);
    snprintf(path, sizeof path, "%s", port_bench_path);
#ifdef _WIN32
    for (s = 0; path[s]; s++)           /* as Windows shows paths */
        if (path[s] == '/')
            path[s] = '\\';
#endif
    strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&t));

    /* tnfree-bench.txt and tnfree.log: a table */
    ADD("Terra Nova: Freefall benchmark, %s\n", when);
    ADD("Freefall %s (commit %s)\n", FREEFALL_VERSION_FULL, FREEFALL_COMMIT);
    ADD("CPU: %s, %d logical cores, %d drawing threads\n", cpu, SDL_GetNumLogicalCPUCores(), port_threads());
    ADD("Window %dx%d, game screen %dx%d\n", port_window_w, port_window_h, port_video.w, port_video.h);
    table = len;
    ADD("\n%-22s %9s %6s %11s %11s\n", "Scene", "a frame", "fps", "95% under", "3D view");
    for (s = 0; s <= NSCENES; s++) {
        const char *name = s < NSCENES ? scenes[s].name : "All scenes";
        if (!st[s].n) {
            ADD("%-22s     -\n", name);
            continue;
        }
        ADD("%-22s %6.1f ms %6.1f %8.1f ms", name, st[s].ms, st[s].fps, st[s].p95);
        if (st[s].nv)
            ADD(" %8.1f ms", st[s].view);
        ADD("%s\n", s < NSCENES && goggles_off[s] ? "  (the goggles did not switch)" : "");
    }
    ADD("\nfps: the frames a second this PC could draw (the game itself keeps to 25 a second, or 60\n"
        "with Frame rate 60 fps).  95%% under: 95 frames in 100 took at most that.  3D view: the\n"
        "main 3D view's drawing in a frame, when a setting draws it again (3D resolution and the\n"
        "others).\n");
    if (note)
        ADD("%s\n", note);

    /* the message box (not a fixed-width font): a line a scene */
    if (note)
        BOX("%s\n\n", note);
    if (st[NSCENES].n)
        BOX("All scenes: %.1f ms a frame, %.0f frames a second\n\n", st[NSCENES].ms, st[NSCENES].fps);
    for (s = 0; s < NSCENES; s++)
        if (st[s].n)
            BOX("%s: %.1f ms (%.0f fps)\n", scenes[s].name, st[s].ms, st[s].fps);
    BOX("\nThe frames a second this PC could draw; the game itself keeps to 25 (60 with Smooth "
        "frame rate).\n\n");
#undef ADD

    port_log("\n%s", text);
    f = fopen(port_bench_path, "ab");
    if (f) {
        fprintf(f, "%.*s", (int)table, text);
        fprintf(f, "Settings changed from the original (as in tnfree.ini):\n");
        {
            long at = ftell(f);
            port_screen_save(f);
            if (ftell(f) == at)
                fprintf(f, "(none)\n");
        }
        fprintf(f, "%s\n", text + table);
        fclose(f);
        BOX("All the results (and your settings) were added to\n%s", path);
    } else
        BOX("Could not write %s", path);
#undef BOX
    snprintf(port_bench_report, sizeof port_bench_report, "%s", box);
}

/* the start of every frame after the mission's start (lockstep.c) */
void port_bench_frame(int in_mission)
{
    uint64_t now;
    long f;
    int s, i;

    if (port_quit_requested) {          /* Esc or the window closed: at once (lockstep runs have no
                                           clock for the game's own way out) */
        port_log("Benchmark: stopped\n");
        label[0] = 0;
        port_exit(0);
    }
    if (!in_mission)
        return;
    now = port_now_us();
    f = frame++ - START;

    if (timing >= 0 && ntimed[timing] < TIMED) {    /* the frame just drawn */
        frame_us[timing][ntimed[timing]++] = (unsigned)(now - last_us);
        if (port_s3d_wanted() && port_s3d_us)     /* (as the FPS meter) */
            view_us[timing][nview[timing]++] = port_s3d_us;
    }
    last_us = now;
    timing = -1;
    if (f < 0)
        return;
    s = (int)(f / PERIOD);
    i = (int)(f % PERIOD);
    if (s == NSCENES - 1 && i == SETTLE + TIMED) {
        label[0] = 0;
        report(NULL);
        port_exit(0);
    }
    if (i <= 1)                         /* (twice: sharp3d.c) */
        port_player_place(scenes[s].x, scenes[s].y, scenes[s].heading);
    if (i == 0) {
        const Scene *sc = &scenes[s];
        if (port_hud_goggles() != sc->goggles) {
            port_lockstep_key(KEY_G, 1);
            port_lockstep_key(KEY_G, 0);
        }
        snprintf(label, sizeof label, "Benchmark %d/%d %s", s + 1, NSCENES, sc->name);
        port_log("Benchmark: %s\n", sc->name);
    }
    if (i == 2)                         /* (at the mission's start the walk needs the move first) */
        port_lockstep_key(KEY_W, 1);
    if (i == SETTLE && port_hud_goggles() != scenes[s].goggles)
        goggles_off[s] = 1;
    if (i >= SETTLE && i < SETTLE + TIMED)
        timing = s;
    if (i == SETTLE + TIMED)
        port_lockstep_key(KEY_W, 0);
}

/* the mission ended first (the player killed): what was timed; not when it was stopped */
void port_bench_cut(void)
{
    label[0] = 0;
    if (port_quit_requested) {
        port_log("Benchmark: stopped\n");
        return;
    }
    report("The course was cut short: the mission ended.");
}
