/* fxaa.frag.hlsl -- "Anti-aliasing: FXAA": smooths the stair steps along edges in the sharp 3D
 * views (platform.c), after the algorithm of Timothy Lottes' FXAA 3.11 (quality version: find
 * the edge through the pixel, follow it both ways to its ends, and blend the pixel with its
 * neighbour across the edge by how far it is from the nearer end; small details by the local
 * contrast).  Written for Terra Nova: Freefall.
 *
 * The picture is drawn 1:1, so every sample is a whole pixel (point sampling); the half-pixel
 * samples along the edge are the mean of the two pixels.  The display marks the 3D's pixels with
 * alpha 254/255 (the HUD's, the maps' and the cockpit's art have 1 or 0): only those are changed,
 * and a neighbour that is not the 3D's counts as the pixel itself, so the HUD neither changes nor
 * bleeds into the 3D.
 *
 * params: x = edge threshold, y = minimum edge contrast, z = subpixel amount */
#include "common.hlsli"

static const float steps[10] = { 1.0, 1.0, 1.0, 1.0, 2.0, 2.0, 2.0, 4.0, 4.0, 8.0 };

float4 at(float2 uv)
{
    return u_texture.Sample(u_sampler, uv);
}

bool is3d(float4 c)
{
    return abs(c.a - 254.0 / 255.0) < 0.5 / 255.0;
}

float luma(float3 c)
{
    return dot(c, float3(0.299, 0.587, 0.114));
}

/* a neighbour's colour: its own if it is the 3D's, else the middle pixel's */
float3 nb(float2 uv, float3 m)
{
    float4 c = at(uv);
    return is3d(c) ? c.rgb : m;
}

PSOutput main(PSInput input)
{
    PSOutput output;
    float2 uv = input.v_uv, px = source_size.zw;
    float4 mc = at(uv);
    float3 m = mc.rgb;
    output.o_color = float4(m, 1.0);
    if (!is3d(mc))
        return output;

    float lM = luma(m);
    float lN = luma(nb(uv + float2(0, -px.y), m));
    float lS = luma(nb(uv + float2(0, px.y), m));
    float lW = luma(nb(uv + float2(-px.x, 0), m));
    float lE = luma(nb(uv + float2(px.x, 0), m));
    float lMax = max(max(max(lN, lS), max(lW, lE)), lM);
    float lMin = min(min(min(lN, lS), min(lW, lE)), lM);
    float range = lMax - lMin;
    if (range < max(params.y, lMax * params.x))
        return output;                  /* no edge here */

    float lNW = luma(nb(uv + float2(-px.x, -px.y), m));
    float lNE = luma(nb(uv + float2(px.x, -px.y), m));
    float lSW = luma(nb(uv + float2(-px.x, px.y), m));
    float lSE = luma(nb(uv + float2(px.x, px.y), m));

    /* the edge's direction: a horizontal edge changes most from row to row */
    float eH = abs(lNW + lSW - 2.0 * lW) + 2.0 * abs(lN + lS - 2.0 * lM) + abs(lNE + lSE - 2.0 * lE);
    float eV = abs(lNW + lNE - 2.0 * lN) + 2.0 * abs(lW + lE - 2.0 * lM) + abs(lSW + lSE - 2.0 * lS);
    bool horz = eH >= eV;

    /* small details: how far the pixel stands out from the mean around it */
    float sub = saturate(abs((2.0 * (lN + lS + lW + lE) + lNW + lNE + lSW + lSE) / 12.0 - lM) / range);
    sub = (3.0 - 2.0 * sub) * sub * sub;
    sub = sub * sub * params.z;

    /* which side of the pixel the edge is on: the steeper of the two neighbours across */
    float l1 = horz ? lN : lW, l2 = horz ? lS : lE;
    float g1 = abs(l1 - lM), g2 = abs(l2 - lM);
    float2 across = horz ? float2(0, px.y) : float2(px.x, 0);
    float2 along = horz ? float2(px.x, 0) : float2(0, px.y);
    float lPair = l2;
    if (g1 >= g2) {
        across = -across;
        lPair = l1;
    }
    float grad = max(g1, g2) * 0.25;
    float avg = 0.5 * (lM + lPair);

    /* along the edge both ways, on the line between the pixel and its neighbour across, until
     * the luma there leaves the edge's (or a pixel is not the 3D's) */
    float dN = 0.0, dP = 0.0, endN = 0.0, endP = 0.0;
    bool doneN = false, doneP = false;
    [unroll] for (int i = 0; i < 10; i++) {
        if (!doneN) {
            dN += steps[i];
            float4 a = at(uv - along * dN), b = at(uv - along * dN + across);
            endN = 0.5 * (luma(a.rgb) + luma(b.rgb)) - avg;
            doneN = abs(endN) >= grad || !is3d(a) || !is3d(b);
        }
        if (!doneP) {
            dP += steps[i];
            float4 a = at(uv + along * dP), b = at(uv + along * dP + across);
            endP = 0.5 * (luma(a.rgb) + luma(b.rgb)) - avg;
            doneP = abs(endP) >= grad || !is3d(a) || !is3d(b);
        }
    }

    /* the blend: more the nearer the pixel is to the end of the edge it is on */
    bool mBelow = lM - avg < 0.0;
    float d = min(dN, dP);
    bool good = (dN < dP ? endN : endP) < 0.0 != mBelow;
    float off = good ? 0.5 - d / (dN + dP) : 0.0;
    off = max(off, sub);
    output.o_color = float4(lerp(m, nb(uv + across, m), off), 1.0);
    return output;
}
