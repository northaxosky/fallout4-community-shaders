// DOF Pass 4: full-res composite. Far blur blends over sharp, then near blur blends on top.

Texture2D<float4>    SharpFB         : register(t0); // full-res framebuffer (graded)
Texture2D<float4>    NearBlurredHalf : register(t1); // half-res foreground blur output
Texture2D<float4>    FarBlurredHalf  : register(t2); // half-res background blur output
Texture2D<float>     CocIn           : register(t3); // half-res signed CoC

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
    float  BokehIntensity;
    float  AnamorphRatio;
    float3 Pad0;
};

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= FullDimensions.x || dtid.y >= FullDimensions.y) return;

    // Bilinear-sample separated blur + CoC at full-res UVs.
    const float2 uv = (float2(dtid.xy) + 0.5) / float2(FullDimensions);
    const float coc = CocIn.SampleLevel(LinearClampSamp, uv, 0);
    const float3 sharp = SharpFB.Load(int3(dtid.xy, 0)).rgb;
    const float3 nearBlurred = NearBlurredHalf.SampleLevel(LinearClampSamp, uv, 0).rgb;
    const float3 farBlurred = FarBlurredHalf.SampleLevel(LinearClampSamp, uv, 0).rgb;

    const float nearT = smoothstep(0.0, 4.0, -coc);
    const float farT = smoothstep(0.0, 4.0, coc);
    float3 outColor = lerp(sharp, farBlurred, farT);
    outColor = lerp(outColor, nearBlurred, nearT);
    Output[dtid.xy] = float4(outColor, 1.0);
}
