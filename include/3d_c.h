/* 3d_c.h -- private declarations of worker 3d-c's part of the 3D library (Terra Nova v1.09):
 * the object interpreter (interp.asm), instancing (instance.asm) and the TN fill-mode stack
 * (fill.asm).  See src/3d/interp.c, instance.c, fill.c and docs/decompilation/3d-map.md.
 *
 * Calls into the other 3D modules (worker 3d-a: points/matrix, 3d-b: polygon/tmap/light) go to
 * their original addresses with their own register conventions, through tn3c_call(): a register
 * block is loaded, the routine is called, and the registers and flags it returns are stored back.
 */
#ifndef TN_3D_C_H
#define TN_3D_C_H
#include "3d.h"

/* ---------------- calling hand-written asm with arbitrary registers ---------------- */
typedef struct {
    ulong eax, ebx, ecx, edx, esi, edi, ebp, eflags;
} tn3c_regs;
#define TN3C_SF 0x80
#define TN3C_CF 0x01
/* Loads eax..ebp from *r, calls addr, stores eax..ebp and eflags back into *r.  (interp.c) */
void tn3c_call(ulong addr, tn3c_regs *r);

/* original entry points called by this part (owners in docs/decompilation/3d-functions.csv) */
#define A_g3_world_to_object     0x2F4240  /* esi=world vec, edi=dest */
#define A_g3_transform_point     0x2F431E  /* esi=vec -> edi=point */
#define A_g3_transform_list      0x2F4389  /* ecx=n, esi=vecs, edi=dest handles */
#define A_g3_rotate_list         0x2F43C0  /* same registers as transform_list */
#define A_g3_copy_add_delta_x    0x2F4A11  /* esi=point, eax=dx -> edi */
#define A_g3_copy_add_delta_y    0x2F4A60
#define A_g3_copy_add_delta_z    0x2F4AAF
#define A_g3_copy_add_delta_xy   0x2F4BB5  /* esi=point, eax, ebx -> edi */
#define A_g3_copy_add_delta_xz   0x2F4C4E
#define A_g3_copy_add_delta_yz   0x2F4CE8
#define A_angles_2_matrix        0x2FA42F  /* esi=angvec, edi=matrix, ecx=order */
#define A_g3_vec_rotate          0x2FA793  /* ebx=dest, esi=src, edi=matrix */
#define A_g3_transpose           0x2FA820  /* esi=matrix in place */
#define A_g3_copy_transpose      0x2FA83C  /* edi=dest, esi=src */
#define A_g3_matrix_x_matrix     0x2FA871  /* ebx=dest, esi, edi */
#define B_g3_eval_light_obj_cen  0x2F8A28  /* reads edi when _g3d_light_type & 0x20 */
#define B_g3_light               0x2F8C09  /* eax=norm, edx=pos -> eax */
#define B_draw_poly_common       0x2F95F5  /* eax=color, ecx=n, esi=list */
#define B_g3_draw_line           0x2F97D8  /* esi, edi = points */
#define B_g3_check_normal_facing 0x2F99AD  /* esi=point, edi=normal -> SF */

/* ---------------- interp.asm ---------------- */
typedef uchar *(WATCOMCALL *interp_op)(uchar *pc);   /* a handler: returns the next opcode, or
                                                       * NULL for eof (the asm's ret) */
extern uchar interp_parm_copy[];       /* 0x361AA8 copy of the object's parameters (name ours) */
extern long interp_saved_fill_type;    /* 0x361AA0 grd_canvas->gc.fill_type on entry (name ours) */
extern long interp_saved_fill_parm;    /* 0x361AA4 grd_canvas->gc.fill_parm on entry (name ours) */
extern g3s_vector interp_scale_vec;    /* 0x361C3C do_scaleres temporary (name ours) */
extern uchar interp_draw_style;        /* 0x361C48 TN: set by op 0x30/0x31 (name ours) */
extern uchar *g3d_interp_edx;          /* not in the original: stands for the edx register that
                                        * do_setuv reads (see interp.c) */

void g3_interpret_object(uchar *object, void *parms, ulong edi);
void g3_interpret_sub_object(uchar *object, void *parms);
void interpreter_loop(uchar *pc);
uchar *do_eof(uchar *pc);
uchar *do_jnorm(uchar *pc);
uchar *do_lnres(uchar *pc);
uchar *do_multires(uchar *pc);
uchar *do_polyres(uchar *pc);
uchar *do_setcolor(uchar *pc);
uchar *do_sortnorm(uchar *pc);
uchar *do_debug(uchar *pc);
uchar *do_setshade(uchar *pc);
uchar *do_goursurf(uchar *pc);
uchar *do_x_rel(uchar *pc);
uchar *do_y_rel(uchar *pc);
uchar *do_z_rel(uchar *pc);
uchar *do_xy_rel(uchar *pc);
uchar *do_xz_rel(uchar *pc);
uchar *do_yz_rel(uchar *pc);
uchar *do_icall_p(uchar *pc);
uchar *do_icall_b(uchar *pc);
uchar *do_icall_h(uchar *pc);
uchar *do_sfcal(uchar *pc);
uchar *do_defres(uchar *pc);
uchar *do_defres_i(uchar *pc);
uchar *do_gour_p(uchar *pc);
uchar *do_gour_vc(uchar *pc);
uchar *do_getvcolor(uchar *pc);
uchar *do_getvscolor(uchar *pc);
uchar *do_rgbshades(uchar *pc);
uchar *do_draw_mode(uchar *pc);
uchar *do_getpcolor(uchar *pc);
uchar *do_getpscolor(uchar *pc);
uchar *do_scaleres(uchar *pc);
uchar *do_vpnt_p(uchar *pc);
uchar *do_vpnt_v(uchar *pc);
uchar *do_setuv(uchar *pc);
uchar *do_uvlist(uchar *pc);
uchar *do_tmap_op(uchar *pc);
uchar *do_dbg(uchar *pc);
uchar *do_tnop_28(uchar *pc);
uchar *do_tnop_29(uchar *pc);
uchar *do_tnop_2a(uchar *pc);
uchar *do_tnop_2b(uchar *pc);
uchar *do_tnop_2c(uchar *pc);
uchar *do_tnop_2d(uchar *pc);
uchar *do_tnop_2e(uchar *pc);
uchar *do_tnop_2f(uchar *pc);
uchar *do_tnop_30(uchar *pc);
uchar *do_tnop_32(uchar *pc);
uchar *do_tnop_33(uchar *pc);
uchar *do_tnop_34(uchar *pc);
uchar *do_poly_var_2F5334(uchar *pc);
uchar *do_poly_var_2F5359(uchar *pc);
uchar *do_poly_var_2F538C(uchar *pc);
uchar *do_poly_var_2F5401(uchar *pc);
uchar *do_poly_var_2F5426(uchar *pc);

/* ---------------- instance.asm ---------------- */
extern g3s_matrix inst_temp_matrix;    /* 0x362168 (name ours) */
extern g3s_matrix inst_temp_matrix2;   /* 0x36218C instance_matrix product (name ours) */
extern g3s_angvec inst_temp_angles;    /* 0x3621B0 g3_start_object_angles_xyz (name ours) */
extern g3s_vector inst_temp_vec;       /* 0x3621B6 (name ours) */
extern fix inst_sin_y, inst_cos_y;     /* 0x3621C2 0x3621C6 (names ours) */
extern fix inst_sin_x, inst_cos_x;     /* 0x3621CA 0x3621CE */
extern fix inst_sin_z, inst_cos_z;     /* 0x3621D2 0x3621D6 */

/* All return -1 (TRUE) on success, 0 when the instance stack is full (as the asm's eax). */
long g3_start_object(g3s_vector *p);
long g3_start_object_angles_xyz(g3s_vector *p, fixang tx, fixang ty, fixang tz, int order);
long g3_start_object_angles_v(g3s_vector *p, g3s_angvec *o, int order);
long g3_start_object_angles_x(g3s_vector *p, fixang tx);
long g3_start_object_angles_y(g3s_vector *p, fixang ty);
long g3_start_object_angles_z(g3s_vector *p, fixang tz);
long g3_start_object_angles_xy(g3s_vector *p, fixang a_ebx, fixang a_edx, int order);
long g3_start_object_angles_yz(g3s_vector *p, fixang a_ebx, fixang a_edx, int order);
long g3_start_object_angles_xz(g3s_vector *p, fixang a_ebx, fixang a_edx, int order);
long g3_start_object_angles_zx(g3s_vector *p, fixang a_ebx, fixang a_edx, int order);
long g3_start_object_matrix(g3s_vector *p, g3s_matrix *m);
long instance_x(fixang a);
long instance_y(fixang a);
long instance_z(fixang a);
bool save_context(void);               /* TRUE (the asm's CF) when the stack is full */
void g3_scale_object(fix s);
void g3_end_object(void);
void instance_matrix(g3s_matrix *dest, g3s_matrix *src);

/* ---------------- fill.asm ---------------- */
extern long fill_depth;                /* 0x365340 push count (name ours) */
extern uchar *fill_point_rec;          /* 0x365344 3-byte records per point (name ours) */
extern long g3d_fill_func_ofs;         /* 0x365348 1 while mode 3 is pushed: added to the poly function index (3d-b's name) */
extern uchar *fill_light_ref;          /* 0x36534C byte table indexed by the records (name ours) */
extern short fill_light_value;         /* 0x365350 (name ours) */
extern short fill_saved_i[];           /* 0x365352 i of the poly's points while relit (name ours) */
extern uchar fill_rec_flags;           /* 0x36538E bit 0: interpret_object fills the records (name ours) */
extern uchar fill_rec_shift;           /* 0x36538F (name ours) */
extern uchar fill_style;               /* 0x365390 op 0x31 argument in mode 4 (name ours) */
extern uchar fill_poly_type;           /* 0x365391 poly type used by mode 3 (name ours) */
extern uchar fill_stack[];             /* 0x365392 mode stack (0x40 bytes) (name ours) */
extern uchar *fill_stackp;             /* 0x3653D2 (name ours) */

void g3_push_fill_mode(int mode);
void g3_pop_fill_mode(void);
bool fill_facing_light(g3s_vector *v); /* TRUE = the asm's SF */
uchar *fill_op_3156CC(uchar *pc);
uchar *fill_op_3156D4(uchar *pc);
uchar *fill_op_3156DC(uchar *pc);
uchar *fill_op_315717(uchar *pc);
uchar *fill_op_31571F(uchar *pc);
uchar *fill_op_315727(uchar *pc);
uchar *fill_op_31574A(uchar *pc);
uchar *fill_op_315752(uchar *pc);
uchar *fill_op_31575A(uchar *pc);
uchar *fill_op_315762(uchar *pc);
uchar *fill_op_3157AC(uchar *pc);
uchar *fill_op_315811(uchar *pc);
uchar *fill_op_315876(uchar *pc);
uchar *fill_op_3158C4(uchar *pc);
uchar *fill_op_315921(uchar *pc);
uchar *fill_op_315953(uchar *pc);
uchar *fill_op_3159C4(uchar *pc);
uchar *fill_op_315A84(uchar *pc);

#endif
