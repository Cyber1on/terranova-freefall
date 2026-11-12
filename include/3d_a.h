/* 3d_a.h -- worker 3d-a's part of the Looking Glass 3D library (Terra Nova v1.09):
 * alloc, vector, points, matrix, the TN C helpers (g3util), star.c, star3d and the clip-file
 * helpers.  Prototypes of the decompiled C and the module-private data they touch.
 *
 * Most of these routines are hand-written asm in the original with non-Watcom register
 * conventions (symbols rows with nargs=custom, no thunk).  The C versions have plain cdecl
 * signatures (System Shock 3D.H argument order); the register convention of each original
 * entry is given in the comment of each C function and in symbols-3d-a.csv.
 */
#ifndef TN_3D_A_H
#define TN_3D_A_H
#include "3d.h"

/* ---------------- module-private data (rows in symbols-3d-a.csv) ---------------- */
/* vector.asm temporaries used by g3_compute_normal_quick */
extern g3s_vector g3d_norm_v0;        /* 0x360568 p1 - p0 (name ours) */
extern g3s_vector g3d_norm_v1;        /* 0x360574 p2 - p1 (name ours) */
extern long g3d_norm_hi[3];           /* 0x360580 high dwords of the 64-bit cross product (name ours) */
/* points.asm */
extern fix g3d_rot_tmp;               /* 0x3605B4 x result of the rotate cores (name ours) */
/* matrix.asm temporaries */
extern fix g3d_sin_x, g3d_cos_x;      /* 0x364490 0x364494 sin/cos of angvec.tx (name ours) */
extern fix g3d_sin_z, g3d_cos_z;      /* 0x364498 0x36449C sin/cos of angvec.tz (name ours) */
extern fix g3d_sin_y, g3d_cos_y;      /* 0x3644A0 0x3644A4 sin/cos of angvec.ty (name ours) */
extern fix g3d_vrot_x, g3d_vrot_y;    /* 0x3644A8 0x3644AC g3_vec_rotate temporaries (name ours) */
extern fix g3d_pyr_t[6];              /* 0x3644B0 view pyramid plane terms (name ours) */
extern fix g3d_pyr_den;               /* 0x3644C8 view pyramid determinant (name ours) */
/* polygon.asm data read by star.c (owned by polygon, worker 3d-b) */
extern long g3d_star_size;            /* 0x364444 star pixel size (name ours) */
extern long g3d_star_minw;            /* 0x364448 canvas width from which stars are antialiased (name ours) */
extern long g3d_poly_n;               /* 0x3640DC vertex count after the last draw_poly clip (name ours) */
extern g3s_phandle g3d_poly_clipped[100]; /* 0x364274 clipped vertex list (name ours) */
/* star.c BSS */
typedef struct { short x, y, z; } g3s_star; /* 6 bytes, unit vector / 2 (name ours) */
extern g3s_star *std_vec;             /* 0x44CE18 (name ours) */
extern long std_color_base;           /* 0x44CE1C 0xD0 (name ours) */
extern uchar *std_col;                /* 0x44CE20 colour per star (name ours) */
extern long std_color_range;          /* 0x44CE24 0x10 (name ours) */
extern long std_num;                  /* 0x44CE28 (name ours) */
extern uchar std_shade[256];          /* 0x44CD18 brightness -> colour table (name ours) */

/* addresses of other workers' asm entries called from this code (original code, custom
 * registers; called through g3a_asm_call) */
#define G3_ORIG_DRAW_POLY 0x2F95EE    /* eax=color ecx=n esi=list -> eax */
#define G3_ORIG_DRAW_LINE 0x2F97D8    /* esi=p0 edi=p1 -> eax */
#define G3_ORIG_CLIP_POLYGON 0x316A74 /* ecx=n esi=src edi=dest -> eax */
#define G3_ORIG_SCALE_OBJECT 0x2F8705 /* eax=scale */
/* calls f with the given registers (ebp preserved); returns eax.  In alloc.c. */
long g3a_asm_call(long f, long eax, long ebx, long ecx, long edx, long esi, long edi);

/* division-overflow trap services used by g3_init (DOS4G/DPMI) */
void g3a_div0_install(long on);                 /* 0x2DF868 (name ours) */
void g3a_div0_add_range(void *start, void *end); /* 0x303F70 (name ours) */
int rand(void);                                  /* 0x317DB5 CRT */
fix fix_pow(fix x, fix y);                       /* 0x30FC90 FIX (orig) */

/* ---------------- alloc.asm ---------------- */
short g3_init(short max_points, int user_x_axis, int user_y_axis, int user_z_axis);
void g3_shutdown(void);
int g3_count_free_points(void);
int g3_alloc_list(int n, g3s_phandle *p);
g3s_phandle g3_alloc_point(void);
void g3_free_point(g3s_phandle p);
void g3_free_list(int n_points, g3s_phandle *p);
g3s_phandle g3_dup_point(g3s_phandle p);
void g3_start_frame_wh(long ww, long wh, fix ratio);
void g3_start_frame(void);
int g3_end_frame(void);

/* ---------------- vector.asm ---------------- */
void g3_vec_add(g3s_vector *dest, g3s_vector *src1, g3s_vector *src2);
void g3_vec_sub(g3s_vector *dest, g3s_vector *src1, g3s_vector *src2);
void g3_vec_scale(g3s_vector *dest, g3s_vector *src, fix s);
fix g3_vec_mag(g3s_vector *v);
fix g3_vec_dotprod(g3s_vector *v0, g3s_vector *v1);
void g3_compute_normal(g3s_vector *norm, g3s_vector *v0, g3s_vector *v1, g3s_vector *v2);
void g3_vec_normalize(g3s_vector *v);
void g3_compute_normal_quick(g3s_vector *norm, g3s_vector *v0, g3s_vector *v1, g3s_vector *v2);

/* ---------------- points.asm ---------------- */
g3s_phandle g3_rotate_norm(g3s_vector *v);
void g3_world_to_object(g3s_vector *dest, g3s_vector *v);
g3s_phandle g3_rotate_point(g3s_vector *v);
g3s_phandle g3_transform_point(g3s_vector *v);
int g3_project_point(g3s_phandle p);
int g3_transform_list(int n, g3s_phandle *dest_list, g3s_vector *v);
int g3_rotate_list(int n, g3s_phandle *dest_list, g3s_vector *v);
int g3_project_list(int n, g3s_phandle *point_list);
g3s_phandle g3_rotate_light_norm(g3s_vector *v);
void rotate_norm(g3s_vector *v, g3s_vector *r);
void do_norm_rotate(fix x, fix y, fix z, g3s_vector *r);
void xlate_rotate_point(g3s_vector *v, g3s_vector *r);
void do_rotate(fix x, fix y, fix z, g3s_vector *r);
void g3_rotate_delta_x(g3s_vector *dest, fix dx);
void g3_rotate_delta_y(g3s_vector *dest, fix dy);
void g3_rotate_delta_z(g3s_vector *dest, fix dz);
void g3_rotate_delta_xz(g3s_vector *dest, fix dx, fix dz);
void g3_rotate_delta_xy(g3s_vector *dest, fix dx, fix dy);
void g3_rotate_delta_yz(g3s_vector *dest, fix dy, fix dz);
void g3_rotate_delta_xyz(g3s_vector *dest, fix dx, fix dy, fix dz);
void g3_rotate_delta_v(g3s_vector *dest, g3s_vector *src);
g3s_phandle g3_copy_add_delta_v(g3s_phandle src, g3s_vector *delta);
void g3_add_delta_v(g3s_phandle p, g3s_vector *delta);
void g3_add_delta_x(g3s_phandle p, fix dx);
void g3_add_delta_y(g3s_phandle p, fix dy);
void g3_add_delta_z(g3s_phandle p, fix dz);
void g3_add_delta_xy(g3s_phandle p, fix dx, fix dy);
void g3_add_delta_xz(g3s_phandle p, fix dx, fix dz);
void g3_add_delta_yz(g3s_phandle p, fix dy, fix dz);
void g3_add_delta_xyz(g3s_phandle p, fix dx, fix dy, fix dz);
g3s_phandle g3_copy_add_delta_x(g3s_phandle src, fix dx);
g3s_phandle g3_copy_add_delta_y(g3s_phandle src, fix dy);
g3s_phandle g3_copy_add_delta_z(g3s_phandle src, fix dz);
g3s_phandle g3_replace_add_delta_x(g3s_phandle src, g3s_phandle dst, fix dx);
g3s_phandle g3_replace_add_delta_y(g3s_phandle src, g3s_phandle dst, fix dy);
g3s_phandle g3_replace_add_delta_z(g3s_phandle src, g3s_phandle dst, fix dz);
g3s_phandle g3_copy_add_delta_xy(g3s_phandle src, fix dx, fix dy);
g3s_phandle g3_copy_add_delta_xz(g3s_phandle src, fix dx, fix dz);
g3s_phandle g3_copy_add_delta_yz(g3s_phandle src, fix dy, fix dz);
g3s_phandle g3_copy_add_delta_xyz(g3s_phandle src, fix dx, fix dy, fix dz);

/* ---------------- g3util (TN Watcom C, standard registers) ---------------- */
long g3_screen_size(fix r, fix z);
long g3_screen_size_x(fix r, fix z);
long g3_screen_size_y(fix r, fix z);
long g3_set_light_shift(long n);
long g3_set_light_max(long n);
void g3_cross_product(g3s_vector *dest, g3s_vector *a, g3s_vector *b);

/* ---------------- star.c (Watcom C) ---------------- */
void g3_star_set(long n, g3s_star *vec, uchar *col);
long g3_star_alloc(long n);
void g3_star_free(void);
fix star_dist(g3s_vector *v);
void g3_star_poly(long n, g3s_phandle *list);
void g3_star_clip_poly(long n, g3s_phandle *list);
void star_reset_bounds(void);
void star_pixel(short x, short y, long a, long b, long c);
void star_color(fix sx, fix sy, long c);
void star_build_table(void);
void g3_star_render(void);
void g3_star_random(uchar color, uchar range);

/* ---------------- star3d.asm ---------------- */
g3s_phandle star_transform(g3s_vector *v);

/* ---------------- matrix.asm ---------------- */
void g3_set_view_angles(g3s_vector *pos, g3s_angvec *angles, int rotation_order, fix zoom);
void g3_set_view_matrix(g3s_vector *pos, g3s_matrix *m, fix zoom);
void process_view_matrix(void);
void scale_view_matrix(void);
void compute_invalid(g3s_matrix *m);
void angles_2_matrix(g3s_angvec *angles, g3s_matrix *m, int rotation_order);
void compute_ZXY(g3s_matrix *m);
void compute_XZY(g3s_matrix *m);
void compute_ZYX(g3s_matrix *m);
void compute_YXZ(g3s_matrix *m);
void compute_YZX(g3s_matrix *m);
void compute_XYZ(g3s_matrix *m);
uchar code_point(g3s_phandle p);
void g3_vec_rotate(g3s_vector *dest, g3s_vector *src, g3s_matrix *m);
void g3_transpose(g3s_matrix *m);
void g3_copy_transpose(g3s_matrix *dest, g3s_matrix *src);
void g3_matrix_x_matrix(g3s_matrix *dest, g3s_matrix *src1, g3s_matrix *src2);
void get_pyr_vector(g3s_vector *corner);
void g3_get_view_pyramid(g3s_vector *corners);

/* ---------------- clip-file helpers (TN Watcom C) ---------------- */
typedef struct {                       /* (name ours) */
    long pad[2];                       /* +0 not read */
    g3s_vector hi;                     /* +8 */
    g3s_vector lo;                     /* +0x14 */
    g3s_vector org;                    /* +0x20 (g3_get_object_axes) */
} g3s_box;
void g3_draw_box(g3s_box *b);
void g3_get_object_axes(g3s_box *b, g3s_vector *axes);

/* ---------------- fixed-point helpers ---------------- */
/* imul / shrd eax,edx,16 */
static inline fix g3a_mul(fix a, fix b) { return (fix)(((long long)a * b) >> 16); }
/* sum of three 64-bit products, bits 16..47 (shr eax,16 / shl edx,16 / or) */
static inline fix g3a_dot3(fix a, fix b, fix c, fix d, fix e, fix f)
{
    return (fix)(((long long)a * b + (long long)c * d + (long long)e * f) >> 16);
}
static inline fix g3a_dot2(fix a, fix b, fix c, fix d)
{
    return (fix)(((long long)a * b + (long long)c * d) >> 16);
}
/* (a << 16 as a 48-bit value) idiv b; faults on overflow in the original */
static inline fix g3a_div(fix a, fix b) { return (fix)(((long long)a << 16) / b); }

#endif
