/* 3d_b.h -- private declarations of 3D worker B's modules: light.asm, tmap.asm, polygon.asm,
 * the detail setters (asm + detail.c) and clip.c (Terra Nova v1.09).
 *
 * OWNERSHIP: worker 3d-b.  Globals declared here that are not in 3d.h have their data rows in
 * symbols-3d-b.csv.
 *
 * Calling convention.  Almost every entry point here is hand-written asm with its own register
 * convention (see docs/decompilation/3d-functions.csv and the comment of each function).  The C versions take
 * the same values as ordinary (cdecl) arguments, in the order given by Shock's 3D.H pragmas.
 * Original code of the other 3D modules (vector/points/matrix: worker 3d-a) is called at its
 * original address through g3b_call(), which loads all registers from a g3b_regs block.
 */
#ifndef TN_3D_B_H
#define TN_3D_B_H
#include "3d.h"

/* ---------------- light.asm ---------------- */
#define LT_NONE 0
#define LT_SOLID 1
#define LT_DIFF 2
#define LT_SPEC 4
#define LT_NEAR_VIEW 8
#define LT_NEAR_LIGHT 16
#define LT_LOC_LIGHT 32
#define LT_GOUR 128

extern g3s_point *g3d_light_tmp1;      /* 0x362230 norm argument (Shock tmp1) */
extern g3s_point *g3d_light_tmp2;      /* 0x362234 pos argument (Shock tmp2) */

void g3_set_light_src(g3s_vector *l);              /* 0x2F88CC eax */
void g3_rotate_light_src(g3s_matrix *m);           /* 0x2F88D8 edi */
void g3_eval_vec_light(void);                      /* 0x2F88EA */
void g3_trans_loc_light(void);                     /* 0x2F8927 */
void g3_eval_loc_light(g3s_vector *pos);           /* 0x2F893E eax */
void g3_eval_view(g3s_vector *pos);                /* 0x2F89A2 eax */
void g3_eval_ldotv(void);                          /* 0x2F8A11 */
void g3_eval_light_obj_cen(g3s_point *pnt);        /* 0x2F8A28 (pnt = caller's edi) */
void g3_eval_view_ahead(void);                     /* 0x2F8A66 */
void check_for_near(void);                         /* 0x2F8A91 */
fix g3_light_diff(g3s_point *norm, g3s_point *pos);  /* 0x2F8AC8 eax, edx -> eax */
fix g3_light_spec(g3s_point *norm, g3s_point *pos);  /* 0x2F8B04 */
fix g3_light_dands(g3s_point *norm, g3s_point *pos); /* 0x2F8B7D */
fix g3_light(g3s_point *norm, g3s_point *pos);       /* 0x2F8C09 */

/* ---------------- tmap.asm ---------------- */
extern long g3d_tmap_index;            /* 0x362544 2D canvas function index of the mapper */
extern g3s_phandle *g3d_tmap_plist;    /* 0x362548 caller's point list */
extern grs_bitmap *g3d_tmap_bm;        /* 0x36254C bitmap being mapped */
extern grs_bitmap g3d_tmap_rsd_bm;     /* 0x362550 RSD bitmap unpacked by gr_rsd8_convert */
extern long g3d_tile_w;                /* 0x362568 tile counts of the quad tiles / compute_warp */
extern long g3d_tile_h;                /* 0x36256C */
extern fix g3d_warp_matrix[9];         /* 0x362570 */
extern g3s_phandle g3d_tmap_points[100];   /* 0x362594 copy of the point list for the clipper */
extern long g3d_tmap_uvs[100][2];      /* 0x362724 uv save buffer of clip_save_uv */
extern grs_vertex g3d_tmap_vbuf[100];  /* 0x362A44 */
extern grs_vertex *g3d_tmap_vlist[100];    /* 0x3633A4 */
extern long g3d_tmap_n;                /* 0x363534 point count before clipping */
extern g3s_vector g3d_warp_pos;        /* 0x363540 */
extern g3s_vector g3d_warp_u;          /* 0x363564 */
extern g3s_vector g3d_warp_v;          /* 0x363570 */
extern fix g3d_tile_uvw[3];            /* 0x363594 */
extern g3s_point *g3d_tile_origin;     /* 0x3635AC */
extern g3s_vector *g3d_tile_uvec;      /* 0x3635B0 */
extern g3s_vector *g3d_tile_vvec;      /* 0x3635B4 */
extern long g3d_tmap_type;             /* 0x3635B8 0/1 linear (unlit/lit), 2/3 perspective */
extern ushort g3d_tmap_codes;          /* 0x3635BC or (low byte) / and (high byte) */
extern uchar g3d_tmap_clip_flags[4];   /* 0x3635BE by type: 1 5 1 5 */

int g3_draw_tmap_tile(g3s_point *ul, g3s_vector *du, g3s_vector *dv, int n, g3s_phandle *pl, grs_bitmap *bm);  /* 0x2F8C4C */
int g3_light_tmap_tile(g3s_point *ul, g3s_vector *du, g3s_vector *dv, int n, g3s_phandle *pl, grs_bitmap *bm); /* 0x2F8C69 */
int g3_tmap_tile_t3(g3s_point *ul, g3s_vector *du, g3s_vector *dv, int n, g3s_phandle *pl, grs_bitmap *bm);    /* 0x2F8C86 */
int g3_tmap_tile_t2(g3s_point *ul, g3s_vector *du, g3s_vector *dv, int n, g3s_phandle *pl, grs_bitmap *bm);    /* 0x2F8CA3 */
int do_tmap_tile(g3s_point *ul, g3s_vector *du, g3s_vector *dv, int n, g3s_phandle *pl, grs_bitmap *bm);       /* 0x2F8CBE */
int g3_check_and_quad_tile_0(int w, int h, g3s_phandle *pl, grs_bitmap *bm); /* 0x2F8E46 */
int g3_check_and_quad_tile_1(int w, int h, g3s_phandle *pl, grs_bitmap *bm); /* 0x2F8E5D */
int g3_check_and_quad_tile_2(int w, int h, g3s_phandle *pl, grs_bitmap *bm); /* 0x2F8E74 */
int g3_check_and_quad_tile_3(int w, int h, g3s_phandle *pl, grs_bitmap *bm); /* 0x2F8E8B */
int g3_draw_floor_map(int n, g3s_phandle *pl, grs_bitmap *bm);   /* 0x2F8ECD */
int g3_light_floor_map(int n, g3s_phandle *pl, grs_bitmap *bm);  /* 0x2F8EE7 */
int g3_draw_wall_map(int n, g3s_phandle *pl, grs_bitmap *bm);    /* 0x2F8F01 */
int g3_light_wall_map(int n, g3s_phandle *pl, grs_bitmap *bm);   /* 0x2F8F18 */
int g3_draw_lmap(int n, g3s_phandle *pl, grs_bitmap *bm);        /* 0x2F8F2F */
int g3_light_lmap(int n, g3s_phandle *pl, grs_bitmap *bm);       /* 0x2F8F46 */
int g3_light_tmap(int n, g3s_phandle *pl, grs_bitmap *bm);       /* 0x2F8F5D */
int g3_draw_tmap(int n, g3s_phandle *pl, grs_bitmap *bm);        /* 0x2F8F74 */
int do_tmap(int n, g3s_phandle *pl, grs_bitmap *bm);             /* 0x2F8F89 */
int g3_quad_tile_0(int w, int h, g3s_phandle *pl, grs_bitmap *bm);   /* 0x2F8FFB */
int g3_quad_tile_1(int w, int h, g3s_phandle *pl, grs_bitmap *bm);   /* 0x2F9012 */
int g3_quad_tile_2(int w, int h, g3s_phandle *pl, grs_bitmap *bm);   /* 0x2F9029 */
int g3_quad_tile_3(int w, int h, g3s_phandle *pl, grs_bitmap *bm);   /* 0x2F9040 */
int quad_tile(int w, int h, g3s_phandle *pl, grs_bitmap *bm);        /* 0x2F9060 */
void g3_compute_warp(g3s_vector *org, g3s_vector *pu, g3s_vector *pv);  /* 0x2F9313 ebx, ecx, edx */
void calc_warp_matrix(g3s_vector *org);                              /* 0x2F93B7 esi */

/* original entry addresses (stored in g3_tmap_func / g3_lit_tmap_func by detail.c) */
#define G3_DRAW_LMAP_ADDR 0x2F8F2F
#define G3_LIGHT_LMAP_ADDR 0x2F8F46
#define G3_LIGHT_TMAP_ADDR 0x2F8F5D
#define G3_DRAW_TMAP_ADDR 0x2F8F74

/* ---------------- polygon.asm ---------------- */
extern grs_vertex g3d_poly_vbuf[100];      /* 0x3635EC */
extern grs_vertex *g3d_poly_vlist[100];    /* 0x363F4C */
extern long g3d_poly_n;                    /* 0x3640DC (also the point count of tmap) */
extern g3s_phandle g3d_poly_src[100];      /* 0x3640E4 */
extern g3s_phandle g3d_poly_clipped[100];  /* 0x364274 */
extern g3s_vector g3d_poly_normal;         /* 0x364404 */
extern g3s_phandle g3d_line_free_list[4];  /* 0x364410 never written */
extern long g3d_line_free_n;               /* 0x364420 never written (0) */
extern long g3d_line_color;                /* 0x364424 */
extern long g3d_poly_func_index[5];        /* 0x364428 2D canvas function index by poly type */
extern uchar g3d_poly_clip_flags[6];       /* 0x36443C clip interpolation flags by poly type */
extern long g3d_fill_func_ofs;             /* 0x365348 fill.asm: added to the 2D function index */

ushort g3_check_codes(int n, g3s_phandle *list);                        /* 0x2F953C ecx, esi -> bx */
long g3_check_poly_facing(g3s_point *p0, g3s_point *p1, g3s_point *p2); /* 0x2F9550 eax, edx, ebx -> al */
long g3_check_and_draw_cpoly(long c, int n, g3s_phandle *list);         /* 0x2F9587 eax, ecx, esi */
long g3_check_and_draw_tluc_spoly(long c, int n, g3s_phandle *list);    /* 0x2F9590 */
long g3_check_and_draw_spoly(long c, int n, g3s_phandle *list);         /* 0x2F9599 */
long g3_check_and_draw_tluc_poly(long c, int n, g3s_phandle *list);     /* 0x2F95A2 */
long g3_check_and_draw_poly(long c, int n, g3s_phandle *list);          /* 0x2F95AB */
long g3_draw_cpoly(long c, int n, g3s_phandle *list);                   /* 0x2F95CA */
long g3_draw_tluc_spoly(long c, int n, g3s_phandle *list);              /* 0x2F95D3 */
long g3_draw_spoly(long c, int n, g3s_phandle *list);                   /* 0x2F95DC */
long g3_draw_tluc_poly(long c, int n, g3s_phandle *list);               /* 0x2F95E5 */
long g3_draw_poly(long c, int n, g3s_phandle *list);                    /* 0x2F95EE */
long draw_poly_common(long c, int n, g3s_phandle *list);                /* 0x2F95F5 */
long g3_draw_point(g3s_point *p);                                       /* 0x2F974D esi */
long g3_draw_cline(g3s_point *p0, g3s_point *p1);                       /* 0x2F9781 esi, edi */
long g3_draw_sline(g3s_point *p0, g3s_point *p1);                       /* 0x2F97C8 */
long g3_draw_line(g3s_point *p0, g3s_point *p1);                        /* 0x2F97D8 */
long g3_check_normal_facing(g3s_vector *v, g3s_vector *normal);         /* 0x2F99AD esi, edi -> al */

/* ---------------- detail ---------------- */
#pragma pack(push, 1)
typedef struct {
    uchar a, b;                        /* -> g3d_detail_a / g3d_detail_b */
    long dist;                         /* -> g3d_detail_dist */
} g3s_detail_entry;                    /* 6 bytes (packed) */
#pragma pack(pop)
LG_STATIC_ASSERT(sizeof(g3s_detail_entry) == 6, g3s_detail_entry_size);
extern g3s_detail_entry g3d_detail_table[]; /* 0x364454 */

void g3_set_detail_params(uchar a, uchar b);   /* 0x2FA080 */
void g3_set_detail_dist(long d);               /* 0x2FA094 */
void g3_set_detail_level(int level);           /* 0x2FA09C */
void g3_set_detail_entry(uchar a, uchar b, long dist, int index); /* 0x2FA0CC */
void g3_set_tmaps_linear(void);                /* 0x3150A0 */
void g3_reset_tmaps(void);                     /* 0x3150BC */

/* ---------------- clip.c ---------------- */
#ifndef CLIP_NONE
#define CLIP_NONE 0
#endif
#ifndef CLIP_ALL
#define CLIP_ALL 0x10
#endif
#define CC_OFF_X 0x10                  /* clip.c: point lies on a clipped x edge */
#define CC_OFF_Y 0x20                  /* clip.c: point lies on a clipped y edge */
#define CC_MASK (0xFF - (CC_OFF_X | CC_OFF_Y))

extern long clip_uv_buf[32][2];        /* 0x45D640 uv slots of clip points (PF_UVSAVED) */
extern uchar clip_tbuff[32][28];       /* 0x45D740 clip points, 0x1C bytes apart (old point size) */
extern g3s_point *clip_e;              /* 0x45DAC0 end of segment (Shock e) */
extern fix clip_den;                   /* 0x45DAC4 */
extern g3s_point *clip_s;              /* 0x45DAC8 start of segment (Shock s) */
extern g3s_point *clip_tmp;            /* 0x45DACC new point (Shock _tmp) */
extern fix clip_a, clip_b, clip_c;     /* 0x45DAD0 0x45DAD4 0x45DAD8 */
extern g3s_point *clip_swapped;        /* 0x45DADC original e while s/e are swapped */
extern fix clip_num;                   /* 0x45DAE0 */
extern long clip_tnum;                 /* 0x45DAE4 */
extern fix clip_d;                     /* 0x365494 near plane z (1) */

void clip_restore_uv(int n, g3s_phandle *list);                 /* 0x316510 */
void clip_save_uv(int n, g3s_phandle *list, long (*uvs)[2]);    /* 0x316554 */
int g3_clip_line(g3s_phandle *src, g3s_phandle *dest);          /* 0x31659C esi, edi */
int g3_clip_polygon(int n, g3s_phandle *src, g3s_phandle *dest); /* 0x316A74 ecx, esi, edi */
void intersect(void);                                           /* 0x317120 */
void clip_behind(void);                                         /* 0x317360 */
void clip_left(void);                                           /* 0x3174C4 */
void clip_top(void);                                            /* 0x31760C */
void clip_right(void);                                          /* 0x317754 */
void clip_bottom(void);                                         /* 0x3178A4 */
void clip_project_list(g3s_phandle *list, int n);               /* 0x3179D4 */

/* ---------------- calls into original asm with its own registers ---------------- */
typedef struct {
    ulong eax, ebx, ecx, edx, esi, edi, ebp, eflags;
} g3b_regs;
void g3b_call(ulong addr, g3b_regs *r);   /* src/3d/g3b_glue.c */

/* worker A's routines, called at their original addresses */
#define G3_VEC_DOTPROD 0x2F403B            /* esi, edi -> eax */
#define G3_VEC_NORMALIZE 0x2F406A          /* esi */
#define G3_COMPUTE_NORMAL_QUICK 0x2F40BB   /* edi = dest, eax, edx, ebx */
#define G3_ROTATE_POINT 0x2F42F5           /* esi -> edi */
#define G3_PROJECT_POINT 0x2F4323          /* edi */
#define G3_VEC_ROTATE 0x2FA793             /* ebx = dest, esi = src, edi = matrix */

static inline fix g3b_vec_dotprod(g3s_vector *a, g3s_vector *b)
{
    g3b_regs r = { 0 };
    r.esi = (ulong)a; r.edi = (ulong)b;
    g3b_call(G3_VEC_DOTPROD, &r);
    return (fix)r.eax;
}
static inline void g3b_vec_normalize(g3s_vector *v)
{
    g3b_regs r = { 0 };
    r.esi = (ulong)v;
    g3b_call(G3_VEC_NORMALIZE, &r);
}
static inline void g3b_compute_normal_quick(g3s_vector *dest, g3s_vector *v0, g3s_vector *v1, g3s_vector *v2)
{
    g3b_regs r = { 0 };
    r.edi = (ulong)dest; r.eax = (ulong)v0; r.edx = (ulong)v1; r.ebx = (ulong)v2;
    g3b_call(G3_COMPUTE_NORMAL_QUICK, &r);
}
static inline g3s_point *g3b_rotate_point(g3s_vector *v)
{
    g3b_regs r = { 0 };
    r.esi = (ulong)v;
    g3b_call(G3_ROTATE_POINT, &r);
    return (g3s_point *)r.edi;
}
static inline void g3b_project_point(g3s_point *p)
{
    g3b_regs r = { 0 };
    r.edi = (ulong)p;
    g3b_call(G3_PROJECT_POINT, &r);
}
static inline void g3b_vec_rotate(g3s_vector *dest, g3s_vector *src, g3s_matrix *m)
{
    g3b_regs r = { 0 };
    r.ebx = (ulong)dest; r.esi = (ulong)src; r.edi = (ulong)m;
    g3b_call(G3_VEC_ROTATE, &r);
}

#endif
