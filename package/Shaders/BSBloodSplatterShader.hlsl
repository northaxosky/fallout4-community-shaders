#if defined(BSBLOODSPLATTER_PS_SOURCE)
// SPDX-License-Identifier: GPL-3.0-or-later
// AE 1.11.240, shaders011.fxp ordinals 2-3.

#ifdef SPLATTER
Texture2D<float4> BloodTex : register(t0);
SamplerState BloodSampler : register(s0);
Texture2D<float4> BloodOffsetTex : register(t1);
SamplerState BloodOffsetSampler : register(s1);
#endif

#ifdef FLARE
Texture2D<float4> FlareTex : register(t2);
SamplerState FlareSampler : register(s2);
Texture2D<float4> BloodTargetTex : register(t3);
SamplerState BloodTargetSampler : register(s3);
#endif

cbuffer PerGeometry : register(b2)
{
    float Alpha;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 TexCoord0 : TEXCOORD0;
#ifdef SPLATTER
    float2 TexCoord1 : TEXCOORD1;
#endif
};

float4 main(PS_INPUT input) : SV_Target
{
#ifdef SPLATTER
    float3 shifted =
        BloodOffsetTex.Sample(BloodOffsetSampler, input.TexCoord1).xyz;
    float4 blood = BloodTex.Sample(BloodSampler, input.TexCoord0.xy);
    float alpha = blood.w * (input.TexCoord0.z * Alpha);
    float3 tinted = lerp(1.0.xxx, blood.xyz, alpha);
    return float4(lerp(tinted, shifted, alpha), 1.0);
#endif

#ifdef FLARE
    float flare = FlareTex.Sample(FlareSampler, input.TexCoord0.xy).x;
    float4 blood =
        BloodTargetTex.Sample(BloodTargetSampler, input.TexCoord0.xy);
    return flare * blood * Alpha;
#endif
}
#elif defined(BSBLOODSPLATTER_VS_SOURCE)
// SPDX-License-Identifier: GPL-3.0-or-later
// AE 1.11.240, shaders011.fxp ordinals 0-1.

cbuffer PerGeometry : register(b2)
{
    row_major float4x4 WorldViewProj;
    float4 ScreenSpaceLightLoc;
#ifdef FLARE
    float4 FlareStretch;
#endif
};

struct VS_INPUT
{
    float4 Position : POSITION0;
    float2 TexCoord0 : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 TexCoord0 : TEXCOORD0;
#ifdef SPLATTER
    float2 TexCoord1 : TEXCOORD1;
#endif
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT vsout;
    float4 pos = mul(WorldViewProj, float4(input.Position.xy, 0.0, 1.0));
    float2 offset = (pos.xy - ScreenSpaceLightLoc.xy) * ScreenSpaceLightLoc.w;

#ifdef SPLATTER
    vsout.Position = pos;
#endif
#ifdef FLARE
    vsout.Position.xy = offset * FlareStretch.x + pos.xy;
    vsout.Position.zw = pos.zw;
#endif

    vsout.TexCoord0 = float3(input.TexCoord0, input.Position.z);

#ifdef SPLATTER
    vsout.TexCoord1 = input.TexCoord0 - offset * float2(-0.5, 0.5);
#endif

    return vsout;
}
#else
#error "Select one shader family stage or kernel."
#endif
