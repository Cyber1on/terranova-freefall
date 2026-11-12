/* crt.frag.hlsl -- CRT monitor look: scanlines, shadow mask, screen curve and a little bloom.
 * Ported to HLSL for Terra Nova: Freefall from crt-lottes (libretro glsl-shaders), whose notice
 * follows:
 *
 * PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER
 *
 *   by Timothy Lottes
 *
 * This is more along the style of a really good CGA arcade monitor.
 * With RGB inputs instead of NTSC.
 * The shadow mask example has the mask rotated 90 degrees for less chromatic aberration.
 *
 * Left it unoptimized to show the theory behind the algorithm.
 *
 * It is an example what I personally would want as a display option for pixel art games.
 * Please take and use, change, or whatever.
 *
 * params: x = curve (0 = flat, 1 = crt-lottes' default), y = mask strength (0..1) */
#include "common.hlsli"

static const float hardScan = -8.0;
static const float hardPix = -3.0;
static const float maskDark = 0.5;
static const float maskLight = 1.5;
static const float hardBloomPix = -1.5;
static const float hardBloomScan = -2.0;
static const float bloomAmount = 0.15;
static const float shape = 2.0;

float ToLinear1(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 ToLinear(float3 c) { return float3(ToLinear1(c.r), ToLinear1(c.g), ToLinear1(c.b)); }
float ToSrgb1(float c) { return c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055; }
float3 ToSrgb(float3 c) { return float3(ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b)); }

/* nearest emulated sample given floating point position and texel offset; black off screen */
float3 Fetch(float2 pos, float2 off)
{
    pos = (floor(pos * source_size.xy + off) + 0.5) * source_size.zw;
    if (pos.x < 0.0 || pos.x > 1.0 || pos.y < 0.0 || pos.y > 1.0)
        return float3(0.0, 0.0, 0.0);
    return ToLinear(tex(pos));
}

float2 Dist(float2 pos)
{
    pos = pos * source_size.xy;
    return -((pos - floor(pos)) - 0.5);
}

float Gaus(float pos, float scale) { return exp2(scale * pow(abs(pos), shape)); }

float3 Horz3(float2 pos, float off)
{
    float3 b = Fetch(pos, float2(-1.0, off)), c = Fetch(pos, float2(0.0, off)), d = Fetch(pos, float2(1.0, off));
    float dst = Dist(pos).x;
    float wb = Gaus(dst - 1.0, hardPix), wc = Gaus(dst, hardPix), wd = Gaus(dst + 1.0, hardPix);
    return (b * wb + c * wc + d * wd) / (wb + wc + wd);
}

float3 Horz5(float2 pos, float off)
{
    float3 a = Fetch(pos, float2(-2.0, off)), b = Fetch(pos, float2(-1.0, off)), c = Fetch(pos, float2(0.0, off));
    float3 d = Fetch(pos, float2(1.0, off)), e = Fetch(pos, float2(2.0, off));
    float dst = Dist(pos).x;
    float wa = Gaus(dst - 2.0, hardPix), wb = Gaus(dst - 1.0, hardPix), wc = Gaus(dst, hardPix);
    float wd = Gaus(dst + 1.0, hardPix), we = Gaus(dst + 2.0, hardPix);
    return (a * wa + b * wb + c * wc + d * wd + e * we) / (wa + wb + wc + wd + we);
}

float3 Horz7(float2 pos, float off)
{
    float3 a = Fetch(pos, float2(-3.0, off)), b = Fetch(pos, float2(-2.0, off)), c = Fetch(pos, float2(-1.0, off));
    float3 d = Fetch(pos, float2(0.0, off)), e = Fetch(pos, float2(1.0, off)), f = Fetch(pos, float2(2.0, off));
    float3 g = Fetch(pos, float2(3.0, off));
    float dst = Dist(pos).x;
    float wa = Gaus(dst - 3.0, hardBloomPix), wb = Gaus(dst - 2.0, hardBloomPix), wc = Gaus(dst - 1.0, hardBloomPix);
    float wd = Gaus(dst, hardBloomPix), we = Gaus(dst + 1.0, hardBloomPix), wf = Gaus(dst + 2.0, hardBloomPix);
    float wg = Gaus(dst + 3.0, hardBloomPix);
    return (a * wa + b * wb + c * wc + d * wd + e * we + f * wf + g * wg) / (wa + wb + wc + wd + we + wf + wg);
}

float Scan(float2 pos, float off) { return Gaus(Dist(pos).y + off, hardScan); }
float BloomScan(float2 pos, float off) { return Gaus(Dist(pos).y + off, hardBloomScan); }

float3 Tri(float2 pos)
{
    return Horz3(pos, -1.0) * Scan(pos, -1.0) + Horz5(pos, 0.0) * Scan(pos, 0.0) + Horz3(pos, 1.0) * Scan(pos, 1.0);
}

float3 Bloom(float2 pos)
{
    return Horz5(pos, -2.0) * BloomScan(pos, -2.0) + Horz7(pos, -1.0) * BloomScan(pos, -1.0) +
           Horz7(pos, 0.0) * BloomScan(pos, 0.0) + Horz7(pos, 1.0) * BloomScan(pos, 1.0) +
           Horz5(pos, 2.0) * BloomScan(pos, 2.0);
}

float2 Warp(float2 pos)
{
    pos = pos * 2.0 - 1.0;
    pos *= float2(1.0 + (pos.y * pos.y) * 0.031 * params.x, 1.0 + (pos.x * pos.x) * 0.041 * params.x);
    return pos * 0.5 + 0.5;
}

/* stretched VGA style shadow mask (crt-lottes' shadowMask 3) */
float3 Mask(float2 pos)
{
    float3 mask = float3(maskDark, maskDark, maskDark);
    pos.x += pos.y * 3.0;
    pos.x = frac(pos.x * 0.166666666);
    if (pos.x < 0.333)
        mask.r = maskLight;
    else if (pos.x < 0.666)
        mask.g = maskLight;
    else
        mask.b = maskLight;
    return lerp(float3(1.0, 1.0, 1.0), mask, params.y);
}

PSOutput main(PSInput input)
{
    PSOutput output;
    float2 pos = Warp(input.v_uv);
    float3 c = Tri(pos) + Bloom(pos) * bloomAmount;
    c *= Mask(floor(input.v_uv * output_size.xy) + 0.5);
    output.o_color = float4(ToSrgb(c), 1.0);
    return output;
}
