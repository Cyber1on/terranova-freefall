/* cursctrl.c -- Terra Nova sticky cursor control (first file of module M040; source name
 * unknown, the error string names CursCtrlDrawStickyCursor).  Decompiled from __FF.EXE v1.09.
 *
 *   0x27C840 damage_27C840             initialise a cursor control block for a view size
 *   0x27C880 damage_27C880             cursor control block of an entity (player / aircraft /
 *                                      other)
 *   0x27C8B8 damage_27C8B8             classify a view position into a 3x4 zone grid
 *   0x27C9C4 damage_27C9C4             zone + steering values of a view position
 *   0x27C9F4 damage_27C9F4             resource id of a cursor kind
 *   0x27C9FC CursCtrlDrawStickyCursor  rescale to a new view size and draw the cursor
 *   0x27CADC damage_27CADC             copy a block rescaled to another view size
 *   0x27CB58 damage_27CB58             steering values of a zone
 *
 * Positions and sizes are passed as packed {short x, y} dwords.  A zone (0..11) is
 * row * 3 + column, column from cursctrl_xlim (3 limits), row from cursctrl_ylim (4 limits).
 * The module ends at 0x27CC85 (zero padding); damage_27CCA0 and DamageInitSystems are dmgsys.c.
 */
#include "g3_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
CursCtrl cursctrl_player, cursctrl_aircraft, cursctrl_other;
long game_392484;
uchar game_392488;
long cursctrl_xlim[4];
long cursctrl_ylim[5];
long cursctrl_refs[8];
ushort cursctrl_mask;
#endif

void damage_27C840(CursCtrl *c, short w, short h)
{
    c->f0 = 4;
    c->f1 = 4;
    c->hidden = 0;
    c->x = w / 2;
    c->y = h / 2;
    c->w = w;
    c->h = h;
}

CursCtrl *damage_27C880(short ent)
{
    if (ent == game_player_entity)
        return &cursctrl_player;
    if (G3A_ENT(ent).cls == 3)
        return &cursctrl_aircraft;
    return &cursctrl_other;
}

/* out: [0] zone, [1] x * 0x8000 / w, [2] y * 0x8000 / h, [3] position within the zone column,
 * [4] within the zone row (0..0x8000). Returns the zone. */
long damage_27C8B8(long pos, long size, short *out)
{
    long i, j;

    out[1] = ((long)(short)pos << 15) / (short)size;
    for (i = 0; i < 3; i++)
        if (out[1] < cursctrl_xlim[i + 1])
            break;
    out[2] = ((pos >> 16) << 15) / (size >> 16);
    for (j = 0; j < 4; j++)
        if (out[2] < cursctrl_ylim[j + 1])
            break;
    out[0] = (short)(j * 3 + i);
    out[3] = ((out[1] - cursctrl_xlim[i]) << 15) / (cursctrl_xlim[i + 1] - cursctrl_xlim[i]);
    if (out[0] == 7 || out[0] == 10)
        out[4] = ((out[2] - cursctrl_ylim[2]) << 15) / (cursctrl_ylim[4] - cursctrl_ylim[2]);
    else
        out[4] = ((out[2] - cursctrl_ylim[j]) << 15) / (cursctrl_ylim[j + 1] - cursctrl_ylim[j]);
    return out[0];
}

/* the original clobbers ebx (third register argument, unused) */
long damage_27C9C4(long pos, long size, long unused, fix *out)
{
    short z[6];

    damage_27C8B8(pos, size, z);
    game_392488 = 1;
    damage_27CB58(z, out);
    game_392488 = 0;
    return z[0];
}

long damage_27C9F4(long kind)
{
    return cursctrl_refs[kind];
}

void CursCtrlDrawStickyCursor(CursCtrl *c, long w, long h)
{
    grs_bitmap *bm;
    Ref ref;

    if (c->hidden)
        return;
    if (w != c->w) {
        if (c->w == 0)
            DbgReportError(1, "CursCtrlDrawStickyCursor: viewSize.x == 0\n");
        c->x = (long)c->x * w / c->w;
        c->w = (short)w;
    }
    if (h != c->h) {
        if (c->h == 0)
            DbgReportError(1, "CursCtrlDrawStickyCursor: viewSize.y == 0\n");
        c->y = (long)c->y * h / c->h;
        c->h = (short)h;
    }
    ref = damage_27C9F4(c->f1) + 0xB;
    RefGet(ref);
    bm = (grs_bitmap *)RefGet(ref);
    bm->bits = (uchar *)bm + 0x1C;
    /* the frame resource: a grs_bitmap followed by the hot spot (+0x10 x, +0x12 y) */
    WATCOM_CALL(grd_canvas_table[0x8F + bm->type * 2], (long)bm,
                (long)(short)(c->x - *(short *)((uchar *)bm + 0x10)),
                (long)(short)(c->y - *(short *)((uchar *)bm + 0x12)));
}

void damage_27CADC(CursCtrl *out, long w, long h, CursCtrl *in, long ow, long oh)
{
    out->f0 = in->f0;
    out->f1 = in->f1;
    out->hidden = 0;
    out->x = (long)in->x * w / ow;
    out->y = (long)in->y * h / oh;
}

void damage_27CB58(short *in, fix *out)
{
    fix v, a, b;

    out[0] = 0;
    ((short *)out)[2] = 0;
    ((short *)out)[3] = 0;
    if (game_392488 && game_392484 == 0)
        v = G3A_FIXMUL(*(fix *)((uchar *)&game_pba[game_player_slot] + 0xC6), 0x20000); /* speed */
    else
        v = 0x800;
    a = G3A_FIXDIV(v, 0x10000);
    b = G3A_FIXDIV(v, 0x10000);
    if (cursctrl_mask & (1u << (in[0] & 31)))
        ((short *)out)[2] = (short)G3A_FIXMUL(in[1] - 0x4000, 0x1000);
    switch ((ushort)in[0]) {
    case 0: case 1: case 2:
        out[0] = G3A_FIXMUL(0x7FFF - in[4], v);
        break;
    case 6: case 7: case 8: case 10:
        out[0] = G3A_FIXMUL(-in[4], a);
        break;
    case 9: case 11:
        out[0] = G3A_FIXMUL(in[1] - 0x4000 < 0 ? 0x4000 - in[1] : in[1] - 0x4000, b);
        ((ushort *)out)[3] = in[0] == 9 ? 0xC000 : 0x4000;
        break;
    }
}
