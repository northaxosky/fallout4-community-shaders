// Log-luma pyramid: SrcIsLDR=1 reads kFrameBuffer, =0 reads previous pyramid mip (2x2 avg). log2 so EMA computes a geometric mean.

Texture2D<float4>   SrcSRGB     : register(t0);   // kFrameBuffer
Texture2D<float>    SrcPyramid  : register(t1);   // lumPyramid (full-mip SRV)
RWTexture2D<float>  DstMip      : register(u0);

cbuffer PyramidCB : register(b0)
{
    uint  SrcIsLDR;
    uint  SrcMipIdx;
    uint2 DstDimensions;
};

float LogLumaSRGB(float3 srgb)
{
    const float3 lin = pow(saturate(srgb), 2.2);
    const float  luma = dot(lin, float3(0.2126, 0.7152, 0.0722));
    return log2(max(luma, 1e-5));
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 px = dtid.xy;
    if (px.x >= DstDimensions.x || px.y >= DstDimensions.y)
        return;

    const int2 src = int2(px) * 2;
    if (SrcIsLDR != 0) {
        const float3 c0 = SrcSRGB.Load(int3(src + int2(0, 0), 0)).rgb;
        const float3 c1 = SrcSRGB.Load(int3(src + int2(1, 0), 0)).rgb;
        const float3 c2 = SrcSRGB.Load(int3(src + int2(0, 1), 0)).rgb;
        const float3 c3 = SrcSRGB.Load(int3(src + int2(1, 1), 0)).rgb;
        DstMip[px] = 0.25 * (LogLumaSRGB(c0) + LogLumaSRGB(c1) + LogLumaSRGB(c2) + LogLumaSRGB(c3));
    } else {
        const int mip = int(SrcMipIdx);
        const float l0 = SrcPyramid.Load(int3(src + int2(0, 0), mip));
        const float l1 = SrcPyramid.Load(int3(src + int2(1, 0), mip));
        const float l2 = SrcPyramid.Load(int3(src + int2(0, 1), mip));
        const float l3 = SrcPyramid.Load(int3(src + int2(1, 1), mip));
        DstMip[px] = 0.25 * (l0 + l1 + l2 + l3);
    }
}
