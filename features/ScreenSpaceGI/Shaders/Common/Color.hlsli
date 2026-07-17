#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__

namespace Color
{
	// Attempt to match vanilla materials that are darker than PBR
	// (upstream Skyrim value; FO4 color-space parity unverified - validate the
	// 1.6 irradiance gamma + this scale against FO4's albedo convention when the
	// GI resolve pass lands, and tune in-game).
	const static float PBRLightingScale = 0.65;

	float3 RGBToYCoCg(float3 color)
	{
		float tmp = 0.25 * (color.r + color.b);
		return float3(
			tmp + 0.5 * color.g,        // Y
			0.5 * (color.r - color.b),  // Co
			-tmp + 0.5 * color.g        // Cg
		);
	}

	float3 YCoCgToRGB(float3 color)
	{
		float tmp = color.x - color.z;
		return float3(
			tmp + color.y,
			color.x + color.z,
			tmp - color.y);
	}

	float IrradianceToLinear(float color)
	{
		return pow(abs(color), 1.6);
	}

	float IrradianceToGamma(float color)
	{
		return pow(abs(color), 1.0 / 1.6);
	}

	float3 IrradianceToLinear(float3 color)
	{
		return pow(abs(color), 1.6);
	}

	float3 IrradianceToGamma(float3 color)
	{
		return pow(abs(color), 1.0 / 1.6);
	}
}

#endif  //__COLOR_DEPENDENCY_HLSL__
