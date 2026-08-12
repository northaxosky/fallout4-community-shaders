#ifndef __SHADING_HLSLI__
#define __SHADING_HLSLI__

// [Jimenez et al. 2016, "Practical Realtime Strategies for Accurate Indirect Occlusion"]
float3 MultiBounceAO(float3 baseColor, float ao)
{
	float3 a = 2.0404 * baseColor - 0.3324;
	float3 b = -4.7951 * baseColor + 0.6417;
	float3 c = 2.7552 * baseColor + 0.6903;
	return max(ao, ((ao * a + b) * ao + c) * ao);
}

#endif  // __SHADING_HLSLI__
