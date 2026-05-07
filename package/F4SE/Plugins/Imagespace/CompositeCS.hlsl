// Tonemap + colour grading + bloom-add + adaptive exposure + lens stack.
// CAS-first on input scene (CAS-last would require resampling the math chain at 9 positions).

#include "Common.hlsli"
#include "Tonemap.hlsli"
#include "Lens.hlsli"
#include "Cas.hlsli"

Texture2D<float4>     InputColor         : register(t0);
Texture3D<float4>     LUT3D              : register(t1);
Texture2D<float4>     BloomTex           : register(t2);
Texture2D<float>      ExpoTex            : register(t3);
SamplerState          LinearClampSampler : register(s0);
RWTexture2D<float4>   OutputColor        : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 px = dtid.xy;
    if (px.x >= OutputDimensions.x || px.y >= OutputDimensions.y)
        return;

    const float2 uv = (float2(px) + 0.5) / float2(OutputDimensions);

    // 1. Sharpening on input scene (CAS-first).
    float3 c;
    if (SharpenEnable != 0)
        c = ApplyCAS(InputColor, int2(px), Sharpness);
    else
        c = InputColor.Load(int3(px, 0)).rgb;

    // 2. CA: resample the input scene at radial offsets to introduce fringing on bright edges.
    if (CAEnable != 0)
        c = SampleWithCA(InputColor, LinearClampSampler, uv, float2(OutputDimensions), CAIntensity);

    // 3. Operator path: linear domain.
    if (Operator != 0 || BloomEnable != 0) {
        float3 lin = SRGBToLinear(c);

        // Bloom-add (linear domain so blur sums sensibly).
        if (BloomEnable != 0) {
            const float3 bloom = BloomTex.SampleLevel(LinearClampSampler, uv, 0).rgb;
            lin += bloom * BloomIntensity;
        }

        // Combined exposure.
        float exposure = ExposureManual;
        if (AdaptiveExposureEnable != 0) {
            const float adapted = ExpoTex.Load(int3(0, 0, 0));
            const float clamped = clamp(adapted, ExposureMin, ExposureMax);
            exposure *= ExposureKey / max(clamped, 1e-5);
        }
        lin *= exposure;

        // Operator.
        if      (Operator == 1) lin = Tonemap_Hable(lin);
        else if (Operator == 2) lin = Tonemap_Reinhard(lin);
        else if (Operator == 3) lin = Tonemap_Lottes(lin);

        c = LinearToSRGB(lin);
    }

    // 4. LUT colour grading.
    if (LUTEnable != 0) {
        const float scale  = 31.0 / 32.0;
        const float offset = 0.5  / 32.0;
        const float3 uvw   = saturate(c) * scale + offset;
        const float3 graded = LUT3D.SampleLevel(LinearClampSampler, uvw, 0).rgb;
        c = lerp(c, graded, LUTStrength);
    }

    // 5. Vignette.
    if (VignetteEnable != 0)
        c *= ApplyVignette(float2(px), float2(OutputDimensions), VignetteIntensity);

    OutputColor[px] = float4(c, 1.0);
}
