/* interp.c -- x:\prj\tech\libsrc\3d\interp.asm (Terra Nova v1.09, RCS string 0x3605CA): the 3D
 * object interpreter.  Decompiled from __FF.EXE (hand-written asm in the original).
 *
 *   0x2F4FA0 g3_interpret_sub_object (name ours)  0x2F4FCC g3_interpret_object
 *   0x2F517C interpreter_loop
 *   opcode handlers (entered with ebp = opcode, they jump back to the loop):
 *   0x2F5187 do_debug (07)     0x2F518C do_eof (00)      0x2F518D do_jnorm (01)
 *   0x2F51A7 do_lnres (02)     0x2F51C7 do_multires (03) 0x2F51E7 do_scaleres (21)
 *   0x2F5245 do_vpnt_p (22)    0x2F5251 do_vpnt_v (23)   0x2F526E do_defres (15)
 *   0x2F5289 do_defres_i (16)  0x2F52AC do_polyres (04)  0x2F52E8 do_tnop_33 (33)
 *   0x2F5334 do_poly_var_2F5334  0x2F5359 do_poly_var_2F5359  0x2F538C do_poly_var_2F538C
 *   0x2F5401 do_poly_var_2F5401  0x2F5426 do_poly_var_2F5426     (op 0x33 variants)
 *   0x2F5464 do_tnop_34 (34)   0x2F549E do_sortnorm (06) 0x2F54EC do_goursurf (09)
 *   0x2F54F2 do_gour_p (19)    0x2F54FF do_gour_vc (1A)  0x2F551A do_draw_mode (1E)
 *   0x2F5533 do_setshade (08)  0x2F555B do_rgbshades (1D) 0x2F5580 do_setuv (24)
 *   0x2F55A7 do_uvlist (25)    0x2F55DA do_setcolor (05) 0x2F55E0 do_getvcolor (1B)
 *   0x2F55ED do_getpcolor (1F) 0x2F560B do_getvscolor (1C) 0x2F561C do_getpscolor (20)
 *   0x2F564A do_x_rel (0A)     0x2F5670 do_y_rel (0B)    0x2F5696 do_z_rel (0C)
 *   0x2F56BC do_xy_rel (0D)    0x2F56E5 do_xz_rel (0E)   0x2F570E do_yz_rel (0F)
 *   0x2F5737 do_icall_p (10)   0x2F5761 do_icall_h (12)  0x2F578B do_icall_b (11)
 *   0x2F57B5 do_tnop_28 (28)   0x2F57EF do_tnop_29 (29)  0x2F5828 do_tnop_2b (2B)
 *   0x2F585B do_tnop_2a (2A)   0x2F587C do_tnop_2c (2C)  0x2F589A do_tnop_2e (2E)
 *   0x2F58A2 do_tnop_2f (2F)   0x2F58AA do_tnop_30 (30, 31)  0x2F5956 do_sfcal (14)
 *   0x2F596B do_dbg (27)       0x2F5973 do_tmap_op (26)  0x2F59C6 do_tnop_32 (32)
 *   0x2F5A06 do_tnop_2d (2D)
 *
 * The bytecode: 16-bit opcodes, each followed by its operands; opcode_table (0x3605F4, 53
 * slots, 0x13/0x17/0x18 empty) is mutable -- fill.c swaps slots for the fill modes, and
 * poly_variant_table (0x3605E8) selects what op 0x33 draws.  In the original every handler is
 * entered with ebp = its opcode and jumps back to the loop with ebp advanced; do_eof is a
 * `ret` out of the current loop call.  Here a handler is a function pc -> next pc, NULL meaning
 * that ret.  The table holds the handlers' Watcom-callable addresses (the generated W_
 * wrappers in the verification build), so fill.c and the original data keep working.
 *
 * Calls to the other 3D modules use their asm register conventions (tn3c_call); where a
 * handler makes several calls the registers left by one call are passed to the next, as in
 * the asm.  The register state a handler inherits from the previous handler is not modelled;
 * two places read it (quirks, kept):
 *  - do_setuv (op 0x24) reads its u,v from [edx+4], [edx+8] instead of [ebp+4], [ebp+8]; edx
 *    is whatever the previous handler left.  The C reads through g3d_interp_edx, which the C
 *    handlers do not update.  No model in the game files uses op 0x24.
 *  - g3_interpret_object calls g3_eval_light_obj_cen with the caller's edi, which that
 *    routine uses as the object position when _g3d_light_type & 0x20 (the game never sets
 *    0x20).  The C takes that edi as a third argument.
 * Other quirks kept: handler loops are do-while (a count of 0 runs once with index -1),
 * do_scaleres with count 0 loops 2^32 times, do_draw_mode does nothing, op 0x28/0x29 end the
 * current loop after their sub-object, icall/0x28/0x29 run the sub-object and end_object even
 * when start_object failed (stack full).
 */
#include "3d_c.h"

#define W16(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define S16(p, o) (*(short *)((uchar *)(p) + (o)))
#define D32(p, o) (*(long *)((uchar *)(p) + (o)))

#ifndef TN_LINK_ORIGINAL_DATA
void *poly_variant_table[3] = { do_poly_var_2F5401, do_poly_var_2F5334, do_poly_var_2F538C };
void *opcode_table[N_OPS] = {
    do_eof, do_jnorm, do_lnres, do_multires, do_polyres, do_setcolor, do_sortnorm, do_debug,
    do_setshade, do_goursurf, do_x_rel, do_y_rel, do_z_rel, do_xy_rel, do_xz_rel, do_yz_rel,
    do_icall_p, do_icall_b, do_icall_h, 0, do_sfcal, do_defres, do_defres_i, 0,
    0, do_gour_p, do_gour_vc, do_getvcolor, do_getvscolor, do_rgbshades, do_draw_mode, do_getpcolor,
    do_getpscolor, do_scaleres, do_vpnt_p, do_vpnt_v, do_setuv, do_uvlist, do_tmap_op, do_dbg,
    do_tnop_28, do_tnop_29, do_tnop_2a, do_tnop_2b, do_tnop_2c, do_tnop_2d, do_tnop_2e, do_tnop_2f,
    do_tnop_30, do_tnop_30, do_tnop_32, do_tnop_33, do_tnop_34 };
g3s_phandle resbuf[1000];
g3s_phandle poly_list[100];
uchar vcolor_tab[32];
long vpoint_tab[32];
grs_bitmap *tmap_tab[64];
void *g3d_interp_callbacks[16];
g3s_vector g3d_interp_vecs[16];
void *interp_parms;
void *interp_object;
long interp_saved_fill_type, interp_saved_fill_parm;
uchar interp_parm_copy[0x194];
g3s_vector interp_scale_vec;
uchar interp_draw_style;
#endif
uchar *g3d_interp_edx;

/* ---------------- register-level calls ----------------
 * tn3c_call(addr, r): cdecl.  Saves the C callee-saved registers, loads eax..ebp from r, calls
 * addr, then stores eax..ebp and eflags back into r. */
#ifdef TN_WATCOM_ABI
__asm__(
    "\t.intel_syntax noprefix\n"
    "\t.text\n"
    "\t.globl tn3c_call\n"
    "tn3c_call:\n"
    "\tpush ebp\n\tpush ebx\n\tpush esi\n\tpush edi\n"
    "\tmov eax, [esp+20]\n"
    "\tmov ecx, [esp+24]\n"
    "\tpush ecx\n"                  /* [esp+4] = r */
    "\tpush eax\n"                  /* [esp]   = addr */
    "\tmov ebx, [ecx+4]\n\tmov edx, [ecx+12]\n\tmov esi, [ecx+16]\n"
    "\tmov edi, [ecx+20]\n\tmov ebp, [ecx+24]\n\tmov eax, [ecx+0]\n\tmov ecx, [ecx+8]\n"
    "\tcall dword ptr [esp]\n"
    "\tpushfd\n"                    /* [esp]=flags [esp+4]=addr [esp+8]=r */
    "\tpush ecx\n"
    "\tmov ecx, [esp+12]\n"
    "\tmov [ecx+0], eax\n\tmov [ecx+4], ebx\n\tmov [ecx+12], edx\n"
    "\tmov [ecx+16], esi\n\tmov [ecx+20], edi\n\tmov [ecx+24], ebp\n"
    "\tpop eax\n\tmov [ecx+8], eax\n"
    "\tpop eax\n\tmov [ecx+28], eax\n"
    "\tadd esp, 8\n"
    "\tpop edi\n\tpop esi\n\tpop ebx\n\tpop ebp\n"
    "\tcld\n"
    "\tret\n"
    "\t.att_syntax prefix\n");
#else
/* standalone: tn3c_call is in port/core/regcall.c */
#endif

static g3s_point *transform_point(void *v)
{
    tn3c_regs r = { 0 };
    r.esi = (ulong)v;
    tn3c_call(A_g3_transform_point, &r);
    return (g3s_point *)r.edi;
}

static void draw_line(g3s_point *a, g3s_point *b)
{
    tn3c_regs r = { 0 };
    r.esi = (ulong)a; r.edi = (ulong)b;
    tn3c_call(B_g3_draw_line, &r);
}

static void draw_poly(long color, int n, g3s_phandle *list, uchar *ebp)
{
    tn3c_regs r = { 0 };
    r.eax = color; r.ecx = n; r.esi = (ulong)list; r.ebx = (ulong)grd_canvas; r.ebp = (ulong)ebp;
    tn3c_call(B_draw_poly_common, &r);
}

/* g3_check_normal_facing(esi=point, edi=normal): the asm's sign flag (TRUE = facing away:
 * the handlers take their branch on SF clear).  r keeps the registers for a following call. */
static bool check_facing(tn3c_regs *r, void *point, void *normal)
{
    r->esi = (ulong)point; r->edi = (ulong)normal;
    tn3c_call(B_g3_check_normal_facing, r);
    return (r->eflags & TN3C_SF) != 0;
}

/* g3_light(eax=norm, edx=pos), shifted down by g3d_light_shift (sar, count & 31) */
static long light_value(tn3c_regs *r, void *norm, void *pos)
{
    r->eax = (ulong)norm; r->edx = (ulong)pos;
    tn3c_call(B_g3_light, r);
    return (long)r->eax >> (g3d_light_shift & 31);
}

/* the light table row of a light value, stored as the canvas fill parameter */
static void set_light_row(long l)
{
    grd_canvas->gc.fill_parm = (l & 0xFFFFFF00) + (long)g3d_light_tab;
}

static void copy_add_delta(ulong addr, uchar *pc, int two)
{
    tn3c_regs r = { 0 };
    r.esi = (ulong)resbuf[W16(pc, 4)];
    r.eax = D32(pc, 6);
    if (two) r.ebx = D32(pc, 10);
    r.ebp = (ulong)pc;
    tn3c_call(addr, &r);
    resbuf[W16(pc, 2)] = (g3s_phandle)r.edi;
}

/* calls one of the tmappers through g3_tmap_func / g3_lit_tmap_func (pushad entries:
 * eax = n, edx = vertex list, ebx = bitmap) */
static void call_tmapper(void *f, int n, int tex, ulong ecx, ulong esi, uchar *ebp)
{
    tn3c_regs r = { 0 };
    r.eax = n; r.edi = tex; r.edx = (ulong)poly_list; r.ebx = (ulong)tmap_tab[tex];
    r.ecx = ecx; r.esi = esi; r.ebp = (ulong)ebp;
    tn3c_call((ulong)f, &r);
}

/* ---------------- entry points ---------------- */

/* Copies the object's parameters (word +0x44 dwords) to interp_parm_copy. */
static void copy_parms(uchar *obj, void *parms)
{
    if (parms) {
        long *s = parms, *d = (long *)interp_parm_copy;
        uint n = W16(obj, 0x44);
        while (n--) *d++ = *s++;
    }
}

/* TN: runs an object's sub-object code (offset dword +0x4A) without the per-object setup:
 * no point buffer clearing, fill mode, canvas or light handling. */
void g3_interpret_sub_object(uchar *object, void *parms)
{
    interp_object = object;
    interp_parms = parms;
    copy_parms(object, parms);
    interpreter_loop(object + D32(object, 0x4A));
}

/* Draws an object.  object: TN object header (word +0x42 point count, word +0x44 parameter
 * dwords, byte +0x47/+0x49 table counts, byte +0x48 shading flag, dword +0x4E code offset or
 * 0 = code after the tables at +0x5E).  edi: the caller's edi register, passed on to
 * g3_eval_light_obj_cen (see the header comment). */
void g3_interpret_object(uchar *object, void *parms, ulong edi)
{
    uchar *pc;
    int i, n;

    interp_object = object;
    interp_parms = parms;
    interp_saved_fill_type = grd_canvas->gc.fill_type;
    interp_saved_fill_parm = grd_canvas->gc.fill_parm;
    if (_g3d_light_type & 6) {
        tn3c_regs r = { 0 };
        r.eax = interp_saved_fill_parm; r.esi = (ulong)grd_canvas; r.edi = edi; r.ebp = (ulong)object;
        tn3c_call(B_g3_eval_light_obj_cen, &r);
        if (object[0x48] != 1)
            g3_push_fill_mode(0);
    } else if (object[0x48] == 1)
        g3_push_fill_mode(2);

    object = interp_object;
    n = W16(object, 0x42);
    if (n <= 0)               /* (a word, so only 0) */
        return;
    for (i = 0; i < n; i++)
        resbuf[i] = 0;
    copy_parms(object, interp_parms);
    if (D32(object, 0x4E) != 0)
        pc = object + D32(object, 0x4E);
    else
        pc = object + (object[0x47] * 2 + object[0x49]) * 6 + 0x5E;
    g3d_poly_type = G3_POLY;
    interp_draw_style = 0;
    interpreter_loop(pc);

    /* TN: store each point's rounded x,y (x shifted by fill_rec_shift) and z>>16 in the
     * 3-byte point records */
    if (fill_rec_flags & 1) {
        object = interp_object;
        for (i = W16(object, 0x42) - 1; i >= 0; i--) {
            g3s_point *p = resbuf[i];
            uchar *rec = fill_point_rec + 3 * i;
            ushort x, y;
            rec[2] = (uchar)(p->z >> 16);
            y = (ushort)((p->y + 0x8000) >> 16);
            x = (ushort)((p->x + 0x8000) >> 16);
            *(ushort *)rec = (ushort)((x << (fill_rec_shift & 31)) + y);
        }
    }
    /* free the points: push every non-NULL handle on the free list, last first */
    object = interp_object;
    for (i = W16(object, 0x42) - 1; i >= 0; i--) {
        g3s_point *p = resbuf[i];
        if (p) {
            p->next = first_free;
            first_free = p;
        }
    }
    if (_g3d_light_type & 6) {
        if (object[0x48] != 1)
            g3_pop_fill_mode();
    } else if (object[0x48] == 1)
        g3_pop_fill_mode();
    grd_canvas->gc.fill_type = interp_saved_fill_type;
    grd_function_table = grd_function_fill_table + interp_saved_fill_type * 390;
    grd_uline_fill_vector = grd_uline_fill_table + interp_saved_fill_type * 9;
    grd_canvas->gc.fill_parm = interp_saved_fill_parm;
}

/* Runs bytecode until an eof (or an op 0x28/0x29) ends this level. */
void interpreter_loop(uchar *pc)
{
    while (pc)
        pc = ((interp_op)opcode_table[W16(pc, 0)])(pc);
}

/* ---------------- opcode handlers ---------------- */

uchar *do_debug(uchar *pc) { return pc + 2; }
uchar *do_eof(uchar *pc) { return NULL; }

/* 01 jnorm: if the normal (+0x10 point, +4 normal) faces away skip 0x1C, else jump +word 2 */
uchar *do_jnorm(uchar *pc)
{
    tn3c_regs r = { 0 };
    r.ebp = (ulong)pc;
    if (check_facing(&r, pc + 0x10, pc + 4))
        return pc + 0x1C;
    return pc + S16(pc, 2);
}

/* 02 lnres: line between two points */
uchar *do_lnres(uchar *pc)
{
    draw_line(resbuf[W16(pc, 2)], resbuf[W16(pc, 4)]);
    return pc + 6;
}

/* 03 multires: n = word 2 vectors after the header, into resbuf from word 4 */
static uchar *multires(uchar *pc, ulong fn)
{
    tn3c_regs r = { 0 };
    uint n = W16(pc, 2);
    r.ecx = n;
    r.esi = (ulong)(pc + 6);
    r.ebx = W16(pc, 4);
    r.edi = (ulong)&resbuf[W16(pc, 4)];
    r.ebp = (ulong)(pc + 6 + n * 12);
    tn3c_call(fn, &r);
    return pc + 6 + n * 12;
}
uchar *do_multires(uchar *pc) { return multires(pc, A_g3_transform_list); }

/* 21 scaleres: n = word 2 vectors scaled by the parameter at byte offset word 4, transformed
 * into resbuf from word 6.  (n = 0 runs 2^32 times.) */
uchar *do_scaleres(uchar *pc)
{
    ulong n = W16(pc, 2);
    fix s = D32(interp_parm_copy, W16(pc, 4));
    uint idx = W16(pc, 6);

    pc += 8;
    do {
        interp_scale_vec.x = (fix)(((long long)s * D32(pc, 0)) >> 16);
        interp_scale_vec.y = (fix)(((long long)s * D32(pc, 4)) >> 16);
        interp_scale_vec.z = (fix)(((long long)s * D32(pc, 8)) >> 16);
        resbuf[idx++] = transform_point(&interp_scale_vec);
        pc += 12;
    } while (--n != 0);
    return pc;
}

/* 22 vpnt_p / 23 vpnt_v: resbuf[word 4] = dword at byte offset word 2 of the parameters /
 * of vpoint_tab */
uchar *do_vpnt_p(uchar *pc)
{
    resbuf[W16(pc, 4)] = (g3s_phandle)D32(interp_parm_copy, W16(pc, 2));
    return pc + 6;
}
uchar *do_vpnt_v(uchar *pc)
{
    resbuf[W16(pc, 4)] = (g3s_phandle)D32(vpoint_tab, W16(pc, 2));
    return pc + 6;
}

/* 15 defres / 16 defres_i: transform the vector at +4 into resbuf[word 2] (and set its i) */
uchar *do_defres(uchar *pc)
{
    resbuf[W16(pc, 2)] = transform_point(pc + 4);
    return pc + 0x10;
}
uchar *do_defres_i(uchar *pc)
{
    g3s_point *p = transform_point(pc + 4);
    resbuf[W16(pc, 2)] = p;
    p->i = S16(pc, 0x10);
    return pc + 0x12;
}

/* fills poly_list[0..n-1] from the point indices at idx (last first; n = 0 writes
 * poly_list[-1] from the word before idx) */
static void fill_poly_list(uchar *idx, int n)
{
    int i = n - 1;
    do {
        poly_list[i] = resbuf[W16(idx, 2 * i)];
    } while (--i >= 0);
}

/* 04 polyres: polygon of word 2 points in the canvas foreground color */
uchar *do_polyres(uchar *pc)
{
    int n = W16(pc, 2);
    fill_poly_list(pc + 4, n);
    pc += 4 + 2 * n;
    draw_poly(grd_canvas->gc.fcolor, n, poly_list, pc);
    return pc;
}

/* 33 (TN): poly with a normal check (+0x10 point, +4 normal), next opcode at +word 2 either
 * way.  When facing, with draw style 1 the light value selects the light table row, then
 * poly_variant_table[word 0x1C] draws it. */
uchar *do_tnop_33(uchar *pc)
{
    tn3c_regs r = { 0 };
    r.ebp = (ulong)pc;
    if (!check_facing(&r, pc + 0x10, pc + 4))
        return pc + S16(pc, 2);
    if (interp_draw_style == 1)
        set_light_row(light_value(&r, pc + 4, pc + 0x10));
    return ((interp_op)poly_variant_table[W16(pc, 0x1C)])(pc);
}

/* op 0x33 layout: +0x1E n, +0x20 n point indices, then the color word (variants 0/1) or the
 * texture index and n (u,v) dword pairs (variant 2) */
static void set_poly_color(uchar *pc)
{
    uint c = W16(pc, 0x20 + 2 * W16(pc, 0x1E));
    if (!(c & 0x8000))
        c = vcolor_tab[c];
    grd_canvas->gc.fcolor = c & 0xFFF;
}

/* closed outline: lines from the last point back to the first (n = 0: one line from the point
 * of the word before the list) */
static void draw_outline(uchar *idx, int n)
{
    int i = n - 1;
    g3s_point *prev = resbuf[W16(idx, 0)];
    do {
        g3s_point *p = resbuf[W16(idx, 2 * i)];
        draw_line(p, prev);
        prev = p;
    } while (--i >= 0);
}

uchar *do_poly_var_2F5334(uchar *pc)
{
    set_poly_color(pc);
    return do_poly_var_2F5359(pc);
}

/* wireframe */
uchar *do_poly_var_2F5359(uchar *pc)
{
    draw_outline(pc + 0x20, W16(pc, 0x1E));
    return pc + S16(pc, 2);
}

/* texture mapped: sets the points' u,v (dwords >> 8) */
uchar *do_poly_var_2F538C(uchar *pc)
{
    int n = W16(pc, 0x1E);
    uchar *uv = pc + 0x22 + 2 * n;
    int i = n - 1, tex;
    do {
        g3s_point *p = resbuf[W16(pc, 0x20 + 2 * i)];
        poly_list[i] = p;
        p->u = (sfix)((ulong)D32(uv, 8 * i) >> 8);
        p->v = (sfix)((ulong)D32(uv, 8 * i + 4) >> 8);
    } while (--i >= 0);
    tex = W16(pc, 0x20 + 2 * n);
    call_tmapper(g3d_poly_type != G3_POLY ? (void *)g3_lit_tmap_func : (void *)g3_tmap_func,
                 n, tex, (ulong)i, interp_draw_style == 1 ? (ulong)grd_canvas : (ulong)(pc + 0x10), pc);
    return pc + S16(pc, 2);
}

uchar *do_poly_var_2F5401(uchar *pc)
{
    set_poly_color(pc);
    return do_poly_var_2F5426(pc);
}

/* flat polygon */
uchar *do_poly_var_2F5426(uchar *pc)
{
    int n = W16(pc, 0x1E);
    fill_poly_list(pc + 0x20, n);
    draw_poly(grd_canvas->gc.fcolor, n, poly_list, pc);
    return pc + S16(pc, 2);
}

/* 34 (TN): list of 0x1A-byte entries {point index, normal, position}: each point's i = light
 * value; ended by index 0xFFFF (whose light is still computed) */
uchar *do_tnop_34(uchar *pc)
{
    tn3c_regs r = { 0 };
    long l;
    uint idx;

    pc += 2;
    for (;;) {
        r.ebp = (ulong)pc;
        l = light_value(&r, pc + 2, pc + 0xE);
        idx = W16(pc, 0);
        if (idx == 0xFFFF)
            break;
        resbuf[idx]->i = (sfix)l;
        pc += 0x1A;
    }
    return pc + 0x1A;
}

/* 06 sortnorm: draws the two sub-objects (dwords +0x1A, +0x1E, relative) back to front by the
 * normal (+0xE point, +2 normal) */
uchar *do_sortnorm(uchar *pc)
{
    tn3c_regs r = { 0 };
    r.ebp = (ulong)pc;
    if (check_facing(&r, pc + 0xE, pc + 2)) {
        interpreter_loop(pc + D32(pc, 0x1A));
        interpreter_loop(pc + D32(pc, 0x1E));
    } else {
        interpreter_loop(pc + D32(pc, 0x1E));
        interpreter_loop(pc + D32(pc, 0x1A));
    }
    return pc + 0x22;
}

/* gouraud base: only its low word is written, = value << 8 */
static void set_gouraud_base(uint v)
{
    *(ushort *)&gouraud_base = (ushort)(v << 8);
}
/* 09 goursurf: word 2 */
uchar *do_goursurf(uchar *pc) { set_gouraud_base(W16(pc, 2)); return pc + 4; }
/* 19 gour_p: word at byte offset word 2 of the parameters */
uchar *do_gour_p(uchar *pc) { set_gouraud_base(W16(interp_parm_copy, W16(pc, 2))); return pc + 4; }
/* 1A gour_vc: vcolor_tab entry */
uchar *do_gour_vc(uchar *pc) { set_gouraud_base(vcolor_tab[W16(pc, 2)]); return pc + 4; }

/* 1E draw_mode: computes a value in ax and drops it */
uchar *do_draw_mode(uchar *pc) { return pc + 4; }

/* 08 setshade: word 2 pairs {point index, i} */
uchar *do_setshade(uchar *pc)
{
    int n = W16(pc, 2), i = n - 1;
    do {
        resbuf[W16(pc, 4 + 4 * i)]->i = S16(pc, 6 + 4 * i);
    } while (--i >= 0);
    return pc + 4 + 4 * n;
}

/* 1D rgbshades: word 2 entries {point index, rgb dword, 4 unused bytes} */
uchar *do_rgbshades(uchar *pc)
{
    int i = W16(pc, 2) - 1;
    uchar *p = pc + 4;
    do {
        resbuf[W16(p, 0)]->rgb = D32(p, 2);
        p += 10;
    } while (--i >= 0);
    return p;
}

/* 24 setuv: point word 2 gets u,v from [edx+4], [edx+8] >> 8 (bug: edx, not the opcode) */
uchar *do_setuv(uchar *pc)
{
    g3s_point *p = resbuf[W16(pc, 2)];
    p->u = (sfix)((ulong)D32(g3d_interp_edx, 4) >> 8);
    p->v = (sfix)((ulong)D32(g3d_interp_edx, 8) >> 8);
    return pc + 12;
}

/* 25 uvlist: word 2 entries {point index, u dword, v dword} (>> 8) */
uchar *do_uvlist(uchar *pc)
{
    int i = W16(pc, 2) - 1;
    uchar *p = pc + 4;
    do {
        g3s_point *pt = resbuf[W16(p, 0)];
        pt->u = (sfix)((ulong)D32(p, 2) >> 8);
        pt->v = (sfix)((ulong)D32(p, 6) >> 8);
        p += 10;
    } while (--i >= 0);
    return p;
}

/* 05 setcolor, 1B getvcolor, 1F getpcolor: canvas foreground color */
uchar *do_setcolor(uchar *pc) { grd_canvas->gc.fcolor = W16(pc, 2); return pc + 4; }
uchar *do_getvcolor(uchar *pc) { grd_canvas->gc.fcolor = vcolor_tab[W16(pc, 2)]; return pc + 4; }
uchar *do_getpcolor(uchar *pc) { grd_canvas->gc.fcolor = W16(interp_parm_copy, W16(pc, 2)); return pc + 4; }

/* 1C getvscolor, 20 getpscolor: color shaded by the screen light table, row = byte of word 4;
 * the result keeps the row in bits 8..15 */
static void set_shaded_color(uint c, uint row)
{
    uint i = ((row & 0xFF) << 8) | (c & 0xFF);
    grd_canvas->gc.fcolor = (i & 0xFF00) | grd_screen->ltab[i];
}
uchar *do_getvscolor(uchar *pc) { set_shaded_color(vcolor_tab[W16(pc, 2)], W16(pc, 4)); return pc + 6; }
uchar *do_getpscolor(uchar *pc) { set_shaded_color(W16(interp_parm_copy, W16(pc, 2)), W16(pc, 4)); return pc + 6; }

/* 0A..0F x_rel..yz_rel: resbuf[word 2] = resbuf[word 4] + delta dword(s) at 6 (and 10) */
uchar *do_x_rel(uchar *pc) { copy_add_delta(A_g3_copy_add_delta_x, pc, 0); return pc + 0xA; }
uchar *do_y_rel(uchar *pc) { copy_add_delta(A_g3_copy_add_delta_y, pc, 0); return pc + 0xA; }
uchar *do_z_rel(uchar *pc) { copy_add_delta(A_g3_copy_add_delta_z, pc, 0); return pc + 0xA; }
uchar *do_xy_rel(uchar *pc) { copy_add_delta(A_g3_copy_add_delta_xy, pc, 1); return pc + 0xE; }
uchar *do_xz_rel(uchar *pc) { copy_add_delta(A_g3_copy_add_delta_xz, pc, 1); return pc + 0xE; }
uchar *do_yz_rel(uchar *pc) { copy_add_delta(A_g3_copy_add_delta_yz, pc, 1); return pc + 0xE; }

/* 10 icall_p, 12 icall_h, 11 icall_b: sub-object at the absolute pointer dword 2, positioned
 * at +6 and rotated by the parameter word at byte offset word 0x12 around y / x / z */
uchar *do_icall_p(uchar *pc)
{
    g3_start_object_angles_y((g3s_vector *)(pc + 6), W16(interp_parm_copy, W16(pc, 0x12)));
    interpreter_loop((uchar *)D32(pc, 2));
    g3_end_object();
    return pc + 0x14;
}
uchar *do_icall_h(uchar *pc)
{
    g3_start_object_angles_x((g3s_vector *)(pc + 6), W16(interp_parm_copy, W16(pc, 0x12)));
    interpreter_loop((uchar *)D32(pc, 2));
    g3_end_object();
    return pc + 0x14;
}
uchar *do_icall_b(uchar *pc)
{
    g3_start_object_angles_z((g3s_vector *)(pc + 6), W16(interp_parm_copy, W16(pc, 0x12)));
    interpreter_loop((uchar *)D32(pc, 2));
    g3_end_object();
    return pc + 0x14;
}

/* 28 (TN): sub-object in matrix (+4 position, +0x10 matrix) and rotated around y about the
 * object centre by parameter dword word 2; its code follows at +0x34 and ends this level.
 * The edi passed to eval_light_obj_cen is what the instance routines leave in it. */
uchar *do_tnop_28(uchar *pc)
{
    tn3c_regs r = { 0 };
    long ok_m = g3_start_object_matrix((g3s_vector *)(pc + 4), (g3s_matrix *)(pc + 0x10));
    long ok_y = g3_start_object_angles_y(&g3d_obj_cen, W16(interp_parm_copy, 4 * W16(pc, 2)));
    r.eax = ok_y;
    r.edi = ok_y ? (ulong)&_wtoo_matrix : ok_m ? (ulong)&_wtoo_position : (ulong)(pc + 0x10);
    r.esi = (ulong)&g3d_obj_cen;
    r.ebp = (ulong)pc;
    tn3c_call(B_g3_eval_light_obj_cen, &r);
    interpreter_loop(pc + 0x34);
    g3_end_object();
    g3_end_object();
    return NULL;
}

/* 29 (TN): sub-object in matrix, then a second context level moved by -parameter dword
 * word 2 along x (no rotation); code at +0x34, ends this level */
uchar *do_tnop_29(uchar *pc)
{
    tn3c_regs r = { 0 };
    long ok_m = g3_start_object_matrix((g3s_vector *)(pc + 4), (g3s_matrix *)(pc + 0x10));
    fix dx = D32(interp_parm_copy, 4 * W16(pc, 2));
    save_context();
    _view_position.x -= dx;
    r.ebx = dx;
    r.edi = ok_m ? (ulong)&_wtoo_position : (ulong)(pc + 0x10);
    r.ebp = (ulong)pc;
    tn3c_call(B_g3_eval_light_obj_cen, &r);
    interpreter_loop(pc + 0x34);
    g3_end_object();
    g3_end_object();
    return NULL;
}

/* 2B (TN): closed outline of word 2 points */
uchar *do_tnop_2b(uchar *pc)
{
    int n = W16(pc, 2);
    draw_outline(pc + 4, n);
    return pc + 4 + 2 * n;
}

/* 2A (TN): calls game callback word 2 (if set) with eax = pc + 4 */
uchar *do_tnop_2a(uchar *pc)
{
    uint i = W16(pc, 2);
    if (g3d_interp_callbacks[i]) {
        tn3c_regs r = { 0 };
        r.eax = (ulong)(pc + 4); r.ebx = (ulong)g3d_interp_callbacks[i]; r.ecx = i; r.ebp = (ulong)pc;
        tn3c_call((ulong)g3d_interp_callbacks[i], &r);
    }
    return pc + 0x10;
}

/* 2C (TN): g3d_interp_vecs[word 2] = vector +4 in object coordinates */
uchar *do_tnop_2c(uchar *pc)
{
    tn3c_regs r = { 0 };
    uint i = W16(pc, 2);
    r.esi = (ulong)(pc + 4); r.ecx = 3 * i; r.edi = (ulong)&g3d_interp_vecs[i]; r.ebp = (ulong)pc;
    tn3c_call(A_g3_world_to_object, &r);
    return pc + 0x10;
}

uchar *do_tnop_2e(uchar *pc) { return pc + 2; }
uchar *do_tnop_2f(uchar *pc) { return pc + 2; }

/* 30, 31 (TN): draw style = byte 2.  1: canvas fill type 1 (clut, by light row); 2: flat
 * fill, gouraud polys (poly type 2); else flat. */
uchar *do_tnop_30(uchar *pc)
{
    uint s = pc[2];
    interp_draw_style = s;
    if (s == 1) {
        grd_canvas->gc.fill_type = 1;
        grd_function_table = grd_function_fill_table + 390;
        grd_uline_fill_vector = grd_uline_fill_table + 9;
        g3d_poly_type = G3_POLY;
    } else {
        grd_canvas->gc.fill_type = 0;
        grd_function_table = grd_function_fill_table;
        grd_uline_fill_vector = grd_uline_fill_table;
        g3d_poly_type = s == 2 ? G3_SPOLY : G3_POLY;
    }
    return pc + 3;
}

/* 14 sfcal: runs the sub-object at +word 2 */
uchar *do_sfcal(uchar *pc)
{
    interpreter_loop(pc + S16(pc, 2));
    return pc + 4;
}

/* 27 dbg */
uchar *do_dbg(uchar *pc) { return pc + 8; }

/* 26 tmap: texture word 2, word 4 points */
uchar *do_tmap_op(uchar *pc)
{
    int n = W16(pc, 4);
    fill_poly_list(pc + 6, n);
    call_tmapper(g3d_poly_type != G3_POLY ? (void *)g3_lit_tmap_func : (void *)g3_tmap_func,
                 n, W16(pc, 2), (ulong)-1, 0, pc + 6 + 2 * n);
    return pc + 6 + 2 * n;
}

/* 32 (TN): like jnorm, but when facing sets the light table row from the normal and goes on
 * at +0x1C */
uchar *do_tnop_32(uchar *pc)
{
    tn3c_regs r = { 0 };
    r.ebp = (ulong)pc;
    if (!check_facing(&r, pc + 0x10, pc + 4))
        return pc + S16(pc, 2);
    set_light_row(light_value(&r, pc + 4, pc + 0x10));
    return pc + 0x1C;
}

/* 2D (TN): point word 2's i = light value of normal +4 / position +0x10 */
uchar *do_tnop_2d(uchar *pc)
{
    tn3c_regs r = { 0 };
    long l;
    r.ebp = (ulong)pc;
    l = light_value(&r, pc + 4, pc + 0x10);
    resbuf[W16(pc, 2)]->i = (sfix)l;
    return pc + 0x1C;
}
