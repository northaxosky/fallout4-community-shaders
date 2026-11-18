#ifndef FO4_EXTENDED_MATERIALS_CONSUMER
#define FO4_EXTENDED_MATERIALS_CONSUMER

#include "FO4/FO4ShaderData.hlsli"
#include "FO4/GBufferViews.hlsli"

namespace ExtendedMaterials
{
	// FO4: the prepass resolves upstream's directional parallax shadow per pixel.
	float GetParallaxShadow(float2 pixelPosition)
	{
		[branch] if (!SharedData::extendedMaterialSettings.EnableShadows) return 1.0;
		uint2 dimensions;
		FO4GBufferViews::Aux.GetDimensions(dimensions.x, dimensions.y);
		// An absent SRV leaves the sun untouched.
		if (any(dimensions == 0))
			return 1.0;
		return FO4GBufferViews::Aux.Load(int3(min(uint2(pixelPosition), dimensions - 1), 0)).r;
	}
}
#endif
