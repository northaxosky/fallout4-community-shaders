#ifndef FO4_LINEAR_SAMPLER_HLSLI
#define FO4_LINEAR_SAMPLER_HLSLI

// FO4: the host binds one linear clamp sampler at s13.
SamplerState LinearSampler : register(s13);
#endif
