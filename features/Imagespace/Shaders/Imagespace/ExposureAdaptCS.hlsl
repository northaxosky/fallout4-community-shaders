// EMA-smooth the 1x1 log-luma tail against the previous exposure scalar.

Texture2D<float>    LumPyramidTail  : register(t0);
Texture2D<float>    ExpoPrev        : register(t1);
RWTexture2D<float>  ExpoNext        : register(u0);

cbuffer ExposureCB : register(b0)
{
    float DeltaTime;       // seconds since last frame
    float TauUp;           // EMA time constant when adapting to a brighter scene (fAdaptationSpeedUp)
    float TauDown;         // EMA time constant when adapting to a darker scene  (fAdaptationSpeedDown)
    uint  TailMipIdx;
};

[numthreads(1, 1, 1)]
void main()
{
    const float logLuma = LumPyramidTail.Load(int3(0, 0, int(TailMipIdx)));
    const float curLuma = exp2(logLuma);
    const float prev    = ExpoPrev.Load(int3(0, 0, 0));

    // Asymmetric EMA: brighten fast to avoid cell-exit flashes, darken slowly.
    const float tau   = (curLuma > prev) ? TauUp : TauDown;
    const float alpha = 1.0 - exp(-DeltaTime / max(tau, 1e-3));
    const float next  = lerp(prev, curLuma, alpha);

    ExpoNext[uint2(0, 0)] = next;
}
