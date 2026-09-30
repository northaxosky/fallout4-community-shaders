#ifndef FO4_DYNAMIC_CUBEMAPS_CONSUMER_HLSLI
#define FO4_DYNAMIC_CUBEMAPS_CONSUMER_HLSLI

#include "Common/Color.hlsli"
#include "FO4/FO4ShaderData.hlsli"
// FO4 consumers supply native samplers rather than Skyrim's global color sampler.
#define DYNAMIC_CUBEMAPS_CUSTOM_CONSUMER
#include "DynamicCubemaps/DynamicCubemaps.hlsli"
#undef DYNAMIC_CUBEMAPS_CUSTOM_CONSUMER

#endif
