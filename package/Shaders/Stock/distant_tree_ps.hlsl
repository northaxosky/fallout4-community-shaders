// SPDX-License-Identifier: GPL-3.0-or-later
// AE 1.11.221, shaders011.fxp ordinals 6-7.

struct PS_INPUT
{
    float4 HPosition : SV_POSITION0;
    float3 TexCoord : TEXCOORD0;
    float4 FogParam : TEXCOORD1;
#ifdef RENDER_DEPTH
    float2 Depth : TEXCOORD3;
#endif
};

struct PS_OUTPUT
{
    float4 Color : SV_Target0;
};

#ifndef RENDER_DEPTH
SamplerState SampDiffuse : register(s0);
Texture2D<float4> TexDiffuse : register(t0);

cbuffer PerTechnique : register(b0)
{
    float4 Reserved[4];
    float3 DiffuseColor;
    float4 AmbientColor;
}
#endif

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT psout;

#ifdef RENDER_DEPTH
    float depth = input.Depth.x / input.Depth.y;
    psout.Color = float4(depth, depth, depth, 1);
#else
    float4 baseColor = TexDiffuse.Sample(SampDiffuse, input.TexCoord.xy);
    float3 diffuseColor =
        baseColor.xyz * (input.TexCoord.z * DiffuseColor + AmbientColor.xyz);
    float3 color =
        lerp(diffuseColor, input.FogParam.xyz, input.FogParam.w);
    psout.Color.xyz = color * AmbientColor.w;
    psout.Color.w = 1;
#endif

    return psout;
}
