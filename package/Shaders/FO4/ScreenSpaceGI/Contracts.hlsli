#ifndef FO4_SSGI_CONTRACTS
#define FO4_SSGI_CONTRACTS

#include "FO4/GBuffer.hlsli"

namespace ScreenSpaceGI
{
	// FO4: readiness and native radiance selection belong to the consumer, not b7.
	cbuffer Consumer : register(b10)
	{
		bool Enabled;
		bool Tiled;
		uint2 pad;
	};
}
#endif
