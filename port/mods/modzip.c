/* modzip.c -- Freefall: mods read from .zip files as they are (no unzipping).
 *
 * A mod is one zip file in Mods/ next to tnfree.exe (Mods/Freefall.zip; the mod's name is the
 * file's name) holding any of the folders Cockpits, Arms, Effects and Objects: a pack of each
 * of those kinds, all with the mod's name (fxpack.c).  The pack paths of fxpack.c then go
 * through the zip: "Mods/Freefall.zip/Effects/1171/01.png" is the member Effects/1171/01.png of
 * Mods/Freefall.zip.  port_mod_load, port_mod_type and port_mod_list_zip read such a path from
 * the zip, any other from the disk, so the loaders (fxpack.c, cockpitpack.c, arms.c) read a mod
 * alike from its zip or, while it is being made, from a folder of the same name.
 *
 * Where a zip's members start: the kind folders may be at the zip's top (Cockpits/...) or under
 * one or two folders (Freefall/Cockpits/..., Mods/Freefall/Cockpits/...: a zipped folder); the
 * first kind folder found within the first three levels decides, and members outside the folders
 * above it are left out.
 *
 * Music packs may be zips too (Music/MyPack.zip, musicpack.c): port_zip_files lists their files
 * and port_mod_load reads them the same way.
 *
 * Members are stored or deflated (stb_image's inflate, fxpack.c); zip64 and encrypted members are
 * not read.  The members' list of each zip is read once and again when the file changes; the
 * loaders run on more than one thread, so the list is guarded by a mutex and each read opens the
 * file on its own.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"
#include "sharp3d.h"
#include "stb/stb_image.h"

#define ZIPS 32                         /* zips known at once */
#define ZNAME 256                       /* longest member name */

typedef struct {
    char name[ZNAME];                   /* without the zip's own leading folders (prefix) */
    Uint32 off, csize, usize;           /* the local header; the sizes */
    Uint16 method;                      /* 0 stored, 8 deflated */
} ZEnt;

typedef struct {
    char path[1300];
    Sint64 size;
    SDL_Time mtime;
    ZEnt *e;
    int n;
} Zip;

static Zip zips[ZIPS];
static int nzips;
static SDL_Mutex *mtx;

static const char *const kinds[4] = { "Effects", "Objects", "Cockpits", "Arms" };

static int is_kind(const char *s, size_t len)
{
    int k;
    for (k = 0; k < 4; k++)
        if (strlen(kinds[k]) == len && !SDL_strncasecmp(s, kinds[k], len))
            return 1;
    return 0;
}

static void lock(void)
{
    static SDL_AtomicInt made;
    if (!mtx) {
        if (SDL_CompareAndSwapAtomicInt(&made, 0, 1))
            mtx = SDL_CreateMutex();
        else
            while (!SDL_GetAtomicPointer((void **)&mtx))
                SDL_Delay(1);
    }
    SDL_LockMutex(mtx);
}

static Uint16 le16(const unsigned char *p) { return (Uint16)(p[0] | p[1] << 8); }
static Uint32 le32(const unsigned char *p) { return (Uint32)p[0] | (Uint32)p[1] << 8 | (Uint32)p[2] << 16 | (Uint32)p[3] << 24; }

/* the members' list of the zip file path; 0 if it is not a zip that can be read */
static int read_list(Zip *z, const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    Sint64 size = io ? SDL_GetIOSize(io) : -1;
    static unsigned char tail[65557];  /* the end of the file (read_list runs with the mutex held) */
    unsigned char *cd = NULL;
    Uint32 cdsize, cdoff, i, at, count;
    size_t tn;
    long k;
    int n = 0;

    if (size < 22) {
        if (io)
            SDL_CloseIO(io);
        return 0;
    }
    tn = (size_t)(size < (Sint64)sizeof tail ? size : (Sint64)sizeof tail);
    if (SDL_SeekIO(io, size - (Sint64)tn, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(io, tail, tn) != tn)
        goto fail;
    for (k = (long)tn - 22; k >= 0 && le32(tail + k) != 0x06054b50; k--)
        ;
    if (k < 0)
        goto fail;
    count = le16(tail + k + 10);
    cdsize = le32(tail + k + 12);
    cdoff = le32(tail + k + 16);
    if (cdoff == 0xFFFFFFFF || (Sint64)cdoff + cdsize > size || !(cd = malloc(cdsize ? cdsize : 1)) ||
        !(z->e = calloc(count ? count : 1, sizeof *z->e)))
        goto fail;
    if (SDL_SeekIO(io, cdoff, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(io, cd, cdsize) != cdsize)
        goto fail;
    for (i = 0, at = 0; i < count && at + 46 <= cdsize && le32(cd + at) == 0x02014b50; i++) {
        Uint16 flags = le16(cd + at + 8), method = le16(cd + at + 10), nl = le16(cd + at + 28);
        Uint32 next = at + 46 + nl + le16(cd + at + 30) + le16(cd + at + 32);
        if (next > cdsize)
            break;
        if (!(flags & 1) && (method == 0 || method == 8) && nl > 0 && nl < ZNAME && n < (int)count &&
            le32(cd + at + 20) != 0xFFFFFFFF && le32(cd + at + 24) != 0xFFFFFFFF) {
            ZEnt *e = &z->e[n++];
            memcpy(e->name, cd + at + 46, nl);
            e->name[nl] = 0;
            for (char *c = e->name; *c; c++)
                if (*c == '\\')
                    *c = '/';
            e->method = method;
            e->csize = le32(cd + at + 20);
            e->usize = le32(cd + at + 24);
            e->off = le32(cd + at + 42);
        }
        at = next;
    }
    free(cd);
    SDL_CloseIO(io);
    z->n = n;
    return 1;
fail:
    free(cd);
    free(z->e);
    z->e = NULL;
    SDL_CloseIO(io);
    return 0;
}

/* drop the folders above the first kind folder (above); members outside them are left out */
static void strip_prefix(Zip *z)
{
    char pre[ZNAME] = "";
    size_t plen;
    int i, d, found = 0;

    for (i = 0; i < z->n && !found; i++) {
        const char *s = z->e[i].name, *c = s, *sl;
        for (d = 0; d < 3 && (sl = strchr(c, '/')) != NULL; d++, c = sl + 1)
            if (is_kind(c, (size_t)(sl - c))) {
                snprintf(pre, sizeof pre, "%.*s", (int)(c - s), s);        /* "" at the top */
                found = 1;
                break;
            }
    }
    if (!(plen = strlen(pre)))
        return;
    for (i = 0; i < z->n; i++) {
        ZEnt *e = &z->e[i];
        if (!SDL_strncasecmp(e->name, pre, plen))
            memmove(e->name, e->name + plen, strlen(e->name + plen) + 1);
        else
            e->name[0] = 0;
    }
}

/* the zip of path's first ".zip" part that is a file, with the rest of path after it (inner,
 * "" for the zip's top); NULL if path goes through no zip.  Called with the mutex held. */
static Zip *zip_of(const char *path, const char **inner)
{
    char zpath[1300];
    const char *c;
    SDL_PathInfo info;
    int i;

    for (c = path; (c = SDL_strcasestr(c, ".zip")) != NULL; c += 4) {
        size_t l = (size_t)(c + 4 - path);
        Zip *z = NULL;
        if (c[4] && c[4] != '/' && c[4] != '\\')
            continue;
        if (l >= sizeof zpath)
            return NULL;
        memcpy(zpath, path, l);
        zpath[l] = 0;
        if (!SDL_GetPathInfo(zpath, &info) || info.type != SDL_PATHTYPE_FILE)
            continue;
        *inner = c[4] ? c + 5 : c + 4;
        for (i = 0; i < nzips; i++)
            if (!strcmp(zips[i].path, zpath)) {
                z = &zips[i];
                if (z->size == (Sint64)info.size && z->mtime == info.modify_time)
                    return z->e ? z : NULL;
                free(z->e);             /* changed: read it again */
                z->e = NULL;
                break;
            }
        if (!z) {
            if (nzips == ZIPS) {        /* forget the oldest */
                free(zips[0].e);
                memmove(zips, zips + 1, sizeof zips[0] * (ZIPS - 1));
                nzips--;
            }
            z = &zips[nzips++];
            memset(z, 0, sizeof *z);
            snprintf(z->path, sizeof z->path, "%s", zpath);
        }
        z->size = (Sint64)info.size;
        z->mtime = info.modify_time;
        if (!read_list(z, zpath)) {
            port_log("Cannot read %s as a zip\n", zpath);
            return NULL;
        }
        strip_prefix(z);
        return z;
    }
    return NULL;
}

static ZEnt *member(Zip *z, const char *inner)
{
    int i;
    for (i = 0; i < z->n; i++)
        if (!SDL_strcasecmp(z->e[i].name, inner))
            return &z->e[i];
    return NULL;
}

/* a mod file's bytes (SDL_free them), from the disk or a zip; NULL if it cannot be read */
void *port_mod_load(const char *path, size_t *len)
{
    const char *inner;
    unsigned char hdr[30];
    char *packed = NULL, *data = NULL;
    SDL_IOStream *io = NULL;
    ZEnt e;
    Zip *z;
    char zpath[1300];

    *len = 0;
    lock();
    z = zip_of(path, &inner);
    if (!z) {
        SDL_UnlockMutex(mtx);
        return SDL_LoadFile(path, len);
    }
    if (!member(z, inner)) {
        SDL_UnlockMutex(mtx);
        return NULL;
    }
    e = *member(z, inner);
    snprintf(zpath, sizeof zpath, "%s", z->path);
    SDL_UnlockMutex(mtx);

    /* a damaged zip (P112): a stored member's two sizes differ (the buffer was the packed size and
     * the end mark went at the other), or a size too big for the inflater's int */
    if ((e.method == 0 && e.usize != e.csize) || e.csize > 0x7FFFFFFE || e.usize > 0x7FFFFFFE)
        goto fail;
    if (!(io = SDL_IOFromFile(zpath, "rb")) || SDL_SeekIO(io, e.off, SDL_IO_SEEK_SET) < 0 ||
        SDL_ReadIO(io, hdr, 30) != 30 || le32(hdr) != 0x04034b50 ||
        SDL_SeekIO(io, (Sint64)e.off + 30 + le16(hdr + 26) + le16(hdr + 28), SDL_IO_SEEK_SET) < 0 ||
        !(packed = SDL_malloc((size_t)e.csize + 1)) || SDL_ReadIO(io, packed, e.csize) != e.csize)
        goto fail;
    if (e.method == 0) {
        packed[e.csize] = 0;            /* text ends there, as SDL_LoadFile's */
        data = packed;
        packed = NULL;
    } else if (!(data = SDL_malloc((size_t)e.usize + 1)) ||
               stbi_zlib_decode_noheader_buffer(data, (int)e.usize, packed, (int)e.csize) != (int)e.usize)
        goto fail;
    data[e.usize] = 0;
    SDL_free(packed);
    SDL_CloseIO(io);
    *len = e.usize;
    return data;
fail:
    port_log("Cannot read %s\n", path);
    SDL_free(packed);
    SDL_free(data);
    if (io)
        SDL_CloseIO(io);
    return NULL;
}

/* what path is: 0 nothing, 1 a file, 2 a folder (a zip's top or a folder in it too) */
int port_mod_type(const char *path)
{
    const char *inner;
    SDL_PathInfo info;
    Zip *z;
    int t = 0, i;
    size_t l;

    lock();
    z = zip_of(path, &inner);
    if (z) {
        l = strlen(inner);
        if (!l)
            t = 2;
        for (i = 0; i < z->n && !t; i++)
            if (!SDL_strncasecmp(z->e[i].name, inner, l) && (z->e[i].name[l] == '/' || !z->e[i].name[l]))
                t = z->e[i].name[l] ? 2 : 1;
        SDL_UnlockMutex(mtx);
        return t;
    }
    SDL_UnlockMutex(mtx);
    if (!SDL_GetPathInfo(path, &info))
        return 0;
    return info.type == SDL_PATHTYPE_DIRECTORY ? 2 : info.type == SDL_PATHTYPE_FILE ? 1 : 0;
}

/* the names in a folder of a zip (path through a zip, as above): what 1 its folders, 0 its .png
 * files, 2 its .zip files; up to max, not sorted; -1 if path goes through no zip */
int port_mod_list_zip(const char *path, int what, char out[][64], int max)
{
    const char *inner;
    Zip *z;
    int n = 0, i, j;
    size_t l;

    lock();
    z = zip_of(path, &inner);
    if (!z) {
        SDL_UnlockMutex(mtx);
        return -1;
    }
    l = strlen(inner);
    for (i = 0; i < z->n && n < max; i++) {
        const char *s = z->e[i].name, *sl;
        size_t nl;
        if (l && (SDL_strncasecmp(s, inner, l) || s[l] != '/'))
            continue;
        s += l ? l + 1 : 0;
        sl = strchr(s, '/');
        nl = sl ? (size_t)(sl - s) : strlen(s);
        if (!nl || nl >= 64 || s[0] == '.' || (what == 1) != (sl != NULL))
            continue;
        if (what != 1 && (nl <= 4 || SDL_strncasecmp(s + nl - 4, what ? ".zip" : ".png", 4)))
            continue;
        for (j = 0; j < n && (strlen(out[j]) != nl || SDL_strncasecmp(out[j], s, nl)); j++)
            ;
        if (j == n)
            snprintf(out[n++], 64, "%.*s", (int)nl, s);
    }
    SDL_UnlockMutex(mtx);
    return n;
}

/* every file in the zip at path (the members' names with their folders, as in the zip), up to
 * max; -1 if path is no zip */
int port_zip_files(const char *path, char out[][256], int max)
{
    const char *inner;
    Zip *z;
    int n = 0, i;

    lock();
    z = zip_of(path, &inner);
    if (!z || *inner) {
        SDL_UnlockMutex(mtx);
        return -1;
    }
    for (i = 0; i < z->n && n < max; i++) {
        size_t l = strlen(z->e[i].name);
        if (l && z->e[i].name[l - 1] != '/' && l < 256)
            snprintf(out[n++], 256, "%s", z->e[i].name);
    }
    SDL_UnlockMutex(mtx);
    return n;
}
