#ifndef FO4_SSS_CONSUMER
#define FO4_SSS_CONSUMER

#include "FO4/Depth.hlsli"

namespace SubsurfaceScattering
{
	// FO4: set after SSS blurred this frame; otherwise the stock skin blur runs.
	cbuffer Consumer : register(b9)
	{
		bool Produced;
		uint3 pad;
	};
}

// Skips the stock skin blur once SSS has already blurred the composite's t10.
// FO4: first-person pixels (raw depth) are never scattered, so they keep it.
#define FO4_SSS_IF_PRODUCED(rawDepth, statement) [branch] if (SubsurfaceScattering::Produced && !FO4Depth::IsFirstPerson(rawDepth)) statement
#endif
