/* fxpack.h -- Freefall: the effects and objects packs' pictures (fxpack.c loads them, effects.c
 * draws them).
 * Plain C types only: shared by the SDL side and the game side. */
#ifndef FXPACK_H
#define FXPACK_H

typedef struct {
    int w, h;
    unsigned char *rgba;                /* the picture as loaded (an objects pack's: NULL) */
    unsigned char *pix;                 /* w * h in the palette's colours (0 transparent), effects.c
                                         * (an objects pack's: its own colour numbers) */
    unsigned long pal;                  /* the palette pix was made for, 0 = not yet */
    int boxed;                          /* an indexed one's: bx0, by0 .. bx1, by1 (not included) */
    short bx0, by0, bx1, by1;           /* the box of its pixels (bx1 <= bx0: none) */
} FxPic;

typedef struct {
    unsigned short res;                 /* the frames resource it stands for */
    int n;
    FxPic *pic;
} FxAnim;

/* an effects pack's named folders for "Tree explosion" (burn.c), kept as animations of these
 * numbers: PlantBlast (a plant's blast) and Pieces (the debris sheets wood, bark and leaf,
 * pictures 0, 1, 2: their grey a place on a ramp, alpha the shape; an empty one w 0) */
#define FX_PLANTBLAST 0xF001
#define FX_PIECES 0xF003

FxAnim *port_fx_anim(unsigned short res);   /* the loaded pack's animation for res, or NULL */
FxAnim *port_fx_left(int big, unsigned short res);  /* its Leftovers/1x (big 0) or 3x (1) frames of
                                             * res, as an objects pack's (pix NULL: none), or NULL */
int port_fx_loaded(void);                   /* animations in the loaded pack */
FxAnim *port_fx_marks(int big);             /* its BlastMarks/1x (big 0, 64 x 64) or 4x (1, 256 x 256):
                                             * res 1164 frame k pic[k] (pix NULL: none), or NULL */
unsigned port_fx_gen(void);                 /* goes up with every pack loaded */
FxAnim *port_obj_set(unsigned short res);   /* the objects pack's frames of res (pic[k].pix NULL:
                                             * the game's frame k), or NULL */
int port_obj_loaded(void);                  /* resources in the loaded objects pack */
unsigned char *port_png_indexed(const char *path, int *w, int *h);  /* an indexed PNG's colour
                                             * numbers, w * h (malloc'd), or NULL */
unsigned char *port_png_rgba(const char *path, int *w, int *h);     /* a PNG's RGBA pixels, w * h * 4
                                             * (malloc'd), or NULL */

/* effects.c, for "Tree explosion" (burn.c) while the sharp view draws: the effects pack's picture
 * k of res in the palette's colours (and its count), or NULL; such a picture scaled to w x h at
 * (x0, y0) by its alpha times op / 256 through clut (NULL none); the palette's key (0 none yet);
 * port_fx_mix_begin before the next ones, for the canvas and palette in use: the RGB of colour c,
 * the nearest colour to (r, g, b), the screen's colour o mixed with c by a (0..255) */
const FxPic *port_fx_pic(unsigned short res, long k, int *n);
void port_fx_draw(const FxPic *p, long x0, long y0, long w, long h, const unsigned char *clut, long op);
void port_fx_draw_m(const FxPic *p, long x0, long y0, long w, long h, const unsigned char *clut, long op, int mirror);
int port_plant_size(long ref, int *w, int *h);  /* the game's frame of a plant (1160..1163): its size */
unsigned long port_fx_pal(void);
void port_fx_mix_begin(void);
void port_fx_rgb(int c, long *r, long *g, long *b);
unsigned char port_fx_near(long r, long g, long b);
unsigned char port_fx_mix(unsigned char o, unsigned char c, long a);

#endif
