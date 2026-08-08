// AE 1.11.221 Composite all-2D accumulator family reconstruction.

cbuffer ScreenData : register(b2)
{
    float4 screenData[COMPOSITE_CB2_COUNT];
};

Texture2D<float4> baseTexture : register(t0);
#if COMPOSITE_MATERIAL_5
Texture2D<float4> typeTexture : register(t3);
#endif
Texture2D<float4> secondaryTexture : register(t4);
Texture2D<float4> directTexture : register(t5);
Texture2D<float4> ambientTexture : register(t6);
#if COMPOSITE_MODULATION
Texture2D<float4> modulationTexture : register(t9);
#endif
#if TILED_LIGHTS
Texture2D<float4> tileDirectTexture : register(t11);
Texture2D<float4> tileAmbientTexture : register(t12);
#endif

SamplerState baseSampler : register(s0);
#if COMPOSITE_MATERIAL_5
SamplerState typeSampler : register(s3);
#endif
SamplerState secondarySampler : register(s4);
SamplerState directSampler : register(s5);
SamplerState ambientSampler : register(s6);
#if COMPOSITE_MODULATION
SamplerState modulationSampler : register(s9);
#endif
#if TILED_LIGHTS
SamplerState tileDirectSampler : register(s11);
SamplerState tileAmbientSampler : register(s12);
#endif

float4 main(float4 position : SV_POSITION) : SV_Target0
{
    float2 uv = position.xy * screenData[0].xy;
    float4 base = baseTexture.SampleLevel(baseSampler, uv, 0.0);
    float3 direct = directTexture.SampleLevel(directSampler, uv, 0.0).xyz;
#if TILED_LIGHTS
    direct += tileDirectTexture.SampleLevel(tileDirectSampler, uv, 0.0).xyz;
#endif
    float3 color = base.xyz * direct * 3.0;

#if COMPOSITE_MATERIAL_5
    float material = typeTexture.SampleLevel(typeSampler, uv, 0.0).w;
    if (abs(material * 255.0 - 5.0) >= 0.25)
#endif
    {
        float3 ambient =
            secondaryTexture.Sample(secondarySampler, uv).xyz +
            ambientTexture.SampleLevel(ambientSampler, uv, 0.0).xyz;
#if TILED_LIGHTS
        ambient +=
            tileAmbientTexture.SampleLevel(tileAmbientSampler, uv, 0.0).xyz;
#endif
        color += ambient;
    }

#if COMPOSITE_MODULATION
    float2 modulationUv = min(uv, screenData[5].xy);
    color *= modulationTexture.Sample(modulationSampler, modulationUv).x;
#endif
    return float4(color, base.w);
}
