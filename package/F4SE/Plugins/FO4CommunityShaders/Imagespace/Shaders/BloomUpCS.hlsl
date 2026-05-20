// Bilinear upsample with additive accumulate: SrcMip is the smaller (already-blurred) mip,
// DstMip is the next-larger mip; we read both and write back to DstMip with src + dst.
// Iterating coarsest-to-finest builds the classic stacked-mip bloom shape.

Texture2D<float4>     SrcMip : register(t0);
Texture2D<float4>     DstReadMip : register(t1);
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

    const float2 dstStep = 1.0 / float2(DstDimensions);
    const float2 uv      = (float2(px) + 0.5) * dstStep;

    const float3 src = SrcMip.SampleLevel(LinearClampSampler, uv, 0).rgb;
    const float3 dst = DstReadMip.Load(int3(px, 0)).rgb;

    DstMip[px] = float4(dst + src, 1.0);
}
