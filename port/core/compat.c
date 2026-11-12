/* compat.c -- DOS / Watcom C library calls the game uses that only Windows' C library has.
 * Used by the Linux build (handy for testing; macOS / Linux ports come later). */
#ifndef _WIN32
#define _FILE_OFFSET_BITS 64        /* fstat() of files with 64-bit inode numbers */
#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int chsize(int fd, long size) { return ftruncate(fd, size); }

long tell(int fd) { return lseek(fd, 0, SEEK_CUR); }

long filelength(int fd)
{
    struct stat st;
    return fstat(fd, &st) == 0 ? (long)st.st_size : -1;
}

int eof(int fd)
{
    long len = filelength(fd), pos = tell(fd);
    if (len < 0 || pos < 0)
        return -1;
    return pos >= len;
}

int getch(void) { return getchar(); }

int strnicmp(const char *a, const char *b, unsigned long n)
{
    for (; n; n--, a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y)
            return x - y;
        if (!x)
            break;
    }
    return 0;
}

int strcmpi(const char *a, const char *b) { return strnicmp(a, b, (unsigned long)-1); }
#endif
