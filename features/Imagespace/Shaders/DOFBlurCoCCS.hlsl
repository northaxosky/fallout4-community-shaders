// IS-5 Pass 3: half-res CoC-weighted disc-sample blur with tile early-out.

Texture2D<float4>    HalfColorIn : register(t0);  // half-res downsampled scene
Texture2D<float>     CocIn       : register(t1);  // half-res signed CoC
Texture2D<float2>    TileIn      : register(t2);  // /16 {minCoC, maxCoC}

RWTexture2D<float4>  HalfColorOut : register(u0); // half-res blur output

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
    float  Pad0;
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

    // Tile early-out: if max |CoC| in tile is < 1px, the disc collapses to a point — passthrough.
    const int2 tileCoord = int2(dtid.xy) / 16;
    const float2 tileMM = TileIn.Load(int3(tileCoord, 0));
    const float tileMaxAbs = max(abs(tileMM.x), abs(tileMM.y));
    if (tileMaxAbs < 1.0) {
        HalfColorOut[dtid.xy] = HalfColorIn.Load(int3(dtid.xy, 0));
        return;
    }

    const float coc = CocIn.Load(int3(dtid.xy, 0));
    const float radiusPx = max(1.0, abs(coc));

    // Uniform disc blur: each sample contributes equally. Energy-conserving by construction.
    // IS-5a uses this; IS-5b can switch to scatter-as-gather (sample contributes if its CoC
    // reaches our pixel) for better in-focus / out-of-focus boundary handling.
    const uint sampleCount = (QualityLevel == 0u) ? 12u : (QualityLevel == 1u ? 24u : 24u);
    float3 accum = 0.0;

    [unroll(24)]
    for (uint i = 0; i < sampleCount; ++i) {
        const float2 offset = kDiscSamples24[i] * radiusPx;
        int2 px = int2(round(float2(dtid.xy) + offset));
        px = clamp(px, int2(0, 0), int2(HalfDimensions) - 1);
        accum += HalfColorIn.Load(int3(px, 0)).rgb;
    }

    HalfColorOut[dtid.xy] = float4(accum / float(sampleCount), 1.0);
}
