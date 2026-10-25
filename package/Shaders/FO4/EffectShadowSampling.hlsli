#ifndef FO4_EFFECT_SHADOW_SAMPLING
#define FO4_EFFECT_SHADOW_SAMPLING

#include "FO4/FO4ShaderData.hlsli"

// FO4: no per-draw descriptor or radius carrier; lookups read fixed state.
namespace Permutation
{
	namespace ExtraFlags
	{
		static const uint InWorld = (1 << 0);
		static const uint SuppressExternalEmittance = (1 << 5);
	}
	static const uint ExtraShaderDescriptor = ExtraFlags::InWorld;
	static const float EffectRadius = 0.0;
}

#define EFFECT
#include "FO4/ShadowSamplingConsumer.hlsli"
#endif
