/* 2D: Looking Glass 2D graphics library (x:\prj\tech\libsrc\2d\), as linked into Terra Nova v1.09.
 * Shared core header for every 2D module.  Layouts are read from the binary (DOS, little
 * endian); System Shock's Mac 2D.H has the same fields but big-endian clip padding.
 *
 * OWNERSHIP: the 2D core worker extends this file (additions only, keep it compiling).  Other
 * 2D modules put their private declarations in their own headers.
 */
#ifndef TN_2D_H
#define TN_2D_H
#include "lg.h"
#include "fix.h"

#pragma pack(push, 1)

/* ---------------- bitmaps ---------------- */
typedef struct {
    uchar *bits;                   /* +0  */
    uchar type;                    /* +4  BMT_XXX */
    uchar align;                   /* +5  */
    ushort flags;                  /* +6  BMF_XXX */
    short w;                       /* +8  */
    short h;                       /* +A  */
    ushort row;                    /* +C  bytes per row */
    uchar wlog;                    /* +E  */
    uchar hlog;                    /* +F  */
} grs_bitmap;
LG_STATIC_ASSERT(sizeof(grs_bitmap) == 16, grs_bitmap_size);

/* Bitmap types (bm.type; index of grd_canvas_table_list / grd_function_table_list /
 * grd_uline_fill_table_list, 9 entries each).  Terra Nova adds type 8 (2 bytes per pixel: row =
 * 2*w in gr_init_bm, w*2*h bytes in gr_alloc_canvas, x*2 in gr_init_sub_bm).  Types 6, 7 and
 * > 8 leave bm.row unset in gr_init_bm (original quirk).
 * The "extra types at list indices 3 and 10" are not bitmap types: they are table SETS of the
 * 12-entry grd_*_table_set lists below (see GRD_TS_xxx). */
#define BMT_DEVICE 0
#define BMT_MONO 1
#define BMT_FLAT8 2
#define BMT_FLAT24 3
#define BMT_RSD8 4
#define BMT_TLUC8 5
#define BMT_SPAN 6
#define BMT_GEN 7
#define BMT_FLAT16 8                /* Terra Nova addition (name ours) */
#define BMT_TYPES 9
#define REAL_BMT_TYPES BMT_SPAN     /* 6 bitmap types per function table fill row */

/* bitmap flags (bm.flags) */
#define BMF_TRANS 1
#define BMF_TLUC8 2

/* ---------------- clipping ---------------- */
typedef struct _sten {
    short l, r;
    struct _sten *n;
} grs_sten_elem;

typedef struct {
    grs_sten_elem *elem;
    long flags;
} grs_stencil;

/* Integer and fixed-point views of the clip rectangle: the integer coordinate is the high
 * word of each fix (x86 little endian, so the pad comes first). */
typedef union {
    struct {
        grs_stencil *sten;
        fix left, top, right, bot;
    } f;
    struct {
        grs_stencil *sten;
        short pad0, left;
        short pad1, top;
        short pad2, right;
        short pad3, bot;
    } i;
} grs_clip;
LG_STATIC_ASSERT(sizeof(grs_clip) == 20, grs_clip_size);

/* ---------------- fonts ---------------- */
typedef struct {
    ushort id;
    char dummy1[34];
    short min;                     /* +24 first char */
    short max;                     /* +26 last char */
    char dummy2[32];
    long cotptr;
    long buf;                      /* +4C */
    short w;
    short h;
    short off_tab[1];              /* +54 */
} grs_font;

/* ---------------- graphics context and canvas ---------------- */
typedef struct {
    long fcolor;                   /* canvas +10 */
    long bcolor;                   /*        +14 */
    grs_font *font;                /*        +18 */
    long text_attr;                /*        +1C */
    long fill_type;                /*        +20 FILL_XXX */
    long fill_parm;                /*        +24 */
    grs_clip clip;                 /*        +28 (i.left at +2E, top +32, right +36, bot +3A) */
} grs_context;

typedef struct {
    grs_bitmap bm;                 /* +0  */
    grs_context gc;                /* +10 */
    uchar **ytab;                  /* +3C */
} grs_canvas;
LG_STATIC_ASSERT(sizeof(grs_canvas) == 0x40, grs_canvas_size);

#define FILL_NORM 0
#define FILL_CLUT 1
#define FILL_XOR 2
#define FILL_BLEND 3
#define FILL_SOLID 4
#define GRD_FILL_TYPES 5

#pragma pack(pop)

/* ---------------- system info, mode info, screens (2d-core) ---------------- */
#pragma pack(push, 1)
typedef ulong grs_rgb;              /* gr_bind_rgb: r<<2 | g<<13 | b<<24 */
#define gr_bind_rgb(r, g, b) ((((grs_rgb)(r)) << 2) | (((grs_rgb)(g)) << 13) | (((grs_rgb)(b)) << 24))

typedef struct {
    uchar id_maj;                  /* +0 0 = VGA, (vesa_get_info fills the rest) */
    uchar id_min;                  /* +1 */
    short memory;                  /* +2 KB of video memory */
    short modes[64];               /* +4 available GRM_ modes, -1 terminated (vesa_get_info
                                    *    stops at +0x82, so at most 63 modes) */
} grs_sys_info;                    /* grd_info 0x44B1B8 (0x84 bytes, up to 0x44B23C) */
LG_STATIC_ASSERT(sizeof(grs_sys_info) == 0x84, grs_sys_info_size);

/* Terra Nova's mode table (grd_mode_info, 0x364DF0, 77 entries of 12 bytes up to 0x36518C;
 * Shock's has 5-byte {w,h,b} entries).  VESA detection (vesa_get_info) fills vmode, flags and
 * win for the SVGA modes.  flags: 1 available, 2 linear frame buffer, 4 one read/write window. */
typedef struct {
    short mode;                    /* +0 GRM_ number (= index) */
    short vmode;                   /* +2 BIOS/VESA mode number, -1 = Mode X */
    short w, h;                    /* +4 +6 */
    uchar b;                       /* +8 bits per pixel */
    uchar flags;                   /* +9 */
    uchar win;                     /* +A log2(64 / window granularity in KB): bank shift,
                                    *    copied to vesa_gran_shift (0x44DB1A) by set_mode */
    uchar pad;                     /* +B */
} grs_mode_info;
LG_STATIC_ASSERT(sizeof(grs_mode_info) == 12, grs_mode_info_size);
#define GRD_MODES 77
enum {                             /* Terra Nova's mode numbers (= index in grd_mode_info) */
    GRM_320x200x8, GRM_320x200x8X, GRM_320x400x8, GRM_320x240x8, GRM_320x480x8, GRM_640x350x4,
    GRM_640x400x4, GRM_640x480x4, GRM_800x600x4, GRM_1024x768x4, GRM_1280x1024x4,
    GRM_1600x1200x4, GRM_320x200x8V, GRM_320x240x8V, GRM_320x400x8V, GRM_360x200x8V,
    GRM_360x240x8V, GRM_360x400x8V, GRM_640x350x8, GRM_640x400x8, GRM_640x480x8, GRM_800x600x8,
    GRM_1024x768x8, GRM_1280x1024x8, GRM_1600x1200x8, GRM_320x200x15, GRM_320x240x15,
    GRM_320x400x15, GRM_360x200x15, GRM_360x240x15, GRM_360x400x15, GRM_640x350x15,
    GRM_640x400x15, GRM_640x480x15, GRM_800x600x15, GRM_1024x768x15, GRM_1280x1024x15,
    GRM_1600x1200x15, GRM_320x200x16, GRM_320x240x16, GRM_320x400x16, GRM_360x200x16,
    GRM_360x240x16, GRM_360x400x16, GRM_640x350x16, GRM_640x400x16, GRM_640x480x16,
    GRM_800x600x16, GRM_1024x768x16, GRM_1280x1024x16, GRM_1600x1200x16, GRM_320x200x24,
    GRM_320x240x24, GRM_320x400x24, GRM_360x200x24, GRM_360x240x24, GRM_360x400x24,
    GRM_640x350x24, GRM_640x400x24, GRM_640x480x24, GRM_800x600x24, GRM_1024x768x24,
    GRM_1280x1024x24, GRM_1600x1200x24, GRM_320x200x32, GRM_320x240x32, GRM_320x400x32,
    GRM_360x200x32, GRM_360x240x32, GRM_360x400x32, GRM_640x350x32, GRM_640x400x32,
    GRM_640x480x32, GRM_800x600x32, GRM_1024x768x32, GRM_1280x1024x32, GRM_1600x1200x32,
    GRM_MODES
};

/* grs_screen and grs_drvcap are also defined privately by 2d_prim.h / 2d_tmap.h (identical
 * layout); this block is skipped inside those headers' translation units until they switch
 * to these definitions. */
#if !defined(TN_2D_TMAP_H)
#ifndef TN_HAVE_GRS_SCREEN
#define TN_HAVE_GRS_SCREEN
typedef struct {
    grs_bitmap bm;                 /* +0  device bitmap of the whole virtual screen */
    grs_canvas *c;                 /* +10 2 canvases: c[0] screen, c[1] visible */
    uchar *pal;                    /* +14 */
    grs_rgb *bpal;                 /* +18 */
    uchar *ipal;                   /* +1C */
    uchar *ltab;                   /* +20 light table */
    uchar ***transtab;             /* +24 */
    uchar *clut;                   /* +28 */
    short x, y;                    /* +2C +2E visible origin (gr_set_focus) */
} grs_screen;
#endif
#ifndef TN_HAVE_GRS_DRVCAP
#define TN_HAVE_GRS_DRVCAP
typedef struct {
    fix aspect;                    /* +0 */
    short w, h;                    /* +4 +6 */
    uchar *vbase;                  /* +8 */
} grs_drvcap;
#endif
#ifndef TN_HAVE_GRS_VERTEX
#define TN_HAVE_GRS_VERTEX
typedef struct {
    fix x, y;
    fix u, v, w;
    fix i;
} grs_vertex;
#endif
#endif
/* Texture mapper loop state (same layout as 2d_prim.h / 2d_tmap.h, which skip theirs when
 * this one is seen first). */
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
LG_STATIC_ASSERT(sizeof(grs_tmap_loop_info) == 0x88, grs_tmap_loop_info_size);
#endif
#pragma pack(pop)

#define CLIP_NONE 0
#define CLIP_LEFT 1
#define CLIP_TOP 2
#define CLIP_RIGHT 4
#define CLIP_BOT 8
#define CLIP_ALL 16

/* ---------------- function tables ----------------
 * Current tables (set by gr_set_canvas from the 9-entry lists, indexed by bitmap type):
 *   grd_canvas_table   = grd_canvas_table_list[BMT_GEN if chn_flags & CHN_GEN else bm.type]
 *   grd_pixel_table    = grd_canvas_table_list[bm.type]
 *   grd_function_fill_table = grd_function_table_list[same index as grd_canvas_table]
 *   grd_function_table = grd_function_fill_table + fill_type*GRD_FUNCS*REAL_BMT_TYPES
 *   grd_uline_fill_table = grd_uline_fill_table_list[bm.type]
 *   grd_uline_fill_vector = grd_uline_fill_table + fill_type*GRD_LINE_TYPES
 * Table SETS (12-entry lists filled by gr_init at 0x304BE0 and read by the mode setters, which
 * copy one set into list[BMT_DEVICE]): index = 2*depth_class + (banked ? 1 : 0), depth_class
 * 0 = 4 bpp, 1 = 8, 2 = 15/16, 3 = 24, 4 = 32; plus 10 = Mode X and 11 = generic
 * (gr_force_generic).  Linked sets: 2 (flat8 linear: canvas 0x365C50, function 0x366160,
 * uline 0x367FD8), 3 (8-bit banked VESA: 0x36A558 / 0x3686E0 / 0x36AA68), 10 (Mode X:
 * 0x36808C / 0x36AB1C / 0x36859C).  Set 11 is never filled: gr_force_generic installs NULL
 * tables (original bug). */
#define GRD_TS_FLAT8 2
#define GRD_TS_BANK8 3
#define GRD_TS_MODEX 10
#define GRD_TS_GEN 11
#define GRD_TABLE_SETS 12
#define GRD_LINE_TYPES 9                  /* uline fill vector entries per fill type */
#define GRD_FUNCS_TN 65                   /* GRC_ functions per bitmap type (Shock GRD_FUNCS) */
#define CALC_ROW 320                      /* canvas table: row of a device bitmap of width w */
#define SUB_BITMAP 321                    /* canvas table: device sub bitmap */

/* device table (grd_device_table) entries, as used in Terra Nova */
enum {
    GRT_INIT_DEVICE, GRT_CLOSE_DEVICE, GRT_SET_MODE, GRT_GET_MODE,
    GRT_SAVE_STATE,                /* 4: gr_push_video_state (Shock GRT_SET_STATE slot) */
    GRT_RESTORE_STATE,             /* 5: gr_pop_video_state */
    GRT_STAT_HTRACE, GRT_STAT_VTRACE, GRT_SET_PAL, GRT_GET_PAL, GRT_SET_WIDTH, GRT_GET_WIDTH,
    GRT_SET_FOCUS, GRT_GET_FOCUS, GRT_CANVAS_TABLE, GRT_SPAN_TABLE,
    GRT_VESA_PAL,                  /* 16: VESA bank switch routine (set by vesa set_mode; old
                                    *     name kept, see GRT_SET_BANK) */
    GRD_DEVICE_FUNCS
};
#define GRT_SET_BANK GRT_VESA_PAL

/* chaining (CHAIN.H) */
#pragma pack(push, 1)
typedef struct grs_func_chain_s {
    void (*f)();
    struct grs_func_chain_s *next;
    uchar flags;                   /* 0 over, 1 before, 3 after */
} grs_func_chain;                  /* 9 bytes (packed) */
#pragma pack(pop)
LG_STATIC_ASSERT(sizeof(grs_func_chain) == 9, grs_func_chain_size);
/* Device/canvas table entries are declared void (*)(); GR_IFN casts one whose result is used
 * (so the plain C build, where WATCOM_CALL is a direct call, compiles). */
#define GR_IFN(f) ((int (*)())(f))
#define CHN_ON 1
#define CHN_GEN 2

/* Edge functions of TMAPFCN.C (0x34B410..), stored in grs_tmap_loop_info.left/right_edge_func
 * (declared here for the standalone build; the verification build reaches them as W_ thunks). */
void gri_x_edge();
void gri_ix_edge();
void gri_rgbx_edge();
void gri_scale_edge();

/* ---------------- globals ----------------
 * GRD.C (data 0x35F8BC.., bss 0x449E90..0x44B244) */
extern grs_canvas *grd_canvas;            /* 0x44B1A0 current canvas */
extern void (**grd_canvas_table)();       /* 0x44B194 current canvas function table (NOT 0x44B198) */
extern void (**grd_pixel_table)();        /* 0x44B198 canvas table of the canvas's own bitmap type */
extern void (**grd_device_table)();       /* 0x44B19C current device driver table */
extern int (*grd_detect_func)(grs_sys_info *info); /* 0x44B190 set to gr_detect by gr_init */
extern grs_canvas *grd_visible_canvas;    /* 0x44B1A4 */
extern grs_canvas *grd_screen_canvas;     /* 0x44B1A8 */
extern grs_sys_info grd_info;             /* 0x44B1B8 */
extern short grd_screen_row;              /* 0x44B240 row of a device bitmap; set by set_mode (name ours) */
extern short grd_canvas_index;            /* 0x44B23C */
extern short grd_pixel_index;             /* 0x44B23E */
extern uchar chn_flags;                   /* 0x44B242 CHN_ON | CHN_GEN */
extern uchar grd_default_pal[768];        /* 0x44AE90 */
extern grs_rgb grd_default_bpal[1024];    /* 0x449E90 */
extern uchar *grd_pal;                    /* 0x35F8C0 (= grd_default_pal) */
extern uchar *grd_ipal;                   /* 0x35F8C8 */
extern int grd_mode;                      /* 0x35F8D0 -1 until gr_set_mode */
extern bool grd_interrupt;                /* 0x35F8D4 */
extern grs_mode_info grd_mode_info[GRD_MODES]; /* 0x364DF0 */
extern int grd_active;                    /* 0x36C994 (INIT.C) */
/* CANVAS.C */
extern int grd_canvas_stackp;             /* 0x35F8D8 */
extern grs_canvas *grd_canvas_stack[16];  /* 0x44B244 */
extern grs_context grd_defgc;             /* 0x360484 (CONTEXT.C) */
/* table lists */
extern void (**grd_canvas_table_list[BMT_TYPES])();     /* 0x365194 */
extern void (**grd_function_table_list[BMT_TYPES])();   /* 0x3604B0 */
extern void (**grd_uline_fill_table_list[BMT_TYPES])(); /* 0x3604D4 */
extern void (**grd_canvas_table_set[GRD_TABLE_SETS])();     /* 0x368680 */
extern void (**grd_function_table_set[GRD_TABLE_SETS])();   /* 0x368650 */
extern void (**grd_uline_fill_table_set[GRD_TABLE_SETS])(); /* 0x3686B0 */
extern void (**grd_function_fill_table)();/* 0x44C5A0 */
extern void (**grd_uline_fill_table)();   /* 0x44C5A8 */
/* allocation hooks (GRMALLOC.C) */
extern void *(WATCOMCALL *gr_malloc_p)(size_t n);  /* 0x360470 (= malloc) */
extern void (WATCOMCALL *gr_free_p)(void *p);      /* 0x360474 (= free) */
#define gr_malloc(n) (*gr_malloc_p)(n)
#define gr_free(p) (*gr_free_p)(p)

/* Declared differently by 2d_prim.h / 2d_tmap.h for now (see above). */
#if !defined(TN_2D_TMAP_H)
extern grs_screen *grd_screen;            /* 0x35F8BC */
extern grs_rgb *grd_bpal;                 /* 0x35F8C4 (= grd_default_bpal) */
extern grs_drvcap *grd_cap;               /* 0x35F8CC (= &grd_mode_cap) */
extern grs_drvcap grd_mode_cap;           /* 0x44B1AC */
extern void (**grd_function_table)();     /* 0x44C5A4 */
extern void (**grd_uline_fill_vector)();  /* 0x44C5AC */
extern uchar **tluc8tab_p;                /* 0x360478 -> uchar *[256] at 0x44C180 */
#endif

#define grd_bm (grd_canvas->bm)
#define grd_gc (grd_canvas->gc)
#define grd_int_clip (grd_gc.clip.i)
#define grd_fix_clip (grd_gc.clip.f)
#define grd_clip (grd_int_clip)

/* Canvas function table indices (ICANVAS.H; Terra Nova's tables have the same 324 entries in
 * the same order as System Shock's).  See docs/decompilation/2d-map.md for the full list. */
#define GRD_CANVAS_FUNCS 324

/* ---------------- context macros (CTXMAC.H) ---------------- */
#define gr_init_gc(c) ((c)->gc = grd_defgc, \
                       (c)->gc.clip.f.right = ((fix)(c)->bm.w) << 16, \
                       (c)->gc.clip.f.bot = ((fix)(c)->bm.h) << 16)
#define gr_cset_fix_cliprect(c, l, t, r, b) \
    ((c)->gc.clip.i.sten = NULL, (c)->gc.clip.f.left = (l), (c)->gc.clip.f.top = (t), \
     (c)->gc.clip.f.right = (r), (c)->gc.clip.f.bot = (b))

/* ---------------- core functions (2d-core) ---------------- */
/* canvas.c */
void gr_set_canvas(grs_canvas *c);                                   /* 0x2EAB30 */
int gr_push_canvas(grs_canvas *c);                                   /* 0x2EABF8 */
grs_canvas *gr_pop_canvas(void);                                     /* 0x2EAC2C */
void gr_init_canvas(grs_canvas *c, uchar *p, uchar type, short w, short h); /* 0x2EAC5C */
void gr_init_sub_canvas(grs_canvas *sc, grs_canvas *dc, short x, short y, short w, short h); /* 0x2EACF4 */
void gr_make_canvas(grs_bitmap *bm, grs_canvas *c);                  /* 0x2EAD88 */
grs_canvas *gr_alloc_canvas(uchar type, short w, short h);           /* 0x2EAE00 */
void gr_free_canvas(grs_canvas *c);                                  /* 0x2EAE78 */
grs_canvas *gr_alloc_sub_canvas(grs_canvas *c, short x, short y, short w, short h); /* 0x2EAE9C */
void gr_free_sub_canvas(grs_canvas *c);                              /* 0x2EAEEC */
/* valloc.c */
uchar *valloc(short w, short h);                                     /* 0x309AE0 */
void vfree(uchar *p);                                                /* 0x309AEC */
extern uchar grd_valloc_mode;                                        /* 0x3651B8 */
/* bitmap.c */
void gr_init_bitmap(grs_bitmap *bm, uchar *p, uchar type, ushort flags, short w, short h); /* 0x2F21D4 */
void gr_init_sub_bitmap(grs_bitmap *sbm, grs_bitmap *dbm, short x, short y, short w, short h); /* 0x2F22C4 */
grs_bitmap *gr_alloc_bitmap(uchar type, ushort flags, short w, short h); /* 0x2F2404 */
#define gr_init_bm gr_init_bitmap
#define gr_init_sub_bm gr_init_sub_bitmap
#define gr_alloc_bm gr_alloc_bitmap

/* init.c / detect.c / close.c */
int gr_init(void);                                                   /* 0x304BE0 */
int gri_init(void);                                                  /* 0x31AA90 */
int gr_detect(grs_sys_info *info);                                   /* 0x31AA30 */
int gr_close(void);                                                  /* 0x3090D0 */
/* statestk.c */
int gr_push_video_state(int flags);                                  /* 0x31D5D0 */
int gr_pop_video_state(int clear);                                   /* 0x31D628 */
/* mode.c / screen.c */
int gr_set_mode(int mode, int clear);                                /* 0x308DF0 */
#if !defined(TN_2D_TMAP_H)
grs_screen *gr_alloc_screen(short w, short h);                       /* 0x308EA0 */
void gr_free_screen(grs_screen *s);                                  /* 0x309068 */
void gr_set_screen(grs_screen *s);                                   /* 0x3090B0 */
#endif
/* chain.c */
void chn_null(void);                                                 /* 0x31C440 */
grs_func_chain *gr_chain_add_over(int n, void (*f)());               /* 0x31C4C4 */
grs_func_chain *gr_chain_add_before(int n, void (*f)());             /* 0x31C548 */
grs_func_chain *gr_chain_add_after(int n, void (*f)());              /* 0x31C58C */
void gr_unchain(int n);                                              /* 0x31C5F0 */
void gr_rechain(int n);                                              /* 0x31C630 */
void gr_unchain_all(void);                                           /* 0x31C668 */
void gr_rechain_all(void);                                           /* 0x31C680 */
void (*chain_rest(void))();                                          /* 0x31C698 */
void gr_force_generic(void);                                         /* 0x31C6F0 */
void gr_unforce_generic(void);                                       /* 0x31C724 */
/* pal.c */
extern uchar grd_gamma_on;                                           /* 0x35F594 (name ours) */
extern uchar grd_gamma_tab[256];                                     /* 0x35F595 */
extern fix grd_gamma;                                                /* 0x44B7F8 */
void gri_set_gamma_screen_pal(int start, int n);                     /* 0x2E7C30 */
void gr_set_pal(int start, int n, uchar *pal_data);                  /* 0x2E7CC4 */
void gr_get_pal(int start, int n, uchar *pal_data);                  /* 0x2E7DA8 */
void gr_set_gamma_pal(int start, int n, fix gamma);                  /* 0x2ED500 */
fix fix_pow(fix x, fix y);                                           /* 0x30FC90 (FIX library) */
void *temp_malloc(size_t n);                                         /* 0x30918C (LG tmpalloc) */
int temp_free(void *p);                                              /* 0x3091CC (LG tmpalloc) */
/* grmalloc.c */
void gr_set_malloc(void *(WATCOMCALL *malloc_func)(size_t n));       /* 0x2F1B70 */
void gr_set_free(void (WATCOMCALL *free_func)(void *p));             /* 0x2F1B78 */
/* tluctab.c */
extern uchar *tluc8tab[256];                                         /* 0x44C180 (via tluc8tab_p) */
extern uchar *tluc8ltab[256];                                        /* 0x44BD80 */
extern uchar **tluc8ltab_p;                                          /* 0x36047C */
extern int tluc8nstab;                                               /* 0x360480 */
extern uchar *tluc8stab;                                             /* 0x44C580 */
uchar *gr_init_blend_table(uchar *p, fix frac, grs_rgb color, uchar *pal); /* 0x2F1B80 */
uchar *gr_init_translucency_table(uchar *p, fix opacity, fix purity, grs_rgb color); /* 0x2F1C64 */
uchar *gr_init_lit_translucency_table(uchar *p, fix opacity, fix purity, grs_rgb color, grs_rgb light); /* 0x2F1DB4 */
uchar *gr_init_lit_translucency_tables(uchar *p, fix opacity, fix purity, grs_rgb color, int n); /* 0x2F1F50 */
int gr_dump_tluc8_table(uchar *buf, int nlit);                       /* 0x2F1FD8 */
void gr_read_tluc8_table(uchar *buf);                                /* 0x2F2098 */
/* line.c / clplin.c / wirpoly.c */
extern void (*grd_line_clip_fill_table[GRD_LINE_TYPES])();          /* 0x3651C0 */
extern void (**grd_line_clip_fill_vector)();                         /* 0x3651E4 */
int gen_hline(short x0, short y0, short x1);                         /* 0x2ED5A0 */
int gri_hline_clip_fill(short x0, short y0, short x1, long c, long parm); /* 0x2ED5C8 */
int gen_vline(short x0, short y0, short y1);                         /* 0x2ED650 */
int gri_vline_clip_fill(short x0, short y0, short y1, long c, long parm); /* 0x2ED678 */
int gr_iline(short x0, short y0, short x1, short y1);                /* 0x2F2100 */
#if !defined(TN_2D_TMAP_H)     /* grs_vertex: see above */
int gri_iline_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1); /* 0x2F213C */
int gr_fix_line(fix x0, fix y0, fix x1, fix y1);                     /* 0x2F3D00 */
int gri_line_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1); /* 0x2F3D34 */
int gr_fix_sline(fix x0, fix y0, fix i0, fix x1, fix y1, fix i1);    /* 0x317C50 */
int gri_sline_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1); /* 0x317C94 */
int gr_clip_fix_line(fix *p0, fix *p1);                              /* 0x313D20 */
void gr_wire_upoly(uchar c, int n, grs_vertex **vpl);                /* 0x2F3AA0 */
void gr_wire_poly(uchar c, int n, grs_vertex **vpl);                 /* 0x2F3B20 */
void gr_wire_ucpoly(int n, grs_vertex **vpl);                        /* 0x2F3C08 */
void gr_wire_cpoly(int n, grs_vertex **vpl);                         /* 0x2F3C80 */
/* 2d-prim (src/2d/genclin.c, clpslin.c, recip.c ...) */
int gri_cline_clip(grs_vertex *v0, grs_vertex *v1);                  /* 0x346220 (Shock CLPCLIN.C) */
int gr_clip_fix_code(fix x, fix y);                                  /* 0x325470 */
int gri_sline_clip(grs_vertex *v0, grs_vertex *v1);                  /* 0x325890 */
int gri_cline_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1); /* 0x324EE0 */
int gri_wire_poly_line_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1); /* 0x324F98 */
int gri_wire_poly_cline_clip_fill(long c, long parm, grs_vertex *v0, grs_vertex *v1); /* 0x324F40 */
#endif
void gr_null(void);                                                  /* 0x324FF0 */
int gr_clip_fix_line4(fix *x0, fix *y0, fix *x1, fix *y1);           /* 0x3254C4 (Shock gr_clip_fix_line) */
int gen_fix_cline(fix x0, fix y0, long c0, fix x1, fix y1, long c1); /* 0x324E50 */
extern fix grd_recip_tab[1024];           /* 0x45F71C [0] = 0x7FFFFFFF, [n] = 0x10000/n */
extern uchar gr_sub_bm_enabled;           /* 0x36DF30 (name ours) per_umap quad-blend shortcut */
extern uchar *grd_last_sub_bm;            /* 0x36DF34 */
extern uchar *grd_last_dsub_bm;           /* 0x36DF38 (name ours) */
extern uchar *grd_sub_bm_buffer;          /* 0x36DF3C 32x32 */
/* genpix.c */
int gen_fill_pixel(long color, short x, short y);                    /* 0x2FAD20 */
int gen_set_pixel(long color, short x, short y);                     /* 0x2FAD60 */
int gen_set_pixel_interrupt(long color, short x, short y);           /* 0x2FADA0 */
int gr_clip_flat8_bitmap(grs_bitmap *bm, short *x, short *y);        /* 0x2FADE0 */
/* clprect.c / clpbm.c */
int gr_clip_rect(short *left, short *top, short *right, short *bot); /* 0x34A150 */
int gr_clip_mono_bitmap(grs_bitmap *bm, short *x, short *y);         /* 0x34AEA0 */
int gr_clip_flat24_bitmap(grs_bitmap *bm, short *x, short *y);       /* 0x34B030 */
/* blend.c */
extern uchar *grd_blend;                  /* 0x36EF34 (1<<levels)-1 tables of 64K */
extern uchar *grd_half_blend;             /* 0x36EF38 */
extern int grd_log_blend_levels;          /* 0x36EF3C */
void gri_build_blend(uchar *dst, int frac);                          /* 0x34B160 */
int gr_free_blend(void);                                             /* 0x34B2CC */
uchar gr_init_blend(int log_blend_levels);                           /* 0x34B2FC 0 = ok */
/* genel.c */
long *gri_elipse_spans(fix x0, fix y0, fix a, fix b);                /* 0x314C60 (name ours) */
int gen_fix_elipse(fix x0, fix y0, fix a, fix b);                    /* 0x34ACC0 */
int gen_int_elipse(short x0, short y0, short a, short b);            /* 0x34ADF8 (name ours) */
int gen_fix_oval(fix x0, fix y0, fix a, fix b);                      /* 0x34AE20 */
int gen_int_oval(short x0, short y0, short a, short b);              /* 0x34AE6C (name ours) */
int gen_fix_ring(fix x0, fix y0, fix r1, fix r2);                    /* 0x2F3D90 (name ours) */
int gen_int_ring(short x0, short y0, short r1, short r2);            /* 0x2F3F9C (name ours) */
/* rsdunpck.c */
uchar *gr_rsd8_unpack(uchar *src, uchar *dst);  /* 0x2FAFC0 (orig: parm [esi] [edi] value [edi]) */
void gr_delta8_unpack(uchar *dst, uchar *src);                       /* 0x2FAF1C (name ours) */
/* chr.c / str.c */
short gr_font_char_width(grs_font *f, char c);                       /* 0x2FCCE0 */
void gr_font_char_size(grs_font *f, char c, short *w, short *h);     /* 0x2FCD20 */
extern short *pCharPixOff;                                           /* 0x44C69C */
int gr_font_string_wrap(grs_font *pfont, char *ps, short width);     /* 0x2F3050 */
void gr_font_string_unwrap(char *s);                                 /* 0x2F3144 */
void gr_font_string_size(grs_font *f, char *s, short *w, short *h);  /* 0x2F31F0 */
/* outside the 2D library, called by it (original code) */
void *temp_mem_get_stack(void);                                      /* 0x309110 (LG tmpalloc) */
int temp_mem_init(void *stack);                                      /* 0x309118 (LG tmpalloc) */
void init_inverse_table(void);                                       /* 0x325A70 (2d-prim, recip.c) */

/* ---------------- device drivers (vga.c, vesa.c; names ours) ---------------- */
/* DPMI real-mode call register block (int 31h ax=0300h), shared by the drivers */
#pragma pack(push, 1)
typedef struct {
    ulong edi, esi, ebp, reserved, ebx, edx, ecx, eax;   /* +00 .. +1C */
    ushort flags, es, ds, fs, gs, ip, cs, sp, ss;        /* +20 .. +30 */
} dpmi_rmregs;
#pragma pack(pop)
LG_STATIC_ASSERT(sizeof(dpmi_rmregs) == 0x32, dpmi_rmregs_size);
extern dpmi_rmregs vesa_rm;               /* 0x45E318 */
extern void (*vga_device_table[GRD_DEVICE_FUNCS])();    /* 0x36DEEC */
extern void (*vesa_device_table[GRD_DEVICE_FUNCS])();   /* 0x36D10C */
/* vga.c (VGA / Mode X) */
int vga_set_crtc_regs(int which);                                    /* 0x31D120 */
void vga_set_pal(int start, int n, uchar *pal);                      /* 0x31D1F0 */
void vga_get_pal(int start, int n, uchar *pal);                      /* 0x31D440 */
int vga_set_mode(int mode, int flags);                               /* 0x31D470 */
void vga_set_focus(int x, int y);                                    /* 0x31D574 */
int vga_save_bios_state(uchar *buf);                                 /* 0x349C70 */
int vga_save_vesa_state(uchar *buf);                                 /* 0x349E30 */
int vga_save_state(uchar *buf);                                      /* 0x349ED4 */
int vga_restore_bios_state(uchar *buf);                              /* 0x349F00 */
int vga_restore_vesa_state(uchar *buf);                              /* 0x34A0B0 */
int vga_restore_state(uchar *buf);                                   /* 0x34A118 */
void vga_get_focus(void);                                            /* 0x34A13C */
void vga_set_width(int w);                                           /* 0x34A140 */
/* vesa.c (VESA BIOS Extensions through DPMI) */
extern void *vesa_pm_info;                /* 0x44DB10 copy of the VBE 2.0 protected mode interface */
extern long vesa_bytes_per_line;          /* 0x44DB14 */
extern ushort vesa_selector;              /* 0x44DB18 selector of the frame buffer / window */
extern uchar vesa_gran_shift;             /* 0x44DB1A */
extern uchar vesa_dac_switchable;         /* 0x44DB1B VBE capabilities bit 0 */
extern uchar vesa_pal_blank;              /* 0x44DB1C VBE capabilities bit 2 (use 4F09 bl=80h) */
extern short vesa_version;                /* 0x364DCC (-1 until vesa_get_info) */
extern long vesa_dac_width;               /* 0x364DD0 */
extern uchar vesa_logical_ok;             /* 0x364DD4 4F06h usable */
extern uchar vesa_linear;                 /* 0x364DD5 current mode uses the linear frame buffer */
extern void *vesa_pm_bank;                /* 0x364DD8 VBE 2.0 protected mode entry points */
extern void *vesa_pm_start;               /* 0x364DDC */
extern void *vesa_pm_pal;                 /* 0x364DE0 */
extern uchar *vesa_lfb;                   /* 0x364DE4 linear address of the frame buffer */
extern uchar *vesa_lfb_addr;              /* 0x364DE8 */
extern ulong vesa_lfb_phys;               /* 0x364DEC */
void *vesa_get_pm_func(int which);                                   /* 0x307910 */
ushort vesa_alloc_selector(ulong base, ulong limit);                 /* 0x3079F4 */
void vesa_check_logical(int w, int h);                               /* 0x307AC0 */
char vesa_get_mode_info(long *w, long *bytes_per_line);              /* 0x307B64 */
int vesa_set_logical_width(int w, long *lines);                      /* 0x307C80 */
long vesa_set_dac_width(long bits);                                  /* 0x307D54 */
long vesa_get_dac_width(void);                                       /* 0x307E20 */
long vesa_reset_dac(void);                                           /* 0x307E94 */
void vesa_set_pal(int start, int n, uchar *pal);                     /* 0x307EA8 */
void vesa_get_pal(int start, int n, uchar *pal);                     /* 0x308040 */
int vesa_set_mode_tables(int mode, uchar linear);                    /* 0x3081F0 */
int vesa_set_mode(int mode, int flags);                              /* 0x308490 */
int vesa_get_mode(void);                                             /* 0x3085BC */
int vesa_set_width(int w);                                           /* 0x308640 */
int vesa_get_width(void);                                            /* 0x308650 */
int vesa_get_bytes_per_line(void);                                   /* 0x3086DC */
int vesa_max_lines(void);                                            /* 0x308768 */
void vesa_get_focus(long *x, long *y);                               /* 0x30880C */
void vesa_set_focus(int x, int y);                                   /* 0x308880 */
int vesa_get_info(grs_sys_info *info);                               /* 0x3088DC */
void vesa_close(void);                                               /* 0x308D4C */
int vesa_set_bank_pm(int bank);                                      /* 0x31D220 */
int vesa_set_bank_pm2(int bank);                                     /* 0x31D240 */
int vesa_set_bank_rm(int bank);                                      /* 0x31D269 */
int vesa_set_bank_rm2(int bank);                                     /* 0x31D2E7 */
int vesa_set_start(int x, int y, int wait);                          /* 0x31D383 */
void vesa_pm_set_pal(int sub, int start, int n, uchar *buf);         /* 0x31D418 (custom regs) */


/* ---------------- 2d-prim declarations (copied from include/2d_prim.h, worker 2d-prim) ----------------
 * Guarded by TN_2D_PRIM_H: including 2d.h makes 2d_prim.h a no-op, and 2d_prim.h (which defines
 * TN_2D_PRIM_H before including 2d.h) skips this copy. Keep the two in step. Skipped inside
 * 2d_tmap.h translation units, like the grs_screen block above (its types are not seen yet). */
#if !defined(TN_2D_PRIM_H) && !defined(TN_2D_TMAP_H)
#define TN_2D_PRIM_H
typedef struct {
    short tmap_type;               /* GRC_XXX */
    short flags;                   /* TMF_XXX */
    uchar *clut;
} grs_tmap_info;

#define TMF_PER 1
#define TMF_CLUT 2
#define TMF_FLOOR 4
#define TMF_WALL 4

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

#endif /* TN_2D_PRIM_H */

#endif
