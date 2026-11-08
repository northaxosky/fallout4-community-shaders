#ifndef FO4_SHADOW_SAMPLING_CONSUMER
#define FO4_SHADOW_SAMPLING_CONSUMER

#include "FO4/FO4ShaderData.hlsli"
#include "FO4/LinearSampler.hlsli"

// FO4: clouds follow the drawn sun disc, not the clamped light.
#define CLOUD_SHADOWS_SUN_DIRECTION FO4SharedData::SunDiscDirection.xyz
#include "Common/ShadowSampling.hlsli"
#endif
