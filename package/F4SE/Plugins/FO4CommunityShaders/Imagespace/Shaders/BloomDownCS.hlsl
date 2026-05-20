// Kawase 4-tap downsample (Masaki Kawase, GDC 2003 "Frame Buffer Postprocessing Effects").

Texture2D<float4>     SrcMip : register(t0);
SamplerState          LinearClampSampler : register(s0);
RWTexture2D<float4>   DstMip : register(u0);

cbuffer BloomCB : register(b0)
{
    uint2 SrcDimensions;
    uint2 DstDimensions;
};

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 px = dtid.xy;
    if (px.x >= DstDimensions.x || px.y >= DstDimensions.y)
        return;

    // Sample at four offset half-pixels in source space (Kawase distance = 0.5 mip step).
    const float2 srcSize  = float2(SrcDimensions);
    const float2 dstStep  = 1.0 / float2(DstDimensions);
    const float2 uv       = (float2(px) + 0.5) * dstStep;
    const float2 step     = 0.5 / srcSize;

    float3 c0 = SrcMip.SampleLevel(LinearClampSampler, uv + float2(-step.x, -step.y), 0).rgb;
    float3 c1 = SrcMip.SampleLevel(LinearClampSampler, uv + float2( step.x, -step.y), 0).rgb;
    float3 c2 = SrcMip.SampleLevel(LinearClampSampler, uv + float2(-step.x,  step.y), 0).rgb;
    float3 c3 = SrcMip.SampleLevel(LinearClampSampler, uv + float2( step.x,  step.y), 0).rgb;

    DstMip[px] = float4(0.25 * (c0 + c1 + c2 + c3), 1.0);
}
