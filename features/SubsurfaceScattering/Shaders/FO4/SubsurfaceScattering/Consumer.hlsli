#ifndef FO4_SSS_CONSUMER
#define FO4_SSS_CONSUMER

namespace SubsurfaceScattering
{
	// FO4: set only after SSS blurred this frame; otherwise the stock skin blur runs.
	cbuffer Consumer : register(b9)
	{
		bool Produced;
		uint3 pad;
	};
}

// Skips the stock skin blur once SSS has already blurred the composite's t10.
#define FO4_SSS_IF_PRODUCED(statement) [branch] if (SubsurfaceScattering::Produced) statement
#endif
