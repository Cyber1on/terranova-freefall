/* filecopy.c -- file copy jobs used by the CD -> hard disk resource cache (M122,
 * 0x2DC5B0-0x2DC9BF; names are the map's), Terra Nova v1.09.  Decompiled from __FF.EXE.
 *
 *   0x2DC5B0 install_2DC5B0   set the shared copy buffer
 *   0x2DC5BC install_2DC5BC   strdup into *dst (bool)
 *   0x2DC610 install_2DC610   close a job: close both files, remove the target if asked, free
 *   0x2DC674 install_2DC674   open a job: 0, -1 source missing, -2 target, -3 no memory
 *   0x2DC760 install_2DC760   malloc'd job, NULL on error; chunk -1 = whole buffer
 *   0x2DC7C8 install_2DC7C8   copy one chunk: 1 done, 0 more, -4 read, -5 write, -6 buffer small
 *   0x2DC85C install_2DC85C   copy until done or an error (unreferenced)
 *   0x2DC88C install_2DC88C   close a job, returns 0
 *   0x2DC89C install_2DC89C   copy a whole file with a buffer of up to 256 KB (unreferenced)
 *
 * Quirks kept: install_2DC760 sets flags = 1 before install_2DC674 clears it, so a job is never
 * freed by install_2DC610 (callers leak it); a failed name copy in install_2DC674 removes
 * job->dst = NULL; install_2DC89C only gives back the old buffer when there was one (with no
 * buffer before, the new one stays installed).
 */
#include "g9_c.h"

#ifndef TN_LINK_ORIGINAL_DATA
void *copy_buf;
long copy_buf_size;
#endif

#define O_RDONLY_BIN 0x200          /* Watcom O_RDONLY | O_BINARY */
#define O_CREAT_BIN  0x261          /* O_WRONLY | O_CREAT | O_TRUNC | O_BINARY */

void install_2DC5B0(void *buf, long size)
{
    copy_buf = buf;
    copy_buf_size = size;
}

static uchar copy_strdup(char **dst, char *s)
{
    char *p = f_malloc(strlen(s) + 1);
    *dst = p;
    if (!p)
        return 0;
    g1c_strcpy(p, s);
    return 1;
}

uchar install_2DC5BC(char **dst, char *s)
{
    return copy_strdup(dst, s);
}

void install_2DC610(CopyJob *job, long del)
{
    if (job->fd_in != -1)
        close(job->fd_in);
    if (job->fd_out != -1)
        close(job->fd_out);
    if ((uchar)del)
        remove(job->dst);
    if (job->src)
        f_free(job->src);
    if (job->dst)
        f_free(job->dst);
    if (job->flags & 1)
        f_free(job);
}

long install_2DC674(CopyJob *job, char *src, char *dst)
{
    long r;

    job->flags = 0;
    job->dst = NULL;
    job->src = job->dst;
    job->fd_out = -1;
    job->fd_in = job->fd_out;
    if (!copy_strdup(&job->src, src) || !install_2DC5BC(&job->dst, dst))
        r = -3;
    else if ((job->fd_in = open(job->src, O_RDONLY_BIN)) == -1)
        r = -1;
    else {
        job->size = filelength(job->fd_in);
        job->left = job->size;
        job->fd_out = open(job->dst, O_CREAT_BIN, 0x80);
        r = job->fd_out == -1 ? -2 : 0;
    }
    if (r)
        install_2DC610(job, 1);
    return r;
}

InstallJob *install_2DC760(char *src, char *dst, long chunk)
{
    CopyJob *job = f_malloc(sizeof(CopyJob));

    if (!job)
        return NULL;
    job->flags = 1;
    if (install_2DC674(job, src, dst) < 0) {
        f_free(job);
        return NULL;
    }
    if (chunk == -1)
        chunk = copy_buf_size > 0 ? copy_buf_size : 0x10000;
    job->chunk = chunk;
    return (InstallJob *)job;
}

long install_2DC7C8(InstallJob *j)
{
    CopyJob *job = (CopyJob *)j;
    long n = job->chunk;

    if (job->left <= 0)
        return 1;
    if (n > job->left)
        n = job->left;
    if (n > copy_buf_size)
        return -6;
    if (read(job->fd_in, copy_buf, n) != n)
        return -4;
    if (write(job->fd_out, copy_buf, n) != n)
        return -5;
    job->left -= n;
    if (job->left <= 0)
        return 1;
    return 0;
}

long install_2DC85C(CopyJob *job)
{
    long r = 0;

    while (r >= 0 && job->left != 0)
        r = install_2DC7C8((InstallJob *)job);
    return r;
}

/* Returns 0 (xor eax, eax; no caller reads it). */
long install_2DC88C(InstallJob *job, long del)
{
    install_2DC610((CopyJob *)job, (uchar)del);
    return 0;
}

long install_2DC89C(char *src, char *dst)
{
    CopyJob job;
    void *buf, *old_buf = NULL;
    long old_size = 0, r;

    r = install_2DC674(&job, src, dst);
    if (r)
        return r;
    job.chunk = job.size < 0x40000 ? job.size : 0x40000;
    if (job.chunk > copy_buf_size) {
        buf = f_malloc(job.chunk);
        if (!buf && job.chunk > 0x1000) {
            job.chunk = 0x1000;
            if (copy_buf_size < 0x1000)
                buf = f_malloc(0x1000);
            else {
                job.chunk = copy_buf_size;
                buf = copy_buf;
            }
        }
    } else
        buf = copy_buf;
    if (!buf) {
        install_2DC610(&job, 1);
        return -3;
    }
    if (buf != copy_buf) {
        old_size = copy_buf_size;
        old_buf = copy_buf;
        copy_buf = buf;
        copy_buf_size = job.chunk;
    }
    r = 0;
    while (r >= 0 && job.left != 0)
        r = install_2DC7C8((InstallJob *)&job);
    install_2DC610(&job, r < 0);
    if (old_buf) {
        f_free(copy_buf);
        copy_buf = old_buf;
        copy_buf_size = old_size;
    }
    return r;
}
