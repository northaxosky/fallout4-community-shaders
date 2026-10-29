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

// FO4: skip the stock blur once SSS ran; first-person pixels keep it.
#define FO4_SSS_IF_SCATTERED(rawDepth, statement) [branch] if (SubsurfaceScattering::Produced && !FO4Depth::IsFirstPerson(rawDepth)) statement
#endif
