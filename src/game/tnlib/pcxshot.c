/* pcxshot.c -- Terra Nova PCX screenshots of the current canvas (names ours).  Decompiled from
 * __FF.EXE.
 *
 *   0x2EAF00 pcx_open_next     0x2EB070 pcx_screenshot     0x2EB090 pcx_write_canvas
 *   0x2EB184 pcx_write_line
 *
 * pcx_open_next finds the first <base>NNN.<ext> that doesn't exist (open for reading fails)
 * and creates it.  The file is a version 5, 8 bit, RLE PCX of grd_canvas read through the
 * pixel table, then 0x0C and the 768-byte grd_pal (6-bit values, written as they are).
 * Quirks kept: the header's palette / filler bytes (0x10-0x40, 0x46-0x7F) are never set, so
 * the original writes stack garbage there (we write whatever our stack holds); pcx_write_line
 * reads one pixel past the end of each run and of the line; pcx_screenshot treats fd 0 as a
 * failure and ignores pcx_write_canvas's result; the name buffer (0x90 bytes) is not checked.
 */
#include "t2_b.h"

int pcx_open_next(char *namebuf, char *base, char *ext)
{
    char name[0x90];
    int len = strlen(base), n = 0, fd;

    t2b_strcpy(name, base);
    t2b_strcat(name, "000.");
    t2b_strcat(name, ext);
    while ((fd = open(name, O_RDONLY | O_BINARY)) != -1) {
        n++;
        close(fd);
        name[len + 2] = '0' + n % 10;
        name[len + 1] = '0' + n / 10 % 10;
        name[len] = '0' + n / 100 % 10;
    }
    fd = open(name, O_BINARY | O_CREAT | O_WRONLY, S_IREAD | S_IWRITE);
    if (fd != -1 && namebuf != NULL)
        t2b_strcpy(namebuf, name);
    return fd;
}

int pcx_screenshot(char *namebuf, char *base)
{
    int fd = pcx_open_next(namebuf, base, "pcx");

    if (fd <= 0)
        return -1;
    pcx_write_canvas(fd);
    return 0;
}

/* Writes the canvas as a PCX and closes the file; -1 if a line was cut short. */
int pcx_write_canvas(int fd)
{
    uchar hdr[0x80];            /* only these fields are set (see the header comment) */
    uchar term;
    grs_canvas *c = grd_canvas;
    int y, w;

    hdr[0] = 10;                /* manufacturer */
    hdr[1] = 5;                 /* version */
    hdr[2] = 1;                 /* RLE */
    hdr[3] = 8;                 /* bits per pixel */
    *(short *)(hdr + 4) = 0;
    *(short *)(hdr + 6) = 0;
    *(short *)(hdr + 8) = c->bm.w - 1;
    *(short *)(hdr + 0xA) = c->bm.h - 1;
    *(short *)(hdr + 0xC) = c->bm.w;
    *(short *)(hdr + 0xE) = c->bm.h;
    hdr[0x41] = 1;              /* planes */
    *(short *)(hdr + 0x42) = c->bm.w;
    *(short *)(hdr + 0x44) = 0;
    write(fd, hdr, 0x80);
    w = *(short *)(hdr + 0x42);
    for (y = 0; y < grd_canvas->bm.h; y++)
        if (pcx_write_line(y, fd, w) != w)
            return -1;
    term = 0x0C;
    write(fd, &term, 1);
    write(fd, grd_pal, 0x300);
    close(fd);
    return 0;
}

#define GET_PIXEL(x, y) WATCOM_CALL(grd_pixel_table[3], (long)(short)(x), (long)(short)(y))

/* RLE-encodes one line (runs of up to 63); returns the x it stopped at. */
int pcx_write_line(int y, int fd, int w)
{
    int x = 0, run;
    uchar c, b;

    do {
        c = (uchar)GET_PIXEL(x, y);
        run = 1;
        x++;
        while ((ulong)c == (ulong)GET_PIXEL(x, y) && run < 0x3F && x < w) {
            run++;
            x++;
        }
        if (run > 1 || c > 0x3F) {
            b = 0xC0 + run;
            write(fd, &b, 1);
        }
        write(fd, &c, 1);
    } while (x < w);
    return x;
}
