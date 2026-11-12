/* log.c -- diagnostics of the standalone build: messages go to stderr and to tnfree.log next
 * to tnfree.exe. */
#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "port.h"

static FILE *log_file;

static void vlog(const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    vfprintf(stderr, fmt, ap);
    if (!log_file) {
        char p[1100];
        const char *b = SDL_GetBasePath();
        snprintf(p, sizeof p, "%stnfree.log", b ? b : "");
        log_file = fopen(p, "w");
    }
    if (log_file) {
        vfprintf(log_file, fmt, ap2);
        fflush(log_file);
    }
    va_end(ap2);
}

void port_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

void port_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
    exit(3);
}

void port_stub_hit(const char *name, unsigned long addr, const char *library)
{
    port_log("not ported yet: %s (0x%06lX, %s) called; returning 0\n", name, addr, library);
}
