// Vignette + chromatic aberration, based on FidelityFX Lens (MIT).

// Radial darkening. centerPx = OutputDimensions / 2; intensity in [0,1].
float ApplyVignette(float2 px, float2 dims, float intensity)
{
    const float2 centered = (px / dims) * 2.0 - 1.0;
    const float  r2 = dot(centered, centered);
    return 1.0 - intensity * smoothstep(0.4, 1.6, r2);
}

// Channel-shifted sampling: R toward center, B away; linear sampling smooths falloff.
float3 SampleWithCA(Texture2D<float4> tex, SamplerState samp, float2 uv, float2 dims, float intensity)
{
    const float2 centered = uv * 2.0 - 1.0;
    const float2 toCenter = -centered;
    const float  r2 = saturate(dot(centered, centered));
    const float  mag = intensity * r2 * 0.01;

    const float2 uvR = uv + toCenter * mag;
    const float2 uvB = uv - toCenter * mag;

    const float r_ch = tex.SampleLevel(samp, uvR, 0).r;
    const float g_ch = tex.SampleLevel(samp, uv,  0).g;
    const float b_ch = tex.SampleLevel(samp, uvB, 0).b;
    return float3(r_ch, g_ch, b_ch);
}

// Up to 7 lens-flare ghosts along the sun-to-screen-center line.
float3 ApplyLensFlare(float2 uv, float2 sunUVNDC, float intensity, uint ghostCount)
{
    const float2 sunUV = float2(sunUVNDC.x * 0.5 + 0.5, 1.0 - (sunUVNDC.y * 0.5 + 0.5));
    const float2 center = float2(0.5, 0.5);
    const float2 dir = center - sunUV;
    const float  flareLen = length(dir);
    if (flareLen < 1e-4) return float3(0, 0, 0);  // sun exactly on center: no flare

    static const float kPositions[7] = { 0.6, 1.4, 1.9, 2.4, 0.4, 0.9, 1.6 };
    static const float kScales[7]    = { 0.20, 0.60, 0.40, 0.30, 1.10, 0.25, 0.45 };
    static const float3 kTints[7] = {
        float3(1.0, 0.4, 0.2),
        float3(0.3, 0.7, 1.0),
        float3(1.0, 0.9, 0.5),
        float3(0.6, 0.4, 1.0),
        float3(1.0, 0.6, 0.6),
        float3(0.5, 1.0, 0.7),
        float3(1.0, 0.8, 0.3)
    };

    float3 accum = 0.0;
    const uint count = min(ghostCount, 7u);
    for (uint i = 0; i < count; ++i) {
        const float2 ghost = sunUV + dir * kPositions[i];
        const float2 d = uv - ghost;
        const float  r2 = dot(d, d);
        const float  radius = 0.012 * kScales[i] * (1.0 + flareLen * 0.5);
        const float  sigma = max(radius * radius, 1e-6);
        accum += kTints[i] * exp(-r2 / sigma);
    }

    // Isotropic edge fade as sunUV leaves [0,1].
    const float2 fadeXY = saturate(1.0 - 2.0 * abs(sunUV - 0.5));
    const float  edgeFade = min(fadeXY.x, fadeXY.y);
    return accum * intensity * edgeFade;
}
