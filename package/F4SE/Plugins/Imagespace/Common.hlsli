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
};

// Approximation: pure power, not the piecewise sRGB curve. Adequate for grading work.
float3 SRGBToLinear(float3 c) { return pow(saturate(c), 2.2); }
float3 LinearToSRGB(float3 c) { return pow(saturate(c), 1.0 / 2.2); }
