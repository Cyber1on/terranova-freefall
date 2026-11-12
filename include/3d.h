/* 3D: Looking Glass 3D library (x:\prj\tech\libsrc\3d\), as linked into Terra Nova v1.09.
 * Layouts and addresses are read from the binary; names follow System Shock's 3D.H / GLOBALV.C
 * where the routine is the same, otherwise they are ours.  See docs/decompilation/3d-map.md.
 *
 * OWNERSHIP: written by the 3d-map worker.  The three 3D workers may ADD declarations
 * (additions only, keep it compiling); module-private declarations go in their own headers.
 * Data symbols: the linker symbols for these externs (kind=data rows) are owned by worker A
 * (symbols-3d-a.csv) so they are defined exactly once.
 */
#ifndef TN_3D_H
#define TN_3D_H
#include "lg.h"
#include "fix.h"
#include "2d.h"

#pragma pack(push, 1)

typedef short sfix;                /* 8.8 fixed point (u, v, i in points) */

/* ---------------- vectors and matrices ---------------- */
typedef struct {
    fix x, y, z;                   /* +0 +4 +8 */
} g3s_vector;
LG_STATIC_ASSERT(sizeof(g3s_vector) == 12, g3s_vector_size);

typedef struct {
    fixang tx, ty, tz;             /* +0 +2 +4 (ushort angles, 0x10000 = 360 degrees) */
} g3s_angvec;
LG_STATIC_ASSERT(sizeof(g3s_angvec) == 6, g3s_angvec_size);

/* Row-major 3x3: m1 m2 m3 / m4 m5 m6 / m7 m8 m9 (Shock layout). */
typedef struct {
    fix m1, m2, m3, m4, m5, m6, m7, m8, m9;
} g3s_matrix;
LG_STATIC_ASSERT(sizeof(g3s_matrix) == 36, g3s_matrix_size);

/* ---------------- points ---------------- */
/* codes (clip codes, point +0x14) */
#define CC_OFF_LEFT 0x01
#define CC_OFF_RIGHT 0x02
#define CC_OFF_BOT 0x04
#define CC_OFF_TOP 0x08
#define CC_CLIP_OVERFLOW 0x10
#define CC_BEHIND 0x80
#define CC_OFF (CC_OFF_LEFT | CC_OFF_RIGHT | CC_OFF_BOT | CC_OFF_TOP)

/* p3_flags (point +0x15) */
#define PF_U 0x01
#define PF_V 0x02
#define PF_I 0x04
#define PF_PROJECTED 0x08
#define PF_RGB 0x10
#define PF_CLIPPNT 0x20            /* point was created by the clipper (freed with it) */
#define PF_LIT 0x40
#define PF_UVSAVED 0x80            /* TN: +0x16 temporarily holds a uv pointer (clip/tmap, 0x316554/0x316510) */

/* 32-byte point (g3_init allocates n*0x20 and links the free list through +0). */
typedef struct g3s_point {
    union {
        fix x;                     /* +0x00 */
        struct g3s_point *next;    /* +0x00 free-list link while the point is free */
    };
    fix y;                         /* +0x04 */
    fix z;                         /* +0x08 */
    fix sx;                        /* +0x0C screen x (after projection) */
    fix sy;                        /* +0x10 screen y */
    uchar codes;                   /* +0x14 CC_xxx */
    uchar p3_flags;                /* +0x15 PF_xxx */
    union {
        struct {
            sfix u;                /* +0x16 */
            sfix v;                /* +0x18 */
            sfix i;                /* +0x1A */
        };
        ulong rgb;                 /* +0x16 (PF_RGB; overlaps u, v) */
        void *uvp;                 /* +0x16 TN: saved uv pointer while PF_UVSAVED */
    };
    uchar pad[4];                  /* +0x1C..0x1F never seen used */
} g3s_point;
LG_STATIC_ASSERT(sizeof(g3s_point) == 32, g3s_point_size);

typedef g3s_point *g3s_phandle;

/* returned in bx by g3_check_codes (bl = or, bh = and) */
typedef struct {
    uchar or;
    uchar and;
} g3s_codes;
LG_STATIC_ASSERT(sizeof(g3s_codes) == 2, g3s_codes_size);

/* angles_2_matrix rotation orders (index of the jump table at 0x2FA40E) */
#define ORDER_XYZ 0
#define ORDER_YXZ 1
#define ORDER_YZX 3
#define ORDER_XZY 4
#define ORDER_ZXY 6
#define ORDER_ZYX 7                /* TN quirk: table slot 7 = compute_XYZ, compute_ZYX unused */

/* axis codes for g3_init */
#define AXIS_RIGHT 1
#define AXIS_UP 2
#define AXIS_IN 3
#define AXIS_LEFT (-AXIS_RIGHT)
#define AXIS_DOWN (-AXIS_UP)
#define AXIS_OUT (-AXIS_IN)

/* instance stacks (instance.asm) */
#define G3_MAX_DEPTH 8
typedef struct {
    g3s_matrix m;                  /* +0x00 */
    g3s_vector p;                  /* +0x24 */
} g3s_mstack_entry;                /* 0x30 bytes */
LG_STATIC_ASSERT(sizeof(g3s_mstack_entry) == 0x30, g3s_mstack_entry_size);

/* poly types (polygon.asm, 0x364442) */
#define G3_POLY 0
#define G3_TLUC_POLY 1
#define G3_SPOLY 2
#define G3_TLUC_SPOLY 3
#define G3_CPOLY 4
#define G3_SLINE 0xFF

/* interp opcode count */
#define N_OPS 53

#pragma pack(pop)

/* ======================= globals ======================= */
/* globv.asm block (RCS string at 0x362238) */
extern g3s_point *point_list;          /* 0x362260 */
extern short n_points;                 /* 0x362264 */
extern g3s_point *first_free;          /* 0x362266 free-list head */
extern g3s_matrix unscaled_matrix;     /* 0x36226A */
extern g3s_matrix view_matrix;         /* 0x36228E */
extern g3s_vector _view_position;      /* 0x3622B2 */
extern fix _view_zoom;                 /* 0x3622BE */
extern long view_heading;              /* 0x3622C2 dword slot, fixang in the low word */
extern long view_pitch;                /* 0x3622C6 */
extern long view_bank;                 /* 0x3622CA */
extern fix pixel_ratio;                /* 0x3622CE from grd_cap */
extern long window_width;              /* 0x3622D2 */
extern long window_height;             /* 0x3622D6 */
extern fix ww2;                        /* 0x3622DA */
extern fix wh2;                        /* 0x3622DE */
extern long _scrw;                     /* 0x3622E2 */
extern long _scrh;                     /* 0x3622E6 */
extern fix _biasx;                     /* 0x3622EA */
extern fix _biasy;                     /* 0x3622EE */
extern g3s_vector _matrix_scale;       /* 0x3622F2 */
extern g3s_vector horizon_vector;      /* 0x3622FE */
extern uchar shift_table[256];         /* 0x36230A */
extern long up_axis;                   /* 0x36240A */
extern long axis_x;                    /* 0x36240E */
extern long axis_z;                    /* 0x362412 */
extern long axis_y;                    /* 0x362416 */
extern long axis_x_ofs;                /* 0x36241A */
extern long axis_z_ofs;                /* 0x36241E */
extern long axis_y_ofs;                /* 0x362422 */
extern uchar axis_swap_flag;           /* 0x362426 */
extern uchar axis_neg_flag;            /* 0x362427 */
extern g3s_vector g3d_obj_cen;         /* 0x362428 TN: object centre (interp op 0x28, eval_light_obj_cen) (name ours) */
extern uchar g3d_clip_flags;           /* 0x362434 TN: clip interpolation flags (from 0x36443C / 0x3635BE) (name ours) */
extern uchar _g3d_light_type;          /* 0x362435 */
extern uchar *g3d_light_tab;           /* 0x362436 */
extern uchar g3d_light_shift;          /* 0x36243A TN (name ours) */
extern fix g3d_light_max;              /* 0x36243B TN (name ours) */
extern fix _g3d_amb_light;             /* 0x36243F */
extern fix _g3d_diff_light;            /* 0x362443 */
extern fix _g3d_spec_light;            /* 0x362447 */
extern fix _g3d_flash;                 /* 0x36244B */
extern g3s_vector _g3d_light_src;      /* 0x36244F  (save_context copies 27 dwords from here) */
extern g3s_vector _g3d_light_trans;    /* 0x36245B */
extern g3s_vector _g3d_light_vec;      /* 0x362467 */
extern g3s_vector _g3d_view_vec;       /* 0x362473 */
extern fix _g3d_ldotv;                 /* 0x36247F */
extern fix _g3d_sdotl;                 /* 0x362483 */
extern fix _g3d_sdotv;                 /* 0x362487 */
extern g3s_matrix _wtoo_matrix;        /* 0x36248B TN world-to-object */
extern g3s_vector _wtoo_position;      /* 0x3624AF (ends 0x3624BB) */
extern fix g3d_tmap_scale;             /* 0x362514 (name ours) */
extern long gouraud_base;              /* 0x362518 */

/* alloc.asm */
extern uchar g3d_axis_neg[3];          /* 0x35F984 (name ours) */

/* points.asm */
extern g3s_codes g_codes;              /* 0x3605BC */

/* interp.asm (RCS string 0x3605CA) */
extern void *poly_variant_table[3];    /* 0x3605E8 TN, used by op 0x33, patched by fill (name ours) */
extern void *opcode_table[N_OPS];      /* 0x3605F4 mutable: fill.asm swaps slots */
extern g3s_phandle resbuf[1000];       /* 0x3606C8 */
extern g3s_phandle poly_list[100];     /* 0x361668 (name ours) */
extern uchar vcolor_tab[32];           /* 0x3617F8 (name ours) */
extern long vpoint_tab[32];            /* 0x361818 (name ours) */
extern grs_bitmap *tmap_tab[64];       /* 0x361898 texture table (element type unverified) (name ours) */
extern void *g3d_interp_callbacks[16]; /* 0x361998 TN op 0x2A, written by game 0x28DD80 (name ours) */
extern g3s_vector g3d_interp_vecs[16]; /* 0x3619D8 TN op 0x2C (name ours) */
extern void *interp_parms;             /* 0x361A98 (name ours) */
extern void *interp_object;            /* 0x361A9C (name ours) */

/* instance.asm (RCS string 0x361C5A) */
extern g3s_mstack_entry g3d_matrix_stack[G3_MAX_DEPTH]; /* 0x361C7C (name ours) */
extern g3s_mstack_entry *g3d_matrix_stackp;  /* 0x361DFC (name ours) */
extern ulong g3d_context_stack[G3_MAX_DEPTH][27]; /* 0x361E00, 0x6C each (name ours) */
extern void *g3d_context_stackp;       /* 0x362160 (name ours) */
extern long g3d_stack_depth;           /* 0x362164 reset by process_view_matrix (name ours) */

/* polygon.asm (RCS string 0x3635CE) */
extern long g3d_poly_color;            /* 0x3640E0 (name ours) */
extern uchar g3d_poly_type;            /* 0x364442 G3_POLY.. / G3_SLINE (name ours) */
extern uchar g3d_detail_a;             /* 0x36444C (also read by 2D 0x327870, 0x34FC70) (name ours) */
extern uchar g3d_detail_b;             /* 0x36444D (name ours) */
extern long g3d_detail_dist;           /* 0x364450 (name ours) */

/* fill.asm (RCS string 0x365322) / detail */
extern void (*g3_tmap_func)();         /* 0x365310 */
extern void (*g3_lit_tmap_func)();     /* 0x365314 */

/* star3d.asm */
extern fix std_min_z;                  /* 0x3654E8 init 0x7FFFFFFF (name ours) */
extern fix std_max_z;                  /* 0x3654EC (name ours) */

#endif
