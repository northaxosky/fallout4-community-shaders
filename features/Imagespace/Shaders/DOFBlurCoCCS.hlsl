// DOF Pass 3: half-res disc-sample blur with tile early-out.

Texture2D<float4>    HalfColorIn : register(t0);  // half-res downsampled scene
Texture2D<float>     CocIn       : register(t1);  // half-res signed CoC
Texture2D<float2>    TileIn      : register(t2);  // /16 {minCoC, maxCoC}

RWTexture2D<float4>  NearColorOut : register(u0); // half-res foreground blur output
RWTexture2D<float4>  FarColorOut  : register(u1); // half-res background blur output

cbuffer DofCB : register(b0)
{
    float  CocScale;
    float  CocBias;
    float  CocLimit;
    float  FocusRange;
    uint2  HalfDimensions;
    uint2  FullDimensions;
    uint   QualityLevel;
    float  NearPlane;
    float  FarPlane;
    float  BokehIntensity;
    float  AnamorphRatio;
    float3 Pad0;
};

// 24-point Halton-disc sample pattern in unit-radius polar coords.
static const float2 kDiscSamples24[24] = {
    float2( 0.000,  0.000), float2( 0.500,  0.000), float2( 0.250,  0.433),
    float2(-0.250,  0.433), float2(-0.500,  0.000), float2(-0.250, -0.433),
    float2( 0.250, -0.433), float2( 0.866,  0.250), float2( 0.000,  0.866),
    float2(-0.866,  0.250), float2(-0.866, -0.250), float2( 0.000, -0.866),
    float2( 0.866, -0.250), float2( 0.354,  0.354), float2(-0.354,  0.354),
    float2(-0.354, -0.354), float2( 0.354, -0.354), float2( 0.707,  0.000),
    float2( 0.000,  0.707), float2(-0.707,  0.000), float2( 0.000, -0.707),
    float2( 0.612,  0.612), float2(-0.612,  0.612), float2(-0.612, -0.612)
};

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= HalfDimensions.x || dtid.y >= HalfDimensions.y) return;

    // If max tile |CoC| < 1px, the disc collapses to passthrough.
    const int2 tileCoord = int2(dtid.xy) / 16;
    const float2 tileMM = TileIn.Load(int3(tileCoord, 0));
    const float tileMaxAbs = max(abs(tileMM.x), abs(tileMM.y));
    const float3 centerColor = HalfColorIn.Load(int3(dtid.xy, 0)).rgb;
    if (tileMaxAbs < 1.0) {
        NearColorOut[dtid.xy] = float4(centerColor, 1.0);
        FarColorOut[dtid.xy] = float4(centerColor, 1.0);
        return;
    }

    const float coc = CocIn.Load(int3(dtid.xy, 0));
    const float centerCocAbs = max(abs(coc), 1e-3);
    const float radiusPx = max(1.0, abs(coc));

    // Split gather by CoC sign so foreground blur can composite over background blur.
    const uint sampleCount = (QualityLevel == 0u) ? 12u : (QualityLevel == 1u ? 24u : 24u);
    float3 nearAccum = 0.0;
    float3 farAccum = 0.0;
    float3 nearMax = 0.0;
    float3 farMax = 0.0;
    float nearWeightSum = 0.0;
    float farWeightSum = 0.0;
    const float2 scale = float2(AnamorphRatio, 1.0) * radiusPx;

    [unroll(24)]
    for (uint i = 0; i < sampleCount; ++i) {
        const float2 offset = kDiscSamples24[i] * scale;
        int2 px = int2(round(float2(dtid.xy) + offset));
        px = clamp(px, int2(0, 0), int2(HalfDimensions) - 1);

        const float sampleCoc = CocIn.Load(int3(px, 0));
        const float3 sampleColor = HalfColorIn.Load(int3(px, 0)).rgb;
        const float nearWeight = saturate(-sampleCoc / centerCocAbs);
        const float farWeight = saturate(sampleCoc / centerCocAbs);

        nearAccum += sampleColor * nearWeight;
        farAccum += sampleColor * farWeight;
        nearMax = max(nearMax, sampleColor * nearWeight);
        farMax = max(farMax, sampleColor * farWeight);
        nearWeightSum += nearWeight;
        farWeightSum += farWeight;
    }

    const float3 nearColor = (nearWeightSum > 1e-4) ? (nearAccum / nearWeightSum) : centerColor;
    const float3 farColor = (farWeightSum > 1e-4) ? (farAccum / farWeightSum) : centerColor;

    if (BokehIntensity > 0.0) {
        const float3 lumaWeights = float3(0.299, 0.587, 0.114);
        const float nearMaxLuma = dot(nearMax, lumaWeights);
        const float nearAvgLuma = dot(nearColor, lumaWeights);
        const float nearW = max(0.0, nearMaxLuma - nearAvgLuma) * BokehIntensity * 2.0;
        const float farMaxLuma = dot(farMax, lumaWeights);
        const float farAvgLuma = dot(farColor, lumaWeights);
        const float farW = max(0.0, farMaxLuma - farAvgLuma) * BokehIntensity * 2.0;
        NearColorOut[dtid.xy] = float4(nearColor + nearMax * saturate(nearW * nearW * radiusPx), 1.0);
        FarColorOut[dtid.xy] = float4(farColor + farMax * saturate(farW * farW * radiusPx), 1.0);
    } else {
        NearColorOut[dtid.xy] = float4(nearColor, 1.0);
        FarColorOut[dtid.xy] = float4(farColor, 1.0);
    }
}
