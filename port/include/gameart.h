/* gameart.h -- the game's resource files, bitmaps and fonts read with a reader of our own
 * (gameart.c): the main thread's screens, and the extra missions (port/mods/extra.c) */
#ifndef TN_GAMEART_H
#define TN_GAMEART_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *d;
    size_t n;
} GaRes;

typedef struct {
    int w, h;
    uint8_t *px;
} GaBitmap;

typedef struct {
    uint8_t *b;
    uint32_t n;
    int colour, mn, mx, buf, row, h;
} GaFont;

uint32_t ga_rd32(const uint8_t *p);
unsigned ga_rd16(const uint8_t *p);
int ga_res_open(GaRes *f, const char *dos);     /* a DOS path of the game's (port_path) */
int ga_res_open_host(GaRes *f, const char *host);   /* a host path */
void ga_res_close(GaRes *f);
int ga_res_info(const GaRes *f, int id, uint32_t *size, int *flags, int *type);    /* its directory entry */
int ga_res_id(const GaRes *f, unsigned k);      /* the k-th resource's id, -1 past the last */
/* resource id: a copy (SDL_free it), its size and whether it is compound */
uint8_t *ga_res_read(const GaRes *f, int id, uint32_t *size, int *compound);
const uint8_t *ga_item(const uint8_t *b, uint32_t n, unsigned k, uint32_t *size);  /* a compound's item k */
/* a bitmap (type 4 run-length or flat); whole: the compound it is in, pal: 256 RGB filled from it */
int ga_bm_decode(const uint8_t *b, uint32_t n, GaBitmap *bm, const uint8_t *whole, uint32_t wn, uint8_t *pal);
int ga_font_load(const GaRes *f, int id, GaFont *ft);
int ga_glyph(const GaFont *f, int c, int *x0);  /* a character's width, x0 its column in the font */
int ga_text_w(const GaFont *f, const char *s);
#endif
