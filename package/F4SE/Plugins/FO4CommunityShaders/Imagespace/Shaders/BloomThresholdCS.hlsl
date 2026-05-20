// Soft-knee threshold + 2x downsample into bloomChain[0] (R11G11B10F, linear).

Texture2D<float4>     InputColor : register(t0);
RWTexture2D<float4>   OutputBloom : register(u0);

cbuffer ThresholdCB : register(b0)
{
    float Threshold;
    float SoftKnee;
    uint2 OutputDimensions;
};

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const uint2 px = dtid.xy;
    if (px.x >= OutputDimensions.x || px.y >= OutputDimensions.y)
        return;

    // 2x box average of source.
    const int2 src = int2(px) * 2;
    float3 c = 0.25 * (
        InputColor.Load(int3(src + int2(0, 0), 0)).rgb +
        InputColor.Load(int3(src + int2(1, 0), 0)).rgb +
        InputColor.Load(int3(src + int2(0, 1), 0)).rgb +
        InputColor.Load(int3(src + int2(1, 1), 0)).rgb
    );

    c = pow(saturate(c), 2.2);

    // Soft-knee threshold (Karis). brightness = max(rgb).
    const float brightness = max(c.r, max(c.g, c.b));
    const float knee = max(SoftKnee, 1e-5);
    const float soft = clamp(brightness - Threshold + knee, 0.0, 2.0 * knee);
    const float weight = max(brightness - Threshold, soft * soft / (4.0 * knee + 1e-5))
                       / max(brightness, 1e-5);

    OutputBloom[px] = float4(c * weight, 1.0);
}
