// Reads the tail mip of the log-luma pyramid (1x1, log2 luma) and the previous adapted
// exposure scalar; writes the new EMA-smoothed scalar into the ping-pong target.

Texture2D<float>    LumPyramidTail  : register(t0);
Texture2D<float>    ExpoPrev        : register(t1);
RWTexture2D<float>  ExpoNext        : register(u0);

cbuffer ExposureCB : register(b0)
{
    float DeltaTime;       // seconds since last frame
    float Tau;             // EMA time constant in seconds (fAdaptationSpeed)
    uint  TailMipIdx;
    uint  _Pad0;
};

[numthreads(1, 1, 1)]
void main()
{
    const float logLuma = LumPyramidTail.Load(int3(0, 0, int(TailMipIdx)));
    const float curLuma = exp2(logLuma);
    const float prev    = ExpoPrev.Load(int3(0, 0, 0));

    // EMA: alpha = 1 - exp(-dt/tau). When tau is small relative to dt, alpha approaches 1
    // (instant snap); when tau >> dt, alpha approaches 0 (slow blend).
    const float alpha = 1.0 - exp(-DeltaTime / max(Tau, 1e-3));
    const float next  = lerp(prev, curLuma, alpha);

    ExpoNext[uint2(0, 0)] = next;
}
