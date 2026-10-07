#ifndef FO4_SKYLIGHTING_CONSUMER
#define FO4_SKYLIGHTING_CONSUMER

#include "FO4/FO4ShaderData.hlsli"

#ifdef SKYLIGHTING
// FO4: the directional families carrying rim, soft and back sun lobes read probe shadow visibility at t53.
#	if defined(DIRECTIONAL) && (defined(BSDFLIGHT_PS_DIRSPLITS1) || defined(BSDFLIGHT_PS_DIRSPLITS2) || defined(BSDFLIGHT_PS_DIRSPLITS3) || defined(BSDFLIGHT_PS_UNSHADOWED))
#		define SKYLIGHTING_SHADOW_VIS
#	endif
// FO4: pixel and compute lighting read the probe array at the same slot.
#	define SKYLIGHTING_PROBE_REGISTER t50
#	include "Common/Color.hlsli"
#	include "Skylighting/Skylighting.hlsli"
#endif

namespace Skylighting
{
#ifdef SKYLIGHTING
	Texture2D<float4> AlbedoTexture : register(t51);
	// FO4: MRT4 alpha stores 1 - vertexAO, upstream's Masks2.x.
	Texture2D<float4> VertexAOTexture : register(t52);

	// FO4: probe shadow visibility of the last sample; one sample serves ambient and the sun lobes.
	static float ShadowVisibility = 1.0;

	sh2 SampleProbes(float3 positionMS, float3 normalWS)
	{
#	ifdef SKYLIGHTING_SHADOW_VIS
		return Sample(positionMS, normalWS, ShadowVisibility);
#	else
		return Sample(positionMS, normalWS);
#	endif
	}

	// FO4: ambient is a separate linear addend and b6 makes the gamma pair the identity,
	// so upstream's ApplySkylighting subtract and clamp collapse to this multiplier.
	float3 GetAmbientScale(float3 viewPosition, float3 normalView, float3 albedo, float vertexAO)
	{
		float3 positionMS = FrameBuffer::ViewToWorld(viewPosition);
		float3 normalWS = normalize(FrameBuffer::ViewToWorld(normalView, false));
		sh2 skylightingSH = SampleProbes(positionMS, normalWS);
		float skylightingDiffuse = GetSkylightingDiffuse(skylightingSH, positionMS, normalWS, vertexAO);
		return MultiBounceAO(albedo, skylightingDiffuse);
	}

	float3 GetAmbientScale(float2 pixelPosition, float3 viewPosition, float3 normalView)
	{
		int3 texel = int3(int2(pixelPosition), 0);
		return GetAmbientScale(viewPosition, normalView, AlbedoTexture.Load(texel).xyz, 1.0 - VertexAOTexture.Load(texel).w);
	}

	float3 GetViewPosition(float2 pixelPosition)
	{
		float2 uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(pixelPosition * SharedData::BufferDim.zw);
		float4 view = mul(FrameBuffer::CameraProjInverse,
			float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), SharedData::GetDepth(uv), 1.0));
		return view.xyz / view.w;
	}

	// FO4: composite probe sites share the upstream DeferredCompositeCS lobe; positions are view space.
	float GetSpecularVisibility(float3 viewPosition, float3 normalView, float glossiness)
	{
		// FO4: the interior branch samples EnvTexture and never reads visibility.
		if (SharedData::InInterior)
			return 1.0;
		float3 positionMS = FrameBuffer::ViewToWorld(viewPosition);
		float3 normalWS = normalize(FrameBuffer::ViewToWorld(normalView, false));
		float3 V = -normalize(positionMS);
		float3 R = reflect(-V, normalWS);
		sh2 skylightingSH = SampleProbes(positionMS, R);
		return EvaluateSpecular(skylightingSH, SphericalHarmonics::FauxSpecularLobe(normalWS, V, 1.0 - glossiness));
	}

	// FO4: lighting stages evaluate the ambient gradient at several directions per pixel.
	static float3 AmbientScale = 1.0;

#	ifdef SKYLIGHTING_SHADOW_VIS
	// FO4: passes without the ambient term sample the probes only for shadow visibility.
	void SampleShadowVisibility(float3 viewPosition, float3 normalView)
	{
		float3 positionMS = FrameBuffer::ViewToWorld(viewPosition);
		float3 normalWS = normalize(FrameBuffer::ViewToWorld(normalView, false));
		SampleProbes(positionMS, normalWS);
	}

	// FO4: upstream swaps the cascade shadow for the probe visibility on the rim, soft and back lobes.
	// The pass already scaled every lobe by shadow, so this adds the difference for the soft lobes alone.
	float3 SoftSunLobeDelta(float3 sunColor, float rim, float backfaceWrap, float3 albedoPremult, float forwardBlend, float3 albedo, float3 tint, float shadow, float softShadow)
	{
		float3 softDiffuse = sunColor * (rim + backfaceWrap * albedoPremult + forwardBlend * albedo) * tint;
		return softDiffuse * (softShadow - shadow);
	}
#	endif
#endif

#ifdef SKYLIGHTING_FULLSCREEN_DEBUG
	// FO4: composite families share no normal input; a producer isolates visibility.
	bool TryGetDebugColor(float2 pixelPosition, out float4 color)
	{
		color = 0.0;
		if (FO4SharedData::DebugOwner != FullscreenDebugOwner::Skylighting || FO4SharedData::DebugMode == 0)
			return false;
		uint2 dimensions;
		FO4SharedData::DebugTexture.GetDimensions(dimensions.x, dimensions.y);
		if (any(dimensions == 0))
			return false;
		color = FO4SharedData::DebugTexture.Load(int3(min(uint2(pixelPosition), dimensions - 1), 0));
		return true;
	}
#endif
}

// Lighting call sites stay declarative and compile away without the feature.
#ifdef SKYLIGHTING
#	define FO4_AMBIENT_SKYLIGHTING_SET(pixelPosition, viewPosition, normalView) \
		Skylighting::AmbientScale = Skylighting::GetAmbientScale(pixelPosition, viewPosition, normalView)
#	define FO4_AMBIENT_SKYLIGHTING(ambient) ((ambient) * Skylighting::AmbientScale)
#	define FO4_SKYLIGHTING_SPECULAR(viewPosition, normalView, glossiness) Skylighting::GetSpecularVisibility(viewPosition, normalView, glossiness)
#else
#	define FO4_AMBIENT_SKYLIGHTING_SET(pixelPosition, viewPosition, normalView)
#	define FO4_AMBIENT_SKYLIGHTING(ambient) (ambient)
#	define FO4_SKYLIGHTING_SPECULAR(viewPosition, normalView, glossiness) 1.0
#endif

// Sun lobes (rim, soft and back) use probe shadow visibility where upstream's SKYLIGHTING_SHADOW_VIS does.
#ifdef SKYLIGHTING_SHADOW_VIS
#	ifdef WATER_EFFECTS
#		define FO4_SKYLIGHTING_CAUSTICS_TINT causticsMult
#	else
#		define FO4_SKYLIGHTING_CAUSTICS_TINT 1.0
#	endif
// The ambient set already sampled the probes.
#	ifdef AMBIENT
#		define FO4_SOFT_SHADOW_BEGIN(softShadow, viewPosition, normalView) float softShadow = Skylighting::ShadowVisibility
#	else
#		define FO4_SOFT_SHADOW_BEGIN(softShadow, viewPosition, normalView) \
			Skylighting::SampleShadowVisibility(viewPosition, normalView);  \
			float softShadow = Skylighting::ShadowVisibility
#	endif
// Light-wide multipliers (fog, terrain) scale the soft shadow too; the cascade and contact terms do not.
#	define FO4_SHADOW_LIGHT_MUL(shadow, softShadow, factor) \
		{                                                    \
			const float lightMul = (factor);                 \
			shadow *= lightMul;                              \
			softShadow *= lightMul;                          \
		}
#	define FO4_SOFT_SUN_LOBES(output, shadow, softShadow, sunColor, rim, backfaceWrap, albedoPremult, forwardBlend, albedo) \
		output.diffuse.xyz += Skylighting::SoftSunLobeDelta(sunColor, rim, backfaceWrap, albedoPremult, forwardBlend, albedo, FO4_SKYLIGHTING_CAUSTICS_TINT, shadow, softShadow) / 3.0
// Unshadowed sun passes fold light-wide multipliers into the diffuse sum, so soft lobes accumulate apart.
#	define FO4_SOFT_LOBES_DECLARE(softDiffuse) float3 softDiffuse = 0.0
#	define FO4_SOFT_LOBE(directDiffuse, softDiffuse) softDiffuse
#	define FO4_SOFT_LOBE_SCALE(softDiffuse, factor) softDiffuse *= (factor)
#	define FO4_SOFT_LOBES_APPLY(output, softDiffuse, softShadow) output.diffuse.xyz += (softDiffuse) * (softShadow) / 3.0
#else
#	define FO4_SOFT_SHADOW_BEGIN(softShadow, viewPosition, normalView)
#	define FO4_SHADOW_LIGHT_MUL(shadow, softShadow, factor) shadow *= (factor)
#	define FO4_SOFT_SUN_LOBES(output, shadow, softShadow, sunColor, rim, backfaceWrap, albedoPremult, forwardBlend, albedo)
#	define FO4_SOFT_LOBES_DECLARE(softDiffuse)
#	define FO4_SOFT_LOBE(directDiffuse, softDiffuse) directDiffuse
#	define FO4_SOFT_LOBE_SCALE(softDiffuse, factor)
#	define FO4_SOFT_LOBES_APPLY(output, softDiffuse, softShadow)
#endif
#endif
