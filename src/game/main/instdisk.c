/* instdisk.c -- resource cache: directories and disk space, module M125 (Terra Nova v1.09).
 *
 *   0x2DD930 / 0x2DD938      8-byte diskfree templates (data in the code segment)
 *   0x2DD940 install_2DD940  critical-error handler: sets install_crit_err, answers "fail" (asm)
 *   0x2DD950 install_2DD950  install it (_harderr)
 *   0x2DD964 install_2DD964  make a directory and its parents (no callers)
 *   0x2DDA9C install_2DDA9C  check the drive root of a path
 *   0x2DDAF4 install_2DDAF4  is a directory writable (writes writetmp.tmp)
 *   0x2DDC30 install_2DDC30  same, falling back to the drive root
 *   0x2DDC40 install_2DDC40  _dos_getdiskfree on the drive of a path
 *   0x2DDCC4 install_2DDCC4  free KB          0x2DDD18 install_2DDD18  total KB (no callers)
 */
#include "g9_b.h"

#ifdef TN_WATCOM_ABI
/* far handler (called by the DOS extender, returns with retf): mov [crit], 1; mov eax, 3 */
__asm__(".text\n\t.globl install_2DD940\n\t.type install_2DD940, @function\n"
        "install_2DD940:\n\tmovb $1, install_crit_err\n\tmovl $3, %eax\n\tlret\n");

void install_2DD950(void)
{
    unsigned cs;
    __asm__("mov %%cs, %0" : "=r"(cs));
    _harderr((void *)install_2DD940, cs);
}
#else
/* Standalone build: there is no DOS critical-error handler to install; a failed disk access
 * just returns an error to the caller (what the handler's "fail" answer did). */
void install_2DD940(void) { install_crit_err = 1; }
void install_2DD950(void) { }
#endif

static char *find_bs(char *s)
{
    for (; *s; s++)
        if (*s == '\\')
            return s;
    return 0;
}

uchar install_2DD964(char *path)
{
    char save[0x80], buf[0x6c];
    char *start, *p, c;
    uchar last, keep;
    int r;

    getcwd(save, 0x69);
    r = mkdir(path);
    if (install_crit_err) {
        install_crit_err = 0;
        return 0;
    }
    if (r != 0) {
        g1c_strcpy(buf, path);
        start = buf;
        last = 0;
        for (;;) {
            keep = 0;
            p = find_bs(start);
            if (!p) {
                p = start + strlen(start);
                last = 1;
                if (p == start)
                    break;
            }
            if (p > start && p[-1] == ':')
                keep = 1;
            c = p[keep];
            p[keep] = 0;
            if (chdir(buf) != 0) {
                r = mkdir(buf);
                if (install_crit_err) {
                    install_crit_err = 0;
                    return 0;
                }
                if (r != 0)
                    return 0;
            }
            p[keep] = c;
            start = p + 1;
            if (last)
                break;
        }
    }
    chdir(save);
    return 1;
}

long install_2DDA9C(char *path)
{
    char *p = find_bs(path);
    char c = p[1];
    long r;

    p[1] = 0;
    r = install_2DDAF4(path, 0);
    p[1] = c;
    if (r == 1)
        return -2;
    if (r == 0)
        return 1;
    return r;
}

/* 0 writable directory, 1 missing (or, with up, the drive root's answer), -1 read-only or
 * not writable, -2 not a directory / critical error */
long install_2DDAF4(char *path, uchar up)
{
    uchar st[0x48];
    char buf[0x6c];
    ushort mode;
    int r, fd;

    r = stat(path, st);
    if (install_crit_err) {
        install_crit_err = 0;
        return -2;
    }
    if (r == -1)
        return up ? install_2DDA9C(path) : 1;
    mode = *(ushort *)(st + 8);
    if ((mode & 0xf000) != 0x4000)
        return -2;
    if (!(st[8] & 0x80))
        return -1;
    g1c_strcpy(buf, path);
    g1c_strcat(buf, "writetmp.tmp");
    fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC, S_IREAD | S_IWRITE);
    if (install_crit_err) {
        install_crit_err = 0;
        return -1;
    }
    if (fd == -1)
        return -1;
    close(fd);
    remove(buf);
    return 0;
}

long install_2DDC30(char *path)
{
    return install_2DDAF4(path, 1);
}

/* drive -1: the drive of path.  Switches the current drive to path's (system("X:")) around
 * the call and back to the drive of the current directory. */
void install_2DDC40(long drive, char *path, G9bDiskFree *df)
{
    char cwd[0x80];
    char was;

    getcwd(cwd, 0x69);
    was = cwd[0];
    if (path) {
        memcpy(cwd, " :", 3);
        cwd[0] = path[0];
        system(cwd);
        if (drive == -1)
            drive = tolower(path[0]) - 0x60;
    }
    _dos_getdiskfree(drive, df);
    if (path) {
        memcpy(cwd, " :", 3);
        cwd[0] = was;
        system(cwd);
    }
}

long install_2DDCC4(long drive, char *path)
{
    G9bDiskFree df;
    memcpy(&df, install_2DD930, sizeof(df));
    install_2DDC40(drive, path, &df);
    return (long)(df.avail_clusters / 32) * df.sectors_per_cluster * (long)(df.bytes_per_sector / 32);
}

long install_2DDD18(long drive, char *path)
{
    G9bDiskFree df;
    memcpy(&df, install_2DD938, sizeof(df));
    install_2DDC40(drive, path, &df);
    return (long)(df.total_clusters / 32) * df.sectors_per_cluster * (long)(df.bytes_per_sector / 32);
}
