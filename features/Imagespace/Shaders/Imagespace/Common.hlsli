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

    uint   LensFlareEnable;
    uint   LensFlareGhosts;
    float  LensFlareIntensity;
    float  _LensPad0;

    float2 SunUV;
    uint   DirtEnable;
    float  DirtIntensity;
};

// Cheap sRGB approximation: ~3-5x faster than pow, max sampled error ~0.002 over [0,1].
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
