/* xbr.frag.hlsl -- "Smooth": rounds off the pixel stairs on edges and keeps the rest sharp.
 * Ported to HLSL for Terra Nova: Freefall from xbr-lv2 (libretro glsl-shaders; corner rule C,
 * smooth tips, scale 3, default parameters); the neighbour offsets its vertex shader passed are
 * worked out here. Its notice follows:
 *
 * Hyllian's xBR-lv2 Shader
 *
 * Copyright (C) 2011-2016 Hyllian - sergiogdb@gmail.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Incorporates some of the ideas from SABR shader. Thanks to Joshua Street. */
#include "common.hlsli"

#define XBR_SCALE 3.0
#define XBR_EQ_THRESHOLD 15.0
#define LV2_CF 2.0

static const float3 rgbw = float3(14.352, 28.176, 5.472);
static const float4 delta = float4(1.0 / XBR_SCALE, 1.0 / XBR_SCALE, 1.0 / XBR_SCALE, 1.0 / XBR_SCALE);
static const float4 delta_l = float4(0.5 / XBR_SCALE, 1.0 / XBR_SCALE, 0.5 / XBR_SCALE, 1.0 / XBR_SCALE);
static const float4 delta_u = float4(1.0 / XBR_SCALE, 0.5 / XBR_SCALE, 1.0 / XBR_SCALE, 0.5 / XBR_SCALE);

static const float4 Ao = float4(1.0, -1.0, -1.0, 1.0);
static const float4 Bo = float4(1.0, 1.0, -1.0, -1.0);
static const float4 Co = float4(1.5, 0.5, -0.5, 0.5);
static const float4 Ax = float4(1.0, -1.0, -1.0, 1.0);
static const float4 Bx = float4(0.5, 2.0, -0.5, -2.0);
static const float4 Cx = float4(1.0, 1.0, -0.5, 0.0);
static const float4 Ay = float4(1.0, -1.0, -1.0, 1.0);
static const float4 By = float4(2.0, 0.5, -2.0, -0.5);
static const float4 Cy = float4(2.0, 0.0, -1.0, 0.5);
static const float4 Ci = float4(0.25, 0.25, 0.25, 0.25);

float4 df(float4 A, float4 B) { return abs(A - B); }
float4 diff(float4 A, float4 B) { return float4(A != B); }
float4 eq(float4 A, float4 B) { return step(df(A, B), XBR_EQ_THRESHOLD); }
float4 neq(float4 A, float4 B) { return 1.0 - eq(A, B); }
float4 wd(float4 a, float4 b, float4 c, float4 d, float4 e, float4 f, float4 g, float4 h)
{
    return df(a, b) + df(a, c) + df(d, e) + df(d, f) + 4.0 * df(g, h);
}
float c_df(float3 c1, float3 c2)
{
    float3 d = abs(c1 - c2);
    return d.r + d.g + d.b;
}

PSOutput main(PSInput input)
{
    PSOutput output;
    float2 uv = input.v_uv;
    float dx = source_size.z, dy = source_size.w;
    float4 t1 = uv.xxxy + float4(-dx, 0, dx, -2.0 * dy);    /* A1 B1 C1 */
    float4 t2 = uv.xxxy + float4(-dx, 0, dx, -dy);          /*  A  B  C */
    float4 t3 = uv.xxxy + float4(-dx, 0, dx, 0);            /*  D  E  F */
    float4 t4 = uv.xxxy + float4(-dx, 0, dx, dy);           /*  G  H  I */
    float4 t5 = uv.xxxy + float4(-dx, 0, dx, 2.0 * dy);     /* G5 H5 I5 */
    float4 t6 = uv.xyyy + float4(-2.0 * dx, -dy, 0, dy);    /* A0 D0 G0 */
    float4 t7 = uv.xyyy + float4(2.0 * dx, -dy, 0, dy);     /* C4 F4 I4 */
    float4 edri, edr, edr_l, edr_u, px, irlv0, irlv1, irlv2l, irlv2u, fx, fx_l, fx_u;
    float2 fp = frac(uv * source_size.xy);

    float3 A1 = tex(t1.xw), B1 = tex(t1.yw), C1 = tex(t1.zw);
    float3 A = tex(t2.xw), B = tex(t2.yw), C = tex(t2.zw);
    float3 D = tex(t3.xw), E = tex(t3.yw), F = tex(t3.zw);
    float3 G = tex(t4.xw), H = tex(t4.yw), I = tex(t4.zw);
    float3 G5 = tex(t5.xw), H5 = tex(t5.yw), I5 = tex(t5.zw);
    float3 A0 = tex(t6.xy), D0 = tex(t6.xz), G0 = tex(t6.xw);
    float3 C4 = tex(t7.xy), F4 = tex(t7.xz), I4 = tex(t7.xw);

    float4 b = float4(dot(B, rgbw), dot(D, rgbw), dot(H, rgbw), dot(F, rgbw));
    float4 c = float4(dot(C, rgbw), dot(A, rgbw), dot(G, rgbw), dot(I, rgbw));
    float4 d = b.yzwx;
    float4 e = dot(E, rgbw).xxxx;
    float4 f = b.wxyz;
    float4 g = c.zwxy;
    float4 h = b.zwxy;
    float4 i = c.wxyz;
    float4 i4 = float4(dot(I4, rgbw), dot(C1, rgbw), dot(A0, rgbw), dot(G5, rgbw));
    float4 i5 = float4(dot(I5, rgbw), dot(C4, rgbw), dot(A1, rgbw), dot(G0, rgbw));
    float4 h5 = float4(dot(H5, rgbw), dot(F4, rgbw), dot(B1, rgbw), dot(D0, rgbw));
    float4 f4 = h5.yzwx;

    /* the lines below which interpolation happens */
    fx = Ao * fp.y + Bo * fp.x;
    fx_l = Ax * fp.y + Bx * fp.x;
    fx_u = Ay * fp.y + By * fp.x;

    irlv0 = diff(e, f) * diff(e, h);
    irlv1 = irlv0 * (neq(f, b) * neq(f, c) + neq(h, d) * neq(h, g) +
                     eq(e, i) * (neq(f, f4) * neq(f, i4) + neq(h, h5) * neq(h, i5)) + eq(e, g) + eq(e, c));
    irlv2l = diff(e, g) * diff(d, g);
    irlv2u = diff(e, c) * diff(b, c);

    float4 fx45i = saturate((fx + delta - Co - Ci) / (2.0 * delta));
    float4 fx45 = saturate((fx + delta - Co) / (2.0 * delta));
    float4 fx30 = saturate((fx_l + delta_l - Cx) / (2.0 * delta_l));
    float4 fx60 = saturate((fx_u + delta_u - Cy) / (2.0 * delta_u));

    float4 wd1 = wd(e, c, g, i, h5, f4, h, f);
    float4 wd2 = wd(h, d, i5, f, i4, b, e, i);

    edri = step(wd1, wd2) * irlv0;
    edr = step(wd1 + 0.1, wd2) * step(0.5, irlv1);
    edr_l = step(LV2_CF * df(f, g), df(h, c)) * irlv2l * edr;
    edr_u = step(LV2_CF * df(h, c), df(f, g)) * irlv2u * edr;

    fx45 = edr * fx45;
    fx30 = edr_l * fx30;
    fx60 = edr_u * fx60;
    fx45i = edri * fx45i;

    px = step(df(e, f), df(e, h));

    float4 maximos = max(max(fx30, fx60), max(fx45, fx45i));

    float3 res1 = E;
    res1 = lerp(res1, lerp(H, F, px.x), maximos.x);
    res1 = lerp(res1, lerp(B, D, px.z), maximos.z);

    float3 res2 = E;
    res2 = lerp(res2, lerp(F, B, px.y), maximos.y);
    res2 = lerp(res2, lerp(D, H, px.w), maximos.w);

    output.o_color = float4(lerp(res1, res2, step(c_df(E, res1), c_df(E, res2))), 1.0);
    return output;
}
