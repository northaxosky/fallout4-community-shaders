cbuffer ResolveCB : register(b0)
{
    uint2 gExtent;
    uint2 gOrigin;
    uint gFrameIndex;
    uint gHasAO;
    uint2 _pad;
};

Texture2D<float> gAoRaw : register(t0); // XeGTAO occlusion: 0 open, 1 occluded
RWTexture2D<float4> gBounce : register(u0);
RWTexture2D<float4> gAO : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gExtent.x || id.y >= gExtent.y) return;
    int2 p = int2(id.xy);
    float vis = 1.0;
    if (gHasAO != 0) {
        float occ = gAoRaw.Load(int3(p, 0));
        vis = saturate(1.0 - occ);
    }
    gAO[p] = float4(vis, vis, vis, 1.0);
    gBounce[p] = float4(0.0, 0.0, 0.0, 0.0);
}
