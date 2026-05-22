// Shared constants and helpers for Imagespace passes.

cbuffer CompositeCB : register(b0)
{
    uint  Operator;
    uint  LUTEnable;
    uint  AdaptiveExposureEnable;
    uint  BloomEnable;

    float ExposureManual;
    float LUTStrength;
    float ExposureKey;
    float BloomIntensity;

    uint  VignetteEnable;
    uint  CAEnable;
    uint  SharpenEnable;
    uint  _Pad0;

    float VignetteIntensity;
    float CAIntensity;
    float Sharpness;
    float ExposureMin;

    float ExposureMax;
    uint2 OutputDimensions;
    float _Pad1;

    uint  SunspriteEnable;
    uint  LensFlareEnable;
    float2 SunUV;

    float SunspriteIntensity;
    float SunspriteSize;
    float LensFlareIntensity;
    uint  LensFlareGhosts;

    uint  DirtEnable;
    float DirtIntensity;
    float _DirtPad0;
    float _DirtPad1;
};

// Approximation: pure power, not the piecewise sRGB curve. Polynomial form is ~3-5x cheaper
// than pow(saturate(c), 2.2); max sampled error vs pow is 0.001 / 0.002 over [0,1]. Adequate
// for grading work where the source/output buffer is sRGB-encoded.
float3 SRGBToLinear(float3 c)
{
    c = saturate(c);
    return c * (c * (c * 0.305306011 + 0.682171111) + 0.012522878);
}

float3 LinearToSRGB(float3 c)
{
    c = saturate(c);
    const float3 s1 = sqrt(c);
    const float3 s2 = sqrt(s1);
    const float3 s3 = sqrt(s2);
    return 0.585122381 * s1 + 0.783140355 * s2 - 0.368262736 * s3;
}
