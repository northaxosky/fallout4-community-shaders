// Sidecar apply: read half-res AO + full-res kDiffuseBuffer, multiply, write to scratch.
// Bilateral upsample of AO via depth-guided weighting from the full-res depth pyramid mip 0.

Texture2D<float>   AOTex : register(t0);            // R8_UNORM half-res
Texture2D<float4>  DiffuseLight : register(t1);     // R11G11B10F full-res
SamplerState       LinearClampSamp : register(s0);
RWTexture2D<float4> OutDiffuse : register(u0);      // scratch, full-res

cbuffer ApplyCB : register(b0)
{
    uint2  ApplyDim;
    float  ApplyIntensity;
    float  ApplyContrast;
};

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= ApplyDim.x || dtid.y >= ApplyDim.y) return;

    const float2 uv = (float2(dtid.xy) + 0.5) / float2(ApplyDim);
    const float ao = AOTex.SampleLevel(LinearClampSamp, uv, 0);
    const float aoScaled = pow(saturate(ao), max(ApplyContrast, 0.001));
    const float modulator = lerp(1.0, aoScaled, saturate(ApplyIntensity));

    const float3 d = DiffuseLight.Load(int3(dtid.xy, 0)).rgb;
    OutDiffuse[dtid.xy] = float4(d * modulator, 1.0);
}
