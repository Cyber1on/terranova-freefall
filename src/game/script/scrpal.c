/* scrpal.c -- TN script palette opcodes and helpers.  Decompiled from __FF.EXE (object file
 * 0x2E8420-0x2E87F2; names ours).
 *
 *   0x2E8420 ScriptOpFadeOut     0x2E84CC ScriptOpFadeIn      0x2E856C ScriptOpPalRes
 *   0x2E85F4 ScriptOpPalRef      0x2E8690 ScriptSavePal       0x2E86A8 ScriptPalFromRef
 *   0x2E8740 ScriptPalBlack      0x2E8788 ScriptPalRestore    0x2E87A8 ScriptSetPal
 *
 * While the screen is faded out (script_pal_held), palette changes go to script_pal instead of
 * the hardware; fading in shows script_pal.  ScriptPalFromRef / ScriptPalBlack /
 * ScriptPalRestore are engine functions 6-8 of the scripts; ScriptSetPal is called by the
 * shell (0x2B5CA1).
 */
#include "t1_a.h"

#ifndef TN_LINK_ORIGINAL_DATA
uchar script_pal[0x300];
uchar script_pal_held = 1;
#endif

/* opcode table (0x35F69C in the original; registered by main) */
const ScriptOpReg script_pal_ops[] = {
    { 0x48, WATCOM_FN(ScriptOpFadeOut) },
    { 0x49, WATCOM_FN(ScriptOpFadeIn) },
    { 0x4A, WATCOM_FN(ScriptOpPalRes) },
    { 0x4B, WATCOM_FN(ScriptOpPalRef) },
    { 0, NULL }
};

void tnui_task_60f4(void);                                  /* 0x2E60F4 */
void tnui_task_6174(void);                                  /* 0x2E6174 */

/* runs a palette effect to its end, one step per vertical retrace */
static void run_effect(uchar id)
{
    while (palette_query_effect((signed char)id) == 1) {
        gfx_2E421C();
        palette_advance_effect((signed char)id, 1);
    }
}

/* op 0x48 delay: fades the screen to black (the palette is kept in script_pal). */
uchar *ScriptOpFadeOut(uchar *pc)
{
    uchar black[0x300];
    short delay;
    uchar id;

    anim_2E66F0();
    tnui_task_60f4();
    tnui_task_6174();
    script_sp--;
    delay = (short)script_stack[script_sp];
    gr_get_pal(0, 0x100, script_pal);
    memset(black, 0, 0x300);
    id = palette_install_effect(0, 0, 0, 0xFF, 0, delay, script_pal, black);
    run_effect(id);
    script_pal_held = 1;
    return pc + 1;
}

/* op 0x49 delay: fades from black to script_pal. */
uchar *ScriptOpFadeIn(uchar *pc)
{
    uchar black[0x300];
    short delay;
    uchar id;

    anim_2E66F0();
    tnui_task_60f4();
    tnui_task_6174();
    script_sp--;
    delay = (short)script_stack[script_sp];
    memset(black, 0, 0x300);
    id = palette_install_effect(0, 1, 0, 0xFF, 0, delay, black, script_pal);
    run_effect(id);
    script_pal_held = 0;
    return pc + 1;
}

/* op 0x4A res: palette resource {short start, short n, rgb[n]}. */
uchar *ScriptOpPalRes(uchar *pc)
{
    short *p;
    short start, n;

    script_sp--;
    p = ResGet((ushort)script_stack[script_sp]);
    start = p[0];
    n = p[1];
    if (script_pal_held)
        memcpy(script_pal + start * 3, p + 2, n * 3);
    else
        gr_set_pal(start, n, (uchar *)(p + 2));
    return pc + 1;
}

/* palette of a frame (+0x18 offset of {short start, short n, rgb[n]} in its resource) */
static short *frame_pal(Ref ref)
{
    uchar *f = RefGet(ref);

    if (*(long *)(f + 0x18) == 0)
        return NULL;
    return (short *)((uchar *)ResGet((ushort)(ref >> 16)) + *(long *)(f + 0x18));
}

/* op 0x4B ref: palette of an animation (its first frame's resource). */
uchar *ScriptOpPalRef(uchar *pc)
{
    Ref ref;
    ushort id;
    short *p;

    script_sp--;
    ref = script_stack[script_sp];
    if (script_pal_held) {
        id = *(ushort *)((uchar *)RefGet(ref) + 4);
        p = frame_pal((Ref)id << 16);
        if (p)
            memcpy(script_pal + p[0] * 3, p + 2, p[1] * 3);
    } else
        anim_2E6B48(ref);
    return pc + 1;
}

void ScriptSavePal(void)
{
    gr_get_pal(0, 0x100, script_pal);
}

/* Engine function 6: palette of a frame resource.  The scripts push the result of every
 * engine function: here the frame address without a palette, the byte count when the palette
 * is held, else whatever gr_set_pal (void) leaves in eax (0 here). */
long ScriptPalFromRef(Ref ref)
{
    uchar *f = RefGet(ref);
    short *p;
    long n;

    if (*(long *)(f + 0x18) == 0)
        return (long)f;
    p = (short *)((uchar *)ResGet((ushort)(ref >> 16)) + *(long *)(f + 0x18));
    if (script_pal_held) {
        n = p[1] * 3;
        memcpy(script_pal + p[0] * 3, p + 2, n);
        return n;
    }
    gr_set_pal(p[0], p[1], (uchar *)(p + 2));
    return 0;
}

/* Engine function 7: black screen, palette kept in script_pal.  Result: gr_set_pal's eax (0). */
long ScriptPalBlack(void)
{
    uchar black[0x300];

    gr_get_pal(0, 0x100, script_pal);
    memset(black, 0, 0x300);
    gr_set_pal(0, 0x100, black);
    script_pal_held = 1;
    return 0;
}

/* Engine function 8.  Result: gr_set_pal's eax with bits 8-15 cleared (0). */
long ScriptPalRestore(void)
{
    gr_set_pal(0, 0x100, script_pal);
    script_pal_held = 0;
    return 0;
}

void ScriptSetPal(int start, int n, uchar *data)
{
    if (script_pal_held)
        memcpy(script_pal + start * 3, data, n * 3);
    else
        gr_set_pal(start, n, data);
}
