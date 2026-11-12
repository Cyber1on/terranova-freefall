/* 2d_prim.h -- private declarations of the flat8 canvas primitives (0x324000-0x32D400 of
 * __FF.EXE, worker 2d-prim): globals and types not (yet) in include/2d.h, the table indices
 * the primitives use, and the original functions they call.
 */
#ifndef TN_2D_PRIM_H
#define TN_2D_PRIM_H
#include "2d.h"

/* ---------------- types (Shock PLYTYP.H, TMAPS.H, 2D.H) ---------------- */
#ifndef TN_HAVE_GRS_VERTEX
#define TN_HAVE_GRS_VERTEX
typedef struct {
    fix x, y;                      /* screen coordinates */
    fix u, v, w;                   /* texture parameters / rgb */
    fix i;                         /* intensity */
} grs_vertex;
#endif

typedef struct {
    short tmap_type;               /* GRC_XXX */
    short flags;                   /* TMF_XXX */
    uchar *clut;
} grs_tmap_info;

#define TMF_PER 1
#define TMF_CLUT 2
#define TMF_FLOOR 4
#define TMF_WALL 4

#ifndef TN_HAVE_GRS_SCREEN
#define TN_HAVE_GRS_SCREEN
typedef struct {
    grs_bitmap bm;
    grs_canvas *c;                 /* +10 */
    uchar *pal;                    /* +14 */
    grs_rgb *bpal;                 /* +18 */
    uchar *ipal;                   /* +1C */
    uchar *ltab;                   /* +20 light table (256-byte rows) */
    uchar ***transtab;             /* +24 */
    uchar *clut;                   /* +28 */
    short x, y;
} grs_screen;
#endif

#ifndef TN_HAVE_GRS_DRVCAP
#define TN_HAVE_GRS_DRVCAP
typedef struct {
    fix aspect;
    short w, h;
    uchar *vbase;
} grs_drvcap;
#endif


#ifndef TN_HAVE_GRS_TMAP_LOOP_INFO
#define TN_HAVE_GRS_TMAP_LOOP_INFO
#pragma pack(push, 1)
/* same layout as 2d_tmap.h's (2d-tmap owns it) */
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
#pragma pack(pop)
#endif

/* ---------------- function table indices (IFCN.H) ---------------- */
enum {
    GRC_PIXEL, GRC_WIRE_POLY_LINE, GRC_DEGEN_LINE,
    GRC_BITMAP, GRC_STENCIL_BITMAP, GRC_CLUT_BITMAP,
    GRC_HFLIP_BITMAP, GRC_CLUT_HFLIP_BITMAP, GRC_MASK_BITMAP,
    GRC_HDOUBLE_BITMAP, GRC_VDOUBLE_BITMAP, GRC_HVDOUBLE_BITMAP,
    GRC_HDOUBLE_BLEND_BITMAP, GRC_VDOUBLE_BLEND_BITMAP, GRC_HVDOUBLE_BLEND_BITMAP,
    GRC_SCALE, GRC_TRANS_SCALE, GRC_LIT_SCALE, GRC_TRANS_LIT_SCALE, GRC_CLUT_SCALE, GRC_TRANS_CLUT_SCALE,
    GRC_POLY, GRC_MORE_POLY,
    GRC_LIN, GRC_TRANS_LIN, GRC_LIT_LIN, GRC_TRANS_LIT_LIN, GRC_CLUT_LIN, GRC_TRANS_CLUT_LIN,
    GRC_BILIN, GRC_TRANS_BILIN, GRC_LIT_BILIN, GRC_TRANS_LIT_BILIN, GRC_CLUT_BILIN, GRC_TRANS_CLUT_BILIN,
    GRC_FLOOR, GRC_TRANS_FLOOR, GRC_LIT_FLOOR, GRC_TRANS_LIT_FLOOR, GRC_CLUT_FLOOR, GRC_TRANS_CLUT_FLOOR,
    GRC_WALL2D, GRC_TRANS_WALL2D, GRC_LIT_WALL2D, GRC_TRANS_LIT_WALL2D, GRC_CLUT_WALL2D, GRC_TRANS_CLUT_WALL2D,
    GRC_WALL1D, GRC_TRANS_WALL1D, GRC_LIT_WALL1D, GRC_TRANS_LIT_WALL1D, GRC_CLUT_WALL1D, GRC_TRANS_CLUT_WALL1D,
    GRC_PER, GRC_TRANS_PER, GRC_LIT_PER, GRC_TRANS_LIT_PER, GRC_CLUT_PER, GRC_TRANS_CLUT_PER,
    GRC_PER_VSCAN, GRC_TRANS_PER_VSCAN, GRC_LIT_PER_VSCAN, GRC_TRANS_LIT_PER_VSCAN, GRC_CLUT_PER_VSCAN,
    GRC_TRANS_CLUT_PER_VSCAN,
    GRD_FUNCS                      /* 65 */
};

/* uline fill vector entries (line types, LINTYP.H) */
#define GRL_UHLINE 2
#define GRL_UVLINE 3

/* canvas table indices used here (ICANVAS.H) */
#define SET_UPIXEL8 0
#define GET_UPIXEL8 2
#define SET_UPIXEL24 4
#define DRAW_URECT 15
#define PUSH_STATE 19
#define POP_STATE 20
#define GET_DEVICE_UBITMAP 206
#define HFLIP_FLAT8_UBITMAP 222
#define CLUT_HFLIP_FLAT8_UBITMAP 234
#define DRAW_DEVICE_UBITMAP 142   /* +2*type: ubitmap, +2*type+1: bitmap */
#define DRAW_DEVICE_BITMAP 143
#define CLUT_DRAW_DEVICE_UBITMAP 154
#define CLUT_DRAW_DEVICE_BITMAP 155
#define SCALE_DEVICE_UBITMAP 168
#define SCALE_DEVICE_BITMAP 169
#define CLUT_SCALE_DEVICE_UBITMAP 182
#define CLUT_SCALE_DEVICE_BITMAP 183

/* ---------------- globals ---------------- */
extern void (**grd_pixel_table)();        /* 0x44B198 canvas table of the bitmap's own type */
extern void (**grd_device_table)();       /* 0x44B19C */
extern void (**grd_function_table)();     /* 0x44C5A4 [bmtype*65 + GRC] for the current fill type */
extern void (**grd_uline_fill_vector)();  /* 0x44C5AC uline fills for the current fill type */
extern grs_screen *grd_screen;            /* 0x35F8BC */
extern uchar *grd_pal;                    /* 0x35F8C0 768-byte palette */
extern grs_rgb *grd_bpal;                 /* 0x35F8C4 blend palette */
extern uchar *grd_ipal;                   /* 0x35F8C8 inverse palette, 15-bit rgb index */
extern grs_drvcap *grd_cap;               /* 0x35F8CC */
extern uchar **tluc8tab_p;                /* 0x360478 translucency tables per color (Shock tluc8tab) */
extern long gr_lit_tolerance;             /* 0x364450 max light spread for the clut short cut */
extern uchar gr_lin_no_split;             /* 0x36DF2C nonzero: never split lin maps in triangles */

extern uchar *grd_unpack_buf;             /* 0x35F4E4 rsd8 unpack buffer (RSDCVT.C) */
#define GR_UNPACK_RSD8_OK 0
extern uchar bitmask[8];
extern uchar *grd_blend;                  /* 0x36EF34 blend tables, 64K each (BLEND.C) */
extern uchar *grd_half_blend;             /* 0x36EF38 the 50% table */
extern int grd_log_blend_levels;          /* 0x36EF3C */
extern uchar gr_sub_bm_enabled;           /* 0x36DF30 set by gr_init_sub_bm (name ours) */
extern uchar *grd_last_sub_bm;            /* 0x36DF34 last source of the quadruple sub bitmap */
extern uchar *grd_last_dsub_bm;           /* 0x36DF38 last source of the double sub bitmap (ours) */
extern uchar *grd_sub_bm_buffer;          /* 0x36DF3C 32x32 buffer (static in Shock) */                  /* 0x36EF28 {0x80,0x40,..,1} (Shock BIT.C) */

#define grd_ytab (grd_canvas->ytab)

/* ---------------- original functions called ---------------- */
void gr_null(void);                                           /* 0x324FF0 */
int gen_fill_pixel(long color, short x, short y);             /* 0x2FAD20 */
int gen_hline(short x0, short y, short x1);                   /* 0x2ED5A0 */
int gen_vline(short x, short y0, short y1);                   /* 0x2ED650 */
int gr_clip_rect(short *left, short *top, short *right, short *bot); /* 0x34A150 */
int h_map(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti);  /* 0x31847C */
void h_umap(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti); /* 0x317FB0 */
void v_umap(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti); /* 0x3184CC */
int per_map(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti); /* 0x34A2EC */
void per_umap(grs_bitmap *bm, int n, grs_vertex **vpl, grs_tmap_info *ti); /* 0x34A33C */
int gr_clip_poly(int n, int l, grs_vertex **vpl, grs_vertex ***pcpl); /* 0x325AC8 */
int temp_free(void *p);                                       /* 0x3091CC */
long gri_scale_34AA7C(grs_bitmap *bm, int n, grs_vertex **vpl); /* 0x34AA7C (2d-core range, name ours) */
long gri_scale_34AC54(grs_bitmap *bm, int n, grs_vertex **vpl); /* 0x34AC54 (2d-core range, name ours) */

int gr_clip_mono_bitmap(grs_bitmap *bm, short *x, short *y);  /* 0x34AEA0 (2d-core range) */
int gr_clip_flat24_bitmap(grs_bitmap *bm, short *x, short *y); /* 0x34B030 (2d-core range) */
int gr_rsd8_convert(grs_bitmap *sbm, grs_bitmap *dbm);        /* 0x2E0F70 */
int gr_free_blend(void);                                       /* 0x34B2CC */
uchar gr_init_blend(int log_blend_levels);                    /* 0x34B2FC bool in al (Shock: TRUE = ok) */
void gr_font_string_size(grs_font *f, char *s, short *w, short *h); /* 0x2F31F0 */
int gen_fix_elipse(fix x0, fix y0, fix a, fix b);             /* 0x34ACC0 (2d-core range, name ours) */
int gen_fix_oval(fix x0, fix y0, fix a, fix b);               /* 0x34AE20 (2d-core range, name ours) */
int gri_cline_clip(grs_vertex *v0, grs_vertex *v1);           /* 0x346220 (2d-core range, Shock CLPCLIN.C) */
extern fix grd_recip_tab[1024];                               /* 0x45F71C (2d-tmap name): [0]=0x7FFFFFFF, [n]=0x10000/n */

/* ---------------- this worker's functions used across files ---------------- */
uchar *gri_lit_clut(int n, grs_vertex **vpl);                 /* 0x327840 */
void temp_lin_umap(grs_bitmap *bm, int n, grs_vertex **vpl);
void temp_lit_lin_umap(grs_bitmap *bm, int n, grs_vertex **vpl);
void temp_clut_lin_umap(grs_bitmap *bm, int n, grs_vertex **vpl, uchar *cl);
int gri_scale_clip(grs_vertex *v0, grs_vertex *v1);
void flat8_flat8_smooth_h_double_ubitmap(grs_bitmap *srcb, grs_bitmap *dstb);
int gen_fix_cline(fix x0, fix y0, long c0, fix x1, fix y1, long c1); /* 0x324E50 */
int gr_clip_fix_line4(fix *x0, fix *y0, fix *x1, fix *y1);   /* 0x3254C4 (Shock gr_clip_fix_line) */
void gri_clip_order_x(fix **a, fix **b);                      /* 0x325AB0 */

/* WATCOM_CALL with every argument cast to long (pointers, shorts) */
#define WC1(f, a) WATCOM_CALL(f, (long)(a))
#define WC2(f, a, b) WATCOM_CALL(f, (long)(a), (long)(b))
#define WC3(f, a, b, c) WATCOM_CALL(f, (long)(a), (long)(b), (long)(c))
#define WC4(f, a, b, c, d) WATCOM_CALL(f, (long)(a), (long)(b), (long)(c), (long)(d))
#define WC5(f, a, b, c, d, e) WATCOM_CALL(f, (long)(a), (long)(b), (long)(c), (long)(d), (long)(e))
#define WC6(f, a, b, c, d, e, g) WATCOM_CALL(f, (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), (long)(g))

#endif
