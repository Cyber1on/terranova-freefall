/* scrgfx.c -- TN script viewport and drawing opcodes.  Decompiled from __FF.EXE (object file
 * 0x2EA4C0-0x2EAAE5; names ours).
 *
 *   0x2EA4C0 ScriptOpVpOpen      0x2EA540 ScriptOpVpClose     0x2EA564 ScriptOpVpMove
 *   0x2EA5C0 ScriptOpVpMoveRel   0x2EA654 ScriptOpVpHide      0x2EA678 ScriptOpVpShow
 *   0x2EA69C ScriptOpVpBegin     0x2EA6C8 ScriptOpVpEnd       0x2EA708 ScriptOpFont
 *   0x2EA72C ScriptOpColor       0x2EA754 ScriptOpBackColor   0x2EA77C ScriptOpClip
 *   0x2EA7C4 ScriptOpClear       0x2EA7F0 ScriptOpLine2       0x2EA82C ScriptOpRect
 *   0x2EA870 ScriptOpBox         0x2EA8B4 ScriptOpBitmap      0x2EA940 ScriptOpText
 *   0x2EA9D4 ScriptOpBitmap2     0x2EAA90 ScriptPopRect
 *
 * Viewports are the tnui regions of 0x2E55D8..; a viewport has its canvas at +0x14 (fg color
 * +0x24, bg color +0x28, clip rectangle +0x3C..). Drawing goes through the current canvas table.
 */
#include "t1_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar *script_vp;
ushort script_font;
uchar script_vp_style[0x10] = { 0x6C, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x59 };
ScriptVpDesc script_vp_template = { script_vp_style, 2 };
#endif

/* opcode table (0x35F824 in the original; registered by main) */
const ScriptOpReg script_gfx_ops[] = {
    { 0x50, WATCOM_FN(ScriptOpVpOpen) },
    { 0x51, WATCOM_FN(ScriptOpVpClose) },
    { 0x52, WATCOM_FN(ScriptOpVpMove) },
    { 0x53, WATCOM_FN(ScriptOpVpMoveRel) },
    { 0x54, WATCOM_FN(ScriptOpVpHide) },
    { 0x55, WATCOM_FN(ScriptOpVpShow) },
    { 0x56, WATCOM_FN(ScriptOpVpBegin) },
    { 0x57, WATCOM_FN(ScriptOpVpEnd) },
    { 0x58, WATCOM_FN(ScriptOpFont) },
    { 0x59, WATCOM_FN(ScriptOpColor) },
    { 0x5A, WATCOM_FN(ScriptOpBackColor) },
    { 0x5B, WATCOM_FN(ScriptOpClip) },
    { 0x5C, WATCOM_FN(ScriptOpClear) },
    { 0x5D, WATCOM_FN(ScriptOpLine2) },
    { 0x5E, WATCOM_FN(ScriptOpRect) },
    { 0x5F, WATCOM_FN(ScriptOpBox) },
    { 0x60, WATCOM_FN(ScriptOpBitmap) },
    { 0x61, WATCOM_FN(ScriptOpText) },
    { 0x62, WATCOM_FN(ScriptOpBitmap2) },
    { 0, NULL }
};

/* op 0x50 b: (x0 y0 x1 y1 z -- vp) opens and shows a viewport (b: flag at +0x0F) */
uchar *ScriptOpVpOpen(uchar *pc)
{
    void *vp;

    script_sp--;
    script_vp_template.z = (short)script_stack[script_sp];
    ScriptPopRect(script_vp_template.rect);
    script_vp_template.flag = pc[1] != 0;
    vp = viewport_2E55D8((TnRegionParams *)&script_vp_template);
    viewport_2E57F0(vp);
    script_stack[script_sp] = (long)vp;
    script_tags[script_sp] = 0;
    script_sp++;
    return pc + 2;
}

/* op 0x51: (vp --) */
uchar *ScriptOpVpClose(uchar *pc)
{
    script_sp--;
    viewport_2E575C((void *)script_stack[script_sp]);
    return pc + 1;
}

/* op 0x52: (vp a b c --) */
uchar *ScriptOpVpMove(uchar *pc)
{
    short sp = script_sp;
    long c = script_stack[sp - 1];
    long b = script_stack[sp - 2];
    long a = script_stack[sp - 3];
    long vp = script_stack[sp - 4];

    script_sp = sp - 4;
    viewport_2E5954((void *)vp, a, b, c, 0, 0);
    return pc + 1;
}

/* op 0x53: (vp x y z --) as 0x52, relative to the current viewport */
uchar *ScriptOpVpMoveRel(uchar *pc)
{
    short sp = script_sp;
    uchar *cv = script_vp;
    long c = script_stack[sp - 1] - *(short *)(cv + 8);
    long b = script_stack[sp - 2] - *(short *)(cv + 0x0E);
    long a = script_stack[sp - 3] - *(short *)(cv + 0x0C);
    long vp = script_stack[sp - 4];

    script_sp = sp - 4;
    viewport_2E5954((void *)vp, a, b, c, 0, 0);
    return pc + 1;
}

/* op 0x54: (vp --) */
uchar *ScriptOpVpHide(uchar *pc)
{
    script_sp--;
    viewport_2E582C((void *)script_stack[script_sp]);
    return pc + 1;
}

/* op 0x55: (vp --) */
uchar *ScriptOpVpShow(uchar *pc)
{
    script_sp--;
    viewport_2E57F0((void *)script_stack[script_sp]);
    return pc + 1;
}

/* op 0x56: (vp --) draws into vp from now on */
uchar *ScriptOpVpBegin(uchar *pc)
{
    script_sp--;
    script_vp = (uchar *)script_stack[script_sp];
    gr_set_canvas((grs_canvas *)(script_vp + 0x14));
    return pc + 1;
}

/* op 0x57 b: ends drawing; b != 0: (x0 y0 x1 y1 --) updates only that rectangle */
uchar *ScriptOpVpEnd(uchar *pc)
{
    short r[4];

    if (pc[1]) {
        ScriptPopRect(r);
        viewport_2E5BC8((TnRegion *)script_vp, (LGRect *)r);
    } else
        viewport_2E5C94((TnRegion *)script_vp, NULL);
    script_vp = NULL;
    return pc + 2;
}

/* op 0x58: (font --) */
uchar *ScriptOpFont(uchar *pc)
{
    script_sp--;
    script_font = (ushort)script_stack[script_sp];
    return pc + 1;
}

/* op 0x59: (color --) */
uchar *ScriptOpColor(uchar *pc)
{
    script_sp--;
    *(long *)(script_vp + 0x24) = script_stack[script_sp];
    return pc + 1;
}

/* op 0x5A: (color --) */
uchar *ScriptOpBackColor(uchar *pc)
{
    script_sp--;
    *(long *)(script_vp + 0x28) = script_stack[script_sp];
    return pc + 1;
}

/* op 0x5B: (x0 y0 x1 y1 --) clip rectangle of the viewport's canvas */
uchar *ScriptOpClip(uchar *pc)
{
    short r[4];
    uchar *vp;

    ScriptPopRect(r);
    vp = script_vp;
    *(long *)(vp + 0x3C) = 0;
    *(short *)(vp + 0x42) = r[0];
    *(short *)(vp + 0x46) = r[1];
    *(short *)(vp + 0x4A) = r[2];
    *(short *)(vp + 0x4E) = r[3];
    return pc + 1;
}

/* op 0x5C: (color --) canvas table entry 8 */
uchar *ScriptOpClear(uchar *pc)
{
    script_sp--;
    WATCOM_CALL(grd_canvas_table[8], script_stack[script_sp]);
    return pc + 1;
}

/* op 0x5D: (x0 y0 x1 y1 --) */
uchar *ScriptOpLine2(uchar *pc)
{
    short r[4];

    ScriptPopRect(r);
    gr_iline(r[0], r[1], r[2], r[3]);
    return pc + 1;
}

/* op 0x5E: (x0 y0 x1 y1 --) canvas table entry 18 */
uchar *ScriptOpRect(uchar *pc)
{
    short r[4];

    ScriptPopRect(r);
    WATCOM_CALL(grd_canvas_table[18], r[0], r[1], r[2], r[3]);
    return pc + 1;
}

/* op 0x5F: (x0 y0 x1 y1 --) canvas table entry 16 */
uchar *ScriptOpBox(uchar *pc)
{
    short r[4];

    ScriptPopRect(r);
    WATCOM_CALL(grd_canvas_table[16], r[0], r[1], r[2], r[3]);
    return pc + 1;
}

/* op 0x60: (ref x y --) draws a bitmap (canvas table entry 0x8F + 2 * bitmap type) */
uchar *ScriptOpBitmap(uchar *pc)
{
    short sp = script_sp;
    long y = script_stack[sp - 1];
    long x = script_stack[sp - 2];
    grs_bitmap *bm;

    script_sp = sp - 3;
    bm = RefGet(script_stack[sp - 3]);
    bm->bits = (uchar *)bm + 0x1C;
    WATCOM_CALL(grd_canvas_table[0x8F + bm->type * 2], (long)bm, (short)x, (short)y);
    return pc + 1;
}

/* op 0x61: (x y ref --) formats string ref and draws it in script_font */
uchar *ScriptOpText(uchar *pc)
{
    char buf[0x200];
    short sp = script_sp;
    Ref ref = script_stack[sp - 1];
    long y = script_stack[sp - 2];
    long x = script_stack[sp - 3];

    script_sp = sp - 3;
    buf[0] = 0;
    ScriptFormat(ref, buf, buf + 0x200);
    GfxDrawText(script_font, buf, x, y);
    return pc + 1;
}

/* op 0x62 b: b != 0: (ref x0 y0 x1 y1 --), else (ref x y --): a bitmap through the 2D helpers
 * 0x309264 / 0x3091F0 */
uchar *ScriptOpBitmap2(uchar *pc)
{
    short r[4];
    short sp;
    long x, y;
    Ref ref;

    if (pc[1]) {
        ScriptPopRect(r);
        script_sp--;
        ref = script_stack[script_sp];
        gr_int_309264(RefLock(ref), r);
    } else {
        sp = script_sp;
        y = script_stack[sp - 1];
        x = script_stack[sp - 2];
        ref = script_stack[sp - 3];
        script_sp = sp - 3;
        gr_int_3091F0(RefLock(ref), (short)x, (short)y);
    }
    ResUnlock((ushort)(ref >> 16));
    return pc + 2;
}

/* (x0 y0 x1 y1 --) into r[0..3] */
short *ScriptPopRect(short *r)
{
    short sp = script_sp;

    r[3] = (short)script_stack[sp - 1];
    r[2] = (short)script_stack[sp - 2];
    r[1] = (short)script_stack[sp - 3];
    r[0] = (short)script_stack[sp - 4];
    script_sp = sp - 4;
    return r;
}
