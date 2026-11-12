/* fxpack.c -- Freefall: "Effects pack" and "Objects pack" on the Mods page: finding and loading a
 * pack's pictures (effects.c draws them), and the mod sets: packs of every kind under one name.
 *
 * An effects pack is the Effects folder of a mod: a zip in Mods/ next to tnfree.exe (with --user,
 * in that folder), Mods/<mod>.zip, or while it is being made a folder Mods/<mod> (port_pack_dir;
 * zips are read as they are, modzip.c).
 * Each folder in it whose name starts with a number (1171, or 1171-fireball) stands for the
 * game's frames resource of that number (1165..1193 are the effect animations; 1171 is the
 * fireball of every death); its PNG pictures, in the order of their names (01.png, 02.png ...),
 * are loaded as RGBA (stb_image).  "Tree explosion" (burn.c) also takes three named folders of an
 * effects pack: PlantBlast (pictures as an animation's), Pieces (wood.png, bark.png, leaf.png:
 * debris sheets) and Leftovers/1x/<number> and Leftovers/3x/<number> (what is left of a plant,
 * NN.png the frame NN - 1, indexed as an objects pack's pictures); and BlastMarks/1x and
 * BlastMarks/4x are the game's blast marks on the ground (res 1164 frames 1..4, 02.png .. 05.png,
 * indexed, 64 x 64 and 256 x 256: marks.c).
 * An objects pack is a mod's Objects folder, laid out the same (1160..1163 are the trees,
 * bushes and rocks), but each picture stands for one frame of the resource, the one its name's
 * number says (01.png the first): the frames are different objects, not an animation.  Its
 * pictures are indexed PNGs whose colour numbers are the game's palette entries (0 is
 * see-through), kept as they are (png_indexed): most of the plants' colours are entries the
 * planet's palette changes, so one picture is coloured for every planet as the game's own.
 * A changed pack is loaded at the start of the next sharp drawing (port_fx_update, sharp3d.c),
 * while the game thread waits, so the options screen and the drawing never meet halfway.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"
#include "fxpack.h"
#include "sharp3d.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG                  /* the options screen's Previews (options.c) */
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb/stb_image.h"

#define FX_ANIMS 64                     /* animations in a pack */
#define FX_FRAMES 240                   /* pictures in one */
#define FX_SIDE 1024                    /* the largest picture's side */

char port_fx_pack[64];                  /* "Effects pack": a mod's name (its Effects), "" = off (screen.c) */
char port_obj_pack[64];                 /* "Objects pack": a mod's name (its Objects), "" = off (screen.c) */
char port_mods_dir[1024];               /* the Mods folder (main.c) */

static char fx_loaded[64];              /* the pack in fx[] */
static FxAnim fx[FX_ANIMS];
static int fx_count;
static FxAnim left[2][4];               /* the effects pack's Leftovers/1x, 3x: 1160..1163 */
static FxAnim marks[2];                 /* its BlastMarks/1x, 4x */
static unsigned fx_gen;                 /* the packs loaded */
static char obj_loaded[64];             /* the objects pack in obj[] */
static FxAnim obj[FX_ANIMS];            /* per resource its frames (pix NULL: the game's own) */
static int obj_count;

static void fx_free(void)
{
    int a, i;
    for (a = 0; a < fx_count; a++) {
        for (i = 0; i < fx[a].n; i++) {
            stbi_image_free(fx[a].pic[i].rgba);
            free(fx[a].pic[i].pix);
        }
        free(fx[a].pic);
    }
    memset(fx, 0, sizeof fx);
    fx_count = 0;
    for (a = 0; a < 8; a++) {
        FxAnim *l = &left[a >> 2][a & 3];
        for (i = 0; i < l->n; i++)
            free(l->pic[i].pix);
        free(l->pic);
    }
    memset(left, 0, sizeof left);
    for (a = 0; a < 2; a++) {
        for (i = 0; i < marks[a].n; i++)
            free(marks[a].pic[i].pix);
        free(marks[a].pic);
    }
    memset(marks, 0, sizeof marks);
}

/* the names in a folder, not in its subfolders (SDL_GlobDirectory goes down the whole tree):
 * dirs 1 for the folders only, 0 for the .png files only, 2 for the .zip files only; sorted by
 * name, up to max.  A folder in a zip (modzip.c) is listed from the zip. */
typedef struct {
    const char *dir;
    int dirs, n, max;
    char (*out)[64];
} Listing;

static SDL_EnumerationResult list_one(void *ud, const char *dirname, const char *name)
{
    Listing *l = ud;
    char p[1300];
    SDL_PathInfo info;
    size_t len = strlen(name);
    if (len >= 64 || name[0] == '.')
        return SDL_ENUM_CONTINUE;
    snprintf(p, sizeof p, "%s/%s", l->dir, name);
    if (!SDL_GetPathInfo(p, &info))
        return SDL_ENUM_CONTINUE;
    if (l->dirs == 1 ? info.type == SDL_PATHTYPE_DIRECTORY :
        info.type == SDL_PATHTYPE_FILE && len > 4 && !SDL_strcasecmp(name + len - 4, l->dirs ? ".zip" : ".png"))
        snprintf(l->out[l->n++], 64, "%s", name);
    return l->n < l->max ? SDL_ENUM_CONTINUE : SDL_ENUM_SUCCESS;
}

static int by_row(const void *a, const void *b)
{
    return SDL_strcasecmp((const char *)a, (const char *)b);
}

static int list_dir(const char *dir, int dirs, char out[][64], int max)
{
    Listing l;
    l.dir = dir;
    l.dirs = dirs;
    l.n = 0;
    l.max = max;
    l.out = out;
    if (max > 0 && (l.n = port_mod_list_zip(dir, dirs, out, max)) < 0) {
        l.n = 0;
        SDL_EnumerateDirectory(dir, list_one, &l);
    }
    qsort(out, l.n, 64, by_row);
    return l.n;
}

static void obj_free(void)
{
    int a, i;
    for (a = 0; a < obj_count; a++) {
        for (i = 0; i < obj[a].n; i++)
            free(obj[a].pic[i].pix);
        free(obj[a].pic);
    }
    memset(obj, 0, sizeof obj);
    obj_count = 0;
}

static unsigned long be32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* an indexed PNG's colour numbers (w * h, malloc'd), not its colours; NULL when the file is not
 * one (or is interlaced or bigger than FX_SIDE).  8 bits a pixel, or 1, 2 or 4 as some editors
 * save a picture of few colours */
static const unsigned char png_sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };

static unsigned char *png_indexed(const unsigned char *d, size_t n, int *pw, int *ph)
{
    unsigned char *z = NULL, *raw = NULL, *pix = NULL;
    size_t p = 8, zn = 0;
    unsigned long w = 0, h = 0, row, x, y;
    int ok = 0, rn = 0, bits = 8;

    if (n < 8 || memcmp(d, png_sig, 8))
        return NULL;
    while (p + 12 <= n) {
        unsigned long len = be32(d + p);
        const unsigned char *t = d + p + 4, *c = d + p + 8;
        if (len > n - p - 12)
            break;
        if (!memcmp(t, "IHDR", 4) && len >= 13) {
            w = be32(c);
            h = be32(c + 4);
            bits = c[8];
            ok = (bits == 1 || bits == 2 || bits == 4 || bits == 8) && c[9] == 3 && c[10] == 0 && c[11] == 0 &&
                 c[12] == 0;
        } else if (!memcmp(t, "IDAT", 4)) {
            unsigned char *nz = realloc(z, zn + len + 1);
            if (!nz)
                goto out;
            z = nz;
            memcpy(z + zn, c, len);
            zn += len;
        } else if (!memcmp(t, "IEND", 4))
            break;
        p += 12 + len;
    }
    if (!ok || !z || w < 1 || h < 1 || w > FX_SIDE || h > FX_SIDE)
        goto out;
    row = (w * bits + 7) / 8;
    raw = (unsigned char *)stbi_zlib_decode_malloc((const char *)z, (int)zn, &rn);
    if (!raw || (unsigned long)rn < (row + 1) * h || !(pix = malloc(w * h)))
        goto out;
    for (y = 0; y < h; y++) {           /* the rows' filters, in place (by bytes) */
        unsigned char *s = raw + y * (row + 1) + 1, *up = y ? s - row - 1 : NULL;
        int f = s[-1];
        for (x = 0; x < row; x++) {
            int a = x ? s[x - 1] : 0, b = up ? up[x] : 0, c = up && x ? up[x - 1] : 0, v;
            switch (f) {
            case 0: v = 0; break;
            case 1: v = a; break;
            case 2: v = b; break;
            case 3: v = (a + b) >> 1; break;
            case 4: {
                int q = a + b - c, qa = q > a ? q - a : a - q, qb = q > b ? q - b : b - q, qc = q > c ? q - c : c - q;
                v = qa <= qb && qa <= qc ? a : qb <= qc ? b : c;
                break;
            }
            default:
                free(pix);
                pix = NULL;
                goto out;
            }
            s[x] = (unsigned char)(s[x] + v);
        }
        for (x = 0; x < w; x++)         /* the pixels, high bits first */
            pix[y * w + x] = bits == 8 ? s[x] :
                (unsigned char)(s[x * bits / 8] >> (8 - bits - x * bits % 8) & ((1 << bits) - 1));
    }
    *pw = (int)w;
    *ph = (int)h;
out:
    free(z);
    STBI_FREE(raw);
    return pix;
}

/* a PNG's RGBA pixels (stb_image's, w * h * 4), NULL when it cannot be read.  Every chunk of the
 * file must fit in it, up to its end chunk, before stb_image reads it (P114): stb_image steps over
 * a chunk by adding the chunk's length to its read pointer, and on a 32-bit build whose memory
 * reaches the top of the address space (Linux; not the Windows build, which has 2 GB) a damaged
 * length took the pointer round past the top, and it read from anywhere in memory */
static unsigned char *png_rgba(const unsigned char *d, size_t n, int *w, int *h)
{
    size_t p = 8;
    int ch;
    if (n < 8 || n > 0x7FFFFFFF || memcmp(d, png_sig, 8))
        return NULL;
    for (;;) {
        unsigned long len;
        if (p + 12 > n)
            return NULL;                /* cut short: no end chunk */
        len = be32(d + p);
        if (len > n - p - 12)
            return NULL;
        if (!memcmp(d + p + 4, "IEND", 4))
            break;
        p += 12 + len;
    }
    return stbi_load_from_memory(d, (int)n, w, h, &ch, 4);
}

/* one resource's folder of an objects pack (what "Objects pack" or "Leftovers" in the log): NN.png
 * the frame NN - 1, into a; 1 if it has any */
/* the box of an indexed picture's pixels */
static void pic_box(FxPic *p)
{
    int x, y, x0 = p->w, x1 = 0, y0 = p->h, y1 = 0;
    for (y = 0; y < p->h; y++)
        for (x = 0; x < p->w; x++)
            if (p->pix[(long)y * p->w + x]) {
                x0 = x < x0 ? x : x0;
                x1 = x >= x1 ? x + 1 : x1;
                y0 = y < y0 ? y : y0;
                y1 = y + 1;
            }
    p->bx0 = (short)(x0 < x1 ? x0 : 0);
    p->bx1 = (short)x1;
    p->by0 = (short)(y0 < y1 ? y0 : 0);
    p->by1 = (short)y1;
    p->boxed = 1;
}

static int load_frames(const char *dir, unsigned short res, FxAnim *a, const char *what)
{
    static char list[FX_FRAMES][64];
    char path[1400];
    int count, i, top = 0, got = 0;

    count = list_dir(dir, 0, list, FX_FRAMES);
    for (i = 0; i < count; i++)
        top = SDL_atoi(list[i]) > top ? SDL_atoi(list[i]) : top;
    if (top <= 0)
        return 0;
    a->pic = calloc(top > FX_FRAMES ? FX_FRAMES : top, sizeof *a->pic);
    if (!a->pic)
        return 0;
    for (i = 0; i < count; i++) {
        int k = SDL_atoi(list[i]) - 1, w = 0, h = 0;
        size_t len;
        void *data;
        unsigned char *pix;
        if (k < 0 || k >= FX_FRAMES || a->pic[k].pix)
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, list[i]);
        data = port_mod_load(path, &len);
        pix = data ? png_indexed(data, len, &w, &h) : NULL;
        SDL_free(data);
        if (!pix) {
            port_log("%s: cannot use %s (an indexed PNG, not interlaced, up to %dx%d)\n", what, path, FX_SIDE, FX_SIDE);
            continue;
        }
        a->pic[k].w = w;
        a->pic[k].h = h;
        a->pic[k].pix = pix;
        pic_box(&a->pic[k]);
        got++;
    }
    if (!got) {
        free(a->pic);
        a->pic = NULL;
        return 0;
    }
    a->res = res;
    a->n = top > FX_FRAMES ? FX_FRAMES : top;
    port_log("%s: %u, %d picture%s\n", what, res, got, got == 1 ? "" : "s");
    return 1;
}

static void load_objects(const char *dir, unsigned short res)
{
    if (load_frames(dir, res, &obj[obj_count], "Objects pack"))
        obj_count++;
}

/* one animation's folder: its PNG pictures in the order of their names */
static void load_anim(const char *dir, unsigned short res)
{
    static char list[FX_FRAMES][64];
    char path[1400];
    int count, i, n = 0;
    FxAnim *a = &fx[fx_count];

    count = list_dir(dir, 0, list, FX_FRAMES);
    if (count <= 0)
        return;
    a->pic = calloc(count, sizeof *a->pic);
    for (i = 0; a->pic && i < count; i++) {
        size_t len;
        void *data;
        int w = 0, h = 0;
        unsigned char *rgba, *pix;
        snprintf(path, sizeof path, "%s/%s", dir, list[i]);
        data = port_mod_load(path, &len);
        rgba = data ? png_rgba(data, len, &w, &h) : NULL;
        SDL_free(data);
        pix = rgba && w >= 1 && h >= 1 && w <= FX_SIDE && h <= FX_SIDE ? malloc((size_t)w * h) : NULL;
        if (!pix) {
            port_log("Effects pack: cannot use %s (a PNG up to %dx%d)\n", path, FX_SIDE, FX_SIDE);
            stbi_image_free(rgba);
            continue;
        }
        a->pic[n].w = w;
        a->pic[n].h = h;
        a->pic[n].rgba = rgba;
        a->pic[n].pix = pix;
        a->pic[n].pal = 0;
        n++;
    }
    if (n == 0) {
        free(a->pic);
        a->pic = NULL;
        return;
    }
    a->res = res;
    a->n = n;
    port_log("Effects pack: %s from %d pictures of %dx%d\n", strrchr(dir, '/') ? strrchr(dir, '/') + 1 : dir, n,
             a->pic[0].w, a->pic[0].h);
    fx_count++;
}

/* Pieces: wood.png, bark.png and leaf.png as pictures 0, 1, 2 (RGBA as they are) */
static void load_pieces(const char *dir)
{
    static const char *name[3] = { "wood", "bark", "leaf" };
    char path[1400];
    FxAnim *a = &fx[fx_count];
    int k, got = 0;

    a->pic = calloc(3, sizeof *a->pic);
    for (k = 0; a->pic && k < 3; k++) {
        size_t len;
        void *data;
        int w = 0, h = 0;
        unsigned char *rgba;
        snprintf(path, sizeof path, "%s/%s.png", dir, name[k]);
        data = port_mod_load(path, &len);
        rgba = data ? png_rgba(data, len, &w, &h) : NULL;
        SDL_free(data);
        if (!rgba)
            continue;
        if (w < 24 || h < 24 || w > FX_SIDE || h > FX_SIDE || !(a->pic[k].pix = malloc((size_t)w * h))) {
            port_log("Effects pack: cannot use %s (cells of 24x24, up to %dx%d)\n", path, FX_SIDE, FX_SIDE);
            stbi_image_free(rgba);
            continue;
        }
        a->pic[k].w = w;
        a->pic[k].h = h;
        a->pic[k].rgba = rgba;
        got++;
    }
    if (!got) {
        free(a->pic);
        a->pic = NULL;
        return;
    }
    a->res = FX_PIECES;
    a->n = 3;
    port_log("Effects pack: Pieces, %d sheet%s\n", got, got == 1 ? "" : "s");
    fx_count++;
}

/* Leftovers/1x/<number> and Leftovers/3x/<number> */
static void load_leftovers(const char *dir)
{
    static char list[16][64];
    char sub[1400], res_dir[1500];
    int big, i, count;

    for (big = 0; big < 2; big++) {
        snprintf(sub, sizeof sub, "%s/%s", dir, big ? "3x" : "1x");
        count = list_dir(sub, 1, list, 16);
        for (i = 0; i < count; i++) {
            int res = SDL_atoi(list[i]);
            if (res < 1160 || res > 1163 || left[big][res - 1160].pic)
                continue;
            snprintf(res_dir, sizeof res_dir, "%s/%s", sub, list[i]);
            load_frames(res_dir, (unsigned short)res, &left[big][res - 1160], big ? "Leftovers 3x" : "Leftovers 1x");
        }
    }
}

/* BlastMarks/1x and BlastMarks/4x: the pictures of the size the textures take (64 x 64, 256 x 256) */
static void load_marks(const char *dir)
{
    char sub[1400];
    int big, i;

    for (big = 0; big < 2; big++) {
        FxAnim *a = &marks[big];
        int side = big ? 256 : 64;
        snprintf(sub, sizeof sub, "%s/%s", dir, big ? "4x" : "1x");
        if (a->pic || !load_frames(sub, 1164, a, big ? "BlastMarks 4x" : "BlastMarks 1x"))
            continue;
        for (i = 0; i < a->n; i++)
            if (a->pic[i].pix && (a->pic[i].w != side || a->pic[i].h != side)) {
                port_log("BlastMarks: %s/%02d.png is %dx%d, not %dx%d\n", sub, i + 1, a->pic[i].w, a->pic[i].h, side, side);
                free(a->pic[i].pix);
                a->pic[i].pix = NULL;
            }
    }
}

/* a pack: kind 0 effects (a mod's Effects), 1 objects (its Objects) */
static void load_pack(int kind, const char *name)
{
    static char list[256][64];
    char dir[1200], sub[1400];
    int count, i;

    if (kind) {
        obj_free();
        snprintf(obj_loaded, sizeof obj_loaded, "%s", name);
    } else {
        fx_free();
        snprintf(fx_loaded, sizeof fx_loaded, "%s", name);
    }
    fx_gen++;
    if (!name[0] || !port_mods_dir[0])
        return;
    port_pack_dir(kind ? "Objects" : "Effects", name, dir, sizeof dir);
    count = list_dir(dir, 1, list, 256);
    for (i = 0; i < count && (kind ? obj_count : fx_count) < FX_ANIMS; i++) {
        long res = 0;
        const char *s = list[i];
        while (*s >= '0' && *s <= '9' && res < 0x10000)
            res = res * 10 + *s++ - '0';
        snprintf(sub, sizeof sub, "%s/%s", dir, list[i]);
        if (!kind && s == list[i]) {    /* the named folders */
            if (!SDL_strcasecmp(list[i], "PlantBlast") && !port_fx_anim(FX_PLANTBLAST))
                load_anim(sub, FX_PLANTBLAST);
            else if (!SDL_strcasecmp(list[i], "BlastMarks"))
                load_marks(sub);
            else if (!SDL_strcasecmp(list[i], "Pieces") && !port_fx_anim(FX_PIECES))
                load_pieces(sub);
            else if (!SDL_strcasecmp(list[i], "Leftovers"))
                load_leftovers(sub);
            continue;
        }
        if (s == list[i] || res <= 0 || res >= 0xF000 ||
            (kind ? port_obj_set((unsigned short)res) : port_fx_anim((unsigned short)res)))
            continue;
        if (kind)
            load_objects(sub, (unsigned short)res);
        else
            load_anim(sub, (unsigned short)res);
    }
    if (kind)
        port_log("Objects pack %s: %d resource%s\n", name, obj_count, obj_count == 1 ? "" : "s");
    else
        port_log("Effects pack %s: %d animation%s\n", name, fx_count, fx_count == 1 ? "" : "s");
}

static char want_fx[64], want_obj[64];

static int SDLCALL load_thread(void *unused)
{
    (void)unused;
    if (strcmp(fx_loaded, want_fx))
        load_pack(0, want_fx);
    if (strcmp(obj_loaded, want_obj))
        load_pack(1, want_obj);
    return 0;
}

/* at the start of a sharp drawing: the chosen packs loaded.  The loading runs on a thread of its
 * own (and the game thread waits for it): the game thread's memory blocks are the game's, which
 * the lockstep tests compare (port/test/lockstep.c), and the pictures are not. */
void port_fx_update(void)
{
    SDL_Thread *t;
    if (!strcmp(fx_loaded, port_fx_pack) && !strcmp(obj_loaded, port_obj_pack))
        return;
    snprintf(want_fx, sizeof want_fx, "%s", port_fx_pack);
    snprintf(want_obj, sizeof want_obj, "%s", port_obj_pack);
    t = SDL_CreateThread(load_thread, "mod packs", NULL);
    if (t)
        SDL_WaitThread(t, NULL);
    else
        load_thread(NULL);
}

FxAnim *port_fx_anim(unsigned short res)
{
    int i;
    for (i = 0; i < fx_count; i++)
        if (fx[i].res == res)
            return &fx[i];
    return NULL;
}

int port_fx_loaded(void)
{
    return fx_count;
}

FxAnim *port_fx_left(int big, unsigned short res)
{
    FxAnim *a;
    if (res < 1160 || res > 1163)
        return NULL;
    a = &left[big ? 1 : 0][res - 1160];
    return a->n > 0 ? a : NULL;
}

FxAnim *port_fx_marks(int big)
{
    FxAnim *a = &marks[big ? 1 : 0];
    return a->n > 0 ? a : NULL;
}

unsigned port_fx_gen(void)
{
    return fx_gen;
}

FxAnim *port_obj_set(unsigned short res)
{
    int i;
    for (i = 0; i < obj_count; i++)
        if (obj[i].res == res)
            return &obj[i];
    return NULL;
}

int port_obj_loaded(void)
{
    return obj_count;
}

/* ------------------------------------------------------------------ packs and mod sets
 * A mod set (a mod) is a zip in Mods/ (or, while it is being made, a folder of the same name)
 * holding any of the folders Effects, Objects, Cockpits and Arms: a pack of each of those kinds,
 * named as the set.  A folder comes before a zip of the same name.  Zips are read as they are
 * (modzip.c). */
static const char *const kinds[4] = { "Effects", "Objects", "Cockpits", "Arms" };

static int is_dir(const char *path)     /* a folder, or a zip's top or a folder in it */
{
    return port_mod_type(path) == 2;
}

/* the folder of the pack name of kind ("Effects", ...): the mod set's Mods/<name>/<kind> or
 * Mods/<name>.zip/<kind>; neither there: the zip's (what a missing pack's messages name) */
const char *port_pack_dir(const char *kind, const char *name, char *out, size_t n)
{
    snprintf(out, n, "%s/%s/%s", port_mods_dir, name, kind);
    if (is_dir(out))
        return out;
    snprintf(out, n, "%s/%s.zip/%s", port_mods_dir, name, kind);
    return out;
}

/* the mod sets: folders and zips in Mods/ with an Effects, Objects, Cockpits or Arms folder in them */
int port_mod_sets(char out[][64], int max)
{
    char all[64][64];                   /* (P137: not static: the main and the game's thread list them) */
    char p[1300];
    int n, i, j, k, m = 0, zip;

    if (!port_mods_dir[0])
        return 0;
    for (zip = 0; zip < 2; zip++) {
        n = list_dir(port_mods_dir, zip ? 2 : 1, all, 64);
        for (i = 0; i < n && m < max; i++) {
            if (zip)
                all[i][strlen(all[i]) - 4] = 0;     /* the set's name: the zip's without .zip */
            for (j = 0; j < m && SDL_strcasecmp(out[j], all[i]); j++)
                ;
            if (j < m)
                continue;                           /* a folder of that name came first */
            for (k = 0; k < 4; k++) {
                snprintf(p, sizeof p, "%s/%s%s/%s", port_mods_dir, all[i], zip ? ".zip" : "", kinds[k]);
                if (is_dir(p))
                    break;
            }
            if (k < 4)
                snprintf(out[m++], 64, "%s", all[i]);
        }
    }
    return m;
}

/* a mod set's mod.txt (optional): "name ...", "version ...", "author ..." lines, as one line of
 * text ("Freefall HD 1.0 by Cyberion"); "" without it */
void port_mod_set_info(const char *set, char *out, size_t n)
{
    char path[1300], name[64] = "", version[32] = "", author[64] = "", *text, *line, *end;
    size_t len;

    out[0] = 0;
    if (!port_mods_dir[0])
        return;
    snprintf(path, sizeof path, "%s/%s", port_mods_dir, set);
    if (!is_dir(path))
        snprintf(path, sizeof path, "%s/%s.zip", port_mods_dir, set);
    SDL_strlcat(path, "/mod.txt", sizeof path);
    if (!(text = port_mod_load(path, &len)))
        return;
    for (line = text; line < text + len; line = end + 1) {
        char *v;
        end = line + strcspn(line, "\n");
        *end = 0;
        line[strcspn(line, "\r")] = 0;
        v = line + strcspn(line, " \t");
        if (*v)
            *v++ = 0;
        v += strspn(v, " \t");
        if (!SDL_strcasecmp(line, "name"))
            snprintf(name, sizeof name, "%s", v);
        else if (!SDL_strcasecmp(line, "version"))
            snprintf(version, sizeof version, "%s", v);
        else if (!SDL_strcasecmp(line, "author"))
            snprintf(author, sizeof author, "%s", v);
    }
    SDL_free(text);
    snprintf(out, n, "%s%s%s%s%s", name[0] ? name : set, version[0] ? " " : "", version, author[0] ? " by " : "",
             author);
}

/* the packs of a kind (names into out, up to max), sorted: the mod sets that have that kind */
static int list_packs(const char *kind, char out[][64], int max)
{
    char sets[64][64];
    char dir[1300];
    int n = 0, ns, i;

    if (!port_mods_dir[0] || max <= 0)
        return 0;
    ns = port_mod_sets(sets, 64);
    for (i = 0; i < ns && n < max; i++) {
        port_pack_dir(kind, sets[i], dir, sizeof dir);
        if (is_dir(dir))
            snprintf(out[n++], 64, "%s", sets[i]);
    }
    qsort(out, n, 64, by_row);
    return n;
}

/* the effects packs (names into out, up to max), sorted; returns how many */
int port_fx_packs(char out[][64], int max)
{
    return list_packs("Effects", out, max);
}

/* ... the objects packs */
int port_obj_packs(char out[][64], int max)
{
    return list_packs("Objects", out, max);
}

/* ... the cockpit packs (cockpitpack.c) */
int port_ck_packs(char out[][64], int max)
{
    return list_packs("Cockpits", out, max);
}

/* ... the arms packs (arms.c) */
int port_arms_packs(char out[][64], int max)
{
    return list_packs("Arms", out, max);
}

/* a PNG file's RGBA pixels (w * h * 4, malloc'd), NULL if it cannot be read (arms.c) */
unsigned char *port_png_rgba(const char *path, int *w, int *h)
{
    size_t len;
    void *data = port_mod_load(path, &len);
    unsigned char *rgba = data ? png_rgba(data, len, w, h) : NULL, *out = NULL;
    SDL_free(data);
    if (rgba && (out = malloc((size_t)*w * *h * 4)))
        memcpy(out, rgba, (size_t)*w * *h * 4);
    stbi_image_free(rgba);
    return out;
}

/* an indexed PNG file's colour numbers (w * h, malloc'd), NULL if it is not one (cockpitpack.c) */
unsigned char *port_png_indexed(const char *path, int *w, int *h)
{
    size_t len;
    void *data = port_mod_load(path, &len);
    unsigned char *pix = data ? png_indexed(data, len, w, h) : NULL;
    SDL_free(data);
    return pix;
}
