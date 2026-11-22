/* ailog.c -- the C library side of port/mods/ai.c and port/test/aitest.c (the game's headers
 * declare their own fopen and the like, so those files cannot include stdio.h): the AI test
 * log's file (FREEFALL_AI_LOG), formatted text, and the keys of FREEFALL_AI_TEST. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *log_file;
static int log_tried;

int port_ai_logging(void)
{
    if (!log_tried) {
        const char *s = getenv("FREEFALL_AI_LOG");
        log_tried = 1;
        if (s && *s)
            log_file = fopen(s, "w");
    }
    return log_file != NULL;
}

void port_ai_logf(const char *fmt, ...)
{
    va_list ap;
    if (!port_ai_logging())
        return;
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    va_end(ap);
    fflush(log_file);
}

int port_ai_fmt(char *buf, unsigned n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

/* FREEFALL_AI_TEST="key=value,...": 1 when it is set */
int port_aitest_set(void)
{
    const char *s = getenv("FREEFALL_AI_TEST");
    return s && *s;
}

/* ... the value of key (def without it) */
long port_aitest_opt(const char *key, long def)
{
    const char *p = getenv("FREEFALL_AI_TEST");
    size_t n = strlen(key);
    while (p && *p) {
        if (!strncmp(p, key, n) && p[n] == '=')
            return atol(p + n + 1);
        p = strchr(p, ',');
        if (p)
            p++;
    }
    return def;
}

/* the game's heading (0x10000 a turn, forward (cos, sin)) from a place towards dx, dy */
unsigned port_aitest_heading(long dx, long dy)
{
    return (unsigned)(long)(atan2((double)dy, (double)dx) * (65536 / 6.283185307179586)) & 0xFFFF;
}
