// DOF Pass 1: read main depth + scene color, output half-res CoC + half-res downsampled color.

Texture2D<float>    DepthIn  : register(t0);  // FO4 main depth (hyperbolic z, 0..1; near=0)
Texture2D<float4>   ColorIn  : register(t1);  // full-res framebuffer (post-graded)

RWTexture2D<float>  CocOut   : register(u0);  // half-res, signed CoC in pixel units
RWTexture2D<float4> ColorOut : register(u1);  // half-res downsampled scene

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

float Linearize(float ndc)
{
    return NearPlane * FarPlane / (FarPlane - ndc * (FarPlane - NearPlane));
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= HalfDimensions.x || dtid.y >= HalfDimensions.y) return;

    // Use closest depth in the matching 2x2 full-res footprint for sharper CoC edges.
    const int2 src = int2(dtid.xy) * 2;
    const float d0 = DepthIn.Load(int3(src + int2(0, 0), 0));
    const float d1 = DepthIn.Load(int3(src + int2(1, 0), 0));
    const float d2 = DepthIn.Load(int3(src + int2(0, 1), 0));
    const float d3 = DepthIn.Load(int3(src + int2(1, 1), 0));
    const float dMin = min(min(d0, d1), min(d2, d3));
    const float vz = Linearize(dMin);

    // Pre-baked scale*z+bias gives signed CoC pixels: negative foreground, positive background.
    float coc = CocScale * vz + CocBias;
    coc = clamp(coc, -CocLimit, CocLimit);
    CocOut[dtid.xy] = coc;

    const float3 c0 = ColorIn.Load(int3(src + int2(0, 0), 0)).rgb;
    const float3 c1 = ColorIn.Load(int3(src + int2(1, 0), 0)).rgb;
    const float3 c2 = ColorIn.Load(int3(src + int2(0, 1), 0)).rgb;
    const float3 c3 = ColorIn.Load(int3(src + int2(1, 1), 0)).rgb;
    ColorOut[dtid.xy] = float4(0.25 * (c0 + c1 + c2 + c3), 1.0);
}
