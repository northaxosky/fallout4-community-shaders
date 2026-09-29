// SPDX-License-Identifier: GPL-3.0-only
// s14 is free in the deferred light program but taken by g_sLitScene in the
// composite, so only the non-composite path of WaterCaustics.hlsli includes it.
namespace WaterEffects
{
	SamplerState WaterCausticsSampler : register(s14);
}
