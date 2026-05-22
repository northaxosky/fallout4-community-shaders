// Kawase 4-tap downsample (Masaki Kawase, GDC 2003 "Frame Buffer Postprocessing Effects").
// First downsample (mip 0 -> 1) uses Karis-average luminance weighting to suppress fireflies
// from HDR specular hotspots (Karis, SIGGRAPH 2013 "Real Shading in Unreal Engine 4").

Texture2D<float4>     SrcMip : register(t0);
SamplerState          LinearClampSampler : register(s0);
RWTexture2D<float4>   DstMip : register(u0);

cbuffer BloomCB : register(b0)
{
    uint2 SrcDimensions;
    uint2 DstDimensions;
    uint  IsFirstDownsample;
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

    float3 result;
    if (IsFirstDownsample != 0) {
        // Karis-average: weight each tap by 1 / (1 + max(rgb)) so bright outliers contribute less.
        const float w0 = rcp(1.0 + max(c0.r, max(c0.g, c0.b)));
        const float w1 = rcp(1.0 + max(c1.r, max(c1.g, c1.b)));
        const float w2 = rcp(1.0 + max(c2.r, max(c2.g, c2.b)));
        const float w3 = rcp(1.0 + max(c3.r, max(c3.g, c3.b)));
        result = (c0 * w0 + c1 * w1 + c2 * w2 + c3 * w3) / max(w0 + w1 + w2 + w3, 1e-5);
    } else {
        result = 0.25 * (c0 + c1 + c2 + c3);
    }

    DstMip[px] = float4(result, 1.0);
}
