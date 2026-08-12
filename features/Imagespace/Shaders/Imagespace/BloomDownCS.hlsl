// COD-style bloom downsample; mip 0 -> 1 uses Karis weights to suppress fireflies.

Texture2D<float4>     SrcMip : register(t0);
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

float KarisWeight(float3 color)
{
    return rcp(1.0 + max(color.r, max(color.g, color.b)));
}

void AccumulateQuad(inout float3 sum, inout float weightSum, float2 uv, float kernelWeight)
{
    const float3 c = SrcMip.SampleLevel(LinearClampSampler, uv, 0).rgb;
    const float w = (IsFirstDownsample != 0) ? kernelWeight * KarisWeight(c) : kernelWeight;
    sum += c * w;
    weightSum += w;
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 px = dtid.xy;
    if (px.x >= DstDimensions.x || px.y >= DstDimensions.y)
        return;

    const float2 uv    = (float2(px) + 0.5) / float2(DstDimensions);
    const float2 texel = 1.0 / float2(SrcDimensions);

    float3 sum = 0.0;
    float weightSum = 0.0;

    AccumulateQuad(sum, weightSum, uv, 4.0 / 16.0);

    const float2 inner = 2.0 * texel;
    AccumulateQuad(sum, weightSum, uv + float2(-inner.x, -inner.y), 2.0 / 16.0);
    AccumulateQuad(sum, weightSum, uv + float2( inner.x, -inner.y), 2.0 / 16.0);
    AccumulateQuad(sum, weightSum, uv + float2(-inner.x,  inner.y), 2.0 / 16.0);
    AccumulateQuad(sum, weightSum, uv + float2( inner.x,  inner.y), 2.0 / 16.0);

    const float2 outer = 4.0 * texel;
    AccumulateQuad(sum, weightSum, uv + float2(-outer.x, 0.0), 1.0 / 16.0);
    AccumulateQuad(sum, weightSum, uv + float2( outer.x, 0.0), 1.0 / 16.0);
    AccumulateQuad(sum, weightSum, uv + float2(0.0, -outer.y), 1.0 / 16.0);
    AccumulateQuad(sum, weightSum, uv + float2(0.0,  outer.y), 1.0 / 16.0);

    DstMip[px] = float4(sum * rcp(max(weightSum, 1e-5)), 1.0);
}
