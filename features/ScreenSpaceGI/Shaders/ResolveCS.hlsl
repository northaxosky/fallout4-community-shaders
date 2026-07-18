cbuffer ResolveCB : register(b0)
{
    uint2 gExtent;
    uint2 gOrigin;
    uint gFrameIndex;
    uint3 _pad;
};

RWTexture2D<float4> gBounce : register(u0);
RWTexture2D<float4> gAO : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gExtent.x || id.y >= gExtent.y) return;
    int2 p = int2(id.xy);
    gAO[p] = float4(1, 1, 1, 1);
    gBounce[p] = float4(0, 0, 0, 0);
}
