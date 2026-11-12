/* fill.c -- x:\prj\tech\libsrc\3d\fill.asm (Terra Nova v1.09, RCS string 0x365322): TN's
 * fill-mode stack.  Pushing a mode saves interpreter opcode_table slots (and the canvas fill
 * state) on a byte stack and installs replacement handlers; popping restores them.
 * Decompiled from __FF.EXE (hand-written asm in the original).
 *
 *   0x3150D8 g3_push_fill_mode (name ours)   0x3150EB g3_pop_fill_mode (name ours)
 *   mode push/pop routines (table 0x3653D6, 7 pairs):
 *   0 0x315102 / 0x315167   1 0x3151B9 / 0x315246   2 0x3154B3 / 0x3154FD
 *   3 0x3152AA / 0x315391   4 0x315406 / 0x31547A   5 0x315523 / 0x3155AC
 *   6 0x315607 / 0x31567B
 *   replacement handlers: 0x3156CC 0x3156D4 0x3156DC 0x315717 0x31571F 0x315727 0x31574A
 *   0x315752 0x31575A 0x315762 0x3157AC 0x315811 0x315876 0x3158C4 0x315921 0x315953
 *   0x3159C4 0x315A84;  0x31577F fill_facing_light (name ours)
 *
 * Modes (slot -> handler):
 *   0 light rows: canvas fill type 1 (clut); 01 jnorm -> do_tnop_32 (facing sets the light row)
 *   1 like 0, and ops 2D/31 skipped
 *   2 32 -> do_jnorm; ops 2D/31 skipped
 *   3 2D map: points drawn flat at (y, x) (sx = y, sy = x, i = z >> 8): 04 polys and 26 tmaps
 *     drawn as polys of type fill_poly_type, 03 uses g3_rotate_list, 01/32/06 test facing
 *     against the light source, 05/1B/2D/31 skipped
 *   4 relit from the point records: 2D sets i from the light or the ambient level, 32 stores
 *     the light value, 04/26 temporarily relight their points (fill_style 1)
 *   5 wireframe: op 0x33 variants all draw outlines, 04 -> do_tnop_2b, 26 outlines, 05/1B/31
 *     skipped
 *   6 op 0x33 variants all flat polys, 26 drawn as polys, 05/1B/31 skipped
 * The table entries written are the handlers' Watcom-callable addresses (WATCOM_FN).
 *
 * Quirks (kept): there is no stack bound; mode 3's pop does not step back over the mode
 * byte, so it restores every slot from dwords shifted by one byte and leaves the stack pointer
 * one byte too high; mode 4's pop falls through into mode 2's push.  Handler 0x3159C4 /
 * 0x315A84 (fill_style 1) put back the saved i values for n + 1 points: the extra one is the
 * point indexed by the word after the list.  The mode 4 handlers compare the record byte + 4
 * with a signed 16-bit compare.
 */
#include "3d_c.h"

#define W16(p, o) (*(ushort *)((uchar *)(p) + (o)))
#define S16(p, o) (*(short *)((uchar *)(p) + (o)))
#define D32(p, o) (*(long *)((uchar *)(p) + (o)))

#ifndef TN_LINK_ORIGINAL_DATA
long fill_depth;
uchar *fill_point_rec;
long g3d_fill_func_ofs;
uchar *fill_light_ref;
short fill_light_value;
short fill_saved_i[30];
uchar fill_rec_flags, fill_rec_shift, fill_style, fill_poly_type;
uchar fill_stack[0x40];
uchar *fill_stackp = fill_stack;
#endif

/* opcode slots */
#define OP(n) opcode_table[n]
#define SET(n, f) (opcode_table[n] = WATCOM_FN(f))

/* ---------------- canvas fill state ---------------- */
static void set_fill_type(long t)
{
    grd_canvas->gc.fill_type = t;
    grd_function_table = grd_function_fill_table + t * 390;
    grd_uline_fill_vector = grd_uline_fill_table + t * 9;
}

/* fill type 1 as a constant (the asm adds 0x618 / 0x24 directly) */
static void set_fill_clut(void)
{
    grd_canvas->gc.fill_type = 1;
    grd_function_table = grd_function_fill_table + 390;
    grd_uline_fill_vector = grd_uline_fill_table + 9;
}

static void set_fill_norm(void)
{
    grd_canvas->gc.fill_type = 0;
    grd_function_table = grd_function_fill_table;
    grd_uline_fill_vector = grd_uline_fill_table;
}

static void set_variants(void *a, void *b, void *c)
{
    poly_variant_table[0] = a;
    poly_variant_table[1] = b;
    poly_variant_table[2] = c;
}

/* ---------------- mode push / pop (sp = the stack pointer, the asm's ebx) ---------------- */
static void save(uchar **sp, int slot) { *(void **)*sp = OP(slot); *sp += 4; }
static void restore(uchar **sp, int slot) { *sp -= 4; OP(slot) = *(void **)*sp; }

void fill_mode0_push(uchar *sp)
{
    D32(sp, 0) = grd_canvas->gc.fill_type;
    D32(sp, 4) = grd_canvas->gc.fill_parm;
    *(void **)(sp + 8) = OP(1);
    SET(1, do_tnop_32);
    set_fill_clut();
    sp[0xC] = 0;
    fill_stackp = sp + 0xD;
}

void fill_mode0_pop(uchar *sp)
{
    OP(1) = *(void **)(sp - 5);
    set_fill_type(D32(sp, -0xD));
    grd_canvas->gc.fill_parm = D32(sp, -9);
    fill_stackp = sp - 0xD;
}

void fill_mode1_push(uchar *sp)
{
    D32(sp, 0) = grd_canvas->gc.fill_type;
    D32(sp, 4) = grd_canvas->gc.fill_parm;
    *(void **)(sp + 8) = OP(1);
    SET(1, do_tnop_32);
    *(void **)(sp + 0xC) = OP(0x2D);
    SET(0x2D, fill_op_3156CC);
    *(void **)(sp + 0x10) = OP(0x31);
    SET(0x31, fill_op_3156D4);
    set_fill_clut();
    sp[0x14] = 1;
    fill_stackp = sp + 0x15;
}

void fill_mode1_pop(uchar *sp)
{
    OP(0x31) = *(void **)(sp - 5);
    OP(0x2D) = *(void **)(sp - 9);
    OP(1) = *(void **)(sp - 0xD);
    set_fill_type(D32(sp, -0x15));
    grd_canvas->gc.fill_parm = D32(sp, -0x11);
    fill_stackp = sp - 0x15;
}

void fill_mode2_push(uchar *sp)
{
    save(&sp, 0x32); SET(0x32, do_jnorm);
    save(&sp, 0x2D); SET(0x2D, fill_op_3156CC);
    save(&sp, 0x31); SET(0x31, fill_op_3156D4);
    *sp++ = 2;
    fill_stackp = sp;
}

void fill_mode2_pop(uchar *sp)
{
    sp--;
    restore(&sp, 0x31);
    restore(&sp, 0x2D);
    restore(&sp, 0x32);
    fill_stackp = sp;
}

void fill_mode3_push(uchar *sp)
{
    save(&sp, 1);    SET(1, fill_op_315762);
    save(&sp, 0x2D); SET(0x2D, fill_op_31574A);
    save(&sp, 0x31); SET(0x31, fill_op_315752);
    save(&sp, 5);    SET(5, fill_op_31575A);
    save(&sp, 3);    SET(3, fill_op_315727);
    save(&sp, 4);    SET(4, fill_op_3157AC);
    save(&sp, 0x32); SET(0x32, fill_op_315762);
    save(&sp, 0x26); SET(0x26, fill_op_315811);
    save(&sp, 6);    SET(6, fill_op_315876);
    save(&sp, 0x1B); SET(0x1B, fill_op_31575A);
    *sp++ = 3;
    fill_stackp = sp;
    g3d_fill_func_ofs = 1;
}

/* bug: no sp-- over the mode byte (see the header) */
void fill_mode3_pop(uchar *sp)
{
    restore(&sp, 0x1B);
    restore(&sp, 6);
    restore(&sp, 0x26);
    restore(&sp, 0x32);
    restore(&sp, 4);
    restore(&sp, 3);
    restore(&sp, 5);
    restore(&sp, 0x31);
    restore(&sp, 0x2D);
    restore(&sp, 1);
    fill_stackp = sp;
    g3d_fill_func_ofs = 0;
}

void fill_mode4_push(uchar *sp)
{
    save(&sp, 0x2D); SET(0x2D, fill_op_3158C4);
    save(&sp, 0x31); SET(0x31, fill_op_315953);
    save(&sp, 0x32); SET(0x32, fill_op_315921);
    save(&sp, 4);    SET(4, fill_op_3159C4);
    save(&sp, 0x26); SET(0x26, fill_op_315A84);
    *sp++ = 4;
    fill_stackp = sp;
}

/* bug: falls through into mode 2's push */
void fill_mode4_pop(uchar *sp)
{
    sp--;
    restore(&sp, 0x26);
    restore(&sp, 4);
    restore(&sp, 0x32);
    restore(&sp, 0x31);
    restore(&sp, 0x2D);
    fill_stackp = sp;
    fill_mode2_push(sp);
}

void fill_mode5_push(uchar *sp)
{
    set_variants(WATCOM_FN(do_poly_var_2F5359), WATCOM_FN(do_poly_var_2F5359), WATCOM_FN(do_poly_var_2F5359));
    save(&sp, 4);    SET(4, do_tnop_2b);
    save(&sp, 0x26); SET(0x26, fill_op_31571F);
    save(&sp, 0x31); SET(0x31, fill_op_315752);
    save(&sp, 0x1B); SET(0x1B, fill_op_31575A);
    save(&sp, 5);    SET(5, fill_op_31575A);
    *sp++ = 5;
    fill_stackp = sp;
}

static void default_variants(void)
{
    set_variants(WATCOM_FN(do_poly_var_2F5401), WATCOM_FN(do_poly_var_2F5334), WATCOM_FN(do_poly_var_2F538C));
}

void fill_mode5_pop(uchar *sp)
{
    default_variants();
    sp--;
    restore(&sp, 5);
    restore(&sp, 0x1B);
    restore(&sp, 0x31);
    restore(&sp, 0x26);
    restore(&sp, 4);
    fill_stackp = sp;
}

void fill_mode6_push(uchar *sp)
{
    set_variants(WATCOM_FN(do_poly_var_2F5426), WATCOM_FN(do_poly_var_2F5426), WATCOM_FN(do_poly_var_2F5426));
    save(&sp, 0x26); SET(0x26, fill_op_315717);
    save(&sp, 0x31); SET(0x31, fill_op_315752);
    save(&sp, 0x1B); SET(0x1B, fill_op_31575A);
    save(&sp, 5);    SET(5, fill_op_31575A);
    *sp++ = 6;
    fill_stackp = sp;
}

void fill_mode6_pop(uchar *sp)
{
    default_variants();
    sp--;
    restore(&sp, 5);
    restore(&sp, 0x1B);
    restore(&sp, 0x31);
    restore(&sp, 0x26);
    fill_stackp = sp;
}

/* 0x3653D6: push / pop per mode */
static void (*const fill_mode_table[7][2])(uchar *sp) = {
    { fill_mode0_push, fill_mode0_pop }, { fill_mode1_push, fill_mode1_pop },
    { fill_mode2_push, fill_mode2_pop }, { fill_mode3_push, fill_mode3_pop },
    { fill_mode4_push, fill_mode4_pop }, { fill_mode5_push, fill_mode5_pop },
    { fill_mode6_push, fill_mode6_pop } };

void g3_push_fill_mode(int mode)
{
    fill_depth++;
    fill_mode_table[mode][0](fill_stackp);
}

/* pops the mode named by the byte below the stack pointer */
void g3_pop_fill_mode(void)
{
    fill_depth--;
    fill_mode_table[fill_stackp[-1]][1](fill_stackp);
}

/* ---------------- helpers ---------------- */
static void draw_poly(long color, int n, ulong edx, uchar *ebp)
{
    tn3c_regs r = { 0 };
    r.eax = color; r.ecx = n; r.esi = (ulong)poly_list; r.ebx = (ulong)grd_canvas;
    r.edx = edx; r.ebp = (ulong)ebp;
    tn3c_call(B_draw_poly_common, &r);
}

static long light_value(void *norm, void *pos, ulong ecx)
{
    tn3c_regs r = { 0 };
    r.eax = (ulong)norm; r.edx = (ulong)pos; r.ecx = ecx;
    tn3c_call(B_g3_light, &r);
    return (long)r.eax >> (g3d_light_shift & 31);
}

/* TRUE when point idx's record says it is lit: record byte 2 + 4 > the reference byte the
 * record's word selects (16-bit signed compare) */
static bool rec_lit(uint idx)
{
    uchar *rec = fill_point_rec + 3 * idx;
    short a = (short)(rec[2] + 4);
    short b = fill_light_ref[W16(rec, 0)];
    return a > b;
}

/* SF of -light_src . v (64-bit sum): TRUE = facing away from the light */
bool fill_facing_light(g3s_vector *v)
{
    long lx = -_g3d_light_src.x, ly = -_g3d_light_src.y, lz = -_g3d_light_src.z;
    long long s = (long long)lx * v->x + (long long)ly * v->y + (long long)lz * v->z;
    return s < 0;
}

/* ---------------- replacement handlers ---------------- */
uchar *fill_op_3156CC(uchar *pc) { return pc + 0x1C; }
uchar *fill_op_3156D4(uchar *pc) { return pc + 3; }

/* unreferenced: like op 0x31 with style 1 without storing the style */
uchar *fill_op_3156DC(uchar *pc)
{
    set_fill_clut();
    g3d_poly_type = G3_POLY;
    return pc + 3;
}

/* tmap as poly / as outline: the texture word is skipped */
uchar *fill_op_315717(uchar *pc) { return do_polyres(pc + 2); }
uchar *fill_op_31571F(uchar *pc) { return do_tnop_2b(pc + 2); }

/* multires with g3_rotate_list */
uchar *fill_op_315727(uchar *pc)
{
    tn3c_regs r = { 0 };
    uint n = W16(pc, 2);
    r.ecx = n;
    r.esi = (ulong)(pc + 6);
    r.ebx = W16(pc, 4);
    r.edi = (ulong)&resbuf[W16(pc, 4)];
    r.ebp = (ulong)(pc + 6 + n * 12);
    tn3c_call(A_g3_rotate_list, &r);
    return pc + 6 + n * 12;
}

uchar *fill_op_31574A(uchar *pc) { return pc + 0x1C; }
uchar *fill_op_315752(uchar *pc) { return pc + 3; }
uchar *fill_op_31575A(uchar *pc) { return pc + 4; }

/* jnorm against the light: the vector at +4 */
uchar *fill_op_315762(uchar *pc)
{
    if (fill_facing_light((g3s_vector *)(pc + 4)))
        return pc + 0x1C;
    return pc + S16(pc, 2);
}

/* mode 3: makes point idx[i] a flat 2D point and puts it in poly_list; returns the last edx */
static ulong flatten_points(uchar *idx, int n)
{
    int i = n - 1;
    ulong edx;
    do {
        g3s_point *p = resbuf[W16(idx, 2 * i)];
        poly_list[i] = p;
        p->p3_flags |= PF_I | PF_PROJECTED;
        p->codes = 0;
        p->sy = p->x;
        p->sx = p->y;
        edx = (ulong)(p->z >> 8);
        p->i = (sfix)edx;
    } while (--i >= 0);
    return edx;
}

uchar *fill_op_3157AC(uchar *pc)
{
    int n = W16(pc, 2);
    ulong edx = flatten_points(pc + 4, n);
    pc += 4 + 2 * n;
    g3d_poly_type = fill_poly_type;
    draw_poly(grd_canvas->gc.fcolor, n, (edx & ~0xFFul) | fill_poly_type, pc);
    return pc;
}

uchar *fill_op_315811(uchar *pc)
{
    int n = W16(pc, 4);
    ulong edx = flatten_points(pc + 6, n);
    pc += 6 + 2 * n;
    g3d_poly_type = fill_poly_type;
    draw_poly(grd_canvas->gc.fcolor, n, (edx & ~0xFFul) | fill_poly_type, pc);
    return pc;
}

/* sortnorm against the light (normal at +2) */
uchar *fill_op_315876(uchar *pc)
{
    if (fill_facing_light((g3s_vector *)(pc + 2))) {
        interpreter_loop(pc + D32(pc, 0x1A));
        interpreter_loop(pc + D32(pc, 0x1E));
    } else {
        interpreter_loop(pc + D32(pc, 0x1E));
        interpreter_loop(pc + D32(pc, 0x1A));
    }
    return pc + 0x22;
}

/* mode 4 op 2D: i of point word 2 = light value when its record says lit, else ambient;
 * sets PF_I */
uchar *fill_op_3158C4(uchar *pc)
{
    uint idx = W16(pc, 2);
    long l;
    g3s_point *p;

    if (rec_lit(idx))
        l = light_value(pc + 4, pc + 0x10, idx);
    else
        l = _g3d_amb_light >> (g3d_light_shift & 31);
    p = resbuf[idx];
    p->i = (sfix)l;
    p->p3_flags |= PF_I;
    return pc + 0x1C;
}

/* mode 4 op 32: when facing, fill_light_value = light value; +0x1C.  Else jump. */
uchar *fill_op_315921(uchar *pc)
{
    tn3c_regs r = { 0 };
    r.esi = (ulong)(pc + 0x10); r.edi = (ulong)(pc + 4); r.ebp = (ulong)pc;
    tn3c_call(B_g3_check_normal_facing, &r);
    if (!(r.eflags & TN3C_SF))
        return pc + S16(pc, 2);
    r.eax = (ulong)(pc + 4); r.edx = (ulong)(pc + 0x10);
    tn3c_call(B_g3_light, &r);
    fill_light_value = (short)((long)r.eax >> (g3d_light_shift & 31));
    return pc + 0x1C;
}

/* mode 4 op 31: fill_style = byte 2; flat fill, poly type 2 for styles 1 and 2 */
uchar *fill_op_315953(uchar *pc)
{
    uint s = pc[2];
    fill_style = s;
    set_fill_norm();
    g3d_poly_type = (s == 1 || s == 2) ? G3_SPOLY : G3_POLY;
    return pc + 3;
}

/* mode 4: fills poly_list from idx; with fill_style 1 saves each point's i and relights it */
static void relight_points(uchar *idx, int n, int set_flag)
{
    int i = n - 1;
    do {
        uint k = W16(idx, 2 * i);
        g3s_point *p = resbuf[k];
        poly_list[i] = p;
        if (set_flag)
            p->p3_flags |= PF_I;
        if (fill_style == 1) {
            fill_saved_i[i] = p->i;
            if (rec_lit(k))
                p->i = fill_light_value;
            else
                p->i = (sfix)(_g3d_amb_light >> (g3d_light_shift & 31));
        }
    } while (--i >= 0);
}

/* puts back the saved i values, for i = n down to 0 (one too many) */
static void unlight_points(uchar *idx, int n)
{
    int i;
    if (fill_style != 1)
        return;
    i = n;
    do {
        resbuf[W16(idx, 2 * i)]->i = fill_saved_i[i];
    } while (--i >= 0);
}

/* mode 4 op 04 */
uchar *fill_op_3159C4(uchar *pc)
{
    int n = W16(pc, 2);
    relight_points(pc + 4, n, 1);
    draw_poly(grd_canvas->gc.fcolor, n, 0, pc);
    unlight_points(pc + 4, n);
    return pc + 4 + 2 * n;
}

/* mode 4 op 26 (tmappers chosen by poly type 2) */
uchar *fill_op_315A84(uchar *pc)
{
    tn3c_regs r = { 0 };
    int n = W16(pc, 4), tex = W16(pc, 2);
    relight_points(pc + 6, n, 0);
    r.eax = n; r.edi = tex; r.edx = (ulong)poly_list; r.ebx = (ulong)tmap_tab[tex];
    r.ecx = (ulong)-1; r.esi = (ulong)resbuf[W16(pc, 6)]; r.ebp = (ulong)pc;
    tn3c_call(g3d_poly_type == G3_SPOLY ? (ulong)g3_lit_tmap_func : (ulong)g3_tmap_func, &r);
    unlight_points(pc + 6, n);
    return pc + 6 + 2 * n;
}
