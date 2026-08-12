cbuffer BounceTelemetryCB : register(b0)
{
    uint2 gSourceExtent;
    uint2 gOutputExtent;
};

Texture2D<float4> gBounce : register(t0);
RWTexture2D<float4> gStats : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gOutputExtent)) return;

    uint2 begin = id.xy * gSourceExtent / gOutputExtent;
    uint2 end = (id.xy + 1) * gSourceExtent / gOutputExtent;
    float sum = 0;
    float maximum = 0;
    float nonzero = 0;
    float count = 0;

    for (uint y = begin.y; y < end.y; ++y) {
        for (uint x = begin.x; x < end.x; ++x) {
            float3 rgb = max(0, gBounce.Load(int3(uint2(x, y), 0)).rgb);
            if (!all(isfinite(rgb))) rgb = 0;
            float luminance = dot(rgb, float3(0.2126, 0.7152, 0.0722));
            sum += luminance;
            maximum = max(maximum, luminance);
            nonzero += luminance > 1e-6 ? 1.0 : 0.0;
            count += 1.0;
        }
    }

    gStats[id.xy] = float4(sum, maximum, nonzero, count);
}
