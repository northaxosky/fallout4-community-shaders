// Contrast Adaptive Sharpening from FidelityFX CAS (MIT); 3x3 no-scale path, sharpness [0,1].

float3 ApplyCAS(Texture2D<float4> tex, int2 px, float sharpness)
{
    // Cross-shaped 3x3 CAS neighborhood; corners are unused in this path.
    const float3 b = tex.Load(int3(px + int2( 0, -1), 0)).rgb;
    const float3 d = tex.Load(int3(px + int2(-1,  0), 0)).rgb;
    const float3 e = tex.Load(int3(px,                0)).rgb;
    const float3 f = tex.Load(int3(px + int2( 1,  0), 0)).rgb;
    const float3 h = tex.Load(int3(px + int2( 0,  1), 0)).rgb;

    // Cross (BDFEH) min/max per channel.
    const float3 mn = min(min(min(b, d), min(e, f)), h);
    const float3 mx = max(max(max(b, d), max(e, f)), h);

    // amplify = sqrt( saturate( min(mn, 1 - mx) * (1 / mx) ) )
    const float3 inv_mx = 1.0 / max(mx, 1e-5);
    const float3 amplify = sqrt(saturate(min(mn, 1.0 - mx) * inv_mx));

    // peak = -1 / lerp(8, 5, sharpness)  (FFX const1.x)
    const float peak = -1.0 / lerp(8.0, 5.0, saturate(sharpness));

    // Cross filter uses green-channel weight; FFX dead-code-removes per-channel weights.
    const float  w = amplify.g * peak;
    const float  invW = 1.0 / (1.0 + 4.0 * w);

    return saturate((b * w + d * w + f * w + h * w + e) * invW);
}
