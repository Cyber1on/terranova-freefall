/* datapath.c -- x:\prj\tech\libsrc\dstruct\datapath.c (Terra Nova v1.09): search paths for
 * data files.  Decompiled from __FF.EXE.
 *
 *   0x2EF200 DatapathFree    0x2EF23C DatapathFOpen    0x2EF320 DatapathOpen (cdecl)
 *   0x2EF478 DatapathFind    0x2EF580 DatapathAdd      0x2EF664 datapath_add_one (static, name ours)
 *   0x2EF71C DatapathLastPath
 *   0x2EF735 strrchr (Watcom CRT, linked right after this module)
 *
 * A Datapath holds up to 16 directory strings, each ending in a separator.  Lookups try the
 * current directory first unless noCurrent is set, then each path in order; FOpen and Open
 * record 1 + the index of the path that worked in 'last' (DatapathFind does not).
 * Quirks: datapath_add_one reads path[-1] for an empty string and doesn't check the
 * allocation; DatapathAdd splits on '+' or ';' (whichever comes first).
 */
#include "sup_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
char *datapath_empty = "";
#endif

/* Frees the path strings and clears the structure. */
void DatapathFree(Datapath *pdp)
{
    int i;

    for (i = 0; i < pdp->numDatapaths; i++)
        if (pdp->datapath[i])
            (*f_free)(pdp->datapath[i]);
    memset(pdp, 0, sizeof(*pdp));
}

/* fopen() along the datapath.  Returns the FILE *, or NULL. */
void *DatapathFOpen(Datapath *pdp, char *fname, char *mode)
{
    char fullname[128];
    void *f;
    int i;

    pdp->last = 0;
    if (!pdp->noCurrent) {
        f = fopen(fname, mode);
        if (f)
            return f;
    }
    for (i = 0; i < pdp->numDatapaths; i++) {
        lg_strcpy(fullname, pdp->datapath[i]);
        lg_strcpy(fullname + lg_strlen(fullname), fname);
        f = fopen(fullname, mode);
        if (f) {
            pdp->last = i + 1;
            return f;
        }
    }
    return NULL;
}

/* open() along the datapath (cdecl; the permission argument is read only with O_CREAT).
 * Returns the handle, or -1. */
int DatapathOpen(Datapath *pdp, char *fname, int access, ...)
{
    char fullname[128];
    int perm = 0, fd, i;

    __builtin_va_list ap;

    __builtin_va_start(ap, access);
    if (access & O_CREAT)
        perm = __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    pdp->last = 0;
    if (!pdp->noCurrent) {
        if (access & O_CREAT)
            fd = open(fname, access, perm);
        else
            fd = open(fname, access);
        if (fd != -1)
            return fd;
    }
    for (i = 0; i < pdp->numDatapaths; i++) {
        lg_strcpy(fullname, pdp->datapath[i]);
        lg_strcpy(fullname + lg_strlen(fullname), fname);
        if (access & O_CREAT)
            fd = open(fullname, access, perm);
        else
            fd = open(fullname, access);
        if (fd != -1) {
            pdp->last = i + 1;
            return fd;
        }
    }
    return -1;
}

/* Looks for fname (_dos_findfirst, normal files) along the datapath; on success buf holds the
 * name that was found and the result is 1, else buf is "" and the result 0. */
int DatapathFind(Datapath *pdp, char *fname, char *buf)
{
    uchar find[0x2C];       /* struct find_t */
    int i;

    if (!pdp->noCurrent) {
        lg_strcpy(buf, fname);
        if (_dos_findfirst(buf, 0, find) == 0)
            return 1;
    }
    for (i = 0; i < pdp->numDatapaths; i++) {
        lg_strcpy(buf, pdp->datapath[i]);
        lg_strcpy(buf + lg_strlen(buf), fname);
        if (_dos_findfirst(buf, 0, find) == 0)
            return 1;
    }
    buf[0] = 0;
    return 0;
}

static inline char *find_char(char *s, char c)
{
    for (; *s != c; s++)
        if (*s == 0)
            return NULL;
    return s;
}

/* Adds one or more paths, separated by '+' or ';'.  Returns TRUE if any was added. */
bool DatapathAdd(Datapath *pdp, char *path)
{
    char buf[128];
    bool added = FALSE;
    char *p, *p2;
    int n;

    if (path == NULL)
        return FALSE;
    while (*path) {
        p = find_char(path, '+');
        p2 = find_char(path, ';');
        if (p == NULL)
            p = p2;
        else if (p2 && p2 < p)
            p = p2;
        if (p == NULL) {
            added |= datapath_add_one(pdp, path);
            break;
        }
        n = p - path;
        strncpy(buf, path, n);
        buf[n] = 0;
        added |= datapath_add_one(pdp, buf);
        path = p + 1;
    }
    return added;
}

/* Appends a copy of path (with a '\' added unless it ends in '/', '\' or ':'). */
bool datapath_add_one(Datapath *pdp, char *path)
{
    int len, sep;
    char c, *p;

    if (pdp->numDatapaths == DP_NUM_PATHS)
        return FALSE;
    if (path == NULL)
        return FALSE;
    len = lg_strlen(path);
    c = path[len - 1];
    sep = (c != '/' && c != '\\' && c != ':');
    p = (*f_malloc)(len + sep + 1);
    pdp->datapath[pdp->numDatapaths] = p;
    lg_strcpy(p, path);
    if (sep)
        lg_strcpy(p + lg_strlen(p), "\\");
    pdp->numDatapaths++;
    return TRUE;
}

/* The path that the last FOpen/Open succeeded with, or "". */
char *DatapathLastPath(Datapath *pdp)
{
    if (pdp->last)
        return pdp->datapath[pdp->last - 1];
    return datapath_empty;
}

/* Watcom CRT strrchr: last occurrence of c (the terminator counts), or NULL. */
char *strrchr(const char *s, int c)
{
    const char *r = NULL;

    do {
        if ((char)c == *s)
            r = s;
    } while (*s++);
    return (char *)r;
}
