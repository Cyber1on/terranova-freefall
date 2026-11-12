/* chain.c -- x:\prj\tech\libsrc\2d\chain.c (Terra Nova v1.09): function chaining.
 * Decompiled from __FF.EXE.
 *
 *   0x31C440 chn_null (ret)           0x31C444 gri_chain_stash (static)
 *   0x31C4C4 gr_chain_add_over        0x31C548 gr_chain_add_before   0x31C58C gr_chain_add_after
 *   0x31C5F0 gr_unchain               0x31C630 gr_rechain            0x31C668 gr_unchain_all
 *   0x31C680 gr_rechain_all           0x31C698 chain_rest            0x31C6F0 gr_force_generic
 *   0x31C724 gr_unforce_generic
 *
 * Terra Nova's version differs from System Shock's (a later revision):
 *  - the stash of the original primitives is a shared helper and stores chn_null for bitmap
 *    types whose canvas table is missing;
 *  - gr_chain_add_before builds its own node (flags 1); gr_chain_add_over (flags 0) inserts
 *    behind the leading "before" nodes instead of at the head;
 *  - chain_rest has no void-function handling: it returns the next node's function when the
 *    current node has flags 0, else the stashed primitive;
 *  - gr_force_generic takes the generic tables from table set 11, which nothing fills, so it
 *    installs NULL tables (kept).
 */
#include "2d.h"

#define GRD_CANVAS_FUNCS 324
#define GET_PIXEL24 7

#ifndef TN_LINK_ORIGINAL_DATA
grs_func_chain *grd_chain_table[GRD_CANVAS_FUNCS];          /* 0x45DDD0 */
void (**chn_primitives[GRD_CANVAS_FUNCS])();                /* 0x36E352 */
int gr_current_primitive;                                   /* 0x36E862 */
grs_func_chain *gr_current_chain;                           /* 0x36DF44 */
extern void (*chn_canvas_table[GRD_CANVAS_FUNCS])();        /* 0x36E866 chnfuncs.asm entry stubs */
#else
extern grs_func_chain *grd_chain_table[GRD_CANVAS_FUNCS];
extern void (**chn_primitives[GRD_CANVAS_FUNCS])();
extern int gr_current_primitive;
extern grs_func_chain *gr_current_chain;
extern void (*chn_canvas_table[GRD_CANVAS_FUNCS])();
#endif

/* Placeholder primitive for bitmap types without a canvas table (just `ret`). */
void chn_null(void)
{
}

/* First chaining of primitive n: stash the primitives of every bitmap type and replace them
 * by the chain handler. */
void gri_chain_stash(int n)
{
    int k;

    chn_primitives[n] = (void (**)())gr_malloc(BMT_TYPES * sizeof(void (*)()));
    for (k = 0; k < BMT_TYPES; k++) {
        if (grd_canvas_table_list[k] != NULL)
            chn_primitives[n][k] = grd_canvas_table_list[k][n];
        else
            chn_primitives[n][k] = (void (*)())WATCOM_FN(chn_null);
    }
    for (k = 0; k < BMT_TYPES; k++)
        if (grd_canvas_table_list[k] != NULL)
            grd_canvas_table_list[k][n] = chn_canvas_table[n];
}

/* Adds f to primitive n's chain, replacing the primitive (behind any "before" functions). */
grs_func_chain *gr_chain_add_over(int n, void (*f)())
{
    grs_func_chain *q = grd_chain_table[n];
    grs_func_chain *p = (grs_func_chain *)gr_malloc(sizeof(grs_func_chain));

    p->next = NULL;
    p->flags = 0;
    p->f = f;
    if (q == NULL) {
        gri_chain_stash(n);
        grd_chain_table[n] = p;
        return p;
    }
    if (q->flags != 1) {
        p->next = q;
        grd_chain_table[n] = p;
        return p;
    }
    while (q->next != NULL) {
        if (q->next->flags != 1) {
            p->next = q->next;
            break;
        }
        q = q->next;
    }
    q->next = p;
    return p;
}

/* Adds f at the head of primitive n's chain, to run before it. */
grs_func_chain *gr_chain_add_before(int n, void (*f)(void))
{
    grs_func_chain *p = (grs_func_chain *)gr_malloc(sizeof(grs_func_chain));

    if (grd_chain_table[n] == NULL)
        gri_chain_stash(n);
    p->f = f;
    p->flags = 1;
    p->next = grd_chain_table[n];
    grd_chain_table[n] = p;
    return p;
}

/* Adds f at the end of primitive n's chain, to run after it. */
grs_func_chain *gr_chain_add_after(int n, void (*f)(void))
{
    grs_func_chain *p = (grs_func_chain *)gr_malloc(sizeof(grs_func_chain));
    grs_func_chain *q;

    p->next = NULL;
    p->f = f;
    p->flags = 3;
    if (grd_chain_table[n] == NULL) {
        gri_chain_stash(n);
        grd_chain_table[n] = p;
    } else {
        q = grd_chain_table[n];
        while (q->next != NULL)
            q = q->next;
        q->next = p;
    }
    return p;
}

/* Puts the original primitive n back into the tables. */
void gr_unchain(int n)
{
    int k;

    if (chn_primitives[n] == NULL)
        return;
    for (k = 0; k < BMT_TYPES; k++)
        if (grd_canvas_table_list[k] != NULL)
            grd_canvas_table_list[k][n] = chn_primitives[n][k];
}

/* Puts the chain handler for primitive n back into the tables. */
void gr_rechain(int n)
{
    int k;

    if (chn_primitives[n] == NULL)
        return;
    for (k = 0; k < BMT_TYPES; k++)
        if (grd_canvas_table_list[k] != NULL)
            grd_canvas_table_list[k][n] = chn_canvas_table[n];
}

void gr_unchain_all(void)
{
    int k;

    for (k = 0; k < GRD_CANVAS_FUNCS; k++)
        gr_unchain(k);
}

void gr_rechain_all(void)
{
    int k;

    for (k = 0; k < GRD_CANVAS_FUNCS; k++)
        gr_rechain(k);
}

/* Returns the next function of the current chain, or the stashed primitive. */
void (*chain_rest(void))()
{
    int prim = gr_current_primitive;
    grs_func_chain *c = gr_current_chain;
    void (*f)();

    if (c != NULL && c->flags == 0) {
        f = c->f;
        gr_current_chain = c->next;
    } else if (prim <= GET_PIXEL24 || prim >= CALC_ROW)
        f = chn_primitives[prim][grd_pixel_index];
    else
        f = chn_primitives[prim][grd_canvas_index];
    gr_current_primitive = prim;
    return f;
}

void gr_force_generic(void)
{
    chn_flags |= CHN_GEN;
    grd_canvas_table_list[BMT_GEN] = grd_canvas_table_set[GRD_TS_GEN];
    grd_function_table_list[BMT_GEN] = grd_function_table_set[GRD_TS_GEN];
    if (grd_canvas_table != NULL)
        gr_set_canvas(grd_canvas);
}

void gr_unforce_generic(void)
{
    chn_flags &= ~CHN_GEN;
    gr_set_canvas(grd_canvas);
}
