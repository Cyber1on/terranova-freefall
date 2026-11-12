/* fl8bldbl.c -- x:\prj\tech\libsrc\2d\fl8bldbl.c (Terra Nova v1.09): blended doubling of flat8
 * bitmaps.  Decompiled from __FF.EXE.
 *
 *   0x32BFA0 gr_sub_bm_init (name ours)      0x32BFE4 gr_sub_bm_close (name ours)
 *   0x32C010 flat8_flat8_v_double_ubitmap
 *   0x32C104 gri_flat8_hv_quadruple_sub_bitmap
 *   0x32C46C gri_flat8_hv_double_sub_bitmap
 *
 * Terra Nova differences from System Shock: the 32x32 sub bitmap buffer is allocated by
 * gr_sub_bm_init (which also sets the flag the perspective mapper tests and makes sure the
 * 2-level blend tables exist) instead of being static, the sub bitmap routines no longer set up
 * the blend tables themselves, each of them has its own "last source" cache and clears the
 * other's, and the double version fills the borders too (commented out in Shock).
 * gr_sub_bm_init returns 1 when the buffer allocation fails and also whenever gr_init_blend
 * (a bool in al, TRUE = ok in Shock) returns TRUE; only al is tested.
 */
#include "2d_prim.h"

#define D_ROW 32
#define GR_BLEND_TABLE_SIZE 0x10000

static void copy16(uchar *d, uchar *s)
{
    int k;
    for (k = 0; k < 16; k++) d[k] = s[k];
}

static void set8(uchar *d, uchar c)
{
    int k;
    for (k = 0; k < 8; k++) d[k] = c;
}

int gr_sub_bm_init(void)
{
    gr_sub_bm_enabled = 1;
    grd_sub_bm_buffer = (uchar *)gr_malloc(D_ROW * D_ROW);
    if (grd_sub_bm_buffer == NULL)
        return 1;
    if (grd_log_blend_levels != 2)
        if (gr_init_blend(2))
            return 1;
    return 0;
}

int gr_sub_bm_close(void)
{
    gr_sub_bm_enabled = 0;
    if (grd_sub_bm_buffer != NULL) {
        gr_free(grd_sub_bm_buffer);
        grd_sub_bm_buffer = NULL;
    }
    return gr_free_blend();
}

void flat8_flat8_v_double_ubitmap(grs_bitmap *bm)
{
    int i, j, bpv;
    uchar *src = bm->bits, *dst = grd_bm.bits, *src_nxt, *dst_nxt;
    int dst_skip = grd_bm.row - bm->w, src_skip = bm->row - bm->w;

    {   /* first copy the top line (inline memcpy, rep movsd + rep movsb) */
        int n = bm->w;
        uchar *d = dst, *s = src;
        unsigned k;
        for (k = (unsigned)n >> 2; k; k--, d += 4, s += 4) *(ulong *)d = *(ulong *)s;
        for (k = n & 3; k; k--) *d++ = *s++;
    }
    for (i = 0; i < bm->h - 1; i++) {   /* for each row source, 2 destination */
        src_nxt = src + bm->row;
        dst += grd_bm.row;              /* interpolated row */
        dst_nxt = dst + grd_bm.row;     /* next clone row */
        for (j = 0; j < bm->w; j++) {
            bpv = *src_nxt++;
            *dst++ = grd_half_blend[(bpv << 8) | (*src++)];
            *dst_nxt++ = bpv;
        }
        dst += dst_skip; src += src_skip;
    }
}

/* destination bitmap: the 32x32 buffer */
static void init_sub_dst(grs_bitmap *dst_bm)
{
    dst_bm->h = D_ROW;
    dst_bm->w = D_ROW;
    dst_bm->bits = grd_sub_bm_buffer;
    dst_bm->wlog = 5;
    dst_bm->row = dst_bm->w;
    dst_bm->hlog = dst_bm->wlog;
}

/* fill the top, bottom, left and right borders from the 16x16 middle */
static void fill_sub_borders(grs_bitmap *dst_bm)
{
    uchar *dst;
    int i;

    dst = dst_bm->bits + (D_ROW + 1) * (D_ROW / 4);
    for (i = 0; i < D_ROW / 4; i++) {
        copy16(dst - D_ROW, dst);
        dst -= D_ROW;
    }
    dst = dst_bm->bits + (3 * D_ROW + 1) * (D_ROW / 4);
    for (i = 0; i < D_ROW / 4; i++) {
        copy16(dst, dst - D_ROW);
        dst += D_ROW;
    }
    dst = dst_bm->bits;
    for (i = 0; i < D_ROW; i++) {
        set8(dst, dst[D_ROW / 4]);
        set8(dst + 3 * D_ROW / 4, dst[(3 * D_ROW / 4) - 1]);
        dst += D_ROW;
    }
}

void gri_flat8_hv_quadruple_sub_bitmap(grs_bitmap *src_bm, grs_bitmap *dst_bm, int u, int v)
{
    int i, j, full_h_blend;
    uchar *src, *dst;

    init_sub_dst(dst_bm);
    src = src_bm->bits + u + src_bm->row * v;
    if (src == grd_last_sub_bm) return;
    grd_last_sub_bm = src;
    grd_last_dsub_bm = NULL;

    dst = dst_bm->bits + (D_ROW + 1) * (D_ROW / 4);
    full_h_blend = (v + 4 < src_bm->h);

    /* horizontally blend the source bitmap into every fourth destination row */
    for (i = 0; i < 4 + full_h_blend; i++) {
        for (j = 0; j < 4; j++) {
            if (u + j + 1 >= src_bm->w) {
                dst[3] = dst[2] = dst[1] = dst[0] = src[j];
            } else {
                int k = (src[j + 1]) | (src[j] << 8);
                dst[0] = src[j];
                dst[1] = grd_blend[k];
                dst[2] = grd_blend[k + GR_BLEND_TABLE_SIZE];
                dst[3] = grd_blend[k + 2 * GR_BLEND_TABLE_SIZE];
            }
            dst += 4;
        }
        dst += 4 * (D_ROW - 4);
        src += src_bm->row;
    }
    /* vertically blend the destination columns */
    dst = dst_bm->bits + (D_ROW + 1) * (D_ROW / 4);
    for (i = 0; i < 4 + full_h_blend - 1; i++) {
        for (j = 0; j < 16; j++) {
            int k = (dst[j + 4 * D_ROW]) | (dst[j] << 8);
            dst[j + D_ROW] = grd_blend[k];
            dst[j + 2 * D_ROW] = grd_blend[k + GR_BLEND_TABLE_SIZE];
            dst[j + 3 * D_ROW] = grd_blend[k + 2 * GR_BLEND_TABLE_SIZE];
        }
        dst += 4 * D_ROW;
    }
    if (full_h_blend == 0) {           /* bottom edge: copy the bottom row 3 times */
        copy16(dst + D_ROW, dst);
        copy16(dst + 2 * D_ROW, dst);
        copy16(dst + 3 * D_ROW, dst);
    }
    fill_sub_borders(dst_bm);
}

void gri_flat8_hv_double_sub_bitmap(grs_bitmap *src_bm, grs_bitmap *dst_bm, int u, int v)
{
    int i, j, full_h_blend;
    uchar *src, *dst;

    init_sub_dst(dst_bm);
    src = src_bm->bits + u + src_bm->row * v;
    if (src == grd_last_dsub_bm) return;
    grd_last_dsub_bm = src;
    grd_last_sub_bm = NULL;

    dst = dst_bm->bits + (D_ROW + 1) * (D_ROW / 4);
    full_h_blend = (v + 8 < src_bm->h);

    /* horizontally blend the source bitmap into every other destination row */
    for (i = 0; i < 8 + full_h_blend; i++) {
        for (j = 0; j < 8; j++) {
            if (u + j + 1 >= src_bm->w) {
                dst[1] = dst[0] = src[j];
            } else {
                int k = (src[j + 1]) | (src[j] << 8);
                dst[0] = src[j];
                dst[1] = grd_half_blend[k];
            }
            dst += 2;
        }
        dst += 2 * (D_ROW - 8);
        src += src_bm->row;
    }
    /* vertically blend the destination columns */
    dst = dst_bm->bits + (D_ROW + 1) * (D_ROW / 4);
    for (i = 0; i < 8 + full_h_blend - 1; i++) {
        for (j = 0; j < 16; j++) {
            int k = (dst[j + 2 * D_ROW]) | (dst[j] << 8);
            dst[j + D_ROW] = grd_half_blend[k];
        }
        dst += 2 * D_ROW;
    }
    if (full_h_blend == 0)
        copy16(dst + D_ROW, dst);
    fill_sub_borders(dst_bm);
}
