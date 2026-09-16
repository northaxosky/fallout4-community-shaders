#ifdef PARTICLE_PS_SOURCE
// SPDX-License-Identifier: GPL-3.0-or-later
// AE 1.11.221, shaders011.fxp ordinals 14-19.

Texture2D<float4> SourceTexture : register(t0);
SamplerState SourceSampler : register(s0);
Texture2D<float4> GrayscaleTexture : register(t1);
SamplerState GrayscaleSampler : register(s1);

cbuffer PerGeometry : register(b2)
{
    float ColorScale;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord0 : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 base = SourceTexture.Sample(SourceSampler, input.TexCoord0);
    float4 color = base * input.Color;

#ifdef GRAYSCALE_TO_COLOR
    color.rgb =
        GrayscaleTexture.Sample(
            GrayscaleSampler,
            float2(base.g, input.Color.r)).rgb;
#endif

#ifdef GRAYSCALE_TO_ALPHA
    color.a =
        GrayscaleTexture.Sample(
            GrayscaleSampler,
            float2(base.a, input.Color.a)).a;
#endif

    color.rgb *= ColorScale;
    return color;
}
#endif

#ifdef PARTICLE_VS_SOURCE
// SPDX-License-Identifier: GPL-3.0-or-later
// AE 1.11.221, shaders011.fxp ordinals 8-13.

cbuffer PerFrame : register(b0)
{
    float2 ParticleScale;
};

cbuffer PerGeometry : register(b2)
{
    row_major float4x4 WorldViewProj;
    float4 Padding[4];
    float4 fVars0;
    float4 fVars1;
    float4 fVars2;
    float4 fVars3;
    float4 fVars4;
    float4 Color1;
    float4 Color2;
    float4 Color3;
    float4 Velocity;
    float4 Acceleration;
    float4 ScaleAdjust;
    float4 Wind;
};

struct VS_INPUT
{
    float4 Position : POSITION;
#ifdef ENVCUBE
    float2 TexCoord0 : TEXCOORD0;
    float4 Offset : TEXCOORD1;
#else
    float4 Normal : NORMAL;
    float3 Spin : TEXCOORD0;
    int4 Corner : TEXCOORD1;
#endif
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord0 : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT vsout;

#ifdef ENVCUBE
    float3 boxOrigin = fVars1.xyz - fVars2.x * 0.5;
    float3 world =
        fmod(input.Position.xyz + fVars0.xyz, fVars2.x) + boxOrigin;

#ifdef RAIN
    float4 head = mul(WorldViewProj, float4(world, 1.0));
    float4 tail =
        mul(WorldViewProj, float4(world, 1.0) - float4(Velocity.xyz, 0.0));
    float4 pos = lerp(tail, head, (input.Offset.y + 1.0) * 0.5);
    pos.xy += input.Offset.xy;
#else
    float spinSin;
    float spinCos;
    sincos(fVars0.w, spinSin, spinCos);
    float2x2 spin = float2x2(spinCos, -spinSin, spinSin, spinCos);
    float2 offset =
        mul(spin, input.Offset.xy) * ParticleScale +
        mul(spin, input.Offset.zw);

    float4 pos = mul(WorldViewProj, float4(world, 1.0));
    pos.xy += offset;
#endif

    vsout.Position = pos;
    vsout.Color = float4(1.0, 1.0, 1.0, fVars1.w);
    vsout.TexCoord0 = input.TexCoord0;
#else
    float age = input.Position.w * input.Normal.w;
    float life = age / fVars0.y;

    float4 sizeKey = float4(0.0, fVars2.x, 0.0, fVars2.z);
    if (life > fVars2.z)
    {
        sizeKey = fVars2;
    }
    if (life > fVars2.w)
    {
        sizeKey = float4(fVars2.y, 0.0, fVars2.w, 1.0);
    }
    float size =
        lerp(
            sizeKey.x,
            sizeKey.y,
            (life - sizeKey.z) / (sizeKey.w - sizeKey.z));

    float4 corner = input.Corner;
    float2 offset = (corner.zw * 2.0 - 1.0) * size;

    float spinSin;
    float spinCos;
    sincos(
        input.Spin.x + input.Spin.y * input.Position.w,
        spinSin,
        spinCos);
    float2 rotated =
        mul(float2x2(spinCos, -spinSin, spinSin, spinCos), offset);

    float3 velocity =
        normalize(input.Position.xyz - ScaleAdjust.xyz) * ScaleAdjust.w +
        Velocity.xyz;
    velocity += input.Normal.xyz * fVars0.z * input.Spin.z;
    float3 accel = input.Normal.xyz * fVars0.w + Acceleration.xyz;
    float3 world =
        input.Position.xyz + velocity * age + accel * (age * age) * 0.5;
    world -= fVars3.xyz;

    float4 pos = mul(WorldViewProj, float4(world, 1.0));
    pos.xy += rotated * ParticleScale;
    vsout.Position = pos;

    float4 colorFrom = float4(Color1.rgb, 0.0);
    float4 colorTo = Color1;
    float2 span = float2(0.0, fVars1.x);
    if (life > fVars1.x)
    {
        colorFrom = Color1;
        colorTo = Color2;
        span = fVars1.xy;
    }
    if (life > fVars1.y)
    {
        colorFrom = Color2;
        colorTo = Color3;
        span = fVars1.yz;
    }
    if (life > fVars1.z)
    {
        colorFrom = Color3;
        colorTo = float4(Color3.rgb, 0.0);
        span = float2(fVars1.z, 1.0);
    }
    float4 color =
        lerp(colorFrom, colorTo, (life - span.x) / (span.y - span.x));
    vsout.Color = float4(color.rgb, color.w * fVars3.w);
    vsout.TexCoord0 = corner.xy * fVars4.xy;
#endif

    return vsout;
}
#endif
