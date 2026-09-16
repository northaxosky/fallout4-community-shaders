// SPDX-License-Identifier: GPL-3.0-or-later
// AE 1.11.221, shaders011.fxp ordinals 0-1.

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
