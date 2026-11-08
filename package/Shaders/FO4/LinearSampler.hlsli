#ifndef FO4_LINEAR_SAMPLER_HLSLI
#define FO4_LINEAR_SAMPLER_HLSLI

// FO4: the host binds one linear clamp sampler for terrain, VSM and local shadow lookups.
SamplerState LinearSampler : register(s13);
#endif
