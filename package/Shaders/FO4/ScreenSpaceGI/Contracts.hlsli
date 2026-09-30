#ifndef FO4_SSGI_CONTRACTS
#define FO4_SSGI_CONTRACTS

#include "Common/GBuffer.hlsli"

namespace ScreenSpaceGI
{
	// FO4: readiness and native radiance selection belong to the consumer, not b7.
	cbuffer Consumer : register(b10)
	{
		bool Enabled;
		bool Tiled;
		uint2 pad;
	};

	float3 DecodeViewNormal(float2 encoded)
	{
		float2 f = encoded * 4.0 - 2.0;
		float f2 = dot(f, f);
		return float3(f * sqrt(max(0, 1.0 - f2 * 0.25)), f2 * 0.5 - 1.0);
	}
}
#endif
