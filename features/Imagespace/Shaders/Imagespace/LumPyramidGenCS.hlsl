// Log-luma pyramid: SrcIsLDR=1 reads kFrameBuffer, =0 reads previous one-mip pyramid SRV. log2 so EMA computes a geometric mean.

Texture2D<float4>   SrcSRGB     : register(t0);   // kFrameBuffer
Texture2D<float>    SrcPyramid  : register(t1);   // previous lumPyramid mip as a one-mip SRV
RWTexture2D<float>  DstMip      : register(u0);

cbuffer PyramidCB : register(b0)
{
    uint  SrcIsLDR;
    uint  _Pad0;
    uint2 DstDimensions;
    uint  TailW;
    uint  TailH;
    uint2 _Pad1;
};

float LogLumaLinear(float3 lin)
{
    // kFrameBuffer is linear HDR (R11G11B10F); no gamma expansion needed.
    const float luma = dot(max(lin, 0.0), float3(0.2126, 0.7152, 0.0722));
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
        DstMip[px] = 0.25 * (LogLumaLinear(c0) + LogLumaLinear(c1) + LogLumaLinear(c2) + LogLumaLinear(c3));
    } else {
        const float l0 = SrcPyramid.Load(int3(src + int2(0, 0), 0));
        const float l1 = SrcPyramid.Load(int3(src + int2(1, 0), 0));
        const float l2 = SrcPyramid.Load(int3(src + int2(0, 1), 0));
        const float l3 = SrcPyramid.Load(int3(src + int2(1, 1), 0));
        DstMip[px] = 0.25 * (l0 + l1 + l2 + l3);
    }
}
