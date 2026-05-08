// XeGTAO-derived horizon-search AO. Half-res output. Phase 1: AO only, no IL, no temporal.
// Reference: Intel XeGTAO https://github.com/GameTechDev/XeGTAO (MIT). Algorithm core only.

#include "Common.hlsli"

Texture2D<float>     DepthPyramid : register(t0);   // R16F linearised view-Z, mips 0..4
Texture2D<float2>    GbufferNormal : register(t1);  // R16G16_UNORM, view-space octahedral
SamplerState         PointSamp : register(s0);
RWTexture2D<float>   AOOut : register(u0);          // R8_UNORM half-res

float3 NDCToView(float2 ndc, float vz)
{
    return float3(ndc * NDCToViewMul.xy + NDCToViewAdd.xy, 1.0) * vz;
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= AODim.x || dtid.y >= AODim.y) return;

    // Sample the pyramid mip 0 (half-res) for our pixel's depth.
    const float vz = DepthPyramid.Load(int3(dtid.xy, 0));
    if (vz <= NearClip || vz >= FarClip * 0.99) {
        AOOut[dtid.xy] = 1.0;
        return;
    }

    // Read view-space normal from full-res gbuffer (lookup at full-res coord).
    const int2 fullCoord = int2(dtid.xy) * 2;
    const float2 nEnc = GbufferNormal.Load(int3(fullCoord, 0));
    const float3 N = DecodeViewNormal(nEnc);

    // Reconstruct view-space position.
    const float2 uv = (float2(dtid.xy) + 0.5) / float2(AODim);
    const float2 ndc = uv * 2.0 - 1.0;
    const float3 P = NDCToView(float2(ndc.x, -ndc.y), vz);
    const float3 V = normalize(-P);

    // Spatial noise: 4x4 Bayer-like pattern using bit interleaving.
    const uint  noiseX = (dtid.x ^ (dtid.x >> 1)) & 3u;
    const uint  noiseY = (dtid.y ^ (dtid.y >> 1)) & 3u;
    const float sliceJitter = float(noiseX) * (1.0 / 4.0);
    const float stepJitter  = float(noiseY) * (1.0 / 4.0);

    // Screen-space sampling radius: project AORadius (world units) onto screen at this depth.
    const float screenRadiusPx = AORadius / max(vz, 1e-3) * 0.5 * float(AODim.x);
    const float maxRadius = clamp(screenRadiusPx, 4.0, 64.0);

    const float invSliceCount = 1.0 / float(SliceCount);
    const float invStepCount  = 1.0 / float(max(StepCount, 1u));

    float visibility = 0.0;

    [unroll(8)] for (uint slice = 0; slice < SliceCount; ++slice) {
        const float sliceK = (float(slice) + sliceJitter) * invSliceCount;
        const float angle = sliceK * 3.14159265;
        const float2 sliceDir = float2(cos(angle), sin(angle));

        // horizons.x = max cosine for - sliceDir direction (becomes negative-angle bound h1).
        // horizons.y = max cosine for + sliceDir direction (becomes positive-angle bound h2).
        float2 horizons = float2(-1.0, -1.0);

        [unroll(16)] for (uint step = 1; step <= StepCount; ++step) {
            const float t = (float(step) + stepJitter) * invStepCount;
            const float radius = t * maxRadius;

            // Pick mip based on radius for cache efficiency.
            const int mip = clamp(int(log2(radius / 4.0)), 0, 4);

            // + direction → updates h2 (positive horizon)
            {
                const int2 spx = int2(round(float2(dtid.xy) + sliceDir * radius));
                const int2 mipPx = spx >> mip;
                if (all(mipPx >= 0) && all(mipPx < int2(AODim) >> mip)) {
                    const float vz_s = DepthPyramid.Load(int3(mipPx, mip));
                    const float3 Ps = NDCToView(float2(((float(spx.x) + 0.5) / float(AODim.x)) * 2.0 - 1.0,
                                                       -(((float(spx.y) + 0.5) / float(AODim.y)) * 2.0 - 1.0)), vz_s);
                    const float3 D  = Ps - P;
                    const float  d  = length(D);
                    if (d < AORadius && abs(D.z) < Thickness) {
                        const float h = dot(D, V) / max(d, 1e-3);
                        horizons.y = max(horizons.y, h);
                    }
                }
            }
            // - direction → updates h1 (negative horizon)
            {
                const int2 spx = int2(round(float2(dtid.xy) - sliceDir * radius));
                const int2 mipPx = spx >> mip;
                if (all(mipPx >= 0) && all(mipPx < int2(AODim) >> mip)) {
                    const float vz_s = DepthPyramid.Load(int3(mipPx, mip));
                    const float3 Ps = NDCToView(float2(((float(spx.x) + 0.5) / float(AODim.x)) * 2.0 - 1.0,
                                                       -(((float(spx.y) + 0.5) / float(AODim.y)) * 2.0 - 1.0)), vz_s);
                    const float3 D  = Ps - P;
                    const float  d  = length(D);
                    if (d < AORadius && abs(D.z) < Thickness) {
                        const float h = dot(D, V) / max(d, 1e-3);
                        horizons.x = max(horizons.x, h);
                    }
                }
            }
        }

        // Project the slice's horizon angles against the surface normal.
        const float3 sliceTangent = float3(sliceDir, 0.0);
        const float3 sliceNormal = cross(sliceTangent, V);
        const float3 projN = N - sliceNormal * dot(N, sliceNormal);
        const float  projLen = length(projN);
        if (projLen < 1e-4) {
            visibility += 1.0;
            continue;
        }
        const float3 projNn = projN / projLen;
        const float  cosN = clamp(dot(projNn, V), -1.0, 1.0);
        // Signed normal angle: positive if projN tilts toward +sliceDir, negative toward -sliceDir.
        const float  nSign = sign(dot(projNn, sliceTangent));
        const float  nAng = nSign * acos(cosN);
        // Clamp horizons within [n - pi/2, n + pi/2]
        const float h1 = -acos(clamp(horizons.x, -1.0, 1.0));
        const float h2 =  acos(clamp(horizons.y, -1.0, 1.0));
        const float h1c = max(h1, nAng - 1.5707963);
        const float h2c = min(h2, nAng + 1.5707963);
        // Visibility integral over [-pi/2, pi/2] of cos(angle - n) bounded by horizons.
        // 0.5 factor matches Intel XeGTAO; was 0.25 (half-strength bug) in initial port.
        const float v = 0.5 * (
            -cos(2.0 * h1c - nAng) + cos(nAng) + 2.0 * h1c * sin(nAng) +
            -cos(2.0 * h2c - nAng) + cos(nAng) + 2.0 * h2c * sin(nAng)
        );
        visibility += v * projLen;
    }

    visibility = saturate(visibility * invSliceCount);
    AOOut[dtid.xy] = pow(visibility, AOPower);
}
