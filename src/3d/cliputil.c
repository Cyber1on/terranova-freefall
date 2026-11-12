/* cliputil.c -- two Terra Nova helpers compiled after clip.c (Watcom C; file and function
 * names ours).  Decompiled from __FF.EXE.
 *
 *   0x317A9C g3_draw_box          0x317BB4 g3_get_object_axes
 *
 * Both use the standard Watcom register convention and are unreferenced.
 */
#include "3d_a.h"

/* Draws the 12 edges of the axis-aligned box from b->lo to b->hi (box axes = world axes,
 * deltas rotated into view space), then frees its 8 points.  g3_draw_line is worker 3d-b's
 * asm entry (esi, edi), still the original code here. */
void g3_draw_box(g3s_box *b)
{
    g3s_phandle pt[8];
    fix dx = b->hi.x - b->lo.x;
    fix dy = b->hi.y - b->lo.y;
    fix dz = b->hi.z - b->lo.z;
    int i, j;

    pt[0] = g3_transform_point(&b->lo);
    pt[1] = g3_copy_add_delta_x(pt[0], dx);
    pt[2] = g3_copy_add_delta_y(pt[1], dy);
    pt[3] = g3_copy_add_delta_y(pt[0], dy);
    pt[4] = g3_copy_add_delta_z(pt[0], dz);
    pt[5] = g3_copy_add_delta_x(pt[4], dx);
    pt[6] = g3_copy_add_delta_y(pt[5], dy);
    pt[7] = g3_copy_add_delta_y(pt[4], dy);
    for (i = 0; i < 4; i++) {
        j = (i + 1) % 4;
        g3a_asm_call(G3_ORIG_DRAW_LINE, 0, 0, 0, 0, (long)pt[i], (long)pt[j]);
        g3a_asm_call(G3_ORIG_DRAW_LINE, 0, 0, 0, 0, (long)pt[4 + i], (long)pt[4 + j]);
        g3a_asm_call(G3_ORIG_DRAW_LINE, 0, 0, 0, 0, (long)pt[i], (long)pt[4 + i]);
    }
    g3_free_list(8, pt);
}

/* axes[0] = b->org in object space; axes[1..3] = the box extents (hi - org) along x, y, z,
 * each as a world vector put through g3_world_to_object (so they include the translation). */
void g3_get_object_axes(g3s_box *b, g3s_vector *axes)
{
    g3s_vector v;
    fix ex, ey, ez;

    g3_world_to_object(&axes[0], &b->org);
    ex = b->hi.x - b->org.x;
    ey = b->hi.y - b->org.y;
    ez = b->hi.z - b->org.z;
    v.x = ex;
    v.y = 0;
    v.z = 0;
    g3_world_to_object(&axes[1], &v);
    v.x = 0;
    v.z = 0;
    v.y = ey;
    g3_world_to_object(&axes[2], &v);
    v.z = ez;
    v.x = 0;
    v.y = 0;
    g3_world_to_object(&axes[3], &v);
}
