#ifndef DYNAMIC_CUBEMAPS_CUBEMAP_COMMON_HLSLI
#define DYNAMIC_CUBEMAPS_CUBEMAP_COMMON_HLSLI

namespace DynamicCubemaps
{
	// FO4 lights in linear HDR, so upstream's linear-lighting branch applies: irradiance conversions are identity.
	float3 IrradianceToLinear(float3 color)
	{
		return color;
	}

	float3 IrradianceToGamma(float3 color)
	{
		return color;
	}

	static const float ReflectionNormalisationScale = 1.0;

	float RGBToLuminance(float3 color)
	{
		return dot(color, float3(0.2125, 0.7154, 0.0721));
	}

	float3 CubeFaceDirection(uint face, float2 uv)
	{
		float3 direction = 0.0;
		switch (face) {
		case 0:
			direction = float3(1.0, uv.y, -uv.x);
			break;
		case 1:
			direction = float3(-1.0, uv.y, uv.x);
			break;
		case 2:
			direction = float3(uv.x, 1.0, -uv.y);
			break;
		case 3:
			direction = float3(uv.x, -1.0, uv.y);
			break;
		case 4:
			direction = float3(uv.x, uv.y, 1.0);
			break;
		case 5:
			direction = float3(-uv.x, uv.y, -1.0);
			break;
		}
		return normalize(direction);
	}
}

#endif
