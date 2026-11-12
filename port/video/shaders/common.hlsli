/* common.hlsli -- the resource layout SDL's GPU renderer gives a custom fragment shader:
 * the texture and its sampler in space 2 (one combined image sampler on Vulkan), the uniform
 * buffer in space 3, and SDL's vertex outputs (colour, texture coordinate). SDL's own shaders are
 * built with SDL_shadercross, which names every vertex output TEXCOORD<location>: Direct3D 12
 * matches them by name, so ours must use the same names (COLOR0 gives a black screen there). */
#ifdef __spirv__
#define VK(x) [[vk::x]]
#else
#define VK(x)
#endif

VK(combinedImageSampler) VK(binding(0, 2))
Texture2D u_texture : register(t0, space2);
VK(combinedImageSampler) VK(binding(0, 2))
SamplerState u_sampler : register(s0, space2);

VK(binding(0, 3))
cbuffer Context : register(b0, space3) {
    float4 source_size;     /* game picture: width, height, 1 / width, 1 / height */
    float4 output_size;     /* on the screen: width, height, 1 / width, 1 / height */
    float4 params;          /* per shader */
};

struct PSInput {
    VK(location(0)) float4 v_color : TEXCOORD0;
    VK(location(1)) float2 v_uv : TEXCOORD1;
};

struct PSOutput {
    float4 o_color : SV_Target;
};

float3 tex(float2 uv)
{
    return u_texture.Sample(u_sampler, uv).rgb;
}
