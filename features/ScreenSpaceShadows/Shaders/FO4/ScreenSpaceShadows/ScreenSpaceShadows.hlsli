// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) Skyrim Community Shaders contributors
// Ported from Skyrim Community Shaders.

namespace ScreenSpaceShadows
{
	Texture2D<float> ScreenSpaceShadowsTexture : register(t24);

	float GetScreenSpaceShadow(float2 screenPosition)
	{
		return ScreenSpaceShadowsTexture.Load(int3(int2(screenPosition), 0)).x;
	}

	float GetScreenSpaceShadow(float2 screenPosition, float NdotL)
	{
		return NdotL >= 0.0 ? GetScreenSpaceShadow(screenPosition) : 1.0;
	}
}
