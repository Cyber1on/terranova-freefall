/* fileutil.c -- paths, numbered file names, bug report check, time strings (M107, no assert
 * path; names are the map's), Terra Nova v1.09.  Decompiled from __FF.EXE.
 *
 *   0x2CD3F0 fileutil_2CD3F0   fopen(dir\name) after giving name the first free number
 *   0x2CD438 fileutil_2CD438   bump the digits in name (octal!) while dir\name exists
 *   0x2CD4EC fileutil_2CD4EC   delete non-empty numbered files (unreferenced)
 *   0x2CD658 fileutil_2CD658   copy a file handle's contents into a FILE
 *   0x2CD6CC fileutil_2CD6CC   "mm:ss" of game_time (minutes mod 100)
 *   0x2CD71C fileutil_2CD71C   "You have bugs to report" check of GEN_DIR\*.bug (unreferenced)
 *   0x2CD7B4 fileutil_2CD7B4   TN_DIR, else GEN_DIR, else "."
 *   0x2CD804 fileutil_2CD804   seconds -> hours % 26, minutes, seconds; returns hours / 26
 *
 * Quirks kept: numbers are written in octal (tnova007.rec is followed by tnova010.rec);
 * fileutil_2CD7B4 takes strlen of the TN_DIR value even when it is NULL, and never closes the
 * directory it opens; fileutil_2CD4EC leaks the handle of an empty file and deletes the
 * non-empty ones; fileutil_2CD71C copies GEN_DIR without a NULL check.
 */
#include "g1_c.h"

static const char digits[] = "0123456789";

static void put_octal(char *last, int ndig, ulong v)
{
    int k, sh = 0;
    for (k = 0; k < ndig; k++, sh += 3)
        last[-k] = ((long)v >> sh & 7) + '0';
}

void *fileutil_2CD3F0(char *dir, char *name, char *mode)
{
    Datapath dp;
    void *fp;

    dp.numDatapaths = 0;
    dp.noCurrent = 1;
    datapath_add_one(&dp, dir);
    dp.noCurrent = 1;
    fileutil_2CD438(&dp, name);
    fp = DatapathFOpen(&dp, name, mode);
    DatapathFree(&dp);
    return fp;
}

char *fileutil_2CD438(Datapath *dp, char *name)
{
    char buf[0x64];
    char *p, *q;
    int ndig = 1;
    long v;

    p = strpbrk(name, digits);
    if (!p)
        return name;
    v = *p - '0';
    for (q = p + 1; strpbrk(q, digits) == q; q++, ndig++)
        v = v * 8 + (*q - '0');
    while (DatapathFind(dp, name, buf) == 1) {
        v++;
        if (ndig > 0)
            put_octal(p + ndig - 1, ndig, v);
    }
    return name;
}

void fileutil_2CD4EC(char *dir, char *name)
{
    char full[0x64];
    Datapath dp;
    char buf[0x14];
    char *p;
    int ndig = 1, fd;
    long count = 0;

    g1c_strcpy(buf, name);
    p = strrchr(buf, '0');
    dp.numDatapaths = 0;
    dp.noCurrent = 1;
    datapath_add_one(&dp, dir);
    dp.noCurrent = 1;
    if (!p)
        return;
    while (g1c_strlen(p) != g1c_strlen(buf) && *p == p[-1]) {
        p--;
        ndig++;
    }
    while (DatapathFind(&dp, buf, full) == 1) {
        if (DatapathFind(&dp, buf, full)) {
            fd = open(full, 0x100);
            if (fd != -1 && filelength(fd) != 0) {
                close(fd);
                remove(full);
            }
        }
        count++;
        if (ndig > 0)
            put_octal(p + ndig - 1, ndig, count);
    }
}

bool fileutil_2CD658(void *fp, int fd)
{
    void *buf;
    int n;

    buf = (*f_malloc)(0x10000);
    if (!buf)
        return 0;
    lseek(fd, 0, 0);
    while ((n = read(fd, buf, 0x10000)) > 0)
        fwrite(buf, n, 1, fp);
    (*f_free)(buf);
    return 1;
}

void fileutil_2CD6CC(char *buf)
{
    long t = game_time / 1000;
    long m = t / 60, s = t - m * 60;

    if (m > 100)
        do
            m -= 100;
        while (m > 100);
    lg_sprintf(buf, "%02d:%02d", m, s);
}

void fileutil_2CD71C(void)
{
    char path[0x50];
    uchar find[0x2C];

    g1c_strcpy(path, getenv("GEN_DIR"));
    g1c_strcat(path, "/*.bug");
    if (_dos_findfirst(path, 0, find) == 0) {
        printf("\nYou have bugs to report!\n");
        printf("Please type 'tnbug' to register them.\n");
    }
}

char *fileutil_2CD7B4(void)
{
    char *tn, *d;

    d = tn = getenv("TN_DIR");
    if (!tn)
        d = getenv("GEN_DIR");
    if (!opendir(d))
        d = fileutil_default_dir;
#ifndef TN_WATCOM_ABI
    if (!tn)
        tn = "";        /* DOS read the bytes at address 0; the port has nothing mapped there */
#endif
    if ((ulong)g1c_strlen(tn) > 0x32)
        d = fileutil_default_dir;
    return d;
}

long fileutil_2CD804(ulong t, char *pday_hour, char *pmin, char *psec)
{
    char hours = t / 3600;

    *pmin = (t - hours * 3600) / 60;
    *psec = t % 60;
    *pday_hour = hours % 26;
    return hours / 26;
}
