// Additive 9-tap tent upsample; coarsest-to-finest builds stacked-mip bloom.

Texture2D<float4>     SrcMip : register(t0);
Texture2D<float4>     DstReadMip : register(t1);
SamplerState          LinearClampSampler : register(s0);
RWTexture2D<float4>   DstMip : register(u0);

cbuffer BloomCB : register(b0)
{
    uint2 SrcDimensions;
    uint2 DstDimensions;
    uint  IsFirstDownsample;
    float MipWeight;
    uint2 _Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 px = dtid.xy;
    if (px.x >= DstDimensions.x || px.y >= DstDimensions.y)
        return;

    const float2 uv = (float2(px) + 0.5) / float2(DstDimensions);
    const float2 o  = 1.5 / float2(SrcDimensions);

    const float3 s0 = SrcMip.SampleLevel(LinearClampSampler, uv + float2(-o.x, -o.y), 0).rgb;
    const float3 s1 = SrcMip.SampleLevel(LinearClampSampler, uv + float2( o.x, -o.y), 0).rgb;
    const float3 s2 = SrcMip.SampleLevel(LinearClampSampler, uv + float2(-o.x,  o.y), 0).rgb;
    const float3 s3 = SrcMip.SampleLevel(LinearClampSampler, uv + float2( o.x,  o.y), 0).rgb;
    const float3 src = (s0 + s1 + s2 + s3) * 0.25;
    const float3 dst = DstReadMip.Load(int3(px, 0)).rgb;

    DstMip[px] = float4(dst + src * MipWeight, 1.0);
}
