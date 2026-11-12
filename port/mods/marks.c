/* marks.c -- Freefall: the effects pack's BlastMarks, the game's blast marks on the ground (res
 * 1164 frames 1..4, which blasttm.c paints over a scorched square's texture by its number mod 4)
 * drawn with the pack's pictures, in the sharp 3D view:
 *
 *  - BlastMarks/1x/02.png .. 05.png (64 x 64): the squares as the game's (weather.c cell_tex), and
 *    the distant strips' row of the frame 2's pixels (strip_2AC128: the game's picks of it,
 *    blast_2AC6D0, at the same places of the pack's 03.png).
 *  - BlastMarks/4x/02.png .. 05.png (256 x 256): a near square, one that takes more than 96 of the
 *    screen's pixels across (weather.c), on its texture's pixels made 4 x 4 (its u, v times 4).
 *  - Indexed as an objects pack's pictures, in colour numbers the same on every planet; 0 keeps
 *    the ground.
 *  - The game's own copies (blasttm.c, on its heap) are made and kept as always, so the game is
 *    the same; these are the port's, made again for a new map (blast_2AC530) or pack.
 */
#include <string.h>

#include "g5_c.h"
#include "port.h"
#include "fxpack.h"

extern int port_s3d_on;

static uchar tex1[0x40][0x1000];        /* the 64 x 64 copies */
static uchar tex4[0x40][0x10000];       /* the 256 x 256 copies */
static unsigned key[2][0x40];           /* the map and pack each was made for (0 none) */
static const uchar *from[2][0x40];      /* and its texture */
static unsigned map_gen = 1;
static ushort pick[0x100];              /* blast_2AC6D0's picks of frame 2: their places */
static int pick_w, pick_h;
static uchar noise[0x100];
static unsigned noise_key;

static unsigned gen(void)
{
    return map_gen * 0x10001u + port_fx_gen();
}

/* blast_2AC530: a new map */
void port_marks_map(void)
{
    map_gen++;
}

/* blast_2AC6D0's pick i: the pixel at of its frame w x h */
void port_marks_pick(int i, long at, int w, int h)
{
    if (i >= 0 && i < 0x100) {
        pick[i] = (ushort)at;
        pick_w = w;
        pick_h = h;
    }
    noise_key = 0;
}

/* the pack's mark of res 1164 frame k (1..4) of the side 64 (big 0) or 256 (1), in the sharp view */
static const FxPic *mark(int big, int k)
{
    FxAnim *a = port_s3d_on ? port_fx_marks(big) : NULL;
    return a && k < a->n && a->pic[k].pix ? &a->pic[k] : NULL;
}

/* weather.c: the square's texture tex (0..0x3F) with the pack's mark (frame tex % 4 + 1) over
 * it, 64 x 64 (big 0) or 256 x 256 (1); NULL: the pack has none */
uchar *port_marks_tex(long tex, int big)
{
    const FxPic *p;
    const uchar *src;
    uchar *d;
    int x, y, sh = big ? 2 : 0, side = 64 << sh;
    if (tex < 0 || tex >= 0x40 || tex >= planet_tex_count || !(p = mark(big, (int)(tex % 4) + 1)))
        return NULL;
    src = planet_tex[tex];
    if (!src)
        return NULL;
    d = big ? tex4[tex] : tex1[tex];
    if (key[big][tex] == gen() && from[big][tex] == src)
        return d;
    for (y = 0; y < side; y++)
        for (x = 0; x < side; x++) {
            uchar c = p->pix[y * side + x];
            d[y * side + x] = c ? c : src[(y >> sh) * 64 + (x >> sh)];
        }
    key[big][tex] = gen();
    from[big][tex] = src;
    return d;
}

/* strip_2AC128: the distant strips' row of marks (0: the ground), the pack's frame 2 at the game's
 * picks; NULL: the pack has none */
const uchar *port_marks_noise(void)
{
    const FxPic *p = mark(0, 2);
    int i;
    if (!p || pick_w <= 0 || pick_h <= 0)
        return NULL;
    if (noise_key != gen()) {
        for (i = 0; i < 0x100; i++) {
            long x = (long)(pick[i] % pick_w) * 64 / pick_w, y = (long)(pick[i] / pick_w) * 64 / pick_h;
            noise[i] = y < 64 ? p->pix[y * 64 + x] : 0;
        }
        noise_key = gen();
    }
    return noise;
}
