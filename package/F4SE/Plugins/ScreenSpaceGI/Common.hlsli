// Shared helpers for SSGI (AO-only variant; indirect-lighting not yet implemented).

cbuffer SSGI_CB : register(b0)
{
    uint2  FrameDim;
    uint2  AODim;
    float  NearClip;
    float  FarClip;
    uint   SliceCount;
    uint   StepCount;
    float  AORadius;
    float  AOPower;
    float  Thickness;
    float  _Pad0;
    float4 NDCToViewMul;
    float4 NDCToViewAdd;
};

float LinearizeDepth(float ndc, float zn, float zf)
{
    return zn * zf / (zf - ndc * (zf - zn));
}

float3 DecodeViewNormal(float2 enc)
{
    float2 nxy = enc * 2.0 - 1.0;
    float  z2 = saturate(1.0 - dot(nxy, nxy));
    return float3(nxy, sqrt(z2));
}
