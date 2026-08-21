// Tonemap, grading, bloom, exposure, lens, and CAS-first sharpening.

#include "Common.hlsli"
#include "Tonemap.hlsli"
#include "Lens.hlsli"
#include "Cas.hlsli"

Texture2D<float4>     InputColor         : register(t0);
Texture3D<float4>     LUT3D              : register(t1);
Texture2D<float4>     BloomTex           : register(t2);
Texture2D<float>      ExpoTex            : register(t3);
Texture2D<float>      DirtTex            : register(t4);
SamplerState          LinearClampSampler : register(s0);
RWTexture2D<float4>   OutputColor        : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 px = dtid.xy;
    if (px.x >= OutputDimensions.x || px.y >= OutputDimensions.y)
        return;

    const float2 uv = (float2(px) + 0.5) / float2(OutputDimensions);

    // CA resamples InputColor, so skip CAS when CA is active.
    float3 c;
    if (CAEnable != 0)
        c = SampleWithCA(InputColor, LinearClampSampler, uv, float2(OutputDimensions), CAIntensity);
    else if (SharpenEnable != 0)
        c = ApplyCAS(InputColor, int2(px), Sharpness);
    else
        c = InputColor.Load(int3(px, 0)).rgb;

    const bool dirtInFrame  = DirtEnable != 0      && abs(SunUV.x) < 1.5 && abs(SunUV.y) < 1.5;
    const bool flareInFrame = LensFlareEnable != 0 && abs(SunUV.x) < 1.5 && abs(SunUV.y) < 1.5;
    const bool useLinearPath = Operator != 0 || BloomEnable != 0 || flareInFrame || dirtInFrame;
    if (useLinearPath) {
        // kFrameBuffer is linear HDR; do NOT sRGB-decode before linear math.
        float3 lin = max(c, 0.0);

        // Add bloom in linear HDR.
        if (BloomEnable != 0) {
            const float3 bloom = BloomTex.SampleLevel(LinearClampSampler, uv, 0).rgb;
            lin += bloom * BloomIntensity;
        }

        // Lens flare in linear HDR domain so tonemap compresses ghost peaks like any bright source.
        if (flareInFrame) {
            lin += ApplyLensFlare(uv, SunUV, LensFlareIntensity, LensFlareGhosts);
        }

        if (DirtEnable != 0) {
            const float2 sunSentinel = float2(SunUV.x, SunUV.y);
            const bool   sunVisible  = abs(sunSentinel.x) < 1.5 && abs(sunSentinel.y) < 1.5;
            const float  sunGlow     = sunVisible
                                     ? saturate(1.0 - dot(sunSentinel, sunSentinel) * 0.25)
                                     : 0.0;
            const float  mask        = DirtTex.SampleLevel(LinearClampSampler, uv, 0).r;
            lin += mask * sunGlow * DirtIntensity;
        }

        float exposure = ExposureManual;
        if (AdaptiveExposureEnable != 0) {
            const float adapted = ExpoTex.Load(int3(0, 0, 0));
            const float clamped = clamp(adapted, ExposureMin, ExposureMax);
            exposure *= ExposureKey / max(clamped, 1e-5);
        }
        lin *= exposure;

        if      (Operator == 1) lin = Tonemap_Hable(lin);
        else if (Operator == 2) lin = Tonemap_Reinhard(lin);
        else if (Operator == 3) lin = Tonemap_Lottes(lin);

        // Write back in the same linear domain we read; the engine owns final display encoding.
        c = lin;
    }

    // LUT color grading.
    if (LUTEnable != 0) {
        const float scale  = 31.0 / 32.0;
        const float offset = 0.5  / 32.0;
        const float3 uvw   = saturate(c) * scale + offset;
        const float3 graded = LUT3D.SampleLevel(LinearClampSampler, uvw, 0).rgb;
        c = lerp(c, graded, LUTStrength);
    }

    // Vignette.
    if (VignetteEnable != 0)
        c *= ApplyVignette(float2(px), float2(OutputDimensions), VignetteIntensity);

    OutputColor[px] = float4(c, 1.0);
}
