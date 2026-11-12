/* bitmap.c -- x:\prj\tech\libsrc\2d\bitmap.c (Terra Nova v1.09): bitmap handling.
 * Decompiled from __FF.EXE.
 *
 *   0x2F21D4 gr_init_bitmap     0x2F22C4 gr_init_sub_bitmap     0x2F2404 gr_alloc_bitmap
 *
 * Terra Nova knows bitmap type 8 (BMT_FLAT16, 2 bytes per pixel).
 * Quirk kept: for types 6, 7 and > 8 gr_init_bitmap stores an uninitialised row (whatever
 * the caller left in edi in the original; here an indeterminate value).
 */
#include "2d.h"

#ifndef TN_LINK_ORIGINAL_DATA
static grs_bitmap gr_alloc_tmp_bm;  /* 0x44C584, static so it works at interrupt time */
#else
extern grs_bitmap gr_alloc_tmp_bm;
#endif

/* Initialises a bitmap structure; row is computed from type and w, align is 0. */
void gr_init_bitmap(grs_bitmap *bm, uchar *p, uchar type, ushort flags, short w, short h)
{
    int row;
    int v;

    switch (type) {
    case BMT_DEVICE: row = grd_screen_row; break;
    case BMT_MONO: row = (w + 7) / 8; break;
    case BMT_FLAT8: case BMT_TLUC8: row = w; break;
    case BMT_FLAT24: row = 3 * w; break;
    case BMT_RSD8: row = 0; break;
    case BMT_FLAT16: row = 2 * w; break;
    default: row = 0; break;  /* original: uninitialised (caller's edi) */
    }
    bm->align = 0;
    bm->hlog = 0;
    bm->flags = flags;
    bm->bits = p;
    bm->type = type;
    bm->h = h;
    bm->wlog = bm->hlog;
    bm->w = w;
    bm->row = row;
    for (v = w >> 1; v != 0; v >>= 1)
        bm->wlog++;
    for (v = h >> 1; v != 0; v >>= 1)
        bm->hlog++;
}

/* Sets dbm to the w x h part of sbm at (x, y). */
void gr_init_sub_bitmap(grs_bitmap *sbm, grs_bitmap *dbm, short x, short y, short w, short h)
{
    *dbm = *sbm;
    dbm->w = w;
    dbm->h = h;
    switch (sbm->type) {
    case BMT_DEVICE:
        /* chain to the device driver's SUB_BITMAP (canvas table of BMT_DEVICE, not the
         * current one); ecx happens to hold y as well. */
        WATCOM_CALL(grd_canvas_table_list[BMT_DEVICE][SUB_BITMAP], (long)dbm, x, y, y);
        break;
    case BMT_MONO:
        dbm->bits += y * dbm->row + x / 8;
        if ((dbm->align += x % 8) > 7) {
            dbm->align -= 8;
            dbm->bits++;
        }
        break;
    case BMT_FLAT8:
    case BMT_TLUC8:
        dbm->bits += y * dbm->row + x;
        break;
    case BMT_FLAT24:
        dbm->bits += y * dbm->row + 3 * x;
        break;
    case BMT_FLAT16:
        dbm->bits += y * dbm->row + 2 * x;
        break;
    default:
        break;
    }
}

/* Allocates a bitmap structure and its pixels in one block (bits right after the header);
 * freeing the returned pointer frees both. */
grs_bitmap *gr_alloc_bitmap(uchar type, ushort flags, short w, short h)
{
    uchar *p;

    gr_init_bitmap(&gr_alloc_tmp_bm, NULL, type, flags, w, h);
    p = (uchar *)gr_malloc(gr_alloc_tmp_bm.h * gr_alloc_tmp_bm.row + sizeof(grs_bitmap));
    if (p != NULL) {
        gr_alloc_tmp_bm.bits = p + sizeof(grs_bitmap);
        *(grs_bitmap *)p = gr_alloc_tmp_bm;
    }
    return (grs_bitmap *)p;
}
