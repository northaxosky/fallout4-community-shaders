// SSGI IL bounce injection compute.
//
// Reads the SH2-encoded indirect-lighting buffers produced by the SSGI v2 chain
// (texIlY = sh2-Y luminance, texIlCoCg = YCoCg chrominance), reconstructs an
// RGB irradiance value at the receiving pixel's view-space normal, and ADDS it
// into kDiffuseBuffer.
//
// Why no albedo multiplication here:
//   FO4's deferred_composite shader does `litColor = albedo * (kDiffuseBuffer + ...)`
//   at the very end. kDiffuseBuffer holds direct-lighting IRRADIANCE (no albedo).
//   Adding `ssgiIl` (without albedo) to kDiffuseBuffer makes the composite multiply
//   `albedo * ssgiIl` naturally, matching the upstream Skyrim CS semantics
//   (`linDiffuseColor += ssgiIl * linAlbedo`). Adding `ssgiIl * albedo` here would
//   cause a double-albedo at composite time.
//
// Coordinate frame:
//   The SSGI v2 chain produces SH coefficients in the basis used internally by
//   gi.cs.hlsl: a hemisphere-reconstructed view-space normal (z >= 0). With
//   CameraViewInverse currently fed as identity (matrix capture wires up in 2c.3),
//   gi.cs's "world-space" SH evaluation is effectively view-space. This consumer
//   matches that frame by decoding kGbufferNormal with the same hemisphere
//   reconstruction (mirrors features/ScreenSpaceGI/Shaders/SSGIv2/prefilterNormal.cs.hlsl
//   EncodeFO4Normal input). Once 2c.3 wires per-frame matrix capture, both producer
//   and consumer move to true world-space without changing the math here.

Texture2D<float4>   NormalTex    : register(t0);   // kGbufferNormal (R10G10B10A2 or equivalent, .rg = encoded view normal)
Texture2D<float4>   IlYTex       : register(t1);   // sh2-Y (R16G16B16A16_FLOAT)
Texture2D<float2>   IlCoCgTex    : register(t2);   // YCoCg chrominance (R16G16_FLOAT)
Texture2D<float4>   DiffuseLight : register(t3);   // kDiffuseBuffer (R11G11B10_FLOAT)
SamplerState        LinearClampSamp : register(s0);
RWTexture2D<float4> OutDiffuse   : register(u0);   // scratch full-res (copied back to kDiffuseBuffer)

cbuffer ApplyILCB : register(b0)
{
    uint2 ApplyDim;
    float ILStrength;
    float _Pad0;
};

static const float MATH_PI = 3.1415926535897932384626433832795f;

float SHHallucinateZH3Irradiance(float4 inSH, float3 direction)
{
    float3 zonalAxis = normalize(float3(inSH.w, inSH.y, inSH.z));
    float  ratio     = abs(dot(float3(-inSH.w, -inSH.y, inSH.z), zonalAxis));
    ratio /= inSH.x;
    float  zonalL2Coeff = inSH.x * (0.08f * ratio + 0.6f * ratio * ratio);
    float  fZ           = dot(zonalAxis, direction);
    float  zhDir        = sqrt(5.0f / (16.0f * MATH_PI)) * (3.0f * fZ * fZ - 1.0f);

    // cosine-lobe (clamped cosine) SH2 evaluation
    float4 cosLobe;
    cosLobe.x =  0.8862269254527580137f;
    cosLobe.y = -1.0233267079464884885f * direction.y;
    cosLobe.z =  1.0233267079464884885f * direction.z;
    cosLobe.w = -1.0233267079464884885f * direction.x;
    float result = dot(inSH, cosLobe);

    result += 0.25f * zonalL2Coeff * zhDir;
    return max(0.0, result);
}

float3 YCoCgToRGB(float3 ycocg)
{
    float y  = ycocg.x;
    float co = ycocg.y;
    float cg = ycocg.z;
    return float3(y + co - cg, y + cg, y - co - cg);
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= ApplyDim.x || dtid.y >= ApplyDim.y) return;

    // Hemisphere-reconstructed view-space normal (matches SSGI v2 prefilterNormal pattern).
    float2 nRaw = NormalTex.Load(int3(dtid.xy, 0)).xy;
    float2 nxy  = nRaw * 2.0f - 1.0f;
    float3 viewspaceNormal = float3(nxy, sqrt(saturate(1.0f - dot(nxy, nxy))));
    // Guard against NaN from edge / unwritten pixels (sky, etc).
    viewspaceNormal = normalize(viewspaceNormal);

    float4 ilY    = IlYTex.Load(int3(dtid.xy, 0));
    float2 ilCoCg = IlCoCgTex.Load(int3(dtid.xy, 0));

    // Skip pixels that hold zero SH data (sky, disocclusion, never-written).
    if (all(ilY == 0.0f) && all(ilCoCg == 0.0f)) {
        OutDiffuse[dtid.xy] = DiffuseLight.Load(int3(dtid.xy, 0));
        return;
    }

    float ssgiIlY = SHHallucinateZH3Irradiance(ilY, viewspaceNormal);
    float3 ssgiIl = max(0.0f, YCoCgToRGB(float3(ssgiIlY, ilCoCg)));
    ssgiIl *= ILStrength;

    float3 existing = DiffuseLight.Load(int3(dtid.xy, 0)).rgb;
    OutDiffuse[dtid.xy] = float4(existing + ssgiIl, 1.0f);
}
