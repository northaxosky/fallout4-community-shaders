#include "Common/Color.hlsli"
#include "Common/SphericalHarmonics.hlsli"

cbuffer ResolveCB : register(b0)
{
    uint2 gExtent;
    uint2 gOrigin;
    uint gFrameIndex;
    uint gHasAO;
    float gAoPower;
    uint gHasBounce;
    float gBounceStrength;
    float3 _pad;
    float4 gViewToWorld[3]; // Raw Ni camera rows: forward, up, right.
};

Texture2D<float> gAoRaw : register(t0); // XeGTAO occlusion: 0 open, 1 occluded
Texture2D<float4> gBounceSH : register(t1);
Texture2D<float2> gBounceCoCg : register(t2);
Texture2D<float3> gViewNormal : register(t3);
Texture2D<float4> gAlbedo : register(t4);
RWTexture2D<float4> gBounce : register(u0);
RWTexture2D<float4> gAO : register(u1);

float3 ViewToWorldDirection(float3 direction)
{
    float3 directionNi = direction.zyx;
    return normalize(
        directionNi.x * gViewToWorld[0].xyz +
        directionNi.y * gViewToWorld[1].xyz +
        directionNi.z * gViewToWorld[2].xyz);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gExtent.x || id.y >= gExtent.y) return;
    int2 p = int2(id.xy);
    float vis = 1.0;
    if (gHasAO != 0) {
        float occ = gAoRaw.Load(int3(p, 0));
        vis = pow(saturate(1.0 - occ), gAoPower);
    }
    gAO[p] = float4(vis, vis, vis, 1.0);

    float3 bounce = 0;
    if (gHasBounce != 0) {
        float4 shY = gBounceSH.Load(int3(p, 0));
        if (shY.x > 1e-6) {
            float2 coCg = gBounceCoCg.Load(int3(p, 0));
            float3 normalWS = ViewToWorldDirection(normalize(gViewNormal.Load(int3(p, 0))));
            float irradianceY = SphericalHarmonics::SHHallucinateZH3Irradiance(shY, normalWS);
            float3 irradiance = max(0, Color::YCoCgToRGB(float3(irradianceY, coCg)));
            float3 albedo = saturate(gAlbedo.Load(int3(p, 0)).rgb);
            bounce = irradiance * albedo * max(0, gBounceStrength);
        }
    }
    gBounce[p] = float4(bounce, 1.0);
}
