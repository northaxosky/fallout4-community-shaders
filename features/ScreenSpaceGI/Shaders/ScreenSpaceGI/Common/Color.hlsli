// SPDX-License-Identifier: MIT
// Copyright (c) 2022 Ilya Perapechka
// Ported from Skyrim Community Shaders.

#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__

// FO4 composites linearly: upstream's linear-lighting branch.
namespace Color
{
	const static float PBRLightingScale = 1.0;

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

	float3 RadianceToLinear(float3 color)
	{
		return color;
	}

	float IrradianceToLinear(float color)
	{
		return color;
	}

	float IrradianceToGamma(float color)
	{
		return color;
	}

	float3 IrradianceToLinear(float3 color)
	{
		return color;
	}

	float3 IrradianceToGamma(float3 color)
	{
		return color;
	}
}

#endif  //__COLOR_DEPENDENCY_HLSL__
