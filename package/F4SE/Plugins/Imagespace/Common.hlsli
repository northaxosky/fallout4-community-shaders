// Shared constants and helpers for Imagespace passes.

cbuffer CompositeCB : register(b0)
{
    uint  Operator;
    uint  LUTEnable;
    float Exposure;
    float LUTStrength;
    uint2 OutputDimensions;
    uint2 _Pad0;
};

// Approximation: pure power, not the piecewise sRGB curve. Adequate for grading work.
float3 SRGBToLinear(float3 c) { return pow(saturate(c), 2.2); }
float3 LinearToSRGB(float3 c) { return pow(saturate(c), 1.0 / 2.2); }
