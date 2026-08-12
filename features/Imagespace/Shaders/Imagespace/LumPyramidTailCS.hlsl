// Single-dispatch LDS reduction of the pyramid tail (current mip <= 8x8 -> 1x1).

Texture2D<float>    SrcPyramid : register(t0);
RWTexture2D<float>  DstMip     : register(u0);

cbuffer PyramidCB : register(b0)
{
    uint  SrcIsLDR;
    uint  _Pad0;
    uint2 DstDimensions;
    uint  TailW;
    uint  TailH;
    uint2 _Pad1;
};

groupshared float lds[64];

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint gIdx : SV_GroupIndex)
{
    const uint2 px = dtid.xy;
    const float val = (px.x < TailW && px.y < TailH)
        ? SrcPyramid.Load(int3(px, 0))
        : 0.0;
    lds[gIdx] = val;
    GroupMemoryBarrierWithGroupSync();

    [unroll] for (uint s = 32; s > 0; s >>= 1) {
        if (gIdx < s)
            lds[gIdx] += lds[gIdx + s];
        GroupMemoryBarrierWithGroupSync();
    }

    if (gIdx == 0) {
        const float validCount = float(TailW * TailH);
        DstMip[uint2(0, 0)] = lds[0] / max(validCount, 1.0);
    }
}
