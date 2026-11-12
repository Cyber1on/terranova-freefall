/* skyasm.c -- Terra Nova module M093 (0x2B26E0-0x2B2929): the hand-written asm span loops of
 * the sky (rndsky.c, M082).  Decompiled from __FF.EXE v1.09 as equivalent C.
 *
 * The routines take their arguments in registers (no Watcom convention: nargs "custom", no
 * thunks) and are reached only from rndsky_2A7654 (cloud spans) and rndsky_2A8658 (water
 * reflection); they are verified through those callers.
 *
 *   0x2B26E0 rasm_2B26E0  cloud span: texel only
 *   0x2B2740 rasm_2B2740  cloud span through a table (at most 256 pixels)
 *   0x2B2785 rasm_2B2785  reflection column
 *   0x2B27DA rasm_2B27DA  patch the texture address into the four loops
 *   0x2B27FB rasm_2B27FB  patch the table address into the two table loops
 *   0x2B2810 rasm_2B2810  warped cloud span (warp tables sky_warp_a / sky_warp_b)
 *   0x2B28A0 rasm_2B28A0  warped cloud span through a table
 * The other map entries of this range (0x2B2719, 0x2B2726, 0x2B2739, 0x2B2770, 0x2B277C,
 * 0x2B279C, 0x2B280C, 0x2B2856, 0x2B2860, 0x2B2866, 0x2B2893, 0x2B28E8, 0x2B28F0, 0x2B28F6,
 * 0x2B290E) are padding, loop labels and patch points inside these routines.
 *
 * The original keeps its state in immediates patched into its own code (0xDEADBEEF /
 * 0xBABEBEAC / 0xAC placeholders).  Here those immediates are variables at the same code
 * addresses (rasm_* in g5_b.h), written and read as the asm does, so the original and this
 * C leave the same bytes behind and either can follow the other.
 *
 * Texture coordinates: u and v are 8.16 (integer byte in bl / bh, the 16-bit fractions in
 * the high halves of two registers); each step adds the fraction and then the integer part
 * with the carry (adc), so the coordinates wrap at 256.
 */
#include "g5_b.h"

typedef ulong u32;

/* add the 16.16 step whose fraction is in the high half; return the carry */
static int add_c(u32 *acc, u32 step)
{
    u32 t = *acc + step;
    int c = t < *acc;
    *acc = t;
    return c;
}

/* 0x2B26E0: esi = p, edi = dst, ebx = v:u integer bytes, ecx = v frac (high) : u frac (low) */
void rasm_2B26E0(uchar *dst, SkyParm *p, ulong uv, ulong frac)
{
    uchar *end = dst + p->len;
    uchar *tex;
    u32 vf = frac & 0xFFFF0000, uf = frac << 16;
    u32 dv = p->dv16, du = p->du_lo << 16;
    uchar u = (uchar)uv, v = (uchar)(uv >> 8), dui = p->du_i, dvi = p->dv_i;
    rasm_end_2B2735 = end;
    tex = rasm_tex_2B2722;
    do {
        uchar al = tex[(v << 8) | u];
        v += dvi + add_c(&vf, dv);
        u += dui + add_c(&uf, du);
        *dst++ = al;
    } while (dst != rasm_end_2B2735);
}

/* 0x2B2740: the same through a table, for (uchar)p->len pixels (0 = 256).  The u fraction
 * register also holds the integer steps in its low half (they accumulate into it). */
void rasm_2B2740(uchar *dst, SkyParm *p, ulong uv, ulong frac)
{
    u32 edx = (p->du_lo << 16) | *(u32 *)&p->du_i;
    uchar cnt = (uchar)p->len;
    u32 dv = p->dv16 & 0xFFFF0000;
    u32 vf = frac & 0xFFFF0000;
    u32 uf = frac << 16;
    uchar u = (uchar)uv, v = (uchar)(uv >> 8);
    uchar *tex = rasm_tex_2B276C, *tab = rasm_tab_2B2778;
    do {
        int c = add_c(&vf, dv);
        uchar al = tex[(v << 8) | u];
        v += (uchar)(edx >> 8) + c;
        c = add_c(&uf, edx);
        u += (uchar)edx + c;
        *dst++ = tab[al];
    } while (--cnt);
}

/* 0x2B2785: reflection column: edi = dst, esi = src (one row below the mirror line), edx =
 * row, ecx = n, ebx = {per-row table pointers, source offset per colour, water flags} */
void rasm_2B2785(uchar *dst, uchar *src, long row, ulong n, void **blk)
{
    uchar **rt = blk[0];
    uchar *flag = blk[2];
    long *off;
    rasm_off_2B27BB = blk[1];
    off = rasm_off_2B27BB;
    src += row;
    do {
        uchar c = *dst;
        src -= row;
        if (flag[c])
            *dst = rt[0][src[off[c]]];
        dst += row;
        rt++;
    } while (--n);
}

/* 0x2B27DA */
void rasm_2B27DA(uchar *tex)
{
    rasm_tex_2B2722 = tex;
    rasm_tex_2B2862 = tex;
    rasm_tex_2B28F2 = tex;
    rasm_tex_2B276C = tex;
}

/* 0x2B27FB */
void rasm_2B27FB(uchar *tab)
{
    rasm_tab_2B2778 = tab;
    rasm_tab_2B290A = tab;
}

/* 0x2B2810: warped span: texel (B[u] + v) << 8 | (A[v] + u), p->len pixels */
void rasm_2B2810(uchar *dst, SkyParm *p, ulong uv, ulong frac)
{
    long i = -(long)(p->len + 1);
    u32 uf = frac << 16, vf = frac & 0xFFFF0000;
    uchar u = (uchar)uv, v = (uchar)(uv >> 8), lo, hi;
    uchar *tex, *end;
    rasm_end_2B287A = dst + p->len;
    rasm_du_2B2868 = p->du_lo << 16;
    rasm_dui_2B286E = p->du_i;
    rasm_dvi_2B2877 = p->dv_i;
    rasm_dv_2B2871 = p->dv16;
    tex = rasm_tex_2B2862;
    end = rasm_end_2B287A;
    goto next;
    for (;;) {
        uchar al = tex[(hi << 8) | lo];
        u += rasm_dui_2B286E + add_c(&uf, rasm_du_2B2868);
        v += rasm_dvi_2B2877 + add_c(&vf, rasm_dv_2B2871);
        end[i] = al;
    next:
        hi = sky_warp_b[u] + v;
        lo = sky_warp_a[v] + u;
        if (++i == 0)
            break;
    }
}

/* 0x2B28A0: the same through a table */
void rasm_2B28A0(uchar *dst, SkyParm *p, ulong uv, ulong frac)
{
    long i = -(long)(p->len + 1);
    u32 uf = frac << 16, vf = frac & 0xFFFF0000;
    uchar u = (uchar)uv, v = (uchar)(uv >> 8), lo, hi;
    uchar *tex, *tab, *end;
    rasm_end_2B2910 = dst + p->len;
    rasm_du_2B28F8 = p->du_lo << 16;
    rasm_dui_2B28FE = p->du_i;
    rasm_dvi_2B2907 = p->dv_i;
    rasm_dv_2B2901 = p->dv16;
    tex = rasm_tex_2B28F2;
    tab = rasm_tab_2B290A;
    end = rasm_end_2B2910;
    goto next;
    for (;;) {
        uchar bl = tex[(hi << 8) | lo];
        u += rasm_dui_2B28FE + add_c(&uf, rasm_du_2B28F8);
        v += rasm_dvi_2B2907 + add_c(&vf, rasm_dv_2B2901);
        end[i] = tab[bl];
    next:
        hi = sky_warp_b[u] + v;
        lo = sky_warp_a[v] + u;
        if (++i == 0)
            break;
    }
}
