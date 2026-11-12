/* gameart.c -- the game's own art read on the main thread (the Mission Selector, missions.c, and
 * the options screen, options.c): the Looking Glass resource files, their bitmaps and fonts, with
 * a reader of our own (the main thread must not use the game's resource system) */
#include <SDL3/SDL.h>
#include <string.h>
#include "port.h"
#include "platform.h"
#include "gameart.h"

/* ------------------------------------------------------------------ the game's resource files
 * (the Looking Glass resource format: a directory of 10-byte entries; compound resources hold
 * items; LZW-compressed ones are expanded here) */
uint32_t ga_rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
unsigned ga_rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

int ga_res_open(GaRes *f, const char *dos)
{
    char host[1100];
    port_path(dos, host, sizeof host);
    f->d = SDL_LoadFile(host, &f->n);
    if (f->d && (f->n < 0x80 || ga_rd32(f->d + 0x7C) + 6 > f->n)) {
        SDL_free(f->d);
        f->d = NULL;
    }
    return f->d != NULL;
}

void ga_res_close(GaRes *f)
{
    SDL_free(f->d);
    f->d = NULL;
}

/* the 14-bit LZW of the resource library (src/res/lzw.c), into out[size] */
static void ga_lzw_expand(const uint8_t *src, size_t n, uint8_t *out, uint32_t size)
{
    static unsigned short prefix[0x4000];
    static uint8_t append[0x4000], stack[0x4000];
    uint32_t buf = 0, o = 0;
    int cnt = 0, next = 256, old, code, ch, sp;
    size_t p = 0;
#define CODE(v) do { while (cnt <= 24) { buf |= (uint32_t)(p < n ? src[p] : 0) << (24 - cnt); p++; cnt += 8; } \
                     v = (int)(buf >> 18); buf <<= 14; cnt -= 14; } while (0)
    CODE(old);
    if (old > 255)
        return;
    ch = old;
    if (o < size)
        out[o++] = (uint8_t)old;
    for (;;) {
        CODE(code);
        if (code == 0x3FFF || p > n + 4)
            break;
        if (code == 0x3FFE) {           /* the table starts again */
            next = 256;
            CODE(old);
            if (old > 255)
                break;
            ch = old;
            if (o < size)
                out[o++] = (uint8_t)old;
            continue;
        }
        if (code > next)
            break;
        sp = 0;
        {
            int c = code;
            if (code >= next) {
                stack[sp++] = (uint8_t)ch;
                c = old;
            }
            while (c > 255 && sp < 0x3FFF) {
                stack[sp++] = append[c];
                c = prefix[c];
            }
            stack[sp++] = (uint8_t)c;
        }
        ch = stack[sp - 1];
        while (sp > 0 && o < size)
            out[o++] = stack[--sp];
        if (next <= 0x3FFD) {
            prefix[next] = (unsigned short)old;
            append[next] = (uint8_t)ch;
            next++;
        }
        old = code;
    }
#undef CODE
}

/* resource id: a copy (SDL_free it), its size and whether it is compound */
uint8_t *ga_res_read(const GaRes *f, int id, uint32_t *size, int *compound)
{
    uint32_t off, pos, i, cnt;
    if (!f->d)
        return NULL;
    off = ga_rd32(f->d + 0x7C);
    cnt = ga_rd16(f->d + off);
    pos = ga_rd32(f->d + off + 2);
    for (i = 0; i < cnt && off + 6 + i * 10 + 10 <= f->n; i++) {
        const uint8_t *e = f->d + off + 6 + i * 10;
        uint32_t a = ga_rd32(e + 2), b = ga_rd32(e + 6), sz = a & 0xFFFFFF, csz = b & 0xFFFFFF, fl = a >> 24;
        if ((int)ga_rd16(e) == id) {
            uint8_t *out;
            if ((size_t)pos + csz > f->n || sz > (64u << 20))
                return NULL;
            out = SDL_calloc(1, sz + 16);
            if (!out)
                return NULL;
            if ((fl & 3) == 3) {        /* compound: the table of items is not compressed */
                uint32_t t = csz >= 2 ? 2 + 4 * (ga_rd16(f->d + pos) + 1) : 0;
                if (!t || t > csz || t > sz) {
                    SDL_free(out);
                    return NULL;
                }
                memcpy(out, f->d + pos, t);
                ga_lzw_expand(f->d + pos + t, csz - t, out + t, sz - t);
            } else if (fl & 1)
                ga_lzw_expand(f->d + pos, csz, out, sz);
            else
                memcpy(out, f->d + pos, sz < csz ? sz : csz);
            *size = sz;
            if (compound)
                *compound = (fl & 2) != 0;
            return out;
        }
        pos += (csz + 3) & ~3u;
    }
    return NULL;
}

/* item k of a compound resource (its bytes and size), or NULL */
const uint8_t *ga_item(const uint8_t *b, uint32_t n, unsigned k, uint32_t *size)
{
    uint32_t o0, o1;
    if (!b || n < 2 || k >= ga_rd16(b) || 6 + 4 * k + 4 > n)
        return NULL;
    o0 = ga_rd32(b + 2 + 4 * k);
    o1 = ga_rd32(b + 6 + 4 * k);
    if (o0 > o1 || o1 > n)
        return NULL;
    if (size)
        *size = o1 - o0;
    return b + o0;
}

/* ------------------------------------------------------------------ bitmaps and fonts */
/* a Looking Glass bitmap (28-byte header; type 4 run-length); `whole`: the compound resource
 * it is in (its palette's offset counts from there), pal: 256 RGB filled from it */
int ga_bm_decode(const uint8_t *b, uint32_t n, GaBitmap *bm, const uint8_t *whole, uint32_t wn, uint8_t *pal)
{
    int type, row;
    uint32_t p = 28, o = 0, need, pal_off;
    if (!b || n < 28)
        return 0;
    type = b[4];
    bm->w = (int)ga_rd16(b + 8);
    bm->h = (int)ga_rd16(b + 10);
    row = (int)ga_rd16(b + 12);
    pal_off = ga_rd32(b + 24);
    need = (uint32_t)bm->w * (uint32_t)bm->h;
    if (!need || need > 4u << 20 || !(bm->px = SDL_calloc(1, need)))
        return 0;
    if (type == 4) {
        while (o < need && p < n) {
            int op = b[p++];
            uint32_t c;
            if (op == 0) {
                if (p + 2 > n)
                    break;
                c = b[p];
                while (c-- && o < need)
                    bm->px[o++] = b[p + 1];
                p += 2;
            } else if (op < 0x80) {
                for (c = 0; c < (uint32_t)op && p < n; c++)
                    if (o < need)
                        bm->px[o++] = b[p++];
                    else
                        p++;
            } else if (op > 0x80)
                o += (uint32_t)(op - 0x80);
            else {
                if (p + 2 > n)
                    break;
                c = ga_rd16(b + p);
                p += 2;
                if (!c)
                    break;
                if (c < 0x8000)
                    o += c;
                else if (c < 0xC000) {
                    c &= 0x3FFF;
                    while (c-- && p < n)
                        if (o < need)
                            bm->px[o++] = b[p++];
                        else
                            p++;
                } else {
                    c &= 0x3FFF;
                    while (c-- && o < need)
                        bm->px[o++] = p < n ? b[p] : 0;
                    p++;
                }
            }
        }
    } else {
        int y;
        for (y = 0; y < bm->h; y++)
            if (28 + (uint32_t)y * row + bm->w <= n)
                memcpy(bm->px + y * bm->w, b + 28 + y * row, bm->w);
    }
    if (pal && whole && pal_off && wn >= 4 && pal_off <= wn - 4) {
        unsigned start = ga_rd16(whole + pal_off), cnt = ga_rd16(whole + pal_off + 2);
        if (start + cnt <= 256 && cnt * 3 <= wn - 4 - pal_off)
            memcpy(pal + start * 3, whole + pal_off + 4, cnt * 3);
    }
    return 1;
}

int ga_font_load(const GaRes *f, int id, GaFont *ft)
{
    int c;
    ft->b = ga_res_read(f, id, &ft->n, &c);
    if (!ft->b || ft->n < 84)
        return 0;
    ft->colour = ga_rd16(ft->b) == 0xCCCC;
    ft->mn = (short)ga_rd16(ft->b + 36);
    ft->mx = (short)ga_rd16(ft->b + 38);
    ft->buf = (int)ga_rd32(ft->b + 76);
    ft->row = (short)ga_rd16(ft->b + 80);
    ft->h = (short)ga_rd16(ft->b + 82);
    if (ft->mx < ft->mn || 84 + (uint32_t)(ft->mx - ft->mn + 2) * 2 > ft->n ||
        (uint32_t)ft->buf + (uint32_t)ft->row * ft->h > ft->n)
        return 0;
    return 1;
}

/* a character's width and its first column in the font's picture (0 wide: none, or columns
 * the picture does not have) */
int ga_glyph(const GaFont *f, int c, int *x0)
{
    int i, w, cols;
    *x0 = 0;
    c &= 0xFF;
    if (c < f->mn || c > f->mx)
        return 0;
    i = c - f->mn;
    *x0 = (short)ga_rd16(f->b + 84 + i * 2);
    w = (short)ga_rd16(f->b + 86 + i * 2) - *x0;
    cols = f->colour ? f->row : f->row * 8;
    if (*x0 < 0 || w <= 0 || *x0 + w > cols)
        return 0;
    return w;
}

int ga_text_w(const GaFont *f, const char *s)
{
    int w = 0, x0;
    for (; *s; s++)
        w += ga_glyph(f, (unsigned char)*s, &x0);
    return w;
}

