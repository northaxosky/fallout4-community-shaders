#ifndef TARGET_COMPONENTS
#define TARGET_COMPONENTS 4
#endif

cbuffer OverwriteCB : register(b0)
{
    uint2 gTargetExtent;
    uint2 gSourceExtent;
    uint gMode;
    uint3 _pad;
};

Texture2D<float4> gEngineAO : register(t0);
Texture2D<float4> gOurAO : register(t1);

#if TARGET_COMPONENTS == 1
#define TARGET_TYPE float
TARGET_TYPE PackTarget(float4 value) { return value.x; }
#elif TARGET_COMPONENTS == 2
#define TARGET_TYPE float2
TARGET_TYPE PackTarget(float4 value) { return value.xy; }
#elif TARGET_COMPONENTS == 3
#define TARGET_TYPE float3
TARGET_TYPE PackTarget(float4 value) { return value.xyz; }
#else
#define TARGET_TYPE float4
TARGET_TYPE PackTarget(float4 value) { return value; }
#endif

RWTexture2D<TARGET_TYPE> gTarget : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gTargetExtent)) return;

    uint2 sourcePixel = min(
        id.xy * gSourceExtent / gTargetExtent,
        gSourceExtent - 1);
    float ao = gOurAO.Load(int3(sourcePixel, 0)).x;
    float4 result = ao.xxxx;
    if (gMode == 2) {
        result = min(gEngineAO.Load(int3(id.xy, 0)), result);
    } else if (gMode == 3) {
        // Diagnostic half-split: left half occluded (0), right half unoccluded (1).
        // A razor-sharp vertical seam in the ambient term that weather cannot produce,
        // proving whether the composite samples this buffer.
        result = (id.x < (gTargetExtent.x / 2u)) ? float4(0.0, 0.0, 0.0, 0.0) : float4(1.0, 1.0, 1.0, 1.0);
    }
    gTarget[id.xy] = PackTarget(result);
}
