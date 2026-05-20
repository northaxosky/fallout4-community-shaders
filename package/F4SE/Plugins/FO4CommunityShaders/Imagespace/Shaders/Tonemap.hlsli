// Three closed-form tonemap operators. Inputs/outputs are in linear domain.

// Hable filmic (Uncharted 2 piecewise). See filmicworlds.com (John Hable, 2010).
float3 HableCurve(float3 x)
{
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
float3 Tonemap_Hable(float3 c)
{
    const float W = 11.2;
    return HableCurve(c * 2.0) / HableCurve(W.xxx);
}

// Reinhard extended with white-point. SIGGRAPH 2002.
float3 Tonemap_Reinhard(float3 c)
{
    const float W = 11.2;
    return c * (1.0 + c / (W * W)) / (1.0 + c);
}

// Lottes parametric. GDC 2016 ("Advanced Techniques and Optimization of HDR Color Pipelines").
float3 Tonemap_Lottes(float3 x)
{
    const float a       = 1.6;
    const float d       = 0.977;
    const float hdrMax  = 8.0;
    const float midIn   = 0.18;
    const float midOut  = 0.267;

    const float ad     = a * d;
    const float midPow = pow(midIn, a);
    const float b = (-midPow + pow(hdrMax, ad) * midOut) /
                    ((pow(hdrMax, ad) - midPow) * midOut);
    const float c = (pow(hdrMax, ad) * midPow - pow(hdrMax, a) * midPow * midOut) /
                    ((pow(hdrMax, ad) - midPow) * midOut);

    return pow(x, a) / (pow(x, ad) * b + c);
}
