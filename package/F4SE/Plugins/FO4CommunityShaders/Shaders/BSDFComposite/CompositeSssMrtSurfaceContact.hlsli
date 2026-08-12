// AE 1.11.221 SSS MRT surface-normal/contact family: decoded t1.xy G-buffer normal substituted for the record shading normal on the no-march shapes.
// Select one instruction-derived shape through WAVE5B_SSS_SURFACE_CONTACT_SHAPE.

#if defined(WAVE5B_SSS_RECORD_NORMAL_SHAPE)
#error WAVE5B_SSS_RECORD_NORMAL_SHAPE selects the record-normal root, not this one
#endif

#if !defined(WAVE5B_SSS_SURFACE_CONTACT_SHAPE)
#error WAVE5B_SSS_SURFACE_CONTACT_SHAPE is required
#endif

#if WAVE5B_SSS_SURFACE_CONTACT_SHAPE == 1
#define WAVE5B_SURFACE_CONTACT_CB12_COUNT 28
#define WAVE5B_SURFACE_CONTACT_CB2_COUNT 4
#define WAVE5B_SURFACE_CONTACT_HAS_T2 0
#define WAVE5B_SURFACE_CONTACT_HAS_WETNESS 0
#elif WAVE5B_SSS_SURFACE_CONTACT_SHAPE == 2
#define WAVE5B_SURFACE_CONTACT_CB12_COUNT 31
#define WAVE5B_SURFACE_CONTACT_CB2_COUNT 7
#define WAVE5B_SURFACE_CONTACT_HAS_T2 0
#define WAVE5B_SURFACE_CONTACT_HAS_WETNESS 1
#define WAVE5B_SURFACE_CONTACT_WETNESS_INDEX 6
#elif WAVE5B_SSS_SURFACE_CONTACT_SHAPE == 3
#define WAVE5B_SURFACE_CONTACT_CB12_COUNT 28
#define WAVE5B_SURFACE_CONTACT_CB2_COUNT 7
#define WAVE5B_SURFACE_CONTACT_HAS_T2 1
#define WAVE5B_SURFACE_CONTACT_HAS_WETNESS 0
#elif WAVE5B_SSS_SURFACE_CONTACT_SHAPE == 4
#define WAVE5B_SURFACE_CONTACT_CB12_COUNT 31
#define WAVE5B_SURFACE_CONTACT_CB2_COUNT 8
#define WAVE5B_SURFACE_CONTACT_HAS_T2 1
#define WAVE5B_SURFACE_CONTACT_HAS_WETNESS 1
#define WAVE5B_SURFACE_CONTACT_WETNESS_INDEX 7
#else
#error Unsupported SSS MRT surface/contact shape
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12[WAVE5B_SURFACE_CONTACT_CB12_COUNT];
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2[WAVE5B_SURFACE_CONTACT_CB2_COUNT];
};

SamplerState g_sNormal : register(s1);
#if WAVE5B_SURFACE_CONTACT_HAS_T2
SamplerState g_sMarch : register(s2);
#endif
SamplerState g_sDepth : register(s7);
SamplerState g_sDecalColor : register(s9);
SamplerState g_sDecalNormal : register(s11);
SamplerState g_sDecalAux : register(s12);

Texture2D<float4> g_tNormal : register(t1);
#if WAVE5B_SURFACE_CONTACT_HAS_T2
Texture2D<float4> g_tMarch : register(t2);
#endif
Texture2D<float4> g_tDepth : register(t7);

struct DecalRecord
{
    float4 projectionRow0;
    float4 projectionRow1;
    float4 projectionRow2;
    float4 pad0x30;
    float4 pad0x40;
    float4 pad0x50;
    float4 pad0x60;
    float4 pad0x70;
    float4 marchFrame;
    float3 decalDirection;
    float pad0x9c;
    float4 recordNormal;
    float4 pad0xb0;
    float4 uvBounds;
    float4 pad0xd0;
};

StructuredBuffer<DecalRecord> g_decalRecords : register(t8);
Texture2D<float4> g_tDecalColor : register(t9);
Texture2D<float4> g_tDecalNormal : register(t11);
Texture2D<float4> g_tDecalAux : register(t12);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    nointerpolation uint decalIndex : COLOR1;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
    float4 auxiliary : SV_Target3;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
    float4 projectionRow0 = g_decalRecords[input.decalIndex].projectionRow0;
    float4 projectionRow1 = g_decalRecords[input.decalIndex].projectionRow1;
    float4 projectionRow2 = g_decalRecords[input.decalIndex].projectionRow2;
#if WAVE5B_SURFACE_CONTACT_HAS_T2
    float3 marchFrame = g_decalRecords[input.decalIndex].marchFrame.xyz;
#endif
    float3 decalDirection = g_decalRecords[input.decalIndex].decalDirection;
#if WAVE5B_SURFACE_CONTACT_HAS_T2
    float3 recordNormal = g_decalRecords[input.decalIndex].recordNormal.xyz;
#endif
    float4 uvBounds = g_decalRecords[input.decalIndex].uvBounds;
    float decalOpacity = g_decalRecords[input.decalIndex].pad0xd0.w;

    float2 screenUv = input.position.xy * cb2[0].xy;
    float depth = g_tDepth.SampleLevel(g_sDepth, screenUv, 0.0).x;
    float2 encodedNormal = g_tNormal.SampleLevel(g_sNormal, screenUv, 0.0).xy;
    float2 octahedral = encodedNormal * 4.0 - 2.0;
    float normalLengthSq = dot(octahedral, octahedral);
    float2 octahedralFactors =
        1.0 - normalLengthSq * float2(0.25, 0.5);
    float3 surfaceNormal = float3(
        octahedral * sqrt(octahedralFactors.x),
        -octahedralFactors.y);
#if !WAVE5B_SURFACE_CONTACT_HAS_T2
    float3 recordNormal = surfaceNormal;
#endif

    float projectedDepth;
    float4 inverseRow0;
    float4 inverseRow1;
    float4 inverseRow2;
    float4 inverseRow3;
    if (depth <= 0.01)
    {
        projectedDepth = depth * 100.0;
        inverseRow0 = cb12[24];
        inverseRow1 = cb12[25];
        inverseRow2 = cb12[26];
        inverseRow3 = cb12[27];
    }
    else
    {
        projectedDepth = depth * 1.01 - 0.01;
        inverseRow0 = cb12[20];
        inverseRow1 = cb12[21];
        inverseRow2 = cb12[22];
        inverseRow3 = cb12[23];
    }

    float2 clipPosition = float2(
        screenUv.x * cb2[0].z,
        1.0 - screenUv.y * cb2[0].w) * 2.0 - 1.0;
    float4 homogeneousPosition = float4(clipPosition, projectedDepth, 1.0);
    float4 reconstructedPosition = float4(
        dot(inverseRow0, homogeneousPosition),
        dot(inverseRow1, homogeneousPosition),
        dot(inverseRow2, homogeneousPosition),
        dot(inverseRow3, homogeneousPosition));
    reconstructedPosition.xyz /= reconstructedPosition.w;
    float3 viewDirection = normalize(-reconstructedPosition.xyz);

#if WAVE5B_SURFACE_CONTACT_HAS_WETNESS
    bool disableWetness = cb2[WAVE5B_SURFACE_CONTACT_WETNESS_INDEX].y == -1.0;
    float wetness = min(
        pow(1.0 - dot(viewDirection, recordNormal), cb2[WAVE5B_SURFACE_CONTACT_WETNESS_INDEX].y),
        1.0);
    wetness = 1.0 + cb12[30].x * (wetness - 1.0);
    wetness = disableWetness ? 1.0 : wetness;
#endif

    float4 worldPosition = float4(reconstructedPosition.xyz, 1.0);
    float3 projectedPosition = float3(
        dot(projectionRow0, worldPosition),
        dot(projectionRow1, worldPosition),
        dot(projectionRow2, worldPosition));

#if WAVE5B_SURFACE_CONTACT_HAS_T2
    float3 localPosition = float3(
        dot(marchFrame.xyz, reconstructedPosition.xyz),
        dot(decalDirection, reconstructedPosition.xyz),
        dot(recordNormal, reconstructedPosition.xyz));
    localPosition = normalize(localPosition);
    float2 localDirection = normalize(localPosition.xy);
    float localRadius = sqrt(dot(localPosition, localPosition) - localPosition.z * localPosition.z);
    float2 marchDirection = localDirection * (localRadius / localPosition.z) * cb2[6].x;
    float facing = dot(recordNormal, viewDirection);
    float maxSteps = min(facing * -56.0 + 72.0, cb2[6].y);
    float inverseMaxSteps = 1.0 / maxSteps;
    float stopCounter = maxSteps + 1.0;
    float2 marchCursor = projectedPosition.xy;
    float marchCounter = 0.0;
    float2 previousTraceAndSample = float2(1.0, 1.0);
    float4 hit = 0.0;

    [loop]
    while (marchCounter < maxSteps)
    {
        marchCursor += inverseMaxSteps * marchDirection;
        float sampledMarch = g_tMarch.SampleLevel(g_sMarch, marchCursor, 0.0).x;
        float trace = previousTraceAndSample.x - inverseMaxSteps;
        bool crossed = trace < sampledMarch;
        marchCounter = crossed ? stopCounter : marchCounter + 1.0;
        hit = crossed
            ? float4(trace, sampledMarch, previousTraceAndSample.x, previousTraceAndSample.y)
            : hit;
        previousTraceAndSample = float2(trace, sampledMarch);
    }

    float previousGap = hit.z - hit.w;
    float currentGap = hit.x - hit.y;
    float denominator = previousGap - currentGap;
    float fraction = 1.0 - (hit.x * previousGap - currentGap * hit.z) / denominator;
    fraction = denominator == 0.0 ? 1.0 : fraction;
    projectedPosition.xy += marchDirection * fraction;
#endif

#if WAVE5B_SURFACE_CONTACT_HAS_T2
    const float kTapOffset[7] = { 0.22, 0.1925, 0.165, 0.1375, 0.11, 0.0825, 0.055 };
    const float kTapBias[7] = { 0.88, 0.77, 0.66, 0.55, 0.44, 0.33, 0.22 };
    const float kTapWeight[7] = { 1.0, 2.0, 4.0, 6.0, 8.0, 10.0, 12.0 };
    float2 shadowStep = g_decalRecords[input.decalIndex].pad0xd0.xy;
    float centerOcclusion =
        g_tMarch.SampleLevel(g_sMarch, projectedPosition.xy, 0.0).x;
    float contact = 0.0;
    [unroll]
    for (int tap = 0; tap < 7; ++tap)
    {
        float2 tapUv = shadowStep * kTapOffset[tap] + projectedPosition.xy;
        float tapValue = g_tMarch.SampleLevel(g_sMarch, tapUv, 0.0).x;
        tapValue = (tapValue - centerOcclusion) - kTapBias[tap];
        tapValue *= kTapWeight[tap];
        contact = tap == 0 ? tapValue : max(tapValue, contact);
    }
    float contactShadow =
        saturate(1.0 - contact * cb2[6].x * 10.0) * 0.8 + 0.2;
#endif

    clip(projectedPosition.xy - uvBounds.xy);
    clip(uvBounds.zw - projectedPosition.xy);
    clip(1.0 - projectedPosition.z);
    clip(projectedPosition.z);

    float decalLod = g_tDecalColor.CalculateLevelOfDetail(g_sDecalColor, projectedPosition.xy);
    float3 projectionDirection = normalize(-projectionRow2.xyz);
    float3 derivativeX = normalize(ddx_coarse(reconstructedPosition.xyz));
    float3 derivativeY = normalize(ddy_coarse(reconstructedPosition.xyz));
    float3 geometricNormal = normalize(cross(derivativeX, derivativeY));
    float geometricFacing = dot(geometricNormal, projectionDirection) - 0.3;
    bool geometricBack = geometricFacing < 0.0;
    float surfaceFacing = dot(projectionDirection, surfaceNormal) - 0.3;
    bool surfaceBack = surfaceFacing < 0.0;

    if (geometricBack && surfaceBack)
        discard;

    float4 decalColor = g_tDecalColor.SampleLevel(g_sDecalColor, projectedPosition.xy, decalLod);
    float2 auxiliary = g_tDecalAux.SampleLevel(g_sDecalAux, projectedPosition.xy, decalLod).xy;
    float2 decalNormalXy = g_tDecalNormal.SampleLevel(g_sDecalNormal, projectedPosition.xy, decalLod).xy;
    float angleFade = geometricBack ? min(max(surfaceFacing, 0.0), 0.25) : 0.25;
    float alpha = decalColor.w * angleFade * 4.0;
    clip(alpha - 4.0 / 255.0);
    alpha *= decalOpacity;

    float2 tangentNormalXy = decalNormalXy * 2.0 - 1.0;
    float tangentNormalZ = sqrt(max(1.0 - dot(tangentNormalXy, tangentNormalXy), 0.0));
    float3 tangentNormal = float3(tangentNormalXy, tangentNormalZ);
    float3 basisTangent = cross(recordNormal, decalDirection);
    float3 basisBitangent = cross(basisTangent, -recordNormal);
    float3 mappedNormal = normalize(
        mul(tangentNormal, float3x3(basisTangent, basisBitangent, recordNormal)));

    output.normal.z = -mappedNormal.z;
    output.normal.xy = mappedNormal.xy / sqrt(mappedNormal.z * -8.0 + 8.0) + 0.5;
#if WAVE5B_SURFACE_CONTACT_HAS_WETNESS
    output.material.z = sqrt(wetness * cb2[3].x * 0.02);
#else
    output.material.z = sqrt(cb2[3].x * 0.02);
#endif
    output.material.w = saturate(cb2[3].y);
#if WAVE5B_SURFACE_CONTACT_HAS_T2
    output.color = float4(decalColor.xyz * contactShadow, alpha);
#else
    output.color = float4(decalColor.xyz, alpha);
#endif
    output.normal.w = alpha;
    output.material.x = 0.0;
    output.material.y = cb2[3].y / 255.0;
    output.auxiliary = float4(auxiliary, 0.0, alpha);
    return output;
}
