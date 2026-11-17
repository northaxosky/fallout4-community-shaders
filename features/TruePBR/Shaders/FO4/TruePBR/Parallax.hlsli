#ifndef FO4_TRUE_PBR_PARALLAX_HLSLI
#define FO4_TRUE_PBR_PARALLAX_HLSLI

// FO4: t4 is host-bound; the EM kernel is called as upstream.
Texture2D<float4> g_tPbrDisplacement : register(t4);
SamplerState g_sPbrDisplacement : register(s4);

namespace TruePBRParallax
{
	struct Result
	{
		float2 uv;
		float pixelOffset;
		float shadow;
	};

	// Lighting.hlsl:1319-1327 and 2542-2543; a_params carries the warping fix.
	Result March(float2 a_uv, float a_viewZ, float3 a_viewDirection, float3x3 a_tbnTr, float a_noise, float a_displacementScale, DisplacementParams a_params)
	{
		Result result;
		result.pixelOffset = 0.0;
		result.shadow = 1.0;
		a_params.HeightScale *= a_displacementScale;

		float2 dims;
		g_tPbrDisplacement.GetDimensions(dims.x, dims.y);
		// FO4: kernel compiles the coarse rule; this is upstream's TRUE_PBR mip.
		const float mipLevel = max(ExtendedMaterials::GetMipLevelFromDims(a_uv, dims * 2.0) - 1.0, 0.0);

		StochasticOffsets noOffset = (StochasticOffsets)0;
		result.uv = ExtendedMaterials::GetParallaxCoords(a_viewZ, a_uv, mipLevel, a_viewDirection, a_tbnTr, a_noise, g_tPbrDisplacement, g_sPbrDisplacement, 0, a_params, false, noOffset, result.pixelOffset);

		const float shadowQuality = a_viewZ < ExtendedMaterials::ParallaxCheapDistance ? ExtendedMaterials::ParallaxNearShadowQuality : ExtendedMaterials::ParallaxFarShadowQuality;
		[branch] if (SharedData::extendedMaterialSettings.EnableShadows && shadowQuality > 0.0)
		{
			const float sh0 = g_tPbrDisplacement.SampleLevel(g_sPbrDisplacement, result.uv, mipLevel).r;
			const float3 dirLightDirectionTS = mul(a_tbnTr, SharedData::DirLightDirection.xyz);
			result.shadow = ExtendedMaterials::GetParallaxSoftShadowMultiplier(result.uv, mipLevel, dirLightDirectionTS, sh0, g_tPbrDisplacement, g_sPbrDisplacement, 0, shadowQuality, a_noise, a_params, false, noOffset);
		}
		return result;
	}
}
#endif
