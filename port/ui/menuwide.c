/* menuwide.c -- the main menu at 16:9 with "Widescreen" on (owner's art).
 *
 * The game's main menu (RESBASE.RES 205, 640 x 480) gets 107 columns each side, 854 x 480 in
 * all, from the owner's AI picture of it, finished in the game's colours (the "Widescreen
 * backgrounds" art kit, main-menu-LOCKED-v1).  Only our own parts are built into tnfree
 * (port/res/menu-wide.png, made by tools/menu-wide.py): the sides, and the planet band in the
 * picture's outer 56 columns, where the AI's band goes over the game's and fades into it by column
 * 56 (the game's band dims towards the old 4:3 edge, which showed as a dark seam beside the sides).
 * The sides' stars are the game's own star pixels, from the player's own picture on the screen
 * (menuwide-stars.h says which pixel each one shows); nothing of the game's picture is in tnfree.
 *
 * The display (platform.c) asks each picture whether the main menu is on the screen: the first
 * time, all the picture's pixels we use (the stars' and the band's) must be the main menu's (a
 * check of their colour numbers), on the screen or in the game's back picture (tnui_back_canvas:
 * the shell draws there and copies it to the screen, then draws its pointer on the screen), and
 * they are kept; from then on most of them must still match on the screen (the game's pointer may
 * be over some).  The picture is then drawn 854 wide, our pixels in the
 * screen's colours (palette, colour settings), the band's mixed with the game's pixel under it in
 * full colour; where the game draws over the band (the pointer) its pixels stay as they are. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "screen.h"
#include "stb/stb_image.h"
#include "menuwide-stars.h"

#define MW_W 854
#define MW_H 480
#define E PORT_MENU_WIDE_E

const unsigned char *port_back_picture(int *row);   /* sharp3d.c: tnui_back_canvas */
extern const unsigned char port_menu_wide[];
extern const unsigned port_menu_wide_len;

static uint8_t *grey, *alpha;           /* port/res/menu-wide.png */
static int tried, ok;
static int band_at[MW_BAND];            /* the band's pixels: row * 640 + column of the game's */
static uint8_t band_grey[MW_BAND], band_a[MW_BAND];
static uint8_t kept_star[MW_STARS], kept_band[MW_BAND];     /* the player's picture there */
static int kept;

static int load(void)
{
    unsigned char *px;
    int w, h, n, i, x, y;

    if (tried)
        return ok;
    tried = 1;
    px = stbi_load_from_memory(port_menu_wide, (int)port_menu_wide_len, &w, &h, &n, 2);
    if (!px)
        return 0;
    if (w != MW_W || h != MW_H) {
        stbi_image_free(px);
        return 0;
    }
    grey = malloc(MW_W * MW_H);
    alpha = malloc(MW_W * MW_H);
    if (!grey || !alpha) {
        stbi_image_free(px);
        return 0;
    }
    for (i = 0; i < MW_W * MW_H; i++) {
        grey[i] = px[2 * i];
        alpha[i] = px[2 * i + 1];
    }
    stbi_image_free(px);
    for (n = 0, y = 0; y < MW_H; y++)
        for (x = E; x < E + 640; x++)
            if (alpha[y * MW_W + x]) {
                if (n == MW_BAND)
                    return 0;
                band_at[n] = y * 640 + x - E;
                band_grey[n] = grey[y * MW_W + x];
                band_a[n++] = alpha[y * MW_W + x];
            }
    ok = n == MW_BAND;
    return ok;
}

#define AT(o) scr[(o) / 640 * pitch + (o) % 640]

/* the picture's pixels we use are the main menu's: keep them */
static int keep(const uint8_t *scr, int pitch)
{
    uint32_t h = 0x811C9DC5u;
    int i;
    for (i = 0; i < MW_STARS; i++)
        h = (h ^ scr[mw_star[i][3] * pitch + mw_star[i][2]]) * 0x01000193u;
    for (i = 0; i < MW_BAND; i++)
        h = (h ^ AT(band_at[i])) * 0x01000193u;
    if (h != MW_CHECK)
        return 0;
    for (i = 0; i < MW_STARS; i++)
        kept_star[i] = scr[mw_star[i][3] * pitch + mw_star[i][2]];
    for (i = 0; i < MW_BAND; i++)
        kept_band[i] = AT(band_at[i]);
    return kept = 1;
}

int port_menu_wide_up(const uint8_t *scr, int pitch)
{
    const unsigned char *back;
    int i, row, miss = 0;

    if (!load())
        return 0;
    if (!kept && !keep(scr, pitch) && !((back = port_back_picture(&row)) != NULL && keep(back, row)))
        return 0;
    for (i = 0; i < MW_STARS; i++)
        miss += scr[mw_star[i][3] * pitch + mw_star[i][2]] != kept_star[i];
    for (i = 0; i < MW_BAND; i++)
        miss += AT(band_at[i]) != kept_band[i];
    return miss * 16 <= MW_STARS + MW_BAND;
}

/* k / 255 of a, the rest b */
static uint32_t mix(uint32_t a, uint32_t b, unsigned k)
{
    unsigned j = 255 - k;
    uint32_t r = (((a >> 16) & 255) * k + ((b >> 16) & 255) * j + 127) / 255;
    uint32_t g = (((a >> 8) & 255) * k + ((b >> 8) & 255) * j + 127) / 255;
    uint32_t bl = ((a & 255) * k + (b & 255) * j + 127) / 255;
    return (a & 0xFF000000u) | r << 16 | g << 8 | bl;
}

void port_menu_wide_fill(void *out, int opitch, const uint8_t *scr, int pitch, const uint32_t *pal)
{
    int x, y, i;

    for (y = 0; y < MW_H; y++) {
        uint32_t *d = (uint32_t *)((uint8_t *)out + (size_t)y * opitch);
        const uint8_t *g = grey + y * MW_W, *s = scr + (size_t)y * pitch;
        for (x = 0; x < E; x++)
            d[x] = pal[g[x]];
        for (x = 0; x < 640; x++)
            d[E + x] = pal[s[x]];
        for (x = E + 640; x < MW_W; x++)
            d[x] = pal[g[x]];
    }
    for (i = 0; i < MW_BAND; i++) {
        int o = band_at[i];
        uint8_t v = AT(o);
        if (v == kept_band[i])          /* not the pointer: our band over the game's */
            ((uint32_t *)((uint8_t *)out + (size_t)(o / 640) * opitch))[E + o % 640] =
                mix(pal[band_grey[i]], pal[v], band_a[i]);
    }
    for (i = 0; i < MW_STARS; i++)
        ((uint32_t *)((uint8_t *)out + (size_t)mw_star[i][1] * opitch))[mw_star[i][0]] = pal[kept_star[i]];
}
