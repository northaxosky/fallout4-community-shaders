// AE 1.11.221 no-t0 accumulator shapes reconstructed from native declarations.
// Select one instruction-derived shape through WAVE5A_ACCUMULATOR_SHAPE.

#if !defined(WAVE5A_ACCUMULATOR_SHAPE)
#error WAVE5A_ACCUMULATOR_SHAPE is required
#endif

#if WAVE5A_ACCUMULATOR_SHAPE == 1
// Native SHA-1 df89353bebcfa292277146c1dad752ba3309426e.
cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2[6];
};

SamplerState g_sAmbientPrimary : register(s6);
SamplerState g_sAmbientSecondary : register(s12);
SamplerState g_sLighting : register(s4);
SamplerState g_sColorPrimary : register(s5);
SamplerState g_sColorSecondary : register(s11);
SamplerState g_sOcclusion : register(s9);

Texture2D<float4> g_tAmbientPrimary : register(t6);
Texture2D<float4> g_tAmbientSecondary : register(t12);
Texture2D<float4> g_tLighting : register(t4);
Texture2D<float4> g_tColorPrimary : register(t5);
Texture2D<float4> g_tColorSecondary : register(t11);
Texture2D<float4> g_tOcclusion : register(t9);

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

float4 main(PS_INPUT input) : SV_Target0
{
    float2 screenUv = input.position.xy * cb2[0].xy;
    float3 ambient = g_tAmbientPrimary.SampleLevel(g_sAmbientPrimary, screenUv, 0).xyz;
    ambient += g_tAmbientSecondary.SampleLevel(g_sAmbientSecondary, screenUv, 0).xyz;
    ambient += g_tLighting.Sample(g_sLighting, screenUv).xyz;
    float3 color = g_tColorPrimary.SampleLevel(g_sColorPrimary, screenUv, 0).xyz;
    color += g_tColorSecondary.SampleLevel(g_sColorSecondary, screenUv, 0).xyz;
    float2 occlusionUv = min(screenUv, cb2[5].xy);
    float occlusion = g_tOcclusion.Sample(g_sOcclusion, occlusionUv).x;
    float3 result = color * 1.5 + ambient;
    return float4(result * occlusion, 0.5);
}

#elif WAVE5A_ACCUMULATOR_SHAPE == 2
// Native SHA-1 1f3e25fe78b759d1278ca3608c0f0d30eb46434d.
cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2[1];
};

SamplerState g_sShading : register(s3);
SamplerState g_sLighting : register(s4);
SamplerState g_sColor : register(s5);
SamplerState g_sAmbient : register(s6);

Texture2D<float4> g_tShading : register(t3);
Texture2D<float4> g_tLighting : register(t4);
Texture2D<float4> g_tColor : register(t5);
Texture2D<float4> g_tAmbient : register(t6);

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

float4 main(PS_INPUT input) : SV_Target0
{
    float2 screenUv = input.position.xy * cb2[0].xy;
    float material = g_tShading.SampleLevel(g_sShading, screenUv, 0).w;
    float3 color = g_tColor.SampleLevel(g_sColor, screenUv, 0).xyz;
    float3 result = color * 1.5;
    if (abs(material * 255.0 - 5.0) >= 0.25)
    {
        float3 lighting = g_tLighting.Sample(g_sLighting, screenUv).xyz;
        float3 tail = g_tAmbient.SampleLevel(g_sAmbient, screenUv, 0).xyz;
        float3 ambient = tail + lighting;
        result = color * 1.5 + ambient;
    }
    return float4(result, 0.5);
}

#elif WAVE5A_ACCUMULATOR_SHAPE == 3
// Native SHA-1 b77db624fe08bc9167e30f3888f99d212fab2882.
cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2[1];
};

SamplerState g_sShading : register(s3);
SamplerState g_sLighting : register(s4);
SamplerState g_sColorPrimary : register(s5);
SamplerState g_sAmbientPrimary : register(s6);
SamplerState g_sColorSecondary : register(s11);
SamplerState g_sAmbientSecondary : register(s12);

Texture2D<float4> g_tShading : register(t3);
Texture2D<float4> g_tLighting : register(t4);
Texture2D<float4> g_tColorPrimary : register(t5);
Texture2D<float4> g_tAmbientPrimary : register(t6);
Texture2D<float4> g_tColorSecondary : register(t11);
Texture2D<float4> g_tAmbientSecondary : register(t12);

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

float4 main(PS_INPUT input) : SV_Target0
{
    float2 screenUv = input.position.xy * cb2[0].xy;
    float material = g_tShading.SampleLevel(g_sShading, screenUv, 0).w;
    float3 color = g_tColorPrimary.SampleLevel(g_sColorPrimary, screenUv, 0).xyz;
    color += g_tColorSecondary.SampleLevel(g_sColorSecondary, screenUv, 0).xyz;
    float3 result = color * 1.5;
    if (abs(material * 255.0 - 5.0) >= 0.25)
    {
        float3 lighting = g_tLighting.Sample(g_sLighting, screenUv).xyz;
        float3 ambient = g_tAmbientPrimary.SampleLevel(g_sAmbientPrimary, screenUv, 0).xyz;
        ambient += g_tAmbientSecondary.SampleLevel(g_sAmbientSecondary, screenUv, 0).xyz;
        ambient += lighting;
        result = color * 1.5 + ambient;
    }
    return float4(result, 0.5);
}

#elif WAVE5A_ACCUMULATOR_SHAPE == 4
// Native SHA-1 1a6e42e6f2cbd101536af7dd12df80e01492eadd.
cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2[6];
};

SamplerState g_sLighting : register(s4);
SamplerState g_sColor : register(s5);
SamplerState g_sAmbient : register(s6);
SamplerState g_sOcclusion : register(s9);

Texture2D<float4> g_tLighting : register(t4);
Texture2D<float4> g_tColor : register(t5);
Texture2D<float4> g_tAmbient : register(t6);
Texture2D<float4> g_tOcclusion : register(t9);

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

float4 main(PS_INPUT input) : SV_Target0
{
    float2 screenUv = input.position.xy * cb2[0].xy;
    float3 ambient = g_tLighting.Sample(g_sLighting, screenUv).xyz;
    ambient += g_tAmbient.SampleLevel(g_sAmbient, screenUv, 0).xyz;
    float3 color = g_tColor.SampleLevel(g_sColor, screenUv, 0).xyz;
    float2 occlusionUv = min(screenUv, cb2[5].xy);
    float occlusion = g_tOcclusion.Sample(g_sOcclusion, occlusionUv).x;
    float3 result = (color * 1.5 + ambient) * occlusion;
    return float4(result, 0.5);
}

#else
#error Unsupported no-t0 accumulator shape
#endif
