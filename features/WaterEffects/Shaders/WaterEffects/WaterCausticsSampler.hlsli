// SPDX-License-Identifier: GPL-3.0-only
// s14 is free in the deferred light program but taken by g_sLitScene in the
// composite, so the sampler lives outside WaterCaustics.hlsli and only the
// light path includes it.
namespace WaterEffects
{
	SamplerState WaterCausticsSampler : register(s14);
}
