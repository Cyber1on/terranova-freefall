/* files.c -- the game's files.
 *
 * The game uses DOS paths: "c:\tnova\data\resgame.res" (its install folder, TARG_DIR in TN.CFG),
 * "D:\maps\map1.res" (the CD), or paths relative to the current folder, which was the install
 * folder.  The port maps them onto three host folders:
 *   - the game folder (port_game_dir, the player's TNOVA folder): read only, never changed;
 *   - the CD folder (port_cd_dir; may be the game folder): read only; or, when the CD is an
 *     image file, the folder CD next to tnfree.exe with the files copied from it (see
 *     cd_image_setup);
 *   - the user folder (port_user_dir): everything the game writes (config, saves, its copies of
 *     CD files), and the first place to look when reading install-folder files.
 * So the player's installation is never modified, and the game still finds what it wrote.
 * On Linux and macOS, names are matched without regard to case, as on DOS.
 *
 * The C library calls of the game that take a path are renamed to port_* here (by
 * port/include/port_libc.h, included first by every file of src/): open, fopen, access,
 * remove, unlink, rename, mkdir, chdir, getcwd, stat, opendir, system; plus the Watcom DOS calls
 * _dos_findfirst / _dos_findnext / _dos_getfileattr / _dos_getdiskfree.  open() takes the
 * Watcom flag values, as the decompiled code passes them. */
#define _FILE_OFFSET_BITS 64        /* 32-bit Linux: stat() of files with 64-bit inode numbers */
#include <SDL3/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#endif
#include "platform.h"
#include "port.h"
#include "extra.h"

char port_game_dir[1024] = ".";
char port_cd_dir[1024] = "";
char port_user_dir[1024] = "";
static char cd_dest[1100];           /* where the files of a CD image are copied to */
static char cd_cache_dir[1100] = "";       /* files copied from a CD image (cd_image_setup) */

static char cwd_dos[256] = "C:\\TNOVA";
static int trace_files;                     /* FREEFALL_TRACE_FILES=1: log every open */

enum { ROOT_INSTALL, ROOT_CD };
enum { FOR_READ, FOR_WRITE, FOR_DELETE };
#define MAX_FDS 256
static unsigned char text_fd[MAX_FDS];      /* opened in (Watcom) text mode */

#ifdef _WIN32
typedef struct _stat hstat_t;           /* msvcrt's own name: not wrapped */
#define HSTAT(p, s) _stat(p, s)
#else
typedef struct stat hstat_t;

#define HSTAT(p, s) stat(p, s)
#endif

/* ------------------------------------------------------------------ host helpers */
static int is_sep(char c) { return c == '/' || c == '\\'; }

static int host_exists(const char *p, int *is_dir)
{
    hstat_t st;
    if (HSTAT(p, &st) != 0)
        return 0;
    if (is_dir)
        *is_dir = S_ISDIR(st.st_mode);
    return 1;
}

static void host_mkdir(const char *p)
{
#ifdef _WIN32
    _mkdir(p);
#else
    mkdir(p, 0755);
#endif
}

/* makes every parent folder of host path p */
static void make_parents(const char *p)
{
    char buf[1024];
    size_t i, n = strlen(p);
    if (n >= sizeof buf)
        return;
    memcpy(buf, p, n + 1);
    for (i = 1; i < n; i++)
        if (buf[i] == '/' && buf[i - 1] != ':') {
            buf[i] = 0;
            if (!host_exists(buf, NULL))
                host_mkdir(buf);
            buf[i] = '/';
        }
}

/* base + "/" + rel (rel uses '/'); on case-sensitive systems each part of rel is matched
 * without regard to case against what exists.  Returns 1 if the result exists. */
static int join(const char *base, const char *rel, char *out, int size)
{
#ifdef _WIN32
    snprintf(out, size, "%s/%s", base, rel);
    return host_exists(out, NULL);
#else
    char part[256];
    const char *r = rel;
    int found = 1;

    snprintf(out, size, "%s", base);
    while (*r) {
        const char *e = r;
        size_t n;
        while (*e && *e != '/')
            e++;
        n = (size_t)(e - r);
        if (n && n < sizeof part) {
            memcpy(part, r, n);
            part[n] = 0;
            if (found && !(n == 1 && part[0] == '.')) {
                DIR *d = opendir(out);
                struct dirent *de;
                int hit = 0;
                if (d) {
                    while ((de = readdir(d)) != NULL)
                        if (strcasecmp(de->d_name, part) == 0) {
                            snprintf(part, sizeof part, "%s", de->d_name);
                            hit = 1;
                            break;
                        }
                    closedir(d);
                }
                found = hit;
            }
            if (!(n == 1 && part[0] == '.'))
                snprintf(out + strlen(out), size - strlen(out), "/%s", part);
        }
        r = *e ? e + 1 : e;
    }
    return found && host_exists(out, NULL);
#endif
}

int port_host_join(const char *base, const char *rel, char *out, int size)
{
    return join(base, rel, out, size);
}

/* ------------------------------------------------------------------ DOS path -> root + rel */
static void split_dos(const char *dos, int *root, char *rel, int size)
{
    char full[512], *p, *o;
    const char *s = dos;
    int drive;

    if (s[0] && s[1] == ':') {
        drive = toupper((unsigned char)s[0]);
        s += 2;
        if (!is_sep(*s)) {                  /* "D:name": relative to the drive's root */
            snprintf(full, sizeof full, "\\%s", s);
            s = full;
        }
    } else {
        drive = toupper((unsigned char)cwd_dos[0]);
        if (!is_sep(*s)) {
            snprintf(full, sizeof full, "%s\\%s", cwd_dos + 2, s);
            s = full;
        }
    }
    *root = drive == 'C' ? ROOT_INSTALL : ROOT_CD;
    /* normalise: '/' separators, no "." parts, resolve "..", no leading separator */
    o = rel;
    for (p = (char *)s; *p;) {
        char *e;
        size_t n;
        while (is_sep(*p))
            p++;
        e = p;
        while (*e && !is_sep(*e))
            e++;
        n = (size_t)(e - p);
        if (n == 1 && p[0] == '.')
            ;
        else if (n == 2 && p[0] == '.' && p[1] == '.') {
            while (o > rel && o[-1] != '/')
                o--;
            if (o > rel)
                o--;
        } else if (n && (o - rel) + n + 2 < (size_t)size) {
            if (o > rel)
                *o++ = '/';
            memcpy(o, p, n);
            o += n;
        }
        p = e;
    }
    *o = 0;
    /* the install folder is "\tnova" on drive C: (TARG_DIR) */
    if (*root == ROOT_INSTALL && SDL_strncasecmp(rel, "tnova", 5) == 0 && (rel[5] == '/' || !rel[5]))
        memmove(rel, rel + (rel[5] ? 6 : 5), strlen(rel + (rel[5] ? 6 : 5)) + 1);
}

/* The host path for a DOS path; `use` is FOR_READ, FOR_WRITE or FOR_DELETE.  Returns 0 if the
 * path can't be used that way (writing to the CD). */
static int map_path(const char *dos, char *out, int size, int use)
{
    char rel[512], tmp[1024];
    int root;

    split_dos(dos, &root, rel, sizeof rel);
    /* an extra mission's own mission file and map, under the game's names (port/mods/extra.c) */
    if (use == FOR_READ && port_extra_on && port_extra_file(rel, out, size))
        return 1;
    if (root == ROOT_CD) {
        if (use != FOR_READ)
            return 0;
        if (port_cd_dir[0] && join(port_cd_dir, rel, out, size))
            return 1;
        if (cd_cache_dir[0] && join(cd_cache_dir, rel, out, size))
            return 1;
        if (join(port_game_dir, rel, out, size))
            return 1;
        if (port_cd_dir[0])
            join(port_cd_dir, rel, out, size);
        return 1;
    }
    if (join(port_user_dir, rel, out, size))
        return 1;
    if (use != FOR_READ) {
        join(port_user_dir, rel, out, size);
        return 1;
    }
    if (join(port_game_dir, rel, tmp, sizeof tmp) ||
        /* a config that names the install drive for files this installation keeps with the CD's */
        (port_cd_dir[0] && join(port_cd_dir, rel, tmp, sizeof tmp)) ||
        (cd_cache_dir[0] && join(cd_cache_dir, rel, tmp, sizeof tmp))) {
        snprintf(out, size, "%s", tmp);
        return 1;
    }
    /* not found anywhere: report the user folder name (a create would go there) */
    join(port_user_dir, rel, out, size);
    return 1;
}

char *port_path(const char *dos, char *out, int size)
{
    if (!map_path(dos, out, size, FOR_READ))
        snprintf(out, size, "%s", dos);
    return out;
}

/* a file of the install folder that is about to be changed: copy it to the user folder first */
static void copy_up(const char *dos, const char *dst)
{
    char rel[512], src[1024];
    int root;
    FILE *a, *b;
    char buf[65536];
    size_t n;

    if (host_exists(dst, NULL))
        return;
    split_dos(dos, &root, rel, sizeof rel);
    if (root != ROOT_INSTALL || !join(port_game_dir, rel, src, sizeof src))
        return;
    make_parents(dst);
    a = fopen(src, "rb");
    b = a ? fopen(dst, "wb") : NULL;
    if (a && b)
        while ((n = fread(buf, 1, sizeof buf, a)) > 0)
            fwrite(buf, 1, n, b);
    if (a)
        fclose(a);
    if (b)
        fclose(b);
}

/* ------------------------------------------------------------------ wrapped C library calls */
/* Watcom open() flags */
#define W_WRONLY 0x0001
#define W_RDWR 0x0002
#define W_APPEND 0x0010
#define W_CREAT 0x0020
#define W_TRUNC 0x0040
#define W_EXCL 0x0400
#define W_BINARY 0x0200

/* a folder, which DOS (and Windows) would not open as a file.  An empty name maps to the user
 * folder itself: the demo player's +PLAY tries "" first and then tn.rec (P88) */
static int is_dir(const char *host)
{
    struct stat st;
    return stat(host, &st) == 0 && S_ISDIR(st.st_mode);
}

int port_open(const char *p, int wflags, ...)
{
    char host[1024];
    int flags, writing = (wflags & (W_WRONLY | W_RDWR | W_CREAT | W_TRUNC | W_APPEND)) != 0;

    if (!map_path(p, host, sizeof host, writing ? FOR_WRITE : FOR_READ) || is_dir(host)) {
        errno = EACCES;
        return -1;
    }
    flags = (wflags & W_RDWR) ? O_RDWR : (wflags & W_WRONLY) ? O_WRONLY : O_RDONLY;
    if (wflags & W_APPEND) flags |= O_APPEND;
    if (wflags & W_CREAT) flags |= O_CREAT;
    if (wflags & W_TRUNC) flags |= O_TRUNC;
    if (wflags & W_EXCL) flags |= O_EXCL;
#ifdef O_BINARY
    flags |= O_BINARY;
#endif
    if (writing) {
        if (!(wflags & W_TRUNC))
            copy_up(p, host);
        make_parents(host);
    }
    {
        int fd = open(host, flags, 0644);
        if (trace_files)
            port_log("open(\"%s\", %#x) -> %s: %d\n", p, wflags, host, fd);
        if (fd >= 0 && fd < MAX_FDS)
            text_fd[fd] = !(wflags & W_BINARY);
        return fd;
    }
}

/* Watcom's text mode (open() without O_BINARY): reading drops every CR and stops at ^Z,
 * writing turns LF into CR LF.  The game's text files (TN.CFG ...) have DOS line ends. */
int port_read(int fd, void *buf, unsigned n)
{
    char *b = buf;
    unsigned got = 0;

    if (fd < 0 || fd >= MAX_FDS || !text_fd[fd])
        return (int)read(fd, buf, n);
    while (got < n) {
        int r = (int)read(fd, b + got, n - got), i;
        unsigned j = got;
        if (r <= 0)
            return got || r == 0 ? (int)got : r;
        for (i = 0; i < r; i++) {
            char c = b[got + i];
            if (c == 0x1A) {                    /* end of a text file */
                lseek(fd, (long)i - r, SEEK_CUR);
                return (int)j;
            }
            if (c != '\r')
                b[j++] = c;
        }
        got = j;
    }
    return (int)got;
}

int port_write(int fd, const void *buf, unsigned n)
{
    const char *b = buf;
    char out[512];
    unsigned i, k = 0;

    if (fd < 0 || fd >= MAX_FDS || !text_fd[fd])
        return (int)write(fd, buf, n);
    for (i = 0; i < n; i++) {
        if (k + 2 > sizeof out) {
            if (write(fd, out, k) != (int)k)
                return -1;
            k = 0;
        }
        if (b[i] == '\n')
            out[k++] = '\r';
        out[k++] = b[i];
    }
    if (k && write(fd, out, k) != (int)k)
        return -1;
    return (int)n;
}

int port_close(int fd)
{
    if (fd >= 0 && fd < MAX_FDS)
        text_fd[fd] = 0;
    return close(fd);
}

FILE *port_fopen(const char *p, const char *mode)
{
    char host[1024];
    int writing = strpbrk(mode, "wa+") != NULL;

    if (!map_path(p, host, sizeof host, writing ? FOR_WRITE : FOR_READ) || is_dir(host)) {
        errno = EACCES;
        return NULL;
    }
    if (writing) {
        if (!strchr(mode, 'w'))
            copy_up(p, host);
        make_parents(host);
    }
    {
        FILE *f;
        char m[8];
        /* Watcom's default is text mode (CR LF line ends) */
        snprintf(m, sizeof m, "%s", mode);
#ifdef _WIN32
        if (!strpbrk(m, "bt") && strlen(m) < sizeof m - 1)
            strcat(m, "t");
        f = fopen(host, m);
#else
        if (!strpbrk(m, "b") && !writing) {
            f = fopen(host, "rb");
            if (f) {
                long size, i, j = 0;
                char *data;
                fseek(f, 0, SEEK_END);
                size = ftell(f);
                fseek(f, 0, SEEK_SET);
                data = malloc(size + 1);
                if (data && fread(data, 1, size, f) == (size_t)size) {
                    for (i = 0; i < size && data[i] != 0x1A; i++)
                        if (data[i] != '\r')
                            data[j++] = data[i];
                    fclose(f);
                    f = j ? fmemopen(data, j, "r") : fopen("/dev/null", "r");
                    /* the buffer lives as long as the program (text files are few and small) */
                } else {
                    free(data);
                    rewind(f);
                }
            }
        } else
            f = fopen(host, m);
#endif
        if (trace_files)
            port_log("fopen(\"%s\", \"%s\") -> %s: %s\n", p, mode, host, f ? "ok" : "failed");
        return f;
    }
}

int port_access(const char *p, int mode)
{
    char host[1024];
    if (!map_path(p, host, sizeof host, (mode & 2) ? FOR_WRITE : FOR_READ))
        return -1;
    if (mode & 2)                       /* the user folder is writable */
        return 0;
    return access(host, 0);
}

int port_remove(const char *p)
{
    char host[1024], rel[512], user[1024];
    int root;
    split_dos(p, &root, rel, sizeof rel);
    if (root != ROOT_INSTALL || !join(port_user_dir, rel, user, sizeof user)) {
        errno = ENOENT;
        return -1;                      /* only the game's own files can be removed */
    }
    (void)host;
    return remove(user);
}

int port_unlink(const char *p)
{
    return port_remove(p);
}

int port_rename(const char *a, const char *b)
{
    char ha[1024], hb[1024];
    if (!map_path(a, ha, sizeof ha, FOR_WRITE) || !map_path(b, hb, sizeof hb, FOR_WRITE))
        return -1;
    copy_up(a, ha);
    make_parents(hb);
    return rename(ha, hb);
}

int port_mkdir(const char *p, ...)
{
    char host[1024];
    if (!map_path(p, host, sizeof host, FOR_WRITE))
        return -1;
    if (host_exists(host, NULL)) {
        errno = EEXIST;
        return -1;
    }
    make_parents(host);
    host_mkdir(host);
    return host_exists(host, NULL) ? 0 : -1;
}

/* the current folder only exists for the game (the process's stays where it is) */
int port_chdir(const char *p)
{
    char host[1024], dos[256];
    int dir = 0;

    if (!map_path(p, host, sizeof host, FOR_READ) || !host_exists(host, &dir) || !dir) {
        char rel[512];
        int root;
        split_dos(p, &root, rel, sizeof rel);
        if (!(root == ROOT_INSTALL && rel[0] == 0)) {
            errno = ENOENT;
            return -1;
        }
    }
    if (p[0] && p[1] == ':')
        snprintf(dos, sizeof dos, "%s", p);
    else if (is_sep(p[0]))
        snprintf(dos, sizeof dos, "%c:%s", cwd_dos[0], p);
    else
        snprintf(dos, sizeof dos, "%s\\%s", cwd_dos, p);
    snprintf(cwd_dos, sizeof cwd_dos, "%s", dos);
    return 0;
}

char *port_getcwd(char *buf, int size)
{
    if (!buf)
        return SDL_strdup(cwd_dos);
    snprintf(buf, size, "%s", cwd_dos);
    return buf;
}

/* stat() fills the Watcom struct stat: st_mode (+8), st_size (+20), st_mtime (+28) */
int port_stat(const char *p, void *wst)
{
    char host[1024];
    hstat_t st;
    unsigned char *w = wst;
    unsigned short mode;

    if (!map_path(p, host, sizeof host, FOR_READ) || HSTAT(host, &st) != 0) {
        char rel[512];
        int root;
        split_dos(p, &root, rel, sizeof rel);
        if (root == ROOT_INSTALL && rel[0] == 0) {      /* the install folder itself */
            memset(w, 0, 0x30);
            *(unsigned short *)(w + 8) = 0x4000 | 0x1FF;
            return 0;
        }
        errno = ENOENT;
        return -1;
    }
    memset(w, 0, 0x30);
    mode = (S_ISDIR(st.st_mode) ? 0x4000 : 0x8000) | 0x124;      /* readable */
    mode |= 0x92;                                                /* writable: copies go to the user folder */
    *(unsigned short *)(w + 8) = mode;
    *(long *)(w + 20) = (long)st.st_size;
    *(long *)(w + 28) = (long)st.st_mtime;
    return 0;
}

DIR *port_opendir(const char *p)
{
    char host[1024];
    if (!p || !map_path(p, host, sizeof host, FOR_READ))
        return NULL;
    return opendir(host);
}

/* the game runs " D:" style commands to change drives; nothing is run here */
int port_system(const char *cmd)
{
    port_log("system(\"%s\") ignored\n", cmd ? cmd : "");
    return 0;
}

/* ------------------------------------------------------------------ Watcom DOS calls */
#pragma pack(push, 1)
typedef struct {
    char reserved[21];                  /* +0x00 our search handle in the first bytes */
    unsigned char attrib;               /* +0x15 */
    unsigned short wr_time, wr_date;    /* +0x16 +0x18 */
    long size;                          /* +0x1A */
    char name[13];                      /* +0x1E */
} w_find_t;
#pragma pack(pop)

typedef struct {
    int used;
    char pattern[64];
    unsigned attrs;
    char **names;                       /* every match, in order, without duplicates */
    char **hosts;
    int n, next;
} search;
static search searches[16];

static int wild_match(const char *pat, const char *s)
{
    for (; *pat; pat++, s++) {
        if (*pat == '*') {
            for (; ; s++) {
                if (wild_match(pat + 1, s))
                    return 1;
                if (!*s)
                    return 0;
            }
        }
        if (!*s)
            return 0;
        if (*pat != '?' && toupper((unsigned char)*pat) != toupper((unsigned char)*s))
            return 0;
    }
    return *s == 0;
}

/* DOS matching of 8.3 names: "*.*" matches names without a dot too */
static int dos_match(const char *pat, const char *name)
{
    if (strcmp(pat, "*.*") == 0)
        return 1;
    if (wild_match(pat, name))
        return 1;
    if (!strchr(name, '.')) {
        char n2[300];
        snprintf(n2, sizeof n2, "%s.", name);
        return wild_match(pat, n2);
    }
    return 0;
}

static void add_dir(search *s, const char *hostdir)
{
    DIR *d = opendir(hostdir);
    struct dirent *de;
    int i;

    if (!d)
        return;
    while ((de = readdir(d)) != NULL) {
        char host[1024];
        int dir = 0;
        if (de->d_name[0] == '.' || strlen(de->d_name) > 12 || !dos_match(s->pattern, de->d_name))
            continue;
        snprintf(host, sizeof host, "%s/%s", hostdir, de->d_name);
        if (!host_exists(host, &dir) || (dir && !(s->attrs & 0x10)))
            continue;
        for (i = 0; i < s->n; i++)
            if (SDL_strcasecmp(s->names[i], de->d_name) == 0)
                break;
        if (i < s->n)
            continue;
        s->names = realloc(s->names, (s->n + 1) * sizeof *s->names);
        s->hosts = realloc(s->hosts, (s->n + 1) * sizeof *s->hosts);
        s->names[s->n] = SDL_strdup(de->d_name);
        s->hosts[s->n] = SDL_strdup(host);
        s->n++;
    }
    closedir(d);
}

static void free_search(search *s)
{
    int i;
    for (i = 0; i < s->n; i++) {
        free(s->names[i]);
        free(s->hosts[i]);
    }
    free(s->names);
    free(s->hosts);
    memset(s, 0, sizeof *s);
}

static int fill_next(search *s, w_find_t *f)
{
    hstat_t st;
    const char *n;
    int i;

    if (s->next >= s->n)
        return 18;                      /* no more files */
    n = s->names[s->next];
    memset(f->name, 0, sizeof f->name);
    for (i = 0; n[i] && i < 12; i++)
        f->name[i] = (char)toupper((unsigned char)n[i]);
    f->size = 0;
    f->attrib = 0x20;
    f->wr_time = f->wr_date = 0;
    if (HSTAT(s->hosts[s->next], &st) == 0) {
        f->size = (long)st.st_size;
        if (S_ISDIR(st.st_mode))
            f->attrib = 0x10;
    }
    s->next++;
    return 0;
}

unsigned _dos_findfirst(const char *path, unsigned attrs, void *find)
{
    w_find_t *f = find;
    char dirpart[512], rel[512], host[1024];
    const char *base = path, *q;
    search *s;
    int root, h;

    for (q = path; *q; q++)
        if (is_sep(*q) || *q == ':')
            base = q + 1;
    snprintf(dirpart, sizeof dirpart, "%.*s", (int)(base - path), path);
    if (!dirpart[0])
        snprintf(dirpart, sizeof dirpart, ".");
    for (h = 0; h < 16 && searches[h].used; h++)
        ;
    if (h == 16) {                      /* the game never closes a search: reuse the oldest */
        h = 0;
        free_search(&searches[0]);
    }
    s = &searches[h];
    free_search(s);
    s->used = 1;
    s->attrs = attrs;
    snprintf(s->pattern, sizeof s->pattern, "%s", base);
    split_dos(dirpart, &root, rel, sizeof rel);
    if (root == ROOT_INSTALL) {
        if (join(port_user_dir, rel, host, sizeof host))
            add_dir(s, host);
        if (join(port_game_dir, rel, host, sizeof host))
            add_dir(s, host);
        else {                      /* as in map_path: kept with the CD's files */
            if (port_cd_dir[0] && join(port_cd_dir, rel, host, sizeof host))
                add_dir(s, host);
            if (cd_cache_dir[0] && join(cd_cache_dir, rel, host, sizeof host))
                add_dir(s, host);
        }
    } else {
        if (port_cd_dir[0] && join(port_cd_dir, rel, host, sizeof host))
            add_dir(s, host);
        if (cd_cache_dir[0] && join(cd_cache_dir, rel, host, sizeof host))
            add_dir(s, host);
        if (join(port_game_dir, rel, host, sizeof host))
            add_dir(s, host);
    }
    memset(f, 0, sizeof *f);
    f->reserved[0] = (char)(h + 1);
    h = fill_next(s, f);
    if (trace_files)
        port_log("_dos_findfirst(\"%s\") -> %d match(es)\n", path, s->n);
    if (h)
        free_search(s);
    return h;
}

unsigned _dos_findnext(void *find)
{
    w_find_t *f = find;
    int h = f->reserved[0] - 1;
    if (h < 0 || h >= 16 || !searches[h].used)
        return 18;
    if (fill_next(&searches[h], f)) {
        free_search(&searches[h]);
        return 18;
    }
    return 0;
}

unsigned _dos_findclose(void *find)
{
    w_find_t *f = find;
    int h = f->reserved[0] - 1;
    if (h >= 0 && h < 16)
        free_search(&searches[h]);
    return 0;
}

unsigned _dos_getfileattr(const char *path, unsigned short *attr)
{
    char host[1024];
    int dir = 0;
    if (!map_path(path, host, sizeof host, FOR_READ) || !host_exists(host, &dir))
        return 2;                       /* file not found */
    *attr = dir ? 0x10 : 0x20;
    return 0;
}

/* drive 0 = current, 1 = A: ...; plenty of room everywhere */
#pragma pack(push, 1)
typedef struct {
    unsigned short total_clusters, avail_clusters, sectors_per_cluster, bytes_per_sector;
} w_diskfree_t;
#pragma pack(pop)

unsigned _dos_getdiskfree(unsigned drive, void *df)
{
    w_diskfree_t *d = df;
    (void)drive;
    d->total_clusters = 0xFFFF;
    d->avail_clusters = 0xFFF0;
    d->sectors_per_cluster = 64;
    d->bytes_per_sector = 512;
    return 0;
}

/* ------------------------------------------------------------------ CD image
 * The Steam and GOG releases keep the CD as an image (ISO, or a BIN with a CUE sheet) that
 * DOSBox mounts; only the maps (MAPS) and a few extras are not also in the game folder (whose
 * MAPS folder is empty).  Whenever there is an image (next to the game folder, or named by
 * --cd), every file of it that the game folder lacks is copied once to the CD folder next to
 * tnfree.exe (cd_dest), which
 * then serves as the CD; the files the game folder has are the same as on the CD. */
typedef struct {
    FILE *f;
    long sector, skip;                  /* bytes per sector, bytes before the 2048 of data */
} cdimg_t;

static int cd_read(cdimg_t *c, unsigned long lba, unsigned char *buf, unsigned long n)
{
    while (n) {
        unsigned long k = n < 2048 ? n : 2048;
        if (fseek(c->f, (long)(lba * (unsigned long)c->sector + c->skip), SEEK_SET) != 0 ||
            fread(buf, 1, k, c->f) != k)
            return 0;
        buf += k;
        n -= k;
        lba++;
    }
    return 1;
}

static unsigned long le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned long)p[3] << 24; }

static int has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && SDL_strcasecmp(name + n - e, ext) == 0;
}

/* a CUE sheet (GOG names it .ins): the first FILE "x" BINARY, relative to the sheet */
static int cue_target(const char *cue, char *out, int size)
{
    FILE *f = fopen(cue, "r");
    char line[600], *a, *b;
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f))
        if ((a = strstr(line, "FILE \"")) != NULL && (b = strchr(a + 6, '"')) != NULL) {
            const char *slash = strrchr(cue, '/'), *bs = strrchr(cue, '\\');
            int dl;
            if (bs > slash)
                slash = bs;
            dl = slash ? (int)(slash - cue) : 1;
            *b = 0;
            snprintf(out, size, "%.*s/%s", dl, slash ? cue : ".", a + 6);
            fclose(f);
            return 1;
        }
    fclose(f);
    return 0;
}

static int cd_open(const char *path, cdimg_t *c)
{
    static const long layout[3][2] = { { 2048, 0 }, { 2352, 16 }, { 2352, 24 } };
    char bin[1100];
    unsigned char pvd[8];
    int i;

    if ((has_ext(path, ".cue") || has_ext(path, ".ins")) && cue_target(path, bin, sizeof bin))
        path = bin;
    c->f = fopen(path, "rb");
    if (!c->f)
        return 0;
    for (i = 0; i < 3; i++) {
        c->sector = layout[i][0];
        c->skip = layout[i][1];
        if (cd_read(c, 16, pvd, sizeof pvd) && pvd[0] == 1 && memcmp(pvd + 1, "CD001", 5) == 0)
            return 1;
    }
    fclose(c->f);
    return 0;
}

/* copies the files of directory (lba, len) to dest/rel, except those the game folder has with
 * the same size; returns the number of files copied, -1 on a read error */
static unsigned long cd_entries;        /* records read so far (P67) */

static int cd_copy_dir(cdimg_t *c, unsigned long lba, unsigned long len, const char *rel, int depth)
{
    unsigned char *dir, *r;
    unsigned long at;
    int copied = 0;

    if (depth > 8 || len > 1u << 20 || !(dir = malloc(len)))
        return -1;
    if (!cd_read(c, lba, dir, len)) {
        free(dir);
        return -1;
    }
    for (at = 0; at < len;) {
        char name[256], sub[512], host[1100];
        unsigned long elba, elen;
        int nl, isdir, k;
        hstat_t st;

        r = dir + at;
        if (r[0] == 0) {                /* records don't cross sectors: on to the next one */
            at = (at / 2048 + 1) * 2048;
            continue;
        }
        /* P67: a record's name must lie inside it (a damaged image read past the directory) */
        if (at + r[0] > len || r[0] < 34 || 33 + r[32] > r[0])
            break;
        at += r[0];
        nl = r[32];
        if (nl == 1 && (r[33] == 0 || r[33] == 1))
            continue;                   /* "." and ".." */
        if (++cd_entries > 100000) {    /* P67: a damaged image whose folders hold themselves */
            free(dir);
            return -1;
        }
        elba = le32(r + 2);
        elen = le32(r + 10);
        isdir = r[25] & 2;
        snprintf(name, sizeof name, "%.*s", nl, (const char *)r + 33);
        if (strchr(name, ';'))
            *strchr(name, ';') = 0;
        k = (int)strlen(name);
        if (k && name[k - 1] == '.')
            name[k - 1] = 0;
        /* P67: a name that would leave the CD folder ("..", "a/../..", "C:x") is not a file of
         * the game's CD: an image made to do so could write anywhere the player may */
        if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..") || strpbrk(name, "/\\:"))
            continue;
        snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", name);
        if (isdir) {
            k = cd_copy_dir(c, elba, elen, sub, depth + 1);
            if (k < 0) {
                free(dir);
                return -1;
            }
            copied += k;
            continue;
        }
        if (join(port_game_dir, sub, host, sizeof host) && HSTAT(host, &st) == 0 &&
            (unsigned long)st.st_size == elen)
            continue;                   /* the game folder has it */
        snprintf(host, sizeof host, "%s/%s", cd_cache_dir, sub);
        make_parents(host);
        {
            FILE *o = fopen(host, "wb");
            unsigned char *buf = malloc(64 * 2048);
            unsigned long left = elen, b = elba;
            int ok = o && buf;
            while (ok && left) {
                unsigned long n = left < 64 * 2048 ? left : 64 * 2048;
                ok = cd_read(c, b, buf, n) && fwrite(buf, 1, n, o) == n;
                left -= n;
                b += 64;
            }
            free(buf);
            if (o)
                fclose(o);
            if (!ok) {
                remove(host);
                free(dir);
                return -1;
            }
        }
        copied++;
    }
    free(dir);
    return copied;
}

/* a CD image in folder d (the first .iso, .cue, .ins, .gog, .bin or .img holding an ISO 9660
 * volume) */
static int find_image_in(const char *d, char *out, int size)
{
    static const char *const ext[] = { ".iso", ".cue", ".ins", ".gog", ".bin", ".img" };
    DIR *dir;
    struct dirent *de;
    int e, found = 0;

    for (e = 0; e < 6 && !found; e++) {
        if (!(dir = opendir(d)))
            return 0;
        while (!found && (de = readdir(dir)) != NULL) {
            cdimg_t c;
            if (!has_ext(de->d_name, ext[e]))
                continue;
            snprintf(out, size, "%s/%s", d, de->d_name);
            if (cd_open(out, &c)) {
                fclose(c.f);
                found = 1;
            }
        }
        closedir(dir);
    }
    return found;
}

static void cd_image_setup(void)
{
    char host[1100], img[1100] = "", up[1100], marker[1200], line[1200] = "";
    const char *where[4];
    cdimg_t c;
    unsigned char pvd[2048];
    int i, n, dir = 0;
    FILE *f;

    if (port_cd_dir[0] && host_exists(port_cd_dir, &dir) && !dir)
        snprintf(img, sizeof img, "%s", port_cd_dir);   /* --cd names the image itself */
    else {
        snprintf(up, sizeof up, "%s/..", port_game_dir);
        where[0] = port_cd_dir[0] ? port_cd_dir : port_game_dir;
        where[1] = port_game_dir;
        where[2] = up;
        where[3] = 0;
        for (i = 0; where[i] && !img[0]; i++)
            if (!find_image_in(where[i], img, sizeof img))
                img[0] = 0;
        if (!img[0]) {                  /* or one folder down from the parent (_ISO, CD, ...) */
            DIR *d = opendir(up);
            struct dirent *de;
            while (d && !img[0] && (de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.')
                    continue;
                snprintf(host, sizeof host, "%s/%s", up, de->d_name);
                if (host_exists(host, &dir) && dir && !find_image_in(host, img, sizeof img))
                    img[0] = 0;
            }
            if (d)
                closedir(d);
        }
    }
    if (!img[0]) {
        if (join(port_game_dir, "maps/lake.res", host, sizeof host) ||
            (port_cd_dir[0] && join(port_cd_dir, "maps/lake.res", host, sizeof host)))
            return;
        if (!port_cd_dir[0]) {          /* or the CD's files in a folder next to the game's (CD, ...) */
            DIR *d;
            struct dirent *de;
            snprintf(up, sizeof up, "%s/..", port_game_dir);
            d = opendir(up);
            while (d && !port_cd_dir[0] && (de = readdir(d)) != NULL) {
                char sib[1100];
                if (de->d_name[0] == '.')
                    continue;
                snprintf(sib, sizeof sib, "%s/%s", up, de->d_name);
                if (host_exists(sib, &dir) && dir && join(sib, "maps/lake.res", host, sizeof host)) {
                    char par[1024], *c, *cut = NULL;    /* named without the ".." where it can be */
                    snprintf(par, sizeof par, "%s", port_game_dir);
                    for (c = par; *c; c++)
                        if ((*c == '/' || *c == '\\') && c[1])
                            cut = c;
                    if (cut && strcmp(cut + 1, "..") && strcmp(cut + 1, ".")) {
                        *cut = 0;
                        snprintf(port_cd_dir, sizeof port_cd_dir, "%s/%s", par, de->d_name);
                    } else
                        snprintf(port_cd_dir, sizeof port_cd_dir, "%s", sib);
                }
            }
            if (d)
                closedir(d);
            if (port_cd_dir[0]) {
                port_log("CD: files in %s\n", port_cd_dir);
                return;
            }
        }
        port_log("CD: no CD image found (missions need the CD's MAPS folder: use --cd)\n");
        return;
    }
    snprintf(cd_cache_dir, sizeof cd_cache_dir, "%s", cd_dest);
    snprintf(marker, sizeof marker, "%s/freefall-cd.txt", cd_cache_dir);
    if ((f = fopen(marker, "r")) != NULL) {
        if (!fgets(line, sizeof line, f))
            line[0] = 0;
        fclose(f);
        line[strcspn(line, "\r\n")] = 0;
        if (strcmp(line, img) == 0) {
            port_log("CD: files from %s (copied before to %s)\n", img, cd_cache_dir);
            return;
        }
    }
    if (!cd_open(img, &c) || !cd_read(&c, 16, pvd, sizeof pvd)) {
        port_log("CD: cannot read %s\n", img);
        cd_cache_dir[0] = 0;
        return;
    }
    port_log("CD: copying the files the game folder lacks from %s to %s...\n", img, cd_cache_dir);
    host_mkdir(cd_cache_dir);
    cd_entries = 0;
    n = cd_copy_dir(&c, le32(pvd + 156 + 2), le32(pvd + 156 + 10), "", 0);
    fclose(c.f);
    if (n < 0) {
        port_log("CD: reading %s failed\n", img);
        cd_cache_dir[0] = 0;
        return;
    }
    if ((f = fopen(marker, "w")) != NULL) {
        fprintf(f, "%s\n", img);
        fclose(f);
    }
    port_log("CD: %d files copied\n", n);
}

/* ------------------------------------------------------------------ set up */
void port_files_init(const char *user_dir, const char *copy_dir)
{
    snprintf(port_user_dir, sizeof port_user_dir, "%s", user_dir);
    snprintf(cd_dest, sizeof cd_dest, "%s", copy_dir);
    {
        size_t n = strlen(port_user_dir);
        while (n > 1 && is_sep(port_user_dir[n - 1]))
            port_user_dir[--n] = 0;
    }
    host_mkdir(port_user_dir);
    trace_files = SDL_getenv("FREEFALL_TRACE_FILES") != NULL;
    port_log("Game folder: %s\nCD folder:   %s\nUser folder: %s (config, saves)\n", port_game_dir,
             port_cd_dir[0] ? port_cd_dir : "(the game folder)", port_user_dir);
    cd_image_setup();
}
