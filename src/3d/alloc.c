/* alloc.c -- x:\prj\tech\libsrc\3d\alloc.asm (Terra Nova v1.09): g3_init, the point free
 * list, start/end frame.  Decompiled from __FF.EXE (hand-written asm in the original;
 * RCS string at 0x35F966).  Names follow System Shock's ALLOC.C / 3D.H.
 *
 *   0x2EC290 g3_init              0x2EC40E g3_shutdown          0x2EC42D g3_count_free_points
 *   0x2EC442 g3_alloc_list        0x2EC45F g3_alloc_point       0x2EC471 g3_free_point
 *   0x2EC47F g3_free_list         0x2EC495 g3_dup_point         0x2EC4B2 g3_start_frame_wh
 *   0x2EC4C5 g3_start_frame       0x2EC56F g3_end_frame
 *
 * Register conventions of the original entries (C versions are cdecl):
 *   g3_init         eax=max_points ebx=x axis ecx=y axis edx=z axis -> eax (ax = n_points)
 *   g3_alloc_list   ecx=n esi=list -> eax; clobbers ebx
 *   g3_alloc_point  -> eax; clobbers ebx            g3_free_point  eax=p; clobbers ebx
 *   g3_free_list    ecx=n esi=list; clobbers eax ebx ecx
 *   g3_dup_point    esi=src -> edi; clobbers ebx ecx esi (esi = src + 32)
 *   g3_start_frame_wh eax=window width ebx=height ecx=pixel ratio (TN; name ours)
 *   g3_shutdown, g3_count_free_points, g3_start_frame, g3_end_frame: no arguments, Watcom.
 *
 * Also here: g3a_asm_call, the glue this worker's C uses to call other 3D asm entries that
 * are still original code (custom registers).
 */
#include "3d_a.h"

/* ------------------------------------------------------------------------------------- */
/* g3a_asm_call(f, eax, ebx, ecx, edx, esi, edi): cdecl -> register call, ebp preserved. */
#ifdef TN_WATCOM_ABI
__asm__(
    "\t.intel_syntax noprefix\n"
    "\t.text\n"
    "\t.globl g3a_asm_call\n"
    "g3a_asm_call:\n"
    "\tpush ebp\n\tpush ebx\n\tpush esi\n\tpush edi\n"
    "\tmov eax, [esp+24]\n\tmov ebx, [esp+28]\n\tmov ecx, [esp+32]\n"
    "\tmov edx, [esp+36]\n\tmov esi, [esp+40]\n\tmov edi, [esp+44]\n"
    "\tcall dword ptr [esp+20]\n"
    "\tpop edi\n\tpop esi\n\tpop ebx\n\tpop ebp\n\tret\n"
    "\t.att_syntax prefix\n");
#else
/* standalone: g3a_asm_call is in port/core/regcall.c */
#endif

/* The original sets _wtoo_matrix to the identity and _wtoo_position to 0 (rep stosd of 12
 * dwords, then m1 = m5 = m9 = 1.0) in g3_init, g3_start_frame and g3_end_frame. */
static void wtoo_identity(void)
{
    long *w = (long *)&_wtoo_matrix;
    int i;
    for (i = 0; i < 12; i++)
        w[i] = 0;
    _wtoo_matrix.m1 = FIX_UNIT;
    _wtoo_matrix.m5 = FIX_UNIT;
    _wtoo_matrix.m9 = FIX_UNIT;
}

/* Sets up the axis mapping and allocates the point pool.  Returns the number of points
 * (0 if the allocation fails).
 * Quirks kept:
 *  - the three g3d_axis_neg bytes are only ever set to 1, never cleared;
 *  - the third swap test (y >= z) swaps the negate bytes but not the axis values;
 *  - up_axis is computed from the *signed* y argument: 3 * (user_y_axis - 1), which is
 *    negative for AXIS_DOWN (-9 in Terra Nova: process_view_matrix then reads the horizon
 *    vector from unscaled_matrix);
 *  - first_free is not set (g3_start_frame builds the free list).
 * Original bug not reproduced: on allocation failure the asm returns with max_points still
 * pushed, so it "returns" to address max_points and crashes. */
short g3_init(short max_points, int user_x_axis, int user_y_axis, int user_z_axis)
{
    long x = user_x_axis, y = user_y_axis, z = user_z_axis;
    ulong n;
    uchar t;

    axis_swap_flag = 0;
    axis_neg_flag = 0;
    if (x < 0) {
        x = -x;
        g3d_axis_neg[0] = 1;
    }
    axis_x = x;
    if (y < 0) {
        y = -y;
        g3d_axis_neg[1] = 1;
    }
    axis_y = y;
    if (z < 0) {
        z = -z;
        g3d_axis_neg[2] = 1;
    }
    axis_z = z;
    if (x >= y) {
        long s = x;
        axis_swap_flag |= 1;
        x = y;
        y = s;
        t = g3d_axis_neg[0];
        g3d_axis_neg[0] = g3d_axis_neg[1];
        g3d_axis_neg[1] = t;
    }
    if (x >= z) {
        long s = x;
        axis_swap_flag |= 2;
        x = z;
        z = s;
        t = g3d_axis_neg[0];
        g3d_axis_neg[0] = g3d_axis_neg[2];
        g3d_axis_neg[2] = t;
    }
    if (y >= z) {
        axis_swap_flag |= 4;
        t = g3d_axis_neg[1];
        g3d_axis_neg[1] = g3d_axis_neg[2];
        g3d_axis_neg[2] = t;
    }
    axis_neg_flag = (uchar)((uchar)((uchar)(g3d_axis_neg[2] << 1 | g3d_axis_neg[1]) << 1) | g3d_axis_neg[0]);
    up_axis = 3 * (user_y_axis - 1);
    axis_x_ofs = 3 * (axis_x - 1);
    axis_y_ofs = 3 * (axis_y - 1);
    axis_z_ofs = 3 * (axis_z - 1);
    pixel_ratio = grd_cap->aspect;

    n = (ushort)max_points;
    point_list = (g3s_point *)f_malloc(n * 32);
    if (point_list == NULL)
        return 0;
    n_points = (short)n;

    /* the division-overflow trap for g3_project_point's two idivs */
    g3a_div0_install(1);
    g3a_div0_add_range((void *)0x2F435B, (void *)0x2F437D);
    g3a_div0_add_range((void *)0x2F4344, (void *)0x2F437D);

    wtoo_identity();
    return n_points;
}

/* Frees the point pool. */
void g3_shutdown(void)
{
    f_free(point_list);
    n_points = 0;
    first_free = NULL;
}

int g3_count_free_points(void)
{
    int n = 0;
    g3s_point *p;
    for (p = first_free; p != NULL; p = p->next)
        n++;
    return n;
}

/* Takes n points off the free list into p[].  Returns n, or 0 if the free list is empty.
 * Quirk: only an empty list is checked; when it runs out part-way, NULL is stored and the
 * next link is read from address 0.  n <= 0 still takes one point (do-while) and returns n. */
int g3_alloc_list(int n, g3s_phandle *p)
{
    g3s_point *q = first_free;
    int i = 0;
    if (q == NULL)
        return 0;
    do {
        p[i] = q;
        q = q->next;
        i++;
    } while (i < n);
    first_free = q;
    return n;
}

/* Returns a free point, or NULL. */
g3s_phandle g3_alloc_point(void)
{
    g3s_point *p = first_free;
    if (p != NULL)
        first_free = p->next;
    return p;
}

void g3_free_point(g3s_phandle p)
{
    p->next = first_free;
    first_free = p;
}

/* Frees p[n-1] .. p[0] (in that order). */
void g3_free_list(int n_points, g3s_phandle *p)
{
    while (--n_points >= 0) {
        g3s_point *q = p[n_points];
        q->next = first_free;
        first_free = q;
    }
}

/* Copies a point into a new one.  Original bug kept: the 32 bytes are copied even when the
 * free list is empty (to address 0). */
g3s_phandle g3_dup_point(g3s_phandle src)
{
    g3s_point *p = first_free;
    ulong *d, *s = (ulong *)src;
    int i;
    if (p != NULL)
        first_free = p->next;
    d = (ulong *)p;
    for (i = 0; i < 8; i++)
        d[i] = s[i];
    return p;
}

/* Common tail of g3_start_frame (the original's jump target 0x2EC4EA). */
static void start_frame_common(void)
{
    g3s_point *p;
    long n;

    _scrw = window_width << 15;
    _biasx = _scrw;
    ww2 = _scrw >> 16;
    _scrh = window_height << 15;
    _biasy = _scrh;
    wh2 = _scrh >> 16;
    wtoo_identity();

    /* link all points into the free list.  Quirk: with fewer than 2 points first_free is
     * left as it was. */
    p = point_list;
    n = (ushort)n_points - 1;
    if (n <= 0)
        return;
    first_free = p;
    do {
        p->next = p + 1;
        p = p + 1;
    } while (--n != 0);
    p->next = NULL;
}

/* TN: start a frame with an explicit window size and pixel ratio (unreferenced; name ours). */
void g3_start_frame_wh(long ww, long wh, fix ratio)
{
    window_width = ww;
    window_height = wh;
    pixel_ratio = ratio;
    start_frame_common();
}

/* Reads the window size from the current canvas and frees all points. */
void g3_start_frame(void)
{
    pixel_ratio = grd_cap->aspect;
    window_width = (ushort)grd_canvas->bm.w;
    window_height = (ushort)grd_canvas->bm.h;
    start_frame_common();
}

/* TN: resets the world-to-object transform and returns 0 (Shock's version counts the points
 * still in use). */
int g3_end_frame(void)
{
    wtoo_identity();
    return 0;
}
