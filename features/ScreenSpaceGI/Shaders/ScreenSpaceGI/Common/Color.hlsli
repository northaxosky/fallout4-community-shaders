// SPDX-License-Identifier: MIT
// Copyright (c) 2022 Ilya Perapechka
// Ported from Skyrim Community Shaders.

#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__

namespace Color
{
	// Skyrim-derived scale; FO4 color-space parity is unverified.
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
