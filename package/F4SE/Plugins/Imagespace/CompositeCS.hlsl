// Tonemap + 3D LUT colour-grading pass. Reads kFrameBuffer (already engine-tonemapped LDR sRGB),
// regrades it via an operator + optional LUT, writes a same-format scratch UAV.

#include "Common.hlsli"
#include "Tonemap.hlsli"

Texture2D<float4>     InputColor         : register(t0);
Texture3D<float4>     LUT3D              : register(t1);
SamplerState          LinearClampSampler : register(s0);
RWTexture2D<float4>   OutputColor        : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 px = dtid.xy;
    if (px.x >= OutputDimensions.x || px.y >= OutputDimensions.y)
        return;

    float3 c = InputColor.Load(int3(px, 0)).rgb;

    if (Operator != 0) {
        c = SRGBToLinear(c) * Exposure;
        if      (Operator == 1) c = Tonemap_Hable(c);
        else if (Operator == 2) c = Tonemap_Reinhard(c);
        else if (Operator == 3) c = Tonemap_Lottes(c);
        c = LinearToSRGB(c);
    }

    if (LUTEnable != 0) {
        const float scale  = 31.0 / 32.0;
        const float offset = 0.5  / 32.0;
        const float3 uvw   = saturate(c) * scale + offset;
        const float3 graded = LUT3D.SampleLevel(LinearClampSampler, uvw, 0).rgb;
        c = lerp(c, graded, LUTStrength);
    }

    OutputColor[px] = float4(c, 1.0);
}
