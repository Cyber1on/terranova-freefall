/* instpath.c -- resource cache: paths and file lists, module M123 (Terra Nova v1.09).
 *
 * The game can run from the CD and copy the big resource files (radio speech, resbase.res,
 * restnobj.res) to a hard disk directory while it runs.  install_cd_dir / install_targ_dir hold
 * the two directories of the current copy pass.
 *
 *   0x2DC9C0 install_2DC9C0  malloc'd "dir\name"       0x2DCAA4 install_2DCAA4  in cd / targ dir
 *   0x2DCABC install_2DCABC  set the two directories   0x2DCB0C install_2DCB0C  file in targ dir?
 *   0x2DCB38 install_2DCB38  delete a file in targ     0x2DCB60 install_2DCB60  start one copy
 *   0x2DCBE4 install_2DCBE4  look a name up in a list  0x2DCC24 install_2DCC24  scan the target
 */
#include "g9_b.h"
#include "g9_c.h"     /* added at the G9/G10 merge */

char *install_2DC9C0(char *dir, char *name)
{
    size_t n = strlen(dir);
    uchar sep = dir[n - 1] != '\\';
    char *p = Malloc(strlen(dir) + strlen(name) + 1 + sep);
    g1c_strcpy(p, dir);
    if (sep)
        g1c_strcat(p, "\\");
    g1c_strcat(p, name);
    return p;
}

char *install_2DCAA4(long hd, char *name)
{
    return install_2DC9C0(hd ? install_targ_dir : install_cd_dir, name);
}

void install_2DCABC(char *cd, char *targ)
{
    g1c_strcpy(install_cd_dir, cd);
    g1c_strcpy(install_targ_dir, targ);
}

uchar install_2DCB0C(char *name)
{
    char *p = install_2DC9C0(install_targ_dir, name);
    uchar r = access(p, 0) == 0;
    Free(p);
    return r;
}

uchar install_2DCB38(char *name)
{
    char *p = install_2DC9C0(install_targ_dir, name);
    uchar r = remove(p) == 0;
    Free(p);
    return r;
}

/* 1 when the file is already in the target directory; else starts copying it from the CD
 * (*job = the copy, 0 when the CD has no such file) and returns 0. */
uchar install_2DCB60(InstallJob **job, char *name)
{
    uchar there = 0;
    char *src = 0, *dst;

    *job = 0;
    dst = install_2DC9C0(install_targ_dir, name);
    if (access(dst, 0) == 0)
        there = 1;
    else {
        src = install_2DC9C0(install_cd_dir, name);
        if (access(src, 0) == 0)
            *job = install_2DC760(src, dst, -1);
    }
    if (src)
        Free(src);
    if (dst)
        Free(dst);
    return there;
}

long install_2DCBE4(InstallFile *list, char *name)
{
    long i;
    for (i = 1; i <= list[0].size; i++)
        if (strcmpi(list[i].name, name) == 0)
            return i;
    return 0;
}

/* Marks the listed files already in the target directory, deletes the other files there that
 * match the list's pattern, and returns the bytes still to copy (sizes read on the CD). */
long install_2DCC24(InstallFile *list)
{
    G9bFind f;
    long total = 0, i;
    char *p;
    int r;
    int fd;

    for (i = 1; i <= list[0].size; i++) {
        list[i].err = 0;
        list[i].found = list[i].err;
    }
    p = install_2DC9C0(install_targ_dir, list[0].name);
    r = _dos_findfirst(p, 0, &f);
    Free(p);
    if (r == 0) {
        do {
            i = install_2DCBE4(list, f.name);
            if (i) {
                list[i].found = 1;
                list[i].size = f.size;
            } else {
                p = install_2DC9C0(install_targ_dir, f.name);
                remove(p);
                Free(p);
            }
        } while (_dos_findnext(&f) == 0);
    }
    for (i = 1; i <= list[0].size; i++) {
        if (list[i].found)
            continue;
        p = install_2DC9C0(install_cd_dir, list[i].name);
        fd = open(p, O_BINARY);
        if (fd == -1) {
            list[i].err = 1;
            list[i].size = fd;
        } else {
            list[i].size = filelength(fd);
            total += list[i].size;
            close(fd);
        }
        Free(p);
    }
    return total;
}
