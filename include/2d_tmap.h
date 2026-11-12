/* 2d_tmap.h -- private declarations of the 2D texture mapper / polygon / scaler modules
 * (Terra Nova 0x32D400-0x345000, worker 2d-tmap).  Types that will move to 2d.h are guarded
 * with TN_HAVE_<TYPE> so the core header can take them over. */
#ifndef TN_2D_TMAP_H
#define TN_2D_TMAP_H
#include "2d.h"

#pragma pack(push, 1)

#ifndef TN_HAVE_GRS_VERTEX
#define TN_HAVE_GRS_VERTEX
typedef struct {
    fix x, y;                      /* screen coordinates */
    fix u, v, w;                   /* texture parameters/rgb */
    fix i;                         /* intensity */
} grs_vertex;
#endif

#ifndef TN_HAVE_GRS_SCREEN
#define TN_HAVE_GRS_SCREEN
typedef struct {
    grs_bitmap bm;                 /* +0  */
    grs_canvas *c;                 /* +10 */
    uchar *pal;                    /* +14 */
    void *bpal;                    /* +18 */
    uchar *ipal;                   /* +1C */
    uchar *ltab;                   /* +20 light table [16][256] */
    uchar ***transtab;             /* +24 */
    uchar *clut;                   /* +28 */
    short x, y;                    /* +2C */
} grs_screen;
#endif

#ifndef TN_HAVE_GRS_DRVCAP
#define TN_HAVE_GRS_DRVCAP
typedef struct {
    fix aspect;                    /* +0 */
    short w, h;                    /* +4 +6 */
    uchar *vbase;                  /* +8 video memory (the 64K bank window when banked) */
} grs_drvcap;
#endif

#ifndef TN_HAVE_GRS_TMAP_LOOP_INFO
#define TN_HAVE_GRS_TMAP_LOOP_INFO
typedef struct {
    fix x, y, u, v, i;
    fix dx;                        /* also dy */
    fix du, dv, di;
} grs_tmap_edge;                   /* 0x24 bytes */

typedef struct {
    int n;                         /* +00 number of lines */
    union { uchar *d; int x; int y; };  /* +04 destination pointer / scanline coord */
    union {
        grs_bitmap bm;             /* +08 */
        struct {
            uchar *s;              /* +08 bitmap bits */
            uchar bm_type, bm_align;   /* +0C */
            short bm_flags, bm_w, bm_h, bm_row;   /* +0E +10 +12 +14 */
            uchar wlog;            /* +16 */
            uchar loop;            /* +17 (= bm.hlog): GRL_xxx */
        };
    };
    fix w;                         /* +18 */
    union { grs_tmap_edge left, top; };     /* +1C */
    union { grs_tmap_edge right, bot; };    /* +40 */
    fix dw;                        /* +64 */
    ulong u_mask;                  /* +68 */
    ulong v_mask;                  /* +6C (also mask) */
    uchar *clut;                   /* +70 */
    long *vtab;                    /* +74 */
    void *scanline_func;           /* +78 */
    void *loop_func;               /* +7C */
    void *left_edge_func;          /* +80 (top) */
    void *right_edge_func;         /* +84 (bot) */
} grs_tmap_loop_info;
LG_STATIC_ASSERT(sizeof(grs_tmap_loop_info) == 0x88, tmap_loop_info_size);
#endif

#pragma pack(pop)

/* loop types (ti->loop) */
#define GRL_OPAQUE 0
#define GRL_TRANS 1
#define GRL_LOG2 2
#define GRL_CLUT 4
#define GRL_TLUC8 0x10

/* ---------------- globals (original data) ---------------- */
extern grs_screen *grd_screen;            /* 0x35F8BC */
extern uchar **tluc8tab_p;                /* 0x360478 -> uchar *tluc8tab[256] (0x44C180) */
extern uchar *tluc8stab;                  /* 0x44C580 */
extern void (**grd_function_table)();      /* 0x44C5A4 current fill/function table (type as in 2d.h since the G2 merge) */

extern uchar *grd_unpack_buf;             /* 0x35F4E4 */
extern grs_rgb *grd_bpal;                 /* 0x35F8C4 */

/* original functions of other 2D modules called by the mappers */
void flat8_flat8_smooth_hv_double_ubitmap(grs_bitmap *src, grs_bitmap *dst);   /* 0x32BEE1 */
int gen_flat8_bitmap(grs_bitmap *bm, short x, short y);             /* 0x32A050 (2d-prim genfl8.c) */
int gen_flat8_clut_bitmap(grs_bitmap *bm, short x, short y, uchar *cl); /* 0x32AB64 (2d-prim genfl8c.c) */
int gri_poly_loop(grs_tmap_loop_info *ti);                          /* 0x32D2E0 (2d-prim fl8plyl.c) */
long gri_flat8_clut_ubitmap(grs_bitmap *bm, short x, short y, uchar *cl);

extern grs_drvcap *grd_cap;               /* 0x35F8CC */

/* ---------------- 8-bit banked VESA (bank8) device state ---------------- */
extern int grd_bank8_lock;                /* 0x36EF50 >0 while a bank switch is in progress
                                           * (forces the next pixel to switch again) */
extern ushort grd_bank8_bank;             /* 0x36EF54 current 64K bank */
/* selects the bank of canvas address a (bits + offset; the window is grd_cap->vbase) the way
 * every bank8 pixel routine does; evaluates to the window pointer.  Uses a local `lock_`. */
#define BANK8_PTR(a) (bank8_select(a, &lock_), grd_cap->vbase + ((ulong)(a) & 0xffff))
static inline void bank8_select(long a, int *lock)
{
    int l = grd_bank8_lock;
    long bank = a >> 16;

    if (l != 0 || grd_bank8_bank != bank) {
        grd_bank8_bank = (ushort)bank;
        grd_bank8_lock = l + 1;
        WATCOM_CALL((void (*)())grd_device_table[GRT_SET_BANK], (long)(short)grd_bank8_bank);
        l = grd_bank8_lock - 1;
    }
    *lock = l;
}

/* ---------------- ModeX (unchained 320x240 VGA) device state ---------------- */
extern uchar grd_mx_lmask[4];             /* 0x36EF40 {0x0F,0x0E,0x0C,0x08}: planes >= x&3 */
extern uchar grd_mx_rmask[4];             /* 0x36EF44 {0x01,0x03,0x07,0x0F}: planes <= x&3 */
extern uchar grd_mx_wmask;                /* 0x36EF48 sequencer map mask last written (3C4/2) */
extern uchar grd_mx_rplane;               /* 0x36EF49 graphics read map last written (3CE/4) */

#ifdef TN_WATCOM_ABI
static inline void tm_outw(int port, int v) { __asm__ volatile("outw %w0, %w1" : : "a"(v), "Nd"(port)); }
static inline void tm_outb(int port, int v) { __asm__ volatile("outb %b0, %w1" : : "a"(v), "Nd"(port)); }
static inline uchar tm_inb(int port)
{
    uchar v;
    __asm__ volatile("inb %w1, %b0" : "=a"(v) : "Nd"(port));
    return v;
}
#else
#define tm_outw port_outw
#define tm_outb port_outb
#define tm_inb port_inb
#endif
/* write-plane mask / read-plane select, skipped when the cached value already matches */
#define MX_WMASK(m) do { if (grd_mx_wmask != (ulong)(m)) {                      \
        grd_mx_wmask = (uchar)(m); tm_outw(0x3C4, (uchar)(m) << 8 | 2); } } while (0)
#define MX_RPLANE(pl) do { if (grd_mx_rplane != (ulong)(pl)) {                   \
        grd_mx_rplane = (uchar)(pl); tm_outw(0x3CE, (uchar)(pl) << 8 | 4); } } while (0)

#define fix_cint(a) ((short)(((a) + 0xffff) >> 16))
#define fix_ceil(a) ((fix)(((a) + 0xffff) & 0xffff0000))

/* x86 helpers that reproduce the exact instruction semantics the asm uses */
static inline fix tm_fix_div(fix a, fix b)      /* edx:eax = a<<16 (sign from a>>16); idiv b */
{
#ifdef TN_WATCOM_ABI
    long long n = (long long)a << 16;
    return (fix)(n / b);
#else
    return fix_idiv64(a >> 16, (unsigned long)a << 16, b);   /* fix.h: traps like the original */
#endif
}
static inline fix tm_fix_mul(fix a, fix b)      /* imul; shrd eax,edx,16 */
{
    return (fix)(((long long)a * b) >> 16);
}

#endif
