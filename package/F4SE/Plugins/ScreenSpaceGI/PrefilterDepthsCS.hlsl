// Depth pyramid build: linearised view-Z, R16F, one dispatch per mip pair.
// Mip 0 reads scene NDC depth (FO4 standard: near=0, far=1); mips 1..N average previous mip.

Texture2D<float> SrcNDCDepth : register(t0);
Texture2D<float> SrcPyramid  : register(t1);
RWTexture2D<float> DstMip    : register(u0);

cbuffer PyramidCB : register(b0)
{
    uint2  SrcDim;
    uint2  DstDim;
    uint   IsLDR;
    uint   SrcMipIdx;
    float  NearC;
    float  FarC;
};

float Linearize(float ndc) { return NearC * FarC / (FarC - ndc * (FarC - NearC)); }

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= DstDim.x || dtid.y >= DstDim.y) return;
    const int2 src = int2(dtid.xy) * 2;

    // XeGTAO uses MIN depth (closest surface) for AO pyramid; average creates phantom mid-depth surfaces at edges.
    if (IsLDR != 0) {
        const float d0 = SrcNDCDepth.Load(int3(src + int2(0, 0), 0));
        const float d1 = SrcNDCDepth.Load(int3(src + int2(1, 0), 0));
        const float d2 = SrcNDCDepth.Load(int3(src + int2(0, 1), 0));
        const float d3 = SrcNDCDepth.Load(int3(src + int2(1, 1), 0));
        DstMip[dtid.xy] = min(min(Linearize(d0), Linearize(d1)), min(Linearize(d2), Linearize(d3)));
    } else {
        const int mip = int(SrcMipIdx);
        const float l0 = SrcPyramid.Load(int3(src + int2(0, 0), mip));
        const float l1 = SrcPyramid.Load(int3(src + int2(1, 0), mip));
        const float l2 = SrcPyramid.Load(int3(src + int2(0, 1), mip));
        const float l3 = SrcPyramid.Load(int3(src + int2(1, 1), mip));
        DstMip[dtid.xy] = min(min(l0, l1), min(l2, l3));
    }
}
