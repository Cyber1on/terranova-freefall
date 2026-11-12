/* regcall.c -- standalone versions of the 3D library's register-call glue.
 *
 * Much of the 3D library was hand-written asm in which routines pass arguments in whatever
 * registers suit them.  The decompiled C of one 3D module calls routines of another one through
 * small helpers that load a register block and call the original address (g3a_asm_call,
 * g3b_call, tn3c_call; in the verification build these are asm, see src/3d/alloc.c,
 * g3b_glue.c, interp.c).  In the port every one of those routines is our C, so the helpers
 * become a table: for each routine, which registers carry which C argument and which register
 * gets the result.  A routine can be named by its original address or by our C function (code
 * pointers in the original data are translated to our functions at load time).
 *
 * Flags: g3_check_normal_facing's callers test the sign flag the asm leaves; it is set when the
 * dot product is negative, which the C returns as bit 0 (to be confirmed by the frame
 * comparison once the port runs).
 */
#include "3d_a.h"
#include "3d_b.h"
#include "3d_c.h"
#include "port.h"

typedef struct {
    ulong eax, ebx, ecx, edx, esi, edi, ebp, eflags;
} regs;

#define P(x) ((void *)(x))
#define SF 0x80

void sfx_28DC9C(g3s_vector *v);      /* the game's 3D model callback (sfx.c), eax = model data */

static void dispatch(ulong addr, regs *r, const void *from)
{
    switch (addr) {
    /* vector.c / points.c / matrix.c (3d-a) */
    case 0x2F403B: r->eax = g3_vec_dotprod(P(r->esi), P(r->edi)); break;
    case 0x2F406A: g3_vec_normalize(P(r->esi)); break;
    case 0x2F40BB: g3_compute_normal_quick(P(r->edi), P(r->eax), P(r->edx), P(r->ebx)); break;
    case 0x2F4240: g3_world_to_object(P(r->edi), P(r->esi)); break;
    case 0x2F42F5: r->edi = (ulong)g3_rotate_point(P(r->esi)); break;
    case 0x2F431E: r->edi = (ulong)g3_transform_point(P(r->esi)); break;
    case 0x2F4323: r->eax = g3_project_point(P(r->edi)); break;
    case 0x2F4389: r->eax = g3_transform_list(r->ecx, P(r->edi), P(r->esi)); break;
    case 0x2F43C0: r->eax = g3_rotate_list(r->ecx, P(r->edi), P(r->esi)); break;
    case 0x2F4A11: r->edi = (ulong)g3_copy_add_delta_x(P(r->esi), r->eax); break;
    case 0x2F4A60: r->edi = (ulong)g3_copy_add_delta_y(P(r->esi), r->eax); break;
    case 0x2F4AAF: r->edi = (ulong)g3_copy_add_delta_z(P(r->esi), r->eax); break;
    case 0x2F4BB5: r->edi = (ulong)g3_copy_add_delta_xy(P(r->esi), r->eax, r->ebx); break;
    case 0x2F4C4E: r->edi = (ulong)g3_copy_add_delta_xz(P(r->esi), r->eax, r->ebx); break;
    case 0x2F4CE8: r->edi = (ulong)g3_copy_add_delta_yz(P(r->esi), r->eax, r->ebx); break;
    case 0x2FA42F: angles_2_matrix(P(r->esi), P(r->edi), r->ecx); break;
    case 0x2FA793: g3_vec_rotate(P(r->ebx), P(r->esi), P(r->edi)); break;
    case 0x2FA820: g3_transpose(P(r->esi)); break;
    case 0x2FA83C: g3_copy_transpose(P(r->edi), P(r->esi)); break;
    case 0x2FA871: g3_matrix_x_matrix(P(r->ebx), P(r->esi), P(r->edi)); break;
    case 0x2F8705: g3_scale_object(r->eax); break;
    /* light.c / polygon.c / tmap.c / clip.c (3d-b) */
    case 0x2F8A28: g3_eval_light_obj_cen(P(r->edi)); break;
    case 0x2F8C09: r->eax = g3_light(P(r->eax), P(r->edx)); break;
    case 0x2F95EE: r->eax = g3_draw_poly(r->eax, r->ecx, P(r->esi)); break;
    case 0x2F95F5: r->eax = draw_poly_common(r->eax, r->ecx, P(r->esi)); break;
    case 0x2F97D8: r->eax = g3_draw_line(P(r->esi), P(r->edi)); break;
    case 0x2F99AD:
        r->eax = g3_check_normal_facing(P(r->esi), P(r->edi));
        r->eflags = (r->eax & 1) ? SF : 0;
        break;
    case 0x316A74: r->eax = g3_clip_polygon(r->ecx, P(r->esi), P(r->edi)); break;
    /* texture mappers stored in g3_tmap_func / g3_lit_tmap_func (eax = n, edx = list, ebx = bm) */
    case 0x2F8F2F: r->eax = g3_draw_lmap(r->eax, P(r->edx), P(r->ebx)); break;
    case 0x2F8F46: r->eax = g3_light_lmap(r->eax, P(r->edx), P(r->ebx)); break;
    case 0x2F8F5D: r->eax = g3_light_tmap(r->eax, P(r->edx), P(r->ebx)); break;
    case 0x2F8F74: r->eax = g3_draw_tmap(r->eax, P(r->edx), P(r->ebx)); break;
    /* game callbacks the 3D interpreter's op 2A calls (g3d_interp_callbacks) */
    case 0x28DC9C: sfx_28DC9C(P(r->eax)); break;
    default: {
        const char *n = port_func_name(addr), *c;
        unsigned long off = 0;
        c = port_routine_at(from, &off);
        port_log("3D register call to %s (0x%lX) is not ported yet\n", n ? n : "an unknown routine", addr);
        port_log("  called from %s+0x%lX; eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX ebp %08lX\n",
                 c ? c : "?", off, r->eax, r->ebx, r->ecx, r->edx, r->esi, r->edi, r->ebp);
        port_fatal("");
    }
    }
}

/* addr may be an original address or one of our functions.  An original routine's address is
 * taken as one first: our program is linked above them, but if Windows placed it lower anyway,
 * one of our functions could sit at an original address (P40: 0x2FA820, g3_transpose, was our
 * objdmg_apply). */
static ulong orig_of(ulong addr)
{
    ulong a;
    if (port_func_at(addr))
        return addr;
    a = port_orig_addr((void *)addr);
    return a ? a : addr;
}

void tn3c_call(ulong addr, tn3c_regs *r)
{
    dispatch(orig_of(addr), (regs *)r, __builtin_return_address(0));
}

void g3b_call(ulong addr, g3b_regs *r)
{
    dispatch(orig_of(addr), (regs *)r, __builtin_return_address(0));
}

long g3a_asm_call(long f, long eax, long ebx, long ecx, long edx, long esi, long edi)
{
    regs r = { (ulong)eax, (ulong)ebx, (ulong)ecx, (ulong)edx, (ulong)esi, (ulong)edi, 0, 0 };
    dispatch(orig_of((ulong)f), &r, __builtin_return_address(0));
    return (long)r.eax;
}
