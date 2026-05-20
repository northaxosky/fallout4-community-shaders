// DOF Pass 4: full-res composite. Lerp(sharpInput, blurredHalf, smoothstep on |CoC|).

Texture2D<float4>    SharpFB   : register(t0);  // full-res framebuffer (graded)
Texture2D<float4>    BlurredHalf : register(t1); // half-res blur output
Texture2D<float>     CocIn     : register(t2);  // half-res signed CoC

SamplerState         LinearClampSamp : register(s0);

RWTexture2D<float4>  Output    : register(u0);  // full-res RGBA8 output (= compositeScratch)

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

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= FullDimensions.x || dtid.y >= FullDimensions.y) return;

    // Bilinear-sample blur + CoC at full-res UVs.
    const float2 uv = (float2(dtid.xy) + 0.5) / float2(FullDimensions);
    const float coc = CocIn.SampleLevel(LinearClampSamp, uv, 0);
    const float3 sharp = SharpFB.Load(int3(dtid.xy, 0)).rgb;
    const float3 blurred = BlurredHalf.SampleLevel(LinearClampSamp, uv, 0).rgb;

    // Smoothstep on |CoC|: under 1px stays sharp, blends to fully-blurred at ~4px.
    const float t = smoothstep(0.5, 4.0, abs(coc));
    const float3 outColor = lerp(sharp, blurred, t);
    Output[dtid.xy] = float4(outColor, 1.0);
}
