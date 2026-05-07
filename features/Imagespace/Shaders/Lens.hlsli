// Vignette + Chromatic Aberration. Math closely follows FidelityFX Lens
// (extern/FidelityFX-SDK/sdk/include/FidelityFX/gpu/lens/ffx_lens.h, MIT).

// Radial darkening. centerPx = OutputDimensions / 2; intensity in [0,1].
float ApplyVignette(float2 px, float2 dims, float intensity)
{
    const float2 centred = (px / dims) * 2.0 - 1.0;
    const float  r2 = dot(centred, centred);
    return 1.0 - intensity * smoothstep(0.4, 1.6, r2);
}

// Channel-shifted sampling: R toward center, B away. Uses linear sampling so falloff
// is smooth across pixel boundaries.
float3 SampleWithCA(Texture2D<float4> tex, SamplerState samp, float2 uv, float2 dims, float intensity)
{
    const float2 centred = uv * 2.0 - 1.0;
    const float2 toCenter = -centred;
    const float  r = saturate(length(centred));
    const float  mag = intensity * r * r * 0.01;

    const float2 uvR = uv + toCenter * mag;
    const float2 uvB = uv - toCenter * mag;

    const float r_ch = tex.SampleLevel(samp, uvR, 0).r;
    const float g_ch = tex.SampleLevel(samp, uv,  0).g;
    const float b_ch = tex.SampleLevel(samp, uvB, 0).b;
    return float3(r_ch, g_ch, b_ch);
}
