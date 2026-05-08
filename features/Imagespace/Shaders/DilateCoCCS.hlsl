// IS-5 Pass 2: per-tile (16x16 half-res pixels) min/max CoC for blur early-out.

Texture2D<float>     CocIn   : register(t0);  // half-res signed CoC
RWTexture2D<float2>  TileOut : register(u0);  // /16 R16G16F: {minCoC, maxCoC}

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
    float  Pad0;
};

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 tileDim = max(uint2(1, 1), (HalfDimensions + 15u) / 16u);
    if (dtid.x >= tileDim.x || dtid.y >= tileDim.y) return;

    const int2 base = int2(dtid.xy) * 16;
    float minC = +1e30;
    float maxC = -1e30;
    [unroll]
    for (int y = 0; y < 16; ++y) {
        [unroll]
        for (int x = 0; x < 16; ++x) {
            const int2 p = base + int2(x, y);
            if (p.x < (int)HalfDimensions.x && p.y < (int)HalfDimensions.y) {
                const float c = CocIn.Load(int3(p, 0));
                minC = min(minC, c);
                maxC = max(maxC, c);
            }
        }
    }
    TileOut[dtid.xy] = float2(minC, maxC);
}
