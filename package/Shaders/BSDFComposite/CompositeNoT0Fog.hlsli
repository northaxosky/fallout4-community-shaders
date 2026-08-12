// AE 1.11.221 no-t0 fog shapes reconstructed from native declarations.
// Select one instruction-derived shape through WAVE5A_FOG_SHAPE.

#if !defined(WAVE5A_FOG_SHAPE)
#error WAVE5A_FOG_SHAPE is required
#endif

#if WAVE5A_FOG_SHAPE == 1
#define WAVE5A_FOG_CB2_COUNT 3
#define WAVE5A_FOG_TILED 0
#define WAVE5A_FOG_MODULATION 0
#define WAVE5A_FOG_MATERIAL5 0
#elif WAVE5A_FOG_SHAPE == 2
#define WAVE5A_FOG_CB2_COUNT 3
#define WAVE5A_FOG_TILED 0
#define WAVE5A_FOG_MODULATION 0
#define WAVE5A_FOG_MATERIAL5 1
#elif WAVE5A_FOG_SHAPE == 3
#define WAVE5A_FOG_CB2_COUNT 6
#define WAVE5A_FOG_TILED 0
#define WAVE5A_FOG_MODULATION 1
#define WAVE5A_FOG_MATERIAL5 0
#elif WAVE5A_FOG_SHAPE == 4
#define WAVE5A_FOG_CB2_COUNT 3
#define WAVE5A_FOG_TILED 1
#define WAVE5A_FOG_MODULATION 0
#define WAVE5A_FOG_MATERIAL5 0
#elif WAVE5A_FOG_SHAPE == 5
#define WAVE5A_FOG_CB2_COUNT 3
#define WAVE5A_FOG_TILED 1
#define WAVE5A_FOG_MODULATION 0
#define WAVE5A_FOG_MATERIAL5 1
#elif WAVE5A_FOG_SHAPE == 6
#define WAVE5A_FOG_CB2_COUNT 6
#define WAVE5A_FOG_TILED 1
#define WAVE5A_FOG_MODULATION 1
#define WAVE5A_FOG_MATERIAL5 0
#else
#error Unsupported no-t0 fog shape
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    float4 scene[47];
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 screenData[WAVE5A_FOG_CB2_COUNT];
};

Texture2D<float4> typeTexture : register(t3);
Texture2D<float4> secondaryTexture : register(t4);
Texture2D<float4> directTexture : register(t5);
Texture2D<float4> ambientTexture : register(t6);
Texture2D<float4> depthTexture : register(t7);
#if WAVE5A_FOG_MODULATION
Texture2D<float4> modulationTexture : register(t9);
#endif
#if WAVE5A_FOG_TILED
Texture2D<float4> directSecondaryTexture : register(t11);
Texture2D<float4> ambientSecondaryTexture : register(t12);
#endif

SamplerState typeSampler : register(s3);
SamplerState secondarySampler : register(s4);
SamplerState directSampler : register(s5);
SamplerState ambientSampler : register(s6);
SamplerState depthSampler : register(s7);
#if WAVE5A_FOG_MODULATION
SamplerState modulationSampler : register(s9);
#endif
#if WAVE5A_FOG_TILED
SamplerState directSecondarySampler : register(s11);
SamplerState ambientSecondarySampler : register(s12);
#endif

float4 main(float4 position : SV_POSITION) : SV_Target0
{
    float2 uv = position.xy * screenData[0].xy;
    float material = typeTexture.SampleLevel(typeSampler, uv, 0.0).w;

#if WAVE5A_FOG_MATERIAL5
    float3 direct = directTexture.SampleLevel(directSampler, uv, 0.0).xyz;
#if WAVE5A_FOG_TILED
    direct += directSecondaryTexture.SampleLevel(directSecondarySampler, uv, 0.0).xyz;
#endif
#endif

    float depth = depthTexture.SampleLevel(depthSampler, uv, 0.0).x;
    float projectedDepth;
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;
    if (depth <= 0.01)
    {
        projectedDepth = depth * 100.0;
        row0 = scene[24];
        row1 = scene[25];
        row2 = scene[26];
        row3 = scene[27];
    }
    else
    {
        projectedDepth = depth * 1.01 - 0.01;
        row0 = scene[20];
        row1 = scene[21];
        row2 = scene[22];
        row3 = scene[23];
    }

#if WAVE5A_FOG_MATERIAL5
    float3 composite = direct * 1.5;
    if (abs(material * 255.0 - 5.0) >= 0.25)
    {
        float3 secondary = secondaryTexture.Sample(secondarySampler, uv).xyz;
        float3 ambient = ambientTexture.SampleLevel(ambientSampler, uv, 0.0).xyz;
#if WAVE5A_FOG_TILED
        ambient += ambientSecondaryTexture.SampleLevel(ambientSecondarySampler, uv, 0.0).xyz;
#endif
        float3 secondaryAmbient = secondary + ambient;
        composite = direct * 1.5 + secondaryAmbient;
    }
#endif

    bool isMaterial2 = abs(material * 255.0 - 2.0) < 0.25;
    bool isMaterial3 = abs(material * 255.0 - 3.0) < 0.25;
    float4 result;
    if (!(isMaterial2 || isMaterial3))
    {

#if !WAVE5A_FOG_MATERIAL5
        float3 direct = directTexture.SampleLevel(directSampler, uv, 0.0).xyz;
#if WAVE5A_FOG_TILED
        direct += directSecondaryTexture.SampleLevel(directSecondarySampler, uv, 0.0).xyz;
#endif
        float3 secondary = secondaryTexture.Sample(secondarySampler, uv).xyz;
#endif

        float2 projectedUv = float2(
            uv.x * screenData[0].z,
            1.0 - uv.y * screenData[0].w);
        float4 projected = float4(projectedUv * 2.0 - 1.0, projectedDepth, 1.0);
        float4 reconstructed = float4(
            dot(row0, projected),
            dot(row1, projected),
            dot(row2, projected),
            dot(row3, projected));
        reconstructed.xyz /= reconstructed.w;

#if !WAVE5A_FOG_MATERIAL5
        float3 ambient = ambientTexture.SampleLevel(ambientSampler, uv, 0.0).xyz;
#if WAVE5A_FOG_TILED
        ambient += ambientSecondaryTexture.SampleLevel(ambientSecondarySampler, uv, 0.0).xyz;
#endif
        ambient = mad(secondary, 1.0, ambient);
        float3 composite = direct * 1.5 + ambient;
#endif

#if WAVE5A_FOG_MODULATION
        composite *= modulationTexture.Sample(
            modulationSampler, min(uv, screenData[5].xy)).x;
#endif

        float fogPlane = dot(scene[14], float4(reconstructed.xyz, 1.0)) + scene[35].z;
        float distanceSquared = dot(reconstructed.xyz, reconstructed.xyz);
        float distanceRampRaw = sqrt(distanceSquared) * scene[41].x - scene[41].z;
        float distanceRamp = saturate(distanceRampRaw);
        float2 heightRemaps = saturate(fogPlane * scene[46].xy - scene[46].zw);
        float heightFactor = lerp(heightRemaps.x, heightRemaps.y, distanceRamp);

        float fogLimit = scene[43].w;
        if (distanceRampRaw > 0.75)
        {
            fogLimit = min(
                scene[43].w + (distanceRamp - 0.75) * 4.0 * (1.0 - scene[43].w),
                1.0);
        }

        float nearEscape = distanceRampRaw < 0.015 ? distanceRamp * 66.666672 : 1.0;
        float fogCurve = min(pow(distanceRamp, scene[42].w), fogLimit);
        float heightScale = 1.0 - heightFactor + heightFactor * scene[44].w;
        float3 lowFog = lerp(scene[42].xyz, scene[44].xyz, fogCurve);
        float3 highFog = lerp(scene[43].xyz, scene[45].xyz, fogCurve);
        float3 fogColor = lerp(lowFog, highFog, heightFactor);
        float fogMix = fogCurve * heightScale * nearEscape;

        float3 viewDirection = normalize(reconstructed.xyz);
        float sun = pow(max(dot(viewDirection, screenData[1].xyz), 0.0), screenData[2].w)
            * screenData[1].w;
        float3 sunlitFog = lerp(fogColor, screenData[2].xyz, sun);
        bool useGrayFog = fogMix < scene[43].w;
        float gray = dot(composite, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
        float3 graySaturated = sunlitFog + gray * (gray - sunlitFog);
        float3 selectedFog = useGrayFog ? graySaturated : sunlitFog;
        float3 outputColor = lerp(composite, selectedFog, fogMix);
        result = float4(outputColor, 0.5);
    }
    else
    {
        result = float4(0.0, 0.0, 0.0, 0.0);
    }
    return result;
}
